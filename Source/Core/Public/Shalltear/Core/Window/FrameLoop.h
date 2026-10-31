#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	class SHALLTEAR_CORE_API FixedTimestep {
	public:
		explicit FixedTimestep(f64 stepSeconds, f64 maximumDeltaSeconds);

		void Configure(f64 stepSeconds, f64 maximumDeltaSeconds);

		u32 Advance(f64 frameDeltaSeconds);

		f32 GetAlpha() const;

		f64 GetStepSeconds() const { return _stepSeconds; }
		u32 GetMaxStepsPerFrame() const { return _maxStepsPerFrame; }

		void Reset();
	private:
		f64 _stepSeconds;
		f64 _accumulator = 0.0;
		u32 _maxStepsPerFrame;
	};

} // namespace Shalltear
