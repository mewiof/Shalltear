#pragma once

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/Vector.h"

#include <string_view>

namespace Shalltear {

	enum class TextureShape : u8 {
		Auto = 0,
		Texture2D,
		Cube,
		Texture2DArray,
		Texture3D
	};

	// normal map is linear (SRGB ignored)
	enum class TextureType : u8 {
		Default = 0,
		NormalMap
	};

	enum class TextureCompression : u8 {
		None = 0,
		HighQuality
	};

	struct SpriteBorder {
		i32 left = 0;
		i32 top = 0;
		i32 right = 0;
		i32 bottom = 0;

		bool operator==(const SpriteBorder&) const = default;
	};

	struct TextureSprite {
		str id;
		str name;
		// texels from the image's top-left corner, y down
		RectI rect;
		// texels inside rect, unscaled by 9-slicing
		SpriteBorder border;

		[[nodiscard]] std::string_view Identity() const { return id.empty() ? std::string_view(name) : std::string_view(id); }
		bool operator==(const TextureSprite&) const = default;
	};

	class SHALLTEAR_CORE_API TextureAsset : public Asset {
	public:
		SLT_REFLECT_CLASS(TextureAsset)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_END_REFLECT()

		TextureAsset() = default;
		~TextureAsset() override;

		struct Settings {
			bool sRGB = true;
			bool generateMips = true;
			RHI::FilterMode filter = RHI::FilterMode::Linear;
			RHI::AddressMode addressMode = RHI::AddressMode::Repeat;
			TextureShape shape = TextureShape::Auto;
			i32 cubeFaceSize = 0;
			bool convolveIBL = true;
			u32 columns = 1;
			u32 rows = 1;
			TextureType type = TextureType::Default;
			TextureCompression compression = TextureCompression::HighQuality;

			bool operator==(const Settings&) const = default;
		};
		Settings settings;

		Vector<u8> pixels{GetMemoryResource(MemTag::Asset)};
		Vec2I size{};
		u32 slices = 1;
		RHI::Format format = RHI::Format::Unknown;
		u32 storedMipLevels = 1;
		bool isCube = false;

		RHI::TextureDimension GetDimension() const;

		// convolved with the cosine lobe, divided by PI
		Vector<Vec3> irradianceSH{GetMemoryResource(MemTag::Asset)};

		Vector<TextureSprite> sprites{GetMemoryResource(MemTag::Asset)};

		// valid until next reimport
		[[nodiscard]] const TextureSprite* FindSprite(std::string_view identity) const;

		// replaced on reimport, read per frame
		RHI::TextureHandle texture;
		RHI::SamplerHandle sampler;

		u32 version = 0;

		bool Import(const Path& sourcePath) override;
		StringHash GetAssetTypeId() const override { return TextureAsset::GetReflectType()->name; }

		bool CanAdoptReimport() const override { return true; }
		void AdoptReimport(Asset& fresh) override;

		bool Cook(CookWriter& descriptor, CookWriter& payload) const override;
		bool LoadCooked(CookReader& descriptor, CookReader& payload) override;
	protected:
		void OnLoad() override;
		void OnUnload() override;
	private:
		bool ImportFromSource(const Path& sourcePath);
		friend struct ImportTierBuilder;

		bool ResolveCubeShape();
		bool ResolveFlipbookShape();
		bool ResolveCompression(const Path& sourcePath);
		u64 ExpectedPayloadBytes() const;
	};

	class SHALLTEAR_CORE_API TextureAssetImporter : public IAssetImporter {
	public:
		StringHash GetAssetType() const override { return StringHash("TextureAsset"); }
		Asset* Import(const Path& sourcePath) override;
		Asset* ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue& settings) override;
		Asset* ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue& settings, AssetImportContext& context) override;
		void SerializeSettings(const Path& sourcePath, JSONValue& out) const override;
		// bump when the cooked record changes shape
		u32 GetImporterVersion() const override { return 6; }
	};

} // namespace Shalltear
