#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Input/InputCodes.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	using ActionId = u32;
	inline constexpr ActionId INVALID_ACTION_ID = ~0u;

	enum class ActionType : u8 {
		Button,
		Axis1D,
		Axis2D,
	};

	struct InputBinding {
		enum class Source : u8 {
			Key,
			MouseButton,
			MouseDelta,
			MouseWheel,
			GamepadButton,
			GamepadAxis,
		};

		Source source = Source::Key;
		u16 code = 0;
		KeyMod requiredMods = KeyMod::None;
		bool exactMods = false;
		f32 scale = 1.0f;
		u8 component = 0;
		u8 gamepadIndex = 0;
	};

	class SHALLTEAR_CORE_API InputActionMap {
	public:
		InputActionMap();

		ActionId RegisterAction(StringHash name, ActionType type);

		ActionId FindAction(StringHash name) const;

		void Bind(ActionId action, const InputBinding& binding);
		void ClearBindings(ActionId action);

		void Enable();
		void Disable();

		void Evaluate();

		bool IsPressed(ActionId action) const;
		bool WasPerformed(ActionId action) const;
		f32 GetAxis1D(ActionId action) const;
		Vec2 GetAxis2D(ActionId action) const;

		u32 GetActionCount() const;
	private:
		struct Action {
			StringHash name;
			ActionType type;
			Vector<InputBinding> bindings;
			bool pressed = false;
			bool performed = false;
			Vec2 axis;
		};

		bool IsBindingActive(const InputBinding& binding) const;
		f32 GetBindingValue(const InputBinding& binding) const;

		Vector<Action> _actions;
		bool _enabled = false;
		bool _seedPending = false;
		u64 _lastEvaluatePump = 0;
		bool _cadenceReported = false;
	};

} // namespace Shalltear
