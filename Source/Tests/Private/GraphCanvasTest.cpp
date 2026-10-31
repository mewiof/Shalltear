#include "TestsPCH.h"
#include "UITestUtil.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/GraphCanvas.h"
#include "Shalltear/Editor/GraphCanvasOps.h"

#include "EditorConfig.h"

#include <algorithm>

using namespace Shalltear;
using namespace UITest;

TEST(GraphCanvasOpsTest, ZoomKeepsPointUnderPointer) {
	GraphView view;
	view.pan = Vec2(37.0f, -12.0f);
	for (f32 zoom : {0.25f, 1.0f, 2.5f}) {
		view.zoom = zoom;
		const Vec2 graph(123.0f, -45.0f);
		const Vec2 back = GraphCanvasOps::LocalToGraph(view, GraphCanvasOps::GraphToLocal(view, graph));
		EXPECT_NEAR(back.x, graph.x, 1e-3f);
		EXPECT_NEAR(back.y, graph.y, 1e-3f);
	}

	view = GraphView{};
	const Vec2 pointer(300.0f, 180.0f);
	const Vec2 before = GraphCanvasOps::LocalToGraph(view, pointer);
	GraphCanvasOps::ZoomAtPoint(view, pointer, 2.0f, 0.25f, 2.5f);
	EXPECT_GT(view.zoom, 1.0f);
	const Vec2 after = GraphCanvasOps::LocalToGraph(view, pointer);
	EXPECT_NEAR(after.x, before.x, 1e-3f);
	EXPECT_NEAR(after.y, before.y, 1e-3f);

	GraphCanvasOps::ZoomAtPoint(view, pointer, 100.0f, 0.25f, 2.5f);
	EXPECT_FLOAT_EQ(view.zoom, 2.5f);
	GraphCanvasOps::ZoomAtPoint(view, pointer, -100.0f, 0.25f, 2.5f);
	EXPECT_FLOAT_EQ(view.zoom, 0.25f);
}

TEST(GraphCanvasOpsTest, FramingCentresWithin1xZoom) {
	GraphView view;
	ASSERT_TRUE(GraphCanvasOps::FrameBounds(view, Vec2(0.0f, 0.0f), Vec2(100.0f, 50.0f), Vec2(800.0f, 600.0f), 48.0f, 0.25f));
	EXPECT_FLOAT_EQ(view.zoom, 1.0f);
	const Vec2 centre = GraphCanvasOps::GraphToLocal(view, Vec2(50.0f, 25.0f));
	EXPECT_NEAR(centre.x, 400.0f, 1e-3f);
	EXPECT_NEAR(centre.y, 300.0f, 1e-3f);

	ASSERT_TRUE(GraphCanvasOps::FrameBounds(view, Vec2(-2000.0f, -500.0f), Vec2(3000.0f, 900.0f), Vec2(800.0f, 600.0f), 48.0f, 0.05f));
	EXPECT_LT(view.zoom, 1.0f);
	const Vec2 lower = GraphCanvasOps::GraphToLocal(view, Vec2(-2000.0f, -500.0f));
	const Vec2 upper = GraphCanvasOps::GraphToLocal(view, Vec2(3000.0f, 900.0f));
	EXPECT_GE(lower.x, 0.0f);
	EXPECT_GE(lower.y, 0.0f);
	EXPECT_LE(upper.x, 800.0f);
	EXPECT_LE(upper.y, 600.0f);

	const GraphView kept = view;
	EXPECT_FALSE(GraphCanvasOps::FrameBounds(view, Vec2(), Vec2(10.0f, 10.0f), Vec2(0.0f, 0.0f), 48.0f, 0.25f));
	EXPECT_FLOAT_EQ(view.zoom, kept.zoom);
	EXPECT_FLOAT_EQ(view.pan.x, kept.pan.x);
}

TEST(GraphCanvasOpsTest, ArrowStopsAtBoxEdge) {
	const UI::Rect box{100.0f, 100.0f, 80.0f, 40.0f};
	for (Vec2 from : {Vec2(0.0f, 120.0f), Vec2(140.0f, 0.0f), Vec2(20.0f, 30.0f)}) {
		const Vec2 hit = GraphCanvasOps::ClipToBox(box, from);
		const bool onVertical = std::abs(hit.x - box.x) < 1e-3f || std::abs(hit.x - (box.x + box.w)) < 1e-3f;
		const bool onHorizontal = std::abs(hit.y - box.y) < 1e-3f || std::abs(hit.y - (box.y + box.h)) < 1e-3f;
		EXPECT_TRUE(onVertical || onHorizontal);
		const Vec2 centre(140.0f, 120.0f);
		const f32 cross = (hit.x - from.x) * (centre.y - from.y) - (hit.y - from.y) * (centre.x - from.x);
		EXPECT_NEAR(cross, 0.0f, 1e-2f);
	}
}

TEST(GraphCanvasOpsTest, DataflowWireLoopsBackward) {
	const Vec2 from(100.0f, 100.0f);
	const Vec2 to(400.0f, 220.0f);
	const GraphCubic curve = GraphCanvasOps::PortWire(from, to, 1.0f);
	const Vec2 start = GraphCanvasOps::Evaluate(curve, 0.0f);
	const Vec2 end = GraphCanvasOps::Evaluate(curve, 1.0f);
	EXPECT_NEAR(start.x, from.x, 1e-3f);
	EXPECT_NEAR(end.y, to.y, 1e-3f);
	EXPECT_FLOAT_EQ(curve.p1.y, from.y);
	EXPECT_GT(curve.p1.x, from.x);
	EXPECT_FLOAT_EQ(curve.p2.y, to.y);
	EXPECT_LT(curve.p2.x, to.x);

	const GraphCubic backward = GraphCanvasOps::PortWire(Vec2(400.0f, 100.0f), Vec2(100.0f, 140.0f), 1.0f);
	f32 rightmost = 0.0f;
	for (u32 step = 0; step <= 32; ++step) {
		rightmost = std::max(rightmost, GraphCanvasOps::Evaluate(backward, static_cast<f32>(step) / 32.0f).x);
	}
	EXPECT_GT(rightmost, 400.0f);
}

TEST(GraphCanvasOpsTest, CurvePickedAlongBend) {
	const GraphCubic curve = GraphCanvasOps::PortWire(Vec2(0.0f, 0.0f), Vec2(300.0f, 200.0f), 1.0f);
	const u32 segments = GraphCanvasOps::SegmentCount(curve, 1.0f);
	const Vec2 onCurve = GraphCanvasOps::Evaluate(curve, 0.2f);
	ASSERT_GT(GraphCanvasOps::DistanceToSegment(curve.p0, curve.p3, onCurve), GraphCanvasOps::WIRE_PICK_RADIUS);
	EXPECT_LT(GraphCanvasOps::DistanceToCubic(curve, segments, onCurve), 1.0f);

	const Vec2 offCurve(60.0f, 140.0f);
	EXPECT_GT(GraphCanvasOps::DistanceToCubic(curve, segments, offCurve), GraphCanvasOps::WIRE_PICK_RADIUS);
}

TEST(GraphCanvasOpsTest, LongerWireMoreSegments) {
	const GraphCubic shortWire = GraphCanvasOps::PortWire(Vec2(0.0f, 0.0f), Vec2(60.0f, 0.0f), 1.0f);
	const GraphCubic longWire = GraphCanvasOps::PortWire(Vec2(0.0f, 0.0f), Vec2(900.0f, 400.0f), 1.0f);
	const u32 few = GraphCanvasOps::SegmentCount(shortWire, 1.0f);
	const u32 many = GraphCanvasOps::SegmentCount(longWire, 1.0f);
	EXPECT_LT(few, many);
	const GraphCubic medium = GraphCanvasOps::PortWire(Vec2(0.0f, 0.0f), Vec2(200.0f, 60.0f), 1.0f);
	EXPECT_GT(GraphCanvasOps::SegmentCount(medium, 2.0f), GraphCanvasOps::SegmentCount(medium, 1.0f));
	EXPECT_GE(GraphCanvasOps::SegmentCount(shortWire, 0.01f), 4u);
	EXPECT_LE(GraphCanvasOps::SegmentCount(longWire, 1000.0f), 64u);

	Vector<Vec2> points{GetMemoryResource(MemTag::Editor)};
	GraphCanvasOps::Tessellate(longWire, many, points);
	ASSERT_EQ(points.size(), static_cast<size_t>(many) + 1u);
	EXPECT_NEAR(points.front().x, longWire.p0.x, 1e-3f);
	EXPECT_NEAR(points.back().x, longWire.p3.x, 1e-3f);
}

TEST(GraphCanvasOpsTest, PortsStackPickTakesNearest) {
	const f32 zoom = 1.5f;
	const UI::Rect box{50.0f, 80.0f, 180.0f * zoom, GraphCanvasOps::PortNodeHeight(3) * zoom};
	const Vec2 in0 = GraphCanvasOps::PortAnchor(box, GraphPortSide::Input, 0, zoom);
	const Vec2 in1 = GraphCanvasOps::PortAnchor(box, GraphPortSide::Input, 1, zoom);
	const Vec2 out0 = GraphCanvasOps::PortAnchor(box, GraphPortSide::Output, 0, zoom);
	EXPECT_NEAR(in1.y - in0.y, GraphCanvasOps::PORT_ROW_HEIGHT * zoom, 1e-3f);
	EXPECT_NEAR(in0.y, out0.y, 1e-3f);
	EXPECT_GT(in0.x, box.x);
	EXPECT_LT(out0.x, box.x + box.w);
	EXPECT_LT(in0.x, out0.x);
	const Vec2 in2 = GraphCanvasOps::PortAnchor(box, GraphPortSide::Input, 2, zoom);
	EXPECT_LT(in2.y + GraphCanvasOps::PORT_RADIUS * zoom, box.y + box.h);

	const Vec2 anchors[] = {in0, in1, out0};
	EXPECT_EQ(GraphCanvasOps::PickNearest(anchors, in1 + Vec2(2.0f, -3.0f), 9.0f), 1);
	EXPECT_EQ(GraphCanvasOps::PickNearest(anchors, in1 + Vec2(40.0f, 0.0f), 9.0f), -1);
}

namespace {

	class TestGraphModel final : public GraphViewModel {
	public:
		struct Node {
			GraphNodeId id = 0;
			Vec2 position{};
			u32 inputs = 0;
			u32 outputs = 0;
			str label;
			bool compact = false;
			GraphNodePreview preview = GraphNodePreview::None;
			Vector<GraphInlineValue> inlineValues{GetMemoryResource(MemTag::Editor)};
			Vector<bool> inputConnected = Vector<bool>(GetMemoryResource(MemTag::Editor));
		};
		struct Group {
			u64 id = 0;
			str title;
			Vector<GraphNodeId> nodes{GetMemoryResource(MemTag::Editor)};
			Vector<u64> notes{GetMemoryResource(MemTag::Editor)};
			Vec2 position{};
		};
		struct Note {
			u64 id = 0;
			Vec2 position{};
			Vec2 size{};
			str title;
			str body;
		};
		Vector<Node> nodes{GetMemoryResource(MemTag::Editor)};
		Vector<GraphWireDesc> wires{GetMemoryResource(MemTag::Editor)};
		Vector<Group> groups{GetMemoryResource(MemTag::Editor)};
		Vector<Note> notes{GetMemoryResource(MemTag::Editor)};
		bool refuseConnections = false;

		u32 groupChanges = 0;
		u64 groupedInto = 99;
		Vector<GraphItem> groupedItems{GetMemoryResource(MemTag::Editor)};
		u64 renamedGroup = 0;
		str renamedTo;
		Vector<GraphOutlineDesc> outlines{GetMemoryResource(MemTag::Editor)};
		u64 renamedOutline = 0;
		str renamedOutlineTo;
		u64 editedNote = 0;
		str editedTitle;
		str editedBody;
		u32 noteMoves = 0;
		u32 resizes = 0;
		Vec2 resizedTo{};
		u32 wireDoubleClicks = 0;
		GraphWireId doubleClickedWire = 0;
		u32 commands = 0;
		GraphCommand lastCommand = GraphCommand::Group;

		u32 previewToggles = 0;
		GraphNodeId toggledNode = 0;
		bool toggledTo = true;
		u32 previewPaints = 0;
		UI::Rect paintedWell{};
		u32 inlineSets = 0;
		GraphPortRef inlinePort{};
		Vector<f32> inlineValues = Vector<f32>(GetMemoryResource(MemTag::Editor));

		Vector<GraphSearchEntry> library{GetMemoryResource(MemTag::Editor)};
		u32 searches = 0;
		bool searchedFromPort = false;
		GraphPortRef searchedFrom{};
		u32 picks = 0;
		u64 pickedKey = 0;
		Vec2 pickedAt{};
		bool pickedWithPort = false;
		GraphPortRef pickedFrom{};

		u32 connects = 0;
		GraphPortRef connectedOutput{};
		GraphPortRef connectedInput{};
		u32 moves = 0;
		Vec2 movedTo{};
		u32 nodeClicks = 0;
		u32 selectionChanges = 0;
		u32 moveBegins = 0;
		u32 moveEnds = 0;

		void GetNodes(Vector<GraphNodeDesc>& out) override {
			for (const Node& node : nodes) {
				GraphNodeDesc desc;
				desc.id = node.id;
				desc.size = Vec2(160.0f, 40.0f);
				desc.label = node.label;
				desc.radius = 4.0f;
				desc.compact = node.compact;
				desc.preview = node.preview;
				if (node.compact) {
					desc.size.x = 24.0f;
				}
				for (u32 index = 0; index < node.inputs; ++index) {
					GraphPortDesc port{str("in"), EditorTheme::ColorToken::GraphPortFloat, false};
					port.connected = index < node.inputConnected.size() && node.inputConnected[index];
					if (index < node.inlineValues.size()) {
						port.inlineValue = node.inlineValues[index];
					}
					desc.inputs.push_back(std::move(port));
				}
				for (u32 index = 0; index < node.outputs; ++index) {
					desc.outputs.push_back(GraphPortDesc{str("out"), EditorTheme::ColorToken::GraphPortFloat, false});
				}
				out.push_back(std::move(desc));
			}
		}
		bool GetNodePosition(GraphNodeId id, Vec2& out) const override {
			for (const Node& node : nodes) {
				if (node.id == id) {
					out = node.position;
					return true;
				}
			}
			return false;
		}
		void GetWires(Vector<GraphWireDesc>& out) const override {
			for (const GraphWireDesc& wire : wires) {
				out.push_back(wire);
			}
		}
		void MoveNode(GraphNodeId id, Vec2 graph) override {
			for (Node& node : nodes) {
				if (node.id == id) {
					node.position = graph;
				}
			}
			++moves;
			movedTo = graph;
		}
		void OnNodeClick(GraphNodeId) override { ++nodeClicks; }
		void OnSelectionChanged() override { ++selectionChanges; }
		void OnMoveBegin() override { ++moveBegins; }
		void OnMoveEnd() override { ++moveEnds; }
		bool CanConnect(const GraphPortRef& output, const GraphPortRef& input) const override {
			return !refuseConnections && output.node != input.node;
		}
		void Connect(const GraphPortRef& output, const GraphPortRef& input) override {
			++connects;
			connectedOutput = output;
			connectedInput = input;
		}
		void GetGroups(Vector<GraphGroupDesc>& out) const override {
			for (const Group& group : groups) {
				GraphGroupDesc desc;
				desc.id = group.id;
				desc.title = group.title;
				desc.nodes.assign(group.nodes.begin(), group.nodes.end());
				desc.notes.assign(group.notes.begin(), group.notes.end());
				desc.position = group.position;
				out.push_back(std::move(desc));
			}
		}
		void GetNotes(Vector<GraphNoteDesc>& out) const override {
			for (const Note& note : notes) {
				GraphNoteDesc desc;
				desc.id = note.id;
				desc.position = note.position;
				desc.size = note.size;
				desc.title = note.title;
				desc.body = note.body;
				out.push_back(std::move(desc));
			}
		}
		void MoveNote(u64 id, Vec2 graph) override {
			for (Note& note : notes) {
				if (note.id == id) {
					note.position = graph;
				}
			}
			++noteMoves;
		}
		void MoveGroup(u64 id, Vec2 graph) override {
			for (Group& group : groups) {
				if (group.id == id) {
					group.position = graph;
				}
			}
		}
		void ResizeNote(u64, Vec2 size) override {
			++resizes;
			resizedTo = size;
		}
		void SetGroupOf(std::span<const GraphItem> items, u64 group) override {
			++groupChanges;
			groupedInto = group;
			groupedItems.assign(items.begin(), items.end());
		}
		void RenameGroup(u64 group, std::string_view title) override {
			renamedGroup = group;
			renamedTo = str(title);
		}
		void GetOutlines(Vector<GraphOutlineDesc>& out) const override {
			out.clear();
			for (const GraphOutlineDesc& outline : outlines) {
				GraphOutlineDesc copy;
				copy.nodes.assign(outline.nodes.begin(), outline.nodes.end());
				copy.id = outline.id;
				copy.title = outline.title;
				out.push_back(std::move(copy));
			}
		}
		void RenameOutline(u64 outline, std::string_view title) override {
			renamedOutline = outline;
			renamedOutlineTo = str(title);
		}
		void EditNote(u64 note, std::string_view title, std::string_view body) override {
			editedNote = note;
			editedTitle = str(title);
			editedBody = str(body);
		}
		void OnWireDoubleClick(GraphWireId wire, Vec2) override {
			++wireDoubleClicks;
			doubleClickedWire = wire;
		}
		bool OnGraphCommand(GraphCommand command) override {
			++commands;
			lastCommand = command;
			return true;
		}
		void SetNodePreviewExpanded(GraphNodeId node, bool expanded) override {
			++previewToggles;
			toggledNode = node;
			toggledTo = expanded;
		}
		void PaintNodePreview(UI::Context&, UI::PaintScope&, GraphNodeId, const UI::Rect& well) override {
			++previewPaints;
			paintedWell = well;
		}
		void SetInlineValue(const GraphPortRef& input, std::span<const f32> values) override {
			++inlineSets;
			inlinePort = input;
			inlineValues.assign(values.begin(), values.end());
		}
		void OpenInlineEditor(UI::Context& context, const GraphPortRef& input, UI::ElementHandle field) override {
			++inlineEditors;
			editedPort = input;
			editorAnchored = context.IsAlive(field);
		}
		u32 inlineEditors = 0;
		GraphPortRef editedPort{};
		bool editorAnchored = false;

