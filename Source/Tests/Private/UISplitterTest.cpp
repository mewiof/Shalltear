#include "TestsPCH.h"

#include "UITestUtil.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	constexpr f32 THICKNESS = 4.0f;

	UI::ContextDesc Desc(Vec2I size = {400, 300}, f32 scale = 1.0f) {
		UI::ContextDesc desc;
		desc.sizePx = size;
		desc.contentScale = scale;
		return desc;
	}

	InputEvent Mouse(InputEventType type, Vec2 point) {
		InputEvent event{};
		event.type = type;
		event.button = MouseButton::Left;
		event.mousePosPx = {static_cast<i32>(point.x), static_cast<i32>(point.y)};
		return event;
	}

	Vec2 Center(UI::Context& ctx, UI::ElementHandle handle) {
		const UI::Rect& box = ctx.GetLayout(handle).borderBox;
		return {box.x + box.w * 0.5f, box.y + box.h * 0.5f};
	}

	struct SplitRig {
		UI::Context ctx;
		UI::Widgets::SplitParts parts;
		f32 reported = -1.0f;
		int reports = 0;

		SplitRig(const UI::Widgets::SplitDesc& in, Vec2I size = {400, 300}, f32 scale = 1.0f)
			: ctx(Desc(size, scale), nullptr) {
			UI::Widgets::SplitDesc desc = in;
			desc.onPositionChanged = [this](f32 position) {
				reported = position;
				++reports;
			};
			parts = UI::Widgets::Split(ctx, ctx.GetRoot(), desc);
			ctx.Update();
		}

		f32 FirstW() const { return ctx.GetLayout(parts.first).borderBox.w; }
		f32 SecondW() const { return ctx.GetLayout(parts.second).borderBox.w; }
		f32 FirstH() const { return ctx.GetLayout(parts.first).borderBox.h; }
		f32 BandX() const { return ctx.GetLayout(parts.splitter).borderBox.x; }

		Vec2 Grab() {
			Vec2 point = Center(ctx, parts.splitter);
			ctx.InjectInput(Mouse(InputEventType::MouseMove, point));
			ctx.InjectInput(Mouse(InputEventType::MouseDown, point));
			ctx.Update();
			return point;
		}

		void MoveTo(Vec2 point) {
			ctx.InjectInput(Mouse(InputEventType::MouseMove, point));
			ctx.Update();
		}

		void Release(Vec2 point) {
			ctx.InjectInput(Mouse(InputEventType::MouseUp, point));
			ctx.Update();
		}
	};

	UI::Widgets::SplitDesc Ratio(f32 position = 0.5f) {
		UI::Widgets::SplitDesc desc;
		desc.sizing = UI::Widgets::SplitSizing::Ratio;
		desc.position = position;
		return desc;
	}

} // namespace

TEST(UISplitterTest, DragMovesSplitSameFrame) {
	SplitRig rig{Ratio()};
	f32 startX = rig.BandX();
	Vec2 grab = rig.Grab();

	rig.ctx.InjectInput(Mouse(InputEventType::MouseMove, {grab.x - 80.0f, grab.y}));
	rig.ctx.Update();

	EXPECT_NEAR(rig.BandX(), startX - 80.0f, 1.0f);
}

TEST(UISplitterTest, ADragCreatesAndDestroysNoElements) {
	SplitRig rig{Ratio()};
	UI::ElementHandle content = rig.ctx.CreateElement();
	rig.ctx.AppendChild(rig.parts.first, content);
	rig.ctx.Update();

	Vec2 grab = rig.Grab();
	rig.MoveTo({grab.x - 60.0f, grab.y});
	rig.MoveTo({grab.x - 20.0f, grab.y});
	rig.Release({grab.x - 20.0f, grab.y});

	EXPECT_TRUE(rig.ctx.IsAlive(rig.parts.root));
	EXPECT_TRUE(rig.ctx.IsAlive(rig.parts.first));
	EXPECT_TRUE(rig.ctx.IsAlive(rig.parts.second));
	EXPECT_TRUE(rig.ctx.IsAlive(rig.parts.splitter));
	EXPECT_TRUE(rig.ctx.IsAlive(content));
}

TEST(UISplitterTest, RatioScalesBothPanesWhenContainerGrows) {
	SplitRig rig{Ratio(0.25f)};
	f32 before = rig.FirstW();
	ASSERT_GT(before, 0.0f);

	rig.ctx.Resize({800, 300});
	rig.ctx.Update();

	EXPECT_NEAR(rig.FirstW() / before, 2.0f, 0.05f);
}

TEST(UISplitterTest, FixedPaneHoldsPixelsWhenContainerGrows) {
	UI::Widgets::SplitDesc desc;
	desc.sizing = UI::Widgets::SplitSizing::FirstFixed;
	desc.position = 120.0f;
	SplitRig rig{desc};
	EXPECT_FLOAT_EQ(rig.FirstW(), 120.0f);

	rig.ctx.Resize({800, 300});
	rig.ctx.Update();

	EXPECT_FLOAT_EQ(rig.FirstW(), 120.0f);
	EXPECT_NEAR(rig.SecondW(), 800.0f - 120.0f - THICKNESS, 0.5f);
}

TEST(UISplitterTest, SecondFixedMirrorsFirstFixed) {
	UI::Widgets::SplitDesc desc;
	desc.sizing = UI::Widgets::SplitSizing::SecondFixed;
	desc.position = 120.0f;
	SplitRig rig{desc};
	EXPECT_FLOAT_EQ(rig.SecondW(), 120.0f);

	Vec2 grab = rig.Grab();
	rig.MoveTo({grab.x - 50.0f, grab.y});

	EXPECT_NEAR(rig.SecondW(), 170.0f, 1.0f);
	EXPECT_NEAR(rig.reported, 170.0f, 1.0f);
}

