#include "Shalltear/Core/Input/InputAction.h"

#include "Shalltear/Core/Assert.h"
#include "Shalltear/Core/Input/Input.h"
#include "Shalltear/Core/Input/InputEvent.h"

#include "Input/InputInternal.h"

namespace Shalltear {

	namespace {

		bool ModsSatisfy(const InputBinding& binding, KeyMod held) {
			if (binding.exactMods) {
				return held == binding.requiredMods;
			}
			return binding.requiredMods == KeyMod::None || HasFlag(held, binding.requiredMods);
		}

		bool HasEventStream(InputBinding::Source source) {
			return source == InputBinding::Source::Key ||
				   source == InputBinding::Source::MouseButton;
		}

		bool BindingPressedThisPump(const InputBinding& binding) {
			for (const InputEvent& event : Input::GetFrameEvents()) {
				if (event.repeat) {
					continue;
				}
				bool match = binding.source == InputBinding::Source::Key
								 ? event.type == InputEventType::KeyDown &&
									   event.key == static_cast<Key>(binding.code)
								 : event.type == InputEventType::MouseDown &&
									   event.button == static_cast<MouseButton>(binding.code);
				if (match && ModsSatisfy(binding, event.mods)) {
					return true;
				}
			}
			return false;
		}

	} // anonymous namespace

	InputActionMap::InputActionMap()
		: _actions(GetMemoryResource(MemTag::Platform)) {}

	ActionId InputActionMap::RegisterAction(StringHash name, ActionType type) {
		ActionId existing = FindAction(name);
		if (existing != INVALID_ACTION_ID) {
			SLT_CORE_ASSERT_MSG(_actions[existing].type == type,
								"Action re-registered with a different type");
			return existing;
		}

		Action action{name, type, Vector<InputBinding>(GetMemoryResource(MemTag::Platform))};
		_actions.push_back(std::move(action));
		return static_cast<ActionId>(_actions.size() - 1);
	}

	ActionId InputActionMap::FindAction(StringHash name) const {
		for (size_t i = 0; i < _actions.size(); ++i) {
			if (_actions[i].name == name) {
				return static_cast<ActionId>(i);
			}
		}
		return INVALID_ACTION_ID;
	}

	void InputActionMap::Bind(ActionId action, const InputBinding& binding) {
		SLT_CORE_ASSERT_MSG(action < _actions.size(), "Bind on an unregistered action");
		if (action >= _actions.size()) {
			return;
		}
		_actions[action].bindings.push_back(binding);
	}

	void InputActionMap::ClearBindings(ActionId action) {
		if (action < _actions.size()) {
			_actions[action].bindings.clear();
		}
	}

	void InputActionMap::Enable() {
		if (_enabled) {
			return;
		}
		_enabled = true;
		_lastEvaluatePump = 0;
		_cadenceReported = false;
		_seedPending = true;
	}

	void InputActionMap::Disable() {
		if (!_enabled) {
			return;
		}
		_enabled = false;
		for (Action& action : _actions) {
			action.pressed = false;
			action.performed = false;
			action.axis = Vec2();
		}
	}

	bool InputActionMap::IsBindingActive(const InputBinding& binding) const {
		if (!ModsSatisfy(binding, Input::GetKeyMods())) {
			return false;
		}

		switch (binding.source) {
		case InputBinding::Source::Key:
			return Input::IsKeyDown(static_cast<Key>(binding.code));
		case InputBinding::Source::MouseButton:
			return Input::IsMouseButtonDown(static_cast<MouseButton>(binding.code));
		case InputBinding::Source::GamepadButton:
			return Input::GetGamepad(binding.gamepadIndex).IsDown(static_cast<GamepadButton>(binding.code));
		case InputBinding::Source::GamepadAxis:
		case InputBinding::Source::MouseDelta:
		case InputBinding::Source::MouseWheel:
			return GetBindingValue(binding) != 0.0f;
		}
		return false;
	}

