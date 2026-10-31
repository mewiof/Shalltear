#pragma once

#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Input/InputCodes.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	class Window;

	enum class InputEventType : u8 {
		MouseMove = 0,
		MouseDown,
		MouseUp,
		MouseWheel,
		KeyDown,
		KeyUp,
		Text,
		MouseEnter,
		MouseLeave,
		GamepadConnected,
		GamepadDisconnected,
	};

	struct InputEvent {
		InputEventType type = InputEventType::MouseMove;
		Handle<Window> window;
		KeyMod mods = KeyMod::None;
		Vec2I mousePosPx;
		Vec2 wheel; // notches
		MouseButton button = MouseButton::Left;
		Key key = Key::Unknown;
		u16 scancode = 0;
		bool repeat = false;
		char32_t codepoint = 0;
		u8 gamepadIndex = 0;
	};

	static_assert(std::is_trivially_copyable_v<InputEvent>, "InputEvent must stay flat/trivially-copyable for the frame queue");

} // namespace Shalltear
