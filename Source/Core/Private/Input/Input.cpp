#include "Shalltear/Core/Input/Input.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Window/Window.h"

#include "Input/InputInternal.h"
#include "Module/EngineStateTable.h"
#include "Window/WindowInternal.h"

#include <cstring>
#include <iterator>

namespace Shalltear {

	namespace InputInternal {

		namespace {
			State* _boundState = nullptr;

			// in InputEventType order
			bool IsPointerEvent(InputEventType type) {
				switch (type) {
				case InputEventType::MouseMove:
				case InputEventType::MouseDown:
				case InputEventType::MouseUp:
				case InputEventType::MouseWheel:
				case InputEventType::MouseEnter:
				case InputEventType::MouseLeave:
					return true;
				case InputEventType::KeyDown:
				case InputEventType::KeyUp:
				case InputEventType::Text:
				case InputEventType::GamepadConnected:
				case InputEventType::GamepadDisconnected:
					return false;
				}
				return false;
			}
		} // anonymous namespace

		State& GetState() {
			if (_boundState)
				return *_boundState;
			static State state;
			return state;
		}

		void ResetState() {
			State& state = GetState();
			state.frameEvents.clear();
			state.frameEvents.shrink_to_fit();
			std::memset(state.keyDown, 0, sizeof(state.keyDown));
			std::memset(state.keyPressed, 0, sizeof(state.keyPressed));
			std::memset(state.keyReleased, 0, sizeof(state.keyReleased));
			std::memset(state.mouseDown, 0, sizeof(state.mouseDown));
			std::memset(state.mousePressed, 0, sizeof(state.mousePressed));
			std::memset(state.mouseReleased, 0, sizeof(state.mouseReleased));
			state.mouseScreenPos = {};
			state.pumpStartScreenPos = {};
			state.havePumpStartPos = false;
			state.mouseDelta = {};
			state.wheel = {};
			state.rawDeltaActive = false;
			state.rawDeltaAccum = {};
			for (u8 i = 0; i < Input::MAX_GAMEPADS; ++i) {
				state.gamepads[i] = {};
				state.gamepadNextProbe[i] = 0.0;
			}
			state.gameplayRouted = true;
			state.gameplayPointerRouted = true;
			state.applicationFocused = true;
			state.gameplayReadsBlocked = false;
			state.gameplayPointerBlocked = false;
			state.gameplayEvents.clear();
			state.routeBlockReported = false;
			state.pointerBlockReported = false;
			state.pumpSequence = 1;
		}

		void BeginPump() {
			State& state = GetState();
			++state.pumpSequence;
			state.frameEvents.clear();
			std::memset(state.keyPressed, 0, sizeof(state.keyPressed));
			std::memset(state.keyReleased, 0, sizeof(state.keyReleased));
			std::memset(state.mousePressed, 0, sizeof(state.mousePressed));
			std::memset(state.mouseReleased, 0, sizeof(state.mouseReleased));
			state.wheel = {};
			state.rawDeltaAccum = {};
			state.pumpStartScreenPos = state.mouseScreenPos;
			state.havePumpStartPos = true;
		}

		void EndPump() {
			State& state = GetState();
			if (state.rawDeltaActive) {
				state.mouseDelta = state.rawDeltaAccum;
			} else if (state.havePumpStartPos) {
				state.mouseDelta = Vec2(Vec2I(state.mouseScreenPos - state.pumpStartScreenPos));
			} else {
				state.mouseDelta = {};
			}
		}

		void PushEvent(const InputEvent& event) {
			State& state = GetState();
			state.frameEvents.push_back(event);

			switch (event.type) {
			case InputEventType::KeyDown:
				if (event.key != Key::Unknown) {
					size_t index = static_cast<size_t>(event.key);
					if (!event.repeat) {
						state.keyPressed[index] = true;
					}
					state.keyDown[index] = true;
				}
				break;
			case InputEventType::KeyUp:
				if (event.key != Key::Unknown) {
					size_t index = static_cast<size_t>(event.key);
					state.keyDown[index] = false;
					state.keyReleased[index] = true;
				}
				break;
			case InputEventType::MouseDown: {
				size_t index = static_cast<size_t>(event.button);
				state.mouseDown[index] = true;
				state.mousePressed[index] = true;
				break;
			}
			case InputEventType::MouseUp: {
				size_t index = static_cast<size_t>(event.button);
				state.mouseDown[index] = false;
				state.mouseReleased[index] = true;
				break;
			}
			case InputEventType::MouseWheel:
				state.wheel += event.wheel;
				break;
			default:
				break;
			}
		}

