#include "Retarget.h"

#include "ClipSampler.h"
#include "PoseOps.h"

#include "Shalltear/Animation/AnimationClip.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math/MathUtil.h"

#include <algorithm>
#include <cmath>

namespace Shalltear {

	namespace AnimationImpl {

		namespace {

			Vec3 ReflectDisplacement(const Vec3& value) { return Vec3(-value.x, value.y, value.z); }

			Quat ReflectRotation(const Quat& value) { return Quat(value.x, -value.y, -value.z, value.w); }

			void ComposeReference(const SkeletonAsset& skeleton, std::span<const BoneTransform> pose,
								  Vector<Quat>& rotations, Vector<Vec3>& positions, Vector<Vec3>& scales) {
				const size_t count = skeleton.parents.size();
				rotations.assign(count, Quat::Identity());
				positions.assign(count, Vec3());
				scales.assign(count, Vec3::One());

				for (size_t bone = 0; bone < count; ++bone) {
					const BoneTransform& local = bone < pose.size() ? pose[bone] : BoneTransform{};
					const i16 parent = skeleton.parents[bone];
					if (parent < 0 || static_cast<size_t>(parent) >= bone) {
						rotations[bone] = local.rotation.Normalized();
						positions[bone] = local.position;
						scales[bone] = local.scale;
						continue;
					}
					const size_t up = static_cast<size_t>(parent);
					rotations[bone] = (rotations[up] * local.rotation).Normalized();
					positions[bone] = positions[up] + rotations[up] * (scales[up] * local.position);
					scales[bone] = scales[up] * local.scale;
				}
			}

			void ComposeModelPositions(const SkeletonAsset& skeleton, std::span<const BoneTransform> pose,
									   const Vector<Quat>& modelRotation, Vector<Vec3>& scales, Vector<Vec3>& out) {
				const size_t count = Math::Min(out.size(), skeleton.parents.size());
				for (size_t bone = 0; bone < count; ++bone) {
					const BoneTransform& local = bone < pose.size() ? pose[bone] : BoneTransform{};
					const i16 parent = skeleton.parents[bone];
					if (parent < 0 || static_cast<size_t>(parent) >= bone) {
						out[bone] = local.position;
						scales[bone] = local.scale;
						continue;
					}
					const size_t up = static_cast<size_t>(parent);
					const Quat upRotation = up < modelRotation.size() ? modelRotation[up] : Quat::Identity();
					out[bone] = out[up] + upRotation * (scales[up] * local.position);
					scales[bone] = scales[up] * local.scale;
				}
			}

			i16 FirstRoot(const SkeletonAsset& skeleton) {
				for (size_t bone = 0; bone < skeleton.parents.size(); ++bone) {
					if (skeleton.parents[bone] < 0) {
						return static_cast<i16>(bone);
					}
				}
				return -1;
			}

			Vec3 ChainReferenceDirection(const Vector<Vec3>& positions, const RetargetProfile::Chain& chain) {
				if (chain.bones.size() < 2) {
					return Vec3();
				}
				const i16 root = chain.bones.front();
				const i16 tip = chain.bones.back();
				if (root < 0 || tip < 0 || static_cast<size_t>(root) >= positions.size() ||
					static_cast<size_t>(tip) >= positions.size()) {
					return Vec3();
				}
				const Vec3 span = positions[static_cast<size_t>(tip)] - positions[static_cast<size_t>(root)];
				return span.Length() > 1e-6f ? span.Normalized() : Vec3();
			}

			constexpr f32 REFERENCE_STANCE_WARN_DEGREES = 60.0f;

			bool ChainIsInRange(const RetargetProfile::Chain& chain, size_t boneCount) {
				for (i16 bone : chain.bones) {
					if (bone < 0 || static_cast<size_t>(bone) >= boneCount) {
						return false;
					}
				}
				return !chain.bones.empty();
			}

			f32 SafeRatio(f32 target, f32 source) {
				return source > Math::EPSILON && target > Math::EPSILON ? target / source : 1.0f;
			}

			Quat RotationBetween(const Vec3& from, const Vec3& to) {
				const Vec3 a = from.Normalized();
				const Vec3 b = to.Normalized();
				const f32 degrees = Vec3::Angle(a, b);
				if (!(degrees > Math::EPSILON)) {
					return Quat::Identity();
				}
				Vec3 axis = Vec3::Cross(a, b);
				if (axis.LengthSquared() <= Math::EPSILON * Math::EPSILON) {
					axis = Vec3::Cross(a, Vec3::Up());
					if (axis.LengthSquared() <= Math::EPSILON * Math::EPSILON) {
						axis = Vec3::Cross(a, Vec3::Right());
					}
				}
				return Quat::AngleAxis(degrees, axis.Normalized());
			}

