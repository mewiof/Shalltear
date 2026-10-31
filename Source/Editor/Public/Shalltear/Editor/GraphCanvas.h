#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Input/InputCodes.h"
#include "Shalltear/Core/Math/AnimationCurve.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"

#include "Shalltear/Editor/EditorAPI.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/GraphCanvasOps.h"
#include "Shalltear/Editor/GraphDocument.h"
#include "Shalltear/Editor/GraphSearch.h"

#include <span>

namespace Shalltear {

	using GraphNodeId = u64;
	using GraphWireId = u64;

	struct GraphPortRef {
		GraphNodeId node = 0;
		GraphPortSide side = GraphPortSide::Output;
		u32 port = 0;

		friend bool operator==(const GraphPortRef&, const GraphPortRef&) = default;
	};

	enum class GraphInlineKind : u8 {
		None,
		Numbers,
		Color,
		Toggle,
		Label,
		Curve,
		Gradient
	};

	struct GraphInlineValue {
		GraphInlineKind kind = GraphInlineKind::None;
		u8 count = 0;
		f32 values[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		str text;
		AnimationCurve curve{GetMemoryResource(MemTag::Editor)};
		Vector<Color> ramp{GetMemoryResource(MemTag::Editor)};
	};

	struct GraphPortDesc {
		str label;
		EditorTheme::ColorToken color = EditorTheme::ColorToken::GraphPortOther;
		bool connected = false;
		GraphInlineValue inlineValue;
		bool dim = false;
		str space;
	};

	enum class GraphSettingEdit : u8 {
		Menu,
		Text
	};

	struct GraphSettingDesc {
		str label;
		str value;
		GraphSettingEdit edit = GraphSettingEdit::Menu;
		str action;
	};

	struct GraphFlowPortDesc {
		str label;
		bool connected = false;
	};

	struct GraphBlockDesc {
		GraphNodeId id = 0;
		str label;
		bool enabled = true;
		Vector<GraphSettingDesc> settings{GetMemoryResource(MemTag::Editor)};
		Vector<GraphPortDesc> inputs{GetMemoryResource(MemTag::Editor)};
		Vector<GraphPortDesc> outputs{GetMemoryResource(MemTag::Editor)};
	};

	struct GraphOutlineDesc {
		Vector<GraphNodeId> nodes{GetMemoryResource(MemTag::Editor)};
		u64 id = 0;
		str title;
	};

	enum class GraphNodePreview : u8 {
		None,
		Expanded,
		Collapsed
	};

	struct GraphNodeDesc {
		GraphNodeId id = 0;
		Vec2 size{};
		str label;
		EditorTheme::ColorToken fill = EditorTheme::ColorToken::GraphNode;
		f32 radius = 0.0f;
		Vector<GraphPortDesc> inputs{GetMemoryResource(MemTag::Editor)};
		Vector<GraphPortDesc> outputs{GetMemoryResource(MemTag::Editor)};
		bool compact = false;
		GraphNodePreview preview = GraphNodePreview::None;

		Vector<GraphSettingDesc> settings{GetMemoryResource(MemTag::Editor)};
		bool stack = false;
		Vector<GraphBlockDesc> blocks{GetMemoryResource(MemTag::Editor)};
		Vector<GraphFlowPortDesc> flowInputs{GetMemoryResource(MemTag::Editor)};
		Vector<GraphFlowPortDesc> flowOutputs{GetMemoryResource(MemTag::Editor)};
	};

	enum class GraphWireStyle : u8 {
		Arrow,
		Curve,
		Flow
	};

	struct GraphWireDesc {
		GraphWireId id = 0;
		GraphWireStyle style = GraphWireStyle::Arrow;
		GraphPortRef from;
		GraphPortRef to;
		i32 fan = 0;
		EditorTheme::ColorToken color = EditorTheme::ColorToken::WidgetTextDim;
	};

	enum class GraphItemKind : u8 {
		Node,
		Wire,
		Group,
		Note,
		Block
	};

	struct GraphItem {
		GraphItemKind kind = GraphItemKind::Node;
		u64 id = 0;

