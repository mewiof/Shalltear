#include "Shalltear/Core/Window/FrameLoop.h"

#include "Shalltear/Core/Assert.h"

namespace Shalltear {

	FixedTimestep::FixedTimestep(f64 stepSeconds, f64 maximumDeltaSeconds)
		: _stepSeconds(stepSeconds), _maxStepsPerFrame(1) {
		Configure(stepSeconds, maximumDeltaSeconds);
	}

	void FixedTimestep::Configure(f64 stepSeconds, f64 maximumDeltaSeconds) {
		SLT_CORE_ASSERT_MSG(stepSeconds > 0.0, "FixedTimestep step must be positive");
		SLT_CORE_ASSERT_MSG(maximumDeltaSeconds > 0.0, "FixedTimestep maximum delta must be positive");
		_stepSeconds = stepSeconds;

		const f64 steps = maximumDeltaSeconds / stepSeconds + 1e-6;
		_maxStepsPerFrame = steps < 1.0 ? 1u : static_cast<u32>(steps);
	}

	u32 FixedTimestep::Advance(f64 frameDeltaSeconds) {
		if (frameDeltaSeconds > 0.0) {
			_accumulator += frameDeltaSeconds;
		}

		u32 steps = static_cast<u32>(_accumulator / _stepSeconds);
		if (steps > _maxStepsPerFrame) {
			steps = _maxStepsPerFrame;
			_accumulator = static_cast<f64>(steps) * _stepSeconds;
		}

		_accumulator -= static_cast<f64>(steps) * _stepSeconds;
		return steps;
	}

	f32 FixedTimestep::GetAlpha() const {
		return static_cast<f32>(_accumulator / _stepSeconds);
	}

	void FixedTimestep::Reset() {
		_accumulator = 0.0;
	}

} // namespace Shalltear
