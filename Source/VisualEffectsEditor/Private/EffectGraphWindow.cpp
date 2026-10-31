#include "EffectGraphWindow.h"

#include "EffectAsset.h"
#include "EffectAssetSettings.h"
#include "EffectControls.h"
#include "EffectInstance.h"
#include "EffectLiveCompile.h"

#include "EffectGraphEditOps.h"
#include "EffectGraphGizmos.h"
#include "GraphDefinitions.h"
#include "GraphEffect.h"
#include "GraphNodeLibrary.h"
#include "GraphProperties.h"
#include "GraphShading.h"
#include "NodeShaderDocuments.h"
#include "NodeShaderEditOps.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"
#include "Shalltear/Core/Window/WindowSystem.h"

#include "Shalltear/Core/Serialization/JSON.h"

#include "Shalltear/Editor/AssetOpeners.h"
#include "Shalltear/Editor/EditorAssetWatch.h"
#include "Shalltear/Editor/EditorDragData.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/EditorViewportOps.h"
#include "Shalltear/Editor/GraphDocument.h"
#include "Shalltear/Editor/ObjectField.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/PropertyRows.h"

#include "Shalltear/VisualEffects/VisualEffect.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <cstdlib>
#include <filesystem>
#include <format>

namespace Shalltear {

	namespace {

		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;
		namespace Ops = EffectGraphEditOps;
		namespace Shared = NodeShaderEditOps;
		using GraphWindowParts::ActiveInputs;
		using GraphWindowParts::AddField;
		using GraphWindowParts::AddText;
		using GraphWindowParts::FindInput;
		using GraphWindowParts::FindProperty;
		using GraphWindowParts::FontRow;
		using GraphWindowParts::InlineFor;

		using GraphEffectTarget::HeldMesh;
		using GraphEffectTarget::HeldTexture;

		str TextureCaption(const Guid& texture, RHI::TextureDimension dimension) {
			const Path path = texture.IsValid() ? AssetDatabase::LookupPath(texture) : Path();
			if (!path.ToString().empty()) {
				return str(path.GetFilename());
			}
			return dimension == RHI::TextureDimension::Texture2D ? str("None (White)") : str("None (Gray)");
		}

		str MeshCaption(const Guid& mesh) {
			const Path path = mesh.IsValid() ? AssetDatabase::LookupPath(mesh) : Path();
			return path.ToString().empty() ? str("None (Mesh)") : str(path.GetFilename());
		}

		bool TakesMesh(const UI::DragPayload& payload) {
			return payload.type == "Asset"_hash && payload.data.Is<AssetDragData>() &&
				   StringHash{payload.data.As<AssetDragData>().assetTypeHash} == StringHash("MeshAsset");
		}

		RHI::TextureDimension PortDimension(GraphPortType type) {
			switch (type) {
			case GraphPortType::Texture2D:
				return RHI::TextureDimension::Texture2D;
			case GraphPortType::Texture2DArray:
				return RHI::TextureDimension::Texture2DArray;
			case GraphPortType::Texture3D:
				return RHI::TextureDimension::Texture3D;
			case GraphPortType::Cubemap:
				return RHI::TextureDimension::Cube;
			default:
				return RHI::TextureDimension::Count;
			}
		}

		constexpr f32 NODE_WIDTH = 240.0f;
		constexpr f32 CONTEXT_WIDTH = 300.0f;
		constexpr f32 BLACKBOARD_WIDTH = 220.0f;
		constexpr f32 INSPECTOR_WIDTH = 280.0f;
		constexpr f32 PASTE_STEP = 24.0f;
		constexpr f32 BOARD_INSET = 10.0f;

		constexpr u64 CONTEXT_KEY = 1ull << 63;
		constexpr u64 PROPERTY_KEY = 1ull << 62;
		constexpr u64 VARIANT_KEY = 1ull << 61;
		constexpr u64 SUBGRAPH_KEY = 1ull << 60;
		constexpr u64 FLOW_WIRE = 1ull << 63;

		bool EndsWith(std::string_view path, std::string_view extension) {
			if (path.size() < extension.size()) {
				return false;
			}
			const std::string_view tail = path.substr(path.size() - extension.size());
			return std::equal(tail.begin(), tail.end(), extension.begin(), [](char a, char b) {
				return std::tolower(static_cast<unsigned char>(a)) == b;
			});
		}

		constexpr std::string_view EDITED_EXTENSIONS[] = {GRAPH_EFFECT_EXTENSION, GRAPH_EFFECT_OPERATOR_SUBGRAPH_EXTENSION,
														  GRAPH_EFFECT_BLOCK_SUBGRAPH_EXTENSION};
		bool IsEditedPath(std::string_view path) {
			return std::any_of(std::begin(EDITED_EXTENSIONS), std::end(EDITED_EXTENSIONS), [path](std::string_view extension) { return EndsWith(path, extension); });
		}

		bool DraggedSubgraph(const UI::DragPayload& payload, std::string_view extension, Guid& out) {
			if (payload.type != "Asset"_hash || !payload.data.Is<AssetDragData>()) {
				return false;
			}
			out = payload.data.As<AssetDragData>().guid;
			return out.IsValid() && EndsWith(AssetDatabase::LookupPath(out).ToString(), extension);
		}

		const GraphContext* FindContext(const GraphDocument& document, u32 id) {
			for (const GraphContext& context : document.contexts) {
				if (context.node.id == id) {
					return &context;
				}
			}
			return nullptr;
		}

		const GraphContext* HolderOf(const GraphDocument& document, u32 block) {
			for (const GraphContext& context : document.contexts) {
				for (const GraphNode& held : context.blocks) {
					if (held.id == block) {
						return &context;
					}
				}
			}
			return nullptr;
		}

		void InGraphSettings(const GraphNodeDefinition& definition, const GraphNode& node, Vector<const GraphSettingDefinition*>& out) {
			out.clear();
			for (u32 index = 0; index < definition.settings.size(); ++index) {
				if (definition.settings[index].inGraph && GraphNodeLibrary::SettingActive(definition.settings, index, node)) {
					out.push_back(&definition.settings[index]);
				}
			}
		}

		GraphSettingEdit EditOf(const GraphSettingDefinition& setting) {
			return setting.kind == GraphSettingKind::Text || setting.kind == GraphSettingKind::Number ? GraphSettingEdit::Text : GraphSettingEdit::Menu;
		}

		bool IsColorPort(const GraphNode& node, const GraphPortDefinition& port) {
			return node.def == GRAPH_EFFECT_SET_ATTRIBUTE && port.type == GraphPortType::Float3 && std::string_view(port.name) == "Color";
		}

		bool IsEventName(const GraphNode& node, const GraphSettingDefinition& setting) {
			return node.def == GRAPH_EFFECT_EVENT && setting.kind == GraphSettingKind::Text;
		}

		str MissingLabel(const GraphNode& node) {
			const Guid asset = GraphSubgraphs::AssetOf(node);
			return asset.IsValid() ? GraphSubgraphs::NameOf(asset) + " (missing)" : node.def + " (unknown)";
		}

	} // anonymous namespace

	class EffectGraphWindow::Model final : public GraphViewModel {
	public:
		explicit Model(EffectGraphWindow& window) : _window(window) {}

		void GetNodes(Vector<GraphNodeDesc>& out) override;
		bool GetNodePosition(GraphNodeId node, Vec2& out) const override;
		void GetWires(Vector<GraphWireDesc>& out) const override;
		ColorToken GetNodeBorder(GraphNodeId node) const override;
		str GetNodeTooltip(GraphNodeId node) const override;
		void GetOutlines(Vector<GraphOutlineDesc>& out) const override;
		void RenameOutline(u64 outline, std::string_view title) override;
		void SetInlineValue(const GraphPortRef& input, std::span<const f32> values) override;
		void OnInlineEditBegin() override { _window.BeginEdit("Edit Value"); }
		void OnInlineEditEnd() override { _window.EndEdit(); }
		void OpenInlineEditor(UI::Context& context, const GraphPortRef& input, UI::ElementHandle field) override;
		void OnSelectionChanged() override;
		void OnMoveBegin() override { _window.BeginEdit("Move"); }
		void MoveNode(GraphNodeId node, Vec2 graph) override;
		void OnMoveEnd() override { _window.EndEdit(); }
		void OnNodeContextMenu(UI::Context& context, GraphNodeId node) override;
		void OnCanvasContextMenu(UI::Context& context, Vec2 graph) override;
		bool AcceptsDrop(const UI::DragPayload& payload) const override;
		void OnDrop(const UI::DragPayload& payload, Vec2 graph) override;
		bool AcceptsNodeDrop(GraphNodeId node, const UI::DragPayload& payload) const override;
		void OnNodeDrop(GraphNodeId node, const UI::DragPayload& payload) override;
		bool CanConnect(const GraphPortRef& output, const GraphPortRef& input) const override;
		void Connect(const GraphPortRef& output, const GraphPortRef& input) override;
		void GetSearchEntries(const GraphPortRef* from, Vector<GraphSearchEntry>& out) override;
		void OnSearchPick(const GraphSearchEntry& entry, Vec2 graph, const GraphPortRef* from) override;
		void GetGroups(Vector<GraphGroupDesc>& out) const override;
		void GetNotes(Vector<GraphNoteDesc>& out) const override;
		void MoveNote(u64 note, Vec2 graph) override;
		void MoveGroup(u64 group, Vec2 graph) override;
		void ResizeNote(u64 note, Vec2 size) override;
		void SetGroupOf(std::span<const GraphItem> items, u64 group) override;
		void RenameGroup(u64 group, std::string_view title) override;
		void EditNote(u64 note, std::string_view title, std::string_view body) override;
		void OnGroupContextMenu(UI::Context& context, u64 group) override;
		void OnNoteContextMenu(UI::Context& context, u64 note) override;
		void OnWireContextMenu(UI::Context& context, GraphWireId wire, Vec2 graph) override;
		void OnWireDoubleClick(GraphWireId wire, Vec2 graph) override;
		bool OnGraphCommand(GraphCommand command) override;
		void OnSettingClick(UI::Context& context, GraphNodeId owner, u32 setting) override;
		void SetSettingText(GraphNodeId owner, u32 setting, std::string_view text) override;
		void OnSettingAction(GraphNodeId owner, u32 setting) override;
		void SetBlockEnabled(GraphNodeId block, bool enabled) override;
		void OnPortSpaceClick(const GraphPortRef& input) override;
		void OnBlockContextMenu(UI::Context& context, GraphNodeId block) override;
		bool CanMoveBlocks(std::span<const GraphNodeId> blocks, GraphNodeId stack) const override;
		void MoveBlocks(std::span<const GraphNodeId> blocks, GraphNodeId stack, u32 index) override;
		void GetBlockSearchEntries(GraphNodeId stack, Vector<GraphSearchEntry>& out) override;
		void OnBlockSearchPick(const GraphSearchEntry& entry, GraphNodeId stack, u32 index) override;

		bool InputOf(const GraphPortRef& ref, u32& node, str& port) const;
		bool OutputOf(const GraphPortRef& ref, u32& node, str& port) const;
		void DrawnOutputs(const GraphNode& node, const GraphNodeDefinition& definition, Vector<u32>& out) const;
		bool FlowOf(const GraphPortRef& ref, u32& context, str& slot) const;
		bool Offered(const Guid& subgraph, u32 context) const;
		bool UsableSystem(const Guid& subgraph) const;
		bool WireInput(GraphWireId wire, u32& node, str& port) const;
		bool WireFlow(GraphWireId wire, u32& from, u32& to, str& slot) const;
	private:
		GraphDocument* Doc() const { return _window.GetDocument(); }
		const GraphNodeDefinition* Definition(const GraphNode& node) const { return _window._definitions.Find(*Doc(), node); }
		GraphNode* Element(u32 id) const {
			GraphDocument* document = Doc();
			return document ? GraphDocumentOps::FindNode(*document, id) : nullptr;
		}
		void OpenMenu(UI::Context& context, const Vector<UI::Widgets::MenuItem>& items);
		void AddClearIgnored(Vector<UI::Widgets::MenuItem>& items, u32 node);
		void DescribePorts(const GraphNode& node, const GraphNodeDefinition& definition, Vector<GraphPortDesc>& out) const;
		void Select(GraphItemKind kind, u32 id);

		EffectGraphWindow& _window;
		Vector<EffectGraphEditOps::BlockChoice> _blockChoices{GetMemoryResource(MemTag::Editor)};
		Vector<EffectGraphEditOps::BlockChoice> _operatorChoices{GetMemoryResource(MemTag::Editor)};
		Vector<EffectGraphEditOps::BlockChoice> _contextChoices{GetMemoryResource(MemTag::Editor)};
		Vector<EffectGraphEditOps::BlockChoice> _subgraphChoices{GetMemoryResource(MemTag::Editor)};
	};

	bool EffectGraphWindow::Model::InputOf(const GraphPortRef& ref, u32& node, str& port) const {
		if (ref.side != GraphPortSide::Input) {
			return false;
		}
		const GraphNode* held = Element(static_cast<u32>(ref.node));
		const GraphNodeDefinition* definition = held ? Definition(*held) : nullptr;
		if (!definition) {
			return false;
		}
		Vector<u32> active{GetMemoryResource(MemTag::Editor)};
		ActiveInputs(*definition, *held, active);
		if (ref.port >= active.size()) {
			return false;
		}
		node = held->id;
		port = str(definition->inputs[active[ref.port]].name);
		return true;
	}

	bool EffectGraphWindow::Model::OutputOf(const GraphPortRef& ref, u32& node, str& port) const {
		if (ref.side != GraphPortSide::Output) {
			return false;
		}
		const GraphNode* held = Element(static_cast<u32>(ref.node));
		const GraphNodeDefinition* definition = held ? Definition(*held) : nullptr;
		if (!definition) {
			return false;
		}
		Vector<u32> drawn{GetMemoryResource(MemTag::Editor)};
		DrawnOutputs(*held, *definition, drawn);
		if (ref.port >= drawn.size()) {
			return false;
		}
		node = held->id;
		port = str(definition->outputs[drawn[ref.port]].name);
		return true;
	}

	void EffectGraphWindow::Model::DrawnOutputs(const GraphNode& node, const GraphNodeDefinition& definition, Vector<u32>& out) const {
		out.clear();
		const bool block = GraphDocumentOps::ContextOf(*Doc(), node.id) != nullptr;
		for (u32 i = 0; i < definition.outputs.size(); ++i) {
			if (!block || definition.outputs[i].type == GraphPortType::GPUEvent) {
				out.push_back(i);
			}
		}
	}

	bool EffectGraphWindow::Model::FlowOf(const GraphPortRef& ref, u32& context, str& slot) const {
		const GraphDocument* document = Doc();
		if (!document || (ref.side != GraphPortSide::FlowInput && ref.side != GraphPortSide::FlowOutput)) {
			return false;
		}
		context = static_cast<u32>(ref.node);
		if (ref.side == GraphPortSide::FlowOutput) {
			slot.clear();
			return Ops::FlowOutput(*document, context);
		}
		const auto slots = Ops::FlowInputs(*document, context, &_window._definitions);
		if (ref.port >= slots.size()) {
			return false;
		}
		slot = str(slots[ref.port]);
		return true;
	}

