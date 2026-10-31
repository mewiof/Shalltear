#include "PoseOps.h"

#include "Shalltear/Core/Math/MathUtil.h"

#include <cmath>

namespace Shalltear {

	namespace AnimationImpl {

		namespace {

			Quat ShortestOffset(const Quat& from, const Quat& to) {
				Quat offset = (from * to.Inverse()).Normalized();
				return offset.w < 0.0f ? -offset : offset;
			}

			InertialChannel CaptureLinear(const Vec3& offset, const Vec3& offsetPrev, f32 step, f32 duration) {
				InertialChannel channel;
				const f32 magnitude = offset.Length();
				if (!(magnitude > Math::EPSILON) || !(step > 0.0f) || !(duration > 0.0f)) {
					return channel;
				}
				channel.direction = offset / magnitude;
				channel.x0 = magnitude;
				channel.v0 = (magnitude - Vec3::Dot(offsetPrev, channel.direction)) / step;
				channel.duration = duration;
				return channel;
			}

			void ClampDuration(InertialChannel& channel) {
				if (channel.v0 < 0.0f && channel.x0 > 0.0f) {
					const f32 bound = -5.0f * channel.x0 / channel.v0;
					channel.duration = Math::Min(channel.duration, bound);
				}
			}

		} // anonymous namespace

		Vec3 SafeScaleRatio(const Vec3& value, const Vec3& reference) {
			return Vec3(Math::Abs(reference.x) > Math::EPSILON ? value.x / reference.x : 1.0f,
						Math::Abs(reference.y) > Math::EPSILON ? value.y / reference.y : 1.0f,
						Math::Abs(reference.z) > Math::EPSILON ? value.z / reference.z : 1.0f);
		}

		Vec3 SafeUnscale(const Vec3& value, const Vec3& scale) {
			return Vec3(Math::Abs(scale.x) > Math::EPSILON ? value.x / scale.x : 0.0f,
						Math::Abs(scale.y) > Math::EPSILON ? value.y / scale.y : 0.0f,
						Math::Abs(scale.z) > Math::EPSILON ? value.z / scale.z : 0.0f);
		}

		BoneTransform BlendBone(const BoneTransform& a, const BoneTransform& b, f32 weight) {
			if (!(weight > 0.0f)) {
				return a;
			}
			if (weight >= 1.0f) {
				return b;
			}
			BoneTransform out;
			out.position = Vec3::Lerp(a.position, b.position, weight);
			out.rotation = Quat::Slerp(a.rotation, b.rotation, weight);
			out.scale = Vec3::Lerp(a.scale, b.scale, weight);
			return out;
		}

		BoneTransform AddBone(const BoneTransform& base, const BoneTransform& add,
							  const BoneTransform& reference, f32 weight) {
			if (!(weight > 0.0f)) {
				return base;
			}
			BoneTransform out = base;
			out.position = base.position + (add.position - reference.position) * weight;

			const Quat delta = (add.rotation * reference.rotation.Inverse()).Normalized();
			out.rotation = (Quat::Slerp(Quat::Identity(), delta, weight) * base.rotation).Normalized();

			out.scale = base.scale * Vec3::Lerp(Vec3::One(), SafeScaleRatio(add.scale, reference.scale), weight);
			return out;
		}

		f32 InertialDecay(const InertialChannel& channel, f32 elapsed) {
			if (channel.IsInert() || !(elapsed < channel.duration)) {
				return 0.0f;
			}
			if (!(elapsed > 0.0f)) {
				return channel.x0;
			}
			const f32 t1 = channel.duration;
			const f32 x0 = channel.x0;
			const f32 v0 = channel.v0;

			const f32 t1Sq = t1 * t1;
			const f32 a0 = (-8.0f * v0 * t1 - 20.0f * x0) / t1Sq;
			const f32 t1Cu = t1Sq * t1;
			const f32 t1Qu = t1Cu * t1;
			const f32 t1Qi = t1Qu * t1;
			const f32 a = -(a0 * t1Sq + 6.0f * v0 * t1 + 12.0f * x0) / (2.0f * t1Qi);
			const f32 b = (3.0f * a0 * t1Sq + 16.0f * v0 * t1 + 30.0f * x0) / (2.0f * t1Qu);
			const f32 c = -(3.0f * a0 * t1Sq + 12.0f * v0 * t1 + 20.0f * x0) / (2.0f * t1Cu);

			const f32 t = elapsed;
			return ((((a * t + b) * t + c) * t + 0.5f * a0) * t + v0) * t + x0;
		}

