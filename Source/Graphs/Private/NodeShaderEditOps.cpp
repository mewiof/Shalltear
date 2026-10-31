#include "NodeShaderEditOps.h"

#include "GraphDefinitions.h"
#include "GraphProperties.h"

#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <format>

namespace Shalltear {

	namespace {

		using P = GraphPortType;
		using EditorTheme::ColorToken;

		GraphSetting& SettingFor(Vector<GraphSetting>& settings, std::string_view key) {
			auto at = std::lower_bound(settings.begin(), settings.end(), key, [](const GraphSetting& held, std::string_view wanted) { return held.key < wanted; });
			if (at != settings.end() && at->key == key) {
				return *at;
			}
			GraphSetting setting;
			setting.key = str(key);
			return *settings.insert(at, std::move(setting));
		}

		bool EraseSetting(Vector<GraphSetting>& settings, std::string_view key) {
			auto at = std::find_if(settings.begin(), settings.end(), [&](const GraphSetting& held) { return held.key == key; });
			if (at == settings.end()) {
				return false;
			}
			settings.erase(at);
			return true;
		}

		GraphInput& InputFor(GraphNode& node, std::string_view port) {
			auto at = std::lower_bound(node.inputs.begin(), node.inputs.end(), port, [](const GraphInput& held, std::string_view wanted) { return held.port < wanted; });
			if (at != node.inputs.end() && at->port == port) {
				return *at;
			}
			GraphInput input;
			input.port = str(port);
			return *node.inputs.insert(at, std::move(input));
		}

		const GraphNode* FindConst(const GraphDocument& document, u32 id) {
			return GraphDocumentOps::FindNode(const_cast<GraphDocument&>(document), id);
		}

		GraphProperty* FindProperty(GraphDocument& document, u32 id) {
			for (GraphProperty& property : document.properties) {
				if (property.id == id) {
					return &property;
				}
			}
			return nullptr;
		}

		bool IsStack(const GraphNodeDefinition& definition) {
			return definition.category == "Context" || definition.category == "Block";
		}

		constexpr std::string_view TEXTURE_TYPES[] = {"Texture2D", "Texture2DArray", "Texture3D", "Cubemap"};

		enum class PortClass : u8 {
			Vector,
			Boolean,
			Matrix,
			Dynamic,
			Texture,
			Gradient,
			Any,
			Connection,
			Curve,
			Mesh,
			Event
		};

		struct PortShape {
			PortClass kind = PortClass::Vector;
			u32 dimension = 0;
			bool testable = false;
		};

		PortShape ShapeOf(GraphPortType type) {
			switch (type) {
			case P::Float:
			case P::Float2:
			case P::Float3:
			case P::Float4:
			case P::DynamicVector:
				return {PortClass::Vector, 0};
			case P::Float2x2:
				return {PortClass::Matrix, 2};
			case P::Float3x3:
				return {PortClass::Matrix, 3};
			case P::Float4x4:
				return {PortClass::Matrix, 4};
			case P::DynamicMatrix:
				return {PortClass::Matrix, 0};
			case P::Dynamic:
				return {PortClass::Dynamic, 0};
			case P::Any:
				return {PortClass::Any, 0};
			case P::Boolean:
				return {PortClass::Boolean, 0};
			// kinds: 1 = 2D, 2 = array, 3 = volume, 4 = cube
			case P::Texture2D:
				return {PortClass::Texture, 1};
			case P::Texture2DArray:
				return {PortClass::Texture, 2};
			case P::Texture3D:
				return {PortClass::Texture, 3};
			case P::Cubemap:
				return {PortClass::Texture, 4};
			case P::Gradient:
				return {PortClass::Gradient, 0};
			case P::ConnectionState:
				return {PortClass::Connection, 0};
			case P::Curve:
				return {PortClass::Curve, 0};
			case P::Mesh:
				return {PortClass::Mesh, 0};
			case P::GPUEvent:
				return {PortClass::Event, 0};
			}
			return {PortClass::Vector, 0};
		}

		PortShape PropertyShape(const GraphProperty& property) {
			PortShape shape = property.type == "Boolean" ? PortShape{PortClass::Boolean, 0} : PortShape{PortClass::Vector, 0};
			for (u32 kind = 0; kind < std::size(TEXTURE_TYPES); ++kind) {
				if (property.type == TEXTURE_TYPES[kind]) {
					shape = {PortClass::Texture, kind + 1};
				}
			}
			GraphPropertyInfo info;
			str error;
			shape.testable = GraphProperties::Parse(property, info, error) && info.customBinding;
			return shape;
		}

		PortShape SourceShape(const GraphDocument& document, GraphDefinitions& definitions, u32 node, std::string_view port, u32 depth = 0) {
			const GraphNode* held = FindConst(document, node);
			const GraphNodeDefinition* definition = held ? definitions.Find(document, *held) : nullptr;
			if (!definition) {
				return {PortClass::Any, 0};
			}
			const i32 index = GraphNodeLibrary::FindOutput(*definition, port);
			if (index < 0) {
				return {PortClass::Any, 0};
			}
			if (definition->id == NodeShaderEditOps::PROPERTY_NODE) {
				const u32 property = NodeShaderEditOps::PropertyOf(document, node);
				for (const GraphProperty& held : document.properties) {
					if (held.id == property) {
						return PropertyShape(held);
					}
				}
				return {PortClass::Any, 0};
			}
			PortShape shape = ShapeOf(definition->outputs[static_cast<size_t>(index)].type);
			if (shape.kind != PortClass::Any || depth > 64) {
				return shape;
			}
			for (const GraphInput& input : held->inputs) {
				if (input.fromNode != 0) {
					return SourceShape(document, definitions, input.fromNode, input.fromPort, depth + 1);
				}
			}
			return {PortClass::Vector, 0};
		}

