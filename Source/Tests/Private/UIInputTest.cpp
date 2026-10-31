#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Window.h"

#include <algorithm>

using namespace Shalltear;
using namespace UITest;

namespace {

	UI::ContextDesc Desc() {
		UI::ContextDesc desc;
		desc.sizePx = {400, 300};
		desc.contentScale = 1.0f;
		return desc;
	}

	InputEvent MouseMove(i32 x, i32 y) {
		InputEvent event{};
		event.type = InputEventType::MouseMove;
		event.mousePosPx = {x, y};
		return event;
	}
	InputEvent MouseDown(i32 x, i32 y, KeyMod mods = KeyMod::None) {
		InputEvent event{};
		event.type = InputEventType::MouseDown;
		event.button = MouseButton::Left;
		event.mousePosPx = {x, y};
		event.mods = mods;
		return event;
	}
	InputEvent MouseUp(i32 x, i32 y) {
		InputEvent event{};
		event.type = InputEventType::MouseUp;
		event.button = MouseButton::Left;
		event.mousePosPx = {x, y};
		return event;
	}
	InputEvent KeyPress(Key key, KeyMod mods = KeyMod::None) {
		InputEvent event{};
		event.type = InputEventType::KeyDown;
		event.key = key;
		event.mods = mods;
		return event;
	}
	InputEvent TextChar(char32_t codepoint) {
		InputEvent event{};
		event.type = InputEventType::Text;
		event.codepoint = codepoint;
		return event;
	}

	void Click(UI::Context& ctx, i32 x, i32 y) {
		ctx.InjectInput(MouseDown(x, y));
		ctx.InjectInput(MouseUp(x, y));
		ctx.Update();
	}

	InputEvent MiddleDown(i32 x, i32 y) {
		InputEvent event{};
		event.type = InputEventType::MouseDown;
		event.button = MouseButton::Middle;
		event.mousePosPx = {x, y};
		return event;
	}
	InputEvent MiddleUp(i32 x, i32 y) {
		InputEvent event{};
		event.type = InputEventType::MouseUp;
		event.button = MouseButton::Middle;
		event.mousePosPx = {x, y};
		return event;
	}
	InputEvent Wheel(f32 notches) {
		InputEvent event{};
		event.type = InputEventType::MouseWheel;
		event.wheel = {0.0f, notches};
		return event;
	}

	UI::ElementHandle MakeFocusedInput(UI::Context& ctx, std::string_view text) {
		UI::ElementHandle input = ctx.CreateElement();
		ctx.AppendChild(ctx.GetRoot(), input);
		ctx.SetStyle(input, FixedBox(200.0f, 30.0f));
		ctx.SetText(input, Handle<FontAsset>{}, 14.0f, Color::White(), text);
		ctx.SetTextInput(input, true);
		ctx.Update();
		Click(ctx, 5, 15);
		return input;
	}

} // anonymous namespace

TEST(UIInputTest, HoverTracksEnterAndLeave) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle root = ctx.GetRoot();
	UI::Style rootStyle;
	rootStyle.alignItems = UI::Align::Start;
	ctx.SetStyle(root, rootStyle);
	UI::ElementHandle box = MakeChild(ctx, root, FixedBox(100.0f, 100.0f));
	ctx.Update();

	ctx.InjectInput(MouseMove(50, 50));
	ctx.Update();
	EXPECT_EQ(ctx.GetHovered(), box);
	EXPECT_TRUE(ctx.WantsMouse());

	ctx.InjectInput(MouseMove(300, 200));
	ctx.Update();
	EXPECT_EQ(ctx.GetHovered(), root);
	EXPECT_FALSE(ctx.WantsMouse());

	InputEvent leave{};
	leave.type = InputEventType::MouseLeave;
	ctx.InjectInput(leave);
	ctx.Update();
	EXPECT_FALSE(ctx.GetHovered().IsValid());
}

TEST(UIInputTest, ClickNeedsSameElement) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle box = MakeChild(ctx, ctx.GetRoot(), FixedBox(100.0f, 100.0f));
	ctx.Update();

	int downs = 0;
	int ups = 0;
	int clicks = 0;
	ctx.Events(box).onMouseDown = [&] { ++downs; };
	ctx.Events(box).onMouseUp = [&] { ++ups; };
	ctx.Events(box).onClick = [&] { ++clicks; };

	Click(ctx, 50, 50);
	EXPECT_EQ(downs, 1);
	EXPECT_EQ(ups, 1);
	EXPECT_EQ(clicks, 1);

	ctx.InjectInput(MouseDown(50, 50));
	ctx.InjectInput(MouseUp(300, 250));
	ctx.Update();
	EXPECT_EQ(downs, 2);
	EXPECT_EQ(clicks, 1);
}

TEST(UIInputTest, PointerEventsNonePassesThrough) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle back = MakeChild(ctx, ctx.GetRoot(), FixedBox(100.0f, 100.0f));
	UI::Style ghostStyle;
	ghostStyle.position = UI::Position::Absolute;
	ghostStyle.left = UI::Length::Pixel(0.0f);
	ghostStyle.top = UI::Length::Pixel(0.0f);
	ghostStyle.width = UI::Length::Pixel(100.0f);
	ghostStyle.height = UI::Length::Pixel(100.0f);
	ghostStyle.pointerEvents = false;
	MakeChild(ctx, ctx.GetRoot(), ghostStyle);
	ctx.Update();

	ctx.InjectInput(MouseMove(50, 50));
	ctx.Update();
	EXPECT_EQ(ctx.GetHovered(), back);
}

TEST(UIInputTest, FocusFollowsFocusableClicks) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle focusable = MakeChild(ctx, ctx.GetRoot(), FixedBox(100.0f, 50.0f));
	ctx.SetFocusable(focusable, true);
	ctx.Update();

	Click(ctx, 50, 25);
	EXPECT_EQ(ctx.GetFocused(), focusable);

	Click(ctx, 300, 250);
	EXPECT_FALSE(ctx.GetFocused().IsValid());

	Click(ctx, 50, 25);
	ASSERT_EQ(ctx.GetFocused(), focusable);
	ctx.InjectInput(KeyPress(Key::Escape));
	ctx.Update();
	EXPECT_FALSE(ctx.GetFocused().IsValid());
}

TEST(UIInputTest, PressFocusesFocusableAncestor) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style columnStyle = FixedBox(200.0f, 200.0f);
	columnStyle.flexDirection = UI::FlexDirection::Column;
	UI::ElementHandle outer = MakeChild(ctx, ctx.GetRoot(), columnStyle);
	ctx.SetFocusable(outer, true);
	UI::ElementHandle inner = MakeChild(ctx, outer, columnStyle);
	ctx.SetFocusable(inner, true);
	UI::ElementHandle row = MakeChild(ctx, inner, FixedBox(200.0f, 20.0f));
	u32 clicks = 0;
	ctx.Events(row).onClick = [&clicks]() { ++clicks; };
	ctx.Update();

	const UI::Rect& box = ctx.GetLayout(row).borderBox;
	Click(ctx, static_cast<i32>(box.x + box.w * 0.5f), static_cast<i32>(box.y + box.h * 0.5f));

	ASSERT_EQ(clicks, 1u);
	EXPECT_EQ(ctx.GetFocused(), inner);
}

TEST(UIInputTest, UnfocusablePressClearsFocus) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style columnStyle = FixedBox(200.0f, 100.0f);
	columnStyle.flexDirection = UI::FlexDirection::Column;
	UI::ElementHandle focusable = MakeChild(ctx, ctx.GetRoot(), columnStyle);
	ctx.SetFocusable(focusable, true);
	UI::ElementHandle chrome = MakeChild(ctx, ctx.GetRoot(), columnStyle);
	UI::ElementHandle bar = MakeChild(ctx, chrome, FixedBox(200.0f, 40.0f));
	UI::ElementHandle button = MakeChild(ctx, bar, FixedBox(40.0f, 20.0f));
	ctx.Update();

	Click(ctx, 100, 50);
	ASSERT_EQ(ctx.GetFocused(), focusable);

	const UI::Rect& box = ctx.GetLayout(button).borderBox;
	Click(ctx, static_cast<i32>(box.x + box.w * 0.5f), static_cast<i32>(box.y + box.h * 0.5f));
	EXPECT_FALSE(ctx.GetFocused().IsValid());
}

