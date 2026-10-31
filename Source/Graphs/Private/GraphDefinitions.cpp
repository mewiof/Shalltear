#include "GraphDefinitions.h"

#include "GraphEffect.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/ProjectLayout.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/StringUtil.h"

#include <algorithm>
#include <array>
#include <filesystem>
#include <format>
#include <iterator>
#include <utility>

namespace Shalltear {

	namespace {

		namespace fs = std::filesystem;
		using P = GraphPortType;

		constexpr P OUTPUT_PORT_TYPES[] = {P::Float, P::Float2, P::Float3, P::Float4, P::Boolean,
										   P::Float2x2, P::Float3x3, P::Float4x4, P::Texture2D, P::Texture2DArray,
										   P::Texture3D, P::Cubemap, P::Gradient};
		static_assert(std::size(OUTPUT_PORT_TYPES) == std::size(GRAPH_SUBGRAPH_OUTPUT_TYPES), "one port type per output type");

		constexpr GraphSettingDefinition OUTPUT_SETTINGS[] = {
			{"Name", GraphSettingKind::Text, {}, 0, "Out_Vector4"},
			{"Type", GraphSettingKind::Choice, GRAPH_SUBGRAPH_OUTPUT_TYPES, GRAPH_SUBGRAPH_DEFAULT_OUTPUT_TYPE},
		};

		template <size_t... I>
		constexpr std::array<GraphPortDefinition, sizeof...(I)> OutputPorts(std::index_sequence<I...>) {
			return {GraphPortDefinition{"Value", OUTPUT_PORT_TYPES[I]}...};
		}
		constexpr auto OUTPUT_PORTS = OutputPorts(std::make_index_sequence<std::size(OUTPUT_PORT_TYPES)>{});

		template <size_t... I>
		constexpr std::array<GraphNodeDefinition, sizeof...(I)> OutputBlocks(std::index_sequence<I...>) {
			return {GraphNodeDefinition{GRAPH_SUBGRAPH_OUTPUT_BLOCK, "Output", "Block", "", std::span<const GraphPortDefinition>(&OUTPUT_PORTS[I], 1), {}, OUTPUT_SETTINGS, nullptr}...};
		}
		constexpr auto OUTPUT_BLOCKS = OutputBlocks(std::make_index_sequence<std::size(OUTPUT_PORT_TYPES)>{});

		constexpr GraphNodeDefinition SUBGRAPH_DEFINITIONS[] = {
			{GRAPH_SUBGRAPH_OUTPUT_CONTEXT, "Output", "Context", "", {}, {}, {}, nullptr},
			OUTPUT_BLOCKS[GRAPH_SUBGRAPH_DEFAULT_OUTPUT_TYPE],
		};

		const GraphSetting* FindSetting(const Vector<GraphSetting>& settings, std::string_view key) {
			for (const GraphSetting& setting : settings) {
				if (setting.key == key) {
					return &setting;
				}
			}
			return nullptr;
		}

		str SettingString(const Vector<GraphSetting>& settings, std::string_view key) {
			const GraphSetting* setting = FindSetting(settings, key);
			if (!setting) {
				return {};
			}
			LinearAllocator arena(setting->value.size() + 64, MemTag::Editor);
			const JSONValue value = JSONParse(setting->value, arena);
			return value.IsString() ? str(value.AsString()) : str();
		}

		bool ReadDocument(const Guid& asset, GraphReadDocument& out) {
			out.asset = asset;
			out.path = AssetDatabase::LookupPath(asset);
			if (out.path.ToString().empty()) {
				out.error = "the project doesn't have";
				return false;
			}
			out.name = str(fs::path(out.path.ToString()).stem().generic_string());
			out.physicalPath = FileSystem::Resolve(out.path).ToString();
			const str text = FileSystem::ReadTextFile(out.path, MemTag::Editor);
			out.contentHash = StringHash(std::string_view(text)).GetValue();
			str readError;
			if (text.empty()) {
				out.error = "is empty or unreadable";
			} else if (!GraphDocumentOps::Read(text, out.document, readError)) {
				out.error = "isn't a graph document: " + readError;
			}
			return out.error.empty();
		}

		P SlotTypeOf(GraphPropertyKind kind) {
			switch (kind) {
			case GraphPropertyKind::Float:
			case GraphPropertyKind::Integer:
				return P::Float;
			case GraphPropertyKind::Boolean:
				return P::Boolean;
			case GraphPropertyKind::Vector2:
				return P::Float2;
			case GraphPropertyKind::Vector3:
				return P::Float3;
			case GraphPropertyKind::Vector4:
			case GraphPropertyKind::Color:
				return P::Float4;
			case GraphPropertyKind::Texture2D:
				return P::Texture2D;
			case GraphPropertyKind::Texture2DArray:
				return P::Texture2DArray;
			case GraphPropertyKind::Texture3D:
				return P::Texture3D;
			case GraphPropertyKind::Cubemap:
				return P::Cubemap;
			case GraphPropertyKind::BooleanKeyword:
			case GraphPropertyKind::EnumKeyword:
				break;
			}
			return P::Any;
		}

		u32 PropertyIdOf(const GraphNode& node) {
			const GraphSetting* setting = FindSetting(node.settings, GRAPH_PROPERTY_SETTING);
			return setting ? static_cast<u32>(std::strtoul(setting->value.c_str(), nullptr, 10)) : 0;
		}

		void BuildOutputStack(GraphSubgraph& out) {
			const GraphDocument& document = out.document;
			const GraphContext* output = nullptr;
			for (const GraphContext& context : document.contexts) {
				if (context.node.def != GRAPH_SUBGRAPH_OUTPUT_CONTEXT) {
					continue;
				}
				if (output != nullptr) {
					out.error = "has 2 Output nodes (a subgraph has 1)";
					return;
				}
				output = &context;
			}
			if (output == nullptr || output->blocks.empty()) {
				out.error = "has no outputs (a subgraph needs at least 1)";
				return;
			}
			for (const GraphNode& block : output->blocks) {
				const u32 type = GraphSubgraphs::OutputTypeOf(block);
				if (block.def != GRAPH_SUBGRAPH_OUTPUT_BLOCK || type >= std::size(OUTPUT_PORT_TYPES)) {
					out.error = "has an output (" + std::to_string(block.id) + ") that isn't a subgraph output of a known type";
					return;
				}
				GraphPortDefinition port;
				port.name = out.strings.emplace_back(std::to_string(block.id));
				str label = GraphSubgraphs::OutputNameOf(block);
				port.label = out.strings.emplace_back(label.empty() ? str("Out") : std::move(label));
				port.type = OUTPUT_PORT_TYPES[type];
				out.outputs.push_back(port);
				out.outputBlocks.push_back(block.id);
			}
		}

