#include "TestsPCH.h"

#include "Shalltear/Core/Input.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Window.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace Shalltear;

class InputTest : public ::testing::Test {
protected:
	void SetUp() override {
		WindowSystem::Initialize();
		_window = WindowSystem::Create(HiddenDesc());
		ASSERT_TRUE(_window.IsValid());
	}
	void TearDown() override {
		WindowSystem::Terminate();
	}

	static WindowDesc HiddenDesc() {
		WindowDesc desc;
		desc.title = "InputTest";
		desc.clientSize = {320, 240};
		desc.flags = WindowFlags::Hidden;
		return desc;
	}

#ifdef _WIN32
	HWND Hwnd(Handle<Window> window = {}) const {
		Handle<Window> target = window.IsValid() ? window : _window;
		return static_cast<HWND>(target->GetNativeHandle().hwnd);
	}

	static LPARAM KeyLParam(u16 scancode, bool extended = false, bool repeat = false, bool up = false) {
		LPARAM lParam = 1;
		lParam |= static_cast<LPARAM>(scancode & 0xFF) << 16;
		if (extended) {
			lParam |= static_cast<LPARAM>(1) << 24;
		}
		if (repeat || up) {
			lParam |= static_cast<LPARAM>(1) << 30;
		}
		if (up) {
			lParam |= static_cast<LPARAM>(1) << 31;
		}
		return lParam;
	}
#endif

	Handle<Window> _window;
};

#ifdef _WIN32

TEST_F(InputTest, PostedKeyDrivesStateAndQueue) {
	::PostMessageW(Hwnd(), WM_KEYDOWN, 'A', KeyLParam(0x1E));
	WindowSystem::PumpEvents();

	EXPECT_TRUE(Input::IsKeyDown(Key::A));
	EXPECT_TRUE(Input::WasKeyPressed(Key::A));
	EXPECT_FALSE(Input::WasKeyReleased(Key::A));

	bool found = false;
	for (const InputEvent& event : Input::GetFrameEvents()) {
		if (event.type == InputEventType::KeyDown && event.key == Key::A) {
			found = true;
			EXPECT_EQ(event.window, _window);
			EXPECT_EQ(event.scancode, 0x1E);
			EXPECT_FALSE(event.repeat);
		}
	}
	EXPECT_TRUE(found);

	WindowSystem::PumpEvents();
	EXPECT_TRUE(Input::IsKeyDown(Key::A));
	EXPECT_FALSE(Input::WasKeyPressed(Key::A));
	EXPECT_TRUE(Input::GetFrameEvents().empty());

	::PostMessageW(Hwnd(), WM_KEYUP, 'A', KeyLParam(0x1E, false, false, true));
	WindowSystem::PumpEvents();
	EXPECT_FALSE(Input::IsKeyDown(Key::A));
	EXPECT_TRUE(Input::WasKeyReleased(Key::A));
}

TEST_F(InputTest, OsRepeatIsFlaggedNotFreshPress) {
	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_F13, KeyLParam(0x64));
	WindowSystem::PumpEvents();
	ASSERT_TRUE(Input::WasKeyPressed(Key::F13));

	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_F13, KeyLParam(0x64, false, true));
	WindowSystem::PumpEvents();
	EXPECT_TRUE(Input::IsKeyDown(Key::F13));
	EXPECT_FALSE(Input::WasKeyPressed(Key::F13));

	auto events = Input::GetFrameEvents();
	ASSERT_EQ(events.size(), 1u);
	EXPECT_TRUE(events[0].repeat);
}

TEST_F(InputTest, ModifiersStampEventsAndPolledView) {
	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_CONTROL, KeyLParam(0x1D));
	::PostMessageW(Hwnd(), WM_KEYDOWN, 'S', KeyLParam(0x1F));
	WindowSystem::PumpEvents();

	EXPECT_TRUE(Input::IsKeyDown(Key::LeftCtrl));
	EXPECT_EQ(Input::GetKeyMods(), KeyMod::Ctrl);

	bool found = false;
	for (const InputEvent& event : Input::GetFrameEvents()) {
		if (event.type == InputEventType::KeyDown && event.key == Key::S) {
			found = true;
			EXPECT_TRUE(HasFlag(event.mods, KeyMod::Ctrl));
		}
	}
	EXPECT_TRUE(found);
}

TEST_F(InputTest, LeftRightModifiersDiscriminate) {
	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_SHIFT, KeyLParam(0x2A));
	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_SHIFT, KeyLParam(0x36));
	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_CONTROL, KeyLParam(0x1D, true));
	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_RETURN, KeyLParam(0x1C, true));
	WindowSystem::PumpEvents();

	EXPECT_TRUE(Input::IsKeyDown(Key::LeftShift));
	EXPECT_TRUE(Input::IsKeyDown(Key::RightShift));
	EXPECT_TRUE(Input::IsKeyDown(Key::RightCtrl));
	EXPECT_TRUE(Input::IsKeyDown(Key::KpEnter));
}

