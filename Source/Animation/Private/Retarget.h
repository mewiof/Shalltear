#pragma once

#if defined(SLT_ANIMATION_PUBLIC_API_ONLY)
#error "AnimatorController is a CLIENT of this package's public API (no private headers). Fix the public path if it's awkward."
#endif

#include "Shalltear/Animation/RetargetProfile.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear {

	namespace AnimationImpl {

		enum class RetargetSides : u8 {
			AsAuthored = 0,
			Mirrored = 1,
		};

		struct RetargetBone {
			i16 sourceA = -1;

			i16 sourceB = -1;
			f32 blend = 0.0f;

			f32 positionScale = 1.0f;
		};

		struct RetargetGoal {
			i16 root = -1;
			i16 mid = -1;
			i16 tip = -1;

			i16 sourceTip = -1;

			Vec3 referenceAxis{};
		};

		struct RetargetMap {
			Vector<RetargetBone> bones{GetMemoryResource(MemTag::Asset)};

			Vector<Quat> sourceReference{GetMemoryResource(MemTag::Asset)};
			Vector<Quat> targetReference{GetMemoryResource(MemTag::Asset)};

			Vector<Vec3> sourceReferenceScale{GetMemoryResource(MemTag::Asset)};
			Vector<Vec3> targetReferenceScale{GetMemoryResource(MemTag::Asset)};

			Vector<Quat> sourceModel{GetMemoryResource(MemTag::Asset)};
			Vector<Quat> targetModel{GetMemoryResource(MemTag::Asset)};

			Vector<RetargetGoal> goals{GetMemoryResource(MemTag::Asset)};
			Vector<Vec3> sourceModelPosition{GetMemoryResource(MemTag::Asset)};
			Vector<Vec3> targetModelPosition{GetMemoryResource(MemTag::Asset)};

			Vector<Vec3> modelScale{GetMemoryResource(MemTag::Asset)};

			i16 sourceRoot = -1;
			i16 targetRoot = -1;

			f32 rigScale = 1.0f;

			u32 correspondedChains = 0;
			u32 correspondedBones = 0;

			RetargetSides sides = RetargetSides::AsAuthored;
		};

		bool BuildRetargetMap(const RetargetProfile& source, const RetargetProfile& target, RetargetMap& out,
							  RetargetSides sides);

		void SolveRetargetPose(RetargetMap& map, const RetargetProfile& source, const RetargetProfile& target,
							   std::span<const BoneTransform> sourcePose, std::span<BoneTransform> targetPose);

		BoneTransform SolveRetargetRoot(const RetargetMap& map, const RetargetProfile& source,
										const RetargetProfile& target, const BoneTransform& local);

		BoneTransform SolveRetargetRootDelta(const RetargetMap& map, const RetargetProfile& source,
											 const RetargetProfile& target, const BoneTransform& delta);

		f32 ChainReferenceLength(const RetargetProfile& profile, const RetargetProfile::Chain& chain);

		f32 ChainRootReferenceOffset(const RetargetProfile& profile, const RetargetProfile::Chain& chain);

		f32 ProfileReferenceScale(const RetargetProfile& profile);

	} // namespace AnimationImpl

	namespace Retarget {

		// caller owns the result
		AnimationClip* MirrorClip(const AnimationClip& clip, const RetargetProfile& profile);

	} // namespace Retarget

} // namespace Shalltear
