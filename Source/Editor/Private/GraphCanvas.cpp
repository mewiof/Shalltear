#include "Shalltear/Editor/GraphCanvas.h"

#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/CurveEditor.h"

#include "EditorConfig.h"
#include "GraphSearcher.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <format>

namespace Shalltear {

	using EditorTheme::ColorToken;
	using EditorTheme::MetricToken;

	namespace {

		constexpr f32 WIRE_WIDTH = 2.0f;
		constexpr f32 ARROW_LENGTH = 11.0f;
		constexpr f32 WIRE_FAN = 7.0f;
		constexpr f32 WIRE_HOVER_SCALE = 1.5f;
		constexpr i32 GRID_MAJOR_EVERY = 5;
		constexpr f32 FRAME_MARGIN = 48.0f;
		constexpr f32 HEADER_INSET = 8.0f;
		constexpr f32 LABEL_GAP = 4.0f;
		constexpr f32 DOT_BORDER = 1.5f;
		constexpr f32 SPACE_BUTTON = 12.0f;
		constexpr f32 FLOW_WIDTH_SCALE = 2.0f;
		constexpr f32 CHECK_SIZE = 10.0f;
		constexpr f32 DASH_LENGTH = 6.0f;
		constexpr f32 DASH_GAP = 4.0f;

		f32 InlineWidth(const GraphInlineValue& value) {
			switch (value.kind) {
			case GraphInlineKind::Numbers:
				return GraphCanvasOps::INLINE_COMPONENT_WIDTH * static_cast<f32>(Math::Max<u32>(value.count, 1u));
			case GraphInlineKind::Color:
				return GraphCanvasOps::INLINE_COMPONENT_WIDTH;
			case GraphInlineKind::Toggle:
				return GraphCanvasOps::INLINE_TOGGLE_WIDTH;
			case GraphInlineKind::Label:
				return GraphCanvasOps::INLINE_LABEL_WIDTH;
			case GraphInlineKind::Curve:
			case GraphInlineKind::Gradient:
				return GraphCanvasOps::INLINE_CURVE_WIDTH;
			case GraphInlineKind::None:
				break;
			}
			return 0.0f;
		}

		void PaintCurveField(UI::Context& context, UI::ElementHandle part, UI::PaintScope& paint, const AnimationCurve& curve) {
			const UI::Rect box = context.GetLayout(part).borderBox;
			if (!(box.w > 2.0f) || !(box.h > 2.0f)) {
				return;
			}
			const CurveEditor::View view = CurveEditor::Frame(curve);
			const f32 scale = context.GetContentScale() > 0.0f ? context.GetContentScale() : 1.0f;
			const auto y = [&](f32 value) { return box.y + (1.0f - (value - view.valueMin) / (view.valueMax - view.valueMin)) * box.h; };
			const auto timeAt = [&](f32 px) { return view.timeMin + (px - box.x) / box.w * (view.timeMax - view.timeMin); };
			const Color ink = EditorTheme::GetColor(ColorToken::CurveLine);
			const f32 stride = Math::Max(1.0f, scale);
			Vec2 previous(box.x, y(curve.Evaluate(timeAt(box.x))));
			for (f32 px = box.x + stride; px <= box.x + box.w; px += stride) {
				const Vec2 next(px, y(curve.Evaluate(timeAt(px))));
				paint.EmitLine(previous, next, scale, ink, UI::LineCap::Round);
				previous = next;
			}
		}

		void PaintGradientField(UI::Context& context, UI::ElementHandle part, UI::PaintScope& paint, std::span<const Color> ramp) {
			const UI::Rect box = context.GetLayout(part).borderBox;
			if (ramp.empty() || !(box.w > 0.0f)) {
				return;
			}
			const f32 band = box.w / static_cast<f32>(ramp.size());
			for (size_t i = 0; i < ramp.size(); ++i) {
				Color opaque = ramp[i];
				opaque.a = 1.0f;
				paint.EmitQuad({box.x + band * static_cast<f32>(i), box.y, band + 0.5f, box.h}, opaque);
			}
		}

		str InlineText(f32 value) {
			return std::format("{:.4g}", value);
		}

		bool IsFlow(GraphPortSide side) {
			return side == GraphPortSide::FlowInput || side == GraphPortSide::FlowOutput;
		}

		f32 GridStep() {
			using Graph = EditorConfig::GraphPreferences;
			return static_cast<f32>(std::clamp(EditorConfig::ReadGraphPreferences().gridSize, Graph::GRAPH_GRID_MIN, Graph::GRAPH_GRID_MAX));
		}

		GraphPortSide OppositeSide(GraphPortSide side) {
			switch (side) {
			case GraphPortSide::Input:
				return GraphPortSide::Output;
			case GraphPortSide::Output:
				return GraphPortSide::Input;
			case GraphPortSide::FlowInput:
				return GraphPortSide::FlowOutput;
			case GraphPortSide::FlowOutput:
				break;
			}
			return GraphPortSide::FlowInput;
		}

	} // anonymous namespace

	UI::Context* GraphCanvas::Live() const {
		if (!_context) {
			return nullptr;
		}
		UI::Context* context = !UISystem::IsInitialized() || UISystem::IsContextLive(_context) ? _context : nullptr;
		return context && context->IsAlive(_canvas) ? context : nullptr;
	}

	Vec2 GraphCanvas::ToLocal(UI::Context& context, Vec2 point) const {
		const UI::Rect content = context.GetLayout(_canvas).content;
		const f32 scale = context.GetContentScale();
		return Vec2((point.x - content.x) / scale, (point.y - content.y) / scale);
	}

	Vec2 GraphCanvas::PointerLocal(UI::Context& context) const {
		return ToLocal(context, context.GetPointerPosition());
	}

	const GraphCanvas::PlacedNode* GraphCanvas::FindNode(GraphNodeId id) const {
		auto found = _index.find(id);
		return found == _index.end() ? nullptr : &_nodes[found->second];
	}

	bool GraphCanvas::NodeBox(const PlacedNode& node, UI::Rect& out) const {
		Vec2 graph{};
		if (!_model || !_model->GetNodePosition(node.id, graph)) {
			return false;
		}
		out = GraphCanvasOps::NodeRect(_view, graph, node.size);
		return true;
	}

	void GraphCanvas::ResetView() {
		_view = GraphView{};
	}

	GraphCanvas::~GraphCanvas() {
		CloseSearcher();
	}

	void GraphCanvas::Build(UI::Context& context, UI::ElementHandle parent, GraphViewModel* model, GraphCanvasDesc desc) {
		EndMove();
		CloseSearcher();
		_context = &context;
		_model = model;
		_desc = desc;
		_nodes.clear();
		_index.clear();
		_blockIndex.clear();
		_blockDrag.clear();
		_dropStack = 0;
		_styledZoom = 0.0f;
		_press = Press::None;
		_portDragging = false;

		_canvas = context.CreateElement();
		context.AppendChild(parent, _canvas);
		UI::Style style;
		style.flexGrow = 1.0f;
		style.height = UI::Length::Percent(1.0f);
		style.minWidth = UI::Length::Pixel(0.0f);
		style.overflow = UI::Overflow::Hidden;
		context.SetStyle(_canvas, style);
		context.AddClass(_canvas, EditorTheme::Class::Graph);
		context.SetFocusable(_canvas, true);

		auto makeHost = [&context, this]() {
			UI::ElementHandle host = context.CreateElement();
			context.AppendChild(_canvas, host);
			UI::Style hostStyle;
			hostStyle.position = UI::Position::Absolute;
			hostStyle.left = UI::Length::Pixel(0.0f);
			hostStyle.top = UI::Length::Pixel(0.0f);
			hostStyle.width = UI::Length::Pixel(0.0f);
			hostStyle.height = UI::Length::Pixel(0.0f);
			context.SetStyle(host, hostStyle);
			return host;
		};
		_groupHost = makeHost();
		_noteHost = makeHost();
		_nodeHost = makeHost();
		_inlineHost = makeHost();
		_groups.clear();
		_notes.clear();
		_editor = {};

		_overlay = context.CreateElement();
		context.AppendChild(_canvas, _overlay);
		UI::Style overlayStyle;
		overlayStyle.position = UI::Position::Absolute;
		overlayStyle.left = UI::Length::Pixel(0.0f);
		overlayStyle.top = UI::Length::Pixel(0.0f);
		overlayStyle.width = UI::Length::Percent(1.0f);
		overlayStyle.height = UI::Length::Percent(1.0f);
		overlayStyle.pointerEvents = false;
		context.SetStyle(_overlay, overlayStyle);
		context.Events(_overlay).customDraw = [this](UI::Context& c, UI::ElementHandle element, UI::PaintScope& paint) {
			PaintOverlay(c, element, paint);
		};

		UI::ElementEvents& events = context.Events(_canvas);
		events.customDraw = [this](UI::Context& c, UI::ElementHandle element, UI::PaintScope& paint) {
			Paint(c, element, paint);
		};

		events.onMiddleDrag = [this](Vec2 delta) {
			UI::Context* c = Live();
			if (!c) {
				return;
			}
			_view.pan = _view.pan + delta / c->GetContentScale();
			PlaceNodes(*c);
		};
		events.onMouseDown = [this]() {
			if (UI::Context* c = Live()) {
				PressGrid(*c);
			}
		};
		events.onPressDrag = [this](Vec2 point) {
			if (UI::Context* c = Live()) {
				Travel(*c, point);
			}
		};
		events.onPressEnd = [this]() { Release(); };

		events.onWheel = [this](Vec2 notches) {
			UI::Context* c = Live();
			if (!c || notches.y == 0.0f) {
				return false;
			}
			GraphCanvasOps::ZoomAtPoint(_view, PointerLocal(*c), notches.y, _desc.minZoom, _desc.maxZoom);
			PlaceNodes(*c);
			return true;
		};

		events.onClick = [this]() {
			GraphNodeId pending = 0;
			if (_model && _model->GetPendingWire(pending)) {
				_model->CancelPendingWire();
			}
		};

		events.onRightClick = [this]() {
			UI::Context* c = Live();
			if (!c || !_model) {
				return;
			}
			const Vec2 local = PointerLocal(*c);
			const Vec2 graph = GraphCanvasOps::LocalToGraph(_view, local);
			GraphWireId wire = 0;
			if (PickWire(local, c->GetContentScale(), wire)) {
				const GraphItem item{GraphItemKind::Wire, wire};
				if (!IsSelected(item) && SetSelectionTo(std::span<const GraphItem>(&item, 1))) {
					Notify();
				}
				_model->OnWireContextMenu(*c, wire, graph);
				return;
			}
			_model->OnCanvasContextMenu(*c, graph);
		};
		events.onDoubleClick = [this]() {
			UI::Context* c = Live();
			if (!c || !_model) {
				return;
			}
			const Vec2 local = PointerLocal(*c);
			GraphWireId wire = 0;
			if (PickWire(local, c->GetContentScale(), wire)) {
				_model->OnWireDoubleClick(wire, GraphCanvasOps::LocalToGraph(_view, local));
			}
		};

		events.onDragOver = [this](const UI::DragPayload& payload) { return _model && _model->AcceptsDrop(payload); };
		events.onDrop = [this](const UI::DragPayload& payload, Vec2 point) {
			UI::Context* c = Live();
			if (c && _model) {
				_model->OnDrop(payload, GraphCanvasOps::LocalToGraph(_view, ToLocal(*c, point)));
			}
		};

		events.onKeyDown = [this](Key key, KeyMod mods) {
			UI::Context* c = Live();
			return c && OnKey(*c, key, mods);
		};
	}

	void GraphCanvas::RebuildNodes(UI::Context& context) {
		if (!context.IsAlive(_nodeHost)) {
			return;
		}
		EndMove();
		_press = Press::None;
		CloseEditor();
		context.ClearChildren(_nodeHost);
		context.ClearChildren(_noteHost);
		context.ClearChildren(_groupHost);
		if (context.IsAlive(_inlineHost)) {
			context.ClearChildren(_inlineHost);
		}
		_nodes.clear();
		_index.clear();
		_blockIndex.clear();
		_groups.clear();
		_titledOutlines.clear();
		_notes.clear();
		_portDragging = false;
		_blockDrag.clear();
		_dropStack = 0;
		_styledZoom = 0.0f;
		if (!_model) {
			return;
		}
		Vector<GraphNodeDesc> descs{GetMemoryResource(MemTag::Editor)};
		_model->GetNodes(descs);
		_nodes.reserve(descs.size());
		for (const GraphNodeDesc& desc : descs) {
			BuildNode(context, desc);
		}
		Vector<GraphGroupDesc> groups{GetMemoryResource(MemTag::Editor)};
		_model->GetGroups(groups);
		for (const GraphGroupDesc& desc : groups) {
			BuildGroup(context, desc);
		}
		BuildOutlineTitles(context);
		Vector<GraphNoteDesc> notes{GetMemoryResource(MemTag::Editor)};
		_model->GetNotes(notes);
		for (const GraphNoteDesc& desc : notes) {
			BuildNote(context, desc);
		}

		_wires.clear();
		_model->GetWires(_wires);
		Vector<GraphItem> kept{GetMemoryResource(MemTag::Editor)};
		for (const GraphItem& item : _selection) {
			bool drawn = false;
			switch (item.kind) {
			case GraphItemKind::Node:
				drawn = _index.contains(item.id);
				break;
			case GraphItemKind::Wire:
				drawn = std::any_of(_wires.begin(), _wires.end(), [&](const GraphWireDesc& wire) { return wire.id == item.id; });
				break;
			case GraphItemKind::Group:
				drawn = FindGroup(item.id) != nullptr;
				break;
			case GraphItemKind::Note:
				drawn = FindNote(item.id) != nullptr;
				break;
			case GraphItemKind::Block:
				drawn = _blockIndex.contains(item.id);
				break;
			}
			if (drawn) {
				kept.push_back(item);
			}
		}
		SetSelectionTo(kept);
		PlaceNodes(context);
	}

	GraphCanvas::PlacedPort GraphCanvas::BuildPort(UI::Context& context, UI::ElementHandle parent, UI::ElementHandle layer, const GraphPortDesc& desc,
												   const GraphPortRef& ref, ColorToken fill, bool labelled) {
		PlacedPort built;
		built.color = desc.color;
		built.connected = desc.connected;
		built.dim = desc.dim;
		built.text = desc.label;
		built.dot = context.CreateElement();
		context.AppendChild(parent, built.dot);
		UI::Style dot;
		dot.position = UI::Position::Absolute;
		dot.background = UI::Brush::SolidColor(EditorTheme::GetColor(desc.connected ? desc.color : fill));
		dot.borderColor = EditorTheme::GetColor(desc.color);
		context.SetStyle(built.dot, dot);

		if (labelled) {
			built.label = context.CreateElement();
			context.AppendChild(parent, built.label);
			UI::Style label;
			label.position = UI::Position::Absolute;
			label.pointerEvents = false;
			label.textAlign = ref.side == GraphPortSide::Input ? UI::Align::Start : UI::Align::End;
			label.textAlignVertical = UI::Align::Center;
			label.textOverflow = UI::TextOverflow::Ellipsis;
			context.SetStyle(built.label, label);
			context.SetText(built.label, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontTiny),
							EditorTheme::GetColor(desc.dim ? ColorToken::TextFaint : ColorToken::WidgetTextDim), desc.label);
		}

