#pragma once

#if defined(SLT_ANIMATION_PUBLIC_API_ONLY)
#error "AnimatorController is a CLIENT of this package's public API (no private headers). Fix the public path if it's awkward."
#endif

#include "ClipSampler.h"

#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear {

	namespace AnimationClipOps {

		struct TrackAddress {
			AnimationTrackKind kind = AnimationTrackKind::Property;
			std::string_view objectPath;
			StringHash componentType;
			std::string_view name;
			u8 component = ANIMATION_WHOLE_VALUE;
		};

		i32 FindTrack(const AnimationClip& clip, const TrackAddress& address);

		i32 AddTrack(AnimationClip& clip, const TrackAddress& address, AnimationInterpolation interp);

		bool RemoveTrack(AnimationClip& clip, i32 track);

		bool SetTrackName(AnimationClip& clip, i32 track, std::string_view name);

		void SliceRange(const AnimationClip& source, f32 start, f32 end, AnimationClip& out);

		i32 KeyAt(const AnimationClip::Track& track, f32 time, f32 tolerance);

		i32 AddKey(AnimationClip& clip, i32 track, f32 time);

		i32 MoveKey(AnimationClip& clip, i32 track, i32 key, f32 time);

		bool SetKeyValue(AnimationClip& clip, i32 track, i32 key, const Vec4& value);

		bool RemoveKey(AnimationClip& clip, i32 track, i32 key);

		bool SetKeyAsset(AnimationClip& clip, i32 track, i32 key, const Guid& asset);

		Vec4 AutoTangent(const AnimationClip::Track& track, i32 key);

		bool SetInterpolation(AnimationClip& clip, i32 track, AnimationInterpolation interp);

		bool SetKeyTangents(AnimationClip& clip, i32 track, i32 key, const Vec4& in, const Vec4& out);

		f32 DerivedLength(const AnimationClip& clip);

		bool RecomputeLength(AnimationClip& clip);

		f32 SnapTime(const AnimationClip& clip, f32 time);

	} // namespace AnimationClipOps

} // namespace Shalltear
