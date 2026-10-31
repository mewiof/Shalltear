#include "ShaderSubgraphImporter.h"

#include "GraphDefinitions.h"
#include "NodeShaderEditOps.h"

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Editor/GraphDocument.h"

#include <cstring>

SLT_REGISTER_TYPE(Shalltear::ShaderSubgraphAsset)

namespace Shalltear {

	ShaderSubgraphAsset::~ShaderSubgraphAsset() = default;

	Asset* ShaderSubgraphImporter::Import(const Path& sourcePath) {
		const str text = FileSystem::ReadTextFile(sourcePath, MemTag::Editor);
		GraphDocument document;
		str error;
		if (text.empty()) {
			SLT_LOG_ERROR("NodeShader", "'{}' is empty or unreadable", sourcePath.ToString());
			return nullptr;
		}
		if (!GraphDocumentOps::Read(text, document, error)) {
			SLT_LOG_ERROR("NodeShader", "'{}' isn't a graph document: {}", sourcePath.ToString(), error);
			return nullptr;
		}
		if (document.type != GRAPH_SUBGRAPH_DOCUMENT_TYPE) {
			SLT_LOG_ERROR("NodeShader", "'{}' is a {} document (not a subgraph)", sourcePath.ToString(), document.type);
			return nullptr;
		}
		return New<ShaderSubgraphAsset>(MemTag::Asset);
	}

	str ShaderSubgraphImporter::TemplateDocument() {
		GraphDocument document;
		document.type = str(GRAPH_SUBGRAPH_DOCUMENT_TYPE);
		document.settings.push_back({str(GRAPH_SUBGRAPH_CATEGORY_SETTING), NodeShaderEditOps::Quote(GRAPH_SUBGRAPH_DEFAULT_CATEGORY)});
		GraphContext output;
		output.node.id = document.nextId++;
		output.node.def = str(GRAPH_SUBGRAPH_OUTPUT_CONTEXT);
		output.node.x = 300;
		output.node.y = 0;
		document.contexts.push_back(std::move(output));
		NodeShaderEditOps::AddSubgraphOutput(document, document.contexts.back().node.id, GRAPH_SUBGRAPH_OUTPUT_TYPES[GRAPH_SUBGRAPH_DEFAULT_OUTPUT_TYPE]);
		return GraphDocumentOps::Write(document);
	}

	bool ShaderSubgraphImporter::CreateAssetSource(Buffer& out) {
		const str text = TemplateDocument();
		out = Buffer(text.size(), MemTag::Asset);
		std::memcpy(out.data, text.data(), text.size());
		return true;
	}

} // namespace Shalltear