			Vec3 BendAxis(const Vec3& root, const Vec3& mid, const Vec3& tip) {
				const Vec3 axis = Vec3::Cross(tip - root, mid - root);
				return axis.LengthSquared() > Math::EPSILON * Math::EPSILON ? axis.Normalized() : Vec3();
			}

			f32 InteriorAngle(const Vec3& corner, const Vec3& a, const Vec3& b) {
				return Vec3::Angle(a - corner, b - corner);
			}

			f32 AngleFromSides(f32 adjacentA, f32 adjacentB, f32 opposite) {
				const f32 denominator = 2.0f * adjacentA * adjacentB;
				if (!(denominator > Math::EPSILON)) {
					return 0.0f;
				}
				const f32 cosine = Math::Clamp(
					(adjacentA * adjacentA + adjacentB * adjacentB - opposite * opposite) / denominator, -1.0f, 1.0f);
				return std::acos(cosine) * Math::RAD_TO_DEG;
			}

		} // anonymous namespace

		f32 ChainReferenceLength(const RetargetProfile& profile, const RetargetProfile::Chain& chain) {
			const SkeletonAsset* skeleton = profile.skeleton.Get();
			if (!skeleton || chain.bones.size() < 2) {
				return 0.0f;
			}
			Vector<Quat> rotations{GetMemoryResource(MemTag::Asset)};
			Vector<Vec3> positions{GetMemoryResource(MemTag::Asset)};
			Vector<Vec3> scales{GetMemoryResource(MemTag::Asset)};
			ComposeReference(*skeleton, profile.GetReferencePose(), rotations, positions, scales);

			f32 length = 0.0f;
			for (size_t index = 1; index < chain.bones.size(); ++index) {
				const i16 previous = chain.bones[index - 1];
				const i16 current = chain.bones[index];
				if (previous < 0 || current < 0 || static_cast<size_t>(previous) >= positions.size() ||
					static_cast<size_t>(current) >= positions.size()) {
					continue;
				}
				length += (positions[static_cast<size_t>(current)] - positions[static_cast<size_t>(previous)]).Length();
			}
			return length;
		}

		f32 ChainRootReferenceOffset(const RetargetProfile& profile, const RetargetProfile::Chain& chain) {
			const SkeletonAsset* skeleton = profile.skeleton.Get();
			if (!skeleton || chain.bones.empty() || chain.bones.front() < 0) {
				return 0.0f;
			}
			Vector<Quat> rotations{GetMemoryResource(MemTag::Asset)};
			Vector<Vec3> positions{GetMemoryResource(MemTag::Asset)};
			Vector<Vec3> scales{GetMemoryResource(MemTag::Asset)};
			ComposeReference(*skeleton, profile.GetReferencePose(), rotations, positions, scales);

			const size_t bone = static_cast<size_t>(chain.bones.front());
			return bone < positions.size() ? positions[bone].Length() : 0.0f;
		}

		f32 ProfileReferenceScale(const RetargetProfile& profile) {
			const SkeletonAsset* skeleton = profile.skeleton.Get();
			if (!skeleton) {
				return 0.0f;
			}
			const i16 root = FirstRoot(*skeleton);
			if (root < 0) {
				return 0.0f;
			}
			Vector<Quat> rotations{GetMemoryResource(MemTag::Asset)};
			Vector<Vec3> positions{GetMemoryResource(MemTag::Asset)};
			Vector<Vec3> scales{GetMemoryResource(MemTag::Asset)};
			ComposeReference(*skeleton, profile.GetReferencePose(), rotations, positions, scales);
			if (static_cast<size_t>(root) >= positions.size()) {
				return 0.0f;
			}

			f32 furthest = 0.0f;
			for (const RetargetProfile::Chain& chain : profile.chains) {
				for (i16 bone : chain.bones) {
					if (bone < 0 || static_cast<size_t>(bone) >= positions.size()) {
						continue;
					}
					const f32 distance =
						(positions[static_cast<size_t>(bone)] - positions[static_cast<size_t>(root)]).Length();
					furthest = Math::Max(furthest, distance);
				}
			}
			return furthest;
		}

