#include "Shalltear/Core/Graphics/TextureAsset.h"

#include "Asset/ImportTierInternal.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Math/HalfFloat.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Graphics/RHIDevice.h"
#include "Shalltear/Core/Graphics/SpriteAsset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/StringUtil.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>
#include <format>

#include "Graphics/DeviceContent.h"
#include "Graphics/EnvironmentIBL.h"
#include "Shalltear/Core/Graphics/TextureCodecs.h"

namespace Shalltear {

	TextureAsset::~TextureAsset() = default;

	const TextureSprite* TextureAsset::FindSprite(std::string_view identity) const {
		for (const TextureSprite& sprite : sprites) {
			if (sprite.Identity() == identity) {
				return &sprite;
			}
		}
		return nullptr;
	}

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

	namespace {

		bool CarriesMipChain(const TextureAsset& asset) {
			return asset.isCube || RHI::IsBlockCompressed(asset.format);
		}

		u32 DeviceMipLevels(const TextureAsset& asset) {
			if (CarriesMipChain(asset)) {
				return Math::Max(asset.storedMipLevels, 1u);
			}
			const bool volume = asset.GetDimension() == RHI::TextureDimension::Texture3D;
			return asset.settings.generateMips && !volume ? GraphicsInternal::TextureMipCount(asset.size) : 1;
		}

		bool UploadPixels(const TextureAsset& asset, RHI::Device& device) {
			const Vector<u8>& pixels = asset.pixels;
			const u32 mipLevels = DeviceMipLevels(asset);
			if (CarriesMipChain(asset)) {
				const u32 layers = asset.isCube ? GraphicsInternal::CUBE_FACE_COUNT : asset.slices;
				size_t offset = 0;
				for (u32 mip = 0; mip < mipLevels; ++mip) {
					const Vec2I level(Math::Max(1, asset.size.x >> mip), Math::Max(1, asset.size.y >> mip));
					const size_t levelBytes = static_cast<size_t>(RHI::FormatLevelBytes(asset.format, level));
					for (u32 layer = 0; layer < layers; ++layer) {
						if (offset + levelBytes > pixels.size()) {
							SLT_LOG_ERROR("Graphics", "TextureAsset: '{}' has {} bytes, short of its chain", asset.virtualPath, pixels.size());
							return false;
						}
						device.UploadTexture(asset.texture, {pixels.data() + offset, levelBytes}, mip, layer);
						offset += levelBytes;
					}
				}
			} else if (asset.GetDimension() == RHI::TextureDimension::Texture2DArray) {
				const size_t sliceBytes = pixels.size() / asset.slices;
				for (u32 layer = 0; layer < asset.slices; ++layer) {
					device.UploadTexture(asset.texture, {pixels.data() + layer * sliceBytes, sliceBytes}, 0, layer);
				}
				if (mipLevels > 1) {
					device.GenerateMips(asset.texture);
				}
			} else {
				device.UploadTexture(asset.texture, {pixels.data(), pixels.size()});
				if (mipLevels > 1) {
					device.GenerateMips(asset.texture);
				}
			}
			return true;
		}

	} // anonymous namespace

	bool GraphicsInternal::RestoreDeviceContent(TextureAsset& texture) {
		return texture.texture.IsValid() && RenderSystem::IsRenderingAvailable() && UploadPixels(texture, *RenderSystem::GetDevice());
	}

	void TextureAsset::OnLoad() {
		if (!RenderSystem::IsRenderingAvailable()) {
			return;
		}
		RHI::Device* device = RenderSystem::GetDevice();

		const RHI::TextureDimension dimension = GetDimension();
		const bool volume = dimension == RHI::TextureDimension::Texture3D;
		u32 mipLevels = DeviceMipLevels(*this);
		RHI::TextureDesc desc;
		desc.size = size;
		desc.format = RenderSystem::GetColorSpace() == ColorSpace::Gamma ? RHI::WithoutSRGB(format) : format;
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
		if (texture.IsValid() && !UploadPixels(*this, *device)) {
			device->DestroyResource(texture);
			texture = {};
		}
	}

