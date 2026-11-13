#pragma once

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"

namespace Shalltear {

	enum class SyntheticBoldSpacing : u8 {
		Widened,
		Regular,
	};

	class SHALLTEAR_CORE_API FontAsset : public Asset {
	public:
		SLT_REFLECT_CLASS(FontAsset)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_END_REFLECT()

		FontAsset() = default;
		~FontAsset() override;

		// font faces point into these bytes
		Vector<u8> data{GetMemoryResource(MemTag::Asset)};

		// bold/italic/other weight files, matched by style stored in each file
		Vector<Guid> faces{GetMemoryResource(MemTag::Asset)};

		// searched in order for a missing glyph, depth first
		Vector<Guid> fallbacks{GetMemoryResource(MemTag::Asset)};

		SyntheticBoldSpacing syntheticBoldSpacing = SyntheticBoldSpacing::Widened;

		u32 version = 0;

		// loaded on 1st call + held while font is loaded
		FontAsset* GetFace(size_t index) const;
		FontAsset* GetFallback(size_t index) const;

		bool Import(const Path& sourcePath) override;
		StringHash GetAssetTypeId() const override { return FontAsset::GetReflectType()->name; }

		bool CanAdoptReimport() const override { return true; }
		void AdoptReimport(Asset& fresh) override;

		bool Cook(CookWriter& descriptor, CookWriter& payload) const override;
		bool LoadCooked(CookReader& descriptor, CookReader& payload) override;
	protected:
		void OnUnload() override;
	private:
		struct Held {
			Handle<FontAsset> font;
			bool holds = false;
			bool refused = false;
		};
		mutable Vector<Held> _heldFaces{GetMemoryResource(MemTag::Asset)};
		mutable Vector<Held> _held{GetMemoryResource(MemTag::Asset)};

		FontAsset* Hold(const Vector<Guid>& list, Vector<Held>& held, size_t index, std::string_view role) const;
	};

	class SHALLTEAR_CORE_API FontAssetImporter : public IAssetImporter {
	public:
		StringHash GetAssetType() const override { return StringHash("FontAsset"); }
		Asset* Import(const Path& sourcePath) override;
		Asset* ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue& settings) override;
		void SerializeSettings(const Path& sourcePath, JSONValue& out) const override;
		// bump when cooked record changes shape
		u32 GetImporterVersion() const override { return 4; }
	};

} // namespace Shalltear