		void GetSearchEntries(const GraphPortRef* from, Vector<GraphSearchEntry>& out) override {
			++searches;
			searchedFromPort = from != nullptr;
			searchedFrom = from ? *from : GraphPortRef{};
			for (const GraphSearchEntry& entry : library) {
				if (!from || (entry.key % 2) == 1) {
					out.push_back(entry);
				}
			}
		}
		void OnSearchPick(const GraphSearchEntry& entry, Vec2 graph, const GraphPortRef* from) override {
			++picks;
			pickedKey = entry.key;
			pickedAt = graph;
			pickedWithPort = from != nullptr;
			pickedFrom = from ? *from : GraphPortRef{};
		}
	};

	GraphSearchEntry Entry(std::string_view name, std::string_view category, u64 key, std::string_view synonyms = {}) {
		GraphSearchEntry entry;
		entry.name = str(name);
		entry.category = str(category);
		entry.synonyms = str(synonyms);
		entry.key = key;
		return entry;
	}

	void SmallLibrary(TestGraphModel& model) {
		model.library.push_back(Entry("Add", "Math/Basic", 2, "plus sum"));
		model.library.push_back(Entry("Multiply", "Math/Basic", 3, "times product"));
		model.library.push_back(Entry("Sine", "Math/Trigonometry", 5, "sin wave"));
		model.library.push_back(Entry("Color", "Input", 6));
	}

	constexpr f32 SCALE = 1.25f;

	template <typename Model>
	struct BasicHarness {
		MockFontSystem fonts;
		UI::Context context;
		Model model;
		GraphCanvas canvas;
		UI::ElementHandle root;
		GraphCanvasDesc desc;

		BasicHarness() : BasicHarness(Desc()) {}
		explicit BasicHarness(const UI::ContextDesc& described) : context(described, &fonts) {
			EditorTheme::ApplyToContext(context);
			root = context.CreateElement();
			context.AppendChild(context.GetRoot(), root);
			UI::Style style;
			style.flexDirection = UI::FlexDirection::Row;
			style.width = UI::Length::Percent(1.0f);
			style.height = UI::Length::Percent(1.0f);
			context.SetStyle(root, style);
		}

		static UI::ContextDesc Desc() {
			UI::ContextDesc desc;
			desc.sizePx = {1000, 700};
			desc.contentScale = SCALE;
			return desc;
		}

		void Build() {
			canvas.Build(context, root, &model, desc);
			context.Update();
			canvas.RebuildNodes(context);
			context.Update();
		}

		Vec2I ToScreen(Vec2 local) {
			const UI::Rect content = context.GetLayout(canvas.GetElement()).content;
			return Vec2I(static_cast<i32>(std::lround(content.x + local.x * SCALE)),
						 static_cast<i32>(std::lround(content.y + local.y * SCALE)));
		}

		Vec2I PortOnScreen(GraphNodeId node, GraphPortSide side, u32 row) {
			Vec2 graph{};
			model.GetNodePosition(node, graph);
			const u32 rows = 1;
			const UI::Rect box = GraphCanvasOps::NodeRect(canvas.GetView(), graph, Vec2(160.0f, GraphCanvasOps::PortNodeHeight(rows)));
			return ToScreen(GraphCanvasOps::PortAnchor(box, side, row, canvas.GetView().zoom));
		}

		void Inject(InputEventType type, Vec2I at, MouseButton button = MouseButton::Left, KeyMod mods = KeyMod::None) {
			InputEvent event{};
			event.type = type;
			event.button = button;
			event.mousePosPx = at;
			event.mods = mods;
			context.InjectInput(event);
			context.Update();
		}

		void Drag(Vec2I from, Vec2I to, KeyMod mods = KeyMod::None) {
			Inject(InputEventType::MouseMove, from, MouseButton::Left, mods);
			Inject(InputEventType::MouseDown, from, MouseButton::Left, mods);
			for (i32 step = 1; step <= 3; ++step) {
				Inject(InputEventType::MouseMove, Vec2I(from.x + (to.x - from.x) * step / 3, from.y + (to.y - from.y) * step / 3),
					   MouseButton::Left, mods);
			}
			Inject(InputEventType::MouseUp, to, MouseButton::Left, mods);
		}

		void Click(Vec2I at, KeyMod mods = KeyMod::None) {
			Inject(InputEventType::MouseMove, at, MouseButton::Left, mods);
			Inject(InputEventType::MouseDown, at, MouseButton::Left, mods);
			Inject(InputEventType::MouseUp, at, MouseButton::Left, mods);
		}

		void Press(Key key, KeyMod mods = KeyMod::None, char32_t character = 0) {
			InputEvent down{};
			down.type = InputEventType::KeyDown;
			down.key = key;
			down.mods = mods;
			context.InjectInput(down);
			context.Update();
			if (character != 0) {
				InputEvent text{};
				text.type = InputEventType::Text;
				text.codepoint = character;
				context.InjectInput(text);
				context.Update();
			}
			InputEvent up = down;
			up.type = InputEventType::KeyUp;
			context.InjectInput(up);
			context.Update();
		}

		void Type(std::string_view text) {
			for (const char c : text) {
				InputEvent event{};
				event.type = InputEventType::Text;
				event.codepoint = static_cast<char32_t>(c);
				context.InjectInput(event);
				context.Update();
			}
		}

		UI::ElementHandle Searcher() {
			const u32 count = context.GetOpenPopupCount();
			return count == 0 ? UI::ElementHandle{} : context.GetOpenPopup(count - 1);
		}

		Vec2I GraphOnScreen(Vec2 graph) { return ToScreen(GraphCanvasOps::GraphToLocal(canvas.GetView(), graph)); }

		Vec2I CardOnScreen(GraphNodeId node) {
			Vec2 at{};
			model.GetNodePosition(node, at);
			return GraphOnScreen(at + Vec2(80.0f, 20.0f));
		}

		bool Selected(GraphItemKind kind, u64 id) const { return canvas.IsSelected(GraphItem{kind, id}); }
	};

	using CanvasHarness = BasicHarness<TestGraphModel>;

	void TwoPortedNodes(TestGraphModel& model) {
		model.nodes.push_back({1, Vec2(40.0f, 60.0f), 0, 1, str("A")});
		model.nodes.push_back({2, Vec2(380.0f, 200.0f), 1, 0, str("B")});
	}

} // anonymous namespace

TEST(GraphCanvasTest, PortDragConnectsOutputFirst) {
	CanvasHarness harness;
	TwoPortedNodes(harness.model);
	harness.Build();

	const Vec2I output = harness.PortOnScreen(1, GraphPortSide::Output, 0);
	const Vec2I input = harness.PortOnScreen(2, GraphPortSide::Input, 0);

	harness.Drag(output, input);
	ASSERT_EQ(harness.model.connects, 1u);
	EXPECT_EQ(harness.model.connectedOutput, (GraphPortRef{1, GraphPortSide::Output, 0}));
	EXPECT_EQ(harness.model.connectedInput, (GraphPortRef{2, GraphPortSide::Input, 0}));
	EXPECT_EQ(harness.model.moves, 0u);

	harness.Drag(input, output);
	ASSERT_EQ(harness.model.connects, 2u);
	EXPECT_EQ(harness.model.connectedOutput, (GraphPortRef{1, GraphPortSide::Output, 0}));
	EXPECT_EQ(harness.model.connectedInput, (GraphPortRef{2, GraphPortSide::Input, 0}));
	EXPECT_EQ(harness.model.searches, 0u);
}

TEST(GraphCanvasTest, GridWireDropOpensSearcher) {
	CanvasHarness harness;
	TwoPortedNodes(harness.model);
	SmallLibrary(harness.model);
	harness.Build();
	const Vec2I output = harness.PortOnScreen(1, GraphPortSide::Output, 0);

	const Vec2 releaseLocal(300.0f, 420.0f);
	harness.Drag(output, harness.ToScreen(releaseLocal));
	ASSERT_TRUE(harness.context.IsAlive(harness.Searcher()));
	EXPECT_TRUE(harness.model.searchedFromPort);
	EXPECT_EQ(harness.model.searchedFrom, (GraphPortRef{1, GraphPortSide::Output, 0}));
	EXPECT_EQ(harness.model.connects, 0u);
	harness.Type("add");
	EXPECT_FALSE(harness.context.IsAlive(FindByText(harness.context, harness.Searcher(), "Add")));
	EXPECT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.Searcher(), "(no matches)")));
	for (i32 erase = 0; erase < 3; ++erase) {
		harness.Press(Key::Backspace);
	}

	harness.Type("mul");
	harness.Press(Key::Enter);
	ASSERT_EQ(harness.model.picks, 1u);
	EXPECT_EQ(harness.model.pickedKey, 3u);
	EXPECT_TRUE(harness.model.pickedWithPort);
	EXPECT_EQ(harness.model.pickedFrom, (GraphPortRef{1, GraphPortSide::Output, 0}));
	const Vec2 expected = GraphCanvasOps::LocalToGraph(harness.canvas.GetView(), releaseLocal);
	EXPECT_NEAR(harness.model.pickedAt.x, expected.x, 1.0f);
	EXPECT_NEAR(harness.model.pickedAt.y, expected.y, 1.0f);
	harness.context.Update();
	EXPECT_EQ(harness.context.GetOpenPopupCount(), 0u);

	harness.model.refuseConnections = true;
	const u32 searches = harness.model.searches;
	harness.Drag(output, harness.PortOnScreen(2, GraphPortSide::Input, 0));
	EXPECT_EQ(harness.model.connects, 0u);
	EXPECT_EQ(harness.model.searches, searches);

	harness.Drag(output, output);
	EXPECT_EQ(harness.model.searches, searches);
	EXPECT_EQ(harness.context.GetOpenPopupCount(), 0u);
}

TEST(GraphCanvasTest, CurvedWireSelectedWhereDrawn) {
	CanvasHarness harness;
	TwoPortedNodes(harness.model);
	GraphWireDesc wire;
	wire.id = 7;
	wire.style = GraphWireStyle::Curve;
	wire.from = GraphPortRef{1, GraphPortSide::Output, 0};
	wire.to = GraphPortRef{2, GraphPortSide::Input, 0};
	wire.color = EditorTheme::ColorToken::GraphPortFloat;
	harness.model.wires.push_back(wire);
	harness.Build();

	Vec2 a{};
	Vec2 b{};
	harness.model.GetNodePosition(1, a);
	harness.model.GetNodePosition(2, b);
	const GraphView& view = harness.canvas.GetView();
	const Vec2 size(160.0f, GraphCanvasOps::PortNodeHeight(1));
	const Vec2 from = GraphCanvasOps::PortAnchor(GraphCanvasOps::NodeRect(view, a, size), GraphPortSide::Output, 0, view.zoom);
	const Vec2 to = GraphCanvasOps::PortAnchor(GraphCanvasOps::NodeRect(view, b, size), GraphPortSide::Input, 0, view.zoom);
	const GraphCubic curve = GraphCanvasOps::PortWire(from, to, view.zoom);

	const Vec2 onCurve = GraphCanvasOps::Evaluate(curve, 0.3f);
	ASSERT_GT(GraphCanvasOps::DistanceToSegment(curve.p0, curve.p3, onCurve), GraphCanvasOps::WIRE_PICK_RADIUS);
	harness.Click(harness.ToScreen(onCurve));
	EXPECT_TRUE(harness.Selected(GraphItemKind::Wire, 7));
	EXPECT_EQ(harness.model.selectionChanges, 1u);

	const Vec2 onChord = curve.p0 + (curve.p3 - curve.p0) * 0.25f;
	ASSERT_GT(GraphCanvasOps::DistanceToCubic(curve, 64, onChord), GraphCanvasOps::WIRE_PICK_RADIUS);
	harness.Click(harness.ToScreen(onChord));
	EXPECT_FALSE(harness.Selected(GraphItemKind::Wire, 7));
	EXPECT_TRUE(harness.canvas.GetSelection().empty());
	EXPECT_EQ(harness.model.selectionChanges, 2u);
}

TEST(GraphCanvasTest, NodeDragKeepsGrabPoint) {
	CanvasHarness harness;
	harness.model.nodes.push_back({1, Vec2(100.0f, 100.0f), 1, 1, str("A")});
	harness.Build();

	const Vec2 grabLocal = GraphCanvasOps::GraphToLocal(harness.canvas.GetView(), Vec2(100.0f, 100.0f)) + Vec2(30.0f, 8.0f);
	const Vec2 travel(120.0f, 70.0f);
	harness.Drag(harness.ToScreen(grabLocal), harness.ToScreen(grabLocal + travel));

	ASSERT_GT(harness.model.moves, 0u);
	const f32 zoom = harness.canvas.GetView().zoom;
	EXPECT_NEAR(harness.model.movedTo.x, 100.0f + travel.x / zoom, 1.5f);
	EXPECT_NEAR(harness.model.movedTo.y, 100.0f + travel.y / zoom, 1.5f);

	EXPECT_EQ(harness.model.nodeClicks, 0u);

	harness.Click(harness.ToScreen(grabLocal + travel));
	EXPECT_EQ(harness.model.nodeClicks, 1u);
}

TEST(GraphCanvasTest, CanvasesKeepSeparateViews) {
	MockFontSystem fonts;
	UI::Context context(CanvasHarness::Desc(), &fonts);
	EditorTheme::ApplyToContext(context);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	UI::Style style;
	style.flexDirection = UI::FlexDirection::Row;
	style.width = UI::Length::Percent(1.0f);
	style.height = UI::Length::Percent(1.0f);
	context.SetStyle(root, style);

	TestGraphModel leftModel;
	TestGraphModel rightModel;
	leftModel.nodes.push_back({1, Vec2(40.0f, 40.0f), 1, 1, str("Left")});
	rightModel.nodes.push_back({1, Vec2(40.0f, 40.0f), 1, 1, str("Right")});
	GraphCanvas left;
	GraphCanvas right;
	left.Build(context, root, &leftModel);
	right.Build(context, root, &rightModel);
	context.Update();
	left.RebuildNodes(context);
	right.RebuildNodes(context);
	context.Update();

	UI::ElementHandle rightNode = FindByText(context, right.GetElement(), "Right");
	ASSERT_TRUE(context.IsAlive(rightNode));
	const UI::Rect rightBefore = context.GetLayout(rightNode).borderBox;

	const UI::Rect leftArea = context.GetLayout(left.GetElement()).borderBox;
	InputEvent move{};
	move.type = InputEventType::MouseMove;
	move.mousePosPx = Vec2I(static_cast<i32>(leftArea.x + leftArea.w * 0.5f), static_cast<i32>(leftArea.y + leftArea.h * 0.5f));
	context.InjectInput(move);
	InputEvent wheel = move;
	wheel.type = InputEventType::MouseWheel;
	wheel.wheel = Vec2(0.0f, 2.0f);
	context.InjectInput(wheel);
	context.Update();

	EXPECT_GT(left.GetView().zoom, 1.0f);
	EXPECT_FLOAT_EQ(right.GetView().zoom, 1.0f);
	const UI::Rect rightAfter = context.GetLayout(rightNode).borderBox;
	EXPECT_FLOAT_EQ(rightAfter.x, rightBefore.x);
	EXPECT_FLOAT_EQ(rightAfter.w, rightBefore.w);
	UI::ElementHandle leftNode = FindByText(context, left.GetElement(), "Left");
	ASSERT_TRUE(context.IsAlive(leftNode));
	EXPECT_GT(context.GetLayout(leftNode).borderBox.w, rightAfter.w);

	const UI::Rect leftBox = context.GetLayout(leftNode).borderBox;
	InputEvent press{};
	press.mousePosPx = Vec2I(static_cast<i32>(leftBox.x + leftBox.w * 0.5f), static_cast<i32>(leftBox.y + 6.0f));
	for (InputEventType type : {InputEventType::MouseMove, InputEventType::MouseDown, InputEventType::MouseUp}) {
		press.type = type;
		context.InjectInput(press);
		context.Update();
	}
	EXPECT_TRUE(left.IsSelected(GraphItem{GraphItemKind::Node, 1}));
	EXPECT_TRUE(right.GetSelection().empty());
}

