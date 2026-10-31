#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Input/Input.h"
#include "Shalltear/Core/Input/InputCodes.h"
#include "Shalltear/Core/Input/InputEvent.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear::InputInternal {

	struct State {
		Vector<InputEvent> frameEvents{GetMemoryResource(MemTag::Platform)};

		bool keyDown[static_cast<size_t>(Key::Count)] = {};
		bool keyPressed[static_cast<size_t>(Key::Count)] = {};
		bool keyReleased[static_cast<size_t>(Key::Count)] = {};

		bool mouseDown[static_cast<size_t>(MouseButton::Count)] = {};
		bool mousePressed[static_cast<size_t>(MouseButton::Count)] = {};
		bool mouseReleased[static_cast<size_t>(MouseButton::Count)] = {};

		Vec2I mouseScreenPos;
		Vec2I pumpStartScreenPos;
		bool havePumpStartPos = false;
		Vec2 mouseDelta;
		Vec2 wheel;

		bool rawDeltaActive = false;
		Vec2 rawDeltaAccum;

		GamepadState gamepads[Input::MAX_GAMEPADS];
		f64 gamepadNextProbe[Input::MAX_GAMEPADS] = {};

		u64 pumpSequence = 1;

		bool gameplayRouted = true;
		bool gameplayPointerRouted = true;
		bool applicationFocused = true;
		bool gameplayReadsBlocked = false;
		bool gameplayPointerBlocked = false;
		Vector<InputEvent> gameplayEvents{GetMemoryResource(MemTag::Platform)};
		bool routeBlockReported = false;
		bool pointerBlockReported = false;
	};

	State& GetState();

	void ResetState();

	void BeginPump();
	void EndPump();

	void PushEvent(const InputEvent& event);

	u64 CurrentPumpSequence();

	void ReportEvaluationCadence(u64 pumpsSinceLast);

	void ReleaseHeldInput(Handle<Window> window, Vec2I mousePosPx);

	KeyMod CurrentMods();

	void SetMouseScreenPosition(Vec2I screenPx);
	void ShiftMouseOrigin(Vec2I shiftPx);
	void AddRawMouseDelta(Vec2 deltaPx);
	void SetRawDeltaActive(bool active);

	void PollGamepads(bool applicationFocused);

	void SetApplicationFocused(bool focused);

	struct GameplayTickScope {
		GameplayTickScope();
		~GameplayTickScope();

		GameplayTickScope(const GameplayTickScope&) = delete;
		GameplayTickScope& operator=(const GameplayTickScope&) = delete;
	private:
		bool _previous;
		bool _previousPointer;
	};

} // namespace Shalltear::InputInternal