		friend bool operator==(const GraphItem&, const GraphItem&) = default;
	};

	struct GraphGroupDesc {
		u64 id = 0;
		str title;
		Vector<GraphNodeId> nodes{GetMemoryResource(MemTag::Editor)};
		Vector<u64> notes{GetMemoryResource(MemTag::Editor)};
		Vec2 position{};
	};

	struct GraphNoteDesc {
		u64 id = 0;
		Vec2 position{};
		Vec2 size{};
		str title;
		str body;
		GraphNoteTheme theme = GraphNoteTheme::Light;
		GraphNoteSize textSize = GraphNoteSize::Medium;
	};

	enum class GraphCommand : u8 {
		Group,
		Ungroup,
		Copy,
		Cut,
		Paste
	};

	// never destroy canvas elements in a callback, drain in OnTick
	class SHALLTEAR_EDITOR_API GraphViewModel {
	public:
		virtual ~GraphViewModel() = default;

		virtual void GetNodes(Vector<GraphNodeDesc>& out) = 0;
		virtual bool GetNodePosition(GraphNodeId node, Vec2& out) const = 0;
		virtual void GetWires(Vector<GraphWireDesc>& out) const = 0;
		virtual EditorTheme::ColorToken GetNodeBorder(GraphNodeId node) const {
			(void)node;
			return EditorTheme::ColorToken::CardEdge;
		}
		virtual str GetNodeTooltip(GraphNodeId node) const {
			(void)node;
			return {};
		}
		virtual void PaintNode(UI::Context& context, UI::ElementHandle element, UI::PaintScope& paint, GraphNodeId node) {
			(void)context, (void)element, (void)paint, (void)node;
		}
		virtual void PaintNodePreview(UI::Context& context, UI::PaintScope& paint, GraphNodeId node, const UI::Rect& well) {
			(void)context, (void)paint, (void)node, (void)well;
		}

		virtual void SetNodePreviewExpanded(GraphNodeId node, bool expanded) { (void)node, (void)expanded; }
		virtual void SetInlineValue(const GraphPortRef& input, std::span<const f32> values) { (void)input, (void)values; }
		virtual void OnInlineEditBegin() {}
		virtual void OnInlineEditEnd() {}
		virtual void OpenInlineEditor(UI::Context& context, const GraphPortRef& input, UI::ElementHandle field) {
			(void)context, (void)input, (void)field;
		}

		virtual void OnSelectionChanged() {}

		virtual void OnMoveBegin() {}
		virtual void MoveNode(GraphNodeId node, Vec2 graph) = 0;
		virtual void OnMoveEnd() {}
		virtual void OnNodeClick(GraphNodeId node) { (void)node; }
		virtual void OnNodeDoubleClick(GraphNodeId node) { (void)node; }
		virtual void OnNodeContextMenu(UI::Context& context, GraphNodeId node) { (void)context, (void)node; }
		virtual bool AcceptsNodeDrop(GraphNodeId node, const UI::DragPayload& payload) const {
			(void)node, (void)payload;
			return false;
		}
		virtual void OnNodeDrop(GraphNodeId node, const UI::DragPayload& payload) { (void)node, (void)payload; }

		virtual void OnCanvasContextMenu(UI::Context& context, Vec2 graph) { (void)context, (void)graph; }
		virtual bool AcceptsDrop(const UI::DragPayload& payload) const {
			(void)payload;
			return false;
		}
		virtual void OnDrop(const UI::DragPayload& payload, Vec2 graph) { (void)payload, (void)graph; }

		virtual bool GetPendingWire(GraphNodeId& from) const {
			(void)from;
			return false;
		}
		virtual void CancelPendingWire() {}
		virtual bool CanConnect(const GraphPortRef& output, const GraphPortRef& input) const {
			(void)output, (void)input;
			return false;
		}
		virtual void Connect(const GraphPortRef& output, const GraphPortRef& input) { (void)output, (void)input; }

		virtual void GetSearchEntries(const GraphPortRef* from, Vector<GraphSearchEntry>& out) { (void)from, (void)out; }
		virtual void OnSearchPick(const GraphSearchEntry& entry, Vec2 graph, const GraphPortRef* from) {
			(void)entry, (void)graph, (void)from;
		}