		void BuildOutputProperties(GraphSubgraph& out) {
			const GraphDocument& document = out.document;
			for (const GraphProperty& property : document.properties) {
				if (!GraphSubgraphs::IsOutput(property)) {
					continue;
				}
				GraphPropertyInfo info;
				str error;
				if (!GraphProperties::Parse(property, info, error)) {
					out.error = "has blackboard property '" + property.name + "' (" + error + ")";
					return;
				}
				const P type = SlotTypeOf(info.kind);
				if (type == P::Any || GraphTypes::IsTexture(GraphProperties::ValueType(info.kind))) {
					out.error = "has an Output '" + property.name + "' of a type no output supports";
					return;
				}
				u32 first = 0;
				u32 wired = 0;
				for (const GraphNode& node : document.nodes) {
					if (node.def != GRAPH_PROPERTY_NODE || PropertyIdOf(node) != property.id) {
						continue;
					}
					first = first != 0 ? first : node.id;
					for (const GraphInput& input : node.inputs) {
						if (input.port != GRAPH_SUBGRAPH_OUTPUT_PORT || input.fromNode == 0) {
							continue;
						}
						if (wired != 0) {
							out.error = "has 2 nodes feeding its Output '" + property.name + "' (an output takes 1)";
							return;
						}
						wired = node.id;
					}
				}
				GraphPortDefinition port;
				port.name = out.strings.emplace_back(std::to_string(property.id));
				port.label = out.strings.emplace_back(property.name);
				port.type = type;
				for (u32 i = 0; i < 4; ++i) {
					port.defaultValue[i] = info.defaultValue[i];
				}
				out.outputs.push_back(port);
				out.outputBlocks.push_back(wired != 0 ? wired : first);
			}
		}

		void BuildBlockContext(GraphSubgraph& out) {
			for (const GraphContext& context : out.document.contexts) {
				if (context.node.def != GRAPH_EFFECT_BLOCK_SUBGRAPH_CONTEXT) {
					out.error = "holds a context other than its Block Subgraph (a block subgraph has no other)";
					return;
				}
				if (out.blockContext != 0) {
					out.error = "has 2 Block Subgraph contexts (a block subgraph has 1)";
					return;
				}
				out.blockContext = context.node.id;
				out.suitable = GraphEffectRules::SuitableOf(context.node);
			}
			if (out.blockContext == 0) {
				out.error = "has no Block Subgraph context (it holds the blocks)";
			} else if (out.suitable >= std::size(GRAPH_EFFECT_SUITABLE_CONTEXTS)) {
				out.error = "has Suitable Contexts that aren't one of its options";
			}
		}

		str EventNameOf(const GraphNode& event) {
			return FindSetting(event.settings, "Event Name") ? SettingString(event.settings, "Event Name") : str(GRAPH_EFFECT_ON_PLAY);
		}

		bool Linked(const GraphContext& context, std::string_view slot) {
			return std::any_of(context.flows.begin(), context.flows.end(), [slot](const GraphFlow& flow) { return flow.slot == slot; });
		}

		void BuildSystemFlow(GraphSubgraph& out, GraphDefinitions& resolver) {
			bool start = false;
			bool stop = false;
			Vector<str> direct{GetMemoryResource(MemTag::Editor)};
			Vector<str> nested{GetMemoryResource(MemTag::Editor)};
			for (const GraphContext& context : out.document.contexts) {
				if (context.node.def == GRAPH_EFFECT_SPAWN) {
					start = start || !Linked(context, GRAPH_EFFECT_START);
					stop = stop || !Linked(context, GRAPH_EFFECT_STOP);
				} else if (context.node.def == GRAPH_EFFECT_EVENT) {
					direct.push_back(EventNameOf(context.node));
				} else if (context.node.def == GRAPH_EFFECT_SYSTEM_SUBGRAPH) {
					const Guid asset = GraphSubgraphs::AssetOf(context.node);
					const GraphSubgraph* inner = asset.IsValid() ? resolver.Subgraph(asset) : nullptr;
					if (!inner || inner->kind != GraphSubgraphKind::EffectSystem || inner->definition.subgraph == nullptr) {
						continue;
					}
					for (const std::string_view slot : inner->flowSlots) {
						if (Linked(context, slot)) {
							continue;
						}
						start = start || slot == GRAPH_EFFECT_ON_PLAY;
						stop = stop || slot == GRAPH_EFFECT_ON_STOP;
						if (slot != GRAPH_EFFECT_ON_PLAY && slot != GRAPH_EFFECT_ON_STOP) {
							nested.push_back(str(slot));
						}
					}
				}
			}
			std::sort(direct.begin(), direct.end());
			std::sort(nested.begin(), nested.end());
			const auto has = [&direct](std::string_view name) { return std::find(direct.begin(), direct.end(), name) != direct.end(); };
			Vector<str> slots{GetMemoryResource(MemTag::Editor)};
			if (start || has(GRAPH_EFFECT_ON_PLAY)) {
				slots.push_back(str(GRAPH_EFFECT_ON_PLAY));
			}
			if (stop || has(GRAPH_EFFECT_ON_STOP)) {
				slots.push_back(str(GRAPH_EFFECT_ON_STOP));
			}
			for (const Vector<str>* list : {&direct, &nested}) {
				for (const str& name : *list) {
					if (std::find(slots.begin(), slots.end(), name) == slots.end()) {
						slots.push_back(name);
					}
				}
			}
			constexpr size_t MAX_SLOTS = 10;
			if (slots.size() > MAX_SLOTS) {
				out.error = std::format("takes {} events, past the {} a context's flow slots show", slots.size(), MAX_SLOTS);
				return;
			}
			for (str& slot : slots) {
				out.flowSlots.push_back(out.strings.emplace_back(std::move(slot)));
			}
		}

