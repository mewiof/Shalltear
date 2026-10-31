#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Editor/UIDebugOps.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	struct DockFixture {
		CountingFont font;
		UI::Context ctx;
		UI::DockManager dock{ctx, Handle<FontAsset>{}};

		static constexpr StringHash Panels[2] = {"panel.a"_hash, "panel.b"_hash};
		static constexpr Vec2 LeftLeafCenter{400.0f, 450.0f};
		static constexpr Vec2 LeftLeafLeftEdge{30.0f, 450.0f};
		static constexpr Vec2 RightLeafRightEdge{1570.0f, 450.0f};

		DockFixture() : ctx(MakeDesc(), &font) {
			for (StringHash panel : Panels) {
				dock.RegisterPanel(panel, "Panel", [](UI::Context& c, UI::ElementHandle parent) {
					UI::ElementHandle body = c.CreateElement();
					c.AppendChild(parent, body);
				});
			}
			dock.DockPanel(Panels[0], dock.GetRootNode(), UI::DockZone::Center);
			dock.DockPanel(Panels[1], dock.GetRootNode(), UI::DockZone::Right);
			Frame();
			Frame();
			Frame();
		}

		static UI::ContextDesc MakeDesc() {
			UI::ContextDesc desc;
			desc.sizePx = {1600, 900};
			return desc;
		}

		void Frame() {
			dock.Build(ctx.GetRoot());
			ctx.Update();
		}

		u32 Revision() const { return ctx.GetTreeRevision(); }
	};

} // namespace

TEST(DockOverlayChurnTest, UntouchedDockDoesNotMoveTreeRevision) {
	DockFixture fixture;

	const u32 before = fixture.Revision();
	for (int i = 0; i < 10; ++i) {
		fixture.Frame();
	}
	EXPECT_EQ(fixture.Revision(), before);
}

TEST(DockOverlayChurnTest, StaticOverlayDoesNotChurn) {
	DockFixture fixture;
	fixture.dock.ShowExternalOverlay(DockFixture::LeftLeafCenter);
	fixture.Frame();

	const u32 before = fixture.Revision();
	constexpr int FRAMES = 10;
	for (int i = 0; i < FRAMES; ++i) {
		fixture.Frame();
	}
	const u32 churn = fixture.Revision() - before;

	EXPECT_EQ(churn, 0u);
}

TEST(DockOverlayChurnTest, MovingBetweenZonesStillMovesThePreview) {
	DockFixture fixture;
	fixture.dock.ShowExternalOverlay(DockFixture::LeftLeafLeftEdge);
	for (int i = 0; i < 3; ++i) {
		fixture.Frame();
	}
	UI::RenderList left;
	fixture.ctx.BuildRenderList(left);

	const u32 before = fixture.Revision();
	fixture.dock.ShowExternalOverlay(DockFixture::RightLeafRightEdge);
	fixture.Frame();
	EXPECT_NE(fixture.Revision(), before);

	UI::RenderList right;
	fixture.ctx.BuildRenderList(right);
	ASSERT_EQ(left.quads.size(), right.quads.size());
	bool moved = false;
	for (size_t i = 0; i < left.quads.size(); ++i) {
		if (left.quads[i].rect.x != right.quads[i].rect.x || left.quads[i].rect.y != right.quads[i].rect.y) {
			moved = true;
			break;
		}
	}
	EXPECT_TRUE(moved);
}

TEST(DockOverlayChurnTest, OverlayDrawnWithoutRebuild) {
	DockFixture fixture;

	UI::RenderList quiet;
	quiet.Clear();
	fixture.ctx.BuildRenderList(quiet);
	const size_t withoutOverlay = quiet.quads.size();

	fixture.dock.ShowExternalOverlay(DockFixture::LeftLeafCenter);
	for (int i = 0; i < 5; ++i) {
		fixture.Frame();
	}
	UI::RenderList shown;
	shown.Clear();
	fixture.ctx.BuildRenderList(shown);

	EXPECT_GT(shown.quads.size(), withoutOverlay);

	fixture.dock.HideExternalOverlay();
	for (int i = 0; i < 5; ++i) {
		fixture.Frame();
	}
	UI::RenderList hidden;
	hidden.Clear();
	fixture.ctx.BuildRenderList(hidden);
	EXPECT_EQ(hidden.quads.size(), withoutOverlay);
}

namespace {

	struct TabDragFixture {
		CountingFont font;
		UI::Context ctx;
		UI::DockManager dock{ctx, Handle<FontAsset>{}};
		Vec2 mouse{};

		static constexpr StringHash Panels[2] = {"panel.a"_hash, "panel.b"_hash};
		static constexpr Vec2I Parked{0, 0};