		bool Accepts(PortShape input, PortShape source) {
			if (input.kind == PortClass::Any || source.kind == PortClass::Any) {
				return true;
			}
			switch (input.kind) {
			case PortClass::Vector:
				return source.kind == PortClass::Vector || source.kind == PortClass::Boolean || source.kind == PortClass::Dynamic;
			case PortClass::Boolean:
				return source.kind == PortClass::Boolean;
			case PortClass::Matrix:
				if (source.kind == PortClass::Dynamic) {
					return true;
				}
				return source.kind == PortClass::Matrix && (input.dimension == 0 || source.dimension == 0 || input.dimension <= source.dimension);
			case PortClass::Dynamic:
				return source.kind == PortClass::Vector || source.kind == PortClass::Matrix || source.kind == PortClass::Dynamic;
			case PortClass::Texture:
				return source.kind == PortClass::Texture && source.dimension == input.dimension;
			case PortClass::Gradient:
				return source.kind == PortClass::Gradient;
			case PortClass::Any:
				return true;
			case PortClass::Connection:
				return source.testable;
			case PortClass::Curve:
				return source.kind == PortClass::Curve;
			case PortClass::Mesh:
				return source.kind == PortClass::Mesh;
			case PortClass::Event:
				return source.kind == PortClass::Event;
			}
			return false;
		}

		std::string_view ShapeName(PortShape shape) {
			switch (shape.kind) {
			case PortClass::Vector:
				return "a vector";
			case PortClass::Boolean:
				return "a Boolean";
			case PortClass::Matrix:
				return "a matrix";
			case PortClass::Dynamic:
				return "a vector or a matrix";
			case PortClass::Texture:
				return "a texture";
			case PortClass::Gradient:
				return "a gradient";
			case PortClass::Any:
				return "anything";
			case PortClass::Connection:
				return "a property with Use Custom Binding";
			case PortClass::Curve:
				return "a curve";
			case PortClass::Mesh:
				return "a mesh";
			case PortClass::Event:
				return "a GPU event";
			}
			return "a value";
		}

		bool Reads(const GraphDocument& document, u32 node, u32 target, HashSet<u32>& seen) {
			if (node == target) {
				return true;
			}
			if (!seen.insert(node).second) {
				return false;
			}
			const GraphNode* held = FindConst(document, node);
			if (!held) {
				return false;
			}
			for (const GraphInput& input : held->inputs) {
				if (input.fromNode != 0 && Reads(document, input.fromNode, target, seen)) {
					return true;
				}
			}
			for (const GraphContext& context : document.contexts) {
				if (context.node.id != node) {
					continue;
				}
				for (const GraphNode& block : context.blocks) {
					if (Reads(document, block.id, target, seen)) {
						return true;
					}
				}
			}
			return false;
		}

		str Number(f32 value) { return std::format("{}", value); }

		u32 ResolveWidth(const GraphDocument& document, GraphDefinitions& definitions, u32 node, HashMap<u32, u32>& memo, u32 depth);

		u32 OutputWidth(const GraphDocument& document, GraphDefinitions& definitions, u32 node, std::string_view port, HashMap<u32, u32>& memo, u32 depth) {
			const GraphNode* held = FindConst(document, node);
			const GraphNodeDefinition* definition = held ? definitions.Find(document, *held) : nullptr;
			const i32 index = definition ? GraphNodeLibrary::FindOutput(*definition, port) : -1;
			if (index < 0 || depth > 64) {
				return 4;
			}
			if (definition->id == NodeShaderEditOps::PROPERTY_NODE) {
				const u32 property = NodeShaderEditOps::PropertyOf(document, node);
				for (const GraphProperty& candidate : document.properties) {
					if (candidate.id == property) {
						if (candidate.type == "Vector2") {
							return 2;
						}
						if (candidate.type == "Vector3") {
							return 3;
						}
						return candidate.type == "Vector4" || candidate.type == "Color" ? 4u : 1u;
					}
				}
				return 1;
			}
			switch (definition->outputs[static_cast<size_t>(index)].type) {
			case P::Float:
			case P::Boolean:
				return 1;
			case P::Float2:
				return 2;
			case P::Float3:
				return 3;
			case P::DynamicVector:
			case P::Dynamic:
				return ResolveWidth(document, definitions, node, memo, depth + 1);
			case P::Any:
				for (const GraphInput& input : held->inputs) {
					if (input.fromNode != 0) {
						return OutputWidth(document, definitions, input.fromNode, input.fromPort, memo, depth + 1);
					}
				}
				return 1;
			default:
				return 4;
			}
		}

		u32 ResolveWidth(const GraphDocument& document, GraphDefinitions& definitions, u32 node, HashMap<u32, u32>& memo, u32 depth) {
			if (auto known = memo.find(node); known != memo.end()) {
				return known->second;
			}
			memo[node] = 1;
			const GraphNode* held = FindConst(document, node);
			const GraphNodeDefinition* definition = held ? definitions.Find(document, *held) : nullptr;
			u32 width = 0;
			if (definition) {
				for (const GraphInput& input : held->inputs) {
					const i32 port = input.fromNode != 0 ? GraphNodeLibrary::FindInput(*definition, input.port) : -1;
					if (port < 0) {
						continue;
					}
					const GraphPortType type = definition->inputs[static_cast<size_t>(port)].type;
					if (type != P::DynamicVector && type != P::Dynamic) {
						continue;
					}
					const u32 source = OutputWidth(document, definitions, input.fromNode, input.fromPort, memo, depth + 1);
					if (source > 1 && (width == 0 || source < width)) {
						width = source;
					}
				}
			}
			memo[node] = width == 0 ? 1u : width;
			return memo[node];
		}

		GraphContext* FindContext(GraphDocument& document, u32 id) {
			for (GraphContext& context : document.contexts) {
				if (context.node.id == id) {
					return &context;
				}
			}
			return nullptr;
		}

		constexpr std::string_view PROPERTY_TYPES[] = {"Float", "Vector2", "Vector3", "Vector4", "Color",
													   "Boolean", "Texture2D", "Texture2DArray", "Texture3D", "Cubemap",
													   "BooleanKeyword", "EnumKeyword"};
		constexpr std::string_view KEYWORD_TYPE = "BooleanKeyword";
		constexpr std::string_view ENUM_KEYWORD_TYPE = "EnumKeyword";
		constexpr std::string_view KEYWORD_NODE = "Utility/Keyword";
		constexpr std::string_view ENTRIES_SETTING = "entries";

		bool IsKeywordType(std::string_view type) { return type == KEYWORD_TYPE || type == ENUM_KEYWORD_TYPE; }

