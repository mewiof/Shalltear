#include "TestsPCH.h"

#include "Shalltear/Core/Input.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Scene/Behavior.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Core/Window.h"

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

using namespace Shalltear;

namespace {

	class RouteProbeBehavior : public Behavior {
	public:
		SLT_REFLECT_COMPONENT(RouteProbeBehavior, Behavior)
		SLT_END_REFLECT()

		bool keyDown = false;
		bool keyPressed = false;
		KeyMod mods = KeyMod::None;
		u32 queuedEvents = 0;
		u32 queuedPointerEvents = 0;
		bool mouseDown = false;
		bool mousePressed = false;
		Vec2 mouseDelta;
		Vec2 wheel;
		Vec2I mousePosition;
		const GamepadState* gamepad = nullptr;

		InputActionMap map;
		ActionId fire = INVALID_ACTION_ID;
		ActionId hotbar = INVALID_ACTION_ID;
		ActionId shoot = INVALID_ACTION_ID;
		bool mapPressed = false;
		bool mapPerformed = false;
		f32 hotbarAxis = 0.0f;
		bool shootPerformed = false;
	protected:
		void OnUpdate() override {
			keyDown = Input::IsKeyDown(Key::G);
			keyPressed = Input::WasKeyPressed(Key::G);
			mods = Input::GetKeyMods();
			queuedEvents = static_cast<u32>(Input::GetFrameEvents().size());
			queuedPointerEvents = 0;
			for (const InputEvent& event : Input::GetFrameEvents()) {
				if (event.type == InputEventType::MouseMove || event.type == InputEventType::MouseDown ||
					event.type == InputEventType::MouseUp || event.type == InputEventType::MouseWheel) {
					++queuedPointerEvents;
				}
			}
			mouseDown = Input::IsMouseButtonDown(MouseButton::Left);
			mousePressed = Input::WasMouseButtonPressed(MouseButton::Left);
			mouseDelta = Input::GetMouseDelta();
			wheel = Input::GetMouseWheel();
			mousePosition = Input::GetMousePosition();
			gamepad = &Input::GetGamepad(0);

			map.Evaluate();
			mapPressed = map.IsPressed(fire);
			mapPerformed = map.WasPerformed(fire);
			hotbarAxis = map.GetAxis1D(hotbar);
			shootPerformed = map.WasPerformed(shoot);
		}
	};

	SLT_REGISTER_TYPE(RouteProbeBehavior)

} // namespace

#ifdef _WIN32

class GameplayInputRouteFixture : public ::testing::Test {
protected:
	void SetUp() override {
		WindowSystem::Initialize();
		_window = WindowSystem::Create(HiddenDesc());
		ASSERT_TRUE(_window.IsValid());
		SceneSystem::Initialize();
		_scene = SceneSystem::CreateScene("GameplayInputRouteTest");
		Handle<SceneObject> object = SceneSystem::CreateObject("Probe", _scene);
		_probe = object->AddComponent<RouteProbeBehavior>();
		ASSERT_TRUE(_probe.IsValid());

		_probe->fire = _probe->map.RegisterAction("Fire"_hash, ActionType::Button);
		InputBinding binding;
		binding.source = InputBinding::Source::Key;
		binding.code = static_cast<u16>(Key::G);
		_probe->map.Bind(_probe->fire, binding);

		_probe->hotbar = _probe->map.RegisterAction("Hotbar"_hash, ActionType::Axis1D);
		InputBinding wheelBinding;
		wheelBinding.source = InputBinding::Source::MouseWheel;
		wheelBinding.code = 1;
		_probe->map.Bind(_probe->hotbar, wheelBinding);

		_probe->shoot = _probe->map.RegisterAction("Shoot"_hash, ActionType::Button);
		InputBinding mouseBinding;
		mouseBinding.source = InputBinding::Source::MouseButton;
		mouseBinding.code = static_cast<u16>(MouseButton::Left);
		_probe->map.Bind(_probe->shoot, mouseBinding);
		_probe->map.Enable();

		Focus();
	}

	void TearDown() override {
		SceneSystem::Terminate();
		WindowSystem::Terminate();
	}

	static WindowDesc HiddenDesc() {
		WindowDesc desc;
		desc.title = "GameplayInputRouteTest";
		desc.clientSize = {320, 240};
		desc.flags = WindowFlags::Hidden;
		return desc;
	}

	HWND Hwnd() const { return static_cast<HWND>(_window->GetNativeHandle().hwnd); }

	void Focus() {
		::PostMessageW(Hwnd(), WM_SETFOCUS, 0, 0);
		WindowSystem::PumpEvents();
		ASSERT_TRUE(WindowSystem::GetFocusedWindow().IsValid());
	}