namespace {

	void ThreeCards(TestGraphModel& model) {
		model.nodes.push_back({1, Vec2(40.0f, 60.0f), 0, 0, str("One")});
		model.nodes.push_back({2, Vec2(300.0f, 60.0f), 0, 0, str("Two")});
		model.nodes.push_back({3, Vec2(40.0f, 300.0f), 0, 0, str("Three")});
	}

	Vec2 PositionOf(const TestGraphModel& model, GraphNodeId node) {
		Vec2 at{};
		model.GetNodePosition(node, at);
		return at;
	}

} // anonymous namespace

TEST(GraphCanvasTest, PressSelectsCtrlToggles) {
	CanvasHarness harness;
	ThreeCards(harness.model);
	harness.Build();
	const Vec2I one = harness.CardOnScreen(1);
	const Vec2I two = harness.CardOnScreen(2);
	const Vec2I three = harness.CardOnScreen(3);

	harness.Click(one);
	ASSERT_EQ(harness.canvas.GetSelection().size(), 1u);
	EXPECT_TRUE(harness.Selected(GraphItemKind::Node, 1));
	harness.Click(two);
	EXPECT_FALSE(harness.Selected(GraphItemKind::Node, 1));
	EXPECT_TRUE(harness.Selected(GraphItemKind::Node, 2));

	harness.Click(one, KeyMod::Ctrl);
	ASSERT_EQ(harness.canvas.GetSelection().size(), 2u);
	EXPECT_EQ(harness.canvas.GetSelection().back(), (GraphItem{GraphItemKind::Node, 1}));
	harness.Click(two, KeyMod::Ctrl);
	ASSERT_EQ(harness.canvas.GetSelection().size(), 1u);
	EXPECT_TRUE(harness.Selected(GraphItemKind::Node, 1));

	harness.Click(three, KeyMod::Shift);
	ASSERT_EQ(harness.canvas.GetSelection().size(), 1u);
	EXPECT_TRUE(harness.Selected(GraphItemKind::Node, 3));

	const u32 changes = harness.model.selectionChanges;
	harness.Click(three);
	EXPECT_EQ(harness.model.selectionChanges, changes);

	const Vec2I empty = harness.GraphOnScreen(Vec2(600.0f, 400.0f));
	harness.Click(empty, KeyMod::Ctrl);
	EXPECT_TRUE(harness.Selected(GraphItemKind::Node, 3));
	harness.Click(empty);
	EXPECT_TRUE(harness.canvas.GetSelection().empty());
	EXPECT_EQ(harness.model.nodeClicks, 6u);
}

TEST(GraphCanvasTest, SelectionDragIsOneBracket) {
	CanvasHarness harness;
	ThreeCards(harness.model);
	harness.Build();
	harness.Click(harness.CardOnScreen(1));
	harness.Click(harness.CardOnScreen(2), KeyMod::Ctrl);
	const Vec2 oneBefore = PositionOf(harness.model, 1);
	const Vec2 twoBefore = PositionOf(harness.model, 2);
	const Vec2 threeBefore = PositionOf(harness.model, 3);

	const Vec2I from = harness.CardOnScreen(1);
	harness.Drag(from, Vec2I(from.x + 75, from.y + 50));
	EXPECT_EQ(harness.model.moveBegins, 1u);
	EXPECT_EQ(harness.model.moveEnds, 1u);
	const Vec2 moved = PositionOf(harness.model, 1) - oneBefore;
	EXPECT_GT(moved.x, 0.0f);
	EXPECT_GT(moved.y, 0.0f);
	const Vec2 alsoMoved = PositionOf(harness.model, 2) - twoBefore;
	EXPECT_NEAR(alsoMoved.x, moved.x, 1e-3f);
	EXPECT_NEAR(alsoMoved.y, moved.y, 1e-3f);
	EXPECT_EQ(PositionOf(harness.model, 3).x, threeBefore.x);
	EXPECT_EQ(PositionOf(harness.model, 3).y, threeBefore.y);
	EXPECT_EQ(harness.canvas.GetSelection().size(), 2u);
	const Vec2 oneAfter = PositionOf(harness.model, 1);

	harness.Drag(harness.CardOnScreen(3), Vec2I(harness.CardOnScreen(3).x + 30, harness.CardOnScreen(3).y));
	EXPECT_EQ(harness.canvas.GetSelection().size(), 1u);
	EXPECT_NE(PositionOf(harness.model, 3).x, threeBefore.x);
	EXPECT_EQ(PositionOf(harness.model, 1).x, oneAfter.x);
	EXPECT_EQ(harness.model.moveBegins, 2u);
	EXPECT_EQ(harness.model.moveEnds, 2u);

	const Vec2 threeNow = PositionOf(harness.model, 3);
	harness.Drag(harness.CardOnScreen(3), Vec2I(harness.CardOnScreen(3).x + 40, harness.CardOnScreen(3).y + 40), KeyMod::Ctrl);
	EXPECT_EQ(PositionOf(harness.model, 3).x, threeNow.x);
	EXPECT_FALSE(harness.Selected(GraphItemKind::Node, 3));
	EXPECT_EQ(harness.model.moveBegins, 2u);
}

TEST(GraphCanvasTest, MarqueeTakesOverlapCtrlToggles) {
	CanvasHarness harness;
	ThreeCards(harness.model);
	GraphWireDesc wire;
	wire.id = 9;
	wire.style = GraphWireStyle::Arrow;
	wire.from.node = 1;
	wire.to.node = 2;
	harness.model.wires.push_back(wire);
	harness.Build();
	harness.Click(harness.CardOnScreen(3));

	harness.Drag(harness.GraphOnScreen(Vec2(10.0f, 20.0f)), harness.GraphOnScreen(Vec2(250.0f, 90.0f)));
	EXPECT_TRUE(harness.Selected(GraphItemKind::Node, 1));
	EXPECT_TRUE(harness.Selected(GraphItemKind::Wire, 9));
	EXPECT_FALSE(harness.Selected(GraphItemKind::Node, 2));
	EXPECT_FALSE(harness.Selected(GraphItemKind::Node, 3));
	ASSERT_FALSE(harness.canvas.GetSelection().empty());
	EXPECT_EQ(harness.canvas.GetSelection().back(), (GraphItem{GraphItemKind::Node, 1}));
	EXPECT_EQ(harness.model.moveBegins, 0u);

	harness.Drag(harness.GraphOnScreen(Vec2(20.0f, 50.0f)), harness.GraphOnScreen(Vec2(120.0f, 320.0f)), KeyMod::Ctrl);
	EXPECT_FALSE(harness.Selected(GraphItemKind::Node, 1));
	EXPECT_TRUE(harness.Selected(GraphItemKind::Node, 3));
	EXPECT_TRUE(harness.Selected(GraphItemKind::Wire, 9));
}

TEST(GraphCanvasTest, AltDragPans) {
	CanvasHarness harness;
	ThreeCards(harness.model);
	harness.Build();
	const Vec2 start = harness.canvas.GetView().pan;
	const Vec2I empty = harness.GraphOnScreen(Vec2(600.0f, 400.0f));

	harness.Drag(empty, Vec2I(empty.x + 100, empty.y + 50));
	EXPECT_EQ(harness.canvas.GetView().pan.x, start.x);
	EXPECT_EQ(harness.canvas.GetView().pan.y, start.y);

	harness.Drag(empty, Vec2I(empty.x + 100, empty.y + 50), KeyMod::Alt);
	EXPECT_NEAR(harness.canvas.GetView().pan.x - start.x, 100.0f / SCALE, 1.0f);

	const Vec2 panned = harness.canvas.GetView().pan;
	const Vec2 node = PositionOf(harness.model, 1);
	const Vec2I on = harness.CardOnScreen(1);
	harness.Drag(on, Vec2I(on.x - 60, on.y), KeyMod::Alt);
	EXPECT_EQ(PositionOf(harness.model, 1).x, node.x);
	EXPECT_LT(harness.canvas.GetView().pan.x, panned.x);
	EXPECT_TRUE(harness.Selected(GraphItemKind::Node, 1));
}

TEST(GraphCanvasTest, RebuildDropsUndrawn) {
	CanvasHarness harness;
	ThreeCards(harness.model);
	GraphWireDesc wire;
	wire.id = 9;
	wire.from.node = 1;
	wire.to.node = 2;
	harness.model.wires.push_back(wire);
	harness.Build();

	harness.Inject(InputEventType::MouseMove, harness.CardOnScreen(2));
	harness.Inject(InputEventType::MouseDown, harness.CardOnScreen(2), MouseButton::Right);
	harness.Inject(InputEventType::MouseUp, harness.CardOnScreen(2), MouseButton::Right);
	EXPECT_TRUE(harness.Selected(GraphItemKind::Node, 2));

	const GraphItem picked[] = {{GraphItemKind::Node, 1}, {GraphItemKind::Node, 2}, {GraphItemKind::Wire, 9}};
	harness.canvas.SetSelection(picked);
	const u32 changes = harness.model.selectionChanges;
	harness.model.nodes.erase(harness.model.nodes.begin() + 1);
	harness.model.wires.clear();
	harness.canvas.RebuildNodes(harness.context);
	ASSERT_EQ(harness.canvas.GetSelection().size(), 1u);
	EXPECT_TRUE(harness.Selected(GraphItemKind::Node, 1));
	EXPECT_EQ(harness.model.selectionChanges, changes);

	harness.canvas.SelectAll();
	EXPECT_EQ(harness.canvas.GetSelection().size(), 2u);
	EXPECT_EQ(harness.model.selectionChanges, changes);
}

TEST(GraphCanvasOpsTest, MarqueeOverlapsAreaAndSegments) {
	const UI::Rect box = GraphCanvasOps::RectBetween(Vec2(100.0f, 80.0f), Vec2(20.0f, 20.0f));
	EXPECT_FLOAT_EQ(box.x, 20.0f);
	EXPECT_FLOAT_EQ(box.w, 80.0f);

	EXPECT_TRUE(GraphCanvasOps::RectsOverlap(box, UI::Rect{90.0f, 70.0f, 50.0f, 50.0f}));
	EXPECT_FALSE(GraphCanvasOps::RectsOverlap(box, UI::Rect{100.0f, 20.0f, 50.0f, 50.0f}));

	EXPECT_TRUE(GraphCanvasOps::SegmentTouchesRect(Vec2(0.0f, 50.0f), Vec2(200.0f, 50.0f), box));
	EXPECT_TRUE(GraphCanvasOps::SegmentTouchesRect(Vec2(50.0f, 50.0f), Vec2(300.0f, 300.0f), box));
	EXPECT_FALSE(GraphCanvasOps::SegmentTouchesRect(Vec2(0.0f, 0.0f), Vec2(10.0f, 200.0f), box));
	EXPECT_FALSE(GraphCanvasOps::SegmentTouchesRect(Vec2(0.0f, 10.0f), Vec2(300.0f, 10.0f), box));
	EXPECT_FALSE(GraphCanvasOps::SegmentTouchesRect(Vec2(0.0f, 0.0f), Vec2(1.0f, 1.0f), UI::Rect{0.0f, 0.0f, 0.0f, 0.0f}));
}

TEST(GraphCanvasTest, SpaceSearcherEnterPlacesMatch) {
	CanvasHarness harness;
	ThreeCards(harness.model);
	SmallLibrary(harness.model);
	harness.Build();

	const Vec2 aimLocal(520.0f, 330.0f);
	harness.Click(harness.ToScreen(aimLocal));
	harness.Press(Key::Space, KeyMod::None, U' ');
	const UI::ElementHandle card = harness.Searcher();
	ASSERT_TRUE(harness.context.IsAlive(card));
	EXPECT_FALSE(harness.model.searchedFromPort);
	const UI::ElementHandle field = harness.context.GetFocused();
	ASSERT_TRUE(harness.context.HasTextInput(field));
	EXPECT_EQ(harness.context.GetTextString(field), "");

	harness.Type("times");
	EXPECT_EQ(harness.context.GetTextString(field), "times");
	harness.Press(Key::Enter);
	ASSERT_EQ(harness.model.picks, 1u);
	EXPECT_EQ(harness.model.pickedKey, 3u);
	EXPECT_FALSE(harness.model.pickedWithPort);
	const Vec2 expected = GraphCanvasOps::LocalToGraph(harness.canvas.GetView(), aimLocal);
	EXPECT_NEAR(harness.model.pickedAt.x, expected.x, 1.0f);
	EXPECT_NEAR(harness.model.pickedAt.y, expected.y, 1.0f);

	harness.context.Update();
	EXPECT_EQ(harness.context.GetFocused(), harness.canvas.GetElement());
	harness.Press(Key::Space, KeyMod::None, U' ');
	EXPECT_EQ(harness.context.GetOpenPopupCount(), 1u);
	harness.Press(Key::Escape);
	harness.context.Update();
	EXPECT_EQ(harness.context.GetFocused(), harness.canvas.GetElement());

	harness.Click(harness.ToScreen(aimLocal));
	harness.Press(Key::Space, KeyMod::None, U' ');
	harness.Type("s");
	harness.Press(Key::Down);
	harness.Press(Key::Enter);
	ASSERT_EQ(harness.model.picks, 2u);
	EXPECT_EQ(harness.model.pickedKey, 2u);

	harness.Click(harness.ToScreen(aimLocal));
	harness.Press(Key::Space, KeyMod::None, U' ');
	harness.Type("sine");
	const UI::ElementHandle sine = FindByText(harness.context, harness.Searcher(), "Sine");
	ASSERT_TRUE(harness.context.IsAlive(sine));
	ClickElement(harness.context, sine);
	ASSERT_EQ(harness.model.picks, 3u);
	EXPECT_EQ(harness.model.pickedKey, 5u);
}

TEST(GraphCanvasTest, SearcherHighlightsTabCompletes) {
	CanvasHarness harness;
	ThreeCards(harness.model);
	SmallLibrary(harness.model);
	harness.Build();
	harness.Click(harness.ToScreen(Vec2(520.0f, 330.0f)));
	harness.Press(Key::Space, KeyMod::None, U' ');
	const UI::ElementHandle card = harness.Searcher();
	ASSERT_TRUE(harness.context.IsAlive(card));
	const UI::ElementHandle field = harness.context.GetFocused();

	harness.Type("mul");
	EXPECT_TRUE(harness.context.IsAlive(FindByText(harness.context, card, "Mul")));
	EXPECT_TRUE(harness.context.IsAlive(FindByText(harness.context, card, "tiply")));
	EXPECT_FALSE(harness.context.IsAlive(FindByText(harness.context, card, "Multiply")));

	harness.Press(Key::Tab, KeyMod::None, U'\t');
	EXPECT_EQ(harness.context.GetTextString(field), "multiply");
	EXPECT_TRUE(harness.context.IsAlive(FindByText(harness.context, card, "Multiply")));

	harness.Press(Key::A, KeyMod::Ctrl);
	harness.Type("times");
	EXPECT_TRUE(harness.context.IsAlive(FindByText(harness.context, card, "Multiply")));
	harness.Press(Key::Tab, KeyMod::None, U'\t');
	EXPECT_EQ(harness.context.GetTextString(field), "times");
	harness.Press(Key::Enter);
	ASSERT_EQ(harness.model.picks, 1u);
	EXPECT_EQ(harness.model.pickedKey, 3u);
}

TEST(GraphCanvasTest, SearcherBrowsesCategories) {
	CanvasHarness harness;
	ThreeCards(harness.model);
	SmallLibrary(harness.model);
	harness.Build();
	harness.Click(harness.GraphOnScreen(Vec2(600.0f, 400.0f)));
	harness.Press(Key::Space, KeyMod::None, U' ');
	const UI::ElementHandle card = harness.Searcher();
	ASSERT_TRUE(harness.context.IsAlive(card));
	EXPECT_TRUE(harness.context.IsAlive(FindByText(harness.context, card, "Math")));
	EXPECT_TRUE(harness.context.IsAlive(FindByText(harness.context, card, "Input")));
	EXPECT_FALSE(harness.context.IsAlive(FindByText(harness.context, card, "Add")));

	harness.Press(Key::Right);
	ASSERT_TRUE(harness.context.IsAlive(FindByText(harness.context, card, "Basic")));
	harness.Press(Key::Down);
	harness.Press(Key::Enter);
	ASSERT_TRUE(harness.context.IsAlive(FindByText(harness.context, card, "Add")));
	EXPECT_EQ(harness.model.picks, 0u);
	harness.Press(Key::Down);
	harness.Press(Key::Enter);
	ASSERT_EQ(harness.model.picks, 1u);
	EXPECT_EQ(harness.model.pickedKey, 2u);

	harness.Click(harness.GraphOnScreen(Vec2(600.0f, 400.0f)));
	harness.Press(Key::Space, KeyMod::None, U' ');
	ASSERT_EQ(harness.context.GetOpenPopupCount(), 1u);
	harness.Press(Key::Escape);
	harness.context.Update();
	EXPECT_EQ(harness.context.GetOpenPopupCount(), 0u);
	EXPECT_EQ(harness.model.picks, 1u);

	harness.model.library.clear();
	harness.Click(harness.GraphOnScreen(Vec2(600.0f, 400.0f)));
	harness.Press(Key::Space, KeyMod::None, U' ');
	EXPECT_EQ(harness.context.GetOpenPopupCount(), 0u);
}