TEST_F(InputTest, TextEventsFuseSurrogatePairs) {
	::PostMessageW(Hwnd(), WM_CHAR, 'A', 1);
	::PostMessageW(Hwnd(), WM_CHAR, 0xD83D, 1);
	::PostMessageW(Hwnd(), WM_CHAR, 0xDE00, 1);
	::PostMessageW(Hwnd(), WM_CHAR, 0x08, 1);
	WindowSystem::PumpEvents();

	std::vector<char32_t> text;
	for (const InputEvent& event : Input::GetFrameEvents()) {
		if (event.type == InputEventType::Text) {
			text.push_back(event.codepoint);
		}
	}
	ASSERT_EQ(text.size(), 2u);
	EXPECT_EQ(text[0], U'A');
	EXPECT_EQ(text[1], static_cast<char32_t>(0x1F600));
}

TEST_F(InputTest, MouseMoveEnterButtonsAndPosition) {
	::PostMessageW(Hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(100, 50));
	::PostMessageW(Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(100, 50));
	WindowSystem::PumpEvents();

	auto events = Input::GetFrameEvents();
	ASSERT_GE(events.size(), 3u);
	EXPECT_EQ(events[0].type, InputEventType::MouseEnter);
	EXPECT_EQ(events[1].type, InputEventType::MouseMove);
	EXPECT_EQ(events[1].mousePosPx, Vec2I(100, 50));
	EXPECT_EQ(events[2].type, InputEventType::MouseDown);
	EXPECT_EQ(events[2].button, MouseButton::Left);

	EXPECT_TRUE(Input::IsMouseButtonDown(MouseButton::Left));
	EXPECT_TRUE(Input::WasMouseButtonPressed(MouseButton::Left));
	EXPECT_EQ(Input::GetMousePosition(_window), Vec2I(100, 50));

	::PostMessageW(Hwnd(), WM_LBUTTONUP, 0, MAKELPARAM(100, 50));
	WindowSystem::PumpEvents();
	EXPECT_FALSE(Input::IsMouseButtonDown(MouseButton::Left));
	EXPECT_TRUE(Input::WasMouseButtonReleased(MouseButton::Left));
}

TEST_F(InputTest, WheelAccumulatesPerPumpInNotches) {
	::PostMessageW(Hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(10, 10));
	::PostMessageW(Hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(10, 10));
	::PostMessageW(Hwnd(), WM_MOUSEHWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), MAKELPARAM(10, 10));
	WindowSystem::PumpEvents();

	Vec2 wheel = Input::GetMouseWheel();
	EXPECT_FLOAT_EQ(wheel.y, 2.0f);
	EXPECT_FLOAT_EQ(wheel.x, -1.0f);

	WindowSystem::PumpEvents();
	EXPECT_FLOAT_EQ(Input::GetMouseWheel().y, 0.0f);
}

TEST_F(InputTest, EventsRouteToTheirSourceWindow) {
	Handle<Window> second = WindowSystem::Create(HiddenDesc());
	ASSERT_TRUE(second.IsValid());

	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_F13, KeyLParam(0x64));
	::PostMessageW(Hwnd(second), WM_KEYDOWN, VK_F14, KeyLParam(0x65));
	WindowSystem::PumpEvents();

	std::vector<InputEvent> keys;
	for (const InputEvent& event : Input::GetFrameEvents()) {
		if (event.type == InputEventType::KeyDown) {
			keys.push_back(event);
		}
	}
	ASSERT_EQ(keys.size(), 2u);
	EXPECT_EQ(keys[0].window, _window);
	EXPECT_EQ(keys[1].window, second);

	InputEvent buffer[4];
	u32 count = Input::GetFrameEvents(_window, buffer);
	ASSERT_EQ(count, 1u);
	EXPECT_EQ(buffer[0].key, Key::F13);

	count = Input::GetFrameEvents(second, buffer);
	ASSERT_EQ(count, 1u);
	EXPECT_EQ(buffer[0].key, Key::F14);
}

TEST_F(InputTest, HeldModifierKeepsLaterChords) {
	InputActionMap actions;
	ActionId save = actions.RegisterAction("Save"_hash, ActionType::Button);
	InputBinding binding;
	binding.source = InputBinding::Source::Key;
	binding.code = static_cast<u16>(Key::S);
	binding.requiredMods = KeyMod::Ctrl;
	actions.Bind(save, binding);
	actions.Enable();

	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_MENU, KeyLParam(0x38));
	WindowSystem::PumpEvents();
	ASSERT_TRUE(Input::IsKeyDown(Key::LeftAlt));

	::PostMessageW(Hwnd(), WM_KILLFOCUS, 0, 0);
	WindowSystem::PumpEvents();

	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_CONTROL, KeyLParam(0x1D));
	::PostMessageW(Hwnd(), WM_KEYDOWN, 'S', KeyLParam(0x1F));
	WindowSystem::PumpEvents();
	actions.Evaluate();

	EXPECT_EQ(Input::GetKeyMods(), KeyMod::Ctrl);
	EXPECT_TRUE(actions.WasPerformed(save));
}