TEST(UIInputTest, TextInputSelectsAllOnClick) {
	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::Style columnStyle = FixedBox(300.0f, 100.0f);
	columnStyle.flexDirection = UI::FlexDirection::Column;
	UI::ElementHandle host = MakeChild(ctx, ctx.GetRoot(), columnStyle);
	ctx.SetFocusable(host, true);

	UI::ElementHandle input = MakeChild(ctx, host, FixedBox(200.0f, 30.0f));
	ctx.SetText(input, Handle<FontAsset>{}, 14.0f, Color::White(), "hello");
	ctx.SetTextInput(input, true);
	ctx.Update();

	const UI::Rect& box = ctx.GetLayout(input).borderBox;
	Click(ctx, static_cast<i32>(box.x + 5.0f), static_cast<i32>(box.y + box.h * 0.5f));
	ASSERT_EQ(ctx.GetFocused(), input);

	ctx.InjectInput(TextChar(U'X'));
	ctx.Update();
	EXPECT_EQ(ctx.GetTextString(input), "X");

	Click(ctx, static_cast<i32>(box.x + 5.0f), static_cast<i32>(box.y + box.h * 0.5f));
	ctx.InjectInput(TextChar(U'Y'));
	ctx.Update();
	EXPECT_EQ(ctx.GetTextString(input).size(), 2u);
}

TEST(UIInputTest, WheelScrollsTheNearestScrollableAncestor) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style scrollStyle;
	scrollStyle.flexDirection = UI::FlexDirection::Column;
	scrollStyle.overflow = UI::Overflow::Scroll;
	scrollStyle.width = UI::Length::Pixel(200.0f);
	scrollStyle.height = UI::Length::Pixel(100.0f);
	UI::ElementHandle scroller = MakeChild(ctx, ctx.GetRoot(), scrollStyle);
	UI::ElementHandle inner = MakeChild(ctx, scroller, FixedBox(180.0f, 400.0f));
	ctx.Update();

	ctx.InjectInput(MouseMove(50, 50));
	InputEvent wheel{};
	wheel.type = InputEventType::MouseWheel;
	wheel.wheel = {0.0f, -1.0f};
	ctx.InjectInput(wheel);
	ctx.Update();

	EXPECT_GT(ctx.GetElement(scroller)->scrollOffset.y, 0.0f);
	(void)inner;
}

TEST(UIInputTest, TypingEditsTextAndFiresCallbacks) {
	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::ElementHandle input = MakeFocusedInput(ctx, "");
	ASSERT_EQ(ctx.GetFocused(), input);
	EXPECT_TRUE(ctx.WantsKeyboard());

	int changes = 0;
	str committed;
	ctx.Events(input).onTextChange = [&](std::string_view) { ++changes; };
	ctx.Events(input).onTextCommit = [&](std::string_view text) { committed = str(text); };

	ctx.InjectInput(TextChar(U'h'));
	ctx.InjectInput(TextChar(U'i'));
	ctx.Update();
	EXPECT_EQ(ctx.GetTextString(input), "hi");
	EXPECT_EQ(changes, 2);

	ctx.InjectInput(KeyPress(Key::Enter));
	ctx.Update();
	EXPECT_EQ(committed, "hi");

	ctx.Events(input).onTextFilter = [](char32_t c) { return c >= U'0' && c <= U'9'; };
	ctx.InjectInput(TextChar(U'x'));
	ctx.InjectInput(TextChar(U'7'));
	ctx.Update();
	EXPECT_EQ(ctx.GetTextString(input), "hi7");
}

TEST(UIInputTest, EditingNavigationAndDeletion) {
	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::ElementHandle input = MakeFocusedInput(ctx, "abcd");
	ASSERT_EQ(ctx.GetFocused(), input);

	ctx.InjectInput(KeyPress(Key::End));
	ctx.InjectInput(KeyPress(Key::Backspace));
	ctx.Update();
	EXPECT_EQ(ctx.GetTextString(input), "abc");

	ctx.InjectInput(KeyPress(Key::Home));
	ctx.InjectInput(KeyPress(Key::Delete));
	ctx.Update();
	EXPECT_EQ(ctx.GetTextString(input), "bc");

	ctx.InjectInput(KeyPress(Key::Home));
	ctx.InjectInput(KeyPress(Key::Right, KeyMod::Shift));
	ctx.InjectInput(TextChar(U'X'));
	ctx.Update();
	EXPECT_EQ(ctx.GetTextString(input), "Xc");
}

TEST(UIInputTest, UTF8EditingStaysOnCodepointBoundaries) {
	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::ElementHandle input = MakeFocusedInput(ctx, "");
	ASSERT_EQ(ctx.GetFocused(), input);

	ctx.InjectInput(TextChar(U'ä'));
	ctx.InjectInput(TextChar(U'→'));
	ctx.Update();
	EXPECT_EQ(ctx.GetTextString(input), "\xC3\xA4\xE2\x86\x92");

	ctx.InjectInput(KeyPress(Key::Backspace));
	ctx.Update();
	EXPECT_EQ(ctx.GetTextString(input), "\xC3\xA4");
	ctx.InjectInput(KeyPress(Key::Backspace));
	ctx.Update();
	EXPECT_EQ(ctx.GetTextString(input), "");
}

TEST(UIInputTest, ClipboardCutCopyPasteSelectAll) {
	if (!WindowSystem::IsInitialized()) {
		WindowSystem::Initialize();
	}
	{
		MockFontSystem fonts;
		UI::Context ctx(Desc(), &fonts);
		UI::ElementHandle input = MakeFocusedInput(ctx, "hello");
		ASSERT_EQ(ctx.GetFocused(), input);

		ctx.InjectInput(KeyPress(Key::A, KeyMod::Ctrl));
		ctx.InjectInput(KeyPress(Key::C, KeyMod::Ctrl));
		ctx.Update();
		EXPECT_EQ(ctx.GetTextString(input), "hello");
		EXPECT_EQ(WindowSystem::GetClipboardText(), "hello");

		ctx.InjectInput(KeyPress(Key::A, KeyMod::Ctrl));
		ctx.InjectInput(KeyPress(Key::X, KeyMod::Ctrl));
		ctx.Update();
		EXPECT_EQ(ctx.GetTextString(input), "");

		ctx.InjectInput(KeyPress(Key::V, KeyMod::Ctrl));
		ctx.Update();
		EXPECT_EQ(ctx.GetTextString(input), "hello");
	}
	WindowSystem::Terminate();
}

TEST(UIInputTest, DesiredCursorFollowsHover) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style handStyle = FixedBox(100.0f, 100.0f);
	handStyle.cursor = SystemCursor::Hand;
	MakeChild(ctx, ctx.GetRoot(), handStyle);
	ctx.Update();

	ctx.InjectInput(MouseMove(50, 50));
	ctx.Update();
	EXPECT_EQ(ctx.GetDesiredCursor(), SystemCursor::Hand);

	ctx.InjectInput(MouseMove(300, 250));
	ctx.Update();
	EXPECT_EQ(ctx.GetDesiredCursor(), SystemCursor::Arrow);
}

TEST(UIInputTest, HoverFollowsMovingElement) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style row;
	row.alignItems = UI::Align::Start;
	ctx.SetStyle(ctx.GetRoot(), row);

	UI::ElementHandle left = MakeChild(ctx, ctx.GetRoot(), FixedBox(100.0f, 100.0f));
	UI::Style rightStyle = FixedBox(100.0f, 100.0f);
	rightStyle.cursor = SystemCursor::Hand;
	UI::ElementHandle right = MakeChild(ctx, ctx.GetRoot(), rightStyle);
	ctx.Update();

	ctx.InjectInput(MouseMove(50, 50));
	ctx.Update();
	ASSERT_EQ(ctx.GetHovered(), left);
	ASSERT_EQ(ctx.GetDesiredCursor(), SystemCursor::Arrow);

	ctx.SetStyle(left, FixedBox(20.0f, 100.0f));
	ctx.Update();

	EXPECT_EQ(ctx.GetHovered(), right);
	EXPECT_EQ(ctx.GetDesiredCursor(), SystemCursor::Hand);
}

