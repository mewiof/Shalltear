#pragma once

#if defined(SLT_ANIMATION_PUBLIC_API_ONLY)
#error "AnimatorController is a CLIENT of this package's public API (no private headers). Fix the public path if it's awkward."
#endif

#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	struct AnimationSettings {
		SLT_REFLECT_VALUE(AnimationSettings)
		SLT_REFLECT_CONSTRUCTIBLE()
		SLT_REFLECT_FIELD(defaultTransitionDuration)
		SLT_REFLECT_FIELD(evaluationBudget)
		SLT_REFLECT_FIELD(maxThrottleInterval, SLT_RANGE(2, 60))
		SLT_REFLECT_FIELD(interpolateThrottled)
		SLT_END_REFLECT()

		f32 defaultTransitionDuration = 0.25f;

		i32 evaluationBudget = 0;

		i32 maxThrottleInterval = 10;

		bool interpolateThrottled = true;
	};

	namespace AnimationImpl {

		const AnimationSettings& GetSettings();

		void SetDeclaredSettings(const AnimationSettings* declared);

	} // namespace AnimationImpl

} // namespace Shalltear