TEST_F(InputTest, DeactivationReleasesHeldKeysAsEvents) {
	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_CONTROL, KeyLParam(0x1D));
	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_F13, KeyLParam(0x64));
	WindowSystem::PumpEvents();
	ASSERT_TRUE(Input::IsKeyDown(Key::F13));

	::PostMessageW(Hwnd(), WM_KILLFOCUS, 0, 0);
	WindowSystem::PumpEvents();

	EXPECT_FALSE(Input::IsKeyDown(Key::LeftCtrl));
	EXPECT_FALSE(Input::IsKeyDown(Key::F13));
	EXPECT_EQ(Input::GetKeyMods(), KeyMod::None);

	EXPECT_TRUE(Input::WasKeyReleased(Key::LeftCtrl));
	EXPECT_TRUE(Input::WasKeyReleased(Key::F13));
	bool sawF13 = false;
	for (const InputEvent& event : Input::GetFrameEvents()) {
		if (event.type == InputEventType::KeyUp && event.key == Key::F13) {
			sawF13 = true;
			EXPECT_EQ(event.window, _window);
		}
	}
	EXPECT_TRUE(sawF13);
}

TEST_F(InputTest, DeactivationReleasesHeldMouseButtons) {
	::PostMessageW(Hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(100, 50));
	::PostMessageW(Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(100, 50));
	WindowSystem::PumpEvents();
	ASSERT_TRUE(Input::IsMouseButtonDown(MouseButton::Left));

	::PostMessageW(Hwnd(), WM_KILLFOCUS, 0, 0);
	WindowSystem::PumpEvents();

	EXPECT_FALSE(Input::IsMouseButtonDown(MouseButton::Left));
	EXPECT_TRUE(Input::WasMouseButtonReleased(MouseButton::Left));
}

TEST_F(InputTest, FocusMovingBetweenWindowsKeepsHeldKeys) {
	Handle<Window> second = WindowSystem::Create(HiddenDesc());
	ASSERT_TRUE(second.IsValid());

	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_SHIFT, KeyLParam(0x2A));
	WindowSystem::PumpEvents();
	ASSERT_TRUE(Input::IsKeyDown(Key::LeftShift));

	::PostMessageW(Hwnd(), WM_KILLFOCUS, reinterpret_cast<WPARAM>(Hwnd(second)), 0);
	WindowSystem::PumpEvents();

	EXPECT_TRUE(Input::IsKeyDown(Key::LeftShift));
	EXPECT_EQ(Input::GetKeyMods(), KeyMod::Shift);
}

TEST_F(InputTest, ButtonActionPressAndPerformEdge) {
	InputActionMap actions;
	ActionId jump = actions.RegisterAction("Jump"_hash, ActionType::Button);
	actions.Bind(jump, InputBinding{InputBinding::Source::Key, static_cast<u16>(Key::Space)});
	actions.Enable();

	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_SPACE, KeyLParam(0x39));
	WindowSystem::PumpEvents();
	actions.Evaluate();
	EXPECT_TRUE(actions.IsPressed(jump));
	EXPECT_TRUE(actions.WasPerformed(jump));

	WindowSystem::PumpEvents();
	actions.Evaluate();
	EXPECT_TRUE(actions.IsPressed(jump));
	EXPECT_FALSE(actions.WasPerformed(jump));
}

TEST_F(InputTest, ChordCompletedInsideOnePumpStillPerforms) {
	InputActionMap actions;
	ActionId save = actions.RegisterAction("Save"_hash, ActionType::Button);
	InputBinding binding;
	binding.source = InputBinding::Source::Key;
	binding.code = static_cast<u16>(Key::S);
	binding.requiredMods = KeyMod::Ctrl;
	actions.Bind(save, binding);
	actions.Enable();

	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_CONTROL, KeyLParam(0x1D));
	::PostMessageW(Hwnd(), WM_KEYDOWN, 'S', KeyLParam(0x1F));
	::PostMessageW(Hwnd(), WM_KEYUP, 'S', KeyLParam(0x1F, false, false, true));
	::PostMessageW(Hwnd(), WM_KEYUP, VK_CONTROL, KeyLParam(0x1D, false, false, true));
	WindowSystem::PumpEvents();
	actions.Evaluate();

	EXPECT_FALSE(actions.IsPressed(save));
	EXPECT_TRUE(actions.WasPerformed(save));

	WindowSystem::PumpEvents();
	actions.Evaluate();
	EXPECT_FALSE(actions.WasPerformed(save));
}

TEST_F(InputTest, ChordModsReadAtPress) {
	InputActionMap actions;
	ActionId save = actions.RegisterAction("Save"_hash, ActionType::Button);
	InputBinding binding;
	binding.source = InputBinding::Source::Key;
	binding.code = static_cast<u16>(Key::S);
	binding.requiredMods = KeyMod::Ctrl;
	actions.Bind(save, binding);
	actions.Enable();

	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_CONTROL, KeyLParam(0x1D));
	::PostMessageW(Hwnd(), WM_KEYDOWN, 'S', KeyLParam(0x1F));
	::PostMessageW(Hwnd(), WM_KEYUP, VK_CONTROL, KeyLParam(0x1D, false, false, true));
	WindowSystem::PumpEvents();
	actions.Evaluate();

	ASSERT_EQ(Input::GetKeyMods(), KeyMod::None);
	EXPECT_TRUE(actions.WasPerformed(save));
}

