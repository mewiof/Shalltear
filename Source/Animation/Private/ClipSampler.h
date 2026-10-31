#pragma once

#if defined(SLT_ANIMATION_PUBLIC_API_ONLY)
#error "AnimatorController is a CLIENT of this package's public API (no private headers). Fix the public path if it's awkward."
#endif

#include "Shalltear/Animation/AnimationClip.h"

namespace Shalltear {

	namespace AnimationImpl {

		f32 WrapTime(f32 time, f32 length, AnimationLoopMode loop);

		struct ClipSegment {
			f32 begin = 0.0f;
			f32 end = 0.0f;
		};

		inline constexpr u32 ANIMATION_MAX_CLIP_RUNS = 64;

		u32 SplitInterval(f32 from, f32 to, f32 length, AnimationLoopMode loop,
						  ClipSegment* out, u32 capacity, bool* truncated = nullptr);

		// cubic keys store in tangent, value, out tangent
		inline size_t KeyCount(const AnimationClip::Track& track) {
			const size_t stride = AnimationValuesPerKey(track.interp);
			const size_t keys = stride == 0 ? 0 : track.values.size() / stride;
			return keys < track.times.size() ? keys : track.times.size();
		}

		inline Vec4 KeyValue(const AnimationClip::Track& track, size_t key) {
			const size_t stride = AnimationValuesPerKey(track.interp);
			const size_t at = key * stride + (stride == 3u ? 1u : 0u);
			return at < track.values.size() ? track.values[at] : Vec4{};
		}

		inline Vec4 KeyInTangent(const AnimationClip::Track& track, size_t key) {
			if (AnimationValuesPerKey(track.interp) != 3u) {
				return Vec4{};
			}
			const size_t at = key * 3u;
			return at < track.values.size() ? track.values[at] : Vec4{};
		}

		inline Vec4 KeyOutTangent(const AnimationClip::Track& track, size_t key) {
			if (AnimationValuesPerKey(track.interp) != 3u) {
				return Vec4{};
			}
			const size_t at = key * 3u + 2u;
			return at < track.values.size() ? track.values[at] : Vec4{};
		}

		Vec4 SampleTrack(const AnimationClip::Track& track, f32 time);

		void SampleForInsert(const AnimationClip::Track& track, f32 time, Vec4& value, Vec4& tangent);

	} // namespace AnimationImpl

} // namespace Shalltear