	f32 InputActionMap::GetBindingValue(const InputBinding& binding) const {
		if (!ModsSatisfy(binding, Input::GetKeyMods())) {
			return 0.0f;
		}

		switch (binding.source) {
		case InputBinding::Source::Key:
			return Input::IsKeyDown(static_cast<Key>(binding.code)) ? binding.scale : 0.0f;
		case InputBinding::Source::MouseButton:
			return Input::IsMouseButtonDown(static_cast<MouseButton>(binding.code)) ? binding.scale : 0.0f;
		case InputBinding::Source::MouseDelta: {
			Vec2 delta = Input::GetMouseDelta();
			return (binding.code == 0 ? delta.x : delta.y) * binding.scale;
		}
		case InputBinding::Source::MouseWheel: {
			Vec2 wheel = Input::GetMouseWheel();
			return (binding.code == 0 ? wheel.x : wheel.y) * binding.scale;
		}
		case InputBinding::Source::GamepadButton:
			return Input::GetGamepad(binding.gamepadIndex).IsDown(static_cast<GamepadButton>(binding.code)) ? binding.scale : 0.0f;
		case InputBinding::Source::GamepadAxis:
			return Input::GetGamepad(binding.gamepadIndex).GetAxis(static_cast<GamepadAxis>(binding.code)) * binding.scale;
		}
		return 0.0f;
	}

	void InputActionMap::Evaluate() {
		if (!_enabled) {
			return;
		}
		bool seeding = _seedPending;
		_seedPending = false;

		const u64 pump = InputInternal::CurrentPumpSequence();
		if (_lastEvaluatePump != 0 && !_cadenceReported && pump - _lastEvaluatePump != 1) {
			_cadenceReported = true;
			InputInternal::ReportEvaluationCadence(pump - _lastEvaluatePump);
		}
		_lastEvaluatePump = pump;

		for (Action& action : _actions) {
			switch (action.type) {
			case ActionType::Button: {
				bool wasPressed = action.pressed;
				action.pressed = false;
				bool eventEdge = false;
				bool haveStreamless = false;
				for (const InputBinding& binding : action.bindings) {
					if (IsBindingActive(binding)) {
						action.pressed = true;
					}
					if (HasEventStream(binding.source)) {
						eventEdge = eventEdge || BindingPressedThisPump(binding);
					} else {
						haveStreamless = true;
					}
				}
				action.performed = eventEdge || (!seeding && haveStreamless && action.pressed && !wasPressed);
				break;
			}
			case ActionType::Axis1D: {
				f32 value = 0.0f;
				for (const InputBinding& binding : action.bindings) {
					value += GetBindingValue(binding);
				}
				action.axis = Vec2(value, 0.0f);
				action.pressed = value != 0.0f;
				action.performed = false;
				break;
			}
			case ActionType::Axis2D: {
				Vec2 value;
				for (const InputBinding& binding : action.bindings) {
					f32 contribution = GetBindingValue(binding);
					if (binding.component == 0) {
						value.x += contribution;
					} else {
						value.y += contribution;
					}
				}
				action.axis = value;
				action.pressed = value.x != 0.0f || value.y != 0.0f;
				action.performed = false;
				break;
			}
			}
		}
	}

	bool InputActionMap::IsPressed(ActionId action) const {
		return action < _actions.size() && _actions[action].pressed;
	}

	bool InputActionMap::WasPerformed(ActionId action) const {
		return action < _actions.size() && _actions[action].performed;
	}

	f32 InputActionMap::GetAxis1D(ActionId action) const {
		return action < _actions.size() ? _actions[action].axis.x : 0.0f;
	}

	Vec2 InputActionMap::GetAxis2D(ActionId action) const {
		return action < _actions.size() ? _actions[action].axis : Vec2();
	}

	u32 InputActionMap::GetActionCount() const {
		return static_cast<u32>(_actions.size());
	}

} // namespace Shalltear