TEST(GraphCanvasTest, FFramesSelectionAndFramesWholeGraph) {
	CanvasHarness harness;
	harness.model.nodes.push_back({1, Vec2(0.0f, 0.0f), 0, 0, str("Near")});
	harness.model.nodes.push_back({2, Vec2(2600.0f, 1800.0f), 0, 0, str("Far")});
	harness.Build();
	const UI::Rect content = harness.context.GetLayout(harness.canvas.GetElement()).content;
	const Vec2 viewSize(content.w / SCALE, content.h / SCALE);
	auto inView = [&](GraphNodeId node) {
		const Vec2 at = GraphCanvasOps::GraphToLocal(harness.canvas.GetView(), PositionOf(harness.model, node));
		return at.x >= 0.0f && at.y >= 0.0f && at.x <= viewSize.x && at.y <= viewSize.y;
	};
	ASSERT_FALSE(inView(2));

	harness.Click(harness.CardOnScreen(1));
	harness.Press(Key::F);
	EXPECT_TRUE(inView(1));
	EXPECT_FALSE(inView(2));
	EXPECT_FLOAT_EQ(harness.canvas.GetView().zoom, 1.0f);

	harness.Press(Key::A);
	EXPECT_TRUE(inView(1));
	EXPECT_TRUE(inView(2));
	EXPECT_LT(harness.canvas.GetView().zoom, 1.0f);

	harness.Click(harness.CardOnScreen(1));
	harness.Press(Key::F);
	ASSERT_FALSE(inView(2));
	harness.Click(harness.GraphOnScreen(PositionOf(harness.model, 1) + Vec2(-30.0f, 300.0f)));
	ASSERT_TRUE(harness.canvas.GetSelection().empty());
	harness.Press(Key::F);
	EXPECT_TRUE(inView(2));

	const GraphView before = harness.canvas.GetView();
	harness.Click(harness.CardOnScreen(1));
	harness.Press(Key::F);
	const GraphView framed = harness.canvas.GetView();
	harness.Press(Key::A, KeyMod::Ctrl);
	EXPECT_FLOAT_EQ(harness.canvas.GetView().zoom, framed.zoom);
	(void)before;
}

TEST(GraphCanvasTest, ACanvasThatGoesClosesItsSearcher) {
	MockFontSystem fonts;
	UI::Context context(CanvasHarness::Desc(), &fonts);
	EditorTheme::ApplyToContext(context);
	TestGraphModel model;
	SmallLibrary(model);
	model.nodes.push_back({1, Vec2(40.0f, 40.0f), 0, 0, str("One")});
	{
		GraphCanvas canvas;
		canvas.Build(context, context.GetRoot(), &model);
		context.Update();
		canvas.RebuildNodes(context);
		context.Update();
		const UI::Rect area = context.GetLayout(canvas.GetElement()).borderBox;
		InputEvent click{};
		click.mousePosPx = Vec2I(static_cast<i32>(area.x + area.w * 0.7f), static_cast<i32>(area.y + area.h * 0.7f));
		for (InputEventType type : {InputEventType::MouseMove, InputEventType::MouseDown, InputEventType::MouseUp}) {
			click.type = type;
			context.InjectInput(click);
			context.Update();
		}
		InputEvent space{};
		space.type = InputEventType::KeyDown;
		space.key = Key::Space;
		context.InjectInput(space);
		context.Update();
		ASSERT_EQ(context.GetOpenPopupCount(), 1u);
	}
	context.Update();
	EXPECT_EQ(context.GetOpenPopupCount(), 0u);
	EXPECT_EQ(model.picks, 0u);
}

namespace {

	void AGroupOfTwo(TestGraphModel& model) {
		ThreeCards(model);
		TestGraphModel::Group group;
		group.id = 50;
		group.title = "Wave";
		group.nodes.push_back(1);
		group.nodes.push_back(2);
		model.groups.push_back(std::move(group));
	}

	UI::Rect FrameAround(const CanvasHarness& harness, std::initializer_list<GraphNodeId> members) {
		Vector<UI::Rect> boxes{GetMemoryResource(MemTag::Editor)};
		for (const GraphNodeId member : members) {
			boxes.push_back(GraphCanvasOps::NodeRect(harness.canvas.GetView(), PositionOf(harness.model, member), Vec2(160.0f, 40.0f)));
		}
		UI::Rect frame{};
		EXPECT_TRUE(GraphCanvasOps::GroupFrame(boxes, harness.canvas.GetView().zoom, frame));
		return frame;
	}

	Vec2I TitleBarOnScreen(CanvasHarness& harness, const UI::Rect& frame) {
		return harness.ToScreen(Vec2(frame.x + 40.0f, frame.y + GraphCanvasOps::GROUP_TITLE_HEIGHT * 0.5f));
	}

} // anonymous namespace

TEST(GraphCanvasOpsTest, OutlinePadsMembersTitleOnTop) {
	const UI::Rect members[] = {{100.0f, 100.0f, 50.0f, 20.0f}, {300.0f, 180.0f, 40.0f, 40.0f}};
	UI::Rect frame{};
	ASSERT_TRUE(GraphCanvasOps::OutlineFrame(members, 2.0f, frame));
	const f32 pad = GraphCanvasOps::GROUP_PADDING * 0.5f * 2.0f;
	EXPECT_FLOAT_EQ(frame.x, 100.0f - pad);
	EXPECT_FLOAT_EQ(frame.y, 100.0f - pad);
	EXPECT_FLOAT_EQ(frame.x + frame.w, 340.0f + pad);
	EXPECT_FLOAT_EQ(frame.y + frame.h, 220.0f + pad);
	const UI::Rect bar = GraphCanvasOps::OutlineTitleBar(frame, 2.0f);
	EXPECT_FLOAT_EQ(bar.y + bar.h, frame.y);
	EXPECT_FLOAT_EQ(bar.h, GraphCanvasOps::GROUP_TITLE_HEIGHT * 2.0f);
	EXPECT_FALSE(GraphCanvasOps::OutlineFrame({}, 1.0f, frame));
}

TEST(GraphCanvasOpsTest, GroupFramePadsMembers) {
	const UI::Rect members[] = {{100.0f, 100.0f, 50.0f, 20.0f}, {300.0f, 180.0f, 40.0f, 40.0f}};
	UI::Rect frame{};
	ASSERT_TRUE(GraphCanvasOps::GroupFrame(members, 2.0f, frame));
	const f32 pad = GraphCanvasOps::GROUP_PADDING * 2.0f;
	const f32 title = GraphCanvasOps::GROUP_TITLE_HEIGHT * 2.0f;
	EXPECT_FLOAT_EQ(frame.x, 100.0f - pad);
	EXPECT_FLOAT_EQ(frame.y, 100.0f - pad - title);
	EXPECT_FLOAT_EQ(frame.x + frame.w, 340.0f + pad);
	EXPECT_FLOAT_EQ(frame.y + frame.h, 220.0f + pad);
	const UI::Rect bar = GraphCanvasOps::GroupTitleBar(frame, 2.0f);
	EXPECT_FLOAT_EQ(bar.y, frame.y);
	EXPECT_FLOAT_EQ(bar.h, title);
	UI::Rect untouched{1.0f, 2.0f, 3.0f, 4.0f};
	EXPECT_FALSE(GraphCanvasOps::GroupFrame({}, 1.0f, untouched));
	EXPECT_FLOAT_EQ(untouched.x, 1.0f);
}

TEST(GraphCanvasTest, GroupHitByTitleFrameFollows) {
	CanvasHarness harness;
	AGroupOfTwo(harness.model);
	harness.Build();
	const UI::Rect frame = FrameAround(harness, {1, 2});
	UI::ElementHandle title = FindByText(harness.context, harness.canvas.GetElement(), "Wave");
	ASSERT_TRUE(harness.context.IsAlive(title));
	const UI::Rect drawn = harness.context.GetLayout(title).borderBox;
	EXPECT_NEAR(drawn.x, harness.ToScreen(Vec2(frame.x, frame.y)).x, 1.5f);
	EXPECT_NEAR(drawn.y, harness.ToScreen(Vec2(frame.x, frame.y)).y, 1.5f);

	harness.Click(TitleBarOnScreen(harness, frame));
	EXPECT_TRUE(harness.Selected(GraphItemKind::Group, 50));
	const Vec2 inside(frame.x + frame.w * 0.5f, frame.y + frame.h - 6.0f);
	harness.Click(harness.ToScreen(inside));
	EXPECT_TRUE(harness.canvas.GetSelection().empty());

	harness.Drag(harness.CardOnScreen(1), Vec2I(harness.CardOnScreen(1).x, harness.CardOnScreen(1).y + 250));
	const UI::Rect grown = FrameAround(harness, {1, 2});
	EXPECT_GT(grown.h, frame.h);
	const UI::Rect after = harness.context.GetLayout(title).borderBox;
	EXPECT_NEAR(after.y, harness.ToScreen(Vec2(grown.x, grown.y)).y, 1.5f);
}

TEST(GraphCanvasTest, GroupTitleDragIsOneBracket) {
	CanvasHarness harness;
	AGroupOfTwo(harness.model);
	TestGraphModel::Note note;
	note.id = 60;
	note.position = Vec2(60.0f, 140.0f);
	note.size = Vec2(120.0f, 60.0f);
	note.title = "here";
	harness.model.notes.push_back(note);
	harness.model.groups[0].notes.push_back(60);
	harness.Build();
	const Vec2 one = PositionOf(harness.model, 1);
	const Vec2 three = PositionOf(harness.model, 3);
	const Vec2I bar = TitleBarOnScreen(harness, FrameAround(harness, {1, 2}));
	harness.Drag(bar, Vec2I(bar.x + 50, bar.y + 25));
	EXPECT_EQ(harness.model.moveBegins, 1u);
	EXPECT_EQ(harness.model.moveEnds, 1u);
	EXPECT_GT(PositionOf(harness.model, 1).x, one.x);
	EXPECT_GT(harness.model.notes[0].position.x, 60.0f);
	EXPECT_EQ(PositionOf(harness.model, 3).x, three.x);
	EXPECT_EQ(harness.model.groupChanges, 0u);
}

TEST(GraphCanvasTest, DropOnGroupJoinsShiftLeaves) {
	CanvasHarness harness;
	AGroupOfTwo(harness.model);
	harness.Build();
	const UI::Rect frame = FrameAround(harness, {1, 2});
	harness.Drag(harness.CardOnScreen(3), harness.ToScreen(Vec2(frame.x + frame.w * 0.5f, frame.y + frame.h - 8.0f)));
	ASSERT_EQ(harness.model.groupChanges, 1u);
	EXPECT_EQ(harness.model.groupedInto, 50u);
	ASSERT_EQ(harness.model.groupedItems.size(), 1u);
	EXPECT_EQ(harness.model.groupedItems[0], (GraphItem{GraphItemKind::Node, 3}));

	harness.Drag(harness.CardOnScreen(1), Vec2I(harness.CardOnScreen(1).x - 10, harness.CardOnScreen(1).y), KeyMod::Shift);
	ASSERT_EQ(harness.model.groupChanges, 2u);
	EXPECT_EQ(harness.model.groupedInto, 0u);
	EXPECT_EQ(harness.model.groupedItems[0], (GraphItem{GraphItemKind::Node, 1}));

	harness.Drag(harness.CardOnScreen(2), Vec2I(harness.CardOnScreen(2).x + 5, harness.CardOnScreen(2).y + 5));
	EXPECT_EQ(harness.model.groupChanges, 2u);
}

TEST(GraphCanvasTest, MarqueeTakesGroupOnlyAcrossTitleBar) {
	CanvasHarness harness;
	AGroupOfTwo(harness.model);
	harness.Build();
	const UI::Rect frame = FrameAround(harness, {1, 2});
	const Vec2 one = GraphCanvasOps::GraphToLocal(harness.canvas.GetView(), PositionOf(harness.model, 1));
	harness.Drag(harness.ToScreen(Vec2(frame.x + 4.0f, one.y - 6.0f)), harness.ToScreen(Vec2(one.x + 40.0f, one.y + 20.0f)));
	EXPECT_TRUE(harness.Selected(GraphItemKind::Node, 1));
	EXPECT_FALSE(harness.Selected(GraphItemKind::Group, 50));
	harness.Drag(harness.ToScreen(Vec2(frame.x + 10.0f, frame.y - 10.0f)), harness.ToScreen(Vec2(frame.x + 60.0f, frame.y + 10.0f)));
	EXPECT_TRUE(harness.Selected(GraphItemKind::Group, 50));
}

TEST(GraphCanvasTest, OutlineTitleDoubleClickRenames) {
	CanvasHarness harness;
	ThreeCards(harness.model);
	GraphOutlineDesc titled;
	titled.nodes.push_back(1);
	titled.nodes.push_back(2);
	titled.id = 70;
	titled.title = "Sparks";
	harness.model.outlines.push_back(std::move(titled));
	GraphOutlineDesc untitled;
	untitled.nodes.push_back(3);
	harness.model.outlines.push_back(std::move(untitled));
	harness.Build();
	ASSERT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "Sparks")));

	const auto barOver = [&](std::initializer_list<GraphNodeId> members) {
		Vector<UI::Rect> boxes{GetMemoryResource(MemTag::Editor)};
		for (const GraphNodeId member : members) {
			boxes.push_back(GraphCanvasOps::NodeRect(harness.canvas.GetView(), PositionOf(harness.model, member), Vec2(160.0f, 40.0f)));
		}
		UI::Rect frame{};
		EXPECT_TRUE(GraphCanvasOps::OutlineFrame(boxes, harness.canvas.GetView().zoom, frame));
		const UI::Rect bar = GraphCanvasOps::OutlineTitleBar(frame, harness.canvas.GetView().zoom);
		return harness.ToScreen(Vec2(bar.x + 20.0f, bar.y + bar.h * 0.5f));
	};
	const Vec2I untitledBar = barOver({3});
	harness.Click(untitledBar);
	harness.Click(untitledBar);
	EXPECT_FALSE(harness.context.HasTextInput(harness.context.GetFocused()));

	const Vec2I bar = barOver({1, 2});
	harness.Click(bar);
	harness.Click(bar);
	const UI::ElementHandle field = harness.context.GetFocused();
	ASSERT_TRUE(harness.context.HasTextInput(field));
	EXPECT_EQ(harness.context.GetTextString(field), "Sparks");
	harness.Type("Smoke");
	harness.Press(Key::Enter);
	EXPECT_EQ(harness.model.renamedOutline, 70u);
	EXPECT_EQ(harness.model.renamedOutlineTo, "Smoke");
}

TEST(GraphCanvasTest, GroupTitleDoubleClickRenames) {
	CanvasHarness harness;
	AGroupOfTwo(harness.model);
	harness.Build();
	const Vec2I bar = TitleBarOnScreen(harness, FrameAround(harness, {1, 2}));
	harness.Click(bar);
	harness.Click(bar);
	const UI::ElementHandle field = harness.context.GetFocused();
	ASSERT_TRUE(harness.context.HasTextInput(field));
	EXPECT_EQ(harness.context.GetTextString(field), "Wave");
	harness.Type("Lighting");
	harness.Press(Key::Enter);
	EXPECT_EQ(harness.model.renamedGroup, 50u);
	EXPECT_EQ(harness.model.renamedTo, "Lighting");
	harness.context.Update();
	EXPECT_FALSE(harness.context.IsAlive(field));
	EXPECT_EQ(harness.context.GetFocused(), harness.canvas.GetElement());
}

TEST(GraphCanvasTest, NoteDragsSizesAndEdits) {
	CanvasHarness harness;
	TestGraphModel::Note note;
	note.id = 60;
	note.position = Vec2(300.0f, 200.0f);
	note.size = Vec2(200.0f, 120.0f);
	note.title = "Title";
	note.body = "body";
	harness.model.notes.push_back(note);
	harness.Build();
	auto noteLocal = [&](Vec2 within) {
		return GraphCanvasOps::GraphToLocal(harness.canvas.GetView(), harness.model.notes[0].position) + within;
	};

	harness.Drag(harness.ToScreen(noteLocal(Vec2(100.0f, 60.0f))), harness.ToScreen(noteLocal(Vec2(140.0f, 80.0f))));
	EXPECT_GT(harness.model.noteMoves, 0u);
	EXPECT_TRUE(harness.Selected(GraphItemKind::Note, 60));
	EXPECT_NEAR(harness.model.notes[0].position.x, 340.0f, 1.5f);

	const f32 grip = GraphCanvasOps::NOTE_GRIP * 0.5f;
	const Vec2 corner = noteLocal(Vec2(200.0f - grip, 120.0f - grip));
	const u32 begins = harness.model.moveBegins;
	harness.Drag(harness.ToScreen(corner), harness.ToScreen(corner + Vec2(50.0f, -200.0f)));
	ASSERT_GT(harness.model.resizes, 0u);
	EXPECT_NEAR(harness.model.resizedTo.x, 250.0f, 1.5f);
	EXPECT_FLOAT_EQ(harness.model.resizedTo.y, GraphCanvasOps::NOTE_MIN_HEIGHT);
	EXPECT_EQ(harness.model.moveBegins, begins + 1);
	EXPECT_EQ(harness.model.noteMoves, 3u);

	const Vec2I body = harness.ToScreen(noteLocal(Vec2(60.0f, 36.0f)));
	harness.Click(body);
	harness.Click(body);
	const UI::ElementHandle field = harness.context.GetFocused();
	ASSERT_TRUE(harness.context.HasTextInput(field));
	EXPECT_EQ(harness.context.GetTextString(field), "body");
	harness.Type("x");
	harness.Press(Key::Enter);
	harness.Type("y");
	EXPECT_EQ(harness.model.editedNote, 0u);
	harness.Click(harness.ToScreen(Vec2(700.0f, 500.0f)));
	EXPECT_EQ(harness.model.editedNote, 60u);
	EXPECT_EQ(harness.model.editedTitle, "Title");
	EXPECT_EQ(harness.model.editedBody, "x\ny");
}