TEST(UIInputTest, FrameThatMovesNothingReResolvesNothing) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style row;
	row.alignItems = UI::Align::Start;
	ctx.SetStyle(ctx.GetRoot(), row);
	UI::ElementHandle box = MakeChild(ctx, ctx.GetRoot(), FixedBox(100.0f, 100.0f));
	ctx.Update();

	ctx.InjectInput(MouseMove(50, 50));
	ctx.Update();
	ASSERT_EQ(ctx.GetHovered(), box);

	ctx.ResetLayoutStats();
	ctx.Update();
	ctx.Update();
	EXPECT_EQ(ctx.GetLayoutStats().passes, 0u) << ctx.GetLayoutStats().passes << " times";
	EXPECT_EQ(ctx.GetHovered(), box);
}

namespace {

	InputEvent RightDown(i32 x, i32 y) {
		InputEvent event{};
		event.type = InputEventType::MouseDown;
		event.button = MouseButton::Right;
		event.mousePosPx = {x, y};
		return event;
	}
	InputEvent RightUp(i32 x, i32 y) {
		InputEvent event{};
		event.type = InputEventType::MouseUp;
		event.button = MouseButton::Right;
		event.mousePosPx = {x, y};
		return event;
	}

} // anonymous namespace

TEST(UIInputTest, RightClickNeedsSameElement) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle box = MakeChild(ctx, ctx.GetRoot(), FixedBox(100.0f, 100.0f));
	ctx.Update();

	int rightClicks = 0;
	int leftClicks = 0;
	ctx.Events(box).onRightClick = [&] { ++rightClicks; };
	ctx.Events(box).onClick = [&] { ++leftClicks; };

	ctx.InjectInput(RightDown(50, 50));
	ctx.InjectInput(RightUp(50, 50));
	ctx.Update();
	EXPECT_EQ(rightClicks, 1);
	EXPECT_EQ(leftClicks, 0);

	ctx.InjectInput(RightDown(50, 50));
	ctx.InjectInput(RightUp(300, 250));
	ctx.Update();
	EXPECT_EQ(rightClicks, 1);
}

TEST(UIInputTest, RightClickDoesNotStealFocus) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style rootStyle;
	rootStyle.alignItems = UI::Align::Start;
	ctx.SetStyle(ctx.GetRoot(), rootStyle);
	UI::ElementHandle box = MakeChild(ctx, ctx.GetRoot(), FixedBox(100.0f, 100.0f));
	UI::ElementHandle input = MakeChild(ctx, ctx.GetRoot(), FixedBox(200.0f, 30.0f));
	ctx.SetText(input, Handle<FontAsset>{}, 14.0f, Color::White(), "abc");
	ctx.SetTextInput(input, true);
	ctx.Update();

	Click(ctx, 150, 15);
	ASSERT_EQ(ctx.GetFocused(), input);

	ctx.InjectInput(RightDown(50, 50));
	ctx.InjectInput(RightUp(50, 50));
	ctx.Update();
	EXPECT_EQ(ctx.GetFocused(), input);
	(void)box;
}

TEST(UIInputTest, DoubleClickFiresOnSecondQuickClick) {
	UI::ContextDesc desc = Desc();
	desc.doubleClickSeconds = 60.0f;
	UI::Context ctx(desc, nullptr);
	UI::ElementHandle box = MakeChild(ctx, ctx.GetRoot(), FixedBox(200.0f, 200.0f));
	ctx.Update();

	int clicks = 0;
	int doubles = 0;
	ctx.Events(box).onClick = [&] { ++clicks; };
	ctx.Events(box).onDoubleClick = [&] { ++doubles; };

	Click(ctx, 50, 50);
	Click(ctx, 51, 51);
	EXPECT_EQ(clicks, 2);
	EXPECT_EQ(doubles, 1);

	Click(ctx, 50, 50);
	EXPECT_EQ(doubles, 1);
	Click(ctx, 50, 50);
	EXPECT_EQ(doubles, 2);
}

TEST(UIInputTest, DoubleClickRespectsDistanceThreshold) {
	UI::ContextDesc desc = Desc();
	desc.doubleClickSeconds = 60.0f;
	UI::Context ctx(desc, nullptr);
	UI::ElementHandle box = MakeChild(ctx, ctx.GetRoot(), FixedBox(200.0f, 200.0f));
	ctx.Update();

	int doubles = 0;
	ctx.Events(box).onDoubleClick = [&] { ++doubles; };

	Click(ctx, 20, 20);
	Click(ctx, 90, 90);
	EXPECT_EQ(doubles, 0);
}

TEST(UIInputTest, HoverCallbacksFireOnEnterAndLeave) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style rootStyle;
	rootStyle.alignItems = UI::Align::Start;
	ctx.SetStyle(ctx.GetRoot(), rootStyle);
	UI::ElementHandle box = MakeChild(ctx, ctx.GetRoot(), FixedBox(100.0f, 100.0f));
	ctx.Update();

	int enters = 0;
	int leaves = 0;
	ctx.Events(box).onHoverEnter = [&] { ++enters; };
	ctx.Events(box).onHoverLeave = [&] { ++leaves; };

	ctx.InjectInput(MouseMove(50, 50));
	ctx.Update();
	EXPECT_EQ(enters, 1);
	EXPECT_EQ(leaves, 0);

	ctx.InjectInput(MouseMove(300, 200));
	ctx.Update();
	EXPECT_EQ(enters, 1);
	EXPECT_EQ(leaves, 1);

	ctx.InjectInput(MouseMove(50, 50));
	ctx.Update();
	EXPECT_EQ(enters, 2);

	InputEvent leave{};
	leave.type = InputEventType::MouseLeave;
	ctx.InjectInput(leave);
	ctx.Update();
	EXPECT_EQ(leaves, 2);
}

TEST(UIInputTest, KeyDownFirstRefusalConsumesBeforeEditing) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle input = MakeFocusedInput(ctx, "abc");

	ctx.Events(input).onKeyDown = [](Key key, KeyMod) { return key == Key::Delete; };
	ctx.InjectInput(KeyPress(Key::Delete));
	ctx.Update();
	EXPECT_EQ(ctx.GetTextString(input), "abc");

	ctx.Events(input).onKeyDown = [](Key, KeyMod) { return false; };
	ctx.InjectInput(KeyPress(Key::Delete));
	ctx.Update();
	EXPECT_EQ(ctx.GetTextString(input), "");
}

TEST(UIInputTest, EscapeRevertsTextEditsSinceFocus) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle input = MakeFocusedInput(ctx, "abc");

	int changes = 0;
	ctx.Events(input).onTextChange = [&](std::string_view) { ++changes; };

	ctx.InjectInput(TextChar('x'));
	ctx.Update();
	EXPECT_EQ(ctx.GetTextString(input), "x");

	ctx.InjectInput(KeyPress(Key::Escape));
	ctx.Update();
	EXPECT_EQ(ctx.GetTextString(input), "abc");
	EXPECT_FALSE(ctx.GetFocused().IsValid());
	EXPECT_EQ(changes, 2);
}

TEST(UIInputTest, EnterCommitBecomesTheNewRevertPoint) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle input = MakeFocusedInput(ctx, "abc");

	ctx.InjectInput(TextChar('x'));
	ctx.InjectInput(KeyPress(Key::Enter));
	ctx.InjectInput(TextChar('y'));
	ctx.Update();
	EXPECT_EQ(ctx.GetTextString(input), "xy");

	ctx.InjectInput(KeyPress(Key::Escape));
	ctx.Update();
	EXPECT_EQ(ctx.GetTextString(input), "x");
}