		if (labelled && ref.side == GraphPortSide::Input && !desc.space.empty()) {
			built.spaceText = desc.space;
			built.space = context.CreateElement();
			context.AppendChild(parent, built.space);
			UI::Style button;
			button.position = UI::Position::Absolute;
			button.textAlign = UI::Align::Center;
			button.textAlignVertical = UI::Align::Center;
			button.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::GraphInlineField));
			button.borderColor = EditorTheme::GetColor(ColorToken::GraphInlineFieldEdge);
			context.SetStyle(built.space, button);
			context.AddClass(built.space, EditorTheme::Class::Clickable);
			context.AddClass(built.space, EditorTheme::Class::GraphSpaceButton);
			context.Events(built.space).onClick = [this, ref]() {
				if (_model) {
					_model->OnPortSpaceClick(ref);
				}
			};
		}

		UI::ElementEvents& events = context.Events(built.dot);
		events.onMouseDown = [this, ref]() {
			_portDragging = true;
			_portDrag = ref;
		};
		events.onPressDrag = [](Vec2) {};
		events.onPressEnd = [this]() { EndPortDrag(); };
		if (ref.side == GraphPortSide::Input && !desc.connected && desc.inlineValue.kind != GraphInlineKind::None && labelled) {
			built.inlineValue = desc.inlineValue;
			BuildInlineField(context, layer, built, ref);
		}
		return built;
	}

	void GraphCanvas::BuildNode(UI::Context& context, const GraphNodeDesc& desc) {
		UI::ElementHandle layer = context.CreateElement();
		context.AppendChild(_nodeHost, layer);
		UI::Style layerStyle;
		layerStyle.position = UI::Position::Absolute;
		layerStyle.left = UI::Length::Pixel(0.0f);
		layerStyle.top = UI::Length::Pixel(0.0f);
		layerStyle.width = UI::Length::Pixel(0.0f);
		layerStyle.height = UI::Length::Pixel(0.0f);
		context.SetStyle(layer, layerStyle);
		UI::ElementHandle node = context.CreateElement();
		context.AppendChild(layer, node);

		PlacedNode placed;
		placed.id = desc.id;
		placed.layer = layer;
		placed.element = node;
		placed.radius = desc.radius;
		placed.label = desc.label;
		placed.inputs = static_cast<u32>(desc.inputs.size());
		const u32 rows = static_cast<u32>(Math::Max(desc.inputs.size(), desc.outputs.size()));
		const bool flowIn = !desc.flowInputs.empty();
		const bool flowOut = !desc.flowOutputs.empty();
		const bool plain = desc.settings.empty() && !desc.stack && !flowIn && !flowOut;
		const bool structured = rows > 0 || !plain;
		placed.compact = rows > 0 && plain && desc.compact;
		placed.rows = rows;
		placed.stack = desc.stack;
		placed.flowInputs = static_cast<u32>(desc.flowInputs.size());
		placed.preview = rows > 0 && plain && !placed.compact ? desc.preview : GraphNodePreview::None;
		for (const GraphBlockDesc& block : desc.blocks) {
			placed.blockHeights.push_back(GraphCanvasOps::BlockHeight(static_cast<u32>(block.settings.size()),
																	  static_cast<u32>(Math::Max(block.inputs.size(), block.outputs.size()))));
		}
		placed.layout = GraphCanvasOps::NodeLayout(flowIn, static_cast<u32>(desc.settings.size()), rows,
												   desc.stack ? GraphCanvasOps::StackHeight(placed.blockHeights) : 0.0f, flowOut);
		const f32 structuredHeight = placed.preview == GraphNodePreview::None
										 ? placed.layout.height
										 : GraphCanvasOps::PreviewNodeHeight(rows, desc.size.x, placed.preview == GraphNodePreview::Expanded);
		placed.size = placed.compact ? Vec2(desc.size.x, GraphCanvasOps::PORT_ROW_HEIGHT)
					  : structured	 ? Vec2(desc.size.x, structuredHeight)
									 : desc.size;

		UI::Style style;
		style.position = UI::Position::Absolute;
		style.background = UI::Brush::SolidColor(EditorTheme::GetColor(desc.fill));
		style.borderWidth = {1.0f, 1.0f, 1.0f, 1.0f};
		style.borderColor = EditorTheme::GetColor(ColorToken::CardEdge);
		if (!structured) {
			style.alignItems = UI::Align::Center;
			style.justifyContent = UI::Align::Center;
			style.padding = {6.0f, 0.0f, 6.0f, 0.0f};
			style.textAlign = UI::Align::Center;
			style.textAlignVertical = UI::Align::Center;
			style.textOverflow = UI::TextOverflow::Ellipsis;
		}
		context.SetStyle(node, style);

		if (structured && !placed.compact) {
			placed.header = context.CreateElement();
			context.AppendChild(node, placed.header);
			UI::Style header;
			header.position = UI::Position::Absolute;
			header.pointerEvents = false;
			header.textAlignVertical = UI::Align::Center;
			header.textOverflow = UI::TextOverflow::Ellipsis;
			context.SetStyle(placed.header, header);
		}
		for (u32 index = 0; index < desc.inputs.size(); ++index) {
			placed.ports.push_back(BuildPort(context, node, placed.layer, desc.inputs[index], GraphPortRef{desc.id, GraphPortSide::Input, index}, desc.fill,
											 !placed.compact));
		}
		for (u32 index = 0; index < desc.outputs.size(); ++index) {
			placed.ports.push_back(BuildPort(context, node, placed.layer, desc.outputs[index], GraphPortRef{desc.id, GraphPortSide::Output, index},
											 desc.fill, !placed.compact));
		}
		BuildSettings(context, node, desc.settings, desc.id, placed.settings);

		auto addFlow = [&](const GraphFlowPortDesc& flow, GraphPortSide side, u32 index) {
			PlacedFlow built;
			built.text = flow.label;
			built.connected = flow.connected;
			built.dot = context.CreateElement();
			context.AppendChild(node, built.dot);
			UI::Style dot;
			dot.position = UI::Position::Absolute;
			dot.background = UI::Brush::SolidColor(EditorTheme::GetColor(flow.connected ? ColorToken::GraphFlow : desc.fill));
			dot.borderColor = EditorTheme::GetColor(ColorToken::GraphFlow);
			context.SetStyle(built.dot, dot);
			context.AddClass(built.dot, EditorTheme::Class::GraphFlowPort);
			if (!flow.label.empty()) {
				built.label = context.CreateElement();
				context.AppendChild(node, built.label);
				UI::Style label;
				label.position = UI::Position::Absolute;
				label.pointerEvents = false;
				label.textAlignVertical = UI::Align::Center;
				label.textOverflow = UI::TextOverflow::Ellipsis;
				context.SetStyle(built.label, label);
			}
			const GraphPortRef ref{desc.id, side, index};
			UI::ElementEvents& events = context.Events(built.dot);
			events.onMouseDown = [this, ref]() {
				_portDragging = true;
				_portDrag = ref;
			};
			events.onPressDrag = [](Vec2) {};
			events.onPressEnd = [this]() { EndPortDrag(); };
			placed.flows.push_back(std::move(built));
		};
		for (u32 index = 0; index < desc.flowInputs.size(); ++index) {
			addFlow(desc.flowInputs[index], GraphPortSide::FlowInput, index);
		}
		for (u32 index = 0; index < desc.flowOutputs.size(); ++index) {
			addFlow(desc.flowOutputs[index], GraphPortSide::FlowOutput, index);
		}

		const u32 nodeIndex = static_cast<u32>(_nodes.size());
		for (u32 index = 0; index < desc.blocks.size(); ++index) {
			BuildBlock(context, placed, desc.blocks[index], index);
			_blockIndex[desc.blocks[index].id] = {nodeIndex, index};
		}

		const GraphNodeId id = desc.id;
		if (placed.preview != GraphNodePreview::None) {
			const bool expanded = placed.preview == GraphNodePreview::Expanded;
			if (expanded) {
				placed.well = context.CreateElement();
				context.AppendChild(node, placed.well);
				UI::Style well;
				well.position = UI::Position::Absolute;
				well.pointerEvents = false;
				well.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::GraphPreviewWell));
				context.SetStyle(placed.well, well);
				context.AddClass(placed.well, EditorTheme::Class::GraphPreviewWell);
				context.Events(placed.well).customDraw = [this, id](UI::Context& c, UI::ElementHandle element, UI::PaintScope& paint) {
					if (_model) {
						_model->PaintNodePreview(c, paint, id, c.GetLayout(element).content);
					}
				};
			}
			placed.strip = context.CreateElement();
			context.AppendChild(node, placed.strip);
			UI::Style strip;
			strip.position = UI::Position::Absolute;
			context.SetStyle(placed.strip, strip);
			context.AddClass(placed.strip, EditorTheme::Class::GraphPreviewStrip);
			context.Events(placed.strip).customDraw = [expanded](UI::Context& c, UI::ElementHandle element, UI::PaintScope& paint) {
				const UI::Rect box = c.GetLayout(element).content;
				const f32 half = Math::Min(box.h * 0.3f, box.w * 0.5f);
				const Vec2 centre(box.x + box.w * 0.5f, box.y + box.h * 0.5f);
				const f32 width = Math::Max(1.0f, c.GetContentScale());
				const Color color = EditorTheme::GetColor(ColorToken::WidgetTextDim);
				const f32 lean = expanded ? -half * 0.5f : half * 0.5f;
				paint.EmitLine(Vec2(centre.x - half, centre.y - lean), centre + Vec2(0.0f, lean), width, color);
				paint.EmitLine(centre + Vec2(0.0f, lean), Vec2(centre.x + half, centre.y - lean), width, color);
			};
			context.Events(placed.strip).onClick = [this, id, expanded]() {
				if (_model) {
					_model->SetNodePreviewExpanded(id, !expanded);
				}
			};
		}

		UI::ElementEvents& events = context.Events(node);
		events.customDraw = [this, id](UI::Context& c, UI::ElementHandle element, UI::PaintScope& paint) {
			if (_model) {
				_model->PaintNode(c, element, paint, id);
			}
		};
		events.onClick = [this, id]() {
			if (_model && !_pressMoved) {
				_model->OnNodeClick(id);
			}
		};
		events.onDoubleClick = [this, id]() {
			if (_model) {
				_model->OnNodeDoubleClick(id);
			}
		};
		events.onRightClick = [this, id]() {
			UI::Context* c = Live();
			if (!c || !_model) {
				return;
			}
			GraphNodeId pending = 0;
			const GraphItem item{GraphItemKind::Node, id};
			if (!_model->GetPendingWire(pending) && !IsSelected(item) && SetSelectionTo(std::span<const GraphItem>(&item, 1))) {
				Notify();
			}
			_model->OnNodeContextMenu(*c, id);
		};
		events.onMouseDown = [this, id]() {
			if (UI::Context* c = Live()) {
				PressItemForMove(*c, GraphItem{GraphItemKind::Node, id});
			}
		};
		events.onPressDrag = [this](Vec2 point) {
			if (UI::Context* c = Live()) {
				Travel(*c, point);
			}
		};
		events.onPressEnd = [this]() { Release(); };
		events.onDragOver = [this, id](const UI::DragPayload& payload) {
			return _model && (_model->AcceptsNodeDrop(id, payload) || _model->AcceptsDrop(payload));
		};
		events.onDrop = [this, id](const UI::DragPayload& payload, Vec2 point) {
			UI::Context* c = Live();
			if (!c || !_model) {
				return;
			}
			if (_model->AcceptsNodeDrop(id, payload)) {
				_model->OnNodeDrop(id, payload);
			} else {
				_model->OnDrop(payload, GraphCanvasOps::LocalToGraph(_view, ToLocal(*c, point)));
			}
		};

		_index[placed.id] = nodeIndex;
		_nodes.push_back(std::move(placed));
	}

	void GraphCanvas::BuildSettings(UI::Context& context, UI::ElementHandle parent, std::span<const GraphSettingDesc> settings,
									GraphNodeId owner, Vector<PlacedSetting>& out) {
		for (u32 index = 0; index < settings.size(); ++index) {
			const GraphSettingDesc& setting = settings[index];
			PlacedSetting built;
			built.labelText = setting.label;
			built.valueText = setting.value;
			built.edit = setting.edit;
			built.element = context.CreateElement();
			context.AppendChild(parent, built.element);
			UI::Style row;
			row.position = UI::Position::Absolute;
			row.flexDirection = UI::FlexDirection::Row;
			row.alignItems = UI::Align::Stretch;
			context.SetStyle(built.element, row);
			context.AddClass(built.element, EditorTheme::Class::Clickable);
			auto addText = [&](UI::ElementHandle& into, bool value) {
				into = context.CreateElement();
				context.AppendChild(built.element, into);
				UI::Style text;
				text.pointerEvents = false;
				text.textAlignVertical = UI::Align::Center;
				text.textOverflow = UI::TextOverflow::Ellipsis;
				if (value) {
					text.flexGrow = 1.0f;
					text.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::GraphInlineField));
					text.borderWidth = {1.0f, 1.0f, 1.0f, 1.0f};
					text.borderColor = EditorTheme::GetColor(ColorToken::GraphInlineFieldEdge);
				} else {
					text.width = UI::Length::Percent(0.45f);
					text.flexShrink = 0.0f;
				}
				context.SetStyle(into, text);
			};
			addText(built.label, false);
			addText(built.value, true);
			if (!setting.action.empty()) {
				built.actionText = setting.action;
				built.action = context.CreateElement();
				context.AppendChild(built.element, built.action);
				UI::Style button;
				button.flexShrink = 0.0f;
				button.textAlignVertical = UI::Align::Center;
				button.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::GraphInlineFieldEdge));
				context.SetStyle(built.action, button);
				context.AddClass(built.action, EditorTheme::Class::GraphSettingAction);
				context.AddClass(built.action, EditorTheme::Class::Clickable);
				context.Events(built.action).onClick = [this, owner, index]() {
					if (_model) {
						_model->OnSettingAction(owner, index);
					}
				};
			}
			context.Events(built.element).onClick = [this, owner, index, edit = setting.edit]() {
				UI::Context* c = Live();
				if (!c || !_model) {
					return;
				}
				if (edit == GraphSettingEdit::Menu) {
					_model->OnSettingClick(*c, owner, index);
				} else {
					BeginSettingEdit(*c, owner, index);
				}
			};
			out.push_back(std::move(built));
		}
	}

	void GraphCanvas::BuildBlock(UI::Context& context, PlacedNode& node, const GraphBlockDesc& desc, u32 index) {
		PlacedBlock placed;
		placed.id = desc.id;
		placed.label = desc.label;
		placed.enabled = desc.enabled;
		placed.height = node.blockHeights[index];
		placed.element = context.CreateElement();
		context.AppendChild(node.element, placed.element);
		UI::Style style;
		style.position = UI::Position::Absolute;
		style.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::GraphBlock));
		style.borderWidth = {1.0f, 1.0f, 1.0f, 1.0f};
		style.borderColor = EditorTheme::GetColor(ColorToken::CardEdge);
		context.SetStyle(placed.element, style);
		context.AddClass(placed.element, EditorTheme::Class::GraphBlock);

		placed.header = context.CreateElement();
		context.AppendChild(placed.element, placed.header);
		UI::Style header;
		header.position = UI::Position::Absolute;
		header.pointerEvents = false;
		header.textAlignVertical = UI::Align::Center;
		header.textOverflow = UI::TextOverflow::Ellipsis;
		context.SetStyle(placed.header, header);

		const GraphNodeId id = desc.id;
		const bool enabled = desc.enabled;
		placed.check = context.CreateElement();
		context.AppendChild(placed.element, placed.check);
		UI::Style check;
		check.position = UI::Position::Absolute;
		check.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::GraphInlineField));
		check.borderColor = EditorTheme::GetColor(ColorToken::GraphInlineFieldEdge);
		context.SetStyle(placed.check, check);
		context.AddClass(placed.check, EditorTheme::Class::Clickable);
		UI::ElementEvents& checkEvents = context.Events(placed.check);
		checkEvents.customDraw = [enabled](UI::Context& c, UI::ElementHandle element, UI::PaintScope& paint) {
			if (!enabled) {
				return;
			}
			const UI::Rect box = c.GetLayout(element).content;
			const f32 width = Math::Max(1.0f, c.GetContentScale() * 1.5f);
			const Color color = EditorTheme::GetColor(ColorToken::TextStrong);
			const Vec2 knee(box.x + box.w * 0.4f, box.y + box.h * 0.75f);
			paint.EmitLine(Vec2(box.x + box.w * 0.15f, box.y + box.h * 0.5f), knee, width, color, UI::LineCap::Round);
			paint.EmitLine(knee, Vec2(box.x + box.w * 0.85f, box.y + box.h * 0.2f), width, color, UI::LineCap::Round);
		};
		checkEvents.onClick = [this, id, enabled]() {
			if (_model) {
				_model->SetBlockEnabled(id, !enabled);
			}
		};

		BuildSettings(context, placed.element, desc.settings, desc.id, placed.settings);
		for (u32 row = 0; row < desc.inputs.size(); ++row) {
			placed.ports.push_back(
				BuildPort(context, placed.element, node.layer, desc.inputs[row], GraphPortRef{desc.id, GraphPortSide::Input, row}, ColorToken::GraphBlock, true));
		}
		placed.inputs = static_cast<u32>(desc.inputs.size());
		for (u32 row = 0; row < desc.outputs.size(); ++row) {
			placed.ports.push_back(
				BuildPort(context, placed.element, node.layer, desc.outputs[row], GraphPortRef{desc.id, GraphPortSide::Output, row}, ColorToken::GraphBlock, true));
		}

		UI::ElementEvents& events = context.Events(placed.element);
		events.onMouseDown = [this, id]() {
			if (UI::Context* c = Live()) {
				PressBlock(*c, id);
			}
		};
		events.onPressDrag = [this](Vec2 point) {
			if (UI::Context* c = Live()) {
				Travel(*c, point);
			}
		};
		events.onPressEnd = [this]() { Release(); };
		events.onRightClick = [this, id]() {
			UI::Context* c = Live();
			if (!c || !_model) {
				return;
			}
			const GraphItem item{GraphItemKind::Block, id};
			if (!IsSelected(item) && SetSelectionTo(std::span<const GraphItem>(&item, 1))) {
				Notify();
			}
			_model->OnBlockContextMenu(*c, id);
		};
		node.blocks.push_back(std::move(placed));
	}

	void GraphCanvas::PlaceNodes(UI::Context& context) {
		const bool children = _styledZoom != _view.zoom;
		for (const PlacedNode& node : _nodes) {
			if (context.IsAlive(node.element)) {
				StyleNode(context, node, children);
			}
		}
		for (const PlacedNote& note : _notes) {
			StyleNote(context, note);
		}
		PlaceGroups(context);
		_styledZoom = _view.zoom;
	}

	void GraphCanvas::StyleNode(UI::Context& context, const PlacedNode& node, bool children) {
		UI::Rect box{};
		if (!NodeBox(node, box)) {
			return;
		}
		const f32 zoom = _view.zoom;
		UI::Style style = context.GetStyle(node.element);
		style.left = UI::Length::Pixel(box.x);
		style.top = UI::Length::Pixel(box.y);
		style.width = UI::Length::Pixel(box.w);
		style.height = UI::Length::Pixel(box.h);
		style.borderColor = EditorTheme::GetColor(_selectedNodes.contains(node.id) ? ColorToken::SelectionWire
																				   : _model->GetNodeBorder(node.id));
		const f32 radius = node.radius * zoom;
		style.borderRadius = {radius, radius, radius, radius};
		context.SetStyle(node.element, style);
		context.SetTooltip(node.element, _model->GetNodeTooltip(node.id));
		const f32 rowsTop = node.compact ? 0.0f : node.layout.rows;
		StyleFields(context, node.ports, node.inputs, box, rowsTop, children);
		if (node.stack) {
			StyleBlocks(context, node, box, children);
		}
		if (!children) {
			return;
		}
		if (context.IsAlive(node.well)) {
			const UI::Rect well = GraphCanvasOps::PreviewWell(UI::Rect{0.0f, 0.0f, box.w, box.h}, node.rows, zoom);
			UI::Style wellStyle = context.GetStyle(node.well);
			wellStyle.left = UI::Length::Pixel(well.x);
			wellStyle.top = UI::Length::Pixel(well.y);
			wellStyle.width = UI::Length::Pixel(well.w);
			wellStyle.height = UI::Length::Pixel(well.h);
			context.SetStyle(node.well, wellStyle);
		}
		if (context.IsAlive(node.strip)) {
			const UI::Rect strip = GraphCanvasOps::PreviewStrip(UI::Rect{0.0f, 0.0f, box.w, box.h}, zoom);
			UI::Style stripStyle = context.GetStyle(node.strip);
			stripStyle.left = UI::Length::Pixel(strip.x);
			stripStyle.top = UI::Length::Pixel(strip.y);
			stripStyle.width = UI::Length::Pixel(strip.w);
			stripStyle.height = UI::Length::Pixel(strip.h);
			context.SetStyle(node.strip, stripStyle);
		}

		const f32 fontSmall = EditorTheme::GetMetric(MetricToken::FontSmall) * zoom;
		if (!node.compact && !context.IsAlive(node.header)) {
			context.SetText(node.element, UISystem::GetDefaultFont(), fontSmall, EditorTheme::GetColor(ColorToken::TextStrong),
							node.label);
			return;
		}
		if (context.IsAlive(node.header)) {
			UI::Style header = context.GetStyle(node.header);
			header.left = UI::Length::Pixel(HEADER_INSET * zoom);
			header.top = UI::Length::Pixel(node.layout.header * zoom);
			header.width = UI::Length::Pixel(Math::Max(box.w - HEADER_INSET * 2.0f * zoom, 0.0f));
			header.height = UI::Length::Pixel(GraphCanvasOps::NODE_HEADER_HEIGHT * zoom);
			context.SetStyle(node.header, header);
			context.SetText(node.header, UISystem::GetDefaultFont(), fontSmall, EditorTheme::GetColor(ColorToken::TextStrong), node.label);
		}
		StylePorts(context, node.ports, node.inputs, box.w, box.h, rowsTop);
		StyleSettings(context, node.settings, node.layout.settings, box.w);
		StyleFlows(context, node, box.w, box.h);
	}

	void GraphCanvas::StylePorts(UI::Context& context, const Vector<PlacedPort>& ports, u32 inputs, f32 width, f32 height, f32 header) {
		using GraphCanvasOps::PORT_INSET;
		using GraphCanvasOps::PORT_RADIUS;
		using GraphCanvasOps::PORT_ROW_HEIGHT;

		const f32 zoom = _view.zoom;
		const f32 fontTiny = EditorTheme::GetMetric(MetricToken::FontTiny) * zoom;
		const f32 diameter = PORT_RADIUS * 2.0f * zoom;
		const f32 labelInset = (PORT_INSET + PORT_RADIUS * 2.0f + LABEL_GAP) * zoom;
		const bool outputs = ports.size() > inputs;
		const f32 half = outputs ? width * 0.5f : width;
		for (size_t index = 0; index < ports.size(); ++index) {
			const PlacedPort& port = ports[index];
			const bool input = index < inputs;
			const u32 row = input ? static_cast<u32>(index) : static_cast<u32>(index) - inputs;
			const Vec2 anchor = GraphCanvasOps::PortAnchor(UI::Rect{0.0f, 0.0f, width, height},
														   input ? GraphPortSide::Input : GraphPortSide::Output, row, zoom, header);
			UI::Style dot = context.GetStyle(port.dot);
			dot.left = UI::Length::Pixel(anchor.x - diameter * 0.5f);
			dot.top = UI::Length::Pixel(anchor.y - diameter * 0.5f);
			dot.width = UI::Length::Pixel(diameter);
			dot.height = UI::Length::Pixel(diameter);
			const f32 round = diameter * 0.5f;
			dot.borderRadius = {round, round, round, round};
			const f32 ring = DOT_BORDER * zoom;
			dot.borderWidth = {ring, ring, ring, ring};
			context.SetStyle(port.dot, dot);

			if (!context.IsAlive(port.label)) {
				continue;
			}
			f32 inset = labelInset;
			if (input && context.IsAlive(port.space)) {
				const f32 side = SPACE_BUTTON * zoom;
				UI::Style button = context.GetStyle(port.space);
				button.left = UI::Length::Pixel(labelInset);
				button.top = UI::Length::Pixel((header + static_cast<f32>(row) * PORT_ROW_HEIGHT) * zoom + (PORT_ROW_HEIGHT * zoom - side) * 0.5f);
				button.width = UI::Length::Pixel(side);
				button.height = UI::Length::Pixel(side);
				const f32 round = 2.0f * zoom;
				button.borderRadius = {round, round, round, round};
				context.SetStyle(port.space, button);
				context.SetText(port.space, UISystem::GetDefaultFont(), fontTiny, EditorTheme::GetColor(ColorToken::Text), port.spaceText);
				inset += side + LABEL_GAP * zoom;
			}
			f32 labelWidth = Math::Max(half - inset, 0.0f);
			if (input && _desc.fieldsInside && context.IsAlive(port.field)) {
				labelWidth = Math::Max(labelWidth - (InlineWidth(port.inlineValue) + GraphCanvasOps::INLINE_INSIDE_INSET) * zoom - LABEL_GAP * zoom,
									   0.0f);
			}
			UI::Style label = context.GetStyle(port.label);
			label.left = UI::Length::Pixel(input ? inset : width * 0.5f);
			label.top = UI::Length::Pixel((header + static_cast<f32>(row) * PORT_ROW_HEIGHT) * zoom);
			label.width = UI::Length::Pixel(input ? labelWidth : Math::Max(width * 0.5f - labelInset, 0.0f));
			label.height = UI::Length::Pixel(PORT_ROW_HEIGHT * zoom);
			context.SetStyle(port.label, label);
			context.SetText(port.label, UISystem::GetDefaultFont(), fontTiny,
							EditorTheme::GetColor(port.dim ? ColorToken::TextFaint : ColorToken::WidgetTextDim), port.text);
		}
	}

	void GraphCanvas::StyleSettings(UI::Context& context, const Vector<PlacedSetting>& settings, f32 top, f32 width) {
		const f32 zoom = _view.zoom;
		const f32 font = EditorTheme::GetMetric(MetricToken::FontTiny) * zoom;
		const f32 inset = HEADER_INSET * zoom;
		for (size_t index = 0; index < settings.size(); ++index) {
			const PlacedSetting& setting = settings[index];
			if (!context.IsAlive(setting.element)) {
				continue;
			}
			UI::Style row = context.GetStyle(setting.element);
			row.left = UI::Length::Pixel(inset);
			row.top = UI::Length::Pixel((top + static_cast<f32>(index) * GraphCanvasOps::SETTING_ROW_HEIGHT + 2.0f) * zoom);
			row.width = UI::Length::Pixel(Math::Max(width - inset * 2.0f, 0.0f));
			row.height = UI::Length::Pixel((GraphCanvasOps::SETTING_ROW_HEIGHT - 4.0f) * zoom);
			context.SetStyle(setting.element, row);
			context.SetText(setting.label, UISystem::GetDefaultFont(), font, EditorTheme::GetColor(ColorToken::WidgetTextDim), setting.labelText);
			UI::Style value = context.GetStyle(setting.value);
			const f32 round = 3.0f * zoom;
			value.borderRadius = {round, round, round, round};
			value.padding = {LABEL_GAP * zoom, 0.0f, LABEL_GAP * zoom, 0.0f};
			context.SetStyle(setting.value, value);
			context.SetText(setting.value, UISystem::GetDefaultFont(), font, EditorTheme::GetColor(ColorToken::Text), setting.valueText);
			if (context.IsAlive(setting.action)) {
				UI::Style button = context.GetStyle(setting.action);
				button.borderRadius = {round, round, round, round};
				button.padding = {LABEL_GAP * zoom, 0.0f, LABEL_GAP * zoom, 0.0f};
				button.margin.left = LABEL_GAP * zoom;
				context.SetStyle(setting.action, button);
				context.SetText(setting.action, UISystem::GetDefaultFont(), font, EditorTheme::GetColor(ColorToken::Text), setting.actionText);
			}
		}
	}

	void GraphCanvas::StyleFlows(UI::Context& context, const PlacedNode& node, f32 width, f32 height) {
		const f32 zoom = _view.zoom;
		const f32 size = GraphCanvasOps::PORT_RADIUS * 2.0f * zoom;
		const f32 font = EditorTheme::GetMetric(MetricToken::FontTiny) * zoom;
		const u32 outputs = static_cast<u32>(node.flows.size()) - node.flowInputs;
		for (size_t index = 0; index < node.flows.size(); ++index) {
			const PlacedFlow& flow = node.flows[index];
			const bool input = index < node.flowInputs;
			const u32 slot = input ? static_cast<u32>(index) : static_cast<u32>(index) - node.flowInputs;
			const u32 count = input ? node.flowInputs : outputs;
			const Vec2 anchor = GraphCanvasOps::FlowAnchor(UI::Rect{0.0f, 0.0f, width, height},
														   input ? GraphPortSide::FlowInput : GraphPortSide::FlowOutput, slot, count, zoom);
			UI::Style dot = context.GetStyle(flow.dot);
			dot.left = UI::Length::Pixel(anchor.x - size * 0.5f);
			dot.top = UI::Length::Pixel(anchor.y - size * 0.5f);
			dot.width = UI::Length::Pixel(size);
			dot.height = UI::Length::Pixel(size);
			const f32 round = size * 0.2f;
			dot.borderRadius = {round, round, round, round};
			const f32 ring = DOT_BORDER * zoom;
			dot.borderWidth = {ring, ring, ring, ring};
			context.SetStyle(flow.dot, dot);
			if (!context.IsAlive(flow.label)) {
				continue;
			}
			const f32 share = width / static_cast<f32>(Math::Max(count, 1u));
			UI::Style label = context.GetStyle(flow.label);
			label.left = UI::Length::Pixel(anchor.x + size * 0.5f + LABEL_GAP * zoom);
			label.top = UI::Length::Pixel(anchor.y - GraphCanvasOps::FLOW_STRIP_HEIGHT * 0.5f * zoom);
			label.width = UI::Length::Pixel(Math::Max(share * 0.5f - size - LABEL_GAP * zoom, 0.0f));
			label.height = UI::Length::Pixel(GraphCanvasOps::FLOW_STRIP_HEIGHT * zoom);
			context.SetStyle(flow.label, label);
			context.SetText(flow.label, UISystem::GetDefaultFont(), font, EditorTheme::GetColor(ColorToken::WidgetTextDim), flow.text);
		}
	}

	const GraphCanvas::PlacedPort* GraphCanvas::FindPort(const GraphPortRef& ref) const {
		if (ref.side != GraphPortSide::Input && ref.side != GraphPortSide::Output) {
			return nullptr;
		}
		if (const PlacedNode* node = FindNode(ref.node)) {
			const size_t index = ref.side == GraphPortSide::Input ? ref.port : node->inputs + ref.port;
			return index < node->ports.size() ? &node->ports[index] : nullptr;
		}
		const PlacedNode* node = nullptr;
		u32 block = 0;
		if (!FindBlock(ref.node, node, block)) {
			return nullptr;
		}
		const PlacedBlock& placed = node->blocks[block];
		const size_t index = ref.side == GraphPortSide::Input ? ref.port : placed.inputs + ref.port;
		if (ref.side == GraphPortSide::Input && ref.port >= placed.inputs) {
			return nullptr;
		}
		return index < placed.ports.size() ? &placed.ports[index] : nullptr;
	}

	void GraphCanvas::BuildInlineField(UI::Context& context, UI::ElementHandle layer, PlacedPort& port, const GraphPortRef& ref) {
		port.field = context.CreateElement();
		context.AppendChild(layer, port.field);
		UI::Style style;
		style.position = UI::Position::Absolute;
		style.flexDirection = UI::FlexDirection::Row;
		style.alignItems = UI::Align::Stretch;
		style.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::GraphInlineField));
		style.borderWidth = {1.0f, 1.0f, 1.0f, 1.0f};
		style.borderColor = EditorTheme::GetColor(ColorToken::GraphInlineFieldEdge);
		style.pointerEvents = port.inlineValue.kind != GraphInlineKind::Gradient;
		context.SetStyle(port.field, style);
		context.AddClass(port.field, EditorTheme::Class::GraphInlineField);

		const GraphInlineValue& value = port.inlineValue;
		const f32 share = value.kind == GraphInlineKind::Numbers ? 1.0f / static_cast<f32>(Math::Max<u32>(value.count, 1u)) : 1.0f;
		auto addPart = [&context, &port, share]() {
			UI::ElementHandle part = context.CreateElement();
			context.AppendChild(port.field, part);
			UI::Style partStyle;
			partStyle.width = UI::Length::Percent(share);
			partStyle.height = UI::Length::Percent(1.0f);
			partStyle.textAlign = UI::Align::Center;
			partStyle.textAlignVertical = UI::Align::Center;
			partStyle.textOverflow = UI::TextOverflow::Ellipsis;
			context.SetStyle(part, partStyle);
			return part;
		};
		switch (value.kind) {
		case GraphInlineKind::Numbers:
			for (u32 component = 0; component < Math::Max<u32>(value.count, 1u); ++component) {
				UI::ElementHandle part = addPart();
				context.Events(part).onClick = [this, ref, component]() {
					if (UI::Context* c = Live()) {
						BeginInlineEdit(*c, ref, component);
					}
				};
			}
			break;
		case GraphInlineKind::Color: {
			UI::ElementHandle part = addPart();
			UI::Style swatch = context.GetStyle(part);
			swatch.background = UI::Brush::SolidColor(Color(value.values[0], value.values[1], value.values[2], 1.0f));
			context.SetStyle(part, swatch);
			context.Events(part).onClick = [this, ref, part]() {
				UI::Context* c = Live();
				const PlacedPort* live = FindPort(ref);
				if (!c || !live || !_model) {
					return;
				}
				const Color current(live->inlineValue.values[0], live->inlineValue.values[1], live->inlineValue.values[2],
									live->inlineValue.values[3]);
				UI::Widgets::OpenColorPicker(
					*c, part, current, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontSmall),
					[this, ref](Color picked) {
						if (_model) {
							const f32 values[4] = {picked.r, picked.g, picked.b, picked.a};
							_model->SetInlineValue(ref, values);
						}
					},
					[this]() {
						if (_model) {
							_model->OnInlineEditBegin();
						}
					},
					[this]() {
						if (_model) {
							_model->OnInlineEditEnd();
						}
					});
			};
			break;
		}
		case GraphInlineKind::Toggle: {
			UI::ElementHandle part = addPart();
			context.Events(part).onClick = [this, ref]() {
				const PlacedPort* live = FindPort(ref);
				if (live && _model) {
					const f32 flipped[1] = {live->inlineValue.values[0] != 0.0f ? 0.0f : 1.0f};
					_model->SetInlineValue(ref, flipped);
				}
			};
			break;
		}
		case GraphInlineKind::Label: {
			UI::ElementHandle part = addPart();
			UI::Style label = context.GetStyle(part);
			label.pointerEvents = false;
			context.SetStyle(part, label);
			break;
		}
		case GraphInlineKind::Curve: {
			UI::ElementHandle part = addPart();
			context.Events(part).customDraw = [this, ref](UI::Context& c, UI::ElementHandle element, UI::PaintScope& paint) {
				if (const PlacedPort* live = FindPort(ref)) {
					PaintCurveField(c, element, paint, live->inlineValue.curve);
				}
			};
			context.Events(part).onClick = [this, ref, part]() {
				UI::Context* c = Live();
				if (c && _model) {
					_model->OpenInlineEditor(*c, ref, part);
				}
			};
			break;
		}
		case GraphInlineKind::Gradient: {
			UI::ElementHandle part = addPart();
			UI::Style ramp = context.GetStyle(part);
			ramp.pointerEvents = false;
			context.SetStyle(part, ramp);
			context.Events(part).customDraw = [this, ref](UI::Context& c, UI::ElementHandle element, UI::PaintScope& paint) {
				if (const PlacedPort* live = FindPort(ref)) {
					PaintGradientField(c, element, paint, live->inlineValue.ramp);
				}
			};
			break;
		}
		case GraphInlineKind::None:
			break;
		}
	}

	void GraphCanvas::StyleFields(UI::Context& context, const Vector<PlacedPort>& ports, u32 inputs, const UI::Rect& box, f32 header,
								  bool children) {
		UI::Rect within = box;
		if (_desc.fieldsInside && ports.size() > inputs) {
			within.w *= 0.5f;
		}
		for (size_t index = 0; index < ports.size() && index < inputs; ++index) {
			if (context.IsAlive(ports[index].field)) {
				StyleInlineField(context, ports[index], within, static_cast<u32>(index), header, children);
			}
		}
	}

	void GraphCanvas::StyleInlineField(UI::Context& context, const PlacedPort& port, const UI::Rect& box, u32 row, f32 header, bool children) {
		const f32 zoom = _view.zoom;
		const f32 width = InlineWidth(port.inlineValue);
		const UI::Rect field = _desc.fieldsInside ? GraphCanvasOps::InlineFieldInside(box, row, width, zoom, header)
												  : GraphCanvasOps::InlineField(box, row, width, zoom, header);
		UI::Style style = context.GetStyle(port.field);
		style.left = UI::Length::Pixel(field.x);
		style.top = UI::Length::Pixel(field.y);
		style.width = UI::Length::Pixel(field.w);
		style.height = UI::Length::Pixel(field.h);
		const f32 round = 3.0f * zoom;
		style.borderRadius = {round, round, round, round};
		context.SetStyle(port.field, style);
		if (!children) {
			return;
		}
		const f32 font = EditorTheme::GetMetric(MetricToken::FontTiny) * zoom;
		const Color text = EditorTheme::GetColor(ColorToken::WidgetTextDim);
		const UI::Element* pod = context.GetElement(port.field);
		u32 component = 0;
		for (u32 child = pod ? pod->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX; ++component) {
			UI::ElementHandle part = context.MakeHandle(child);
			const GraphInlineValue& value = port.inlineValue;
			switch (value.kind) {
			case GraphInlineKind::Numbers:
				context.SetText(part, UISystem::GetDefaultFont(), font, text, InlineText(value.values[Math::Min<u32>(component, 3u)]));
				break;
			case GraphInlineKind::Toggle:
				context.SetText(part, UISystem::GetDefaultFont(), font, text, value.values[0] != 0.0f ? "x" : "");
				break;
			case GraphInlineKind::Label:
				context.SetText(part, UISystem::GetDefaultFont(), font, text, value.text);
				break;
			case GraphInlineKind::Color:
			case GraphInlineKind::Curve:
			case GraphInlineKind::Gradient:
			case GraphInlineKind::None:
				break;
			}
			child = context.GetElement(part)->nextSibling;
		}
	}

	void GraphCanvas::BeginInlineEdit(UI::Context& context, const GraphPortRef& ref, u32 component) {
		const PlacedPort* port = FindPort(ref);
		if (!port || !_model || !context.IsAlive(port->field) || component >= 4) {
			return;
		}
		CloseEditor();
		const UI::Rect field = context.GetLayout(port->field).borderBox;
		const f32 scale = context.GetContentScale();
		const u32 count = Math::Max<u32>(port->inlineValue.count, 1u);
		const f32 partWidth = field.w / static_cast<f32>(count);
		const GraphInlineValue before = port->inlineValue;
		const str current = InlineText(before.values[component]);
		UI::Widgets::TextFieldDesc desc;
		desc.text = current;
		desc.font = UISystem::GetDefaultFont();
		desc.fontSizePx = EditorTheme::GetMetric(MetricToken::FontTiny) * _view.zoom;
		desc.onCommit = [this, ref, component, before](std::string_view typed) {
			str text(typed);
			char* end = nullptr;
			const f32 parsed = std::strtof(text.c_str(), &end);
			if (_model && end != text.c_str() && std::isfinite(parsed)) {
				f32 values[4] = {before.values[0], before.values[1], before.values[2], before.values[3]};
				values[component] = parsed;
				_model->SetInlineValue(ref, std::span<const f32>(values, Math::Max<u32>(before.count, 1u)));
			}
			if (UI::Context* c = Live()) {
				c->SetFocus(_canvas);
			}
		};
		_editor = UI::Widgets::TextField(context, _inlineHost, desc);
		UI::Style style = context.GetStyle(_editor);
		style.position = UI::Position::Absolute;
		const UI::Rect canvas = context.GetLayout(_canvas).content;
		style.left = UI::Length::Pixel((field.x - canvas.x + partWidth * static_cast<f32>(component)) / scale);
		style.top = UI::Length::Pixel((field.y - canvas.y) / scale);
		style.width = UI::Length::Pixel(Math::Max(partWidth, 1.0f) / scale);
		style.height = UI::Length::Pixel(Math::Max(field.h, 1.0f) / scale);
		context.SetStyle(_editor, style);
		UI::ElementEvents& events = context.Events(_editor);
		std::function<void()> commitOnBlur = events.onFocusLost;
		events.onFocusLost = [this, commitOnBlur]() {
			if (commitOnBlur) {
				commitOnBlur();
			}
			CloseEditor();
		};
		context.SetFocus(_editor);
	}

	bool GraphCanvas::FindBlock(GraphNodeId id, const PlacedNode*& node, u32& index) const {
		auto found = _blockIndex.find(id);
		if (found == _blockIndex.end() || found->second.first >= _nodes.size()) {
			return false;
		}
		node = &_nodes[found->second.first];
		index = found->second.second;
		return index < node->blocks.size();
	}

	bool GraphCanvas::BlockBox(const PlacedNode& node, u32 index, UI::Rect& out) const {
		UI::Rect box{};
		if (index >= node.blocks.size() || !NodeBox(node, box)) {
			return false;
		}
		const f32 zoom = _view.zoom;
		const f32 pad = GraphCanvasOps::STACK_PADDING * zoom;
		out = UI::Rect{box.x + pad, box.y + (node.layout.stack + GraphCanvasOps::BlockTop(node.blockHeights, index)) * zoom,
					   Math::Max(box.w - pad * 2.0f, 0.0f), node.blocks[index].height * zoom};
		return true;
	}

	void GraphCanvas::StyleBlocks(UI::Context& context, const PlacedNode& node, const UI::Rect& box, bool children) {
		const f32 zoom = _view.zoom;
		const f32 pad = GraphCanvasOps::STACK_PADDING * zoom;
		for (u32 index = 0; index < node.blocks.size(); ++index) {
			const PlacedBlock& block = node.blocks[index];
			if (!context.IsAlive(block.element)) {
				continue;
			}
			UI::Rect blockBox{};
			BlockBox(node, index, blockBox);
			UI::Style style = context.GetStyle(block.element);
			style.left = UI::Length::Pixel(pad);
			style.top = UI::Length::Pixel(blockBox.y - box.y);
			style.width = UI::Length::Pixel(blockBox.w);
			style.height = UI::Length::Pixel(blockBox.h);
			style.borderColor =
				EditorTheme::GetColor(_selectedBlocks.contains(block.id) ? ColorToken::SelectionWire : _model->GetNodeBorder(block.id));
			const f32 round = 3.0f * zoom;
			style.borderRadius = {round, round, round, round};
			context.SetStyle(block.element, style);
			context.SetTooltip(block.element, _model->GetNodeTooltip(block.id));
			const f32 rowsTop = GraphCanvasOps::BLOCK_HEADER_HEIGHT + static_cast<f32>(block.settings.size()) * GraphCanvasOps::SETTING_ROW_HEIGHT;
			StyleFields(context, block.ports, block.inputs, blockBox, rowsTop, children);
			if (!children) {
				continue;
			}
			const f32 check = CHECK_SIZE * zoom;
			UI::Style checkStyle = context.GetStyle(block.check);
			checkStyle.left = UI::Length::Pixel(HEADER_INSET * zoom);
			checkStyle.top = UI::Length::Pixel((GraphCanvasOps::BLOCK_HEADER_HEIGHT * zoom - check) * 0.5f);
			checkStyle.width = UI::Length::Pixel(check);
			checkStyle.height = UI::Length::Pixel(check);
			const f32 edge = DOT_BORDER * zoom * 0.75f;
			checkStyle.borderWidth = {edge, edge, edge, edge};
			const f32 corner = 2.0f * zoom;
			checkStyle.borderRadius = {corner, corner, corner, corner};
			context.SetStyle(block.check, checkStyle);
			UI::Style header = context.GetStyle(block.header);
			const f32 titleLeft = HEADER_INSET * zoom + check + LABEL_GAP * zoom;
			header.left = UI::Length::Pixel(titleLeft);
			header.top = UI::Length::Pixel(0.0f);
			header.width = UI::Length::Pixel(Math::Max(blockBox.w - titleLeft - HEADER_INSET * zoom, 0.0f));
			header.height = UI::Length::Pixel(GraphCanvasOps::BLOCK_HEADER_HEIGHT * zoom);
			context.SetStyle(block.header, header);
			context.SetText(block.header, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontSmall) * zoom,
							EditorTheme::GetColor(block.enabled ? ColorToken::TextStrong : ColorToken::TextFaint), block.label);
			StyleSettings(context, block.settings, GraphCanvasOps::BLOCK_HEADER_HEIGHT, blockBox.w);
			StylePorts(context, block.ports, block.inputs, blockBox.w, blockBox.h, rowsTop);
		}
	}

	const GraphCanvas::PlacedNode* GraphCanvas::StackAt(Vec2 local) const {
		for (size_t index = _nodes.size(); index-- > 0;) {
			const PlacedNode& node = _nodes[index];
			UI::Rect box{};
			if (node.stack && NodeBox(node, box) && box.Contains(local)) {
				return &node;
			}
		}
		return nullptr;
	}

	u32 GraphCanvas::GapAt(const PlacedNode& stack, Vec2 local) const {
		UI::Rect box{};
		if (!NodeBox(stack, box)) {
			return static_cast<u32>(stack.blocks.size());
		}
		return GraphCanvasOps::StackGap(stack.blockHeights, (local.y - box.y) / _view.zoom - stack.layout.stack);
	}

	void GraphCanvas::BeginSettingEdit(UI::Context& context, GraphNodeId owner, u32 setting) {
		const PlacedSetting* placed = nullptr;
		if (const PlacedNode* node = FindNode(owner)) {
			placed = setting < node->settings.size() ? &node->settings[setting] : nullptr;
		} else {
			const PlacedNode* holder = nullptr;
			u32 block = 0;
			if (FindBlock(owner, holder, block) && setting < holder->blocks[block].settings.size()) {
				placed = &holder->blocks[block].settings[setting];
			}
		}
		if (!placed || !_model || !context.IsAlive(placed->value)) {
			return;
		}
		CloseEditor();
		const UI::Rect value = context.GetLayout(placed->value).borderBox;
		const UI::Rect canvas = context.GetLayout(_canvas).content;
		const f32 scale = context.GetContentScale();
		UI::Widgets::TextFieldDesc desc;
		desc.text = placed->valueText;
		desc.font = UISystem::GetDefaultFont();
		desc.fontSizePx = EditorTheme::GetMetric(MetricToken::FontTiny) * _view.zoom;
		desc.onCommit = [this, owner, setting](std::string_view typed) {
			if (_model) {
				_model->SetSettingText(owner, setting, typed);
			}
			if (UI::Context* c = Live()) {
				c->SetFocus(_canvas);
			}
		};
		_editor = UI::Widgets::TextField(context, _inlineHost, desc);
		UI::Style style = context.GetStyle(_editor);
		style.position = UI::Position::Absolute;
		style.left = UI::Length::Pixel((value.x - canvas.x) / scale);
		style.top = UI::Length::Pixel((value.y - canvas.y) / scale);
		style.width = UI::Length::Pixel(Math::Max(value.w, 1.0f) / scale);
		style.height = UI::Length::Pixel(Math::Max(value.h, 1.0f) / scale);
		context.SetStyle(_editor, style);
		UI::ElementEvents& events = context.Events(_editor);
		std::function<void()> commitOnBlur = events.onFocusLost;
		events.onFocusLost = [this, commitOnBlur]() {
			if (commitOnBlur) {
				commitOnBlur();
			}
			CloseEditor();
		};
		context.SetFocus(_editor);
	}

	bool GraphCanvas::FrameAll(UI::Context& context) {
		return FrameNodes(context, [](const PlacedNode&) { return true; });
	}

	bool GraphCanvas::FrameSelection(UI::Context& context) {
		if (_selectedNodes.empty()) {
			return FrameAll(context);
		}
		return FrameNodes(context, [this](const PlacedNode& node) { return _selectedNodes.contains(node.id); });
	}

	template <typename Include>
	bool GraphCanvas::FrameNodes(UI::Context& context, Include&& include) {
		if (_nodes.empty() || !context.IsAlive(_canvas) || !_model) {
			return false;
		}
		Vec2 lower{};
		Vec2 upper{};
		bool any = false;
		for (const PlacedNode& node : _nodes) {
			Vec2 at{};
			if (!include(node) || !_model->GetNodePosition(node.id, at)) {
				continue;
			}
			const Vec2 far = at + node.size;
			lower = any ? Vec2(Math::Min(lower.x, at.x), Math::Min(lower.y, at.y)) : at;
			upper = any ? Vec2(Math::Max(upper.x, far.x), Math::Max(upper.y, far.y)) : far;
			any = true;
		}
		if (!any) {
			return false;
		}
		const UI::Rect content = context.GetLayout(_canvas).content;
		const f32 scale = context.GetContentScale();
		if (!GraphCanvasOps::FrameBounds(_view, lower, upper, Vec2(content.w / scale, content.h / scale), FRAME_MARGIN,
										 _desc.minZoom)) {
			return false;
		}
		PlaceNodes(context);
		return true;
	}

	bool GraphCanvas::ArrowEnds(const GraphWireDesc& wire, Vec2& from, Vec2& to) const {
		const PlacedNode* source = FindNode(wire.from.node);
		const PlacedNode* target = FindNode(wire.to.node);
		UI::Rect fromBox{};
		UI::Rect toBox{};
		if (!source || !target || !NodeBox(*source, fromBox) || !NodeBox(*target, toBox)) {
			return false;
		}
		const Vec2 fromCentre(fromBox.x + fromBox.w * 0.5f, fromBox.y + fromBox.h * 0.5f);
		const Vec2 toCentre(toBox.x + toBox.w * 0.5f, toBox.y + toBox.h * 0.5f);
		from = GraphCanvasOps::ClipToBox(fromBox, toCentre);
		to = GraphCanvasOps::ClipToBox(toBox, fromCentre);
		if (wire.fan != 0) {
			const Vec2 span = to - from;
			const f32 length = span.Length();
			if (length > 1e-3f) {
				const Vec2 perpendicular(-span.y / length, span.x / length);
				const Vec2 shift = perpendicular * (static_cast<f32>(wire.fan) * WIRE_FAN * _view.zoom);
				from = from + shift;
				to = to + shift;
			}
		}
		return true;
	}

	bool GraphCanvas::PortAt(const GraphPortRef& port, Vec2& out) const {
		if (const PlacedNode* node = FindNode(port.node)) {
			UI::Rect box{};
			if (!NodeBox(*node, box)) {
				return false;
			}
			if (IsFlow(port.side)) {
				const bool input = port.side == GraphPortSide::FlowInput;
				const u32 count = input ? node->flowInputs : static_cast<u32>(node->flows.size()) - node->flowInputs;
				if (port.port >= count) {
					return false;
				}
				out = GraphCanvasOps::FlowAnchor(box, port.side, port.port, count, _view.zoom);
				return true;
			}
			const u32 outputs = static_cast<u32>(node->ports.size()) - node->inputs;
			if (port.port >= (port.side == GraphPortSide::Input ? node->inputs : outputs)) {
				return false;
			}
			out = GraphCanvasOps::PortAnchor(box, port.side, port.port, _view.zoom, node->compact ? 0.0f : node->layout.rows);
			return true;
		}
		const PlacedNode* node = nullptr;
		u32 block = 0;
		UI::Rect box{};
		if (IsFlow(port.side) || !FindBlock(port.node, node, block) || !BlockBox(*node, block, box)) {
			return false;
		}
		const PlacedBlock& placed = node->blocks[block];
		const u32 outputs = static_cast<u32>(placed.ports.size()) - placed.inputs;
		if (port.port >= (port.side == GraphPortSide::Input ? placed.inputs : outputs)) {
			return false;
		}
		const f32 header = GraphCanvasOps::BLOCK_HEADER_HEIGHT + static_cast<f32>(placed.settings.size()) * GraphCanvasOps::SETTING_ROW_HEIGHT;
		out = GraphCanvasOps::PortAnchor(box, port.side, port.port, _view.zoom, header);
		return true;
	}

	bool GraphCanvas::CurveOf(const GraphWireDesc& wire, GraphCubic& out) const {
		Vec2 from{};
		Vec2 to{};
		if (!PortAt(wire.from, from) || !PortAt(wire.to, to)) {
			return false;
		}
		out = wire.style == GraphWireStyle::Flow ? GraphCanvasOps::FlowWire(from, to, _view.zoom) : GraphCanvasOps::PortWire(from, to, _view.zoom);
		return true;
	}

	bool GraphCanvas::PickWire(Vec2 local, f32 scale, GraphWireId& out) const {
		if (!_model) {
			return false;
		}
		_wires.clear();
		_model->GetWires(_wires);
		f32 best = GraphCanvasOps::WIRE_PICK_RADIUS;
		bool found = false;
		for (const GraphWireDesc& wire : _wires) {
			if (wire.id == 0) {
				continue;
			}
			f32 distance = best + 1.0f;
			if (wire.style == GraphWireStyle::Arrow) {
				Vec2 from{};
				Vec2 to{};
				if (ArrowEnds(wire, from, to)) {
					distance = GraphCanvasOps::DistanceToSegment(from, to, local);
				}
			} else {
				GraphCubic curve{};
				if (CurveOf(wire, curve)) {
					distance = GraphCanvasOps::DistanceToCubic(curve, GraphCanvasOps::SegmentCount(curve, scale), local);
				}
			}
			if (distance <= best) {
				best = distance;
				out = wire.id;
				found = true;
			}
		}
		return found;
	}

	bool GraphCanvas::PickPort(Vec2 local, GraphPortSide side, GraphPortRef& out) const {
		f32 best = GraphCanvasOps::PORT_PICK_RADIUS * _view.zoom;
		bool found = false;
		auto consider = [&](const GraphPortRef& ref) {
			Vec2 anchor{};
			if (!PortAt(ref, anchor)) {
				return;
			}
			const f32 distance = (anchor - local).Length();
			if (distance <= best) {
				best = distance;
				out = ref;
				found = true;
			}
		};
		for (const PlacedNode& node : _nodes) {
			u32 count = 0;
			switch (side) {
			case GraphPortSide::Input:
				count = node.inputs;
				break;
			case GraphPortSide::Output:
				count = static_cast<u32>(node.ports.size()) - node.inputs;
				break;
			case GraphPortSide::FlowInput:
				count = node.flowInputs;
				break;
			case GraphPortSide::FlowOutput:
				count = static_cast<u32>(node.flows.size()) - node.flowInputs;
				break;
			}
			for (u32 row = 0; row < count; ++row) {
				consider(GraphPortRef{node.id, side, row});
			}
			if (IsFlow(side)) {
				continue;
			}
			for (const PlacedBlock& block : node.blocks) {
				const u32 rows = side == GraphPortSide::Input ? block.inputs : static_cast<u32>(block.ports.size()) - block.inputs;
				for (u32 row = 0; row < rows; ++row) {
					consider(GraphPortRef{block.id, side, row});
				}
			}
		}
		return found;
	}

	void GraphCanvas::EndPortDrag() {
		if (!_portDragging) {
			return;
		}
		_portDragging = false;
		UI::Context* c = Live();
		if (!c || !_model) {
			return;
		}
		const GraphPortRef start = _portDrag;
		const Vec2 local = PointerLocal(*c);
		Vec2 origin{};
		if (PortAt(start, origin) && (local - origin).Length() <= GraphCanvasOps::PORT_PICK_RADIUS * _view.zoom) {
			return;
		}
		GraphPortRef target{};
		bool accepted = false;
		if (PortDragTarget(start, local, target, accepted)) {
			if (accepted) {
				const bool fromOutput = start.side == GraphPortSide::Output || start.side == GraphPortSide::FlowOutput;
				_model->Connect(fromOutput ? start : target, fromOutput ? target : start);
			}
			return;
		}
		OpenSearcher(*c, c->GetPointerPosition(), GraphCanvasOps::LocalToGraph(_view, local), &start);
	}

	bool GraphCanvas::PortDragTarget(const GraphPortRef& source, Vec2 local, GraphPortRef& target, bool& accepted) const {
		accepted = false;
		if (!_model || !PickPort(local, OppositeSide(source.side), target)) {
			return false;
		}
		const bool fromOutput = source.side == GraphPortSide::Output || source.side == GraphPortSide::FlowOutput;
		accepted = _model->CanConnect(fromOutput ? source : target, fromOutput ? target : source);
		return true;
	}

	UI::ElementHandle GraphCanvas::DotOf(const GraphPortRef& ref) const {
		if (const PlacedPort* port = FindPort(ref)) {
			return port->dot;
		}
		const PlacedNode* node = IsFlow(ref.side) ? FindNode(ref.node) : nullptr;
		if (!node) {
			return {};
		}
		const u32 index = ref.side == GraphPortSide::FlowInput ? ref.port : node->flowInputs + ref.port;
		const bool inRange = ref.side == GraphPortSide::FlowInput ? ref.port < node->flowInputs : index < node->flows.size();
		return inRange ? node->flows[index].dot : UI::ElementHandle{};
	}

	bool GraphCanvas::IsPortDot(UI::ElementHandle element) const {
		if (!element.IsValid()) {
			return false;
		}
		for (const PlacedNode& node : _nodes) {
			const auto matches = [&](const PlacedPort& port) { return port.dot == element; };
			if (std::any_of(node.ports.begin(), node.ports.end(), matches) ||
				std::any_of(node.flows.begin(), node.flows.end(), [&](const PlacedFlow& flow) { return flow.dot == element; })) {
				return true;
			}
			for (const PlacedBlock& block : node.blocks) {
				if (std::any_of(block.ports.begin(), block.ports.end(), matches)) {
					return true;
				}
			}
		}
		return false;
	}

	const GraphCanvas::PlacedNote* GraphCanvas::FindNote(u64 id) const {
		for (const PlacedNote& note : _notes) {
			if (note.id == id) {
				return &note;
			}
		}
		return nullptr;
	}

	GraphCanvas::PlacedNote* GraphCanvas::FindNote(u64 id) {
		for (PlacedNote& note : _notes) {
			if (note.id == id) {
				return &note;
			}
		}
		return nullptr;
	}

	GraphCanvas::PlacedGroup* GraphCanvas::FindGroup(u64 id) {
		for (PlacedGroup& group : _groups) {
			if (group.id == id) {
				return &group;
			}
		}
		return nullptr;
	}

	UI::Rect GraphCanvas::NoteBox(const PlacedNote& note) const {
		return GraphCanvasOps::NodeRect(_view, note.position, note.size);
	}

	void GraphCanvas::BuildGroup(UI::Context& context, const GraphGroupDesc& desc) {
		PlacedGroup placed;
		placed.id = desc.id;
		placed.text = desc.title;
		placed.nodes.assign(desc.nodes.begin(), desc.nodes.end());
		placed.notes.assign(desc.notes.begin(), desc.notes.end());
		placed.position = desc.position;

		placed.title = context.CreateElement();
		context.AppendChild(_groupHost, placed.title);
		UI::Style style;
		style.position = UI::Position::Absolute;
		style.textAlignVertical = UI::Align::Center;
		style.textOverflow = UI::TextOverflow::Ellipsis;
		context.SetStyle(placed.title, style);

		const u64 id = desc.id;
		UI::ElementEvents& events = context.Events(placed.title);
		events.onMouseDown = [this, id]() {
			if (UI::Context* c = Live()) {
				PressItemForMove(*c, GraphItem{GraphItemKind::Group, id});
			}
		};
		events.onPressDrag = [this](Vec2 point) {
			if (UI::Context* c = Live()) {
				Travel(*c, point);
			}
		};
		events.onPressEnd = [this]() { Release(); };
		events.onDoubleClick = [this, id]() {
			if (UI::Context* c = Live()) {
				BeginGroupRename(*c, id);
			}
		};
		events.onRightClick = [this, id]() {
			UI::Context* c = Live();
			if (!c || !_model) {
				return;
			}
			const GraphItem item{GraphItemKind::Group, id};
			if (!IsSelected(item) && SetSelectionTo(std::span<const GraphItem>(&item, 1))) {
				Notify();
			}
			_model->OnGroupContextMenu(*c, id);
		};
		_groups.push_back(std::move(placed));
	}

	void GraphCanvas::PlaceGroups(UI::Context& context) {
		const f32 zoom = _view.zoom;
		Vector<UI::Rect> boxes{GetMemoryResource(MemTag::Editor)};
		for (PlacedGroup& group : _groups) {
			boxes.clear();
			for (const GraphNodeId member : group.nodes) {
				UI::Rect box{};
				const PlacedNode* node = FindNode(member);
				if (node && NodeBox(*node, box)) {
					boxes.push_back(box);
				}
			}
			for (const u64 member : group.notes) {
				if (const PlacedNote* note = FindNote(member)) {
					boxes.push_back(NoteBox(*note));
				}
			}
			if (!GraphCanvasOps::GroupFrame(boxes, zoom, group.frame)) {
				group.frame = GraphCanvasOps::NodeRect(
					_view, group.position, Vec2(GraphCanvasOps::GROUP_EMPTY_WIDTH, GraphCanvasOps::GROUP_EMPTY_HEIGHT));
			}
			if (!context.IsAlive(group.title)) {
				continue;
			}
			const UI::Rect bar = GraphCanvasOps::GroupTitleBar(group.frame, zoom);
			UI::Style style = context.GetStyle(group.title);
			style.left = UI::Length::Pixel(bar.x);
			style.top = UI::Length::Pixel(bar.y);
			style.width = UI::Length::Pixel(bar.w);
			style.height = UI::Length::Pixel(bar.h);
			style.padding = {HEADER_INSET * zoom, 0.0f, HEADER_INSET * zoom, 0.0f};
			context.SetStyle(group.title, style);
			context.SetText(group.title, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontSmall) * zoom,
							EditorTheme::GetColor(ColorToken::TextStrong), group.text);
		}
		PlaceOutlineTitles(context);
	}

	void GraphCanvas::BuildOutlineTitles(UI::Context& context) {
		_titledOutlines.clear();
		Vector<GraphOutlineDesc> outlines{GetMemoryResource(MemTag::Editor)};
		_model->GetOutlines(outlines);
		for (const GraphOutlineDesc& desc : outlines) {
			if (desc.title.empty()) {
				continue;
			}
			PlacedOutline placed;
			placed.id = desc.id;
			placed.text = desc.title;
			placed.nodes.assign(desc.nodes.begin(), desc.nodes.end());
			placed.title = context.CreateElement();
			context.AppendChild(_groupHost, placed.title);
			UI::Style style;
			style.position = UI::Position::Absolute;
			style.textAlignVertical = UI::Align::Center;
			style.textOverflow = UI::TextOverflow::Ellipsis;
			context.SetStyle(placed.title, style);
			const u64 id = desc.id;
			context.Events(placed.title).onDoubleClick = [this, id]() {
				if (UI::Context* c = Live()) {
					BeginOutlineRename(*c, id);
				}
			};
			_titledOutlines.push_back(std::move(placed));
		}
	}

	void GraphCanvas::PlaceOutlineTitles(UI::Context& context) {
		const f32 zoom = _view.zoom;
		Vector<UI::Rect> boxes{GetMemoryResource(MemTag::Editor)};
		for (PlacedOutline& outline : _titledOutlines) {
			boxes.clear();
			for (const GraphNodeId member : outline.nodes) {
				UI::Rect box{};
				const PlacedNode* node = FindNode(member);
				if (node && NodeBox(*node, box)) {
					boxes.push_back(box);
				}
			}
			UI::Rect frame{};
			if (!GraphCanvasOps::OutlineFrame(boxes, zoom, frame) || !context.IsAlive(outline.title)) {
				continue;
			}
			outline.bar = GraphCanvasOps::OutlineTitleBar(frame, zoom);
			UI::Style style = context.GetStyle(outline.title);
			style.left = UI::Length::Pixel(outline.bar.x);
			style.top = UI::Length::Pixel(outline.bar.y);
			style.width = UI::Length::Pixel(outline.bar.w);
			style.height = UI::Length::Pixel(outline.bar.h);
			style.padding = {HEADER_INSET * zoom, 0.0f, HEADER_INSET * zoom, 0.0f};
			context.SetStyle(outline.title, style);
			context.SetText(outline.title, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontBody) * zoom,
							EditorTheme::GetColor(ColorToken::TextStrong), outline.text);
		}
	}

	namespace {

		MetricToken NoteBodySize(GraphNoteSize size) {
			switch (size) {
			case GraphNoteSize::Small:
				return MetricToken::GraphNoteSmall;
			case GraphNoteSize::Large:
				return MetricToken::GraphNoteLarge;
			case GraphNoteSize::Huge:
				return MetricToken::GraphNoteHuge;
			case GraphNoteSize::Medium:
				break;
			}
			return MetricToken::GraphNoteMedium;
		}

		MetricToken NoteTitleSize(GraphNoteSize size) {
			switch (size) {
			case GraphNoteSize::Small:
				return MetricToken::GraphNoteMedium;
			case GraphNoteSize::Medium:
				return MetricToken::GraphNoteLarge;
			case GraphNoteSize::Large:
			case GraphNoteSize::Huge:
				break;
			}
			return MetricToken::GraphNoteHuge;
		}

	} // anonymous namespace

	void GraphCanvas::BuildNote(UI::Context& context, const GraphNoteDesc& desc) {
		PlacedNote placed;
		placed.id = desc.id;
		placed.position = desc.position;
		placed.size = desc.size;
		placed.titleText = desc.title;
		placed.bodyText = desc.body;
		placed.theme = desc.theme;
		placed.textSize = desc.textSize;

		placed.element = context.CreateElement();
		context.AppendChild(_noteHost, placed.element);
		UI::Style style;
		style.position = UI::Position::Absolute;
		style.flexDirection = UI::FlexDirection::Column;
		style.overflow = UI::Overflow::Hidden;
		style.borderWidth = {1.0f, 1.0f, 1.0f, 1.0f};
		context.SetStyle(placed.element, style);

		auto addText = [&](UI::ElementHandle& into) {
			into = context.CreateElement();
			context.AppendChild(placed.element, into);
			UI::Style text;
			text.pointerEvents = false;
			text.width = UI::Length::Percent(1.0f);
			text.flexShrink = 0.0f;
			context.SetStyle(into, text);
		};
		addText(placed.title);
		addText(placed.body);

		placed.grip = context.CreateElement();
		context.AppendChild(placed.element, placed.grip);
		UI::Style grip;
		grip.position = UI::Position::Absolute;
		context.SetStyle(placed.grip, grip);
		context.AddClass(placed.grip, EditorTheme::Class::Clickable);

		const u64 id = desc.id;
		UI::ElementEvents& events = context.Events(placed.element);
		events.onMouseDown = [this, id]() {
			if (UI::Context* c = Live()) {
				PressItemForMove(*c, GraphItem{GraphItemKind::Note, id});
			}
		};
		events.onPressDrag = [this](Vec2 point) {
			if (UI::Context* c = Live()) {
				Travel(*c, point);
			}
		};
		events.onPressEnd = [this]() { Release(); };
		events.onDoubleClick = [this, id]() {
			UI::Context* c = Live();
			const PlacedNote* note = FindNote(id);
			if (!c || !note) {
				return;
			}
			const UI::Rect title = c->GetLayout(note->title).borderBox;
			BeginNoteEdit(*c, id, c->GetPointerPosition().y > title.y + title.h);
		};
		events.onRightClick = [this, id]() {
			UI::Context* c = Live();
			if (!c || !_model) {
				return;
			}
			const GraphItem item{GraphItemKind::Note, id};
			if (!IsSelected(item) && SetSelectionTo(std::span<const GraphItem>(&item, 1))) {
				Notify();
			}
			_model->OnNoteContextMenu(*c, id);
		};
		UI::ElementEvents& gripEvents = context.Events(placed.grip);
		gripEvents.onMouseDown = [this, id]() {
			if (UI::Context* c = Live()) {
				PressGrip(*c, id);
			}
		};
		gripEvents.onPressDrag = [this](Vec2 point) {
			if (UI::Context* c = Live()) {
				Travel(*c, point);
			}
		};
		gripEvents.onPressEnd = [this]() { Release(); };
		_notes.push_back(std::move(placed));
	}

	void GraphCanvas::StyleNote(UI::Context& context, const PlacedNote& note) {
		if (!context.IsAlive(note.element)) {
			return;
		}
		const f32 zoom = _view.zoom;
		const UI::Rect box = NoteBox(note);
		const bool dark = note.theme == GraphNoteTheme::Dark;
		UI::Style style = context.GetStyle(note.element);
		style.left = UI::Length::Pixel(box.x);
		style.top = UI::Length::Pixel(box.y);
		style.width = UI::Length::Pixel(box.w);
		style.height = UI::Length::Pixel(box.h);
		style.padding = {HEADER_INSET * zoom, HEADER_INSET * zoom * 0.5f, HEADER_INSET * zoom, HEADER_INSET * zoom * 0.5f};
		style.background = UI::Brush::SolidColor(EditorTheme::GetColor(dark ? ColorToken::GraphNoteDark : ColorToken::GraphNoteLight));
		style.borderColor = EditorTheme::GetColor(_selectedNotes.contains(note.id) ? ColorToken::SelectionWire : ColorToken::CardEdge);
		context.SetStyle(note.element, style);

		const Color ink = EditorTheme::GetColor(dark ? ColorToken::GraphNoteDarkText : ColorToken::GraphNoteLightText);
		const Handle<FontAsset> font = UISystem::GetDefaultFont();
		context.SetText(note.title, font, EditorTheme::GetMetric(NoteTitleSize(note.textSize)) * zoom, ink, note.titleText);
		context.SetText(note.body, font, EditorTheme::GetMetric(NoteBodySize(note.textSize)) * zoom, ink, note.bodyText);

		const f32 gripSize = GraphCanvasOps::NOTE_GRIP * zoom;
		UI::Style grip = context.GetStyle(note.grip);
		grip.left = UI::Length::Pixel(box.w - gripSize);
		grip.top = UI::Length::Pixel(box.h - gripSize);
		grip.width = UI::Length::Pixel(gripSize);
		grip.height = UI::Length::Pixel(gripSize);
		context.SetStyle(note.grip, grip);
	}

	void GraphCanvas::DropIntoGroup(UI::Context& context) {
		if (!_model || !_selectedGroups.empty()) {
			return;
		}
		Vector<GraphItem> items{GetMemoryResource(MemTag::Editor)};
		HashSet<u64> current{GetMemoryResource(MemTag::Editor)};
		auto groupOf = [&](const MoveStart& start) -> u64 {
			for (const PlacedGroup& group : _groups) {
				const bool held = start.kind == GraphItemKind::Node
									  ? std::find(group.nodes.begin(), group.nodes.end(), start.id) != group.nodes.end()
									  : std::find(group.notes.begin(), group.notes.end(), start.id) != group.notes.end();
				if (held) {
					return group.id;
				}
			}
			return 0;
		};
		Vector<GraphItem> grouped{GetMemoryResource(MemTag::Editor)};
		for (const MoveStart& start : _moveStarts) {
			if (start.kind != GraphItemKind::Node && start.kind != GraphItemKind::Note) {
				continue;
			}
			const GraphItem item{start.kind, start.id};
			items.push_back(item);
			if (const u64 group = groupOf(start)) {
				current.insert(group);
				grouped.push_back(item);
			}
		}
		if (items.empty()) {
			return;
		}
		if (_pressShift) {
			if (!grouped.empty()) {
				_model->SetGroupOf(grouped, 0);
			}
			return;
		}
		const u64 target = GroupAt(PointerLocal(context), current);
		if (target == 0) {
			return;
		}
		_model->SetGroupOf(items, target);
	}

	u64 GraphCanvas::GroupAt(Vec2 local, const HashSet<u64>& except) const {
		for (size_t index = _groups.size(); index-- > 0;) {
			const PlacedGroup& group = _groups[index];
			if (!except.contains(group.id) && group.frame.Contains(local)) {
				return group.id;
			}
		}
		return 0;
	}

	void GraphCanvas::OpenTitleEditor(UI::Context& context, const UI::Rect& bar, const str& text, f32 fontSizePx,
									  std::function<void(std::string_view)> commit) {
		CloseEditor();
		UI::Widgets::TextFieldDesc desc;
		desc.text = text;
		desc.font = UISystem::GetDefaultFont();
		desc.fontSizePx = fontSizePx;
		desc.onCommit = [this, commit = std::move(commit)](std::string_view title) {
			if (_model && commit) {
				commit(title);
			}
			if (UI::Context* c = Live()) {
				c->SetFocus(_canvas);
			}
		};
		_editor = UI::Widgets::TextField(context, _groupHost, desc);
		UI::Style style = context.GetStyle(_editor);
		style.position = UI::Position::Absolute;
		style.left = UI::Length::Pixel(bar.x);
		style.top = UI::Length::Pixel(bar.y);
		style.width = UI::Length::Pixel(bar.w);
		style.height = UI::Length::Pixel(bar.h);
		context.SetStyle(_editor, style);
		UI::ElementEvents& events = context.Events(_editor);
		std::function<void()> commitOnBlur = events.onFocusLost;
		events.onFocusLost = [this, commitOnBlur]() {
			if (commitOnBlur) {
				commitOnBlur();
			}
			CloseEditor();
		};
		context.SetFocus(_editor);
	}

	void GraphCanvas::BeginGroupRename(UI::Context& context, u64 group) {
		const PlacedGroup* placed = FindGroup(group);
		if (!placed || !_model) {
			return;
		}
		OpenTitleEditor(context, GraphCanvasOps::GroupTitleBar(placed->frame, _view.zoom), placed->text, EditorTheme::GetMetric(MetricToken::FontSmall),
						[this, group](std::string_view title) { _model->RenameGroup(group, title); });
	}

	void GraphCanvas::BeginOutlineRename(UI::Context& context, u64 outline) {
		const auto placed = std::find_if(_titledOutlines.begin(), _titledOutlines.end(), [outline](const PlacedOutline& held) { return held.id == outline; });
		if (placed == _titledOutlines.end() || !_model) {
			return;
		}
		OpenTitleEditor(context, placed->bar, placed->text, EditorTheme::GetMetric(MetricToken::FontBody),
						[this, outline](std::string_view title) { _model->RenameOutline(outline, title); });
	}

	void GraphCanvas::BeginNoteEdit(UI::Context& context, u64 note, bool body) {
		const PlacedNote* placed = FindNote(note);
		if (!placed || !_model) {
			return;
		}
		CloseEditor();
		const UI::ElementHandle over = body ? placed->body : placed->title;
		const UI::Rect noteBox = context.GetLayout(placed->element).borderBox;
		const UI::Rect textBox = context.GetLayout(over).borderBox;
		const f32 scale = context.GetContentScale();
		UI::Widgets::TextFieldDesc desc;
		desc.text = body ? placed->bodyText : placed->titleText;
		desc.font = UISystem::GetDefaultFont();
		desc.fontSizePx = EditorTheme::GetMetric(body ? NoteBodySize(placed->textSize) : NoteTitleSize(placed->textSize)) * _view.zoom;
		desc.multiline = body;
		const str keepTitle = placed->titleText;
		const str keepBody = placed->bodyText;
		desc.onCommit = [this, note, body, keepTitle, keepBody](std::string_view text) {
			if (_model) {
				_model->EditNote(note, body ? std::string_view(keepTitle) : text, body ? text : std::string_view(keepBody));
			}
			if (!body) {
				if (UI::Context* c = Live()) {
					c->SetFocus(_canvas);
				}
			}
		};
		_editor = UI::Widgets::TextField(context, placed->element, desc);
		UI::Style style = context.GetStyle(_editor);
		style.position = UI::Position::Absolute;
		style.left = UI::Length::Pixel((textBox.x - noteBox.x) / scale);
		style.top = UI::Length::Pixel((textBox.y - noteBox.y) / scale);
		style.width = UI::Length::Pixel(Math::Max(textBox.w, 1.0f) / scale);
		if (body) {
			style.height = UI::Length::Pixel(Math::Max(noteBox.y + noteBox.h - textBox.y, 1.0f) / scale);
		}
		context.SetStyle(_editor, style);
		UI::ElementEvents& events = context.Events(_editor);
		std::function<void()> commitOnBlur = events.onFocusLost;
		events.onFocusLost = [this, commitOnBlur]() {
			if (commitOnBlur) {
				commitOnBlur();
			}
			CloseEditor();
		};
		context.SetFocus(_editor);
	}

	void GraphCanvas::CloseEditor() {
		if (_context && _editor.IsValid()) {
			const bool live = !UISystem::IsInitialized() || UISystem::IsContextLive(_context);
			if (live && _context->IsAlive(_editor)) {
				_context->DestroyElementNextUpdate(_editor);
			}
		}
		_editor = {};
	}

	bool GraphCanvas::OnKey(UI::Context& context, Key key, KeyMod mods) {
		if (!_model || _press != Press::None || _portDragging) {
			return false;
		}
		if (mods == KeyMod::Ctrl) {
			if (key == Key::G) {
				return _model->OnGraphCommand(GraphCommand::Group);
			}
			if (key == Key::U) {
				return _model->OnGraphCommand(GraphCommand::Ungroup);
			}
			if (key == Key::C) {
				return _model->OnGraphCommand(GraphCommand::Copy);
			}
			if (key == Key::X) {
				return _model->OnGraphCommand(GraphCommand::Cut);
			}
			if (key == Key::V) {
				return _model->OnGraphCommand(GraphCommand::Paste);
			}
			return false;
		}
		if (mods != KeyMod::None) {
			return false;
		}
		switch (key) {
		case Key::F:
			FrameSelection(context);
			return true;
		case Key::A:
			FrameAll(context);
			return true;
		case Key::Space: {
			const UI::Rect content = context.GetLayout(_canvas).content;
			Vec2 anchor = context.GetPointerPosition();
			const bool inside = anchor.x >= content.x && anchor.y >= content.y && anchor.x < content.x + content.w &&
								anchor.y < content.y + content.h;
			if (!inside) {
				anchor = Vec2(content.x + content.w * 0.5f, content.y + content.h * 0.5f);
			}
			const Vec2 local = ToLocal(context, anchor);
			if (const PlacedNode* stack = inside ? StackAt(local) : nullptr) {
				if (OpenBlockSearcher(context, anchor, stack->id, GapAt(*stack, local))) {
					return true;
				}
			}
			return OpenSearcher(context, anchor, GraphCanvasOps::LocalToGraph(_view, local), nullptr);
		}
		default:
			return false;
		}
	}

	bool GraphCanvas::OpenSearcherAt(UI::Context& context, Vec2 anchor) {
		return OpenSearcher(context, anchor, GraphCanvasOps::LocalToGraph(_view, ToLocal(context, anchor)), nullptr);
	}

	bool GraphCanvas::OpenBlockSearcherAt(UI::Context& context, GraphNodeId stack, Vec2 anchor) {
		const PlacedNode* node = FindNode(stack);
		if (!node || !node->stack) {
			return false;
		}
		return OpenBlockSearcher(context, anchor, stack, GapAt(*node, ToLocal(context, anchor)));
	}

	bool GraphCanvas::OpenBlockSearcher(UI::Context& context, Vec2 anchor, GraphNodeId stack, u32 gap) {
		if (!_model) {
			return false;
		}
		GraphSearcher::Desc desc;
		_model->GetBlockSearchEntries(stack, desc.library);
		if (desc.library.empty()) {
			return false;
		}
		CloseSearcher();
		desc.at = anchor;
		desc.onPick = [this, stack, gap](const GraphSearchEntry& entry) {
			_searcher = {};
			if (_model) {
				_model->OnBlockSearchPick(entry, stack, gap);
			}
		};
		_searcher = GraphSearcher::Open(context, std::move(desc));
		return true;
	}

	bool GraphCanvas::OpenSearcher(UI::Context& context, Vec2 anchor, Vec2 graph, const GraphPortRef* from) {
		if (!_model) {
			return false;
		}
		GraphSearcher::Desc desc;
		_model->GetSearchEntries(from, desc.library);
		if (desc.library.empty()) {
			return false;
		}
		CloseSearcher();
		desc.at = anchor;
		const bool wire = from != nullptr;
		const GraphPortRef port = wire ? *from : GraphPortRef{};
		desc.onPick = [this, graph, wire, port](const GraphSearchEntry& entry) {
			_searcher = {};
			if (_model) {
				_model->OnSearchPick(entry, graph, wire ? &port : nullptr);
			}
		};
		_searcher = GraphSearcher::Open(context, std::move(desc));
		return true;
	}

	void GraphCanvas::CloseSearcher() {
		if (!_context || !_searcher.IsValid()) {
			return;
		}
		const bool live = !UISystem::IsInitialized() || UISystem::IsContextLive(_context);
		if (live && _context->IsPopupOpen(_searcher)) {
			_context->ClosePopup(_searcher);
		}
		_searcher = {};
	}

	bool GraphCanvas::IsSelected(const GraphItem& item) const {
		switch (item.kind) {
		case GraphItemKind::Node:
			return _selectedNodes.contains(item.id);
		case GraphItemKind::Wire:
			return _selectedWires.contains(item.id);
		case GraphItemKind::Group:
			return _selectedGroups.contains(item.id);
		case GraphItemKind::Note:
			return _selectedNotes.contains(item.id);
		case GraphItemKind::Block:
			return _selectedBlocks.contains(item.id);
		}
		return false;
	}

	void GraphCanvas::SetSelection(std::span<const GraphItem> items) {
		SetSelectionTo(items);
	}

	void GraphCanvas::SelectAll() {
		Vector<GraphItem> all{GetMemoryResource(MemTag::Editor)};
		all.reserve(_nodes.size());
		for (const PlacedNode& node : _nodes) {
			all.push_back(GraphItem{GraphItemKind::Node, node.id});
		}
		SetSelectionTo(all);
	}

	bool GraphCanvas::SetSelectionTo(std::span<const GraphItem> items) {
		if (items.size() == _selection.size() && std::equal(items.begin(), items.end(), _selection.begin())) {
			return false;
		}
		_selection.clear();
		_selectedNodes.clear();
		_selectedWires.clear();
		_selectedGroups.clear();
		_selectedNotes.clear();
		_selectedBlocks.clear();
		for (const GraphItem& item : items) {
			bool fresh = false;
			switch (item.kind) {
			case GraphItemKind::Node:
				fresh = _selectedNodes.insert(item.id).second;
				break;
			case GraphItemKind::Wire:
				fresh = _selectedWires.insert(item.id).second;
				break;
			case GraphItemKind::Group:
				fresh = _selectedGroups.insert(item.id).second;
				break;
			case GraphItemKind::Note:
				fresh = _selectedNotes.insert(item.id).second;
				break;
			case GraphItemKind::Block:
				fresh = _selectedBlocks.insert(item.id).second;
				break;
			}
			if (fresh) {
				_selection.push_back(item);
			}
		}
		if (UI::Context* context = Live()) {
			PlaceNodes(*context);
		}
		return true;
	}

	void GraphCanvas::Notify() {
		if (_model) {
			_model->OnSelectionChanged();
		}
	}

	bool GraphCanvas::PressItem(const GraphItem& item, KeyMod mods) {
		const bool ctrl = (mods & KeyMod::Ctrl) != KeyMod::None;
		Vector<GraphItem> next{GetMemoryResource(MemTag::Editor)};
		if (IsSelected(item)) {
			if (!ctrl) {
				return true;
			}
			for (const GraphItem& held : _selection) {
				if (!(held == item)) {
					next.push_back(held);
				}
			}
			if (SetSelectionTo(next)) {
				Notify();
			}
			return false;
		}
		if (ctrl) {
			next.assign(_selection.begin(), _selection.end());
		}
		next.push_back(item);
		if (SetSelectionTo(next)) {
			Notify();
		}
		return true;
	}

	void GraphCanvas::PressNode(UI::Context& context, GraphNodeId node) {
		PressItemForMove(context, GraphItem{GraphItemKind::Node, node});
	}

	void GraphCanvas::PressItemForMove(UI::Context& context, const GraphItem& item) {
		_press = Press::None;
		_pressMoved = false;
		_pressAt = context.GetPointerPosition();
		if (!_model) {
			return;
		}
		GraphNodeId pending = 0;
		if (_model->GetPendingWire(pending)) {
			return;
		}
		const KeyMod mods = context.GetPressMods();
		const bool selected = PressItem(item, mods);
		if ((mods & KeyMod::Alt) != KeyMod::None) {
			_press = Press::Pan;
			_panAnchor = _pressAt;
			return;
		}
		if (!selected) {
			return;
		}
		_press = Press::Move;
		_pressShift = (mods & KeyMod::Shift) != KeyMod::None;
		_moveAnchor = item;
		_pressGraph = GraphCanvasOps::LocalToGraph(_view, ToLocal(context, _pressAt));
		GatherMoveStarts(context);
	}

	void GraphCanvas::GatherMoveStarts(UI::Context& context) {
		(void)context;
		_moveStarts.clear();
		HashSet<u64> nodes{GetMemoryResource(MemTag::Editor)};
		HashSet<u64> notes{GetMemoryResource(MemTag::Editor)};
		auto addNode = [&](GraphNodeId id) {
			Vec2 at{};
			if (nodes.insert(id).second && _model->GetNodePosition(id, at)) {
				_moveStarts.push_back(MoveStart{GraphItemKind::Node, id, at});
			}
		};
		auto addNote = [&](u64 id) {
			const PlacedNote* note = FindNote(id);
			if (note && notes.insert(id).second) {
				_moveStarts.push_back(MoveStart{GraphItemKind::Note, id, note->position});
			}
		};
		for (const GraphItem& item : _selection) {
			switch (item.kind) {
			case GraphItemKind::Node:
				addNode(item.id);
				break;
			case GraphItemKind::Note:
				addNote(item.id);
				break;
			case GraphItemKind::Group:
				if (const PlacedGroup* group = FindGroup(item.id)) {
					for (const GraphNodeId member : group->nodes) {
						addNode(member);
					}
					for (const u64 member : group->notes) {
						addNote(member);
					}
					if (group->nodes.empty() && group->notes.empty()) {
						_moveStarts.push_back(MoveStart{GraphItemKind::Group, item.id, group->position});
					}
				}
				break;
			case GraphItemKind::Wire:
			case GraphItemKind::Block:
				break;
			}
		}
	}

	void GraphCanvas::PressGrip(UI::Context& context, u64 note) {
		_press = Press::None;
		_pressMoved = false;
		_pressAt = context.GetPointerPosition();
		const PlacedNote* placed = FindNote(note);
		if (!_model || !placed) {
			return;
		}
		_press = Press::Resize;
		_resizing = note;
		_resizeStart = placed->size;
		_pressGraph = GraphCanvasOps::LocalToGraph(_view, ToLocal(context, _pressAt));
	}

	void GraphCanvas::PressBlock(UI::Context& context, GraphNodeId block) {
		_press = Press::None;
		_pressMoved = false;
		_pressAt = context.GetPointerPosition();
		_blockDrag.clear();
		_dropStack = 0;
		if (!_model) {
			return;
		}
		GraphNodeId pending = 0;
		if (_model->GetPendingWire(pending)) {
			return;
		}
		const KeyMod mods = context.GetPressMods();
		const bool selected = PressItem(GraphItem{GraphItemKind::Block, block}, mods);
		if ((mods & KeyMod::Alt) != KeyMod::None) {
			_press = Press::Pan;
			_panAnchor = _pressAt;
			return;
		}
		if (!selected) {
			return;
		}
		_press = Press::Block;
		for (const PlacedNode& node : _nodes) {
			for (const PlacedBlock& held : node.blocks) {
				if (_selectedBlocks.contains(held.id)) {
					_blockDrag.push_back(held.id);
				}
			}
		}
	}

	void GraphCanvas::TrackBlockDrop(UI::Context& context, Vec2 point) {
		_dropStack = 0;
		const Vec2 local = ToLocal(context, point);
		const PlacedNode* stack = StackAt(local);
		if (!stack || _blockDrag.empty() || !_model->CanMoveBlocks(_blockDrag, stack->id)) {
			return;
		}
		_dropStack = stack->id;
		_dropGap = GapAt(*stack, local);
	}

	void GraphCanvas::PressGrid(UI::Context& context) {
		_press = Press::None;
		_pressMoved = false;
		_pressAt = context.GetPointerPosition();
		if (!_model) {
			return;
		}
		const KeyMod mods = context.GetPressMods();
		if ((mods & KeyMod::Alt) != KeyMod::None) {
			_press = Press::Pan;
			_panAnchor = _pressAt;
			return;
		}
		GraphNodeId pending = 0;
		if (_model->GetPendingWire(pending)) {
			return;
		}
		const Vec2 local = ToLocal(context, _pressAt);
		GraphWireId wire = 0;
		if (PickWire(local, context.GetContentScale(), wire)) {
			PressItem(GraphItem{GraphItemKind::Wire, wire}, mods);
			return;
		}
		const bool ctrl = (mods & KeyMod::Ctrl) != KeyMod::None;
		_press = Press::Marquee;
		_marqueeToggles = ctrl;
		_marqueeFrom = local;
		_marqueeTo = local;
		if (!ctrl && SetSelectionTo({})) {
			Notify();
		}
	}

	void GraphCanvas::Travel(UI::Context& context, Vec2 point) {
		if (!_pressMoved && point.x == _pressAt.x && point.y == _pressAt.y) {
			return;
		}
		_pressMoved = true;
		switch (_press) {
		case Press::Pan:
			_view.pan = _view.pan + (point - _panAnchor) / context.GetContentScale();
			_panAnchor = point;
			PlaceNodes(context);
			break;
		case Press::Marquee:
			_marqueeTo = ToLocal(context, point);
			break;
		case Press::Move: {
			if (!_model || _moveStarts.empty()) {
				break;
			}
			if (!_moving) {
				_moving = true;
				_model->OnMoveBegin();
			}
			Vec2 delta = GraphCanvasOps::LocalToGraph(_view, ToLocal(context, point)) - _pressGraph;
			if ((context.GetPointerMods() & KeyMod::Ctrl) != KeyMod::None) {
				const auto anchor = std::find_if(_moveStarts.begin(), _moveStarts.end(),
												 [&](const MoveStart& start) { return start.kind == _moveAnchor.kind && start.id == _moveAnchor.id; });
				const Vec2 from = anchor != _moveStarts.end() ? anchor->at : _moveStarts.front().at;
				const f32 step = GridStep();
				const Vec2 to = from + delta;
				delta = Vec2(std::round(to.x / step) * step, std::round(to.y / step) * step) - from;
			}
			for (const MoveStart& start : _moveStarts) {
				const Vec2 to = start.at + delta;
				switch (start.kind) {
				case GraphItemKind::Node:
					_model->MoveNode(start.id, to);
					if (const PlacedNode* moved = FindNode(start.id)) {
						StyleNode(context, *moved, false);
					}
					break;
				case GraphItemKind::Note:
					_model->MoveNote(start.id, to);
					if (PlacedNote* moved = FindNote(start.id)) {
						moved->position = to;
						StyleNote(context, *moved);
					}
					break;
				case GraphItemKind::Group:
					_model->MoveGroup(start.id, to);
					if (PlacedGroup* moved = FindGroup(start.id)) {
						moved->position = to;
					}
					break;
				case GraphItemKind::Wire:
				case GraphItemKind::Block:
					break;
				}
			}
			PlaceGroups(context);
			break;
		}
		case Press::Resize: {
			PlacedNote* note = FindNote(_resizing);
			if (!_model || !note) {
				break;
			}
			if (!_moving) {
				_moving = true;
				_model->OnMoveBegin();
			}
			const Vec2 delta = GraphCanvasOps::LocalToGraph(_view, ToLocal(context, point)) - _pressGraph;
			note->size = Vec2(Math::Max(_resizeStart.x + delta.x, GraphCanvasOps::NOTE_MIN_WIDTH),
							  Math::Max(_resizeStart.y + delta.y, GraphCanvasOps::NOTE_MIN_HEIGHT));
			_model->ResizeNote(note->id, note->size);
			StyleNote(context, *note);
			PlaceGroups(context);
			break;
		}
		case Press::Block:
			if (_model) {
				TrackBlockDrop(context, point);
			}
			break;
		case Press::None:
			break;
		}
	}

	void GraphCanvas::Release() {
		const Press press = _press;
		_press = Press::None;
		if (press == Press::Marquee && _pressMoved) {
			if (UI::Context* context = Live()) {
				EndMarquee(*context);
			}
		}
		if (press == Press::Block && _pressMoved && _dropStack != 0 && _model) {
			const Vector<GraphNodeId> moved = _blockDrag;
			const GraphNodeId stack = _dropStack;
			const u32 gap = _dropGap;
			_blockDrag.clear();
			_dropStack = 0;
			_model->MoveBlocks(moved, stack, gap);
		}
		_blockDrag.clear();
		_dropStack = 0;
		if (press == Press::Move && _moving) {
			if (UI::Context* context = Live()) {
				DropIntoGroup(*context);
			}
		}
		EndMove();
		_moveStarts.clear();
		_resizing = 0;
		_pressShift = false;
	}

	void GraphCanvas::EndMove() {
		if (!_moving) {
			return;
		}
		_moving = false;
		if (_model) {
			_model->OnMoveEnd();
		}
	}

	void GraphCanvas::EndMarquee(UI::Context& context) {
		if (!_model) {
			return;
		}
		const UI::Rect box = GraphCanvasOps::RectBetween(_marqueeFrom, _marqueeTo);
		Vector<GraphItem> covered{GetMemoryResource(MemTag::Editor)};

		const f32 scale = context.GetContentScale();
		_wires.clear();
		_model->GetWires(_wires);
		for (const GraphWireDesc& wire : _wires) {
			if (wire.id == 0) {
				continue;
			}
			bool touches = false;
			if (wire.style == GraphWireStyle::Arrow) {
				Vec2 from{};
				Vec2 to{};
				touches = ArrowEnds(wire, from, to) && GraphCanvasOps::SegmentTouchesRect(from, to, box);
			} else {
				GraphCubic curve{};
				if (CurveOf(wire, curve)) {
					GraphCanvasOps::Tessellate(curve, GraphCanvasOps::SegmentCount(curve, scale), _points);
					for (size_t index = 1; index < _points.size() && !touches; ++index) {
						touches = GraphCanvasOps::SegmentTouchesRect(_points[index - 1], _points[index], box);
					}
				}
			}
			if (touches) {
				covered.push_back(GraphItem{GraphItemKind::Wire, wire.id});
			}
		}
		for (const PlacedGroup& group : _groups) {
			if (GraphCanvasOps::RectsOverlap(GraphCanvasOps::GroupTitleBar(group.frame, _view.zoom), box)) {
				covered.push_back(GraphItem{GraphItemKind::Group, group.id});
			}
		}
		for (const PlacedNote& note : _notes) {
			if (GraphCanvasOps::RectsOverlap(NoteBox(note), box)) {
				covered.push_back(GraphItem{GraphItemKind::Note, note.id});
			}
		}
		for (const PlacedNode& node : _nodes) {
			UI::Rect nodeBox{};
			if (NodeBox(node, nodeBox) && GraphCanvasOps::RectsOverlap(nodeBox, box)) {
				covered.push_back(GraphItem{GraphItemKind::Node, node.id});
			}
		}

		Vector<GraphItem> next(_selection.begin(), _selection.end(), GetMemoryResource(MemTag::Editor));
		for (const GraphItem& item : covered) {
			if (!IsSelected(item)) {
				next.push_back(item);
			} else if (_marqueeToggles) {
				std::erase(next, item);
			}
		}
		if (SetSelectionTo(next)) {
			Notify();
		}
	}

	void GraphCanvas::Paint(UI::Context& context, UI::ElementHandle element, UI::PaintScope& paint) {
		const UI::Rect content = context.GetLayout(element).content;
		if (content.w <= 2.0f || content.h <= 2.0f) {
			return;
		}
		const f32 scale = context.GetContentScale();
		auto toPaint = [&](Vec2 local) { return Vec2(content.x + local.x * scale, content.y + local.y * scale); };

		const f32 step = GridStep() * _view.zoom * scale;
		if (step >= 4.0f) {
			const Color minor = EditorTheme::GetColor(ColorToken::GraphGrid);
			const Color major = EditorTheme::GetColor(ColorToken::GraphGridMajor);
			const Vec2 origin = toPaint(GraphCanvasOps::GraphToLocal(_view, Vec2{}));
			const i32 firstX = static_cast<i32>(std::floor((content.x - origin.x) / step));
			const i32 lastX = static_cast<i32>(std::ceil((content.x + content.w - origin.x) / step));
			for (i32 index = firstX; index <= lastX; ++index) {
				const f32 at = origin.x + static_cast<f32>(index) * step;
				paint.EmitQuad({at, content.y, 1.0f, content.h}, (index % GRID_MAJOR_EVERY) == 0 ? major : minor);
			}
			const i32 firstY = static_cast<i32>(std::floor((content.y - origin.y) / step));
			const i32 lastY = static_cast<i32>(std::ceil((content.y + content.h - origin.y) / step));
			for (i32 index = firstY; index <= lastY; ++index) {
				const f32 at = origin.y + static_cast<f32>(index) * step;
				paint.EmitQuad({content.x, at, content.w, 1.0f}, (index % GRID_MAJOR_EVERY) == 0 ? major : minor);
			}
		}

		if (!_model) {
			return;
		}

		const f32 width = Math::Max(1.0f, WIRE_WIDTH * _view.zoom * scale);
		const Color selection = EditorTheme::GetColor(ColorToken::SelectionWire);

		const Color groupFill = EditorTheme::GetColor(ColorToken::GraphGroup);
		const f32 edge = Math::Max(1.0f, std::floor(scale));
		for (const PlacedGroup& group : _groups) {
			const UI::Rect frame{content.x + group.frame.x * scale, content.y + group.frame.y * scale, group.frame.w * scale,
								 group.frame.h * scale};
			paint.EmitQuad(frame, groupFill);
			const Color edgeColor = _selectedGroups.contains(group.id) ? selection : EditorTheme::GetColor(ColorToken::GraphGroupEdge);
			paint.EmitQuad({frame.x, frame.y, frame.w, edge}, edgeColor);
			paint.EmitQuad({frame.x, frame.y + frame.h - edge, frame.w, edge}, edgeColor);
			paint.EmitQuad({frame.x, frame.y + edge, edge, Math::Max(0.0f, frame.h - edge * 2.0f)}, edgeColor);
			paint.EmitQuad({frame.x + frame.w - edge, frame.y + edge, edge, Math::Max(0.0f, frame.h - edge * 2.0f)}, edgeColor);
		}

		_outlines.clear();
		_model->GetOutlines(_outlines);
		const Color outlineColor = EditorTheme::GetColor(ColorToken::GraphOutline);
		const f32 dash = DASH_LENGTH * _view.zoom * scale;
		const f32 gap = DASH_GAP * _view.zoom * scale;
		auto dashes = [&](Vec2 from, Vec2 to) {
			const Vec2 span = to - from;
			const f32 length = span.Length();
			if (length < 1e-3f || dash < 1.0f) {
				return;
			}
			const Vec2 along = span / length;
			for (f32 at = 0.0f; at < length; at += dash + gap) {
				const Vec2 start = from + along * at;
				const Vec2 end = from + along * Math::Min(at + dash, length);
				paint.EmitQuad({Math::Min(start.x, end.x), Math::Min(start.y, end.y), Math::Max(Math::Abs(end.x - start.x), edge),
								Math::Max(Math::Abs(end.y - start.y), edge)},
							   outlineColor);
			}
		};
		for (const GraphOutlineDesc& outline : _outlines) {
			Vector<UI::Rect> boxes{GetMemoryResource(MemTag::Editor)};
			for (const GraphNodeId member : outline.nodes) {
				UI::Rect box{};
				const PlacedNode* node = FindNode(member);
				if (node && NodeBox(*node, box)) {
					boxes.push_back(box);
				}
			}
			UI::Rect around{};
			if (!GraphCanvasOps::OutlineFrame(boxes, _view.zoom, around)) {
				continue;
			}
			const Vec2 a = toPaint(Vec2(around.x, around.y));
			const Vec2 b = toPaint(Vec2(around.x + around.w, around.y + around.h));
			dashes(a, Vec2(b.x, a.y));
			dashes(Vec2(a.x, b.y), b);
			dashes(a, Vec2(a.x, b.y));
			dashes(Vec2(b.x, a.y), b);
		}

		auto paintCurve = [&](const GraphCubic& curve, Color color, f32 thickness) {
			GraphCanvasOps::Tessellate(curve, GraphCanvasOps::SegmentCount(curve, scale), _points);
			for (size_t index = 1; index < _points.size(); ++index) {
				paint.EmitLine(toPaint(_points[index - 1]), toPaint(_points[index]), thickness, color, UI::LineCap::Round);
			}
		};

		const Vec2 pointerLocal = PointerLocal(context);

		GraphNodeId pending = 0;
		const bool aiming = _model->GetPendingWire(pending);
		GraphWireId hovered = 0;
		if (_press == Press::None && !_portDragging && !aiming && context.GetHovered() == _canvas) {
			PickWire(pointerLocal, scale, hovered);
		}

		auto paintWire = [&](const GraphWireDesc& wire, Color color, f32 thickness) {
			if (wire.style != GraphWireStyle::Arrow) {
				GraphCubic curve{};
				if (CurveOf(wire, curve)) {
					paintCurve(curve, color, wire.style == GraphWireStyle::Flow ? thickness * FLOW_WIDTH_SCALE : thickness);
				}
				return;
			}
			Vec2 fromLocal{};
			Vec2 toLocal{};
			if (!ArrowEnds(wire, fromLocal, toLocal)) {
				return;
			}
			const Vec2 from = toPaint(fromLocal);
			const Vec2 to = toPaint(toLocal);
			paint.EmitLine(from, to, thickness, color, UI::LineCap::Butt);

			const Vec2 span = to - from;
			const f32 length = span.Length();
			if (length < 1e-3f) {
				return;
			}
			const Vec2 direction = span / length;
			const Vec2 perpendicular(-direction.y, direction.x);
			const Vec2 tip = from + direction * (length * 0.5f + ARROW_LENGTH * 0.5f * _view.zoom * scale);
			const Vec2 back = tip - direction * (ARROW_LENGTH * _view.zoom * scale);
			const Vec2 wing = perpendicular * (ARROW_LENGTH * 0.55f * _view.zoom * scale);
			paint.EmitLine(back + wing, tip, thickness, color, UI::LineCap::Round);
			paint.EmitLine(back - wing, tip, thickness, color, UI::LineCap::Round);
		};

		_wires.clear();
		_model->GetWires(_wires);
		const GraphWireDesc* lit = nullptr;
		for (const GraphWireDesc& wire : _wires) {
			if (hovered != 0 && wire.id == hovered) {
				lit = &wire;
				continue;
			}
			paintWire(wire, wire.id != 0 && _selectedWires.contains(wire.id) ? selection : EditorTheme::GetColor(wire.color), width);
		}
		if (lit) {
			paintWire(*lit, selection, width * WIRE_HOVER_SCALE);
		}

		if (aiming) {
			UI::Rect box{};
			const PlacedNode* source = FindNode(pending);
			if (source && NodeBox(*source, box)) {
				paint.EmitLine(toPaint(GraphCanvasOps::ClipToBox(box, pointerLocal)), toPaint(pointerLocal), width, selection,
							   UI::LineCap::Round);
			}
		}

		if (_portDragging) {
			Vec2 anchor{};
			if (PortAt(_portDrag, anchor)) {
				Vec2 end = pointerLocal;
				GraphPortRef target{};
				bool accepted = false;
				Vec2 targetAnchor{};
				if (PortDragTarget(_portDrag, pointerLocal, target, accepted) && accepted && PortAt(target, targetAnchor)) {
					end = targetAnchor;
				}
				switch (_portDrag.side) {
				case GraphPortSide::Output:
					paintCurve(GraphCanvasOps::PortWire(anchor, end, _view.zoom), selection, width);
					break;
				case GraphPortSide::Input:
					paintCurve(GraphCanvasOps::PortWire(end, anchor, _view.zoom), selection, width);
					break;
				case GraphPortSide::FlowOutput:
					paintCurve(GraphCanvasOps::FlowWire(anchor, end, _view.zoom), selection, width * FLOW_WIDTH_SCALE);
					break;
				case GraphPortSide::FlowInput:
					paintCurve(GraphCanvasOps::FlowWire(end, anchor, _view.zoom), selection, width * FLOW_WIDTH_SCALE);
					break;
				}
			}
		}
	}

	void GraphCanvas::PaintOverlay(UI::Context& context, UI::ElementHandle element, UI::PaintScope& paint) {
		(void)element;
		if (!context.IsAlive(_canvas)) {
			return;
		}

		auto light = [&](UI::ElementHandle dot) {
			if (!dot.IsValid() || !context.IsAlive(dot)) {
				return;
			}
			const UI::Rect box = context.GetLayout(dot).borderBox;
			if (box.w > 0.0f && box.h > 0.0f) {
				paint.EmitQuad(box, EditorTheme::GetColor(ColorToken::SelectionWire), UI::Radii::All(Math::Min(box.w, box.h) * 0.5f));
			}
		};
		if (_portDragging) {
			light(DotOf(_portDrag));
			GraphPortRef target{};
			bool accepted = false;
			if (PortDragTarget(_portDrag, PointerLocal(context), target, accepted) && accepted) {
				light(DotOf(target));
			}
		} else if (GraphNodeId pending = 0; _press == Press::None && !(_model && _model->GetPendingWire(pending))) {
			const UI::ElementHandle hovered = context.GetHovered();
			if (IsPortDot(hovered)) {
				light(hovered);
			}
		}

		if (!_pressMoved) {
			return;
		}
		const UI::Rect content = context.GetLayout(_canvas).content;
		const f32 scale = context.GetContentScale();
		if (_press == Press::Block) {
			const PlacedNode* stack = _dropStack != 0 ? FindNode(_dropStack) : nullptr;
			UI::Rect box{};
			if (!stack || !NodeBox(*stack, box)) {
				return;
			}
			const f32 pad = GraphCanvasOps::STACK_PADDING * _view.zoom;
			const f32 y = box.y + (stack->layout.stack + GraphCanvasOps::StackGapLine(stack->blockHeights, _dropGap)) * _view.zoom;
			const f32 thickness = Math::Max(2.0f, std::floor(2.0f * scale));
			paint.EmitQuad({content.x + (box.x + pad) * scale, content.y + y * scale - thickness * 0.5f, (box.w - pad * 2.0f) * scale, thickness},
						   EditorTheme::GetColor(ColorToken::SelectionWire));
			return;
		}
		if (_press != Press::Marquee) {
			return;
		}
		const UI::Rect box = GraphCanvasOps::RectBetween(_marqueeFrom, _marqueeTo);
		const UI::Rect onScreen{content.x + box.x * scale, content.y + box.y * scale, box.w * scale, box.h * scale};
		paint.EmitQuad(onScreen, EditorTheme::GetColor(ColorToken::GraphMarquee));
		const f32 edge = Math::Max(1.0f, std::floor(scale));
		const Color edgeColor = EditorTheme::GetColor(ColorToken::GraphMarqueeEdge);
		paint.EmitQuad({onScreen.x, onScreen.y, onScreen.w, edge}, edgeColor);
		paint.EmitQuad({onScreen.x, onScreen.y + onScreen.h - edge, onScreen.w, edge}, edgeColor);
		paint.EmitQuad({onScreen.x, onScreen.y + edge, edge, Math::Max(0.0f, onScreen.h - edge * 2.0f)}, edgeColor);
		paint.EmitQuad({onScreen.x + onScreen.w - edge, onScreen.y + edge, edge, Math::Max(0.0f, onScreen.h - edge * 2.0f)},
					   edgeColor);
	}

} // namespace Shalltear
