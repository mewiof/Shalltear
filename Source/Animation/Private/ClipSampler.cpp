#include "ClipSampler.h"

#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Math/Quat.h"

#include <algorithm>
#include <cmath>

namespace Shalltear {

	namespace AnimationImpl {

		namespace {

			size_t SegmentAt(const Vector<f32>& times, f32 time) {
				auto after = std::upper_bound(times.begin(), times.end(), time);
				return static_cast<size_t>(after - times.begin()) - 1;
			}

			Vec4 Mix(const AnimationClip::Track& track, const Vec4& a, const Vec4& b, f32 t) {
				if (track.kind != AnimationTrackKind::BoneRotation) {
					return Vec4::LerpUnclamped(a, b, t);
				}
				const Quat mixed = Quat::SlerpUnclamped(Quat(a.x, a.y, a.z, a.w), Quat(b.x, b.y, b.z, b.w), t);
				return Vec4(mixed.x, mixed.y, mixed.z, mixed.w);
			}

			Vec4 Hermite(const Vec4& v0, const Vec4& outTangent0, const Vec4& v1, const Vec4& inTangent1, f32 t, f32 span) {
				const Math::HermiteWeights w = Math::HermiteBasis(t);
				return v0 * w.h00 + outTangent0 * (w.h10 * span) + v1 * w.h01 + inTangent1 * (w.h11 * span);
			}

		} // anonymous namespace

		f32 WrapTime(f32 time, f32 length, AnimationLoopMode loop) {
			if (!(length > 0.0f) || !Math::IsFinite(time)) {
				return 0.0f;
			}
			switch (loop) {
			case AnimationLoopMode::Loop: {
				f32 wrapped = std::fmod(time, length);
				return wrapped < 0.0f ? wrapped + length : wrapped;
			}
			case AnimationLoopMode::PingPong: {
				const f32 period = length * 2.0f;
				f32 wrapped = std::fmod(time, period);
				if (wrapped < 0.0f) {
					wrapped += period;
				}
				return wrapped <= length ? wrapped : period - wrapped;
			}
			case AnimationLoopMode::None:
			default:
				return Math::Clamp(time, 0.0f, length);
			}
		}

		u32 SplitInterval(f32 from, f32 to, f32 length, AnimationLoopMode loop,
						  ClipSegment* out, u32 capacity, bool* truncated) {
			u32 written = 0;
			if (truncated) {
				*truncated = false;
			}
			if (!out || capacity == 0 || !(length > 0.0f) || !Math::IsFinite(from) || !Math::IsFinite(to) || from == to) {
				return 0;
			}

			if (loop == AnimationLoopMode::None) {
				const f32 begin = Math::Clamp(from, 0.0f, length);
				const f32 end = Math::Clamp(to, 0.0f, length);
				if (begin != end) {
					out[written++] = ClipSegment{begin, end};
				}
				return written;
			}

			const bool forward = to > from;
			const f64 lengthWide = static_cast<f64>(length);
			const f64 fromRun = std::floor(static_cast<f64>(from) / lengthWide);
			const f64 toRun = std::floor(static_cast<f64>(to) / lengthWide);

			f64 span = forward ? toRun - fromRun : fromRun - toRun;
			bool clipped = false;
			if (!(span > 0.0)) {
				span = 0.0;
			}
			if (span > static_cast<f64>(capacity - 1)) {
				span = static_cast<f64>(capacity - 1);
				clipped = true;
				if (truncated) {
					*truncated = true;
				}
			}

			const f64 firstRun = forward ? toRun - span : toRun + span;
			const f32 entry = clipped
								  ? (forward ? 0.0f : length)
								  : Math::Clamp(static_cast<f32>(static_cast<f64>(from) - firstRun * lengthWide), 0.0f, length);
			const f32 exit = Math::Clamp(static_cast<f32>(static_cast<f64>(to) - toRun * lengthWide), 0.0f, length);

			const u32 runs = static_cast<u32>(span) + 1u;
			for (u32 index = 0; index < runs; ++index) {
				const f32 begin = index == 0 ? entry : (forward ? 0.0f : length);
				const f32 end = index + 1 == runs ? exit : (forward ? length : 0.0f);
				if (forward ? !(end > begin) : !(end < begin)) {
					continue;
				}
				const f64 run = forward ? firstRun + static_cast<f64>(index) : firstRun - static_cast<f64>(index);

				if (loop == AnimationLoopMode::PingPong && std::fmod(std::fabs(run), 2.0) >= 1.0) {
					out[written++] = ClipSegment{length - begin, length - end};
				} else {
					out[written++] = ClipSegment{begin, end};
				}
			}
			return written;
		}

		Vec4 SampleTrack(const AnimationClip::Track& track, f32 time) {
			const size_t count = KeyCount(track);
			if (count == 0) {
				return Vec4{};
			}
			if (!(time > track.times[0])) {
				return KeyValue(track, 0);
			}
			if (!(time < track.times[count - 1])) {
				return KeyValue(track, count - 1);
			}

			size_t index = SegmentAt(track.times, time);
			if (index >= count - 1) {
				return KeyValue(track, count - 1);
			}
			const f32 t0 = track.times[index];
			const f32 t1 = track.times[index + 1];
			const f32 span = t1 - t0;
			const f32 alpha = span > 0.0f ? (time - t0) / span : 0.0f;

			switch (track.interp) {
			case AnimationInterpolation::Step:
				return KeyValue(track, index);

			case AnimationInterpolation::CubicSpline: {
				const Vec4 sampled = Hermite(KeyValue(track, index), KeyOutTangent(track, index),
											 KeyValue(track, index + 1), KeyInTangent(track, index + 1), alpha, span);
				if (track.kind != AnimationTrackKind::BoneRotation) {
					return sampled;
				}
				const Quat unit = Quat(sampled.x, sampled.y, sampled.z, sampled.w).Normalized();
				return Vec4(unit.x, unit.y, unit.z, unit.w);
			}

			case AnimationInterpolation::Linear:
			default:
				return Mix(track, KeyValue(track, index), KeyValue(track, index + 1), alpha);
			}
		}

		void SampleForInsert(const AnimationClip::Track& track, f32 time, Vec4& value, Vec4& tangent) {
			value = SampleTrack(track, time);
			tangent = Vec4{};
			if (AnimationValuesPerKey(track.interp) != 3u) {
				return;
			}
			const size_t count = KeyCount(track);
			if (count < 2 || !(time > track.times[0]) || !(time < track.times[count - 1])) {
				return;
			}
			const size_t index = SegmentAt(track.times, time);
			if (index + 1 >= count) {
				return;
			}
			const f32 span = track.times[index + 1] - track.times[index];
			if (!(span > 0.0f)) {
				return;
			}
			const f32 u = (time - track.times[index]) / span;
			const f32 u2 = u * u;
			const f32 d00 = 6.0f * u2 - 6.0f * u;
			const f32 d10 = 3.0f * u2 - 4.0f * u + 1.0f;
			const f32 d01 = -6.0f * u2 + 6.0f * u;
			const f32 d11 = 3.0f * u2 - 2.0f * u;
			const Vec4 v0 = KeyValue(track, index);
			const Vec4 v1 = KeyValue(track, index + 1);
			const Vec4 m0 = KeyOutTangent(track, index);
			const Vec4 m1 = KeyInTangent(track, index + 1);
			const Vec4 slope = (v0 * d00 + v1 * d01) * (1.0f / span) + m0 * d10 + m1 * d11;
			tangent = slope;
		}

	} // namespace AnimationImpl

} // namespace Shalltear
