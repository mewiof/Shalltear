#include "TestsPCH.h"

#include "UITestUtil.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	UI::ContextDesc Desc() {
		UI::ContextDesc desc;
		desc.sizePx = {200, 200};
		desc.contentScale = 1.0f;
		return desc;
	}

	UI::Style Placed(f32 left, f32 top, f32 width, f32 height, f32 rotate) {
		UI::Style style;
		style.position = UI::Position::Absolute;
		style.left = UI::Length::Pixel(left);
		style.top = UI::Length::Pixel(top);
		style.width = UI::Length::Pixel(width);
		style.height = UI::Length::Pixel(height);
		style.background = UI::Brush::SolidColor(Color(1.0f, 0.0f, 0.0f, 1.0f));
		style.rotate = rotate;
		return style;
	}

	f32 Rotate(std::string_view value, f32 start = 0.0f) {
		UI::Style style;
		style.rotate = start;
		UI::ApplyStyleDeclaration(style, StringHash("rotate"), value, nullptr);
		return style.rotate;
	}

	UI::TransformOrigin Origin(std::string_view value) {
		UI::Style style;
		UI::ApplyStyleDeclaration(style, StringHash("transform-origin"), value, nullptr);
		return style.transformOrigin;
	}

	UI::TransformOrigin OriginOf(UI::Length x, UI::Length y) {
		return UI::TransformOrigin{x, y};
	}

	struct Composite {
		const UI::UICommand* push = nullptr;
		const UI::UIQuad* quad = nullptr;
		u32 layers = 0;
	};

	Composite FindComposite(const UI::RenderList& list) {
		Composite found;
		for (const UI::UICommand& command : list.commands) {
			if (command.kind == UI::UICommandKind::PushLayer) {
				++found.layers;
				if (!found.push) {
					found.push = &command;
				}
			}
			if (command.kind == UI::UICommandKind::Quads && command.texture == UI::LayerCompositeTexture && !found.quad) {
				found.quad = &list.quads[command.firstQuad];
			}
		}
		return found;
	}

	Composite Paint(UI::Context& ctx, UI::RenderList& list) {
		ctx.Update();
		ctx.BuildRenderList(list);
		return FindComposite(list);
	}

	InputEvent Mouse(InputEventType type, f32 x, f32 y) {
		InputEvent event{};
		event.type = type;
		event.button = MouseButton::Left;
		event.mousePosPx = {static_cast<i32>(x), static_cast<i32>(y)};
		return event;
	}

	void ClickAt(UI::Context& ctx, f32 x, f32 y) {
		ctx.InjectInput(Mouse(InputEventType::MouseMove, x, y));
		ctx.InjectInput(Mouse(InputEventType::MouseDown, x, y));
		ctx.InjectInput(Mouse(InputEventType::MouseUp, x, y));
		ctx.Update();
	}

} // anonymous namespace

TEST(UIRotateTest, RotateParsesAngleUnits) {
	EXPECT_FLOAT_EQ(Rotate("45deg"), 45.0f);
	EXPECT_FLOAT_EQ(Rotate("-100grad"), -90.0f);
	EXPECT_NEAR(Rotate("3.14159265rad"), 180.0f, 0.001f);
	EXPECT_FLOAT_EQ(Rotate("0.75turn"), 270.0f);
	EXPECT_FLOAT_EQ(Rotate("30"), 30.0f);
	EXPECT_FLOAT_EQ(Rotate("none", 12.0f), 0.0f);
	EXPECT_FLOAT_EQ(Rotate("45px", 12.0f), 12.0f);
	EXPECT_FLOAT_EQ(Rotate("deg", 12.0f), 12.0f);

	UI::Style style;
	style.rotate = 270.0f;
	char buffer[UI::MAX_STYLE_VALUE_TEXT];
	EXPECT_EQ(UI::ReadStyleProperty(style, StringHash("rotate"), buffer, sizeof(buffer)), "270deg");
}

