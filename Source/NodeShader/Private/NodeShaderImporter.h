#pragma once

#include "NodeShaderAPI.h"

#include "Shalltear/Core/Asset/AssetImporter.h"

namespace Shalltear {

	class NodeShaderImporter : public IAssetImporter {
	public:
		StringHash GetAssetType() const override { return StringHash("ShaderAsset"); }

		Asset* Import(const Path& sourcePath) override;

		Asset* ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue& settings) override;

		u32 GetImporterVersion() const override { return 10; }

		bool CanCreateAsset() const override { return true; }
		std::string_view GetCreateMenuLabel() const override { return GetCreateTemplateLabel(0); }
		std::string_view GetCreateDefaultFileName() const override { return "New Node Shader.slshader"; }
		bool CreateAssetSource(Buffer& out) override { return CreateTemplateSource(0, out); }
		u32 GetCreateTemplateCount() const override;
		std::string_view GetCreateTemplateLabel(u32 index) const override;
		std::string_view GetCreateTemplateFileName(u32 index) const override;
		bool CreateTemplateSource(u32 index, Buffer& out) override;

		SHALLTEAR_NODESHADER_API static str TemplateDocument(u32 index);
	};

} // namespace Shalltear