		virtual void GetGroups(Vector<GraphGroupDesc>& out) const { (void)out; }
		virtual void GetNotes(Vector<GraphNoteDesc>& out) const { (void)out; }
		virtual void MoveNote(u64 note, Vec2 graph) { (void)note, (void)graph; }
		virtual void MoveGroup(u64 group, Vec2 graph) { (void)group, (void)graph; }
		virtual void ResizeNote(u64 note, Vec2 size) { (void)note, (void)size; }
		virtual void SetGroupOf(std::span<const GraphItem> items, u64 group) { (void)items, (void)group; }
		virtual void RenameGroup(u64 group, std::string_view title) { (void)group, (void)title; }
		virtual void EditNote(u64 note, std::string_view title, std::string_view body) {
			(void)note, (void)title, (void)body;
		}
		virtual void OnGroupContextMenu(UI::Context& context, u64 group) { (void)context, (void)group; }
		virtual void OnNoteContextMenu(UI::Context& context, u64 note) { (void)context, (void)note; }

		virtual void OnWireContextMenu(UI::Context& context, GraphWireId wire, Vec2 graph) {
			(void)context, (void)wire, (void)graph;
		}
		virtual void OnWireDoubleClick(GraphWireId wire, Vec2 graph) { (void)wire, (void)graph; }
		virtual bool OnGraphCommand(GraphCommand command) {
			(void)command;
			return false;
		}

		virtual void GetOutlines(Vector<GraphOutlineDesc>& out) const { (void)out; }
		virtual void RenameOutline(u64 outline, std::string_view title) { (void)outline, (void)title; }
		virtual void OnSettingClick(UI::Context& context, GraphNodeId owner, u32 setting) { (void)context, (void)owner, (void)setting; }
		virtual void SetSettingText(GraphNodeId owner, u32 setting, std::string_view text) { (void)owner, (void)setting, (void)text; }
		virtual void OnSettingAction(GraphNodeId owner, u32 setting) { (void)owner, (void)setting; }
		virtual void SetBlockEnabled(GraphNodeId block, bool enabled) { (void)block, (void)enabled; }
		virtual void OnPortSpaceClick(const GraphPortRef& input) { (void)input; }
		virtual void OnBlockContextMenu(UI::Context& context, GraphNodeId block) { (void)context, (void)block; }
		virtual bool CanMoveBlocks(std::span<const GraphNodeId> blocks, GraphNodeId stack) const {
			(void)blocks, (void)stack;
			return false;
		}
		virtual void MoveBlocks(std::span<const GraphNodeId> blocks, GraphNodeId stack, u32 index) { (void)blocks, (void)stack, (void)index; }
		virtual void GetBlockSearchEntries(GraphNodeId stack, Vector<GraphSearchEntry>& out) { (void)stack, (void)out; }
		virtual void OnBlockSearchPick(const GraphSearchEntry& entry, GraphNodeId stack, u32 index) { (void)entry, (void)stack, (void)index; }
	};

	struct GraphCanvasDesc {
		f32 minZoom = 0.25f;
		f32 maxZoom = 2.5f;
		bool fieldsInside = false;
	};

	class SHALLTEAR_EDITOR_API GraphCanvas {
	public:
		GraphCanvas() = default;
		~GraphCanvas();
		GraphCanvas(const GraphCanvas&) = delete;
		GraphCanvas& operator=(const GraphCanvas&) = delete;

		// model must outlive the canvas
		void Build(UI::Context& context, UI::ElementHandle parent, GraphViewModel* model, GraphCanvasDesc desc = {});

		// drain from OnTick, never from a callback
		void RebuildNodes(UI::Context& context);
		void PlaceNodes(UI::Context& context);
		bool FrameAll(UI::Context& context);
		void ResetView();
		bool OpenSearcherAt(UI::Context& context, Vec2 anchor);
		bool OpenBlockSearcherAt(UI::Context& context, GraphNodeId stack, Vec2 anchor);

