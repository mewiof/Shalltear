#include "TestsPCH.h"

#include "ProductLookup.h"

#include "ModelImport.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics/SkeletonAsset.h"
#include "Shalltear/Core/Graphics/Skeleton.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Settings.h"

#include <algorithm>
#include <atomic>
#include <cmath>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <initializer_list>
#include <iterator>
#include <span>
#include <string>
#include <thread>
#include <vector>

#ifndef SHALLTEAR_SHARED
#include "AnimationPass.h"
#include "ClipSampler.h"
#include "Retarget.h"
#include "RetargetPoseOps.h"

#include "Shalltear/Animation/AnimationClip.h"
#include "Shalltear/Animation/AnimationLayer.h"
#include "Shalltear/Animation/Animator.h"
#include "Shalltear/Animation/RetargetProfile.h"
#endif

#ifdef SHALLTEAR_SHARED
#include "Shalltear/Animation/RetargetProfile.h"

#include "Shalltear/Animation/AnimationClip.h"
#endif

using namespace Shalltear;

#ifndef SHALLTEAR_SHARED

SLT_DECLARE_LINKED_PACKAGE(Animation)

namespace {

	struct BoneSpec {
		const char* name;
		i16 parent;
		Vec3 position;
		Quat rotation;

		Vec3 scale{1.0f, 1.0f, 1.0f};
	};

	class RetargetFixture : public ::testing::Test {
	protected:
		std::vector<Asset*> _owned;

		void TearDown() override {
			for (Asset* asset : _owned) {
				Delete(asset);
			}
			_owned.clear();
		}

		SkeletonAsset* MakeRig(std::initializer_list<BoneSpec> bones) {
			SkeletonAsset* skeleton = New<SkeletonAsset>(MemTag::Asset);
			for (const BoneSpec& bone : bones) {
				skeleton->boneNames.push_back(bone.name);
				skeleton->parents.push_back(bone.parent);
				BoneTransform rest;
				rest.position = bone.position;
				rest.rotation = bone.rotation;
				rest.scale = bone.scale;
				skeleton->restPose.push_back(rest);
			}
			_owned.push_back(skeleton);
			return skeleton;
		}

		SkeletonAsset* MakeNamedRig(const std::vector<const char*>& names, const std::vector<i16>& parents) {
			SkeletonAsset* skeleton = New<SkeletonAsset>(MemTag::Asset);
			for (size_t index = 0; index < names.size(); ++index) {
				skeleton->boneNames.push_back(names[index]);
				skeleton->parents.push_back(parents[index]);
				BoneTransform rest;
				rest.position = Vec3(0.0f, 0.1f, 0.0f);
				skeleton->restPose.push_back(rest);
			}
			_owned.push_back(skeleton);
			return skeleton;
		}

		RetargetProfile* MakeProfile(SkeletonAsset* skeleton,
									 std::initializer_list<std::pair<const char*, std::vector<i16>>> chains) {
			RetargetProfile* profile = New<RetargetProfile>(MemTag::Asset);
			profile->skeleton = Handle<SkeletonAsset>(skeleton);
			for (const auto& entry : chains) {
				RetargetProfile::Chain chain;
				chain.role = str(entry.first);
				chain.bones.assign(entry.second.begin(), entry.second.end());
				profile->chains.push_back(std::move(chain));
			}
			_owned.push_back(profile);
			return profile;
		}

		AnimationClip* MakeClip() {
			AnimationClip* clip = New<AnimationClip>(MemTag::Asset);
			clip->virtualPath = "project://retarget.clip";
			clip->length = 1.0f;
			_owned.push_back(clip);
			return clip;
		}

		static void AddConstant(AnimationClip& clip, AnimationTrackKind kind, const char* bone, const Vec4& value) {
			AnimationClip::Track track;
			track.kind = kind;
			track.interp = AnimationInterpolation::Linear;
			track.name = str(bone);
			track.times.push_back(0.0f);
			track.values.push_back(value);
			clip.tracks.push_back(std::move(track));
		}

		static void AddRotation(AnimationClip& clip, const char* bone, const Quat& rotation) {
			AddConstant(clip, AnimationTrackKind::BoneRotation, bone, Vec4(rotation.x, rotation.y, rotation.z, rotation.w));
		}

		static void AddPosition(AnimationClip& clip, const char* bone, const Vec3& position) {
			AddConstant(clip, AnimationTrackKind::BonePosition, bone, Vec4(position.x, position.y, position.z, 0.0f));
		}

		static std::vector<BoneTransform> RestPose(const SkeletonAsset& skeleton) {
			return std::vector<BoneTransform>(skeleton.restPose.begin(), skeleton.restPose.end());
		}

		static Quat ModelRotation(const SkeletonAsset& skeleton, std::span<const BoneTransform> pose, i16 bone) {
			Quat out = Quat::Identity();
			std::vector<i16> path;
			for (i16 walk = bone; walk >= 0; walk = skeleton.parents[static_cast<size_t>(walk)]) {
				path.push_back(walk);
			}
			for (auto it = path.rbegin(); it != path.rend(); ++it) {
				out = (out * pose[static_cast<size_t>(*it)].rotation).Normalized();
			}
			return out;
		}

		static Vec3 ModelPosition(const SkeletonAsset& skeleton, std::span<const BoneTransform> pose, i16 bone) {
			std::vector<i16> path;
			for (i16 walk = bone; walk >= 0; walk = skeleton.parents[static_cast<size_t>(walk)]) {
				path.push_back(walk);
			}
			Quat rotation = Quat::Identity();
			Vec3 position;
			Vec3 scale = Vec3::One();
			for (auto it = path.rbegin(); it != path.rend(); ++it) {
				const BoneTransform& local = pose[static_cast<size_t>(*it)];
				position = position + rotation * (scale * local.position);
				rotation = (rotation * local.rotation).Normalized();
				scale = scale * local.scale;
			}
			return position;
		}

		static void PairChains(RetargetProfile& profile, const char* first, const char* second) {
			for (RetargetProfile::Chain& chain : profile.chains) {
				if (chain.role == first) {
					chain.mirror = str(second);
				} else if (chain.role == second) {
					chain.mirror = str(first);
				}
			}
		}

		static std::vector<BoneTransform> SolveMirrored(const RetargetProfile& profile,
														std::span<const BoneTransform> pose) {
			AnimationImpl::RetargetMap map{};
			if (!AnimationImpl::BuildRetargetMap(profile, profile, map, AnimationImpl::RetargetSides::Mirrored)) {
				return {};
			}
			std::vector<BoneTransform> out(profile.skeleton.Get()->parents.size());
			AnimationImpl::SolveRetargetPose(map, profile, profile, pose, std::span<BoneTransform>(out.data(), out.size()));
			return out;
		}

		SkeletonAsset* MakeMirrorRig() {
			return MakeRig({{"hips", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
							{"spine", 0, Vec3(0.0f, 0.3f, 0.0f), Quat::Identity()},
							{"lShoulder", 1, Vec3(0.2f, 0.2f, 0.0f), Quat::Identity()},
							{"lElbow", 2, Vec3(0.3f, 0.0f, 0.0f), Quat::Identity()},
							{"lHand", 3, Vec3(0.3f, 0.0f, 0.0f), Quat::Identity()},
							{"rShoulder", 1, Vec3(-0.2f, 0.2f, 0.0f), Quat::Identity()},
							{"rElbow", 5, Vec3(-0.3f, 0.0f, 0.0f), Quat::Identity()},
							{"rHand", 6, Vec3(-0.3f, 0.0f, 0.0f), Quat::Identity()}});
		}

		RetargetProfile* MakeMirrorProfile(SkeletonAsset* skeleton) {
			RetargetProfile* profile =
				MakeProfile(skeleton, {{"hips", {0}}, {"spine", {1}}, {"leftArm", {2, 3, 4}}, {"rightArm", {5, 6, 7}}});
			PairChains(*profile, "leftArm", "rightArm");
			return profile;
		}

		static std::vector<BoneTransform> PoseFromClip(const AnimationClip& clip, const SkeletonAsset& skeleton, f32 at) {
			std::vector<BoneTransform> pose(skeleton.restPose.begin(), skeleton.restPose.end());
			for (const AnimationClip::Track& track : clip.tracks) {
				const i32 bone = skeleton.FindBone(track.name);
				if (bone < 0) {
					continue;
				}
				const Vec4 value = AnimationImpl::SampleTrack(track, at);
				BoneTransform& into = pose[static_cast<size_t>(bone)];
				switch (track.kind) {
				case AnimationTrackKind::BoneRotation:
					into.rotation = Quat(value.x, value.y, value.z, value.w);
					break;
				case AnimationTrackKind::BonePosition:
					into.position = Vec3(value.x, value.y, value.z);
					break;
				case AnimationTrackKind::BoneScale:
					into.scale = Vec3(value.x, value.y, value.z);
					break;
				default:
					break;
				}
			}
			return pose;
		}

		static std::vector<BoneTransform> Solve(const RetargetProfile& source, const RetargetProfile& target,
												std::span<const BoneTransform> sourcePose) {
			AnimationImpl::RetargetMap map{};
			if (!AnimationImpl::BuildRetargetMap(source, target, map, AnimationImpl::RetargetSides::AsAuthored)) {
				return {};
			}
			std::vector<BoneTransform> out(target.skeleton.Get()->parents.size());
			AnimationImpl::SolveRetargetPose(map, source, target, sourcePose,
											 std::span<BoneTransform>(out.data(), out.size()));
			return out;
		}
	};

