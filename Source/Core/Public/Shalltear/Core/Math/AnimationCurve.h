#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Types.h"

#include <initializer_list>

namespace Shalltear {

	enum class KeyframeTangentMode : u8 {
		Free = 0,
		Auto = 1,
		Linear = 2,
		Constant = 3,
		ClampedAuto = 4,
	};

	enum class AnimationCurveWrapMode : u8 {
		Clamp = 0,
		Loop = 1,
		PingPong = 2,
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::KeyframeTangentMode)
SLT_ENUM_VALUE(Free)
SLT_ENUM_VALUE(Auto)
SLT_ENUM_VALUE(Linear)
SLT_ENUM_VALUE(Constant)
SLT_ENUM_VALUE(ClampedAuto)
SLT_END_ENUM()

SLT_REFLECT_ENUM(::Shalltear::AnimationCurveWrapMode)
SLT_ENUM_VALUE(Clamp)
SLT_ENUM_VALUE(Loop)
SLT_ENUM_VALUE(PingPong)
SLT_END_ENUM()

namespace Shalltear {

	struct SHALLTEAR_CORE_API Keyframe {
		SLT_REFLECT_VALUE(Keyframe)
		SLT_REFLECT_FIELD(time)
		SLT_REFLECT_FIELD(value)
		SLT_REFLECT_FIELD(inTangent)
		SLT_REFLECT_FIELD(outTangent)
		SLT_REFLECT_FIELD(inTangentMode)
		SLT_REFLECT_FIELD(outTangentMode)
		SLT_REFLECT_FIELD(broken)
		SLT_END_REFLECT()

		f32 time = 0.0f;
		f32 value = 0.0f;
		f32 inTangent = 0.0f;
		f32 outTangent = 0.0f;
		KeyframeTangentMode inTangentMode = KeyframeTangentMode::Free;
		KeyframeTangentMode outTangentMode = KeyframeTangentMode::Free;
		bool broken = false;

		friend bool operator==(const Keyframe&, const Keyframe&) = default;
	};

	struct SHALLTEAR_CORE_API AnimationCurve {
		SLT_REFLECT_VALUE(AnimationCurve)
		SLT_REFLECT_ARRAY(keys)
		SLT_REFLECT_FIELD(preWrapMode)
		SLT_REFLECT_FIELD(postWrapMode)
		SLT_END_REFLECT()

		AnimationCurve() = default;
		explicit AnimationCurve(std::pmr::memory_resource* resource) : keys(resource) {}
		AnimationCurve(std::initializer_list<Keyframe> initial, std::pmr::memory_resource* resource = GetMemoryResource(MemTag::Core))
			: keys(initial, resource) {}

		AnimationCurve(const AnimationCurve& other)
			: keys(other.keys, other.keys.get_allocator().resource()), preWrapMode(other.preWrapMode), postWrapMode(other.postWrapMode) {}
		AnimationCurve& operator=(const AnimationCurve&) = default;
		AnimationCurve(AnimationCurve&&) = default;
		AnimationCurve& operator=(AnimationCurve&&) = default;

		[[nodiscard]] f32 Evaluate(f32 time) const;

		friend bool operator==(const AnimationCurve& a, const AnimationCurve& b) {
			return a.preWrapMode == b.preWrapMode && a.postWrapMode == b.postWrapMode && a.keys == b.keys;
		}

		Vector<Keyframe> keys{GetMemoryResource(MemTag::Core)};
		AnimationCurveWrapMode preWrapMode = AnimationCurveWrapMode::Clamp;
		AnimationCurveWrapMode postWrapMode = AnimationCurveWrapMode::Clamp;
	};

} // namespace Shalltear