		std::span<const GraphItem> GetSelection() const { return _selection; }
		bool IsSelected(const GraphItem& item) const;
		void SetSelection(std::span<const GraphItem> items);
		void SelectAll();

		const GraphView& GetView() const { return _view; }
		UI::ElementHandle GetElement() const { return _canvas; }
		Vec2 PointerLocal(UI::Context& context) const;
	private:
		struct PlacedPort {
			UI::ElementHandle dot;
			UI::ElementHandle label;
			EditorTheme::ColorToken color = EditorTheme::ColorToken::GraphPortOther;
			bool connected = false;
			bool dim = false;
			str text;
			UI::ElementHandle field;
			GraphInlineValue inlineValue;
			UI::ElementHandle space;
			str spaceText;
		};

		struct PlacedSetting {
			UI::ElementHandle element;
			UI::ElementHandle label;
			UI::ElementHandle value;
			UI::ElementHandle action;
			str labelText;
			str valueText;
			str actionText;
			GraphSettingEdit edit = GraphSettingEdit::Menu;
		};

		struct PlacedFlow {
			UI::ElementHandle dot;
			UI::ElementHandle label;
			str text;
			bool connected = false;
		};

		struct PlacedBlock {
			GraphNodeId id = 0;
			UI::ElementHandle element;
			UI::ElementHandle header;
			UI::ElementHandle check;
			str label;
			bool enabled = true;
			f32 height = 0.0f;
			Vector<PlacedSetting> settings{GetMemoryResource(MemTag::Editor)};
			Vector<PlacedPort> ports{GetMemoryResource(MemTag::Editor)};
			u32 inputs = 0;
		};

		struct PlacedNode {
			GraphNodeId id = 0;
			UI::ElementHandle layer;
			UI::ElementHandle element;
			UI::ElementHandle header;
			Vec2 size{};
			f32 radius = 0.0f;
			str label;
			u32 inputs = 0;
			bool compact = false;
			u32 rows = 0;
			GraphNodePreview preview = GraphNodePreview::None;
			UI::ElementHandle well;
			UI::ElementHandle strip;
			Vector<PlacedPort> ports{GetMemoryResource(MemTag::Editor)};
			GraphCanvasOps::GraphNodeLayout layout{};
			Vector<PlacedSetting> settings{GetMemoryResource(MemTag::Editor)};
			bool stack = false;
			Vector<PlacedBlock> blocks{GetMemoryResource(MemTag::Editor)};
			Vector<f32> blockHeights{GetMemoryResource(MemTag::Editor)};
			Vector<PlacedFlow> flows{GetMemoryResource(MemTag::Editor)};
			u32 flowInputs = 0;
		};

		struct PlacedGroup {
			u64 id = 0;
			UI::ElementHandle title;
			str text;
			Vector<GraphNodeId> nodes{GetMemoryResource(MemTag::Editor)};
			Vector<u64> notes{GetMemoryResource(MemTag::Editor)};
			Vec2 position{};
			UI::Rect frame{};
		};

		struct PlacedNote {
			u64 id = 0;
			UI::ElementHandle element;
			UI::ElementHandle title;
			UI::ElementHandle body;
			UI::ElementHandle grip;
			Vec2 position{};
			Vec2 size{};
			str titleText;
			str bodyText;
			GraphNoteTheme theme = GraphNoteTheme::Light;
			GraphNoteSize textSize = GraphNoteSize::Medium;
		};

		enum class Press : u8 {
			None,
			Move,
			Resize,
			Pan,
			Marquee,
			Block
		};

		struct MoveStart {
			GraphItemKind kind = GraphItemKind::Node;
			u64 id = 0;
			Vec2 at{};
		};