		void Build(GraphSubgraph& out, GraphDefinitions& resolver) {
			const GraphDocument& document = out.document;
			if (!GraphSubgraphs::KindOf(document.type, out.kind)) {
				out.error = "is a " + document.type + " document, not a subgraph";
				return;
			}
			out.category = SettingString(document.settings, GRAPH_SUBGRAPH_CATEGORY_SETTING);
			if (out.category.empty()) {
				out.category = str(GraphSubgraphs::DefaultCategory(out.kind));
			}

			for (const GraphProperty& property : document.properties) {
				GraphPropertyInfo info;
				str error;
				if (!GraphProperties::Parse(property, info, error)) {
					out.error = "has blackboard property '" + property.name + "' (" + error + ")";
					return;
				}
				if (GraphProperties::IsKeyword(info.kind)) {
					continue;
				}
				if (GraphSubgraphs::IsOutput(property)) {
					if (out.kind != GraphSubgraphKind::EffectOperator) {
						out.error = "has an Output property '" + property.name + "' (only an operator subgraph has one)";
						return;
					}
					continue;
				}
				GraphPortDefinition port;
				port.name = out.strings.emplace_back(std::to_string(property.id));
				port.label = out.strings.emplace_back(property.name);
				port.type = SlotTypeOf(info.kind);
				for (u32 i = 0; i < 4; ++i) {
					port.defaultValue[i] = info.defaultValue[i];
				}
				if (info.customBinding && !info.bindingLabel.empty()) {
					port.customBinding = out.strings.emplace_back(info.bindingLabel);
				}
				out.inputs.push_back(port);
				out.inputProperties.push_back(property.id);
				out.inputKinds.push_back(info.kind);
			}

			switch (out.kind) {
			case GraphSubgraphKind::Shader:
				BuildOutputStack(out);
				break;
			case GraphSubgraphKind::EffectOperator:
				BuildOutputProperties(out);
				break;
			case GraphSubgraphKind::EffectBlock:
				BuildBlockContext(out);
				break;
			case GraphSubgraphKind::EffectSystem:
				BuildSystemFlow(out, resolver);
				break;
			}
			if (!out.error.empty()) {
				return;
			}

			GraphNodeDefinition& definition = out.definition;
			definition.id = out.kind == GraphSubgraphKind::EffectBlock	  ? GRAPH_EFFECT_SUBGRAPH_BLOCK
							: out.kind == GraphSubgraphKind::EffectSystem ? GRAPH_EFFECT_SYSTEM_SUBGRAPH
																		  : GRAPH_SUBGRAPH_NODE;
			definition.name = out.name;
			definition.category = out.category;
			definition.inputs = out.inputs;
			definition.outputs = out.outputs;
			definition.flowInputs = out.flowSlots;
			definition.subgraph = &out;
		}

		constexpr bool PortTypesAreOutputTypes() {
			for (size_t i = 0; i < std::size(GRAPH_CUSTOM_FUNCTION_PORT_TYPES); ++i) {
				if (GRAPH_CUSTOM_FUNCTION_PORT_TYPES[i] != GRAPH_SUBGRAPH_OUTPUT_TYPES[i]) {
					return false;
				}
			}
			return true;
		}
		static_assert(PortTypesAreOutputTypes(), "a custom function's port types are a subgraph output's first 8");

		constexpr GraphSettingDefinition CUSTOM_FUNCTION_SETTINGS[] = {
			{GRAPH_CUSTOM_FUNCTION_TYPE, GraphSettingKind::Choice, GRAPH_CUSTOM_FUNCTION_SOURCE_TYPES},
			{GRAPH_CUSTOM_FUNCTION_NAME, GraphSettingKind::Text},
			{GRAPH_CUSTOM_FUNCTION_SOURCE, GraphSettingKind::Text},
			{GRAPH_CUSTOM_FUNCTION_BODY, GraphSettingKind::Text},
		};
		enum : u32 { SETTING_TYPE,
					 SETTING_NAME,
					 SETTING_SOURCE,
					 SETTING_BODY };

		constexpr std::string_view HLSL_KEYWORDS[] = {
			"AppendStructuredBuffer", "asm", "asm_fragment", "auto", "BlendState", "break", "Buffer", "ByteAddressBuffer", "case", "catch",
			"cbuffer", "centroid", "char", "class", "column_major", "compile", "compile_fragment", "CompileShader", "const", "const_cast",
			"continue", "ComputeShader", "ConsumeStructuredBuffer", "default", "delete", "DepthStencilState", "DepthStencilView", "discard",
			"do", "double", "DomainShader", "dynamic_cast", "dword", "else", "enum", "explicit", "export", "extern", "false", "for", "friend",
			"fxgroup", "GeometryShader", "goto", "groupshared", "half", "Hullshader", "if", "in", "inline", "inout", "InputPatch", "interface",
			"line", "lineadj", "linear", "LineStream", "long", "matrix", "mutable", "namespace", "new", "nointerpolation", "noperspective",
			"NULL", "operator", "out", "OutputPatch", "packoffset", "pass", "pixelfragment", "PixelShader", "point", "PointStream", "precise",
			"private", "protected", "public", "RasterizerState", "reinterpret_cast", "RenderTargetView", "return", "register", "row_major",
			"RWBuffer", "RWByteAddressBuffer", "RWStructuredBuffer", "RWTexture1D", "RWTexture1DArray", "RWTexture2D", "RWTexture2DArray",
			"RWTexture3D", "sample", "sampler", "SamplerState", "SamplerComparisonState", "shared", "short", "signed", "sizeof", "snorm",
			"stateblock", "stateblock_state", "static", "static_cast", "string", "struct", "switch", "StructuredBuffer", "tbuffer", "technique",
			"technique10", "technique11", "template", "texture", "Texture1D", "Texture1DArray", "Texture2D", "Texture2DArray", "Texture2DMS",
			"Texture2DMSArray", "Texture3D", "TextureCube", "TextureCubeArray", "this", "throw", "true", "try", "typedef", "typename",
			"triangle", "triangleadj", "TriangleStream", "uniform", "unorm", "union", "unsigned", "using", "vector", "vertexfragment",
			"VertexShader", "virtual", "void", "volatile", "while"};
		constexpr std::string_view HLSL_NUMERIC_TYPES[] = {"float", "half", "real", "int", "uint", "bool",
														   "min10float", "min16float", "min12int", "min16int", "min16uint"};
		constexpr std::string_view HLSL_NUMERIC_SUFFIXES[] = {"", "1", "2", "3", "4", "1x1", "1x2", "1x3", "1x4", "2x1", "2x2",
															  "2x3", "2x4", "3x1", "3x2", "3x3", "3x4", "4x1", "4x2", "4x3", "4x4"};

		bool IsHLSLKeyword(std::string_view id) {
			for (std::string_view keyword : HLSL_KEYWORDS) {
				if (keyword == id) {
					return true;
				}
			}
			for (std::string_view type : HLSL_NUMERIC_TYPES) {
				if (id.starts_with(type)) {
					const std::string_view rest = id.substr(type.size());
					for (std::string_view suffix : HLSL_NUMERIC_SUFFIXES) {
						if (rest == suffix) {
							return true;
						}
					}
				}
			}
			return false;
		}