	bool EffectGraphWindow::Model::WireInput(GraphWireId wire, u32& node, str& port) const {
		return (wire & FLOW_WIRE) == 0 && InputOf(GraphPortRef{wire >> 32, GraphPortSide::Input, static_cast<u32>(wire & 0xffffffffu)}, node, port);
	}

	bool EffectGraphWindow::Model::WireFlow(GraphWireId wire, u32& from, u32& to, str& slot) const {
		const GraphDocument* document = Doc();
		if (!document || (wire & FLOW_WIRE) == 0) {
			return false;
		}
		to = static_cast<u32>((wire >> 32) & 0x7fffffffu);
		from = static_cast<u32>((wire >> 4) & 0x0fffffffu);
		const auto slots = Ops::FlowInputs(*document, to, &_window._definitions);
		const u32 index = static_cast<u32>(wire & 0xfu);
		if (index >= slots.size()) {
			return false;
		}
		slot = str(slots[index]);
		return true;
	}

	void EffectGraphWindow::Model::DescribePorts(const GraphNode& node, const GraphNodeDefinition& definition, Vector<GraphPortDesc>& out) const {
		const GraphDocument* document = Doc();
		const u32 width = Shared::DynamicWidth(*document, node.id, &_window._definitions);
		Vector<u32> active{GetMemoryResource(MemTag::Editor)};
		ActiveInputs(definition, node, active);
		for (u32 index : active) {
			const GraphPortDefinition& input = definition.inputs[index];
			GraphPortDesc port;
			port.label = str(GraphPortLabel(input));
			port.color = Shared::PortColor(input.type);
			const GraphInput* fed = FindInput(node, input.name);
			port.connected = fed && fed->fromNode != 0;
			const GraphSubgraph* subgraph = definition.subgraph;
			const bool color = subgraph && index < subgraph->inputKinds.size() && subgraph->inputKinds[index] == GraphPropertyKind::Color;
			port.inlineValue = InlineFor(node, input, width, color || IsColorPort(node, input));
			const RHI::TextureDimension dimension = PortDimension(input.type);
			if (dimension != RHI::TextureDimension::Count && !port.connected) {
				port.inlineValue.kind = GraphInlineKind::Label;
				port.inlineValue.text = TextureCaption(HeldTexture(node, input.name), dimension);
			} else if (input.type == GraphPortType::Mesh && !port.connected) {
				port.inlineValue.kind = GraphInlineKind::Label;
				port.inlineValue.text = MeshCaption(HeldMesh(node, input.name));
			}
			if (input.space != GraphPortSpace::None) {
				port.space = fed && fed->space == GRAPH_SPACE_WORLD ? "W" : "L";
			}
			out.push_back(std::move(port));
		}
	}

	void EffectGraphWindow::Model::GetNodes(Vector<GraphNodeDesc>& out) {
		out.clear();
		const GraphDocument* document = Doc();
		if (!document) {
			return;
		}
		Vector<const GraphSettingDefinition*> shown{GetMemoryResource(MemTag::Editor)};

		for (const GraphContext& context : document->contexts) {
			GraphNodeDesc desc;
			desc.id = context.node.id;
			desc.size = {CONTEXT_WIDTH, 0.0f};
			desc.fill = ColorToken::GraphNodeMuted;
			desc.stack = true;
			const GraphNodeDefinition* definition = Definition(context.node);
			if (!definition) {
				desc.label = MissingLabel(context.node);
				out.push_back(std::move(desc));
				continue;
			}
			desc.label = str(definition->name);
			DescribePorts(context.node, *definition, desc.inputs);
			InGraphSettings(*definition, context.node, shown);
			for (const GraphSettingDefinition* setting : shown) {
				desc.settings.push_back({str(setting->key), GraphWindowParts::SettingText(context.node, *setting), EditOf(*setting)});
				if (IsEventName(context.node, *setting)) {
					desc.settings.back().action = "Send";
				}
			}
			const bool named = definition->flowInputs.size() > 1 || context.node.def == GRAPH_EFFECT_SYSTEM_SUBGRAPH;
			for (std::string_view slot : definition->flowInputs) {
				GraphFlowPortDesc port;
				port.label = named ? str(slot) : str();
				port.connected = std::any_of(context.flows.begin(), context.flows.end(), [&](const GraphFlow& flow) { return flow.slot == slot; });
				desc.flowInputs.push_back(std::move(port));
			}
			if (definition->flowOutput) {
				GraphFlowPortDesc port;
				port.connected = std::any_of(document->contexts.begin(), document->contexts.end(), [&](const GraphContext& other) {
					return std::any_of(other.flows.begin(), other.flows.end(), [&](const GraphFlow& flow) { return flow.from == context.node.id; });
				});
				desc.flowOutputs.push_back(std::move(port));
			}
			for (const GraphNode& block : context.blocks) {
				GraphBlockDesc entry;
				entry.id = block.id;
				entry.enabled = !block.disabled;
				const GraphNodeDefinition* blockDefinition = Definition(block);
				if (!blockDefinition) {
					entry.label = MissingLabel(block);
					desc.blocks.push_back(std::move(entry));
					continue;
				}
				entry.label = str(blockDefinition->name);
				InGraphSettings(*blockDefinition, block, shown);
				for (const GraphSettingDefinition* setting : shown) {
					entry.settings.push_back({str(setting->key), GraphWindowParts::SettingText(block, *setting), EditOf(*setting)});
				}
				DescribePorts(block, *blockDefinition, entry.inputs);
				Vector<u32> drawn{GetMemoryResource(MemTag::Editor)};
				DrawnOutputs(block, *blockDefinition, drawn);
				for (const u32 index : drawn) {
					const GraphPortDefinition& output = blockDefinition->outputs[index];
					entry.outputs.push_back({str(GraphPortLabel(output)), Shared::PortColor(output.type), false});
				}
				desc.blocks.push_back(std::move(entry));
			}
			out.push_back(std::move(desc));
		}

		for (const GraphNode& node : document->nodes) {
			GraphNodeDesc desc;
			desc.id = node.id;
			desc.size = {NODE_WIDTH, 0.0f};
			const GraphNodeDefinition* definition = Definition(node);
			if (!definition) {
				desc.label = MissingLabel(node);
				out.push_back(std::move(desc));
				continue;
			}
			desc.label = str(definition->name);
			desc.compact = node.def == GraphDocumentOps::REDIRECT_DEF;
			if (const GraphProperty* property = FindProperty(*document, Shared::PropertyOf(*document, node.id))) {
				desc.label = property->name;
			}
			DescribePorts(node, *definition, desc.inputs);
			if (node.def == Shared::PROPERTY_NODE) {
				for (GraphPortDesc& port : desc.inputs) {
					port.label.clear();
				}
			}
			InGraphSettings(*definition, node, shown);
			for (const GraphSettingDefinition* setting : shown) {
				desc.settings.push_back({str(setting->key), GraphWindowParts::SettingText(node, *setting), EditOf(*setting)});
			}
			for (const GraphPortDefinition& output : definition->outputs) {
				GraphPortDesc port;
				port.label = desc.compact || node.def == Shared::PROPERTY_NODE ? str() : str(GraphPortLabel(output));
				port.color = Shared::PortColor(output.type);
				desc.outputs.push_back(std::move(port));
			}
			if (desc.compact) {
				for (GraphPortDesc& port : desc.inputs) {
					port.label.clear();
				}
			}
			out.push_back(std::move(desc));
		}
	}

	bool EffectGraphWindow::Model::GetNodePosition(GraphNodeId node, Vec2& out) const {
		const GraphNode* held = Element(static_cast<u32>(node));
		if (!held) {
			return false;
		}
		out = Vec2(static_cast<f32>(held->x), static_cast<f32>(held->y));
		return true;
	}

	void EffectGraphWindow::Model::GetWires(Vector<GraphWireDesc>& out) const {
		out.clear();
		GraphDocument* document = Doc();
		if (!document) {
			return;
		}
		auto addInputs = [&](const GraphNode& node) {
			const GraphNodeDefinition* definition = Definition(node);
			if (!definition) {
				return;
			}
			Vector<u32> active{GetMemoryResource(MemTag::Editor)};
			ActiveInputs(*definition, node, active);
			for (const GraphInput& input : node.inputs) {
				const i32 port = input.fromNode != 0 ? GraphNodeLibrary::FindInput(*definition, input.port) : -1;
				const auto drawn = std::find(active.begin(), active.end(), static_cast<u32>(port));
				if (port < 0 || drawn == active.end()) {
					continue;
				}
				const GraphNode* source = GraphDocumentOps::FindNode(*document, input.fromNode);
				const GraphNodeDefinition* sourceDefinition = source ? Definition(*source) : nullptr;
				const i32 named = sourceDefinition ? GraphNodeLibrary::FindOutput(*sourceDefinition, input.fromPort) : -1;
				if (named < 0) {
					continue;
				}
				Vector<u32> sourceOutputs{GetMemoryResource(MemTag::Editor)};
				DrawnOutputs(*source, *sourceDefinition, sourceOutputs);
				const auto place = std::find(sourceOutputs.begin(), sourceOutputs.end(), static_cast<u32>(named));
				if (place == sourceOutputs.end()) {
					continue;
				}
				const i32 output = static_cast<i32>(place - sourceOutputs.begin());
				const u32 row = static_cast<u32>(drawn - active.begin());
				GraphWireDesc wire;
				wire.id = (static_cast<u64>(node.id) << 32) | row;
				wire.style = GraphWireStyle::Curve;
				wire.from = {input.fromNode, GraphPortSide::Output, static_cast<u32>(output)};
				wire.to = {node.id, GraphPortSide::Input, row};
				wire.color = Shared::PortColor(sourceDefinition->outputs[static_cast<size_t>(named)].type);
				out.push_back(wire);
			}
		};
		for (const GraphNode& node : document->nodes) {
			addInputs(node);
		}
		for (const GraphContext& context : document->contexts) {
			addInputs(context.node);
			for (const GraphNode& block : context.blocks) {
				addInputs(block);
			}
		}
		for (const GraphContext& context : document->contexts) {
			const auto slots = Ops::FlowInputs(*document, context.node.id, &_window._definitions);
			for (const GraphFlow& flow : context.flows) {
				const auto slot = std::find(slots.begin(), slots.end(), std::string_view(flow.slot));
				if (slot == slots.end() || !Ops::FlowOutput(*document, flow.from)) {
					continue;
				}
				const u32 index = static_cast<u32>(slot - slots.begin());
				GraphWireDesc wire;
				wire.id = FLOW_WIRE | (static_cast<u64>(context.node.id) << 32) | (static_cast<u64>(flow.from & 0x0fffffffu) << 4) | index;
				wire.style = GraphWireStyle::Flow;
				wire.from = {flow.from, GraphPortSide::FlowOutput, 0};
				wire.to = {context.node.id, GraphPortSide::FlowInput, index};
				wire.color = ColorToken::GraphFlow;
				out.push_back(wire);
			}
		}
	}

	ColorToken EffectGraphWindow::Model::GetNodeBorder(GraphNodeId node) const {
		return GraphWindowParts::MarkOf(_window._diagnostics, [node](const GraphDiagnostic& diagnostic) { return diagnostic.node != 0 && diagnostic.node == node; });
	}

	str EffectGraphWindow::Model::GetNodeTooltip(GraphNodeId node) const {
		return GraphWindowParts::TooltipOf(_window._diagnostics, [node](const GraphDiagnostic& diagnostic) { return diagnostic.node != 0 && diagnostic.node == node; });
	}

	void EffectGraphWindow::Model::GetOutlines(Vector<GraphOutlineDesc>& out) const {
		out.clear();
		const GraphDocument* document = Doc();
		if (!document) {
			return;
		}
		auto fedBy = [](const GraphContext& context, u32 source) {
			return std::any_of(context.flows.begin(), context.flows.end(), [source](const GraphFlow& flow) { return flow.from == source; });
		};
		_window.ReadRecordedSystems();
		for (const GraphContext& initialize : document->contexts) {
			if (initialize.node.def != GRAPH_EFFECT_INITIALIZE) {
				continue;
			}
			GraphOutlineDesc outline;
			outline.nodes.push_back(initialize.node.id);
			outline.id = initialize.node.id;
			const GraphSetting* named = GraphWindowParts::FindSetting(initialize.node.settings, GRAPH_EFFECT_SYSTEM_NAME);
			outline.title = named ? GraphWindowParts::ReadString(named->value) : str();
			outline.title = outline.title.empty() ? str(GRAPH_EFFECT_DEFAULT_SYSTEM_NAME) : outline.title;
			for (const RecordedSystem& system : _window._recordedSystems) {
				outline.title = system.initialize == initialize.node.id ? system.name : outline.title;
			}
			u32 update = 0;
			for (const GraphContext& context : document->contexts) {
				if (context.node.def == GRAPH_EFFECT_UPDATE && fedBy(context, initialize.node.id)) {
					update = context.node.id;
					outline.nodes.push_back(update);
				}
			}
			for (const GraphContext& context : document->contexts) {
				if (GraphEffectRules::IsOutput(context.node.def) && (fedBy(context, initialize.node.id) || (update != 0 && fedBy(context, update)))) {
					outline.nodes.push_back(context.node.id);
				}
			}
			out.push_back(std::move(outline));
		}
	}

	void EffectGraphWindow::Model::RenameOutline(u64 outline, std::string_view title) {
		const u32 initialize = static_cast<u32>(outline);
		const str value = title.empty() ? str() : Shared::Quote(title);
		_window.Edit("Rename System", [&](GraphDocument& document) {
			return Shared::SetNodeSetting(document, initialize, GRAPH_EFFECT_SYSTEM_NAME, value, &_window._definitions);
		});
	}

	void EffectGraphWindow::Model::SetInlineValue(const GraphPortRef& input, std::span<const f32> values) {
		u32 node = 0;
		str port;
		if (!InputOf(input, node, port)) {
			return;
		}
		const GraphNode* held = Element(node);
		const GraphNodeDefinition* definition = held ? Definition(*held) : nullptr;
		const i32 index = definition ? GraphNodeLibrary::FindInput(*definition, port) : -1;
		std::span<const f32> stored = values;
		if (index >= 0 && definition->inputs[static_cast<size_t>(index)].type == GraphPortType::Float3 && values.size() > 3) {
			stored = values.first(3);
		}
		_window.Edit("Edit Value", [&](GraphDocument& document) { return Shared::SetInputConstant(document, node, port, stored); });
	}

	void EffectGraphWindow::Model::OpenInlineEditor(UI::Context& context, const GraphPortRef& input, UI::ElementHandle field) {
		u32 node = 0;
		str port;
		const GraphNode* held = InputOf(input, node, port) ? Element(node) : nullptr;
		const GraphNodeDefinition* definition = held ? Definition(*held) : nullptr;
		const i32 index = definition ? GraphNodeLibrary::FindInput(*definition, port) : -1;
		if (index >= 0) {
			GraphWindowParts::OpenCurveEditor(context, field, node, definition->inputs[static_cast<size_t>(index)], _window.MakeEditor());
		}
	}

