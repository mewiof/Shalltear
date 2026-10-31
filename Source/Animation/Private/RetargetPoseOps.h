#pragma once

#if defined(SLT_ANIMATION_PUBLIC_API_ONLY)
#error "AnimatorController is a CLIENT of this package's public API (no private headers). Fix the public path if it's awkward."
#endif

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/SkeletonAsset.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear {

	namespace RetargetPoseOps {

		bool SampleBindPose(const SkeletonAsset& skeleton, std::span<const i16> jointToBone,
							std::span<const Mat4> inverseBind, const Mat4& meshToModel,
							Vector<BoneTransform>& outPose);

		bool CopyReferencePoseByName(const SkeletonAsset& from, std::span<const BoneTransform> fromPose,
									 const SkeletonAsset& to, Vector<BoneTransform>& outPose);

	} // namespace RetargetPoseOps

} // namespace Shalltear