TEST(GraphCanvasTest, RedirectPortsCentredWireDoubleClick) {
	CanvasHarness harness;
	harness.model.nodes.push_back({1, Vec2(40.0f, 100.0f), 0, 1, str("A")});
	TestGraphModel::Node redirect{2, Vec2(300.0f, 100.0f), 1, 1, str("")};
	redirect.compact = true;
	harness.model.nodes.push_back(redirect);
	harness.model.nodes.push_back({3, Vec2(500.0f, 100.0f), 1, 0, str("B")});
	GraphWireDesc wire;
	wire.id = 9;
	wire.style = GraphWireStyle::Curve;
	wire.from = GraphPortRef{2, GraphPortSide::Output, 0};
	wire.to = GraphPortRef{3, GraphPortSide::Input, 0};
	harness.model.wires.push_back(wire);
	harness.Build();

	const GraphView& view = harness.canvas.GetView();
	const UI::Rect box = GraphCanvasOps::NodeRect(view, Vec2(300.0f, 100.0f), Vec2(24.0f, GraphCanvasOps::PORT_ROW_HEIGHT));
	const Vec2 input = GraphCanvasOps::PortAnchor(box, GraphPortSide::Input, 0, view.zoom, 0.0f);
	EXPECT_NEAR(input.y, box.y + box.h * 0.5f, 0.01f);
	const Vec2 output = GraphCanvasOps::PortAnchor(
		GraphCanvasOps::NodeRect(view, Vec2(40.0f, 100.0f), Vec2(160.0f, GraphCanvasOps::PortNodeHeight(1))), GraphPortSide::Output, 0,
		view.zoom);
	harness.Drag(harness.ToScreen(output), harness.ToScreen(input));
	ASSERT_EQ(harness.model.connects, 1u);
	EXPECT_EQ(harness.model.connectedInput, (GraphPortRef{2, GraphPortSide::Input, 0}));

	const Vec2 from = GraphCanvasOps::PortAnchor(box, GraphPortSide::Output, 0, view.zoom, 0.0f);
	const Vec2 to = GraphCanvasOps::PortAnchor(
		GraphCanvasOps::NodeRect(view, Vec2(500.0f, 100.0f), Vec2(160.0f, GraphCanvasOps::PortNodeHeight(1))), GraphPortSide::Input, 0,
		view.zoom);
	const Vec2 onWire = GraphCanvasOps::Evaluate(GraphCanvasOps::PortWire(from, to, view.zoom), 0.5f);
	harness.Click(harness.ToScreen(onWire));
	harness.Click(harness.ToScreen(onWire));
	EXPECT_EQ(harness.model.wireDoubleClicks, 1u);
	EXPECT_EQ(harness.model.doubleClickedWire, 9u);
}

TEST(GraphCanvasTest, GroupChordsReachGraphKind) {
	CanvasHarness harness;
	ThreeCards(harness.model);
	harness.Build();
	harness.Click(harness.CardOnScreen(1));
	harness.Press(Key::G, KeyMod::Ctrl);
	ASSERT_EQ(harness.model.commands, 1u);
	EXPECT_EQ(harness.model.lastCommand, GraphCommand::Group);
	harness.Press(Key::U, KeyMod::Ctrl);
	EXPECT_EQ(harness.model.lastCommand, GraphCommand::Ungroup);
	harness.Press(Key::R, KeyMod::Ctrl);
	EXPECT_EQ(harness.model.commands, 2u);
}

namespace {

	void OnePortedNode(TestGraphModel& model, u32 inputs, GraphNodePreview preview) {
		TestGraphModel::Node node;
		node.id = 1;
		node.position = Vec2(100.0f, 100.0f);
		node.inputs = inputs;
		node.outputs = 1;
		node.label = "Ported";
		node.preview = preview;
		model.nodes.push_back(std::move(node));
	}

	template <typename Harness>
	UI::Rect OnScreen(Harness& harness, const UI::Rect& local) {
		const Vec2I corner = harness.ToScreen(Vec2(local.x, local.y));
		return UI::Rect{static_cast<f32>(corner.x), static_cast<f32>(corner.y), local.w * SCALE, local.h * SCALE};
	}

} // anonymous namespace

TEST(GraphCanvasTest, PreviewNodeGrowsWell) {
	CanvasHarness harness;
	OnePortedNode(harness.model, 1, GraphNodePreview::Expanded);
	harness.Build();

	const GraphView& view = harness.canvas.GetView();
	const UI::Rect box = GraphCanvasOps::NodeRect(view, Vec2(100.0f, 100.0f),
												  Vec2(160.0f, GraphCanvasOps::PreviewNodeHeight(1, 160.0f, true)));
	const UI::Rect expected = OnScreen(harness, GraphCanvasOps::PreviewWell(box, 1, view.zoom));
	UI::ElementHandle well = FindByClass(harness.context, harness.root, EditorTheme::Class::GraphPreviewWell, 0);
	ASSERT_TRUE(harness.context.IsAlive(well));
	const UI::Rect drawn = harness.context.GetLayout(well).borderBox;
	EXPECT_NEAR(drawn.x, expected.x, 1.0f);
	EXPECT_NEAR(drawn.y, expected.y, 1.0f);
	EXPECT_NEAR(drawn.w, expected.w, 1.0f);
	EXPECT_NEAR(drawn.h, expected.h, 1.0f);

	UI::RenderList list;
	harness.context.BuildRenderList(list);
	ASSERT_GE(harness.model.previewPaints, 1u);
	EXPECT_NEAR(harness.model.paintedWell.y, expected.y, 1.0f);
	EXPECT_NEAR(harness.model.paintedWell.h, expected.h, 1.0f);
}

TEST(GraphCanvasTest, StripFoldsWell) {
	CanvasHarness harness;
	OnePortedNode(harness.model, 1, GraphNodePreview::Expanded);
	harness.Build();
	UI::ElementHandle strip = FindByClass(harness.context, harness.root, EditorTheme::Class::GraphPreviewStrip, 0);
	ASSERT_TRUE(harness.context.IsAlive(strip));
	ClickElement(harness.context, strip);
	ASSERT_EQ(harness.model.previewToggles, 1u);
	EXPECT_EQ(harness.model.toggledNode, 1u);
	EXPECT_FALSE(harness.model.toggledTo);

	harness.model.nodes[0].preview = GraphNodePreview::Collapsed;
	harness.canvas.RebuildNodes(harness.context);
	harness.context.Update();
	EXPECT_FALSE(harness.context.IsAlive(FindByClass(harness.context, harness.root, EditorTheme::Class::GraphPreviewWell, 0)));
	strip = FindByClass(harness.context, harness.root, EditorTheme::Class::GraphPreviewStrip, 0);
	ASSERT_TRUE(harness.context.IsAlive(strip));
	ClickElement(harness.context, strip);
	EXPECT_TRUE(harness.model.toggledTo);
}

TEST(GraphCanvasTest, OpenInputsFieldEditsComponentTypedOver) {
	CanvasHarness harness;
	OnePortedNode(harness.model, 2, GraphNodePreview::None);
	GraphInlineValue vector;
	vector.kind = GraphInlineKind::Numbers;
	vector.count = 2;
	vector.values[0] = 0.5f;
	vector.values[1] = 2.0f;
	harness.model.nodes[0].inlineValues.push_back(vector);
	harness.model.nodes[0].inlineValues.push_back(vector);
	harness.model.nodes[0].inputConnected.push_back(false);
	harness.model.nodes[0].inputConnected.push_back(true);
	harness.Build();

	ASSERT_EQ(CountByClass(harness.context, harness.root, EditorTheme::Class::GraphInlineField), 1u);
	UI::ElementHandle field = FindByClass(harness.context, harness.root, EditorTheme::Class::GraphInlineField, 0);
	const GraphView& view = harness.canvas.GetView();
	const UI::Rect box = GraphCanvasOps::NodeRect(view, Vec2(100.0f, 100.0f), Vec2(160.0f, GraphCanvasOps::PortNodeHeight(2)));
	const UI::Rect expected =
		OnScreen(harness, GraphCanvasOps::InlineField(box, 0, GraphCanvasOps::INLINE_COMPONENT_WIDTH * 2.0f, view.zoom));
	const UI::Rect drawn = harness.context.GetLayout(field).borderBox;
	EXPECT_NEAR(drawn.x, expected.x, 1.0f);
	EXPECT_NEAR(drawn.y, expected.y, 1.0f);
	ASSERT_TRUE(harness.context.IsAlive(FindByText(harness.context, field, "0.5")));

	const Vec2I second(static_cast<i32>(drawn.x + drawn.w * 0.75f), static_cast<i32>(drawn.y + drawn.h * 0.5f));
	harness.Click(second);
	for (i32 erase = 0; erase < 4; ++erase) {
		harness.Press(Key::Backspace);
	}
	harness.Type("0.75");
	harness.Press(Key::Enter);
	ASSERT_EQ(harness.model.inlineSets, 1u);
	EXPECT_EQ(harness.model.inlinePort, (GraphPortRef{1, GraphPortSide::Input, 0}));
	ASSERT_EQ(harness.model.inlineValues.size(), 2u);
	EXPECT_FLOAT_EQ(harness.model.inlineValues[0], 0.5f);
	EXPECT_FLOAT_EQ(harness.model.inlineValues[1], 0.75f);
}

TEST(GraphCanvasTest, ToggleFieldFlipsAndLabelTakesNothing) {
	CanvasHarness harness;
	OnePortedNode(harness.model, 2, GraphNodePreview::None);
	GraphInlineValue toggle;
	toggle.kind = GraphInlineKind::Toggle;
	toggle.values[0] = 1.0f;
	GraphInlineValue label;
	label.kind = GraphInlineKind::Label;
	label.text = "UV0";
	harness.model.nodes[0].inlineValues.push_back(toggle);
	harness.model.nodes[0].inlineValues.push_back(label);
	harness.Build();

	UI::ElementHandle first = FindByClass(harness.context, harness.root, EditorTheme::Class::GraphInlineField, 0);
	ASSERT_TRUE(harness.context.IsAlive(first));
	ClickElement(harness.context, first);
	ASSERT_EQ(harness.model.inlineSets, 1u);
	ASSERT_EQ(harness.model.inlineValues.size(), 1u);
	EXPECT_FLOAT_EQ(harness.model.inlineValues[0], 0.0f);

	UI::ElementHandle second = FindByClass(harness.context, harness.root, EditorTheme::Class::GraphInlineField, 1);
	ASSERT_TRUE(harness.context.IsAlive(second));
	ASSERT_TRUE(harness.context.IsAlive(FindByText(harness.context, second, "UV0")));
	ClickElement(harness.context, second);
	EXPECT_EQ(harness.model.inlineSets, 1u);
}

TEST(GraphCanvasTest, TopNodeCoversAndTakesPress) {
	CanvasHarness harness;
	OnePortedNode(harness.model, 1, GraphNodePreview::None);
	GraphInlineValue number;
	number.kind = GraphInlineKind::Numbers;
	number.count = 1;
	number.values[0] = 0.5f;
	harness.model.nodes[0].inlineValues.push_back(number);
	TestGraphModel::Node over;
	over.id = 2;
	over.position = Vec2(-60.0f, 60.0f);
	over.inputs = 4;
	over.outputs = 1;
	over.label = "Over";
	harness.model.nodes.push_back(std::move(over));
	harness.Build();

	const UI::ElementHandle field = FindByClass(harness.context, harness.root, EditorTheme::Class::GraphInlineField, 0);
	ASSERT_TRUE(harness.context.IsAlive(field));
	const UI::Rect drawn = harness.context.GetLayout(field).borderBox;
	const Vec2I middle(static_cast<i32>(drawn.x + drawn.w * 0.5f), static_cast<i32>(drawn.y + drawn.h * 0.5f));
	const UI::Element* title = harness.context.GetElement(FindByText(harness.context, harness.root, "Over"));
	ASSERT_NE(title, nullptr);
	const UI::Rect covering = harness.context.GetLayout(harness.context.MakeHandle(title->parent)).borderBox;
	ASSERT_TRUE(middle.x > covering.x && middle.x < covering.x + covering.w && middle.y > covering.y && middle.y < covering.y + covering.h);

	harness.Click(middle);
	EXPECT_TRUE(harness.Selected(GraphItemKind::Node, 2));
	EXPECT_EQ(harness.model.inlineSets, 0u);
	harness.Press(Key::Enter);
	EXPECT_EQ(harness.model.inlineSets, 0u);
}

namespace {

	struct Drawn {
		u32 quads = 0;
		u32 colors = 0;
	};

	Drawn DrawnInside(UI::Context& context, const UI::Rect& box) {
		UI::RenderList list;
		context.BuildRenderList(list);
		Drawn drawn;
		Vector<Color> seen{GetMemoryResource(MemTag::Editor)};
		for (const UI::UIQuad& quad : list.quads) {
			const Vec2 centre(quad.rect.x + quad.rect.w * 0.5f, quad.rect.y + quad.rect.h * 0.5f);
			if (centre.x < box.x || centre.x > box.x + box.w || centre.y < box.y || centre.y > box.y + box.h) {
				continue;
			}
			++drawn.quads;
			if (std::find(seen.begin(), seen.end(), quad.color0) == seen.end()) {
				seen.push_back(quad.color0);
			}
		}
		drawn.colors = static_cast<u32>(seen.size());
		return drawn;
	}

} // anonymous namespace

TEST(GraphCanvasTest, CurveFieldClickEdits) {
	CanvasHarness harness;
	OnePortedNode(harness.model, 1, GraphNodePreview::None);
	GraphInlineValue curve;
	curve.kind = GraphInlineKind::Curve;
	Keyframe low;
	Keyframe high;
	high.time = 1.0f;
	high.value = 1.0f;
	curve.curve.keys.push_back(low);
	curve.curve.keys.push_back(high);
	harness.model.nodes[0].inlineValues.push_back(curve);
	harness.Build();

	UI::ElementHandle field = FindByClass(harness.context, harness.root, EditorTheme::Class::GraphInlineField, 0);
	ASSERT_TRUE(harness.context.IsAlive(field));
	EXPECT_GE(DrawnInside(harness.context, harness.context.GetLayout(field).borderBox).quads, 12u);

	ClickElement(harness.context, field);
	ASSERT_EQ(harness.model.inlineEditors, 1u);
	EXPECT_EQ(harness.model.editedPort, (GraphPortRef{1, GraphPortSide::Input, 0}));
	EXPECT_TRUE(harness.model.editorAnchored);
	EXPECT_EQ(harness.model.inlineSets, 0u);
}

TEST(GraphCanvasTest, TheClipboardChordsReachTheGraphKind) {
	CanvasHarness harness;
	ThreeCards(harness.model);
	harness.Build();
	harness.Click(harness.CardOnScreen(1));
	harness.Press(Key::C, KeyMod::Ctrl);
	EXPECT_EQ(harness.model.lastCommand, GraphCommand::Copy);
	harness.Press(Key::X, KeyMod::Ctrl);
	EXPECT_EQ(harness.model.lastCommand, GraphCommand::Cut);
	harness.Press(Key::V, KeyMod::Ctrl);
	EXPECT_EQ(harness.model.lastCommand, GraphCommand::Paste);
	EXPECT_EQ(harness.model.commands, 3u);
}

TEST(GraphCanvasOpsTest, NodeReadsTopToBottom) {
	const GraphCanvasOps::GraphNodeLayout plain = GraphCanvasOps::NodeLayout(false, 0, 3, 0.0f, false);
	EXPECT_FLOAT_EQ(plain.height, GraphCanvasOps::PortNodeHeight(3));
	EXPECT_FLOAT_EQ(plain.rows, GraphCanvasOps::NODE_HEADER_HEIGHT);

	const std::array<f32, 2> blocks{30.0f, 50.0f};
	const f32 stack = GraphCanvasOps::StackHeight(blocks);
	const GraphCanvasOps::GraphNodeLayout context = GraphCanvasOps::NodeLayout(true, 2, 1, stack, true);
	EXPECT_FLOAT_EQ(context.header, GraphCanvasOps::FLOW_STRIP_HEIGHT);
	EXPECT_FLOAT_EQ(context.settings, context.header + GraphCanvasOps::NODE_HEADER_HEIGHT);
	EXPECT_FLOAT_EQ(context.rows, context.settings + 2.0f * GraphCanvasOps::SETTING_ROW_HEIGHT);
	EXPECT_GE(context.stack, context.rows + GraphCanvasOps::PORT_ROW_HEIGHT);
	EXPECT_FLOAT_EQ(context.height, context.stack + stack + GraphCanvasOps::FLOW_STRIP_HEIGHT);

	EXPECT_GT(GraphCanvasOps::StackHeight({}), GraphCanvasOps::STACK_PADDING * 2.0f);
	EXPECT_FLOAT_EQ(GraphCanvasOps::BlockHeight(1, 2),
					GraphCanvasOps::BLOCK_HEADER_HEIGHT + GraphCanvasOps::SETTING_ROW_HEIGHT + 2.0f * GraphCanvasOps::PORT_ROW_HEIGHT);
}