		bool IsWordCharacter(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9'); }

		GraphValueType ValueTypeOf(GraphPortType type) {
			switch (type) {
			case P::Float2:
				return GraphValueType::Float2;
			case P::Float3:
				return GraphValueType::Float3;
			case P::Float4:
				return GraphValueType::Float4;
			case P::Boolean:
				return GraphValueType::Bool;
			case P::Float2x2:
				return GraphValueType::Float2x2;
			case P::Float3x3:
				return GraphValueType::Float3x3;
			case P::Float4x4:
				return GraphValueType::Float4x4;
			default:
				return GraphValueType::Float;
			}
		}

		bool IncludeOf(std::string_view source, str& out, str& error) {
			const Guid asset = source.empty() ? Guid{} : Guid::FromString(source);
			const Path path = asset.IsValid() ? AssetDatabase::LookupPath(asset) : Path();
			if (path.ToString().empty()) {
				error = source.empty() ? "has no source file" : "uses a source file the project doesn't have";
				return false;
			}
			const str extension = StringUtil::ToLower(fs::path(path.ToString()).extension().generic_string());
			if (std::find(std::begin(GRAPH_CUSTOM_FUNCTION_EXTENSIONS), std::end(GRAPH_CUSTOM_FUNCTION_EXTENSIONS), extension) ==
				std::end(GRAPH_CUSTOM_FUNCTION_EXTENSIONS)) {
				error = "uses '" + path.ToString() + "' (not an .hlsl file)";
				return false;
			}
			const fs::path file(FileSystem::Resolve(path).ToString());
			const fs::path directory(FileSystem::Resolve(Path(str(GRAPH_GENERATED_DIRECTORY))).ToString());
			const fs::path relative = file.lexically_relative(directory);
			out = relative.empty() ? file.generic_string() : relative.generic_string();
			return true;
		}

		str Definition(std::string_view function, const GraphNodeDefinition& definition, std::string_view body) {
			str out = "void " + str(function) + "(";
			bool first = true;
			auto parameter = [&](const GraphPortDefinition& port, bool output) {
				out += first ? "" : ", ";
				out += output ? "out " : "";
				out += str(GraphTypes::HLSLName(ValueTypeOf(port.type))) + " " + str(GraphPortLabel(port));
				first = false;
			};
			for (const GraphPortDefinition& port : definition.inputs) {
				parameter(port, false);
			}
			for (const GraphPortDefinition& port : definition.outputs) {
				parameter(port, true);
			}
			out += ")\n{\n";
			constexpr std::string_view TOKEN = "$precision";
			for (size_t at = 0; at < body.size();) {
				const size_t found = body.find(TOKEN, at);
				out += body.substr(at, found == std::string_view::npos ? std::string_view::npos : found - at);
				if (found == std::string_view::npos) {
					break;
				}
				out += "float";
				at = found + TOKEN.size();
			}
			if (!body.empty() && body.back() != '\n') {
				out += "\n";
			}
			return out + "}\n\n";
		}

		void CustomFunctionBody(GraphBodyContext& c) {
			const GraphNodeDefinition& definition = *c.definition;
			const std::string_view name = c.Text(SETTING_NAME);
			if (name.empty()) {
				c.error = "has no function name";
				return;
			}
			if (GraphCustomFunctions::Identifier(name) != name) {
				c.error = "has function name '" + str(name) + "' (not an HLSL name)";
				return;
			}
			GraphCall call;
			call.function = str(name) + "_float";
			for (const GraphPortDefinition& port : definition.outputs) {
				call.outputs.push_back(ValueTypeOf(port.type));
			}
			if (c.Setting(SETTING_TYPE) == 0) {
				if (!IncludeOf(c.Text(SETTING_SOURCE), call.include, c.error)) {
					return;
				}
			} else {
				if (c.Text(SETTING_BODY).empty()) {
					c.error = "has no body";
					return;
				}
				call.definition = Definition(call.function, definition, c.Text(SETTING_BODY));
			}
			c.builder.Call(std::move(call), c.inputs, c.outputs);
		}

		void BuildCustomFunction(const GraphNode& node, GraphBuiltDefinition& out) {
			Vector<GraphCustomFunctionPort> inputs{GetMemoryResource(MemTag::Editor)};
			Vector<GraphCustomFunctionPort> outputs{GetMemoryResource(MemTag::Editor)};
			str error;
			if (!GraphCustomFunctions::ReadPorts(node, GRAPH_CUSTOM_FUNCTION_INPUTS, inputs, error) ||
				!GraphCustomFunctions::ReadPorts(node, GRAPH_CUSTOM_FUNCTION_OUTPUTS, outputs, error)) {
				out.error = std::move(error);
				return;
			}
			Vector<std::string_view> names{GetMemoryResource(MemTag::Editor)};
			Vector<u32> ids{GetMemoryResource(MemTag::Editor)};
			for (Vector<GraphCustomFunctionPort>* list : {&inputs, &outputs}) {
				Vector<GraphPortDefinition>& ports = list == &inputs ? out.inputs : out.outputs;
				for (const GraphCustomFunctionPort& entry : *list) {
					if (GraphCustomFunctions::Identifier(entry.name) != entry.name) {
						out.error = "has a port named '" + entry.name + "' (not an HLSL name)";
						return;
					}
					if (std::find(names.begin(), names.end(), std::string_view(entry.name)) != names.end()) {
						out.error = "has 2 ports named '" + entry.name + "'";
						return;
					}
					if (std::find(ids.begin(), ids.end(), entry.id) != ids.end()) {
						out.error = "has 2 ports with the id " + std::to_string(entry.id);
						return;
					}
					names.push_back(entry.name);
					ids.push_back(entry.id);
					GraphPortDefinition port;
					port.name = out.strings.emplace_back(std::to_string(entry.id));
					port.label = out.strings.emplace_back(entry.name);
					port.type = OUTPUT_PORT_TYPES[entry.type];
					ports.push_back(port);
				}
			}
			const str function = SettingString(node.settings, GRAPH_CUSTOM_FUNCTION_NAME);
			GraphNodeDefinition& definition = out.definition;
			definition.id = GRAPH_CUSTOM_FUNCTION_NODE;
			definition.name = out.strings.emplace_back(function.empty() ? str("Custom Function") : function + " (Custom Function)");
			definition.category = "Utility";
			definition.synonyms = "code HLSL";
			definition.inputs = out.inputs;
			definition.outputs = out.outputs;
			definition.settings = CUSTOM_FUNCTION_SETTINGS;
			definition.body = CustomFunctionBody;
		}

		constexpr std::string_view KEYWORD_NODE = "Utility/Keyword";

		void BuildEnumKeyword(const GraphPropertyInfo& info, GraphBuiltDefinition& out) {
			const GraphNodeDefinition* table = GraphNodeLibrary::Find(KEYWORD_NODE);
			for (size_t i = 0; i < info.entries.size(); ++i) {
				GraphPortDefinition port;
				port.name = out.strings.emplace_back(std::to_string(info.entries[i].id));
				port.label = out.strings.emplace_back(info.entries[i].name);
				port.keyword = out.strings.emplace_back(GraphProperties::EntryKeyword(info, i));
				out.inputs.push_back(port);
			}
			out.definition = *table;
			out.definition.inputs = out.inputs;
		}

	} // anonymous namespace

	std::span<const GraphNodeDefinition> GraphSubgraphDefinitions() { return SUBGRAPH_DEFINITIONS; }

	GraphDefinitions::GraphDefinitions() = default;
	GraphDefinitions::~GraphDefinitions() = default;