TEST(UIInputTest, HoverSuppressionSilencesHoverUntilReleased) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style rootStyle;
	rootStyle.alignItems = UI::Align::Start;
	ctx.SetStyle(ctx.GetRoot(), rootStyle);
	UI::ElementHandle box = MakeChild(ctx, ctx.GetRoot(), FixedBox(100.0f, 100.0f));
	int enters = 0;
	int leaves = 0;
	ctx.Events(box).onHoverEnter = [&]() { ++enters; };
	ctx.Events(box).onHoverLeave = [&]() { ++leaves; };
	ctx.Update();

	ctx.InjectInput(MouseMove(50, 50));
	ctx.Update();
	EXPECT_EQ(ctx.GetHovered(), box);
	EXPECT_EQ(enters, 1);

	ctx.SetHoverSuppressed(true);
	EXPECT_FALSE(ctx.GetHovered().IsValid());
	EXPECT_EQ(leaves, 1);

	ctx.InjectInput(MouseMove(55, 55));
	ctx.Update();
	EXPECT_FALSE(ctx.GetHovered().IsValid());
	EXPECT_EQ(enters, 1);

	ctx.SetHoverSuppressed(false);
	ctx.InjectInput(MouseMove(50, 50));
	ctx.Update();
	EXPECT_EQ(ctx.GetHovered(), box);
	EXPECT_EQ(enters, 2);
}

namespace {

	struct CaptureRig {
		UI::Context ctx{Desc(), nullptr};
		UI::ElementHandle dragger;
		UI::ElementHandle bystander;
		int drags = 0;
		int pressEnds = 0;
		int bystanderEnters = 0;
		int bystanderDowns = 0;
		int bystanderUps = 0;
		int bystanderClicks = 0;
		int bystanderRightClicks = 0;

		explicit CaptureRig(bool installDragHandler) {
			UI::Style rootStyle;
			rootStyle.alignItems = UI::Align::Start;
			ctx.SetStyle(ctx.GetRoot(), rootStyle);
			dragger = MakeChild(ctx, ctx.GetRoot(), FixedBox(100.0f, 100.0f));
			bystander = MakeChild(ctx, ctx.GetRoot(), FixedBox(100.0f, 100.0f));
			if (installDragHandler) {
				ctx.Events(dragger).onPressDrag = [this](Vec2) { ++drags; };
			}
			ctx.Events(dragger).onPressEnd = [this]() { ++pressEnds; };
			ctx.Events(bystander).onHoverEnter = [this]() { ++bystanderEnters; };
			ctx.Events(bystander).onMouseDown = [this]() { ++bystanderDowns; };
			ctx.Events(bystander).onMouseUp = [this]() { ++bystanderUps; };
			ctx.Events(bystander).onClick = [this]() { ++bystanderClicks; };
			ctx.Events(bystander).onRightClick = [this]() { ++bystanderRightClicks; };
			ctx.Update();
		}
	};

} // anonymous namespace

TEST(UIInputTest, PressDragOwnsPointerForWholeGesture) {
	CaptureRig rig(true);

	rig.ctx.InjectInput(MouseMove(50, 50));
	rig.ctx.InjectInput(MouseDown(50, 50));
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetHovered(), rig.dragger);

	rig.ctx.InjectInput(MouseMove(150, 50));
	rig.ctx.Update();
	EXPECT_EQ(rig.drags, 1);
	EXPECT_FALSE(rig.ctx.GetHovered().IsValid());
	EXPECT_EQ(rig.bystanderEnters, 0);

	rig.ctx.InjectInput(MouseUp(150, 50));
	rig.ctx.Update();
	EXPECT_EQ(rig.pressEnds, 1);
	EXPECT_EQ(rig.bystanderUps, 0);
	EXPECT_EQ(rig.bystanderClicks, 0);

	EXPECT_EQ(rig.ctx.GetHovered(), rig.bystander);
	EXPECT_EQ(rig.bystanderEnters, 1);
}

TEST(UIInputTest, PressWithNoDragHandlerCapturesNothing) {
	CaptureRig rig(false);

	rig.ctx.InjectInput(MouseDown(50, 50));
	rig.ctx.InjectInput(MouseMove(150, 50));
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetHovered(), rig.bystander);
	EXPECT_EQ(rig.bystanderEnters, 1);

	rig.ctx.InjectInput(MouseUp(150, 50));
	rig.ctx.Update();
	EXPECT_EQ(rig.bystanderUps, 1);
}

TEST(UIInputTest, DragOwnsPointer) {
	CaptureRig rig(true);

	rig.ctx.InjectInput(MouseDown(50, 50));
	rig.ctx.InjectInput(MouseMove(150, 50));

	InputEvent rightDown{};
	rightDown.type = InputEventType::MouseDown;
	rightDown.button = MouseButton::Right;
	rightDown.mousePosPx = {150, 50};
	InputEvent rightUp = rightDown;
	rightUp.type = InputEventType::MouseUp;
	rig.ctx.InjectInput(rightDown);
	rig.ctx.InjectInput(rightUp);
	rig.ctx.InjectInput(MouseDown(150, 50));
	rig.ctx.Update();

	EXPECT_EQ(rig.bystanderRightClicks, 0);
	EXPECT_EQ(rig.bystanderDowns, 0);

	rig.ctx.InjectInput(MouseUp(150, 50));
	rig.ctx.Update();
	EXPECT_EQ(rig.pressEnds, 1);
}

TEST(UIInputTest, WheelFollowsDragNotCursor) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style rootStyle;
	rootStyle.alignItems = UI::Align::Start;
	ctx.SetStyle(ctx.GetRoot(), rootStyle);

	UI::Style scrollStyle;
	scrollStyle.flexDirection = UI::FlexDirection::Column;
	scrollStyle.overflow = UI::Overflow::Scroll;
	scrollStyle.width = UI::Length::Pixel(100.0f);
	scrollStyle.height = UI::Length::Pixel(100.0f);

	UI::ElementHandle dragScroller = MakeChild(ctx, ctx.GetRoot(), scrollStyle);
	UI::ElementHandle dragInner = MakeChild(ctx, dragScroller, FixedBox(80.0f, 400.0f));
	UI::ElementHandle bystandScroller = MakeChild(ctx, ctx.GetRoot(), scrollStyle);
	MakeChild(ctx, bystandScroller, FixedBox(80.0f, 400.0f));
	ctx.Events(dragInner).onPressDrag = [](Vec2) {};
	ctx.Update();

	ctx.InjectInput(MouseMove(50, 50));
	ctx.InjectInput(MouseDown(50, 50));
	ctx.InjectInput(MouseMove(150, 50));
	InputEvent wheel{};
	wheel.type = InputEventType::MouseWheel;
	wheel.wheel = {0.0f, -1.0f};
	ctx.InjectInput(wheel);
	ctx.Update();

	EXPECT_GT(ctx.GetElement(dragScroller)->scrollOffset.y, 0.0f);
	EXPECT_FLOAT_EQ(ctx.GetElement(bystandScroller)->scrollOffset.y, 0.0f);
}

namespace {
	struct ScrollerAndLeaf {
		UI::ElementHandle scroller;
		UI::ElementHandle leaf;
	};
	ScrollerAndLeaf MakeScrollerAndLeaf(UI::Context& ctx) {
		UI::Style scrollStyle;
		scrollStyle.flexDirection = UI::FlexDirection::Column;
		scrollStyle.overflow = UI::Overflow::Scroll;
		scrollStyle.width = UI::Length::Pixel(200.0f);
		scrollStyle.height = UI::Length::Pixel(100.0f);
		ScrollerAndLeaf out;
		out.scroller = MakeChild(ctx, ctx.GetRoot(), scrollStyle);
		out.leaf = MakeChild(ctx, out.scroller, FixedBox(180.0f, 400.0f));
		return out;
	}
} // namespace

TEST(UIInputTest, WheelHandlerBeatsScrollAncestor) {
	UI::Context ctx(Desc(), nullptr);
	ScrollerAndLeaf parts = MakeScrollerAndLeaf(ctx);
	Vec2 seen{-99.0f, -99.0f};
	ctx.Events(parts.leaf).onWheel = [&seen](Vec2 notches) {
		seen = notches;
		return true;
	};
	ctx.Update();

	ctx.InjectInput(MouseMove(50, 50));
	ctx.InjectInput(Wheel(-1.0f));
	ctx.Update();

	EXPECT_FLOAT_EQ(seen.y, -1.0f);
	EXPECT_FLOAT_EQ(ctx.GetElement(parts.scroller)->scrollOffset.y, 0.0f);
}

