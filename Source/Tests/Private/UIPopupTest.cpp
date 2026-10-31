#include "TestsPCH.h"

#include "UITestUtil.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	UI::ContextDesc Desc() {
		UI::ContextDesc desc;
		desc.sizePx = {400, 300};
		desc.contentScale = 1.0f;
		return desc;
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
	InputEvent KeyPress(Key key) {
		InputEvent event{};
		event.type = InputEventType::KeyDown;
		event.key = key;
		return event;
	}

	void Click(UI::Context& ctx, i32 x, i32 y) {
		ctx.InjectInput(MouseDown(x, y));
		ctx.InjectInput(MouseUp(x, y));
		ctx.Update();
	}

	UI::ElementHandle MakeContent(UI::Context& ctx, f32 width, f32 height) {
		UI::ElementHandle content = ctx.CreateElement();
		ctx.SetStyle(content, FixedBox(width, height));
		return content;
	}

} // anonymous namespace

TEST(UIPopupTest, OpensAtAnchorAndTopsHitTesting) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle background = MakeChild(ctx, ctx.GetRoot(), FixedBox(400.0f, 300.0f));
	UI::ElementHandle popup = MakeContent(ctx, 100.0f, 50.0f);

	ctx.OpenPopup(popup, {50.0f, 50.0f});
	ctx.Update();

	EXPECT_EQ(ctx.GetOpenPopupCount(), 1u);
	EXPECT_TRUE(ctx.IsPopupOpen(popup));

	const UI::Rect& box = ctx.GetLayout(popup).borderBox;
	EXPECT_FLOAT_EQ(box.x, 50.0f);
	EXPECT_FLOAT_EQ(box.y, 50.0f);
	EXPECT_FLOAT_EQ(box.w, 100.0f);
	EXPECT_FLOAT_EQ(box.h, 50.0f);

	EXPECT_EQ(ctx.HitTest({60.0f, 60.0f}), popup);
	EXPECT_EQ(ctx.HitTest({300.0f, 200.0f}), background);
}

TEST(UIPopupTest, OutsideClickClosesAndEatsThePress) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle background = MakeChild(ctx, ctx.GetRoot(), FixedBox(400.0f, 300.0f));
	UI::ElementHandle popup = MakeContent(ctx, 100.0f, 50.0f);

	int backgroundClicks = 0;
	int closed = 0;
	ctx.Events(background).onClick = [&] { ++backgroundClicks; };
	ctx.Events(popup).onPopupClosed = [&] { ++closed; };

	ctx.OpenPopup(popup, {50.0f, 50.0f});
	ctx.Update();

	Click(ctx, 300, 200);
	EXPECT_EQ(ctx.GetOpenPopupCount(), 0u);
	EXPECT_FALSE(ctx.IsPopupOpen(popup));
	EXPECT_EQ(closed, 1);
	EXPECT_EQ(backgroundClicks, 0);

	EXPECT_TRUE(ctx.IsAlive(popup));
	ctx.OpenPopup(popup, {10.0f, 10.0f});
	ctx.Update();
	EXPECT_TRUE(ctx.IsPopupOpen(popup));
}

TEST(UIPopupTest, InsideClickDeliversAndStaysOpen) {
	UI::Context ctx(Desc(), nullptr);
	MakeChild(ctx, ctx.GetRoot(), FixedBox(400.0f, 300.0f));
	UI::ElementHandle popup = MakeContent(ctx, 100.0f, 50.0f);

	int popupClicks = 0;
	ctx.Events(popup).onClick = [&] { ++popupClicks; };

	ctx.OpenPopup(popup, {50.0f, 50.0f});
	ctx.Update();

	Click(ctx, 60, 60);
	EXPECT_TRUE(ctx.IsPopupOpen(popup));
	EXPECT_EQ(popupClicks, 1);
}

TEST(UIPopupTest, ClosingPressLeavesNoPress) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle background = MakeChild(ctx, ctx.GetRoot(), FixedBox(400.0f, 300.0f));
	UI::ElementHandle popup = MakeContent(ctx, 100.0f, 50.0f);
	UI::Context* context = &ctx;
	int ended = 0;
	ctx.Events(popup).onPressEnd = [context, popup, &ended] {
		++ended;
		context->ClosePopup(popup);
	};
	int backgroundPresses = 0;
	ctx.Events(background).onMouseDown = [&] { ++backgroundPresses; };

	ctx.OpenPopup(popup, {50.0f, 50.0f});
	ctx.Update();
	Click(ctx, 60, 60);
	EXPECT_EQ(ended, 1);
	EXPECT_FALSE(ctx.IsPopupOpen(popup));
	EXPECT_TRUE(ctx.IsAlive(popup));

	ctx.InjectInput(MouseDown(300, 200));
	ctx.Update();
	EXPECT_EQ(backgroundPresses, 1);
	ctx.InjectInput(MouseUp(300, 200));
	ctx.Update();
}