	static LPARAM KeyLParam(u16 scancode, bool up = false) {
		LPARAM lParam = 1;
		lParam |= static_cast<LPARAM>(scancode & 0xFF) << 16;
		if (up) {
			lParam |= (static_cast<LPARAM>(1) << 30) | (static_cast<LPARAM>(1) << 31);
		}
		return lParam;
	}

	void PressGAndTick() {
		::PostMessageW(Hwnd(), WM_KEYDOWN, 'G', KeyLParam(0x22));
		WindowSystem::PumpEvents();
		SceneSystem::UpdateAll();
	}

	void PressGAndClickAndTick(i32 x = 40, i32 y = 30) {
		::PostMessageW(Hwnd(), WM_KEYDOWN, 'G', KeyLParam(0x22));
		::PostMessageW(Hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
		::PostMessageW(Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
		WindowSystem::PumpEvents();
		SceneSystem::UpdateAll();
	}

	Handle<Window> _window;
	Handle<Scene> _scene;
	Handle<RouteProbeBehavior> _probe;
};

TEST_F(GameplayInputRouteFixture, OpenRouteLetsScriptReadInputNormally) {
	Input::SetGameplayInputRouted(true);
	PressGAndTick();

	EXPECT_TRUE(_probe->keyDown);
	EXPECT_TRUE(_probe->keyPressed);
	EXPECT_GT(_probe->queuedEvents, 0u);
	EXPECT_TRUE(_probe->mapPressed);
	EXPECT_TRUE(_probe->mapPerformed);
}

TEST_F(GameplayInputRouteFixture, ClosedRouteStopsScriptOnly) {
	Input::SetGameplayInputRouted(false);
	PressGAndTick();

	EXPECT_FALSE(_probe->keyDown);
	EXPECT_FALSE(_probe->keyPressed);
	EXPECT_EQ(_probe->mods, KeyMod::None);

	EXPECT_TRUE(Input::IsKeyDown(Key::G));
	EXPECT_TRUE(Input::WasKeyPressed(Key::G));
}

TEST_F(GameplayInputRouteFixture, ClosedRouteEmptiesQueue) {
	Input::SetGameplayInputRouted(false);
	PressGAndTick();

	EXPECT_EQ(_probe->queuedEvents, 0u);
	EXPECT_FALSE(_probe->mapPressed);
	EXPECT_FALSE(_probe->mapPerformed);

	EXPECT_GT(Input::GetFrameEvents().size(), 0u);
}

TEST_F(GameplayInputRouteFixture, RouteClosesMapWithoutDisablingIt) {
	Input::SetGameplayInputRouted(false);
	PressGAndTick();
	ASSERT_FALSE(_probe->mapPressed);

	Input::SetGameplayInputRouted(true);
	PressGAndTick();
	EXPECT_TRUE(_probe->mapPressed);
}

TEST_F(GameplayInputRouteFixture, ClosedRouteKeepsGamepadAndCursor) {
	Input::SetGameplayInputRouted(true);
	PressGAndTick();
	const GamepadState* openSlot = _probe->gamepad;
	ASSERT_NE(openSlot, nullptr);

	Input::SetGameplayInputRouted(false);
	PressGAndTick();
	EXPECT_EQ(_probe->gamepad, openSlot);

	EXPECT_EQ(_probe->mousePosition, Input::GetMousePosition());
}

TEST_F(GameplayInputRouteFixture, FocusLossClosesRoute) {
	Input::SetGameplayInputRouted(true);

	::PostMessageW(Hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(10, 10));
	WindowSystem::PumpEvents();
	::PostMessageW(Hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(60, 40));
	WindowSystem::PumpEvents();
	SceneSystem::UpdateAll();
	ASSERT_NE(_probe->mouseDelta.x, 0.0f);

	::PostMessageW(Hwnd(), WM_KILLFOCUS, 0, 0);
	WindowSystem::PumpEvents();
	ASSERT_FALSE(WindowSystem::GetFocusedWindow().IsValid());

	::PostMessageW(Hwnd(), WM_MOUSEMOVE, 0, MAKELPARAM(160, 140));
	WindowSystem::PumpEvents();
	SceneSystem::UpdateAll();

	EXPECT_EQ(_probe->mouseDelta.x, 0.0f);
	EXPECT_EQ(_probe->mouseDelta.y, 0.0f);
	EXPECT_NE(Input::GetMouseDelta().x, 0.0f);
}

TEST_F(GameplayInputRouteFixture, WheelBindingFollowsRoute) {
	Input::SetGameplayInputRouted(true);
	::PostMessageW(Hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(10, 10));
	WindowSystem::PumpEvents();
	SceneSystem::UpdateAll();

	ASSERT_FLOAT_EQ(_probe->wheel.y, 1.0f);
	EXPECT_FLOAT_EQ(_probe->hotbarAxis, 1.0f);

	Input::SetGameplayInputRouted(false);
	::PostMessageW(Hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(10, 10));
	WindowSystem::PumpEvents();
	SceneSystem::UpdateAll();

	EXPECT_FLOAT_EQ(_probe->wheel.y, 0.0f);
	EXPECT_FLOAT_EQ(_probe->hotbarAxis, 0.0f);

	EXPECT_FLOAT_EQ(Input::GetMouseWheel().y, 1.0f);
}

namespace {

	class RouteLogSink : public ILogSink {
	public:
		static constexpr const char* SENTINEL = "route-log-sink-sentinel";

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

class GameplayRouteLogFixture : public GameplayInputRouteFixture {
protected:
	void SetUp() override {
		JobSystem::Initialize();
		LogManager::Initialize({false, false});
		LogManager::SetGlobalLevel(LogLevel::Trace);
		_sink = std::make_shared<RouteLogSink>();
		LogManager::AddSink(_sink);

		GameplayInputRouteFixture::SetUp();
		ASSERT_FALSE(HasFatalFailure());
		ASSERT_TRUE(Arm());
	}

	void TearDown() override {
		GameplayInputRouteFixture::TearDown();
		LogManager::Terminate();
		_sink.reset();
		JobSystem::Terminate();
	}

	void Drain() { LogManager::Terminate(); }

	u32 Closed() const { return _sink->CountMatching("gameplay input route closed"); }
	u32 Opened() const { return _sink->CountMatching("gameplay input route open"); }
	u32 Saying(std::string_view needle) const { return _sink->CountMatching(needle); }

	void Unfocus() {
		::PostMessageW(Hwnd(), WM_KILLFOCUS, 0, 0);
		WindowSystem::PumpEvents();
		ASSERT_FALSE(WindowSystem::GetFocusedWindow().IsValid());
	}

	bool Arm() {
		SLT_LOG_WARN("Input", "{}", RouteLogSink::SENTINEL);
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (std::chrono::steady_clock::now() < deadline) {
			if (_sink->Armed()) {
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		return false;
	}

	std::shared_ptr<RouteLogSink> _sink;
};

TEST_F(GameplayRouteLogFixture, OpenRouteLogsNothing) {
	Input::SetGameplayInputRouted(true);
	for (i32 frame = 0; frame < 10; ++frame) {
		PressGAndTick();
	}
	ASSERT_TRUE(_probe->keyDown);

	SLT_LOG_WARN("Input", "route-log-still-listening");
	Drain();
	ASSERT_EQ(Saying("route-log-still-listening"), 1u);
	EXPECT_EQ(Closed(), 0u);
	EXPECT_EQ(Opened(), 0u);
}

TEST_F(GameplayRouteLogFixture, ClosedRouteReportedOnce) {
	Input::SetGameplayInputRouted(false);
	for (i32 frame = 0; frame < 10; ++frame) {
		PressGAndTick();
	}
	ASSERT_FALSE(_probe->keyDown);

	Drain();
	EXPECT_EQ(Closed(), 1u);
}

TEST_F(GameplayRouteLogFixture, HostClosureNamed) {
	Input::SetGameplayInputRouted(false);
	PressGAndTick();

	Drain();
	ASSERT_EQ(Closed(), 1u);
	EXPECT_EQ(Saying("the host routed input away"), 1u);
	EXPECT_EQ(Saying("not in the foreground"), 0u);
}

TEST_F(GameplayRouteLogFixture, FocusClosureNamed) {
	Input::SetGameplayInputRouted(true);
	Unfocus();
	ASSERT_FALSE(HasFatalFailure());
	PressGAndTick();
	ASSERT_FALSE(_probe->keyDown);

	Drain();
	ASSERT_EQ(Closed(), 1u);
	EXPECT_EQ(Saying("the application isn't in the foreground"), 1u);
	EXPECT_EQ(Saying("the host routed input away"), 0u);
}

TEST_F(GameplayRouteLogFixture, ReopeningTheRouteIsReportedToo) {
	Input::SetGameplayInputRouted(false);
	PressGAndTick();
	Input::SetGameplayInputRouted(true);
	PressGAndTick();
	ASSERT_TRUE(_probe->keyDown);

	Drain();
	EXPECT_EQ(Closed(), 1u);
	EXPECT_EQ(Opened(), 1u);
}

#endif

TEST_F(GameplayInputRouteFixture, ClosedPointerRouteStopsMouseOnly) {
	Input::SetGameplayInputRouted(true);
	Input::SetGameplayPointerRouted(true);
	PressGAndClickAndTick();
	ASSERT_TRUE(_probe->mousePressed);
	ASSERT_TRUE(_probe->keyPressed);

	Input::SetGameplayPointerRouted(false);
	PressGAndClickAndTick(80, 60);

	EXPECT_FALSE(_probe->mouseDown);
	EXPECT_FALSE(_probe->mousePressed);
	EXPECT_FLOAT_EQ(_probe->mouseDelta.x, 0.0f);
	EXPECT_FLOAT_EQ(_probe->mouseDelta.y, 0.0f);
	EXPECT_TRUE(_probe->keyDown);
	EXPECT_TRUE(_probe->keyPressed);
	EXPECT_TRUE(_probe->mapPressed);

	EXPECT_TRUE(Input::IsMouseButtonDown(MouseButton::Left));
}

TEST_F(GameplayInputRouteFixture, ClosedPointerRouteDropsPointerEvents) {
	Input::SetGameplayInputRouted(true);
	Input::SetGameplayPointerRouted(true);
	PressGAndClickAndTick();
	ASSERT_GT(_probe->queuedPointerEvents, 0u);
	ASSERT_TRUE(_probe->shootPerformed);
	const u32 openEvents = _probe->queuedEvents;

	Input::SetGameplayPointerRouted(false);
	PressGAndClickAndTick(80, 60);

	EXPECT_EQ(_probe->queuedPointerEvents, 0u);
	EXPECT_FALSE(_probe->shootPerformed);
	EXPECT_GT(_probe->queuedEvents, 0u);
	EXPECT_LT(_probe->queuedEvents, openEvents);
	EXPECT_TRUE(_probe->mapPerformed);

	EXPECT_GT(Input::GetFrameEvents().size(), 0u);
}

TEST_F(GameplayInputRouteFixture, PointerSwitchCantReopenRoute) {
	Input::SetGameplayInputRouted(false);
	Input::SetGameplayPointerRouted(true);
	PressGAndClickAndTick();

	EXPECT_FALSE(_probe->mouseDown);
	EXPECT_FALSE(_probe->mousePressed);
	EXPECT_FALSE(_probe->keyDown);
	EXPECT_EQ(_probe->queuedEvents, 0u);

	Input::SetGameplayInputRouted(true);
	PressGAndClickAndTick();
	EXPECT_TRUE(_probe->mousePressed);
	EXPECT_TRUE(_probe->keyPressed);
}

TEST_F(GameplayInputRouteFixture, ClosedPointerKeepsCursorAndGamepad) {
	Input::SetGameplayInputRouted(true);
	Input::SetGameplayPointerRouted(true);
	PressGAndClickAndTick();
	const GamepadState* openSlot = _probe->gamepad;
	ASSERT_NE(openSlot, nullptr);

	Input::SetGameplayPointerRouted(false);
	PressGAndClickAndTick(80, 60);

	EXPECT_EQ(_probe->gamepad, openSlot);
	EXPECT_EQ(_probe->mousePosition, Input::GetMousePosition());
}

TEST_F(GameplayInputRouteFixture, ClosedPointerRouteStopsWheelAndBinding) {
	Input::SetGameplayInputRouted(true);
	Input::SetGameplayPointerRouted(true);
	::PostMessageW(Hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(10, 10));
	WindowSystem::PumpEvents();
	SceneSystem::UpdateAll();
	ASSERT_FLOAT_EQ(_probe->wheel.y, 1.0f);
	ASSERT_FLOAT_EQ(_probe->hotbarAxis, 1.0f);

	Input::SetGameplayPointerRouted(false);
	::PostMessageW(Hwnd(), WM_KEYDOWN, 'G', KeyLParam(0x22));
	::PostMessageW(Hwnd(), WM_MOUSEWHEEL, MAKEWPARAM(0, WHEEL_DELTA), MAKELPARAM(10, 10));
	WindowSystem::PumpEvents();
	SceneSystem::UpdateAll();

	EXPECT_FLOAT_EQ(_probe->wheel.y, 0.0f);
	EXPECT_FLOAT_EQ(_probe->hotbarAxis, 0.0f);
	EXPECT_TRUE(_probe->keyPressed);
	EXPECT_FLOAT_EQ(Input::GetMouseWheel().y, 1.0f);
}
