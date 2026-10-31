#include "Shalltear/Core/Graphics/TextureAsset.h"

#include "Asset/ImportTierInternal.h"

#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Math/HalfFloat.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Graphics/RHIDevice.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/StringUtil.h"

#include <algorithm>
#include <bit>
#include <cstring>

#include "Graphics/EnvironmentIBL.h"
#include "Shalltear/Core/Graphics/TextureCodecs.h"

namespace Shalltear {

	TextureAsset::~TextureAsset() = default;

	bool TextureAsset::Import(const Path& sourcePath) {
		if (const ImportTierTable* tier = ImportTierInternal::Get()) {
			return tier->importTexture(*this, sourcePath);
		}
		SLT_LOG_ERROR("Graphics", "TextureAsset: can't import '{}' (no ImportTier::Install in this process)",
					  sourcePath.ToString());
		return false;
	}

	RHI::TextureDimension TextureAsset::GetDimension() const {
		if (isCube) {
			return RHI::TextureDimension::Cube;
		}
		switch (settings.shape) {
		case TextureShape::Texture2DArray:
			return RHI::TextureDimension::Texture2DArray;
		case TextureShape::Texture3D:
			return RHI::TextureDimension::Texture3D;
		default:
			return RHI::TextureDimension::Texture2D;
		}
	}

	u64 TextureAsset::ExpectedPayloadBytes() const {
		const u32 layers = isCube ? GraphicsInternal::CUBE_FACE_COUNT : slices;
		const u32 levels = Math::Max(storedMipLevels, 1u);
		u64 total = 0;
		for (u32 mip = 0; mip < levels; ++mip) {
			const Vec2I level(Math::Max(1, size.x >> mip), Math::Max(1, size.y >> mip));
			total += RHI::FormatLevelBytes(format, level) * layers;
		}
		return total;
	}

	void TextureAsset::OnLoad() {
		if (!RenderSystem::IsRenderingAvailable()) {
			return;
		}
		RHI::Device* device = RenderSystem::GetDevice();

		const RHI::TextureDimension dimension = GetDimension();
		const bool volume = dimension == RHI::TextureDimension::Texture3D;
		const bool carried = isCube || RHI::IsBlockCompressed(format);
		u32 mipLevels = carried ? Math::Max(storedMipLevels, 1u) : (settings.generateMips && !volume ? GraphicsInternal::TextureMipCount(size) : 1);
		RHI::TextureDesc desc;
		desc.size = size;
		desc.format = format;
		desc.usage = RHI::TextureUsage::Sampled | RHI::TextureUsage::TransferDst | RHI::TextureUsage::TransferSrc;
		desc.mipLevels = mipLevels;
		desc.debugName = virtualPath.c_str();
		if (isCube) {
			desc.arrayLayers = GraphicsInternal::CUBE_FACE_COUNT;
			desc.cube = true;
		} else if (dimension == RHI::TextureDimension::Texture2DArray) {
			desc.dimension = dimension;
			desc.arrayLayers = slices;
		} else if (volume) {
			desc.dimension = dimension;
			desc.depth = slices;
		}
		texture = device->CreateTextureResource(desc);
		if (!texture.IsValid()) {
			return;
		}
		if (carried) {
			const u32 layers = isCube ? GraphicsInternal::CUBE_FACE_COUNT : slices;
			size_t offset = 0;
			for (u32 mip = 0; mip < mipLevels; ++mip) {
				const Vec2I level(Math::Max(1, size.x >> mip), Math::Max(1, size.y >> mip));
				const size_t levelBytes = static_cast<size_t>(RHI::FormatLevelBytes(format, level));
				for (u32 layer = 0; layer < layers; ++layer) {
					if (offset + levelBytes > pixels.size()) {
						SLT_LOG_ERROR("Graphics", "TextureAsset: '{}' has {} bytes, short of its chain", virtualPath, pixels.size());
						device->DestroyResource(texture);
						texture = {};
						return;
					}
					device->UploadTexture(texture, {pixels.data() + offset, levelBytes}, mip, layer);
					offset += levelBytes;
				}
			}
		} else if (dimension == RHI::TextureDimension::Texture2DArray) {
			const size_t sliceBytes = pixels.size() / slices;
			for (u32 layer = 0; layer < slices; ++layer) {
				device->UploadTexture(texture, {pixels.data() + layer * sliceBytes, sliceBytes}, 0, layer);
			}
			if (mipLevels > 1) {
				device->GenerateMips(texture);
			}
		} else {
			device->UploadTexture(texture, {pixels.data(), pixels.size()});
			if (mipLevels > 1) {
				device->GenerateMips(texture);
			}
		}

		RHI::SamplerDesc samplerDesc;
		samplerDesc.filter = settings.filter;
		samplerDesc.addressMode = settings.addressMode;
		samplerDesc.debugName = virtualPath.c_str();
		sampler = device->CreateSamplerResource(samplerDesc);
	}

