#pragma once

#include "Shalltear/Core/Math/AnimationCurve.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

namespace Shalltear::AnimationCurveOps {

	enum class KeySide : u8 {
		In,
		Out,
		Both,
	};

	SHALLTEAR_EDITOR_API i32 AddKey(AnimationCurve& curve, f32 time, f32 value);

	SHALLTEAR_EDITOR_API i32 MoveKey(AnimationCurve& curve, i32 index, f32 time, f32 value);

	SHALLTEAR_EDITOR_API bool RemoveKey(AnimationCurve& curve, i32 index);

	SHALLTEAR_EDITOR_API void SetSmooth(AnimationCurve& curve, i32 index, KeyframeTangentMode mode);
	SHALLTEAR_EDITOR_API void Flatten(AnimationCurve& curve, i32 index);
	SHALLTEAR_EDITOR_API void SetBroken(AnimationCurve& curve, i32 index, bool broken);
	SHALLTEAR_EDITOR_API void SetSideMode(AnimationCurve& curve, i32 index, KeySide side, KeyframeTangentMode mode);

	SHALLTEAR_EDITOR_API void SetTangent(AnimationCurve& curve, i32 index, KeySide side, f32 slope);

	SHALLTEAR_EDITOR_API void UpdateTangents(AnimationCurve& curve);

} // namespace Shalltear::AnimationCurveOps