TEST(UIPopupTest, StackFoldsAboveTheClickedPopup) {
	UI::Context ctx(Desc(), nullptr);
	MakeChild(ctx, ctx.GetRoot(), FixedBox(400.0f, 300.0f));
	UI::ElementHandle menu = MakeContent(ctx, 100.0f, 100.0f);
	UI::ElementHandle submenu = MakeContent(ctx, 100.0f, 100.0f);

	ctx.OpenPopup(menu, {10.0f, 10.0f});
	ctx.OpenPopup(submenu, {150.0f, 10.0f});
	ctx.Update();
	EXPECT_EQ(ctx.GetOpenPopupCount(), 2u);

	Click(ctx, 50, 50);
	EXPECT_TRUE(ctx.IsPopupOpen(menu));
	EXPECT_FALSE(ctx.IsPopupOpen(submenu));

	Click(ctx, 300, 250);
	EXPECT_EQ(ctx.GetOpenPopupCount(), 0u);
}

TEST(UIPopupTest, EscapeClosesTheTopmost) {
	UI::Context ctx(Desc(), nullptr);
	MakeChild(ctx, ctx.GetRoot(), FixedBox(400.0f, 300.0f));
	UI::ElementHandle menu = MakeContent(ctx, 100.0f, 100.0f);
	UI::ElementHandle submenu = MakeContent(ctx, 100.0f, 100.0f);

	ctx.OpenPopup(menu, {10.0f, 10.0f});
	ctx.OpenPopup(submenu, {150.0f, 10.0f});
	ctx.Update();

	ctx.InjectInput(KeyPress(Key::Escape));
	ctx.Update();
	EXPECT_TRUE(ctx.IsPopupOpen(menu));
	EXPECT_FALSE(ctx.IsPopupOpen(submenu));

	ctx.InjectInput(KeyPress(Key::Escape));
	ctx.Update();
	EXPECT_EQ(ctx.GetOpenPopupCount(), 0u);
}

namespace {

	UI::ElementHandle MakeScroller(UI::Context& ctx) {
		UI::Style style = FixedBox(400.0f, 300.0f);
		style.overflow = UI::Overflow::Scroll;
		UI::ElementHandle scroller = MakeChild(ctx, ctx.GetRoot(), style);
		MakeChild(ctx, scroller, FixedBox(400.0f, 1000.0f));
		return scroller;
	}

	void WheelAt(UI::Context& ctx, i32 x, i32 y) {
		InputEvent move{};
		move.type = InputEventType::MouseMove;
		move.mousePosPx = {x, y};
		ctx.InjectInput(move);
		InputEvent wheel{};
		wheel.type = InputEventType::MouseWheel;
		wheel.wheel = {0.0f, -1.0f};
		wheel.mousePosPx = {x, y};
		ctx.InjectInput(wheel);
		ctx.Update();
	}

} // anonymous namespace

TEST(UIPopupTest, WheelOutsideClosesPopupAndIsSpent) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle scroller = MakeScroller(ctx);
	UI::ElementHandle popup = MakeContent(ctx, 100.0f, 50.0f);
	ctx.OpenPopup(popup, {50.0f, 50.0f});
	ctx.Update();

	WheelAt(ctx, 300, 200);
	EXPECT_FALSE(ctx.IsPopupOpen(popup));
	EXPECT_FLOAT_EQ(ctx.GetElement(scroller)->scrollOffset.y, 0.0f);

	WheelAt(ctx, 300, 200);
	EXPECT_GT(ctx.GetElement(scroller)->scrollOffset.y, 0.0f);
}

TEST(UIPopupTest, WheelKeepsDialogOpen) {
	UI::Context ctx(Desc(), nullptr);
	MakeScroller(ctx);
	UI::ElementHandle popup = MakeContent(ctx, 100.0f, 50.0f);
	ctx.OpenPopup(popup, {50.0f, 50.0f});
	ctx.Update();
	WheelAt(ctx, 60, 60);
	EXPECT_TRUE(ctx.IsPopupOpen(popup));
	ctx.ClosePopup(popup);

	UI::ElementHandle dialog = MakeContent(ctx, 100.0f, 50.0f);
	ctx.OpenPopup(dialog, {50.0f, 50.0f}, UI::PopupFlags::NoOutsideDismiss);
	ctx.Update();
	WheelAt(ctx, 300, 200);
	EXPECT_TRUE(ctx.IsPopupOpen(dialog));
}