	void TextureAsset::AdoptReimport(Asset& fresh) {
		TextureAsset& imported = static_cast<TextureAsset&>(fresh);
		if (imported.settings == settings && imported.size == size && imported.slices == slices &&
			imported.format == format && imported.storedMipLevels == storedMipLevels && imported.isCube == isCube &&
			imported.pixels == pixels && imported.irradianceSH == irradianceSH) {
			return;
		}
		const bool loaded = GetLoadState() == AssetLoadState::Loaded;
		if (loaded) {
			OnUnload();
		}
		settings = imported.settings;
		pixels = std::move(imported.pixels);
		size = imported.size;
		slices = imported.slices;
		format = imported.format;
		storedMipLevels = imported.storedMipLevels;
		isCube = imported.isCube;
		irradianceSH = std::move(imported.irradianceSH);
		++version;
		if (loaded) {
			OnLoad();
		}
	}

	void TextureAsset::OnUnload() {
		if (!RenderSystem::IsRenderingAvailable()) {
			texture = {};
			sampler = {};
			return;
		}
		RHI::Device* device = RenderSystem::GetDevice();
		if (texture.IsValid()) {
			device->DestroyResource(texture);
			texture = {};
		}
		if (sampler.IsValid()) {
			device->DestroyResource(sampler);
			sampler = {};
		}
	}

	bool TextureAsset::Cook(CookWriter& descriptor, CookWriter& payload) const {
		descriptor.Write(static_cast<i32>(size.x));
		descriptor.Write(static_cast<i32>(size.y));
		descriptor.Write(static_cast<u32>(format));
		descriptor.Write(static_cast<u32>(pixels.size()));
		descriptor.Write(static_cast<u8>(settings.sRGB ? 1 : 0));
		descriptor.Write(static_cast<u8>(settings.generateMips ? 1 : 0));
		descriptor.Write(static_cast<u8>(settings.filter));
		descriptor.Write(static_cast<u8>(settings.addressMode));
		descriptor.Write(static_cast<u8>(isCube ? 1 : 0));
		descriptor.Write(static_cast<u8>(settings.convolveIBL ? 1 : 0));
		descriptor.Write(storedMipLevels);
		descriptor.Write(static_cast<u32>(irradianceSH.size()));
		for (const Vec3& coefficient : irradianceSH) {
			descriptor.Write(coefficient.x);
			descriptor.Write(coefficient.y);
			descriptor.Write(coefficient.z);
		}
		descriptor.Write(static_cast<u8>(GetDimension()));
		descriptor.Write(slices);

		payload.WriteBytes(pixels.data(), pixels.size());
		return true;
	}

	bool TextureAsset::LoadCooked(CookReader& descriptor, CookReader& payload) {
		i32 width = 0;
		i32 height = 0;
		u32 readFormat = 0;
		u32 pixelCount = 0;
		u8 sRGB = 0;
		u8 generateMips = 0;
		u8 filter = 0;
		u8 addressMode = 0;
		descriptor.Read(width);
		descriptor.Read(height);
		descriptor.Read(readFormat);
		descriptor.Read(pixelCount);
		descriptor.Read(sRGB);
		descriptor.Read(generateMips);
		descriptor.Read(filter);
		descriptor.Read(addressMode);
		u8 cube = 0;
		u8 convolve = 0;
		u32 storedMips = 1;
		u32 shCount = 0;
		descriptor.Read(cube);
		descriptor.Read(convolve);
		descriptor.Read(storedMips);
		descriptor.Read(shCount);
		irradianceSH.clear();
		if (shCount == 9) {
			irradianceSH.resize(9);
			for (Vec3& coefficient : irradianceSH) {
				descriptor.Read(coefficient.x);
				descriptor.Read(coefficient.y);
				descriptor.Read(coefficient.z);
			}
		} else if (shCount != 0) {
			SLT_LOG_ERROR("Graphics", "TextureAsset: cooked record has {} SH coefficients (should be 0 or 9)", shCount);
			return false;
		}
		u8 dimension = 0;
		u32 readSlices = 1;
		descriptor.Read(dimension);
		descriptor.Read(readSlices);
		payload.ReadInto(pixels, pixelCount);
		if (descriptor.Failed() || payload.Failed()) {
			return false;
		}

		if (readFormat >= static_cast<u32>(RHI::Format::Count) || width < 0 || height < 0) {
			SLT_LOG_ERROR("Graphics", "TextureAsset: cooked record has invalid format {} at {}x{}", readFormat, width, height);
			return false;
		}
		size = Vec2I(width, height);
		format = static_cast<RHI::Format>(readFormat);
		settings.sRGB = sRGB != 0;
		settings.generateMips = generateMips != 0;
		settings.filter = static_cast<RHI::FilterMode>(filter);
		settings.addressMode = static_cast<RHI::AddressMode>(addressMode);
		settings.convolveIBL = convolve != 0;
		settings.shape = cube != 0 ? TextureShape::Cube : TextureShape::Texture2D;
		if (dimension == static_cast<u8>(RHI::TextureDimension::Texture2DArray)) {
			settings.shape = TextureShape::Texture2DArray;
		} else if (dimension == static_cast<u8>(RHI::TextureDimension::Texture3D)) {
			settings.shape = TextureShape::Texture3D;
		}
		isCube = cube != 0;
		const bool flipbook = settings.shape == TextureShape::Texture2DArray || settings.shape == TextureShape::Texture3D;
		if (readSlices == 0 || (!flipbook && readSlices != 1) || pixels.size() % readSlices != 0) {
			SLT_LOG_ERROR("Graphics", "TextureAsset: cooked record says {} slice(s) for a {}-byte payload", readSlices, pixels.size());
			return false;
		}
		slices = readSlices;
		storedMipLevels = Math::Max(storedMips, 1u);
		settings.compression = RHI::IsBlockCompressed(format) ? TextureCompression::HighQuality : TextureCompression::None;
		settings.type = format == RHI::Format::BC5Unorm ? TextureType::NormalMap : TextureType::Default;
		if (pixels.size() != ExpectedPayloadBytes()) {
			SLT_LOG_ERROR("Graphics", "TextureAsset: cooked record holds {} bytes where {}x{}, {} slices and {} levels need {}", pixels.size(), size.x,
						  size.y, slices, storedMipLevels, ExpectedPayloadBytes());
			return false;
		}
		return true;
	}

