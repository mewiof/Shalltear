#include "NodeShaderWindow.h"

#include "GraphDefinitions.h"
#include "GraphEffect.h"
#include "GraphNodeLibrary.h"
#include "GraphPreview.h"
#include "GraphProperties.h"
#include "GraphSurface.h"
#include "GraphWindowParts.h"
#include "NodeShaderDocuments.h"
#include "NodeShaderEditOps.h"
#include "NodeShaderStackOps.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/StringUtil.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"
#include "Shalltear/Core/Window/WindowSystem.h"

#include "Shalltear/Editor/AssetBrowserOps.h"
#include "Shalltear/Editor/AssetOpeners.h"
#include "Shalltear/Editor/EditorDialogs.h"
#include "Shalltear/Editor/EditorDragData.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/GraphDocument.h"
#include "Shalltear/Editor/ObjectField.h"
#include "Shalltear/Editor/PanelManager.h"

#include <algorithm>
#include <cctype>
#include <cmath>
#include <filesystem>
#include <format>
#include <memory>

namespace Shalltear {

	namespace {

		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;
		namespace Ops = NodeShaderEditOps;
		namespace Stack = NodeShaderStackOps;
		using GraphWindowParts::ActiveInputs;
		using GraphWindowParts::AddField;
		using GraphWindowParts::AddRow;
		using GraphWindowParts::AddText;
		using GraphWindowParts::FindInput;
		using GraphWindowParts::FindProperty;
		using GraphWindowParts::FindSetting;
		using GraphWindowParts::FontRow;
		using GraphWindowParts::InlineFor;
		using GraphWindowParts::Numbers;
		using GraphWindowParts::ReadNumbers;
		using GraphWindowParts::ReadString;

		constexpr f32 NODE_WIDTH = 190.0f;
		constexpr f32 STACK_WIDTH = 220.0f;
		constexpr f32 BLACKBOARD_WIDTH = 220.0f;
		constexpr f32 INSPECTOR_WIDTH = 280.0f;
		constexpr f32 MAIN_PREVIEW_SIZE = 200.0f;
		constexpr f32 MAIN_PREVIEW_INSET = 10.0f;
		constexpr f32 PASTE_STEP = 24.0f;
		constexpr f64 UNFOCUSED_PREVIEW_INTERVAL = 0.1;

		constexpr u64 PROPERTY_KEY = 1ull << 63;
		constexpr u64 INTERPOLATOR_KEY = 1ull << 62;
		constexpr u64 SUBGRAPH_KEY = 1ull << 61;
		constexpr u64 CUSTOM_FUNCTION_KEY = 1ull << 60;

		bool FunctionFileOf(const UI::DragPayload& payload, Guid& out) {
			if (payload.type != "Asset"_hash || !payload.data.Is<AssetDragData>()) {
				return false;
			}
			const Guid guid = payload.data.As<AssetDragData>().guid;
			const str extension = StringUtil::ToLower(std::filesystem::path(AssetDatabase::LookupPath(guid).ToString()).extension().generic_string());
			if (std::find(std::begin(GRAPH_CUSTOM_FUNCTION_EXTENSIONS), std::end(GRAPH_CUSTOM_FUNCTION_EXTENSIONS), extension) ==
				std::end(GRAPH_CUSTOM_FUNCTION_EXTENSIONS)) {
				return false;
			}
			out = guid;
			return out.IsValid();
		}

		constexpr std::string_view STACK_FRAGMENT = "Context/Fragment";

		bool EndsWith(std::string_view path, std::string_view extension) {
			if (path.size() < extension.size()) {
				return false;
			}
			const std::string_view tail = path.substr(path.size() - extension.size());
			return std::equal(tail.begin(), tail.end(), extension.begin(), [](char a, char b) {
				return std::tolower(static_cast<unsigned char>(a)) == b;
			});
		}

		bool IsNodeShaderPath(std::string_view path) { return EndsWith(path, ".slshader") || EndsWith(path, GRAPH_SUBGRAPH_EXTENSION); }

		bool MeshOf(const UI::DragPayload& payload, Guid& out) {
			if (payload.type != "Asset"_hash || !payload.data.Is<AssetDragData>()) {
				return false;
			}
			const AssetDragData& data = payload.data.As<AssetDragData>();
			if (data.assetTypeHash != MeshAsset::GetReflectType()->name.GetValue()) {
				return false;
			}
			out = data.guid;
			return out.IsValid();
		}

		bool IsColorBlock(std::string_view def) { return def == "Block/BaseColor" || def == "Block/Emission"; }

		const GraphContext* FindContext(const GraphDocument& document, u32 id) {
			for (const GraphContext& context : document.contexts) {
				if (context.node.id == id) {
					return &context;
				}
			}
			return nullptr;
		}

		str InterpolatorName(const GraphNode& node, std::string_view key = "Name") {
			const GraphSetting* stored = FindSetting(node.settings, key);
			return stored ? ReadString(stored->value) : str();
		}

		std::span<const GraphSettingDefinition> InterpolatorSettings() {
			const GraphNodeDefinition* definition = GraphNodeLibrary::Find(GraphSurface::INTERPOLATOR_BLOCK);
			return definition ? definition->settings : std::span<const GraphSettingDefinition>{};
		}

	} // anonymous namespace

	class NodeShaderWindow::Model final : public GraphViewModel {
	public:
		explicit Model(NodeShaderWindow& window) : _window(window) {}

		void GetNodes(Vector<GraphNodeDesc>& out) override;
		bool GetNodePosition(GraphNodeId node, Vec2& out) const override;
		void GetWires(Vector<GraphWireDesc>& out) const override;
		ColorToken GetNodeBorder(GraphNodeId node) const override;
		str GetNodeTooltip(GraphNodeId node) const override;
		void PaintNodePreview(UI::Context& context, UI::PaintScope& paint, GraphNodeId node, const UI::Rect& well) override;
		void SetNodePreviewExpanded(GraphNodeId node, bool expanded) override;
		void SetInlineValue(const GraphPortRef& input, std::span<const f32> values) override;
		void OnInlineEditBegin() override { _window.BeginEdit("Edit Value"); }
		void OnInlineEditEnd() override { _window.EndEdit(); }
		void OnSelectionChanged() override;
		void OnMoveBegin() override { _window.BeginEdit("Move"); }
		void MoveNode(GraphNodeId node, Vec2 graph) override;
		void OnMoveEnd() override { _window.EndEdit(); }
		void OnNodeDoubleClick(GraphNodeId node) override;
		void OnNodeContextMenu(UI::Context& context, GraphNodeId node) override;
		void OnCanvasContextMenu(UI::Context& context, Vec2 graph) override;
		bool AcceptsDrop(const UI::DragPayload& payload) const override;
		void OnDrop(const UI::DragPayload& payload, Vec2 graph) override;
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

		bool InputOf(const GraphPortRef& ref, u32& node, str& port) const;
		bool OutputOf(const GraphPortRef& ref, u32& node, str& port) const;
	private:
		GraphDocument* Doc() const { return _window.GetDocument(); }
		const GraphNodeDefinition* Definition(const GraphNode& node) const { return _window._definitions.Find(*Doc(), node); }
		void OpenMenu(UI::Context& context, const Vector<UI::Widgets::MenuItem>& items);
		void AddOutputMenu(Vector<UI::Widgets::MenuItem>& items, const GraphContext& stack);

		NodeShaderWindow& _window;
		Vector<GraphPreview::Tile> _previewable{GetMemoryResource(MemTag::Editor)};
		Vector<Guid> _subgraphChoices{GetMemoryResource(MemTag::Editor)};
	};

	bool NodeShaderWindow::Model::InputOf(const GraphPortRef& ref, u32& node, str& port) const {
		const GraphDocument* document = Doc();
		if (!document || ref.side != GraphPortSide::Input) {
			return false;
		}
		const u32 id = static_cast<u32>(ref.node);
		if (const GraphContext* stack = FindContext(*document, id)) {
			if (ref.port >= stack->blocks.size()) {
				return false;
			}
			node = stack->blocks[ref.port].id;
			port = "Value";
			return true;
		}
		const GraphNode* held = GraphDocumentOps::FindNode(*const_cast<GraphDocument*>(document), id);
		const GraphNodeDefinition* definition = held ? Definition(*held) : nullptr;
		if (!definition) {
			return false;
		}
		Vector<u32> active{GetMemoryResource(MemTag::Editor)};
		ActiveInputs(*definition, *held, active);
		if (ref.port >= active.size()) {
			return false;
		}
		node = id;
		port = str(definition->inputs[active[ref.port]].name);
		return true;
	}

	bool NodeShaderWindow::Model::OutputOf(const GraphPortRef& ref, u32& node, str& port) const {
		const GraphDocument* document = Doc();
		if (!document || ref.side != GraphPortSide::Output) {
			return false;
		}
		const u32 id = static_cast<u32>(ref.node);
		const GraphNode* held = GraphDocumentOps::FindNode(*const_cast<GraphDocument*>(document), id);
		const GraphNodeDefinition* definition = held ? Definition(*held) : nullptr;
		if (!definition || ref.port >= definition->outputs.size()) {
			return false;
		}
		node = id;
		port = str(definition->outputs[ref.port].name);
		return true;
	}

	void NodeShaderWindow::Model::GetNodes(Vector<GraphNodeDesc>& out) {
		out.clear();
		const GraphDocument* document = Doc();
		if (!document) {
			return;
		}
		GraphPreview::Previewable(*document, _previewable, &_window._definitions);

		for (const GraphContext& stack : document->contexts) {
			GraphNodeDesc desc;
			desc.id = CanvasId(stack.node.id);
			const GraphNodeDefinition* definition = GraphNodeLibrary::Find(stack.node.def);
			desc.label = definition ? str(definition->name) : stack.node.def;
			desc.size = {STACK_WIDTH, 0.0f};
			desc.fill = ColorToken::GraphNodeMuted;
			for (const GraphNode& block : stack.blocks) {
				const GraphNodeDefinition* blockDefinition = Definition(block);
				GraphPortDesc port;
				port.label = blockDefinition ? str(blockDefinition->name) : block.def;
				if (block.def == GraphSurface::INTERPOLATOR_BLOCK) {
					port.label = InterpolatorName(block);
				} else if (block.def == GRAPH_SUBGRAPH_OUTPUT_BLOCK) {
					port.label = GraphSubgraphs::OutputNameOf(block);
				}
				port.dim = block.def != GRAPH_SUBGRAPH_OUTPUT_BLOCK && !Stack::IsBlockActive(*document, block.def);
				if (blockDefinition && !blockDefinition->inputs.empty()) {
					const GraphPortDefinition& value = blockDefinition->inputs[0];
					port.color = Ops::PortColor(value.type);
					const GraphInput* fed = FindInput(block, value.name);
					port.connected = fed && fed->fromNode != 0;
					port.inlineValue = InlineFor(block, value, 1, IsColorBlock(block.def));
				}
				desc.inputs.push_back(std::move(port));
			}
			out.push_back(std::move(desc));
		}

		for (const GraphNode& node : document->nodes) {
			GraphNodeDesc desc;
			desc.id = CanvasId(node.id);
			desc.size = {NODE_WIDTH, 0.0f};
			const GraphNodeDefinition* definition = Definition(node);
			if (!definition) {
				desc.label = node.def == GRAPH_SUBGRAPH_NODE ? GraphSubgraphs::NameOf(GraphSubgraphs::AssetOf(node)) + " (missing)" : node.def + " (unknown)";
				out.push_back(std::move(desc));
				continue;
			}
			desc.label = str(definition->name);
			if (node.def == GraphDocumentOps::REDIRECT_DEF) {
				desc.compact = true;
			}
			if (const GraphProperty* property = FindProperty(*document, Ops::PropertyOf(*document, node.id))) {
				desc.label = property->name;
			}
			if (node.def == Stack::INTERPOLATOR_NODE) {
				desc.label = InterpolatorName(node, "Interpolator") + " (Custom Interpolator)";
			}
			const u32 width = Ops::DynamicWidth(*document, node.id, &_window._definitions);
			Vector<u32> active{GetMemoryResource(MemTag::Editor)};
			ActiveInputs(*definition, node, active);
			for (u32 index : active) {
				const GraphPortDefinition& input = definition->inputs[index];
				GraphPortDesc port;
				port.label = desc.compact ? str() : str(GraphPortLabel(input));
				port.color = Ops::PortColor(input.type);
				const GraphInput* fed = FindInput(node, input.name);
				port.connected = fed && fed->fromNode != 0;
				const GraphSubgraph* subgraph = definition->subgraph;
				const bool color = subgraph && index < subgraph->inputKinds.size() && subgraph->inputKinds[index] == GraphPropertyKind::Color;
				port.inlineValue = InlineFor(node, input, width, color);
				desc.inputs.push_back(std::move(port));
			}
			for (const GraphPortDefinition& output : definition->outputs) {
				GraphPortDesc port;
				port.label = desc.compact || node.def == Ops::PROPERTY_NODE ? str() : str(GraphPortLabel(output));
				port.color = Ops::PortColor(output.type);
				desc.outputs.push_back(std::move(port));
			}
			const bool previewable = std::any_of(_previewable.begin(), _previewable.end(), [&](const GraphPreview::Tile& tile) { return tile.node == node.id; });
			if (previewable) {
				desc.preview = node.previewCollapsed ? GraphNodePreview::Collapsed : GraphNodePreview::Expanded;
			}
			out.push_back(std::move(desc));
		}
	}

