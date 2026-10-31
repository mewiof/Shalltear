#pragma once

#include "GraphsAPI.h"

#include "GraphCustomHLSL.h"
#include "GraphNodeLibrary.h"
#include "GraphProperties.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/GraphDocument.h"

#include <deque>
#include <memory_resource>
#include <span>
#include <string_view>

namespace Shalltear {

	inline constexpr std::string_view GRAPH_SUBGRAPH_DOCUMENT_TYPE = "ShaderSubgraph";
	inline constexpr std::string_view GRAPH_SUBGRAPH_EXTENSION = ".slsubgraph";
	inline constexpr std::string_view GRAPH_SUBGRAPH_NODE = "Utility/Subgraph";
	inline constexpr std::string_view GRAPH_SUBGRAPH_ASSET_SETTING = "asset";
	inline constexpr std::string_view GRAPH_SUBGRAPH_OUTPUT_CONTEXT = "Context/SubgraphOutput";
	inline constexpr std::string_view GRAPH_SUBGRAPH_OUTPUT_BLOCK = "Block/SubgraphOutput";
	inline constexpr std::string_view GRAPH_SUBGRAPH_CATEGORY_SETTING = "category";
	inline constexpr std::string_view GRAPH_SUBGRAPH_DEFAULT_CATEGORY = "Subgraphs";

	inline constexpr std::string_view GRAPH_SUBGRAPH_OUTPUT_TYPES[] = {"Float", "Vector2", "Vector3", "Vector4", "Boolean",
																	   "Matrix2", "Matrix3", "Matrix4", "Texture2D", "Texture2DArray",
																	   "Texture3D", "Cubemap", "Gradient"};
	inline constexpr u32 GRAPH_SUBGRAPH_DEFAULT_OUTPUT_TYPE = 3;

	inline constexpr std::string_view GRAPH_SUBGRAPH_OUTPUT_PORT = "Value";
	inline constexpr std::string_view GRAPH_PROPERTY_OUTPUT_SETTING = "output";

	enum class GraphSubgraphKind : u8 {
		Shader,
		EffectOperator,
		EffectBlock,
		EffectSystem
	};

	struct GraphReadDocument {
		Guid asset;
		Path path;
		str physicalPath;
		u64 contentHash = 0;
		str name;
		GraphDocument document;
		str error;
	};

	struct GraphReadText : GraphReadDocument {
		str text;
	};

	struct GraphSubgraph : GraphReadDocument {
		GraphSubgraphKind kind = GraphSubgraphKind::Shader;
		str category;
		GraphNodeDefinition definition;
		Vector<u32> inputProperties{GetMemoryResource(MemTag::Editor)};
		Vector<GraphPropertyKind> inputKinds{GetMemoryResource(MemTag::Editor)};
		Vector<u32> outputBlocks{GetMemoryResource(MemTag::Editor)};
		u32 blockContext = 0;
		u32 suitable = 0;

		Vector<GraphPortDefinition> inputs{GetMemoryResource(MemTag::Editor)};
		Vector<GraphPortDefinition> outputs{GetMemoryResource(MemTag::Editor)};
		Vector<std::string_view> flowSlots{GetMemoryResource(MemTag::Editor)};
		std::pmr::deque<str> strings{GetMemoryResource(MemTag::Editor)};
	};

	inline constexpr std::string_view GRAPH_CUSTOM_FUNCTION_NODE = "Utility/CustomFunction";

	inline constexpr std::string_view GRAPH_CUSTOM_FUNCTION_TYPE = "Type";
	inline constexpr std::string_view GRAPH_CUSTOM_FUNCTION_NAME = "Name";
	inline constexpr std::string_view GRAPH_CUSTOM_FUNCTION_SOURCE = "Source";
	inline constexpr std::string_view GRAPH_CUSTOM_FUNCTION_BODY = "Body";
	inline constexpr std::string_view GRAPH_CUSTOM_FUNCTION_INPUTS = "Inputs";
	inline constexpr std::string_view GRAPH_CUSTOM_FUNCTION_OUTPUTS = "Outputs";

	inline constexpr std::string_view GRAPH_CUSTOM_FUNCTION_SOURCE_TYPES[] = {"File", "String"};

	inline constexpr std::string_view GRAPH_CUSTOM_FUNCTION_PORT_TYPES[] = {"Float", "Vector2", "Vector3", "Vector4",
																			"Boolean", "Matrix2", "Matrix3", "Matrix4"};

	inline constexpr std::string_view GRAPH_CUSTOM_FUNCTION_EXTENSIONS[] = {".hlsl"};

	inline constexpr std::string_view GRAPH_GENERATED_DIRECTORY = "project://Library/NodeShader";

	struct GraphCustomFunctionPort {
		u32 id = 0;
		str name;
		u32 type = 0;
	};