		void ReleaseHeldInput(Handle<Window> window, Vec2I mousePosPx) {
			State& state = GetState();
			for (u16 i = 0; i < static_cast<u16>(Key::Count); ++i) {
				if (!state.keyDown[i]) {
					continue;
				}
				InputEvent event;
				event.type = InputEventType::KeyUp;
				event.window = window;
				event.mods = CurrentMods();
				event.mousePosPx = mousePosPx;
				event.key = static_cast<Key>(i);
				PushEvent(event);
			}
			for (u8 i = 0; i < static_cast<u8>(MouseButton::Count); ++i) {
				if (!state.mouseDown[i]) {
					continue;
				}
				InputEvent event;
				event.type = InputEventType::MouseUp;
				event.window = window;
				event.mods = CurrentMods();
				event.mousePosPx = mousePosPx;
				event.button = static_cast<MouseButton>(i);
				PushEvent(event);
			}
		}

		KeyMod CurrentMods() {
			State& state = GetState();
			auto down = [&state](Key key) {
				return state.keyDown[static_cast<size_t>(key)];
			};
			KeyMod mods = KeyMod::None;
			if (down(Key::LeftCtrl) || down(Key::RightCtrl)) {
				mods |= KeyMod::Ctrl;
			}
			if (down(Key::LeftShift) || down(Key::RightShift)) {
				mods |= KeyMod::Shift;
			}
			if (down(Key::LeftAlt) || down(Key::RightAlt)) {
				mods |= KeyMod::Alt;
			}
			if (down(Key::LeftSuper) || down(Key::RightSuper)) {
				mods |= KeyMod::Super;
			}
			return mods;
		}

		void SetMouseScreenPosition(Vec2I screenPx) {
			GetState().mouseScreenPos = screenPx;
		}

		void ShiftMouseOrigin(Vec2I shiftPx) {
			State& state = GetState();
			if (state.havePumpStartPos) {
				state.pumpStartScreenPos += shiftPx;
			}
		}

		void AddRawMouseDelta(Vec2 deltaPx) {
			GetState().rawDeltaAccum += deltaPx;
		}

		void SetRawDeltaActive(bool active) {
			GetState().rawDeltaActive = active;
		}

		void SetApplicationFocused(bool focused) {
			GetState().applicationFocused = focused;
		}

		GameplayTickScope::GameplayTickScope() {
			State& state = GetState();
			_previous = state.gameplayReadsBlocked;
			_previousPointer = state.gameplayPointerBlocked;
			state.gameplayReadsBlocked = !(state.gameplayRouted && state.applicationFocused);
			state.gameplayPointerBlocked = state.gameplayReadsBlocked || !state.gameplayPointerRouted;
			if (!state.gameplayReadsBlocked && state.gameplayPointerBlocked) {
				state.gameplayEvents.clear();
				for (const InputEvent& event : state.frameEvents) {
					if (!IsPointerEvent(event.type)) {
						state.gameplayEvents.push_back(event);
					}
				}
			}

			if (state.gameplayReadsBlocked != state.routeBlockReported) {
				state.routeBlockReported = state.gameplayReadsBlocked;
				if (state.gameplayReadsBlocked) {
					const char* reason = "the application isn't in the foreground";
					if (!state.gameplayRouted) {
						reason = state.applicationFocused
									 ? "the host routed input away"
									 : "the host routed input away and the application isn't in the foreground";
					}
					SLT_LOG_INFO("Input", "gameplay input route closed ({})", reason);
				} else {
					SLT_LOG_INFO("Input", "gameplay input route open");
				}
			}
			if (!state.gameplayReadsBlocked && state.gameplayPointerBlocked != state.pointerBlockReported) {
				state.pointerBlockReported = state.gameplayPointerBlocked;
				if (state.gameplayPointerBlocked) {
					SLT_LOG_INFO("Input", "gameplay POINTER route closed (the host aimed the pointer somewhere else)");
				} else {
					SLT_LOG_INFO("Input", "gameplay POINTER route open");
				}
			}
		}

		GameplayTickScope::~GameplayTickScope() {
			State& state = GetState();
			state.gameplayReadsBlocked = _previous;
			state.gameplayPointerBlocked = _previousPointer;
		}

		u64 CurrentPumpSequence() {
			return GetState().pumpSequence;
		}