	const GraphNodeDefinition* GraphDefinitions::Find(const GraphDocument& document, const GraphNode& node, str* error) {
		if (node.def == KEYWORD_NODE) {
			const GraphSetting* setting = FindSetting(node.settings, GRAPH_PROPERTY_SETTING);
			const u32 id = setting ? static_cast<u32>(std::strtoul(setting->value.c_str(), nullptr, 10)) : 0;
			for (const GraphProperty& property : document.properties) {
				if (property.id == id && id != 0 && property.type == "EnumKeyword") {
					return EnumKeyword(property, error);
				}
			}
		}
		if (node.def == GRAPH_SUBGRAPH_NODE || node.def == GRAPH_EFFECT_SUBGRAPH_BLOCK || node.def == GRAPH_EFFECT_SYSTEM_SUBGRAPH) {
			return UsedSubgraph(document, node, error);
		}
		if (node.def == GRAPH_PROPERTY_NODE) {
			const u32 id = PropertyIdOf(node);
			for (const GraphProperty& property : document.properties) {
				if (property.id == id && id != 0 && GraphSubgraphs::IsOutput(property)) {
					return OutputProperty(property, error);
				}
			}
		}
		if (node.def == GRAPH_CUSTOM_FUNCTION_NODE) {
			return CustomFunction(node, error);
		}
		if (node.def == GRAPH_EFFECT_CUSTOM_HLSL_BLOCK || node.def == GRAPH_EFFECT_CUSTOM_HLSL_OPERATOR) {
			return CustomHLSL(node);
		}
		if (GraphEffectShading::IsNodeShaderOutput(node.def)) {
			return NodeShaderOutput(node, error);
		}
		if (GraphEffectAttributes::BuildsPerNode(node.def)) {
			return EffectNode(node, error);
		}
		if (node.def == GRAPH_SUBGRAPH_OUTPUT_BLOCK) {
			const GraphNodeDefinition* definition = GraphSubgraphs::OutputBlockDefinition(GraphSubgraphs::OutputTypeOf(node));
			if (!definition && error) {
				*error = "is an output with a Type that isn't one of its options";
			}
			return definition;
		}
		const GraphNodeDefinition* definition = GraphNodeLibrary::Find(node.def);
		if (!definition && error) {
			*error = "has no definition '" + node.def + "' (unchanged, compiles nothing)";
		}
		return definition;
	}

	const GraphSubgraph* GraphDefinitions::Subgraph(const Guid& asset, str* error) {
		for (const GraphSubgraph& known : _subgraphs) {
			if (known.asset == asset) {
				if (!known.error.empty() && error) {
					*error = "uses subgraph '" + (known.name.empty() ? asset.ToString() : known.name) + "', which " + known.error;
				}
				return known.error.empty() ? &known : nullptr;
			}
		}

		GraphSubgraph& subgraph = _subgraphs.emplace_back();
		if (ReadDocument(asset, subgraph)) {
			Build(subgraph, *this);
		}
		if (!subgraph.error.empty()) {
			if (error) {
				*error = subgraph.name.empty() ? "uses subgraph " + asset.ToString() + ", which " + subgraph.error
											   : "uses subgraph '" + subgraph.name + "', which " + subgraph.error;
			}
			return nullptr;
		}
		return &subgraph;
	}

	const GraphReadDocument* GraphDefinitions::NodeShader(const Guid& asset, str* error) {
		const GraphReadDocument* read = nullptr;
		for (const GraphReadDocument& known : _nodeShaders) {
			read = known.asset == asset ? &known : read;
		}
		if (!read) {
			GraphReadDocument& made = _nodeShaders.emplace_back();
			ReadDocument(asset, made);
			read = &made;
		}
		if (!read->error.empty()) {
			if (error) {
				*error = read->name.empty() ? "uses node shader " + asset.ToString() + ", which " + read->error
											: "uses node shader '" + read->name + "', which " + read->error;
			}
			return nullptr;
		}
		return read;
	}

	const GraphReadDocument& GraphDefinitions::DefaultNodeShader() {
		if (_defaultNodeShader.empty()) {
			GraphReadDocument& made = _defaultNodeShader.emplace_back();
			made.name = str(GraphEffectShading::DEFAULT_NAME);
			str error;
			if (!GraphDocumentOps::Read(GraphEffectShading::DefaultDocumentText(), made.document, error)) {
				made.error = "is a built-in the editor couldn't read: " + error;
			}
		}
		return _defaultNodeShader.front();
	}

	const GraphReadText* GraphDefinitions::ShaderFile(const Guid& asset, str* error) {
		const GraphReadText* read = nullptr;
		for (const GraphReadText& known : _shaderFiles) {
			read = known.asset == asset ? &known : read;
		}
		if (!read) {
			GraphReadText& made = _shaderFiles.emplace_back();
			made.asset = asset;
			made.path = AssetDatabase::LookupPath(asset);
			if (made.path.ToString().empty()) {
				made.error = "the project doesn't have";
			} else {
				made.name = str(fs::path(made.path.ToString()).stem().generic_string());
				made.physicalPath = FileSystem::Resolve(made.path).ToString();
				const str extension = StringUtil::ToLower(fs::path(made.path.ToString()).extension().generic_string());
				if (std::find(std::begin(GRAPH_CUSTOM_HLSL_EXTENSIONS), std::end(GRAPH_CUSTOM_HLSL_EXTENSIONS), extension) ==
					std::end(GRAPH_CUSTOM_HLSL_EXTENSIONS)) {
					made.error = "isn't an .hlsl file";
				} else {
					made.text = FileSystem::ReadTextFile(made.path, MemTag::Editor);
					made.contentHash = StringHash(std::string_view(made.text)).GetValue();
					made.error = made.text.empty() ? "is empty or unreadable" : "";
				}
			}
			read = &made;
		}
		if (!read->error.empty()) {
			if (error) {
				*error = read->name.empty() ? "uses shader file " + asset.ToString() + ", which " + read->error
											: "uses shader file '" + read->name + "', which " + read->error;
			}
			return nullptr;
		}
		return read;
	}

	void GraphDefinitions::Read(Vector<const GraphReadDocument*>& out) const {
		out.clear();
		for (const GraphSubgraph& subgraph : _subgraphs) {
			out.push_back(&subgraph);
		}
		for (const GraphReadDocument& graph : _nodeShaders) {
			out.push_back(&graph);
		}
		for (const GraphReadText& file : _shaderFiles) {
			out.push_back(&file);
		}
	}

	const GraphNodeDefinition* GraphDefinitions::NodeShaderOutput(const GraphNode& node, str*) {
		Guid asset;
		const GraphEffectShading::Held held = GraphEffectShading::HeldNodeShader(node, asset);
		const GraphDocument* graph = nullptr;
		str key = node.def + "\n";
		if (held == GraphEffectShading::Held::Default) {
			graph = &DefaultNodeShader().document;
			key += "default";
		} else if (const GraphReadDocument* read = held == GraphEffectShading::Held::Asset ? NodeShader(asset) : nullptr) {
			graph = &read->document;
			key += asset.ToString() + "\n" + std::to_string(read->contentHash);
		} else {
			key += "none";
		}
		bool made = false;
		GraphBuiltDefinition& found = Built(std::move(key), made);
		if (made) {
			GraphEffectShading::Build(node, graph, found);
		}
		return &found.definition;
	}