	void EffectGraphWindow::Model::OnSelectionChanged() {
		const std::span<const GraphItem> selected = _window._canvas.GetSelection();
		u32 inspected = 0;
		if (!selected.empty() && (selected.back().kind == GraphItemKind::Node || selected.back().kind == GraphItemKind::Block)) {
			inspected = static_cast<u32>(selected.back().id);
		}
		if (inspected != _window._inspected || _window._inspectedProperty != 0) {
			_window._inspected = inspected;
			_window._inspectedProperty = 0;
			_window._needsInspectorRebuild = true;
		}
	}

	void EffectGraphWindow::Model::Select(GraphItemKind kind, u32 id) {
		const GraphItem item{kind, id};
		_window._canvas.SetSelection({&item, 1});
		_window._inspected = id;
		_window._inspectedProperty = 0;
		_window._needsInspectorRebuild = true;
	}

	void EffectGraphWindow::Model::MoveNode(GraphNodeId node, Vec2 graph) {
		if (GraphDocument* document = Doc()) {
			GraphDocumentOps::MoveNode(*document, static_cast<u32>(node), graph);
		}
	}

	void EffectGraphWindow::Model::OpenMenu(UI::Context& context, const Vector<UI::Widgets::MenuItem>& items) {
		UI::Widgets::OpenMenu(context, context.GetPointerPosition(), items, UISystem::GetDefaultFont(), FontRow());
	}

