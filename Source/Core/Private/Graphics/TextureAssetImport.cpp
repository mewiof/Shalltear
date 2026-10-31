#include "Shalltear/Core/Graphics/TextureAsset.h"

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math/HalfFloat.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Memory.h"

#include <algorithm>
#include <bit>
#include <cstring>

#define STBI_NO_STDIO
#include "Graphics/stb_image.h"

#include "Graphics/EnvironmentIBL.h"
#include "Shalltear/Core/Graphics/TextureCodecs.h"

namespace Shalltear {

	namespace {

		using Math::FloatToHalf;

		constexpr i32 MAX_DERIVED_CUBE_FACE_SIZE = 256;
		constexpr i32 MIN_CUBE_FACE_SIZE = 16;
		constexpr i32 MAX_CUBE_FACE_SIZE = 2048;

		i32 PowerOfTwoFloor(i32 value) {
			i32 result = 1;
			while (result * 2 <= value) {
				result *= 2;
			}
			return result;
		}

		i32 DeriveCubeFaceSize(Vec2I sourceSize) {
			const i32 derived = PowerOfTwoFloor(Math::Max(sourceSize.x / 4, 1));
			return Math::Clamp(derived, MIN_CUBE_FACE_SIZE, MAX_DERIVED_CUBE_FACE_SIZE);
		}

	} // anonymous namespace

	bool TextureAsset::ImportFromSource(const Path& sourcePath) {
		Buffer file = FileSystem::ReadFile(sourcePath, MemTag::Asset);
		if (!file || file.size == 0) {
			SLT_LOG_ERROR("Graphics", "TextureAsset: '{}' unreadable", sourcePath.ToString());
			return false;
		}

		int width = 0;
		int height = 0;
		int channels = 0;

		if (stbi_is_hdr_from_memory(file.data, static_cast<int>(file.size))) {
			f32* decoded = stbi_loadf_from_memory(file.data, static_cast<int>(file.size), &width, &height, &channels, 4);
			if (!decoded) {
				SLT_LOG_ERROR("Graphics", "TextureAsset: '{}' failed to decode: {}", sourcePath.ToString(), stbi_failure_reason());
				return false;
			}
			size.x = width;
			size.y = height;
			format = RHI::Format::RGBA16Float;

			size_t texelCount = static_cast<size_t>(width) * height * 4;
			pixels.resize(texelCount * sizeof(u16));
			u16* halves = reinterpret_cast<u16*>(pixels.data());
			for (size_t i = 0; i < texelCount; ++i) {
				halves[i] = FloatToHalf(decoded[i]);
			}
			stbi_image_free(decoded);
		} else {
			u8* decoded = stbi_load_from_memory(file.data, static_cast<int>(file.size), &width, &height, &channels, 4);
			if (!decoded) {
				SLT_LOG_ERROR("Graphics", "TextureAsset: '{}' failed to decode: {}", sourcePath.ToString(), stbi_failure_reason());
				return false;
			}
			size.x = width;
			size.y = height;
			const bool color = settings.sRGB && settings.type != TextureType::NormalMap;
			format = color ? RHI::Format::RGBA8SRGB : RHI::Format::RGBA8Unorm;

			pixels.assign(decoded, decoded + static_cast<size_t>(width) * height * 4);
			stbi_image_free(decoded);
		}

		return ResolveCubeShape() && ResolveFlipbookShape() && ResolveCompression(sourcePath);
	}

	bool TextureAsset::ResolveFlipbookShape() {
		slices = 1;
		if (settings.shape != TextureShape::Texture2DArray && settings.shape != TextureShape::Texture3D) {
			return true;
		}
		const u32 columns = settings.columns;
		const u32 rows = settings.rows;
		if (columns == 0 || rows == 0 || static_cast<u32>(size.x) % columns != 0 || static_cast<u32>(size.y) % rows != 0) {
			SLT_LOG_ERROR("Graphics", "TextureAsset: '{}' is {}x{} (doesn't cut into {} column(s) and {} row(s) of whole cells)", virtualPath,
						  size.x, size.y, columns, rows);
			return false;
		}
		const size_t texel = format == RHI::Format::RGBA16Float ? 8 : 4;
		const u32 cellWidth = static_cast<u32>(size.x) / columns;
		const u32 cellHeight = static_cast<u32>(size.y) / rows;
		const size_t cellRowBytes = cellWidth * texel;
		const size_t imageRowBytes = static_cast<size_t>(size.x) * texel;
		Vector<u8> cut{GetMemoryResource(MemTag::Asset)};
		cut.resize(pixels.size());
		u8* out = cut.data();
		for (u32 row = 0; row < rows; ++row) {
			for (u32 column = 0; column < columns; ++column) {
				for (u32 y = 0; y < cellHeight; ++y) {
					const u8* source = pixels.data() + (static_cast<size_t>(row) * cellHeight + y) * imageRowBytes + column * cellRowBytes;
					std::memcpy(out, source, cellRowBytes);
					out += cellRowBytes;
				}
			}
		}
		pixels = std::move(cut);
		size = Vec2I(static_cast<i32>(cellWidth), static_cast<i32>(cellHeight));
		slices = columns * rows;
		return true;
	}