		str ReferenceFor(std::string_view name, bool keyword = false) {
			str out = "_";
			for (char c : name) {
				if (std::isalnum(static_cast<unsigned char>(c)) || c == '_') {
					out += keyword ? static_cast<char>(std::toupper(static_cast<unsigned char>(c))) : c;
				} else if (keyword && c == ' ') {
					out += '_';
				}
			}
			return out.size() > 1 ? out : str(keyword ? "_KEYWORD" : "_Property");
		}

		bool NameTaken(const GraphDocument& document, std::string_view name, u32 except) {
			return std::any_of(document.properties.begin(), document.properties.end(),
							   [&](const GraphProperty& held) { return held.id != except && held.name == name; });
		}

		bool ReferenceTaken(const GraphDocument& document, std::string_view reference, u32 except) {
			return std::any_of(document.properties.begin(), document.properties.end(),
							   [&](const GraphProperty& held) { return held.id != except && held.reference == reference; });
		}

		str UniqueName(const GraphDocument& document, std::string_view base, u32 except) {
			str name(base);
			for (u32 n = 1; NameTaken(document, name, except); ++n) {
				name = std::format("{} ({})", base, n);
			}
			return name;
		}

		str UniqueReference(const GraphDocument& document, std::string_view base, u32 except) {
			str reference(base);
			for (u32 n = 1; ReferenceTaken(document, reference, except); ++n) {
				reference = std::format("{}_{}", base, n);
			}
			return reference;
		}

		str DefaultFor(std::string_view type) {
			if (type == "Color") {
				return "[0,0,0,1]";
			}
			if (type == "Boolean") {
				return "false";
			}
			if (type == "Vector2") {
				return "[0,0]";
			}
			if (type == "Vector3") {
				return "[0,0,0]";
			}
			if (type == "Vector4") {
				return "[0,0,0,0]";
			}
			if (std::find(std::begin(TEXTURE_TYPES), std::end(TEXTURE_TYPES), type) != std::end(TEXTURE_TYPES)) {
				return {};
			}
			if (type == KEYWORD_TYPE) {
				return "false";
			}
			return "0";
		}

		bool ReadKeyword(GraphDocument& document, u32 id, GraphProperty*& out, Vector<GraphKeywordEntry>& entries) {
			out = FindProperty(document, id);
			str error;
			return out && out->type == ENUM_KEYWORD_TYPE && GraphProperties::ReadEntries(*out, entries, error);
		}

		void WriteKeyword(GraphProperty& property, const Vector<GraphKeywordEntry>& entries) {
			SettingFor(property.settings, ENTRIES_SETTING).value = GraphProperties::WriteEntries(entries);
		}

		size_t DefaultEntry(const GraphProperty& property) { return static_cast<size_t>(std::max(0l, std::strtol(property.defaultValue.c_str(), nullptr, 10))); }

		str EntrySuffix(std::string_view name) {
			str out;
			for (char c : name) {
				const unsigned char byte = static_cast<unsigned char>(c);
				if ((byte & 0xC0) == 0x80) {
					continue;
				}
				out += std::isalnum(byte) && byte < 0x80 ? static_cast<char>(std::toupper(byte)) : '_';
			}
			return out;
		}

		template <typename Taken>
		str Deduplicate(std::string_view base, std::string_view join, Taken taken) {
			str candidate(base);
			for (u32 n = 1; taken(candidate); ++n) {
				candidate = str(base) + str(join) + std::to_string(n);
			}
			return candidate;
		}