TEST(UIRotateTest, OriginParsesKeywordsAndLengths) {
	using L = UI::Length;
	EXPECT_EQ(UI::Style{}.transformOrigin, OriginOf(L::Percent(0.5f), L::Percent(0.5f)));
	EXPECT_EQ(Origin("left"), OriginOf(L::Percent(0.0f), L::Percent(0.5f)));
	EXPECT_EQ(Origin("bottom"), OriginOf(L::Percent(0.5f), L::Percent(1.0f)));
	EXPECT_EQ(Origin("top left"), OriginOf(L::Percent(0.0f), L::Percent(0.0f)));
	EXPECT_EQ(Origin("bottom right"), OriginOf(L::Percent(1.0f), L::Percent(1.0f)));
	EXPECT_EQ(Origin("top center"), OriginOf(L::Percent(0.5f), L::Percent(0.0f)));
	EXPECT_EQ(Origin("20px 75%"), OriginOf(L::Pixel(20.0f), L::Percent(0.75f)));
	EXPECT_EQ(Origin("10"), OriginOf(L::Pixel(10.0f), L::Percent(0.5f)));
	EXPECT_EQ(Origin("20px top"), OriginOf(L::Pixel(20.0f), L::Percent(0.0f)));
	EXPECT_EQ(Origin("top 20px"), UI::TransformOrigin{});
	EXPECT_EQ(Origin("left right"), UI::TransformOrigin{});
	EXPECT_EQ(Origin("sideways"), UI::TransformOrigin{});

	UI::Style style;
	style.transformOrigin = OriginOf(L::Pixel(20.0f), L::Percent(0.75f));
	char buffer[UI::MAX_STYLE_VALUE_TEXT];
	EXPECT_EQ(UI::ReadStyleProperty(style, StringHash("transform-origin"), buffer, sizeof(buffer)), "20px 75%");
}

TEST(UIRotateTest, TurnedElementCompositesOneLayer) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style style = Placed(40.0f, 60.0f, 80.0f, 20.0f, 90.0f);
	style.opacity = 0.5f;
	MakeChild(ctx, ctx.GetRoot(), style);

	UI::RenderList list;
	const Composite composite = Paint(ctx, list);
	ASSERT_EQ(composite.layers, 1u);
	ASSERT_NE(composite.quad, nullptr);
	EXPECT_EQ(composite.push->layerBounds, (UI::Rect{38.0f, 58.0f, 84.0f, 24.0f}));
	EXPECT_EQ(composite.quad->rect, (UI::Rect{38.0f, 58.0f, 84.0f, 24.0f}));
	EXPECT_NE(composite.quad->flags & UI::QUAD_FLAG_ROTATED, 0u);
	EXPECT_NEAR(composite.quad->rotation, 1.5707964f, 1e-6f);
	EXPECT_FLOAT_EQ(composite.quad->color0.a, 0.5f);

	bool unturnedBox = false;
	for (const UI::UIQuad& quad : list.quads) {
		if (quad.rect == UI::Rect{40.0f, 60.0f, 80.0f, 20.0f}) {
			unturnedBox = (quad.flags & UI::QUAD_FLAG_ROTATED) == 0 && quad.color0.a == 1.0f;
		}
	}
	EXPECT_TRUE(unturnedBox);
}

TEST(UIRotateTest, OriginMovesComposite) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style style = Placed(40.0f, 60.0f, 80.0f, 20.0f, 90.0f);
	style.transformOrigin = OriginOf(UI::Length::Percent(0.0f), UI::Length::Percent(0.0f));
	MakeChild(ctx, ctx.GetRoot(), style);

	UI::RenderList list;
	const Composite composite = Paint(ctx, list);
	ASSERT_NE(composite.quad, nullptr);
	EXPECT_EQ(composite.quad->rect, (UI::Rect{-12.0f, 88.0f, 84.0f, 24.0f}));
}

TEST(UIRotateTest, RightAngleLandsOnWholePixels) {
	struct Case {
		f32 height;
		f32 rotate;
		f32 originPx;
	};
	for (const Case& turn : {Case{21.0f, 90.0f, -1.0f}, Case{20.0f, 180.0f, 10.25f}, Case{21.0f, 270.0f, 10.25f}}) {
		UI::Context ctx(Desc(), nullptr);
		UI::Style style = Placed(40.0f, 60.0f, 80.0f, turn.height, turn.rotate);
		if (turn.originPx >= 0.0f) {
			style.transformOrigin.x = UI::Length::Pixel(turn.originPx);
		}
		MakeChild(ctx, ctx.GetRoot(), style);

		UI::RenderList list;
		const Composite composite = Paint(ctx, list);
		ASSERT_NE(composite.quad, nullptr);
		const UI::Rect& rect = composite.quad->rect;
		const bool sideways = turn.rotate != 180.0f;
		const f32 left = rect.x + rect.w * 0.5f - (sideways ? rect.h : rect.w) * 0.5f;
		const f32 top = rect.y + rect.h * 0.5f - (sideways ? rect.w : rect.h) * 0.5f;
		EXPECT_EQ(left, std::round(left)) << turn.rotate;
		EXPECT_EQ(top, std::round(top)) << turn.rotate;
	}
}

TEST(UIRotateTest, TurnedInkSurvivesSecondFrame) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style frame = Placed(0.0f, 0.0f, 200.0f, 100.0f, 0.0f);
	frame.overflow = UI::Overflow::Hidden;
	UI::ElementHandle clipper = MakeChild(ctx, ctx.GetRoot(), frame);
	MakeChild(ctx, clipper, Placed(40.0f, 110.0f, 120.0f, 20.0f, 90.0f));

	for (int pass = 0; pass < 2; ++pass) {
		UI::RenderList list;
		EXPECT_NE(Paint(ctx, list).quad, nullptr) << pass;
	}
}

