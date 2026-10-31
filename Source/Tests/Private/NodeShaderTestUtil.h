#pragma once

#include "GraphCompiler.h"
#include "GraphHLSL.h"
#include "GraphNodeLibrary.h"

#include "Shalltear/Editor/GraphDocument.h"

#include <array>
#include <span>
#include <string_view>

namespace Shalltear::NodeShaderTest {

	struct DocumentBuilder {
		GraphDocument document;

		DocumentBuilder() { document.type = "NodeShader"; }

		GraphNode& Node(u32 id, std::string_view def) {
			GraphNode node;
			node.id = id;
			node.def.assign(def);
			document.nodes.push_back(std::move(node));
			document.nextId = std::max(document.nextId, id + 1);
			return document.nodes.back();
		}

		u32 Property(u32 propertyId, u32 nodeId, std::string_view reference, std::string_view type) {
			GraphProperty property;
			property.id = propertyId;
			property.name.assign(reference);
			property.reference.assign(reference);
			property.type.assign(type);
			document.properties.push_back(std::move(property));
			Setting(Node(nodeId, "Input/Property"), "property", std::to_string(propertyId));
			return nodeId;
		}

		static void Setting(GraphNode& node, std::string_view key, std::string_view json) {
			GraphSetting setting;
			setting.key.assign(key);
			setting.value.assign(json);
			node.settings.push_back(std::move(setting));
		}

		static void Constant(GraphNode& node, std::string_view port, std::string_view json) {
			GraphInput input;
			input.port.assign(port);
			input.value.assign(json);
			node.inputs.push_back(std::move(input));
		}

		void Connect(u32 from, std::string_view fromPort, u32 to, std::string_view toPort) {
			GraphDocumentOps::Connect(document, from, fromPort, to, toPort);
		}
	};

	struct GraphBuilder : DocumentBuilder {
		GraphContext& Context(u32 id, std::string_view def) {
			GraphContext context;
			context.node.id = id;
			context.node.def.assign(def);
			document.contexts.push_back(std::move(context));
			document.nextId = std::max(document.nextId, id + 1);
			return document.contexts.back();
		}

		GraphNode& Block(GraphContext& context, u32 id, std::string_view def) {
			GraphNode block;
			block.id = id;
			block.def.assign(def);
			context.blocks.push_back(std::move(block));
			document.nextId = std::max(document.nextId, id + 1);
			return context.blocks.back();
		}

		void Graph(std::string_view key, std::string_view json) { document.settings.push_back({str(key), str(json)}); }

		GraphProperty& Blackboard(u32 id, std::string_view name, std::string_view reference, std::string_view type, std::string_view defaultJson = {}) {
			GraphProperty property;
			property.id = id;
			property.name.assign(name);
			property.reference.assign(reference);
			property.type.assign(type);
			property.defaultValue.assign(defaultJson);
			document.properties.push_back(std::move(property));
			return document.properties.back();
		}
	};

	struct UnlitBuilder : GraphBuilder {
		GraphContext* fragment = nullptr;

		UnlitBuilder() {
			Graph("target", "\"unlit\"");
			fragment = &Context(1, "Context/Fragment");
			Block(*fragment, 2, "Block/BaseColor");
		}
	};

	struct Compiled {
		bool ok = false;
		GraphProgram program;
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		GraphHLSLText text;

		str Report() const {
			str out;
			for (const GraphDiagnostic& diagnostic : diagnostics) {
				out += "node " + std::to_string(diagnostic.node) + ": " + diagnostic.message + "\n";
			}
			return out;
		}

		bool Names(u32 node, std::string_view fragment) const {
			for (const GraphDiagnostic& diagnostic : diagnostics) {
				if (diagnostic.node == node && diagnostic.message.find(fragment) != str::npos) {
					return true;
				}
			}
			return false;
		}
	};

	Compiled Compile(const GraphDocument& document, std::initializer_list<std::pair<u32, std::string_view>> roots,
					 GraphStageMask stage = GraphStageFragment, std::span<const GraphBinding> bindings = {});

	std::span<const GraphBinding> TestBindings();
	std::string_view TestBindingExpression(std::string_view name);

	inline constexpr u32 REF_INPUTS = 12;

	struct RefValue {
		std::array<f32, 16> c{};
		u32 n = 1;
		f32 operator[](u32 i) const { return n == 1 ? c[0] : c[i]; }
	};

	enum class Feed : u8 {
		None = 0,
		F1,
		F2,
		F3,
		F4,
		M2,
		M3,
		M4,
		B1
	};

	struct RefCase {
		std::array<std::array<RefValue, REF_INPUTS>, 3> corners{};
		std::array<Feed, REF_INPUTS> feeds{};
		const RefValue& In(u32 input, u32 corner = 0) const { return corners[corner][input]; }
		bool IsMatrix(u32 input) const { return feeds[input] == Feed::M2 || feeds[input] == Feed::M3 || feeds[input] == Feed::M4; }
	};

	using RefFunction = void (*)(const RefCase& c, u32 variant, std::span<RefValue> out);

	u32 FeedComponents(Feed feed);

	struct RefShape {
		std::array<Feed, REF_INPUTS> inputs{};
	};

	struct RefBinding {
		std::string_view name;
		Feed feed = Feed::None;
		u32 input = 0;
	};

	struct RefVariant {
		std::string_view settings;
		u32 tag = 0;
	};

	enum class RefCheck : u8 {
		Value,
		Range
	};

	struct RefEntry {
		std::string_view id;
		RefFunction function = nullptr;
		std::span<const RefShape> shapes;
		std::span<const RefVariant> variants;
		std::array<std::array<f32, 2>, REF_INPUTS> domains{};
		f32 absolute = 1e-6f;
		f32 relative = 1e-6f;
		bool derivatives = false;
		RefCheck check = RefCheck::Value;
		void (*range)(const RefCase& c, std::span<std::array<f32, 2>> out) = nullptr;
		bool (*decidable)(const RefCase& c, u32 variant) = nullptr;
		void (*prepare)(std::array<RefValue, REF_INPUTS>& inputs, u32 index) = nullptr;
		std::span<const RefBinding> bindings;
		u32 unrooted = 0;
		void (*graph)(DocumentBuilder& builder, u32 variant) = nullptr;
	};

	std::span<const RefEntry> ReferenceTable();

} // namespace Shalltear::NodeShaderTest