		bool BuildRetargetMap(const RetargetProfile& source, const RetargetProfile& target, RetargetMap& out,
							  RetargetSides sides) {
			out.bones.clear();
			out.correspondedChains = 0;
			out.correspondedBones = 0;
			out.sides = sides;

			const auto sourceRoleFor = [sides](const RetargetProfile::Chain& chain) -> std::string_view {
				return sides == RetargetSides::AsAuthored || chain.mirror.empty() ? std::string_view(chain.role)
																				  : std::string_view(chain.mirror);
			};

			const SkeletonAsset* sourceRig = source.skeleton.Get();
			const SkeletonAsset* targetRig = target.skeleton.Get();
			if (!sourceRig || !targetRig) {
				SLT_LOG_WARN("Animation", "can't retarget between profiles '{}' and '{}' (one has no skeleton)",
							 source.virtualPath, target.virtualPath);
				return false;
			}

			Vector<Vec3> sourceReferencePosition{GetMemoryResource(MemTag::Asset)};
			Vector<Vec3> targetReferencePosition{GetMemoryResource(MemTag::Asset)};
			ComposeReference(*sourceRig, source.GetReferencePose(), out.sourceReference, sourceReferencePosition,
							 out.sourceReferenceScale);
			ComposeReference(*targetRig, target.GetReferencePose(), out.targetReference, targetReferencePosition,
							 out.targetReferenceScale);
			out.sourceModel.assign(out.sourceReference.size(), Quat::Identity());
			out.targetModel.assign(out.targetReference.size(), Quat::Identity());
			out.bones.assign(targetRig->parents.size(), RetargetBone{});
			out.goals.clear();
			out.sourceModelPosition.clear();
			out.targetModelPosition.clear();
			out.sourceRoot = FirstRoot(*sourceRig);
			out.targetRoot = FirstRoot(*targetRig);

			const f32 scaleRatio = SafeRatio(ProfileReferenceScale(target), ProfileReferenceScale(source));
			out.rigScale = scaleRatio;

			f32 worstStanceDegrees = 0.0f;
			std::string_view worstStanceRole;

			for (const RetargetProfile::Chain& targetChain : target.chains) {
				const std::string_view wanted = sourceRoleFor(targetChain);
				const RetargetProfile::Chain* sourceChain = source.FindChain(wanted);
				if (!sourceChain) {
					if (wanted != targetChain.role) {
						SLT_LOG_WARN("Animation", "mirror: chain '{}' mirrors onto role '{}' (no such chain in the rig, skipped)",
									 targetChain.role, wanted);
					} else {
						SLT_LOG_WARN("Animation", "retarget: role '{}' is on the target rig (not on the source, skipped)",
									 targetChain.role);
					}
					continue;
				}
				if (!ChainIsInRange(targetChain, out.bones.size()) ||
					!ChainIsInRange(*sourceChain, out.sourceReference.size())) {
					SLT_LOG_WARN("Animation", "retarget: role '{}' uses a bone its skeleton doesn't have (skipped)",
								 targetChain.role);
					continue;
				}

				const Vec3 sourceDirection = ChainReferenceDirection(sourceReferencePosition, *sourceChain);
				const Vec3 targetDirection = ChainReferenceDirection(targetReferencePosition, targetChain);
				if (sourceDirection.LengthSquared() > 0.0f && targetDirection.LengthSquared() > 0.0f) {
					const f32 cosine = Math::Clamp(Vec3::Dot(sourceDirection, targetDirection), -1.0f, 1.0f);
					const f32 degrees = std::acos(cosine) * Math::RAD_TO_DEG;
					if (degrees > worstStanceDegrees) {
						worstStanceDegrees = degrees;
						worstStanceRole = targetChain.role;
					}
				}

				const size_t targetCount = targetChain.bones.size();
				const size_t sourceCount = sourceChain->bones.size();
				const f32 lengthRatio = SafeRatio(ChainReferenceLength(target, targetChain),
												  ChainReferenceLength(source, *sourceChain));

				const f32 targetPlacement = ChainRootReferenceOffset(target, targetChain);
				const f32 sourcePlacement = ChainRootReferenceOffset(source, *sourceChain);
				const f32 placementRatio = targetPlacement > Math::EPSILON && sourcePlacement > Math::EPSILON
											   ? targetPlacement / sourcePlacement
											   : scaleRatio;

				for (size_t index = 0; index < targetCount; ++index) {
					const f32 along = targetCount > 1 ? static_cast<f32>(index) / static_cast<f32>(targetCount - 1) : 0.0f;
					const f32 scaled = along * static_cast<f32>(sourceCount - 1);
					const size_t first = static_cast<size_t>(scaled);
					const size_t second = Math::Min(first + 1, sourceCount - 1);

					RetargetBone& row = out.bones[static_cast<size_t>(targetChain.bones[index])];
					row.sourceA = sourceChain->bones[first];
					row.sourceB = sourceChain->bones[second];
					row.blend = scaled - static_cast<f32>(first);

					row.positionScale = index >= 1 ? lengthRatio : placementRatio;
					++out.correspondedBones;
				}
				++out.correspondedChains;
			}

			for (const RetargetProfile::Chain& sourceChain : source.chains) {
				bool received = false;
				for (const RetargetProfile::Chain& targetChain : target.chains) {
					if (sourceRoleFor(targetChain) == sourceChain.role) {
						received = true;
						break;
					}
				}
				if (!received) {
					SLT_LOG_WARN("Animation", "retarget: role '{}' is on the source rig (not on the target, motion dropped)",
								 sourceChain.role);
				}
			}

			if (out.correspondedChains == 0) {
				SLT_LOG_WARN("Animation", "retarget: profiles '{}' and '{}' have no role in common (nothing transferred)",
							 source.virtualPath, target.virtualPath);
				return false;
			}

			if (worstStanceDegrees > REFERENCE_STANCE_WARN_DEGREES) {
				SLT_LOG_WARN("Animation",
							 "retarget: profiles '{}' and '{}' have stances {:.1f} deg apart at role '{}' (an animation only file takes its stance from a frame)",
							 source.virtualPath, target.virtualPath, worstStanceDegrees, worstStanceRole);
			}

			for (const str& role : target.goals) {
				if (out.sourceRoot < 0 || out.targetRoot < 0) {
					break;
				}
				const RetargetProfile::Chain* targetChain = target.FindChain(role);
				const RetargetProfile::Chain* sourceChain =
					targetChain != nullptr ? source.FindChain(sourceRoleFor(*targetChain)) : nullptr;
				if (!targetChain || !sourceChain) {
					SLT_LOG_WARN("Animation", "retarget: goal '{}' uses a chain one of the two profiles doesn't have (skipped)",
								 role);
					continue;
				}
				if (!ChainIsInRange(*targetChain, out.bones.size()) ||
					!ChainIsInRange(*sourceChain, out.sourceReference.size())) {
					continue;
				}
				if (targetChain->bones.size() < 3) {
					SLT_LOG_WARN("Animation", "retarget: goal '{}' uses a chain of {} bone(s) (2 bone solve needs 3). Goal skipped",
								 role, targetChain->bones.size());
					continue;
				}

				const size_t count = targetChain->bones.size();
				RetargetGoal goal;
				goal.root = targetChain->bones[0];
				goal.mid = targetChain->bones[count - 2];
				goal.tip = targetChain->bones[count - 1];
				goal.sourceTip = sourceChain->bones.back();

				const bool contiguous = targetRig->parents[static_cast<size_t>(goal.tip)] == goal.mid &&
										targetRig->parents[static_cast<size_t>(goal.mid)] == targetChain->bones[count - 3];
				if (!contiguous) {
					SLT_LOG_WARN("Animation", "retarget: goal '{}' uses a chain (last 3 bones aren't parent to child). Goal skipped",
								 role);
					continue;
				}

				goal.referenceAxis = BendAxis(targetReferencePosition[static_cast<size_t>(goal.root)],
											  targetReferencePosition[static_cast<size_t>(goal.mid)],
											  targetReferencePosition[static_cast<size_t>(goal.tip)]);
				out.goals.push_back(goal);
			}
			if (!out.goals.empty()) {
				out.sourceModelPosition.assign(sourceRig->parents.size(), Vec3());
				out.targetModelPosition.assign(targetRig->parents.size(), Vec3());
				out.modelScale.assign(Math::Max(sourceRig->parents.size(), targetRig->parents.size()), Vec3::One());
			}
			return true;
		}

