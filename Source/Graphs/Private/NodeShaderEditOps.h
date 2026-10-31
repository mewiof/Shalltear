#pragma once

#include "GraphsAPI.h"

#include "GraphNodeLibrary.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorTheme.h"

#include <span>
#include <string_view>

namespace Shalltear {

	struct GraphDocument;
	class GraphDefinitions;

	namespace NodeShaderEditOps {

		inline constexpr std::string_view PROPERTY_SETTING = GRAPH_PROPERTY_SETTING;
		inline constexpr std::string_view PROPERTY_NODE = GRAPH_PROPERTY_NODE;

		SHALLTEAR_GRAPHS_API EditorTheme::ColorToken PortColor(GraphPortType type);

		SHALLTEAR_GRAPHS_API u32 AddNode(GraphDocument& document, std::string_view def, Vec2 position);

		SHALLTEAR_GRAPHS_API bool CanConnect(const GraphDocument& document, u32 fromNode, std::string_view fromPort, u32 toNode,
											 std::string_view toPort, str* why = nullptr, GraphDefinitions* definitions = nullptr);

		SHALLTEAR_GRAPHS_API bool OutputFeeds(const GraphDocument& document, u32 node, std::string_view port, GraphPortType input,
											  GraphDefinitions* definitions = nullptr);
		SHALLTEAR_GRAPHS_API bool InputTakes(const GraphDocument& document, u32 node, std::string_view port, GraphPortType output,
											 GraphDefinitions* definitions = nullptr);
		SHALLTEAR_GRAPHS_API bool PropertyFeeds(const GraphDocument& document, u32 property, u32 node, std::string_view port,
												GraphDefinitions* definitions = nullptr);

		SHALLTEAR_GRAPHS_API u32 DynamicWidth(const GraphDocument& document, u32 node, GraphDefinitions* definitions = nullptr);

		SHALLTEAR_GRAPHS_API bool SetInputConstant(GraphDocument& document, u32 node, std::string_view port, std::span<const f32> values);
		SHALLTEAR_GRAPHS_API bool SetInputValue(GraphDocument& document, u32 node, std::string_view port, std::string_view json);
		SHALLTEAR_GRAPHS_API bool SetNodeSetting(GraphDocument& document, u32 node, std::string_view key, std::string_view json,
												 GraphDefinitions* definitions = nullptr);
		SHALLTEAR_GRAPHS_API bool SetGraphSetting(GraphDocument& document, std::string_view key, std::string_view json);
		SHALLTEAR_GRAPHS_API str GraphSettingText(const GraphDocument& document, std::string_view key);
		SHALLTEAR_GRAPHS_API str Quote(std::string_view text);

		SHALLTEAR_GRAPHS_API u32 Delete(GraphDocument& document, std::span<const u32> ids);

		SHALLTEAR_GRAPHS_API u32 AddSubgraphOutput(GraphDocument& document, u32 context, std::string_view type);
		SHALLTEAR_GRAPHS_API bool RenameSubgraphOutput(GraphDocument& document, u32 block, std::string_view name);
		SHALLTEAR_GRAPHS_API bool SetSubgraphOutputType(GraphDocument& document, u32 block, std::string_view type);
		SHALLTEAR_GRAPHS_API u32 AddSubgraphNode(GraphDocument& document, const Guid& asset, Vec2 position);

		SHALLTEAR_GRAPHS_API u32 AddCustomFunctionNode(GraphDocument& document, Vec2 position);
		SHALLTEAR_GRAPHS_API u32 AddCustomFunctionPort(GraphDocument& document, u32 node, bool output, std::string_view type);
		SHALLTEAR_GRAPHS_API bool RenameCustomFunctionPort(GraphDocument& document, u32 node, u32 port, std::string_view name);
		SHALLTEAR_GRAPHS_API bool SetCustomFunctionPortType(GraphDocument& document, u32 node, u32 port, std::string_view type);
		SHALLTEAR_GRAPHS_API bool MoveCustomFunctionPort(GraphDocument& document, u32 node, u32 port, u32 index);
		SHALLTEAR_GRAPHS_API u32 RemoveCustomFunctionPorts(GraphDocument& document, u32 node, std::span<const u32> ports);

		SHALLTEAR_GRAPHS_API std::span<const std::string_view> PropertyTypes();
		SHALLTEAR_GRAPHS_API std::string_view PropertyTypeLabel(std::string_view type);
		SHALLTEAR_GRAPHS_API u32 AddProperty(GraphDocument& document, std::string_view type);
		SHALLTEAR_GRAPHS_API bool RenameProperty(GraphDocument& document, u32 property, std::string_view name);
		SHALLTEAR_GRAPHS_API bool SetPropertyReference(GraphDocument& document, u32 property, std::string_view reference);
		SHALLTEAR_GRAPHS_API bool SetPropertyDefault(GraphDocument& document, u32 property, std::string_view json);
		SHALLTEAR_GRAPHS_API bool SetPropertySetting(GraphDocument& document, u32 property, std::string_view key, std::string_view json);
		SHALLTEAR_GRAPHS_API bool MoveProperty(GraphDocument& document, u32 property, u32 index);
		SHALLTEAR_GRAPHS_API u32 DeleteProperties(GraphDocument& document, std::span<const u32> properties);
		SHALLTEAR_GRAPHS_API u32 AddPropertyNode(GraphDocument& document, u32 property, Vec2 position);
		SHALLTEAR_GRAPHS_API u32 PropertyOf(const GraphDocument& document, u32 node);

		SHALLTEAR_GRAPHS_API u32 AddKeywordEntry(GraphDocument& document, u32 property);
		SHALLTEAR_GRAPHS_API bool RenameKeywordEntry(GraphDocument& document, u32 property, u32 entry, std::string_view name);
		SHALLTEAR_GRAPHS_API bool MoveKeywordEntry(GraphDocument& document, u32 property, u32 entry, u32 index);
		SHALLTEAR_GRAPHS_API u32 RemoveKeywordEntries(GraphDocument& document, u32 property, std::span<const u32> entries);

		SHALLTEAR_GRAPHS_API str Copy(const GraphDocument& document, std::span<const u32> ids);
		SHALLTEAR_GRAPHS_API str CopyWithProperties(const GraphDocument& document, std::span<const u32> ids);
		SHALLTEAR_GRAPHS_API bool Paste(GraphDocument& document, std::string_view fragment, Vec2 offset, Vector<u32>& pasted,
										str& error);

	} // namespace NodeShaderEditOps

} // namespace Shalltear