	struct GraphBuiltDefinition {
		str key;
		str error;
		GraphNodeDefinition definition;
		Vector<GraphPortDefinition> inputs{GetMemoryResource(MemTag::Editor)};
		Vector<GraphPortDefinition> outputs{GetMemoryResource(MemTag::Editor)};
		Vector<GraphSettingDefinition> settings{GetMemoryResource(MemTag::Editor)};
		std::pmr::deque<Vector<std::string_view>> options{GetMemoryResource(MemTag::Editor)};
		std::pmr::deque<str> strings{GetMemoryResource(MemTag::Editor)};
		GraphCustomHLSLCall customHLSL;
	};

	class SHALLTEAR_GRAPHS_API GraphDefinitions {
	public:
		GraphDefinitions();
		~GraphDefinitions();
		GraphDefinitions(const GraphDefinitions&) = delete;
		GraphDefinitions& operator=(const GraphDefinitions&) = delete;

		const GraphNodeDefinition* Find(const GraphDocument& document, const GraphNode& node, str* error = nullptr);

		const GraphSubgraph* Subgraph(const Guid& asset, str* error = nullptr);

		const GraphReadDocument* NodeShader(const Guid& asset, str* error = nullptr);
		const GraphReadText* ShaderFile(const Guid& asset, str* error = nullptr);
		const GraphReadDocument& DefaultNodeShader();

		void Read(Vector<const GraphReadDocument*>& out) const;

		// invalidates definitions returned before
		void Clear();
	private:
		const GraphNodeDefinition* CustomFunction(const GraphNode& node, str* error);
		const GraphNodeDefinition* CustomHLSL(const GraphNode& node);
		const GraphNodeDefinition* EnumKeyword(const GraphProperty& property, str* error);
		const GraphNodeDefinition* EffectNode(const GraphNode& node, str* error);
		GraphBuiltDefinition& Built(str key, bool& made);

		const GraphNodeDefinition* NodeShaderOutput(const GraphNode& node, str* error);
		const GraphNodeDefinition* OutputProperty(const GraphProperty& property, str* error);
		const GraphNodeDefinition* UsedSubgraph(const GraphDocument& document, const GraphNode& node, str* error);

		std::pmr::deque<GraphSubgraph> _subgraphs{GetMemoryResource(MemTag::Editor)};
		std::pmr::deque<GraphReadDocument> _nodeShaders{GetMemoryResource(MemTag::Editor)};
		std::pmr::deque<GraphReadText> _shaderFiles{GetMemoryResource(MemTag::Editor)};
		std::pmr::deque<GraphReadDocument> _defaultNodeShader{GetMemoryResource(MemTag::Editor)};
		std::pmr::deque<GraphBuiltDefinition> _built{GetMemoryResource(MemTag::Editor)};
	};

	namespace GraphSubgraphs {

		SHALLTEAR_GRAPHS_API Guid AssetOf(const GraphNode& node);

		SHALLTEAR_GRAPHS_API bool KindOf(std::string_view type, GraphSubgraphKind& out);
		SHALLTEAR_GRAPHS_API std::string_view DocumentType(GraphSubgraphKind kind);
		SHALLTEAR_GRAPHS_API std::string_view Extension(GraphSubgraphKind kind);
		SHALLTEAR_GRAPHS_API std::string_view DefaultCategory(GraphSubgraphKind kind);
		SHALLTEAR_GRAPHS_API bool IsOutput(const GraphProperty& property);
		SHALLTEAR_GRAPHS_API u32 BlockContext(const GraphDocument& document);
		SHALLTEAR_GRAPHS_API bool Uses(GraphDefinitions& definitions, const GraphDocument& document, const Guid& target);
		SHALLTEAR_GRAPHS_API str NameOf(const Guid& asset);

		SHALLTEAR_GRAPHS_API u32 OutputTypeOf(const GraphNode& block);
		SHALLTEAR_GRAPHS_API str OutputNameOf(const GraphNode& block);
		SHALLTEAR_GRAPHS_API const GraphNodeDefinition* OutputBlockDefinition(u32 type);

		SHALLTEAR_GRAPHS_API u32 OutputContext(const GraphDocument& document);

		SHALLTEAR_GRAPHS_API void ProjectSubgraphs(Vector<Guid>& out);
		SHALLTEAR_GRAPHS_API void ProjectFiles(std::string_view extension, Vector<Guid>& out);

		SHALLTEAR_GRAPHS_API GraphPortType InputPortType(GraphPropertyKind kind);

		SHALLTEAR_GRAPHS_API bool CollectKeywords(const GraphDocument& document, GraphDefinitions& definitions,
												  Vector<GraphPropertyInfo>& properties, Vector<GraphDiagnostic>& diagnostics);

	} // namespace GraphSubgraphs

	namespace GraphCustomFunctions {

		SHALLTEAR_GRAPHS_API bool ReadPorts(const GraphNode& node, std::string_view key, Vector<GraphCustomFunctionPort>& out, str& error);
		SHALLTEAR_GRAPHS_API str WritePorts(std::span<const GraphCustomFunctionPort> ports);

		SHALLTEAR_GRAPHS_API str Identifier(std::string_view name);

	} // namespace GraphCustomFunctions

} // namespace Shalltear