		void ReportEvaluationCadence(u64 pumpsSinceLast) {
			if (pumpsSinceLast == 0) {
				SLT_LOG_WARN("Input",
							 "InputActionMap::Evaluate() ran more than once on one input pump. Evaluate once per frame after WindowSystem::PumpEvents");
				return;
			}
			SLT_LOG_WARN("Input",
						 "InputActionMap::Evaluate() ran once in {} input pumps -> {} in between never read + their mouse delta/press edges are gone. Evaluate once per frame after WindowSystem::PumpEvents. Accumulate those reads and consume them in the tick",
						 pumpsSinceLast, pumpsSinceLast - 1);
		}

	} // namespace InputInternal

	using InputInternal::GetState;

	std::span<const InputEvent> Input::GetFrameEvents() {
		InputInternal::State& state = GetState();
		if (state.gameplayReadsBlocked) {
			return {};
		}
		if (state.gameplayPointerBlocked) {
			return {state.gameplayEvents.data(), state.gameplayEvents.size()};
		}
		return {state.frameEvents.data(), state.frameEvents.size()};
	}

	u32 Input::GetFrameEvents(Handle<Window> window, std::span<InputEvent> out) {
		InputInternal::State& state = GetState();
		u32 written = 0;
		for (const InputEvent& event : state.frameEvents) {
			if (event.window != window) {
				continue;
			}
			if (written >= out.size()) {
				break;
			}
			out[written++] = event;
		}
		return written;
	}

	bool Input::IsKeyDown(Key key) {
		const InputInternal::State& state = GetState();
		return !state.gameplayReadsBlocked && key < Key::Count && state.keyDown[static_cast<size_t>(key)];
	}

	bool Input::WasKeyPressed(Key key) {
		const InputInternal::State& state = GetState();
		return !state.gameplayReadsBlocked && key < Key::Count && state.keyPressed[static_cast<size_t>(key)];
	}

	bool Input::WasKeyReleased(Key key) {
		const InputInternal::State& state = GetState();
		return !state.gameplayReadsBlocked && key < Key::Count && state.keyReleased[static_cast<size_t>(key)];
	}

	KeyMod Input::GetKeyMods() {
		return GetState().gameplayReadsBlocked ? KeyMod::None : InputInternal::CurrentMods();
	}

	bool Input::IsMouseButtonDown(MouseButton button) {
		const InputInternal::State& state = GetState();
		return !state.gameplayPointerBlocked && button < MouseButton::Count && state.mouseDown[static_cast<size_t>(button)];
	}

	bool Input::WasMouseButtonPressed(MouseButton button) {
		const InputInternal::State& state = GetState();
		return !state.gameplayPointerBlocked && button < MouseButton::Count && state.mousePressed[static_cast<size_t>(button)];
	}

	bool Input::WasMouseButtonReleased(MouseButton button) {
		const InputInternal::State& state = GetState();
		return !state.gameplayPointerBlocked && button < MouseButton::Count && state.mouseReleased[static_cast<size_t>(button)];
	}

	Vec2I Input::GetMousePosition() {
		return GetState().mouseScreenPos;
	}

	Vec2I Input::GetMousePosition(Handle<Window> window) {
		InputInternal::State& state = GetState();
		if (Window* ptr = window.Get()) {
			return WindowInternal::ClientToRender(*ptr, ptr->ScreenToClient(state.mouseScreenPos));
		}
		return state.mouseScreenPos;
	}

	Vec2 Input::GetMouseDelta() {
		const InputInternal::State& state = GetState();
		return state.gameplayPointerBlocked ? Vec2() : state.mouseDelta;
	}

	Vec2 Input::GetMouseWheel() {
		const InputInternal::State& state = GetState();
		return state.gameplayPointerBlocked ? Vec2() : state.wheel;
	}

	const GamepadState& Input::GetGamepad(u8 index) {
		static const GamepadState disconnected{};
		if (index >= MAX_GAMEPADS) {
			return disconnected;
		}
		return GetState().gamepads[index];
	}

	void Input::SetGameplayInputRouted(bool routed) {
		GetState().gameplayRouted = routed;
	}

	void Input::SetGameplayPointerRouted(bool routed) {
		GetState().gameplayPointerRouted = routed;
	}

	namespace {

