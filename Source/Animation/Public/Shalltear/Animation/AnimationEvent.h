#pragma once

#include "Shalltear/Animation/AnimationAPI.h"

#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	class AnimationClip;

	struct AnimationEvent {
		StringHash name;

		Handle<AnimationClip> clip;

		i32 layer = 0;

		f32 time = 0.0f;

		Vec4 value{};
	};

	using AnimationEventFn = void (*)(const AnimationEvent& event, void* user);

} // namespace Shalltear
