#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/UI/VectorAsset.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	UI::ContextDesc Desc() {
		UI::ContextDesc desc;
		desc.sizePx = {800, 600};
		desc.contentScale = 1.0f;
		return desc;
	}

	struct DockRig {
		UI::Context ctx{Desc(), nullptr};
		UI::DockManager dock{ctx, Handle<FontAsset>{}};
		int builds = 0;
		UI::ElementHandle lastContentA;
		int buildsD = 0;
		UI::ElementHandle contentD;
		UI::ElementHandle bodyD;

		DockRig() {
			dock.RegisterPanel("panel.a"_hash, "A", [this](UI::Context& c, UI::ElementHandle parent) {
				UI::ElementHandle content = c.CreateElement();
				c.AppendChild(parent, content);
				lastContentA = content;
				++builds;
			});
			dock.RegisterPanel("panel.d"_hash, "D", [this](UI::Context& c, UI::ElementHandle parent) {
				UI::ElementHandle content = c.CreateElement();
				c.AppendChild(parent, content);
				UI::ElementHandle body = c.CreateElement();
				c.AppendChild(content, body);
				contentD = content;
				bodyD = body;
				++buildsD;
			});
			dock.RegisterPanel("panel.b"_hash, "B", [](UI::Context&, UI::ElementHandle) {});
			dock.RegisterPanel("panel.c"_hash, "C", [](UI::Context&, UI::ElementHandle) {});
		}

		void BuildFrame() {
			dock.Build(ctx.GetRoot());
			ctx.Update();
		}

		u32 DockedCount() {
			Vector<StringHash> panels{GetMemoryResource(MemTag::UI)};
			dock.GetDockedPanels(panels);
			return static_cast<u32>(panels.size());
		}
	};

} // anonymous namespace