		TabDragFixture() : ctx(MakeDesc(), &font) {
			for (StringHash panel : Panels) {
				dock.RegisterPanel(panel, "Panel", [](UI::Context& c, UI::ElementHandle parent) {
					UI::ElementHandle body = c.CreateElement();
					c.AppendChild(parent, body);
				});
			}
			dock.DockPanel(Panels[0], dock.GetRootNode(), UI::DockZone::Center);
			dock.DockPanel(Panels[1], dock.GetRootNode(), UI::DockZone::Center);
			Frame();
			Frame();
			Frame();
		}

		static UI::ContextDesc MakeDesc() {
			UI::ContextDesc desc;
			desc.sizePx = {1600, 900};
			return desc;
		}

		void Frame() {
			dock.UpdateInteraction(mouse, Parked, dragging, false);
			dock.Build(ctx.GetRoot());
			ctx.Update();
		}

		void StartDrag() {
			UI::ElementHandle tab = FindByClass(ctx, ctx.GetRoot(), StringHash("dock-tab"), 0);
			const UI::Rect box = ctx.GetLayout(tab).borderBox;
			Vec2 grab{box.x + box.w * 0.5f, box.y + box.h * 0.5f};

			dock.UpdateInteraction(grab, Parked, false, false);
			InputEvent down{};
			down.type = InputEventType::MouseDown;
			down.button = MouseButton::Left;
			down.mousePosPx = {static_cast<i32>(grab.x), static_cast<i32>(grab.y)};
			ctx.InjectInput(down);
			ctx.Update();
			dock.UpdateInteraction(grab, Parked, true, true);

			mouse = {grab.x + 200.0f, grab.y + 200.0f};
			dragging = true;
			dock.UpdateInteraction(mouse, Parked, true, false);
		}

		str GhostText() {
			UI::ElementHandle ghost = FindByClass(ctx, ctx.GetRoot(), StringHash("dock-tab-ghost"), 0);
			return ctx.IsAlive(ghost) ? ctx.GetTextString(ghost) : str{};
		}

		bool dragging = false;
		u32 Revision() const { return ctx.GetTreeRevision(); }
	};

} // namespace

TEST(DockOverlayChurnTest, StillTabGhostDoesNotChurn) {
	TabDragFixture fixture;
	fixture.StartDrag();
	fixture.Frame();
	ASSERT_TRUE(fixture.dock.IsDraggingTab());

	const u32 before = fixture.Revision();
	constexpr int FRAMES = 10;
	for (int i = 0; i < FRAMES; ++i) {
		fixture.Frame();
	}
	const u32 churn = fixture.Revision() - before;

	EXPECT_EQ(churn, 0u);
}

TEST(DockOverlayChurnTest, StillGhostKeepsSnapshot) {
	TabDragFixture fixture;
	fixture.StartDrag();
	fixture.Frame();
	ASSERT_TRUE(fixture.dock.IsDraggingTab());

	UIDebugOps::Snapshot first;
	UIDebugOps::BuildSnapshot(fixture.ctx, first);
	fixture.Frame();
	UIDebugOps::Snapshot second;
	UIDebugOps::BuildSnapshot(fixture.ctx, second);

	ASSERT_EQ(first.rows.size(), second.rows.size());
	for (size_t i = 0; i < first.rows.size(); ++i) {
		ASSERT_EQ(first.rows[i].element, second.rows[i].element);
	}
}

TEST(DockOverlayChurnTest, MovingTabGhostStillFollowsCursor) {
	TabDragFixture fixture;
	fixture.StartDrag();
	fixture.Frame();
	ASSERT_TRUE(fixture.dock.IsDraggingTab());

	UI::RenderList before;
	fixture.ctx.BuildRenderList(before);

	fixture.mouse = {fixture.mouse.x + 300.0f, fixture.mouse.y + 150.0f};
	fixture.Frame();
	UI::RenderList after;
	fixture.ctx.BuildRenderList(after);

	ASSERT_EQ(before.quads.size(), after.quads.size());
	bool moved = false;
	for (size_t i = 0; i < before.quads.size(); ++i) {
		if (before.quads[i].rect.x != after.quads[i].rect.x || before.quads[i].rect.y != after.quads[i].rect.y) {
			moved = true;
			break;
		}
	}
	EXPECT_TRUE(moved);
}

TEST(DockOverlayChurnTest, MidDragUnregisterClearsGhost) {
	TabDragFixture fixture;
	fixture.StartDrag();
	fixture.Frame();
	ASSERT_TRUE(fixture.dock.IsDraggingTab());

	ASSERT_EQ(fixture.GhostText(), "Panel");

	fixture.dock.UnregisterPanel(TabDragFixture::Panels[0]);
	fixture.Frame();
	ASSERT_TRUE(fixture.dock.IsDraggingTab());

	EXPECT_EQ(fixture.GhostText(), "?");
}