TEST_F(InputTest, Axis2DCompositeSumsBindings) {
	InputActionMap actions;
	ActionId move = actions.RegisterAction("Move"_hash, ActionType::Axis2D);
	auto bind = [&actions, move](Key key, u8 component, f32 scale) {
		InputBinding binding;
		binding.source = InputBinding::Source::Key;
		binding.code = static_cast<u16>(key);
		binding.component = component;
		binding.scale = scale;
		actions.Bind(move, binding);
	};
	bind(Key::D, 0, 1.0f);
	bind(Key::A, 0, -1.0f);
	bind(Key::W, 1, 1.0f);
	bind(Key::S, 1, -1.0f);
	actions.Enable();

	::PostMessageW(Hwnd(), WM_KEYDOWN, 'W', KeyLParam(0x11));
	::PostMessageW(Hwnd(), WM_KEYDOWN, 'D', KeyLParam(0x20));
	WindowSystem::PumpEvents();
	actions.Evaluate();
	EXPECT_EQ(actions.GetAxis2D(move), Vec2(1.0f, 1.0f));

	::PostMessageW(Hwnd(), WM_KEYDOWN, 'S', KeyLParam(0x1F));
	WindowSystem::PumpEvents();
	actions.Evaluate();
	EXPECT_EQ(actions.GetAxis2D(move), Vec2(1.0f, 0.0f));
}

TEST_F(InputTest, RequiredModsGateBindings) {
	InputActionMap actions;
	ActionId save = actions.RegisterAction("Save"_hash, ActionType::Button);
	InputBinding binding;
	binding.source = InputBinding::Source::Key;
	binding.code = static_cast<u16>(Key::S);
	binding.requiredMods = KeyMod::Ctrl;
	actions.Bind(save, binding);
	actions.Enable();

	::PostMessageW(Hwnd(), WM_KEYDOWN, 'S', KeyLParam(0x1F));
	WindowSystem::PumpEvents();
	actions.Evaluate();
	EXPECT_FALSE(actions.IsPressed(save));

	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_CONTROL, KeyLParam(0x1D));
	WindowSystem::PumpEvents();
	actions.Evaluate();
	EXPECT_TRUE(actions.IsPressed(save));
}

TEST_F(InputTest, ExactModsSeparatesOverlappingChords) {
	InputActionMap actions;
	auto chord = [](Key key, KeyMod mods) {
		InputBinding binding;
		binding.source = InputBinding::Source::Key;
		binding.code = static_cast<u16>(key);
		binding.requiredMods = mods;
		binding.exactMods = true;
		return binding;
	};
	ActionId play = actions.RegisterAction("Play"_hash, ActionType::Button);
	actions.Bind(play, chord(Key::P, KeyMod::Ctrl));
	ActionId pause = actions.RegisterAction("Pause"_hash, ActionType::Button);
	actions.Bind(pause, chord(Key::P, KeyMod::Ctrl | KeyMod::Shift));
	actions.Enable();

	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_CONTROL, KeyLParam(0x1D));
	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_SHIFT, KeyLParam(0x2A));
	::PostMessageW(Hwnd(), WM_KEYDOWN, 'P', KeyLParam(0x19));
	WindowSystem::PumpEvents();
	actions.Evaluate();

	EXPECT_TRUE(actions.WasPerformed(pause));
	EXPECT_FALSE(actions.WasPerformed(play));
}

TEST_F(InputTest, RequiredModsRemainMinimumWithoutExactMods) {
	InputActionMap actions;
	ActionId sprint = actions.RegisterAction("Sprint"_hash, ActionType::Button);
	InputBinding binding;
	binding.source = InputBinding::Source::Key;
	binding.code = static_cast<u16>(Key::P);
	binding.requiredMods = KeyMod::Ctrl;
	actions.Bind(sprint, binding);
	actions.Enable();

	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_CONTROL, KeyLParam(0x1D));
	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_SHIFT, KeyLParam(0x2A));
	::PostMessageW(Hwnd(), WM_KEYDOWN, 'P', KeyLParam(0x19));
	WindowSystem::PumpEvents();
	actions.Evaluate();

	EXPECT_TRUE(actions.IsPressed(sprint));
	EXPECT_TRUE(actions.WasPerformed(sprint));
}

TEST_F(InputTest, RegisterActionIsIdempotentByName) {
	InputActionMap actions;
	ActionId first = actions.RegisterAction("Fire"_hash, ActionType::Button);
	ActionId second = actions.RegisterAction("Fire"_hash, ActionType::Button);
	EXPECT_EQ(first, second);
	EXPECT_EQ(actions.GetActionCount(), 1u);
	EXPECT_EQ(actions.FindAction("Fire"_hash), first);
	EXPECT_EQ(actions.FindAction("Missing"_hash), INVALID_ACTION_ID);
}

