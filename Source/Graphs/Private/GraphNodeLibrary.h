#pragma once

#include "GraphsAPI.h"

#include "GraphProgram.h"

#include <span>
#include <string_view>

namespace Shalltear {

	enum class GraphPortType : u8 {
		Float,
		Float2,
		Float3,
		Float4,
		Float2x2,
		Float3x3,
		Float4x4,
		DynamicVector,
		DynamicMatrix,
		Dynamic,
		Any,
		Boolean,
		Texture2D,
		Gradient,
		Texture2DArray,
		Texture3D,
		Cubemap,
		ConnectionState,
		Curve,
		Mesh,
		GPUEvent
	};

	inline constexpr u8 GRAPH_PORT_ALWAYS = 0xff;

	enum class GraphPortSpace : u8 {
		None = 0,
		Position,
		Direction,
		Vector,
		Matrix
	};

	inline constexpr std::string_view GRAPH_FOREIGN_SPACE_ROWS[] = {"Space/Foreign/X", "Space/Foreign/Y", "Space/Foreign/Z"};
	inline constexpr std::string_view GRAPH_SPACE_WORLD = "World";

	inline constexpr std::string_view GRAPH_INTERPOLATOR_TYPES[] = {"Float", "Vector2", "Vector3", "Vector4"};
	inline constexpr std::string_view GRAPH_INTERPOLATOR_MODES[] = {"Linear", "NoPerspective", "NoInterpolation"};

	inline constexpr std::string_view GRAPH_PROPERTY_NODE = "Input/Property";
	inline constexpr std::string_view GRAPH_PROPERTY_SETTING = "property";

	struct GraphPortDefinition {
		std::string_view name;
		GraphPortType type = GraphPortType::DynamicVector;
		f32 defaultValue[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		std::string_view binding;
		u8 whenSetting = GRAPH_PORT_ALWAYS;
		u32 whenOptions = 0;
		u8 orSetting = GRAPH_PORT_ALWAYS;
		u32 orOptions = 0;
		u8 orAlsoSetting = GRAPH_PORT_ALWAYS;
		u32 orAlsoOptions = 0;
		std::string_view label;
		std::string_view customBinding;
		std::string_view keyword;
		std::string_view defaultJson;
		GraphPortSpace space = GraphPortSpace::None;
	};

	inline std::string_view GraphPortLabel(const GraphPortDefinition& port) { return port.label.empty() ? port.name : port.label; }

	enum class GraphSettingKind : u8 {
		Choice,
		Toggle,
		Text,
		Property,
		Number,
		// linear
		Color,
		Vector,
		Gradient,
		Block,
		Asset
	};

	struct GraphSettingDefinition {
		std::string_view key;
		GraphSettingKind kind = GraphSettingKind::Choice;
		std::span<const std::string_view> options;
		u32 defaultIndex = 0;
		std::string_view defaultText;
		f32 defaultValue[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		bool inGraph = false;
		u8 whenSetting = GRAPH_PORT_ALWAYS;
		u32 whenOptions = 0;
		u8 orSetting = GRAPH_PORT_ALWAYS;
		u32 orOptions = 0;
		u8 orAlsoSetting = GRAPH_PORT_ALWAYS;
		u32 orAlsoOptions = 0;
		bool multiline = false;
		bool optionsMove = false;
	};

	struct GraphSettingValue {
		u32 index = 0;
		std::string_view text;
		GraphValueType type = GraphValueType::None;
		f32 numbers[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		std::string_view expression;
		std::string_view sampler;
		std::span<const f32> data;
		GraphValueId value = GRAPH_NO_VALUE;
	};

	struct GraphBinding;
	struct GraphNode;
	struct GraphSubgraph;
	struct GraphCustomHLSLCall;
	struct GraphNodeDefinition;

	struct GraphBodyContext {
		GraphProgramBuilder& builder;
		std::span<const GraphValueId> inputs;
		std::span<const GraphSettingValue> settings;
		std::span<GraphValueId> outputs;
		str error;
		u32 connected = 0;
		std::span<const GraphBinding> bindings;
		const GraphNodeDefinition* definition = nullptr;

		u32 foreign = 0;

		GraphValueId In(u32 index) const { return inputs[index]; }
		bool IsForeign(u32 index) const { return (foreign & (1u << index)) != 0; }
		SHALLTEAR_GRAPHS_API GraphValueId FromForeign(GraphValueId value, GraphPortSpace kind);
		SHALLTEAR_GRAPHS_API GraphValueId Binding(std::string_view name);
		bool IsConnected(u32 index) const { return (connected & (1u << index)) != 0; }
		GraphValueType TypeOf(u32 index) const { return builder.TypeOf(inputs[index]); }
		u32 Setting(u32 index) const { return settings[index].index; }
		std::string_view Text(u32 index) const { return settings[index].text; }
		f32 Number(u32 index) const { return settings[index].numbers[0]; }
	};

	using GraphBodyFunction = void (*)(GraphBodyContext& context);

	enum class GraphPreviewMode : u8 {
		Inherit = 0,
		None,
		Preview2D,
		Preview3D
	};

	struct GraphNodeDefinition {
		std::string_view id;
		std::string_view name;
		std::string_view category;
		std::string_view synonyms;
		std::span<const GraphPortDefinition> inputs;
		std::span<const GraphPortDefinition> outputs;
		std::span<const GraphSettingDefinition> settings;
		GraphBodyFunction body = nullptr;
		GraphPreviewMode preview = GraphPreviewMode::Inherit;
		bool subgraphOnly = false;
		const GraphSubgraph* subgraph = nullptr;
		std::span<const std::string_view> flowInputs;
		bool flowOutput = false;
		const GraphCustomHLSLCall* customHLSL = nullptr;
	};

	namespace GraphNodeLibrary {
		SHALLTEAR_GRAPHS_API const GraphNodeDefinition* Find(std::string_view id);
		SHALLTEAR_GRAPHS_API std::span<const GraphNodeDefinition* const> All();

		SHALLTEAR_GRAPHS_API i32 FindInput(const GraphNodeDefinition& definition, std::string_view name);
		SHALLTEAR_GRAPHS_API i32 FindOutput(const GraphNodeDefinition& definition, std::string_view name);

		SHALLTEAR_GRAPHS_API bool InputActive(const GraphNodeDefinition& definition, u32 input, std::span<const u32> settingIndices);
		SHALLTEAR_GRAPHS_API bool InputActive(const GraphNodeDefinition& definition, u32 input, const GraphNode& node);
		SHALLTEAR_GRAPHS_API bool SettingActive(std::span<const GraphSettingDefinition> settings, u32 setting, const GraphNode& node);
	} // namespace GraphNodeLibrary

	std::span<const GraphNodeDefinition> GraphInputDefinitions();
	std::span<const GraphNodeDefinition> GraphMathDefinitions();
	std::span<const GraphNodeDefinition> GraphChannelDefinitions();
	std::span<const GraphNodeDefinition> GraphProceduralDefinitions();
	std::span<const GraphNodeDefinition> GraphUVDefinitions();
	std::span<const GraphNodeDefinition> GraphArtisticDefinitions();
	std::span<const GraphNodeDefinition> GraphUtilityDefinitions();
	std::span<const GraphNodeDefinition> GraphMasterStackDefinitions();
	std::span<const GraphNodeDefinition> GraphSubgraphDefinitions();
	std::span<const GraphNodeDefinition> GraphEffectDefinitions();

} // namespace Shalltear