	constexpr Vec3 XAxis{1.0f, 0.0f, 0.0f};

} // anonymous namespace

TEST_F(RetargetFixture, ModelSpaceDeltaIgnoresRestOrientation) {
	SkeletonAsset* source = MakeRig({{"a", -1, Vec3(), Quat::Identity()},
									 {"b", 0, XAxis, Quat::Identity()},
									 {"c", 1, XAxis, Quat::Identity()}});
	const Quat roll = Quat::AngleAxis(90.0f, XAxis);
	SkeletonAsset* target = MakeRig({{"A", -1, Vec3(), roll},
									 {"B", 0, XAxis, Quat::Identity()},
									 {"C", 1, XAxis, Quat::Identity()}});

	RetargetProfile* sourceProfile = MakeProfile(source, {{"arm", {0, 1, 2}}});
	RetargetProfile* targetProfile = MakeProfile(target, {{"arm", {0, 1, 2}}});

	std::vector<BoneTransform> pose = RestPose(*source);
	const Quat swing = Quat::AngleAxis(30.0f, Vec3::Up());
	pose[1].rotation = swing;

	const std::vector<BoneTransform> solved = Solve(*sourceProfile, *targetProfile, pose);
	ASSERT_EQ(solved.size(), 3u);

	const Quat expected = (swing * roll).Normalized();
	EXPECT_NEAR(Quat::Angle(ModelRotation(*target, solved, 1), expected), 0.0f, 1e-2f);

	EXPECT_GT(Quat::Angle(solved[1].rotation, swing), 1.0f);

	EXPECT_NEAR(Quat::Angle(solved[2].rotation, Quat::Identity()), 0.0f, 1e-3f);
}

TEST_F(RetargetFixture, RotationsMeasuredFromReferencePose) {
	SkeletonAsset* source = MakeRig({{"shoulder", -1, Vec3(0.0f, 1.5f, 0.0f), Quat::Identity()},
									 {"elbow", 0, XAxis * 0.3f, Quat::Identity()}});
	SkeletonAsset* target = MakeRig({{"SHOULDER", -1, Vec3(0.0f, 1.5f, 0.0f), Quat::Identity()},
									 {"ELBOW", 0, XAxis * 0.3f, Quat::Identity()}});

	RetargetProfile* sourceProfile = MakeProfile(source, {{"arm", {0, 1}}});
	RetargetProfile* targetProfile = MakeProfile(target, {{"arm", {0, 1}}});

	const Quat aPose = Quat::AngleAxis(45.0f, Vec3(0.0f, 0.0f, 1.0f));
	sourceProfile->referencePose.assign(source->restPose.begin(), source->restPose.end());
	sourceProfile->referencePose[1].rotation = aPose;
	sourceProfile->referencePose[0].position = Vec3(0.0f, 1.2f, 0.0f);
	ASSERT_EQ(sourceProfile->GetReferencePose().size(), 2u);

	std::vector<BoneTransform> pose = RestPose(*source);
	pose[1].rotation = aPose;
	pose[0].position = Vec3(0.0f, 1.2f, 0.0f);

	const std::vector<BoneTransform> solved = Solve(*sourceProfile, *targetProfile, pose);
	ASSERT_EQ(solved.size(), 2u);
	EXPECT_NEAR(Quat::Angle(solved[1].rotation, Quat::Identity()), 0.0f, 1e-2f);
	EXPECT_NEAR(solved[0].position.y, 1.5f, 1e-3f);

	const std::vector<BoneTransform> moved = Solve(*sourceProfile, *targetProfile, RestPose(*source));
	ASSERT_EQ(moved.size(), 2u);
	EXPECT_NEAR(Quat::Angle(moved[1].rotation, aPose.Conjugated()), 0.0f, 1e-2f);
}

TEST_F(RetargetFixture, LongerChainSpreadsRotation) {
	SkeletonAsset* source = MakeRig({{"s0", -1, Vec3(), Quat::Identity()},
									 {"s1", 0, XAxis, Quat::Identity()},
									 {"s2", 1, XAxis, Quat::Identity()}});
	SkeletonAsset* target = MakeRig({{"t0", -1, Vec3(), Quat::Identity()},
									 {"t1", 0, XAxis, Quat::Identity()},
									 {"t2", 1, XAxis, Quat::Identity()},
									 {"t3", 2, XAxis, Quat::Identity()},
									 {"t4", 3, XAxis, Quat::Identity()}});

	RetargetProfile* sourceProfile = MakeProfile(source, {{"spine", {0, 1, 2}}});
	RetargetProfile* targetProfile = MakeProfile(target, {{"spine", {0, 1, 2, 3, 4}}});

	std::vector<BoneTransform> pose = RestPose(*source);
	pose[1].rotation = Quat::AngleAxis(40.0f, Vec3::Up());

	const std::vector<BoneTransform> solved = Solve(*sourceProfile, *targetProfile, pose);
	ASSERT_EQ(solved.size(), 5u);

	const f32 expected[5] = {0.0f, 20.0f, 40.0f, 40.0f, 40.0f};
	for (i16 bone = 0; bone < 5; ++bone) {
		const f32 angle = Quat::Angle(ModelRotation(*target, solved, bone), Quat::Identity());
		EXPECT_NEAR(angle, expected[bone], 0.5f);
	}
}

TEST_F(RetargetFixture, OneSidedRoleSkipped) {
	SkeletonAsset* source = MakeRig({{"s0", -1, Vec3(), Quat::Identity()},
									 {"s1", 0, XAxis, Quat::Identity()}});
	SkeletonAsset* target = MakeRig({{"t0", -1, Vec3(), Quat::Identity()},
									 {"t1", 0, XAxis, Quat::Identity()},
									 {"tail", 0, Vec3(-1.0f, 0.0f, 0.0f), Quat::Identity()}});

	RetargetProfile* sourceProfile = MakeProfile(source, {{"spine", {0, 1}}});
	RetargetProfile* targetProfile = MakeProfile(target, {{"spine", {0, 1}}, {"tail", {2}}});

	AnimationImpl::RetargetMap map{};
	ASSERT_TRUE(AnimationImpl::BuildRetargetMap(*sourceProfile, *targetProfile, map, AnimationImpl::RetargetSides::AsAuthored));
	EXPECT_EQ(map.correspondedChains, 1u);
	EXPECT_EQ(map.correspondedBones, 2u);

	std::vector<BoneTransform> pose = RestPose(*source);
	pose[1].rotation = Quat::AngleAxis(50.0f, Vec3::Up());
	const std::vector<BoneTransform> solved = Solve(*sourceProfile, *targetProfile, pose);
	ASSERT_EQ(solved.size(), 3u);

	EXPECT_NEAR(Quat::Angle(ModelRotation(*target, solved, 1), pose[1].rotation), 0.0f, 1e-2f);
	EXPECT_NEAR(Quat::Angle(solved[2].rotation, Quat::Identity()), 0.0f, 1e-3f);
	EXPECT_NEAR((solved[2].position - Vec3(-1.0f, 0.0f, 0.0f)).Length(), 0.0f, 1e-4f);

	RetargetProfile* unrelated = MakeProfile(target, {{"antenna", {2}}});
	AnimationImpl::RetargetMap empty{};
	EXPECT_FALSE(AnimationImpl::BuildRetargetMap(*sourceProfile, *unrelated, empty, AnimationImpl::RetargetSides::AsAuthored));
}

TEST_F(RetargetFixture, RootTranslationScalesBy2RigsProportions) {
	SkeletonAsset* small = MakeRig({{"hips", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									{"spine", 0, Vec3(0.0f, 0.5f, 0.0f), Quat::Identity()}});
	SkeletonAsset* large = MakeRig({{"HIPS", -1, Vec3(0.0f, 2.0f, 0.0f), Quat::Identity()},
									{"SPINE", 0, Vec3(0.0f, 1.5f, 0.0f), Quat::Identity()}});

	RetargetProfile* smallProfile = MakeProfile(small, {{"hips", {0}}, {"spine", {1}}});
	RetargetProfile* largeProfile = MakeProfile(large, {{"hips", {0}}, {"spine", {1}}});

	EXPECT_NEAR(AnimationImpl::ProfileReferenceScale(*smallProfile), 0.5f, 1e-4f);
	EXPECT_NEAR(AnimationImpl::ProfileReferenceScale(*largeProfile), 1.5f, 1e-4f);

	std::vector<BoneTransform> pose = RestPose(*small);
	pose[0].position = Vec3(1.0f, 1.0f, 0.0f);

	const std::vector<BoneTransform> solved = Solve(*smallProfile, *largeProfile, pose);
	ASSERT_EQ(solved.size(), 2u);

	EXPECT_NEAR(solved[0].position.x, 2.0f, 1e-3f);
	EXPECT_NEAR(solved[0].position.y, 2.0f, 1e-3f);

	const std::vector<BoneTransform> back = Solve(*largeProfile, *smallProfile, solved);
	ASSERT_EQ(back.size(), 2u);
	EXPECT_NEAR(back[0].position.x, 1.0f, 1e-3f);
	EXPECT_NEAR(back[0].position.y, 1.0f, 1e-3f);
}

TEST_F(RetargetFixture, ChainRootScalesByPlacement) {
	SkeletonAsset* human = MakeRig({{"Armature", -1, Vec3(), Quat::Identity()},
									{"hips", 0, Vec3(0.0f, 0.9796f, 0.0f), Quat::Identity()},
									{"spine", 1, Vec3(0.0f, 0.4f, 0.0f), Quat::Identity()},
									{"head", 2, Vec3(0.0f, 0.26f, 0.0f), Quat::Identity()},
									{"leftArm", 2, Vec3(-0.25f, 0.0f, 0.0f), Quat::Identity()},
									{"leftForeArm", 4, Vec3(-0.25f, 0.0f, 0.0f), Quat::Identity()}});
	SkeletonAsset* brawler = MakeRig({{"ROOT", -1, Vec3(), Quat::Identity()},
									  {"Hips", 0, Vec3(0.0f, 0.8647f, 0.0f), Quat::Identity()},
									  {"Spine", 1, Vec3(0.0f, 0.36f, 0.0f), Quat::Identity()},
									  {"Head", 2, Vec3(0.0f, 0.25f, 0.0f), Quat::Identity()},
									  {"LeftArm", 2, Vec3(-0.66f, 0.0f, 0.0f), Quat::Identity()},
									  {"LeftForeArm", 4, Vec3(-0.66f, 0.0f, 0.0f), Quat::Identity()}});

	RetargetProfile* humanProfile = MakeProfile(human, {{"hips", {1}}, {"spine", {2, 3}}, {"leftArm", {4, 5}}});
	RetargetProfile* brawlerProfile = MakeProfile(brawler, {{"hips", {1}}, {"spine", {2, 3}}, {"leftArm", {4, 5}}});

	const f32 extentRatio =
		AnimationImpl::ProfileReferenceScale(*brawlerProfile) / AnimationImpl::ProfileReferenceScale(*humanProfile);
	EXPECT_NEAR(extentRatio, 1.0982f, 1e-3f);
	EXPECT_NEAR(0.8647f / 0.9796f, 0.8827f, 1e-3f);
	EXPECT_GT(extentRatio, 1.0f);

	std::vector<BoneTransform> pose = RestPose(*human);
	pose[1].position = Vec3(0.0f, 0.0757f, 0.0f);

	const std::vector<BoneTransform> solved = Solve(*humanProfile, *brawlerProfile, pose);
	ASSERT_EQ(solved.size(), 6u);

	EXPECT_GT(ModelPosition(*brawler, solved, 1).y, 0.0f);
	EXPECT_NEAR(ModelPosition(*brawler, solved, 1).y, 0.0668f, 1e-3f);
}

TEST_F(RetargetFixture, OriginChainFallsBackToRig) {
	SkeletonAsset* small = MakeRig({{"root", -1, Vec3(), Quat::Identity()},
									{"hips", 0, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									{"spine", 1, Vec3(0.0f, 0.5f, 0.0f), Quat::Identity()}});
	SkeletonAsset* large = MakeRig({{"ROOT", -1, Vec3(), Quat::Identity()},
									{"HIPS", 0, Vec3(0.0f, 2.0f, 0.0f), Quat::Identity()},
									{"SPINE", 1, Vec3(0.0f, 1.75f, 0.0f), Quat::Identity()}});

	RetargetProfile* smallProfile = MakeProfile(small, {{"root", {0}}, {"hips", {1}}, {"spine", {2}}});
	RetargetProfile* largeProfile = MakeProfile(large, {{"root", {0}}, {"hips", {1}}, {"spine", {2}}});

	AnimationImpl::RetargetMap map{};
	ASSERT_TRUE(AnimationImpl::BuildRetargetMap(*smallProfile, *largeProfile, map, AnimationImpl::RetargetSides::AsAuthored));
	EXPECT_NEAR(map.rigScale, 2.5f, 1e-4f);
	EXPECT_NEAR(map.bones[0].positionScale, 2.5f, 1e-4f);
	EXPECT_NEAR(map.bones[1].positionScale, 2.0f, 1e-4f);

	std::vector<BoneTransform> pose = RestPose(*small);
	pose[0].position = Vec3(1.0f, 0.0f, 0.0f);
	const std::vector<BoneTransform> solved = Solve(*smallProfile, *largeProfile, pose);
	ASSERT_EQ(solved.size(), 3u);
	EXPECT_NEAR(solved[0].position.x, 2.5f, 1e-3f);

	BoneTransform local;
	local.position = Vec3(1.0f, 0.0f, 0.0f);
	const BoneTransform throughRoot = AnimationImpl::SolveRetargetRoot(map, *smallProfile, *largeProfile, local);
	EXPECT_NEAR(throughRoot.position.x, solved[0].position.x, 1e-4f);
}

TEST_F(RetargetFixture, MidChainTranslationScalesByChain) {
	SkeletonAsset* source = MakeRig({{"root", -1, Vec3(), Quat::Identity()},
									 {"a0", 0, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									 {"a1", 1, XAxis, Quat::Identity()},
									 {"a2", 2, XAxis, Quat::Identity()}});
	SkeletonAsset* target = MakeRig({{"ROOT", -1, Vec3(), Quat::Identity()},
									 {"A0", 0, Vec3(0.0f, 2.0f, 0.0f), Quat::AngleAxis(90.0f, Vec3(0.0f, 0.0f, 1.0f))},
									 {"A1", 1, XAxis * 3.0f, Quat::Identity()},
									 {"A2", 2, XAxis * 3.0f, Quat::Identity()}});

	RetargetProfile* sourceProfile = MakeProfile(source, {{"root", {0}}, {"arm", {1, 2, 3}}});
	RetargetProfile* targetProfile = MakeProfile(target, {{"root", {0}}, {"arm", {1, 2, 3}}});

	const RetargetProfile::Chain* sourceArm = sourceProfile->FindChain("arm");
	const RetargetProfile::Chain* targetArm = targetProfile->FindChain("arm");
	ASSERT_NE(sourceArm, nullptr);
	ASSERT_NE(targetArm, nullptr);
	EXPECT_NEAR(AnimationImpl::ChainReferenceLength(*sourceProfile, *sourceArm), 2.0f, 1e-4f);
	EXPECT_NEAR(AnimationImpl::ChainReferenceLength(*targetProfile, *targetArm), 6.0f, 1e-4f);

	const f32 rigRatio = AnimationImpl::ProfileReferenceScale(*targetProfile) /
						 AnimationImpl::ProfileReferenceScale(*sourceProfile);
	EXPECT_LT(Math::Abs(rigRatio - 3.0f), 3.0f);
	EXPECT_GT(Math::Abs(rigRatio - 3.0f), 0.05f);

	std::vector<BoneTransform> pose = RestPose(*source);
	pose[2].position = Vec3(1.1f, 0.0f, 0.0f);

	const std::vector<BoneTransform> solved = Solve(*sourceProfile, *targetProfile, pose);
	ASSERT_EQ(solved.size(), 4u);

	EXPECT_NEAR(solved[2].position.x, 3.0f, 1e-3f);
	EXPECT_NEAR(solved[2].position.y, -0.3f, 1e-3f);
	EXPECT_NEAR(solved[2].position.z, 0.0f, 1e-3f);
}

TEST_F(RetargetFixture, ScaledAncestorKeepsRootTranslation) {
	SkeletonAsset* centimetre = MakeRig({{"Reference", -1, Vec3(), Quat::Identity(), Vec3(0.01f, 0.01f, 0.01f)},
										 {"hips", 0, Vec3(0.0f, 100.0f, 0.0f), Quat::Identity()},
										 {"spine", 1, Vec3(0.0f, 50.0f, 0.0f), Quat::Identity()}});
	SkeletonAsset* metre = MakeRig({{"Armature", -1, Vec3(), Quat::Identity()},
									{"Hips", 0, Vec3(0.0f, 2.0f, 0.0f), Quat::Identity()},
									{"Spine", 1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()}});

	RetargetProfile* centimetreProfile = MakeProfile(centimetre, {{"root", {0}}, {"hips", {1}}, {"spine", {2}}});
	RetargetProfile* metreProfile = MakeProfile(metre, {{"root", {0}}, {"hips", {1}}, {"spine", {2}}});

	EXPECT_NEAR(AnimationImpl::ProfileReferenceScale(*centimetreProfile), 1.5f, 1e-4f);
	EXPECT_NEAR(AnimationImpl::ProfileReferenceScale(*metreProfile), 3.0f, 1e-4f);

	std::vector<BoneTransform> pose = RestPose(*centimetre);
	pose[1].position = Vec3(0.0f, 130.0f, 0.0f);

	const std::vector<BoneTransform> solved = Solve(*centimetreProfile, *metreProfile, pose);
	ASSERT_EQ(solved.size(), 3u);

	EXPECT_NEAR(solved[1].position.y, 2.6f, 1e-3f);
	EXPECT_NEAR(solved[1].position.x, 0.0f, 1e-3f);
	EXPECT_NEAR(solved[1].position.z, 0.0f, 1e-3f);

	EXPECT_NEAR(ModelPosition(*centimetre, pose, 1).y, 1.3f, 1e-3f);
	EXPECT_NEAR(ModelPosition(*metre, solved, 1).y, 2.6f, 1e-3f);

	const std::vector<BoneTransform> back = Solve(*metreProfile, *centimetreProfile, solved);
	ASSERT_EQ(back.size(), 3u);
	EXPECT_NEAR(back[1].position.y, 130.0f, 1e-2f);
	EXPECT_NEAR(back[1].position.x, 0.0f, 1e-2f);
}

TEST_F(RetargetFixture, ScaledAncestorMidChainInModelSpace) {
	SkeletonAsset* source = MakeRig({{"root", -1, Vec3(), Quat::Identity()},
									 {"a0", 0, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									 {"a1", 1, XAxis, Quat::Identity()},
									 {"a2", 2, XAxis, Quat::Identity()}});
	SkeletonAsset* target = MakeRig({{"ROOT", -1, Vec3(), Quat::Identity(), Vec3(0.1f, 0.1f, 0.1f)},
									 {"A0", 0, Vec3(0.0f, 100.0f, 0.0f), Quat::Identity()},
									 {"A1", 1, XAxis * 30.0f, Quat::Identity()},
									 {"A2", 2, XAxis * 30.0f, Quat::Identity()}});

	RetargetProfile* sourceProfile = MakeProfile(source, {{"root", {0}}, {"arm", {1, 2, 3}}});
	RetargetProfile* targetProfile = MakeProfile(target, {{"root", {0}}, {"arm", {1, 2, 3}}});

	const RetargetProfile::Chain* sourceArm = sourceProfile->FindChain("arm");
	const RetargetProfile::Chain* targetArm = targetProfile->FindChain("arm");
	ASSERT_NE(sourceArm, nullptr);
	ASSERT_NE(targetArm, nullptr);
	EXPECT_NEAR(AnimationImpl::ChainReferenceLength(*sourceProfile, *sourceArm), 2.0f, 1e-4f);
	EXPECT_NEAR(AnimationImpl::ChainReferenceLength(*targetProfile, *targetArm), 6.0f, 1e-4f);

	const f32 rigRatio = AnimationImpl::ProfileReferenceScale(*targetProfile) /
						 AnimationImpl::ProfileReferenceScale(*sourceProfile);
	EXPECT_GT(Math::Abs(rigRatio - 3.0f), 0.05f);

	std::vector<BoneTransform> pose = RestPose(*source);
	pose[2].position = Vec3(1.1f, 0.0f, 0.0f);

	const std::vector<BoneTransform> solved = Solve(*sourceProfile, *targetProfile, pose);
	ASSERT_EQ(solved.size(), 4u);

	EXPECT_NEAR(solved[2].position.x, 33.0f, 1e-2f);
	EXPECT_NEAR(ModelPosition(*target, solved, 2).x, 3.3f, 1e-3f);
	EXPECT_NEAR(ModelPosition(*source, pose, 2).x, 1.1f, 1e-3f);
}

TEST_F(RetargetFixture, RigRootTakesNoScaleCorrection) {
	SkeletonAsset* centimetre = MakeRig({{"Reference", -1, Vec3(), Quat::Identity(), Vec3(0.01f, 0.01f, 0.01f)},
										 {"hips", 0, Vec3(0.0f, 100.0f, 0.0f), Quat::Identity()}});
	SkeletonAsset* metre = MakeRig({{"Armature", -1, Vec3(), Quat::Identity()},
									{"Hips", 0, Vec3(0.0f, 2.0f, 0.0f), Quat::Identity()}});

	RetargetProfile* centimetreProfile = MakeProfile(centimetre, {{"root", {0}}, {"hips", {1}}});
	RetargetProfile* metreProfile = MakeProfile(metre, {{"root", {0}}, {"hips", {1}}});

	AnimationImpl::RetargetMap map{};
	ASSERT_TRUE(AnimationImpl::BuildRetargetMap(*centimetreProfile, *metreProfile, map, AnimationImpl::RetargetSides::AsAuthored));
	EXPECT_NEAR(map.rigScale, 2.0f, 1e-4f);

	BoneTransform delta;
	delta.position = Vec3(0.0f, 0.0f, 1.0f);
	const BoneTransform transferred =
		AnimationImpl::SolveRetargetRootDelta(map, *centimetreProfile, *metreProfile, delta);
	EXPECT_NEAR(transferred.position.z, 2.0f, 1e-4f);

	BoneTransform anchor;
	anchor.position = Vec3(0.0f, 0.3f, 0.0f);
	const BoneTransform solvedAnchor =
		AnimationImpl::SolveRetargetRoot(map, *centimetreProfile, *metreProfile, anchor);
	EXPECT_NEAR(solvedAnchor.position.y, 0.6f, 1e-4f);
}

TEST_F(RetargetFixture, ScaleArrivesAsTargetRatio) {
	SkeletonAsset* source = MakeRig({{"s", -1, Vec3(), Quat::Identity()}});
	SkeletonAsset* target = MakeRig({{"t", -1, Vec3(), Quat::Identity()}});
	target->restPose[0].scale = Vec3(2.0f, 2.0f, 2.0f);

	RetargetProfile* sourceProfile = MakeProfile(source, {{"body", {0}}});
	RetargetProfile* targetProfile = MakeProfile(target, {{"body", {0}}});

	std::vector<BoneTransform> pose = RestPose(*source);
	pose[0].scale = Vec3(0.6f, 1.0f, 1.0f);

	const std::vector<BoneTransform> solved = Solve(*sourceProfile, *targetProfile, pose);
	ASSERT_EQ(solved.size(), 1u);
	EXPECT_NEAR(solved[0].scale.x, 1.2f, 1e-4f);
	EXPECT_NEAR(solved[0].scale.y, 2.0f, 1e-4f);
}

namespace {

	struct LegRig {
		SkeletonAsset* skeleton = nullptr;
		RetargetProfile* profile = nullptr;
	};

	constexpr f32 RIG_HEIGHT = 1.4f;
	constexpr f32 SOURCE_HIP = 0.9f;
	constexpr f32 TARGET_HIP = 1.0f;

	constexpr f32 SOURCE_THIGH_DROP = 0.045f;
	constexpr f32 TARGET_THIGH_DROP = 0.18f;
	constexpr f32 SOURCE_LEG = SOURCE_HIP - SOURCE_THIGH_DROP;
	constexpr f32 TARGET_LEG = TARGET_HIP - TARGET_THIGH_DROP;

	f32 PlantedHipHeight(f32 degrees) {
		return SOURCE_THIGH_DROP + SOURCE_LEG * std::cos(degrees * Math::DEG_TO_RAD);
	}

} // anonymous namespace

TEST_F(RetargetFixture, FootPlantHoldsAcrossProportions) {
	SkeletonAsset* source = MakeRig({{"root", -1, Vec3(), Quat::Identity()},
									 {"hips", 0, Vec3(0.0f, SOURCE_HIP, 0.0f), Quat::Identity()},
									 {"spine", 1, Vec3(0.0f, RIG_HEIGHT - SOURCE_HIP, 0.0f), Quat::Identity()},
									 {"thigh", 1, Vec3(0.0f, -SOURCE_THIGH_DROP, 0.0f), Quat::Identity()},
									 {"shin", 3, Vec3(0.0f, -SOURCE_LEG * 0.5f, 0.0f), Quat::Identity()},
									 {"foot", 4, Vec3(0.0f, -SOURCE_LEG * 0.5f, 0.0f), Quat::Identity()}});
	SkeletonAsset* target = MakeRig({{"ROOT", -1, Vec3(), Quat::Identity()},
									 {"HIPS", 0, Vec3(0.0f, TARGET_HIP, 0.0f), Quat::Identity()},
									 {"SPINE", 1, Vec3(0.0f, RIG_HEIGHT - TARGET_HIP, 0.0f), Quat::Identity()},
									 {"THIGH", 1, Vec3(0.0f, -TARGET_THIGH_DROP, 0.0f), Quat::Identity()},
									 {"SHIN", 3, Vec3(0.0f, -TARGET_LEG * 0.5f, 0.0f), Quat::Identity()},
									 {"FOOT", 4, Vec3(0.0f, -TARGET_LEG * 0.5f, 0.0f), Quat::Identity()}});

	RetargetProfile* sourceProfile = MakeProfile(source, {{"hips", {1}}, {"spine", {2}}, {"leg", {3, 4, 5}}});
	RetargetProfile* targetProfile = MakeProfile(target, {{"hips", {1}}, {"spine", {2}}, {"leg", {3, 4, 5}}});

	f32 worstWithout = 0.0f;
	f32 worstWith = 0.0f;
	for (f32 degrees : {10.0f, 25.0f, 40.0f, 50.0f}) {
		std::vector<BoneTransform> sourcePose = RestPose(*source);
		sourcePose[1].position = Vec3(0.0f, PlantedHipHeight(degrees), 0.0f);
		sourcePose[3].rotation = Quat::AngleAxis(degrees, Vec3::Forward());
		sourcePose[4].rotation = Quat::AngleAxis(-2.0f * degrees, Vec3::Forward());

		ASSERT_NEAR(ModelPosition(*source, sourcePose, 5).y, 0.0f, 1e-5f) << degrees << " degrees";

		targetProfile->goals.clear();
		const std::vector<BoneTransform> without = Solve(*sourceProfile, *targetProfile, sourcePose);
		ASSERT_FALSE(without.empty());
		worstWithout = Math::Max(worstWithout, Math::Abs(ModelPosition(*target, without, 5).y));

		targetProfile->goals.push_back(str("leg"));
		const std::vector<BoneTransform> with = Solve(*sourceProfile, *targetProfile, sourcePose);
		ASSERT_FALSE(with.empty());
		const Vec3 planted = ModelPosition(*target, with, 5);
		worstWith = Math::Max(worstWith, Math::Abs(planted.y));
		EXPECT_NEAR(planted.x, 0.0f, 1e-4f) << degrees << " degrees";
	}

	EXPECT_GT(worstWithout, 0.03f);
	EXPECT_LT(worstWith, 1e-4f);
}

TEST_F(RetargetFixture, GoalContactScalesFromRoot) {
	SkeletonAsset* source = MakeRig({{"root", -1, Vec3(1.0f, 0.0f, 0.0f), Quat::Identity()},
									 {"hips", 0, Vec3(0.0f, 0.9f, 0.0f), Quat::Identity()},
									 {"spine", 1, Vec3(0.0f, 0.5f, 0.0f), Quat::Identity()},
									 {"thigh", 1, Vec3(), Quat::Identity()},
									 {"shin", 3, Vec3(0.0f, -0.45f, 0.0f), Quat::Identity()},
									 {"foot", 4, Vec3(0.0f, -0.45f, 0.0f), Quat::Identity()}});
	SkeletonAsset* target = MakeRig({{"ROOT", -1, Vec3(-2.0f, 0.0f, 0.0f), Quat::Identity()},
									 {"HIPS", 0, Vec3(0.0f, 2.0f, 0.0f), Quat::Identity()},
									 {"SPINE", 1, Vec3(0.0f, 0.8f, 0.0f), Quat::Identity()},
									 {"THIGH", 1, Vec3(), Quat::Identity()},
									 {"SHIN", 3, Vec3(0.0f, -1.0f, 0.0f), Quat::Identity()},
									 {"FOOT", 4, Vec3(0.0f, -1.0f, 0.0f), Quat::Identity()}});

	RetargetProfile* sourceProfile = MakeProfile(source, {{"hips", {1}}, {"spine", {2}}, {"leg", {3, 4, 5}}});
	RetargetProfile* targetProfile = MakeProfile(target, {{"hips", {1}}, {"spine", {2}}, {"leg", {3, 4, 5}}});

	std::vector<BoneTransform> sourcePose = RestPose(*source);
	sourcePose[3].rotation = Quat::AngleAxis(35.0f, Vec3::Forward());
	sourcePose[4].rotation = Quat::AngleAxis(-50.0f, Vec3::Forward());

	const Vec3 sourceContact = ModelPosition(*source, sourcePose, 5) - ModelPosition(*source, sourcePose, 0);
	ASSERT_GT(Math::Abs(sourceContact.x), 0.05f);
	ASSERT_GT(Math::Abs(sourceContact.y), 0.05f);

	const std::vector<BoneTransform> without = Solve(*sourceProfile, *targetProfile, sourcePose);
	ASSERT_FALSE(without.empty());
	const Vec3 driftWithout =
		(ModelPosition(*target, without, 5) - ModelPosition(*target, without, 0)) - sourceContact * 2.0f;
	EXPECT_GT(driftWithout.Length(), 0.02f);

	targetProfile->goals.push_back(str("leg"));
	const std::vector<BoneTransform> with = Solve(*sourceProfile, *targetProfile, sourcePose);
	ASSERT_FALSE(with.empty());
	const Vec3 targetContact = ModelPosition(*target, with, 5) - ModelPosition(*target, with, 0);

	EXPECT_NEAR((targetContact - sourceContact * 2.0f).Length(), 0.0f, 1e-4f);
	EXPECT_NEAR(ModelPosition(*target, with, 0).x, -2.0f, 1e-4f);
}

TEST_F(RetargetFixture, AGoalKeepsTheTipsTransferredOrientation) {
	SkeletonAsset* source = MakeRig({{"root", -1, Vec3(), Quat::Identity()},
									 {"hips", 0, Vec3(0.0f, SOURCE_HIP, 0.0f), Quat::Identity()},
									 {"spine", 1, Vec3(0.0f, RIG_HEIGHT - SOURCE_HIP, 0.0f), Quat::Identity()},
									 {"thigh", 1, Vec3(0.0f, -SOURCE_THIGH_DROP, 0.0f), Quat::Identity()},
									 {"shin", 3, Vec3(0.0f, -SOURCE_LEG * 0.5f, 0.0f), Quat::Identity()},
									 {"foot", 4, Vec3(0.0f, -SOURCE_LEG * 0.5f, 0.0f), Quat::Identity()}});
	SkeletonAsset* target = MakeRig({{"ROOT", -1, Vec3(), Quat::Identity()},
									 {"HIPS", 0, Vec3(0.0f, TARGET_HIP, 0.0f), Quat::Identity()},
									 {"SPINE", 1, Vec3(0.0f, RIG_HEIGHT - TARGET_HIP, 0.0f), Quat::Identity()},
									 {"THIGH", 1, Vec3(0.0f, -TARGET_THIGH_DROP, 0.0f), Quat::Identity()},
									 {"SHIN", 3, Vec3(0.0f, -TARGET_LEG * 0.5f, 0.0f), Quat::Identity()},
									 {"FOOT", 4, Vec3(0.0f, -TARGET_LEG * 0.5f, 0.0f), Quat::Identity()}});

	RetargetProfile* sourceProfile = MakeProfile(source, {{"hips", {1}}, {"spine", {2}}, {"leg", {3, 4, 5}}});
	RetargetProfile* targetProfile = MakeProfile(target, {{"hips", {1}}, {"spine", {2}}, {"leg", {3, 4, 5}}});

	std::vector<BoneTransform> sourcePose = RestPose(*source);
	sourcePose[1].position = Vec3(0.0f, PlantedHipHeight(45.0f), 0.0f);
	sourcePose[3].rotation = Quat::AngleAxis(45.0f, Vec3::Forward());
	sourcePose[4].rotation = Quat::AngleAxis(-90.0f, Vec3::Forward());
	sourcePose[5].rotation = Quat::AngleAxis(18.0f, Vec3::Forward());

	const std::vector<BoneTransform> without = Solve(*sourceProfile, *targetProfile, sourcePose);
	ASSERT_FALSE(without.empty());
	const Quat transferred = ModelRotation(*target, without, 5);

	targetProfile->goals.push_back(str("leg"));
	const std::vector<BoneTransform> with = Solve(*sourceProfile, *targetProfile, sourcePose);
	ASSERT_FALSE(with.empty());

	EXPECT_NEAR(Quat::Angle(ModelRotation(*target, with, 5), transferred), 0.0f, 1e-2f);
	EXPECT_GT(Quat::Angle(ModelRotation(*target, with, 4), ModelRotation(*target, without, 4)), 1.0f);
}

TEST_F(RetargetFixture, StraightLimbBendsFromReference) {
	SkeletonAsset* source = MakeRig({{"root", -1, Vec3(), Quat::Identity()},
									 {"hips", 0, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									 {"thigh", 1, Vec3(), Quat::Identity()},
									 {"shin", 2, Vec3(0.0f, -0.5f, 0.0f), Quat::Identity()},
									 {"foot", 3, Vec3(0.0f, -0.5f, 0.0f), Quat::Identity()}});
	SkeletonAsset* target = MakeRig({{"ROOT", -1, Vec3(), Quat::Identity()},
									 {"HIPS", 0, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									 {"THIGH", 1, Vec3(), Quat::Identity()},
									 {"SHIN", 2, Vec3(0.0f, -0.5f, 0.0f), Quat::Identity()},
									 {"FOOT", 3, Vec3(0.0f, -0.5f, 0.0f), Quat::Identity()}});

	RetargetProfile* sourceProfile = MakeProfile(source, {{"hips", {1}}, {"leg", {2, 3, 4}}});
	RetargetProfile* targetProfile = MakeProfile(target, {{"hips", {1}}, {"leg", {2, 3, 4}}});

	constexpr f32 REFERENCE_BEND = 10.0f;
	targetProfile->referencePose.assign(target->restPose.begin(), target->restPose.end());
	targetProfile->referencePose[2].rotation = Quat::AngleAxis(REFERENCE_BEND, Vec3::Forward());
	targetProfile->referencePose[3].rotation = Quat::AngleAxis(-2.0f * REFERENCE_BEND, Vec3::Forward());

	std::vector<BoneTransform> sourcePose = RestPose(*source);
	sourcePose[2].rotation = Quat::AngleAxis(-REFERENCE_BEND, Vec3::Forward());
	sourcePose[3].rotation = Quat::AngleAxis(2.0f * REFERENCE_BEND, Vec3::Forward());

	const std::vector<BoneTransform> without = Solve(*sourceProfile, *targetProfile, sourcePose);
	ASSERT_FALSE(without.empty());
	const Vec3 straightRoot = ModelPosition(*target, without, 2);
	const Vec3 straightMid = ModelPosition(*target, without, 3);
	const Vec3 straightTip = ModelPosition(*target, without, 4);
	ASSERT_LT(Vec3::Cross(straightTip - straightRoot, straightMid - straightRoot).Length(), 1e-5f);

	targetProfile->goals.push_back(str("leg"));
	const std::vector<BoneTransform> solved = Solve(*sourceProfile, *targetProfile, sourcePose);
	ASSERT_FALSE(solved.empty());

	const Vec3 knee = ModelPosition(*target, solved, 3);
	const Vec3 foot = ModelPosition(*target, solved, 4);
	EXPECT_NEAR((foot - ModelPosition(*source, sourcePose, 4)).Length(), 0.0f, 1e-4f);

	const Vec3 referenceKnee =
		ModelPosition(*target, std::span<const BoneTransform>(targetProfile->referencePose.data(), targetProfile->referencePose.size()),
					  3);
	EXPECT_GT(Math::Abs(knee.x), 0.05f);
	EXPECT_GT(knee.x * referenceKnee.x, 0.0f);
}

TEST_F(RetargetFixture, UnreachableGoalStraightensLimb) {
	SkeletonAsset* source = MakeRig({{"root", -1, Vec3(), Quat::Identity()},
									 {"hips", 0, Vec3(0.0f, 2.0f, 0.0f), Quat::Identity()},
									 {"thigh", 1, Vec3(), Quat::Identity()},
									 {"shin", 2, Vec3(0.0f, -1.0f, 0.0f), Quat::Identity()},
									 {"foot", 3, Vec3(0.0f, -1.0f, 0.0f), Quat::Identity()}});
	SkeletonAsset* target = MakeRig({{"ROOT", -1, Vec3(), Quat::Identity()},
									 {"HIPS", 0, Vec3(0.0f, 2.0f, 0.0f), Quat::Identity()},
									 {"THIGH", 1, Vec3(), Quat::Identity()},
									 {"SHIN", 2, Vec3(0.0f, -0.5f, 0.0f), Quat::Identity()},
									 {"FOOT", 3, Vec3(0.0f, -0.5f, 0.0f), Quat::Identity()}});

	RetargetProfile* sourceProfile = MakeProfile(source, {{"hips", {1}}, {"leg", {2, 3, 4}}});
	RetargetProfile* targetProfile = MakeProfile(target, {{"hips", {1}}, {"leg", {2, 3, 4}}});
	targetProfile->goals.push_back(str("leg"));

	std::vector<BoneTransform> sourcePose = RestPose(*source);
	sourcePose[2].rotation = Quat::AngleAxis(30.0f, Vec3::Forward());
	sourcePose[3].rotation = Quat::AngleAxis(-60.0f, Vec3::Forward());

	const std::vector<BoneTransform> solved = Solve(*sourceProfile, *targetProfile, sourcePose);
	ASSERT_FALSE(solved.empty());

	const Vec3 hip = ModelPosition(*target, solved, 2);
	const Vec3 foot = ModelPosition(*target, solved, 4);
	const Vec3 contact = ModelPosition(*source, sourcePose, 4);

	EXPECT_TRUE(Math::IsFinite(foot.x) && Math::IsFinite(foot.y) && Math::IsFinite(foot.z));
	EXPECT_NEAR((foot - hip).Length(), 1.0f, 1e-3f);
	EXPECT_NEAR(Vec3::Angle(foot - hip, contact - hip), 0.0f, 1e-2f);
}

TEST_F(RetargetFixture, UnsolvableGoalSkipped) {
	SkeletonAsset* source = MakeRig({{"root", -1, Vec3(), Quat::Identity()},
									 {"hips", 0, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									 {"thigh", 1, Vec3(), Quat::Identity()},
									 {"shin", 2, Vec3(0.0f, -0.5f, 0.0f), Quat::Identity()},
									 {"foot", 3, Vec3(0.0f, -0.5f, 0.0f), Quat::Identity()},
									 {"tail", 1, Vec3(0.0f, -0.1f, -0.3f), Quat::Identity()}});
	SkeletonAsset* target = MakeRig({{"ROOT", -1, Vec3(), Quat::Identity()},
									 {"HIPS", 0, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									 {"THIGH", 1, Vec3(), Quat::Identity()},
									 {"SHIN", 2, Vec3(0.0f, -0.5f, 0.0f), Quat::Identity()},
									 {"FOOT", 3, Vec3(0.0f, -0.5f, 0.0f), Quat::Identity()},
									 {"TAIL", 1, Vec3(0.0f, -0.1f, -0.3f), Quat::Identity()}});

	RetargetProfile* sourceProfile = MakeProfile(source, {{"hips", {1}}, {"leg", {2, 3, 4}}, {"tail", {5}}});
	RetargetProfile* targetProfile = MakeProfile(target, {{"hips", {1}}, {"leg", {2, 3, 4}}, {"tail", {5}}});

	std::vector<BoneTransform> sourcePose = RestPose(*source);
	sourcePose[2].rotation = Quat::AngleAxis(20.0f, Vec3::Forward());
	sourcePose[3].rotation = Quat::AngleAxis(-40.0f, Vec3::Forward());
	const std::vector<BoneTransform> plain = Solve(*sourceProfile, *targetProfile, sourcePose);
	ASSERT_FALSE(plain.empty());

	auto expectUntouched = [&](const std::vector<BoneTransform>& solved, const char* what) {
		ASSERT_EQ(solved.size(), plain.size()) << what;
		for (i16 bone = 0; bone < static_cast<i16>(target->parents.size()); ++bone) {
			EXPECT_NEAR(Quat::Angle(ModelRotation(*target, solved, bone), ModelRotation(*target, plain, bone)), 0.0f,
						1e-3f)
				<< what << " moved bone " << bone;
			EXPECT_NEAR((ModelPosition(*target, solved, bone) - ModelPosition(*target, plain, bone)).Length(), 0.0f,
						1e-5f)
				<< what << " moved bone " << bone;
		}
	};

	targetProfile->goals.assign({str("wing")});
	expectUntouched(Solve(*sourceProfile, *targetProfile, sourcePose), "a goal naming no chain");

	targetProfile->goals.assign({str("tail")});
	expectUntouched(Solve(*sourceProfile, *targetProfile, sourcePose), "a goal on a one-bone chain");

	targetProfile->chains.push_back(RetargetProfile::Chain{});
	targetProfile->chains.back().role = str("scatter");
	targetProfile->chains.back().bones.assign({static_cast<i16>(1), static_cast<i16>(4), static_cast<i16>(5)});
	sourceProfile->chains.push_back(RetargetProfile::Chain{});
	sourceProfile->chains.back().role = str("scatter");
	sourceProfile->chains.back().bones.assign({static_cast<i16>(1), static_cast<i16>(4), static_cast<i16>(5)});
	targetProfile->goals.assign({str("scatter")});
	expectUntouched(Solve(*sourceProfile, *targetProfile, sourcePose), "a goal on a chain that is not a run");
}

TEST_F(RetargetFixture, RootDeltaScalesTravelOnly) {
	SkeletonAsset* source = MakeRig({{"root", -1, Vec3(), Quat::Identity()},
									 {"hips", 0, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()}});
	SkeletonAsset* target = MakeRig({{"ROOT", -1, Vec3(), Quat::AngleAxis(90.0f, Vec3::Up())},
									 {"HIPS", 0, Vec3(0.0f, 2.0f, 0.0f), Quat::Identity()}});

	RetargetProfile* sourceProfile = MakeProfile(source, {{"root", {0}}, {"hips", {1}}});
	RetargetProfile* targetProfile = MakeProfile(target, {{"root", {0}}, {"hips", {1}}});

	AnimationImpl::RetargetMap map{};
	ASSERT_TRUE(AnimationImpl::BuildRetargetMap(*sourceProfile, *targetProfile, map, AnimationImpl::RetargetSides::AsAuthored));
	EXPECT_NEAR(map.rigScale, 2.0f, 1e-4f);

	BoneTransform delta;
	delta.position = Vec3(0.0f, 0.0f, 1.0f);
	delta.rotation = Quat::AngleAxis(90.0f, Vec3::Up());

	const BoneTransform transferred =
		AnimationImpl::SolveRetargetRootDelta(map, *sourceProfile, *targetProfile, delta);

	EXPECT_NEAR(transferred.position.Length(), 2.0f, 1e-4f);
	EXPECT_NEAR(Quat::Angle(transferred.rotation, Quat::AngleAxis(90.0f, Vec3::Up())), 0.0f, 1e-2f);
	EXPECT_NEAR(transferred.position.x, -2.0f, 1e-3f);

	BoneTransform anchor;
	anchor.position = Vec3(0.0f, 0.3f, 0.0f);
	const BoneTransform solvedAnchor =
		AnimationImpl::SolveRetargetRoot(map, *sourceProfile, *targetProfile, anchor);
	EXPECT_NEAR(solvedAnchor.position.y, 0.6f, 1e-4f);
	EXPECT_NEAR(Quat::Angle(solvedAnchor.rotation, Quat::AngleAxis(90.0f, Vec3::Up())), 0.0f, 1e-2f);
}

TEST_F(RetargetFixture, FingerFullRangeWithinCentidegree) {
	const Quat sourceRoll = Quat::AngleAxis(15.0f, XAxis);
	const Quat targetRoll = Quat::AngleAxis(-70.0f, Vec3(0.3f, 1.0f, 0.4f));

	SkeletonAsset* source = MakeRig({{"hand", -1, Vec3(0.0f, 1.0f, 0.0f), sourceRoll},
									 {"index1", 0, XAxis * 0.04f, Quat::Identity()},
									 {"index2", 1, XAxis * 0.03f, Quat::Identity()},
									 {"index3", 2, XAxis * 0.02f, Quat::Identity()}});
	SkeletonAsset* target = MakeRig({{"HAND", -1, Vec3(0.0f, 1.7f, 0.0f), targetRoll},
									 {"INDEX1", 0, XAxis * 0.09f, Quat::AngleAxis(20.0f, Vec3::Up())},
									 {"INDEX2", 1, XAxis * 0.07f, Quat::Identity()},
									 {"INDEX3", 2, XAxis * 0.05f, Quat::Identity()}});

	RetargetProfile* sourceProfile = MakeProfile(source, {{"hand", {0}}, {"index", {1, 2, 3}}});
	RetargetProfile* targetProfile = MakeProfile(target, {{"hand", {0}}, {"index", {1, 2, 3}}});

	std::vector<BoneTransform> pose = RestPose(*source);
	const Quat curl = Quat::AngleAxis(85.0f, Vec3::Forward());
	pose[1].rotation = curl;
	pose[2].rotation = curl;
	pose[3].rotation = curl;

	const std::vector<BoneTransform> solved = Solve(*sourceProfile, *targetProfile, pose);
	ASSERT_EQ(solved.size(), 4u);

	const std::vector<BoneTransform> sourceRest = RestPose(*source);
	const std::vector<BoneTransform> targetRest = RestPose(*target);
	for (i16 bone = 1; bone <= 3; ++bone) {
		const Quat sourceDelta =
			(ModelRotation(*source, pose, bone) * ModelRotation(*source, sourceRest, bone).Conjugated()).Normalized();
		const Quat targetDelta =
			(ModelRotation(*target, solved, bone) * ModelRotation(*target, targetRest, bone).Conjugated()).Normalized();
		EXPECT_NEAR(Quat::Angle(sourceDelta, targetDelta), 0.0f, 0.01f);
	}

	const Quat middleDelta =
		(ModelRotation(*source, pose, 2) * ModelRotation(*source, sourceRest, 2).Conjugated()).Normalized();
	EXPECT_GT(Quat::Angle(middleDelta, Quat::Identity()), 150.0f);
}

namespace {

	const std::vector<const char*> MixamoNames = {
		"mixamorig:Hips", "mixamorig:Spine", "mixamorig:Spine1", "mixamorig:Spine2",
		"mixamorig:Neck", "mixamorig:Head",
		"mixamorig:LeftShoulder", "mixamorig:LeftArm", "mixamorig:LeftForeArm", "mixamorig:LeftHand",
		"mixamorig:RightShoulder", "mixamorig:RightArm", "mixamorig:RightForeArm", "mixamorig:RightHand",
		"mixamorig:LeftUpLeg", "mixamorig:LeftLeg", "mixamorig:LeftFoot", "mixamorig:LeftToeBase",
		"mixamorig:RightUpLeg", "mixamorig:RightLeg", "mixamorig:RightFoot", "mixamorig:RightToeBase",
		"mixamorig:LeftHandIndex1", "mixamorig:LeftHandIndex2", "mixamorig:LeftHandIndex3"};
	const std::vector<i16> MixamoParents = {-1, 0, 1, 2, 3, 4, 3, 6, 7, 8, 3, 10, 11, 12, 0, 14, 15, 16, 0, 18, 19, 20,
											9, 22, 23};

} // anonymous namespace

TEST_F(RetargetFixture, MixamoRigAutoMaps) {
	SkeletonAsset* skeleton = MakeNamedRig(MixamoNames, MixamoParents);
	RetargetProfile* profile = Retarget::AutoMapHumanoid(Handle<SkeletonAsset>(skeleton));
	ASSERT_NE(profile, nullptr);
	_owned.push_back(profile);

	auto boneOf = [&](const char* name) {
		return static_cast<i16>(skeleton->FindBone(name));
	};
	auto chainIs = [&](const char* role, const std::vector<const char*>& names) {
		const RetargetProfile::Chain* chain = profile->FindChain(role);
		ASSERT_NE(chain, nullptr) << role << "'";
		ASSERT_EQ(chain->bones.size(), names.size()) << "role '" << role;
		for (size_t index = 0; index < names.size(); ++index) {
			EXPECT_EQ(chain->bones[index], boneOf(names[index]))
				<< "role '" << role << "' bone " << index << " is not '" << names[index] << "'";
		}
	};

	chainIs("hips", {"mixamorig:Hips"});
	chainIs("spine", {"mixamorig:Spine", "mixamorig:Spine1", "mixamorig:Spine2"});
	chainIs("neck", {"mixamorig:Neck", "mixamorig:Head"});
	chainIs("leftShoulder", {"mixamorig:LeftShoulder"});
	chainIs("leftArm", {"mixamorig:LeftArm", "mixamorig:LeftForeArm", "mixamorig:LeftHand"});
	chainIs("rightArm", {"mixamorig:RightArm", "mixamorig:RightForeArm", "mixamorig:RightHand"});
	chainIs("leftLeg", {"mixamorig:LeftUpLeg", "mixamorig:LeftLeg", "mixamorig:LeftFoot", "mixamorig:LeftToeBase"});
	chainIs("rightLeg", {"mixamorig:RightUpLeg", "mixamorig:RightLeg", "mixamorig:RightFoot", "mixamorig:RightToeBase"});

	chainIs("leftIndex", {"mixamorig:LeftHandIndex1", "mixamorig:LeftHandIndex2", "mixamorig:LeftHandIndex3"});
	EXPECT_EQ(profile->FindChain("rightIndex"), nullptr);

	std::vector<i16> seen;
	for (const RetargetProfile::Chain& chain : profile->chains) {
		for (i16 bone : chain.bones) {
			EXPECT_EQ(std::find(seen.begin(), seen.end(), bone), seen.end());
			seen.push_back(bone);
		}
	}
}

TEST_F(RetargetFixture, ChainStopsAtBranchTwistIsOne) {
	std::vector<const char*> names = MixamoNames;
	std::vector<i16> parents = MixamoParents;
	names.push_back("mixamorig:LeftHandIndex1Twist");
	parents.push_back(static_cast<i16>(22));

	SkeletonAsset* skeleton = MakeNamedRig(names, parents);
	RetargetProfile* profile = Retarget::AutoMapHumanoid(Handle<SkeletonAsset>(skeleton));
	ASSERT_NE(profile, nullptr);
	_owned.push_back(profile);

	const RetargetProfile::Chain* index = profile->FindChain("leftIndex");
	ASSERT_NE(index, nullptr);
	ASSERT_EQ(index->bones.size(), 1u);
	EXPECT_EQ(index->bones[0], skeleton->FindBone("mixamorig:LeftHandIndex1"));

	const RetargetProfile::Chain* arm = profile->FindChain("leftArm");
	ASSERT_NE(arm, nullptr);
	EXPECT_EQ(arm->bones.size(), 3u);

	const RetargetProfile::Chain* leftArm = profile->FindChain("leftArm");
	const RetargetProfile::Chain* rightArm = profile->FindChain("rightArm");
	ASSERT_NE(leftArm, nullptr);
	ASSERT_NE(rightArm, nullptr);
	EXPECT_NE(leftArm->bones[0], rightArm->bones[0]);

	ASSERT_EQ(profile->referencePose.size(), skeleton->restPose.size());
	for (size_t bone = 0; bone < profile->referencePose.size(); ++bone) {
		EXPECT_TRUE(Vec3::Approximately(profile->referencePose[bone].position, skeleton->restPose[bone].position, 1e-5f));
	}
	EXPECT_EQ(profile->GetReferencePose().size(), skeleton->restPose.size());
}

TEST_F(RetargetFixture, BlenderSideSuffixedRigAutoMapsToo) {
	const std::vector<const char*> names = {
		"hips", "spine", "chest",
		"upper_arm.L", "forearm.L", "hand.L",
		"upper_arm.R", "forearm.R", "hand.R",
		"thigh.L", "shin.L", "foot.L",
		"thigh.R", "shin.R", "foot.R"};
	const std::vector<i16> parents = {-1, 0, 1, 2, 3, 4, 2, 6, 7, 0, 9, 10, 0, 12, 13};

	SkeletonAsset* skeleton = MakeNamedRig(names, parents);
	RetargetProfile* profile = Retarget::AutoMapHumanoid(Handle<SkeletonAsset>(skeleton));
	ASSERT_NE(profile, nullptr);
	_owned.push_back(profile);

	const RetargetProfile::Chain* arm = profile->FindChain("leftArm");
	ASSERT_NE(arm, nullptr);
	ASSERT_EQ(arm->bones.size(), 3u);
	EXPECT_EQ(arm->bones[0], skeleton->FindBone("upper_arm.L"));
	EXPECT_EQ(arm->bones[2], skeleton->FindBone("hand.L"));

	const RetargetProfile::Chain* leg = profile->FindChain("rightLeg");
	ASSERT_NE(leg, nullptr);
	EXPECT_EQ(leg->bones[0], skeleton->FindBone("thigh.R"));
	EXPECT_EQ(leg->bones.back(), skeleton->FindBone("foot.R"));

	const RetargetProfile::Chain* spine = profile->FindChain("spine");
	ASSERT_NE(spine, nullptr);
	EXPECT_EQ(spine->bones.back(), skeleton->FindBone("chest"));
}

TEST_F(RetargetFixture, HipsAndPelvisGiveOneRoot) {
	const std::vector<const char*> names = {
		"Root", "Hips", "Pelvis", "Spine", "Chest",
		"LeftArm", "LeftForeArm", "LeftHand",
		"RightArm", "RightForeArm", "RightHand",
		"LeftUpLeg", "LeftLeg", "LeftFoot",
		"RightUpLeg", "RightLeg", "RightFoot"};
	const std::vector<i16> parents = {-1, 0, 0, 1, 3, 4, 5, 6, 4, 8, 9, 1, 11, 12, 1, 14, 15};

	SkeletonAsset* skeleton = MakeNamedRig(names, parents);
	RetargetProfile* profile = Retarget::AutoMapHumanoid(Handle<SkeletonAsset>(skeleton));
	ASSERT_NE(profile, nullptr);
	_owned.push_back(profile);

	const RetargetProfile::Chain* hips = profile->FindChain("hips");
	ASSERT_NE(hips, nullptr);
	ASSERT_EQ(hips->bones.size(), 1u);
	EXPECT_EQ(hips->bones[0], skeleton->FindBone("Hips"));

	const RetargetProfile::Chain* spine = profile->FindChain("spine");
	ASSERT_NE(spine, nullptr);
	EXPECT_EQ(spine->bones.back(), skeleton->FindBone("Chest"));
}

TEST_F(RetargetFixture, ShortSidePrefixesAutoMap) {
	const std::vector<const char*> names = {
		"Bip01", "Bip01 Pelvis", "Bip01 Spine", "Bip01 Spine1", "Bip01 Neck", "Bip01 Head",
		"Bip01 L UpperArm", "Bip01 L Forearm", "Bip01 L Hand",
		"Bip01 R UpperArm", "Bip01 R Forearm", "Bip01 R Hand",
		"Bip01 L Thigh", "Bip01 L Calf", "Bip01 L Foot",
		"Bip01 R Thigh", "Bip01 R Calf", "Bip01 R Foot"};
	const std::vector<i16> parents = {-1, 0, 1, 2, 3, 4, 3, 6, 7, 3, 9, 10, 1, 12, 13, 1, 15, 16};

	SkeletonAsset* skeleton = MakeNamedRig(names, parents);
	RetargetProfile* profile = Retarget::AutoMapHumanoid(Handle<SkeletonAsset>(skeleton));
	ASSERT_NE(profile, nullptr);
	_owned.push_back(profile);

	const RetargetProfile::Chain* arm = profile->FindChain("leftArm");
	ASSERT_NE(arm, nullptr);
	ASSERT_EQ(arm->bones.size(), 3u);
	EXPECT_EQ(arm->bones[0], skeleton->FindBone("Bip01 L UpperArm"));
	EXPECT_EQ(arm->bones[2], skeleton->FindBone("Bip01 L Hand"));

	const RetargetProfile::Chain* leg = profile->FindChain("rightLeg");
	ASSERT_NE(leg, nullptr);
	EXPECT_EQ(leg->bones[0], skeleton->FindBone("Bip01 R Thigh"));
	EXPECT_EQ(leg->bones.back(), skeleton->FindBone("Bip01 R Foot"));

	const RetargetProfile::Chain* hips = profile->FindChain("hips");
	ASSERT_NE(hips, nullptr);
	EXPECT_EQ(hips->bones[0], skeleton->FindBone("Bip01 Pelvis"));
}

TEST_F(RetargetFixture, DecoySocketDoesNotStealChainsEndpoint) {
	const std::vector<const char*> names = {
		"mixamorig:Hips", "IK_LeftHand", "mixamorig:Spine", "mixamorig:Spine1",
		"mixamorig:LeftArm", "mixamorig:LeftForeArm", "mixamorig:LeftHand",
		"mixamorig:RightArm", "mixamorig:RightForeArm", "mixamorig:RightHand",
		"mixamorig:LeftUpLeg", "mixamorig:LeftLeg", "mixamorig:LeftFoot",
		"mixamorig:RightUpLeg", "mixamorig:RightLeg", "mixamorig:RightFoot"};
	const std::vector<i16> parents = {-1, 0, 0, 2, 3, 4, 5, 3, 7, 8, 0, 10, 11, 0, 13, 14};

	SkeletonAsset* skeleton = MakeNamedRig(names, parents);
	RetargetProfile* profile = Retarget::AutoMapHumanoid(Handle<SkeletonAsset>(skeleton));
	ASSERT_NE(profile, nullptr);
	_owned.push_back(profile);

	const RetargetProfile::Chain* arm = profile->FindChain("leftArm");
	ASSERT_NE(arm, nullptr);
	ASSERT_EQ(arm->bones.size(), 3u);
	EXPECT_EQ(arm->bones.back(), skeleton->FindBone("mixamorig:LeftHand"));

	const i16 decoy = static_cast<i16>(skeleton->FindBone("IK_LeftHand"));
	ASSERT_GE(decoy, 0);
	for (const RetargetProfile::Chain& chain : profile->chains) {
		EXPECT_EQ(std::find(chain.bones.begin(), chain.bones.end(), decoy), chain.bones.end()) << chain.role << "'";
	}
}

TEST_F(RetargetFixture, RigMissingLimbRefused) {
	std::vector<const char*> names = MixamoNames;
	std::vector<i16> parents = MixamoParents;
	names.erase(names.begin() + 10, names.begin() + 14);
	parents.erase(parents.begin() + 10, parents.begin() + 14);
	for (size_t index = 10; index < parents.size(); ++index) {
		if (parents[index] >= 10) {
			parents[index] = static_cast<i16>(parents[index] - 4);
		}
	}

	SkeletonAsset* skeleton = MakeNamedRig(names, parents);
	EXPECT_EQ(Retarget::AutoMapHumanoid(Handle<SkeletonAsset>(skeleton)), nullptr);

	SkeletonAsset* empty = MakeRig({});
	EXPECT_EQ(Retarget::AutoMapHumanoid(Handle<SkeletonAsset>(empty)), nullptr);

	const std::vector<const char*> stranded = {
		"Hips", "Spine", "Chest", "IK_LeftHand",
		"LeftArm", "LeftForeArm",
		"RightArm", "RightForeArm", "RightHand",
		"LeftUpLeg", "LeftLeg", "LeftFoot",
		"RightUpLeg", "RightLeg", "RightFoot"};
	const std::vector<i16> strandedParents = {-1, 0, 1, 0, 2, 4, 2, 6, 7, 0, 9, 10, 0, 12, 13};
	SkeletonAsset* orphan = MakeNamedRig(stranded, strandedParents);
	EXPECT_EQ(Retarget::AutoMapHumanoid(Handle<SkeletonAsset>(orphan)), nullptr);
}

TEST_F(RetargetFixture, ProfileRoundTripsThroughCook) {
	SkeletonAsset* skeleton = MakeRig({{"root", -1, Vec3(), Quat::Identity()},
									   {"mid", 0, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									   {"tip", 1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()}});
	RetargetProfile* profile = MakeProfile(skeleton, {{"hips", {0}}, {"spine", {1, 2}}});
	profile->skeletonGuid = Guid{0x2468, 0x1357};

	profile->chains[1].mirror = str("spine");

	profile->referencePose.assign(skeleton->restPose.begin(), skeleton->restPose.end());
	profile->referencePose[1].rotation = Quat::AngleAxis(33.0f, Vec3::Up());

	profile->goals.assign({str("spine")});

	Vector<u8> descriptorBytes{GetMemoryResource(MemTag::Asset)};
	Vector<u8> payloadBytes{GetMemoryResource(MemTag::Asset)};
	PakStringPool pool;
	CookWriter descriptorWriter(descriptorBytes, pool);
	CookWriter payloadWriter(payloadBytes, pool);
	ASSERT_TRUE(profile->Cook(descriptorWriter, payloadWriter));

	Vector<u8> stringBytes{GetMemoryResource(MemTag::Asset)};
	PakWriteStringBlob(stringBytes, pool.Texts());
	PakStringTable table;
	ASSERT_TRUE(PakReadStringBlob(stringBytes.data(), stringBytes.size(), table));

	RetargetProfile* loaded = New<RetargetProfile>(MemTag::Asset);
	_owned.push_back(loaded);
	CookReader descriptorReader(descriptorBytes.data(), descriptorBytes.size(), table);
	CookReader payloadReader(payloadBytes.data(), payloadBytes.size(), table);
	ASSERT_TRUE(loaded->LoadCooked(descriptorReader, payloadReader));

	EXPECT_EQ(loaded->skeletonGuid, profile->skeletonGuid);
	ASSERT_EQ(loaded->chains.size(), 2u);
	EXPECT_EQ(loaded->chains[0].role, "hips");
	EXPECT_EQ(loaded->chains[1].role, "spine");
	EXPECT_TRUE(loaded->chains[0].mirror.empty());
	EXPECT_EQ(loaded->chains[1].mirror, "spine");
	ASSERT_EQ(loaded->chains[1].bones.size(), 2u);
	EXPECT_EQ(loaded->chains[1].bones[0], 1);
	EXPECT_EQ(loaded->chains[1].bones[1], 2);
	ASSERT_EQ(loaded->goals.size(), 1u);
	EXPECT_EQ(loaded->goals[0], "spine");
	ASSERT_EQ(loaded->referencePose.size(), 3u);
	EXPECT_NEAR(Quat::Angle(loaded->referencePose[1].rotation, Quat::AngleAxis(33.0f, Vec3::Up())), 0.0f, 1e-3f);
	EXPECT_NEAR(loaded->referencePose[2].position.y, 1.0f, 1e-4f);
}

TEST_F(RetargetFixture, MirrorSwapsAndReflectsChains) {
	SkeletonAsset* rig = MakeMirrorRig();
	RetargetProfile* profile = MakeMirrorProfile(rig);

	std::vector<BoneTransform> pose = RestPose(*rig);
	pose[2].rotation = Quat::AngleAxis(40.0f, Vec3::Up());

	const std::vector<BoneTransform> mirrored = SolveMirrored(*profile, pose);
	ASSERT_EQ(mirrored.size(), 8u);

	EXPECT_NEAR(Quat::Angle(ModelRotation(*rig, mirrored, 5), Quat::AngleAxis(-40.0f, Vec3::Up())), 0.0f, 1e-2f);
	EXPECT_NEAR(Quat::Angle(ModelRotation(*rig, mirrored, 2), Quat::Identity()), 0.0f, 1e-2f);
}

TEST_F(RetargetFixture, LateralAxisMirrorsToItself) {
	SkeletonAsset* rig = MakeMirrorRig();
	RetargetProfile* profile = MakeMirrorProfile(rig);

	struct Row {
		const char* what;
		Vec3 axis;
		f32 expectedDegrees;
	};
	const Row ROWS[] = {
		{"a forward swing about the lateral axis", Vec3(1.0f, 0.0f, 0.0f), 35.0f},
		{"a turn about the vertical", Vec3(0.0f, 1.0f, 0.0f), -35.0f},
		{"a roll about the forward axis", Vec3(0.0f, 0.0f, 1.0f), -35.0f},
	};

	for (const Row& row : ROWS) {
		std::vector<BoneTransform> pose = RestPose(*rig);
		pose[2].rotation = Quat::AngleAxis(35.0f, row.axis);

		const std::vector<BoneTransform> mirrored = SolveMirrored(*profile, pose);
		ASSERT_EQ(mirrored.size(), 8u);
		EXPECT_NEAR(Quat::Angle(ModelRotation(*rig, mirrored, 5), Quat::AngleAxis(row.expectedDegrees, row.axis)),
					0.0f, 1e-2f)
			<< row.what << ' ' << row.expectedDegrees;
	}
}

TEST_F(RetargetFixture, MirrorKeepsAsymmetricStance) {
	SkeletonAsset* rig = MakeMirrorRig();
	RetargetProfile* profile = MakeMirrorProfile(rig);

	profile->referencePose.assign(rig->restPose.begin(), rig->restPose.end());
	profile->referencePose[2].rotation = Quat::AngleAxis(25.0f, Vec3::Up());
	profile->referencePose[3].rotation = Quat::AngleAxis(-12.0f, Vec3(0.0f, 0.0f, 1.0f));

	const std::span<const BoneTransform> stance = profile->GetReferencePose();
	const std::vector<BoneTransform> mirrored = SolveMirrored(*profile, stance);
	ASSERT_EQ(mirrored.size(), 8u);

	for (i16 bone = 0; bone < 8; ++bone) {
		EXPECT_NEAR(Quat::Angle(mirrored[static_cast<size_t>(bone)].rotation, stance[static_cast<size_t>(bone)].rotation),
					0.0f, 1e-3f);
	}
}

TEST_F(RetargetFixture, MirroringTwiceGivesTheOriginalPoseBack) {
	SkeletonAsset* rig = MakeMirrorRig();
	RetargetProfile* profile = MakeMirrorProfile(rig);

	std::vector<BoneTransform> pose = RestPose(*rig);
	pose[1].rotation = Quat::AngleAxis(10.0f, Vec3::Up());
	pose[2].rotation = Quat::AngleAxis(40.0f, Vec3::Up());
	pose[3].rotation = Quat::AngleAxis(20.0f, XAxis);
	pose[6].rotation = Quat::AngleAxis(15.0f, Vec3(0.0f, 0.0f, 1.0f));
	pose[0].position = Vec3(0.4f, 1.0f, 0.2f);

	const std::vector<BoneTransform> once = SolveMirrored(*profile, pose);
	ASSERT_EQ(once.size(), 8u);
	const std::vector<BoneTransform> twice = SolveMirrored(*profile, once);
	ASSERT_EQ(twice.size(), 8u);

	for (i16 bone = 0; bone < 8; ++bone) {
		EXPECT_NEAR(Quat::Angle(ModelRotation(*rig, twice, bone), ModelRotation(*rig, pose, bone)), 0.0f, 1e-2f) << "bone " << bone;
		EXPECT_NEAR((ModelPosition(*rig, twice, bone) - ModelPosition(*rig, pose, bone)).Length(), 0.0f, 1e-4f) << "bone " << bone;
	}
}

TEST_F(RetargetFixture, UnmirroredChainStillReverses) {
	SkeletonAsset* rig = MakeMirrorRig();
	RetargetProfile* profile = MakeMirrorProfile(rig);
	ASSERT_TRUE(profile->chains[1].mirror.empty());

	std::vector<BoneTransform> pose = RestPose(*rig);
	pose[1].rotation = Quat::AngleAxis(30.0f, Vec3::Up());

	const std::vector<BoneTransform> mirrored = SolveMirrored(*profile, pose);
	ASSERT_EQ(mirrored.size(), 8u);
	EXPECT_NEAR(Quat::Angle(ModelRotation(*rig, mirrored, 1), Quat::AngleAxis(-30.0f, Vec3::Up())), 0.0f, 1e-2f);
}

TEST_F(RetargetFixture, MissingMirrorRoleSkipped) {
	SkeletonAsset* rig = MakeMirrorRig();
	RetargetProfile* profile = MakeProfile(rig, {{"hips", {0}}, {"spine", {1}}, {"leftArm", {2, 3, 4}}});
	profile->chains[2].mirror = str("rightArm");

	std::vector<BoneTransform> pose = RestPose(*rig);
	pose[1].rotation = Quat::AngleAxis(30.0f, Vec3::Up());
	pose[2].rotation = Quat::AngleAxis(40.0f, Vec3::Up());

	const std::vector<BoneTransform> mirrored = SolveMirrored(*profile, pose);
	ASSERT_EQ(mirrored.size(), 8u);
	EXPECT_NEAR(Quat::Angle(mirrored[2].rotation, rig->restPose[2].rotation), 0.0f, 1e-3f);
	EXPECT_NEAR(Quat::Angle(ModelRotation(*rig, mirrored, 1), Quat::AngleAxis(-30.0f, Vec3::Up())), 0.0f, 1e-2f);
}

TEST_F(RetargetFixture, RootTravelMirrorsAboutRigMidline) {
	SkeletonAsset* rig = MakeRig({{"hips", -1, Vec3(5.0f, 1.0f, 0.0f), Quat::Identity()},
								  {"lLeg", 0, Vec3(0.1f, -0.5f, 0.0f), Quat::Identity()},
								  {"rLeg", 0, Vec3(-0.1f, -0.5f, 0.0f), Quat::Identity()}});
	RetargetProfile* profile = MakeProfile(rig, {{"hips", {0}}, {"leftLeg", {1}}, {"rightLeg", {2}}});
	PairChains(*profile, "leftLeg", "rightLeg");

	std::vector<BoneTransform> pose = RestPose(*rig);
	pose[0].position = Vec3(8.0f, 1.0f, 0.0f);

	const std::vector<BoneTransform> mirrored = SolveMirrored(*profile, pose);
	ASSERT_EQ(mirrored.size(), 3u);
	EXPECT_NEAR(mirrored[0].position.x, 2.0f, 1e-4f);
	EXPECT_NEAR(mirrored[0].position.y, 1.0f, 1e-4f);
	EXPECT_NEAR(mirrored[0].position.z, 0.0f, 1e-4f);
}

TEST_F(RetargetFixture, MirrorAndBakeCommute) {
	SkeletonAsset* source = MakeMirrorRig();
	SkeletonAsset* target = MakeRig({{"HIPS", -1, Vec3(0.0f, 1.4f, 0.0f), Quat::AngleAxis(25.0f, XAxis)},
									 {"SPINE", 0, Vec3(0.0f, 0.5f, 0.0f), Quat::Identity()},
									 {"L_SHOULDER", 1, Vec3(0.3f, 0.3f, 0.0f), Quat::AngleAxis(15.0f, Vec3::Up())},
									 {"L_ELBOW", 2, Vec3(0.5f, 0.0f, 0.0f), Quat::Identity()},
									 {"L_HAND", 3, Vec3(0.5f, 0.0f, 0.0f), Quat::Identity()},
									 {"R_SHOULDER", 1, Vec3(-0.3f, 0.3f, 0.0f), Quat::AngleAxis(-40.0f, Vec3::Up())},
									 {"R_ELBOW", 5, Vec3(-0.5f, 0.0f, 0.0f), Quat::Identity()},
									 {"R_HAND", 6, Vec3(-0.5f, 0.0f, 0.0f), Quat::Identity()}});

	RetargetProfile* sourceProfile = MakeMirrorProfile(source);
	RetargetProfile* targetProfile = MakeMirrorProfile(target);

	AnimationClip* clip = MakeClip();
	clip->length = 2.0f;
	AnimationClip::Track turn;
	turn.kind = AnimationTrackKind::BoneRotation;
	turn.interp = AnimationInterpolation::Linear;
	turn.name = "lElbow";
	for (i32 key = 0; key < 3; ++key) {
		const Quat value = Quat::AngleAxis(static_cast<f32>(key) * 25.0f, Vec3::Up());
		turn.times.push_back(static_cast<f32>(key));
		turn.values.push_back(Vec4(value.x, value.y, value.z, value.w));
	}
	clip->tracks.push_back(std::move(turn));
	AddRotation(*clip, "spine", Quat::AngleAxis(18.0f, Vec3(0.0f, 0.0f, 1.0f)));
	AddRotation(*clip, "rShoulder", Quat::AngleAxis(-22.0f, XAxis));

	AnimationClip* mirroredFirst = Retarget::MirrorClip(*clip, *sourceProfile);
	ASSERT_NE(mirroredFirst, nullptr);
	_owned.push_back(mirroredFirst);
	AnimationClip* thenBaked = Retarget::Bake(*mirroredFirst, *sourceProfile, *targetProfile);
	ASSERT_NE(thenBaked, nullptr);
	_owned.push_back(thenBaked);

	AnimationClip* bakedFirst = Retarget::Bake(*clip, *sourceProfile, *targetProfile);
	ASSERT_NE(bakedFirst, nullptr);
	_owned.push_back(bakedFirst);
	AnimationClip* thenMirrored = Retarget::MirrorClip(*bakedFirst, *targetProfile);
	ASSERT_NE(thenMirrored, nullptr);
	_owned.push_back(thenMirrored);

	for (f32 at : {0.0f, 0.5f, 1.0f, 2.0f}) {
		const std::vector<BoneTransform> a = PoseFromClip(*thenBaked, *target, at);
		const std::vector<BoneTransform> b = PoseFromClip(*thenMirrored, *target, at);
		for (i16 bone = 0; bone < 8; ++bone) {
			EXPECT_NEAR(Quat::Angle(ModelRotation(*target, a, bone), ModelRotation(*target, b, bone)), 0.0f, 1e-2f) << bone << " at t=" << at;
		}
	}
}

TEST_F(RetargetFixture, BakedClipMatchesSolver) {
	SkeletonAsset* source = MakeRig({{"hips", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									 {"spine", 0, Vec3(0.0f, 0.5f, 0.0f), Quat::Identity()},
									 {"chest", 1, Vec3(0.0f, 0.4f, 0.0f), Quat::Identity()}});
	SkeletonAsset* target = MakeRig({{"HIPS", -1, Vec3(0.0f, 1.4f, 0.0f), Quat::AngleAxis(25.0f, XAxis)},
									 {"SPINE", 0, Vec3(0.0f, 0.8f, 0.0f), Quat::Identity()},
									 {"CHEST", 1, Vec3(0.0f, 0.6f, 0.0f), Quat::Identity()}});

	RetargetProfile* sourceProfile = MakeProfile(source, {{"hips", {0}}, {"spine", {1, 2}}});
	RetargetProfile* targetProfile = MakeProfile(target, {{"hips", {0}}, {"spine", {1, 2}}});

	sourceProfile->referencePose.assign(source->restPose.begin(), source->restPose.end());
	sourceProfile->referencePose[2].rotation = Quat::AngleAxis(20.0f, Vec3(0.0f, 0.0f, 1.0f));

	AnimationClip* clip = MakeClip();
	clip->length = 2.0f;
	AnimationClip::Track turn;
	turn.kind = AnimationTrackKind::BoneRotation;
	turn.interp = AnimationInterpolation::Linear;
	turn.name = "spine";
	for (i32 key = 0; key < 3; ++key) {
		const Quat value = Quat::AngleAxis(static_cast<f32>(key) * 30.0f, Vec3::Up());
		turn.times.push_back(static_cast<f32>(key));
		turn.values.push_back(Vec4(value.x, value.y, value.z, value.w));
	}
	clip->tracks.push_back(std::move(turn));
	AddPosition(*clip, "hips", Vec3(0.7f, 1.0f, 0.0f));

	AnimationClip* baked = Retarget::Bake(*clip, *sourceProfile, *targetProfile);
	ASSERT_NE(baked, nullptr);
	_owned.push_back(baked);
	EXPECT_FLOAT_EQ(baked->length, clip->length);
	EXPECT_EQ(baked->skeletonGuid, targetProfile->skeletonGuid);

	for (f32 at : {0.0f, 1.0f, 2.0f}) {
		const std::span<const BoneTransform> reference = sourceProfile->GetReferencePose();
		std::vector<BoneTransform> sourcePose(reference.begin(), reference.end());
		for (const AnimationClip::Track& track : clip->tracks) {
			const i32 bone = source->FindBone(track.name);
			ASSERT_GE(bone, 0);
			const Vec4 value = AnimationImpl::SampleTrack(track, at);
			if (track.kind == AnimationTrackKind::BoneRotation) {
				sourcePose[static_cast<size_t>(bone)].rotation = Quat(value.x, value.y, value.z, value.w);
			} else {
				sourcePose[static_cast<size_t>(bone)].position = Vec3(value.x, value.y, value.z);
			}
		}
		const std::vector<BoneTransform> expected = Solve(*sourceProfile, *targetProfile, sourcePose);

		std::vector<BoneTransform> fromBake = RestPose(*target);
		for (const AnimationClip::Track& track : baked->tracks) {
			const i32 bone = target->FindBone(track.name);
			ASSERT_GE(bone, 0);
			const Vec4 value = AnimationImpl::SampleTrack(track, at);
			BoneTransform& into = fromBake[static_cast<size_t>(bone)];
			switch (track.kind) {
			case AnimationTrackKind::BoneRotation:
				into.rotation = Quat(value.x, value.y, value.z, value.w);
				break;
			case AnimationTrackKind::BonePosition:
				into.position = Vec3(value.x, value.y, value.z);
				break;
			default:
				into.scale = Vec3(value.x, value.y, value.z);
				break;
			}
		}

		for (i16 bone = 0; bone < 3; ++bone) {
			EXPECT_NEAR(Quat::Angle(ModelRotation(*target, fromBake, bone), ModelRotation(*target, expected, bone)),
						0.0f, 1e-2f);
			EXPECT_NEAR((ModelPosition(*target, fromBake, bone) - ModelPosition(*target, expected, bone)).Length(),
						0.0f, 1e-4f);
		}
	}
}

TEST_F(RetargetFixture, BakeSkipsUnmovedBones) {
	SkeletonAsset* source = MakeRig({{"hips", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									 {"spine", 0, Vec3(0.0f, 0.5f, 0.0f), Quat::Identity()},
									 {"tailBone", 0, Vec3(0.0f, -0.2f, 0.0f), Quat::Identity()}});
	SkeletonAsset* target = MakeRig({{"HIPS", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									 {"SPINE", 0, Vec3(0.0f, 0.5f, 0.0f), Quat::Identity()},
									 {"TAIL", 0, Vec3(0.0f, -0.2f, 0.0f), Quat::Identity()}});

	RetargetProfile* sourceProfile = MakeProfile(source, {{"hips", {0}}, {"spine", {1}}, {"tail", {2}}});
	RetargetProfile* targetProfile = MakeProfile(target, {{"hips", {0}}, {"spine", {1}}, {"tail", {2}}});

	AnimationClip* clip = MakeClip();
	AddRotation(*clip, "spine", Quat::AngleAxis(35.0f, Vec3::Up()));

	AnimationClip::Track footstep;
	footstep.kind = AnimationTrackKind::Event;
	footstep.name = "Footstep";
	footstep.times.push_back(0.4f);
	footstep.values.push_back(Vec4(1.0f, 2.0f, 3.0f, 4.0f));
	clip->tracks.push_back(std::move(footstep));

	AnimationClip::Track fade;
	fade.kind = AnimationTrackKind::Property;
	fade.name = "_localPosition";
	fade.objectPath = "Effects";
	fade.component = 1;
	fade.times.push_back(0.0f);
	fade.values.push_back(Vec4(5.0f, 0.0f, 0.0f, 0.0f));
	clip->tracks.push_back(std::move(fade));

	AnimationClip* baked = Retarget::Bake(*clip, *sourceProfile, *targetProfile);
	ASSERT_NE(baked, nullptr);
	_owned.push_back(baked);

	u32 spineTracks = 0;
	u32 tailTracks = 0;
	u32 events = 0;
	u32 properties = 0;
	for (const AnimationClip::Track& track : baked->tracks) {
		if (track.kind == AnimationTrackKind::Event) {
			++events;
			EXPECT_EQ(track.name, "Footstep");
			ASSERT_EQ(track.times.size(), 1u);
			EXPECT_FLOAT_EQ(track.times[0], 0.4f);
			ASSERT_EQ(track.values.size(), 1u);
			EXPECT_FLOAT_EQ(track.values[0].w, 4.0f);
		} else if (track.kind == AnimationTrackKind::Property) {
			++properties;
			EXPECT_EQ(track.objectPath, "Effects");
			EXPECT_EQ(track.component, 1);
		} else if (track.name == "SPINE") {
			++spineTracks;
		} else if (track.name == "TAIL") {
			++tailTracks;
		}
	}
	EXPECT_EQ(spineTracks, 1u);
	EXPECT_EQ(tailTracks, 0u);
	EXPECT_EQ(events, 1u);
	EXPECT_EQ(properties, 1u);
}

TEST_F(RetargetFixture, BakeWritesBonesPlaybackLeavesAtRest) {
	SkeletonAsset* source = MakeRig({{"hips", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									 {"spine", 0, Vec3(0.0f, 0.5f, 0.0f), Quat::Identity()},
									 {"arm", 0, Vec3(0.3f, 0.4f, 0.0f), Quat::Identity()}});
	SkeletonAsset* target = MakeRig({{"HIPS", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									 {"SPINE", 0, Vec3(0.0f, 0.5f, 0.0f), Quat::Identity()},
									 {"ARM", 0, Vec3(0.3f, 0.4f, 0.0f), Quat::Identity()}});

	RetargetProfile* sourceProfile = MakeProfile(source, {{"hips", {0}}, {"spine", {1}}, {"arm", {2}}});
	RetargetProfile* targetProfile = MakeProfile(target, {{"hips", {0}}, {"spine", {1}}, {"arm", {2}}});

	constexpr f32 ARM_REFERENCE_DEGREES = 40.0f;
	targetProfile->referencePose.assign(target->restPose.begin(), target->restPose.end());
	targetProfile->referencePose[2].rotation = Quat::AngleAxis(ARM_REFERENCE_DEGREES, Vec3::Forward());

	AnimationClip* clip = MakeClip();
	AddRotation(*clip, "spine", Quat::AngleAxis(35.0f, Vec3::Up()));

	AnimationClip* baked = Retarget::Bake(*clip, *sourceProfile, *targetProfile);
	ASSERT_NE(baked, nullptr);
	_owned.push_back(baked);

	const AnimationClip::Track* armRotation = nullptr;
	for (const AnimationClip::Track& track : baked->tracks) {
		if (track.name == "ARM" && track.kind == AnimationTrackKind::BoneRotation) {
			armRotation = &track;
		}
	}
	ASSERT_NE(armRotation, nullptr);
	ASSERT_FALSE(armRotation->values.empty());
	const Vec4 value = armRotation->values[0];
	EXPECT_NEAR(Quat::Angle(Quat(value.x, value.y, value.z, value.w),
							Quat::AngleAxis(ARM_REFERENCE_DEGREES, Vec3::Forward())),
				0.0f, 1e-3f);

	u32 hipsTracks = 0;
	for (const AnimationClip::Track& track : baked->tracks) {
		hipsTracks += track.name == "HIPS" ? 1u : 0u;
	}
	EXPECT_EQ(hipsTracks, 0u);
}

namespace {

	std::filesystem::path RepoDir(const char* relative) {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
			std::filesystem::path candidate = probe / relative;
			if (std::filesystem::exists(candidate, ec)) {
				return candidate;
			}
			std::filesystem::path parent = probe.parent_path();
			if (parent == probe) {
				break;
			}
			probe = parent;
		}
		return {};
	}

	struct ParsedTake {
		SkeletonAsset* skeleton = nullptr;
		AnimationClip* clip = nullptr;
	};

	ParsedTake ParseTake(const char* fixture, const char* takeName, std::vector<Asset*>& owned) {
		ParsedTake out;
		std::filesystem::path fixtures = RepoDir("Source/Tests/Fixtures/Graphics");
		if (fixtures.empty()) {
			return out;
		}
		ModelParseOptions options;
		options.tracks = true;
		ModelParseResult parsed = ParseModel(Path((fixtures / fixture).generic_string()), options);
		if (!parsed.success) {
			return out;
		}
		const ModelRig rig = BuildModelRig(parsed);
		if (rig.boneNodes.empty()) {
			return out;
		}

		SkeletonAsset* skeleton = New<SkeletonAsset>(MemTag::Asset);
		for (i32 node : rig.boneNodes) {
			const ModelNode& source = parsed.nodes[static_cast<size_t>(node)];
			skeleton->boneNames.push_back(source.name);
			skeleton->parents.push_back(static_cast<i16>(rig.BoneOfNode(source.parent)));
			BoneTransform rest;
			rest.position = source.position;
			rest.rotation = source.rotation;
			rest.scale = source.scale;
			skeleton->restPose.push_back(rest);
		}
		owned.push_back(skeleton);
		out.skeleton = skeleton;

		for (const ModelTakeRange& take : parsed.takes) {
			if (take.name != takeName) {
				continue;
			}
			AnimationClip* clip = New<AnimationClip>(MemTag::Asset);
			clip->virtualPath = str("project://") + fixture;
			clip->length = take.end > take.begin ? take.end - take.begin : 0.0f;
			for (i32 trackIndex : take.tracks) {
				const ModelTrack& source = parsed.tracks[static_cast<size_t>(trackIndex)];
				AnimationClip::Track track;
				track.kind = source.channel == ModelChannel::Rotation ? AnimationTrackKind::BoneRotation
							 : source.channel == ModelChannel::Scale  ? AnimationTrackKind::BoneScale
																	  : AnimationTrackKind::BonePosition;
				track.interp = source.interp == ModelInterpolation::Step		  ? AnimationInterpolation::Step
							   : source.interp == ModelInterpolation::CubicSpline ? AnimationInterpolation::CubicSpline
																				  : AnimationInterpolation::Linear;
				track.name = parsed.nodes[static_cast<size_t>(source.node)].name;
				for (f32 time : source.times) {
					track.times.push_back(time - take.begin);
				}
				track.values.assign(source.values.begin(), source.values.end());
				clip->tracks.push_back(std::move(track));
			}
			owned.push_back(clip);
			out.clip = clip;
			break;
		}
		return out;
	}

} // anonymous namespace

TEST_F(RetargetFixture, FoxsWalkRetargetsOntoDifferentlyProportionedQuadruped) {
	const ParsedTake fox = ParseTake("Fox.glb", "Walk", _owned);
	ASSERT_NE(fox.skeleton, nullptr);
	ASSERT_NE(fox.clip, nullptr);
	ASSERT_GT(fox.clip->tracks.size(), 0u);

	SkeletonAsset* big = New<SkeletonAsset>(MemTag::Asset);
	for (size_t bone = 0; bone < fox.skeleton->parents.size(); ++bone) {
		big->boneNames.push_back(str("big_") + fox.skeleton->boneNames[bone]);
		big->parents.push_back(fox.skeleton->parents[bone]);
		BoneTransform rest = fox.skeleton->restPose[bone];
		rest.position = rest.position * 2.0f;
		big->restPose.push_back(rest);
	}
	_owned.push_back(big);

	auto chainOf = [&](std::initializer_list<const char*> names) {
		std::vector<i16> bones;
		for (const char* name : names) {
			const i32 bone = fox.skeleton->FindBone(name);
			EXPECT_GE(bone, 0) << name << "'";
			bones.push_back(static_cast<i16>(bone));
		}
		return bones;
	};
	const std::vector<std::pair<const char*, std::vector<i16>>> layout = {
		{"spine", chainOf({"b_Spine01_02", "b_Spine02_03"})},
		{"neck", chainOf({"b_Neck_04", "b_Head_05"})},
		{"tail", chainOf({"b_Tail01_012", "b_Tail02_013", "b_Tail03_014"})},
		{"legFL", chainOf({"b_LeftUpperArm_09", "b_LeftForeArm_010", "b_LeftHand_011"})},
		{"legFR", chainOf({"b_RightUpperArm_06", "b_RightForeArm_07", "b_RightHand_08"})},
		{"legBL", chainOf({"b_LeftLeg01_015", "b_LeftLeg02_016", "b_LeftFoot01_017", "b_LeftFoot02_018"})},
		{"legBR", chainOf({"b_RightLeg01_019", "b_RightLeg02_020", "b_RightFoot01_021", "b_RightFoot02_022"})},
		{"hips", chainOf({"b_Hip_01"})},
	};

	auto build = [&](SkeletonAsset* skeleton) {
		RetargetProfile* profile = New<RetargetProfile>(MemTag::Asset);
		profile->skeleton = Handle<SkeletonAsset>(skeleton);
		for (const auto& entry : layout) {
			RetargetProfile::Chain chain;
			chain.role = str(entry.first);
			chain.bones.assign(entry.second.begin(), entry.second.end());
			profile->chains.push_back(std::move(chain));
		}
		_owned.push_back(profile);
		return profile;
	};
	RetargetProfile* sourceProfile = build(fox.skeleton);
	RetargetProfile* targetProfile = build(big);

	AnimationClip* baked = Retarget::Bake(*fox.clip, *sourceProfile, *targetProfile);
	ASSERT_NE(baked, nullptr);
	EXPECT_GT(baked->tracks.size(), 0u);
	EXPECT_FLOAT_EQ(baked->length, fox.clip->length);

	for (const AnimationClip::Track& track : baked->tracks) {
		EXPECT_GE(big->FindBone(track.name), 0) << track.name;
	}

	const f32 at = fox.clip->length / 3.0f;
	std::vector<BoneTransform> pose = RestPose(*big);
	for (const AnimationClip::Track& track : baked->tracks) {
		const i32 bone = big->FindBone(track.name);
		if (bone < 0) {
			continue;
		}
		const Vec4 value = AnimationImpl::SampleTrack(track, at);
		BoneTransform& into = pose[static_cast<size_t>(bone)];
		if (track.kind == AnimationTrackKind::BoneRotation) {
			into.rotation = Quat(value.x, value.y, value.z, value.w);
		} else if (track.kind == AnimationTrackKind::BonePosition) {
			into.position = Vec3(value.x, value.y, value.z);
		}
	}
	const i16 tailTip = static_cast<i16>(big->FindBone("big_b_Tail03_014"));
	ASSERT_GE(tailTip, 0);
	const std::vector<BoneTransform> rest = RestPose(*big);
	EXPECT_GT(Quat::Angle(ModelRotation(*big, pose, tailTip), ModelRotation(*big, rest, tailTip)), 1.0f);

	std::vector<BoneTransform> smallPose = RestPose(*fox.skeleton);
	for (const AnimationClip::Track& track : fox.clip->tracks) {
		const i32 bone = fox.skeleton->FindBone(track.name);
		if (bone < 0) {
			continue;
		}
		const Vec4 value = AnimationImpl::SampleTrack(track, at);
		BoneTransform& into = smallPose[static_cast<size_t>(bone)];
		if (track.kind == AnimationTrackKind::BoneRotation) {
			into.rotation = Quat(value.x, value.y, value.z, value.w);
		} else if (track.kind == AnimationTrackKind::BonePosition) {
			into.position = Vec3(value.x, value.y, value.z);
		}
	}
	const i16 smallTip = static_cast<i16>(fox.skeleton->FindBone("b_Tail03_014"));
	ASSERT_GE(smallTip, 0);
	const f32 smallReach = ModelPosition(*fox.skeleton, smallPose, smallTip).Length();
	const f32 bigReach = ModelPosition(*big, pose, tailTip).Length();
	ASSERT_GT(smallReach, 0.1f);
	EXPECT_NEAR(bigReach / smallReach, 2.0f, 0.05f);
}

TEST_F(RetargetFixture, MixamoNamedClipDrivesDifferentlyProportionedRig) {
	SkeletonAsset* mixamo = MakeNamedRig(MixamoNames, MixamoParents);
	auto setRest = [&](const char* name, const Vec3& position) {
		const i32 bone = mixamo->FindBone(name);
		ASSERT_GE(bone, 0);
		mixamo->restPose[static_cast<size_t>(bone)].position = position;
	};
	setRest("mixamorig:Hips", Vec3(0.0f, 1.0f, 0.0f));
	setRest("mixamorig:LeftUpLeg", Vec3(0.1f, -0.05f, 0.0f));
	setRest("mixamorig:LeftLeg", Vec3(0.0f, -0.45f, 0.0f));
	setRest("mixamorig:LeftArm", Vec3(0.15f, 0.0f, 0.0f));
	setRest("mixamorig:LeftForeArm", Vec3(0.28f, 0.0f, 0.0f));

	const std::vector<const char*> otherNames = {
		"hips", "spine", "chest",
		"upper_arm.L", "forearm.L", "hand.L",
		"upper_arm.R", "forearm.R", "hand.R",
		"thigh.L", "shin.L", "foot.L",
		"thigh.R", "shin.R", "foot.R"};
	const std::vector<i16> otherParents = {-1, 0, 1, 2, 3, 4, 2, 6, 7, 0, 9, 10, 0, 12, 13};
	SkeletonAsset* other = MakeNamedRig(otherNames, otherParents);
	for (BoneTransform& rest : other->restPose) {
		rest.position = rest.position * 0.5f;
	}
	other->restPose[other->FindBone("hips")].position = Vec3(0.0f, 0.5f, 0.0f);
	other->restPose[other->FindBone("thigh.L")].position = Vec3(0.05f, -0.03f, 0.0f);
	other->restPose[other->FindBone("shin.L")].position = Vec3(0.0f, -0.22f, 0.0f);

	RetargetProfile* sourceProfile = Retarget::AutoMapHumanoid(Handle<SkeletonAsset>(mixamo));
	RetargetProfile* targetProfile = Retarget::AutoMapHumanoid(Handle<SkeletonAsset>(other));
	ASSERT_NE(sourceProfile, nullptr);
	ASSERT_NE(targetProfile, nullptr);
	_owned.push_back(sourceProfile);
	_owned.push_back(targetProfile);

	AnimationClip* clip = MakeClip();
	AddPosition(*clip, "mixamorig:Hips", Vec3(0.0f, 1.0f, 0.9f));
	AddRotation(*clip, "mixamorig:LeftArm", Quat::AngleAxis(45.0f, XAxis));

	AnimationClip* baked = Retarget::Bake(*clip, *sourceProfile, *targetProfile);
	ASSERT_NE(baked, nullptr);
	_owned.push_back(baked);

	std::vector<BoneTransform> pose = RestPose(*other);
	for (const AnimationClip::Track& track : baked->tracks) {
		const i32 bone = other->FindBone(track.name);
		ASSERT_GE(bone, 0);
		const Vec4 value = AnimationImpl::SampleTrack(track, 0.0f);
		BoneTransform& into = pose[static_cast<size_t>(bone)];
		if (track.kind == AnimationTrackKind::BoneRotation) {
			into.rotation = Quat(value.x, value.y, value.z, value.w);
		} else if (track.kind == AnimationTrackKind::BonePosition) {
			into.position = Vec3(value.x, value.y, value.z);
		}
	}

	const i16 upperArm = static_cast<i16>(other->FindBone("upper_arm.L"));
	ASSERT_GE(upperArm, 0);
	const std::vector<BoneTransform> rest = RestPose(*other);
	const Quat armDelta = (ModelRotation(*other, pose, upperArm) *
						   ModelRotation(*other, rest, upperArm).Conjugated())
							  .Normalized();
	EXPECT_NEAR(Quat::Angle(armDelta, Quat::AngleAxis(45.0f, XAxis)), 0.0f, 0.05f);

	const i16 hips = static_cast<i16>(other->FindBone("hips"));
	ASSERT_GE(hips, 0);
	const f32 travelled = pose[static_cast<size_t>(hips)].position.z;
	EXPECT_GT(travelled, 0.05f);
	EXPECT_LT(travelled, 0.9f);
}

namespace {

	class RetargetLiveFixture : public RetargetFixture {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		Handle<Scene> _scene;
		char _token = 0;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_retarget_live_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			SLT_CALL_LINKED_PACKAGE(Animation, &_token);

			_scene = SceneSystem::CreateScene("retarget");
			ASSERT_TRUE(_scene.IsValid());
		}

		void TearDown() override {
			SceneSystem::Terminate();
			RetargetFixture::TearDown();
			FrameStages::UnregisterOwnedBy(&_token);
			ModuleSystem::Terminate();
			Settings::Terminate();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		Handle<SceneObject> MakeRigObject(SkeletonAsset* skeleton, const char* name) {
			Handle<SceneObject> object = SceneSystem::CreateObject(name, _scene);
			Handle<Skeleton> component = object->AddComponent<Skeleton>();
			component->SetSkeleton(Handle<SkeletonAsset>(skeleton));
			return object;
		}

		void RunUpdate(f32 delta) {
			FrameTime clockState;
			clockState.fixedDeltaTime = 1.0f / 50.0f;
			clockState.maximumDeltaTime = 1000.0f;
			Time::FrameScope clock{clockState};
			Time::BeginFrame(delta);

			FrameStageArgs args;
			FrameStages::Run(FrameStage::Update, args);
		}
	};

} // anonymous namespace

TEST_F(RetargetLiveFixture, BakedAndLiveRetargetMatch) {
	SkeletonAsset* source = MakeRig({{"root", -1, Vec3(), Quat::Identity()},
									 {"hips", 0, Vec3(0.0f, SOURCE_HIP, 0.0f), Quat::Identity()},
									 {"spine", 1, Vec3(0.0f, 0.25f, 0.0f), Quat::Identity()},
									 {"chest", 2, Vec3(0.0f, 0.25f, 0.0f), Quat::Identity()},
									 {"thigh", 1, Vec3(0.0f, -SOURCE_THIGH_DROP, 0.0f), Quat::Identity()},
									 {"shin", 4, Vec3(0.0f, -SOURCE_LEG * 0.5f, 0.0f), Quat::Identity()},
									 {"foot", 5, Vec3(0.0f, -SOURCE_LEG * 0.5f, 0.0f), Quat::Identity()},
									 {"tail", 1, Vec3(0.0f, -0.1f, -0.3f), Quat::Identity()}});
	SkeletonAsset* target = MakeRig({{"ROOT", -1, Vec3(), Quat::AngleAxis(15.0f, Vec3::Up())},
									 {"HIPS", 0, Vec3(0.0f, TARGET_HIP, 0.0f), Quat::Identity()},
									 {"SPINE1", 1, Vec3(0.0f, 0.13f, 0.0f), Quat::AngleAxis(7.0f, Vec3::Right())},
									 {"SPINE2", 2, Vec3(0.0f, 0.13f, 0.0f), Quat::Identity()},
									 {"CHEST", 3, Vec3(0.0f, 0.14f, 0.0f), Quat::Identity()},
									 {"THIGH", 1, Vec3(0.0f, -TARGET_THIGH_DROP, 0.0f), Quat::Identity()},
									 {"SHIN", 5, Vec3(0.0f, -TARGET_LEG * 0.5f, 0.0f), Quat::Identity()},
									 {"FOOT", 6, Vec3(0.0f, -TARGET_LEG * 0.5f, 0.0f), Quat::Identity()}});

	RetargetProfile* sourceProfile =
		MakeProfile(source, {{"hips", {1}}, {"spine", {2, 3}}, {"leg", {4, 5, 6}}, {"tail", {7}}});
	RetargetProfile* targetProfile =
		MakeProfile(target, {{"hips", {1}}, {"spine", {2, 3, 4}}, {"leg", {5, 6, 7}}});
	targetProfile->goals.assign({str("leg")});

	targetProfile->referencePose.assign(target->restPose.begin(), target->restPose.end());
	targetProfile->referencePose[2].rotation = Quat::AngleAxis(12.0f, Vec3::Forward());
	targetProfile->referencePose[5].rotation = Quat::AngleAxis(6.0f, Vec3::Forward());

	sourceProfile->referencePose.assign(source->restPose.begin(), source->restPose.end());
	sourceProfile->referencePose[3].rotation = Quat::AngleAxis(17.0f, Vec3::Right());
	sourceProfile->referencePose[2].position = Vec3(0.0f, 0.30f, 0.0f);

	AnimationClip* clip = MakeClip();
	clip->length = 2.0f;
	AnimationClip::Track hips;
	hips.kind = AnimationTrackKind::BonePosition;
	hips.interp = AnimationInterpolation::Linear;
	hips.name = "hips";
	AnimationClip::Track thigh;
	thigh.kind = AnimationTrackKind::BoneRotation;
	thigh.interp = AnimationInterpolation::Linear;
	thigh.name = "thigh";
	AnimationClip::Track shin;
	shin.kind = AnimationTrackKind::BoneRotation;
	shin.interp = AnimationInterpolation::Linear;
	shin.name = "shin";
	AnimationClip::Track spine;
	spine.kind = AnimationTrackKind::BoneRotation;
	spine.interp = AnimationInterpolation::Linear;
	spine.name = "spine";
	for (i32 key = 0; key < 3; ++key) {
		const f32 at = static_cast<f32>(key);
		const f32 degrees = 12.0f + static_cast<f32>(key) * 16.0f;
		const Quat thighValue = Quat::AngleAxis(degrees, Vec3::Forward());
		const Quat shinValue = Quat::AngleAxis(-2.0f * degrees, Vec3::Forward());
		const Quat spineValue = Quat::AngleAxis(static_cast<f32>(key) * 14.0f, Vec3::Up());
		const f32 height = PlantedHipHeight(degrees);

		hips.times.push_back(at);
		hips.values.push_back(Vec4(0.0f, height, 0.0f, 0.0f));
		thigh.times.push_back(at);
		thigh.values.push_back(Vec4(thighValue.x, thighValue.y, thighValue.z, thighValue.w));
		shin.times.push_back(at);
		shin.values.push_back(Vec4(shinValue.x, shinValue.y, shinValue.z, shinValue.w));
		spine.times.push_back(at);
		spine.values.push_back(Vec4(spineValue.x, spineValue.y, spineValue.z, spineValue.w));
	}
	clip->tracks.push_back(std::move(hips));
	clip->tracks.push_back(std::move(thigh));
	clip->tracks.push_back(std::move(shin));
	clip->tracks.push_back(std::move(spine));

	AnimationClip* baked = Retarget::Bake(*clip, *sourceProfile, *targetProfile);
	ASSERT_NE(baked, nullptr);
	_owned.push_back(baked);

	Handle<SceneObject> bakedRig = MakeRigObject(target, "baked");
	Handle<Animator> bakedAnimator = bakedRig->AddComponent<Animator>();
	bakedAnimator->SetClip(Handle<AnimationClip>(baked));
	bakedAnimator->SetApplyRootMotion(false);

	Handle<SceneObject> liveRig = MakeRigObject(target, "live");
	Handle<Animator> liveAnimator = liveRig->AddComponent<Animator>();
	liveAnimator->SetClip(Handle<AnimationClip>(clip));
	liveAnimator->SetApplyRootMotion(false);
	liveAnimator->SetRetarget(Handle<RetargetProfile>(sourceProfile), Handle<RetargetProfile>(targetProfile));

	RunUpdate(0.0f);
	ASSERT_TRUE(liveAnimator->IsRetargeting());
	ASSERT_GT(liveAnimator->GetBoundTrackCount(), 0u);

	Handle<Skeleton> bakedPose = bakedRig->GetComponent<Skeleton>();
	Handle<Skeleton> livePose = liveRig->GetComponent<Skeleton>();

	f32 worstRotation = 0.0f;
	f32 worstPosition = 0.0f;
	auto compare = [&](f32 advance) {
		RunUpdate(advance);
		ASSERT_EQ(bakedPose->GetLocalPose().size(), target->parents.size());
		ASSERT_EQ(livePose->GetLocalPose().size(), target->parents.size());
		for (i16 bone = 0; bone < static_cast<i16>(target->parents.size()); ++bone) {
			worstRotation = Math::Max(worstRotation,
									  Quat::Angle(ModelRotation(*target, bakedPose->GetLocalPose(), bone),
												  ModelRotation(*target, livePose->GetLocalPose(), bone)));
			worstPosition = Math::Max(worstPosition,
									  (ModelPosition(*target, bakedPose->GetLocalPose(), bone) -
									   ModelPosition(*target, livePose->GetLocalPose(), bone))
										  .Length());
		}
	};

	for (f32 advance : {0.0f, 1.0f, 1.0f}) {
		compare(advance);
	}
	EXPECT_LT(worstRotation, 1e-2f);
	EXPECT_LT(worstPosition, 1e-4f);

	worstRotation = 0.0f;
	worstPosition = 0.0f;
	bakedAnimator->SetTime(0.5f);
	liveAnimator->SetTime(0.5f);
	compare(0.0f);
	bakedAnimator->SetTime(1.5f);
	liveAnimator->SetTime(1.5f);
	compare(0.0f);
	EXPECT_LT(worstPosition, 2e-3f);
	EXPECT_LT(worstRotation, 0.5f);

	EXPECT_GT(Quat::Angle(ModelRotation(*target, livePose->GetLocalPose(), 4), target->restPose[4].rotation), 5.0f);
	EXPECT_GT(Math::Abs(livePose->GetLocalPose()[1].position.y - target->restPose[1].position.y), 0.05f);
}

TEST_F(RetargetLiveFixture, FootStaysPlantedThroughLivePath) {
	SkeletonAsset* source = MakeRig({{"root", -1, Vec3(), Quat::Identity()},
									 {"hips", 0, Vec3(0.0f, SOURCE_HIP, 0.0f), Quat::Identity()},
									 {"spine", 1, Vec3(0.0f, RIG_HEIGHT - SOURCE_HIP, 0.0f), Quat::Identity()},
									 {"thigh", 1, Vec3(0.0f, -SOURCE_THIGH_DROP, 0.0f), Quat::Identity()},
									 {"shin", 3, Vec3(0.0f, -SOURCE_LEG * 0.5f, 0.0f), Quat::Identity()},
									 {"foot", 4, Vec3(0.0f, -SOURCE_LEG * 0.5f, 0.0f), Quat::Identity()}});
	SkeletonAsset* target = MakeRig({{"ROOT", -1, Vec3(), Quat::Identity()},
									 {"HIPS", 0, Vec3(0.0f, TARGET_HIP, 0.0f), Quat::Identity()},
									 {"SPINE", 1, Vec3(0.0f, RIG_HEIGHT - TARGET_HIP, 0.0f), Quat::Identity()},
									 {"THIGH", 1, Vec3(0.0f, -TARGET_THIGH_DROP, 0.0f), Quat::Identity()},
									 {"SHIN", 3, Vec3(0.0f, -TARGET_LEG * 0.5f, 0.0f), Quat::Identity()},
									 {"FOOT", 4, Vec3(0.0f, -TARGET_LEG * 0.5f, 0.0f), Quat::Identity()}});

	RetargetProfile* sourceProfile = MakeProfile(source, {{"hips", {1}}, {"spine", {2}}, {"leg", {3, 4, 5}}});
	RetargetProfile* targetProfile = MakeProfile(target, {{"hips", {1}}, {"spine", {2}}, {"leg", {3, 4, 5}}});

	AnimationClip* clip = MakeClip();
	clip->length = 2.0f;
	AnimationClip::Track hips;
	hips.kind = AnimationTrackKind::BonePosition;
	hips.interp = AnimationInterpolation::Linear;
	hips.name = "hips";
	AnimationClip::Track thigh;
	thigh.kind = AnimationTrackKind::BoneRotation;
	thigh.interp = AnimationInterpolation::Linear;
	thigh.name = "thigh";
	AnimationClip::Track shin;
	shin.kind = AnimationTrackKind::BoneRotation;
	shin.interp = AnimationInterpolation::Linear;
	shin.name = "shin";
	for (i32 key = 0; key < 3; ++key) {
		const f32 degrees = 10.0f + static_cast<f32>(key) * 20.0f;
		const Quat thighValue = Quat::AngleAxis(degrees, Vec3::Forward());
		const Quat shinValue = Quat::AngleAxis(-2.0f * degrees, Vec3::Forward());
		hips.times.push_back(static_cast<f32>(key));
		hips.values.push_back(Vec4(0.0f, PlantedHipHeight(degrees), 0.0f, 0.0f));
		thigh.times.push_back(static_cast<f32>(key));
		thigh.values.push_back(Vec4(thighValue.x, thighValue.y, thighValue.z, thighValue.w));
		shin.times.push_back(static_cast<f32>(key));
		shin.values.push_back(Vec4(shinValue.x, shinValue.y, shinValue.z, shinValue.w));
	}
	clip->tracks.push_back(std::move(hips));
	clip->tracks.push_back(std::move(thigh));
	clip->tracks.push_back(std::move(shin));

	Handle<SceneObject> object = MakeRigObject(target, "character");
	Handle<Animator> animator = object->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(clip));
	animator->SetApplyRootMotion(false);
	animator->SetRetarget(Handle<RetargetProfile>(sourceProfile), Handle<RetargetProfile>(targetProfile));
	Handle<Skeleton> pose = object->GetComponent<Skeleton>();

	RunUpdate(0.0f);
	ASSERT_TRUE(animator->IsRetargeting());

	f32 worstWithout = 0.0f;
	for (i32 step = 0; step < 4; ++step) {
		RunUpdate(0.5f);
		worstWithout = Math::Max(worstWithout, Math::Abs(ModelPosition(*target, pose->GetLocalPose(), 5).y));
	}
	EXPECT_GT(worstWithout, 0.02f);

	targetProfile->goals.assign({str("leg")});
	animator->SetRetarget(Handle<RetargetProfile>(sourceProfile), Handle<RetargetProfile>(targetProfile));
	animator->SetTime(0.0f);
	RunUpdate(0.0f);
	ASSERT_TRUE(animator->IsRetargeting());

	f32 worstWith = 0.0f;
	for (i32 step = 0; step < 4; ++step) {
		RunUpdate(0.5f);
		worstWith = Math::Max(worstWith, Math::Abs(ModelPosition(*target, pose->GetLocalPose(), 5).y));
	}
	EXPECT_LT(worstWith, 1e-3f);
}

TEST_F(RetargetLiveFixture, LayerMaskUsesAuthoringRig) {
	SkeletonAsset* source = MakeRig({{"hips", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									 {"arm", 0, Vec3(0.3f, 0.3f, 0.0f), Quat::Identity()},
									 {"leg", 0, Vec3(0.0f, -0.5f, 0.0f), Quat::Identity()}});
	SkeletonAsset* target = MakeRig({{"HIPS", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									 {"LEG", 0, Vec3(0.0f, -0.5f, 0.0f), Quat::Identity()},
									 {"ARM", 0, Vec3(0.3f, 0.3f, 0.0f), Quat::Identity()}});

	RetargetProfile* sourceProfile = MakeProfile(source, {{"hips", {0}}, {"arm", {1}}, {"leg", {2}}});
	RetargetProfile* targetProfile = MakeProfile(target, {{"hips", {0}}, {"arm", {2}}, {"leg", {1}}});

	AnimationClip* clip = MakeClip();
	AddRotation(*clip, "arm", Quat::AngleAxis(50.0f, Vec3::Up()));
	AddRotation(*clip, "leg", Quat::AngleAxis(50.0f, Vec3::Up()));

	Handle<SceneObject> object = MakeRigObject(target, "character");
	Handle<Animator> animator = object->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(clip));
	animator->SetApplyRootMotion(false);
	animator->SetRetarget(Handle<RetargetProfile>(sourceProfile), Handle<RetargetProfile>(targetProfile));

	RunUpdate(0.0f);
	ASSERT_TRUE(animator->IsRetargeting());

	AnimationLayer* layer = animator->GetLayer(0);
	ASSERT_NE(layer, nullptr);
	ASSERT_TRUE(layer->SetMaskSubtree(*source, "hips", 1.0f));
	ASSERT_TRUE(layer->SetMaskSubtree(*source, "arm", 0.0f));

	RunUpdate(0.1f);
	const std::span<const BoneTransform> pose = object->GetComponent<Skeleton>()->GetLocalPose();
	ASSERT_EQ(pose.size(), 3u);

	EXPECT_NEAR(Quat::Angle(pose[2].rotation, target->restPose[2].rotation), 0.0f, 1e-2f);
	EXPECT_GT(Quat::Angle(pose[1].rotation, target->restPose[1].rotation), 40.0f);
}

TEST_F(RetargetLiveFixture, ClearedRetargetRestoresRig) {
	SkeletonAsset* rig = MakeRig({{"Hips", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
								  {"Spine", 0, Vec3(0.0f, 0.5f, 0.0f), Quat::Identity()}});
	SkeletonAsset* stranger = MakeRig({{"Hips", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									   {"Spine", 0, Vec3(0.0f, 0.5f, 0.0f), Quat::Identity()}});

	RetargetProfile* onTheRig = MakeProfile(rig, {{"hips", {0}}, {"spine", {1}}});
	RetargetProfile* onTheStranger = MakeProfile(stranger, {{"hips", {0}}, {"spine", {1}}});
	RetargetProfile* nothingInCommon = MakeProfile(stranger, {{"wing", {0}}, {"antenna", {1}}});

	AnimationClip* clip = MakeClip();
	AddRotation(*clip, "Spine", Quat::AngleAxis(45.0f, Vec3::Up()));

	Handle<SceneObject> object = MakeRigObject(rig, "character");
	Handle<Animator> animator = object->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(clip));
	animator->SetApplyRootMotion(false);
	Handle<Skeleton> pose = object->GetComponent<Skeleton>();

	animator->SetRetarget(Handle<RetargetProfile>(onTheStranger), Handle<RetargetProfile>(onTheStranger));
	RunUpdate(0.1f);
	EXPECT_FALSE(animator->IsRetargeting());

	animator->SetRetarget(Handle<RetargetProfile>(nothingInCommon), Handle<RetargetProfile>(onTheRig));
	RunUpdate(0.1f);
	EXPECT_FALSE(animator->IsRetargeting());

	animator->SetRetarget(Handle<RetargetProfile>(onTheStranger), Handle<RetargetProfile>());
	EXPECT_FALSE(animator->IsRetargeting());

	animator->SetRetarget(Handle<RetargetProfile>(onTheStranger), Handle<RetargetProfile>(onTheRig));
	animator->SetTime(0.0f);
	RunUpdate(0.1f);
	ASSERT_TRUE(animator->IsRetargeting());

	animator->ClearRetarget();
	animator->SetTime(0.0f);
	RunUpdate(0.1f);
	EXPECT_FALSE(animator->IsRetargeting());
	ASSERT_EQ(pose->GetLocalPose().size(), 2u);
	EXPECT_GT(Quat::Angle(pose->GetLocalPose()[1].rotation, Quat::Identity()), 40.0f);
}

namespace {

	class RetargetCapturingSink : public ILogSink {
	public:
		static constexpr const char* SENTINEL = "retarget-sink-sentinel";

		void ReceiveLog(const LogEvent& event) override {
			std::lock_guard<std::mutex> lock(_mutex);
			if (event.message.find(SENTINEL) != str::npos) {
				_armed = true;
				return;
			}
			if (!_armed) {
				return;
			}
			_messages.push_back(str(event.message));
		}
		void Flush() override {}

		bool Armed() const {
			std::lock_guard<std::mutex> lock(_mutex);
			return _armed;
		}

		u32 CountMatching(std::string_view needle) const {
			std::lock_guard<std::mutex> lock(_mutex);
			u32 count = 0;
			for (const str& message : _messages) {
				if (message.find(needle) != str::npos) {
					++count;
				}
			}
			return count;
		}
	private:
		mutable std::mutex _mutex;
		std::vector<str> _messages;
		bool _armed = false;
	};

	bool ArmRetargetSink(const std::shared_ptr<RetargetCapturingSink>& sink) {
		SLT_LOG_WARN("Animation", "{}", RetargetCapturingSink::SENTINEL);
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (std::chrono::steady_clock::now() < deadline) {
			if (sink->Armed()) {
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		return false;
	}

} // anonymous namespace

TEST_F(RetargetLiveFixture, RefusedRetargetReportedOnce) {
	SkeletonAsset* rig = MakeRig({{"Hips", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
								  {"Spine", 0, Vec3(0.0f, 0.5f, 0.0f), Quat::Identity()}});
	SkeletonAsset* stranger = MakeRig({{"Hips", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									   {"Spine", 0, Vec3(0.0f, 0.5f, 0.0f), Quat::Identity()}});
	RetargetProfile* onTheStranger = MakeProfile(stranger, {{"hips", {0}}, {"spine", {1}}});
	onTheStranger->virtualPath = "project://stranger.rig";

	AnimationClip* clip = MakeClip();
	AddRotation(*clip, "Spine", Quat::AngleAxis(45.0f, Vec3::Up()));

	Handle<SceneObject> object = MakeRigObject(rig, "character");
	Handle<Animator> animator = object->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(clip));
	animator->SetApplyRootMotion(false);

	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	LogManager::SetGlobalLevel(LogLevel::Trace);
	auto sink = std::make_shared<RetargetCapturingSink>();
	LogManager::AddSink(sink);
	ASSERT_TRUE(ArmRetargetSink(sink));

	animator->SetRetarget(Handle<RetargetProfile>(onTheStranger), Handle<RetargetProfile>(onTheStranger));
	for (i32 frame = 0; frame < 6; ++frame) {
		RunUpdate(0.05f);
		ASSERT_FALSE(animator->IsRetargeting());
	}

	LogManager::Terminate();
	ASSERT_GT(sink->CountMatching(""), 0u);
	EXPECT_EQ(sink->CountMatching("doesn't use this object's skeleton"), 1u);
	sink.reset();
	JobSystem::Terminate();
}

TEST_F(RetargetLiveFixture, MismatchedStancesWarn) {
	SkeletonAsset* lying = MakeRig({{"Hips", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									{"Spine", 0, Vec3(0.0f, 0.0f, 0.5f), Quat::Identity()},
									{"Chest", 1, Vec3(0.0f, 0.0f, 0.5f), Quat::Identity()}});
	SkeletonAsset* standing = MakeRig({{"Hips", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									   {"Spine", 0, Vec3(0.0f, 0.5f, 0.0f), Quat::Identity()},
									   {"Chest", 1, Vec3(0.0f, 0.5f, 0.0f), Quat::Identity()}});

	RetargetProfile* lyingProfile = MakeProfile(lying, {{"spine", {0, 1, 2}}});
	lyingProfile->virtualPath = "project://lying.rig";
	RetargetProfile* standingProfile = MakeProfile(standing, {{"spine", {0, 1, 2}}});
	standingProfile->virtualPath = "project://standing.rig";

	SkeletonAsset* aStance = MakeRig({{"Hips", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									  {"Spine", 0, Vec3(0.0f, 0.35355f, 0.35355f), Quat::Identity()},
									  {"Chest", 1, Vec3(0.0f, 0.35355f, 0.35355f), Quat::Identity()}});
	RetargetProfile* aStanceProfile = MakeProfile(aStance, {{"spine", {0, 1, 2}}});
	aStanceProfile->virtualPath = "project://a-stance.rig";

	AnimationClip* clip = MakeClip();
	AddRotation(*clip, "Spine", Quat::AngleAxis(20.0f, Vec3::Up()));

	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	LogManager::SetGlobalLevel(LogLevel::Trace);
	auto sink = std::make_shared<RetargetCapturingSink>();
	LogManager::AddSink(sink);
	ASSERT_TRUE(ArmRetargetSink(sink));

	AnimationClip* baked = Retarget::Bake(*clip, *lyingProfile, *standingProfile);
	ASSERT_NE(baked, nullptr);

	AnimationClip* quiet = Retarget::Bake(*clip, *aStanceProfile, *standingProfile);
	ASSERT_NE(quiet, nullptr);

	LogManager::Terminate();
	ASSERT_GT(sink->CountMatching(""), 0u);
	EXPECT_EQ(sink->CountMatching("have stances"), 1u);
	EXPECT_EQ(sink->CountMatching("'spine'"), 1u);

	Delete(baked);
	Delete(quiet);
	sink.reset();
	JobSystem::Terminate();
}

TEST_F(RetargetLiveFixture, BindPoseSilencesStanceWarning) {
	SkeletonAsset* lying = MakeRig({{"Hips", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									{"Spine", 0, Vec3(0.0f, 0.0f, 0.5f), Quat::Identity()},
									{"Chest", 1, Vec3(0.0f, 0.0f, 0.5f), Quat::Identity()}});
	SkeletonAsset* standing = MakeRig({{"Hips", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									   {"Spine", 0, Vec3(0.0f, 0.5f, 0.0f), Quat::Identity()},
									   {"Chest", 1, Vec3(0.0f, 0.5f, 0.0f), Quat::Identity()}});
	RetargetProfile* lyingProfile = MakeProfile(lying, {{"spine", {0, 1, 2}}});
	lyingProfile->virtualPath = "project://lying.rig";
	RetargetProfile* standingProfile = MakeProfile(standing, {{"spine", {0, 1, 2}}});
	standingProfile->virtualPath = "project://standing.rig";

	Vector<Mat4> inverseBind{GetMemoryResource(MemTag::Asset)};
	for (f32 height : {1.0f, 1.5f, 2.0f}) {
		inverseBind.push_back(Mat4::Translation(Vec3(0.0f, height, 0.0f)).Inverse());
	}
	const std::array<i16, 3> jointToBone{0, 1, 2};
	ASSERT_TRUE(RetargetPoseOps::SampleBindPose(*lying, std::span<const i16>(jointToBone.data(), jointToBone.size()),
												std::span<const Mat4>(inverseBind.data(), inverseBind.size()),
												Mat4::Identity(), lyingProfile->referencePose));

	AnimationClip* clip = MakeClip();
	AddRotation(*clip, "Spine", Quat::AngleAxis(20.0f, Vec3::Up()));

	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	LogManager::SetGlobalLevel(LogLevel::Trace);
	auto sink = std::make_shared<RetargetCapturingSink>();
	LogManager::AddSink(sink);
	ASSERT_TRUE(ArmRetargetSink(sink));

	AnimationClip* baked = Retarget::Bake(*clip, *lyingProfile, *standingProfile);
	ASSERT_NE(baked, nullptr);

	LogManager::Terminate();
	ASSERT_GT(sink->CountMatching(""), 0u);
	EXPECT_EQ(sink->CountMatching("have stances"), 0u);

	ASSERT_EQ(lyingProfile->referencePose.size(), 3u);
	EXPECT_NEAR(lyingProfile->referencePose[1].position.y, 0.5f, 1e-4f);
	EXPECT_NEAR(lyingProfile->referencePose[1].position.z, 0.0f, 1e-4f);

	Delete(baked);
	sink.reset();
	JobSystem::Terminate();
}

TEST_F(RetargetLiveFixture, RigSwapTurnsRetargetOff) {
	SkeletonAsset* source = MakeRig({{"hips", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									 {"spine", 0, Vec3(0.0f, 0.5f, 0.0f), Quat::Identity()}});
	SkeletonAsset* target = MakeRig({{"HIPS", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									 {"SPINE", 0, Vec3(0.0f, 0.5f, 0.0f), Quat::Identity()}});
	SkeletonAsset* other = MakeRig({{"Root", -1, Vec3(), Quat::Identity()},
									{"A", 0, Vec3(0.0f, 0.4f, 0.0f), Quat::Identity()},
									{"B", 1, Vec3(0.0f, 0.4f, 0.0f), Quat::Identity()},
									{"C", 2, Vec3(0.0f, 0.4f, 0.0f), Quat::Identity()}});

	RetargetProfile* sourceProfile = MakeProfile(source, {{"hips", {0}}, {"spine", {1}}});
	RetargetProfile* targetProfile = MakeProfile(target, {{"hips", {0}}, {"spine", {1}}});

	AnimationClip* clip = MakeClip();
	AddRotation(*clip, "spine", Quat::AngleAxis(50.0f, Vec3::Up()));

	Handle<SceneObject> object = MakeRigObject(target, "character");
	Handle<Animator> animator = object->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(clip));
	animator->SetApplyRootMotion(false);
	animator->SetRetarget(Handle<RetargetProfile>(sourceProfile), Handle<RetargetProfile>(targetProfile));
	Handle<Skeleton> rig = object->GetComponent<Skeleton>();

	RunUpdate(0.1f);
	ASSERT_TRUE(animator->IsRetargeting());
	ASSERT_GT(Quat::Angle(rig->GetLocalPose()[1].rotation, Quat::Identity()), 40.0f);

	rig->SetSkeleton(Handle<SkeletonAsset>(other));
	RunUpdate(0.1f);

	EXPECT_FALSE(animator->IsRetargeting());
	const std::span<const BoneTransform> pose = rig->GetLocalPose();
	ASSERT_EQ(pose.size(), other->parents.size());
	for (size_t bone = 0; bone < pose.size(); ++bone) {
		EXPECT_NEAR(Quat::Angle(pose[bone].rotation, other->restPose[bone].rotation), 0.0f, 1e-3f);
	}
}

TEST_F(RetargetLiveFixture, RetargetedRootMotionTravelsAtTargetsProportions) {
	SkeletonAsset* source = MakeRig({{"hips", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									 {"spine", 0, Vec3(0.0f, 0.5f, 0.0f), Quat::Identity()}});
	SkeletonAsset* target = MakeRig({{"HIPS", -1, Vec3(0.0f, 2.0f, 0.0f), Quat::AngleAxis(90.0f, Vec3::Up())},
									 {"SPINE", 0, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()}});

	RetargetProfile* sourceProfile = MakeProfile(source, {{"hips", {0}}, {"spine", {1}}});
	RetargetProfile* targetProfile = MakeProfile(target, {{"hips", {0}}, {"spine", {1}}});

	AnimationClip* clip = MakeClip();
	clip->length = 1.0f;
	clip->bakeRootMotionIntoPose = false;
	AnimationClip::Track travel;
	travel.kind = AnimationTrackKind::BonePosition;
	travel.interp = AnimationInterpolation::Linear;
	travel.name = "hips";
	AnimationClip::Track turn;
	turn.kind = AnimationTrackKind::BoneRotation;
	turn.interp = AnimationInterpolation::Linear;
	turn.name = "hips";
	for (i32 key = 0; key < 2; ++key) {
		const Quat facing = Quat::AngleAxis(static_cast<f32>(key) * 40.0f, Vec3::Up());
		travel.times.push_back(static_cast<f32>(key));
		travel.values.push_back(Vec4(0.0f, 1.0f, static_cast<f32>(key), 0.0f));
		turn.times.push_back(static_cast<f32>(key));
		turn.values.push_back(Vec4(facing.x, facing.y, facing.z, facing.w));
	}
	clip->tracks.push_back(std::move(travel));
	clip->tracks.push_back(std::move(turn));

	Handle<SceneObject> plain = MakeRigObject(source, "plain");
	Handle<Animator> plainAnimator = plain->AddComponent<Animator>();
	plainAnimator->SetClip(Handle<AnimationClip>(clip));
	plainAnimator->SetApplyRootMotion(false);

	AnimationClip* baked = Retarget::Bake(*clip, *sourceProfile, *targetProfile);
	ASSERT_NE(baked, nullptr);
	_owned.push_back(baked);
	Handle<SceneObject> bakedObject = MakeRigObject(target, "baked");
	Handle<Animator> bakedAnimator = bakedObject->AddComponent<Animator>();
	bakedAnimator->SetClip(Handle<AnimationClip>(baked));
	bakedAnimator->SetApplyRootMotion(false);

	Handle<SceneObject> retargeted = MakeRigObject(target, "retargeted");
	Handle<Animator> retargetedAnimator = retargeted->AddComponent<Animator>();
	retargetedAnimator->SetClip(Handle<AnimationClip>(clip));
	retargetedAnimator->SetApplyRootMotion(false);
	retargetedAnimator->SetRetarget(Handle<RetargetProfile>(sourceProfile), Handle<RetargetProfile>(targetProfile));

	RunUpdate(0.0f);
	ASSERT_TRUE(retargetedAnimator->IsRetargeting());
	RunUpdate(0.25f);

	ASSERT_TRUE(plainAnimator->IsExtractingRootMotion());
	ASSERT_TRUE(bakedAnimator->IsExtractingRootMotion());
	ASSERT_TRUE(retargetedAnimator->IsExtractingRootMotion());

	const f32 plainTravel = plainAnimator->GetRootMotionDelta().Length();
	const f32 retargetedTravel = retargetedAnimator->GetRootMotionDelta().Length();
	ASSERT_GT(plainTravel, 1e-3f);
	EXPECT_NEAR(retargetedTravel / plainTravel, 2.0f, 1e-2f);

	EXPECT_NEAR((retargetedAnimator->GetRootMotionDelta() - bakedAnimator->GetRootMotionDelta()).Length(), 0.0f, 1e-4f);
	EXPECT_NEAR(Quat::Angle(retargetedAnimator->GetRootRotationDelta(), bakedAnimator->GetRootRotationDelta()), 0.0f,
				1e-2f);
	EXPECT_GT(Quat::Angle(bakedAnimator->GetRootRotationDelta(), Quat::Identity()), 1.0f);
}

TEST_F(RetargetLiveFixture, UnchainedBoneLeftAlone) {
	SkeletonAsset* source = MakeRig({{"hips", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									 {"spine", 0, Vec3(0.0f, 0.5f, 0.0f), Quat::Identity()}});
	SkeletonAsset* target = MakeRig({{"HIPS", -1, Vec3(0.0f, 1.0f, 0.0f), Quat::Identity()},
									 {"SPINE", 0, Vec3(0.0f, 0.5f, 0.0f), Quat::Identity()},
									 {"CAPE", 0, Vec3(0.0f, 0.2f, -0.2f), Quat::Identity()}});

	RetargetProfile* sourceProfile = MakeProfile(source, {{"hips", {0}}, {"spine", {1}}});
	RetargetProfile* targetProfile = MakeProfile(target, {{"hips", {0}}, {"spine", {1}}});

	AnimationClip* clip = MakeClip();
	AddRotation(*clip, "spine", Quat::AngleAxis(30.0f, Vec3::Up()));

	Handle<SceneObject> object = MakeRigObject(target, "character");
	Handle<Animator> animator = object->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(clip));
	animator->SetApplyRootMotion(false);
	animator->SetRetarget(Handle<RetargetProfile>(sourceProfile), Handle<RetargetProfile>(targetProfile));
	Handle<Skeleton> pose = object->GetComponent<Skeleton>();

	RunUpdate(0.0f);
	ASSERT_TRUE(animator->IsRetargeting());

	const Quat capeOwner = Quat::AngleAxis(70.0f, Vec3::Right());
	BoneTransform cape = target->restPose[2];
	cape.rotation = capeOwner;
	pose->SetBoneLocalPose(2, cape);

	RunUpdate(0.1f);
	ASSERT_EQ(pose->GetLocalPose().size(), 3u);
	EXPECT_NEAR(Quat::Angle(pose->GetLocalPose()[2].rotation, capeOwner), 0.0f, 1e-2f);
	EXPECT_GT(Quat::Angle(pose->GetLocalPose()[1].rotation, Quat::Identity()), 25.0f);
}

#endif

#ifdef SHALLTEAR_SHARED

namespace {

	constexpr const char* PACKAGE_TARGET = "Animation";

	std::filesystem::path RepoDir(const char* relative) {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
			std::filesystem::path candidate = probe / relative;
			if (std::filesystem::exists(candidate, ec)) {
				return candidate;
			}
			std::filesystem::path parent = probe.parent_path();
			if (parent == probe) {
				break;
			}
			probe = parent;
		}
		return {};
	}

	class RetargetImportFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		ModuleHandle _module;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_retarget_import_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));

			TypeRegistry::InitializePrimitives();
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			_module = ModuleSystem::Load(Path(str(PACKAGE_TARGET) + ".dll"));
			ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			ModuleSystem::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		bool StageFixture(const char* name) {
			return StageFixtureAs(name, name);
		}

		bool StageFixtureAs(const char* name, const char* asName) {
			std::filesystem::path fixtures = RepoDir("Source/Tests/Fixtures/Graphics");
			if (fixtures.empty()) {
				return false;
			}
			std::error_code ec;
			std::filesystem::copy_file(fixtures / name, _root / asName,
									   std::filesystem::copy_options::overwrite_existing, ec);
			return !ec;
		}

		bool StageFixtureWithoutTakes(const char* name) {
			if (!StageFixture(name)) {
				return false;
			}
			const std::filesystem::path staged = _root / name;
			std::string text;
			{
				std::ifstream input(staged, std::ios::binary);
				text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
			}
			const std::string key = "\"animations\"";
			const size_t at = text.find(key);
			if (at == std::string::npos) {
				return false;
			}
			text.replace(at, key.size(), "\"animationsRemoved\"");
			std::ofstream output(staged, std::ios::binary | std::ios::trunc);
			output.write(text.data(), static_cast<std::streamsize>(text.size()));
			return output.good();
		}

		static u32 ParsedNodeCount(const Path& virtualPath) {
			return static_cast<u32>(ParseModel(FileSystem::Resolve(virtualPath)).nodes.size());
		}

		bool StageFixtureWithoutSkin(const char* name) {
			return StageFixtureWithoutSkinAs(name, name);
		}

		bool StageFixtureWithoutSkinAs(const char* name, const char* asName) {
			if (!StageFixtureAs(name, asName)) {
				return false;
			}
			const std::filesystem::path staged = _root / asName;
			std::string text;
			{
				std::ifstream input(staged, std::ios::binary);
				text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
			}
			const std::string bound = "\"mesh\": 0,\n      \"skin\": 0";
			const size_t node = text.find(bound);
			if (node == std::string::npos) {
				return false;
			}
			text.replace(node, bound.size(), "\"mesh\": 0");

			const std::string key = "\"skins\"";
			const size_t at = text.find(key);
			if (at == std::string::npos) {
				return false;
			}
			text.replace(at, key.size(), "\"skinsRemoved\"");
			std::ofstream output(staged, std::ios::binary | std::ios::trunc);
			output.write(text.data(), static_cast<std::streamsize>(text.size()));
			return output.good();
		}

		bool StageAnimationLibraryAs(const char* name, const char* asName) {
			if (!StageFixtureWithoutSkinAs(name, asName)) {
				return false;
			}
			const std::filesystem::path staged = _root / asName;
			std::string text;
			{
				std::ifstream input(staged, std::ios::binary);
				text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
			}
			const std::string hips = "\"name\": \"mixamorig:Hips\",\n      \"translation\": [\n        0.0,\n        1.0,";
			const size_t hipsAt = text.find(hips);
			if (hipsAt == std::string::npos) {
				return false;
			}
			text.replace(hipsAt, hips.size(),
						 "\"name\": \"mixamorig:Hips\",\n      \"translation\": [\n        0.0,\n        0.24,");
			std::ofstream output(staged, std::ios::binary | std::ios::trunc);
			output.write(text.data(), static_cast<std::streamsize>(text.size()));
			return output.good();
		}

		bool StageFixtureSavedMidTake(const char* name) {
			if (!StageFixture(name)) {
				return false;
			}
			const std::filesystem::path staged = _root / name;
			std::string text;
			{
				std::ifstream input(staged, std::ios::binary);
				text.assign(std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>());
			}
			const std::string host = "\"name\": \"Body\",\n      \"mesh\": 0,";
			const size_t hostAt = text.find(host);
			if (hostAt == std::string::npos) {
				return false;
			}
			text.replace(hostAt, host.size(),
						 "\"name\": \"Body\",\n      \"translation\": [0.0, 0.5, 0.0],\n      \"mesh\": 0,");

			const std::string hips = "\"name\": \"mixamorig:Hips\",\n      \"translation\": [\n        0.0,\n        1.0,";
			const size_t hipsAt = text.find(hips);
			if (hipsAt == std::string::npos) {
				return false;
			}
			text.replace(hipsAt, hips.size(),
						 "\"name\": \"mixamorig:Hips\",\n      \"translation\": [\n        0.0,\n        0.24,");

			std::ofstream output(staged, std::ios::binary | std::ios::trunc);
			output.write(text.data(), static_cast<std::streamsize>(text.size()));
			return output.good();
		}

		bool WriteAnimationBlock(const Path& model, std::string_view blockJSON) {
			Path physMeta = FileSystem::Resolve(AssetDatabase::GetMetaPath(model));
			str text = FileSystem::ReadTextFile(physMeta, MemTag::Editor);
			const size_t settings = text.find("\"settings\"");
			if (settings == str::npos) {
				return false;
			}
			const size_t open = text.find('{', settings);
			if (open == str::npos) {
				return false;
			}
			str spliced = text.substr(0, open + 1);
			spliced += blockJSON;
			str rest = text.substr(open + 1);
			const size_t firstReal = rest.find_first_not_of(" \t\r\n");
			if (firstReal != str::npos && rest[firstReal] != '}') {
				spliced += ",";
			}
			spliced += rest;
			return FileSystem::WriteTextFile(physMeta, spliced);
		}
	};

} // anonymous namespace

TEST_F(RetargetImportFixture, HumanoidImportsWithProfile) {
	ASSERT_TRUE(StageFixture("HumanoidRig.gltf"));

	const Path model("project://HumanoidRig.gltf");
	Asset* prefab = AssetDatabase::LoadAsset(model);
	ASSERT_NE(prefab, nullptr);

	const Guid skeletonGuid = ProductLookup::Find(model, "Skeletons/HumanoidRig");
	ASSERT_TRUE(skeletonGuid.IsValid());

	const Guid profileGuid = ProductLookup::Find(model, "Rigs/HumanoidRig");
	ASSERT_TRUE(profileGuid.IsValid());

	Asset* profile = AssetDatabase::LoadAssetByGuid(profileGuid);
	ASSERT_NE(profile, nullptr);
	EXPECT_EQ(profile->GetAssetTypeId(), StringHash("RetargetProfile"));

	EXPECT_TRUE(ProductLookup::Find(model, "Clips/Idle").IsValid());
}

TEST_F(RetargetImportFixture, TakelessRigGetsProfile) {
	ASSERT_TRUE(StageFixtureWithoutTakes("HumanoidRig.gltf"));

	const Path model("project://HumanoidRig.gltf");
	Asset* prefab = AssetDatabase::LoadAsset(model);
	ASSERT_NE(prefab, nullptr);

	EXPECT_TRUE(ProductLookup::Find(model, "Rigs/HumanoidRig").IsValid());
	EXPECT_FALSE(ProductLookup::Find(model, "Clips/Idle").IsValid());
}

TEST_F(RetargetImportFixture, AnimationOnlyFileIsRetargetSource) {
	ASSERT_TRUE(StageFixtureWithoutSkin("HumanoidRig.gltf"));

	const Path model("project://HumanoidRig.gltf");
	Asset* prefab = AssetDatabase::LoadAsset(model);
	ASSERT_NE(prefab, nullptr);

	const Guid skeletonGuid = ProductLookup::Find(model, "Skeletons/HumanoidRig");
	ASSERT_TRUE(skeletonGuid.IsValid());

	const Guid profileGuid = ProductLookup::Find(model, "Rigs/HumanoidRig");
	ASSERT_TRUE(profileGuid.IsValid());

	EXPECT_TRUE(ProductLookup::Find(model, "Clips/Idle").IsValid());

	Asset* resolved = AssetDatabase::LoadAssetByGuid(skeletonGuid);
	ASSERT_NE(resolved, nullptr);
	auto* skeleton = static_cast<SkeletonAsset*>(resolved);
	EXPECT_EQ(skeleton->GetBoneCount(), ParsedNodeCount(model));

	Asset* clip = AssetDatabase::LoadAssetByGuid(ProductLookup::Find(model, "Clips/Idle"));
	ASSERT_NE(clip, nullptr);
	EXPECT_EQ(static_cast<AnimationClip*>(clip)->skeletonGuid, skeletonGuid);
}

TEST_F(RetargetImportFixture, AnimationOnlyChainsSkipUnnamedNodes) {
	ASSERT_TRUE(StageFixtureWithoutSkin("HumanoidRig.gltf"));

	const Path model("project://HumanoidRig.gltf");
	ASSERT_NE(AssetDatabase::LoadAsset(model), nullptr);

	Asset* resolved = AssetDatabase::LoadAssetByGuid(ProductLookup::Find(model, "Rigs/HumanoidRig"));
	ASSERT_NE(resolved, nullptr);
	auto* profile = static_cast<RetargetProfile*>(resolved);
	ASSERT_FALSE(profile->chains.empty());

	Asset* rig = AssetDatabase::LoadAssetByGuid(profile->skeletonGuid);
	ASSERT_NE(rig, nullptr);
	auto* skeleton = static_cast<SkeletonAsset*>(rig);
	const i32 body = skeleton->FindBone("Body");
	ASSERT_GE(body, 0);
	for (const RetargetProfile::Chain& chain : profile->chains) {
		for (i16 bone : chain.bones) {
			EXPECT_NE(static_cast<i32>(bone), body);
		}
	}
}

TEST_F(RetargetImportFixture, AnimationOnlyModelHasNoRig) {
	ASSERT_TRUE(StageFixtureWithoutSkin("HumanoidRig.gltf"));

	const Path model("project://HumanoidRig.gltf");
	Asset* prefab = AssetDatabase::LoadAsset(model);
	ASSERT_NE(prefab, nullptr);
	ASSERT_TRUE(ProductLookup::Find(model, "Rigs/HumanoidRig").IsValid());

	Handle<Scene> scene = SceneSystem::CreateScene("instantiate");
	Handle<SceneObject> root = SceneSystem::Instantiate(prefab->assetId, scene);
	ASSERT_TRUE(root.IsValid());

	const TypeInfo* animatorType = TypeRegistry::FindType(StringHash("Animator"));
	for (u32 i = 0; i < root->GetComponentCount(); ++i) {
		Handle<Component> component = root->GetComponentAt(i);
		ASSERT_TRUE(component.IsValid());
		EXPECT_NE(component->GetType(), animatorType);
	}
	EXPECT_FALSE(root->GetComponent<Skeleton>().IsValid());
}

TEST_F(RetargetImportFixture, RigKeepsGuidWhenSkinAdded) {
	ASSERT_TRUE(StageFixtureWithoutSkin("HumanoidRig.gltf"));
	const Path model("project://HumanoidRig.gltf");

	Guid before;
	{
		Asset* prefab = AssetDatabase::LoadAsset(model);
		ASSERT_NE(prefab, nullptr);
		before = ProductLookup::Find(model, "Skeletons/HumanoidRig");
		ASSERT_TRUE(before.IsValid());
		AssetDatabase::UnloadAsset(prefab);
	}

	ASSERT_TRUE(StageFixture("HumanoidRig.gltf"));
	ASSERT_TRUE(AssetDatabase::ReimportAsset(model));

	const Guid after = ProductLookup::Find(model, "Skeletons/HumanoidRig");
	ASSERT_TRUE(after.IsValid());
	EXPECT_EQ(before, after);
}

TEST_F(RetargetImportFixture, RangePairMakesMotionAndMirror) {
	ASSERT_TRUE(StageFixture("HumanoidRig.gltf"));

	const Path model("project://HumanoidRig.gltf");
	{
		Asset* first = AssetDatabase::LoadAsset(model);
		ASSERT_NE(first, nullptr);
		AssetDatabase::UnloadAsset(first);
	}
	ASSERT_TRUE(WriteAnimationBlock(
		model,
		R"("animation":{"animationType":"Generic","clips":[{"name":"StrafeL","start":0.0,"end":1.0,"loop":"None"},{"name":"StrafeR","start":0.0,"end":1.0,"loop":"None","mirror":true}]})"));
	ASSERT_TRUE(AssetDatabase::ReimportAsset(model));

	Asset* prefab = AssetDatabase::LoadAsset(model);
	ASSERT_NE(prefab, nullptr);

	const Guid plain = ProductLookup::Find(model, "Clips/StrafeL");
	const Guid mirrored = ProductLookup::Find(model, "Clips/StrafeR");
	ASSERT_TRUE(plain.IsValid());
	ASSERT_TRUE(mirrored.IsValid());
	EXPECT_NE(plain, mirrored);

	Asset* plainAsset = AssetDatabase::LoadAssetByGuid(plain);
	Asset* mirroredAsset = AssetDatabase::LoadAssetByGuid(mirrored);
	ASSERT_NE(plainAsset, nullptr);
	ASSERT_NE(mirroredAsset, nullptr);
	const AnimationClip* left = static_cast<const AnimationClip*>(plainAsset);
	const AnimationClip* right = static_cast<const AnimationClip*>(mirroredAsset);

	auto hipsRotation = [](const AnimationClip& clip) -> const AnimationClip::Track* {
		for (const AnimationClip::Track& track : clip.tracks) {
			if (track.kind == AnimationTrackKind::BoneRotation && track.name == "mixamorig:Hips") {
				return &track;
			}
		}
		return nullptr;
	};

	const AnimationClip::Track* plainTrack = hipsRotation(*left);
	const AnimationClip::Track* mirrorTrack = hipsRotation(*right);
	ASSERT_NE(plainTrack, nullptr);
	ASSERT_NE(mirrorTrack, nullptr);
	ASSERT_EQ(plainTrack->values.size(), mirrorTrack->values.size());
	ASSERT_GE(plainTrack->values.size(), 2u);

	bool anyReflectable = false;
	for (const Vec4& value : plainTrack->values) {
		anyReflectable = anyReflectable || std::abs(value.y) > 1e-3f || std::abs(value.z) > 1e-3f;
	}
	ASSERT_TRUE(anyReflectable);

	for (size_t key = 0; key < plainTrack->values.size(); ++key) {
		const Vec4& was = plainTrack->values[key];
		const Vec4& now = mirrorTrack->values[key];
		const Quat expected(was.x, -was.y, -was.z, was.w);
		EXPECT_NEAR(Quat::Angle(Quat(now.x, now.y, now.z, now.w), expected), 0.0f, 1e-2f);
	}

	AssetDatabase::UnloadAsset(mirroredAsset);
	AssetDatabase::UnloadAsset(plainAsset);
	AssetDatabase::UnloadAsset(prefab);
}

TEST_F(RetargetImportFixture, MirrorRangeWithoutSidesMakesNoClip) {
	ASSERT_TRUE(StageFixture("Fox.glb"));

	const Path model("project://Fox.glb");
	{
		Asset* first = AssetDatabase::LoadAsset(model);
		ASSERT_NE(first, nullptr);
		AssetDatabase::UnloadAsset(first);
	}
	ASSERT_TRUE(WriteAnimationBlock(
		model,
		R"("animation":{"animationType":"Generic","clips":[{"name":"Plain","start":0.0,"end":0.5,"loop":"None"},{"name":"Mirrored","start":0.0,"end":0.5,"loop":"None","mirror":true}]})"));
	ASSERT_TRUE(AssetDatabase::ReimportAsset(model));

	Asset* prefab = AssetDatabase::LoadAsset(model);
	ASSERT_NE(prefab, nullptr);

	EXPECT_FALSE(ProductLookup::Find(model, "Rigs/Fox").IsValid());
	EXPECT_TRUE(ProductLookup::Find(model, "Clips/Plain").IsValid());
	EXPECT_FALSE(ProductLookup::Find(model, "Clips/Mirrored").IsValid());

	AssetDatabase::UnloadAsset(prefab);
}

TEST_F(RetargetImportFixture, QuadrupedGetsClipsNoProfile) {
	ASSERT_TRUE(StageFixture("Fox.glb"));

	const Path model("project://Fox.glb");
	Asset* prefab = AssetDatabase::LoadAsset(model);
	ASSERT_NE(prefab, nullptr);

	EXPECT_TRUE(ProductLookup::Find(model, "Skeletons/Fox").IsValid());
	EXPECT_TRUE(ProductLookup::Find(model, "Clips/Walk").IsValid());
	EXPECT_FALSE(ProductLookup::Find(model, "Rigs/Fox").IsValid());
}

TEST_F(RetargetImportFixture, SampleBindPoseUsesSkinStance) {
	ASSERT_TRUE(StageFixtureSavedMidTake("HumanoidRig.gltf"));
	const Path model("project://HumanoidRig.gltf");

	{
		Asset* prefab = AssetDatabase::LoadAsset(model);
		ASSERT_NE(prefab, nullptr);
		const Guid profileGuid = ProductLookup::Find(model, "Rigs/HumanoidRig");
		ASSERT_TRUE(profileGuid.IsValid());
		Asset* asset = AssetDatabase::LoadAssetByGuid(profileGuid);
		ASSERT_NE(asset, nullptr);
		const RetargetProfile* profile = static_cast<const RetargetProfile*>(asset);
		ASSERT_FALSE(profile->referencePose.empty());
		EXPECT_NEAR(profile->referencePose[0].position.y, 0.24f, 1e-3f);
		AssetDatabase::UnloadAsset(asset);
		AssetDatabase::UnloadAsset(prefab);
	}

	ASSERT_TRUE(WriteAnimationBlock(model, R"("animation":{"animationType":"Generic","rigPose":"BindPose"})"));
	ASSERT_TRUE(AssetDatabase::ReimportAsset(model));

	const Guid profileGuid = ProductLookup::Find(model, "Rigs/HumanoidRig");
	ASSERT_TRUE(profileGuid.IsValid());
	Asset* asset = AssetDatabase::LoadAssetByGuid(profileGuid);
	ASSERT_NE(asset, nullptr);
	const RetargetProfile* profile = static_cast<const RetargetProfile*>(asset);
	ASSERT_EQ(profile->referencePose.size(), 22u);
	EXPECT_NEAR(profile->referencePose[0].position.y, 1.5f, 1e-3f);

	EXPECT_NEAR(profile->referencePose[1].position.y, 0.15f, 1e-3f);
	AssetDatabase::UnloadAsset(asset);
}

TEST_F(RetargetImportFixture, AnimationOnlyRigKeepsFileStance) {
	ASSERT_TRUE(StageFixtureWithoutSkin("HumanoidRig.gltf"));

	const Path model("project://HumanoidRig.gltf");
	{
		Asset* prefab = AssetDatabase::LoadAsset(model);
		ASSERT_NE(prefab, nullptr);
		AssetDatabase::UnloadAsset(prefab);
	}
	ASSERT_TRUE(WriteAnimationBlock(model, R"("animation":{"animationType":"Generic","rigPose":"BindPose"})"));
	ASSERT_TRUE(AssetDatabase::ReimportAsset(model));

	const Guid profileGuid = ProductLookup::Find(model, "Rigs/HumanoidRig");
	ASSERT_TRUE(profileGuid.IsValid());

	Asset* asset = AssetDatabase::LoadAssetByGuid(profileGuid);
	ASSERT_NE(asset, nullptr);
	auto* profile = static_cast<RetargetProfile*>(asset);

	ASSERT_FALSE(profile->chains.empty());
	const i16 hips = profile->chains.front().bones.front();
	const ModelParseResult parsed = ParseModel(FileSystem::Resolve(model));
	ASSERT_TRUE(parsed.success);
	const Vec3 stated = parsed.nodes[static_cast<size_t>(hips)].position;
	ASSERT_GT(profile->referencePose.size(), static_cast<size_t>(hips));
	EXPECT_NEAR(profile->referencePose[static_cast<size_t>(hips)].position.y, stated.y, 1e-4f);
	AssetDatabase::UnloadAsset(asset);
}

static f32 ProfileHipsHeight(const Path& model, const char* rigKey) {
	const Guid profileGuid = ProductLookup::Find(model, rigKey);
	if (!profileGuid.IsValid()) {
		return -9999.0f;
	}
	Asset* asset = AssetDatabase::LoadAssetByGuid(profileGuid);
	if (!asset) {
		return -9999.0f;
	}
	auto* profile = static_cast<RetargetProfile*>(asset);
	f32 height = -9999.0f;
	if (!profile->chains.empty()) {
		const i16 hips = profile->chains.front().bones.front();
		if (profile->referencePose.size() > static_cast<size_t>(hips)) {
			height = profile->referencePose[static_cast<size_t>(hips)].position.y;
		}
	}
	AssetDatabase::UnloadAsset(asset);
	return height;
}

TEST_F(RetargetImportFixture, AnimationOnlyRigStanceFromSettings) {
	ASSERT_TRUE(StageFixture("HumanoidRig.gltf"));
	ASSERT_TRUE(StageAnimationLibraryAs("HumanoidRig.gltf", "Library.gltf"));

	const Path twin("project://HumanoidRig.gltf");
	const Path library("project://Library.gltf");
	{
		Asset* a = AssetDatabase::LoadAsset(twin);
		ASSERT_NE(a, nullptr);
		AssetDatabase::UnloadAsset(a);
		Asset* b = AssetDatabase::LoadAsset(library);
		ASSERT_NE(b, nullptr);
		AssetDatabase::UnloadAsset(b);
	}

	const f32 own = ProfileHipsHeight(library, "Rigs/Library");
	ASSERT_GT(own, -9000.0f);

	const Guid twinGuid = AssetDatabase::LookupGuid(twin);
	ASSERT_TRUE(twinGuid.IsValid());
	str block = str(R"("animation":{"animationType":"Generic","rigPose":"CopyFrom","stanceSource":")") +
				twinGuid.ToString() + str(R"("})");
	ASSERT_TRUE(WriteAnimationBlock(library, block));
	ASSERT_TRUE(AssetDatabase::ReimportAsset(library));

	const f32 copied = ProfileHipsHeight(library, "Rigs/Library");
	const f32 twinStance = ProfileHipsHeight(twin, "Rigs/HumanoidRig");
	ASSERT_GT(twinStance, -9000.0f);

	ASSERT_GT(std::abs(own - twinStance), 0.1f);

	EXPECT_NEAR(copied, twinStance, 1e-4f);
}

TEST_F(RetargetImportFixture, CopiedStanceTakesTwinCorrection) {
	ASSERT_TRUE(StageFixtureSavedMidTake("HumanoidRig.gltf"));
	ASSERT_TRUE(StageFixtureWithoutSkinAs("HumanoidRig.gltf", "Library.gltf"));

	const Path twin("project://HumanoidRig.gltf");
	const Path library("project://Library.gltf");
	{
		Asset* a = AssetDatabase::LoadAsset(twin);
		ASSERT_NE(a, nullptr);
		AssetDatabase::UnloadAsset(a);
		Asset* b = AssetDatabase::LoadAsset(library);
		ASSERT_NE(b, nullptr);
		AssetDatabase::UnloadAsset(b);
	}

	ASSERT_TRUE(WriteAnimationBlock(twin, R"("animation":{"animationType":"Generic","rigPose":"BindPose"})"));
	ASSERT_TRUE(AssetDatabase::ReimportAsset(twin));
	const f32 twinCorrected = ProfileHipsHeight(twin, "Rigs/HumanoidRig");
	ASSERT_GT(twinCorrected, -9000.0f);

	const Guid twinGuid = AssetDatabase::LookupGuid(twin);
	ASSERT_TRUE(twinGuid.IsValid());
	str block = str(R"("animation":{"animationType":"Generic","rigPose":"CopyFrom","stanceSource":")") +
				twinGuid.ToString() + str(R"("})");
	ASSERT_TRUE(WriteAnimationBlock(library, block));
	ASSERT_TRUE(AssetDatabase::ReimportAsset(library));

	ASSERT_GT(std::abs(twinCorrected - 1.0f), 0.1f);

	EXPECT_NEAR(ProfileHipsHeight(library, "Rigs/Library"), twinCorrected, 1e-4f);
}

TEST_F(RetargetImportFixture, EmptyStanceSourceKeepsFileStance) {
	ASSERT_TRUE(StageFixtureWithoutSkin("HumanoidRig.gltf"));

	const Path model("project://HumanoidRig.gltf");
	{
		Asset* prefab = AssetDatabase::LoadAsset(model);
		ASSERT_NE(prefab, nullptr);
		AssetDatabase::UnloadAsset(prefab);
	}
	const f32 own = ProfileHipsHeight(model, "Rigs/HumanoidRig");
	ASSERT_GT(own, -9000.0f);

	ASSERT_TRUE(WriteAnimationBlock(
		model,
		R"("animation":{"animationType":"Generic","rigPose":"CopyFrom","stanceSource":"deadbeefdeadbeef-deadbeefdeadbeef"})"));
	ASSERT_TRUE(AssetDatabase::ReimportAsset(model));

	EXPECT_NEAR(ProfileHipsHeight(model, "Rigs/HumanoidRig"), own, 1e-4f);
}

TEST_F(RetargetImportFixture, SelfStanceSourceRefused) {
	ASSERT_TRUE(StageFixtureWithoutSkin("HumanoidRig.gltf"));

	const Path model("project://HumanoidRig.gltf");
	{
		Asset* prefab = AssetDatabase::LoadAsset(model);
		ASSERT_NE(prefab, nullptr);
		AssetDatabase::UnloadAsset(prefab);
	}
	const f32 own = ProfileHipsHeight(model, "Rigs/HumanoidRig");
	ASSERT_GT(own, -9000.0f);

	const Guid selfGuid = AssetDatabase::LookupGuid(model);
	ASSERT_TRUE(selfGuid.IsValid());
	str block = str(R"("animation":{"animationType":"Generic","rigPose":"CopyFrom","stanceSource":")") +
				selfGuid.ToString() + str(R"("})");
	ASSERT_TRUE(WriteAnimationBlock(model, block));
	ASSERT_TRUE(AssetDatabase::ReimportAsset(model));

	EXPECT_NEAR(ProfileHipsHeight(model, "Rigs/HumanoidRig"), own, 1e-4f);
}

#endif