TEST_F(InputTest, MapEvaluatesNothingUntilEnabled) {
	InputActionMap actions;
	ActionId jump = actions.RegisterAction("Jump"_hash, ActionType::Button);
	actions.Bind(jump, InputBinding{InputBinding::Source::Key, static_cast<u16>(Key::Space)});

	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_SPACE, KeyLParam(0x39));
	WindowSystem::PumpEvents();
	actions.Evaluate();

	ASSERT_TRUE(Input::IsKeyDown(Key::Space));
	EXPECT_FALSE(actions.IsPressed(jump));
	EXPECT_FALSE(actions.WasPerformed(jump));

	actions.Enable();
	WindowSystem::PumpEvents();
	actions.Evaluate();
	EXPECT_TRUE(actions.IsPressed(jump));
}

TEST_F(InputTest, DisabledMapIgnoresChord) {
	InputActionMap actions;
	ActionId save = actions.RegisterAction("Save"_hash, ActionType::Button);
	InputBinding binding;
	binding.source = InputBinding::Source::Key;
	binding.code = static_cast<u16>(Key::S);
	binding.requiredMods = KeyMod::Ctrl;
	actions.Bind(save, binding);

	auto pressCtrlS = [this] {
		::PostMessageW(Hwnd(), WM_KEYDOWN, VK_CONTROL, KeyLParam(0x1D));
		::PostMessageW(Hwnd(), WM_KEYDOWN, 'S', KeyLParam(0x1F));
		::PostMessageW(Hwnd(), WM_KEYUP, 'S', KeyLParam(0x1F, false, false, true));
		::PostMessageW(Hwnd(), WM_KEYUP, VK_CONTROL, KeyLParam(0x1D, false, false, true));
		WindowSystem::PumpEvents();
	};

	pressCtrlS();
	actions.Evaluate();
	EXPECT_FALSE(actions.WasPerformed(save));

	actions.Enable();
	pressCtrlS();
	actions.Evaluate();
	EXPECT_TRUE(actions.WasPerformed(save));
}

TEST_F(InputTest, DisableClearsEvaluatedStateWithoutAnotherEvaluate) {
	InputActionMap actions;
	ActionId move = actions.RegisterAction("Move"_hash, ActionType::Axis2D);
	actions.Bind(move, InputBinding{InputBinding::Source::Key, static_cast<u16>(Key::D)});
	actions.Enable();

	::PostMessageW(Hwnd(), WM_KEYDOWN, 'D', KeyLParam(0x20));
	WindowSystem::PumpEvents();
	actions.Evaluate();
	ASSERT_EQ(actions.GetAxis2D(move), Vec2(1.0f, 0.0f));

	actions.Disable();
	EXPECT_EQ(actions.GetAxis2D(move), Vec2());
	EXPECT_FALSE(actions.IsPressed(move));

	WindowSystem::PumpEvents();
	actions.Evaluate();
	ASSERT_TRUE(Input::IsKeyDown(Key::D));
	EXPECT_EQ(actions.GetAxis2D(move), Vec2());
}

TEST_F(InputTest, EnablingOneMapLeavesTheOtherAlone) {
	InputActionMap first;
	ActionId a = first.RegisterAction("A"_hash, ActionType::Button);
	first.Bind(a, InputBinding{InputBinding::Source::Key, static_cast<u16>(Key::F13)});
	InputActionMap second;
	ActionId b = second.RegisterAction("B"_hash, ActionType::Button);
	second.Bind(b, InputBinding{InputBinding::Source::Key, static_cast<u16>(Key::F13)});

	first.Enable();
	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_F13, KeyLParam(0x64));
	WindowSystem::PumpEvents();
	first.Evaluate();
	second.Evaluate();
	EXPECT_TRUE(first.IsPressed(a));
	EXPECT_FALSE(second.IsPressed(b));

	second.Enable();
	WindowSystem::PumpEvents();
	first.Evaluate();
	second.Evaluate();
	EXPECT_TRUE(first.IsPressed(a));
	EXPECT_TRUE(second.IsPressed(b));

	first.Disable();
	WindowSystem::PumpEvents();
	first.Evaluate();
	second.Evaluate();
	EXPECT_FALSE(first.IsPressed(a));
	EXPECT_TRUE(second.IsPressed(b));
}

TEST_F(InputTest, ReEnableInventsNoPress) {
	InputActionMap actions;
	ActionId look = actions.RegisterAction("Look"_hash, ActionType::Button);
	InputBinding binding;
	binding.source = InputBinding::Source::MouseDelta;
	binding.code = 0;
	actions.Bind(look, binding);
	actions.Enable();

	i32 x = 140;
	auto moveAndPump = [this, &x] {
		::PostMessageW(Hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(x, 50));
		x += 40;
		WindowSystem::PumpEvents();
	};

	::PostMessageW(Hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(100, 50));
	WindowSystem::PumpEvents();
	actions.Evaluate();
	WindowSystem::PumpEvents();
	actions.Evaluate();
	ASSERT_FALSE(actions.IsPressed(look));

	moveAndPump();
	actions.Evaluate();
	ASSERT_TRUE(actions.IsPressed(look));
	ASSERT_TRUE(actions.WasPerformed(look));

	actions.Disable();
	moveAndPump();
	actions.Evaluate();

	actions.Enable();
	moveAndPump();
	actions.Evaluate();
	EXPECT_TRUE(actions.IsPressed(look));
	EXPECT_FALSE(actions.WasPerformed(look));
}

