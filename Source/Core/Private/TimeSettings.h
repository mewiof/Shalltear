#pragma once

#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Time.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	inline constexpr f32 MIN_FIXED_DELTA_TIME = 0.0001f;

	struct TimeSettings {
		SLT_REFLECT_VALUE(TimeSettings)
		SLT_REFLECT_CONSTRUCTIBLE()
		SLT_REFLECT_FIELD(fixedDeltaTime)
		SLT_REFLECT_FIELD(maximumDeltaTime)
		SLT_END_REFLECT()

		f32 fixedDeltaTime = DEFAULT_FIXED_DELTA_TIME;
		f32 maximumDeltaTime = DEFAULT_MAXIMUM_DELTA_TIME;
	};

} // namespace Shalltear