	bool NodeShaderWindow::Model::GetNodePosition(GraphNodeId node, Vec2& out) const {
		GraphDocument* document = Doc();
		const GraphNode* held = document ? GraphDocumentOps::FindNode(*document, static_cast<u32>(node)) : nullptr;
		if (!held) {
			return false;
		}
		out = Vec2(static_cast<f32>(held->x), static_cast<f32>(held->y));
		return true;
	}

	void NodeShaderWindow::Model::GetWires(Vector<GraphWireDesc>& out) const {
		out.clear();
		GraphDocument* document = Doc();
		if (!document) {
			return;
		}
		auto add = [&](const GraphNode& node, GraphNodeId drawnAt, u32 port, const GraphInput& input) {
			const GraphNode* source = GraphDocumentOps::FindNode(*document, input.fromNode);
			const GraphNodeDefinition* sourceDefinition = source ? Definition(*source) : nullptr;
			const i32 output = sourceDefinition ? GraphNodeLibrary::FindOutput(*sourceDefinition, input.fromPort) : -1;
			if (output < 0) {
				return;
			}
			(void)node;
			GraphWireDesc wire;
			wire.id = (drawnAt << 32) | port;
			wire.style = GraphWireStyle::Curve;
			wire.from = {CanvasId(input.fromNode), GraphPortSide::Output, static_cast<u32>(output)};
			wire.to = {drawnAt, GraphPortSide::Input, port};
			wire.color = Ops::PortColor(sourceDefinition->outputs[static_cast<size_t>(output)].type);
			out.push_back(wire);
		};
		for (const GraphContext& stack : document->contexts) {
			for (u32 index = 0; index < stack.blocks.size(); ++index) {
				const GraphNode& block = stack.blocks[index];
				for (const GraphInput& input : block.inputs) {
					if (input.fromNode != 0) {
						add(block, CanvasId(stack.node.id), index, input);
					}
				}
			}
		}
		for (const GraphNode& node : document->nodes) {
			const GraphNodeDefinition* definition = Definition(node);
			if (!definition) {
				continue;
			}
			Vector<u32> active{GetMemoryResource(MemTag::Editor)};
			ActiveInputs(*definition, node, active);
			for (const GraphInput& input : node.inputs) {
				const i32 port = input.fromNode != 0 ? GraphNodeLibrary::FindInput(*definition, input.port) : -1;
				const auto drawn = std::find(active.begin(), active.end(), static_cast<u32>(port));
				if (port >= 0 && drawn != active.end()) {
					add(node, CanvasId(node.id), static_cast<u32>(drawn - active.begin()), input);
				}
			}
		}
	}

	ColorToken NodeShaderWindow::Model::GetNodeBorder(GraphNodeId node) const {
		return GraphWindowParts::MarkOf(_window._previews.GetDiagnostics(),
										[node](const GraphDiagnostic& diagnostic) { return diagnostic.node != 0 && CanvasId(diagnostic.node) == node; });
	}

	str NodeShaderWindow::Model::GetNodeTooltip(GraphNodeId node) const {
		return GraphWindowParts::TooltipOf(_window._previews.GetDiagnostics(),
										   [node](const GraphDiagnostic& diagnostic) { return diagnostic.node != 0 && CanvasId(diagnostic.node) == node; });
	}

	void NodeShaderWindow::Model::PaintNodePreview(UI::Context& context, UI::PaintScope& paint, GraphNodeId node, const UI::Rect& well) {
		(void)context;
		GraphPreviewAtlas& atlas = _window._previews.GetAtlas();
		const i32 tile = _window._previews.TileOf(static_cast<u32>(node));
		const RHI::TextureHandle texture = atlas.GetTexture();
		if (tile < 0 || !texture.IsValid()) {
			return;
		}
		paint.EmitTexturedQuad(well, texture, atlas.TileUV(static_cast<u32>(tile)), Color::White());
	}

	void NodeShaderWindow::Model::SetNodePreviewExpanded(GraphNodeId node, bool expanded) {
		_window.Edit(expanded ? "Expand Preview" : "Collapse Preview", [&](GraphDocument& document) {
			GraphNode* held = GraphDocumentOps::FindNode(document, static_cast<u32>(node));
			if (!held || held->previewCollapsed == !expanded) {
				return false;
			}
			held->previewCollapsed = !expanded;
			return true;
		});
	}

	void NodeShaderWindow::Model::SetInlineValue(const GraphPortRef& input, std::span<const f32> values) {
		u32 node = 0;
		str port;
		if (!InputOf(input, node, port)) {
			return;
		}
		_window.Edit("Edit Value", [&](GraphDocument& document) { return Ops::SetInputConstant(document, node, port, values); });
	}

	void NodeShaderWindow::Model::OnSelectionChanged() {
		const std::span<const GraphItem> selected = _window._canvas.GetSelection();
		u32 inspected = 0;
		if (!selected.empty() && selected.back().kind == GraphItemKind::Node) {
			inspected = static_cast<u32>(selected.back().id);
		}
		if (inspected != _window._inspected || _window._inspectedProperty != 0) {
			_window._inspected = inspected;
			_window._inspectedProperty = 0;
			_window._graphSettingsTab = false;
			_window._needsInspectorRebuild = true;
		}
	}

	void NodeShaderWindow::Model::MoveNode(GraphNodeId node, Vec2 graph) {
		if (GraphDocument* document = Doc()) {
			GraphDocumentOps::MoveNode(*document, static_cast<u32>(node), graph);
		}
	}

	void NodeShaderWindow::Model::OpenMenu(UI::Context& context, const Vector<UI::Widgets::MenuItem>& items) {
		UI::Widgets::OpenMenu(context, context.GetPointerPosition(), items, UISystem::GetDefaultFont(), FontRow());
	}