TEST(GraphCanvasOpsTest, BlockDropsInNearestGap) {
	const std::array<f32, 3> blocks{30.0f, 50.0f, 30.0f};
	const f32 first = GraphCanvasOps::BlockTop(blocks, 0);
	const f32 second = GraphCanvasOps::BlockTop(blocks, 1);
	const f32 third = GraphCanvasOps::BlockTop(blocks, 2);
	EXPECT_FLOAT_EQ(second - first, 30.0f + GraphCanvasOps::BLOCK_GAP);
	EXPECT_FLOAT_EQ(third - second, 50.0f + GraphCanvasOps::BLOCK_GAP);

	EXPECT_EQ(GraphCanvasOps::StackGap(blocks, 0.0f), 0u);
	EXPECT_EQ(GraphCanvasOps::StackGap(blocks, first + 14.0f), 0u);
	EXPECT_EQ(GraphCanvasOps::StackGap(blocks, first + 16.0f), 1u);
	EXPECT_EQ(GraphCanvasOps::StackGap(blocks, second + 26.0f), 2u);
	EXPECT_EQ(GraphCanvasOps::StackGap(blocks, third + 20.0f), 3u);
	EXPECT_EQ(GraphCanvasOps::StackGap({}, 40.0f), 0u);

	const f32 between = GraphCanvasOps::StackGapLine(blocks, 1);
	EXPECT_GT(between, first + 30.0f);
	EXPECT_LT(between, second);
	EXPECT_LT(GraphCanvasOps::StackGapLine(blocks, 0), first);
	EXPECT_GT(GraphCanvasOps::StackGapLine(blocks, 3), third + 30.0f);
}

TEST(GraphCanvasOpsTest, FlowLinkLeavesDownward) {
	const UI::Rect box{100.0f, 50.0f, 200.0f, 120.0f};
	const Vec2 start = GraphCanvasOps::FlowAnchor(box, GraphPortSide::FlowInput, 0, 2, 1.0f);
	const Vec2 stop = GraphCanvasOps::FlowAnchor(box, GraphPortSide::FlowInput, 1, 2, 1.0f);
	const Vec2 out = GraphCanvasOps::FlowAnchor(box, GraphPortSide::FlowOutput, 0, 1, 1.0f);
	EXPECT_FLOAT_EQ(start.x, 150.0f);
	EXPECT_FLOAT_EQ(stop.x, 250.0f);
	EXPECT_FLOAT_EQ(out.x, 200.0f);
	EXPECT_LT(start.y, box.y + GraphCanvasOps::FLOW_STRIP_HEIGHT);
	EXPECT_GT(out.y, box.y + box.h - GraphCanvasOps::FLOW_STRIP_HEIGHT);
	EXPECT_LT(out.y, box.y + box.h);

	const GraphCubic down = GraphCanvasOps::FlowWire(Vec2(100.0f, 100.0f), Vec2(140.0f, 300.0f), 1.0f);
	EXPECT_FLOAT_EQ(down.p1.x, 100.0f);
	EXPECT_GT(down.p1.y, 100.0f);
	EXPECT_FLOAT_EQ(down.p2.x, 140.0f);
	EXPECT_LT(down.p2.y, 300.0f);
	const GraphCubic up = GraphCanvasOps::FlowWire(Vec2(100.0f, 300.0f), Vec2(100.0f, 100.0f), 1.0f);
	f32 lowest = 0.0f;
	for (u32 step = 0; step <= 32; ++step) {
		lowest = std::max(lowest, GraphCanvasOps::Evaluate(up, static_cast<f32>(step) / 32.0f).y);
	}
	EXPECT_GT(lowest, 300.0f);
}

namespace {

	class StackModel final : public GraphViewModel {
	public:
		static constexpr f32 WIDTH = 220.0f;

		struct Block {
			GraphNodeId id = 0;
			u32 settings = 0;
			bool enabled = true;
			bool gradient = false;
			str space;
			bool output = false;
		};
		struct Stack {
			GraphNodeId id = 0;
			Vec2 position{};
			u32 flowIn = 1;
			u32 flowOut = 1;
			u32 settings = 0;
			str action;
			bool takes = true;
			Vector<Block> blocks{GetMemoryResource(MemTag::Editor)};
		};
		Vector<Stack> stacks{GetMemoryResource(MemTag::Editor)};
		Vector<GraphSearchEntry> library{GetMemoryResource(MemTag::Editor)};
		Vector<GraphSearchEntry> blockLibrary{GetMemoryResource(MemTag::Editor)};

		u32 blockMoves = 0;
		Vector<GraphNodeId> movedBlocks{GetMemoryResource(MemTag::Editor)};
		GraphNodeId movedInto = 0;
		u32 movedGap = 99;
		u32 connects = 0;
		GraphPortRef connectedOutput{};
		GraphPortRef connectedInput{};
		u32 searches = 0;
		bool searchedFromPort = false;
		GraphPortRef searchedFrom{};
		u32 blockSearches = 0;
		GraphNodeId searchedStack = 0;
		u32 blockPicks = 0;
		GraphNodeId pickedStack = 0;
		u32 pickedGap = 99;
		u32 settingClicks = 0;
		GraphNodeId settingOwner = 0;
		u32 settingIndex = 99;
		u32 settingTexts = 0;
		str settingText;
		u32 settingActions = 0;
		u32 enables = 0;
		GraphNodeId enabledBlock = 0;
		bool enabledTo = true;
		u32 blockMenus = 0;
		GraphNodeId menuBlock = 0;
		u32 inlineSets = 0;
		GraphPortRef inlinePort{};
		u32 spaceClicks = 0;
		GraphPortRef spacePort{};
		u32 selectionChanges = 0;
		Vector<std::pair<GraphNodeId, EditorTheme::ColorToken>> marks{GetMemoryResource(MemTag::Editor)};

		EditorTheme::ColorToken GetNodeBorder(GraphNodeId node) const override {
			for (const auto& [id, mark] : marks) {
				if (id == node) {
					return mark;
				}
			}
			return EditorTheme::ColorToken::CardEdge;
		}
		Vector<std::pair<GraphNodeId, str>> tooltips{GetMemoryResource(MemTag::Editor)};
		str GetNodeTooltip(GraphNodeId node) const override {
			for (const auto& [id, words] : tooltips) {
				if (id == node) {
					return words;
				}
			}
			return {};
		}

		void GetNodes(Vector<GraphNodeDesc>& out) override {
			for (const Stack& stack : stacks) {
				GraphNodeDesc desc;
				desc.id = stack.id;
				desc.size = Vec2(WIDTH, 0.0f);
				desc.label = "Stack";
				desc.stack = true;
				for (u32 i = 0; i < stack.flowIn; ++i) {
					desc.flowInputs.push_back({stack.flowIn > 1 ? str(i == 0 ? "Start" : "Stop") : str(), false});
				}
				for (u32 i = 0; i < stack.flowOut; ++i) {
					desc.flowOutputs.push_back({});
				}
				for (u32 i = 0; i < stack.settings; ++i) {
					desc.settings.push_back({str("Setting"), str("Value"), i == 0 ? GraphSettingEdit::Menu : GraphSettingEdit::Text, i == 0 ? stack.action : str()});
				}
				for (const Block& held : stack.blocks) {
					GraphBlockDesc block;
					block.id = held.id;
					block.label = "Block";
					block.enabled = held.enabled;
					for (u32 i = 0; i < held.settings; ++i) {
						block.settings.push_back({str("Mode"), str("Set"), GraphSettingEdit::Menu});
					}
					GraphPortDesc input{str("Value"), EditorTheme::ColorToken::GraphPortFloat, false};
					input.inlineValue.kind = held.gradient ? GraphInlineKind::Gradient : GraphInlineKind::Numbers;
					input.inlineValue.count = 1;
					input.inlineValue.values[0] = 1.5f;
					if (held.gradient) {
						for (const Color color : {Color::Red(), Color::Green(), Color::Blue()}) {
							input.inlineValue.ramp.push_back(color);
						}
					}
					input.space = held.space;
					block.inputs.push_back(std::move(input));
					if (held.output) {
						block.outputs.push_back({str("Evt"), EditorTheme::ColorToken::GraphPortOther, false});
					}
					desc.blocks.push_back(std::move(block));
				}
				out.push_back(std::move(desc));
			}
		}
		bool GetNodePosition(GraphNodeId id, Vec2& out) const override {
			for (const Stack& stack : stacks) {
				if (stack.id == id) {
					out = stack.position;
					return true;
				}
			}
			return false;
		}
		void GetWires(Vector<GraphWireDesc>&) const override {}
		void MoveNode(GraphNodeId, Vec2) override {}
		void OnSelectionChanged() override { ++selectionChanges; }
		bool CanConnect(const GraphPortRef& output, const GraphPortRef& input) const override { return output.node != input.node; }
		void Connect(const GraphPortRef& output, const GraphPortRef& input) override {
			++connects;
			connectedOutput = output;
			connectedInput = input;
		}
		void GetSearchEntries(const GraphPortRef* from, Vector<GraphSearchEntry>& out) override {
			++searches;
			searchedFromPort = from != nullptr;
			searchedFrom = from ? *from : GraphPortRef{};
			out.assign(library.begin(), library.end());
		}
		bool CanMoveBlocks(std::span<const GraphNodeId>, GraphNodeId stack) const override {
			for (const Stack& held : stacks) {
				if (held.id == stack) {
					return held.takes;
				}
			}
			return false;
		}
		void MoveBlocks(std::span<const GraphNodeId> blocks, GraphNodeId stack, u32 index) override {
			++blockMoves;
			movedBlocks.assign(blocks.begin(), blocks.end());
			movedInto = stack;
			movedGap = index;
		}
		void GetBlockSearchEntries(GraphNodeId stack, Vector<GraphSearchEntry>& out) override {
			++blockSearches;
			searchedStack = stack;
			out.assign(blockLibrary.begin(), blockLibrary.end());
		}
		void OnBlockSearchPick(const GraphSearchEntry&, GraphNodeId stack, u32 index) override {
			++blockPicks;
			pickedStack = stack;
			pickedGap = index;
		}
		void OnSettingClick(UI::Context&, GraphNodeId owner, u32 setting) override {
			++settingClicks;
			settingOwner = owner;
			settingIndex = setting;
		}
		void SetSettingText(GraphNodeId owner, u32 setting, std::string_view text) override {
			++settingTexts;
			settingOwner = owner;
			settingIndex = setting;
			settingText = str(text);
		}
		void OnSettingAction(GraphNodeId owner, u32 setting) override {
			++settingActions;
			settingOwner = owner;
			settingIndex = setting;
		}
		void SetBlockEnabled(GraphNodeId block, bool enabled) override {
			++enables;
			enabledBlock = block;
			enabledTo = enabled;
		}
		void OnBlockContextMenu(UI::Context&, GraphNodeId block) override {
			++blockMenus;
			menuBlock = block;
		}
		void SetInlineValue(const GraphPortRef& input, std::span<const f32>) override {
			++inlineSets;
			inlinePort = input;
		}
		void OnPortSpaceClick(const GraphPortRef& input) override {
			++spaceClicks;
			spacePort = input;
		}
	};

	using StackHarness = BasicHarness<StackModel>;

	GraphSearchEntry Named(std::string_view name, u64 key) {
		GraphSearchEntry entry;
		entry.name = str(name);
		entry.category = "Test";
		entry.key = key;
		return entry;
	}

	void TwoStacks(StackModel& model) {
		StackModel::Stack a;
		a.id = 1;
		a.position = Vec2(40.0f, 40.0f);
		a.blocks.push_back({11});
		a.blocks.push_back({12});
		StackModel::Stack b;
		b.id = 2;
		b.position = Vec2(400.0f, 40.0f);
		b.blocks.push_back({21});
		model.stacks.push_back(std::move(a));
		model.stacks.push_back(std::move(b));
	}

	Vector<f32> HeightsOf(const StackModel::Stack& stack) {
		Vector<f32> heights{GetMemoryResource(MemTag::Editor)};
		for (const StackModel::Block& block : stack.blocks) {
			heights.push_back(GraphCanvasOps::BlockHeight(block.settings, 1));
		}
		return heights;
	}

	GraphCanvasOps::GraphNodeLayout LayoutOf(const StackModel::Stack& stack) {
		const Vector<f32> heights = HeightsOf(stack);
		return GraphCanvasOps::NodeLayout(stack.flowIn > 0, stack.settings, 0, GraphCanvasOps::StackHeight(heights), stack.flowOut > 0);
	}

	UI::Rect StackBox(StackHarness& harness, const StackModel::Stack& stack) {
		return GraphCanvasOps::NodeRect(harness.canvas.GetView(), stack.position, Vec2(StackModel::WIDTH, LayoutOf(stack).height));
	}

	UI::Rect BlockBoxOf(StackHarness& harness, const StackModel::Stack& stack, u32 index) {
		const f32 zoom = harness.canvas.GetView().zoom;
		const UI::Rect box = StackBox(harness, stack);
		const Vector<f32> heights = HeightsOf(stack);
		const f32 pad = GraphCanvasOps::STACK_PADDING * zoom;
		return UI::Rect{box.x + pad, box.y + (LayoutOf(stack).stack + GraphCanvasOps::BlockTop(heights, index)) * zoom, box.w - pad * 2.0f,
						heights[index] * zoom};
	}

	Vec2I BlockTitleOnScreen(StackHarness& harness, const StackModel::Stack& stack, u32 index) {
		const UI::Rect block = BlockBoxOf(harness, stack, index);
		return harness.ToScreen(Vec2(block.x + block.w * 0.6f, block.y + GraphCanvasOps::BLOCK_HEADER_HEIGHT * 0.5f * harness.canvas.GetView().zoom));
	}

	Vec2I FlowOnScreen(StackHarness& harness, const StackModel::Stack& stack, GraphPortSide side, u32 index, u32 count) {
		return harness.ToScreen(GraphCanvasOps::FlowAnchor(StackBox(harness, stack), side, index, count, harness.canvas.GetView().zoom));
	}

	Vec2 Centre(const UI::Rect& rect) {
		return Vec2(rect.x + rect.w * 0.5f, rect.y + rect.h * 0.5f);
	}

} // anonymous namespace

TEST(GraphCanvasTest, BlockPortPlacedPressSelectsBlock) {
	StackHarness harness;
	TwoStacks(harness.model);
	harness.Build();
	const StackModel::Stack& a = harness.model.stacks[0];
	const f32 zoom = harness.canvas.GetView().zoom;
	const UI::Rect block = BlockBoxOf(harness, a, 1);
	const Vec2 expected = GraphCanvasOps::PortAnchor(block, GraphPortSide::Input, 0, zoom, GraphCanvasOps::BLOCK_HEADER_HEIGHT);

	EXPECT_EQ(CountByClass(harness.context, harness.root, EditorTheme::Class::GraphInlineField), 3u);
	UI::ElementHandle field = FindByClass(harness.context, harness.root, EditorTheme::Class::GraphInlineField, 1);
	const UI::Rect drawn = harness.context.GetLayout(field).borderBox;
	EXPECT_NEAR(drawn.y + drawn.h * 0.5f, static_cast<f32>(harness.ToScreen(expected).y), 1.5f);

	harness.Click(BlockTitleOnScreen(harness, a, 1));
	EXPECT_TRUE(harness.Selected(GraphItemKind::Block, 12));
	EXPECT_FALSE(harness.Selected(GraphItemKind::Node, 1));
	EXPECT_EQ(harness.model.selectionChanges, 1u);
	harness.Click(BlockTitleOnScreen(harness, a, 0), KeyMod::Ctrl);
	EXPECT_TRUE(harness.Selected(GraphItemKind::Block, 11));
	EXPECT_TRUE(harness.Selected(GraphItemKind::Block, 12));
	const UI::Rect box = StackBox(harness, a);
	harness.Click(harness.ToScreen(Vec2(box.x + box.w * 0.5f, box.y + (LayoutOf(a).header + 4.0f) * zoom)));
	EXPECT_TRUE(harness.Selected(GraphItemKind::Node, 1));
	EXPECT_FALSE(harness.Selected(GraphItemKind::Block, 11));

	harness.Click(BlockTitleOnScreen(harness, a, 0));
	harness.canvas.RebuildNodes(harness.context);
	EXPECT_TRUE(harness.Selected(GraphItemKind::Block, 11));
	harness.model.stacks[0].blocks.erase(harness.model.stacks[0].blocks.begin());
	harness.canvas.RebuildNodes(harness.context);
	harness.context.Update();
	EXPECT_FALSE(harness.Selected(GraphItemKind::Block, 11));
}

