#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/EnumFlags.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	// US layout identities, text entry uses InputEventType::Text
	enum class Key : u16 {
		Unknown = 0,

		A,
		B,
		C,
		D,
		E,
		F,
		G,
		H,
		I,
		J,
		K,
		L,
		M,
		N,
		O,
		P,
		Q,
		R,
		S,
		T,
		U,
		V,
		W,
		X,
		Y,
		Z,

		D0,
		D1,
		D2,
		D3,
		D4,
		D5,
		D6,
		D7,
		D8,
		D9,

		F1,
		F2,
		F3,
		F4,
		F5,
		F6,
		F7,
		F8,
		F9,
		F10,
		F11,
		F12,
		F13,
		F14,
		F15,
		F16,
		F17,
		F18,
		F19,
		F20,
		F21,
		F22,
		F23,
		F24,

		Kp0,
		Kp1,
		Kp2,
		Kp3,
		Kp4,
		Kp5,
		Kp6,
		Kp7,
		Kp8,
		Kp9,
		KpDecimal,
		KpDivide,
		KpMultiply,
		KpSubtract,
		KpAdd,
		KpEnter,
		KpEqual,

		Escape,
		Enter,
		Tab,
		Backspace,
		Insert,
		Delete,
		Home,
		End,
		PageUp,
		PageDown,
		Left,
		Right,
		Up,
		Down,

		Space,
		CapsLock,
		ScrollLock,
		NumLock,
		PrintScreen,
		Pause,
		Menu,

		LeftShift,
		LeftCtrl,
		LeftAlt,
		LeftSuper,
		RightShift,
		RightCtrl,
		RightAlt,
		RightSuper,

		Apostrophe,
		Comma,
		Minus,
		Period,
		Slash,
		Semicolon,
		Equal,
		LeftBracket,
		Backslash,
		RightBracket,
		GraveAccent,

		Count
	};

	enum class MouseButton : u8 {
		Left = 0,
		Right,
		Middle,
		X1,
		X2,
		Count
	};

	enum class GamepadButton : u8 {
		South = 0,
		East,
		West,
		North,
		LeftBumper,
		RightBumper,
		Back,
		Start,
		Guide,
		LeftThumb,
		RightThumb,
		DpadUp,
		DpadDown,
		DpadLeft,
		DpadRight,
		Count
	};

	enum class GamepadAxis : u8 {
		LeftX = 0,
		LeftY,
		RightX,
		RightY,
		LeftTrigger,
		RightTrigger,
		Count
	};

	enum class KeyMod : u8 {
		None = 0,
		Ctrl = 1 << 0,
		Shift = 1 << 1,
		Alt = 1 << 2,
		Super = 1 << 3,
	};
	SLT_ENUM_CLASS_FLAGS(KeyMod)

	SHALLTEAR_CORE_API const char* ToString(Key key);
	SHALLTEAR_CORE_API const char* ToString(MouseButton button);
	SHALLTEAR_CORE_API const char* ToString(GamepadButton button);
	SHALLTEAR_CORE_API const char* ToString(GamepadAxis axis);

	SHALLTEAR_CORE_API Key KeyFromString(std::string_view name);
	SHALLTEAR_CORE_API MouseButton MouseButtonFromString(std::string_view name);
	SHALLTEAR_CORE_API GamepadButton GamepadButtonFromString(std::string_view name);
	SHALLTEAR_CORE_API GamepadAxis GamepadAxisFromString(std::string_view name);

} // namespace Shalltear
