#pragma once

#if defined(SLT_ANIMATION_PUBLIC_API_ONLY)
#error "AnimatorController is a CLIENT of this package's public API (no private headers). Fix the public path if it's awkward."
#endif

#include "Shalltear/Core/Graphics/SkeletonAsset.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	namespace AnimationImpl {

		Vec3 SafeScaleRatio(const Vec3& value, const Vec3& reference);

		Vec3 SafeUnscale(const Vec3& value, const Vec3& scale);

		BoneTransform BlendBone(const BoneTransform& a, const BoneTransform& b, f32 weight);

		BoneTransform AddBone(const BoneTransform& base, const BoneTransform& add,
							  const BoneTransform& reference, f32 weight);

		struct InertialChannel {
			Vec3 direction{};

			f32 x0 = 0.0f;

			f32 v0 = 0.0f;

			f32 duration = 0.0f;

			bool IsInert() const { return !(duration > 0.0f) || (x0 == 0.0f && v0 == 0.0f); }
		};

		struct InertialBone {
			i16 bone = -1;
			InertialChannel position;
			InertialChannel rotation;
			InertialChannel scale;
		};

		f32 InertialDecay(const InertialChannel& channel, f32 elapsed);

		bool CaptureInertialBone(const BoneTransform& source, const BoneTransform& target,
								 const BoneTransform& sourcePrev, const BoneTransform& targetPrev,
								 f32 step, f32 duration, InertialBone& out);

		BoneTransform ApplyInertialBone(const BoneTransform& target, const InertialBone& offset, f32 elapsed);

		BoneTransform DeltaBetween(const BoneTransform& from, const BoneTransform& to);

		BoneTransform ComposeDelta(const BoneTransform& first, const BoneTransform& second);

		BoneTransform ScaleDelta(const BoneTransform& delta, f32 weight);

		BoneTransform ConjugateDelta(const BoneTransform& anchor, const BoneTransform& delta);

	} // namespace AnimationImpl

} // namespace Shalltear
