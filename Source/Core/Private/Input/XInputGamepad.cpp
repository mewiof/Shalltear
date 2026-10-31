#include "Shalltear/Core/Platform.h"

#ifdef SLT_PLATFORM_WINDOWS

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Time.h"

#include "Input/InputInternal.h"

#include <cmath>
#include <cstddef>
#include <cstring>

namespace Shalltear::InputInternal {

	namespace {

		struct XInputGamepad {
			u16 buttons;
			u8 leftTrigger;
			u8 rightTrigger;
			i16 thumbLX;
			i16 thumbLY;
			i16 thumbRX;
			i16 thumbRY;
		};

		struct XInputStateBlock {
			u32 packetNumber;
			XInputGamepad gamepad;
		};

		static_assert(sizeof(XInputGamepad) == 12 && alignof(XInputGamepad) == 2, "XINPUT_GAMEPAD is 12 bytes packed to 2");
		static_assert(offsetof(XInputGamepad, leftTrigger) == 2, "the triggers follow the button word");
		static_assert(offsetof(XInputGamepad, thumbLX) == 4, "the sticks follow the 2 trigger bytes");
		static_assert(sizeof(XInputStateBlock) == 16, "XINPUT_STATE is a DWORD then the pad");
		static_assert(offsetof(XInputStateBlock, gamepad) == 4, "the pad follows the packet number");

		enum : u16 {
			PAD_DPAD_UP = 0x0001,
			PAD_DPAD_DOWN = 0x0002,
			PAD_DPAD_LEFT = 0x0004,
			PAD_DPAD_RIGHT = 0x0008,
			PAD_START = 0x0010,
			PAD_BACK = 0x0020,
			PAD_LEFT_THUMB = 0x0040,
			PAD_RIGHT_THUMB = 0x0080,
			PAD_LEFT_SHOULDER = 0x0100,
			PAD_RIGHT_SHOULDER = 0x0200,
			PAD_A = 0x1000,
			PAD_B = 0x2000,
			PAD_X = 0x4000,
			PAD_Y = 0x8000,
		};

		constexpr f32 PAD_LEFT_THUMB_DEADZONE = 7849.0f;
		constexpr f32 PAD_RIGHT_THUMB_DEADZONE = 8689.0f;
		constexpr f32 PAD_TRIGGER_THRESHOLD = 30.0f;

		using XInputGetStateFn = DWORD(WINAPI*)(DWORD, XInputStateBlock*);

		XInputGetStateFn ResolveGetState() {
			static XInputGetStateFn resolved = [] {
				HMODULE module = ::LoadLibraryW(L"xinput1_4.dll");
				XInputGetStateFn fn = module ? reinterpret_cast<XInputGetStateFn>(
												   reinterpret_cast<void*>(::GetProcAddress(module, "XInputGetState")))
											 : nullptr;
				if (!fn) {
					SLT_LOG_WARN("Platform", "xinput1_4.dll didn't resolve");
				}
				return fn;
			}();
			return resolved;
		}

		void ApplyStickDeadzone(SHORT rawX, SHORT rawY, f32 deadzone, f32& outX, f32& outY) {
			f32 x = static_cast<f32>(rawX);
			f32 y = static_cast<f32>(rawY);
			f32 magnitude = std::sqrt(x * x + y * y);
			if (magnitude <= deadzone) {
				outX = 0.0f;
				outY = 0.0f;
				return;
			}
			f32 clamped = magnitude > 32767.0f ? 32767.0f : magnitude;
			f32 normalized = (clamped - deadzone) / (32767.0f - deadzone);
			outX = (x / magnitude) * normalized;
			outY = (y / magnitude) * normalized;
		}

		f32 ApplyTriggerThreshold(u8 raw) {
			constexpr f32 THRESHOLD = PAD_TRIGGER_THRESHOLD;
			f32 value = static_cast<f32>(raw);
			if (value <= THRESHOLD) {
				return 0.0f;
			}
			return (value - THRESHOLD) / (255.0f - THRESHOLD);
		}