		namespace {

			void SolveGoals(RetargetMap& map, const SkeletonAsset& sourceRig, const SkeletonAsset& targetRig,
							std::span<const BoneTransform> sourcePose, std::span<BoneTransform> targetPose) {
				ComposeModelPositions(sourceRig, sourcePose, map.sourceModel, map.modelScale, map.sourceModelPosition);
				ComposeModelPositions(targetRig, targetPose, map.targetModel, map.modelScale, map.targetModelPosition);

				const Vec3 sourceOrigin = map.sourceModelPosition[static_cast<size_t>(map.sourceRoot)];
				const Vec3 targetOrigin = map.targetModelPosition[static_cast<size_t>(map.targetRoot)];

				for (const RetargetGoal& goal : map.goals) {
					const size_t root = static_cast<size_t>(goal.root);
					const size_t mid = static_cast<size_t>(goal.mid);
					const size_t tip = static_cast<size_t>(goal.tip);
					if (root >= targetPose.size() || mid >= targetPose.size() || tip >= targetPose.size() ||
						static_cast<size_t>(goal.sourceTip) >= map.sourceModelPosition.size()) {
						continue;
					}

					const Vec3 a = map.targetModelPosition[root];
					const Vec3 b = map.targetModelPosition[mid];
					const Vec3 c = map.targetModelPosition[tip];
					const f32 upper = (b - a).Length();
					const f32 lower = (c - b).Length();
					if (!(upper > Math::EPSILON) || !(lower > Math::EPSILON)) {
						continue;
					}

					const Vec3 contact =
						map.sourceModelPosition[static_cast<size_t>(goal.sourceTip)] - sourceOrigin;
					const Vec3 desired =
						targetOrigin +
						(map.sides == RetargetSides::Mirrored ? ReflectDisplacement(contact) : contact) * map.rigScale;
					const Vec3 toGoal = desired - a;

					const f32 reach = toGoal.Length();

					Vec3 axis = BendAxis(a, b, c);
					if (axis.LengthSquared() <= Math::EPSILON * Math::EPSILON) {
						axis = goal.referenceAxis;
					}
					if (axis.LengthSquared() <= Math::EPSILON * Math::EPSILON) {
						continue;
					}

					const Quat bendRoot = Quat::AngleAxis(AngleFromSides(upper, reach, lower) - InteriorAngle(a, b, c), axis);
					const Quat bendMid = Quat::AngleAxis(AngleFromSides(upper, lower, reach) - InteriorAngle(b, a, c), axis);

					const Quat aim = RotationBetween(c - a, toGoal);
					const Quat upperTurn = (aim * bendRoot).Normalized();

					const Quat rootModel = (upperTurn * map.targetModel[root]).Normalized();
					const Quat midModel = (upperTurn * bendMid * map.targetModel[mid]).Normalized();
					const Quat tipModel = map.targetModel[tip];

					const i16 rootParent = targetRig.parents[root];
					const i16 midParent = targetRig.parents[mid];
					const Quat rootParentModel = rootParent >= 0 && static_cast<size_t>(rootParent) < map.targetModel.size()
													 ? map.targetModel[static_cast<size_t>(rootParent)]
													 : Quat::Identity();
					const Quat midParentModel = midParent >= 0 && static_cast<size_t>(midParent) < map.targetModel.size()
													? (upperTurn * map.targetModel[static_cast<size_t>(midParent)]).Normalized()
													: Quat::Identity();

					targetPose[root].rotation = (rootParentModel.Conjugated() * rootModel).Normalized();
					targetPose[mid].rotation = (midParentModel.Conjugated() * midModel).Normalized();
					targetPose[tip].rotation = (midModel.Conjugated() * tipModel).Normalized();

					map.targetModel[root] = rootModel;
					map.targetModel[mid] = midModel;
				}
			}

		} // anonymous namespace

