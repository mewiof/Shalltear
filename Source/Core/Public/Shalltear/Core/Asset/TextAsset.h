#pragma once

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/CoreAPI.h"

namespace Shalltear {

	class SHALLTEAR_CORE_API TextAsset : public Asset {
	public:
		SLT_REFLECT_CLASS(TextAsset)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_REFLECT_FIELD(content)
		SLT_END_REFLECT()

		TextAsset() = default;
		~TextAsset() override;

		str content;

		bool Import(const Path& sourcePath) override;

		bool Export(const Path& destPath) const override;

		StringHash GetAssetTypeId() const override { return TextAsset::GetReflectType()->name; }
	};

	class SHALLTEAR_CORE_API TextAssetImporter : public IAssetImporter {
	public:
		StringHash GetAssetType() const override { return StringHash("TextAsset"); }
		Asset* Import(const Path& sourcePath) override;

		bool CanCreateAsset() const override { return true; }
		std::string_view GetCreateMenuLabel() const override { return "Text File"; }
		std::string_view GetCreateDefaultFileName() const override { return "New Text File.txt"; }
		Asset* CreateAsset() override;
	};

} // namespace Shalltear