		UI::Context* Live() const;
		Vec2 ToLocal(UI::Context& context, Vec2 point) const;
		const PlacedNode* FindNode(GraphNodeId id) const;
		bool NodeBox(const PlacedNode& node, UI::Rect& out) const;
		UI::Rect NoteBox(const PlacedNote& note) const;
		const PlacedNote* FindNote(u64 id) const;
		PlacedNote* FindNote(u64 id);
		PlacedGroup* FindGroup(u64 id);
		void BuildGroup(UI::Context& context, const GraphGroupDesc& desc);
		void BuildOutlineTitles(UI::Context& context);
		void PlaceOutlineTitles(UI::Context& context);
		void BeginOutlineRename(UI::Context& context, u64 outline);
		void BuildNote(UI::Context& context, const GraphNoteDesc& desc);
		void PlaceGroups(UI::Context& context);
		void StyleNote(UI::Context& context, const PlacedNote& note);
		u64 GroupAt(Vec2 local, const HashSet<u64>& except) const;
		void BeginGroupRename(UI::Context& context, u64 group);
		void OpenTitleEditor(UI::Context& context, const UI::Rect& bar, const str& text, f32 fontSizePx, std::function<void(std::string_view)> commit);
		void BeginNoteEdit(UI::Context& context, u64 note, bool body);
		void CloseEditor();
		template <typename Include>
		bool FrameNodes(UI::Context& context, Include&& include);
		bool FrameSelection(UI::Context& context);
		bool OnKey(UI::Context& context, Key key, KeyMod mods);
		bool OpenSearcher(UI::Context& context, Vec2 anchor, Vec2 graph, const GraphPortRef* from);
		void CloseSearcher();
		void BuildNode(UI::Context& context, const GraphNodeDesc& desc);
		void StyleNode(UI::Context& context, const PlacedNode& node, bool children);
		PlacedPort BuildPort(UI::Context& context, UI::ElementHandle parent, UI::ElementHandle layer, const GraphPortDesc& desc,
							 const GraphPortRef& ref, EditorTheme::ColorToken fill, bool labelled);
		void StylePorts(UI::Context& context, const Vector<PlacedPort>& ports, u32 inputs, f32 width, f32 height, f32 header);
		void StyleFields(UI::Context& context, const Vector<PlacedPort>& ports, u32 inputs, const UI::Rect& box, f32 header, bool children);
		void BuildSettings(UI::Context& context, UI::ElementHandle parent, std::span<const GraphSettingDesc> settings, GraphNodeId owner,
						   Vector<PlacedSetting>& out);
		void StyleSettings(UI::Context& context, const Vector<PlacedSetting>& settings, f32 top, f32 width);
		void BuildBlock(UI::Context& context, PlacedNode& node, const GraphBlockDesc& desc, u32 index);
		void StyleBlocks(UI::Context& context, const PlacedNode& node, const UI::Rect& box, bool children);
		void StyleFlows(UI::Context& context, const PlacedNode& node, f32 width, f32 height);
		bool BlockBox(const PlacedNode& node, u32 index, UI::Rect& out) const;
		bool FindBlock(GraphNodeId id, const PlacedNode*& node, u32& index) const;
		const PlacedNode* StackAt(Vec2 local) const;
		u32 GapAt(const PlacedNode& stack, Vec2 local) const;
		void BeginSettingEdit(UI::Context& context, GraphNodeId owner, u32 setting);
		void BuildInlineField(UI::Context& context, UI::ElementHandle layer, PlacedPort& port, const GraphPortRef& ref);
		void StyleInlineField(UI::Context& context, const PlacedPort& port, const UI::Rect& box, u32 row, f32 header, bool children);
		void BeginInlineEdit(UI::Context& context, const GraphPortRef& ref, u32 component);
		const PlacedPort* FindPort(const GraphPortRef& ref) const;
		void Paint(UI::Context& context, UI::ElementHandle element, UI::PaintScope& paint);
		void PaintOverlay(UI::Context& context, UI::ElementHandle element, UI::PaintScope& paint);
		bool ArrowEnds(const GraphWireDesc& wire, Vec2& from, Vec2& to) const;
		bool CurveOf(const GraphWireDesc& wire, GraphCubic& out) const;
		bool PickWire(Vec2 local, f32 scale, GraphWireId& out) const;
		bool PortAt(const GraphPortRef& port, Vec2& out) const;
		bool PickPort(Vec2 local, GraphPortSide side, GraphPortRef& out) const;
		bool PortDragTarget(const GraphPortRef& source, Vec2 local, GraphPortRef& target, bool& accepted) const;
		UI::ElementHandle DotOf(const GraphPortRef& ref) const;
		bool IsPortDot(UI::ElementHandle element) const;
		void EndPortDrag();
		bool OpenBlockSearcher(UI::Context& context, Vec2 anchor, GraphNodeId stack, u32 gap);

