#include "NodeShaderImporter.h"

#include "GraphDefinitions.h"
#include "GraphNodeLibrary.h"
#include "GraphSurface.h"
#include "NodeShaderEditOps.h"
#include "NodeShaderStackOps.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>
#include <cstring>
#include <format>
#include <iterator>
#include <span>

namespace Shalltear {

	namespace {

		Path GeneratedPath(const Path& sourcePath, const Path& virtualPath) {
			Path library = FileSystem::Resolve(Path(str(GRAPH_GENERATED_DIRECTORY)));
			if (library.ToString().find("://") != str::npos) {
				return {};
			}
			Guid guid = virtualPath.ToString().empty() ? Guid{} : AssetDatabase::LookupGuid(virtualPath);
			str stem = guid.IsValid() ? guid.ToString() : std::format("{:016x}", StringHash(sourcePath.ToString()).GetValue());
			return Path(library.ToString() + "/" + stem + ".hlsl");
		}

		void Report(const GraphDocument& document, GraphDefinitions& definitions, std::string_view graph, const GraphDiagnostic& diagnostic) {
			if (diagnostic.node == 0) {
				SLT_LOG_ERROR("NodeShader", "'{}': {}", graph, diagnostic.message);
				return;
			}
			const GraphNode* node = GraphDocumentOps::FindNode(const_cast<GraphDocument&>(document), diagnostic.node);
			const GraphNodeDefinition* definition = node ? definitions.Find(document, *node) : nullptr;
			const str what = definition != nullptr ? str(definition->name) : (node ? node->def : str("?"));
			SLT_LOG_ERROR("NodeShader", "'{}': node {} ({}) {}", graph, diagnostic.node, what, diagnostic.message);
		}

	} // anonymous namespace

	namespace {

		struct GraphTemplate {
			std::string_view label;
			std::string_view target;
			std::span<const std::string_view> blocks;
		};

		constexpr std::string_view LIT_BLOCKS[] = {"Block/BaseColor", "Block/NormalTS", "Block/Metallic", "Block/Roughness",
												   "Block/Emission", "Block/Occlusion", "Block/Alpha"};
		constexpr std::string_view UNLIT_BLOCKS[] = {"Block/BaseColor", "Block/Alpha"};
		constexpr std::string_view SKY_BLOCKS[] = {"Block/BaseColor"};

		constexpr GraphTemplate TEMPLATES[] = {
			{"Node Shader/Lit", "lit", LIT_BLOCKS},
			{"Node Shader/Unlit", "unlit", UNLIT_BLOCKS},
			{"Node Shader/Custom Lighting", "customLighting", LIT_BLOCKS},
			{"Node Shader/Fullscreen", "fullscreen", UNLIT_BLOCKS},
			{"Node Shader/Sky", "sky", SKY_BLOCKS},
		};

	} // anonymous namespace

	u32 NodeShaderImporter::GetCreateTemplateCount() const { return static_cast<u32>(std::size(TEMPLATES)); }

	std::string_view NodeShaderImporter::GetCreateTemplateLabel(u32 index) const {
		return index < std::size(TEMPLATES) ? TEMPLATES[index].label : std::string_view{};
	}

	std::string_view NodeShaderImporter::GetCreateTemplateFileName(u32 index) const {
		return index < std::size(TEMPLATES) ? GetCreateDefaultFileName() : std::string_view{};
	}

	str NodeShaderImporter::TemplateDocument(u32 index) {
		if (index >= std::size(TEMPLATES)) {
			return {};
		}
		const GraphTemplate& chosen = TEMPLATES[index];
		GraphDocument document;
		document.type = str(GraphSurface::DOCUMENT_TYPE);
		document.settings.push_back({str("target"), "\"" + str(chosen.target) + "\""});
		GraphContext fragment;
		fragment.node.id = document.nextId++;
		fragment.node.def = "Context/Fragment";
		fragment.node.x = 300;
		fragment.node.y = 0;
		for (std::string_view block : chosen.blocks) {
			GraphNode entry;
			entry.id = document.nextId++;
			entry.def = str(block);
			fragment.blocks.push_back(std::move(entry));
		}
		document.contexts.push_back(std::move(fragment));
		if (GraphSurface::HasVertexStage(document)) {
			NodeShaderStackOps::EnsureVertexStack(document);
		}
		if (chosen.target == "customLighting") {
			NodeShaderStackOps::EnsureLightingStack(document);
		}
		return GraphDocumentOps::Write(document);
	}

	bool NodeShaderImporter::CreateTemplateSource(u32 index, Buffer& out) {
		const str text = TemplateDocument(index);
		if (text.empty()) {
			return false;
		}
		out = Buffer(text.size(), MemTag::Asset);
		std::memcpy(out.data, text.data(), text.size());
		return true;
	}

	Asset* NodeShaderImporter::Import(const Path& sourcePath) { return ImportWithSettings(sourcePath, Path(), JSONValue{}); }

	Asset* NodeShaderImporter::ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue&) {
		const str graph = virtualPath.ToString().empty() ? sourcePath.ToString() : virtualPath.ToString();

		str text = FileSystem::ReadTextFile(sourcePath, MemTag::Editor);
		if (text.empty()) {
			SLT_LOG_ERROR("NodeShader", "'{}' is empty or unreadable", graph);
			return nullptr;
		}
		GraphDocument document;
		str error;
		if (!GraphDocumentOps::Read(text, document, error)) {
			SLT_LOG_ERROR("NodeShader", "'{}' isn't a graph document: {}", graph, error);
			return nullptr;
		}

		GraphDefinitions definitions;
		str generated;
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		if (!GraphSurface::Generate(document, graph, generated, diagnostics, &definitions)) {
			for (const GraphDiagnostic& diagnostic : diagnostics) {
				Report(document, definitions, graph, diagnostic);
			}
			return nullptr;
		}

		Path generatedPath = GeneratedPath(sourcePath, virtualPath);
		if (generatedPath.ToString().empty()) {
			SLT_LOG_ERROR("NodeShader", "'{}' can't import with no project open (its shader is generated under the project's Library)", graph);
			return nullptr;
		}
		if (FileSystem::ReadTextFile(generatedPath, MemTag::Editor) != generated) {
			FileSystem::CreateDirectory(FileSystem::Resolve(Path(str(GRAPH_GENERATED_DIRECTORY))));
			if (!FileSystem::WriteTextFile(generatedPath, generated)) {
				SLT_LOG_ERROR("NodeShader", "'{}': couldn't write its generated shader to '{}'", graph, generatedPath.ToString());
				return nullptr;
			}
		}

		ShaderAsset* shader = New<ShaderAsset>(MemTag::Asset);
		if (!shader->Import(generatedPath)) {
			SLT_LOG_ERROR("NodeShader", "'{}': the shader generated from it didn't compile ('{}')", graph, generatedPath.ToString());
			Delete(shader);
			return nullptr;
		}
		Vector<const GraphReadDocument*> read{GetMemoryResource(MemTag::Editor)};
		definitions.Read(read);
		for (const GraphReadDocument* subgraph : read) {
			if (subgraph->physicalPath.empty()) {
				continue;
			}
			const bool listed = std::any_of(shader->dependencies.begin(), shader->dependencies.end(),
											[&](const ShaderAsset::Dependency& dependency) { return dependency.physicalPath == subgraph->physicalPath; });
			if (!listed) {
				shader->dependencies.push_back({subgraph->physicalPath, subgraph->contentHash});
			}
		}
		return shader;
	}

} // namespace Shalltear