TEST(UIPopupTest, NoOutsideDismissSurvivesOutsideClicks) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle background = MakeChild(ctx, ctx.GetRoot(), FixedBox(400.0f, 300.0f));
	UI::ElementHandle popup = MakeContent(ctx, 100.0f, 50.0f);

	int backgroundClicks = 0;
	ctx.Events(background).onClick = [&] { ++backgroundClicks; };

	ctx.OpenPopup(popup, {50.0f, 50.0f}, UI::PopupFlags::NoOutsideDismiss);
	ctx.Update();

	Click(ctx, 300, 200);
	EXPECT_TRUE(ctx.IsPopupOpen(popup));
	EXPECT_EQ(backgroundClicks, 1);

	ctx.InjectInput(KeyPress(Key::Escape));
	ctx.Update();
	EXPECT_FALSE(ctx.IsPopupOpen(popup));
}

TEST(UIPopupTest, ClampsIntoTheContextRect) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle popup = MakeContent(ctx, 100.0f, 50.0f);

	ctx.OpenPopup(popup, {380.0f, 280.0f});
	ctx.Update();

	const UI::Rect& box = ctx.GetLayout(popup).borderBox;
	EXPECT_FLOAT_EQ(box.x, 300.0f);
	EXPECT_FLOAT_EQ(box.y, 250.0f);
}

TEST(UIPopupTest, PopupFlipsLeft) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle cramped = MakeContent(ctx, 100.0f, 50.0f);
	ctx.OpenPopup(cramped, {370.0f, 40.0f}, UI::PopupFlags::None, {250.0f, 40.0f, 120.0f, 20.0f});
	UI::ElementHandle roomy = MakeContent(ctx, 100.0f, 50.0f);
	ctx.OpenPopup(roomy, {170.0f, 100.0f}, UI::PopupFlags::None, {50.0f, 100.0f, 120.0f, 20.0f});
	UI::ElementHandle nowhere = MakeContent(ctx, 100.0f, 50.0f);
	ctx.OpenPopup(nowhere, {380.0f, 160.0f}, UI::PopupFlags::None, {20.0f, 160.0f, 360.0f, 20.0f});
	for (int frame = 0; frame < 2; ++frame) {
		ctx.Update();
		EXPECT_FLOAT_EQ(ctx.GetLayout(cramped).borderBox.x, 150.0f);
		EXPECT_FLOAT_EQ(ctx.GetLayout(cramped).borderBox.y, 40.0f);
		EXPECT_FLOAT_EQ(ctx.GetLayout(roomy).borderBox.x, 170.0f);
		EXPECT_FLOAT_EQ(ctx.GetLayout(nowhere).borderBox.x, 300.0f);
	}
}

TEST(UIPopupTest, PopupFlipsAboveOrSlides) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle below = MakeContent(ctx, 100.0f, 50.0f);
	ctx.OpenPopup(below, {50.0f, 290.0f}, UI::PopupFlags::None, {50.0f, 270.0f, 80.0f, 20.0f});
	UI::ElementHandle beside = MakeContent(ctx, 100.0f, 50.0f);
	ctx.OpenPopup(beside, {250.0f, 270.0f}, UI::PopupFlags::None, {150.0f, 270.0f, 100.0f, 20.0f});
	ctx.Update();
	EXPECT_FLOAT_EQ(ctx.GetLayout(below).borderBox.x, 50.0f);
	EXPECT_FLOAT_EQ(ctx.GetLayout(below).borderBox.y, 220.0f);
	EXPECT_FLOAT_EQ(ctx.GetLayout(beside).borderBox.x, 250.0f);
	EXPECT_FLOAT_EQ(ctx.GetLayout(beside).borderBox.y, 250.0f);
}