	void NodeShaderWindow::Model::OnNodeContextMenu(UI::Context& context, GraphNodeId node) {
		GraphDocument* document = Doc();
		if (!document) {
			return;
		}
		const u32 id = static_cast<u32>(node);
		NodeShaderWindow* window = &_window;
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::UI)};
		if (const GraphContext* stack = FindContext(*document, id); stack && stack->node.def == GRAPH_SUBGRAPH_OUTPUT_CONTEXT) {
			AddOutputMenu(items, *stack);
			OpenMenu(context, items);
			return;
		}
		if (const GraphContext* stack = FindContext(*document, id)) {
			UI::Widgets::MenuItem add;
			add.label = "Add Block Node";
			Vector<std::string_view> missing{GetMemoryResource(MemTag::Editor)};
			Stack::MissingBlocks(*document, id, missing);
			for (std::string_view def : missing) {
				const GraphNodeDefinition* definition = GraphNodeLibrary::Find(def);
				UI::Widgets::MenuItem item;
				item.label = definition ? str(definition->name) : str(def);
				item.onSelect = [window, id, which = str(def)]() {
					window->Edit("Add Block", [&](GraphDocument& edited) { return Stack::AddBlock(edited, id, which) != 0; });
				};
				add.children.push_back(std::move(item));
			}
			add.enabled = !add.children.empty();
			items.push_back(std::move(add));
			for (const GraphNode& block : stack->blocks) {
				if (block.def != GraphSurface::INTERPOLATOR_BLOCK) {
					continue;
				}
				UI::Widgets::MenuItem settings;
				settings.label = InterpolatorName(block);
				UI::Widgets::MenuItem rename;
				rename.label = "Rename...";
				rename.onSelect = [window, where = &context, blockId = block.id, current = InterpolatorName(block)]() {
					window->_popup = EditorDialogs::OpenTextInput(*where, "Rename Custom Interpolator", current, "Rename", [window, blockId](const str& name) {
						window->_popup = {};
						window->Edit("Rename Custom Interpolator", [&](GraphDocument& edited) { return Stack::RenameInterpolator(edited, blockId, name); });
					});
				};
				settings.children.push_back(std::move(rename));
				for (const GraphSettingDefinition& setting : InterpolatorSettings()) {
					if (setting.kind != GraphSettingKind::Choice) {
						continue;
					}
					UI::Widgets::MenuItem choice;
					choice.label = str(setting.key);
					const str current = InterpolatorName(block, setting.key);
					for (u32 i = 0; i < setting.options.size(); ++i) {
						UI::Widgets::MenuItem option;
						option.label = str(setting.options[i]);
						option.checked = current.empty() ? i == setting.defaultIndex : current == setting.options[i];
						option.onSelect = [window, blockId = block.id, key = str(setting.key), value = str(setting.options[i])]() {
							window->Edit("Custom Interpolator", [&](GraphDocument& edited) {
								return key == "Type" ? Stack::SetInterpolatorType(edited, blockId, value) : Stack::SetInterpolatorMode(edited, blockId, value);
							});
						};
						choice.children.push_back(std::move(option));
					}
					settings.children.push_back(std::move(choice));
				}
				items.push_back(std::move(settings));
			}
			UI::Widgets::MenuItem remove;
			remove.label = "Remove Block";
			for (const GraphNode& block : stack->blocks) {
				const GraphNodeDefinition* definition = GraphNodeLibrary::Find(block.def);
				UI::Widgets::MenuItem item;
				item.label = definition ? str(definition->name) : block.def;
				if (block.def == GraphSurface::INTERPOLATOR_BLOCK) {
					item.label = InterpolatorName(block);
				}
				item.onSelect = [window, blockId = block.id]() {
					window->Edit("Remove Block", [&](GraphDocument& edited) { return Stack::RemoveBlock(edited, blockId); });
				};
				remove.children.push_back(std::move(item));
			}
			remove.enabled = !remove.children.empty();
			items.push_back(std::move(remove));
			OpenMenu(context, items);
			return;
		}
		if (!_window._canvas.IsSelected({GraphItemKind::Node, node})) {
			const GraphItem item{GraphItemKind::Node, node};
			_window._canvas.SetSelection({&item, 1});
			OnSelectionChanged();
		}
		items.push_back({"Copy", "", true, false, [window]() { window->CopySelection(false); }});
		items.push_back({"Cut", "", true, false, [window]() { window->CopySelection(true); }});
		items.push_back({"Duplicate", "", true, false, [window]() { window->DuplicateSelection(); }});
		items.push_back({"Delete", "", true, true, [window]() { window->DeleteSelection(); }});
		OpenMenu(context, items);
	}

	void NodeShaderWindow::Model::AddOutputMenu(Vector<UI::Widgets::MenuItem>& items, const GraphContext& stack) {
		NodeShaderWindow* window = &_window;
		const u32 id = stack.node.id;
		UI::Widgets::MenuItem add;
		add.label = "Add Output";
		for (std::string_view type : GRAPH_SUBGRAPH_OUTPUT_TYPES) {
			add.children.push_back({str(type), "", true, false, [window, id, type = str(type)]() {
										window->Edit("Add Output", [&](GraphDocument& edited) { return Ops::AddSubgraphOutput(edited, id, type) != 0; });
									}});
		}
		items.push_back(std::move(add));
		for (const GraphNode& block : stack.blocks) {
			const str name = GraphSubgraphs::OutputNameOf(block);
			UI::Widgets::MenuItem output;
			output.label = name;
			UI::Widgets::MenuItem rename;
			rename.label = "Rename...";
			rename.onSelect = [window, blockId = block.id, name]() {
				UI::Context* where = window->GetPanelContext();
				if (!where) {
					return;
				}
				window->_popup = EditorDialogs::OpenTextInput(*where, "Rename Output", name, "Rename", [window, blockId](const str& renamed) {
					window->_popup = {};
					window->Edit("Rename Output", [&](GraphDocument& edited) { return Ops::RenameSubgraphOutput(edited, blockId, renamed); });
				});
			};
			output.children.push_back(std::move(rename));
			UI::Widgets::MenuItem retype;
			retype.label = "Type";
			const u32 current = GraphSubgraphs::OutputTypeOf(block);
			for (u32 i = 0; i < std::size(GRAPH_SUBGRAPH_OUTPUT_TYPES); ++i) {
				UI::Widgets::MenuItem option;
				option.label = str(GRAPH_SUBGRAPH_OUTPUT_TYPES[i]);
				option.checked = i == current;
				option.onSelect = [window, blockId = block.id, type = str(GRAPH_SUBGRAPH_OUTPUT_TYPES[i])]() {
					window->Edit("Output Type", [&](GraphDocument& edited) { return Ops::SetSubgraphOutputType(edited, blockId, type); });
				};
				retype.children.push_back(std::move(option));
			}
			output.children.push_back(std::move(retype));
			output.children.push_back({"Remove", "", true, false, [window, blockId = block.id]() {
										   window->Edit("Remove Output", [&](GraphDocument& edited) { return Stack::RemoveBlock(edited, blockId); });
									   }});
			items.push_back(std::move(output));
		}
	}

	void NodeShaderWindow::Model::OnNodeDoubleClick(GraphNodeId node) {
		const GraphDocument* document = Doc();
		const GraphNode* held = document ? GraphDocumentOps::FindNode(*const_cast<GraphDocument*>(document), static_cast<u32>(node)) : nullptr;
		const Guid asset = held ? GraphSubgraphs::AssetOf(*held) : Guid{};
		if (asset.IsValid()) {
			NodeShaderWindowRegistry::Open(asset);
		}
	}

	void NodeShaderWindow::Model::OnCanvasContextMenu(UI::Context& context, Vec2 graph) {
		NodeShaderWindow* window = &_window;
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

	bool NodeShaderWindow::Model::AcceptsDrop(const UI::DragPayload& payload) const {
		u64 property = 0;
		return _window._blackboard.OwnsPayload(payload, property);
	}

	void NodeShaderWindow::Model::OnDrop(const UI::DragPayload& payload, Vec2 graph) {
		u64 property = 0;
		if (!_window._blackboard.OwnsPayload(payload, property)) {
			return;
		}
		u32 created = 0;
		_window.Edit("Create Property Node", [&](GraphDocument& document) {
			created = Ops::AddPropertyNode(document, static_cast<u32>(property), graph);
			return created != 0;
		});
		if (created != 0) {
			const GraphItem item{GraphItemKind::Node, CanvasId(created)};
			_window.SelectItems({&item, 1});
		}
	}

	bool NodeShaderWindow::Model::CanConnect(const GraphPortRef& output, const GraphPortRef& input) const {
		const GraphDocument* document = Doc();
		u32 fromNode = 0;
		u32 toNode = 0;
		str fromPort;
		str toPort;
		if (!document || !OutputOf(output, fromNode, fromPort) || !InputOf(input, toNode, toPort)) {
			return false;
		}
		return Ops::CanConnect(*document, fromNode, fromPort, toNode, toPort, nullptr, &_window._definitions);
	}

	void NodeShaderWindow::Model::Connect(const GraphPortRef& output, const GraphPortRef& input) {
		u32 fromNode = 0;
		u32 toNode = 0;
		str fromPort;
		str toPort;
		if (!OutputOf(output, fromNode, fromPort) || !InputOf(input, toNode, toPort)) {
			return;
		}
		_window.Edit("Connect", [&](GraphDocument& document) {
			return Ops::CanConnect(document, fromNode, fromPort, toNode, toPort, nullptr, &_window._definitions) &&
				   GraphDocumentOps::Connect(document, fromNode, fromPort, toNode, toPort);
		});
	}

	void NodeShaderWindow::Model::GetSearchEntries(const GraphPortRef* from, Vector<GraphSearchEntry>& out) {
		out.clear();
		const GraphDocument* document = Doc();
		if (!document) {
			return;
		}
		u32 fromNode = 0;
		str fromPort;
		const bool fromOutput = from && OutputOf(*from, fromNode, fromPort);
		const bool fromInput = from && !fromOutput && InputOf(*from, fromNode, fromPort);
		if (from && !fromOutput && !fromInput) {
			return;
		}
		const bool subgraphDocument = document->type == GRAPH_SUBGRAPH_DOCUMENT_TYPE;
		GraphWindowParts::LibraryEntries(
			*document, _window._definitions,
			[subgraphDocument](const GraphNodeDefinition& definition) {
				return definition.category != "Context" && definition.category != "Block" && definition.id != Ops::PROPERTY_NODE &&
					   definition.id != Stack::INTERPOLATOR_NODE && definition.id != GraphDocumentOps::REDIRECT_DEF &&
					   (!definition.subgraphOnly || subgraphDocument) && !GraphEffectRules::IsEffectOnly(definition.id);
			},
			fromOutput, fromInput, fromNode, fromPort, out);
		_subgraphChoices.clear();
		Vector<Guid> subgraphs{GetMemoryResource(MemTag::Editor)};
		GraphSubgraphs::ProjectSubgraphs(subgraphs);
		for (const Guid& asset : subgraphs) {
			const GraphSubgraph* subgraph = asset == _window._open ? nullptr : _window._definitions.Subgraph(asset);
			if (!subgraph || GraphSubgraphs::Uses(_window._definitions, subgraph->document, _window._open)) {
				continue;
			}
			const i32 port = GraphWindowParts::JoiningPort(*document, _window._definitions, subgraph->definition, fromOutput, fromInput, fromNode, fromPort);
			if (port < 0 && (fromOutput || fromInput)) {
				continue;
			}
			GraphSearchEntry entry;
			entry.name = subgraph->name;
			entry.category = subgraph->category;
			if (port >= 0) {
				entry.detail = str(fromOutput ? GraphPortLabel(subgraph->inputs[static_cast<size_t>(port)])
											  : GraphPortLabel(subgraph->outputs[static_cast<size_t>(port)]));
			}
			entry.key = SUBGRAPH_KEY | (static_cast<u64>(_subgraphChoices.size()) << 8) | static_cast<u64>(port < 0 ? 0 : port);
			_subgraphChoices.push_back(asset);
			out.push_back(std::move(entry));
		}
		if (!fromOutput && !fromInput) {
			GraphSearchEntry entry;
			entry.name = "Custom Function";
			entry.category = "Utility";
			entry.synonyms = "code HLSL";
			entry.key = CUSTOM_FUNCTION_KEY;
			out.push_back(std::move(entry));
		}
		if (fromOutput) {
			return;
		}
		Vector<u32> interpolators{GetMemoryResource(MemTag::Editor)};
		Stack::Interpolators(*document, interpolators);
		for (u32 block : interpolators) {
			const GraphNode* held = GraphDocumentOps::FindNode(*const_cast<GraphDocument*>(document), block);
			if (!held || (fromInput && !Ops::InputTakes(*document, fromNode, fromPort, GraphPortType::Float4, &_window._definitions))) {
				continue;
			}
			GraphSearchEntry entry;
			entry.name = InterpolatorName(*held);
			entry.category = "Custom Interpolator";
			entry.key = INTERPOLATOR_KEY | block;
			out.push_back(std::move(entry));
		}
		for (const GraphProperty& property : document->properties) {
			if (fromInput && !Ops::PropertyFeeds(*document, property.id, fromNode, fromPort, &_window._definitions)) {
				continue;
			}
			GraphSearchEntry entry;
			entry.name = property.name;
			entry.category = "Properties";
			entry.key = PROPERTY_KEY | property.id;
			out.push_back(std::move(entry));
		}
	}

	void NodeShaderWindow::Model::OnSearchPick(const GraphSearchEntry& entry, Vec2 graph, const GraphPortRef* from) {
		u32 fromNode = 0;
		str fromPort;
		const bool fromOutput = from && OutputOf(*from, fromNode, fromPort);
		const bool fromInput = from && !fromOutput && InputOf(*from, fromNode, fromPort);
		u32 created = 0;
		_window.Edit("Create Node", [&](GraphDocument& document) {
			u32 port = 0;
			if ((entry.key & PROPERTY_KEY) != 0) {
				created = Ops::AddPropertyNode(document, static_cast<u32>(entry.key & ~PROPERTY_KEY), graph);
			} else if ((entry.key & INTERPOLATOR_KEY) != 0) {
				created = Stack::AddInterpolatorNode(document, static_cast<u32>(entry.key & ~INTERPOLATOR_KEY), graph);
			} else if ((entry.key & CUSTOM_FUNCTION_KEY) != 0) {
				created = Ops::AddCustomFunctionNode(document, graph);
			} else if ((entry.key & SUBGRAPH_KEY) != 0) {
				const u64 index = (entry.key & ~SUBGRAPH_KEY) >> 8;
				port = static_cast<u32>(entry.key & 0xff);
				if (index < _subgraphChoices.size()) {
					created = Ops::AddSubgraphNode(document, _subgraphChoices[index], graph);
				}
			} else {
				return (created = GraphWindowParts::MakeLibraryNode(document, _window._definitions, entry.key, graph, fromOutput, fromInput, fromNode,
																	fromPort)) != 0;
			}
			if (created != 0) {
				GraphWindowParts::JoinDroppedWire(document, _window._definitions, created, port, fromOutput, fromInput, fromNode, fromPort);
			}
			return created != 0;
		});
		if (created != 0) {
			const GraphItem item{GraphItemKind::Node, CanvasId(created)};
			_window._canvas.SetSelection({&item, 1});
			_window._inspected = created;
			_window._inspectedProperty = 0;
			_window._needsInspectorRebuild = true;
		}
	}

	void NodeShaderWindow::Model::GetGroups(Vector<GraphGroupDesc>& out) const {
		out.clear();
		if (const GraphDocument* document = Doc()) {
			GraphWindowParts::DescribeGroups(*document, out);
		}
	}

	void NodeShaderWindow::Model::GetNotes(Vector<GraphNoteDesc>& out) const {
		out.clear();
		if (const GraphDocument* document = Doc()) {
			GraphWindowParts::DescribeNotes(*document, out);
		}
	}

	void NodeShaderWindow::Model::MoveNote(u64 note, Vec2 graph) {
		if (GraphDocument* document = Doc()) {
			GraphDocumentOps::MoveNode(*document, static_cast<u32>(note), graph);
		}
	}

	void NodeShaderWindow::Model::MoveGroup(u64 group, Vec2 graph) {
		if (GraphDocument* document = Doc()) {
			GraphDocumentOps::MoveNode(*document, static_cast<u32>(group), graph);
		}
	}

	void NodeShaderWindow::Model::ResizeNote(u64 note, Vec2 size) {
		if (GraphDocument* document = Doc()) {
			GraphWindowParts::ResizeNote(*document, note, size);
		}
	}

	void NodeShaderWindow::Model::SetGroupOf(std::span<const GraphItem> items, u64 group) {
		if (GraphDocument* document = Doc()) {
			GraphWindowParts::SetGroupOf(*document, items, group);
		}
	}

	void NodeShaderWindow::Model::RenameGroup(u64 group, std::string_view title) {
		_window.Edit("Rename Group", [&](GraphDocument& document) { return GraphDocumentOps::SetGroupTitle(document, static_cast<u32>(group), title); });
	}

	void NodeShaderWindow::Model::EditNote(u64 note, std::string_view title, std::string_view body) {
		_window.Edit("Edit Sticky Note", [&](GraphDocument& document) { return GraphWindowParts::EditNote(document, note, title, body); });
	}

	void NodeShaderWindow::Model::OnGroupContextMenu(UI::Context& context, u64 group) {
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::UI)};
		GraphWindowParts::GroupMenu(items, _window.MakeEditor(), group,
									[](GraphDocument& document, std::span<const u32> ids) { return Ops::Delete(document, ids); });
		OpenMenu(context, items);
	}

	void NodeShaderWindow::Model::OnNoteContextMenu(UI::Context& context, u64 note) {
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::UI)};
		GraphWindowParts::NoteMenu(items, _window.MakeEditor(), note);
		OpenMenu(context, items);
	}

	void NodeShaderWindow::Model::OnWireContextMenu(UI::Context& context, GraphWireId wire, Vec2 graph) {
		NodeShaderWindow* window = &_window;
		const GraphPortRef input{wire >> 32, GraphPortSide::Input, static_cast<u32>(wire & 0xffffffffu)};
		u32 node = 0;
		str port;
		if (!InputOf(input, node, port)) {
			return;
		}
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::UI)};
		items.push_back({"Insert Redirect", "", true, false, [window, node, port, graph]() {
							 window->Edit("Insert Redirect", [&](GraphDocument& document) { return GraphDocumentOps::InsertRedirect(document, node, port, graph) != 0; });
						 }});
		items.push_back({"Delete", "", true, true, [window, node, port]() {
							 window->Edit("Delete Wire", [&](GraphDocument& document) { return GraphDocumentOps::Disconnect(document, node, port); });
						 }});
		OpenMenu(context, items);
	}

	void NodeShaderWindow::Model::OnWireDoubleClick(GraphWireId wire, Vec2 graph) {
		const GraphPortRef input{wire >> 32, GraphPortSide::Input, static_cast<u32>(wire & 0xffffffffu)};
		u32 node = 0;
		str port;
		if (InputOf(input, node, port)) {
			_window.Edit("Insert Redirect", [&](GraphDocument& document) { return GraphDocumentOps::InsertRedirect(document, node, port, graph) != 0; });
		}
	}

	bool NodeShaderWindow::Model::OnGraphCommand(GraphCommand command) {
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
			GetNodePosition(CanvasId(members[0]), corner);
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

	NodeShaderWindow::NodeShaderWindow() { _model = New<Model>(MemTag::Editor, *this); }

	NodeShaderWindow::~NodeShaderWindow() {
		CloseGraph();
		if (_model) {
			Delete(_model);
			_model = nullptr;
		}
	}

	GraphViewModel& NodeShaderWindow::GetModel() { return *_model; }

	GraphDocument* NodeShaderWindow::GetDocument() const { return _documentOpen ? NodeShaderDocuments::Get(_open) : nullptr; }

	str NodeShaderWindow::GetTitleBase() const {
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

	bool NodeShaderWindow::OpenGraph(const Guid& asset) {
		if (asset == _open && _documentOpen) {
			return true;
		}
		const Path path = AssetDatabase::LookupPath(asset);
		if (!asset.IsValid() || !IsNodeShaderPath(path.ToString())) {
			SLT_LOG_WARN("NodeShader", "'{}' isn't a node shader", path.ToString());
			return false;
		}
		str error;
		if (!NodeShaderDocuments::Open(asset, GetType() ? GetType()->ownerModule : nullptr, error, &NodeShaderStackOps::UpgradeOnOpen)) {
			SLT_LOG_ERROR("NodeShader", "cannot open '{}': {}", path.ToString(), error);
			return false;
		}
		CloseGraph();
		_open = asset;
		graph = asset;
		_documentOpen = true;
		_seenRevision = NodeShaderDocuments::GetRevision(asset);
		_definitions.Clear();
		_previews.Bind(asset, path.ToString(), GetInstanceSerial());
		_previews.Invalidate();
		_inspected = 0;
		_inspectedProperty = 0;
		_canvas.ResetView();
		_canvas.SetSelection({});
		_needsNodeRebuild = true;
		_needsInspectorRebuild = true;
		_needsBlackboardRebuild = true;
		_needsFrame = true;
		ApplyMeshSetting();
		return true;
	}

	void NodeShaderWindow::CloseGraph() {
		if (_popup.IsValid()) {
			if (UI::Context* context = GetPanelContext(); context && context->IsAlive(_popup)) {
				context->ClosePopup(_popup);
			}
			_popup = {};
		}
		if (_documentOpen) {
			NodeShaderDocuments::Close(_open);
			_documentOpen = false;
		}
		_open = {};
		_definitions.Clear();
	}

	void NodeShaderWindow::OnOpen() {
		if (graph.IsValid() && !_documentOpen) {
			OpenGraph(graph);
		}
	}

	void NodeShaderWindow::OnClose() { CloseGraph(); }

	void NodeShaderWindow::BeginEdit(std::string_view label) {
		if (_documentOpen) {
			NodeShaderDocuments::BeginEdit(_open, label);
		}
	}

	void NodeShaderWindow::EndEdit() {
		if (!_documentOpen) {
			return;
		}
		NodeShaderDocuments::EndEdit(_open);
		if (NodeShaderDocuments::GetRevision(_open) != _seenRevision) {
			OnDocumentChanged();
		}
	}

	void NodeShaderWindow::OnDocumentChanged() {
		_seenRevision = NodeShaderDocuments::GetRevision(_open);
		_previews.Invalidate();
		_needsNodeRebuild = true;
		_needsInspectorRebuild = true;
		_needsBlackboardRebuild = true;
		ApplyMeshSetting();
	}

	GraphWindowParts::Editor NodeShaderWindow::MakeEditor() {
		GraphWindowParts::Editor editor;
		editor.edit = [this](std::string_view label, const std::function<bool(GraphDocument&)>& op) { return Edit(label, op); };
		editor.begin = [this](std::string_view label) { BeginEdit(label); };
		editor.end = [this]() { EndEdit(); };
		editor.document = [this]() { return GetDocument(); };
		return editor;
	}

	bool NodeShaderWindow::HasUnsavedChanges() const { return _documentOpen && NodeShaderDocuments::HasUnsavedChanges(_open); }

	str NodeShaderWindow::GetSaveChangesMessage() const {
		return "Save the changes to this node shader?\n\n" + AssetDatabase::LookupPath(_open).ToString() + "\n\nUnsaved changes are lost when it closes.";
	}

	void NodeShaderWindow::SaveChanges() { Save(); }

	void NodeShaderWindow::DiscardChanges() {
		if (_documentOpen) {
			NodeShaderDocuments::Discard(_open);
		}
	}

	bool NodeShaderWindow::Save() { return _documentOpen && NodeShaderDocuments::Save(_open); }

	Guid NodeShaderWindow::SaveAs(const Path& path) {
		if (!_documentOpen) {
			return Guid{};
		}
		const Guid copy = NodeShaderDocuments::SaveAs(_open, path);
		if (copy.IsValid()) {
			NodeShaderWindowRegistry::Open(copy);
		}
		return copy;
	}

	bool NodeShaderWindow::ViewGeneratedShader() {
		const str& text = _previews.GetSurfaceText();
		if (text.empty()) {
			return false;
		}
		const Path library = FileSystem::Resolve(Path(str(GRAPH_GENERATED_DIRECTORY)));
		const Path file(library.ToString() + "/" + GetTitleBase() + ".generated.hlsl");
		FileSystem::CreateDirectory(library);
		if (!FileSystem::WriteTextFile(file, text)) {
			return false;
		}
		return AssetBrowserOps::OpenInExternalEditor(file, 0);
	}

	void NodeShaderWindow::ApplyMeshSetting() {
		const GraphDocument* document = GetDocument();
		if (!document) {
			return;
		}
		const str stored = Ops::GraphSettingText(*document, NodeShaderPreviews::MESH_SETTING);
		struct Named {
			std::string_view name;
			NodeShaderPreviewMesh mesh;
		};
		constexpr Named NAMES[] = {{"sphere", NodeShaderPreviewMesh::Sphere},
								   {"capsule", NodeShaderPreviewMesh::Capsule},
								   {"cylinder", NodeShaderPreviewMesh::Cylinder},
								   {"cube", NodeShaderPreviewMesh::Cube},
								   {"quad", NodeShaderPreviewMesh::Quad}};
		for (const Named& named : NAMES) {
			if (stored == named.name) {
				_previews.SetMesh(named.mesh, Guid{});
				return;
			}
		}
		if (const Guid custom = Guid::FromString(stored); custom.IsValid()) {
			_previews.SetMesh(NodeShaderPreviewMesh::Custom, custom);
			return;
		}
		_previews.SetMesh(NodeShaderPreviewMesh::Sphere, Guid{});
	}

	void NodeShaderWindow::OnBuildContent(UI::Context& context, UI::ElementHandle root) {
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

		UI::ElementHandle middle = context.CreateElement();
		context.AppendChild(body, middle);
		UI::Style middleStyle;
		middleStyle.flexDirection = UI::FlexDirection::Column;
		middleStyle.flexGrow = 1.0f;
		middleStyle.minWidth = UI::Length::Pixel(0.0f);
		middleStyle.height = UI::Length::Percent(1.0f);
		context.SetStyle(middle, middleStyle);
		_canvas.Build(context, middle, _model);
		BuildMainPreview(context, middle);

		BuildInspector(context, body);

		RebuildNodes(context);
		_needsFrame = true;
		RebuildInspector(context);
		RebuildBlackboard();
		RefreshStatus(context);
	}

	void NodeShaderWindow::BuildToolbar(UI::Context& context, UI::ElementHandle parent) {
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
			UI::Widgets::Button(context, bar, desc);
		};
		button("Save Asset", [this]() { Save(); });
		button("Save As...", [this]() {
			if (UI::Context* c = GetPanelContext()) {
				OpenSaveAs(*c);
			}
		});
		button("View Generated Shader", [this]() { ViewGeneratedShader(); });
		UI::Widgets::Spacer(context, bar);
		_status = AddText(context, bar, ColorToken::WidgetTextDim, MetricToken::FontSmall, "");
	}

	void NodeShaderWindow::BuildBlackboard(UI::Context& context, UI::ElementHandle parent) {
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

		GraphBlackboardDesc desc;
		desc.title = GetTitleBase();
		for (std::string_view type : Ops::PropertyTypes()) {
			desc.kinds.push_back(str(Ops::PropertyTypeLabel(type)));
		}
		desc.onAdd = [this](u32 kind) -> u64 {
			u32 created = 0;
			Edit("Add Property", [&](GraphDocument& document) {
				created = Ops::AddProperty(document, Ops::PropertyTypes()[kind]);
				return created != 0;
			});
			return created;
		};
		desc.onRename = [this](u64 id, std::string_view name) -> u64 {
			return Edit("Rename Property", [&](GraphDocument& document) { return Ops::RenameProperty(document, static_cast<u32>(id), name); }) ? id : 0;
		};
		desc.onMove = [this](u64 id, u32 index) -> u64 {
			return Edit("Move Property", [&](GraphDocument& document) { return Ops::MoveProperty(document, static_cast<u32>(id), index); }) ? id : 0;
		};
		desc.onDelete = [this](std::span<const u64> ids) {
			Vector<u32> properties{GetMemoryResource(MemTag::Editor)};
			for (u64 id : ids) {
				properties.push_back(static_cast<u32>(id));
			}
			Edit("Delete Property", [&](GraphDocument& document) { return Ops::DeleteProperties(document, properties) > 0; });
		};
		desc.onSelectionChanged = [this](std::span<const u64> ids) {
			const u32 property = ids.empty() ? 0u : static_cast<u32>(ids.back());
			if (property != _inspectedProperty) {
				_inspectedProperty = property;
				_graphSettingsTab = false;
				_needsInspectorRebuild = true;
			}
		};
		_blackboard.Build(this, context, column, std::move(desc));
	}

	void NodeShaderWindow::BuildInspector(UI::Context& context, UI::ElementHandle parent) {
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

		UI::ElementHandle tabs = AddRow(context, column);
		auto tab = [&](std::string_view label, bool graphTab) {
			UI::Widgets::ButtonDesc desc;
			desc.label = label;
			desc.font = UISystem::GetDefaultFont();
			desc.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
			desc.onClick = [this, graphTab]() {
				_graphSettingsTab = graphTab;
				_needsInspectorRebuild = true;
			};
			UI::Widgets::Button(context, tabs, desc);
		};
		tab("Node Settings", false);
		tab("Graph Settings", true);

		_inspector = context.CreateElement();
		context.AppendChild(column, _inspector);
		UI::Style content;
		content.flexDirection = UI::FlexDirection::Column;
		content.width = UI::Length::Percent(1.0f);
		content.margin = {0.0f, 6.0f, 0.0f, 0.0f};
		context.SetStyle(_inspector, content);
	}

	void NodeShaderWindow::BuildMainPreview(UI::Context& context, UI::ElementHandle parent) {
		_mainPane = context.CreateElement();
		context.AppendChild(parent, _mainPane);
		UI::Style style;
		style.position = UI::Position::Absolute;
		style.right = UI::Length::Pixel(MAIN_PREVIEW_INSET);
		style.bottom = UI::Length::Pixel(MAIN_PREVIEW_INSET);
		style.width = UI::Length::Pixel(MAIN_PREVIEW_SIZE);
		style.height = UI::Length::Pixel(MAIN_PREVIEW_SIZE);
		style.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::ViewportClear));
		style.borderColor = EditorTheme::GetColor(ColorToken::CardEdge);
		style.borderWidth = {1.0f, 1.0f, 1.0f, 1.0f};
		style.zIndex = 1;
		context.SetStyle(_mainPane, style);

		UI::ElementEvents& events = context.Events(_mainPane);
		events.customDraw = [this](UI::Context& c, UI::ElementHandle element, UI::PaintScope& paint) {
			const RHI::TextureHandle texture = _previews.GetMainTexture();
			if (!texture.IsValid() || !_previews.HasMainPreview()) {
				return;
			}
			paint.EmitTexturedQuad(c.GetLayout(element).content, texture, UI::Rect{0.0f, 0.0f, 1.0f, 1.0f}, Color::White());
		};
		events.onPressDrag = [this](Vec2 pointPx) {
			if (!_dragging) {
				_dragging = true;
				_dragFrom = pointPx;
				return;
			}
			_previews.SetOrbit(EditorPreviewOps::OrbitAfterDrag(_previews.GetOrbit(), pointPx - _dragFrom));
			_dragFrom = pointPx;
		};
		events.onPressEnd = [this]() { _dragging = false; };
		events.onWheel = [this](Vec2 notches) {
			if (notches.y == 0.0f) {
				return false;
			}
			_previews.SetOrbit(EditorPreviewOps::OrbitAfterZoom(_previews.GetOrbit(), notches.y));
			return true;
		};
		events.onRightClick = [this]() {
			if (UI::Context* c = GetPanelContext()) {
				OpenMeshMenu(*c);
			}
		};
		events.onDragOver = [](const UI::DragPayload& payload) {
			Guid mesh;
			return MeshOf(payload, mesh);
		};
		events.onDrop = [this](const UI::DragPayload& payload, Vec2) {
			Guid mesh;
			if (MeshOf(payload, mesh)) {
				SetMeshSetting(mesh.ToString());
			}
		};
	}

	void NodeShaderWindow::SetMeshSetting(std::string_view value) {
		Edit("Change Preview Mesh", [&](GraphDocument& document) { return Ops::SetGraphSetting(document, NodeShaderPreviews::MESH_SETTING, Ops::Quote(value)); });
		ApplyMeshSetting();
	}

	void NodeShaderWindow::OpenMeshMenu(UI::Context& context) {
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::UI)};
		constexpr std::string_view PRIMITIVES[][2] = {{"Sphere", "sphere"}, {"Capsule", "capsule"}, {"Cylinder", "cylinder"}, {"Cube", "cube"}, {"Quad", "quad"}};
		for (const auto& primitive : PRIMITIVES) {
			items.push_back({str(primitive[0]), "", true, false, [this, value = str(primitive[1])]() { SetMeshSetting(value); }});
		}
		UI::Widgets::MenuItem custom{"Custom Mesh"};
		custom.separatorBefore = true;
		Vector<AssetBrowserOps::AssignableAsset> meshes{GetMemoryResource(MemTag::Editor)};
		AssetBrowserOps::CollectAssignableAssets(MeshAsset::GetReflectType()->name, meshes);
		for (const AssetBrowserOps::AssignableAsset& mesh : meshes) {
			custom.children.push_back({mesh.display, "", true, false, [this, guid = mesh.guid]() { SetMeshSetting(guid.ToString()); }});
		}
		custom.enabled = !custom.children.empty();
		items.push_back(std::move(custom));
		UI::Widgets::OpenMenu(context, context.GetPointerPosition(), items, UISystem::GetDefaultFont(), FontRow());
	}

	void NodeShaderWindow::OpenSaveAs(UI::Context& context) {
		const str current = AssetDatabase::LookupPath(_open).ToString();
		_popup = EditorDialogs::OpenTextInput(context, "Save Graph As", current, "Save", [this](const str& path) {
			_popup = {};
			SaveAs(Path(path));
		});
	}

	void NodeShaderWindow::RebuildInspector(UI::Context& context) {
		_needsInspectorRebuild = false;
		if (!context.IsAlive(_inspector)) {
			return;
		}
		context.ClearChildren(_inspector);
		if (!GetDocument()) {
			AddText(context, _inspector, ColorToken::WidgetTextDim, MetricToken::FontRow, "No graph open");
			return;
		}
		if (_graphSettingsTab) {
			BuildGraphSettings(context, _inspector);
		} else if (_inspectedProperty != 0) {
			BuildPropertySettings(context, _inspector, _inspectedProperty);
		} else {
			BuildNodeSettings(context, _inspector);
		}
	}

	void NodeShaderWindow::BuildGraphSettings(UI::Context& context, UI::ElementHandle parent) {
		const GraphDocument* document = GetDocument();
		AddText(context, parent, ColorToken::TextStrong, MetricToken::FontBody, "Graph Settings");
		if (document->type == GRAPH_SUBGRAPH_DOCUMENT_TYPE) {
			UI::ElementHandle row = AddField(context, parent, "Category");
			const str stored = Ops::GraphSettingText(*document, GRAPH_SUBGRAPH_CATEGORY_SETTING);
			const str shown = stored.empty() ? str(GRAPH_SUBGRAPH_DEFAULT_CATEGORY) : stored;
			UI::Widgets::TextFieldDesc desc;
			desc.text = shown;
			desc.font = UISystem::GetDefaultFont();
			desc.fontSizePx = FontRow();
			desc.onCommit = [this](std::string_view text) {
				Edit("Graph Settings", [&](GraphDocument& edited) {
					return Ops::SetGraphSetting(edited, GRAPH_SUBGRAPH_CATEGORY_SETTING, text.empty() ? str() : Ops::Quote(text));
				});
			};
			UI::Widgets::TextField(context, row, desc);
			return;
		}
		struct Choice {
			std::string_view label;
			std::string_view key;
			std::span<const std::string_view> shown;
			std::span<const std::string_view> stored;
		};
		static constexpr std::string_view TARGET_SHOWN[] = {"Lit", "Unlit", "Custom Lighting", "Fullscreen", "Sky"};
		static constexpr std::string_view TARGET_STORED[] = {"lit", "unlit", "customLighting", "fullscreen", "sky"};
		static constexpr std::string_view BLEND_SHOWN[] = {"Opaque", "Alpha", "Premultiply", "Additive", "Multiply"};
		static constexpr std::string_view BLEND_STORED[] = {"opaque", "alpha", "premultiplied", "additive", "multiply"};
		static constexpr std::string_view FACE_SHOWN[] = {"Front", "Back", "Both"};
		static constexpr std::string_view FACE_STORED[] = {"back", "front", "off"};
		static constexpr std::string_view DEPTH_SHOWN[] = {"Test And Write", "Test Only", "Off"};
		static constexpr std::string_view DEPTH_STORED[] = {"test_write", "test_only", "off"};
		static constexpr std::string_view NORMAL_SHOWN[] = {"Tangent", "World", "Object"};
		static constexpr std::string_view NORMAL_STORED[] = {"tangent", "world", "object"};
		const Choice CHOICES[] = {
			{"Material", "target", TARGET_SHOWN, TARGET_STORED},
			{"Blending Mode", "blend", BLEND_SHOWN, BLEND_STORED},
			{"Render Face", "cull", FACE_SHOWN, FACE_STORED},
			{"Depth", "depth", DEPTH_SHOWN, DEPTH_STORED},
			{"Fragment Normal Space", "normalSpace", NORMAL_SHOWN, NORMAL_STORED},
		};
		const bool fullscreen = GraphSurface::IsFullscreen(*document);
		const bool sky = GraphSurface::IsSky(*document);
		for (const Choice& choice : CHOICES) {
			if ((fullscreen && choice.key != "target" && choice.key != "blend") || (sky && choice.key != "target")) {
				continue;
			}
			UI::ElementHandle row = AddField(context, parent, choice.label);
			const str stored = Ops::GraphSettingText(*document, choice.key);
			u32 selected = 0;
			for (u32 i = 0; i < choice.stored.size(); ++i) {
				selected = choice.stored[i] == stored ? i : selected;
			}
			UI::Widgets::DropdownDesc desc;
			desc.items = choice.shown;
			desc.selected = selected;
			desc.font = UISystem::GetDefaultFont();
			desc.fontSizePx = FontRow();
			desc.onSelect = [this, key = str(choice.key), stored = choice.stored](u32 index) {
				if (index < stored.size()) {
					Edit("Graph Settings", [&](GraphDocument& edited) {
						const bool set = Ops::SetGraphSetting(edited, key, Ops::Quote(stored[index]));
						if (set && key == "target" && stored[index] == "customLighting") {
							Stack::EnsureLightingStack(edited);
						}
						if (set && key == "target" && GraphSurface::HasVertexStage(edited)) {
							Stack::EnsureVertexStack(edited);
						}
						return set;
					});
				}
			};
			UI::Widgets::Dropdown(context, row, desc);
		}
		if (!fullscreen && !sky) {
			UI::ElementHandle row = AddField(context, parent, "Support Effects");
			const bool on = std::any_of(document->settings.begin(), document->settings.end(),
										[](const GraphSetting& setting) { return setting.key == "supportEffects" && setting.value == "true"; });
			UI::Widgets::Checkbox(context, row, on, [this](bool checked) {
				Edit("Graph Settings", [&](GraphDocument& edited) { return Ops::SetGraphSetting(edited, "supportEffects", checked ? "true" : ""); });
			});
		}
	}

	void NodeShaderWindow::BuildNodeSettings(UI::Context& context, UI::ElementHandle parent) {
		GraphDocument* document = GetDocument();
		GraphNode* node = _inspected != 0 ? GraphDocumentOps::FindNode(*document, _inspected) : nullptr;
		if (!node) {
			AddText(context, parent, ColorToken::WidgetTextDim, MetricToken::FontRow, "Select a node to edit its settings");
			return;
		}
		if (const u32 property = Ops::PropertyOf(*document, node->id); property != 0) {
			BuildPropertySettings(context, parent, property);
			return;
		}
		if (const u32 block = Stack::InterpolatorOf(*document, node->id); block != 0) {
			BuildInterpolatorSettings(context, parent, block);
			return;
		}
		str missing;
		const GraphNodeDefinition* definition = _definitions.Find(*document, *node, &missing);
		AddText(context, parent, ColorToken::TextStrong, MetricToken::FontBody, definition ? definition->name : std::string_view(node->def));
		for (const GraphDiagnostic& diagnostic : _previews.GetDiagnostics()) {
			if (diagnostic.node == node->id) {
				UI::ElementHandle line = AddText(context, parent, GraphWindowParts::ColorOf(diagnostic.severity), MetricToken::FontRow, diagnostic.message);
				UI::Style wrap;
				wrap.width = UI::Length::Percent(1.0f);
				context.SetStyle(line, wrap);
			}
		}
		if (const Guid asset = GraphSubgraphs::AssetOf(*node); asset.IsValid()) {
			const Path path = AssetDatabase::LookupPath(asset);
			UI::ElementHandle line = AddText(context, parent, ColorToken::WidgetTextDim, MetricToken::FontRow,
											 path.ToString().empty() ? missing : path.ToString());
			UI::Style wrap;
			wrap.width = UI::Length::Percent(1.0f);
			context.SetStyle(line, wrap);
			if (!path.ToString().empty()) {
				UI::Widgets::ButtonDesc open;
				open.label = "Open Subgraph";
				open.font = UISystem::GetDefaultFont();
				open.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
				open.onClick = [asset]() { NodeShaderWindowRegistry::Open(asset); };
				UI::Widgets::Button(context, AddRow(context, parent), open);
			}
		}
		if (node->def == GRAPH_CUSTOM_FUNCTION_NODE) {
			BuildCustomFunctionSettings(context, parent, node->id);
			return;
		}
		if (!definition) {
			return;
		}
		const u32 id = node->id;
		GraphWindowParts::BuildSettings(context, parent, id, definition->settings, MakeEditor());
		GraphWindowParts::BuildOpenGradients(context, parent, *node, *definition, MakeEditor());
	}

	void NodeShaderWindow::BuildInterpolatorSettings(UI::Context& context, UI::ElementHandle parent, u32 block) {
		GraphDocument* document = GetDocument();
		const GraphNode* held = GraphDocumentOps::FindNode(*document, block);
		if (!held) {
			return;
		}
		AddText(context, parent, ColorToken::TextStrong, MetricToken::FontBody, "Custom Interpolator");
		UI::ElementHandle nameRow = AddField(context, parent, "Name");
		const str currentName = InterpolatorName(*held);
		UI::Widgets::TextFieldDesc name;
		name.text = currentName;
		name.font = UISystem::GetDefaultFont();
		name.fontSizePx = FontRow();
		name.onCommit = [this, block](std::string_view text) {
			Edit("Rename Custom Interpolator", [&](GraphDocument& edited) { return Stack::RenameInterpolator(edited, block, text); });
		};
		UI::Widgets::TextField(context, nameRow, name);
		for (const GraphSettingDefinition& setting : InterpolatorSettings()) {
			if (setting.kind != GraphSettingKind::Choice) {
				continue;
			}
			const str current = InterpolatorName(*held, setting.key);
			u32 selected = setting.defaultIndex;
			for (u32 i = 0; i < setting.options.size(); ++i) {
				selected = setting.options[i] == current ? i : selected;
			}
			UI::ElementHandle row = AddField(context, parent, setting.key);
			UI::Widgets::DropdownDesc desc;
			desc.items = setting.options;
			desc.selected = selected;
			desc.font = UISystem::GetDefaultFont();
			desc.fontSizePx = FontRow();
			desc.onSelect = [this, block, key = str(setting.key), options = setting.options](u32 index) {
				if (index < options.size()) {
					Edit("Custom Interpolator", [&](GraphDocument& edited) {
						return key == "Type" ? Stack::SetInterpolatorType(edited, block, options[index]) : Stack::SetInterpolatorMode(edited, block, options[index]);
					});
				}
			};
			UI::Widgets::Dropdown(context, row, desc);
		}
	}

	void NodeShaderWindow::BuildPropertySettings(UI::Context& context, UI::ElementHandle parent, u32 id) {
		const GraphDocument* document = GetDocument();
		const GraphProperty* property = FindProperty(*document, id);
		if (!property) {
			AddText(context, parent, ColorToken::WidgetTextDim, MetricToken::FontRow, "The property is gone");
			return;
		}
		AddText(context, parent, ColorToken::TextStrong, MetricToken::FontBody, str(Ops::PropertyTypeLabel(property->type)) + " Property");
		GraphPropertyInfo info;
		str refused;
		if (!GraphProperties::Parse(*property, info, refused)) {
			UI::ElementHandle line = AddText(context, parent, ColorToken::StatusError, MetricToken::FontRow, property->name + " " + refused);
			UI::Style wrap;
			wrap.width = UI::Length::Percent(1.0f);
			context.SetStyle(line, wrap);
		}

		auto text = [&](std::string_view label, std::string_view value, std::function<void(std::string_view)> commit) {
			UI::ElementHandle row = AddField(context, parent, label);
			UI::Widgets::TextFieldDesc desc;
			desc.text = value;
			desc.font = UISystem::GetDefaultFont();
			desc.fontSizePx = FontRow();
			desc.onCommit = std::move(commit);
			UI::Widgets::TextField(context, row, desc);
		};
		text("Name", property->name, [this, id](std::string_view name) {
			Edit("Rename Property", [&](GraphDocument& edited) { return Ops::RenameProperty(edited, id, name); });
		});
		const bool keyword = GraphProperties::IsKeyword(info.kind);
		const bool enumKeyword = property->type == "EnumKeyword";
		const bool materialRows = document->type != GRAPH_SUBGRAPH_DOCUMENT_TYPE;
		if (materialRows || keyword) {
			text("Reference", property->reference, [this, id](std::string_view reference) {
				Edit("Property Reference", [&](GraphDocument& edited) { return Ops::SetPropertyReference(edited, id, reference); });
			});
			std::string_view said = "Materials store this property under its reference. A new reference (typed, or made by renaming) starts "
									"all materials at the default.";
			if (keyword) {
				said = enumKeyword ? "Materials store this keyword's entries under its reference. A new reference (typed, or made by renaming) "
									 "loses the entry every material picked."
								   : "Materials store this keyword under its reference. A new reference (typed, or made by renaming) turns it "
									 "off on all materials.";
			}
			UI::ElementHandle warning = AddText(context, parent, ColorToken::StatusWarn, MetricToken::FontSmall, said);
			UI::Style wrap;
			wrap.width = UI::Length::Percent(1.0f);
			context.SetStyle(warning, wrap);
		}

		UI::ElementHandle row = GraphProperties::IsTexture(info.kind) ? UI::ElementHandle{} : AddField(context, parent, "Default");
		if (enumKeyword) {
			BuildKeywordEntries(context, parent, row, id);
		} else {
			GraphWindowParts::BuildPropertyDefault(context, row, id, MakeEditor());
		}

		auto setSetting = [this, id](std::string_view key, str json) {
			Edit("Property Setting", [&](GraphDocument& edited) { return Ops::SetPropertySetting(edited, id, key, json); });
		};
		auto modeChoice = [&](std::span<const std::string_view> shown, std::span<const std::string_view> stored) {
			const GraphSetting* held = FindSetting(property->settings, "mode");
			const str current = held ? ReadString(held->value) : str();
			u32 selected = 0;
			for (u32 i = 0; i < stored.size(); ++i) {
				selected = stored[i] == current ? i : selected;
			}
			UI::ElementHandle modeRow = AddField(context, parent, "Mode");
			UI::Widgets::DropdownDesc desc;
			desc.items = shown;
			desc.selected = selected;
			desc.font = UISystem::GetDefaultFont();
			desc.fontSizePx = FontRow();
			desc.onSelect = [setSetting, stored](u32 index) {
				if (index < stored.size()) {
					setSetting("mode", index == 0 ? str() : Ops::Quote(stored[index]));
				}
			};
			UI::Widgets::Dropdown(context, modeRow, desc);
		};
		if (property->type == "Float") {
			static constexpr std::string_view SHOWN[] = {"Default", "Slider", "Integer"};
			static constexpr std::string_view STORED[] = {"default", "slider", "integer"};
			modeChoice(SHOWN, STORED);
			if (info.slider) {
				UI::ElementHandle range = AddField(context, parent, "Min / Max");
				for (u32 end = 0; end < 2; ++end) {
					UI::Widgets::NumberFieldDesc desc;
					desc.value = end == 0 ? info.minValue : info.maxValue;
					desc.font = UISystem::GetDefaultFont();
					desc.fontSizePx = FontRow();
					desc.onChange = [setSetting, end](f64 number) {
						const f32 one = static_cast<f32>(number);
						setSetting(end == 0 ? "min" : "max", Numbers(&one, 1));
					};
					UI::Widgets::NumberField(context, range, desc);
				}
			}
		} else if (property->type == "Color" && materialRows) {
			static constexpr std::string_view SHOWN[] = {"Default", "HDR"};
			static constexpr std::string_view STORED[] = {"default", "hdr"};
			modeChoice(SHOWN, STORED);
		} else if (property->type == "Texture2D" && materialRows) {
			static constexpr std::string_view SHOWN[] = {"White", "Black", "Normal", "Transparent", "Grey"};
			static constexpr std::string_view STORED[] = {"white", "black", "normal", "transparent", "gray"};
			modeChoice(SHOWN, STORED);
		}
		if (!materialRows && !keyword && !GraphProperties::IsTexture(info.kind)) {
			UI::ElementHandle binding = AddField(context, parent, "Use Custom Binding");
			UI::Widgets::Checkbox(context, binding, info.customBinding, [setSetting](bool on) { setSetting("customBinding", on ? str("true") : str()); });
			if (info.customBinding) {
				text("Label", info.bindingLabel, [setSetting](std::string_view label) { setSetting("bindingLabel", label.empty() ? str() : Ops::Quote(label)); });
			}
		}
		if (keyword || !materialRows) {
			return;
		}
		UI::ElementHandle shown = AddField(context, parent, "Show In Inspector");
		UI::Widgets::Checkbox(context, shown, !info.hidden, [setSetting](bool on) { setSetting("hidden", on ? str() : str("true")); });
		const GraphSetting* group = FindSetting(property->settings, "group");
		text("Group", group ? ReadString(group->value) : str(), [setSetting](std::string_view name) { setSetting("group", name.empty() ? str() : Ops::Quote(name)); });
	}

	void NodeShaderWindow::BuildKeywordEntries(UI::Context& context, UI::ElementHandle parent, UI::ElementHandle row, u32 id) {
		const GraphDocument* document = GetDocument();
		const GraphProperty* property = document ? FindProperty(*document, id) : nullptr;
		Vector<GraphKeywordEntry> entries{GetMemoryResource(MemTag::Editor)};
		str unread;
		if (!property || !GraphProperties::ReadEntries(*property, entries, unread)) {
			return;
		}

		Vector<str> names{GetMemoryResource(MemTag::Editor)};
		for (const GraphKeywordEntry& entry : entries) {
			names.push_back(entry.name);
		}
		Vector<std::string_view> items{names.begin(), names.end(), GetMemoryResource(MemTag::Editor)};
		f32 stored[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		ReadNumbers(property->defaultValue, stored);
		UI::Widgets::DropdownDesc pick;
		pick.items = items;
		pick.selected = std::min(static_cast<u32>(std::max(stored[0], 0.0f)), static_cast<u32>(entries.size() - 1));
		pick.font = UISystem::GetDefaultFont();
		pick.fontSizePx = FontRow();
		pick.onSelect = [this, id](u32 index) {
			Edit("Property Default", [&](GraphDocument& edited) { return Ops::SetPropertyDefault(edited, id, std::to_string(index)); });
		};
		UI::Widgets::Dropdown(context, row, pick);

		GraphBlackboardDesc desc;
		desc.title = "Entries";
		desc.kinds.push_back("Entry");
		desc.onAdd = [this, id](u32) -> u64 {
			u32 made = 0;
			Edit("Add Entry", [&](GraphDocument& edited) {
				made = Ops::AddKeywordEntry(edited, id);
				return made != 0;
			});
			return made;
		};
		desc.onRename = [this, id](u64 entry, std::string_view name) -> u64 {
			return Edit("Rename Entry", [&](GraphDocument& edited) { return Ops::RenameKeywordEntry(edited, id, static_cast<u32>(entry), name); })
					   ? entry
					   : 0;
		};
		desc.onMove = [this, id](u64 entry, u32 index) -> u64 {
			return Edit("Move Entry", [&](GraphDocument& edited) { return Ops::MoveKeywordEntry(edited, id, static_cast<u32>(entry), index); })
					   ? entry
					   : 0;
		};
		desc.onDelete = [this, id](std::span<const u64> doomed) {
			Vector<u32> entries{GetMemoryResource(MemTag::Editor)};
			for (u64 entry : doomed) {
				entries.push_back(static_cast<u32>(entry));
			}
			Edit("Delete Entry", [&](GraphDocument& edited) { return Ops::RemoveKeywordEntries(edited, id, entries) > 0; });
		};
		_keywordEntries.Build(this, context, parent, std::move(desc));
		Vector<GraphBlackboardItem> rows{GetMemoryResource(MemTag::Editor)};
		for (const GraphKeywordEntry& entry : entries) {
			GraphBlackboardItem item;
			item.id = entry.id;
			item.name = entry.name;
			item.onBuildControls = [suffix = entry.reference](UI::Context& context, UI::ElementHandle row) {
				AddText(context, row, ColorToken::WidgetTextDim, MetricToken::FontRow, suffix);
			};
			rows.push_back(std::move(item));
		}
		_keywordEntries.SetItems(rows);
	}

	void NodeShaderWindow::BuildCustomFunctionSettings(UI::Context& context, UI::ElementHandle parent, u32 id) {
		GraphDocument* document = GetDocument();
		const GraphNode* node = document ? GraphDocumentOps::FindNode(*document, id) : nullptr;
		if (!node) {
			return;
		}
		Vector<GraphCustomFunctionPort> inputs{GetMemoryResource(MemTag::Editor)};
		Vector<GraphCustomFunctionPort> outputs{GetMemoryResource(MemTag::Editor)};
		str unread;
		GraphCustomFunctions::ReadPorts(*node, GRAPH_CUSTOM_FUNCTION_INPUTS, inputs, unread);
		GraphCustomFunctions::ReadPorts(*node, GRAPH_CUSTOM_FUNCTION_OUTPUTS, outputs, unread);

		auto list = [&](GraphBlackboard& board, bool output, const Vector<GraphCustomFunctionPort>& ports, std::string_view title) {
			GraphBlackboardDesc desc;
			desc.title = str(title);
			for (std::string_view type : GRAPH_CUSTOM_FUNCTION_PORT_TYPES) {
				desc.kinds.push_back(str(type));
			}
			desc.onAdd = [this, id, output](u32 kind) -> u64 {
				u32 made = 0;
				Edit("Add Port", [&](GraphDocument& edited) {
					made = kind < std::size(GRAPH_CUSTOM_FUNCTION_PORT_TYPES)
							   ? Ops::AddCustomFunctionPort(edited, id, output, GRAPH_CUSTOM_FUNCTION_PORT_TYPES[kind])
							   : 0;
					return made != 0;
				});
				return made;
			};
			desc.onRename = [this, id](u64 port, std::string_view name) -> u64 {
				return Edit("Rename Port", [&](GraphDocument& edited) { return Ops::RenameCustomFunctionPort(edited, id, static_cast<u32>(port), name); })
						   ? port
						   : 0;
			};
			desc.onMove = [this, id](u64 port, u32 index) -> u64 {
				return Edit("Move Port", [&](GraphDocument& edited) { return Ops::MoveCustomFunctionPort(edited, id, static_cast<u32>(port), index); })
						   ? port
						   : 0;
			};
			desc.onDelete = [this, id](std::span<const u64> ports) {
				Vector<u32> doomed{GetMemoryResource(MemTag::Editor)};
				for (u64 port : ports) {
					doomed.push_back(static_cast<u32>(port));
				}
				Edit("Delete Port", [&](GraphDocument& edited) { return Ops::RemoveCustomFunctionPorts(edited, id, doomed) > 0; });
			};
			board.Build(this, context, parent, std::move(desc));
			Vector<GraphBlackboardItem> items{GetMemoryResource(MemTag::Editor)};
			for (const GraphCustomFunctionPort& port : ports) {
				GraphBlackboardItem item;
				item.id = port.id;
				item.name = port.name;
				item.onBuildControls = [this, id, port = port.id, type = port.type](UI::Context& context, UI::ElementHandle row) {
					UI::Widgets::DropdownDesc desc;
					desc.items = GRAPH_CUSTOM_FUNCTION_PORT_TYPES;
					desc.selected = type;
					desc.font = UISystem::GetDefaultFont();
					desc.fontSizePx = FontRow();
					desc.onSelect = [this, id, port](u32 index) {
						if (index < std::size(GRAPH_CUSTOM_FUNCTION_PORT_TYPES)) {
							Edit("Port Type", [&](GraphDocument& edited) {
								return Ops::SetCustomFunctionPortType(edited, id, port, GRAPH_CUSTOM_FUNCTION_PORT_TYPES[index]);
							});
						}
					};
					UI::Widgets::Dropdown(context, row, desc);
				};
				items.push_back(std::move(item));
			}
			board.SetItems(items);
		};
		list(_functionInputs, false, inputs, "Inputs");
		list(_functionOutputs, true, outputs, "Outputs");

		auto setting = [this, id](std::string_view key, str json) {
			Edit("Node Setting", [&](GraphDocument& edited) { return Ops::SetNodeSetting(edited, id, key, json); });
		};
		auto stored = [node](std::string_view key) {
			const GraphSetting* held = FindSetting(node->settings, key);
			return held ? ReadString(held->value) : str();
		};

		const str type = stored(GRAPH_CUSTOM_FUNCTION_TYPE);
		const bool file = type != GRAPH_CUSTOM_FUNCTION_SOURCE_TYPES[1];
		UI::Widgets::DropdownDesc types;
		types.items = GRAPH_CUSTOM_FUNCTION_SOURCE_TYPES;
		types.selected = file ? 0u : 1u;
		types.font = UISystem::GetDefaultFont();
		types.fontSizePx = FontRow();
		types.onSelect = [setting](u32 index) {
			if (index < std::size(GRAPH_CUSTOM_FUNCTION_SOURCE_TYPES)) {
				setting(GRAPH_CUSTOM_FUNCTION_TYPE, Ops::Quote(GRAPH_CUSTOM_FUNCTION_SOURCE_TYPES[index]));
			}
		};
		UI::Widgets::Dropdown(context, AddField(context, parent, "Type"), types);

		const str name = stored(GRAPH_CUSTOM_FUNCTION_NAME);
		UI::Widgets::TextFieldDesc named;
		named.text = name;
		named.font = UISystem::GetDefaultFont();
		named.fontSizePx = FontRow();
		named.onCommit = [setting](std::string_view text) { setting(GRAPH_CUSTOM_FUNCTION_NAME, Ops::Quote(text)); };
		UI::Widgets::TextField(context, AddField(context, parent, "Name"), named);

		if (file) {
			const Guid source = Guid::FromString(stored(GRAPH_CUSTOM_FUNCTION_SOURCE));
			const Path path = source.IsValid() ? AssetDatabase::LookupPath(source) : Path();
			const str caption = path.ToString().empty() ? str("None") : std::filesystem::path(path.ToString()).filename().generic_string();
			ObjectField::Desc field;
			field.caption = caption;
			field.accepts = [](const UI::DragPayload& payload) {
				Guid dropped;
				return FunctionFileOf(payload, dropped);
			};
			field.onDrop = [setting](const UI::DragPayload& payload, Vec2) {
				Guid dropped;
				if (FunctionFileOf(payload, dropped)) {
					setting(GRAPH_CUSTOM_FUNCTION_SOURCE, Ops::Quote(dropped.ToString()));
				}
			};
			field.onPick = [this, setting]() {
				UI::Context* c = GetPanelContext();
				if (!c) {
					return;
				}
				Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::UI)};
				Vector<AssetBrowserOps::AssignableAsset> shaders{GetMemoryResource(MemTag::Editor)};
				AssetBrowserOps::CollectAssignableAssets(ShaderAsset::GetReflectType()->name, shaders);
				for (const AssetBrowserOps::AssignableAsset& candidate : shaders) {
					const str extension =
						StringUtil::ToLower(std::filesystem::path(AssetDatabase::LookupPath(candidate.guid).ToString()).extension().generic_string());
					if (std::find(std::begin(GRAPH_CUSTOM_FUNCTION_EXTENSIONS), std::end(GRAPH_CUSTOM_FUNCTION_EXTENSIONS), extension) !=
						std::end(GRAPH_CUSTOM_FUNCTION_EXTENSIONS)) {
						items.push_back({candidate.display, "", true, false,
										 [setting, guid = candidate.guid]() { setting(GRAPH_CUSTOM_FUNCTION_SOURCE, Ops::Quote(guid.ToString())); }});
					}
				}
				if (!items.empty()) {
					UI::Widgets::OpenMenu(*c, c->GetPointerPosition(), items, UISystem::GetDefaultFont(), FontRow());
				}
			};
			ObjectField::Build(context, AddField(context, parent, "Source"), field);
		} else {
			const str body = stored(GRAPH_CUSTOM_FUNCTION_BODY);
			UI::Widgets::TextFieldDesc text;
			text.text = body;
			text.font = UISystem::GetDefaultFont();
			text.fontSizePx = FontRow();
			text.multiline = true;
			text.onCommit = [setting](std::string_view lines) { setting(GRAPH_CUSTOM_FUNCTION_BODY, Ops::Quote(lines)); };
			UI::Widgets::TextField(context, AddField(context, parent, "Body"), text);
		}
	}

	void NodeShaderWindow::RebuildBlackboard() {
		_needsBlackboardRebuild = false;
		const GraphDocument* document = GetDocument();
		Vector<GraphBlackboardItem> items{GetMemoryResource(MemTag::Editor)};
		if (document) {
			for (const GraphProperty& property : document->properties) {
				GraphBlackboardItem item;
				item.id = property.id;
				item.name = property.name;
				item.onBuildControls = [type = str(Ops::PropertyTypeLabel(property.type))](UI::Context& context, UI::ElementHandle row) {
					AddText(context, row, ColorToken::WidgetTextDim, MetricToken::FontSmall, type);
				};
				items.push_back(std::move(item));
			}
		}
		_blackboard.SetItems(items);
	}

	void NodeShaderWindow::RefreshStatus(UI::Context& context) {
		if (!context.IsAlive(_status)) {
			return;
		}
		str text;
		ColorToken color = ColorToken::WidgetTextDim;
		const u32 errors = static_cast<u32>(_previews.GetDiagnostics().size());
		if (_previews.IsCompiling()) {
			const f64 seconds = std::chrono::duration<f64>(std::chrono::steady_clock::now() - _compileStarted).count();
			text = std::format("Compiling... {:.1f} s", seconds);
		} else if (errors > 0) {
			text = errors == 1 ? str("1 error") : std::format("{} errors", errors);
			color = ColorToken::StatusError;
		} else if (_previews.SurfaceFailed()) {
			text = "The generated shader didn't compile. See the Console";
			color = ColorToken::StatusError;
		} else if (HasUnsavedChanges()) {
			text = "Unsaved";
		}
		context.SetText(_status, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontSmall), EditorTheme::GetColor(color), text);
	}

	Vector<u32> NodeShaderWindow::SelectedDocumentIds() const {
		Vector<u32> ids{GetMemoryResource(MemTag::Editor)};
		for (const GraphItem& item : _canvas.GetSelection()) {
			if (item.kind != GraphItemKind::Wire) {
				ids.push_back(static_cast<u32>(item.id));
			}
		}
		return ids;
	}

	void NodeShaderWindow::CopySelection(bool cut) {
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

	Vec2 NodeShaderWindow::PastePoint() const {
		if (UI::Context* context = GetPanelContext(); context && context->IsAlive(_canvas.GetElement())) {
			const UI::Rect rect = context->GetLayout(_canvas.GetElement()).content;
			const Vec2 pointer = context->GetPointerPosition();
			if (rect.Contains(pointer)) {
				return GraphCanvasOps::LocalToGraph(_canvas.GetView(), _canvas.PointerLocal(*context));
			}
		}
		return _lastPasteAt + Vec2(PASTE_STEP, PASTE_STEP) * static_cast<f32>(_pasteCount + 1);
	}

	void NodeShaderWindow::PasteAt(Vec2 graph) {
		const str clip = WindowSystem::GetClipboardText();
		if (clip.empty() || !GetDocument()) {
			return;
		}
		GraphDocument fragment;
		str error;
		if (!GraphDocumentOps::Read(clip, fragment, error)) {
			return;
		}
		const Vec2 corner = GraphWindowParts::FragmentCorner(clip, graph);
		Vector<u32> pasted{GetMemoryResource(MemTag::Editor)};
		Edit("Paste", [&](GraphDocument& document) { return Ops::Paste(document, clip, graph - corner, pasted, error); });
		_lastPasteAt = graph;
		++_pasteCount;
		Vector<GraphItem> selection{GetMemoryResource(MemTag::Editor)};
		if (const GraphDocument* document = GetDocument()) {
			GraphWindowParts::PastedItems(*document, pasted, selection);
		}
		SelectItems(selection);
	}

	void NodeShaderWindow::DuplicateSelection() {
		const GraphDocument* document = GetDocument();
		const Vector<u32> ids = SelectedDocumentIds();
		if (!document || ids.empty()) {
			return;
		}
		const str clip = Ops::Copy(*document, ids);
		if (clip.empty()) {
			return;
		}
		Vector<u32> pasted{GetMemoryResource(MemTag::Editor)};
		str error;
		Edit("Duplicate", [&](GraphDocument& edited) { return Ops::Paste(edited, clip, Vec2(PASTE_STEP, PASTE_STEP), pasted, error); });
		Vector<GraphItem> selection{GetMemoryResource(MemTag::Editor)};
		for (u32 id : pasted) {
			selection.push_back({GraphItemKind::Node, id});
		}
		SelectItems(selection);
	}

	void NodeShaderWindow::DeleteSelection() {
		Vector<u32> ids = SelectedDocumentIds();
		Vector<GraphPortRef> wires{GetMemoryResource(MemTag::Editor)};
		for (const GraphItem& item : _canvas.GetSelection()) {
			if (item.kind == GraphItemKind::Wire) {
				wires.push_back({item.id >> 32, GraphPortSide::Input, static_cast<u32>(item.id & 0xffffffffu)});
			}
		}
		Edit("Delete", [&](GraphDocument& document) {
			bool changed = false;
			for (const GraphPortRef& wire : wires) {
				u32 node = 0;
				str port;
				if (_model->InputOf(wire, node, port)) {
					changed = GraphDocumentOps::Disconnect(document, node, port) || changed;
				}
			}
			return Ops::Delete(document, ids) > 0 || changed;
		});
		_canvas.SetSelection({});
		_inspected = 0;
		_needsInspectorRebuild = true;
	}

	void NodeShaderWindow::SelectItems(std::span<const GraphItem> items) {
		_canvas.SetSelection(items);
		_model->OnSelectionChanged();
	}

	void NodeShaderWindow::RebuildNodes(UI::Context& context) {
		_needsNodeRebuild = false;
		_canvas.RebuildNodes(context);
		if (_inspectedProperty == 0) {
			_model->OnSelectionChanged();
		}
	}

	bool NodeShaderWindow::OnCommand(PanelCommand command) {
		if (_blackboard.HasFocus() || _functionInputs.HasFocus() || _functionOutputs.HasFocus() || _keywordEntries.HasFocus()) {
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

	void NodeShaderWindow::OnTick(f32 deltaSeconds) {
		if (_documentOpen && NodeShaderDocuments::GetRevision(_open) != _seenRevision) {
			OnDocumentChanged();
		}
		if (const u64 serial = NodeShaderDocuments::GetSaveSerial(); serial != _seenSaveSerial) {
			_seenSaveSerial = serial;
			_definitions.Clear();
			_previews.Invalidate();
			_needsNodeRebuild = true;
			_needsInspectorRebuild = true;
		}
		if (const GraphDocument* document = GetDocument()) {
			_previews.Tick(*document);
		}
		const bool compiling = _previews.IsCompiling();
		if (compiling && !_wasCompiling) {
			_compileStarted = std::chrono::steady_clock::now();
		}
		if (_wasCompiling && !compiling) {
			_needsNodeRebuild = true;
		}
		_wasCompiling = compiling;

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
		_functionInputs.Tick(deltaSeconds);
		_functionOutputs.Tick(deltaSeconds);
		_keywordEntries.Tick(deltaSeconds);
		if (_needsInspectorRebuild) {
			RebuildInspector(*context);
		}
		if (_needsFrame && _canvas.FrameAll(*context)) {
			_needsFrame = false;
		}
		RefreshStatus(*context);
		if (context->IsAlive(_mainPane)) {
			const UI::Rect pane = context->GetLayout(_mainPane).content;
			_previews.SetMainSize(Vec2I(static_cast<i32>(pane.w), static_cast<i32>(pane.h)));
		}
	}

	void NodeShaderWindow::OnSubmitView(f32 deltaSeconds) {
		(void)deltaSeconds;
		const f64 now = std::chrono::duration<f64>(std::chrono::steady_clock::now() - _started).count();
		const bool focused = PanelManager::GetFocusedPanel().Get() == this;
		const bool animate = focused || now - _lastTimedSubmit >= UNFOCUSED_PREVIEW_INTERVAL;
		if (animate) {
			_lastTimedSubmit = now;
		}
		_previews.SubmitViews(now, animate, EditorTheme::GetColor(ColorToken::GraphPreviewWell));
	}

	namespace NodeShaderWindowRegistry {

		NodeShaderWindow* Open(const Guid& asset) {
			Vector<Handle<EditorPanel>> panels{GetMemoryResource(MemTag::Editor)};
			PanelManager::GetOpenPanels(panels);
			for (const Handle<EditorPanel>& panel : panels) {
				EditorPanel* live = panel.Get();
				if (live && live->GetType() == NodeShaderWindow::GetReflectType()) {
					auto* window = static_cast<NodeShaderWindow*>(live);
					if (window->GetGraph() == asset) {
						PanelManager::FocusPanel(panel);
						return window;
					}
				}
			}
			Handle<EditorPanel> opened = PanelManager::OpenPanel(NodeShaderWindow::GetReflectType());
			auto* window = static_cast<NodeShaderWindow*>(opened.Get());
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
			AssetOpeners::Register(".slshader", [](const Guid& asset) { Open(asset); }, owner);
			AssetOpeners::Register(GRAPH_SUBGRAPH_EXTENSION, [](const Guid& asset) { Open(asset); }, owner);
		}

	} // namespace NodeShaderWindowRegistry

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::NodeShaderWindow)