		void FillGamepadState(GamepadState& out, const XInputGamepad& pad) {
			out.connected = true;

			auto set = [&out, &pad](GamepadButton button, u16 mask) {
				out.buttons[static_cast<size_t>(button)] = (pad.buttons & mask) != 0;
			};
			set(GamepadButton::South, PAD_A);
			set(GamepadButton::East, PAD_B);
			set(GamepadButton::West, PAD_X);
			set(GamepadButton::North, PAD_Y);
			set(GamepadButton::LeftBumper, PAD_LEFT_SHOULDER);
			set(GamepadButton::RightBumper, PAD_RIGHT_SHOULDER);
			set(GamepadButton::Back, PAD_BACK);
			set(GamepadButton::Start, PAD_START);
			set(GamepadButton::LeftThumb, PAD_LEFT_THUMB);
			set(GamepadButton::RightThumb, PAD_RIGHT_THUMB);
			set(GamepadButton::DpadUp, PAD_DPAD_UP);
			set(GamepadButton::DpadDown, PAD_DPAD_DOWN);
			set(GamepadButton::DpadLeft, PAD_DPAD_LEFT);
			set(GamepadButton::DpadRight, PAD_DPAD_RIGHT);

			f32* axes = out.axes;
			ApplyStickDeadzone(pad.thumbLX, pad.thumbLY, PAD_LEFT_THUMB_DEADZONE,
							   axes[static_cast<size_t>(GamepadAxis::LeftX)],
							   axes[static_cast<size_t>(GamepadAxis::LeftY)]);
			ApplyStickDeadzone(pad.thumbRX, pad.thumbRY, PAD_RIGHT_THUMB_DEADZONE,
							   axes[static_cast<size_t>(GamepadAxis::RightX)],
							   axes[static_cast<size_t>(GamepadAxis::RightY)]);
			axes[static_cast<size_t>(GamepadAxis::LeftTrigger)] = ApplyTriggerThreshold(pad.leftTrigger);
			axes[static_cast<size_t>(GamepadAxis::RightTrigger)] = ApplyTriggerThreshold(pad.rightTrigger);
		}

		void PushConnectionEvent(u8 index, bool connected) {
			InputEvent event;
			event.type = connected ? InputEventType::GamepadConnected : InputEventType::GamepadDisconnected;
			event.gamepadIndex = index;
			PushEvent(event);
		}

	} // anonymous namespace

	void PollGamepads(bool applicationFocused) {
		State& state = GetState();

		if (!applicationFocused) {
			for (u8 i = 0; i < Input::MAX_GAMEPADS; ++i) {
				GamepadState& pad = state.gamepads[i];
				std::memset(pad.buttons, 0, sizeof(pad.buttons));
				std::memset(pad.axes, 0, sizeof(pad.axes));
			}
			return;
		}

		XInputGetStateFn getState = ResolveGetState();
		if (!getState) {
			return;
		}

		f64 now = Time::GetRealtimeSinceStartup();

		for (u8 i = 0; i < Input::MAX_GAMEPADS; ++i) {
			GamepadState& pad = state.gamepads[i];

			if (!pad.connected && now < state.gamepadNextProbe[i]) {
				continue;
			}

			XInputStateBlock xinput{};
			if (getState(i, &xinput) == ERROR_SUCCESS) {
				bool wasConnected = pad.connected;
				FillGamepadState(pad, xinput.gamepad);
				if (!wasConnected) {
					PushConnectionEvent(i, true);
				}
			} else {
				bool wasConnected = pad.connected;
				pad = {};
				state.gamepadNextProbe[i] = now + 1.0;
				if (wasConnected) {
					PushConnectionEvent(i, false);
				}
			}
		}
	}

} // namespace Shalltear::InputInternal

#endif