		bool CaptureInertialBone(const BoneTransform& source, const BoneTransform& target,
								 const BoneTransform& sourcePrev, const BoneTransform& targetPrev,
								 f32 step, f32 duration, InertialBone& out) {
			out.position = CaptureLinear(source.position - target.position,
										 sourcePrev.position - targetPrev.position, step, duration);
			out.scale = CaptureLinear(source.scale - target.scale,
									  sourcePrev.scale - targetPrev.scale, step, duration);

			out.rotation = InertialChannel{};
			if (step > 0.0f && duration > 0.0f) {
				const Quat offset = ShortestOffset(source.rotation, target.rotation);
				const Vec3 imaginary(offset.x, offset.y, offset.z);
				const f32 sine = imaginary.Length();
				if (sine > Math::EPSILON) {
					out.rotation.direction = imaginary / sine;
					out.rotation.x0 = 2.0f * std::atan2(sine, Math::Clamp(offset.w, -1.0f, 1.0f)) * Math::RAD_TO_DEG;

					const Quat before = ShortestOffset(sourcePrev.rotation, targetPrev.rotation);
					const f32 alongAxis = Vec3::Dot(Vec3(before.x, before.y, before.z), out.rotation.direction);
					const f32 anglePrev = 2.0f * std::atan2(alongAxis, Math::Clamp(before.w, -1.0f, 1.0f)) * Math::RAD_TO_DEG;
					out.rotation.v0 = (out.rotation.x0 - anglePrev) / step;
					out.rotation.duration = duration;
				}
			}

			ClampDuration(out.position);
			ClampDuration(out.rotation);
			ClampDuration(out.scale);
			return !out.position.IsInert() || !out.rotation.IsInert() || !out.scale.IsInert();
		}

		BoneTransform ApplyInertialBone(const BoneTransform& target, const InertialBone& offset, f32 elapsed) {
			BoneTransform out = target;
			out.position = target.position + offset.position.direction * InertialDecay(offset.position, elapsed);
			out.scale = target.scale + offset.scale.direction * InertialDecay(offset.scale, elapsed);
			const f32 degrees = InertialDecay(offset.rotation, elapsed);
			if (degrees != 0.0f) {
				out.rotation = (Quat::AngleAxis(degrees, offset.rotation.direction) * target.rotation).Normalized();
			}
			return out;
		}

		BoneTransform DeltaBetween(const BoneTransform& from, const BoneTransform& to) {
			const Quat inverse = from.rotation.Inverse();
			BoneTransform delta;
			delta.position = inverse * (to.position - from.position);
			delta.rotation = (inverse * to.rotation).Normalized();
			return delta;
		}

		BoneTransform ComposeDelta(const BoneTransform& first, const BoneTransform& second) {
			BoneTransform out;
			out.position = first.position + first.rotation * second.position;
			out.rotation = (first.rotation * second.rotation).Normalized();
			return out;
		}

		BoneTransform ScaleDelta(const BoneTransform& delta, f32 weight) {
			BoneTransform out = BlendBone(BoneTransform{}, delta, weight);
			out.scale = Vec3(1.0f, 1.0f, 1.0f);
			return out;
		}

		BoneTransform ConjugateDelta(const BoneTransform& anchor, const BoneTransform& delta) {
			BoneTransform out;
			out.rotation = (anchor.rotation * delta.rotation * anchor.rotation.Inverse()).Normalized();
			out.position = anchor.position + anchor.rotation * delta.position - out.rotation * anchor.position;
			return out;
		}

	} // namespace AnimationImpl

} // namespace Shalltear