		std::string_view Trim(std::string_view text) {
			while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front()))) {
				text.remove_prefix(1);
			}
			while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back()))) {
				text.remove_suffix(1);
			}
			return text;
		}

	} // anonymous namespace

	namespace NodeShaderEditOps {

		ColorToken PortColor(GraphPortType type) {
			switch (type) {
			case P::Float:
				return ColorToken::GraphPortFloat;
			case P::Float2:
				return ColorToken::GraphPortVector2;
			case P::Float3:
				return ColorToken::GraphPortVector3;
			case P::Float4:
				return ColorToken::GraphPortVector4;
			case P::Float2x2:
			case P::Float3x3:
			case P::Float4x4:
			case P::DynamicMatrix:
				return ColorToken::GraphPortMatrix;
			case P::Boolean:
				return ColorToken::GraphPortBoolean;
			case P::Texture2D:
			case P::Texture2DArray:
			case P::Texture3D:
			case P::Cubemap:
				return ColorToken::GraphPortTexture;
			case P::DynamicVector:
			case P::Dynamic:
			case P::Any:
			case P::Gradient:
			case P::ConnectionState:
			case P::Curve:
			case P::Mesh:
			case P::GPUEvent:
				break;
			}
			return ColorToken::GraphPortOther;
		}

		u32 AddNode(GraphDocument& document, std::string_view def, Vec2 position) {
			const GraphNodeDefinition* definition = GraphNodeLibrary::Find(def);
			if (!definition || IsStack(*definition)) {
				return 0;
			}
			GraphNode node;
			node.id = document.nextId++;
			node.def = str(def);
			document.nodes.push_back(std::move(node));
			GraphDocumentOps::MoveNode(document, document.nodes.back().id, position);
			return document.nodes.back().id;
		}

		bool CanConnect(const GraphDocument& document, u32 fromNode, std::string_view fromPort, u32 toNode, std::string_view toPort, str* why,
						GraphDefinitions* definitions) {
			GraphDefinitions local;
			GraphDefinitions& resolver = definitions ? *definitions : local;
			auto refuse = [&](str message) {
				if (why) {
					*why = std::move(message);
				}
				return false;
			};
			if (fromNode == toNode) {
				return refuse("a node can't feed itself");
			}
			const GraphNode* from = FindConst(document, fromNode);
			const GraphNode* to = FindConst(document, toNode);
			const GraphNodeDefinition* fromDefinition = from ? resolver.Find(document, *from) : nullptr;
			const GraphNodeDefinition* toDefinition = to ? resolver.Find(document, *to) : nullptr;
			if (!fromDefinition || !toDefinition) {
				return refuse("one of the nodes has no definition");
			}
			const i32 output = GraphNodeLibrary::FindOutput(*fromDefinition, fromPort);
			const i32 input = GraphNodeLibrary::FindInput(*toDefinition, toPort);
			if (output < 0 || input < 0) {
				return refuse("no such port");
			}
			const PortShape source = SourceShape(document, resolver, fromNode, fromPort);
			const PortShape target = ShapeOf(toDefinition->inputs[static_cast<size_t>(input)].type);
			if (!Accepts(target, source)) {
				return refuse(std::format("'{}' takes {}, and '{}' gives {}", GraphPortLabel(toDefinition->inputs[static_cast<size_t>(input)]), ShapeName(target),
										  GraphPortLabel(fromDefinition->outputs[static_cast<size_t>(output)]), ShapeName(source)));
			}
			HashSet<u32> seen{GetMemoryResource(MemTag::Editor)};
			if (Reads(document, fromNode, toNode, seen)) {
				return refuse("the wire would make a cycle");
			}
			return true;
		}

		bool OutputFeeds(const GraphDocument& document, u32 node, std::string_view port, GraphPortType input, GraphDefinitions* definitions) {
			GraphDefinitions local;
			return Accepts(ShapeOf(input), SourceShape(document, definitions ? *definitions : local, node, port));
		}

		bool InputTakes(const GraphDocument& document, u32 node, std::string_view port, GraphPortType output, GraphDefinitions* definitions) {
			GraphDefinitions local;
			GraphDefinitions& resolver = definitions ? *definitions : local;
			const GraphNode* held = FindConst(document, node);
			const GraphNodeDefinition* definition = held ? resolver.Find(document, *held) : nullptr;
			const i32 input = definition ? GraphNodeLibrary::FindInput(*definition, port) : -1;
			if (input < 0) {
				return false;
			}
			PortShape source = ShapeOf(output);
			return Accepts(ShapeOf(definition->inputs[static_cast<size_t>(input)].type), source);
		}

		bool PropertyFeeds(const GraphDocument& document, u32 property, u32 node, std::string_view port, GraphDefinitions* definitions) {
			GraphDefinitions local;
			GraphDefinitions& resolver = definitions ? *definitions : local;
			const GraphNode* held = FindConst(document, node);
			const GraphNodeDefinition* definition = held ? resolver.Find(document, *held) : nullptr;
			const i32 input = definition ? GraphNodeLibrary::FindInput(*definition, port) : -1;
			if (input < 0) {
				return false;
			}
			for (const GraphProperty& candidate : document.properties) {
				if (candidate.id == property) {
					return Accepts(ShapeOf(definition->inputs[static_cast<size_t>(input)].type), PropertyShape(candidate));
				}
			}
			return false;
		}

		u32 DynamicWidth(const GraphDocument& document, u32 node, GraphDefinitions* definitions) {
			GraphDefinitions local;
			HashMap<u32, u32> memo{GetMemoryResource(MemTag::Editor)};
			return ResolveWidth(document, definitions ? *definitions : local, node, memo, 0);
		}

		bool SetInputConstant(GraphDocument& document, u32 node, std::string_view port, std::span<const f32> values) {
			GraphNode* target = GraphDocumentOps::FindNode(document, node);
			if (!target || port.empty() || values.empty() || values.size() > 4) {
				return false;
			}
			str json;
			if (values.size() == 1) {
				json = Number(values[0]);
			} else {
				json = "[";
				for (size_t i = 0; i < values.size(); ++i) {
					json += (i == 0 ? "" : ",") + Number(values[i]);
				}
				json += "]";
			}
			InputFor(*target, port).value = std::move(json);
			return true;
		}

		bool SetInputValue(GraphDocument& document, u32 node, std::string_view port, std::string_view json) {
			GraphNode* target = GraphDocumentOps::FindNode(document, node);
			if (!target || port.empty() || json.empty()) {
				return false;
			}
			InputFor(*target, port).value = str(json);
			return true;
		}

		bool SetNodeSetting(GraphDocument& document, u32 node, std::string_view key, std::string_view json, GraphDefinitions* definitions) {
			GraphNode* target = GraphDocumentOps::FindNode(document, node);
			if (!target || key.empty()) {
				return false;
			}
			if (json.empty()) {
				EraseSetting(target->settings, key);
			} else {
				SettingFor(target->settings, key).value = str(json);
			}
			GraphDefinitions local;
			GraphDefinitions& resolver = definitions ? *definitions : local;
			if (const GraphNodeDefinition* definition = resolver.Find(document, *target)) {
				for (u32 i = 0; i < definition->inputs.size(); ++i) {
					if (!GraphNodeLibrary::InputActive(*definition, i, *target)) {
						GraphDocumentOps::Disconnect(document, node, definition->inputs[i].name);
					}
				}
			}
			return true;
		}

		bool SetGraphSetting(GraphDocument& document, std::string_view key, std::string_view json) {
			if (key.empty()) {
				return false;
			}
			if (json.empty()) {
				return EraseSetting(document.settings, key);
			}
			SettingFor(document.settings, key).value = str(json);
			return true;
		}

		str Quote(std::string_view text) {
			str out = "\"";
			for (char c : text) {
				switch (c) {
				case '"':
					out += "\\\"";
					break;
				case '\\':
					out += "\\\\";
					break;
				case '\n':
					out += "\\n";
					break;
				case '\r':
					out += "\\r";
					break;
				case '\t':
					out += "\\t";
					break;
				default:
					if (static_cast<u8>(c) < 0x20) {
						out += std::format("\\u{:04x}", static_cast<u32>(static_cast<u8>(c)));
					} else {
						out += c;
					}
					break;
				}
			}
			return out + "\"";
		}

		str GraphSettingText(const GraphDocument& document, std::string_view key) {
			for (const GraphSetting& setting : document.settings) {
				if (setting.key == key && setting.value.size() >= 2 && setting.value.front() == '"' && setting.value.back() == '"') {
					return setting.value.substr(1, setting.value.size() - 2);
				}
			}
			return {};
		}

		u32 Delete(GraphDocument& document, std::span<const u32> ids) {
			Vector<u32> doomed{GetMemoryResource(MemTag::Editor)};
			for (u32 id : ids) {
				const bool context = std::any_of(document.contexts.begin(), document.contexts.end(), [&](const GraphContext& held) { return held.node.id == id; });
				bool block = false;
				for (const GraphContext& held : document.contexts) {
					block = block || std::any_of(held.blocks.begin(), held.blocks.end(), [&](const GraphNode& b) { return b.id == id; });
				}
				if (!context && !block) {
					doomed.push_back(id);
				}
			}
			return doomed.empty() ? 0u : GraphDocumentOps::Delete(document, doomed);
		}

		u32 AddSubgraphOutput(GraphDocument& document, u32 context, std::string_view type) {
			GraphContext* stack = FindContext(document, context);
			const auto& types = GRAPH_SUBGRAPH_OUTPUT_TYPES;
			if (!stack || stack->node.def != GRAPH_SUBGRAPH_OUTPUT_CONTEXT || std::find(std::begin(types), std::end(types), type) == std::end(types)) {
				return 0;
			}
			const str base = "Out_" + str(type);
			str name = base;
			for (u32 n = 1; std::any_of(stack->blocks.begin(), stack->blocks.end(), [&](const GraphNode& held) { return GraphSubgraphs::OutputNameOf(held) == name; });
				 ++n) {
				name = std::format("{} ({})", base, n);
			}
			GraphNode block;
			block.id = document.nextId++;
			block.def = str(GRAPH_SUBGRAPH_OUTPUT_BLOCK);
			SettingFor(block.settings, "Name").value = Quote(name);
			SettingFor(block.settings, "Type").value = Quote(type);
			const u32 id = block.id;
			stack->blocks.push_back(std::move(block));
			return id;
		}

		bool RenameSubgraphOutput(GraphDocument& document, u32 block, std::string_view name) {
			for (GraphContext& context : document.contexts) {
				for (GraphNode& held : context.blocks) {
					if (held.id != block || held.def != GRAPH_SUBGRAPH_OUTPUT_BLOCK) {
						continue;
					}
					const bool taken = std::any_of(context.blocks.begin(), context.blocks.end(),
												   [&](const GraphNode& other) { return other.id != block && GraphSubgraphs::OutputNameOf(other) == name; });
					if (name.empty() || taken || GraphSubgraphs::OutputNameOf(held) == name) {
						return false;
					}
					SettingFor(held.settings, "Name").value = Quote(name);
					return true;
				}
			}
			return false;
		}

		bool SetSubgraphOutputType(GraphDocument& document, u32 block, std::string_view type) {
			const auto& types = GRAPH_SUBGRAPH_OUTPUT_TYPES;
			if (std::find(std::begin(types), std::end(types), type) == std::end(types)) {
				return false;
			}
			for (GraphContext& context : document.contexts) {
				for (GraphNode& held : context.blocks) {
					if (held.id == block && held.def == GRAPH_SUBGRAPH_OUTPUT_BLOCK && GraphSubgraphs::OutputTypeOf(held) < std::size(types) &&
						types[GraphSubgraphs::OutputTypeOf(held)] != type) {
						SettingFor(held.settings, "Type").value = Quote(type);
						return true;
					}
				}
			}
			return false;
		}

		u32 AddSubgraphNode(GraphDocument& document, const Guid& asset, Vec2 position) {
			if (!asset.IsValid()) {
				return 0;
			}
			GraphNode node;
			node.id = document.nextId++;
			node.def = str(GRAPH_SUBGRAPH_NODE);
			SettingFor(node.settings, GRAPH_SUBGRAPH_ASSET_SETTING).value = Quote(asset.ToString());
			const u32 id = node.id;
			document.nodes.push_back(std::move(node));
			GraphDocumentOps::MoveNode(document, id, position);
			return id;
		}

		u32 AddCustomFunctionNode(GraphDocument& document, Vec2 position) {
			GraphNode node;
			node.id = document.nextId++;
			node.def = str(GRAPH_CUSTOM_FUNCTION_NODE);
			const u32 id = node.id;
			document.nodes.push_back(std::move(node));
			GraphDocumentOps::MoveNode(document, id, position);
			return id;
		}

		namespace {

			bool ReadCustomFunction(GraphDocument& document, u32 node, GraphNode*& out, Vector<GraphCustomFunctionPort>& inputs,
									Vector<GraphCustomFunctionPort>& outputs) {
				out = GraphDocumentOps::FindNode(document, node);
				str error;
				return out && out->def == GRAPH_CUSTOM_FUNCTION_NODE &&
					   GraphCustomFunctions::ReadPorts(*out, GRAPH_CUSTOM_FUNCTION_INPUTS, inputs, error) &&
					   GraphCustomFunctions::ReadPorts(*out, GRAPH_CUSTOM_FUNCTION_OUTPUTS, outputs, error);
			}

			void WriteCustomFunction(GraphNode& node, const Vector<GraphCustomFunctionPort>& inputs, const Vector<GraphCustomFunctionPort>& outputs) {
				SettingFor(node.settings, GRAPH_CUSTOM_FUNCTION_INPUTS).value = GraphCustomFunctions::WritePorts(inputs);
				SettingFor(node.settings, GRAPH_CUSTOM_FUNCTION_OUTPUTS).value = GraphCustomFunctions::WritePorts(outputs);
			}

			str UniquePortName(std::string_view name, u32 port, const Vector<GraphCustomFunctionPort>& inputs,
							   const Vector<GraphCustomFunctionPort>& outputs) {
				const str base = GraphCustomFunctions::Identifier(name);
				auto taken = [&](const str& candidate) {
					for (const Vector<GraphCustomFunctionPort>* list : {&inputs, &outputs}) {
						for (const GraphCustomFunctionPort& held : *list) {
							if (held.id != port && held.name == candidate) {
								return true;
							}
						}
					}
					return false;
				};
				str candidate = base;
				for (u32 suffix = 1; taken(candidate); ++suffix) {
					candidate = base + "_" + std::to_string(suffix);
				}
				return candidate;
			}

			GraphCustomFunctionPort* FindPort(Vector<GraphCustomFunctionPort>& inputs, Vector<GraphCustomFunctionPort>& outputs, u32 port) {
				for (Vector<GraphCustomFunctionPort>* list : {&inputs, &outputs}) {
					for (GraphCustomFunctionPort& held : *list) {
						if (held.id == port) {
							return &held;
						}
					}
				}
				return nullptr;
			}

		} // anonymous namespace

		u32 AddCustomFunctionPort(GraphDocument& document, u32 node, bool output, std::string_view type) {
			const auto& types = GRAPH_CUSTOM_FUNCTION_PORT_TYPES;
			const auto* known = std::find(std::begin(types), std::end(types), type);
			GraphNode* held = nullptr;
			Vector<GraphCustomFunctionPort> inputs{GetMemoryResource(MemTag::Editor)};
			Vector<GraphCustomFunctionPort> outputs{GetMemoryResource(MemTag::Editor)};
			if (known == std::end(types) || !ReadCustomFunction(document, node, held, inputs, outputs)) {
				return 0;
			}
			u32 id = 1;
			for (const Vector<GraphCustomFunctionPort>* list : {&inputs, &outputs}) {
				for (const GraphCustomFunctionPort& port : *list) {
					id = std::max(id, port.id + 1);
				}
			}
			GraphCustomFunctionPort port;
			port.id = id;
			port.name = UniquePortName("New", id, inputs, outputs);
			port.type = static_cast<u32>(known - std::begin(types));
			(output ? outputs : inputs).push_back(std::move(port));
			WriteCustomFunction(*held, inputs, outputs);
			return id;
		}

		bool RenameCustomFunctionPort(GraphDocument& document, u32 node, u32 port, std::string_view name) {
			GraphNode* held = nullptr;
			Vector<GraphCustomFunctionPort> inputs{GetMemoryResource(MemTag::Editor)};
			Vector<GraphCustomFunctionPort> outputs{GetMemoryResource(MemTag::Editor)};
			if (name.empty() || !ReadCustomFunction(document, node, held, inputs, outputs)) {
				return false;
			}
			GraphCustomFunctionPort* found = FindPort(inputs, outputs, port);
			if (!found) {
				return false;
			}
			str unique = UniquePortName(name, port, inputs, outputs);
			if (unique == found->name) {
				return false;
			}
			found->name = std::move(unique);
			WriteCustomFunction(*held, inputs, outputs);
			return true;
		}

		bool SetCustomFunctionPortType(GraphDocument& document, u32 node, u32 port, std::string_view type) {
			const auto& types = GRAPH_CUSTOM_FUNCTION_PORT_TYPES;
			const auto* known = std::find(std::begin(types), std::end(types), type);
			GraphNode* held = nullptr;
			Vector<GraphCustomFunctionPort> inputs{GetMemoryResource(MemTag::Editor)};
			Vector<GraphCustomFunctionPort> outputs{GetMemoryResource(MemTag::Editor)};
			if (known == std::end(types) || !ReadCustomFunction(document, node, held, inputs, outputs)) {
				return false;
			}
			GraphCustomFunctionPort* found = FindPort(inputs, outputs, port);
			const u32 index = static_cast<u32>(known - std::begin(types));
			if (!found || found->type == index) {
				return false;
			}
			found->type = index;
			WriteCustomFunction(*held, inputs, outputs);
			return true;
		}

		bool MoveCustomFunctionPort(GraphDocument& document, u32 node, u32 port, u32 index) {
			GraphNode* held = nullptr;
			Vector<GraphCustomFunctionPort> inputs{GetMemoryResource(MemTag::Editor)};
			Vector<GraphCustomFunctionPort> outputs{GetMemoryResource(MemTag::Editor)};
			if (!ReadCustomFunction(document, node, held, inputs, outputs)) {
				return false;
			}
			for (Vector<GraphCustomFunctionPort>* list : {&inputs, &outputs}) {
				auto at = std::find_if(list->begin(), list->end(), [port](const GraphCustomFunctionPort& held) { return held.id == port; });
				if (at == list->end()) {
					continue;
				}
				const u32 from = static_cast<u32>(at - list->begin());
				const u32 to = std::min<u32>(index, static_cast<u32>(list->size()) - 1);
				if (from == to) {
					return false;
				}
				GraphCustomFunctionPort moving = std::move(*at);
				list->erase(at);
				list->insert(list->begin() + to, std::move(moving));
				WriteCustomFunction(*held, inputs, outputs);
				return true;
			}
			return false;
		}

		u32 RemoveCustomFunctionPorts(GraphDocument& document, u32 node, std::span<const u32> ports) {
			GraphNode* held = nullptr;
			Vector<GraphCustomFunctionPort> inputs{GetMemoryResource(MemTag::Editor)};
			Vector<GraphCustomFunctionPort> outputs{GetMemoryResource(MemTag::Editor)};
			if (!ReadCustomFunction(document, node, held, inputs, outputs)) {
				return 0;
			}
			auto gone = [ports](const GraphCustomFunctionPort& port) { return std::find(ports.begin(), ports.end(), port.id) != ports.end(); };
			const size_t before = inputs.size() + outputs.size();
			std::erase_if(inputs, gone);
			std::erase_if(outputs, gone);
			const u32 removed = static_cast<u32>(before - inputs.size() - outputs.size());
			if (removed == 0) {
				return 0;
			}
			WriteCustomFunction(*held, inputs, outputs);
			for (u32 port : ports) {
				const str name = std::to_string(port);
				std::erase_if(held->inputs, [&name](const GraphInput& input) { return input.port == name; });
				Vector<std::pair<u32, str>> readers{GetMemoryResource(MemTag::Editor)};
				auto collect = [&](const GraphNode& reader) {
					for (const GraphInput& input : reader.inputs) {
						if (input.fromNode == node && input.fromPort == name) {
							readers.emplace_back(reader.id, input.port);
						}
					}
				};
				for (const GraphNode& reader : document.nodes) {
					collect(reader);
				}
				for (const GraphContext& context : document.contexts) {
					for (const GraphNode& block : context.blocks) {
						collect(block);
					}
				}
				for (const auto& [reader, input] : readers) {
					GraphDocumentOps::Disconnect(document, reader, input);
				}
			}
			return removed;
		}

		std::span<const std::string_view> PropertyTypes() { return PROPERTY_TYPES; }

		std::string_view PropertyTypeLabel(std::string_view type) {
			if (type == KEYWORD_TYPE) {
				return "Boolean Keyword";
			}
			return type == ENUM_KEYWORD_TYPE ? std::string_view("Enum Keyword") : type;
		}

		u32 AddProperty(GraphDocument& document, std::string_view type) {
			if (std::find(std::begin(PROPERTY_TYPES), std::end(PROPERTY_TYPES), type) == std::end(PROPERTY_TYPES)) {
				return 0;
			}
			GraphProperty property;
			property.id = document.nextId++;
			property.type = str(type);
			const bool keyword = IsKeywordType(type);
			property.name = UniqueName(document, keyword ? PropertyTypeLabel(type) : type, 0);
			property.reference = UniqueReference(document, ReferenceFor(property.name, keyword), 0);
			property.defaultValue = DefaultFor(type);
			if (type == ENUM_KEYWORD_TYPE) {
				Vector<GraphKeywordEntry> entries{GetMemoryResource(MemTag::Editor)};
				for (u32 i = 0; i < 3; ++i) {
					const str letter(1, static_cast<char>('A' + i));
					entries.push_back({i + 1, letter, letter});
				}
				WriteKeyword(property, entries);
			}
			document.properties.push_back(std::move(property));
			return document.properties.back().id;
		}

		bool RenameProperty(GraphDocument& document, u32 id, std::string_view name) {
			GraphProperty* property = FindProperty(document, id);
			if (!property || name.empty() || name == property->name) {
				return false;
			}
			const bool keyword = IsKeywordType(property->type);
			const bool referenceFollows = property->reference == UniqueReference(document, ReferenceFor(property->name, keyword), id) ||
										  property->reference == ReferenceFor(property->name, keyword);
			property->name = UniqueName(document, name, id);
			if (referenceFollows) {
				property->reference = UniqueReference(document, ReferenceFor(property->name, keyword), id);
			}
			return true;
		}

		bool SetPropertyReference(GraphDocument& document, u32 id, std::string_view reference) {
			GraphProperty* property = FindProperty(document, id);
			if (!property || reference.empty() || reference == property->reference) {
				return false;
			}
			property->reference = UniqueReference(document, reference, id);
			return true;
		}

		bool SetPropertyDefault(GraphDocument& document, u32 id, std::string_view json) {
			GraphProperty* property = FindProperty(document, id);
			if (!property || property->defaultValue == json) {
				return false;
			}
			property->defaultValue = str(json);
			return true;
		}

		bool SetPropertySetting(GraphDocument& document, u32 id, std::string_view key, std::string_view json) {
			GraphProperty* property = FindProperty(document, id);
			if (!property || key.empty()) {
				return false;
			}
			if (json.empty()) {
				return EraseSetting(property->settings, key);
			}
			GraphSetting& setting = SettingFor(property->settings, key);
			if (setting.value == json) {
				return false;
			}
			setting.value = str(json);
			return true;
		}

		bool MoveProperty(GraphDocument& document, u32 id, u32 index) {
			auto at = std::find_if(document.properties.begin(), document.properties.end(), [&](const GraphProperty& held) { return held.id == id; });
			if (at == document.properties.end() || index >= document.properties.size()) {
				return false;
			}
			const size_t from = static_cast<size_t>(at - document.properties.begin());
			if (from == index) {
				return false;
			}
			GraphProperty moved = std::move(*at);
			document.properties.erase(at);
			document.properties.insert(document.properties.begin() + index, std::move(moved));
			return true;
		}

		u32 PropertyOf(const GraphDocument& document, u32 node) {
			const GraphNode* held = FindConst(document, node);
			if (!held || (held->def != PROPERTY_NODE && held->def != KEYWORD_NODE)) {
				return 0;
			}
			for (const GraphSetting& setting : held->settings) {
				if (setting.key == PROPERTY_SETTING) {
					return static_cast<u32>(std::strtoul(setting.value.c_str(), nullptr, 10));
				}
			}
			return 0;
		}

		u32 DeleteProperties(GraphDocument& document, std::span<const u32> properties) {
			HashSet<u32> doomed{GetMemoryResource(MemTag::Editor)};
			doomed.insert(properties.begin(), properties.end());
			Vector<u32> readers{GetMemoryResource(MemTag::Editor)};
			for (const GraphNode& node : document.nodes) {
				if (doomed.contains(PropertyOf(document, node.id))) {
					readers.push_back(node.id);
				}
			}
			if (!readers.empty()) {
				GraphDocumentOps::Delete(document, readers);
			}
			const size_t before = document.properties.size();
			std::erase_if(document.properties, [&](const GraphProperty& property) { return doomed.contains(property.id); });
			return static_cast<u32>(before - document.properties.size());
		}

		u32 AddPropertyNode(GraphDocument& document, u32 property, Vec2 position) {
			const GraphProperty* read = FindProperty(document, property);
			if (!read) {
				return 0;
			}
			const u32 id = AddNode(document, IsKeywordType(read->type) ? KEYWORD_NODE : PROPERTY_NODE, position);
			if (id != 0) {
				SetNodeSetting(document, id, PROPERTY_SETTING, std::to_string(property));
			}
			return id;
		}

		u32 AddKeywordEntry(GraphDocument& document, u32 property) {
			GraphProperty* held = nullptr;
			Vector<GraphKeywordEntry> entries{GetMemoryResource(MemTag::Editor)};
			if (!ReadKeyword(document, property, held, entries) || entries.size() >= GRAPH_KEYWORD_MAX_ENTRIES) {
				return 0;
			}
			u32 id = 1;
			while (std::any_of(entries.begin(), entries.end(), [id](const GraphKeywordEntry& entry) { return entry.id == id; })) {
				++id;
			}
			GraphKeywordEntry entry{id, {}, {}};
			entry.name = Deduplicate("New", " ", [&](const str& name) {
				return std::any_of(entries.begin(), entries.end(), [&](const GraphKeywordEntry& held) { return held.name == name; });
			});
			entry.reference = Deduplicate(EntrySuffix(entry.name), "_", [&](const str& suffix) {
				return std::any_of(entries.begin(), entries.end(), [&](const GraphKeywordEntry& held) { return held.reference == suffix; });
			});
			if (entry.reference != EntrySuffix(entry.name)) {
				entry.name = entry.reference;
			}
			entries.push_back(std::move(entry));
			WriteKeyword(*held, entries);
			return id;
		}

		bool RenameKeywordEntry(GraphDocument& document, u32 property, u32 entry, std::string_view name) {
			GraphProperty* held = nullptr;
			Vector<GraphKeywordEntry> entries{GetMemoryResource(MemTag::Editor)};
			name = Trim(name);
			if (name.empty() || name.find_first_of("\"\r\n") != std::string_view::npos || !ReadKeyword(document, property, held, entries)) {
				return false;
			}
			auto found = std::find_if(entries.begin(), entries.end(), [entry](const GraphKeywordEntry& candidate) { return candidate.id == entry; });
			const str suffix = EntrySuffix(name);
			const bool taken = std::any_of(entries.begin(), entries.end(), [&](const GraphKeywordEntry& other) {
				return other.id != entry && other.reference == suffix;
			});
			if (found == entries.end() || taken || (found->name == name && found->reference == suffix)) {
				return false;
			}
			found->name = str(name);
			found->reference = suffix;
			WriteKeyword(*held, entries);
			return true;
		}

		bool MoveKeywordEntry(GraphDocument& document, u32 property, u32 entry, u32 index) {
			GraphProperty* held = nullptr;
			Vector<GraphKeywordEntry> entries{GetMemoryResource(MemTag::Editor)};
			if (!ReadKeyword(document, property, held, entries)) {
				return false;
			}
			auto found = std::find_if(entries.begin(), entries.end(), [entry](const GraphKeywordEntry& candidate) { return candidate.id == entry; });
			if (found == entries.end()) {
				return false;
			}
			const size_t from = static_cast<size_t>(found - entries.begin());
			const size_t to = std::min<size_t>(index, entries.size() - 1);
			if (from == to) {
				return false;
			}
			const size_t current = DefaultEntry(*held);
			const u32 defaultId = current < entries.size() ? entries[current].id : 0;
			GraphKeywordEntry moving = std::move(*found);
			entries.erase(found);
			entries.insert(entries.begin() + static_cast<std::ptrdiff_t>(to), std::move(moving));
			for (size_t i = 0; i < entries.size(); ++i) {
				if (entries[i].id == defaultId) {
					held->defaultValue = std::to_string(i);
				}
			}
			WriteKeyword(*held, entries);
			return true;
		}

		u32 RemoveKeywordEntries(GraphDocument& document, u32 property, std::span<const u32> doomed) {
			GraphProperty* held = nullptr;
			Vector<GraphKeywordEntry> entries{GetMemoryResource(MemTag::Editor)};
			if (!ReadKeyword(document, property, held, entries)) {
				return 0;
			}
			auto gone = [doomed](const GraphKeywordEntry& entry) { return std::find(doomed.begin(), doomed.end(), entry.id) != doomed.end(); };
			const size_t removed = static_cast<size_t>(std::count_if(entries.begin(), entries.end(), gone));
			if (removed == 0 || entries.size() - removed < GRAPH_KEYWORD_MIN_ENTRIES) {
				return 0;
			}
			const size_t current = DefaultEntry(*held);
			const u32 defaultId = current < entries.size() && !gone(entries[current]) ? entries[current].id : 0;
			std::erase_if(entries, gone);
			size_t next = std::min(current, entries.size() - 1);
			for (size_t i = 0; i < entries.size(); ++i) {
				next = entries[i].id == defaultId ? i : next;
			}
			held->defaultValue = std::to_string(next);
			WriteKeyword(*held, entries);
			for (GraphNode& node : document.nodes) {
				if (PropertyOf(document, node.id) != property) {
					continue;
				}
				for (u32 id : doomed) {
					const str name = std::to_string(id);
					std::erase_if(node.inputs, [&name](const GraphInput& input) { return input.port == name; });
				}
			}
			return static_cast<u32>(removed);
		}

		str Copy(const GraphDocument& document, std::span<const u32> ids) {
			Vector<u32> copyable{GetMemoryResource(MemTag::Editor)};
			for (u32 id : ids) {
				const bool stack = std::any_of(document.contexts.begin(), document.contexts.end(), [&](const GraphContext& context) {
					return context.node.id == id ||
						   std::any_of(context.blocks.begin(), context.blocks.end(), [&](const GraphNode& block) { return block.id == id; });
				});
				if (!stack) {
					copyable.push_back(id);
				}
			}
			return CopyWithProperties(document, copyable);
		}

		str CopyWithProperties(const GraphDocument& document, std::span<const u32> ids) {
			if (ids.empty()) {
				return {};
			}
			const str nodes = GraphDocumentOps::Copy(document, ids);
			if (nodes.empty()) {
				return {};
			}
			GraphDocument fragment;
			str error;
			if (!GraphDocumentOps::Read(nodes, fragment, error)) {
				return {};
			}
			HashSet<u32> wanted{GetMemoryResource(MemTag::Editor)};
			for (const GraphNode& node : fragment.nodes) {
				const u32 property = PropertyOf(fragment, node.id);
				if (property != 0) {
					wanted.insert(property);
				}
			}
			for (const GraphProperty& property : document.properties) {
				if (wanted.contains(property.id)) {
					fragment.properties.push_back(property);
				}
			}
			return GraphDocumentOps::Write(fragment);
		}

		bool Paste(GraphDocument& document, std::string_view text, Vec2 offset, Vector<u32>& pasted, str& error) {
			GraphDocument fragment;
			if (!GraphDocumentOps::Read(text, fragment, error)) {
				return false;
			}
			if (fragment.type != document.type) {
				error = std::format("a {} fragment can't be pasted into a {} document", fragment.type, document.type);
				return false;
			}
			fragment.contexts.clear();

			HashMap<u32, u32> remap{GetMemoryResource(MemTag::Editor)};
			for (const GraphProperty& incoming : fragment.properties) {
				u32 into = 0;
				for (const GraphProperty& held : document.properties) {
					if (held.reference == incoming.reference && held.type == incoming.type) {
						into = held.id;
					}
				}
				if (into == 0) {
					GraphProperty copy = incoming;
					copy.id = document.nextId++;
					copy.name = UniqueName(document, incoming.name, 0);
					copy.reference = UniqueReference(document, incoming.reference, 0);
					into = copy.id;
					document.properties.push_back(std::move(copy));
				}
				remap[incoming.id] = into;
			}
			fragment.properties.clear();
			for (GraphNode& node : fragment.nodes) {
				const u32 property = PropertyOf(fragment, node.id);
				if (property == 0) {
					continue;
				}
				auto found = remap.find(property);
				if (found != remap.end()) {
					SettingFor(node.settings, PROPERTY_SETTING).value = std::to_string(found->second);
				}
			}
			return GraphDocumentOps::Paste(document, GraphDocumentOps::Write(fragment), offset, pasted, error);
		}

	} // namespace NodeShaderEditOps

} // namespace Shalltear