TEST(UIPopupTest, ShrinkingPopupIsPlacedFromAnchorAgain) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle lifted = MakeContent(ctx, 100.0f, 200.0f);
	ctx.OpenPopup(lifted, {20.0f, 250.0f});
	UI::ElementHandle flipped = MakeContent(ctx, 100.0f, 200.0f);
	ctx.OpenPopup(flipped, {200.0f, 290.0f}, UI::PopupFlags::None, {200.0f, 270.0f, 80.0f, 20.0f});
	ctx.Update();
	ASSERT_FLOAT_EQ(ctx.GetLayout(lifted).borderBox.y, 100.0f);
	ASSERT_FLOAT_EQ(ctx.GetLayout(flipped).borderBox.y, 70.0f);

	for (UI::ElementHandle popup : {lifted, flipped}) {
		UI::Style style = ctx.GetStyle(popup);
		style.height = UI::Length::Pixel(40.0f);
		ctx.SetStyle(popup, style);
	}
	ctx.Update();
	EXPECT_FLOAT_EQ(ctx.GetLayout(lifted).borderBox.y, 250.0f);
	EXPECT_FLOAT_EQ(ctx.GetLayout(flipped).borderBox.y, 230.0f);
}

TEST(UIPopupTest, CenteredPopupIgnoresAnchorAndFollowsResize) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle popup = MakeContent(ctx, 100.0f, 50.0f);

	ctx.OpenPopup(popup, {7.0f, 9.0f}, UI::PopupFlags::Center);
	ctx.Update();
	{
		const UI::Rect& box = ctx.GetLayout(popup).borderBox;
		EXPECT_FLOAT_EQ(box.x, 150.0f);
		EXPECT_FLOAT_EQ(box.y, 125.0f);
	}

	ctx.Resize({600, 500});
	ctx.Update();
	{
		const UI::Rect& box = ctx.GetLayout(popup).borderBox;
		EXPECT_FLOAT_EQ(box.x, 250.0f);
		EXPECT_FLOAT_EQ(box.y, 225.0f);
	}

	UI::ElementHandle huge = MakeContent(ctx, 800.0f, 40.0f);
	ctx.OpenPopup(huge, {0.0f, 0.0f}, UI::PopupFlags::Center);
	ctx.Update();
	EXPECT_FLOAT_EQ(ctx.GetLayout(huge).borderBox.x, 0.0f);
}

TEST(UIPopupTest, ScrimBlocksUntilClose) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle background = MakeChild(ctx, ctx.GetRoot(), FixedBox(400.0f, 300.0f));
	UI::ElementHandle popup = MakeContent(ctx, 100.0f, 50.0f);

	int backgroundClicks = 0;
	ctx.Events(background).onClick = [&] { ++backgroundClicks; };

	ctx.OpenPopup(popup, {50.0f, 50.0f}, UI::PopupFlags::NoOutsideDismiss);
	ctx.Update();
	Click(ctx, 300, 200);
	ASSERT_EQ(backgroundClicks, 1);
	ctx.CloseAllPopups();
	ctx.Update();

	ctx.OpenPopup(popup, {50.0f, 50.0f}, UI::PopupFlags::NoOutsideDismiss | UI::PopupFlags::Scrim);
	ctx.Update();
	Click(ctx, 300, 200);
	EXPECT_TRUE(ctx.IsPopupOpen(popup));
	EXPECT_EQ(backgroundClicks, 1);
	EXPECT_TRUE(ctx.WantsMouse());

	ctx.CloseAllPopups();
	ctx.Update();
	Click(ctx, 300, 200);
	EXPECT_EQ(backgroundClicks, 2);
}

TEST(UIPopupTest, ScrimSurvivesRebuildThatReAppendsTree) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle background = MakeChild(ctx, ctx.GetRoot(), FixedBox(400.0f, 300.0f));
	UI::ElementHandle popup = MakeContent(ctx, 100.0f, 50.0f);

	int backgroundClicks = 0;
	ctx.Events(background).onClick = [&] { ++backgroundClicks; };

	ctx.OpenPopup(popup, {50.0f, 50.0f}, UI::PopupFlags::Scrim | UI::PopupFlags::NoOutsideDismiss);
	ctx.Update();

	ctx.AppendChild(ctx.GetRoot(), background);
	ctx.Update();

	Click(ctx, 300, 200);
	EXPECT_EQ(backgroundClicks, 0);
}