	const GraphNodeDefinition* GraphDefinitions::UsedSubgraph(const GraphDocument& document, const GraphNode& node, str* error) {
		const Guid asset = GraphSubgraphs::AssetOf(node);
		if (!asset.IsValid()) {
			if (error) {
				*error = "has no subgraph";
			}
			return nullptr;
		}
		const GraphSubgraph* subgraph = Subgraph(asset, error);
		if (!subgraph) {
			return nullptr;
		}
		const GraphSubgraphKind wanted = node.def == GRAPH_EFFECT_SUBGRAPH_BLOCK	? GraphSubgraphKind::EffectBlock
										 : node.def == GRAPH_EFFECT_SYSTEM_SUBGRAPH ? GraphSubgraphKind::EffectSystem
										 : IsEffectDocumentType(document.type)		? GraphSubgraphKind::EffectOperator
																					: GraphSubgraphKind::Shader;
		if (subgraph->kind != wanted) {
			constexpr std::string_view KINDS[] = {"a shader subgraph", "an effect operator", "an effect block",
												  "an effect graph"};
			if (error) {
				*error = "uses '" + subgraph->name + "', which is " + str(KINDS[static_cast<u32>(subgraph->kind)]) + ", where it takes " +
						 str(KINDS[static_cast<u32>(wanted)]);
			}
			return nullptr;
		}
		return &subgraph->definition;
	}

	const GraphNodeDefinition* GraphDefinitions::OutputProperty(const GraphProperty& property, str* error) {
		GraphPropertyInfo info;
		str refused;
		if (!GraphProperties::Parse(property, info, refused)) {
			if (error) {
				*error = "reads property '" + property.name + "', which " + refused;
			}
			return nullptr;
		}
		bool made = false;
		GraphBuiltDefinition& found =
			Built(str(GRAPH_PROPERTY_NODE) + "\noutput\n" + std::to_string(property.id) + "\n" + property.type + "\n" + property.defaultValue, made);
		if (made) {
			GraphPortDefinition port;
			port.name = GRAPH_SUBGRAPH_OUTPUT_PORT;
			port.type = SlotTypeOf(info.kind);
			for (u32 i = 0; i < 4; ++i) {
				port.defaultValue[i] = info.defaultValue[i];
			}
			found.inputs.push_back(port);
			found.definition = *GraphNodeLibrary::Find(GRAPH_PROPERTY_NODE);
			found.definition.inputs = found.inputs;
			found.definition.outputs = {};
			found.definition.body = nullptr;
		}
		return &found.definition;
	}

	const GraphNodeDefinition* GraphDefinitions::CustomFunction(const GraphNode& node, str* error) {
		str key;
		for (std::string_view part : {GRAPH_CUSTOM_FUNCTION_NAME, GRAPH_CUSTOM_FUNCTION_INPUTS, GRAPH_CUSTOM_FUNCTION_OUTPUTS}) {
			const GraphSetting* setting = FindSetting(node.settings, part);
			key += (setting ? setting->value : str()) + "\n";
		}
		bool made = false;
		GraphBuiltDefinition& found = Built(str(GRAPH_CUSTOM_FUNCTION_NODE) + "\n" + key, made);
		if (made) {
			BuildCustomFunction(node, found);
		}
		if (!found.error.empty() && error) {
			*error = found.error;
		}
		return found.error.empty() ? &found.definition : nullptr;
	}

	const GraphNodeDefinition* GraphDefinitions::CustomHLSL(const GraphNode& node) {
		const bool block = node.def == GRAPH_EFFECT_CUSTOM_HLSL_BLOCK;
		str key = node.def + "\n" + SettingString(node.settings, block ? GRAPH_CUSTOM_HLSL_BLOCK_NAME : GRAPH_CUSTOM_HLSL_OPERATOR_NAME) + "\n" +
				  SettingString(node.settings, GRAPH_CUSTOM_HLSL_FUNCTION) + "\n";
		const str held = SettingString(node.settings, GRAPH_CUSTOM_HLSL_FILE);
		const Guid asset = held.empty() ? Guid{} : Guid::FromString(held);
		str fileError;
		const GraphReadText* file = held.empty() ? nullptr : ShaderFile(asset, &fileError);
		str code;
		if (!held.empty()) {
			key += held + "\n" + (file ? std::to_string(file->contentHash) : str("missing"));
		} else {
			const GraphSetting* stored = FindSetting(node.settings, GRAPH_CUSTOM_HLSL_CODE);
			code = stored ? SettingString(node.settings, GRAPH_CUSTOM_HLSL_CODE)
						  : str(block ? GraphCustomHLSL::DEFAULT_BLOCK_CODE : GraphCustomHLSL::DEFAULT_OPERATOR_CODE);
			key += "code\n" + code;
		}
		bool made = false;
		GraphBuiltDefinition& found = Built(std::move(key), made);
		if (made) {
			GraphCustomHLSL::Build(node, file ? std::string_view(file->text) : std::string_view(code), file ? std::string_view(file->physicalPath) : std::string_view(),
								   found);
			if (!held.empty() && !file) {
				found.customHLSL.error = fileError;
			}
		}
		return &found.definition;
	}

	const GraphNodeDefinition* GraphDefinitions::EnumKeyword(const GraphProperty& property, str* error) {
		GraphPropertyInfo info;
		str refused;
		if (!GraphProperties::Parse(property, info, refused)) {
			if (error) {
				*error = "reads keyword '" + property.name + "', which " + refused;
			}
			return nullptr;
		}
		bool made = false;
		GraphBuiltDefinition& found = Built(str(KEYWORD_NODE) + "\n" + info.reference + "\n" + GraphProperties::WriteEntries(info.entries), made);
		if (made) {
			BuildEnumKeyword(info, found);
		}
		return &found.definition;
	}

	const GraphNodeDefinition* GraphDefinitions::EffectNode(const GraphNode& node, str* error) {
		const GraphNodeDefinition* table = GraphNodeLibrary::Find(node.def);
		str key(node.def);
		for (const GraphSettingDefinition& part : table ? table->settings : std::span<const GraphSettingDefinition>()) {
			const GraphSetting* setting = FindSetting(node.settings, part.key);
			key += "\n" + (setting ? setting->value : str());
		}
		bool made = false;
		GraphBuiltDefinition& found = Built(std::move(key), made);
		if (made) {
			GraphEffectAttributes::Build(node, found);
		}
		if (!found.error.empty() && error) {
			*error = found.error;
		}
		return found.error.empty() ? &found.definition : nullptr;
	}