	void EffectGraphWindow::Model::OnNodeContextMenu(UI::Context& context, GraphNodeId node) {
		const GraphDocument* document = Doc();
		if (!document) {
			return;
		}
		EffectGraphWindow* window = &_window;
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::UI)};
		const GraphNode* held = Element(static_cast<u32>(node));
		const bool system = held && held->def == GRAPH_EFFECT_SYSTEM_SUBGRAPH;
		if (held && GraphSubgraphs::AssetOf(*held).IsValid()) {
			items.push_back({system ? "Enter Subgraph" : "Open Subgraph", "", true, false,
							 [asset = GraphSubgraphs::AssetOf(*held)]() { EffectGraphWindowRegistry::Open(asset); }});
		}
		if (FindContext(*document, static_cast<u32>(node)) && !system) {
			const Vec2 anchor = context.GetPointerPosition();
			UI::Context* where = &context;
			items.push_back({"Create Block", "", true, false, [window, where, node, anchor]() {
								 window->_canvas.OpenBlockSearcherAt(*where, node, anchor);
							 }});
		}
		UI::Widgets::MenuItem copy{"Copy", "", true, false, [window]() { window->CopySelection(false); }};
		copy.separatorBefore = !items.empty();
		items.push_back(std::move(copy));
		items.push_back({"Cut", "", true, false, [window]() { window->CopySelection(true); }});
		items.push_back({"Duplicate", "", true, false, [window]() { window->DuplicateSelection(); }});
		items.push_back({"Delete", "", true, true, [window]() { window->DeleteSelection(); }});
		AddClearIgnored(items, static_cast<u32>(node));
		OpenMenu(context, items);
	}

	void EffectGraphWindow::Model::AddClearIgnored(Vector<UI::Widgets::MenuItem>& items, u32 node) {
		const GraphNode* held = Element(node);
		EffectGraphWindow* window = &_window;
		UI::Widgets::MenuItem clear{"Clear Ignored Errors", "", held && GraphEffectRules::HasIgnoredErrors(*held), false, [window, node]() {
										window->Edit("Clear Ignored Errors", [node](GraphDocument& document) { return Ops::ClearIgnoredErrors(document, node); });
									}};
		clear.separatorBefore = true;
		items.push_back(std::move(clear));
	}

	void EffectGraphWindow::Model::OnBlockContextMenu(UI::Context& context, GraphNodeId block) {
		const GraphDocument* document = Doc();
		const GraphContext* holder = document ? HolderOf(*document, static_cast<u32>(block)) : nullptr;
		const GraphNode* held = Element(static_cast<u32>(block));
		if (!holder || !held) {
			return;
		}
		EffectGraphWindow* window = &_window;
		const GraphNodeId stack = holder->node.id;
		const Vec2 anchor = context.GetPointerPosition();
		UI::Context* where = &context;
		const bool enabled = !held->disabled;
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::UI)};
		if (const Guid asset = GraphSubgraphs::AssetOf(*held); asset.IsValid()) {
			items.push_back({"Open Subgraph", "", true, false, [asset]() { EffectGraphWindowRegistry::Open(asset); }});
		}
		items.push_back({"Create Block", "", true, false, [window, where, stack, anchor]() {
							 window->_canvas.OpenBlockSearcherAt(*where, stack, anchor);
						 }});
		UI::Widgets::MenuItem toggle{enabled ? "Disable" : "Enable", "", true, false, [this, block, enabled]() { SetBlockEnabled(block, !enabled); }};
		toggle.separatorBefore = true;
		items.push_back(std::move(toggle));
		items.push_back({"Delete", "", true, true, [window]() { window->DeleteSelection(); }});
		AddClearIgnored(items, static_cast<u32>(block));
		OpenMenu(context, items);
	}

	void EffectGraphWindow::Model::OnCanvasContextMenu(UI::Context& context, Vec2 graph) {
		EffectGraphWindow* window = &_window;
		const Vec2 anchor = context.GetPointerPosition();
		UI::Context* where = &context;
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::UI)};
		items.push_back({"Create Node", "", true, false, [window, where, anchor]() { window->_canvas.OpenSearcherAt(*where, anchor); }});
		items.push_back({"Create Sticky Note", "", true, false, [window, graph]() {
							 window->Edit("Create Sticky Note", [&](GraphDocument& document) {
								 GraphNote note;
								 note.title = "Title";
								 note.body = "Write something here";
								 note.x = static_cast<i32>(std::lround(graph.x));
								 note.y = static_cast<i32>(std::lround(graph.y));
								 return GraphDocumentOps::AddNote(document, note) != 0;
							 });
						 }});
		UI::Widgets::MenuItem paste{"Paste", "", true, true, [window, graph]() { window->PasteAt(graph); }};
		paste.enabled = !WindowSystem::GetClipboardText().empty();
		items.push_back(std::move(paste));
		OpenMenu(context, items);
	}

	bool EffectGraphWindow::Model::AcceptsDrop(const UI::DragPayload& payload) const {
		u64 property = 0;
		Guid subgraph;
		return _window._blackboard.OwnsPayload(payload, property) || _window._outputs.OwnsPayload(payload, property) ||
			   (DraggedSubgraph(payload, GRAPH_EFFECT_OPERATOR_SUBGRAPH_EXTENSION, subgraph) && Offered(subgraph, 0)) ||
			   (DraggedSubgraph(payload, GRAPH_EFFECT_EXTENSION, subgraph) && UsableSystem(subgraph));
	}

	bool EffectGraphWindow::Model::UsableSystem(const Guid& subgraph) const {
		const GraphDocument* document = Doc();
		return document && Ops::CanUseSystemSubgraph(*document, subgraph, _window._definitions, _window._open);
	}

	bool EffectGraphWindow::Model::Offered(const Guid& subgraph, u32 context) const {
		const GraphDocument* document = Doc();
		if (!document) {
			return false;
		}
		Vector<Ops::BlockChoice> choices{GetMemoryResource(MemTag::Editor)};
		Ops::SubgraphChoices(*document, _window._open, context, _window._definitions, choices);
		const str named = Shared::Quote(subgraph.ToString());
		return std::any_of(choices.begin(), choices.end(), [&named](const Ops::BlockChoice& choice) { return choice.settings[0].value == named; });
	}

	void EffectGraphWindow::Model::OnDrop(const UI::DragPayload& payload, Vec2 graph) {
		u32 created = 0;
		if (Guid subgraph; DraggedSubgraph(payload, GRAPH_EFFECT_OPERATOR_SUBGRAPH_EXTENSION, subgraph)) {
			if (!Offered(subgraph, 0)) {
				return;
			}
			_window.Edit("Create Node", [&](GraphDocument& document) {
				created = Shared::AddSubgraphNode(document, subgraph, graph);
				return created != 0;
			});
		} else if (DraggedSubgraph(payload, GRAPH_EFFECT_EXTENSION, subgraph)) {
			if (!UsableSystem(subgraph)) {
				return;
			}
			_window.Edit("Create Subgraph", [&](GraphDocument& document) {
				created = Ops::AddSystemSubgraph(document, subgraph, graph, _window._definitions, _window._open);
				return created != 0;
			});
		} else {
			u64 property = 0;
			if (!_window._blackboard.OwnsPayload(payload, property) && !_window._outputs.OwnsPayload(payload, property)) {
				return;
			}
			_window.Edit("Create Property Node", [&](GraphDocument& document) {
				created = Shared::AddPropertyNode(document, static_cast<u32>(property), graph);
				return created != 0;
			});
		}
		if (created != 0) {
			Select(GraphItemKind::Node, created);
		}
	}

	bool EffectGraphWindow::Model::AcceptsNodeDrop(GraphNodeId node, const UI::DragPayload& payload) const {
		const GraphDocument* document = Doc();
		Guid subgraph;
		return document && FindContext(*document, static_cast<u32>(node)) && DraggedSubgraph(payload, GRAPH_EFFECT_BLOCK_SUBGRAPH_EXTENSION, subgraph) &&
			   Offered(subgraph, static_cast<u32>(node));
	}

	void EffectGraphWindow::Model::OnNodeDrop(GraphNodeId node, const UI::DragPayload& payload) {
		const GraphDocument* document = Doc();
		Guid subgraph;
		if (!document || !AcceptsNodeDrop(node, payload) || !DraggedSubgraph(payload, GRAPH_EFFECT_BLOCK_SUBGRAPH_EXTENSION, subgraph)) {
			return;
		}
		Ops::BlockChoice choice;
		choice.def = str(GRAPH_EFFECT_SUBGRAPH_BLOCK);
		choice.settings.push_back({str(GRAPH_SUBGRAPH_ASSET_SETTING), Shared::Quote(subgraph.ToString())});
		u32 created = 0;
		_window.Edit("Create Block", [&](GraphDocument& edited) {
			const GraphContext* context = FindContext(edited, static_cast<u32>(node));
			created = context ? Ops::AddBlock(edited, context->node.id, static_cast<u32>(context->blocks.size()), choice, &_window._definitions) : 0;
			return created != 0;
		});
		if (created != 0) {
			Select(GraphItemKind::Block, created);
		}
	}

	bool EffectGraphWindow::Model::CanConnect(const GraphPortRef& output, const GraphPortRef& input) const {
		const GraphDocument* document = Doc();
		if (!document) {
			return false;
		}
		u32 from = 0;
		u32 to = 0;
		str slot;
		str ignored;
		if (output.side == GraphPortSide::FlowOutput) {
			return FlowOf(output, from, ignored) && FlowOf(input, to, slot) && Ops::CanLink(*document, from, to, slot, nullptr, &_window._definitions);
		}
		str fromPort;
		str toPort;
		return OutputOf(output, from, fromPort) && InputOf(input, to, toPort) &&
			   Shared::CanConnect(*document, from, fromPort, to, toPort, nullptr, &_window._definitions);
	}

	void EffectGraphWindow::Model::Connect(const GraphPortRef& output, const GraphPortRef& input) {
		u32 from = 0;
		u32 to = 0;
		if (output.side == GraphPortSide::FlowOutput) {
			str slot;
			str ignored;
			if (FlowOf(output, from, ignored) && FlowOf(input, to, slot)) {
				_window.Edit("Link Contexts", [&](GraphDocument& document) { return Ops::Link(document, from, to, slot, &_window._definitions); });
			}
			return;
		}
		str fromPort;
		str toPort;
		if (!OutputOf(output, from, fromPort) || !InputOf(input, to, toPort)) {
			return;
		}
		_window.Edit("Connect", [&](GraphDocument& document) {
			return Shared::CanConnect(document, from, fromPort, to, toPort, nullptr, &_window._definitions) &&
				   GraphDocumentOps::Connect(document, from, fromPort, to, toPort);
		});
	}

	void EffectGraphWindow::Model::GetSearchEntries(const GraphPortRef* from, Vector<GraphSearchEntry>& out) {
		out.clear();
		const GraphDocument* document = Doc();
		if (!document) {
			return;
		}
		Ops::ContextChoices(_contextChoices);
		if (document->type != GRAPH_EFFECT_DOCUMENT_TYPE) {
			_contextChoices.clear();
		}
		if (from && (from->side == GraphPortSide::FlowOutput || from->side == GraphPortSide::FlowInput)) {
			u32 context = 0;
			str slot;
			const GraphContext* held = FlowOf(*from, context, slot) ? FindContext(*document, context) : nullptr;
			if (!held) {
				return;
			}
			for (u32 index = 0; index < _contextChoices.size(); ++index) {
				const GraphNodeDefinition* candidate = GraphNodeLibrary::Find(_contextChoices[index].def);
				if (!candidate) {
					continue;
				}
				i32 joins = -1;
				if (from->side == GraphPortSide::FlowOutput) {
					for (u32 s = 0; s < candidate->flowInputs.size() && joins < 0; ++s) {
						joins = GraphEffectRules::MayFeed(held->node.def, candidate->id, candidate->flowInputs[s]) ? static_cast<i32>(s) : -1;
					}
				} else if (candidate->flowOutput && GraphEffectRules::MayFeed(candidate->id, held->node.def, slot)) {
					joins = 0;
				}
				if (joins < 0) {
					continue;
				}
				GraphSearchEntry entry;
				entry.name = _contextChoices[index].name;
				entry.category = "Context";
				entry.synonyms = _contextChoices[index].synonyms;
				entry.key = CONTEXT_KEY | (static_cast<u64>(index) << 8) | static_cast<u64>(joins);
				out.push_back(std::move(entry));
			}
			return;
		}
		u32 fromNode = 0;
		str fromPort;
		const bool fromOutput = from && OutputOf(*from, fromNode, fromPort);
		const bool fromInput = from && !fromOutput && InputOf(*from, fromNode, fromPort);
		if (from && !fromOutput && !fromInput) {
			return;
		}
		GraphWindowParts::LibraryEntries(
			*document, _window._definitions,
			[](const GraphNodeDefinition& definition) { return Ops::IsOperator(definition) && !Ops::IsOfferedAsVariants(definition.id); },
			fromOutput, fromInput, fromNode, fromPort, out);
		Ops::OperatorChoices(_operatorChoices);
		for (u32 index = 0; index < _operatorChoices.size(); ++index) {
			const Ops::BlockChoice& choice = _operatorChoices[index];
			GraphNode shape;
			shape.def = choice.def;
			shape.settings.assign(choice.settings.begin(), choice.settings.end());
			const GraphNodeDefinition* definition = _window._definitions.Find(*document, shape);
			const i32 port = definition ? GraphWindowParts::JoiningPort(*document, _window._definitions, *definition, fromOutput, fromInput, fromNode, fromPort)
										: -1;
			if (!definition || (port < 0 && (fromOutput || fromInput))) {
				continue;
			}
			GraphSearchEntry entry;
			entry.name = choice.name;
			entry.category = choice.category;
			entry.synonyms = choice.synonyms;
			entry.key = VARIANT_KEY | (static_cast<u64>(index) << GraphWindowParts::LIBRARY_PORT_BITS) | static_cast<u64>(port < 0 ? 0 : port);
			out.push_back(std::move(entry));
		}
		Ops::SubgraphChoices(*document, _window._open, 0, _window._definitions, _subgraphChoices);
		for (u32 index = 0; index < _subgraphChoices.size(); ++index) {
			const Ops::BlockChoice& choice = _subgraphChoices[index];
			GraphNode shape;
			shape.def = choice.def;
			shape.settings.assign(choice.settings.begin(), choice.settings.end());
			const GraphNodeDefinition* definition = _window._definitions.Find(*document, shape);
			const i32 port = definition ? GraphWindowParts::JoiningPort(*document, _window._definitions, *definition, fromOutput, fromInput, fromNode, fromPort)
										: -1;
			if (!definition || (port < 0 && (fromOutput || fromInput))) {
				continue;
			}
			GraphSearchEntry entry;
			entry.name = choice.name;
			entry.category = choice.category;
			entry.synonyms = choice.synonyms;
			if (port >= 0) {
				entry.detail = str(fromOutput ? GraphPortLabel(definition->inputs[static_cast<size_t>(port)]) : GraphPortLabel(definition->outputs[static_cast<size_t>(port)]));
			}
			entry.key = SUBGRAPH_KEY | (static_cast<u64>(index) << GraphWindowParts::LIBRARY_PORT_BITS) | static_cast<u64>(port < 0 ? 0 : port);
			out.push_back(std::move(entry));
		}
		if (!fromOutput && !fromInput) {
			for (u32 index = 0; index < _contextChoices.size(); ++index) {
				GraphSearchEntry entry;
				entry.name = _contextChoices[index].name;
				entry.category = "Context";
				entry.synonyms = _contextChoices[index].synonyms;
				entry.key = CONTEXT_KEY | (static_cast<u64>(index) << 8);
				out.push_back(std::move(entry));
			}
		}
		if (fromOutput) {
			return;
		}
		for (const GraphProperty& property : document->properties) {
			if (fromInput && !Shared::PropertyFeeds(*document, property.id, fromNode, fromPort, &_window._definitions)) {
				continue;
			}
			GraphSearchEntry entry;
			entry.name = property.name;
			entry.category = "Properties";
			entry.key = PROPERTY_KEY | property.id;
			out.push_back(std::move(entry));
		}
	}

	void EffectGraphWindow::Model::OnSearchPick(const GraphSearchEntry& entry, Vec2 graph, const GraphPortRef* from) {
		u32 created = 0;
		if ((entry.key & CONTEXT_KEY) != 0) {
			const u64 index = (entry.key & ~CONTEXT_KEY) >> 8;
			const u32 joins = static_cast<u32>(entry.key & 0xff);
			if (index >= _contextChoices.size()) {
				return;
			}
			u32 linked = 0;
			str slot;
			const bool flow = from && FlowOf(*from, linked, slot);
			_window.Edit("Create Context", [&](GraphDocument& document) {
				created = Ops::AddContext(document, _contextChoices[index], graph);
				if (created == 0 || !flow) {
					return created != 0;
				}
				if (from->side == GraphPortSide::FlowOutput) {
					const auto slots = Ops::FlowInputs(document, created);
					if (joins < slots.size()) {
						Ops::Link(document, linked, created, slots[joins]);
					}
				} else {
					Ops::Link(document, created, linked, slot);
				}
				return true;
			});
			if (created != 0) {
				Select(GraphItemKind::Node, created);
			}
			return;
		}
		u32 fromNode = 0;
		str fromPort;
		const bool fromOutput = from && OutputOf(*from, fromNode, fromPort);
		const bool fromInput = from && !fromOutput && InputOf(*from, fromNode, fromPort);
		_window.Edit("Create Node", [&](GraphDocument& document) {
			if ((entry.key & PROPERTY_KEY) != 0) {
				created = Shared::AddPropertyNode(document, static_cast<u32>(entry.key & ~PROPERTY_KEY), graph);
				if (created != 0) {
					GraphWindowParts::JoinDroppedWire(document, _window._definitions, created, 0, fromOutput, fromInput, fromNode, fromPort);
				}
			} else if ((entry.key & (VARIANT_KEY | SUBGRAPH_KEY)) != 0) {
				const bool subgraph = (entry.key & SUBGRAPH_KEY) != 0;
				const Vector<Ops::BlockChoice>& choices = subgraph ? _subgraphChoices : _operatorChoices;
				const u64 index = (entry.key & ~(VARIANT_KEY | SUBGRAPH_KEY)) >> GraphWindowParts::LIBRARY_PORT_BITS;
				if (index < choices.size()) {
					created = Ops::AddOperator(document, choices[index], graph);
				}
				if (created != 0) {
					const u32 port = static_cast<u32>(entry.key & ((1ull << GraphWindowParts::LIBRARY_PORT_BITS) - 1));
					GraphWindowParts::JoinDroppedWire(document, _window._definitions, created, port, fromOutput, fromInput, fromNode, fromPort);
				}
			} else {
				created = GraphWindowParts::MakeLibraryNode(document, _window._definitions, entry.key, graph, fromOutput, fromInput, fromNode, fromPort);
			}
			return created != 0;
		});
		if (created != 0) {
			Select(GraphItemKind::Node, created);
		}
	}

	void EffectGraphWindow::Model::GetGroups(Vector<GraphGroupDesc>& out) const {
		out.clear();
		if (const GraphDocument* document = Doc()) {
			GraphWindowParts::DescribeGroups(*document, out);
		}
	}

	void EffectGraphWindow::Model::GetNotes(Vector<GraphNoteDesc>& out) const {
		out.clear();
		if (const GraphDocument* document = Doc()) {
			GraphWindowParts::DescribeNotes(*document, out);
		}
	}

	void EffectGraphWindow::Model::MoveNote(u64 note, Vec2 graph) {
		if (GraphDocument* document = Doc()) {
			GraphDocumentOps::MoveNode(*document, static_cast<u32>(note), graph);
		}
	}

	void EffectGraphWindow::Model::MoveGroup(u64 group, Vec2 graph) {
		if (GraphDocument* document = Doc()) {
			GraphDocumentOps::MoveNode(*document, static_cast<u32>(group), graph);
		}
	}

	void EffectGraphWindow::Model::ResizeNote(u64 note, Vec2 size) {
		if (GraphDocument* document = Doc()) {
			GraphWindowParts::ResizeNote(*document, note, size);
		}
	}

	void EffectGraphWindow::Model::SetGroupOf(std::span<const GraphItem> items, u64 group) {
		if (GraphDocument* document = Doc()) {
			GraphWindowParts::SetGroupOf(*document, items, group);
		}
	}

	void EffectGraphWindow::Model::RenameGroup(u64 group, std::string_view title) {
		_window.Edit("Rename Group", [&](GraphDocument& document) { return GraphDocumentOps::SetGroupTitle(document, static_cast<u32>(group), title); });
	}

	void EffectGraphWindow::Model::EditNote(u64 note, std::string_view title, std::string_view body) {
		_window.Edit("Edit Sticky Note", [&](GraphDocument& document) { return GraphWindowParts::EditNote(document, note, title, body); });
	}

	void EffectGraphWindow::Model::OnGroupContextMenu(UI::Context& context, u64 group) {
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::UI)};
		GraphWindowParts::GroupMenu(items, _window.MakeEditor(), group,
									[](GraphDocument& document, std::span<const u32> ids) { return Ops::Delete(document, ids); });
		OpenMenu(context, items);
	}

	void EffectGraphWindow::Model::OnNoteContextMenu(UI::Context& context, u64 note) {
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::UI)};
		GraphWindowParts::NoteMenu(items, _window.MakeEditor(), note);
		OpenMenu(context, items);
	}

	void EffectGraphWindow::Model::OnWireContextMenu(UI::Context& context, GraphWireId wire, Vec2 graph) {
		EffectGraphWindow* window = &_window;
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::UI)};
		u32 node = 0;
		str port;
		if (WireInput(wire, node, port)) {
			items.push_back({"Insert Redirect", "", true, false, [window, node, port, graph]() {
								 window->Edit("Insert Redirect", [&](GraphDocument& document) { return GraphDocumentOps::InsertRedirect(document, node, port, graph) != 0; });
							 }});
		}
		items.push_back({"Delete", "", true, true, [window]() { window->DeleteSelection(); }});
		OpenMenu(context, items);
	}

	void EffectGraphWindow::Model::OnWireDoubleClick(GraphWireId wire, Vec2 graph) {
		u32 node = 0;
		str port;
		if (WireInput(wire, node, port)) {
			_window.Edit("Insert Redirect", [&](GraphDocument& document) { return GraphDocumentOps::InsertRedirect(document, node, port, graph) != 0; });
		}
	}

	bool EffectGraphWindow::Model::OnGraphCommand(GraphCommand command) {
		switch (command) {
		case GraphCommand::Copy:
			_window.CopySelection(false);
			return true;
		case GraphCommand::Cut:
			_window.CopySelection(true);
			return true;
		case GraphCommand::Paste:
			_window.PasteAt(_window.PastePoint());
			return true;
		case GraphCommand::Group: {
			const Vector<u32> members = _window.SelectedDocumentIds();
			if (members.empty()) {
				return true;
			}
			Vec2 corner{};
			GetNodePosition(members[0], corner);
			_window.Edit("Group Selection", [&](GraphDocument& document) { return GraphDocumentOps::AddGroup(document, "Group", members, corner) != 0; });
			return true;
		}
		case GraphCommand::Ungroup: {
			const Vector<u32> members = _window.SelectedDocumentIds();
			_window.Edit("Ungroup", [&](GraphDocument& document) { return GraphDocumentOps::SetGroup(document, members, 0); });
			return true;
		}
		}
		return false;
	}

	void EffectGraphWindow::Model::OnSettingClick(UI::Context& context, GraphNodeId owner, u32 setting) {
		const GraphNode* held = Element(static_cast<u32>(owner));
		const GraphNodeDefinition* definition = held ? Definition(*held) : nullptr;
		if (!definition) {
			return;
		}
		Vector<const GraphSettingDefinition*> shown{GetMemoryResource(MemTag::Editor)};
		InGraphSettings(*definition, *held, shown);
		if (setting >= shown.size()) {
			return;
		}
		const GraphSettingDefinition& picked = *shown[setting];
		const str current = GraphWindowParts::SettingText(*held, picked);
		const u32 id = static_cast<u32>(owner);
		EffectGraphWindow* window = &_window;
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::UI)};
		auto option = [&](std::string_view label, str json) {
			UI::Widgets::MenuItem item{str(label), "", true, false, [window, id, key = str(picked.key), json]() {
										   window->Edit("Node Setting", [&](GraphDocument& document) { return Shared::SetNodeSetting(document, id, key, json); });
									   }};
			item.checked = current == label;
			items.push_back(std::move(item));
		};
		if (picked.kind == GraphSettingKind::Toggle) {
			option("On", "true");
			option("Off", "false");
		} else if (picked.kind == GraphSettingKind::Asset) {
			option("None", "null");
			option(picked.defaultText, "");
			const GraphShadingTarget* shading = GraphShadingTargets::Find(GRAPH_SHADER_DOCUMENT_TYPE);
			Vector<Guid> found{GetMemoryResource(MemTag::Editor)};
			for (std::string_view extension : picked.options) {
				GraphSubgraphs::ProjectFiles(extension, found);
				for (const Guid& asset : found) {
					const Path path = AssetDatabase::LookupPath(asset);
					const str name = std::filesystem::path(path.ToString()).stem().generic_string();
					GraphDocument held;
					str error;
					if (picked.key == GRAPH_EFFECT_NODE_SHADER_SETTING &&
						(!shading || !GraphDocumentOps::Read(FileSystem::ReadTextFile(path, MemTag::Editor), held, error) ||
						 !shading->canShadeEffects(held, name, error))) {
						continue;
					}
					option(name, "\"" + asset.ToString() + "\"");
				}
			}
		} else {
			for (std::string_view name : picked.options) {
				option(name, Shared::Quote(name));
			}
		}
		OpenMenu(context, items);
	}

	void EffectGraphWindow::Model::SetSettingText(GraphNodeId owner, u32 setting, std::string_view text) {
		const GraphNode* held = Element(static_cast<u32>(owner));
		const GraphNodeDefinition* definition = held ? Definition(*held) : nullptr;
		if (!definition) {
			return;
		}
		Vector<const GraphSettingDefinition*> shown{GetMemoryResource(MemTag::Editor)};
		InGraphSettings(*definition, *held, shown);
		if (setting >= shown.size()) {
			return;
		}
		const GraphSettingDefinition& typed = *shown[setting];
		str json;
		if (typed.kind == GraphSettingKind::Number) {
			const str copy(text);
			char* end = nullptr;
			const f32 parsed = std::strtof(copy.c_str(), &end);
			if (end == copy.c_str() || !std::isfinite(parsed)) {
				return;
			}
			json = GraphWindowParts::Numbers(&parsed, 1);
		} else {
			json = Shared::Quote(text);
		}
		const u32 id = static_cast<u32>(owner);
		_window.Edit("Node Setting", [&](GraphDocument& document) { return Shared::SetNodeSetting(document, id, typed.key, json); });
	}

	void EffectGraphWindow::Model::OnSettingAction(GraphNodeId owner, u32 setting) {
		const GraphNode* held = Element(static_cast<u32>(owner));
		const GraphNodeDefinition* definition = held ? Definition(*held) : nullptr;
		if (!definition) {
			return;
		}
		Vector<const GraphSettingDefinition*> shown{GetMemoryResource(MemTag::Editor)};
		InGraphSettings(*definition, *held, shown);
		if (setting < shown.size() && IsEventName(*held, *shown[setting])) {
			VisualEffectsEditor::EffectControls::SendToEvery(GraphWindowParts::SettingText(*held, *shown[setting]));
		}
	}

	void EffectGraphWindow::Model::OnPortSpaceClick(const GraphPortRef& input) {
		u32 node = 0;
		str port;
		const GraphNode* held = InputOf(input, node, port) ? Element(node) : nullptr;
		if (!held) {
			return;
		}
		const GraphInput* fed = FindInput(*held, port);
		const std::string_view next = fed && fed->space == GRAPH_SPACE_WORLD ? std::string_view() : GRAPH_SPACE_WORLD;
		_window.Edit("Change Space", [&](GraphDocument& document) { return GraphDocumentOps::SetInputSpace(document, node, port, next); });
	}

	void EffectGraphWindow::Model::SetBlockEnabled(GraphNodeId block, bool enabled) {
		_window.Edit(enabled ? "Enable Block" : "Disable Block",
					 [&](GraphDocument& document) { return GraphDocumentOps::SetBlockEnabled(document, static_cast<u32>(block), enabled); });
	}

	bool EffectGraphWindow::Model::CanMoveBlocks(std::span<const GraphNodeId> blocks, GraphNodeId stack) const {
		const GraphDocument* document = Doc();
		if (!document) {
			return false;
		}
		Vector<u32> ids{GetMemoryResource(MemTag::Editor)};
		for (GraphNodeId block : blocks) {
			ids.push_back(static_cast<u32>(block));
		}
		return Ops::CanMoveBlocks(*document, ids, static_cast<u32>(stack), &_window._definitions);
	}

	void EffectGraphWindow::Model::MoveBlocks(std::span<const GraphNodeId> blocks, GraphNodeId stack, u32 index) {
		Vector<u32> ids{GetMemoryResource(MemTag::Editor)};
		for (GraphNodeId block : blocks) {
			ids.push_back(static_cast<u32>(block));
		}
		_window.Edit("Move Block",
					 [&](GraphDocument& document) { return Ops::MoveBlocks(document, ids, static_cast<u32>(stack), index, &_window._definitions); });
	}

	void EffectGraphWindow::Model::GetBlockSearchEntries(GraphNodeId stack, Vector<GraphSearchEntry>& out) {
		out.clear();
		_blockChoices.clear();
		const GraphDocument* document = Doc();
		if (!document) {
			return;
		}
		Ops::BlockChoices(*document, static_cast<u32>(stack), _blockChoices);
		Vector<Ops::BlockChoice> subgraphs{GetMemoryResource(MemTag::Editor)};
		Ops::SubgraphChoices(*document, _window._open, static_cast<u32>(stack), _window._definitions, subgraphs);
		for (Ops::BlockChoice& choice : subgraphs) {
			_blockChoices.push_back(std::move(choice));
		}
		for (u32 index = 0; index < _blockChoices.size(); ++index) {
			GraphSearchEntry entry;
			entry.name = _blockChoices[index].name;
			entry.category = _blockChoices[index].category;
			entry.synonyms = _blockChoices[index].synonyms;
			entry.key = index;
			out.push_back(std::move(entry));
		}
	}

	void EffectGraphWindow::Model::OnBlockSearchPick(const GraphSearchEntry& entry, GraphNodeId stack, u32 index) {
		if (entry.key >= _blockChoices.size()) {
			return;
		}
		const Ops::BlockChoice choice = _blockChoices[static_cast<size_t>(entry.key)];
		u32 created = 0;
		_window.Edit("Create Block", [&](GraphDocument& document) {
			created = Ops::AddBlock(document, static_cast<u32>(stack), index, choice, &_window._definitions);
			return created != 0;
		});
		if (created != 0) {
			Select(GraphItemKind::Block, created);
		}
	}

	EffectGraphWindow::EffectGraphWindow() {
		_model = New<Model>(MemTag::Editor, *this);
		static u32 serials = 0;
		_shapeSerial = (++serials) & 0xffffu;
	}

	EffectGraphWindow::~EffectGraphWindow() {
		CloseGraph();
		if (_model) {
			Delete(_model);
			_model = nullptr;
		}
	}

	GraphDocument* EffectGraphWindow::GetDocument() const { return _documentOpen ? NodeShaderDocuments::Get(_open) : nullptr; }

	str EffectGraphWindow::GetTitleBase() const {
		if (_open.IsValid()) {
			const str path = AssetDatabase::LookupPath(_open).ToString();
			const size_t slash = path.find_last_of("/\\");
			str name = slash == str::npos ? path : path.substr(slash + 1);
			const size_t dot = name.find_last_of('.');
			if (dot != str::npos) {
				name.resize(dot);
			}
			if (!name.empty()) {
				return name;
			}
		}
		return EditorPanel::GetTitleBase();
	}

	bool EffectGraphWindow::HoldsGraph(const Guid& asset) {
		Vector<Handle<EditorPanel>> panels{GetMemoryResource(MemTag::Editor)};
		PanelManager::GetOpenPanels(panels);
		return std::any_of(panels.begin(), panels.end(), [&](const Handle<EditorPanel>& panel) {
			const EditorPanel* live = panel.Get();
			return live && live->GetType() == GetReflectType() && static_cast<const EffectGraphWindow*>(live)->_documentOpen &&
				   static_cast<const EffectGraphWindow*>(live)->_open == asset;
		});
	}

	bool EffectGraphWindow::OpenGraph(const Guid& asset) {
		if (asset == _open && _documentOpen) {
			return true;
		}
		const Path path = AssetDatabase::LookupPath(asset);
		if (!asset.IsValid() || !IsEditedPath(path.ToString())) {
			SLT_LOG_WARN("VisualEffects", "'{}' isn't an effect graph or subgraph", path.ToString());
			return false;
		}
		str error;
		if (!NodeShaderDocuments::Open(asset, GetType() ? GetType()->ownerModule : nullptr, error)) {
			SLT_LOG_ERROR("VisualEffects", "cannot open '{}': {}", path.ToString(), error);
			return false;
		}
		const GraphDocument* document = NodeShaderDocuments::Get(asset);
		if (!document || !IsEffectDocumentType(document->type)) {
			SLT_LOG_ERROR("VisualEffects", "can't open '{}' (it holds a {} document, not an effect graph or subgraph)", path.ToString(),
						  document ? document->type : str("missing"));
			NodeShaderDocuments::Close(asset);
			return false;
		}
		CloseGraph();
		_open = asset;
		graph = asset;
		_documentOpen = true;
		_seenRevision = NodeShaderDocuments::GetRevision(asset);
		_seenSaveSerial = NodeShaderDocuments::GetSaveSerial();
		_definitions.Clear();
		_diagnostics.clear();
		_inspected = 0;
		_inspectedProperty = 0;
		_canvas.ResetView();
		_canvas.SetSelection({});
		if (document->type == GRAPH_EFFECT_DOCUMENT_TYPE) {
			VisualEffectsEditor::EffectLiveCompiles::Seed(asset, *document);
			_seenAdopted = VisualEffectsEditor::EffectLiveCompiles::GetStatus(asset).adopted;
			GraphEffect check;
			GraphEffectTarget::Compile(*document, AssetDatabase::LookupPath(asset).ToString(), check, _diagnostics, &_definitions);
			RefreshWindowWarnings();
		} else {
			_seenAdopted = 0;
			Compile();
		}
		_needsNodeRebuild = true;
		_needsInspectorRebuild = true;
		_needsBlackboardRebuild = true;
		_needsFrame = true;
		_seenActive = ~0ull;
		return true;
	}

	void EffectGraphWindow::CloseGraph() {
		SetRecordingBounds(false);
		_recordedSystems.clear();
		_recordedRevision = ~0ull;
		Detach();
		if (_documentOpen) {
			NodeShaderDocuments::Close(_open);
			_documentOpen = false;
		}
		_open = {};
		_definitions.Clear();
		_diagnostics.clear();
	}

	void EffectGraphWindow::OnOpen() {
		if (graph.IsValid() && !_documentOpen) {
			OpenGraph(graph);
		}
	}

	void EffectGraphWindow::OnClose() { CloseGraph(); }

	void EffectGraphWindow::BeginEdit(std::string_view label) {
		if (_documentOpen) {
			NodeShaderDocuments::BeginEdit(_open, label);
		}
	}

	void EffectGraphWindow::EndEdit() {
		if (!_documentOpen) {
			return;
		}
		NodeShaderDocuments::EndEdit(_open);
		if (NodeShaderDocuments::GetRevision(_open) != _seenRevision) {
			OnDocumentChanged();
		}
	}

	void EffectGraphWindow::OnDocumentChanged() {
		_seenRevision = NodeShaderDocuments::GetRevision(_open);
		_needsNodeRebuild = true;
		_needsInspectorRebuild = true;
		_needsBlackboardRebuild = true;
		if (autoCompile) {
			Compile();
		}
	}

	GraphWindowParts::Editor EffectGraphWindow::MakeEditor() {
		GraphWindowParts::Editor editor;
		editor.edit = [this](std::string_view label, const std::function<bool(GraphDocument&)>& op) { return Edit(label, op); };
		editor.begin = [this](std::string_view label) { BeginEdit(label); };
		editor.end = [this]() { EndEdit(); };
		editor.document = [this]() { return GetDocument(); };
		return editor;
	}

	bool EffectGraphWindow::Compile() {
		const GraphDocument* document = GetDocument();
		if (!document) {
			return false;
		}
		if (document->type != GRAPH_EFFECT_DOCUMENT_TYPE) {
			_diagnostics.clear();
			const bool checked = GraphEffectTarget::CheckSubgraph(*document, _open, _diagnostics, &_definitions);
			_needsNodeRebuild = true;
			_needsInspectorRebuild = true;
			return checked;
		}
		GraphEffectCompileOptions options;
		options.recordBounds = _recording;
		const bool compiled = VisualEffectsEditor::EffectLiveCompiles::Request(_open, *document, _diagnostics, options);
		RefreshWindowWarnings();
		_needsNodeRebuild = true;
		_needsInspectorRebuild = true;
		return compiled;
	}

	void EffectGraphWindow::RefreshWindowWarnings() {
		static constexpr std::string_view NEEDS_RECORDING = "recording-needs-instance";
		std::erase_if(_diagnostics, [](const GraphDiagnostic& said) { return said.key == NEEDS_RECORDING; });
		_warnedDetached = !_attached.Get();
		const GraphDocument* document = GetDocument();
		if (!document || document->type != GRAPH_EFFECT_DOCUMENT_TYPE || !_warnedDetached) {
			return;
		}
		for (const GraphContext& context : document->contexts) {
			if (context.node.def == GRAPH_EFFECT_INITIALIZE && GraphEffectRules::BoundsModeOf(context.node) == GRAPH_EFFECT_BOUNDS_RECORDED &&
				!GraphEffectRules::IgnoresError(context.node, NEEDS_RECORDING)) {
				_diagnostics.push_back({context.node.id,
										"To record bounds, select an object playing this graph (the window attaches to it)",
										GraphDiagnosticSeverity::Warning, str(NEEDS_RECORDING)});
			}
		}
	}

	bool EffectGraphWindow::HasUnsavedChanges() const { return _documentOpen && NodeShaderDocuments::HasUnsavedChanges(_open); }

	str EffectGraphWindow::GetSaveChangesMessage() const {
		return "Save the changes to this effect graph?\n\n" + AssetDatabase::LookupPath(_open).ToString() + "\n\nUnsaved changes are lost when it closes.";
	}

	void EffectGraphWindow::SaveChanges() { Save(); }

	void EffectGraphWindow::DiscardChanges() {
		if (_documentOpen) {
			NodeShaderDocuments::Discard(_open);
		}
	}

	bool EffectGraphWindow::Save() { return _documentOpen && NodeShaderDocuments::Save(_open); }

	void EffectGraphWindow::OnBuildContent(UI::Context& context, UI::ElementHandle root) {
		UI::Style rootStyle;
		rootStyle.flexDirection = UI::FlexDirection::Column;
		rootStyle.width = UI::Length::Percent(1.0f);
		rootStyle.height = UI::Length::Percent(1.0f);
		context.SetStyle(root, rootStyle);

		BuildToolbar(context, root);

		UI::ElementHandle body = context.CreateElement();
		context.AppendChild(root, body);
		UI::Style bodyStyle;
		bodyStyle.flexDirection = UI::FlexDirection::Row;
		bodyStyle.width = UI::Length::Percent(1.0f);
		bodyStyle.flexGrow = 1.0f;
		bodyStyle.minHeight = UI::Length::Pixel(0.0f);
		context.SetStyle(body, bodyStyle);

		BuildBlackboard(context, body);
		GraphCanvasDesc desc;
		desc.fieldsInside = true;
		UI::ElementHandle host = context.CreateElement();
		context.AppendChild(body, host);
		UI::Style hostStyle;
		hostStyle.flexDirection = UI::FlexDirection::Row;
		hostStyle.flexGrow = 1.0f;
		hostStyle.height = UI::Length::Percent(1.0f);
		hostStyle.minWidth = UI::Length::Pixel(0.0f);
		context.SetStyle(host, hostStyle);
		_canvas.Build(context, host, _model, desc);
		_board = context.CreateElement();
		context.AppendChild(host, _board);
		context.AddClass(_board, EditorTheme::Class::GraphBoard);
		UI::Style boardStyle;
		boardStyle.position = UI::Position::Absolute;
		boardStyle.top = UI::Length::Pixel(BOARD_INSET);
		boardStyle.right = UI::Length::Pixel(BOARD_INSET);
		boardStyle.flexDirection = UI::FlexDirection::Column;
		context.SetStyle(_board, boardStyle);
		_boardKey = ~0ull;
		_boardRefresh = {};
		BuildInspector(context, body);

		RebuildNodes(context);
		_needsFrame = true;
		RebuildInspector(context);
		RebuildBlackboard();
		RefreshStatus(context);
	}

	void EffectGraphWindow::BuildToolbar(UI::Context& context, UI::ElementHandle parent) {
		UI::ElementHandle bar = context.CreateElement();
		context.AppendChild(parent, bar);
		UI::Style style;
		style.flexDirection = UI::FlexDirection::Row;
		style.alignItems = UI::Align::Center;
		style.width = UI::Length::Percent(1.0f);
		style.flexShrink = 0.0f;
		style.padding = {6.0f, 4.0f, 6.0f, 4.0f};
		style.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::HeaderFill));
		context.SetStyle(bar, style);

		auto button = [&](std::string_view label, std::function<void()> onClick) {
			UI::Widgets::ButtonDesc desc;
			desc.label = label;
			desc.font = UISystem::GetDefaultFont();
			desc.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
			desc.onClick = std::move(onClick);
			return UI::Widgets::Button(context, bar, desc);
		};
		button("Save", [this]() { Save(); });
		button("Compile", [this]() { Compile(); });
		UI::Widgets::Checkbox(context, bar, autoCompile, [this](bool on) {
			autoCompile = on;
			if (on) {
				Compile();
			}
		});
		AddText(context, bar, ColorToken::Text, MetricToken::FontSmall, "Auto Compile");
		UI::Widgets::Separator(context, bar);
		_attachButton = button("Attach to Selection", [this]() { AttachToSelection(); });
		_detachButton = button("Detach", [this]() { Detach(); });
		UI::Widgets::Checkbox(context, bar, attachLocked, [this](bool on) { attachLocked = on; });
		AddText(context, bar, ColorToken::Text, MetricToken::FontSmall, "Lock");
		UI::Widgets::Checkbox(context, bar, showControls, [this](bool on) { showControls = on; });
		AddText(context, bar, ColorToken::Text, MetricToken::FontSmall, "Effect Control");
		UI::Widgets::Spacer(context, bar);
		_status = AddText(context, bar, ColorToken::WidgetTextDim, MetricToken::FontSmall, "");
	}

	void EffectGraphWindow::BuildBlackboard(UI::Context& context, UI::ElementHandle parent) {
		UI::ElementHandle column = context.CreateElement();
		context.AppendChild(parent, column);
		UI::Style style;
		style.flexDirection = UI::FlexDirection::Column;
		style.width = UI::Length::Pixel(BLACKBOARD_WIDTH);
		style.flexShrink = 0.0f;
		style.height = UI::Length::Percent(1.0f);
		style.padding = {8.0f, 8.0f, 8.0f, 8.0f};
		style.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::SectionFill));
		context.SetStyle(column, style);

		const auto describe = [this](bool outputs) {
			GraphBlackboardDesc desc;
			desc.title = outputs ? str("Output") : GetTitleBase();
			for (std::string_view type : Ops::PropertyTypes()) {
				desc.kinds.push_back(str(Shared::PropertyTypeLabel(type)));
			}
			desc.onAdd = [this, outputs](u32 kind) -> u64 {
				u32 created = 0;
				Edit(outputs ? "Add Output" : "Add Property", [&](GraphDocument& document) {
					const std::span<const std::string_view> types = Ops::PropertyTypes();
					created = kind >= types.size() ? 0 : outputs ? Ops::AddOutputProperty(document, types[kind])
																 : Shared::AddProperty(document, types[kind]);
					return created != 0;
				});
				return created;
			};
			desc.onRename = [this](u64 id, std::string_view name) -> u64 {
				return Edit("Rename Property", [&](GraphDocument& document) { return Shared::RenameProperty(document, static_cast<u32>(id), name); }) ? id : 0;
			};
			desc.onMove = [this, outputs](u64 id, u32 index) -> u64 { return MoveProperty(static_cast<u32>(id), index, outputs); };
			desc.onDelete = [this](std::span<const u64> ids) {
				Vector<u32> properties{GetMemoryResource(MemTag::Editor)};
				for (u64 id : ids) {
					properties.push_back(static_cast<u32>(id));
				}
				Edit("Delete Property", [&](GraphDocument& document) { return Shared::DeleteProperties(document, properties) > 0; });
			};
			desc.onSelectionChanged = [this](std::span<const u64> ids) {
				const u32 property = ids.empty() ? 0u : static_cast<u32>(ids.back());
				if (property != _inspectedProperty) {
					_inspectedProperty = property;
					_needsInspectorRebuild = true;
				}
			};
			return desc;
		};
		GraphBlackboardDesc properties = describe(false);
		properties.onBuildSubtitle = [this](UI::Context&, UI::ElementHandle host) { _subtitle = host; };
		_blackboard.Build(this, context, column, std::move(properties));
		_outputsHost = context.CreateElement();
		context.AppendChild(column, _outputsHost);
		UI::Style host;
		host.flexDirection = UI::FlexDirection::Column;
		host.width = UI::Length::Percent(1.0f);
		host.display = UI::Display::None;
		context.SetStyle(_outputsHost, host);
		_outputs.Build(this, context, _outputsHost, describe(true));
	}

	u64 EffectGraphWindow::MoveProperty(u32 property, u32 index, bool outputs) {
		const bool moved = Edit("Move Property", [&](GraphDocument& document) {
			Vector<u32> others{GetMemoryResource(MemTag::Editor)};
			u32 from = 0;
			for (u32 at = 0; at < document.properties.size(); ++at) {
				const GraphProperty& held = document.properties[at];
				if (held.id == property) {
					from = at;
				} else if (GraphSubgraphs::IsOutput(held) == outputs) {
					others.push_back(at);
				}
			}
			if (others.empty()) {
				return false;
			}
			const auto after = [from](u32 at) { return at > from ? at - 1 : at; };
			const u32 to = index < others.size() ? after(others[index]) : after(others.back()) + 1;
			return Shared::MoveProperty(document, property, to);
		});
		return moved ? property : 0;
	}

	void EffectGraphWindow::BuildInspector(UI::Context& context, UI::ElementHandle parent) {
		UI::ElementHandle column = context.CreateElement();
		context.AppendChild(parent, column);
		UI::Style style;
		style.flexDirection = UI::FlexDirection::Column;
		style.width = UI::Length::Pixel(INSPECTOR_WIDTH);
		style.flexShrink = 0.0f;
		style.height = UI::Length::Percent(1.0f);
		style.overflow = UI::Overflow::Scroll;
		style.padding = {8.0f, 8.0f, 8.0f, 8.0f};
		style.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::SectionFill));
		context.SetStyle(column, style);
		_inspector = context.CreateElement();
		context.AppendChild(column, _inspector);
		UI::Style content;
		content.flexDirection = UI::FlexDirection::Column;
		content.width = UI::Length::Percent(1.0f);
		context.SetStyle(_inspector, content);
	}

	void EffectGraphWindow::RebuildInspector(UI::Context& context) {
		_needsInspectorRebuild = false;
		if (!context.IsAlive(_inspector)) {
			return;
		}
		context.ClearChildren(_inspector);
		GraphDocument* document = GetDocument();
		if (!document) {
			AddText(context, _inspector, ColorToken::WidgetTextDim, MetricToken::FontRow, "No graph open");
			return;
		}
		auto wrapped = [&](ColorToken color, std::string_view text) {
			UI::ElementHandle line = AddText(context, _inspector, color, MetricToken::FontRow, text);
			UI::Style wrap;
			wrap.width = UI::Length::Percent(1.0f);
			context.SetStyle(line, wrap);
		};
		if (const GraphProperty* property = _inspectedProperty != 0 ? FindProperty(*document, _inspectedProperty) : nullptr) {
			const u32 id = property->id;
			AddText(context, _inspector, ColorToken::TextStrong, MetricToken::FontBody, str(Shared::PropertyTypeLabel(property->type)) + " Property");
			UI::ElementHandle nameRow = AddField(context, _inspector, "Name");
			UI::Widgets::TextFieldDesc name;
			name.text = property->name;
			name.font = UISystem::GetDefaultFont();
			name.fontSizePx = FontRow();
			name.onCommit = [this, id](std::string_view text) {
				Edit("Rename Property", [&](GraphDocument& edited) { return Shared::RenameProperty(edited, id, text); });
			};
			UI::Widgets::TextField(context, nameRow, name);
			GraphWindowParts::BuildPropertyDefault(context, AddField(context, _inspector, "Default"), id, MakeEditor());
			return;
		}
		GraphNode* node = _inspected != 0 ? GraphDocumentOps::FindNode(*document, _inspected) : nullptr;
		if (!node && document->type == GRAPH_EFFECT_DOCUMENT_TYPE) {
			BuildAssetSettings(context, *document);
			return;
		}
		if (!node) {
			AddText(context, _inspector, ColorToken::WidgetTextDim, MetricToken::FontRow, "Select a node to edit its settings");
			return;
		}
		str missing;
		const GraphNodeDefinition* definition = _definitions.Find(*document, *node, &missing);
		AddText(context, _inspector, ColorToken::TextStrong, MetricToken::FontBody, definition ? definition->name : std::string_view(node->def));
		for (const GraphDiagnosticSeverity severity : {GraphDiagnosticSeverity::Error, GraphDiagnosticSeverity::Warning}) {
			for (const GraphDiagnostic& diagnostic : _diagnostics) {
				if (diagnostic.node != node->id || diagnostic.severity != severity) {
					continue;
				}
				wrapped(GraphWindowParts::ColorOf(severity), diagnostic.message);
				if (severity != GraphDiagnosticSeverity::Warning || diagnostic.key.empty()) {
					continue;
				}
				UI::Widgets::ButtonDesc ignore;
				ignore.label = "Ignore";
				ignore.font = UISystem::GetDefaultFont();
				ignore.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
				ignore.onClick = [this, id = node->id, key = diagnostic.key]() {
					if (Edit("Ignore Warning", [&](GraphDocument& edited) { return Ops::IgnoreError(edited, id, key); })) {
						std::erase_if(_diagnostics, [&](const GraphDiagnostic& said) { return said.node == id && said.key == key; });
						_needsNodeRebuild = true;
						_needsInspectorRebuild = true;
					}
				};
				UI::Widgets::Button(context, _inspector, ignore);
			}
		}
		if (!definition) {
			wrapped(ColorToken::StatusError, missing);
			return;
		}
		GraphWindowParts::BuildSettings(context, _inspector, node->id, definition->settings, MakeEditor());
		GraphWindowParts::BuildOpenGradients(context, _inspector, *node, *definition, MakeEditor());
		const u32 id = node->id;
		for (u32 index = 0; index < definition->inputs.size(); ++index) {
			const GraphPortDefinition& input = definition->inputs[index];
			const GraphInput* fed = FindInput(*node, input.name);
			const RHI::TextureDimension dimension = PortDimension(input.type);
			if (dimension == RHI::TextureDimension::Count || !GraphNodeLibrary::InputActive(*definition, index, *node) || (fed && fed->fromNode != 0)) {
				continue;
			}
			const str port(input.name);
			const str caption = TextureCaption(HeldTexture(*node, port), dimension);
			ObjectField::Desc field;
			field.caption = caption;
			field.accepts = [dimension](const UI::DragPayload& payload) { return ObjectField::TakesTexture(payload, dimension); };
			field.onDrop = [this, id, port, dimension](const UI::DragPayload& payload, Vec2) {
				if (!ObjectField::TakesTexture(payload, dimension)) {
					return;
				}
				const str json = "\"" + payload.data.As<AssetDragData>().guid.ToString() + "\"";
				Edit("Set Texture", [&](GraphDocument& edited) { return Shared::SetInputValue(edited, id, port, json); });
			};
			field.onClear = [this, id, port]() { Edit("Clear Texture", [&](GraphDocument& edited) { return Shared::SetInputValue(edited, id, port, "null"); }); };
			field.resolveTarget = [this, id, port]() -> EditorPingTarget {
				GraphDocument* document = GetDocument();
				const GraphNode* held = document ? GraphDocumentOps::FindNode(*document, id) : nullptr;
				return {.asset = held ? HeldTexture(*held, port) : Guid{}};
			};
			ObjectField::Build(context, AddField(context, _inspector, GraphPortLabel(input)), field);
		}
		for (u32 index = 0; index < definition->inputs.size(); ++index) {
			const GraphPortDefinition& input = definition->inputs[index];
			const GraphInput* fed = FindInput(*node, input.name);
			if (input.type != GraphPortType::Mesh || !GraphNodeLibrary::InputActive(*definition, index, *node) || (fed && fed->fromNode != 0)) {
				continue;
			}
			const str port(input.name);
			const str caption = MeshCaption(HeldMesh(*node, port));
			ObjectField::Desc field;
			field.caption = caption;
			field.accepts = [](const UI::DragPayload& payload) { return TakesMesh(payload); };
			field.onDrop = [this, id, port](const UI::DragPayload& payload, Vec2) {
				if (!TakesMesh(payload)) {
					return;
				}
				const str json = "\"" + payload.data.As<AssetDragData>().guid.ToString() + "\"";
				Edit("Set Mesh", [&](GraphDocument& edited) { return Shared::SetInputValue(edited, id, port, json); });
			};
			field.onClear = [this, id, port]() { Edit("Clear Mesh", [&](GraphDocument& edited) { return Shared::SetInputValue(edited, id, port, "null"); }); };
			field.resolveTarget = [this, id, port]() -> EditorPingTarget {
				GraphDocument* document = GetDocument();
				const GraphNode* held = document ? GraphDocumentOps::FindNode(*document, id) : nullptr;
				return {.asset = held ? HeldMesh(*held, port) : Guid{}};
			};
			ObjectField::Build(context, AddField(context, _inspector, GraphPortLabel(input)), field);
		}
	}

	void EffectGraphWindow::BuildAssetSettings(UI::Context& context, const GraphDocument& document) {
		VisualEffectsEditor::EffectAssetSettings::Build(context, _inspector, document, MakeEditor());
	}

	void EffectGraphWindow::RebuildBlackboard() {
		_needsBlackboardRebuild = false;
		const GraphDocument* document = GetDocument();
		Vector<GraphBlackboardItem> items{GetMemoryResource(MemTag::Editor)};
		Vector<GraphBlackboardItem> outputs{GetMemoryResource(MemTag::Editor)};
		if (document) {
			for (const GraphProperty& property : document->properties) {
				GraphBlackboardItem item;
				item.id = property.id;
				item.name = property.name;
				item.onBuildControls = [type = str(Shared::PropertyTypeLabel(property.type))](UI::Context& context, UI::ElementHandle row) {
					AddText(context, row, ColorToken::WidgetTextDim, MetricToken::FontSmall, type);
				};
				(GraphSubgraphs::IsOutput(property) ? outputs : items).push_back(std::move(item));
			}
		}
		_blackboard.SetItems(items);
		_outputs.SetItems(outputs);
		UI::Context* context = GetPanelContext();
		if (!context) {
			return;
		}
		const bool operatorSubgraph = document && document->type == GRAPH_EFFECT_OPERATOR_SUBGRAPH_DOCUMENT_TYPE;
		if (context->IsAlive(_outputsHost)) {
			UI::Style host = context->GetStyle(_outputsHost);
			host.display = operatorSubgraph ? UI::Display::Flex : UI::Display::None;
			context->SetStyle(_outputsHost, host);
		}
		if (!context->IsAlive(_subtitle)) {
			return;
		}
		context->ClearChildren(_subtitle);
		GraphSubgraphKind kind = GraphSubgraphKind::Shader;
		if (!document || !GraphSubgraphs::KindOf(document->type, kind) || kind == GraphSubgraphKind::EffectSystem) {
			return;
		}
		str category;
		for (const GraphSetting& setting : document->settings) {
			if (setting.key == GRAPH_SUBGRAPH_CATEGORY_SETTING) {
				LinearAllocator arena(setting.value.size() + 64, MemTag::Editor);
				const JSONValue value = JSONParse(setting.value, arena);
				category = value.IsString() ? str(value.AsString()) : str();
			}
		}
		const str shown = category.empty() ? str(GraphSubgraphs::DefaultCategory(kind)) : category;
		UI::Widgets::TextFieldDesc field;
		field.text = shown;
		field.font = UISystem::GetDefaultFont();
		field.fontSizePx = FontRow();
		field.onCommit = [this](std::string_view text) {
			Edit("Set Category", [&](GraphDocument& edited) { return Shared::SetGraphSetting(edited, GRAPH_SUBGRAPH_CATEGORY_SETTING, Shared::Quote(text)); });
		};
		UI::ElementHandle input = UI::Widgets::TextField(*context, _subtitle, field);
		UI::Style grow = context->GetStyle(input);
		grow.flexGrow = 1.0f;
		context->SetStyle(input, grow);
	}

	void EffectGraphWindow::RefreshStatus(UI::Context& context) {
		if (!context.IsAlive(_status)) {
			return;
		}
		str text;
		ColorToken color = ColorToken::WidgetTextDim;
		const VisualEffectsEditor::EffectLiveCompiles::Status status = VisualEffectsEditor::EffectLiveCompiles::GetStatus(_open);
		const u32 errors = CountDiagnostics(_diagnostics, GraphDiagnosticSeverity::Error);
		const u32 warnings = CountDiagnostics(_diagnostics, GraphDiagnosticSeverity::Warning);
		if (errors > 0) {
			text = errors == 1 ? str("1 error") : std::format("{} errors", errors);
			color = ColorToken::StatusError;
		} else if (status.compiling) {
			text = std::format("Compiling... {:.1f} s", status.seconds);
		} else if (status.failed) {
			text = "The generated shaders didn't compile. See the Console";
			color = ColorToken::StatusError;
		} else {
			const str warned = warnings == 0 ? str() : (warnings == 1 ? str("1 warning") : std::format("{} warnings", warnings));
			text = HasUnsavedChanges() ? (warned.empty() ? str("Unsaved") : "Unsaved - " + warned) : warned;
			color = warnings > 0 ? ColorToken::StatusWarn : color;
		}
		context.SetText(_status, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontSmall), EditorTheme::GetColor(color), text);
	}

	VisualEffect* EffectGraphWindow::GetAttached() const { return _attached.Get(); }

	bool EffectGraphWindow::PlaysThisGraph(const VisualEffect& effect) const {
		const VisualEffectAsset* asset = effect.asset.Get();
		return asset && _documentOpen && asset->assetId == _open;
	}

	VisualEffect* EffectGraphWindow::SelectedEffect() const {
		Object* active = EditorSelection::GetActive().Get();
		if (!active || !TypeRegistry::IsDerivedFrom(active->GetType(), SceneObject::GetReflectType())) {
			return nullptr;
		}
		const SceneObject& object = *static_cast<SceneObject*>(active);
		for (u32 index = 0; index < object.GetComponentCount(); ++index) {
			Component* component = object.GetComponentAt(index).Get();
			if (component && TypeRegistry::IsDerivedFrom(component->GetType(), VisualEffect::GetReflectType())) {
				VisualEffect* effect = static_cast<VisualEffect*>(component);
				if (PlaysThisGraph(*effect)) {
					return effect;
				}
			}
		}
		return nullptr;
	}

	void EffectGraphWindow::AttachTo(VisualEffect* effect) {
		VisualEffect* current = _attached.Get();
		if (current == effect) {
			return;
		}
		if (current) {
			VisualEffectsEditor::EffectControls::Detach(*current);
		}
		_attached = effect ? Handle<VisualEffect>(effect) : Handle<VisualEffect>{};
		if (effect) {
			VisualEffectsEditor::EffectControls::Attach(*effect);
		}
		for (RecordedSystem& system : _recordedSystems) {
			system.box = AABB::Empty();
		}
		if (!effect) {
			SetRecordingBounds(false);
		}
	}

	bool EffectGraphWindow::AttachToSelection() {
		VisualEffect* effect = SelectedEffect();
		if (effect) {
			AttachTo(effect);
		}
		return effect != nullptr;
	}

	void EffectGraphWindow::Detach() { AttachTo(nullptr); }

	void EffectGraphWindow::UpdateAttach() {
		VisualEffect* current = _attached.Get();
		if (current && !PlaysThisGraph(*current)) {
			Detach();
		} else if (!current) {
			_attached = {};
		}
		const u64 active = EditorSelection::GetActive().GetInstanceId();
		if (active != _seenActive) {
			_seenActive = active;
			if (!attachLocked) {
				AttachToSelection();
			}
		}
		if (VisualEffect* attached = _attached.Get()) {
			VisualEffectsEditor::EffectControls::Attach(*attached);
		}
		if (_documentOpen && (_attached.Get() == nullptr) != _warnedDetached) {
			RefreshWindowWarnings();
			_needsNodeRebuild = true;
			_needsInspectorRebuild = true;
		}
	}

	u64 EffectGraphWindow::BoardKey() const {
		const VisualEffect* attached = _attached.Get();
		const VisualEffectAsset* asset = attached ? attached->asset.Get() : nullptr;
		u64 key = (static_cast<u64>(showControls) << 63) ^ _attached.GetInstanceId();
		key = key * 0x100000001b3ull ^ (asset ? VisualEffectsImpl::EffectAssetAccess::Generation(*asset) : 0u);
		const VisualEffectsImpl::EffectInstance* instance = attached ? VisualEffectsImpl::EffectComponentAccess::Instance(*const_cast<VisualEffect*>(attached)) : nullptr;
		key = key * 0x100000001b3ull ^ (instance ? instance->GetSystemCount() : 0u);
		key = key * 0x100000001b3ull ^ (_recording ? 1u : 0u);
		return key * 0x100000001b3ull ^ (_documentOpen ? NodeShaderDocuments::GetRevision(_open) : 0u);
	}

	void EffectGraphWindow::RebuildBoard(UI::Context& context) {
		_boardKey = BoardKey();
		_boardRefresh = {};
		if (!context.IsAlive(_board)) {
			return;
		}
		context.ClearChildren(_board);
		UI::Style style = context.GetStyle(_board);
		style.display = showControls && _documentOpen ? UI::Display::Flex : UI::Display::None;
		context.SetStyle(_board, style);
		AddText(context, _board, ColorToken::TextStrong, MetricToken::FontSmall, "Effect Control");
		VisualEffect* attached = _attached.Get();
		if (!attached) {
			AddText(context, _board, ColorToken::WidgetTextDim, MetricToken::FontSmall, "Select an object running this effect");
			return;
		}
		str path;
		for (const SceneObject* at = attached->GetOwner().Get(); at; at = at->GetParent().Get()) {
			path = path.empty() ? at->name : at->name + " > " + path;
		}
		AddText(context, _board, ColorToken::Text, MetricToken::FontSmall, path);
		const Handle<VisualEffect> target = _attached;
		const VisualEffectsEditor::EffectControls::Refresh controls = VisualEffectsEditor::EffectControls::BuildControls(
			context, _board, [target](Vector<Handle<VisualEffect>>& out) { out.assign(1, target); });

		auto row = [&]() {
			UI::ElementHandle line = context.CreateElement();
			context.AppendChild(_board, line);
			UI::Style rowStyle;
			rowStyle.flexDirection = UI::FlexDirection::Row;
			rowStyle.alignItems = UI::Align::Center;
			context.SetStyle(line, rowStyle);
			return line;
		};
		auto button = [&](UI::ElementHandle line, std::string_view label, std::function<void(VisualEffect&)> act) {
			UI::Widgets::ButtonDesc desc;
			desc.label = label;
			desc.font = UISystem::GetDefaultFont();
			desc.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
			desc.onClick = [target, act = std::move(act)]() {
				if (VisualEffect* effect = target.Get()) {
					act(*effect);
				}
			};
			UI::Widgets::Button(context, line, desc);
		};
		const UI::ElementHandle defaults = row();
		button(defaults, "Play()", [](VisualEffect& effect) { effect.Play(); });
		button(defaults, "Stop()", [](VisualEffect& effect) { effect.Stop(); });
		if (const VisualEffectAsset* asset = attached->asset.Get()) {
			for (const str& event : VisualEffectsImpl::EffectAssetAccess::Description(*asset).events) {
				const UI::ElementHandle line = row();
				AddText(context, line, ColorToken::Text, MetricToken::FontSmall, event);
				UI::Widgets::Spacer(context, line);
				button(line, "Send", [event](VisualEffect& effect) { effect.SendEvent(event); });
			}
		}

		Vector<UI::ElementHandle> counts{GetMemoryResource(MemTag::Editor)};
		const VisualEffectsImpl::EffectInstance* instance = VisualEffectsImpl::EffectComponentAccess::Instance(*attached);
		const VisualEffectAsset* played = attached->asset.Get();
		const u32 systems = instance && played ? instance->GetSystemCount() : 0u;
		for (u32 system = 0; system < systems; ++system) {
			const Vector<VisualEffectsImpl::EffectSystem>& described = VisualEffectsImpl::EffectAssetAccess::Description(*played).systems;
			if (system >= described.size()) {
				break;
			}
			const UI::ElementHandle line = row();
			AddText(context, line, ColorToken::Text, MetricToken::FontSmall, described[system].name);
			UI::Widgets::Spacer(context, line);
			counts.push_back(AddText(context, line, ColorToken::WidgetTextDim, MetricToken::FontSmall, ""));
		}
		BuildRecordingRows(context, row);
		_boardRefresh = [controls, target, counts](UI::Context& refreshed) {
			controls(refreshed);
			VisualEffect* effect = target.Get();
			const VisualEffectsImpl::EffectInstance* live = effect ? VisualEffectsImpl::EffectComponentAccess::Instance(*effect) : nullptr;
			for (u32 system = 0; system < counts.size(); ++system) {
				if (!refreshed.IsAlive(counts[system]) || !live || system >= live->GetSystemCount()) {
					continue;
				}
				const VisualEffectsImpl::ParticleSystem& held = live->GetSystem(system);
				const VisualEffectsImpl::ParticleCounts& known = held.GetCounts();
				const str text = known.known ? std::format("{} / {}, {} dropped", known.alive, held.GetCapacity(), known.dropped)
											 : std::format("- / {}", held.GetCapacity());
				refreshed.SetText(counts[system], UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontSmall),
								  EditorTheme::GetColor(ColorToken::WidgetTextDim), text);
			}
		};
	}

	void EffectGraphWindow::RefreshBoard(UI::Context& context) {
		if (BoardKey() != _boardKey) {
			RebuildBoard(context);
		}
		if (_boardRefresh) {
			_boardRefresh(context);
		}
		const bool attached = _attached.Get() != nullptr;
		for (auto [element, visible] : {std::pair{_attachButton, !attached}, std::pair{_detachButton, attached}}) {
			if (context.IsAlive(element)) {
				UI::Style buttonStyle = context.GetStyle(element);
				buttonStyle.display = visible ? UI::Display::Flex : UI::Display::None;
				context.SetStyle(element, buttonStyle);
			}
		}
	}

	void EffectGraphWindow::ReadRecordedSystems() {
		const GraphDocument* document = GetDocument();
		const u64 revision = document ? NodeShaderDocuments::GetRevision(_open) : ~0ull;
		if (revision == _recordedRevision) {
			return;
		}
		_recordedRevision = revision;
		Vector<RecordedSystem> read{GetMemoryResource(MemTag::Editor)};
		GraphEffect effect;
		Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
		if (document && document->type == GRAPH_EFFECT_DOCUMENT_TYPE &&
			GraphEffectTarget::Compile(*document, AssetDatabase::LookupPath(_open).ToString(), effect, diagnostics, &_definitions)) {
			const u32 own = static_cast<u32>(std::count_if(document->contexts.begin(), document->contexts.end(),
														   [](const GraphContext& context) { return context.node.def == GRAPH_EFFECT_INITIALIZE; }));
			for (u32 s = 0; s < effect.systems.size(); ++s) {
				RecordedSystem system;
				system.initialize = s < own ? effect.systems[s].initialize : 0u;
				system.world = effect.systems[s].world;
				system.name = effect.systems[s].name;
				for (const RecordedSystem& held : _recordedSystems) {
					system.box = held.initialize != 0 && held.initialize == system.initialize ? held.box : system.box;
				}
				read.push_back(std::move(system));
			}
		}
		_recordedSystems = std::move(read);
	}

	bool EffectGraphWindow::CanRecordBounds() const {
		const GraphDocument* document = GetDocument();
		if (!_attached.Get() || !document) {
			return false;
		}
		return std::any_of(_recordedSystems.begin(), _recordedSystems.end(), [document](const RecordedSystem& system) {
			return system.initialize != 0 && Ops::BoundsExclusionOf(*document, system.initialize) == Ops::BoundsExclusion::None;
		});
	}

	void EffectGraphWindow::SetRecordingBounds(bool on) {
		if (on == _recording) {
			return;
		}
		if (on) {
			ReadRecordedSystems();
			if (!CanRecordBounds()) {
				return;
			}
			for (RecordedSystem& system : _recordedSystems) {
				system.box = AABB::Empty();
			}
		}
		_recording = on;
		if (_documentOpen) {
			Compile();
		}
	}

	void EffectGraphWindow::RecordBounds() {
		if (!_recording) {
			return;
		}
		VisualEffect* attached = _attached.Get();
		if (!attached) {
			SetRecordingBounds(false);
			return;
		}
		ReadRecordedSystems();
		const VisualEffectAsset* asset = attached->asset.Get();
		const VisualEffectsImpl::EffectInstance* instance = VisualEffectsImpl::EffectComponentAccess::Instance(*attached);
		const GraphDocument* document = GetDocument();
		if (!asset || !instance || !instance->IsBuiltFrom(*asset) || !document) {
			return;
		}
		const u32 generation = VisualEffectsImpl::EffectAssetAccess::Generation(*asset);
		if (generation != _recordedGeneration) {
			_recordedGeneration = generation;
			for (RecordedSystem& system : _recordedSystems) {
				system.box = AABB::Empty();
			}
		}
		for (u32 s = 0; s < _recordedSystems.size() && s < instance->GetSystemCount(); ++s) {
			RecordedSystem& system = _recordedSystems[s];
			if (system.initialize == 0 || Ops::BoundsExclusionOf(*document, system.initialize) != Ops::BoundsExclusion::None) {
				continue;
			}
			const VisualEffectsImpl::ParticleBounds measured = instance->GetMeasuredBounds(s);
			if (measured.known && measured.box.IsValid()) {
				system.box.Encapsulate(measured.box);
			}
		}
	}

	bool EffectGraphWindow::ApplyRecordedBounds() {
		const VisualEffect* attached = _attached.Get();
		const SceneObject* owner = attached ? attached->GetOwner().Get() : nullptr;
		const Mat4 localToWorld = owner ? owner->GetWorldMatrix() : Mat4::Identity();
		const Vector<RecordedSystem> systems = _recordedSystems;
		return Edit("Apply Bounds", [&](GraphDocument& document) {
			bool applied = false;
			for (const RecordedSystem& system : systems) {
				if (system.initialize != 0 && system.box.IsValid()) {
					applied = Ops::ApplyRecordedBounds(document, system.initialize, system.box, system.world, localToWorld) || applied;
				}
			}
			return applied;
		});
	}

	void EffectGraphWindow::AppendShape(Vector<DebugLine>& lines, Vector<SceneHandle>& handles) {
		const VisualEffect* attached = _attached.Get();
		const GraphDocument* document = GetDocument();
		const SceneObject* owner = attached ? attached->GetOwner().Get() : nullptr;
		if (!owner || !document || _inspected == 0) {
			return;
		}
		const GraphNode* node = nullptr;
		for (const GraphContext& context : document->contexts) {
			for (const GraphNode& block : context.blocks) {
				node = block.id == _inspected ? &block : node;
			}
		}
		const GraphNodeDefinition* definition = node ? _definitions.Find(*document, *node) : nullptr;
		EffectGraphGizmoOps::BlockGizmo gizmo;
		const Color32 ink = EditorTheme::GetColor32(ColorToken::GizmoEffectShape);
		if (!definition || !EffectGraphGizmoOps::Build(*document, _inspected, _definitions, owner->GetWorldMatrix(), owner->GetWorldRotation(), ink, ink,
													   definition->name, gizmo)) {
			return;
		}
		lines.insert(lines.end(), gizmo.lines.begin(), gizmo.lines.end());
		for (SceneHandle& handle : gizmo.handles) {
			handle.id = (_shapeSerial << 16) | (handle.id & 0xffffu);
			handles.push_back(std::move(handle));
		}
	}

	bool EffectGraphWindow::WriteShape(const SceneHandle& dragged) {
		const VisualEffect* attached = _attached.Get();
		const SceneObject* owner = attached ? attached->GetOwner().Get() : nullptr;
		if ((dragged.id >> 16) != _shapeSerial || !owner || _inspected == 0) {
			return false;
		}
		SceneHandle mine = dragged;
		mine.id = dragged.id & 0xffffu;
		const Mat4 effectToWorld = owner->GetWorldMatrix();
		const u32 block = _inspected;
		Edit(dragged.label, [&](GraphDocument& document) { return EffectGraphGizmoOps::Write(document, block, _definitions, effectToWorld, mine); });
		return true;
	}

	void EffectGraphWindow::AppendRecordedBounds(Vector<DebugLine>& out) const {
		const VisualEffect* attached = _attached.Get();
		const GraphDocument* document = GetDocument();
		if (!_recording || !attached || !document) {
			return;
		}
		const VisualEffectsImpl::EffectInstance* instance = VisualEffectsImpl::EffectComponentAccess::Instance(*const_cast<VisualEffect*>(attached));
		const SceneObject* owner = attached->GetOwner().Get();
		const Mat4 localToWorld = owner ? owner->GetWorldMatrix() : Mat4::Identity();
		const auto recorded = [document](const RecordedSystem& system) {
			return system.initialize != 0 && system.box.IsValid() && Ops::BoundsExclusionOf(*document, system.initialize) == Ops::BoundsExclusion::None;
		};
		const auto selected = [this](u32 initialize) {
			const std::span<const GraphItem> items = _canvas.GetSelection();
			return std::any_of(items.begin(), items.end(), [initialize](const GraphItem& item) { return item.kind == GraphItemKind::Node && item.id == initialize; });
		};
		bool anySelected = false;
		for (const RecordedSystem& system : _recordedSystems) {
			anySelected = anySelected || (recorded(system) && selected(system.initialize));
		}
		for (u32 s = 0; s < _recordedSystems.size(); ++s) {
			const RecordedSystem& system = _recordedSystems[s];
			if (!recorded(system) || (anySelected && !selected(system.initialize))) {
				continue;
			}
			const Vec3 padding = instance ? instance->GetBoundsPadding(s) : Vec3(0.0f, 0.0f, 0.0f);
			const Vec3 half = system.box.Extents();
			const Vec3 grown(std::max(half.x + padding.x, 0.0f), std::max(half.y + padding.y, 0.0f), std::max(half.z + padding.z, 0.0f));
			const AABB padded = AABB::FromCenterExtents(system.box.Center(), grown);
			EditorViewportOps::AppendBoxWireframe(out, system.world ? padded : padded.Transformed(localToWorld),
												  EditorTheme::GetColor32(ColorToken::GizmoEffectRecordedBounds));
		}
	}

	void EffectGraphWindow::BuildRecordingRows(UI::Context& context, const std::function<UI::ElementHandle()>& row) {
		ReadRecordedSystems();
		const GraphDocument* document = GetDocument();
		AddText(context, row(), ColorToken::TextStrong, MetricToken::FontSmall, _recording ? "Recording bounds..." : "Bounds Recording");
		const auto button = [&](UI::ElementHandle line, std::string_view label, std::function<void()> onClick, bool offered) {
			UI::Widgets::ButtonDesc desc;
			desc.label = label;
			desc.font = UISystem::GetDefaultFont();
			desc.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
			desc.onClick = [offered, act = std::move(onClick)]() {
				if (offered) {
					act();
				}
			};
			const UI::ElementHandle made = UI::Widgets::Button(context, line, desc);
			if (!offered) {
				PropertyRows::MakeDisabled(context, made);
			}
		};
		bool held = false;
		for (const RecordedSystem& system : _recordedSystems) {
			held = held || (system.initialize != 0 && system.box.IsValid());
		}
		const UI::ElementHandle controls = row();
		button(controls, _recording ? "Stop Recording" : "Record", [this]() { SetRecordingBounds(!_recording); }, _recording || CanRecordBounds());
		button(controls, "Apply Bounds", [this]() { ApplyRecordedBounds(); }, _recording || held);
		for (const RecordedSystem& system : _recordedSystems) {
			const GraphNode* node = document && system.initialize != 0 ? GraphDocumentOps::FindNode(const_cast<GraphDocument&>(*document), system.initialize) : nullptr;
			if (!node) {
				continue;
			}
			const UI::ElementHandle line = row();
			const Ops::BoundsExclusion exclusion = Ops::BoundsExclusionOf(*document, system.initialize);
			const std::string_view why = Ops::BoundsExclusionLabel(exclusion);
			const UI::ElementHandle name = AddText(context, line, why.empty() ? ColorToken::Text : ColorToken::WidgetTextDim, MetricToken::FontSmall,
												   why.empty() ? system.name : system.name + " " + str(why));
			context.SetTooltip(name, Ops::BoundsExclusionTooltip(exclusion));
			UI::Widgets::Spacer(context, line);
			UI::Widgets::DropdownDesc mode;
			mode.items = GRAPH_EFFECT_BOUNDS_MODES;
			mode.selected = GraphEffectRules::BoundsModeOf(*node);
			mode.font = UISystem::GetDefaultFont();
			mode.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
			const u32 initialize = system.initialize;
			mode.onSelect = [this, initialize](u32 index) {
				if (index < std::size(GRAPH_EFFECT_BOUNDS_MODES)) {
					Edit("Bounds Setting Mode", [&](GraphDocument& edited) {
						return Shared::SetNodeSetting(edited, initialize, GRAPH_EFFECT_BOUNDS_SETTING_MODE, Shared::Quote(GRAPH_EFFECT_BOUNDS_MODES[index]));
					});
				}
			};
			UI::Widgets::Dropdown(context, line, mode);
		}
	}

	Vector<u32> EffectGraphWindow::SelectedDocumentIds() const {
		Vector<u32> ids{GetMemoryResource(MemTag::Editor)};
		for (const GraphItem& item : _canvas.GetSelection()) {
			if (item.kind != GraphItemKind::Wire) {
				ids.push_back(static_cast<u32>(item.id));
			}
		}
		return ids;
	}

	void EffectGraphWindow::CopySelection(bool cut) {
		const GraphDocument* document = GetDocument();
		const Vector<u32> ids = SelectedDocumentIds();
		if (!document || ids.empty()) {
			return;
		}
		const str clip = Ops::Copy(*document, ids);
		if (clip.empty()) {
			return;
		}
		WindowSystem::SetClipboardText(clip);
		_pasteCount = 0;
		if (cut) {
			DeleteSelection();
		}
	}

	Vec2 EffectGraphWindow::PastePoint() const {
		if (UI::Context* context = GetPanelContext(); context && context->IsAlive(_canvas.GetElement())) {
			const UI::Rect rect = context->GetLayout(_canvas.GetElement()).content;
			if (rect.Contains(context->GetPointerPosition())) {
				return GraphCanvasOps::LocalToGraph(_canvas.GetView(), _canvas.PointerLocal(*context));
			}
		}
		return _lastPasteAt + Vec2(PASTE_STEP, PASTE_STEP) * static_cast<f32>(_pasteCount + 1);
	}

	void EffectGraphWindow::PasteAt(Vec2 graph) {
		const str clip = WindowSystem::GetClipboardText();
		if (clip.empty() || !GetDocument()) {
			return;
		}
		if (Ops::IsBlockFragment(clip)) {
			PasteBlocks("Paste", clip);
			return;
		}
		const Vec2 corner = GraphWindowParts::FragmentCorner(clip, graph);
		Vector<u32> pasted{GetMemoryResource(MemTag::Editor)};
		str error;
		Edit("Paste", [&](GraphDocument& document) { return Shared::Paste(document, clip, graph - corner, pasted, error); });
		_lastPasteAt = graph;
		++_pasteCount;
		Vector<GraphItem> selection{GetMemoryResource(MemTag::Editor)};
		if (const GraphDocument* document = GetDocument()) {
			GraphWindowParts::PastedItems(*document, pasted, selection);
		}
		SelectItems(selection);
	}

	void EffectGraphWindow::DuplicateSelection() {
		const GraphDocument* document = GetDocument();
		const Vector<u32> ids = SelectedDocumentIds();
		if (!document || ids.empty()) {
			return;
		}
		const str clip = Ops::Copy(*document, ids);
		if (clip.empty()) {
			return;
		}
		if (Ops::IsBlockFragment(clip)) {
			PasteBlocks("Duplicate", clip);
			return;
		}
		Vector<u32> pasted{GetMemoryResource(MemTag::Editor)};
		str error;
		Edit("Duplicate", [&](GraphDocument& edited) { return Shared::Paste(edited, clip, Vec2(PASTE_STEP, PASTE_STEP), pasted, error); });
		Vector<GraphItem> selection{GetMemoryResource(MemTag::Editor)};
		if (const GraphDocument* after = GetDocument()) {
			GraphWindowParts::PastedItems(*after, pasted, selection);
		}
		SelectItems(selection);
	}

	void EffectGraphWindow::PasteBlocks(std::string_view label, const str& clip) {
		const Vector<u32> selection = SelectedDocumentIds();
		Vector<u32> pasted{GetMemoryResource(MemTag::Editor)};
		str error;
		Edit(label, [&](GraphDocument& document) { return Ops::PasteBlocks(document, clip, selection, pasted, error, &_definitions); });
		if (pasted.empty()) {
			SLT_LOG_WARN("VisualEffects", "{}", error);
			return;
		}
		Vector<GraphItem> items{GetMemoryResource(MemTag::Editor)};
		if (const GraphDocument* document = GetDocument()) {
			GraphWindowParts::PastedItems(*document, pasted, items);
		}
		SelectItems(items);
	}

	void EffectGraphWindow::DeleteSelection() {
		const Vector<u32> ids = SelectedDocumentIds();
		Vector<GraphWireId> wires{GetMemoryResource(MemTag::Editor)};
		for (const GraphItem& item : _canvas.GetSelection()) {
			if (item.kind == GraphItemKind::Wire) {
				wires.push_back(item.id);
			}
		}
		Edit("Delete", [&](GraphDocument& document) {
			bool changed = false;
			for (GraphWireId wire : wires) {
				u32 node = 0;
				u32 from = 0;
				str port;
				if (_model->WireInput(wire, node, port)) {
					changed = GraphDocumentOps::Disconnect(document, node, port) || changed;
				} else if (_model->WireFlow(wire, from, node, port)) {
					changed = GraphDocumentOps::Unlink(document, from, node, port) || changed;
				}
			}
			return Ops::Delete(document, ids) > 0 || changed;
		});
		_canvas.SetSelection({});
		_inspected = 0;
		_needsInspectorRebuild = true;
	}

	void EffectGraphWindow::SelectItems(std::span<const GraphItem> items) {
		_canvas.SetSelection(items);
		_model->OnSelectionChanged();
	}

	void EffectGraphWindow::RebuildNodes(UI::Context& context) {
		_needsNodeRebuild = false;
		_canvas.RebuildNodes(context);
		if (_inspectedProperty == 0) {
			_model->OnSelectionChanged();
		}
	}

	bool EffectGraphWindow::OnCommand(PanelCommand command) {
		if (_blackboard.HasFocus() || _outputs.HasFocus()) {
			return EditorPanel::OnCommand(command);
		}
		switch (command) {
		case PanelCommand::Delete:
			DeleteSelection();
			return true;
		case PanelCommand::Duplicate:
			DuplicateSelection();
			return true;
		case PanelCommand::SelectAll:
			_canvas.SelectAll();
			_model->OnSelectionChanged();
			return true;
		default:
			return EditorPanel::OnCommand(command);
		}
	}

	bool EffectGraphWindow::ReadChanged() const {
		if (EditorAssetWatch::DidOverflowThisFrame()) {
			return true;
		}
		const std::span<const Path> changed = EditorAssetWatch::GetChangedThisFrame();
		if (changed.empty()) {
			return false;
		}
		Vector<const GraphReadDocument*> read{GetMemoryResource(MemTag::Editor)};
		_definitions.Read(read);
		return std::any_of(read.begin(), read.end(), [changed](const GraphReadDocument* document) {
			return std::find(changed.begin(), changed.end(), document->path) != changed.end();
		});
	}

	void EffectGraphWindow::OnTick(f32 deltaSeconds) {
		if (_documentOpen && NodeShaderDocuments::GetRevision(_open) != _seenRevision) {
			OnDocumentChanged();
		}
		const u64 serial = NodeShaderDocuments::GetSaveSerial();
		if (serial != _seenSaveSerial || ReadChanged()) {
			_seenSaveSerial = serial;
			_definitions.Clear();
			_needsNodeRebuild = true;
			_needsInspectorRebuild = true;
			if (_documentOpen && autoCompile) {
				Compile();
			}
		}
		UI::Context* context = GetPanelContext();
		if (!context) {
			return;
		}
		if (_needsNodeRebuild) {
			RebuildNodes(*context);
		}
		if (_needsBlackboardRebuild) {
			RebuildBlackboard();
		}
		_blackboard.Tick(deltaSeconds);
		_outputs.Tick(deltaSeconds);
		if (_needsInspectorRebuild) {
			RebuildInspector(*context);
		}
		if (_needsFrame && _canvas.FrameAll(*context)) {
			_needsFrame = false;
		}
		RefreshStatus(*context);
		UpdateAttach();
		RecordBounds();
		RefreshBoard(*context);
	}

	namespace EffectGraphWindowRegistry {

		EffectGraphWindow* Open(const Guid& asset) {
			Vector<Handle<EditorPanel>> panels{GetMemoryResource(MemTag::Editor)};
			PanelManager::GetOpenPanels(panels);
			for (const Handle<EditorPanel>& panel : panels) {
				EditorPanel* live = panel.Get();
				if (live && live->GetType() == EffectGraphWindow::GetReflectType()) {
					auto* window = static_cast<EffectGraphWindow*>(live);
					if (window->GetGraph() == asset) {
						PanelManager::FocusPanel(panel);
						return window;
					}
				}
			}
			Handle<EditorPanel> opened = PanelManager::OpenPanel(EffectGraphWindow::GetReflectType());
			auto* window = static_cast<EffectGraphWindow*>(opened.Get());
			if (!window) {
				return nullptr;
			}
			if (!window->OpenGraph(asset)) {
				PanelManager::ClosePanel(opened);
				return nullptr;
			}
			return window;
		}

		void RegisterOpener(const void* owner) {
			for (const std::string_view extension : EDITED_EXTENSIONS) {
				AssetOpeners::Register(extension, [](const Guid& asset) { Open(asset); }, owner);
			}
		}

	} // namespace EffectGraphWindowRegistry

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::EffectGraphWindow)