		void PressNode(UI::Context& context, GraphNodeId node);
		void PressItemForMove(UI::Context& context, const GraphItem& item);
		void PressGrip(UI::Context& context, u64 note);
		void PressBlock(UI::Context& context, GraphNodeId block);
		void TrackBlockDrop(UI::Context& context, Vec2 point);
		void GatherMoveStarts(UI::Context& context);
		void DropIntoGroup(UI::Context& context);
		void PressGrid(UI::Context& context);
		void Travel(UI::Context& context, Vec2 point);
		void Release();
		bool PressItem(const GraphItem& item, KeyMod mods);
		void EndMarquee(UI::Context& context);
		void EndMove();

		bool SetSelectionTo(std::span<const GraphItem> items);
		void Notify();

		UI::Context* _context = nullptr;
		GraphViewModel* _model = nullptr;
		GraphCanvasDesc _desc{};
		GraphView _view{};
		f32 _styledZoom = 0.0f;

		UI::ElementHandle _canvas;
		UI::ElementHandle _groupHost;
		UI::ElementHandle _noteHost;
		UI::ElementHandle _nodeHost;
		UI::ElementHandle _inlineHost;
		UI::ElementHandle _overlay;
		Vector<PlacedNode> _nodes{GetMemoryResource(MemTag::Editor)};
		HashMap<GraphNodeId, u32> _index{GetMemoryResource(MemTag::Editor)};
		Vector<PlacedGroup> _groups{GetMemoryResource(MemTag::Editor)};
		struct PlacedOutline {
			u64 id = 0;
			str text;
			Vector<GraphNodeId> nodes{GetMemoryResource(MemTag::Editor)};
			UI::ElementHandle title;
			UI::Rect bar{};
		};
		Vector<PlacedOutline> _titledOutlines{GetMemoryResource(MemTag::Editor)};
		Vector<PlacedNote> _notes{GetMemoryResource(MemTag::Editor)};
		HashMap<GraphNodeId, std::pair<u32, u32>> _blockIndex{GetMemoryResource(MemTag::Editor)};

		Vector<GraphItem> _selection{GetMemoryResource(MemTag::Editor)};
		HashSet<GraphNodeId> _selectedNodes{GetMemoryResource(MemTag::Editor)};
		HashSet<GraphWireId> _selectedWires{GetMemoryResource(MemTag::Editor)};
		HashSet<u64> _selectedGroups{GetMemoryResource(MemTag::Editor)};
		HashSet<u64> _selectedNotes{GetMemoryResource(MemTag::Editor)};
		HashSet<GraphNodeId> _selectedBlocks{GetMemoryResource(MemTag::Editor)};

		Press _press = Press::None;
		Vec2 _pressAt{};
		bool _pressMoved = false;
		Vec2 _panAnchor{};
		Vec2 _pressGraph{};
		Vector<MoveStart> _moveStarts{GetMemoryResource(MemTag::Editor)};
		GraphItem _moveAnchor{};
		bool _moving = false;
		bool _pressShift = false;
		u64 _resizing = 0;
		Vec2 _resizeStart{};
		UI::ElementHandle _editor;
		Vec2 _marqueeFrom{};
		Vec2 _marqueeTo{};
		bool _marqueeToggles = false;

		bool _portDragging = false;
		GraphPortRef _portDrag{};

		Vector<GraphNodeId> _blockDrag{GetMemoryResource(MemTag::Editor)};
		GraphNodeId _dropStack = 0;
		u32 _dropGap = 0;

		UI::ElementHandle _searcher;

		mutable Vector<GraphWireDesc> _wires{GetMemoryResource(MemTag::Editor)};
		mutable Vector<Vec2> _points{GetMemoryResource(MemTag::Editor)};
		mutable Vector<GraphOutlineDesc> _outlines{GetMemoryResource(MemTag::Editor)};
	};

} // namespace Shalltear