	bool TextureAsset::ResolveCubeShape() {
		isCube = false;
		storedMipLevels = 1;
		irradianceSH.clear();

		const bool linearSource = format == RHI::Format::RGBA16Float;
		const bool equirectAspect = size.x == size.y * 2;
		const u32 stripFace = GraphicsInternal::StripFaceSize(size);
		bool wantCube = settings.shape == TextureShape::Cube;
		if (settings.shape == TextureShape::Auto) {
			wantCube = linearSource && (equirectAspect || stripFace > 0);
		}
		if (!wantCube) {
			return true;
		}
		if (!equirectAspect && stripFace == 0) {
			SLT_LOG_WARN("Graphics", "TextureAsset: '{}' is {}x{}, neither a 2:1 equirectangular image nor a 6-frame strip (importing as a cube anyway)",
						 virtualPath, size.x, size.y);
		}

		i32 requested = settings.cubeFaceSize;
		if (requested <= 0) {
			requested = stripFace > 0 ? static_cast<i32>(stripFace) : DeriveCubeFaceSize(size);
		}
		const u32 faceSize = static_cast<u32>(Math::Clamp(requested, MIN_CUBE_FACE_SIZE, MAX_CUBE_FACE_SIZE));

		GraphicsInternal::RadianceCube cube;
		const std::span<const u8> source{pixels.data(), pixels.size()};
		const bool built = stripFace > 0 ? GraphicsInternal::BuildCubeFromStrip(source, size, format, faceSize, cube)
										 : GraphicsInternal::BuildCubeFromEquirect(source, size, format, faceSize, cube);
		if (!built) {
			SLT_LOG_ERROR("Graphics", "TextureAsset: '{}' couldn't be resampled to a cube", virtualPath);
			return false;
		}
		if (settings.convolveIBL) {
			GraphicsInternal::IrradianceSH sh;
			GraphicsInternal::PrefilterRadianceCube(cube, sh);
			irradianceSH.assign(sh.coefficients, sh.coefficients + 9);
		} else {
			cube.pixels.resize(static_cast<size_t>(GraphicsInternal::RadianceCubeBytes(faceSize, 1)));
		}

		pixels = std::move(cube.pixels);
		size = Vec2I(static_cast<i32>(faceSize), static_cast<i32>(faceSize));
		format = RHI::Format::RGBA16Float;
		storedMipLevels = settings.convolveIBL ? cube.mipCount : 1;
		isCube = true;
		settings.addressMode = RHI::AddressMode::ClampToEdge;
		return true;
	}

	bool TextureAsset::ResolveCompression(const Path& sourcePath) {
		if (settings.compression == TextureCompression::None) {
			return true;
		}
		const bool hdr = format == RHI::Format::RGBA16Float;
		const bool ldr = format == RHI::Format::RGBA8SRGB || format == RHI::Format::RGBA8Unorm;
		if ((!hdr && !ldr) || GetDimension() == RHI::TextureDimension::Texture3D) {
			return true;
		}
		if (size.x % 4 != 0 || size.y % 4 != 0) {
			SLT_LOG_WARN("Graphics", "TextureAsset: '{}' is {}x{} (not a whole number of 4x4 blocks). Imported uncompressed",
						 sourcePath.ToString(), size.x, size.y);
			return true;
		}

		const bool normal = !hdr && settings.type == TextureType::NormalMap;
		const bool srgb = format == RHI::Format::RGBA8SRGB;
		const RHI::Format target =
			hdr ? RHI::Format::BC6HUFloat : (normal ? RHI::Format::BC5Unorm : (srgb ? RHI::Format::BC7SRGB : RHI::Format::BC7Unorm));
		const TextureCodec* codec = TextureCodecs::Find(target);
		if (!codec) {
			SLT_LOG_ERROR("Graphics", "TextureAsset: '{}' compresses to {} and no encoders for it registered", sourcePath.ToString(),
						  hdr ? "BC6H" : (normal ? "BC5" : "BC7"));
			return false;
		}

		TextureEncodeInput input;
		input.texels = {pixels.data(), pixels.size()};
		input.texelFormat = hdr ? RHI::Format::RGBA16Float : RHI::Format::RGBA8Unorm;
		input.size = size;
		if (isCube) {
			input.slices = GraphicsInternal::CUBE_FACE_COUNT;
			input.mipLevels = Math::Max(storedMipLevels, 1u);
			input.suppliedLevels = input.mipLevels;
		} else {
			input.slices = slices;
			input.mipLevels = settings.generateMips ? GraphicsInternal::TextureMipCount(size) : 1;
		}
		input.meaning = normal ? TexelMeaning::Normal : (srgb ? TexelMeaning::SRGBColor : TexelMeaning::Linear);
		Vector<u8> blocks{GetMemoryResource(MemTag::Asset)};
		if (!codec->encode(target, input, blocks)) {
			SLT_LOG_ERROR("Graphics", "TextureAsset: '{}' couldn't be encoded", sourcePath.ToString());
			return false;
		}
		pixels = std::move(blocks);
		format = target;
		storedMipLevels = input.mipLevels;
		return true;
	}

} // namespace Shalltear