		void SolveRetargetPose(RetargetMap& map, const RetargetProfile& source, const RetargetProfile& target,
							   std::span<const BoneTransform> sourcePose, std::span<BoneTransform> targetPose) {
			const SkeletonAsset* sourceRig = source.skeleton.Get();
			const SkeletonAsset* targetRig = target.skeleton.Get();
			if (!sourceRig || !targetRig) {
				return;
			}
			const std::span<const BoneTransform> sourceReferencePose = source.GetReferencePose();
			const std::span<const BoneTransform> targetReferencePose = target.GetReferencePose();

			const size_t sourceCount = Math::Min(map.sourceModel.size(), sourceRig->parents.size());
			for (size_t bone = 0; bone < sourceCount; ++bone) {
				const Quat local = bone < sourcePose.size() ? sourcePose[bone].rotation : Quat::Identity();
				const i16 parent = sourceRig->parents[bone];
				map.sourceModel[bone] = parent >= 0 && static_cast<size_t>(parent) < bone
											? (map.sourceModel[static_cast<size_t>(parent)] * local).Normalized()
											: local.Normalized();
			}

			const size_t targetCount = Math::Min(map.bones.size(), targetRig->parents.size());
			for (size_t bone = 0; bone < targetCount; ++bone) {
				const i16 parent = targetRig->parents[bone];
				const bool hasParent = parent >= 0 && static_cast<size_t>(parent) < bone;
				const Quat parentModel = hasParent ? map.targetModel[static_cast<size_t>(parent)] : Quat::Identity();

				BoneTransform out = bone < targetReferencePose.size() ? targetReferencePose[bone] : BoneTransform{};

				const RetargetBone& row = map.bones[bone];
				if (row.sourceA < 0) {
					map.targetModel[bone] = (parentModel * out.rotation).Normalized();
					if (bone < targetPose.size()) {
						targetPose[bone] = out;
					}
					continue;
				}

				const size_t first = static_cast<size_t>(row.sourceA);
				const size_t second = static_cast<size_t>(row.sourceB);

				const Quat deltaFirst = (map.sourceModel[first] * map.sourceReference[first].Conjugated()).Normalized();
				const Quat delta =
					first == second || !(row.blend > 0.0f)
						? deltaFirst
						: Quat::Slerp(deltaFirst,
									  (map.sourceModel[second] * map.sourceReference[second].Conjugated()).Normalized(),
									  row.blend);
				const Quat oriented = map.sides == RetargetSides::Mirrored ? ReflectRotation(delta) : delta;
				const Quat model = (oriented * map.targetReference[bone]).Normalized();
				map.targetModel[bone] = model;
				out.rotation = (parentModel.Conjugated() * model).Normalized();

				const BoneTransform sampledFirst = first < sourcePose.size() ? sourcePose[first] : BoneTransform{};
				const BoneTransform referenceFirst =
					first < sourceReferencePose.size() ? sourceReferencePose[first] : BoneTransform{};
				const BoneTransform sampledSecond = second < sourcePose.size() ? sourcePose[second] : BoneTransform{};
				const BoneTransform referenceSecond =
					second < sourceReferencePose.size() ? sourceReferencePose[second] : BoneTransform{};

				const Vec3 travelled = Vec3::Lerp(sampledFirst.position - referenceFirst.position,
												  sampledSecond.position - referenceSecond.position, row.blend);
				if (travelled.LengthSquared() > 0.0f) {
					const i16 sourceParent = sourceRig->parents[first];
					const bool hasSourceParent =
						sourceParent >= 0 && static_cast<size_t>(sourceParent) < map.sourceReference.size();
					const Quat sourceParentReference =
						hasSourceParent ? map.sourceReference[static_cast<size_t>(sourceParent)] : Quat::Identity();
					const Vec3 sourceParentScale =
						hasSourceParent ? map.sourceReferenceScale[static_cast<size_t>(sourceParent)] : Vec3::One();
					const Quat targetParentReference = hasParent ? map.targetReference[static_cast<size_t>(parent)]
																 : Quat::Identity();
					const Vec3 targetParentScale =
						hasParent ? map.targetReferenceScale[static_cast<size_t>(parent)] : Vec3::One();

					const Vec3 lifted = (sourceParentReference * (sourceParentScale * travelled)) * row.positionScale;
					const Vec3 model = map.sides == RetargetSides::Mirrored ? ReflectDisplacement(lifted) : lifted;
					out.position += SafeUnscale(targetParentReference.Conjugated() * model, targetParentScale);
				}

				out.scale = out.scale * SafeScaleRatio(sampledFirst.scale, referenceFirst.scale);

				if (bone < targetPose.size()) {
					targetPose[bone] = out;
				}
			}

			if (!map.goals.empty()) {
				SolveGoals(map, *sourceRig, *targetRig, sourcePose, targetPose);
			}
		}

