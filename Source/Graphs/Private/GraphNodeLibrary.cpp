#include "GraphNodeLibrary.h"

#include "GraphCompiler.h"

#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Editor/GraphDocument.h"

#include <array>

namespace Shalltear {

	namespace {

		constexpr size_t MAX_DEFINITIONS = 384;

		struct Table {
			std::array<const GraphNodeDefinition*, MAX_DEFINITIONS> entries{};
			size_t count = 0;
		};

		const Table& GetTable() {
			static const Table table = [] {
				Table built;
				auto add = [&built](std::span<const GraphNodeDefinition> category) {
					for (const GraphNodeDefinition& definition : category) {
						if (built.count < built.entries.size()) {
							built.entries[built.count++] = &definition;
						}
					}
				};
				add(GraphInputDefinitions());
				add(GraphMathDefinitions());
				add(GraphChannelDefinitions());
				add(GraphProceduralDefinitions());
				add(GraphUVDefinitions());
				add(GraphArtisticDefinitions());
				add(GraphUtilityDefinitions());
				add(GraphMasterStackDefinitions());
				add(GraphSubgraphDefinitions());
				add(GraphEffectDefinitions());
				return built;
			}();
			return table;
		}

	} // anonymous namespace

	GraphValueId GraphBodyContext::Binding(std::string_view name) {
		for (const GraphBinding& binding : bindings) {
			if (binding.name == name) {
				return binding.Read(builder);
			}
		}
		if (error.empty()) {
			error = "reads " + str(name) + " (this target doesn't provide it)";
		}
		return GRAPH_NO_VALUE;
	}

	GraphValueId GraphBodyContext::FromForeign(GraphValueId value, GraphPortSpace kind) {
		if (kind == GraphPortSpace::None || kind == GraphPortSpace::Matrix) {
			return value;
		}
		GraphValueId components[3];
		for (u32 row = 0; row < 3; ++row) {
			const GraphValueId rowValue = Binding(GRAPH_FOREIGN_SPACE_ROWS[row]);
			if (rowValue == GRAPH_NO_VALUE) {
				return GRAPH_NO_VALUE;
			}
			components[row] = builder.Dot(builder.Swizzle(rowValue, "xyz"), value);
			if (kind == GraphPortSpace::Position) {
				components[row] = builder.Add(components[row], builder.Swizzle(rowValue, "w"));
			}
		}
		const GraphValueId converted = builder.Construct(GraphValueType::Float3, components);
		return kind == GraphPortSpace::Direction ? builder.Unary(GraphOp::Normalize, converted) : converted;
	}

	namespace GraphNodeLibrary {

		const GraphNodeDefinition* Find(std::string_view id) {
			for (const GraphNodeDefinition* definition : All()) {
				if (definition->id == id) {
					return definition;
				}
			}
			return nullptr;
		}

		std::span<const GraphNodeDefinition* const> All() {
			const Table& table = GetTable();
			return {table.entries.data(), table.count};
		}

		i32 FindInput(const GraphNodeDefinition& definition, std::string_view name) {
			for (size_t i = 0; i < definition.inputs.size(); ++i) {
				if (definition.inputs[i].name == name) {
					return static_cast<i32>(i);
				}
			}
			return -1;
		}

		i32 FindOutput(const GraphNodeDefinition& definition, std::string_view name) {
			for (size_t i = 0; i < definition.outputs.size(); ++i) {
				if (definition.outputs[i].name == name) {
					return static_cast<i32>(i);
				}
			}
			return -1;
		}

		namespace {

			bool SettingShown(std::span<const GraphSettingDefinition> settings, u32 setting, const GraphNode& node, u32 depth);

			bool Shown(std::span<const GraphSettingDefinition> settings, u8 when, u32 options, const GraphNode& node, u32 depth = 0) {
				if (when == GRAPH_PORT_ALWAYS || when >= settings.size()) {
					return when == GRAPH_PORT_ALWAYS;
				}
				if (!SettingShown(settings, when, node, depth + 1)) {
					return false;
				}
				const GraphSettingDefinition& setting = settings[when];
				u32 option = setting.defaultIndex;
				for (const GraphSetting& stored : node.settings) {
					if (stored.key != setting.key) {
						continue;
					}
					LinearAllocator arena(stored.value.size() + 64, MemTag::Editor);
					const JSONValue value = JSONParse(stored.value, arena);
					if (value.IsBool()) {
						option = value.AsBool() ? 1u : 0u;
					}
					for (u32 i = 0; value.IsString() && i < setting.options.size(); ++i) {
						option = setting.options[i] == value.AsString() ? i : option;
					}
				}
				return option < 32 && (options & (1u << option)) != 0;
			}

			bool OrShown(std::span<const GraphSettingDefinition> settings, u8 orSetting, u32 orOptions, u8 alsoSetting, u32 alsoOptions,
						 const GraphNode& node, u32 depth) {
				if (orSetting == GRAPH_PORT_ALWAYS || !Shown(settings, orSetting, orOptions, node, depth)) {
					return false;
				}
				return alsoSetting == GRAPH_PORT_ALWAYS || Shown(settings, alsoSetting, alsoOptions, node, depth);
			}

			bool SettingShown(std::span<const GraphSettingDefinition> settings, u32 setting, const GraphNode& node, u32 depth) {
				if (depth > 8) {
					return true;
				}
				const GraphSettingDefinition& definition = settings[setting];
				return Shown(settings, definition.whenSetting, definition.whenOptions, node, depth) ||
					   OrShown(settings, definition.orSetting, definition.orOptions, definition.orAlsoSetting, definition.orAlsoOptions, node, depth);
			}

			bool OptionIn(std::span<const u32> settingIndices, u8 when, u32 options) {
				const u32 option = when < settingIndices.size() ? settingIndices[when] : 0u;
				return option < 32 && (options & (1u << option)) != 0;
			}

		} // anonymous namespace

		bool InputActive(const GraphNodeDefinition& definition, u32 input, std::span<const u32> settingIndices) {
			if (input >= definition.inputs.size()) {
				return false;
			}
			const GraphPortDefinition& port = definition.inputs[input];
			if (port.whenSetting == GRAPH_PORT_ALWAYS) {
				return true;
			}
			if (OptionIn(settingIndices, port.whenSetting, port.whenOptions)) {
				return true;
			}
			return port.orSetting != GRAPH_PORT_ALWAYS && OptionIn(settingIndices, port.orSetting, port.orOptions) &&
				   (port.orAlsoSetting == GRAPH_PORT_ALWAYS || OptionIn(settingIndices, port.orAlsoSetting, port.orAlsoOptions));
		}

		bool InputActive(const GraphNodeDefinition& definition, u32 input, const GraphNode& node) {
			if (input >= definition.inputs.size()) {
				return false;
			}
			const GraphPortDefinition& port = definition.inputs[input];
			return Shown(definition.settings, port.whenSetting, port.whenOptions, node) ||
				   OrShown(definition.settings, port.orSetting, port.orOptions, port.orAlsoSetting, port.orAlsoOptions, node, 0);
		}

		bool SettingActive(std::span<const GraphSettingDefinition> settings, u32 setting, const GraphNode& node) {
			if (setting >= settings.size()) {
				return false;
			}
			return SettingShown(settings, setting, node, 0);
		}

	} // namespace GraphNodeLibrary

} // namespace Shalltear