	GraphBuiltDefinition& GraphDefinitions::Built(str key, bool& made) {
		for (GraphBuiltDefinition& known : _built) {
			if (known.key == key) {
				made = false;
				return known;
			}
		}
		GraphBuiltDefinition& built = _built.emplace_back();
		built.key = std::move(key);
		made = true;
		return built;
	}

	void GraphDefinitions::Clear() {
		_subgraphs.clear();
		_nodeShaders.clear();
		_shaderFiles.clear();
		_defaultNodeShader.clear();
		_built.clear();
	}

	namespace GraphSubgraphs {

		Guid AssetOf(const GraphNode& node) {
			if (node.def != GRAPH_SUBGRAPH_NODE && node.def != GRAPH_EFFECT_SUBGRAPH_BLOCK && node.def != GRAPH_EFFECT_SYSTEM_SUBGRAPH) {
				return Guid{};
			}
			const str text = SettingString(node.settings, GRAPH_SUBGRAPH_ASSET_SETTING);
			return text.empty() ? Guid{} : Guid::FromString(text);
		}

		namespace {
			constexpr std::string_view KIND_TYPES[] = {GRAPH_SUBGRAPH_DOCUMENT_TYPE, GRAPH_EFFECT_OPERATOR_SUBGRAPH_DOCUMENT_TYPE,
													   GRAPH_EFFECT_BLOCK_SUBGRAPH_DOCUMENT_TYPE, GRAPH_EFFECT_DOCUMENT_TYPE};
			constexpr std::string_view KIND_EXTENSIONS[] = {GRAPH_SUBGRAPH_EXTENSION, GRAPH_EFFECT_OPERATOR_SUBGRAPH_EXTENSION,
															GRAPH_EFFECT_BLOCK_SUBGRAPH_EXTENSION, GRAPH_EFFECT_EXTENSION};
			constexpr std::string_view KIND_CATEGORIES[] = {GRAPH_SUBGRAPH_DEFAULT_CATEGORY, GRAPH_EFFECT_OPERATOR_SUBGRAPH_CATEGORY,
															GRAPH_EFFECT_BLOCK_SUBGRAPH_CATEGORY, "System Subgraph"};
			static_assert(std::size(KIND_TYPES) == static_cast<size_t>(GraphSubgraphKind::EffectSystem) + 1, "a document type per subgraph kind");
		} // anonymous namespace

		bool KindOf(std::string_view type, GraphSubgraphKind& out) {
			for (u32 kind = 0; kind < std::size(KIND_TYPES); ++kind) {
				if (KIND_TYPES[kind] == type) {
					out = static_cast<GraphSubgraphKind>(kind);
					return true;
				}
			}
			return false;
		}

		std::string_view DocumentType(GraphSubgraphKind kind) { return KIND_TYPES[static_cast<u32>(kind)]; }

		std::string_view Extension(GraphSubgraphKind kind) { return KIND_EXTENSIONS[static_cast<u32>(kind)]; }

		std::string_view DefaultCategory(GraphSubgraphKind kind) { return KIND_CATEGORIES[static_cast<u32>(kind)]; }

		bool IsOutput(const GraphProperty& property) {
			for (const GraphSetting& setting : property.settings) {
				if (setting.key == GRAPH_PROPERTY_OUTPUT_SETTING) {
					return setting.value == "true";
				}
			}
			return false;
		}

		u32 BlockContext(const GraphDocument& document) {
			for (const GraphContext& context : document.contexts) {
				if (context.node.def == GRAPH_EFFECT_BLOCK_SUBGRAPH_CONTEXT) {
					return context.node.id;
				}
			}
			return 0;
		}

		namespace {
			bool UsesFrom(GraphDefinitions& definitions, const GraphDocument& document, const Guid& target, u32 depth) {
				if (depth > 32) {
					return false;
				}
				const auto uses = [&](const GraphNode& node) {
					const Guid used = AssetOf(node);
					if (!used.IsValid()) {
						return false;
					}
					if (used == target) {
						return true;
					}
					const GraphSubgraph* inner = definitions.Subgraph(used);
					return inner != nullptr && UsesFrom(definitions, inner->document, target, depth + 1);
				};
				for (const GraphNode& node : document.nodes) {
					if (uses(node)) {
						return true;
					}
				}
				for (const GraphContext& context : document.contexts) {
					if (uses(context.node)) {
						return true;
					}
					for (const GraphNode& block : context.blocks) {
						if (uses(block)) {
							return true;
						}
					}
				}
				return false;
			}
		} // anonymous namespace

		bool Uses(GraphDefinitions& definitions, const GraphDocument& document, const Guid& target) { return UsesFrom(definitions, document, target, 0); }

		str NameOf(const Guid& asset) {
			const Path path = AssetDatabase::LookupPath(asset);
			return path.ToString().empty() ? asset.ToString() : str(fs::path(path.ToString()).stem().generic_string());
		}

		u32 OutputTypeOf(const GraphNode& block) {
			const str type = SettingString(block.settings, "Type");
			if (type.empty() && FindSetting(block.settings, "Type") == nullptr) {
				return GRAPH_SUBGRAPH_DEFAULT_OUTPUT_TYPE;
			}
			for (u32 i = 0; i < std::size(GRAPH_SUBGRAPH_OUTPUT_TYPES); ++i) {
				if (GRAPH_SUBGRAPH_OUTPUT_TYPES[i] == type) {
					return i;
				}
			}
			return static_cast<u32>(std::size(GRAPH_SUBGRAPH_OUTPUT_TYPES));
		}

		str OutputNameOf(const GraphNode& block) { return SettingString(block.settings, "Name"); }

		GraphPortType InputPortType(GraphPropertyKind kind) { return SlotTypeOf(kind); }

		const GraphNodeDefinition* OutputBlockDefinition(u32 type) { return type < OUTPUT_BLOCKS.size() ? &OUTPUT_BLOCKS[type] : nullptr; }

		u32 OutputContext(const GraphDocument& document) {
			for (const GraphContext& context : document.contexts) {
				if (context.node.def == GRAPH_SUBGRAPH_OUTPUT_CONTEXT) {
					return context.node.id;
				}
			}
			return 0;
		}

		void ProjectSubgraphs(Vector<Guid>& out) { ProjectFiles(GRAPH_SUBGRAPH_EXTENSION, out); }

		void ProjectFiles(std::string_view extension, Vector<Guid>& out) {
			out.clear();
			Vector<std::pair<str, Guid>> found{GetMemoryResource(MemTag::Editor)};
			for (std::string_view root : ProjectLayout::GetContentRoots()) {
				const str prefix = str("project://") + str(root) + "/";
				const Path physical = FileSystem::Resolve(Path(str("project://") + str(root)));
				std::error_code ec;
				if (physical.ToString().empty() || !fs::is_directory(fs::path(physical.ToString()), ec)) {
					continue;
				}
				const fs::path base(physical.ToString());
				for (fs::recursive_directory_iterator it(base, fs::directory_options::skip_permission_denied, ec), end; !ec && it != end;
					 it.increment(ec)) {
					if (!it->is_regular_file(ec) || ec || StringUtil::ToLower(it->path().extension().generic_string()) != extension) {
						continue;
					}
					const str relative = fs::relative(it->path(), base, ec).generic_string();
					if (ec || relative.empty()) {
						continue;
					}
					const str virtualPath = prefix + relative;
					const Guid guid = AssetDatabase::LookupGuid(Path(virtualPath));
					if (guid.IsValid()) {
						found.emplace_back(virtualPath, guid);
					}
				}
			}
			std::sort(found.begin(), found.end(), [](const auto& a, const auto& b) { return a.first < b.first; });
			for (const auto& [path, guid] : found) {
				out.push_back(guid);
			}
		}

