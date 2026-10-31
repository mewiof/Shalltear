#pragma once

#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Types.h"

#include <cmath>

namespace Shalltear {

	struct PhysicsSpringTuning {
		f32 hertz = 0.0f;
		f32 dampingRatio = 0.0f;
	};

	inline PhysicsSpringTuning ConvertSpring(f32 stiffness, f32 damper, f32 effectiveMass) {
		PhysicsSpringTuning tuning;
		if (stiffness <= 0.0f || effectiveMass <= 0.0f) {
			return tuning;
		}

		tuning.hertz = std::sqrt(stiffness / effectiveMass) / Math::TAU;

		const f32 critical = 2.0f * std::sqrt(stiffness * effectiveMass);
		if (critical > 0.0f) {
			tuning.dampingRatio = damper / critical;
		}
		return tuning;
	}

	inline f32 EffectiveMassOf(f32 inverseMassA, f32 inverseMassB) {
		const f32 sum = inverseMassA + inverseMassB;
		return sum > 0.0f ? 1.0f / sum : 0.0f;
	}

	inline constexpr f32 JOINT_ANGULAR_LIMIT = 0.99f * Math::PI;

	inline constexpr f32 JOINT_CONE_LIMIT = 0.99f * Math::PI;

	inline bool ClampJointAngle(f32& value, f32 lower, f32 upper) {
		const f32 clamped = Math::Clamp(value, lower, upper);
		const bool moved = Math::Abs(clamped - value) > 1e-4f;
		value = clamped;
		return moved;
	}

	inline bool ClampJointAngularLimit(f32& lower, f32& upper) {
		if (lower > upper) {
			const f32 swap = lower;
			lower = upper;
			upper = swap;
		}
		const bool movedLower = ClampJointAngle(lower, -JOINT_ANGULAR_LIMIT, JOINT_ANGULAR_LIMIT);
		const bool movedUpper = ClampJointAngle(upper, -JOINT_ANGULAR_LIMIT, JOINT_ANGULAR_LIMIT);
		return movedLower || movedUpper;
	}

	inline bool ClampJointConeAngles(f32& angleX, f32& angleY) {
		const bool movedX = ClampJointAngle(angleX, 0.0f, JOINT_CONE_LIMIT);
		const bool movedY = ClampJointAngle(angleY, 0.0f, JOINT_CONE_LIMIT);
		return movedX || movedY;
	}

	inline constexpr f32 JOINT_MIN_LENGTH = 0.005f;

} // namespace Shalltear
