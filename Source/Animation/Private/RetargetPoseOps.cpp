#include "RetargetPoseOps.h"

#include "PoseOps.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Memory.h"

namespace Shalltear {

	namespace RetargetPoseOps {

		bool SampleBindPose(const SkeletonAsset& skeleton, std::span<const i16> jointToBone,
							std::span<const Mat4> inverseBind, const Mat4& meshToModel,
							Vector<BoneTransform>& outPose) {
			const size_t boneCount = skeleton.parents.size();
			if (boneCount == 0) {
				return false;
			}

			Vector<Mat4> bindModel{GetMemoryResource(MemTag::Asset)};
			bindModel.assign(boneCount, Mat4::Identity());
			Vector<u8> bound{GetMemoryResource(MemTag::Asset)};
			bound.assign(boneCount, 0);

			size_t boundCount = 0;
			const size_t jointCount = Math::Min(jointToBone.size(), inverseBind.size());
			for (size_t joint = 0; joint < jointCount; ++joint) {
				const i16 bone = jointToBone[joint];
				if (bone < 0 || static_cast<size_t>(bone) >= boneCount) {
					continue;
				}
				if (bound[static_cast<size_t>(bone)]) {
					continue;
				}
				bindModel[static_cast<size_t>(bone)] = meshToModel * inverseBind[joint].Inverse();
				bound[static_cast<size_t>(bone)] = 1;
				++boundCount;
			}
			if (boundCount == 0) {
				return false;
			}

			Vector<Quat> modelRotation{GetMemoryResource(MemTag::Asset)};
			Vector<Vec3> modelPosition{GetMemoryResource(MemTag::Asset)};
			Vector<Vec3> modelScale{GetMemoryResource(MemTag::Asset)};
			modelRotation.assign(boneCount, Quat::Identity());
			modelPosition.assign(boneCount, Vec3());
			modelScale.assign(boneCount, Vec3::One());

			outPose.assign(boneCount, BoneTransform{});
			u32 undecomposable = 0;

			for (size_t bone = 0; bone < boneCount; ++bone) {
				const i16 parent = skeleton.parents[bone];
				const bool hasParent = parent >= 0 && static_cast<size_t>(parent) < bone;
				const Quat parentRotation = hasParent ? modelRotation[static_cast<size_t>(parent)] : Quat::Identity();
				const Vec3 parentPosition = hasParent ? modelPosition[static_cast<size_t>(parent)] : Vec3();
				const Vec3 parentScale = hasParent ? modelScale[static_cast<size_t>(parent)] : Vec3::One();

				BoneTransform local;
				if (bound[bone]) {
					Vec3 position;
					Quat rotation;
					Vec3 scale;
					if (!Mat4::DecomposeTRS(bindModel[bone], position, rotation, scale)) {
						++undecomposable;
					}
					modelRotation[bone] = rotation;
					modelPosition[bone] = position;
					modelScale[bone] = scale;

					local.rotation = hasParent ? (parentRotation.Conjugated() * rotation).Normalized()
											   : rotation.Normalized();
					local.position = hasParent ? AnimationImpl::SafeUnscale(
													 parentRotation.Conjugated() * (position - parentPosition), parentScale)
											   : position;
					local.scale = hasParent ? AnimationImpl::SafeScaleRatio(scale, parentScale) : scale;
				} else {
					local = bone < skeleton.restPose.size() ? skeleton.restPose[bone] : BoneTransform{};
					modelRotation[bone] = hasParent ? (parentRotation * local.rotation).Normalized()
													: local.rotation.Normalized();
					modelPosition[bone] = hasParent ? parentPosition + parentRotation * (parentScale * local.position)
													: local.position;
					modelScale[bone] = hasParent ? parentScale * local.scale : local.scale;
				}
				outPose[bone] = local;
			}

			if (undecomposable > 0) {
				SLT_LOG_WARN("Animation", "sampling a bind pose: {} joint(s) bind with shear or a collapsed axis (closest translation/rotation/scale used)", undecomposable);
			}
			return true;
		}

		bool CopyReferencePoseByName(const SkeletonAsset& from, std::span<const BoneTransform> fromPose,
									 const SkeletonAsset& to, Vector<BoneTransform>& outPose) {
			const u32 boneCount = to.GetBoneCount();
			if (boneCount == 0) {
				return false;
			}

			Vector<BoneTransform> copied{GetMemoryResource(MemTag::Asset)};
			copied.assign(to.restPose.begin(), to.restPose.end());
			copied.resize(boneCount);

			size_t matched = 0;
			for (u32 bone = 0; bone < boneCount; ++bone) {
				if (bone >= to.boneNames.size()) {
					break;
				}
				const i32 source = from.FindBone(to.boneNames[bone]);
				if (source < 0 || static_cast<size_t>(source) >= fromPose.size()) {
					continue;
				}
				copied[bone] = fromPose[static_cast<size_t>(source)];
				++matched;
			}

			if (matched == 0) {
				return false;
			}
			if (matched < boneCount) {
				SLT_LOG_WARN("Animation", "copying a rig stance: the source has {} of this rig's {} bone(s) by name (others use the file's transforms)",
							 matched, boneCount);
			}
			outPose = std::move(copied);
			return true;
		}

	} // namespace RetargetPoseOps

} // namespace Shalltear