TEST(UIPopupTest, ScrimSurvivesTheRootBeingCleared) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle popup = MakeContent(ctx, 100.0f, 50.0f);
	ctx.OpenPopup(popup, {0.0f, 0.0f}, UI::PopupFlags::Scrim | UI::PopupFlags::NoOutsideDismiss);
	ctx.Update();

	ctx.ClearChildren(ctx.GetRoot());
	ctx.Update();
	EXPECT_EQ(ctx.GetOpenPopupCount(), 0u);

	UI::ElementHandle background = MakeChild(ctx, ctx.GetRoot(), FixedBox(400.0f, 300.0f));
	int backgroundClicks = 0;
	ctx.Events(background).onClick = [&] { ++backgroundClicks; };
	UI::ElementHandle second = MakeContent(ctx, 100.0f, 50.0f);
	ctx.OpenPopup(second, {0.0f, 0.0f}, UI::PopupFlags::Scrim | UI::PopupFlags::NoOutsideDismiss);
	ctx.Update();

	Click(ctx, 300, 200);
	EXPECT_EQ(backgroundClicks, 0);
}

TEST(UIPopupTest, ExplicitCloseDetachesAndNotifies) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle popup = MakeContent(ctx, 100.0f, 50.0f);

	int closed = 0;
	ctx.Events(popup).onPopupClosed = [&] { ++closed; };

	ctx.OpenPopup(popup, {50.0f, 50.0f});
	ctx.Update();
	ctx.ClosePopup(popup);

	EXPECT_EQ(closed, 1);
	EXPECT_FALSE(ctx.IsPopupOpen(popup));
	EXPECT_TRUE(ctx.IsAlive(popup));
	const UI::Element* element = ctx.GetElement(popup);
	ASSERT_NE(element, nullptr);
	EXPECT_EQ(element->parent, UI::INVALID_UI_INDEX);
}

TEST(UIPopupTest, DestroyingOpenContentCleansTheStack) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle popup = MakeContent(ctx, 100.0f, 50.0f);

	int closed = 0;
	ctx.Events(popup).onPopupClosed = [&] { ++closed; };

	ctx.OpenPopup(popup, {50.0f, 50.0f});
	ctx.Update();
	ctx.DestroyElement(popup);

	EXPECT_EQ(ctx.GetOpenPopupCount(), 0u);
	EXPECT_EQ(closed, 0);
	ctx.Update();
}

TEST(UIPopupTest, FocusInsideClosedPopupClears) {
	UI::Context ctx(Desc(), nullptr);
	MakeChild(ctx, ctx.GetRoot(), FixedBox(400.0f, 300.0f));

	UI::Style popupStyle = FixedBox(200.0f, 100.0f);
	popupStyle.alignItems = UI::Align::Start;
	popupStyle.justifyContent = UI::Align::Start;
	UI::ElementHandle popup = ctx.CreateElement();
	ctx.SetStyle(popup, popupStyle);

	UI::ElementHandle field = MakeChild(ctx, popup, FixedBox(100.0f, 20.0f));
	ctx.SetText(field, Handle<FontAsset>{}, 14.0f, Color::White(), "abc");
	ctx.SetTextInput(field, true);

	ctx.OpenPopup(popup, {50.0f, 50.0f});
	ctx.Update();
	Click(ctx, 55, 55);
	ASSERT_EQ(ctx.GetFocused(), field);

	ctx.CloseAllPopups();
	EXPECT_FALSE(ctx.GetFocused().IsValid());
}

namespace {

	class TooltipRig {
	public:
		MockFontSystem fonts;
		FontAsset* font = New<FontAsset>(MemTag::Asset);
		UI::Context ctx;

		explicit TooltipRig(f32 delaySeconds = 0.0f)
			: ctx(MakeDesc(Handle<FontAsset>(font), delaySeconds), &fonts) {}
		~TooltipRig() { Delete(font); }

		UI::ElementHandle Card() {
			const UI::Element* root = ctx.GetElement(ctx.GetRoot());
			for (u32 child = root->firstChild; child != UI::INVALID_UI_INDEX;
				 child = ctx.GetElement(ctx.MakeHandle(child))->nextSibling) {
				UI::ElementHandle handle = ctx.MakeHandle(child);
				if (ctx.HasClass(handle, StringHash("slt-tooltip"))) {
					return handle;
				}
			}
			return {};
		}
		bool Showing() {
			UI::ElementHandle card = Card();
			return card.IsValid() && HasFlag(ctx.GetElement(card)->flags, UI::ElementFlags::Visible);
		}
		str Words() {
			UI::ElementHandle card = Card();
			return card.IsValid() ? ctx.GetTextString(card) : str{};
		}
		void Hover(i32 x, i32 y) {
			InputEvent move{};
			move.type = InputEventType::MouseMove;
			move.mousePosPx = {x, y};
			ctx.InjectInput(move);
			ctx.Update();
		}
	private:
		static UI::ContextDesc MakeDesc(Handle<FontAsset> font, f32 delaySeconds) {
			UI::ContextDesc desc;
			desc.sizePx = {400, 300};
			desc.contentScale = 1.0f;
			desc.tooltipFont = font;
			desc.tooltipDelaySeconds = delaySeconds;
			return desc;
		}
	};

