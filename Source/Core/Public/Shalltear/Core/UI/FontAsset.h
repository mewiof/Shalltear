#pragma once

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"

namespace Shalltear {

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

		bool Import(const Path& sourcePath) override;
		StringHash GetAssetTypeId() const override { return FontAsset::GetReflectType()->name; }

		bool Cook(CookWriter& descriptor, CookWriter& payload) const override;
		bool LoadCooked(CookReader& descriptor, CookReader& payload) override;
	};

	class SHALLTEAR_CORE_API FontAssetImporter : public IAssetImporter {
	public:
		StringHash GetAssetType() const override { return StringHash("FontAsset"); }
		Asset* Import(const Path& sourcePath) override;
	};

} // namespace Shalltear