		BoneTransform SolveRetargetRoot(const RetargetMap& map, const RetargetProfile& source,
										const RetargetProfile& target, const BoneTransform& local) {
			if (map.sourceRoot < 0 || map.targetRoot < 0) {
				return local;
			}
			const std::span<const BoneTransform> sourceReference = source.GetReferencePose();
			const std::span<const BoneTransform> targetReference = target.GetReferencePose();
			const size_t sourceIndex = static_cast<size_t>(map.sourceRoot);
			const size_t targetIndex = static_cast<size_t>(map.targetRoot);
			if (sourceIndex >= sourceReference.size() || targetIndex >= targetReference.size()) {
				return local;
			}
			const BoneTransform& sourceRest = sourceReference[sourceIndex];
			const BoneTransform& targetRest = targetReference[targetIndex];

			const Quat turn = (local.rotation.Normalized() * sourceRest.rotation.Normalized().Conjugated()).Normalized();
			const Vec3 travel = local.position - sourceRest.position;
			const bool mirrored = map.sides == RetargetSides::Mirrored;

			BoneTransform out = targetRest;
			out.rotation = ((mirrored ? ReflectRotation(turn) : turn) * targetRest.rotation.Normalized()).Normalized();
			out.position = targetRest.position + (mirrored ? ReflectDisplacement(travel) : travel) * map.rigScale;
			out.scale = targetRest.scale * SafeScaleRatio(local.scale, sourceRest.scale);
			return out;
		}

