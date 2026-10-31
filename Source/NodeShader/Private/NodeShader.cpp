#include "NodeShader.h"

#include "GraphDefinitions.h"
#include "GraphShading.h"
#include "GraphSurface.h"
#include "NodeShaderImporter.h"
#include "NodeShaderWindow.h"
#include "ShaderSubgraphImporter.h"

#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorPreviews.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/MaterialInspectorOps.h"

namespace Shalltear {

	namespace {

		NodeShaderImporter& GetImporter() {
			static NodeShaderImporter importer;
			return importer;
		}

		ShaderSubgraphImporter& GetSubgraphImporter() {
			static ShaderSubgraphImporter importer;
			return importer;
		}

		const void* _owner = nullptr;

	} // anonymous namespace

	void RegisterNodeShader(const void* owner) {
		AssetImporterRegistry::RegisterExtension(".slshader", &GetImporter(), owner);
		AssetImporterRegistry::Register(GetSubgraphImporter().GetAssetType(), &GetSubgraphImporter(), {GRAPH_SUBGRAPH_EXTENSION}, owner);
		EditorPreviews::AssetBadges::Register(GetSubgraphImporter().GetAssetType(), EditorIcons::Glyph::AssetShaderSubgraph,
											  EditorTheme::ColorToken::AssetShader, owner);
		MaterialInspectorOps::RegisterShaderSource(".slshader", &GraphSurface::IsMaterialShaderSource, owner);
		NodeShaderWindowRegistry::RegisterOpener(owner);

		GraphShadingTarget target;
		target.canShadeEffects = &GraphSurface::CanShadeEffects;
		target.generateFunctions = &GraphSurface::GenerateFunctions;
		if (GraphShadingTargets::Register(GRAPH_SHADER_DOCUMENT_TYPE, target, owner)) {
			_owner = owner;
		}
	}

	void ShutdownNodeShader() {
		GraphShadingTargets::UnregisterOwnedBy(_owner);
		_owner = nullptr;
	}

} // namespace Shalltear

SLT_IMPLEMENT_PACKAGE_WITH_SHUTDOWN(NodeShader, &Shalltear::RegisterNodeShader, &Shalltear::ShutdownNodeShader)