TEST(UIDockingTest, CenterDockJoinsTheRootLeaf) {
	DockRig rig;
	rig.dock.DockPanel("panel.a"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.dock.DockPanel("panel.b"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);

	EXPECT_TRUE(rig.dock.IsPanelDocked("panel.a"_hash));
	EXPECT_TRUE(rig.dock.IsPanelDocked("panel.b"_hash));
	EXPECT_EQ(rig.dock.FindNodeOfPanel("panel.a"_hash), rig.dock.FindNodeOfPanel("panel.b"_hash));
	EXPECT_EQ(rig.DockedCount(), 2u);
}

TEST(UIDockingTest, EdgeZonesSplitTheTarget) {
	DockRig rig;
	rig.dock.DockPanel("panel.a"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.dock.DockPanel("panel.b"_hash, rig.dock.GetRootNode(), UI::DockZone::Right);

	u32 nodeA = rig.dock.FindNodeOfPanel("panel.a"_hash);
	u32 nodeB = rig.dock.FindNodeOfPanel("panel.b"_hash);
	EXPECT_NE(nodeA, UI::INVALID_DOCK_NODE);
	EXPECT_NE(nodeB, UI::INVALID_DOCK_NODE);
	EXPECT_NE(nodeA, nodeB);

	rig.BuildFrame();
	rig.BuildFrame();
	EXPECT_EQ(rig.DockedCount(), 2u);
}

TEST(UIDockingTest, AllZonesProduceDistinctLeaves) {
	DockRig rig;
	rig.dock.DockPanel("panel.a"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.dock.DockPanel("panel.b"_hash, rig.dock.GetRootNode(), UI::DockZone::Bottom);
	rig.dock.DockPanel("panel.c"_hash, rig.dock.FindNodeOfPanel("panel.a"_hash), UI::DockZone::Left);

	u32 nodeA = rig.dock.FindNodeOfPanel("panel.a"_hash);
	u32 nodeB = rig.dock.FindNodeOfPanel("panel.b"_hash);
	u32 nodeC = rig.dock.FindNodeOfPanel("panel.c"_hash);
	EXPECT_NE(nodeA, nodeB);
	EXPECT_NE(nodeA, nodeC);
	EXPECT_NE(nodeB, nodeC);
	EXPECT_EQ(rig.DockedCount(), 3u);
}

TEST(UIDockingTest, UndockRemovesThePanel) {
	DockRig rig;
	rig.dock.DockPanel("panel.a"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.dock.DockPanel("panel.b"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.dock.UndockPanel("panel.a"_hash);

	EXPECT_FALSE(rig.dock.IsPanelDocked("panel.a"_hash));
	EXPECT_TRUE(rig.dock.IsPanelDocked("panel.b"_hash));
	EXPECT_EQ(rig.dock.FindNodeOfPanel("panel.a"_hash), UI::INVALID_DOCK_NODE);
}

TEST(UIDockingTest, MovingAPanelBetweenNodes) {
	DockRig rig;
	rig.dock.DockPanel("panel.a"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.dock.DockPanel("panel.b"_hash, rig.dock.GetRootNode(), UI::DockZone::Right);
	u32 nodeB = rig.dock.FindNodeOfPanel("panel.b"_hash);

	rig.dock.DockPanel("panel.a"_hash, nodeB, UI::DockZone::Center);
	EXPECT_EQ(rig.dock.FindNodeOfPanel("panel.a"_hash), rig.dock.FindNodeOfPanel("panel.b"_hash));
	EXPECT_EQ(rig.DockedCount(), 2u);
}

namespace {

	bool NoEmptyLeafBelowTheRoot(const UI::DockManager& dock) {
		Vector<UI::DockNodeDesc> layout{GetMemoryResource(MemTag::Debug)};
		dock.SaveLayout(layout);
		for (size_t i = 1; i < layout.size(); ++i) {
			if (layout[i].split == UI::DockSplit::None && layout[i].tabs.empty()) {
				return false;
			}
		}
		return true;
	}

	size_t NodeCount(const UI::DockManager& dock) {
		Vector<UI::DockNodeDesc> layout{GetMemoryResource(MemTag::Debug)};
		dock.SaveLayout(layout);
		return layout.size();
	}

} // namespace

TEST(UIDockingTest, MovingLeafsOnlyPanelAwayCollapsesLeaf) {
	DockRig rig;
	rig.dock.DockPanel("panel.a"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.dock.DockPanel("panel.b"_hash, rig.dock.GetRootNode(), UI::DockZone::Right);
	ASSERT_EQ(NodeCount(rig.dock), 3u);

	rig.dock.DockPanel("panel.b"_hash, rig.dock.FindNodeOfPanel("panel.a"_hash), UI::DockZone::Center);
	EXPECT_EQ(NodeCount(rig.dock), 1u);
	EXPECT_EQ(rig.dock.FindNodeOfPanel("panel.a"_hash), rig.dock.GetRootNode());
	EXPECT_EQ(rig.dock.FindNodeOfPanel("panel.b"_hash), rig.dock.GetRootNode());
}

TEST(UIDockingTest, UndockingLeafsOnlyPanelCollapsesLeaf) {
	DockRig rig;
	rig.dock.DockPanel("panel.a"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.dock.DockPanel("panel.b"_hash, rig.dock.GetRootNode(), UI::DockZone::Bottom);
	rig.dock.DockPanel("panel.c"_hash, rig.dock.FindNodeOfPanel("panel.b"_hash), UI::DockZone::Right);
	ASSERT_EQ(NodeCount(rig.dock), 5u);

	rig.dock.UndockPanel("panel.c"_hash);
	EXPECT_TRUE(NoEmptyLeafBelowTheRoot(rig.dock));
	EXPECT_EQ(NodeCount(rig.dock), 3u);
	rig.dock.UndockPanel("panel.a"_hash);
	EXPECT_TRUE(NoEmptyLeafBelowTheRoot(rig.dock));
	EXPECT_EQ(rig.dock.FindNodeOfPanel("panel.b"_hash), rig.dock.GetRootNode());

	rig.dock.UndockPanel("panel.b"_hash);
	EXPECT_EQ(NodeCount(rig.dock), 1u);
	EXPECT_EQ(rig.DockedCount(), 0u);
}

TEST(UIDockingTest, LonePanelEdgeDropNoOp) {
	DockRig rig;
	rig.dock.DockPanel("panel.a"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.dock.DockPanel("panel.b"_hash, rig.dock.GetRootNode(), UI::DockZone::Right);
	rig.dock.DockPanel("panel.c"_hash, rig.dock.FindNodeOfPanel("panel.b"_hash), UI::DockZone::Bottom);
	Vector<UI::DockNodeDesc> before{GetMemoryResource(MemTag::Debug)};
	rig.dock.SaveLayout(before);

	rig.dock.DockPanel("panel.c"_hash, rig.dock.FindNodeOfPanel("panel.c"_hash), UI::DockZone::Left);
	Vector<UI::DockNodeDesc> after{GetMemoryResource(MemTag::Debug)};
	rig.dock.SaveLayout(after);
	ASSERT_EQ(after.size(), before.size());
	for (size_t i = 0; i < after.size(); ++i) {
		EXPECT_EQ(after[i].split, before[i].split) << "node " << i;
		EXPECT_EQ(after[i].tabs, before[i].tabs) << "node " << i;
	}
}

TEST(UIDockingTest, EdgeOfEmptyRootDocksIntoRoot) {
	DockRig rig;
	rig.dock.DockPanel("panel.a"_hash, rig.dock.GetRootNode(), UI::DockZone::Left);
	EXPECT_EQ(NodeCount(rig.dock), 1u);
	EXPECT_EQ(rig.dock.FindNodeOfPanel("panel.a"_hash), rig.dock.GetRootNode());
}

TEST(UIDockingTest, BuildCreatesElementsAndRunsPanelBuilders) {
	DockRig rig;
	rig.dock.DockPanel("panel.a"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	EXPECT_EQ(rig.builds, 0);
	u32 before = rig.ctx.GetLiveCount();
	rig.BuildFrame();
	EXPECT_GT(rig.ctx.GetLiveCount(), before);
	EXPECT_EQ(rig.builds, 1);

	rig.BuildFrame();
	EXPECT_EQ(rig.builds, 1);
	rig.dock.DockPanel("panel.b"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.BuildFrame();
	EXPECT_EQ(rig.builds, 1);
}

TEST(UIDockingTest, SplitElsewhereLeavesUntouchedPanelsContentStanding) {
	DockRig rig;
	rig.dock.DockPanel("panel.a"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.BuildFrame();
	ASSERT_EQ(rig.builds, 1);
	const UI::ElementHandle contentBefore = rig.lastContentA;

	rig.dock.DockPanel("panel.b"_hash, rig.dock.GetRootNode(), UI::DockZone::Right);
	rig.BuildFrame();

	EXPECT_EQ(rig.builds, 1);
	EXPECT_EQ(rig.lastContentA, contentBefore);
	EXPECT_TRUE(rig.ctx.IsAlive(contentBefore));
	const UI::Element* pod = rig.ctx.GetElement(contentBefore);
	ASSERT_NE(pod, nullptr);
	EXPECT_NE(pod->parent, rig.ctx.GetRoot().index);
}

TEST(UIDockingTest, DeactivatedPanelsContentIsDestroyedNotParked) {
	DockRig rig;
	rig.dock.DockPanel("panel.a"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.BuildFrame();
	ASSERT_EQ(rig.builds, 1);
	const UI::ElementHandle contentA = rig.lastContentA;
	ASSERT_TRUE(rig.ctx.IsAlive(contentA));

	rig.dock.DockPanel("panel.b"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.BuildFrame();
	EXPECT_FALSE(rig.ctx.IsAlive(contentA));

	rig.dock.ActivatePanel("panel.a"_hash);
	rig.BuildFrame();
	EXPECT_EQ(rig.builds, 2);
}

TEST(UIDockingTest, DroppedPanelKeepsContent) {
	for (UI::DockZone seed : {UI::DockZone::Right, UI::DockZone::Left}) {
		DockRig rig;
		rig.dock.DockPanel("panel.d"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
		rig.dock.DockPanel("panel.b"_hash, rig.dock.GetRootNode(), seed);
		rig.BuildFrame();
		ASSERT_EQ(rig.buildsD, 1);
		const UI::ElementHandle content = rig.contentD;
		const UI::ElementHandle body = rig.bodyD;
		ASSERT_TRUE(rig.ctx.IsAlive(body));

		rig.dock.DockPanel("panel.d"_hash, rig.dock.FindNodeOfPanel("panel.b"_hash), UI::DockZone::Bottom);
		rig.BuildFrame();

		const int order = seed == UI::DockZone::Right ? 0 : 1;
		EXPECT_TRUE(rig.ctx.IsAlive(content)) << "order " << order;
		EXPECT_TRUE(rig.ctx.IsAlive(body));
		ASSERT_TRUE(rig.ctx.IsAlive(content));
		const UI::Element* pod = rig.ctx.GetElement(content);
		ASSERT_NE(pod, nullptr);
		EXPECT_NE(pod->parent, rig.ctx.GetRoot().index) << "order " << order;
		EXPECT_EQ(rig.buildsD, 1);
	}
}

TEST(UIDockingTest, UnregisteringPanelTakesContentWithIt) {
	DockRig rig;
	rig.dock.DockPanel("panel.d"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.BuildFrame();
	ASSERT_EQ(rig.buildsD, 1);
	const UI::ElementHandle content = rig.contentD;
	ASSERT_TRUE(rig.ctx.IsAlive(content));

	rig.dock.UnregisterPanel("panel.d"_hash);
	rig.BuildFrame();

	EXPECT_FALSE(rig.dock.HasPanel("panel.d"_hash));
	EXPECT_FALSE(rig.ctx.IsAlive(content));
}

TEST(UIDockingTest, MovedPanelCarriesContent) {
	DockRig rig;
	rig.dock.DockPanel("panel.a"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.dock.DockPanel("panel.b"_hash, rig.dock.GetRootNode(), UI::DockZone::Right);
	rig.BuildFrame();
	ASSERT_EQ(rig.builds, 1);
	const UI::ElementHandle contentA = rig.lastContentA;
	const u32 nodeBefore = rig.dock.FindNodeOfPanel("panel.a"_hash);

	rig.dock.DockPanel("panel.a"_hash, rig.dock.FindNodeOfPanel("panel.b"_hash), UI::DockZone::Center);
	rig.BuildFrame();

	EXPECT_NE(rig.dock.FindNodeOfPanel("panel.a"_hash), nodeBefore);
	EXPECT_EQ(rig.builds, 1);
	EXPECT_TRUE(rig.ctx.IsAlive(contentA));
}

TEST(UIDockingTest, ResetRestoresASingleEmptyLeaf) {
	DockRig rig;
	rig.dock.DockPanel("panel.a"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.dock.DockPanel("panel.b"_hash, rig.dock.GetRootNode(), UI::DockZone::Left);
	rig.dock.Reset();
	EXPECT_EQ(rig.DockedCount(), 0u);
	EXPECT_FALSE(rig.dock.IsPanelDocked("panel.a"_hash));
}

TEST(UIDockingTest, UnknownPanelIsRejected) {
	DockRig rig;
	rig.dock.DockPanel("panel.unknown"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	EXPECT_EQ(rig.DockedCount(), 0u);
}

TEST(UIDockingTest, ZoneFromMousePicksTriggers) {
	DockRig rig;
	rig.BuildFrame();
	EXPECT_EQ(rig.dock.GetDropTargetAt({400.0f, 300.0f}).zone, UI::DockZone::Center);
	EXPECT_EQ(rig.dock.GetDropTargetAt({30.0f, 300.0f}).zone, UI::DockZone::Left);
	EXPECT_EQ(rig.dock.GetDropTargetAt({770.0f, 300.0f}).zone, UI::DockZone::Right);
	EXPECT_EQ(rig.dock.GetDropTargetAt({400.0f, 30.0f}).zone, UI::DockZone::Top);
	EXPECT_EQ(rig.dock.GetDropTargetAt({400.0f, 570.0f}).zone, UI::DockZone::Bottom);
	EXPECT_EQ(rig.dock.GetDropTargetAt({100.0f, 100.0f}).zone, UI::DockZone::None);
}

TEST(UIDockingTest, DropTargetsLeafThenEdges) {
	DockRig rig;
	rig.dock.DockPanel("panel.a"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.dock.DockPanel("panel.b"_hash, rig.dock.GetRootNode(), UI::DockZone::Right);
	rig.dock.DockPanel("panel.c"_hash, rig.dock.FindNodeOfPanel("panel.b"_hash), UI::DockZone::Bottom);
	rig.BuildFrame();
	const u32 a = rig.dock.FindNodeOfPanel("panel.a"_hash);
	const u32 b = rig.dock.FindNodeOfPanel("panel.b"_hash);
	const u32 c = rig.dock.FindNodeOfPanel("panel.c"_hash);

	auto boxOf = [&](u32 node) {
		for (u32 k = 0;; ++k) {
			const UI::ElementHandle leaf = FindByClass(rig.ctx, rig.ctx.GetRoot(), StringHash("dock-leaf"), k);
			if (!rig.ctx.IsAlive(leaf)) {
				return UI::Rect{};
			}
			const UI::Rect box = rig.ctx.GetLayout(leaf).content;
			if (rig.dock.GetDropTargetAt({box.x + box.w * 0.5f, box.y + box.h * 0.5f}).node == node) {
				return box;
			}
		}
	};
	const UI::Rect boxA = boxOf(a);
	const UI::Rect boxB = boxOf(b);
	const UI::Rect boxC = boxOf(c);
	ASSERT_GT(boxA.w, 0.0f);
	ASSERT_GT(boxB.w, 0.0f);
	ASSERT_GT(boxC.w, 0.0f);

	UI::DockDropTarget target = rig.dock.GetDropTargetAt({boxA.x + boxA.w * 0.5f, boxA.y + boxA.h * 0.5f});
	EXPECT_EQ(target.node, a);
	EXPECT_EQ(target.zone, UI::DockZone::Center);
	target = rig.dock.GetDropTargetAt({boxB.x + 30.0f, boxB.y + boxB.h * 0.5f});
	EXPECT_EQ(target.node, b);
	EXPECT_EQ(target.zone, UI::DockZone::Left);
	target = rig.dock.GetDropTargetAt({boxC.x + boxC.w * 0.5f, boxC.y + 30.0f});
	EXPECT_EQ(target.node, c);
	EXPECT_EQ(target.zone, UI::DockZone::Top);

	target = rig.dock.GetDropTargetAt({400.0f, 594.0f});
	EXPECT_EQ(target.node, rig.dock.GetRootNode());
	EXPECT_EQ(target.zone, UI::DockZone::Bottom);
	target = rig.dock.GetDropTargetAt({4.0f, 300.0f});
	EXPECT_EQ(target.node, rig.dock.GetRootNode());
	EXPECT_EQ(target.zone, UI::DockZone::Left);
	rig.dock.ShowExternalOverlay({200.0f, 300.0f});
	rig.BuildFrame();
	u32 arrows = 0;
	std::function<void(UI::ElementHandle)> walk = [&](UI::ElementHandle handle) {
		const str text = rig.ctx.GetTextString(handle);
		arrows += text == "\xE2\x86\x90" || text == "\xE2\x86\x92" || text == "\xE2\x86\x91" || text == "\xE2\x86\x93";
		const UI::Element* element = rig.ctx.GetElement(handle);
		for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle next = rig.ctx.MakeHandle(child);
			walk(next);
			child = rig.ctx.GetElement(next)->nextSibling;
		}
	};
	walk(rig.ctx.GetRoot());
	EXPECT_EQ(arrows, 4u);
}

TEST(UIDockingTest, ZoneTriggersScaleWithTheContentScale) {
	for (f32 scale : {1.0f, 2.0f}) {
		UI::ContextDesc desc;
		desc.sizePx = {800, 600};
		desc.contentScale = scale;
		UI::Context ctx{desc, nullptr};
		UI::DockManager dock{ctx, Handle<FontAsset>{}};
		dock.RegisterPanel("panel.a"_hash, "A", [](UI::Context&, UI::ElementHandle) {});
		dock.DockPanel("panel.a"_hash, dock.GetRootNode(), UI::DockZone::Center);
		dock.Build(ctx.GetRoot());
		ctx.Update();
		EXPECT_EQ(dock.GetDropTargetAt({90.0f, 300.0f}).zone, scale > 1.0f ? UI::DockZone::Left : UI::DockZone::None) << "at " << scale << "x";
		EXPECT_EQ(dock.GetDropTargetAt({30.0f, 300.0f}).zone, UI::DockZone::Left) << "at " << scale << "x";
	}
}

TEST(UIDockingTest, ZoneMarkAtCenter) {
	for (f32 scale : {1.0f, 1.25f, 2.0f}) {
		MockFontSystem font;
		UI::ContextDesc desc;
		desc.sizePx = {800, 600};
		desc.contentScale = scale;
		UI::Context ctx{desc, &font};
		UI::DockManager dock{ctx, Handle<FontAsset>{}};
		dock.RegisterPanel("panel.a"_hash, "A", [](UI::Context&, UI::ElementHandle) {});
		dock.DockPanel("panel.a"_hash, dock.GetRootNode(), UI::DockZone::Center);
		dock.Build(ctx.GetRoot());
		ctx.Update();
		dock.ShowExternalOverlay({100.0f, 100.0f});
		dock.Build(ctx.GetRoot());
		ctx.Update();
		UI::RenderList list;
		ctx.BuildRenderList(list);

		Vector<UI::ElementHandle> marks{GetMemoryResource(MemTag::Debug)};
		std::function<void(UI::ElementHandle)> walk = [&](UI::ElementHandle handle) {
			const str text = ctx.GetTextString(handle);
			if (text == "+" || text == "\xE2\x86\x90" || text == "\xE2\x86\x92" || text == "\xE2\x86\x91" || text == "\xE2\x86\x93") {
				marks.push_back(handle);
			}
			const UI::Element* element = ctx.GetElement(handle);
			for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle next = ctx.MakeHandle(child);
				walk(next);
				child = ctx.GetElement(next)->nextSibling;
			}
		};
		walk(ctx.GetRoot());
		ASSERT_EQ(marks.size(), 5u);

		for (UI::ElementHandle mark : marks) {
			const UI::Rect box = ctx.GetLayout(mark).content;
			const Vec2 boxCenter{box.x + box.w * 0.5f, box.y + box.h * 0.5f};
			const UI::UIQuad* glyph = nullptr;
			for (const UI::UIQuad& quad : list.quads) {
				const Vec2 center{quad.rect.x + quad.rect.w * 0.5f, quad.rect.y + quad.rect.h * 0.5f};
				if (quad.uv.w > 0.0f && box.Contains(center)) {
					glyph = &quad;
				}
			}
			ASSERT_NE(glyph, nullptr) << ctx.GetTextString(mark) << "' box at " << scale << "x";
			const Vec2 glyphCenter{glyph->rect.x + glyph->rect.w * 0.5f, glyph->rect.y + glyph->rect.h * 0.5f};
			EXPECT_NEAR(glyphCenter.x, boxCenter.x, 1.0f) << "'" << ctx.GetTextString(mark) << "' at " << scale << "x";
			EXPECT_NEAR(glyphCenter.y, boxCenter.y, 1.0f) << "'" << ctx.GetTextString(mark) << "' at " << scale << "x";
		}
	}
}

namespace {

	UI::ElementHandle FindByClass(UI::Context& ctx, UI::ElementHandle from, StringHash klass) {
		if (ctx.HasClass(from, klass)) {
			return from;
		}
		const UI::Element* element = ctx.GetElement(from);
		if (!element) {
			return {};
		}
		for (u32 child = element->firstChild; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = ctx.MakeHandle(child);
			UI::ElementHandle found = FindByClass(ctx, handle, klass);
			if (found.IsValid()) {
				return found;
			}
			const UI::Element* childElement = ctx.GetElement(handle);
			child = childElement ? childElement->nextSibling : UI::INVALID_UI_INDEX;
		}
		return {};
	}

	void ClickAt(UI::Context& ctx, Vec2 point, MouseButton button = MouseButton::Left) {
		InputEvent down{};
		down.type = InputEventType::MouseDown;
		down.button = button;
		down.mousePosPx = {static_cast<i32>(point.x), static_cast<i32>(point.y)};
		InputEvent up = down;
		up.type = InputEventType::MouseUp;
		ctx.InjectInput(down);
		ctx.InjectInput(up);
		ctx.Update();
	}

	Vec2 CenterOf(UI::Context& ctx, UI::ElementHandle handle) {
		const UI::Rect& box = ctx.GetLayout(handle).borderBox;
		return {box.x + box.w * 0.5f, box.y + box.h * 0.5f};
	}

	UI::ElementHandle NthChild(UI::Context& ctx, UI::ElementHandle parent, u32 index) {
		const UI::Element* element = ctx.GetElement(parent);
		for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = ctx.MakeHandle(child);
			if (index-- == 0) {
				return handle;
			}
			const UI::Element* childElement = ctx.GetElement(handle);
			child = childElement ? childElement->nextSibling : UI::INVALID_UI_INDEX;
		}
		return {};
	}

	UI::ElementHandle TabAt(UI::Context& ctx, u32 index) {
		UI::ElementHandle bar = FindByClass(ctx, ctx.GetRoot(), StringHash("dock-tab-bar"));
		return bar.IsValid() ? NthChild(ctx, bar, index) : UI::ElementHandle{};
	}

	void PickTabMenuItem(UI::Context& ctx, UI::ElementHandle tab, u32 row) {
		ClickAt(ctx, CenterOf(ctx, tab), MouseButton::Right);
		ctx.Update();
		ASSERT_EQ(ctx.GetOpenPopupCount(), 1u);
		UI::ElementHandle item = NthChild(ctx, ctx.GetOpenPopup(0), row);
		ASSERT_TRUE(item.IsValid());
		ClickAt(ctx, CenterOf(ctx, item));
	}

	bool LayoutsEqual(const Vector<UI::DockNodeDesc>& a, const Vector<UI::DockNodeDesc>& b) {
		if (a.size() != b.size()) {
			return false;
		}
		for (size_t i = 0; i < a.size(); ++i) {
			if (a[i].split != b[i].split || a[i].activeTab != b[i].activeTab ||
				a[i].childA != b[i].childA || a[i].childB != b[i].childB ||
				a[i].tabs.size() != b[i].tabs.size() ||
				std::abs(a[i].ratio - b[i].ratio) > 0.001f) {
				return false;
			}
			for (size_t t = 0; t < a[i].tabs.size(); ++t) {
				if (a[i].tabs[t] != b[i].tabs[t]) {
					return false;
				}
			}
		}
		return true;
	}

} // anonymous namespace

TEST(UIDockingTest, UnregisterPanelUndocksAndForgets) {
	DockRig rig;
	rig.dock.DockPanel("panel.a"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.dock.DockPanel("panel.b"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);

	rig.dock.UnregisterPanel("panel.a"_hash);
	EXPECT_FALSE(rig.dock.IsPanelDocked("panel.a"_hash));
	EXPECT_FALSE(rig.dock.HasPanel("panel.a"_hash));
	EXPECT_TRUE(rig.dock.IsPanelDocked("panel.b"_hash));
	EXPECT_EQ(rig.DockedCount(), 1u);
}

TEST(UIDockingTest, TabMenuCloseUndocksByDefault) {
	DockRig rig;
	rig.dock.DockPanel("panel.a"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.BuildFrame();
	rig.BuildFrame();

	PickTabMenuItem(rig.ctx, TabAt(rig.ctx, 0), 0);

	EXPECT_FALSE(rig.dock.IsPanelDocked("panel.a"_hash));
	EXPECT_TRUE(rig.dock.HasPanel("panel.a"_hash));
}

TEST(UIDockingTest, TabMenuCloseRoutesTheCallback) {
	DockRig rig;
	rig.dock.DockPanel("panel.a"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);

	StringHash closedPanel{};
	rig.dock.SetPanelCloseCallback([&](StringHash id) { closedPanel = id; });

	rig.BuildFrame();
	rig.BuildFrame();

	PickTabMenuItem(rig.ctx, TabAt(rig.ctx, 0), 0);

	EXPECT_EQ(closedPanel, "panel.a"_hash);
	EXPECT_TRUE(rig.dock.IsPanelDocked("panel.a"_hash));
}

TEST(UIDockingTest, TabMenuActsOnClickedTab) {
	DockRig rig;
	rig.dock.DockPanel("panel.a"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.dock.DockPanel("panel.b"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.BuildFrame();
	rig.BuildFrame();

	ClickAt(rig.ctx, CenterOf(rig.ctx, TabAt(rig.ctx, 0)));
	rig.BuildFrame();
	rig.BuildFrame();
	ASSERT_TRUE(rig.ctx.HasClass(TabAt(rig.ctx, 0), StringHash("dock-active-tab")));

	ClickAt(rig.ctx, CenterOf(rig.ctx, TabAt(rig.ctx, 1)), MouseButton::Right);
	rig.ctx.Update();

	EXPECT_FALSE(rig.dock.IsDraggingTab());
	EXPECT_TRUE(rig.ctx.HasClass(TabAt(rig.ctx, 0), StringHash("dock-active-tab")));
	EXPECT_FALSE(rig.ctx.HasClass(TabAt(rig.ctx, 1), StringHash("dock-active-tab")));

	ASSERT_EQ(rig.ctx.GetOpenPopupCount(), 1u);
	UI::ElementHandle item = NthChild(rig.ctx, rig.ctx.GetOpenPopup(0), 0);
	ASSERT_TRUE(item.IsValid());
	ClickAt(rig.ctx, CenterOf(rig.ctx, item));

	EXPECT_FALSE(rig.dock.IsPanelDocked("panel.b"_hash));
	EXPECT_TRUE(rig.dock.IsPanelDocked("panel.a"_hash));
}

TEST(UIDockingTest, TrailingEdgeClickActivates) {
	DockRig rig;
	rig.dock.DockPanel("panel.a"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.dock.DockPanel("panel.b"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.BuildFrame();
	rig.BuildFrame();

	const UI::Rect box = rig.ctx.GetLayout(TabAt(rig.ctx, 0)).borderBox;
	ClickAt(rig.ctx, {box.x + box.w - 1.0f, box.y + box.h * 0.5f});
	rig.BuildFrame();
	rig.BuildFrame();

	EXPECT_TRUE(rig.dock.IsPanelDocked("panel.a"_hash));
	EXPECT_TRUE(rig.dock.IsPanelDocked("panel.b"_hash));
	EXPECT_TRUE(rig.ctx.HasClass(TabAt(rig.ctx, 0), StringHash("dock-active-tab")));
	EXPECT_EQ(rig.ctx.GetOpenPopupCount(), 0u);
}

TEST(UIDockingTest, SaveRestoreLayoutRoundTrips) {
	DockRig rig;
	rig.dock.DockPanel("panel.a"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.dock.DockPanel("panel.b"_hash, rig.dock.GetRootNode(), UI::DockZone::Right);
	rig.dock.DockPanel("panel.c"_hash, rig.dock.FindNodeOfPanel("panel.a"_hash), UI::DockZone::Bottom);

	Vector<UI::DockNodeDesc> saved{GetMemoryResource(MemTag::UI)};
	rig.dock.SaveLayout(saved);
	ASSERT_GE(saved.size(), 5u);
	EXPECT_EQ(saved[0].split, UI::DockSplit::Horizontal);

	rig.dock.Reset();
	EXPECT_EQ(rig.DockedCount(), 0u);
	ASSERT_TRUE(rig.dock.RestoreLayout({saved.data(), saved.size()}));

	EXPECT_EQ(rig.DockedCount(), 3u);
	EXPECT_TRUE(rig.dock.IsPanelDocked("panel.a"_hash));
	EXPECT_TRUE(rig.dock.IsPanelDocked("panel.b"_hash));
	EXPECT_TRUE(rig.dock.IsPanelDocked("panel.c"_hash));

	Vector<UI::DockNodeDesc> resaved{GetMemoryResource(MemTag::UI)};
	rig.dock.SaveLayout(resaved);
	EXPECT_TRUE(LayoutsEqual(saved, resaved));

	rig.BuildFrame();
}

TEST(UIDockingTest, RestoreLayoutRejectsMalformedSnapshots) {
	DockRig rig;
	rig.dock.DockPanel("panel.a"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);

	Vector<UI::DockNodeDesc> bad{GetMemoryResource(MemTag::UI)};
	bad.emplace_back();
	bad[0].split = UI::DockSplit::Horizontal;
	bad[0].childA = 0;
	bad[0].childB = 0;
	EXPECT_FALSE(rig.dock.RestoreLayout({bad.data(), bad.size()}));

	bad.clear();
	bad.emplace_back();
	bad.emplace_back();
	EXPECT_FALSE(rig.dock.RestoreLayout({bad.data(), bad.size()}));

	bad.clear();
	bad.emplace_back();
	bad[0].split = UI::DockSplit::Vertical;
	bad[0].childA = 1;
	bad[0].childB = 1;
	bad.emplace_back();
	EXPECT_FALSE(rig.dock.RestoreLayout({bad.data(), bad.size()}));

	EXPECT_TRUE(rig.dock.IsPanelDocked("panel.a"_hash));
}

TEST(UIDockingTest, RestoreLayoutDropsUnknownPanels) {
	DockRig rig;

	Vector<UI::DockNodeDesc> layout{GetMemoryResource(MemTag::UI)};
	layout.emplace_back();
	layout[0].tabs.push_back("panel.a"_hash);
	layout[0].tabs.push_back("panel.zombie"_hash);
	layout[0].activeTab = 1;

	ASSERT_TRUE(rig.dock.RestoreLayout({layout.data(), layout.size()}));
	EXPECT_TRUE(rig.dock.IsPanelDocked("panel.a"_hash));
	EXPECT_FALSE(rig.dock.IsPanelDocked("panel.zombie"_hash));
	EXPECT_EQ(rig.DockedCount(), 1u);
}

namespace {

	struct HostedDockRig {
		UI::Context ctx{Desc(), nullptr};
		UI::DockManager dock{ctx, Handle<FontAsset>{}};
		UI::ElementHandle host;

		HostedDockRig() {
			UI::Style rootStyle;
			rootStyle.flexDirection = UI::FlexDirection::Column;
			rootStyle.alignItems = UI::Align::Stretch;
			ctx.SetStyle(ctx.GetRoot(), rootStyle);

			UI::ElementHandle header = ctx.CreateElement();
			ctx.AppendChild(ctx.GetRoot(), header);
			UI::Style headerStyle;
			headerStyle.height = UI::Length::Pixel(60.0f);
			headerStyle.width = UI::Length::Percent(1.0f);
			ctx.SetStyle(header, headerStyle);

			host = ctx.CreateElement();
			ctx.AppendChild(ctx.GetRoot(), host);
			UI::Style hostStyle;
			hostStyle.height = UI::Length::Pixel(0.0f);
			hostStyle.flexGrow = 1.0f;
			hostStyle.width = UI::Length::Percent(1.0f);
			ctx.SetStyle(host, hostStyle);

			dock.RegisterPanel("panel.a"_hash, "A", [](UI::Context&, UI::ElementHandle) {});
			dock.DockPanel("panel.a"_hash, dock.GetRootNode(), UI::DockZone::Center);
		}

		void BuildFrame() {
			dock.Build(host);
			ctx.Update();
		}

		bool HasElementWithRect(const UI::Rect& expected, f32 tolerance = 1.5f) {
			Vector<u32> stack{GetMemoryResource(MemTag::UI)};
			const UI::Element* root = ctx.GetElement(ctx.GetRoot());
			for (u32 child = root->firstChild; child != UI::INVALID_UI_INDEX;
				 child = ctx.GetElement(ctx.MakeHandle(child))->nextSibling) {
				stack.push_back(child);
			}
			while (!stack.empty()) {
				u32 index = stack.back();
				stack.pop_back();
				UI::ElementHandle handle = ctx.MakeHandle(index);
				const UI::Rect& box = ctx.GetLayout(handle).borderBox;
				if (std::abs(box.x - expected.x) <= tolerance && std::abs(box.y - expected.y) <= tolerance &&
					std::abs(box.w - expected.w) <= tolerance && std::abs(box.h - expected.h) <= tolerance) {
					return true;
				}
				for (u32 child = ctx.GetElement(handle)->firstChild; child != UI::INVALID_UI_INDEX;
					 child = ctx.GetElement(ctx.MakeHandle(child))->nextSibling) {
					stack.push_back(child);
				}
			}
			return false;
		}
	};

} // anonymous namespace

TEST(UIDockingTest, HostedDockAttachZonesTrackDockArea) {
	HostedDockRig rig;
	rig.BuildFrame();
	rig.BuildFrame();

	EXPECT_EQ(rig.dock.GetDropTargetAt({400.0f, 330.0f}).zone, UI::DockZone::Center);
	EXPECT_EQ(rig.dock.GetDropTargetAt({30.0f, 330.0f}).zone, UI::DockZone::Left);
	EXPECT_EQ(rig.dock.GetDropTargetAt({400.0f, 30.0f}).zone, UI::DockZone::None);
}

TEST(UIDockingTest, HostedDockExternalOverlayCoversDockArea) {
	HostedDockRig rig;
	rig.BuildFrame();
	rig.BuildFrame();

	rig.dock.ShowExternalOverlay({30.0f, 330.0f});
	rig.BuildFrame();

	EXPECT_TRUE(rig.HasElementWithRect({0.0f, 60.0f, 400.0f, 540.0f}));
	EXPECT_FALSE(rig.HasElementWithRect({0.0f, 120.0f, 400.0f, 540.0f}));
	EXPECT_FALSE(rig.HasElementWithRect({0.0f, 60.0f, 400.0f, 600.0f}));
}

TEST(UIDockingTest, PanelContentFitsLeafInSplitDock) {
	for (f32 scale : {1.0f, 1.25f}) {
		UI::ContextDesc desc;
		desc.sizePx = {1920, 1030};
		desc.contentScale = scale;
		UI::Context ctx(desc, nullptr);

		UI::Style rootStyle;
		rootStyle.flexDirection = UI::FlexDirection::Column;
		rootStyle.alignItems = UI::Align::Stretch;
		ctx.SetStyle(ctx.GetRoot(), rootStyle);

		UI::ElementHandle menuBar = MakeChild(ctx, ctx.GetRoot(), [] {
			UI::Style style;
			style.height = UI::Length::Pixel(30.0f);
			style.width = UI::Length::Percent(1.0f);
			return style;
		}());
		(void)menuBar;

		UI::ElementHandle host = MakeChild(ctx, ctx.GetRoot(), [] {
			UI::Style style;
			style.height = UI::Length::Pixel(0.0f);
			style.flexGrow = 1.0f;
			style.alignItems = UI::Align::Stretch;
			return style;
		}());

		UI::DockManager dock(ctx, Handle<FontAsset>{});
		UI::ElementHandle contentParent;
		UI::ElementHandle row;
		dock.RegisterPanel("panel.left"_hash, "Left", [](UI::Context&, UI::ElementHandle) {});
		dock.RegisterPanel("panel.right"_hash, "Right",
						   [&contentParent, &row](UI::Context& c, UI::ElementHandle parent) {
							   contentParent = parent;
							   UI::Style rowStyle;
							   rowStyle.flexDirection = UI::FlexDirection::Row;
							   rowStyle.width = UI::Length::Percent(1.0f);
							   rowStyle.padding = {8.0f, 2.0f, 8.0f, 2.0f};
							   row = MakeChild(c, parent, rowStyle);
							   MakeChild(c, row, FixedBox(110.0f, 20.0f));
							   UI::Style vectorStyle;
							   vectorStyle.flexDirection = UI::FlexDirection::Row;
							   vectorStyle.flexGrow = 1.0f;
							   UI::ElementHandle vector = MakeChild(c, row, vectorStyle);
							   for (u32 i = 0; i < 3; ++i) {
								   UI::Style fieldStyle;
								   fieldStyle.flexGrow = 1.0f;
								   fieldStyle.minWidth = UI::Length::Pixel(40.0f);
								   fieldStyle.height = UI::Length::Pixel(20.0f);
								   MakeChild(c, vector, fieldStyle);
							   }
						   });
		dock.DockPanel("panel.left"_hash, dock.GetRootNode(), UI::DockZone::Center);
		dock.DockPanel("panel.right"_hash, dock.GetRootNode(), UI::DockZone::Right);
		dock.Build(host);
		ctx.Update();

		ASSERT_TRUE(contentParent.IsValid()) << "scale " << scale;
		const UI::Rect& leaf = ctx.GetLayout(contentParent).content;
		const UI::Rect& rowBox = ctx.GetLayout(row).borderBox;
		EXPECT_LE(rowBox.x + rowBox.w, leaf.x + leaf.w + 0.01f) << "scale " << scale;
		EXPECT_NEAR(rowBox.w, leaf.w, 0.01f) << "scale " << scale;
	}
}

TEST(UIDockingTest, TabOffsetHoldsAtFractionalScale) {
	UI::ContextDesc desc;
	desc.sizePx = {1920, 1030};
	desc.contentScale = 1.25f;
	UI::Context ctx(desc, nullptr);

	UI::Style rootStyle;
	rootStyle.flexDirection = UI::FlexDirection::Column;
	rootStyle.alignItems = UI::Align::Stretch;
	ctx.SetStyle(ctx.GetRoot(), rootStyle);
	UI::ElementHandle host = MakeChild(ctx, ctx.GetRoot(), [] {
		UI::Style style;
		style.height = UI::Length::Pixel(0.0f);
		style.flexGrow = 1.0f;
		style.alignItems = UI::Align::Stretch;
		return style;
	}());

	UI::DockManager dock(ctx, Handle<FontAsset>{});
	UI::ElementHandle rightContent;
	dock.RegisterPanel("panel.left"_hash, "Left", [](UI::Context&, UI::ElementHandle) {});
	dock.RegisterPanel("panel.right"_hash, "Right",
					   [&rightContent](UI::Context&, UI::ElementHandle parent) { rightContent = parent; });
	dock.DockPanel("panel.left"_hash, dock.GetRootNode(), UI::DockZone::Center);
	dock.DockPanel("panel.right"_hash, dock.GetRootNode(), UI::DockZone::Right);

	auto childOf = [&ctx](UI::ElementHandle handle) {
		const UI::Element* element = ctx.GetElement(handle);
		return element ? ctx.MakeHandle(element->firstChild) : UI::ElementHandle{};
	};
	auto parentOf = [&ctx](UI::ElementHandle handle) {
		const UI::Element* element = ctx.GetElement(handle);
		return element ? ctx.MakeHandle(element->parent) : UI::ElementHandle{};
	};

	bool sawFractionalLeaf = false;
	for (i32 step = 0; step <= 60; ++step) {
		const f32 ratio = 0.300f + 0.002f * static_cast<f32>(step);
		Vector<UI::DockNodeDesc> layout;
		dock.SaveLayout(layout);
		ASSERT_FALSE(layout.empty());
		layout[0].ratio = ratio;
		ASSERT_TRUE(dock.RestoreLayout(std::span<const UI::DockNodeDesc>(layout.data(), layout.size())));
		dock.Build(host);
		ctx.Update();

		ASSERT_TRUE(rightContent.IsValid()) << "ratio " << ratio;
		const UI::ElementHandle leaf = parentOf(rightContent);
		const UI::ElementHandle bar = childOf(leaf);
		const UI::ElementHandle tab = childOf(bar);
		ASSERT_TRUE(tab.IsValid()) << "ratio " << ratio;

		const UI::Rect rawBar = ctx.GetLayout(bar).borderBox;
		const UI::Rect barBox = UI::SnapRectToPixels(rawBar);
		const UI::Rect tabBox = UI::SnapRectToPixels(ctx.GetLayout(tab).borderBox);
		EXPECT_EQ(tabBox.x - barBox.x, 0.0f) << ratio;
		EXPECT_EQ(tabBox.y - barBox.y, 0.0f) << ratio;
		if (rawBar.x != UI::SnapToPixel(rawBar.x)) {
			sawFractionalLeaf = true;
		}
	}
	EXPECT_TRUE(sawFractionalLeaf);
}

namespace {

	InputEvent DockMouse(InputEventType type, Vec2 point) {
		InputEvent event{};
		event.type = type;
		event.button = MouseButton::Left;
		event.mousePosPx = {static_cast<i32>(point.x), static_cast<i32>(point.y)};
		return event;
	}

	struct SplitRig {
		UI::Context ctx{Desc(), nullptr};
		UI::DockManager dock{ctx, Handle<FontAsset>{}};
		int contentEnters = 0;
		int contentDowns = 0;
		int contentClicks = 0;

		SplitRig() {
			auto fill = [](UI::Context& c, UI::ElementHandle parent) {
				UI::Style style;
				style.width = UI::Length::Percent(1.0f);
				style.flexGrow = 1.0f;
				return MakeChild(c, parent, style);
			};
			dock.RegisterPanel("panel.a"_hash, "A", [fill](UI::Context& c, UI::ElementHandle parent) {
				fill(c, parent);
			});
			dock.RegisterPanel("panel.b"_hash, "B", [this, fill](UI::Context& c, UI::ElementHandle parent) {
				UI::ElementHandle content = fill(c, parent);
				c.AddClass(content, StringHash("test-content"));
				c.Events(content).onHoverEnter = [this]() { ++contentEnters; };
				c.Events(content).onMouseDown = [this]() { ++contentDowns; };
				c.Events(content).onClick = [this]() { ++contentClicks; };
			});
			dock.DockPanel("panel.a"_hash, dock.GetRootNode(), UI::DockZone::Center);
			dock.DockPanel("panel.b"_hash, dock.GetRootNode(), UI::DockZone::Right);
			Frame();
			Frame();
		}

		void Frame() {
			dock.Build(ctx.GetRoot());
			ctx.Update();
		}

		UI::ElementHandle Splitter() { return FindByClass(ctx, ctx.GetRoot(), StringHash("dock-splitter")); }
		UI::ElementHandle Content() { return FindByClass(ctx, ctx.GetRoot(), StringHash("test-content")); }
		f32 SplitterX() { return ctx.GetLayout(Splitter()).borderBox.x; }
	};

} // anonymous namespace

TEST(UIDockingTest, SplitterResizeRidesTheSplittersOwnPress) {
	SplitRig rig;
	ASSERT_TRUE(rig.Splitter().IsValid());
	f32 startX = rig.SplitterX();

	Vec2 grab = CenterOf(rig.ctx, rig.Splitter());
	rig.ctx.InjectInput(DockMouse(InputEventType::MouseMove, grab));
	rig.ctx.InjectInput(DockMouse(InputEventType::MouseDown, grab));
	rig.ctx.Update();
	rig.ctx.InjectInput(DockMouse(InputEventType::MouseMove, {grab.x - 80.0f, grab.y}));
	rig.ctx.Update();

	EXPECT_LT(rig.SplitterX(), startX - 60.0f);
	f32 dragged = rig.SplitterX();
	rig.ctx.InjectInput(DockMouse(InputEventType::MouseUp, {grab.x - 80.0f, grab.y}));
	rig.ctx.Update();
	EXPECT_FLOAT_EQ(rig.SplitterX(), dragged);

	rig.ctx.InjectInput(DockMouse(InputEventType::MouseMove, {grab.x + 120.0f, grab.y}));
	rig.ctx.Update();
	EXPECT_FLOAT_EQ(rig.SplitterX(), dragged);
}

TEST(UIDockingTest, SplitterDragLocksOutEverythingUnderIt) {
	SplitRig rig;
	Vec2 grab = CenterOf(rig.ctx, rig.Splitter());
	Vec2 deepInPanel = {static_cast<f32>(rig.ctx.GetSize().x) - 10.0f, CenterOf(rig.ctx, rig.Content()).y};

	rig.ctx.InjectInput(DockMouse(InputEventType::MouseMove, grab));
	rig.ctx.InjectInput(DockMouse(InputEventType::MouseDown, grab));
	rig.ctx.Update();

	rig.ctx.InjectInput(DockMouse(InputEventType::MouseMove, deepInPanel));
	rig.ctx.Update();
	ASSERT_EQ(rig.ctx.HitTest(deepInPanel), rig.Content());
	EXPECT_EQ(rig.contentEnters, 0);
	EXPECT_FALSE(rig.ctx.GetHovered().IsValid());

	rig.ctx.InjectInput(DockMouse(InputEventType::MouseUp, deepInPanel));
	rig.ctx.Update();
	EXPECT_EQ(rig.contentDowns, 0);
	EXPECT_EQ(rig.contentClicks, 0);

	EXPECT_EQ(rig.ctx.GetHovered(), rig.Content());
	EXPECT_EQ(rig.contentEnters, 1);
}

TEST(UIDockingTest, PressBesideSplitterBelongsToPanelAlone) {
	SplitRig rig;
	const UI::Rect& band = rig.ctx.GetLayout(rig.Splitter()).borderBox;
	f32 startX = band.x;
	Vec2 beside = {band.x + band.w + 2.0f, CenterOf(rig.ctx, rig.Content()).y};

	rig.ctx.InjectInput(DockMouse(InputEventType::MouseMove, beside));
	rig.ctx.InjectInput(DockMouse(InputEventType::MouseDown, beside));
	rig.ctx.Update();
	rig.ctx.InjectInput(DockMouse(InputEventType::MouseMove, {beside.x - 80.0f, beside.y}));
	rig.ctx.Update();

	EXPECT_EQ(rig.contentDowns, 1);
	EXPECT_FLOAT_EQ(rig.SplitterX(), startX);
}

namespace {

	struct CrowdedStripRig {
		UI::Context ctx{[] {
							UI::ContextDesc desc;
							desc.sizePx = {160, 300};
							desc.contentScale = 1.0f;
							return desc;
						}(),
						nullptr};
		UI::DockManager dock{ctx, Handle<FontAsset>{}};

		CrowdedStripRig() {
			for (u32 i = 0; i < 12; ++i) {
				StringHash id = StringHash(("panel." + std::to_string(i)).c_str());
				dock.RegisterPanel(id, "P", [](UI::Context&, UI::ElementHandle) {});
				dock.DockPanel(id, dock.GetRootNode(), UI::DockZone::Center);
			}
			dock.Build(ctx.GetRoot());
			ctx.Update();
		}

		UI::ElementHandle Bar() { return FindByClass(ctx, ctx.GetRoot(), StringHash("dock-tab-bar")); }
	};

	void WheelOver(UI::Context& ctx, Vec2 point, f32 notches) {
		InputEvent move{};
		move.type = InputEventType::MouseMove;
		move.mousePosPx = {static_cast<i32>(point.x), static_cast<i32>(point.y)};
		ctx.InjectInput(move);

		InputEvent wheel{};
		wheel.type = InputEventType::MouseWheel;
		wheel.mousePosPx = move.mousePosPx;
		wheel.wheel = {0.0f, notches};
		ctx.InjectInput(wheel);
		ctx.Update();
	}

} // anonymous namespace

TEST(UIDockingTest, ScrollRowOverflowsInsteadOfCrushingChildren) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle row = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), row);
	UI::Style rowStyle;
	rowStyle.width = UI::Length::Pixel(160.0f);
	rowStyle.height = UI::Length::Pixel(30.0f);
	rowStyle.flexDirection = UI::FlexDirection::Row;
	rowStyle.overflow = UI::Overflow::Scroll;
	ctx.SetStyle(row, rowStyle);
	for (u32 i = 0; i < 12; ++i) {
		UI::ElementHandle child = ctx.CreateElement();
		ctx.AppendChild(row, child);
		UI::Style childStyle;
		childStyle.width = UI::Length::Pixel(40.0f);
		childStyle.height = UI::Length::Pixel(20.0f);
		childStyle.flexShrink = 0.0f;
		ctx.SetStyle(child, childStyle);
	}
	ctx.Update();

	const UI::Element* element = ctx.GetElement(row);
	EXPECT_FLOAT_EQ(ctx.GetLayout(ctx.MakeHandle(element->firstChild)).borderBox.w, 40.0f);
	EXPECT_FLOAT_EQ(element->maxScroll.x, 320.0f);
}

TEST(UIDockingTest, TabsKeepWidthWhenStripIsFull) {
	CrowdedStripRig rig;
	UI::ElementHandle bar = rig.Bar();
	ASSERT_TRUE(bar.IsValid());

	const UI::Element* element = rig.ctx.GetElement(bar);
	f32 first = rig.ctx.GetLayout(rig.ctx.MakeHandle(element->firstChild)).borderBox.w;
	ASSERT_GT(first, 0.0f);

	f32 total = 0.0f;
	for (u32 child = element->firstChild; child != UI::INVALID_UI_INDEX;) {
		UI::ElementHandle handle = rig.ctx.MakeHandle(child);
		EXPECT_FLOAT_EQ(rig.ctx.GetLayout(handle).borderBox.w, first);
		total += rig.ctx.GetLayout(handle).borderBox.w;
		const UI::Element* childElement = rig.ctx.GetElement(handle);
		child = childElement ? childElement->nextSibling : UI::INVALID_UI_INDEX;
	}

	EXPECT_FLOAT_EQ(first, 16.0f);
	EXPECT_GT(total, rig.ctx.GetLayout(bar).content.w);
}

TEST(UIDockingTest, FullTabStripHasSomewhereToScroll) {
	CrowdedStripRig rig;
	UI::ElementHandle bar = rig.Bar();
	ASSERT_TRUE(bar.IsValid());

	const UI::Element* element = rig.ctx.GetElement(bar);
	ASSERT_NE(element, nullptr);
	EXPECT_GT(element->maxScroll.x, 0.0f);
	EXPECT_FLOAT_EQ(element->maxScroll.y, 0.0f);
}

TEST(UIDockingTest, VerticalWheelNotchScrollsStripSideways) {
	CrowdedStripRig rig;
	UI::ElementHandle bar = rig.Bar();
	ASSERT_TRUE(bar.IsValid());

	Vec2 overStrip = CenterOf(rig.ctx, bar);
	ASSERT_FLOAT_EQ(rig.ctx.GetElement(bar)->scrollOffset.x, 0.0f);

	WheelOver(rig.ctx, overStrip, -1.0f);
	f32 scrolled = rig.ctx.GetElement(bar)->scrollOffset.x;
	EXPECT_GT(scrolled, 0.0f);

	WheelOver(rig.ctx, overStrip, 1.0f);
	EXPECT_LT(rig.ctx.GetElement(bar)->scrollOffset.x, scrolled);
}

TEST(UIDockingTest, VerticallyScrollableElementKeepsVerticalNotch) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle box = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), box);
	UI::Style style;
	style.width = UI::Length::Pixel(100.0f);
	style.height = UI::Length::Pixel(100.0f);
	style.overflow = UI::Overflow::Scroll;
	style.flexDirection = UI::FlexDirection::Column;
	ctx.SetStyle(box, style);

	for (u32 i = 0; i < 8; ++i) {
		UI::ElementHandle child = ctx.CreateElement();
		ctx.AppendChild(box, child);
		UI::Style childStyle;
		childStyle.width = UI::Length::Pixel(400.0f);
		childStyle.height = UI::Length::Pixel(40.0f);
		ctx.SetStyle(child, childStyle);
	}
	ctx.Update();

	const UI::Element* element = ctx.GetElement(box);
	ASSERT_GT(element->maxScroll.y, 0.0f);

	WheelOver(ctx, CenterOf(ctx, box), -1.0f);
	EXPECT_GT(ctx.GetElement(box)->scrollOffset.y, 0.0f);
	EXPECT_FLOAT_EQ(ctx.GetElement(box)->scrollOffset.x, 0.0f);
}

namespace {

	bool HasAccentTopStripe(const UI::Context& ctx, UI::ElementHandle tab) {
		const UI::Style& style = ctx.GetComputedStyle(tab);
		const Color accent = ctx.GetStyleVarColor("--slt-accent"_hash, Color::Black());
		for (u8 i = 0; i < style.boxShadowCount; ++i) {
			const UI::BoxShadow& shadow = style.boxShadows[i];
			if (shadow.inset && shadow.y > 0.0f && shadow.x == 0.0f && shadow.color == accent) {
				return true;
			}
		}
		return false;
	}

	void CollectByClass(UI::Context& ctx, UI::ElementHandle root, StringHash klass, Vector<UI::ElementHandle>& out) {
		if (ctx.HasClass(root, klass)) {
			out.push_back(root);
		}
		const UI::Element* pod = ctx.GetElement(root);
		if (!pod) {
			return;
		}
		for (u32 child = pod->firstChild; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = ctx.MakeHandle(child);
			CollectByClass(ctx, handle, klass, out);
			child = ctx.GetElement(handle)->nextSibling;
		}
	}

	struct FocusRig : DockRig {
		FocusRig() {
			dock.DockPanel("panel.a"_hash, dock.GetRootNode(), UI::DockZone::Center);
			dock.DockPanel("panel.b"_hash, dock.GetRootNode(), UI::DockZone::Center);
			BuildFrame();
		}
		UI::ElementHandle Tab(u32 index) {
			Vector<UI::ElementHandle> tabs{GetMemoryResource(MemTag::Debug)};
			CollectByClass(ctx, ctx.GetRoot(), StringHash("dock-tab"), tabs);
			return index < tabs.size() ? tabs[index] : UI::ElementHandle{};
		}
	};

} // anonymous namespace

TEST(UIDockingTest, OnlyFocusedTabStriped) {
	FocusRig rig;
	ASSERT_FALSE(HasAccentTopStripe(rig.ctx, rig.Tab(0)));
	ASSERT_FALSE(HasAccentTopStripe(rig.ctx, rig.Tab(1)));

	rig.dock.SetFocusedPanel("panel.b"_hash);
	rig.ctx.Update();

	EXPECT_TRUE(HasAccentTopStripe(rig.ctx, rig.Tab(1)));
	EXPECT_FALSE(HasAccentTopStripe(rig.ctx, rig.Tab(0)));

	rig.dock.SetFocusedPanel("panel.a"_hash);
	rig.ctx.Update();
	EXPECT_TRUE(HasAccentTopStripe(rig.ctx, rig.Tab(0)));
	EXPECT_FALSE(HasAccentTopStripe(rig.ctx, rig.Tab(1)));
}

TEST(UIDockingTest, UnknownIdClearsStripe) {
	FocusRig rig;
	rig.dock.SetFocusedPanel("panel.a"_hash);
	rig.ctx.Update();
	ASSERT_TRUE(HasAccentTopStripe(rig.ctx, rig.Tab(0)));

	rig.dock.SetFocusedPanel("panel.somewhere.else"_hash);
	rig.ctx.Update();
	EXPECT_FALSE(HasAccentTopStripe(rig.ctx, rig.Tab(0)));
	EXPECT_FALSE(HasAccentTopStripe(rig.ctx, rig.Tab(1)));
	EXPECT_EQ(rig.dock.GetFocusedPanel(), StringHash("panel.somewhere.else"));
}

TEST(UIDockingTest, PressingATabFocusesItsPanel) {
	FocusRig rig;
	ASSERT_EQ(rig.dock.GetFocusedPanel(), StringHash{});

	ClickElement(rig.ctx, rig.Tab(1));
	rig.BuildFrame();

	EXPECT_EQ(rig.dock.GetFocusedPanel(), StringHash("panel.b"));
	EXPECT_TRUE(HasAccentTopStripe(rig.ctx, rig.Tab(1)));
}

TEST(UIDockingTest, FocusChangeLeavesPanelContentStanding) {
	FocusRig rig;
	rig.dock.ActivatePanel("panel.a"_hash);
	rig.BuildFrame();
	const int buildsBefore = rig.builds;
	ASSERT_GT(buildsBefore, 0);

	rig.dock.SetFocusedPanel("panel.a"_hash);
	rig.BuildFrame();
	rig.dock.SetFocusedPanel("panel.b"_hash);
	rig.BuildFrame();
	rig.dock.SetFocusedPanel(StringHash{});
	rig.BuildFrame();

	EXPECT_EQ(rig.builds, buildsBefore);
}

TEST(UIDockingTest, FocusedTabIsStillMarkedAfterRebuild) {
	FocusRig rig;
	rig.dock.SetFocusedPanel("panel.b"_hash);
	rig.ctx.Update();
	ASSERT_TRUE(HasAccentTopStripe(rig.ctx, rig.Tab(1)));

	rig.dock.DockPanel("panel.c"_hash, rig.dock.GetRootNode(), UI::DockZone::Center);
	rig.BuildFrame();

	EXPECT_TRUE(HasAccentTopStripe(rig.ctx, rig.Tab(1)));
	EXPECT_FALSE(HasAccentTopStripe(rig.ctx, rig.Tab(0)));
	EXPECT_FALSE(HasAccentTopStripe(rig.ctx, rig.Tab(2)));
}

VectorAsset* MakeTabPicture() {
	VectorAsset* image = New<VectorAsset>(MemTag::Asset);
	str error;
	const bool ok = image->ParseSvg(
		R"(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 16 16">)"
		R"(<path fill="#ffffff" d="M2 2 L14 2 L14 14 L2 14 Z M5 5 L5 11 L11 11 L11 5 Z"/></svg>)",
		error);
	SLT_CORE_ASSERT_MSG(ok, "the dock tab picture fixture no longer imports");
	(void)ok;
	return image;
}

TEST(UIDockingTest, TabShowsRegisteredIcon) {
	FontAsset* face = New<FontAsset>(MemTag::Asset);
	VectorAsset* picture = MakeTabPicture();
	{
		MockFontSystem fonts;
		UI::ContextDesc desc = Desc();
		UI::Context ctx(desc, &fonts);
		UI::DockManager dock(ctx, Handle<FontAsset>(face));

		UI::DockTabIcon icon;
		icon.image = Handle<VectorAsset>(picture);
		dock.RegisterPanel("panel.iconed"_hash, "Iconed", [](UI::Context&, UI::ElementHandle) {}, icon);
		dock.RegisterPanel("panel.bare"_hash, "Bare", [](UI::Context&, UI::ElementHandle) {});
		dock.DockPanel("panel.iconed"_hash, dock.GetRootNode(), UI::DockZone::Center);
		dock.DockPanel("panel.bare"_hash, dock.GetRootNode(), UI::DockZone::Center);
		dock.Build(ctx.GetRoot());
		ctx.Update();

		Vector<UI::ElementHandle> tabs{GetMemoryResource(MemTag::Debug)};
		CollectByClass(ctx, ctx.GetRoot(), StringHash("dock-tab"), tabs);
		ASSERT_EQ(tabs.size(), 2u);

		Vector<UI::ElementHandle> icons{GetMemoryResource(MemTag::Debug)};
		CollectByClass(ctx, tabs[0], StringHash("dock-tab-icon"), icons);
		EXPECT_EQ(icons.size(), 1u);

		icons.clear();
		CollectByClass(ctx, tabs[1], StringHash("dock-tab-icon"), icons);
		EXPECT_TRUE(icons.empty());

		Vector<UI::ElementHandle> labels{GetMemoryResource(MemTag::Debug)};
		CollectByClass(ctx, tabs[1], StringHash("dock-tab-label"), labels);
		ASSERT_EQ(labels.size(), 1u);
		const UI::Rect& bareTab = ctx.GetLayout(tabs[1]).content;
		const UI::Rect& bareLabel = ctx.GetLayout(labels[0]).borderBox;
		EXPECT_NEAR(bareLabel.x, bareTab.x, 0.5f);

		labels.clear();
		icons.clear();
		CollectByClass(ctx, tabs[0], StringHash("dock-tab-label"), labels);
		CollectByClass(ctx, tabs[0], StringHash("dock-tab-icon"), icons);
		ASSERT_EQ(labels.size(), 1u);
		ASSERT_EQ(icons.size(), 1u);
		const UI::Rect& iconBox = ctx.GetLayout(icons[0]).borderBox;
		EXPECT_GE(ctx.GetLayout(labels[0]).borderBox.x, iconBox.x + iconBox.w);
	}
	Delete(picture);
	Delete(face);
}

TEST(UIDockingTest, IconlessReRegisterClears) {
	FontAsset* face = New<FontAsset>(MemTag::Asset);
	VectorAsset* picture = MakeTabPicture();
	{
		MockFontSystem fonts;
		UI::ContextDesc desc = Desc();
		UI::Context ctx(desc, &fonts);
		UI::DockManager dock(ctx, Handle<FontAsset>(face));

		UI::DockTabIcon icon;
		icon.image = Handle<VectorAsset>(picture);
		dock.RegisterPanel("panel.a"_hash, "A", [](UI::Context&, UI::ElementHandle) {}, icon);
		dock.RegisterPanel("panel.b"_hash, "B", [](UI::Context&, UI::ElementHandle) {});
		dock.DockPanel("panel.a"_hash, dock.GetRootNode(), UI::DockZone::Center);
		dock.Build(ctx.GetRoot());
		ctx.Update();

		Vector<UI::ElementHandle> icons{GetMemoryResource(MemTag::Debug)};
		CollectByClass(ctx, ctx.GetRoot(), StringHash("dock-tab-icon"), icons);
		ASSERT_EQ(icons.size(), 1u);

		dock.RegisterPanel("panel.a"_hash, "A", [](UI::Context&, UI::ElementHandle) {});
		dock.DockPanel("panel.b"_hash, dock.GetRootNode(), UI::DockZone::Center);
		dock.Build(ctx.GetRoot());
		ctx.Update();

		icons.clear();
		CollectByClass(ctx, ctx.GetRoot(), StringHash("dock-tab-icon"), icons);
		EXPECT_TRUE(icons.empty());
	}
	Delete(picture);
	Delete(face);
}
