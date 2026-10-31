#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Input/InputCodes.h"
#include "Shalltear/Core/Input/InputEvent.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear {

	class Window;

	struct GamepadState {
		bool connected = false;
		bool buttons[static_cast<size_t>(GamepadButton::Count)] = {};
		f32 axes[static_cast<size_t>(GamepadAxis::Count)] = {};

		[[nodiscard]] bool IsDown(GamepadButton button) const {
			return buttons[static_cast<size_t>(button)];
		}
		[[nodiscard]] f32 GetAxis(GamepadAxis axis) const {
			return axes[static_cast<size_t>(axis)];
		}
	};

	class SHALLTEAR_CORE_API Input {
	public:
		static std::span<const InputEvent> GetFrameEvents();

		static u32 GetFrameEvents(Handle<Window> window, std::span<InputEvent> out);

		static bool IsKeyDown(Key key);
		static bool WasKeyPressed(Key key);
		static bool WasKeyReleased(Key key);
		static KeyMod GetKeyMods();

		static bool IsMouseButtonDown(MouseButton button);
		static bool WasMouseButtonPressed(MouseButton button);
		static bool WasMouseButtonReleased(MouseButton button);

		static Vec2I GetMousePosition();
		// content pxs (screen space for a null/dead handle)
		static Vec2I GetMousePosition(Handle<Window> window);
		static Vec2 GetMouseDelta();
		static Vec2 GetMouseWheel();

		static constexpr u8 MAX_GAMEPADS = 4;

		static const GamepadState& GetGamepad(u8 index);

		static void SetGameplayInputRouted(bool routed);

		static void SetGameplayPointerRouted(bool routed);
	};

} // namespace Shalltear