	Asset* TextureAssetImporter::Import(const Path& sourcePath) {
		TextureAsset* asset = New<TextureAsset>(MemTag::Asset);
		if (!asset->Import(sourcePath)) {
			Delete(asset);
			return nullptr;
		}
		return asset;
	}

	Asset* TextureAssetImporter::ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue& settings) {
		(void)virtualPath;
		TextureAsset* asset = New<TextureAsset>(MemTag::Asset);

		if (settings.IsObject()) {
			asset->settings.sRGB = settings["sRGB"].AsBool(asset->settings.sRGB);
			asset->settings.generateMips = settings["generateMips"].AsBool(asset->settings.generateMips);
			std::string_view filter = settings["filter"].AsString("linear");
			asset->settings.filter = filter == "nearest" ? RHI::FilterMode::Nearest : RHI::FilterMode::Linear;
			std::string_view address = settings["addressMode"].AsString("repeat");
			if (address == "clamp") {
				asset->settings.addressMode = RHI::AddressMode::ClampToEdge;
			} else if (address == "mirror") {
				asset->settings.addressMode = RHI::AddressMode::MirroredRepeat;
			} else {
				asset->settings.addressMode = RHI::AddressMode::Repeat;
			}
			std::string_view shape = settings["shape"].AsString("auto");
			if (shape == "cube") {
				asset->settings.shape = TextureShape::Cube;
			} else if (shape == "2d") {
				asset->settings.shape = TextureShape::Texture2D;
			} else if (shape == "2darray") {
				asset->settings.shape = TextureShape::Texture2DArray;
			} else if (shape == "3d") {
				asset->settings.shape = TextureShape::Texture3D;
			} else {
				asset->settings.shape = TextureShape::Auto;
			}
			asset->settings.columns = static_cast<u32>(Math::Max(settings["columns"].AsNumber(1.0), 0.0));
			asset->settings.rows = static_cast<u32>(Math::Max(settings["rows"].AsNumber(1.0), 0.0));
			asset->settings.cubeFaceSize = static_cast<i32>(settings["cubeFaceSize"].AsNumber(0.0));
			asset->settings.convolveIBL = settings["convolveIBL"].AsBool(asset->settings.convolveIBL);
			asset->settings.type = settings["textureType"].AsString("default") == "normalMap" ? TextureType::NormalMap : TextureType::Default;
			asset->settings.compression = settings["compression"].AsString("highQuality") == "none" ? TextureCompression::None
																									: TextureCompression::HighQuality;
		}

		if (!asset->Import(sourcePath)) {
			Delete(asset);
			return nullptr;
		}
		return asset;
	}

	void TextureAssetImporter::SerializeSettings(const Path& sourcePath, JSONValue& out) const {
		(void)sourcePath;
		out.AddMember("sRGB", JSONValue::MakeBool(true));
		out.AddMember("generateMips", JSONValue::MakeBool(true));
		out.AddMember("filter", JSONValue::MakeString("linear"));
		out.AddMember("addressMode", JSONValue::MakeString("repeat"));
		out.AddMember("shape", JSONValue::MakeString("auto"));
		out.AddMember("cubeFaceSize", JSONValue::MakeNumber(0.0));
		out.AddMember("convolveIBL", JSONValue::MakeBool(true));
		out.AddMember("columns", JSONValue::MakeNumber(1.0));
		out.AddMember("rows", JSONValue::MakeNumber(1.0));
		out.AddMember("textureType", JSONValue::MakeString("default"));
		out.AddMember("compression", JSONValue::MakeString("highQuality"));
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::TextureAsset)
