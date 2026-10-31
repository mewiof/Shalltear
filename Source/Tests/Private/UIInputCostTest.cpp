#include "TestsPCH.h"

#include "UITestUtil.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	UI::ContextDesc Desc() {
		UI::ContextDesc desc;
		desc.sizePx = {1600, 200};
		desc.contentScale = 1.0f;
		return desc;
	}

	InputEvent MouseMove(i32 x, i32 y) {
		InputEvent event{};
		event.type = InputEventType::MouseMove;
		event.mousePosPx = {x, y};
		return event;
	}
	InputEvent MouseDown(i32 x, i32 y) {
		InputEvent event{};
		event.type = InputEventType::MouseDown;
		event.button = MouseButton::Left;
		event.mousePosPx = {x, y};
		return event;
	}
	InputEvent MouseUp(i32 x, i32 y) {
		InputEvent event{};
		event.type = InputEventType::MouseUp;
		event.button = MouseButton::Left;
		event.mousePosPx = {x, y};
		return event;
	}

	struct SweepRow {
		static constexpr f32 BOX = 20.0f;

		UI::Context ctx{Desc(), nullptr};
		Vector<UI::ElementHandle> boxes{GetMemoryResource(MemTag::Debug)};
		Vector<u32> enters{GetMemoryResource(MemTag::Debug)};
		Vector<i32> centers{GetMemoryResource(MemTag::Debug)};
		u32 totalEnters = 0;
		u32 drags = 0;
		Vec2 lastDragAt;
		i32 centerY = 0;

		explicit SweepRow(u32 count) {
			UI::Style rootStyle;
			rootStyle.alignItems = UI::Align::Start;
			ctx.SetStyle(ctx.GetRoot(), rootStyle);
			enters.resize(count, 0);
			for (u32 i = 0; i < count; ++i) {
				UI::ElementHandle box = MakeChild(ctx, ctx.GetRoot(), FixedBox(BOX, BOX));
				ctx.Events(box).onHoverEnter = [this, i]() {
					++enters[i];
					++totalEnters;
				};
				ctx.Events(box).onPressDrag = [this](Vec2 at) {
					++drags;
					lastDragAt = at;
				};
				boxes.push_back(box);
			}
			ctx.Update();
			for (UI::ElementHandle box : boxes) {
				const UI::Rect rect = ctx.GetLayout(box).borderBox;
				centers.push_back(static_cast<i32>(rect.x + rect.w * 0.5f));
				centerY = static_cast<i32>(rect.y + rect.h * 0.5f);
			}
		}

		i32 CenterX(u32 index) const { return centers[index]; }

		void SweepFrame(u32 moves) {
			for (u32 i = 0; i < moves; ++i) {
				ctx.InjectInput(MouseMove(CenterX(i), centerY));
			}
			ctx.Update();
		}
	};

} // anonymous namespace

TEST(UIInputCostTest, FrameOfMovesFeedsDragOnce) {
	SweepRow row(32);
	row.ctx.InjectInput(MouseDown(row.CenterX(0), row.centerY));
	row.ctx.Update();
	ASSERT_EQ(row.drags, 0u);

	for (u32 i = 1; i <= 16; ++i) {
		row.ctx.InjectInput(MouseMove(row.CenterX(i), row.centerY));
	}
	row.ctx.Update();

	EXPECT_EQ(row.drags, 1u);
	EXPECT_FLOAT_EQ(row.lastDragAt.x, static_cast<f32>(row.CenterX(16)));
}

TEST(UIInputCostTest, AFrameOfMovesResolvesHoverOnce) {
	SweepRow row(32);
	row.SweepFrame(16);

	EXPECT_EQ(row.totalEnters, 1u);
	EXPECT_EQ(row.enters[15], 1u);
	EXPECT_EQ(row.ctx.GetHovered(), row.boxes[15]);
}

