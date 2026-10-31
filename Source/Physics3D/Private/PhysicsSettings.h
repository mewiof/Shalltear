#pragma once

#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	struct PhysicsSettings {
		SLT_REFLECT_VALUE(PhysicsSettings)
		SLT_REFLECT_CONSTRUCTIBLE()
		SLT_REFLECT_FIELD(gravityX)
		SLT_REFLECT_FIELD(gravityY)
		SLT_REFLECT_FIELD(gravityZ)
		SLT_REFLECT_FIELD(bounceThreshold)
		SLT_REFLECT_FIELD(hitEventThreshold)
		SLT_REFLECT_FIELD(contactHertz)
		SLT_REFLECT_FIELD(contactDampingRatio)
		SLT_REFLECT_FIELD(contactSpeed)
		SLT_REFLECT_FIELD(maxLinearSpeed)
		SLT_REFLECT_FIELD(enableSleeping)
		SLT_REFLECT_FIELD(enableContinuous)
		SLT_REFLECT_FIELD(substeps)
		SLT_REFLECT_FIELD(workerCount)
		SLT_REFLECT_FIELD(queriesHitTriggers)
		SLT_REFLECT_FIELD(categories)
		SLT_END_REFLECT()

		f32 gravityX = 0.0f;
		f32 gravityY = -9.81f;
		f32 gravityZ = 0.0f;

		f32 bounceThreshold = 1.0f;

		f32 hitEventThreshold = 1.0f;

		f32 contactHertz = 30.0f;
		f32 contactDampingRatio = 10.0f;
		f32 contactSpeed = 3.0f;

		f32 maxLinearSpeed = 400.0f;

		bool enableSleeping = true;
		bool enableContinuous = true;

		i32 substeps = 4;

		i32 workerCount = 0;

		bool queriesHitTriggers = true;

		// comma separated category names
		str categories = "Default";
	};

} // namespace Shalltear