TEST(UIRotateTest, ZeroAndFullTurnsDrawNoLayer) {
	for (f32 rotate : {0.0f, 360.0f, -720.0f}) {
		UI::Context ctx(Desc(), nullptr);
		MakeChild(ctx, ctx.GetRoot(), Placed(40.0f, 60.0f, 80.0f, 20.0f, rotate));
		UI::RenderList list;
		EXPECT_EQ(Paint(ctx, list).layers, 0u) << rotate;
	}
}

TEST(UIRotateTest, LayerHoldsContentPastScreenEdge) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style style = Placed(150.0f, 20.0f, 120.0f, 20.0f, 90.0f);
	style.transformOrigin = OriginOf(UI::Length::Percent(0.0f), UI::Length::Percent(0.0f));
	MakeChild(ctx, ctx.GetRoot(), style);

	UI::RenderList list;
	const Composite composite = Paint(ctx, list);
	ASSERT_NE(composite.push, nullptr);
	EXPECT_EQ(composite.push->layerBounds, (UI::Rect{148.0f, 18.0f, 124.0f, 24.0f}));
}

TEST(UIRotateTest, ClickHitsTurnedFootprint) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle box = MakeChild(ctx, ctx.GetRoot(), Placed(40.0f, 90.0f, 120.0f, 20.0f, 90.0f));
	int clicks = 0;
	ctx.Events(box).onClick = [&] { ++clicks; };
	ctx.Update();

	ClickAt(ctx, 100.0f, 50.0f);
	EXPECT_EQ(clicks, 1);
	ClickAt(ctx, 50.0f, 100.0f);
	EXPECT_EQ(clicks, 1);
	EXPECT_EQ(ctx.HitTest({100.0f, 150.0f}), box);
	EXPECT_NE(ctx.HitTest({150.0f, 100.0f}), box);
}

TEST(UIRotateTest, ClipStopsTurnedHits) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style frame = Placed(0.0f, 0.0f, 200.0f, 100.0f, 0.0f);
	frame.overflow = UI::Overflow::Hidden;
	UI::ElementHandle clipper = MakeChild(ctx, ctx.GetRoot(), frame);
	UI::ElementHandle box = MakeChild(ctx, clipper, Placed(40.0f, 90.0f, 120.0f, 20.0f, 90.0f));
	ctx.Update();

	EXPECT_EQ(ctx.HitTest({100.0f, 60.0f}), box);
	EXPECT_NE(ctx.HitTest({100.0f, 140.0f}), box);
}

TEST(UIRotateTest, PointerPositionUndoesNestedTurns) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle outer = MakeChild(ctx, ctx.GetRoot(), Placed(50.0f, 50.0f, 100.0f, 100.0f, 90.0f));
	UI::ElementHandle inner = MakeChild(ctx, outer, Placed(10.0f, 20.0f, 40.0f, 20.0f, 90.0f));
	ctx.Update();
	ASSERT_EQ(ctx.GetLayout(inner).borderBox, (UI::Rect{60.0f, 70.0f, 40.0f, 20.0f}));

	ctx.InjectInput(Mouse(InputEventType::MouseMove, 130.0f, 85.0f));
	ctx.Update();
	EXPECT_EQ(ctx.GetPointerPosition(), (Vec2{130.0f, 85.0f}));
	EXPECT_EQ(ctx.GetPointerPosition(outer), (Vec2{85.0f, 70.0f}));
	EXPECT_EQ(ctx.GetPointerPosition(inner), (Vec2{70.0f, 75.0f}));
	EXPECT_EQ(ctx.GetHovered(), inner);

	EXPECT_EQ(ctx.HitTest({120.0f, 80.0f}), inner);
	EXPECT_EQ(ctx.HitTest({70.0f, 120.0f}), outer);
}

TEST(UIRotateTest, DropArrivesInLayoutSpace) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle box = MakeChild(ctx, ctx.GetRoot(), Placed(40.0f, 90.0f, 120.0f, 20.0f, 90.0f));
	Vec2 dropped{-1.0f, -1.0f};
	ctx.Events(box).onDragOver = [](const UI::DragPayload&) { return true; };
	ctx.Events(box).onDrop = [&](const UI::DragPayload&, Vec2 point) { dropped = point; };
	ctx.Update();

	UI::DragPayload payload;
	payload.type = StringHash("probe");
	ASSERT_TRUE(ctx.DeliverDrop(payload, {105.0f, 60.0f}));
	EXPECT_EQ(dropped, (Vec2{60.0f, 95.0f}));
}