		BoneTransform SolveRetargetRootDelta(const RetargetMap& map, const RetargetProfile& source,
											 const RetargetProfile& target, const BoneTransform& delta) {
			if (map.sourceRoot < 0 || map.targetRoot < 0) {
				return delta;
			}
			const std::span<const BoneTransform> sourceReference = source.GetReferencePose();
			const std::span<const BoneTransform> targetReference = target.GetReferencePose();
			const size_t sourceIndex = static_cast<size_t>(map.sourceRoot);
			const size_t targetIndex = static_cast<size_t>(map.targetRoot);
			if (sourceIndex >= sourceReference.size() || targetIndex >= targetReference.size()) {
				return delta;
			}

			BoneTransform scaled = delta;
			scaled.position = delta.position * map.rigScale;
			if (map.sides == RetargetSides::Mirrored) {
				scaled.position = ReflectDisplacement(scaled.position);
				scaled.rotation = ReflectRotation(delta.rotation.Normalized());
			}

			BoneTransform reorient;
			reorient.rotation = (targetReference[targetIndex].rotation.Normalized().Conjugated() *
								 sourceReference[sourceIndex].rotation.Normalized())
									.Normalized();
			return ConjugateDelta(reorient, scaled);
		}

	} // namespace AnimationImpl

	namespace Retarget {

		namespace {

			void CollectSampleTimes(const AnimationClip& clip, Vector<f32>& out) {
				for (const AnimationClip::Track& track : clip.tracks) {
					switch (track.kind) {
					case AnimationTrackKind::BonePosition:
					case AnimationTrackKind::BoneRotation:
					case AnimationTrackKind::BoneScale:
						break;
					default:
						continue;
					}
					for (f32 time : track.times) {
						out.push_back(time);
					}
				}
				std::sort(out.begin(), out.end());
				out.erase(std::unique(out.begin(), out.end()), out.end());
				if (out.empty()) {
					out.push_back(0.0f);
				}
			}

			void ResolveTrackBones(const AnimationClip& clip, const SkeletonAsset& skeleton, Vector<i32>& out) {
				out.assign(clip.tracks.size(), -1);
				for (size_t index = 0; index < clip.tracks.size(); ++index) {
					const AnimationClip::Track& track = clip.tracks[index];
					switch (track.kind) {
					case AnimationTrackKind::BonePosition:
					case AnimationTrackKind::BoneRotation:
					case AnimationTrackKind::BoneScale:
						out[index] = skeleton.FindBone(track.name);
						break;
					default:
						break;
					}
				}
			}

			void SamplePose(const AnimationClip& clip, std::span<const i32> trackBones,
							std::span<const BoneTransform> reference, size_t boneCount, f32 at,
							Vector<BoneTransform>& out) {
				out.assign(reference.begin(), reference.end());
				out.resize(boneCount);

				for (size_t index = 0; index < clip.tracks.size() && index < trackBones.size(); ++index) {
					const i32 bone = trackBones[index];
					if (bone < 0 || static_cast<size_t>(bone) >= out.size()) {
						continue;
					}
					const AnimationClip::Track& track = clip.tracks[index];
					const Vec4 value = AnimationImpl::SampleTrack(track, at);
					BoneTransform& target = out[static_cast<size_t>(bone)];
					switch (track.kind) {
					case AnimationTrackKind::BonePosition:
						target.position = Vec3(value.x, value.y, value.z);
						break;
					case AnimationTrackKind::BoneRotation:
						target.rotation = Quat(value.x, value.y, value.z, value.w);
						break;
					default:
						target.scale = Vec3(value.x, value.y, value.z);
						break;
					}
				}
			}

			AnimationClip::Track CopyTrack(const AnimationClip::Track& track) {
				AnimationClip::Track out;
				out.kind = track.kind;
				out.interp = track.interp;
				out.component = track.component;
				out.objectPath = track.objectPath;
				out.componentType = track.componentType;
				out.name = track.name;
				out.times.assign(track.times.begin(), track.times.end());
				out.values.assign(track.values.begin(), track.values.end());
				return out;
			}

			constexpr f32 BAKE_POSITION_EPSILON = 1e-5f;
			constexpr f32 BAKE_ROTATION_EPSILON = 1e-3f;
			constexpr f32 BAKE_SCALE_EPSILON = 1e-5f;

		} // anonymous namespace