	UI::ElementHandle MakeLabelledRow(TooltipRig& rig, f32 rowWidth, std::string_view text) {
		UI::Style rowStyle = FixedBox(rowWidth, 20.0f);
		rowStyle.flexDirection = UI::FlexDirection::Row;
		UI::ElementHandle row = MakeChild(rig.ctx, rig.ctx.GetRoot(), rowStyle);

		UI::ElementHandle label = rig.ctx.CreateElement();
		UI::Style labelStyle;
		labelStyle.textOverflow = UI::TextOverflow::Ellipsis;
		rig.ctx.SetStyle(label, labelStyle);
		rig.ctx.SetHoverable(label, false);
		rig.ctx.AppendChild(row, label);
		rig.ctx.SetText(label, Handle<FontAsset>{}, 14.0f, Color::White(), text);
		return row;
	}

} // anonymous namespace

TEST(UITooltipTest, ExplicitTooltipAppearsUnderPointerAfterDwell) {
	TooltipRig rig;
	UI::ElementHandle target = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(100.0f, 40.0f));
	rig.ctx.SetTooltip(target, "Reset the transform to identity");
	rig.ctx.Update();
	EXPECT_FALSE(rig.Showing());

	rig.Hover(50, 20);
	ASSERT_TRUE(rig.Showing());
	EXPECT_EQ(rig.Words(), "Reset the transform to identity");

	const UI::Rect& box = rig.ctx.GetLayout(rig.Card()).borderBox;
	EXPECT_FLOAT_EQ(box.x, 50.0f);
	EXPECT_FLOAT_EQ(box.y, 20.0f + rig.ctx.GetStyleVarFloat(StringHash("--slt-tooltip-offset"), 0.0f));
}

TEST(UITooltipTest, TruncatedLabelHasTooltip) {
	TooltipRig rig;
	MakeLabelledRow(rig, 100.0f, "New Object (Copy) (Copy)");
	rig.ctx.Update();

	rig.Hover(50, 10);
	ASSERT_TRUE(rig.Showing());
	EXPECT_EQ(rig.Words(), "New Object (Copy) (Copy)");
}

TEST(UITooltipTest, ShortLabelKeepsWords) {
	TooltipRig rig;
	MakeLabelledRow(rig, 40.0f, "Light (1)");
	rig.ctx.Update();

	rig.Hover(20, 10);
	ASSERT_TRUE(rig.Showing());
	EXPECT_EQ(rig.Words(), "Light (1)");
}

TEST(UITooltipTest, ALabelThatFitsOffersNothing) {
	TooltipRig rig;
	MakeLabelledRow(rig, 300.0f, "Cube");
	rig.ctx.Update();

	rig.Hover(50, 10);
	EXPECT_FALSE(rig.Showing());
}

TEST(UITooltipTest, ExplicitTooltipWinsOverTruncatedLabelsText) {
	TooltipRig rig;
	UI::ElementHandle row = MakeLabelledRow(rig, 100.0f, "Barrel.prefab is a long name");
	rig.ctx.SetTooltip(row, "Assets/Props/Barrel.prefab");
	rig.ctx.Update();

	rig.Hover(50, 10);
	ASSERT_TRUE(rig.Showing());
	EXPECT_EQ(rig.Words(), "Assets/Props/Barrel.prefab");
}

TEST(UITooltipTest, InnerControlInheritsTooltip) {
	TooltipRig rig;
	UI::Style rowStyle = FixedBox(200.0f, 40.0f);
	rowStyle.flexDirection = UI::FlexDirection::Row;
	UI::ElementHandle row = MakeChild(rig.ctx, rig.ctx.GetRoot(), rowStyle);
	rig.ctx.SetTooltip(row, "How many charges the ability starts with");

	UI::ElementHandle field = MakeChild(rig.ctx, row, FixedBox(80.0f, 20.0f));
	rig.ctx.Update();

	const UI::Rect& box = rig.ctx.GetLayout(field).borderBox;
	rig.Hover(static_cast<i32>(box.x + box.w * 0.5f), static_cast<i32>(box.y + box.h * 0.5f));
	ASSERT_EQ(rig.ctx.GetHovered(), field);
	ASSERT_TRUE(rig.Showing());
	EXPECT_EQ(rig.Words(), "How many charges the ability starts with");

	rig.ctx.SetTooltip(field, "Charges");
	rig.Hover(380, 280);
	rig.Hover(static_cast<i32>(box.x + box.w * 0.5f), static_cast<i32>(box.y + box.h * 0.5f));
	EXPECT_EQ(rig.Words(), "Charges");
}

