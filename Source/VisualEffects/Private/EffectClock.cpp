#include "EffectClock.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Shalltear::VisualEffectsImpl {

	namespace {

		constexpr f64 STEP_SLACK = 1e-4;

		constexpr f64 MOST_STEPS = 1e6;

		void AdvanceSource(f64& banked, f32 rawDelta, f64 step, f32 maxDelta, EffectClockSource& out) {
			const f32 raw = rawDelta > 0.0f ? rawDelta : 0.0f;
			out.delta = std::min(raw, maxDelta);
			if (!(step > 0.0)) {
				out.fixedDelta = out.delta;
				out.steps = 0;
				return;
			}
			banked += static_cast<f64>(raw);
			const f64 whole = std::min(std::floor(banked / step), MOST_STEPS);
			banked -= whole * step;
			if (!(banked >= 0.0) || banked >= step) {
				banked = std::fmod(std::max(banked, 0.0), step);
			}
			out.fixedDelta = static_cast<f32>(std::min(whole * step, static_cast<f64>(maxDelta)));
			const f64 fit = std::floor(static_cast<f64>(maxDelta) / step + STEP_SLACK);
			out.steps = static_cast<u32>(std::min(whole, fit));
		}

	} // anonymous namespace

	void EffectUpdates(const EffectTiming& timing, const EffectFrameTime& time, f32 rate, Vector<f32>& out) {
		const EffectClockSource& source = timing.ignoreTimeScale ? time.unscaled : time.scaled;
		if (!timing.fixedDeltaTime) {
			out.push_back(source.delta * rate);
			return;
		}
		if (timing.exactFixedTime && source.steps > 0) {
			for (u32 step = 0; step < source.steps; ++step) {
				out.push_back(time.step * rate);
			}
			return;
		}
		out.push_back(source.fixedDelta * rate);
	}

	EffectFrameTime EffectClock::Advance(f32 scaledDelta, f32 unscaledDelta, const VisualEffectsSettings& settings) {
		EffectFrameTime time;
		const f64 step = settings.fixedTimeStep > 0.0f ? static_cast<f64>(settings.fixedTimeStep) : 0.0;
		const f32 maxDelta = settings.maxDeltaTime > 0.0f ? settings.maxDeltaTime : std::numeric_limits<f32>::infinity();
		time.step = static_cast<f32>(step);
		AdvanceSource(_banked[0], scaledDelta, step, maxDelta, time.scaled);
		AdvanceSource(_banked[1], unscaledDelta, step, maxDelta, time.unscaled);
		return time;
	}

	void EffectClock::Reset() {
		_banked[0] = 0.0;
		_banked[1] = 0.0;
	}

} // namespace Shalltear::VisualEffectsImpl