		namespace {

			void CollectFrom(const GraphDocument& document, GraphDefinitions& definitions, Vector<GraphPropertyInfo>& properties,
							 Vector<GraphDiagnostic>& diagnostics, Vector<Guid>& visited, u32 attribute) {
				for (const GraphNode& node : document.nodes) {
					const Guid asset = AssetOf(node);
					if (!asset.IsValid() || std::find(visited.begin(), visited.end(), asset) != visited.end()) {
						continue;
					}
					const GraphSubgraph* subgraph = definitions.Subgraph(asset);
					if (subgraph == nullptr) {
						continue;
					}
					visited.push_back(asset);
					const u32 user = attribute != 0 ? attribute : node.id;
					for (const GraphProperty& property : subgraph->document.properties) {
						GraphPropertyInfo info;
						str error;
						if (!GraphProperties::Parse(property, info, error) || !GraphProperties::IsKeyword(info.kind)) {
							continue;
						}
						bool known = false;
						bool clashed = false;
						for (const GraphPropertyInfo& held : properties) {
							if (held.reference != info.reference) {
								if (str clash = GraphProperties::Clash(held, info); !clash.empty() && !clashed) {
									clashed = true;
									diagnostics.push_back({user, "uses subgraph '" + subgraph->name + "', whose keyword '" + info.name + "' and '" +
																	 held.name + "' " + clash});
								}
								continue;
							}
							known = true;
							bool same = held.kind == info.kind && held.entries.size() == info.entries.size();
							for (size_t i = 0; same && i < info.entries.size(); ++i) {
								same = held.entries[i].reference == info.entries[i].reference;
							}
							if (!same) {
								diagnostics.push_back({user, "uses subgraph '" + subgraph->name + "', whose keyword '" + info.reference +
																 "' differs from the '" + held.name + "' already under that reference. 1 reference is 1 keyword"});
							}
						}
						if (!known && !clashed) {
							properties.push_back(std::move(info));
						}
					}
					CollectFrom(subgraph->document, definitions, properties, diagnostics, visited, user);
				}
			}

		} // anonymous namespace

		bool CollectKeywords(const GraphDocument& document, GraphDefinitions& definitions, Vector<GraphPropertyInfo>& properties,
							 Vector<GraphDiagnostic>& diagnostics) {
			const size_t firstDiagnostic = diagnostics.size();
			Vector<Guid> visited{GetMemoryResource(MemTag::Editor)};
			CollectFrom(document, definitions, properties, diagnostics, visited, 0);
			return diagnostics.size() == firstDiagnostic;
		}

	} // namespace GraphSubgraphs

	namespace GraphCustomFunctions {

		bool ReadPorts(const GraphNode& node, std::string_view key, Vector<GraphCustomFunctionPort>& out, str& error) {
			out.clear();
			const GraphSetting* setting = FindSetting(node.settings, key);
			if (!setting || setting->value.empty()) {
				return true;
			}
			LinearAllocator arena(setting->value.size() + 64, MemTag::Editor);
			const JSONValue list = JSONParse(setting->value, arena);
			const str what = key == GRAPH_CUSTOM_FUNCTION_INPUTS ? "inputs" : "outputs";
			if (!list.IsArray()) {
				error = "has " + what + " that aren't a list";
				return false;
			}
			for (size_t i = 0; i < list.Size(); ++i) {
				const JSONValue& entry = list[i];
				const JSONValue* id = entry.IsObject() ? entry.Find("id") : nullptr;
				const JSONValue* name = entry.IsObject() ? entry.Find("name") : nullptr;
				const JSONValue* type = entry.IsObject() ? entry.Find("type") : nullptr;
				if (!id || !id->IsNumber() || id->AsNumber() < 1.0 || !name || !name->IsString() || !type || !type->IsString()) {
					error = "has " + what + " with entry " + std::to_string(i) + " missing an id, a name or a type";
					return false;
				}
				GraphCustomFunctionPort port;
				port.id = static_cast<u32>(id->AsNumber());
				port.name = str(name->AsString());
				const auto* known = std::find(std::begin(GRAPH_CUSTOM_FUNCTION_PORT_TYPES), std::end(GRAPH_CUSTOM_FUNCTION_PORT_TYPES), type->AsString());
				if (known == std::end(GRAPH_CUSTOM_FUNCTION_PORT_TYPES)) {
					error = "has a port '" + port.name + "' of type '" + str(type->AsString()) + "' (a custom function doesn't take it)";
					return false;
				}
				port.type = static_cast<u32>(known - std::begin(GRAPH_CUSTOM_FUNCTION_PORT_TYPES));
				out.push_back(std::move(port));
			}
			return true;
		}

		str WritePorts(std::span<const GraphCustomFunctionPort> ports) {
			JSONValue list = JSONValue::MakeArray();
			for (const GraphCustomFunctionPort& port : ports) {
				JSONValue entry = JSONValue::MakeObject();
				entry.AddMember("id", JSONValue::MakeNumber(port.id));
				entry.AddMember("name", JSONValue::MakeString(port.name));
				entry.AddMember("type", JSONValue::MakeString(GRAPH_CUSTOM_FUNCTION_PORT_TYPES[port.type]));
				list.Push(std::move(entry));
			}
			return JSONSerialize(list, false);
		}

		str Identifier(std::string_view name) {
			auto kept = [](char c) { return IsWordCharacter(c) || c == '_'; };
			size_t first = 0;
			size_t last = name.size();
			while (first < last && !kept(name[first])) {
				++first;
			}
			while (last > first && !kept(name[last - 1])) {
				--last;
			}
			str result;
			bool run = false;
			for (size_t i = first; i < last; ++i) {
				if (IsWordCharacter(name[i])) {
					result += name[i];
					run = false;
				} else if (!run) {
					result += '_';
					run = true;
				}
			}
			for (u32 suffix = 0; result.empty() || (result[0] >= '0' && result[0] <= '9') || IsHLSLKeyword(result);) {
				if (!result.empty() && result[0] == '_') {
					result += "_" + std::to_string(++suffix);
				} else {
					result = "_" + result;
				}
			}
			return result;
		}

	} // namespace GraphCustomFunctions

} // namespace Shalltear