TEST_F(InputTest, AxisIsOnePumpsTravel) {
	InputActionMap actions;
	ActionId look = actions.RegisterAction("Look"_hash, ActionType::Axis2D);
	InputBinding binding;
	binding.source = InputBinding::Source::MouseDelta;
	binding.code = 0;
	actions.Bind(look, binding);
	actions.Enable();

	::PostMessageW(Hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(100, 50));
	WindowSystem::PumpEvents();
	actions.Evaluate();

	i32 x = 100;
	for (int pump = 0; pump < 20; ++pump) {
		x += 10;
		::PostMessageW(Hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(x, 50));
		WindowSystem::PumpEvents();
	}
	actions.Evaluate();

	EXPECT_FLOAT_EQ(actions.GetAxis2D(look).x, 10.0f);
}

TEST_F(InputTest, DeltaCodePicksAxisAndTarget) {
	InputActionMap actions;
	ActionId look = actions.RegisterAction("Look"_hash, ActionType::Axis2D);
	InputBinding binding;
	binding.source = InputBinding::Source::MouseDelta;
	binding.code = 1;
	binding.component = 0;
	actions.Bind(look, binding);
	actions.Enable();

	::PostMessageW(Hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(100, 50));
	WindowSystem::PumpEvents();
	actions.Evaluate();

	::PostMessageW(Hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(110, 80));
	WindowSystem::PumpEvents();
	actions.Evaluate();

	EXPECT_FLOAT_EQ(actions.GetAxis2D(look).x, 30.0f);
	EXPECT_FLOAT_EQ(actions.GetAxis2D(look).y, 0.0f);
}

TEST_F(InputTest, WheelBindingDrivesSignedAxis1D) {
	InputActionMap actions;
	ActionId hotbar = actions.RegisterAction("Hotbar"_hash, ActionType::Axis1D);
	InputBinding binding;
	binding.source = InputBinding::Source::MouseWheel;
	binding.code = 1;
	actions.Bind(hotbar, binding);
	actions.Enable();

	::PostMessageW(Hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(10, 10));
	::PostMessageW(Hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(10, 10));
	WindowSystem::PumpEvents();
	actions.Evaluate();
	EXPECT_FLOAT_EQ(actions.GetAxis1D(hotbar), 2.0f);

	::PostMessageW(Hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), MAKELPARAM(10, 10));
	WindowSystem::PumpEvents();
	actions.Evaluate();
	EXPECT_FLOAT_EQ(actions.GetAxis1D(hotbar), -1.0f);

	WindowSystem::PumpEvents();
	actions.Evaluate();
	EXPECT_FLOAT_EQ(actions.GetAxis1D(hotbar), 0.0f);
}

TEST_F(InputTest, HorizontalWheelIsBindableAxis) {
	InputActionMap actions;
	ActionId pan = actions.RegisterAction("Pan"_hash, ActionType::Axis1D);
	InputBinding binding;
	binding.source = InputBinding::Source::MouseWheel;
	binding.code = 0;
	actions.Bind(pan, binding);
	actions.Enable();

	::PostMessageW(Hwnd(), WM_MOUSEHWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(10, 10));
	::PostMessageW(Hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), MAKELPARAM(10, 10));
	WindowSystem::PumpEvents();
	actions.Evaluate();

	EXPECT_FLOAT_EQ(actions.GetAxis1D(pan), 1.0f);
}

TEST_F(InputTest, ButtonOnWheelFiresOnEitherDirection) {
	InputActionMap actions;
	ActionId cycle = actions.RegisterAction("Cycle"_hash, ActionType::Button);
	InputBinding binding;
	binding.source = InputBinding::Source::MouseWheel;
	binding.code = 1;
	actions.Bind(cycle, binding);
	actions.Enable();

	WindowSystem::PumpEvents();
	actions.Evaluate();
	ASSERT_FALSE(actions.IsPressed(cycle));

	::PostMessageW(Hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(10, 10));
	WindowSystem::PumpEvents();
	actions.Evaluate();
	EXPECT_TRUE(actions.IsPressed(cycle));
	EXPECT_TRUE(actions.WasPerformed(cycle));

	WindowSystem::PumpEvents();
	actions.Evaluate();
	ASSERT_FALSE(actions.IsPressed(cycle));

	::PostMessageW(Hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(0, static_cast<WORD>(-WHEEL_DELTA)), MAKELPARAM(10, 10));
	WindowSystem::PumpEvents();
	actions.Evaluate();
	EXPECT_TRUE(actions.IsPressed(cycle));
	EXPECT_TRUE(actions.WasPerformed(cycle));
}