TEST(GraphCanvasTest, SelectionRingBeatsMark) {
	StackHarness harness;
	TwoStacks(harness.model);
	harness.model.marks.push_back({12, EditorTheme::ColorToken::StatusError});
	harness.model.marks.push_back({2, EditorTheme::ColorToken::StatusWarn});
	harness.Build();
	ASSERT_EQ(CountByClass(harness.context, harness.root, EditorTheme::Class::GraphBlock), 3u);
	auto border = [&](u32 index) { return harness.context.GetStyle(FindByClass(harness.context, harness.root, EditorTheme::Class::GraphBlock, index)).borderColor; };
	const Color edge = EditorTheme::GetColor(EditorTheme::ColorToken::CardEdge);
	const Color error = EditorTheme::GetColor(EditorTheme::ColorToken::StatusError);
	ASSERT_NE(edge, error);
	EXPECT_EQ(border(0), edge);
	EXPECT_EQ(border(1), error);
	EXPECT_EQ(border(2), edge);

	const GraphItem block{GraphItemKind::Block, 12};
	harness.canvas.SetSelection(std::span<const GraphItem>(&block, 1));
	harness.canvas.PlaceNodes(harness.context);
	EXPECT_EQ(border(1), EditorTheme::GetColor(EditorTheme::ColorToken::SelectionWire));

	harness.canvas.SetSelection({});
	harness.model.marks.clear();
	harness.canvas.PlaceNodes(harness.context);
	EXPECT_EQ(border(1), edge);
}

TEST(GraphCanvasTest, MarkWordsShowOnHover) {
	FontAsset* font = New<FontAsset>(MemTag::Asset);
	UI::ContextDesc desc = StackHarness::Desc();
	desc.tooltipFont = Handle<FontAsset>(font);
	desc.tooltipDelaySeconds = 0.0f;
	{
		StackHarness harness(desc);
		TwoStacks(harness.model);
		harness.model.tooltips.push_back({12, str("This block does nothing")});
		harness.model.tooltips.push_back({2, str("A capacity over a million particles can make this system slow")});
		harness.Build();
		const auto shown = [&]() {
			const UI::ElementHandle card = FindByClass(harness.context, harness.context.GetRoot(), StringHash("slt-tooltip"), 0);
			const UI::Element* element = card.IsValid() ? harness.context.GetElement(card) : nullptr;
			return element && HasFlag(element->flags, UI::ElementFlags::Visible) ? str(harness.context.GetTextString(card)) : str();
		};
		harness.Inject(InputEventType::MouseMove, BlockTitleOnScreen(harness, harness.model.stacks[0], 1));
		EXPECT_EQ(shown(), "This block does nothing");
		harness.Inject(InputEventType::MouseMove, BlockTitleOnScreen(harness, harness.model.stacks[0], 0));
		EXPECT_EQ(shown(), "");
		const UI::Rect box = StackBox(harness, harness.model.stacks[1]);
		harness.Inject(InputEventType::MouseMove, harness.ToScreen(Vec2(box.x + box.w * 0.5f, box.y + 4.0f * harness.canvas.GetView().zoom)));
		EXPECT_EQ(shown(), "A capacity over a million particles can make this system slow");

		harness.model.tooltips.clear();
		harness.canvas.PlaceNodes(harness.context);
		harness.Inject(InputEventType::MouseMove, BlockTitleOnScreen(harness, harness.model.stacks[0], 1));
		EXPECT_EQ(shown(), "");
	}
	Delete(font);
}

TEST(GraphCanvasTest, BlockOutputOnRightEdge) {
	StackHarness harness;
	TwoStacks(harness.model);
	harness.model.stacks[0].blocks[1].output = true;
	harness.Build();
	const StackModel::Stack& a = harness.model.stacks[0];
	const StackModel::Stack& b = harness.model.stacks[1];
	const f32 zoom = harness.canvas.GetView().zoom;

	const UI::Rect block = BlockBoxOf(harness, a, 1);
	const Vec2 output = GraphCanvasOps::PortAnchor(block, GraphPortSide::Output, 0, zoom, GraphCanvasOps::BLOCK_HEADER_HEIGHT);
	const Vec2 input = GraphCanvasOps::PortAnchor(block, GraphPortSide::Input, 0, zoom, GraphCanvasOps::BLOCK_HEADER_HEIGHT);
	EXPECT_GT(output.x, block.x + block.w * 0.9f);
	EXPECT_FLOAT_EQ(output.y, input.y);

	const UI::Rect other = BlockBoxOf(harness, b, 0);
	const Vec2I otherInput = harness.ToScreen(GraphCanvasOps::PortAnchor(other, GraphPortSide::Input, 0, zoom, GraphCanvasOps::BLOCK_HEADER_HEIGHT));
	harness.Drag(harness.ToScreen(output), otherInput);
	ASSERT_EQ(harness.model.connects, 1u);
	EXPECT_EQ(harness.model.connectedOutput, (GraphPortRef{12, GraphPortSide::Output, 0}));
	EXPECT_EQ(harness.model.connectedInput, (GraphPortRef{21, GraphPortSide::Input, 0}));
	EXPECT_EQ(harness.model.blockMoves, 0u);

	harness.Drag(otherInput, harness.ToScreen(output));
	ASSERT_EQ(harness.model.connects, 2u);
	EXPECT_EQ(harness.model.connectedOutput, (GraphPortRef{12, GraphPortSide::Output, 0}));
	EXPECT_EQ(harness.model.connectedInput, (GraphPortRef{21, GraphPortSide::Input, 0}));

	EXPECT_EQ(CountByClass(harness.context, harness.root, EditorTheme::Class::GraphInlineField), 3u);
}

TEST(GraphCanvasTest, BlockDragLandsInPointerGap) {
	StackHarness harness;
	TwoStacks(harness.model);
	harness.Build();
	const StackModel::Stack& a = harness.model.stacks[0];
	const StackModel::Stack& b = harness.model.stacks[1];

	const UI::Rect target = BlockBoxOf(harness, b, 0);
	harness.Drag(BlockTitleOnScreen(harness, a, 0), harness.ToScreen(Vec2(target.x + target.w * 0.5f, target.y + target.h * 0.9f)));
	ASSERT_EQ(harness.model.blockMoves, 1u);
	EXPECT_EQ(harness.model.movedInto, 2u);
	EXPECT_EQ(harness.model.movedGap, 1u);
	ASSERT_EQ(harness.model.movedBlocks.size(), 1u);
	EXPECT_EQ(harness.model.movedBlocks[0], 11u);

	const UI::Rect first = BlockBoxOf(harness, a, 0);
	harness.Drag(BlockTitleOnScreen(harness, a, 1), harness.ToScreen(Vec2(first.x + first.w * 0.5f, first.y + 2.0f)));
	ASSERT_EQ(harness.model.blockMoves, 2u);
	EXPECT_EQ(harness.model.movedInto, 1u);
	EXPECT_EQ(harness.model.movedGap, 0u);

	harness.Click(BlockTitleOnScreen(harness, a, 1));
	harness.Click(BlockTitleOnScreen(harness, a, 0), KeyMod::Ctrl);
	harness.Drag(BlockTitleOnScreen(harness, a, 1), harness.ToScreen(Centre(BlockBoxOf(harness, b, 0))));
	ASSERT_EQ(harness.model.blockMoves, 3u);
	ASSERT_EQ(harness.model.movedBlocks.size(), 2u);
	EXPECT_EQ(harness.model.movedBlocks[0], 11u);
	EXPECT_EQ(harness.model.movedBlocks[1], 12u);

	harness.model.stacks[1].takes = false;
	harness.Click(BlockTitleOnScreen(harness, a, 0));
	harness.Drag(BlockTitleOnScreen(harness, a, 0), harness.ToScreen(Centre(BlockBoxOf(harness, b, 0))));
	harness.Drag(BlockTitleOnScreen(harness, a, 0), harness.GraphOnScreen(Vec2(300.0f, 400.0f)));
	EXPECT_EQ(harness.model.blockMoves, 3u);
}

TEST(GraphCanvasTest, FlowPortDragLinksOrSearches) {
	StackHarness harness;
	TwoStacks(harness.model);
	harness.model.stacks[1].flowIn = 2;
	harness.model.library.push_back(Named("Update", 1));
	harness.Build();
	const StackModel::Stack& a = harness.model.stacks[0];
	const StackModel::Stack& b = harness.model.stacks[1];

	harness.Drag(FlowOnScreen(harness, a, GraphPortSide::FlowOutput, 0, 1), FlowOnScreen(harness, b, GraphPortSide::FlowInput, 1, 2));
	ASSERT_EQ(harness.model.connects, 1u);
	EXPECT_EQ(harness.model.connectedOutput, (GraphPortRef{1, GraphPortSide::FlowOutput, 0}));
	EXPECT_EQ(harness.model.connectedInput, (GraphPortRef{2, GraphPortSide::FlowInput, 1}));
	harness.Drag(FlowOnScreen(harness, b, GraphPortSide::FlowInput, 0, 2), FlowOnScreen(harness, a, GraphPortSide::FlowOutput, 0, 1));
	ASSERT_EQ(harness.model.connects, 2u);
	EXPECT_EQ(harness.model.connectedOutput.side, GraphPortSide::FlowOutput);
	EXPECT_EQ(harness.model.connectedInput, (GraphPortRef{2, GraphPortSide::FlowInput, 0}));

	harness.Drag(FlowOnScreen(harness, a, GraphPortSide::FlowOutput, 0, 1),
				 harness.ToScreen(GraphCanvasOps::PortAnchor(BlockBoxOf(harness, b, 0), GraphPortSide::Input, 0, harness.canvas.GetView().zoom,
															 GraphCanvasOps::BLOCK_HEADER_HEIGHT)));
	EXPECT_EQ(harness.model.connects, 2u);
	harness.Press(Key::Escape);

	const u32 before = harness.model.searches;
	harness.Drag(FlowOnScreen(harness, a, GraphPortSide::FlowOutput, 0, 1), harness.GraphOnScreen(Vec2(200.0f, 400.0f)));
	EXPECT_EQ(harness.model.searches, before + 1u);
	ASSERT_TRUE(harness.model.searchedFromPort);
	EXPECT_EQ(harness.model.searchedFrom, (GraphPortRef{1, GraphPortSide::FlowOutput, 0}));
}

TEST(GraphCanvasTest, SpaceOverStackListsBlocks) {
	StackHarness harness;
	TwoStacks(harness.model);
	harness.model.blockLibrary.push_back(Named("Set Velocity", 7));
	harness.model.library.push_back(Named("Add", 3));
	harness.Build();
	const StackModel::Stack& a = harness.model.stacks[0];

	const UI::Rect below = BlockBoxOf(harness, a, 1);
	harness.Click(harness.GraphOnScreen(Vec2(300.0f, 400.0f)));
	harness.Inject(InputEventType::MouseMove, harness.ToScreen(Vec2(below.x + below.w * 0.5f, below.y + below.h * 0.8f)));
	harness.Press(Key::Space, KeyMod::None, U' ');
	EXPECT_EQ(harness.model.blockSearches, 1u);
	EXPECT_EQ(harness.model.searchedStack, 1u);
	EXPECT_EQ(harness.model.searches, 0u);
	harness.Type("vel");
	harness.Press(Key::Enter);
	ASSERT_EQ(harness.model.blockPicks, 1u);
	EXPECT_EQ(harness.model.pickedStack, 1u);
	EXPECT_EQ(harness.model.pickedGap, 2u);

	harness.model.blockLibrary.clear();
	harness.Click(harness.GraphOnScreen(Vec2(300.0f, 400.0f)));
	harness.Inject(InputEventType::MouseMove, harness.ToScreen(Centre(BlockBoxOf(harness, a, 0))));
	harness.Press(Key::Space, KeyMod::None, U' ');
	EXPECT_EQ(harness.model.searches, 1u);
	harness.Press(Key::Escape);

	harness.model.blockLibrary.push_back(Named("Set Size", 8));
	const UI::Rect top = BlockBoxOf(harness, a, 0);
	const Vec2I anchor = harness.ToScreen(Vec2(top.x + 4.0f, top.y + 1.0f));
	ASSERT_TRUE(harness.canvas.OpenBlockSearcherAt(harness.context, 1, Vec2(static_cast<f32>(anchor.x), static_cast<f32>(anchor.y))));
	harness.context.Update();
	harness.Type("size");
	harness.Press(Key::Enter);
	ASSERT_EQ(harness.model.blockPicks, 2u);
	EXPECT_EQ(harness.model.pickedGap, 0u);
	EXPECT_FALSE(harness.canvas.OpenBlockSearcherAt(harness.context, 11, Vec2(static_cast<f32>(anchor.x), static_cast<f32>(anchor.y))));
}

TEST(GraphCanvasTest, SettingsReachModel) {
	StackHarness harness;
	TwoStacks(harness.model);
	harness.model.stacks[0].settings = 2;
	harness.model.stacks[0].blocks[0].settings = 1;
	harness.Build();
	const StackModel::Stack& a = harness.model.stacks[0];
	const f32 zoom = harness.canvas.GetView().zoom;
	const UI::Rect box = StackBox(harness, a);
	auto settingRow = [&](u32 row) {
		return harness.ToScreen(Vec2(box.x + box.w * 0.75f,
									 box.y + (LayoutOf(a).settings + (static_cast<f32>(row) + 0.5f) * GraphCanvasOps::SETTING_ROW_HEIGHT) * zoom));
	};

	harness.Click(settingRow(0));
	ASSERT_EQ(harness.model.settingClicks, 1u);
	EXPECT_EQ(harness.model.settingOwner, 1u);
	EXPECT_EQ(harness.model.settingIndex, 0u);
	EXPECT_FALSE(harness.Selected(GraphItemKind::Node, 1));

	harness.Click(settingRow(1));
	EXPECT_EQ(harness.model.settingClicks, 1u);
	for (i32 erase = 0; erase < 5; ++erase) {
		harness.Press(Key::Backspace);
	}
	harness.Type("256");
	harness.Press(Key::Enter);
	ASSERT_EQ(harness.model.settingTexts, 1u);
	EXPECT_EQ(harness.model.settingIndex, 1u);
	EXPECT_EQ(harness.model.settingText, "256");

	const UI::Rect block = BlockBoxOf(harness, a, 0);
	harness.Click(harness.ToScreen(
		Vec2(block.x + block.w * 0.75f, block.y + (GraphCanvasOps::BLOCK_HEADER_HEIGHT + GraphCanvasOps::SETTING_ROW_HEIGHT * 0.5f) * zoom)));
	ASSERT_EQ(harness.model.settingClicks, 2u);
	EXPECT_EQ(harness.model.settingOwner, 11u);
	EXPECT_EQ(harness.model.settingIndex, 0u);
}

TEST(GraphCanvasTest, PushButtonSettingPresses) {
	StackHarness harness;
	TwoStacks(harness.model);
	harness.model.stacks[0].settings = 1;
	harness.model.stacks[0].action = "Send";
	harness.Build();
	const StackModel::Stack& a = harness.model.stacks[0];
	const f32 zoom = harness.canvas.GetView().zoom;
	const UI::Rect box = StackBox(harness, a);

	const UI::ElementHandle button = FindByClass(harness.context, harness.root, EditorTheme::Class::GraphSettingAction, 0);
	ASSERT_TRUE(harness.context.IsAlive(button));
	EXPECT_FALSE(harness.context.IsAlive(FindByClass(harness.context, harness.root, EditorTheme::Class::GraphSettingAction, 1)));
	const UI::Rect drawn = harness.context.GetLayout(button).borderBox;
	ASSERT_GT(drawn.w, 0.0f);
	harness.Click(Vec2I(static_cast<i32>(std::lround(drawn.x + drawn.w * 0.5f)), static_cast<i32>(std::lround(drawn.y + drawn.h * 0.5f))));
	EXPECT_EQ(harness.model.settingActions, 1u);
	EXPECT_EQ(harness.model.settingOwner, 1u);
	EXPECT_EQ(harness.model.settingIndex, 0u);
	EXPECT_EQ(harness.model.settingClicks, 0u);

	harness.Click(harness.ToScreen(Vec2(box.x + box.w * 0.5f, box.y + (LayoutOf(a).settings + 0.5f * GraphCanvasOps::SETTING_ROW_HEIGHT) * zoom)));
	EXPECT_EQ(harness.model.settingClicks, 1u);
	EXPECT_EQ(harness.model.settingActions, 1u);
}