TEST(UIInputTest, DeclinedNotchReachesScroll) {
	UI::Context ctx(Desc(), nullptr);
	ScrollerAndLeaf parts = MakeScrollerAndLeaf(ctx);
	u32 calls = 0;
	ctx.Events(parts.leaf).onWheel = [&calls](Vec2) {
		++calls;
		return false;
	};
	ctx.Update();

	ctx.InjectInput(MouseMove(50, 50));
	ctx.InjectInput(Wheel(-1.0f));
	ctx.Update();

	EXPECT_EQ(calls, 1u);
	EXPECT_GT(ctx.GetElement(parts.scroller)->scrollOffset.y, 0.0f);
}

TEST(UIInputTest, WheelAsksEveryAncestor) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style scrollStyle;
	scrollStyle.flexDirection = UI::FlexDirection::Column;
	scrollStyle.overflow = UI::Overflow::Scroll;
	scrollStyle.width = UI::Length::Pixel(200.0f);
	scrollStyle.height = UI::Length::Pixel(100.0f);
	UI::ElementHandle scroller = MakeChild(ctx, ctx.GetRoot(), scrollStyle);
	UI::ElementHandle middle = MakeChild(ctx, scroller, FixedBox(180.0f, 400.0f));
	UI::ElementHandle leaf = MakeChild(ctx, middle, FixedBox(100.0f, 50.0f));
	bool fired = false;
	ctx.Events(middle).onWheel = [&fired](Vec2) {
		fired = true;
		return true;
	};
	ctx.Update();

	ctx.InjectInput(MouseMove(50, 20));
	ctx.InjectInput(Wheel(-1.0f));
	ctx.Update();

	EXPECT_TRUE(fired);
	EXPECT_FLOAT_EQ(ctx.GetElement(scroller)->scrollOffset.y, 0.0f);
	(void)leaf;
}

TEST(UIInputTest, WheelHandlersTakeOwnNotches) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle left = MakeChild(ctx, ctx.GetRoot(), FixedBox(100.0f, 100.0f));
	UI::ElementHandle right = MakeChild(ctx, ctx.GetRoot(), FixedBox(100.0f, 100.0f));
	u32 leftCalls = 0;
	u32 rightCalls = 0;
	ctx.Events(left).onWheel = [&leftCalls](Vec2) {
		++leftCalls;
		return true;
	};
	ctx.Events(right).onWheel = [&rightCalls](Vec2) {
		++rightCalls;
		return true;
	};
	ctx.Update();

	ctx.InjectInput(MouseMove(50, 50));
	ctx.InjectInput(Wheel(-1.0f));
	ctx.Update();
	EXPECT_EQ(leftCalls, 1u);
	EXPECT_EQ(rightCalls, 0u);

	ctx.InjectInput(MouseMove(150, 50));
	ctx.InjectInput(Wheel(-1.0f));
	ctx.Update();
	EXPECT_EQ(leftCalls, 1u);
	EXPECT_EQ(rightCalls, 1u);
}

TEST(UIInputTest, AWheelHandlerDiesWithItsElement) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style scrollStyle;
	scrollStyle.flexDirection = UI::FlexDirection::Column;
	scrollStyle.overflow = UI::Overflow::Scroll;
	scrollStyle.width = UI::Length::Pixel(200.0f);
	scrollStyle.height = UI::Length::Pixel(100.0f);
	UI::ElementHandle scroller = MakeChild(ctx, ctx.GetRoot(), scrollStyle);
	UI::ElementHandle content = MakeChild(ctx, scroller, FixedBox(180.0f, 400.0f));
	UI::ElementHandle handled = MakeChild(ctx, content, FixedBox(100.0f, 50.0f));
	u32 calls = 0;
	ctx.Events(handled).onWheel = [&calls](Vec2) {
		++calls;
		return true;
	};
	ctx.Update();

	ctx.InjectInput(MouseMove(50, 20));
	ctx.InjectInput(Wheel(-1.0f));
	ctx.Update();
	ASSERT_EQ(calls, 1u);

	ctx.DestroyElement(handled);
	ctx.Update();

	ctx.InjectInput(MouseMove(50, 20));
	ctx.InjectInput(Wheel(-1.0f));
	ctx.Update();
	EXPECT_EQ(calls, 1u);
	EXPECT_GT(ctx.GetElement(scroller)->scrollOffset.y, 0.0f);
}

TEST(UIInputTest, MiddleDragReportsTravelSinceLastReport) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle box = MakeChild(ctx, ctx.GetRoot(), FixedBox(200.0f, 200.0f));
	Vector<Vec2> deltas{GetMemoryResource(MemTag::Debug)};
	ctx.Events(box).onMiddleDrag = [&deltas](Vec2 delta) { deltas.push_back(delta); };
	ctx.Update();

	ctx.InjectInput(MouseMove(50, 50));
	ctx.InjectInput(MiddleDown(50, 50));
	ctx.Update();
	ctx.InjectInput(MouseMove(70, 80));
	ctx.Update();
	ctx.InjectInput(MouseMove(75, 80));
	ctx.Update();

	ASSERT_EQ(deltas.size(), 2u);
	EXPECT_FLOAT_EQ(deltas[0].x, 20.0f);
	EXPECT_FLOAT_EQ(deltas[0].y, 30.0f);
	EXPECT_FLOAT_EQ(deltas[1].x, 5.0f);
	EXPECT_FLOAT_EQ(deltas[1].y, 0.0f);
}

TEST(UIInputTest, CoalescedMovesLoseNoneOfDragsTravel) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle box = MakeChild(ctx, ctx.GetRoot(), FixedBox(200.0f, 200.0f));
	Vec2 total{};
	ctx.Events(box).onMiddleDrag = [&total](Vec2 delta) {
		total.x += delta.x;
		total.y += delta.y;
	};
	ctx.Update();

	ctx.InjectInput(MouseMove(50, 50));
	ctx.InjectInput(MiddleDown(50, 50));
	ctx.Update();
	ctx.InjectInput(MouseMove(60, 65));
	ctx.InjectInput(MouseMove(70, 80));
	ctx.InjectInput(MouseMove(90, 110));
	ctx.Update();

	EXPECT_FLOAT_EQ(total.x, 40.0f);
	EXPECT_FLOAT_EQ(total.y, 60.0f);
}

TEST(UIInputTest, MiddlePressIsInert) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle box = MakeChild(ctx, ctx.GetRoot(), FixedBox(200.0f, 200.0f));
	ctx.SetFocusable(box, true);
	u32 clicks = 0;
	u32 downs = 0;
	u32 drags = 0;
	ctx.Events(box).onClick = [&clicks]() { ++clicks; };
	ctx.Events(box).onMouseDown = [&downs]() { ++downs; };
	ctx.Events(box).onMiddleDrag = [&drags](Vec2) { ++drags; };
	ctx.Update();

	ctx.InjectInput(MouseMove(50, 50));
	ctx.InjectInput(MiddleDown(50, 50));
	ctx.Update();
	ctx.InjectInput(MouseMove(70, 70));
	ctx.InjectInput(MiddleUp(70, 70));
	ctx.Update();

	ASSERT_EQ(drags, 1u);
	EXPECT_EQ(clicks, 0u);
	EXPECT_EQ(downs, 0u);
	EXPECT_NE(ctx.GetFocused(), box);
	EXPECT_FALSE(HasFlag(ctx.GetElement(box)->flags, UI::ElementFlags::Pressed));
}

TEST(UIInputTest, AMiddleDragStopsAtTheRelease) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle box = MakeChild(ctx, ctx.GetRoot(), FixedBox(200.0f, 200.0f));
	u32 calls = 0;
	ctx.Events(box).onMiddleDrag = [&calls](Vec2) { ++calls; };
	ctx.Update();

	ctx.InjectInput(MouseMove(50, 50));
	ctx.InjectInput(MiddleDown(50, 50));
	ctx.InjectInput(MouseMove(60, 60));
	ctx.InjectInput(MiddleUp(60, 60));
	ctx.InjectInput(MouseMove(90, 90));
	ctx.Update();

	EXPECT_EQ(calls, 1u);
}

