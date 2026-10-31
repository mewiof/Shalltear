#pragma once

#include "EffectDescription.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	struct VisualEffectsSettings {
		SLT_REFLECT_VALUE(VisualEffectsSettings)
		SLT_REFLECT_CONSTRUCTIBLE()
		SLT_REFLECT_FIELD(fixedTimeStep, SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))
		SLT_REFLECT_FIELD(maxDeltaTime, SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))
		SLT_REFLECT_FIELD(batchEmptyLifetime, SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))
		SLT_END_REFLECT()

		f32 fixedTimeStep = 1.0f / 60.0f;
		f32 maxDeltaTime = 0.05f;
		u32 batchEmptyLifetime = 300;
	};

	namespace VisualEffectsImpl {

		inline constexpr std::string_view EFFECT_SETTINGS_PATH = "Effects";

		struct EffectClockSource {
			f32 delta = 0.0f;
			f32 fixedDelta = 0.0f;
			u32 steps = 0;
		};

		struct EffectFrameTime {
			EffectClockSource scaled;
			EffectClockSource unscaled;
			f32 step = 0.0f;
		};

		void EffectUpdates(const EffectTiming& timing, const EffectFrameTime& time, f32 rate, Vector<f32>& out);

		class EffectClock {
		public:
			EffectFrameTime Advance(f32 scaledDelta, f32 unscaledDelta, const VisualEffectsSettings& settings);
			void Reset();
		private:
			f64 _banked[2] = {};
		};

	} // namespace VisualEffectsImpl

} // namespace Shalltear