TEST(UITooltipTest, TheDwellHasToElapseFirst) {
	TooltipRig rig(0.5f);
	UI::ElementHandle target = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(100.0f, 40.0f));
	rig.ctx.SetTooltip(target, "Not yet");
	rig.ctx.Update();

	rig.Hover(50, 20);
	rig.ctx.Update();
	rig.ctx.Update();
	EXPECT_FALSE(rig.Showing());
}

TEST(UITooltipTest, LeavingTheElementTakesItAway) {
	TooltipRig rig;
	UI::ElementHandle target = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(100.0f, 40.0f));
	rig.ctx.SetTooltip(target, "Gone when you leave");
	rig.ctx.Update();

	rig.Hover(50, 20);
	ASSERT_TRUE(rig.Showing());

	rig.Hover(380, 280);
	EXPECT_FALSE(rig.Showing());
}

TEST(UITooltipTest, PressingTakesItAway) {
	TooltipRig rig;
	UI::ElementHandle target = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(100.0f, 40.0f));
	rig.ctx.SetTooltip(target, "Gone when you act");
	rig.ctx.Update();

	rig.Hover(50, 20);
	ASSERT_TRUE(rig.Showing());

	rig.ctx.InjectInput(MouseDown(50, 20));
	rig.ctx.Update();
	EXPECT_FALSE(rig.Showing());
}

TEST(UITooltipTest, TheCardNeverStealsAHit) {
	TooltipRig rig;
	UI::ElementHandle target = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(400.0f, 300.0f));
	rig.ctx.SetTooltip(target, "Over everything, in the way of nothing");
	rig.ctx.Update();
	rig.Hover(50, 20);
	ASSERT_TRUE(rig.Showing());

	const UI::Rect& box = rig.ctx.GetLayout(rig.Card()).borderBox;
	UI::ElementHandle hit = rig.ctx.HitTest({box.x + box.w * 0.5f, box.y + box.h * 0.5f});
	EXPECT_NE(hit, rig.Card());
	EXPECT_EQ(hit, target);
}

TEST(UITooltipTest, ATooltipDiesWithItsElement) {
	TooltipRig rig;
	UI::ElementHandle target = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(100.0f, 40.0f));
	rig.ctx.SetTooltip(target, "About the old element");
	rig.ctx.Update();
	rig.Hover(50, 20);
	ASSERT_TRUE(rig.Showing());

	rig.ctx.DestroyElement(target);
	UI::ElementHandle reused = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(100.0f, 40.0f));
	ASSERT_EQ(reused.index, target.index);
	rig.ctx.Update();
	rig.Hover(50, 20);

	EXPECT_FALSE(rig.Showing());
}

TEST(UITooltipTest, CardFlipsAbove) {
	TooltipRig rig;
	UI::ElementHandle target = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(400.0f, 300.0f));
	rig.ctx.SetTooltip(target, "Down at the bottom edge");
	rig.ctx.Update();

	rig.Hover(50, 295);
	ASSERT_TRUE(rig.Showing());
	const UI::Rect& box = rig.ctx.GetLayout(rig.Card()).borderBox;
	EXPECT_LT(box.y + box.h, 295.0f);
}

namespace {

	UI::ElementHandle MakeCellStrip(TooltipRig& rig, f32 stripWidth, std::span<const std::string_view> names) {
		UI::Style stripStyle = FixedBox(stripWidth, 40.0f);
		stripStyle.flexDirection = UI::FlexDirection::Row;
		UI::ElementHandle strip = MakeChild(rig.ctx, rig.ctx.GetRoot(), stripStyle);

		for (std::string_view name : names) {
			UI::ElementHandle cell = MakeChild(rig.ctx, strip, FixedBox(80.0f, 40.0f));
			UI::ElementHandle label = rig.ctx.CreateElement();
			UI::Style labelStyle;
			labelStyle.textOverflow = UI::TextOverflow::Ellipsis;
			rig.ctx.SetStyle(label, labelStyle);
			rig.ctx.SetHoverable(label, false);
			rig.ctx.AppendChild(cell, label);
			rig.ctx.SetText(label, Handle<FontAsset>{}, 14.0f, Color::White(), name);
		}
		return strip;
	}

} // anonymous namespace