TEST(UIInputTest, DestroyingMiddlePressedElementEndsDrag) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle box = MakeChild(ctx, ctx.GetRoot(), FixedBox(200.0f, 200.0f));
	u32 calls = 0;
	ctx.Events(box).onMiddleDrag = [&calls](Vec2) { ++calls; };
	ctx.Update();

	ctx.InjectInput(MouseMove(50, 50));
	ctx.InjectInput(MiddleDown(50, 50));
	ctx.InjectInput(MouseMove(60, 60));
	ctx.Update();
	EXPECT_EQ(calls, 1u);

	ctx.DestroyElement(box);
	ctx.Update();
	ctx.InjectInput(MouseMove(90, 90));
	ctx.Update();
	EXPECT_EQ(calls, 1u);
}

namespace {

	struct ClaimRig {
		UI::Context ctx{Desc(), nullptr};
		UI::ElementHandle viewport;
		UI::ElementHandle bystander;
		int bystanderEnters = 0;
		int bystanderDowns = 0;
		int bystanderClicks = 0;

		ClaimRig() {
			UI::Style rootStyle;
			rootStyle.alignItems = UI::Align::Start;
			ctx.SetStyle(ctx.GetRoot(), rootStyle);
			viewport = MakeChild(ctx, ctx.GetRoot(), FixedBox(100.0f, 100.0f));
			bystander = MakeChild(ctx, ctx.GetRoot(), FixedBox(100.0f, 100.0f));
			ctx.Events(bystander).onHoverEnter = [this]() { ++bystanderEnters; };
			ctx.Events(bystander).onMouseDown = [this]() { ++bystanderDowns; };
			ctx.Events(bystander).onClick = [this]() { ++bystanderClicks; };
			ctx.Update();
		}
	};

} // anonymous namespace

TEST(UIInputTest, AClaimedPointerHoversAndClicksNothing) {
	ClaimRig rig;
	rig.ctx.InjectInput(MouseMove(50, 50));
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetHovered(), rig.viewport);

	u32 claim = rig.ctx.ClaimPointer(rig.viewport, CursorMode::Wrapped);
	ASSERT_NE(claim, 0u);
	EXPECT_FALSE(rig.ctx.GetHovered().IsValid());
	EXPECT_EQ(rig.ctx.GetPointerClaimMode(), CursorMode::Wrapped);

	rig.ctx.InjectInput(MouseMove(150, 50));
	Click(rig.ctx, 150, 50);
	rig.ctx.Update();
	EXPECT_FALSE(rig.ctx.GetHovered().IsValid());
	EXPECT_EQ(rig.bystanderEnters, 0);
	EXPECT_EQ(rig.bystanderDowns, 0);
	EXPECT_EQ(rig.bystanderClicks, 0);

	rig.ctx.ReleasePointer(claim);
	EXPECT_EQ(rig.ctx.GetPointerClaimMode(), CursorMode::Normal);
	rig.ctx.InjectInput(MouseMove(150, 50));
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetHovered(), rig.bystander);
	EXPECT_EQ(rig.bystanderEnters, 1);
	Click(rig.ctx, 150, 50);
	rig.ctx.Update();
	EXPECT_EQ(rig.bystanderClicks, 1);
}

TEST(UIInputTest, ClaimedWheelNotchReachesNoScrollContainer) {
	UI::Context ctx(Desc(), nullptr);
	UI::Style scrollStyle;
	scrollStyle.flexDirection = UI::FlexDirection::Column;
	scrollStyle.overflow = UI::Overflow::Scroll;
	scrollStyle.width = UI::Length::Pixel(100.0f);
	scrollStyle.height = UI::Length::Pixel(100.0f);
	UI::ElementHandle scroller = MakeChild(ctx, ctx.GetRoot(), scrollStyle);
	MakeChild(ctx, scroller, FixedBox(80.0f, 400.0f));
	ctx.Update();

	ctx.InjectInput(MouseMove(50, 50));
	ctx.Update();
	u32 claim = ctx.ClaimPointer(scroller, CursorMode::Wrapped);
	ASSERT_NE(claim, 0u);

	InputEvent wheel{};
	wheel.type = InputEventType::MouseWheel;
	wheel.wheel = {0.0f, -1.0f};
	ctx.InjectInput(wheel);
	ctx.Update();
	EXPECT_FLOAT_EQ(ctx.GetElement(scroller)->scrollOffset.y, 0.0f);

	ctx.ReleasePointer(claim);
	ctx.InjectInput(wheel);
	ctx.Update();
	EXPECT_GT(ctx.GetElement(scroller)->scrollOffset.y, 0.0f);
}

TEST(UIInputTest, ClaimedPointerFeedsDrag) {
	CaptureRig rig(true);
	rig.ctx.InjectInput(MouseMove(50, 50));
	rig.ctx.InjectInput(MouseDown(50, 50));
	rig.ctx.Update();

	u32 claim = rig.ctx.ClaimPointer(rig.dragger, CursorMode::Wrapped);
	ASSERT_NE(claim, 0u);
	rig.ctx.InjectInput(MouseMove(150, 50));
	rig.ctx.Update();
	EXPECT_EQ(rig.drags, 1);
	rig.ctx.InjectInput(MouseMove(250, 50));
	rig.ctx.Update();
	EXPECT_EQ(rig.drags, 2);

	rig.ctx.InjectInput(MouseUp(250, 50));
	rig.ctx.Update();
	EXPECT_EQ(rig.pressEnds, 1);
}

TEST(UIInputTest, PointerIsOneDevice) {
	ClaimRig rig;
	u32 first = rig.ctx.ClaimPointer(rig.viewport, CursorMode::Wrapped);
	ASSERT_NE(first, 0u);
	EXPECT_EQ(rig.ctx.ClaimPointer(rig.bystander, CursorMode::Locked), 0u);
	EXPECT_EQ(rig.ctx.GetPointerClaimMode(), CursorMode::Wrapped);

	rig.ctx.ReleasePointer(0);
	rig.ctx.ReleasePointer(first + 99);
	EXPECT_EQ(rig.ctx.GetPointerClaimMode(), CursorMode::Wrapped);

	rig.ctx.ReleasePointer(first);
	EXPECT_EQ(rig.ctx.GetPointerClaimMode(), CursorMode::Normal);
	u32 second = rig.ctx.ClaimPointer(rig.viewport, CursorMode::Wrapped);
	ASSERT_NE(second, first);
	rig.ctx.ReleasePointer(first);
	EXPECT_EQ(rig.ctx.GetPointerClaimMode(), CursorMode::Wrapped);
	rig.ctx.ReleasePointer(second);
}

TEST(UIInputTest, ClaimDiesWithElementThatOwnsIt) {
	ClaimRig rig;
	u32 claim = rig.ctx.ClaimPointer(rig.viewport, CursorMode::Wrapped);
	ASSERT_NE(claim, 0u);

	rig.ctx.DestroyElement(rig.viewport);
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetPointerClaimMode(), CursorMode::Normal);

	rig.ctx.InjectInput(MouseMove(50, 50));
	Click(rig.ctx, 50, 50);
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.GetHovered(), rig.bystander);
	EXPECT_EQ(rig.bystanderClicks, 1);
}

TEST(UIInputTest, ClaimNeedsLiveOwnerToStartWith) {
	ClaimRig rig;
	rig.ctx.DestroyElement(rig.viewport);
	rig.ctx.Update();
	EXPECT_EQ(rig.ctx.ClaimPointer(rig.viewport, CursorMode::Wrapped), 0u);
	EXPECT_EQ(rig.ctx.ClaimPointer(UI::ElementHandle{}, CursorMode::Wrapped), 0u);
	EXPECT_EQ(rig.ctx.GetPointerClaimMode(), CursorMode::Normal);
}