TEST(UISplitterTest, FixedPaneYields) {
	UI::Widgets::SplitDesc desc;
	desc.sizing = UI::Widgets::SplitSizing::FirstFixed;
	desc.position = 300.0f;
	SplitRig rig{desc, {400, 300}};
	ASSERT_FLOAT_EQ(rig.FirstW(), 300.0f);

	rig.ctx.Resize({160, 300});
	rig.ctx.Update();

	EXPECT_LE(rig.FirstW(), 160.0f - THICKNESS);
	EXPECT_GE(rig.SecondW(), 0.0f);
	EXPECT_LE(rig.FirstW() + rig.SecondW() + THICKNESS, 160.5f);

	rig.ctx.Resize({400, 300});
	rig.ctx.Update();
	EXPECT_FLOAT_EQ(rig.FirstW(), 300.0f);
}

TEST(UISplitterTest, MinimumsClampBothEnds) {
	UI::Widgets::SplitDesc desc = Ratio(0.5f);
	desc.minFirstPx = 60.0f;
	desc.minSecondPx = 100.0f;
	SplitRig rig{desc};

	Vec2 grab = rig.Grab();
	rig.MoveTo({0.0f, grab.y});
	EXPECT_NEAR(rig.FirstW(), 60.0f, 1.0f);

	rig.MoveTo({400.0f, grab.y});
	EXPECT_NEAR(rig.SecondW(), 100.0f, 1.0f);
}

TEST(UISplitterTest, UnfittingMinimumsPinFirst) {
	UI::Widgets::SplitDesc desc = Ratio(0.5f);
	desc.minFirstPx = 300.0f;
	desc.minSecondPx = 300.0f;
	SplitRig rig{desc};

	Vec2 grab = rig.Grab();
	rig.MoveTo({0.0f, grab.y});
	EXPECT_NEAR(rig.FirstW(), 300.0f, 1.0f);

	rig.MoveTo({400.0f, grab.y});
	EXPECT_NEAR(rig.FirstW(), 300.0f, 1.0f);
}

TEST(UISplitterTest, AVerticalSplitDragsOnY) {
	UI::Widgets::SplitDesc desc = Ratio(0.5f);
	desc.axis = UI::FlexDirection::Column;
	SplitRig rig{desc};
	f32 before = rig.FirstH();

	Vec2 grab = rig.Grab();
	rig.MoveTo({grab.x + 90.0f, grab.y - 40.0f});

	EXPECT_NEAR(rig.FirstH(), before - 40.0f, 1.0f);
}

TEST(UISplitterTest, DragTracksCursorUnderContentScale) {
	UI::Widgets::SplitDesc desc;
	desc.sizing = UI::Widgets::SplitSizing::FirstFixed;
	desc.position = 100.0f;
	SplitRig rig{desc, {500, 300}, 1.25f};
	ASSERT_FLOAT_EQ(rig.FirstW(), 125.0f);

	Vec2 grab = rig.Grab();
	rig.MoveTo({grab.x + 50.0f, grab.y});

	EXPECT_NEAR(rig.FirstW(), 175.0f, 1.0f);
	EXPECT_NEAR(rig.reported, 140.0f, 1.0f);
}

TEST(UISplitterTest, PaneEdgeLandsOnWholePixel) {
	SplitRig rig{Ratio(0.5f), {401, 300}};
	ASSERT_FLOAT_EQ(rig.FirstW(), 198.5f);

	Vec2 grab = rig.Grab();
	rig.MoveTo({grab.x + 10.0f, grab.y});

	f32 width = rig.FirstW();
	EXPECT_FLOAT_EQ(width, std::round(width)) << "dragged to " << width;
}

TEST(UISplitterTest, PressBesideBandDoesNotResize) {
	SplitRig rig{Ratio()};
	const UI::Rect band = rig.ctx.GetLayout(rig.parts.splitter).borderBox;
	f32 startX = band.x;
	Vec2 beside = {band.x + band.w + 3.0f, band.y + band.h * 0.5f};

	rig.ctx.InjectInput(Mouse(InputEventType::MouseMove, beside));
	rig.ctx.InjectInput(Mouse(InputEventType::MouseDown, beside));
	rig.ctx.Update();
	rig.MoveTo({beside.x - 80.0f, beside.y});

	EXPECT_FLOAT_EQ(rig.ctx.GetLayout(rig.parts.splitter).borderBox.x, startX);
	EXPECT_EQ(rig.reports, 0);
}

TEST(UISplitterTest, PositionReportedInUnits) {
	SplitRig rig{Ratio(0.5f)};
	Vec2 grab = rig.Grab();
	EXPECT_EQ(rig.reports, 0);

	rig.MoveTo({grab.x - 100.0f, grab.y});
	ASSERT_GT(rig.reports, 0);
	EXPECT_GT(rig.reported, 0.0f);
	EXPECT_LT(rig.reported, 0.5f);

	f32 settled = rig.BandX();
	rig.Release({grab.x - 100.0f, grab.y});
	EXPECT_FLOAT_EQ(rig.BandX(), settled);

	int before = rig.reports;
	rig.MoveTo({grab.x + 120.0f, grab.y});
	EXPECT_EQ(rig.reports, before);
}

TEST(UISplitterTest, HidingSecondPaneAndBandCollapsesSplit) {
	SplitRig rig{Ratio(0.5f)};
	ASSERT_LT(rig.FirstW(), 300.0f);

	rig.ctx.SetVisible(rig.parts.second, false);
	rig.ctx.SetVisible(rig.parts.splitter, false);
	rig.ctx.Update();

	EXPECT_FLOAT_EQ(rig.FirstW(), 400.0f);
}