		static AnimationClip* Resample(const AnimationClip& clip, const RetargetProfile& source,
									   const RetargetProfile& target, AnimationImpl::RetargetSides sides,
									   std::string_view what) {
			AnimationImpl::RetargetMap map{};
			if (!AnimationImpl::BuildRetargetMap(source, target, map, sides)) {
				return nullptr;
			}
			const SkeletonAsset* sourceRig = source.skeleton.Get();
			const SkeletonAsset* targetRig = target.skeleton.Get();
			if (!sourceRig || !targetRig) {
				return nullptr;
			}

			Vector<f32> times{GetMemoryResource(MemTag::Asset)};
			CollectSampleTimes(clip, times);

			const std::span<const BoneTransform> sourceReference = source.GetReferencePose();
			const size_t boneCount = targetRig->parents.size();

			Vector<BoneTransform> baked{GetMemoryResource(MemTag::Asset)};
			baked.assign(times.size() * boneCount, BoneTransform{});

			Vector<i32> trackBones{GetMemoryResource(MemTag::Asset)};
			ResolveTrackBones(clip, *sourceRig, trackBones);

			Vector<BoneTransform> sampled{GetMemoryResource(MemTag::Asset)};
			Vector<BoneTransform> solved{GetMemoryResource(MemTag::Asset)};
			solved.assign(boneCount, BoneTransform{});
			for (size_t frame = 0; frame < times.size(); ++frame) {
				SamplePose(clip, trackBones, sourceReference, sourceRig->parents.size(), times[frame], sampled);
				AnimationImpl::SolveRetargetPose(map, source, target, sampled, solved);
				for (size_t bone = 0; bone < boneCount; ++bone) {
					baked[frame * boneCount + bone] = solved[bone];
				}
			}

			AnimationClip* out = New<AnimationClip>(MemTag::Asset);
			out->length = clip.length;
			out->frameRate = clip.frameRate;
			out->loop = clip.loop;
			out->additiveReferenceTime = clip.additiveReferenceTime;
			out->bakeRootMotionIntoPose = clip.bakeRootMotionIntoPose;
			out->skeletonGuid = target.skeletonGuid;
			out->skeleton = target.skeleton;

			for (size_t bone = 0; bone < boneCount; ++bone) {
				if (map.bones[bone].sourceA < 0) {
					continue;
				}
				const BoneTransform reference =
					bone < targetRig->restPose.size() ? targetRig->restPose[bone] : BoneTransform{};

				bool movedPosition = false;
				bool movedRotation = false;
				bool movedScale = false;
				for (size_t frame = 0; frame < times.size(); ++frame) {
					const BoneTransform& value = baked[frame * boneCount + bone];
					movedPosition = movedPosition ||
									(value.position - reference.position).LengthSquared() > BAKE_POSITION_EPSILON * BAKE_POSITION_EPSILON;
					movedRotation = movedRotation || Quat::Angle(value.rotation, reference.rotation) > BAKE_ROTATION_EPSILON;
					movedScale = movedScale ||
								 (value.scale - reference.scale).LengthSquared() > BAKE_SCALE_EPSILON * BAKE_SCALE_EPSILON;
				}

				const AnimationTrackKind kinds[3] = {AnimationTrackKind::BonePosition, AnimationTrackKind::BoneRotation,
													 AnimationTrackKind::BoneScale};
				const bool wanted[3] = {movedPosition, movedRotation, movedScale};
				for (i32 channel = 0; channel < 3; ++channel) {
					if (!wanted[channel]) {
						continue;
					}
					AnimationClip::Track track;
					track.kind = kinds[channel];
					track.interp = AnimationInterpolation::Linear;
					track.name = targetRig->boneNames[bone];
					track.times.reserve(times.size());
					track.values.reserve(times.size());
					for (size_t frame = 0; frame < times.size(); ++frame) {
						const BoneTransform& value = baked[frame * boneCount + bone];
						track.times.push_back(times[frame]);
						switch (kinds[channel]) {
						case AnimationTrackKind::BonePosition:
							track.values.push_back(Vec4(value.position.x, value.position.y, value.position.z, 0.0f));
							break;
						case AnimationTrackKind::BoneRotation:
							track.values.push_back(Vec4(value.rotation.x, value.rotation.y, value.rotation.z, value.rotation.w));
							break;
						default:
							track.values.push_back(Vec4(value.scale.x, value.scale.y, value.scale.z, 0.0f));
							break;
						}
					}
					out->tracks.push_back(std::move(track));
				}
			}

			for (const AnimationClip::Track& track : clip.tracks) {
				switch (track.kind) {
				case AnimationTrackKind::BonePosition:
				case AnimationTrackKind::BoneRotation:
				case AnimationTrackKind::BoneScale:
					continue;
				default:
					break;
				}
				out->tracks.push_back(CopyTrack(track));
			}

			SLT_LOG_TRACE("Animation", "{} '{}': {} chain(s), {} bone(s), {} sample(s), {} track(s) out",
						  what, clip.virtualPath, map.correspondedChains, map.correspondedBones, times.size(), out->tracks.size());
			return out;
		}

		AnimationClip* Bake(const AnimationClip& clip, const RetargetProfile& source, const RetargetProfile& target) {
			return Resample(clip, source, target, AnimationImpl::RetargetSides::AsAuthored, "retargeted");
		}

		AnimationClip* MirrorClip(const AnimationClip& clip, const RetargetProfile& profile) {
			return Resample(clip, profile, profile, AnimationImpl::RetargetSides::Mirrored, "mirrored");
		}

	} // namespace Retarget

} // namespace Shalltear