TEST(UIInputTest, CommitHandlerMayBuild) {
	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::ElementHandle input = MakeFocusedInput(ctx, "");
	ASSERT_EQ(ctx.GetFocused(), input);

	str committed;
	ctx.Events(input).onTextCommit = [&](std::string_view text) {
		committed = str(text);
		for (u32 i = 0; i < 64; ++i) {
			UI::ElementHandle built = ctx.CreateElement();
			ctx.AppendChild(ctx.GetRoot(), built);
			ctx.SetText(built, Handle<FontAsset>{}, 14.0f, Color::White(), "rebuilt row");
		}
	};

	ctx.InjectInput(TextChar(U'h'));
	ctx.InjectInput(TextChar(U'i'));
	ctx.InjectInput(KeyPress(Key::Enter));
	ctx.Update();

	EXPECT_EQ(committed, "hi");
	EXPECT_EQ(ctx.GetTextString(input), "hi");
	ctx.InjectInput(TextChar(U'!'));
	ctx.InjectInput(KeyPress(Key::Escape));
	ctx.Update();
	EXPECT_EQ(ctx.GetTextString(input), "hi");
}

TEST(UIInputTest, FilterMayBuildDuringPaste) {
	if (!WindowSystem::IsInitialized()) {
		WindowSystem::Initialize();
	}
	{
		MockFontSystem fonts;
		UI::Context ctx(Desc(), &fonts);
		UI::ElementHandle input = MakeFocusedInput(ctx, "");
		ASSERT_EQ(ctx.GetFocused(), input);
		WindowSystem::SetClipboardText("pasted");

		ctx.Events(input).onTextFilter = [&](char32_t) {
			for (u32 i = 0; i < 8; ++i) {
				UI::ElementHandle built = ctx.CreateElement();
				ctx.AppendChild(ctx.GetRoot(), built);
				ctx.SetText(built, Handle<FontAsset>{}, 14.0f, Color::White(), "row");
			}
			return true;
		};

		ctx.InjectInput(KeyPress(Key::V, KeyMod::Ctrl));
		ctx.Update();
		EXPECT_EQ(ctx.GetTextString(input), "pasted");
	}
	WindowSystem::Terminate();
}

namespace {

	UI::ElementHandle MakeFocusedMultiline(UI::Context& ctx, std::string_view text, f32 width = 200.0f) {
		UI::Style root;
		root.alignItems = UI::Align::Start;
		ctx.SetStyle(ctx.GetRoot(), root);
		UI::ElementHandle input = ctx.CreateElement();
		ctx.AppendChild(ctx.GetRoot(), input);
		UI::Style style;
		style.width = UI::Length::Pixel(width);
		ctx.SetStyle(input, style);
		ctx.SetText(input, Handle<FontAsset>{}, 14.0f, Color::White(), text);
		ctx.SetTextInput(input, true);
		ctx.SetTextInputMultiline(input, true);
		ctx.Update();
		Click(ctx, 5, 5);
		ctx.InjectInput(KeyPress(Key::End, KeyMod::Ctrl));
		ctx.Update();
		return input;
	}

	void Type(UI::Context& ctx, std::u32string_view text) {
		for (const char32_t c : text) {
			ctx.InjectInput(TextChar(c));
		}
		ctx.Update();
	}

	void Press(UI::Context& ctx, Key key, KeyMod mods = KeyMod::None) {
		ctx.InjectInput(KeyPress(key, mods));
		ctx.Update();
	}

} // anonymous namespace

TEST(UIInputTest, MultilineEnterBreaksLine) {
	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::ElementHandle input = MakeFocusedMultiline(ctx, "");
	ASSERT_EQ(ctx.GetFocused(), input);
	u32 commits = 0;
	ctx.Events(input).onTextCommit = [&](std::string_view) { ++commits; };
	const f32 oneLine = ctx.GetLayout(input).borderBox.h;

	Type(ctx, U"ab");
	Press(ctx, Key::Enter);
	Type(ctx, U"cd");
	EXPECT_EQ(ctx.GetTextString(input), "ab\ncd");
	EXPECT_EQ(commits, 0u);
	EXPECT_NEAR(ctx.GetLayout(input).borderBox.h - oneLine, MockFontSystem::LINE_HEIGHT, 0.5f);

	Press(ctx, Key::Left, KeyMod::Shift);
	Press(ctx, Key::Enter);
	EXPECT_EQ(ctx.GetTextString(input), "ab\nc\n");
}

TEST(UIInputTest, UpDownKeepColumn) {
	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::ElementHandle input = MakeFocusedMultiline(ctx, "abcd\nx\nabcd");

	Press(ctx, Key::Home, KeyMod::Ctrl);
	for (int step = 0; step < 3; ++step) {
		Press(ctx, Key::Right);
	}
	Press(ctx, Key::Down);
	Press(ctx, Key::Down);
	Type(ctx, U"Z");
	EXPECT_EQ(ctx.GetTextString(input), "abcd\nx\nabcZd");

	Press(ctx, Key::Up);
	Type(ctx, U"Y");
	EXPECT_EQ(ctx.GetTextString(input), "abcd\nxY\nabcZd");

	Press(ctx, Key::Up);
	Press(ctx, Key::Up);
	Type(ctx, U"<");
	Press(ctx, Key::Down);
	Press(ctx, Key::Down);
	Press(ctx, Key::Down);
	Type(ctx, U">");
	EXPECT_EQ(ctx.GetTextString(input), "<abcd\nxY\nabcZd>");

	Press(ctx, Key::Home, KeyMod::Ctrl);
	Press(ctx, Key::Down);
	Press(ctx, Key::Home);
	Press(ctx, Key::Down, KeyMod::Shift);
	Type(ctx, U"-");
	EXPECT_EQ(ctx.GetTextString(input), "<abcd\n-abcZd>");
}

TEST(UIInputTest, HomeEndReachLineAndText) {
	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::ElementHandle input = MakeFocusedMultiline(ctx, "ab\ncd");

	Press(ctx, Key::Home);
	Type(ctx, U"[");
	Press(ctx, Key::End);
	Type(ctx, U"]");
	EXPECT_EQ(ctx.GetTextString(input), "ab\n[cd]");
	Press(ctx, Key::Home, KeyMod::Ctrl);
	Press(ctx, Key::End);
	Type(ctx, U"!");
	EXPECT_EQ(ctx.GetTextString(input), "ab!\n[cd]");

	MockFontSystem wrapFonts;
	UI::Context wrapped(Desc(), &wrapFonts);
	UI::ElementHandle field = MakeFocusedMultiline(wrapped, "abcdefgh", 60.0f);
	Press(wrapped, Key::Home, KeyMod::Ctrl);
	Press(wrapped, Key::End);
	Type(wrapped, U"|");
	const str text = wrapped.GetTextString(field);
	EXPECT_LT(text.find('|'), text.find('g'));
}

TEST(UIInputTest, ClickAndDragFindLineUnderPointer) {
	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::ElementHandle input = MakeFocusedMultiline(ctx, "ab\ncd");
	const UI::Rect content = ctx.GetLayout(input).content;
	auto at = [&](f32 x, f32 line) {
		return Vec2I(static_cast<i32>(content.x + x), static_cast<i32>(content.y + (line + 0.5f) * MockFontSystem::LINE_HEIGHT));
	};

	const Vec2I cd = at(12.0f, 1.0f);
	Click(ctx, cd.x, cd.y);
	Type(ctx, U"X");
	EXPECT_EQ(ctx.GetTextString(input), "ab\ncXd");

	const Vec2I from = at(12.0f, 0.0f);
	const Vec2I to = at(22.0f, 1.0f);
	ctx.InjectInput(MouseMove(from.x, from.y));
	ctx.InjectInput(MouseDown(from.x, from.y));
	ctx.Update();
	ctx.InjectInput(MouseMove((from.x + to.x) / 2, (from.y + to.y) / 2));
	ctx.Update();
	ctx.InjectInput(MouseMove(to.x, to.y));
	ctx.Update();
	ctx.InjectInput(MouseUp(to.x, to.y));
	ctx.Update();
	Type(ctx, U"-");
	EXPECT_EQ(ctx.GetTextString(input), "a-d");
}