	void TextureAsset::AdoptReimport(Asset& fresh) {
		TextureAsset& imported = static_cast<TextureAsset&>(fresh);
		if (imported.settings == settings && imported.size == size && imported.slices == slices &&
			imported.format == format && imported.storedMipLevels == storedMipLevels && imported.isCube == isCube &&
			imported.pixels == pixels && imported.irradianceSH == irradianceSH && imported.sprites == sprites) {
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
		sprites = std::move(imported.sprites);
		++version;
		if (loaded) {
			OnLoad();
		}
	}

	void TextureAsset::OnUnload() {
		if (!RenderSystem::IsRenderingAvailable()) {
			texture = {};
			return;
		}
		if (texture.IsValid()) {
			RenderSystem::GetDevice()->DestroyResource(texture);
			texture = {};
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
		descriptor.Write(settings.anisoLevel);
		descriptor.Write(settings.pixelsPerUnit);
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
		descriptor.Write(static_cast<u32>(sprites.size()));
		for (const TextureSprite& sprite : sprites) {
			descriptor.WriteString(sprite.id);
			descriptor.WriteString(sprite.name);
			descriptor.Write(sprite.rect);
			descriptor.Write(sprite.border);
		}

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
		u8 anisoLevel = 0;
		f32 pixelsPerUnit = 0.0f;
		descriptor.Read(width);
		descriptor.Read(height);
		descriptor.Read(readFormat);
		descriptor.Read(pixelCount);
		descriptor.Read(sRGB);
		descriptor.Read(generateMips);
		descriptor.Read(filter);
		descriptor.Read(addressMode);
		descriptor.Read(anisoLevel);
		descriptor.Read(pixelsPerUnit);
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
		u32 spriteCount = 0;
		descriptor.Read(spriteCount);
		sprites.clear();
		for (u32 i = 0; i < spriteCount && !descriptor.Failed(); ++i) {
			TextureSprite sprite;
			sprite.id = str(descriptor.ReadString());
			sprite.name = str(descriptor.ReadString());
			descriptor.Read(sprite.rect);
			descriptor.Read(sprite.border);
			sprites.push_back(std::move(sprite));
		}
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
		settings.filter = static_cast<TextureFilter>(filter);
		settings.addressMode = static_cast<RHI::AddressMode>(addressMode);
		settings.anisoLevel = anisoLevel;
		settings.pixelsPerUnit = pixelsPerUnit;
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

	namespace {

		bool ReadInts(const JSONValue& array, i32* out, size_t count) {
			if (!array.IsArray() || array.Size() != count) {
				return false;
			}
			for (size_t i = 0; i < count; ++i) {
				if (!array[i].IsNumber()) {
					return false;
				}
				out[i] = static_cast<i32>(array[i].AsInt());
			}
			return true;
		}

		str RefuseSprite(const TextureSprite& sprite, std::string_view why) {
			return std::format("sprite '{}' {}", sprite.name.empty() ? sprite.id : sprite.name, why);
		}

		bool ReadSprite(const JSONValue& entry, Vec2I imageSize, TextureSprite& out, str& error) {
			out.id = str(entry["id"].AsString());
			out.name = str(entry["name"].AsString());
			if (out.name.empty()) {
				out.name = out.id;
			}
			if (out.name.empty()) {
				error = "a sprite has neither id nor name";
				return false;
			}
			i32 rect[4] = {};
			if (!ReadInts(entry["rect"], rect, 4)) {
				error = RefuseSprite(out, "needs rect [x, y, width, height]");
				return false;
			}
			out.rect = RectI(rect[0], rect[1], rect[2], rect[3]);
			if (out.rect.size.x <= 0 || out.rect.size.y <= 0 || out.rect.Left() < 0 || out.rect.Top() < 0 ||
				out.rect.Right() > imageSize.x || out.rect.Bottom() > imageSize.y) {
				error = RefuseSprite(out, std::format("has rect {},{} {}x{} (outside the {}x{} image)", rect[0], rect[1], rect[2],
													  rect[3], imageSize.x, imageSize.y));
				return false;
			}
			if (entry.HasKey("border")) {
				i32 border[4] = {};
				if (!ReadInts(entry["border"], border, 4)) {
					error = RefuseSprite(out, "needs border [left, top, right, bottom]");
					return false;
				}
				out.border = SpriteBorder{border[0], border[1], border[2], border[3]};
				const SpriteBorder& b = out.border;
				if (b.left < 0 || b.top < 0 || b.right < 0 || b.bottom < 0 || b.left + b.right > out.rect.size.x ||
					b.top + b.bottom > out.rect.size.y) {
					error = RefuseSprite(out, "has a border larger than its rect");
					return false;
				}
			}
			return true;
		}

		void ReadSprites(const JSONValue& list, TextureAsset& texture, const Path& virtualPath) {
			texture.sprites.clear();
			if (!list.IsArray() || list.Size() == 0) {
				return;
			}
			if (texture.GetDimension() != RHI::TextureDimension::Texture2D) {
				SLT_LOG_WARN("Graphics", "TextureAsset: '{}' isn't a 2D texture. Ignoring {} sprite(s)", virtualPath.ToString(), list.Size());
				return;
			}
			for (size_t i = 0; i < list.Size(); ++i) {
				TextureSprite sprite;
				str error;
				if (!ReadSprite(list[i], texture.size, sprite, error)) {
					SLT_LOG_WARN("Graphics", "TextureAsset: '{}' {}. Skipping", virtualPath.ToString(), error);
					continue;
				}
				if (texture.FindSprite(sprite.Identity())) {
					SLT_LOG_WARN("Graphics", "TextureAsset: '{}' defines the sprite '{}' twice (2nd ignored)", virtualPath.ToString(),
								 sprite.Identity());
					continue;
				}
				texture.sprites.push_back(std::move(sprite));
			}
		}

	} // anonymous namespace

	Asset* TextureAssetImporter::Import(const Path& sourcePath) {
		TextureAsset* asset = New<TextureAsset>(MemTag::Asset);
		if (!asset->Import(sourcePath)) {
			Delete(asset);
			return nullptr;
		}
		return asset;
	}

	Asset* TextureAssetImporter::ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue& settings) {
		TextureAsset* asset = New<TextureAsset>(MemTag::Asset);
		ReadSettings(settings, asset->settings);

		if (!asset->Import(sourcePath)) {
			Delete(asset);
			return nullptr;
		}
		if (settings.IsObject()) {
			ReadSprites(settings["sprites"], *asset, virtualPath);
		}
		return asset;
	}

	Asset* TextureAssetImporter::ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue& settings,
													AssetImportContext& context) {
		Asset* imported = ImportWithSettings(sourcePath, virtualPath, settings);
		auto* texture = static_cast<TextureAsset*>(imported);
		if (!texture || texture->sprites.empty()) {
			return texture;
		}
		const Guid textureGuid = AssetDatabase::LookupGuid(virtualPath);
		for (const TextureSprite& sprite : texture->sprites) {
			context.AddProduct(std::format("Sprites/{}", sprite.Identity()), New<SpriteAsset>(MemTag::Asset, textureGuid, sprite.Identity()),
							   sprite.name);
		}
		return texture;
	}

	void TextureAssetImporter::SerializeSettings(const Path& sourcePath, JSONValue& out) const {
		(void)sourcePath;
		WriteSettings(TextureAsset::Settings{}, out);
	}

	namespace {

		template <typename E>
		struct SettingName {
			E value;
			std::string_view name;
		};

		constexpr SettingName<TextureFilter> FilterNames[] = {{TextureFilter::Point, "point"},
															  {TextureFilter::Bilinear, "bilinear"},
															  {TextureFilter::Trilinear, "trilinear"},
															  {TextureFilter::Point, "nearest"},
															  {TextureFilter::Trilinear, "linear"}};
		constexpr SettingName<RHI::AddressMode> AddressNames[] = {{RHI::AddressMode::Repeat, "repeat"},
																  {RHI::AddressMode::ClampToEdge, "clamp"},
																  {RHI::AddressMode::MirroredRepeat, "mirror"}};
		constexpr SettingName<TextureShape> ShapeNames[] = {{TextureShape::Auto, "auto"},
															{TextureShape::Texture2D, "2d"},
															{TextureShape::Cube, "cube"},
															{TextureShape::Texture2DArray, "2darray"},
															{TextureShape::Texture3D, "3d"}};
		constexpr SettingName<TextureType> TypeNames[] = {{TextureType::Default, "default"}, {TextureType::NormalMap, "normalMap"}};
		constexpr SettingName<TextureCompression> CompressionNames[] = {{TextureCompression::None, "none"},
																		{TextureCompression::HighQuality, "highQuality"}};

		template <typename E, size_t N>
		void ReadName(const JSONValue& value, const SettingName<E> (&names)[N], E& out) {
			for (const SettingName<E>& entry : names) {
				if (value.IsString() && entry.name == value.AsString()) {
					out = entry.value;
					return;
				}
			}
		}

		template <typename E, size_t N>
		JSONValue WriteName(const SettingName<E> (&names)[N], E value) {
			for (const SettingName<E>& entry : names) {
				if (entry.value == value) {
					return JSONValue::MakeString(entry.name);
				}
			}
			return JSONValue::MakeString(names[0].name);
		}

	} // anonymous namespace

	void TextureAssetImporter::ReadSettings(const JSONValue& settings, TextureAsset::Settings& out) {
		if (!settings.IsObject()) {
			return;
		}
		out.sRGB = settings["sRGB"].AsBool(out.sRGB);
		out.generateMips = settings["generateMips"].AsBool(out.generateMips);
		ReadName(settings["filter"], FilterNames, out.filter);
		ReadName(settings["addressMode"], AddressNames, out.addressMode);
		out.anisoLevel = static_cast<u8>(Math::Clamp(settings["anisoLevel"].AsNumber(out.anisoLevel), 0.0, 16.0));
		ReadName(settings["shape"], ShapeNames, out.shape);
		out.columns = static_cast<u32>(Math::Max(settings["columns"].AsNumber(out.columns), 0.0));
		out.rows = static_cast<u32>(Math::Max(settings["rows"].AsNumber(out.rows), 0.0));
		out.cubeFaceSize = static_cast<i32>(settings["cubeFaceSize"].AsNumber(out.cubeFaceSize));
		out.convolveIBL = settings["convolveIBL"].AsBool(out.convolveIBL);
		ReadName(settings["textureType"], TypeNames, out.type);
		ReadName(settings["compression"], CompressionNames, out.compression);
		const f64 pixelsPerUnit = settings["pixelsPerUnit"].AsNumber(out.pixelsPerUnit);
		if (std::isfinite(pixelsPerUnit) && pixelsPerUnit > 0.0) {
			out.pixelsPerUnit = static_cast<f32>(Math::Max(pixelsPerUnit, 0.01));
		}
	}

	void TextureAssetImporter::WriteSettings(const TextureAsset::Settings& settings, JSONValue& out) {
		out.AddMember("sRGB", JSONValue::MakeBool(settings.sRGB));
		out.AddMember("generateMips", JSONValue::MakeBool(settings.generateMips));
		out.AddMember("filter", WriteName(FilterNames, settings.filter));
		out.AddMember("addressMode", WriteName(AddressNames, settings.addressMode));
		out.AddMember("anisoLevel", JSONValue::MakeNumber(settings.anisoLevel));
		out.AddMember("shape", WriteName(ShapeNames, settings.shape));
		out.AddMember("cubeFaceSize", JSONValue::MakeNumber(settings.cubeFaceSize));
		out.AddMember("convolveIBL", JSONValue::MakeBool(settings.convolveIBL));
		out.AddMember("columns", JSONValue::MakeNumber(settings.columns));
		out.AddMember("rows", JSONValue::MakeNumber(settings.rows));
		out.AddMember("textureType", WriteName(TypeNames, settings.type));
		out.AddMember("compression", WriteName(CompressionNames, settings.compression));
		out.AddMember("pixelsPerUnit", JSONValue::MakeNumber(settings.pixelsPerUnit));
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::TextureAsset)
