#pragma once

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/StringHash.h"

#include <string_view>

namespace Shalltear {

	class SHALLTEAR_CORE_API StyleSheetAsset : public Asset {
	public:
		SLT_REFLECT_CLASS(StyleSheetAsset)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_REFLECT_FIELD(source)
		SLT_END_REFLECT()

		StyleSheetAsset() = default;
		~StyleSheetAsset() override;

		str source;

		u32 version = 0;

		bool CanAdoptReimport() const override { return true; }
		void AdoptReimport(Asset& fresh) override;

		bool Import(const Path& sourcePath) override;
		bool Export(const Path& destPath) const override;
		StringHash GetAssetTypeId() const override { return StyleSheetAsset::GetReflectType()->name; }

		bool Parse(std::string_view text, str& error);
	};

	class SHALLTEAR_CORE_API StyleSheetImporter : public IAssetImporter {
	public:
		StringHash GetAssetType() const override { return StringHash("StyleSheetAsset"); }
		Asset* Import(const Path& sourcePath) override;

		bool CanCreateAsset() const override { return true; }
		std::string_view GetCreateMenuLabel() const override { return "Style Sheet"; }
		std::string_view GetCreateDefaultFileName() const override { return "New Style Sheet.slss"; }
		Asset* CreateAsset() override;
	};

} // namespace Shalltear