TEST(GraphCanvasTest, BlocksCheckboxMenuAndFieldAreBlocks) {
	StackHarness harness;
	harness.desc.fieldsInside = true;
	TwoStacks(harness.model);
	harness.Build();
	const StackModel::Stack& a = harness.model.stacks[0];
	const f32 zoom = harness.canvas.GetView().zoom;
	const UI::Rect block = BlockBoxOf(harness, a, 1);

	harness.Click(harness.ToScreen(Vec2(block.x + (8.0f + 5.0f) * zoom, block.y + GraphCanvasOps::BLOCK_HEADER_HEIGHT * 0.5f * zoom)));
	ASSERT_EQ(harness.model.enables, 1u);
	EXPECT_EQ(harness.model.enabledBlock, 12u);
	EXPECT_FALSE(harness.model.enabledTo);

	harness.Inject(InputEventType::MouseMove, BlockTitleOnScreen(harness, a, 1));
	harness.Inject(InputEventType::MouseDown, BlockTitleOnScreen(harness, a, 1), MouseButton::Right);
	harness.Inject(InputEventType::MouseUp, BlockTitleOnScreen(harness, a, 1), MouseButton::Right);
	EXPECT_EQ(harness.model.blockMenus, 1u);
	EXPECT_EQ(harness.model.menuBlock, 12u);
	EXPECT_TRUE(harness.Selected(GraphItemKind::Block, 12));

	UI::ElementHandle field = FindByClass(harness.context, harness.root, EditorTheme::Class::GraphInlineField, 1);
	const UI::Rect drawn = harness.context.GetLayout(field).borderBox;
	const UI::Rect inside = OnScreen(harness, block);
	EXPECT_GE(drawn.x, inside.x);
	EXPECT_LE(drawn.x + drawn.w, inside.x + inside.w + 1.0f);
	harness.Click(Vec2I(static_cast<i32>(drawn.x + drawn.w * 0.5f), static_cast<i32>(drawn.y + drawn.h * 0.5f)));
	for (i32 erase = 0; erase < 4; ++erase) {
		harness.Press(Key::Backspace);
	}
	harness.Type("2");
	harness.Press(Key::Enter);
	ASSERT_EQ(harness.model.inlineSets, 1u);
	EXPECT_EQ(harness.model.inlinePort, (GraphPortRef{12, GraphPortSide::Input, 0}));
}

TEST(GraphCanvasTest, SpaceButtonReachesModel) {
	StackHarness harness;
	TwoStacks(harness.model);
	harness.model.stacks[0].blocks[1].space = "W";
	harness.Build();
	Vector<UI::ElementHandle> buttons{GetMemoryResource(MemTag::Editor)};
	for (u32 index = 0; index < 4; ++index) {
		const UI::ElementHandle found = FindByClass(harness.context, harness.root, EditorTheme::Class::GraphSpaceButton, index);
		if (harness.context.IsAlive(found)) {
			buttons.push_back(found);
		}
	}
	ASSERT_EQ(buttons.size(), 1u);
	const UI::Rect drawn = harness.context.GetLayout(buttons[0]).borderBox;
	EXPECT_GT(drawn.w, 0.0f);
	const UI::Rect block = OnScreen(harness, BlockBoxOf(harness, harness.model.stacks[0], 1));
	EXPECT_TRUE(drawn.x >= block.x && drawn.y >= block.y && drawn.x + drawn.w <= block.x + block.w && drawn.y + drawn.h <= block.y + block.h);

	harness.Click(Vec2I(static_cast<i32>(drawn.x + drawn.w * 0.5f), static_cast<i32>(drawn.y + drawn.h * 0.5f)));
	ASSERT_EQ(harness.model.spaceClicks, 1u);
	EXPECT_EQ(harness.model.spacePort, (GraphPortRef{12, GraphPortSide::Input, 0}));
	EXPECT_FALSE(harness.Selected(GraphItemKind::Block, 12));
	EXPECT_EQ(harness.model.connects, 0u);
}

TEST(GraphCanvasTest, GradientFieldPressSelectsBlock) {
	StackHarness harness;
	TwoStacks(harness.model);
	harness.model.stacks[0].blocks[1].gradient = true;
	harness.desc.fieldsInside = true;
	harness.Build();

	UI::ElementHandle field = FindByClass(harness.context, harness.root, EditorTheme::Class::GraphInlineField, 1);
	ASSERT_TRUE(harness.context.IsAlive(field));
	const UI::Rect box = harness.context.GetLayout(field).borderBox;
	EXPECT_GE(DrawnInside(harness.context, box).colors, 3u);

	harness.Click(Vec2I(static_cast<i32>(box.x + box.w * 0.5f), static_cast<i32>(box.y + box.h * 0.5f)));
	EXPECT_TRUE(harness.Selected(GraphItemKind::Block, 12));
	EXPECT_EQ(harness.model.inlineSets, 0u);
}

namespace {

	bool SameColor(const Color& a, const Color& b) {
		return std::abs(a.r - b.r) < 0.01f && std::abs(a.g - b.g) < 0.01f && std::abs(a.b - b.b) < 0.01f && std::abs(a.a - b.a) < 0.01f;
	}

	struct Stroke {
		Vec2 a{};
		Vec2 b{};
		f32 width = 0.0f;
	};

	std::vector<Stroke> StrokesIn(UI::Context& context, const Color& color) {
		UI::RenderList list;
		context.BuildRenderList(list);
		std::vector<Stroke> found;
		for (const UI::UIQuad& quad : list.quads) {
			const UI::Rect box = (quad.flags & UI::QUAD_FLAG_ROTATED) != 0 ? quad.uv : quad.rect;
			if (!SameColor(quad.color0, color) || !(box.w > box.h + 0.5f)) {
				continue;
			}
			const bool round = quad.radii.topLeft > 0.0f;
			const f32 half = (round ? box.w - box.h : box.w) * 0.5f;
			const Vec2 centre(box.x + box.w * 0.5f, box.y + box.h * 0.5f);
			const Vec2 along(std::cos(quad.rotation), std::sin(quad.rotation));
			found.push_back(Stroke{centre - along * half, centre + along * half, box.h});
		}
		return found;
	}

	std::vector<Vec2> DiscsIn(UI::Context& context, const Color& color) {
		UI::RenderList list;
		context.BuildRenderList(list);
		std::vector<Vec2> found;
		for (const UI::UIQuad& quad : list.quads) {
			const f32 side = Math::Min(quad.rect.w, quad.rect.h);
			const bool disc = side > 0.0f && std::abs(quad.rect.w - quad.rect.h) < 1.0f && quad.radii.topLeft >= side * 0.5f - 1.0f;
			if (disc && SameColor(quad.color0, color)) {
				found.push_back(Vec2(quad.rect.x + quad.rect.w * 0.5f, quad.rect.y + quad.rect.h * 0.5f));
			}
		}
		return found;
	}

	f32 Distance(Vec2 a, Vec2I b) {
		return (a - Vec2(static_cast<f32>(b.x), static_cast<f32>(b.y))).Length();
	}

	f32 NearestEnd(const std::vector<Stroke>& strokes, Vec2I point) {
		f32 best = 1e9f;
		for (const Stroke& stroke : strokes) {
			best = Math::Min(best, Math::Min(Distance(stroke.a, point), Distance(stroke.b, point)));
		}
		return best;
	}

	GraphWireDesc FloatWire(GraphWireId id) {
		GraphWireDesc wire;
		wire.id = id;
		wire.style = GraphWireStyle::Curve;
		wire.from = GraphPortRef{1, GraphPortSide::Output, 0};
		wire.to = GraphPortRef{2, GraphPortSide::Input, 0};
		wire.color = EditorTheme::ColorToken::GraphPortFloat;
		return wire;
	}

} // anonymous namespace

TEST(GraphCanvasTest, HoveredWireLitWhereTakeable) {
	CanvasHarness harness;
	TwoPortedNodes(harness.model);
	harness.model.wires.push_back(FloatWire(7));

	const GraphView view = harness.canvas.GetView();
	const Vec2 size(160.0f, GraphCanvasOps::PortNodeHeight(1));
	const Vec2 from = GraphCanvasOps::PortAnchor(GraphCanvasOps::NodeRect(view, Vec2(40.0f, 60.0f), size), GraphPortSide::Output, 0, view.zoom);
	const Vec2 to = GraphCanvasOps::PortAnchor(GraphCanvasOps::NodeRect(view, Vec2(380.0f, 200.0f), size), GraphPortSide::Input, 0, view.zoom);
	const GraphCubic curve = GraphCanvasOps::PortWire(from, to, view.zoom);
	const Vec2 covered = GraphCanvasOps::Evaluate(curve, 0.5f);
	const Vec2 cardAt = GraphCanvasOps::LocalToGraph(view, covered) - Vec2(80.0f, 20.0f);
	harness.model.nodes.push_back({3, cardAt, 0, 0, str("Cover")});
	harness.Build();

	const Vec2 open = GraphCanvasOps::Evaluate(curve, 0.15f);
	const UI::Rect card = GraphCanvasOps::NodeRect(view, cardAt, Vec2(160.0f, 40.0f));
	const auto inside = [](const UI::Rect& box, Vec2 p) { return p.x >= box.x && p.x <= box.x + box.w && p.y >= box.y && p.y <= box.y + box.h; };
	ASSERT_FALSE(inside(card, open));
	ASSERT_FALSE(inside(GraphCanvasOps::NodeRect(view, Vec2(40.0f, 60.0f), size), open));
	ASSERT_TRUE(inside(card, covered));

	const Color lit = EditorTheme::GetColor(EditorTheme::ColorToken::SelectionWire);
	const Color unlit = EditorTheme::GetColor(EditorTheme::ColorToken::GraphPortFloat);
	const f32 base = 2.0f * view.zoom * SCALE;
	const auto wide = [&](const std::vector<Stroke>& strokes) {
		return std::count_if(strokes.begin(), strokes.end(), [&](const Stroke& stroke) { return stroke.width > base * 1.25f; });
	};

	harness.Inject(InputEventType::MouseMove, harness.ToScreen(open));
	const std::vector<Stroke> hovered = StrokesIn(harness.context, lit);
	ASSERT_GT(hovered.size(), 2u);
	EXPECT_EQ(wide(hovered), static_cast<i64>(hovered.size()));
	EXPECT_TRUE(StrokesIn(harness.context, unlit).empty());
	harness.Click(harness.ToScreen(open));
	EXPECT_TRUE(harness.Selected(GraphItemKind::Wire, 7));

	harness.Inject(InputEventType::MouseMove, harness.ToScreen(covered));
	EXPECT_EQ(wide(StrokesIn(harness.context, lit)), 0);

	const Vec2 empty(620.0f, 520.0f);
	harness.Inject(InputEventType::MouseMove, harness.ToScreen(empty));
	const std::vector<Stroke> rest = StrokesIn(harness.context, lit);
	ASSERT_GT(rest.size(), 2u);
	EXPECT_EQ(wide(rest), 0);
	EXPECT_NEAR(rest.front().width, base, 0.05f);
	EXPECT_NEAR(hovered.front().width, base * 1.5f, 0.05f);

	harness.Inject(InputEventType::MouseDown, harness.ToScreen(empty));
	harness.Inject(InputEventType::MouseMove, harness.ToScreen((empty + open) * 0.5f));
	harness.Inject(InputEventType::MouseMove, harness.ToScreen(open));
	EXPECT_EQ(wide(StrokesIn(harness.context, lit)), 0);
	harness.Inject(InputEventType::MouseUp, harness.ToScreen(open));
}

TEST(GraphCanvasTest, DraggedWireLightsTargets) {
	CanvasHarness harness;
	TwoPortedNodes(harness.model);
	harness.Build();
	const Vec2I output = harness.PortOnScreen(1, GraphPortSide::Output, 0);
	const Vec2I input = harness.PortOnScreen(2, GraphPortSide::Input, 0);
	const Color lit = EditorTheme::GetColor(EditorTheme::ColorToken::SelectionWire);

	harness.Inject(InputEventType::MouseMove, output);
	std::vector<Vec2> discs = DiscsIn(harness.context, lit);
	ASSERT_EQ(discs.size(), 1u);
	EXPECT_LT(Distance(discs[0], output), 1.5f);
	harness.Inject(InputEventType::MouseMove, Vec2I(output.x - 16, output.y));
	EXPECT_TRUE(DiscsIn(harness.context, lit).empty());

	const Vec2I aim(input.x + 3, input.y + 2);
	harness.Inject(InputEventType::MouseMove, output);
	harness.Inject(InputEventType::MouseDown, output);
	harness.Inject(InputEventType::MouseMove, Vec2I((output.x + aim.x) / 2, (output.y + aim.y) / 2));
	harness.Inject(InputEventType::MouseMove, aim);
	discs = DiscsIn(harness.context, lit);
	ASSERT_EQ(discs.size(), 2u);
	const auto at = [&](Vec2I where) {
		return std::any_of(discs.begin(), discs.end(), [&](Vec2 disc) { return Distance(disc, where) < 1.5f; });
	};
	EXPECT_TRUE(at(output));
	EXPECT_TRUE(at(input));
	std::vector<Stroke> wire = StrokesIn(harness.context, lit);
	ASSERT_FALSE(wire.empty());
	EXPECT_LT(NearestEnd(wire, input), 1.0f);
	EXPECT_GT(NearestEnd(wire, aim), 2.0f);

	harness.model.refuseConnections = true;
	discs = DiscsIn(harness.context, lit);
	ASSERT_EQ(discs.size(), 1u);
	EXPECT_TRUE(at(output));
	wire = StrokesIn(harness.context, lit);
	EXPECT_LT(NearestEnd(wire, aim), 0.75f);
	harness.Inject(InputEventType::MouseUp, aim);
	EXPECT_EQ(harness.model.connects, 0u);
}

TEST(GraphCanvasTest, CtrlDragSnapsGrabbedNode) {
	const std::filesystem::path root =
		std::filesystem::temp_directory_path() /
		("shalltear_graph_snap_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(root);
	SettingsPaths paths;
	paths.project = Path((root / "ProjectSettings.ini").generic_string());
	paths.user = Path((root / "UserSettings.ini").generic_string());
	paths.userProject = Path((root / "UserProjectSettings.ini").generic_string());
	Settings::Initialize(paths);
	auto* graph = Settings::DeclareCategory<EditorConfig::GraphPreferences>("Editor/Graph", SettingScope::User);
	ASSERT_NE(graph, nullptr);
	constexpr f32 GRID = 25.0f;
	graph->gridSize = static_cast<i32>(GRID);
	{
		CanvasHarness harness;
		harness.model.nodes.push_back({1, Vec2(43.0f, 61.0f), 0, 0, str("One")});
		harness.model.nodes.push_back({2, Vec2(301.0f, 77.0f), 0, 0, str("Two")});
		harness.Build();
		harness.Click(harness.CardOnScreen(1));
		harness.Click(harness.CardOnScreen(2), KeyMod::Ctrl);
		ASSERT_EQ(harness.canvas.GetSelection().size(), 2u);

		const Vec2I grab = harness.CardOnScreen(1);
		const Vec2I to(grab.x + 29, grab.y + 33);
		harness.Inject(InputEventType::MouseMove, grab);
		harness.Inject(InputEventType::MouseDown, grab);
		harness.Inject(InputEventType::MouseMove, Vec2I(grab.x + 14, grab.y + 16), MouseButton::Left, KeyMod::Ctrl);
		harness.Inject(InputEventType::MouseMove, to, MouseButton::Left, KeyMod::Ctrl);

		const f32 zoom = harness.canvas.GetView().zoom;
		const Vec2 unsnapped = Vec2(43.0f, 61.0f) + Vec2(29.0f, 33.0f) / (SCALE * zoom);
		const Vec2 snapped(std::round(unsnapped.x / GRID) * GRID, std::round(unsnapped.y / GRID) * GRID);
		ASSERT_NE(snapped.x, std::round(unsnapped.x / 20.0f) * 20.0f);
		EXPECT_EQ(PositionOf(harness.model, 1), snapped);
		EXPECT_EQ(PositionOf(harness.model, 2), Vec2(301.0f, 77.0f) + (snapped - Vec2(43.0f, 61.0f)));

		harness.Inject(InputEventType::MouseMove, Vec2I(to.x + 1, to.y), MouseButton::Left);
		const Vec2 loose = Vec2(43.0f, 61.0f) + Vec2(30.0f, 33.0f) / (SCALE * zoom);
		EXPECT_NEAR(PositionOf(harness.model, 1).x, loose.x, 0.01f);
		EXPECT_NEAR(PositionOf(harness.model, 1).y, loose.y, 0.01f);

		harness.Inject(InputEventType::MouseMove, to, MouseButton::Left, KeyMod::Ctrl);
		harness.Inject(InputEventType::MouseUp, to, MouseButton::Left, KeyMod::Ctrl);
		EXPECT_EQ(PositionOf(harness.model, 1), snapped);
		EXPECT_EQ(harness.model.moveBegins, 1u);
		EXPECT_EQ(harness.model.moveEnds, 1u);

		UI::RenderList list;
		harness.context.BuildRenderList(list);
		const Color minor = EditorTheme::GetColor(EditorTheme::ColorToken::GraphGrid);
		const Color major = EditorTheme::GetColor(EditorTheme::ColorToken::GraphGridMajor);
		const UI::Rect content = harness.context.GetLayout(harness.canvas.GetElement()).content;
		const f32 left = content.x + GraphCanvasOps::GraphToLocal(harness.canvas.GetView(), snapped).x * SCALE;
		const auto lineAt = [&](f32 x) {
			return std::any_of(list.quads.begin(), list.quads.end(), [&](const UI::UIQuad& quad) {
				const bool vertical = quad.rect.w <= 1.5f && quad.rect.h > content.h * 0.5f;
				return vertical && (SameColor(quad.color0, minor) || SameColor(quad.color0, major)) && std::abs(quad.rect.x - x) <= 1.0f;
			});
		};
		EXPECT_TRUE(lineAt(left));
		EXPECT_TRUE(lineAt(left + GRID * zoom * SCALE));
		EXPECT_FALSE(lineAt(left + 20.0f * zoom * SCALE));
	}
	Settings::Terminate();
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
}