TEST(UIInputTest, MultilineSelectionBands) {
	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::ElementHandle input = MakeFocusedMultiline(ctx, "abcd\nxy\nabc");
	Press(ctx, Key::Home, KeyMod::Ctrl);
	Press(ctx, Key::Right);
	Press(ctx, Key::End, KeyMod::Ctrl | KeyMod::Shift);

	UI::RenderList list;
	ctx.BuildRenderList(list);
	const UI::Rect content = ctx.GetLayout(input).content;
	Vector<UI::Rect> bands{GetMemoryResource(MemTag::UI)};
	for (const UI::UIQuad& quad : list.quads) {
		if (quad.uv.w == 0.0f && quad.color0.a > 0.1f && quad.color0.a < 0.9f &&
			std::abs(quad.rect.h - MockFontSystem::LINE_HEIGHT) < 0.5f) {
			bands.push_back(quad.rect);
		}
	}
	ASSERT_EQ(bands.size(), 3u);
	std::sort(bands.begin(), bands.end(), [](const UI::Rect& a, const UI::Rect& b) { return a.y < b.y; });
	for (u32 line = 0; line < 3; ++line) {
		EXPECT_NEAR(bands[line].y, content.y + static_cast<f32>(line) * MockFontSystem::LINE_HEIGHT, 0.5f);
	}
	EXPECT_NEAR(bands[0].x, content.x + 10.0f, 0.5f);
	EXPECT_NEAR(bands[0].w, 30.0f, 0.5f);
	EXPECT_NEAR(bands[1].w, 20.0f, 0.5f);
	EXPECT_NEAR(bands[2].w, 30.0f, 0.5f);
}

TEST(UIInputTest, ShortMultilineFollowsCaret) {
	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::ElementHandle input = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), input);
	ctx.SetStyle(input, FixedBox(200.0f, 2.0f * MockFontSystem::LINE_HEIGHT));
	ctx.SetText(input, Handle<FontAsset>{}, 14.0f, Color::White(), "1\n2\n3\n4\n5");
	ctx.SetTextInput(input, true);
	ctx.SetTextInputMultiline(input, true);
	ctx.Update();
	Click(ctx, 5, 5);
	Press(ctx, Key::End, KeyMod::Ctrl);

	const UI::Rect content = ctx.GetLayout(input).content;
	UI::Context::ShapedTextView view = ctx.GetShapedText(input);
	ASSERT_NE(view.run, nullptr);
	const f32 lastLineTop = view.originPx.y + 4.0f * MockFontSystem::LINE_HEIGHT;
	EXPECT_GE(lastLineTop, content.y - 0.5f);
	EXPECT_LE(lastLineTop + MockFontSystem::LINE_HEIGHT, content.y + content.h + 1.5f);
	EXPECT_LT(view.originPx.y, content.y);

	Press(ctx, Key::Home, KeyMod::Ctrl);
	view = ctx.GetShapedText(input);
	EXPECT_NEAR(view.originPx.y, content.y, 0.5f);
}

TEST(UIInputTest, MultilineWrapsCommitsOnBlur) {
	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::Style root;
	root.alignItems = UI::Align::Start;
	ctx.SetStyle(ctx.GetRoot(), root);
	UI::ElementHandle column = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), column);
	UI::Style columnStyle;
	columnStyle.flexDirection = UI::FlexDirection::Column;
	columnStyle.width = UI::Length::Pixel(80.0f);
	ctx.SetStyle(column, columnStyle);
	str committed;
	UI::Widgets::TextFieldDesc desc;
	desc.text = "a fairly long note";
	desc.multiline = true;
	desc.onCommit = [&](std::string_view text) { committed = str(text); };
	UI::ElementHandle field = UI::Widgets::TextField(ctx, column, desc);
	ctx.Update();
	UI::RenderList painted;
	ctx.BuildRenderList(painted);
	UI::Context::ShapedTextView view = ctx.GetShapedText(field);
	ASSERT_NE(view.run, nullptr);
	EXPECT_TRUE(view.run->multiline);
	EXPECT_GT(ctx.GetLayout(field).borderBox.h, 2.0f * MockFontSystem::LINE_HEIGHT);

	const UI::Rect box = ctx.GetLayout(field).borderBox;
	Click(ctx, static_cast<i32>(box.x + 4.0f), static_cast<i32>(box.y + 4.0f));
	ASSERT_EQ(ctx.GetFocused(), field);
	Press(ctx, Key::End, KeyMod::Ctrl);
	Press(ctx, Key::Enter);
	Type(ctx, U"!");
	EXPECT_TRUE(committed.empty());
	Click(ctx, 390, 290);
	EXPECT_EQ(committed, "a fairly long note\n!");
}

TEST(UIInputTest, PasteKeepsBreaksInMultiline) {
	if (!WindowSystem::IsInitialized()) {
		WindowSystem::Initialize();
	}
	{
		MockFontSystem fonts;
		UI::Context ctx(Desc(), &fonts);
		UI::ElementHandle input = MakeFocusedMultiline(ctx, "");
		WindowSystem::SetClipboardText("one\ntwo");
		Press(ctx, Key::V, KeyMod::Ctrl);
		EXPECT_EQ(ctx.GetTextString(input), "one\ntwo");
	}
	{
		MockFontSystem fonts;
		UI::Context ctx(Desc(), &fonts);
		UI::ElementHandle input = MakeFocusedInput(ctx, "");
		WindowSystem::SetClipboardText("one\ntwo");
		Press(ctx, Key::V, KeyMod::Ctrl);
		EXPECT_EQ(ctx.GetTextString(input), "onetwo");
	}
	WindowSystem::Terminate();
}

namespace {

	Vector<f32> PaintedGlyphXs(UI::Context& ctx) {
		ctx.Update();
		UI::RenderList list;
		ctx.BuildRenderList(list);
		Vector<f32> xs{GetMemoryResource(MemTag::UI)};
		for (const UI::UIQuad& quad : list.quads) {
			if (quad.uv.w > 0.0f) {
				xs.push_back(quad.rect.x);
			}
		}
		std::sort(xs.begin(), xs.end());
		return xs;
	}

} // anonymous namespace

TEST(UIInputTest, CompletionTakenByTabAtEnd) {
	MockFontSystem fonts;
	UI::Context ctx(Desc(), &fonts);
	UI::ElementHandle input = MakeFocusedInput(ctx, "");
	Type(ctx, U"mul");
	Vector<str> heard{GetMemoryResource(MemTag::UI)};
	ctx.Events(input).onTextChange = [&](std::string_view text) { heard.push_back(str(text)); };
	ctx.SetTextInputCompletion(input, "tiply", Color::White());

	const Vector<f32> shown = PaintedGlyphXs(ctx);
	ASSERT_EQ(shown.size(), 8u);
	for (size_t at = 1; at < shown.size(); ++at) {
		EXPECT_NEAR(shown[at] - shown[at - 1], MockFontSystem::ADVANCE, 0.5f);
	}

	Press(ctx, Key::Left);
	EXPECT_EQ(PaintedGlyphXs(ctx).size(), 3u);
	Press(ctx, Key::Tab);
	EXPECT_EQ(ctx.GetTextString(input), "mul");
	Press(ctx, Key::End);
	EXPECT_EQ(PaintedGlyphXs(ctx).size(), 8u);
	Press(ctx, Key::Tab, KeyMod::Shift);
	EXPECT_EQ(ctx.GetTextString(input), "mul");
	EXPECT_TRUE(heard.empty());

	Press(ctx, Key::Tab);
	EXPECT_EQ(ctx.GetTextString(input), "multiply");
	ASSERT_EQ(heard.size(), 1u);
	EXPECT_EQ(heard.front(), "multiply");
	EXPECT_EQ(PaintedGlyphXs(ctx).size(), 8u);
	Type(ctx, U"!");
	EXPECT_EQ(ctx.GetTextString(input), "multiply!");

	ctx.SetTextInputCompletion(input, "?", Color::White());
	Press(ctx, Key::Backspace);
	Press(ctx, Key::Tab);
	EXPECT_EQ(ctx.GetTextString(input), "multiply");
	ctx.SetTextInputCompletion(input, "?", Color::White());
	Press(ctx, Key::A, KeyMod::Ctrl);
	EXPECT_EQ(PaintedGlyphXs(ctx).size(), 8u);
	Press(ctx, Key::Tab);
	EXPECT_EQ(ctx.GetTextString(input), "multiply");
}