TEST_F(InputTest, ReadingOnePumpTwiceCountsItTwice) {
	InputActionMap actions;
	ActionId look = actions.RegisterAction("Look"_hash, ActionType::Axis2D);
	InputBinding binding;
	binding.source = InputBinding::Source::MouseDelta;
	binding.code = 0;
	actions.Bind(look, binding);
	ActionId fire = actions.RegisterAction("Fire"_hash, ActionType::Button);
	actions.Bind(fire, InputBinding{InputBinding::Source::MouseButton, static_cast<u16>(MouseButton::Left)});
	actions.Enable();

	::PostMessageW(Hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(100, 50));
	WindowSystem::PumpEvents();
	actions.Evaluate();

	::PostMessageW(Hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(110, 50));
	::PostMessageW(Hwnd(), WM_LBUTTONDOWN, 0, MAKELPARAM(110, 50));
	WindowSystem::PumpEvents();

	f32 travel = 0.0f;
	u32 edges = 0;
	for (int step = 0; step < 3; ++step) {
		actions.Evaluate();
		travel += actions.GetAxis2D(look).x;
		if (actions.WasPerformed(fire)) {
			++edges;
		}
	}

	EXPECT_FLOAT_EQ(travel, 30.0f);
	EXPECT_EQ(edges, 3u);
}

namespace {

	class CadenceLogSink : public ILogSink {
	public:
		static constexpr const char* SENTINEL = "cadence-log-sink-sentinel";

		void ReceiveLog(const LogEvent& event) override {
			std::lock_guard<std::mutex> lock(_mutex);
			if (event.message.find(SENTINEL) != str::npos) {
				_armed = true;
				return;
			}
			if (!_armed) {
				return;
			}
			_messages.push_back(str(event.message));
		}
		void Flush() override {}

		bool Armed() const {
			std::lock_guard<std::mutex> lock(_mutex);
			return _armed;
		}

		u32 CountMatching(std::string_view needle) const {
			std::lock_guard<std::mutex> lock(_mutex);
			u32 count = 0;
			for (const str& message : _messages) {
				if (message.find(needle) != str::npos) {
					++count;
				}
			}
			return count;
		}
	private:
		mutable std::mutex _mutex;
		std::vector<str> _messages;
		bool _armed = false;
	};

} // namespace

class InputCadenceLogFixture : public InputTest {
protected:
	void SetUp() override {
		JobSystem::Initialize();
		LogManager::Initialize({false, false});
		LogManager::SetGlobalLevel(LogLevel::Trace);
		_sink = std::make_shared<CadenceLogSink>();
		LogManager::AddSink(_sink);

		InputTest::SetUp();
		ASSERT_FALSE(HasFatalFailure());
		for (int attempt = 0; attempt < 200 && !_sink->Armed(); ++attempt) {
			SLT_LOG_INFO("Input", CadenceLogSink::SENTINEL);
			::Sleep(1);
		}
		ASSERT_TRUE(_sink->Armed());
	}

	void TearDown() override {
		InputTest::TearDown();
		LogManager::Terminate();
		_sink.reset();
		JobSystem::Terminate();
	}

	void Drain() { LogManager::Terminate(); }

	u32 Saying(std::string_view needle) const { return _sink->CountMatching(needle); }
	u32 Complaints() const { return Saying("InputActionMap::Evaluate()"); }

	void BuildMap(InputActionMap& map) {
		ActionId look = map.RegisterAction("Look"_hash, ActionType::Axis2D);
		InputBinding binding;
		binding.source = InputBinding::Source::MouseDelta;
		binding.code = 0;
		map.Bind(look, binding);
		map.Enable();
	}

	std::shared_ptr<CadenceLogSink> _sink;
};

TEST_F(InputCadenceLogFixture, EvaluatingOncePerPumpSaysNothing) {
	InputActionMap actions;
	BuildMap(actions);
	for (int frame = 0; frame < 10; ++frame) {
		WindowSystem::PumpEvents();
		actions.Evaluate();
	}

	Drain();
	EXPECT_EQ(Complaints(), 0u);
}

TEST_F(InputCadenceLogFixture, SkippingPumpsIsReportedWithTheCount) {
	InputActionMap actions;
	BuildMap(actions);
	WindowSystem::PumpEvents();
	actions.Evaluate();

	for (int pump = 0; pump < 20; ++pump) {
		WindowSystem::PumpEvents();
	}
	actions.Evaluate();

	Drain();
	EXPECT_EQ(Saying("ran once in 20 input pumps"), 1u);
}

TEST_F(InputCadenceLogFixture, ReadingOnePumpTwiceIsReported) {
	InputActionMap actions;
	BuildMap(actions);
	WindowSystem::PumpEvents();
	actions.Evaluate();
	actions.Evaluate();

	Drain();
	EXPECT_EQ(Saying("more than once on one input pump"), 1u);
}

TEST_F(InputCadenceLogFixture, ADisabledStretchIsNotASkip) {
	InputActionMap actions;
	BuildMap(actions);
	WindowSystem::PumpEvents();
	actions.Evaluate();

	actions.Disable();
	for (int pump = 0; pump < 20; ++pump) {
		WindowSystem::PumpEvents();
		actions.Evaluate();
	}
	actions.Enable();

	WindowSystem::PumpEvents();
	actions.Evaluate();
	WindowSystem::PumpEvents();
	actions.Evaluate();

	Drain();
	EXPECT_EQ(Complaints(), 0u);
}