		constexpr const char* KEY_NAMES[] = {
			"Unknown",
			"A", "B", "C", "D", "E", "F", "G", "H", "I", "J", "K", "L", "M",
			"N", "O", "P", "Q", "R", "S", "T", "U", "V", "W", "X", "Y", "Z",
			"D0", "D1", "D2", "D3", "D4", "D5", "D6", "D7", "D8", "D9",
			"F1", "F2", "F3", "F4", "F5", "F6", "F7", "F8", "F9", "F10",
			"F11", "F12", "F13", "F14", "F15", "F16", "F17", "F18", "F19",
			"F20", "F21", "F22", "F23", "F24",
			"Kp0", "Kp1", "Kp2", "Kp3", "Kp4", "Kp5", "Kp6", "Kp7", "Kp8", "Kp9",
			"KpDecimal", "KpDivide", "KpMultiply", "KpSubtract", "KpAdd", "KpEnter", "KpEqual",
			"Escape", "Enter", "Tab", "Backspace", "Insert", "Delete",
			"Home", "End", "PageUp", "PageDown",
			"Left", "Right", "Up", "Down",
			"Space", "CapsLock", "ScrollLock", "NumLock", "PrintScreen", "Pause", "Menu",
			"LeftShift", "LeftCtrl", "LeftAlt", "LeftSuper",
			"RightShift", "RightCtrl", "RightAlt", "RightSuper",
			"Apostrophe", "Comma", "Minus", "Period", "Slash", "Semicolon",
			"Equal", "LeftBracket", "Backslash", "RightBracket", "GraveAccent"};
		static_assert(std::size(KEY_NAMES) == static_cast<size_t>(Key::Count),
					  "Key enum changed: update KEY_NAMES");

		constexpr const char* MOUSE_BUTTON_NAMES[] = {
			"Left", "Right", "Middle", "X1", "X2"};
		static_assert(std::size(MOUSE_BUTTON_NAMES) == static_cast<size_t>(MouseButton::Count),
					  "MouseButton enum changed: update MOUSE_BUTTON_NAMES");

		constexpr const char* GAMEPAD_BUTTON_NAMES[] = {
			"South", "East", "West", "North", "LeftBumper", "RightBumper",
			"Back", "Start", "Guide", "LeftThumb", "RightThumb",
			"DpadUp", "DpadDown", "DpadLeft", "DpadRight"};
		static_assert(std::size(GAMEPAD_BUTTON_NAMES) == static_cast<size_t>(GamepadButton::Count),
					  "GamepadButton enum changed: update GAMEPAD_BUTTON_NAMES");

		constexpr const char* GAMEPAD_AXIS_NAMES[] = {
			"LeftX", "LeftY", "RightX", "RightY", "LeftTrigger", "RightTrigger"};
		static_assert(std::size(GAMEPAD_AXIS_NAMES) == static_cast<size_t>(GamepadAxis::Count),
					  "GamepadAxis enum changed: update GAMEPAD_AXIS_NAMES");

		template <typename TEnum, size_t N>
		TEnum EnumFromName(const char* const (&names)[N], std::string_view name, TEnum fallback) {
			for (size_t i = 0; i < N; ++i) {
				if (name == names[i]) {
					return static_cast<TEnum>(i);
				}
			}
			return fallback;
		}

	} // anonymous namespace

	const char* ToString(Key key) {
		size_t index = static_cast<size_t>(key);
		return index < std::size(KEY_NAMES) ? KEY_NAMES[index] : "Unknown";
	}

	const char* ToString(MouseButton button) {
		size_t index = static_cast<size_t>(button);
		return index < std::size(MOUSE_BUTTON_NAMES) ? MOUSE_BUTTON_NAMES[index] : "Unknown";
	}

	const char* ToString(GamepadButton button) {
		size_t index = static_cast<size_t>(button);
		return index < std::size(GAMEPAD_BUTTON_NAMES) ? GAMEPAD_BUTTON_NAMES[index] : "Unknown";
	}

	const char* ToString(GamepadAxis axis) {
		size_t index = static_cast<size_t>(axis);
		return index < std::size(GAMEPAD_AXIS_NAMES) ? GAMEPAD_AXIS_NAMES[index] : "Unknown";
	}

	Key KeyFromString(std::string_view name) {
		return EnumFromName(KEY_NAMES, name, Key::Unknown);
	}

	MouseButton MouseButtonFromString(std::string_view name) {
		return EnumFromName(MOUSE_BUTTON_NAMES, name, MouseButton::Left);
	}

	GamepadButton GamepadButtonFromString(std::string_view name) {
		return EnumFromName(GAMEPAD_BUTTON_NAMES, name, GamepadButton::South);
	}

	GamepadAxis GamepadAxisFromString(std::string_view name) {
		return EnumFromName(GAMEPAD_AXIS_NAMES, name, GamepadAxis::LeftX);
	}

	namespace ModuleInternal {

		void CaptureInputEngineState(EngineStateTable& table) {
			table.input = &InputInternal::GetState();
		}

		void BindInputEngineState(const EngineStateTable& table) {
			InputInternal::_boundState = static_cast<InputInternal::State*>(table.input);
		}

	} // namespace ModuleInternal

} // namespace Shalltear