TEST(UITooltipTest, ContainerHoveredOnFillerSpaceOffersNothing) {
	TooltipRig rig;
	const std::string_view names[] = {"Player Controller.prefab", "Untitled Scene 001.scene"};
	UI::ElementHandle strip = MakeCellStrip(rig, 340.0f, names);
	rig.ctx.Update();

	rig.Hover(250, 20);
	ASSERT_EQ(rig.ctx.GetHovered(), strip);
	EXPECT_FALSE(rig.Showing()) << "offered \"" << rig.Words();

	rig.Hover(40, 20);
	ASSERT_TRUE(rig.Showing());
	EXPECT_EQ(rig.Words(), "Player Controller.prefab");
}

TEST(UITooltipTest, RowOffersHitlessLabel) {
	TooltipRig rig;
	UI::ElementHandle host = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(140.0f, 40.0f));

	UI::Widgets::TreeRowDesc desc;
	desc.label = "Directional Light (1)";
	desc.font = Handle<FontAsset>(rig.font);
	desc.hasChildren = true;
	UI::Widgets::TreeRowParts parts = UI::Widgets::TreeRow(rig.ctx, host, desc);
	UI::Style labelStyle;
	labelStyle.textOverflow = UI::TextOverflow::Ellipsis;
	rig.ctx.SetStyle(parts.labelElement, labelStyle);
	rig.ctx.Update();

	const UI::Rect& label = rig.ctx.GetLayout(parts.labelElement).borderBox;
	rig.Hover(static_cast<i32>(label.x + label.w * 0.5f), static_cast<i32>(label.y + label.h * 0.5f));
	ASSERT_EQ(rig.ctx.GetHovered(), parts.row);
	ASSERT_TRUE(rig.Showing());
	EXPECT_EQ(rig.Words(), "Directional Light (1)");
}

TEST(UITooltipTest, ExpandableRowsArrowOffersNothingOfOwn) {
	TooltipRig rig;
	UI::ElementHandle host = MakeChild(rig.ctx, rig.ctx.GetRoot(), FixedBox(140.0f, 40.0f));

	UI::Widgets::TreeRowDesc desc;
	desc.label = "Directional Light (1)";
	desc.font = Handle<FontAsset>(rig.font);
	desc.hasChildren = true;
	UI::Widgets::TreeRowParts parts = UI::Widgets::TreeRow(rig.ctx, host, desc);
	UI::Style labelStyle;
	labelStyle.textOverflow = UI::TextOverflow::Ellipsis;
	rig.ctx.SetStyle(parts.labelElement, labelStyle);
	rig.ctx.Update();

	const UI::Rect& arrow = rig.ctx.GetLayout(parts.arrow).borderBox;
	rig.Hover(static_cast<i32>(arrow.x + arrow.w * 0.5f), static_cast<i32>(arrow.y + arrow.h * 0.5f));
	ASSERT_EQ(rig.ctx.GetHovered(), parts.arrow);
	EXPECT_FALSE(rig.Showing());
}

TEST(UITooltipTest, GridCellHoveredDirectlyStillOffersLabel) {
	TooltipRig rig;
	UI::Style cellStyle = FixedBox(80.0f, 90.0f);
	cellStyle.flexDirection = UI::FlexDirection::Column;
	UI::ElementHandle cell = MakeChild(rig.ctx, rig.ctx.GetRoot(), cellStyle);

	UI::ElementHandle thumbnail = MakeChild(rig.ctx, cell, FixedBox(64.0f, 64.0f));
	rig.ctx.SetHoverable(thumbnail, false);
	UI::ElementHandle label = rig.ctx.CreateElement();
	UI::Style labelStyle;
	labelStyle.textOverflow = UI::TextOverflow::Ellipsis;
	rig.ctx.SetStyle(label, labelStyle);
	rig.ctx.SetHoverable(label, false);
	rig.ctx.AppendChild(cell, label);
	rig.ctx.SetText(label, Handle<FontAsset>{}, 14.0f, Color::White(), "Standard Lit Material.mat");
	rig.ctx.Update();

	rig.Hover(40, 45);
	ASSERT_EQ(rig.ctx.GetHovered(), cell);
	ASSERT_TRUE(rig.Showing());
	EXPECT_EQ(rig.Words(), "Standard Lit Material.mat");
}
