#pragma once

#include "NodeShaderAPI.h"

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetImporter.h"

namespace Shalltear {

	class SHALLTEAR_NODESHADER_API ShaderSubgraphAsset : public Asset {
	public:
		SLT_REFLECT_CLASS(ShaderSubgraphAsset)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_END_REFLECT()

		~ShaderSubgraphAsset() override;

		StringHash GetAssetTypeId() const override { return ShaderSubgraphAsset::GetReflectType()->name; }
	};

	class ShaderSubgraphImporter : public IAssetImporter {
	public:
		StringHash GetAssetType() const override { return StringHash("ShaderSubgraphAsset"); }

		Asset* Import(const Path& sourcePath) override;

		bool CanCreateAsset() const override { return true; }
		std::string_view GetCreateMenuLabel() const override { return "Node Shader/Subgraph"; }
		std::string_view GetCreateDefaultFileName() const override { return "New Shader Subgraph.slsubgraph"; }
		bool CreateAssetSource(Buffer& out) override;

		SHALLTEAR_NODESHADER_API static str TemplateDocument();
	};

} // namespace Shalltear