TEST_F(InputCadenceLogFixture, WrongCadenceReportedOnce) {
	InputActionMap actions;
	BuildMap(actions);
	WindowSystem::PumpEvents();
	actions.Evaluate();

	for (int tick = 0; tick < 5; ++tick) {
		WindowSystem::PumpEvents();
		WindowSystem::PumpEvents();
		actions.Evaluate();
	}

	Drain();
	EXPECT_EQ(Complaints(), 1u);
}

TEST_F(InputTest, ApplicationFocusFollowsTheRealFocusMessages) {
	::PostMessageW(Hwnd(), WM_SETFOCUS, 0, 0);
	WindowSystem::PumpEvents();
	EXPECT_EQ(WindowSystem::GetFocusedWindow(), _window);

	::PostMessageW(Hwnd(), WM_KILLFOCUS, 0, 0);
	WindowSystem::PumpEvents();
	EXPECT_FALSE(WindowSystem::GetFocusedWindow().IsValid());

	Handle<Window> second = WindowSystem::Create(HiddenDesc());
	ASSERT_TRUE(second.IsValid());
	::PostMessageW(Hwnd(), WM_SETFOCUS, 0, 0);
	WindowSystem::PumpEvents();
	ASSERT_TRUE(WindowSystem::GetFocusedWindow().IsValid());

	::PostMessageW(Hwnd(), WM_KILLFOCUS, reinterpret_cast<WPARAM>(Hwnd(second)), 0);
	::PostMessageW(Hwnd(second), WM_SETFOCUS, 0, 0);
	WindowSystem::PumpEvents();
	EXPECT_EQ(WindowSystem::GetFocusedWindow(), second);
}

#endif

TEST(InputCodesTest, NamesRoundTripEveryValue) {
	for (u16 i = 0; i < static_cast<u16>(Key::Count); ++i) {
		Key key = static_cast<Key>(i);
		EXPECT_EQ(KeyFromString(ToString(key)), key);
	}
	for (u8 i = 0; i < static_cast<u8>(MouseButton::Count); ++i) {
		MouseButton button = static_cast<MouseButton>(i);
		EXPECT_EQ(MouseButtonFromString(ToString(button)), button);
	}
	for (u8 i = 0; i < static_cast<u8>(GamepadButton::Count); ++i) {
		GamepadButton button = static_cast<GamepadButton>(i);
		EXPECT_EQ(GamepadButtonFromString(ToString(button)), button);
	}
	for (u8 i = 0; i < static_cast<u8>(GamepadAxis::Count); ++i) {
		GamepadAxis axis = static_cast<GamepadAxis>(i);
		EXPECT_EQ(GamepadAxisFromString(ToString(axis)), axis);
	}
}

TEST(InputCodesTest, OutOfRangeQueriesAreSafe) {
	EXPECT_STREQ(ToString(static_cast<Key>(0xFFFF)), "Unknown");
	EXPECT_FALSE(Input::GetGamepad(200).connected);
	EXPECT_FALSE(Input::IsKeyDown(Key::Count));
}

TEST_F(InputTest, CtrlChordDeliversNoText) {
	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_CONTROL, KeyLParam(0x1D));
	::PostMessageW(Hwnd(), WM_KEYDOWN, 'A', KeyLParam(0x1E));
	WindowSystem::PumpEvents();

	bool chord = false;
	std::vector<char32_t> text;
	for (const InputEvent& event : Input::GetFrameEvents()) {
		if (event.type == InputEventType::KeyDown && event.key == Key::A) {
			chord = true;
			EXPECT_TRUE(HasFlag(event.mods, KeyMod::Ctrl));
		}
		if (event.type == InputEventType::Text) {
			text.push_back(event.codepoint);
		}
	}
	EXPECT_TRUE(chord);
	EXPECT_TRUE(text.empty());
}

TEST_F(InputTest, AltGrTextSurvivesCtrlChordGuard) {
	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_CONTROL, KeyLParam(0x1D));
	::PostMessageW(Hwnd(), WM_KEYDOWN, VK_MENU, KeyLParam(0x38, true));
	::PostMessageW(Hwnd(), WM_CHAR, '@', 1);
	WindowSystem::PumpEvents();

	std::vector<char32_t> text;
	for (const InputEvent& event : Input::GetFrameEvents()) {
		if (event.type == InputEventType::Text) {
			text.push_back(event.codepoint);
		}
	}
	ASSERT_EQ(text.size(), 1u);
	EXPECT_EQ(text[0], U'@');
}

TEST_F(InputTest, PlainTextIsUnaffectedByChordGuard) {
	::PostMessageW(Hwnd(), WM_CHAR, 'k', 1);
	WindowSystem::PumpEvents();

	std::vector<char32_t> text;
	for (const InputEvent& event : Input::GetFrameEvents()) {
		if (event.type == InputEventType::Text) {
			text.push_back(event.codepoint);
		}
	}
	ASSERT_EQ(text.size(), 1u);
	EXPECT_EQ(text[0], U'k');
}