TEST(UIInputCostTest, CostIgnoresMoveCount) {
	u32 dragsAt[3] = {};
	u32 entersAt[3] = {};
	const u32 samples[3] = {1, 16, 64};

	for (u32 column = 0; column < 3; ++column) {
		SweepRow row(80);
		row.SweepFrame(samples[column]);
		entersAt[column] = row.totalEnters;

		row.ctx.InjectInput(MouseDown(row.CenterX(0), row.centerY));
		row.ctx.Update();
		for (u32 i = 1; i <= samples[column]; ++i) {
			row.ctx.InjectInput(MouseMove(row.CenterX(i), row.centerY));
		}
		row.ctx.Update();
		dragsAt[column] = row.drags;
	}

	EXPECT_EQ(dragsAt[0], dragsAt[1]);
	EXPECT_EQ(dragsAt[1], dragsAt[2]);
	EXPECT_EQ(entersAt[0], entersAt[1]);
	EXPECT_EQ(entersAt[1], entersAt[2]);
}

TEST(UIInputCostTest, PressEndsRunItArrivesIn) {
	SweepRow row(32);
	u32 downs = 0;
	u32 clicks = 0;
	row.ctx.Events(row.boxes[2]).onMouseDown = [&downs]() { ++downs; };
	row.ctx.Events(row.boxes[2]).onClick = [&clicks]() { ++clicks; };

	row.ctx.InjectInput(MouseMove(row.CenterX(1), row.centerY));
	row.ctx.InjectInput(MouseMove(row.CenterX(2), row.centerY));
	row.ctx.InjectInput(MouseDown(row.CenterX(2), row.centerY));
	row.ctx.InjectInput(MouseMove(row.CenterX(5), row.centerY));
	row.ctx.InjectInput(MouseMove(row.CenterX(9), row.centerY));
	row.ctx.Update();

	EXPECT_EQ(downs, 1u);
	EXPECT_EQ(row.drags, 1u);
	EXPECT_FLOAT_EQ(row.lastDragAt.x, static_cast<f32>(row.CenterX(9)));

	row.ctx.InjectInput(MouseUp(row.CenterX(9), row.centerY));
	row.ctx.Update();
	EXPECT_EQ(clicks, 0u);
}

TEST(UIInputCostTest, ClickInsideMoveRunIsStillClick) {
	SweepRow row(32);
	u32 clicks = 0;
	row.ctx.Events(row.boxes[3]).onClick = [&clicks]() { ++clicks; };

	row.ctx.InjectInput(MouseMove(row.CenterX(1), row.centerY));
	row.ctx.InjectInput(MouseMove(row.CenterX(3), row.centerY));
	row.ctx.InjectInput(MouseDown(row.CenterX(3), row.centerY));
	row.ctx.InjectInput(MouseUp(row.CenterX(3), row.centerY));
	row.ctx.InjectInput(MouseMove(row.CenterX(7), row.centerY));
	row.ctx.Update();

	EXPECT_EQ(clicks, 1u);
	EXPECT_EQ(row.ctx.GetHovered(), row.boxes[7]);
}

TEST(UIInputCostTest, LeaveEndsRunItArrivesIn) {
	SweepRow row(32);
	InputEvent leave{};
	leave.type = InputEventType::MouseLeave;

	row.ctx.InjectInput(MouseMove(row.CenterX(4), row.centerY));
	row.ctx.InjectInput(leave);
	row.ctx.InjectInput(MouseMove(row.CenterX(4), row.centerY));
	row.ctx.Update();

	EXPECT_EQ(row.enters[4], 2u);
	EXPECT_EQ(row.ctx.GetHovered(), row.boxes[4]);
}

TEST(UIInputCostTest, StalledFrameDoesNotGrowQueue) {
	constexpr u32 SAMPLES = 1000;
	constexpr u32 SETTLED_ON = 5;
	SweepRow row(32);
	u32 downs = 0;
	row.ctx.Events(row.boxes[SETTLED_ON]).onMouseDown = [&downs]() { ++downs; };

	for (u32 i = 0; i < SAMPLES - 1; ++i) {
		row.ctx.InjectInput(MouseMove(row.CenterX(i % 32), row.centerY));
	}
	row.ctx.InjectInput(MouseMove(row.CenterX(SETTLED_ON), row.centerY));
	row.ctx.InjectInput(MouseDown(row.CenterX(SETTLED_ON), row.centerY));
	row.ctx.Update();

	EXPECT_EQ(row.totalEnters, 1u) << row.totalEnters << " times";
	EXPECT_EQ(row.enters[SETTLED_ON], 1u);
	EXPECT_EQ(downs, 1u);
}
