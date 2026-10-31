#include "TestsPCH.h"

#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics/SkeletonAsset.h"
#include "Shalltear/Core/Graphics/Skeleton.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Settings.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <initializer_list>
#include <span>

#ifndef SHALLTEAR_SHARED
#include "AnimationPass.h"
#include "AnimationSettings.h"
#include "PoseOps.h"

#include "Shalltear/Animation/AnimationClip.h"
#include "Shalltear/Animation/AnimationLayer.h"
#include "Shalltear/Animation/Animator.h"
#endif

using namespace Shalltear;

#ifndef SHALLTEAR_SHARED

SLT_DECLARE_LINKED_PACKAGE(Animation)

namespace {

	constexpr f32 FIXED_STEP = 1.0f / 50.0f;

	BoneTransform MakeBone(const Vec3& position, const Quat& rotation, const Vec3& scale) {
		BoneTransform out;
		out.position = position;
		out.rotation = rotation;
		out.scale = scale;
		return out;
	}

	void RunUpdate(f32 delta) {
		FrameTime clockState;
		clockState.fixedDeltaTime = FIXED_STEP;
		clockState.maximumDeltaTime = 1000.0f;
		Time::FrameScope clock{clockState};
		Time::BeginFrame(delta);

		FrameStageArgs args;
		FrameStages::Run(FrameStage::Update, args);
	}

} // anonymous namespace

TEST(PoseOpsTest, BlendLerpsPositionSlerpsRotation) {
	using namespace AnimationImpl;
	const BoneTransform a = MakeBone(Vec3(0.0f, 0.0f, 0.0f), Quat::AngleAxis(-170.0f, Vec3::Up()), Vec3(1.0f, 1.0f, 1.0f));
	const BoneTransform b = MakeBone(Vec3(4.0f, 0.0f, 0.0f), Quat::AngleAxis(170.0f, Vec3::Up()), Vec3(3.0f, 3.0f, 3.0f));

	EXPECT_FLOAT_EQ(BlendBone(a, b, 0.0f).position.x, 0.0f);
	EXPECT_FLOAT_EQ(BlendBone(a, b, 1.0f).position.x, 4.0f);
	EXPECT_FLOAT_EQ(BlendBone(a, b, 1.0f).scale.y, 3.0f);

	const BoneTransform half = BlendBone(a, b, 0.5f);
	EXPECT_NEAR(half.position.x, 2.0f, 1e-5f);
	EXPECT_NEAR(half.scale.z, 2.0f, 1e-5f);
	EXPECT_NEAR(half.rotation.LengthSquared(), 1.0f, 1e-5f);

	EXPECT_NEAR(Math::Abs(Quat::Angle(half.rotation, Quat::AngleAxis(180.0f, Vec3::Up()))), 0.0f, 1e-3f);
}

TEST(PoseOpsTest, AdditiveFoldIsHalfDelta) {
	using namespace AnimationImpl;
	const BoneTransform base = MakeBone(Vec3(10.0f, 0.0f, 0.0f), Quat::AngleAxis(90.0f, Vec3::Up()), Vec3(2.0f, 2.0f, 2.0f));
	const BoneTransform reference = MakeBone(Vec3(0.0f, 0.0f, 0.0f), Quat::Identity(), Vec3(1.0f, 1.0f, 1.0f));
	const BoneTransform add = MakeBone(Vec3(4.0f, 0.0f, 0.0f), Quat::AngleAxis(40.0f, Vec3::Up()), Vec3(1.5f, 1.0f, 1.0f));

	const BoneTransform half = AddBone(base, add, reference, 0.5f);
	EXPECT_NEAR(half.position.x, 12.0f, 1e-4f);

	EXPECT_NEAR(Quat::Angle(half.rotation, Quat::AngleAxis(110.0f, Vec3::Up())), 0.0f, 1e-2f);

	EXPECT_NEAR(AddBone(base, add, reference, 1.0f).scale.x, 3.0f, 1e-4f);
	EXPECT_NEAR(half.scale.x, 2.5f, 1e-4f);

	EXPECT_FLOAT_EQ(AddBone(base, add, reference, 0.0f).position.x, 10.0f);
	EXPECT_FLOAT_EQ(AddBone(base, add, reference, 0.0f).scale.x, 2.0f);
	EXPECT_NEAR(Quat::Angle(AddBone(base, add, reference, 0.0f).rotation, base.rotation), 0.0f, 1e-3f);
}

TEST(PoseOpsTest, InertialDecayHitsEndsThen0) {
	using namespace AnimationImpl;
	InertialChannel channel;
	channel.direction = Vec3::Up();
	channel.x0 = 3.0f;
	channel.v0 = 0.0f;
	channel.duration = 0.5f;

	EXPECT_FLOAT_EQ(InertialDecay(channel, 0.0f), 3.0f);
	EXPECT_NEAR(InertialDecay(channel, 0.5f), 0.0f, 1e-5f);

	EXPECT_FLOAT_EQ(InertialDecay(channel, 0.5f), 0.0f);
	EXPECT_FLOAT_EQ(InertialDecay(channel, 2.0f), 0.0f);

	constexpr f32 H = 1e-4f;
	const f32 slopeAtEnd = (InertialDecay(channel, 0.5f) - InertialDecay(channel, 0.5f - H)) / H;
	EXPECT_NEAR(slopeAtEnd, 0.0f, 1e-2f);

	InertialChannel moving = channel;
	moving.v0 = -5.0f;
	const f32 slopeOfMoving = (InertialDecay(moving, H) - InertialDecay(moving, 0.0f)) / H;
	EXPECT_NEAR(slopeOfMoving, -5.0f, 0.05f);

	f32 previous = channel.x0;
	for (i32 step = 1; step <= 50; ++step) {
		const f32 value = InertialDecay(channel, 0.5f * static_cast<f32>(step) / 50.0f);
		EXPECT_LE(value, previous + 1e-5f) << step;
		previous = value;
	}

	InertialChannel closing = channel;
	closing.v0 = -4.0f;
	EXPECT_LT(InertialDecay(closing, 0.05f), InertialDecay(channel, 0.05f));
}

TEST(PoseOpsTest, InertialCaptureCarriesOffsetsAxisAndVelocity) {
	using namespace AnimationImpl;
	const BoneTransform target = MakeBone(Vec3::Zero(), Quat::Identity(), Vec3::One());
	const BoneTransform targetPrev = target;

	const BoneTransform source = MakeBone(Vec3(2.0f, 0.0f, 0.0f), Quat::AngleAxis(30.0f, Vec3::Right()), Vec3::One());
	const BoneTransform sourcePrev = MakeBone(Vec3(1.0f, 0.0f, 0.0f), Quat::AngleAxis(-10.0f, Vec3::Right()), Vec3::One());

	InertialBone offset;
	offset.bone = 0;
	ASSERT_TRUE(CaptureInertialBone(source, target, sourcePrev, targetPrev, 0.01f, 0.4f, offset));

	EXPECT_NEAR(offset.position.x0, 2.0f, 1e-4f);
	EXPECT_NEAR(offset.position.direction.x, 1.0f, 1e-4f);
	EXPECT_NEAR(offset.position.v0, 100.0f, 1e-2f);

	EXPECT_NEAR(Math::Abs(offset.rotation.direction.x), 1.0f, 1e-3f);
	EXPECT_NEAR(offset.rotation.x0, 30.0f, 1e-2f);
	EXPECT_NEAR(offset.rotation.v0, 4000.0f, 1.0f);

	InertialBone none;
	none.bone = 0;
	EXPECT_FALSE(CaptureInertialBone(target, target, targetPrev, targetPrev, 0.01f, 0.4f, none));
	EXPECT_TRUE(none.position.IsInert());
	EXPECT_TRUE(none.rotation.IsInert());
	EXPECT_TRUE(none.scale.IsInert());
}

TEST(PoseOpsTest, CollapsingOffsetClampsDurationNoOvershoot) {
	using namespace AnimationImpl;
	const BoneTransform target = MakeBone(Vec3::Zero(), Quat::Identity(), Vec3::One());

	const BoneTransform source = MakeBone(Vec3(0.1f, 0.0f, 0.0f), Quat::Identity(), Vec3::One());
	const BoneTransform sourcePrev = MakeBone(Vec3(0.6f, 0.0f, 0.0f), Quat::Identity(), Vec3::One());

	InertialBone offset;
	offset.bone = 0;
	ASSERT_TRUE(CaptureInertialBone(source, target, sourcePrev, target, 0.01f, 1.0f, offset));
	EXPECT_NEAR(offset.position.duration, 0.01f, 1e-4f);

	for (i32 step = 0; step <= 40; ++step) {
		const f32 at = 0.01f * static_cast<f32>(step) / 40.0f;
		EXPECT_GE(InertialDecay(offset.position, at), -1e-4f) << step;
	}
}

TEST(PoseOpsTest, SpentOffsetLeavesTargetPose) {
	using namespace AnimationImpl;
	const BoneTransform target = MakeBone(Vec3(1.0f, 2.0f, 3.0f), Quat::AngleAxis(35.0f, Vec3::Forward()), Vec3(1.0f, 2.0f, 3.0f));
	const BoneTransform source = MakeBone(Vec3(9.0f, 9.0f, 9.0f), Quat::AngleAxis(-60.0f, Vec3::Right()), Vec3(4.0f, 4.0f, 4.0f));

	InertialBone offset;
	offset.bone = 0;
	ASSERT_TRUE(CaptureInertialBone(source, target, source, target, 0.02f, 0.3f, offset));

	const BoneTransform atStart = ApplyInertialBone(target, offset, 0.0f);
	EXPECT_NEAR(atStart.position.x, 9.0f, 1e-3f);
	EXPECT_NEAR(Quat::Angle(atStart.rotation, source.rotation), 0.0f, 1e-2f);
	EXPECT_NEAR(atStart.scale.x, 4.0f, 1e-3f);

	const BoneTransform atEnd = ApplyInertialBone(target, offset, 0.3f);
	EXPECT_FLOAT_EQ(atEnd.position.x, target.position.x);
	EXPECT_FLOAT_EQ(atEnd.position.z, target.position.z);
	EXPECT_FLOAT_EQ(atEnd.scale.y, target.scale.y);
	EXPECT_FLOAT_EQ(atEnd.rotation.w, target.rotation.w);
}

namespace {

	class AnimationLayerFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		Handle<Scene> _scene;
		char _token = 0;
		std::vector<Asset*> _owned;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_animation_blend_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			SLT_CALL_LINKED_PACKAGE(Animation, &_token);

			_scene = SceneSystem::CreateScene("animation");
			ASSERT_TRUE(_scene.IsValid());
		}

		void TearDown() override {
			SceneSystem::Terminate();
			for (Asset* asset : _owned) {
				Delete(asset);
			}
			_owned.clear();
			FrameStages::UnregisterOwnedBy(&_token);
			ModuleSystem::Terminate();
			Settings::Terminate();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		SkeletonAsset* MakeSpine() {
			static const char* NAMES[] = {"Hips", "Spine1", "Spine2", "Chest", "Arm"};
			SkeletonAsset* skeleton = New<SkeletonAsset>(MemTag::Asset);
			for (i32 index = 0; index < 5; ++index) {
				skeleton->boneNames.push_back(NAMES[index]);
				skeleton->parents.push_back(static_cast<i16>(index - 1));
				skeleton->restPose.push_back(BoneTransform{});
			}
			_owned.push_back(skeleton);
			return skeleton;
		}

		AnimationClip* MakeClip() {
			AnimationClip* clip = New<AnimationClip>(MemTag::Asset);
			clip->virtualPath = "project://blend.clip";
			_owned.push_back(clip);
			return clip;
		}

		Handle<SceneObject> MakeRig(SkeletonAsset* skeleton) {
			Handle<SceneObject> object = SceneSystem::CreateObject("rig", _scene);
			Handle<Skeleton> component = object->AddComponent<Skeleton>();
			component->SetSkeleton(Handle<SkeletonAsset>(skeleton));
			return object;
		}

		f32 BoneAngle(Handle<SceneObject> rig, u32 bone) {
			const std::span<const BoneTransform> pose = rig->GetComponent<Skeleton>()->GetLocalPose();
			return bone < pose.size() ? Quat::Angle(Quat::Identity(), pose[bone].rotation) : 0.0f;
		}
	};

	void AddConstantRotation(AnimationClip& clip, std::initializer_list<const char*> bones, f32 degrees) {
		for (const char* bone : bones) {
			AnimationClip::Track track;
			track.kind = AnimationTrackKind::BoneRotation;
			track.interp = AnimationInterpolation::Linear;
			track.name = str(bone);
			const Quat value = Quat::AngleAxis(degrees, Vec3::Up());
			track.times.push_back(0.0f);
			track.values.push_back(Vec4(value.x, value.y, value.z, value.w));
			clip.tracks.push_back(std::move(track));
		}
		clip.length = 1.0f;
		clip.loop = AnimationLoopMode::Loop;
	}

	AnimationClip::Track MakeLocalPositionTrack(f32 to) {
		AnimationClip::Track track;
		track.kind = AnimationTrackKind::Property;
		track.interp = AnimationInterpolation::Linear;
		track.objectPath = "";
		track.componentType = StringHash{};
		track.name = "_localPosition";
		track.component = 1;
		track.times.push_back(0.0f);
		track.values.push_back(Vec4(to, 0.0f, 0.0f, 0.0f));
		return track;
	}

	void AddPositionTrack(AnimationClip& clip, std::string_view bone, f32 from, f32 to) {
		AnimationClip::Track track;
		track.kind = AnimationTrackKind::BonePosition;
		track.interp = AnimationInterpolation::Linear;
		track.name = str(bone);
		track.times.push_back(0.0f);
		track.times.push_back(1.0f);
		track.values.push_back(Vec4(from, 0.0f, 0.0f, 0.0f));
		track.values.push_back(Vec4(to, 0.0f, 0.0f, 0.0f));
		clip.tracks.push_back(std::move(track));
		clip.length = 1.0f;
		clip.loop = AnimationLoopMode::Loop;
	}

} // anonymous namespace

TEST_F(AnimationLayerFixture, UpperBodyLayerFeathersDownSpine) {
	SkeletonAsset* skeleton = MakeSpine();
	AnimationClip* base = MakeClip();
	AddConstantRotation(*base, {"Hips", "Spine1", "Spine2", "Chest", "Arm"}, 0.0f);
	AnimationClip* upper = MakeClip();
	AddConstantRotation(*upper, {"Hips", "Spine1", "Spine2", "Chest", "Arm"}, 60.0f);

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(base));

	const i32 layerIndex = animator->AddLayer();
	ASSERT_EQ(layerIndex, 1);
	AnimationLayer* layer = animator->GetLayer(layerIndex);
	ASSERT_NE(layer, nullptr);
	layer->weight = 1.0f;

	ASSERT_TRUE(layer->SetMaskSubtree(*skeleton, "Chest", 1.0f));
	animator->Play(Handle<AnimationClip>(upper), 0.0f, layerIndex);
	RunUpdate(0.1f);

	f32 angles[5];
	for (u32 bone = 0; bone < 5; ++bone) {
		angles[bone] = BoneAngle(rig, bone);
	}
	EXPECT_NEAR(angles[0], 0.0f, 1e-2f);
	EXPECT_NEAR(angles[1], 0.0f, 1e-2f);
	EXPECT_NEAR(angles[2], 0.0f, 1e-2f);
	EXPECT_NEAR(angles[3], 60.0f, 1e-2f);
	EXPECT_NEAR(angles[4], 60.0f, 1e-2f);

	f32 cutStep = 0.0f;
	for (u32 bone = 1; bone < 5; ++bone) {
		cutStep = Math::Max(cutStep, Math::Abs(angles[bone] - angles[bone - 1]));
	}
	EXPECT_NEAR(cutStep, 60.0f, 1e-2f);

	layer->ClearMask();
	ASSERT_TRUE(layer->SetMaskSubtree(*skeleton, "Spine1", 0.25f));
	ASSERT_TRUE(layer->SetMaskSubtree(*skeleton, "Spine2", 0.5f));
	ASSERT_TRUE(layer->SetMaskSubtree(*skeleton, "Chest", 1.0f));
	RunUpdate(0.1f);

	for (u32 bone = 0; bone < 5; ++bone) {
		angles[bone] = BoneAngle(rig, bone);
	}
	EXPECT_NEAR(angles[0], 0.0f, 1e-2f);
	EXPECT_NEAR(angles[1], 15.0f, 1e-2f);
	EXPECT_NEAR(angles[2], 30.0f, 1e-2f);
	EXPECT_NEAR(angles[3], 60.0f, 1e-2f);
	EXPECT_NEAR(angles[4], 60.0f, 1e-2f);

	f32 featheredStep = 0.0f;
	for (u32 bone = 1; bone < 5; ++bone) {
		featheredStep = Math::Max(featheredStep, Math::Abs(angles[bone] - angles[bone - 1]));
	}
	EXPECT_LT(featheredStep, cutStep * 0.6f);
}

TEST_F(AnimationLayerFixture, OverrideLayerWeightBlendsBase) {
	SkeletonAsset* skeleton = MakeSpine();
	AnimationClip* base = MakeClip();
	AddConstantRotation(*base, {"Chest"}, 20.0f);
	AnimationClip* over = MakeClip();
	AddConstantRotation(*over, {"Chest"}, 80.0f);

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(base));
	const i32 index = animator->AddLayer();
	AnimationLayer* layer = animator->GetLayer(index);
	layer->weight = 0.0f;
	animator->Play(Handle<AnimationClip>(over), 0.0f, index);

	RunUpdate(0.1f);
	EXPECT_NEAR(BoneAngle(rig, 3), 20.0f, 1e-2f);

	layer->weight = 1.0f;
	RunUpdate(0.1f);
	EXPECT_NEAR(BoneAngle(rig, 3), 80.0f, 1e-2f);

	layer->weight = 0.5f;
	RunUpdate(0.1f);
	EXPECT_NEAR(BoneAngle(rig, 3), 50.0f, 1e-1f);

	layer->weight = 4.0f;
	RunUpdate(0.1f);
	EXPECT_NEAR(BoneAngle(rig, 3), 80.0f, 1e-2f);
}

TEST_F(AnimationLayerFixture, AdditiveLayerAddsAgainstClipReference) {
	SkeletonAsset* skeleton = MakeSpine();
	AnimationClip* base = MakeClip();
	AddPositionTrack(*base, "Chest", 10.0f, 10.0f);

	AnimationClip* recoil = MakeClip();
	AddPositionTrack(*recoil, "Arm", 4.0f, 6.0f);

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(base));
	const i32 index = animator->AddLayer();
	AnimationLayer* layer = animator->GetLayer(index);
	layer->blend = AnimationBlendMode::Additive;
	layer->weight = 1.0f;
	animator->Play(Handle<AnimationClip>(recoil), 0.0f, index);

	RunUpdate(0.5f);
	const std::span<const BoneTransform> pose = rig->GetComponent<Skeleton>()->GetLocalPose();
	EXPECT_NEAR(pose[4].position.x, 1.0f, 1e-3f);
	EXPECT_NEAR(pose[3].position.x, 10.0f, 1e-3f);

	recoil->additiveReferenceTime = 1.0f;
	++recoil->generation;
	RunUpdate(0.0f);
	EXPECT_NEAR(rig->GetComponent<Skeleton>()->GetLocalPose()[4].position.x, -1.0f, 1e-3f);
}

TEST_F(AnimationLayerFixture, RotationOnlyLayerLeavesPositionBelow) {
	SkeletonAsset* skeleton = MakeSpine();
	AnimationClip* base = MakeClip();
	AddPositionTrack(*base, "Chest", 7.0f, 7.0f);
	AnimationClip* twist = MakeClip();
	AddConstantRotation(*twist, {"Chest"}, 45.0f);

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(base));
	const i32 index = animator->AddLayer();
	animator->GetLayer(index)->weight = 0.5f;
	animator->Play(Handle<AnimationClip>(twist), 0.0f, index);

	RunUpdate(0.1f);
	const std::span<const BoneTransform> pose = rig->GetComponent<Skeleton>()->GetLocalPose();
	EXPECT_NEAR(pose[3].position.x, 7.0f, 1e-3f);
	EXPECT_NEAR(Quat::Angle(Quat::Identity(), pose[3].rotation), 22.5f, 1e-2f);
}

TEST_F(AnimationLayerFixture, PropertyTrackOffBaseLayerDropped) {
	SkeletonAsset* skeleton = MakeSpine();

	AnimationClip* base = MakeClip();
	base->tracks.push_back(MakeLocalPositionTrack(2.0f));
	AddPositionTrack(*base, "Chest", 3.0f, 3.0f);

	AnimationClip* upper = MakeClip();
	upper->tracks.push_back(MakeLocalPositionTrack(9.0f));
	AddPositionTrack(*upper, "Arm", 5.0f, 5.0f);

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(base));
	const i32 index = animator->AddLayer();
	animator->Play(Handle<AnimationClip>(upper), 0.0f, index);

	RunUpdate(0.1f);

	EXPECT_EQ(animator->GetLayer(0)->GetBoundTrackCount(), 2u);
	EXPECT_EQ(animator->GetLayer(index)->GetBoundTrackCount(), 1u);
	EXPECT_NEAR(rig->GetLocalPosition().y, 2.0f, 1e-3f);

	const std::span<const BoneTransform> pose = rig->GetComponent<Skeleton>()->GetLocalPose();
	EXPECT_NEAR(pose[3].position.x, 3.0f, 1e-3f);
	EXPECT_NEAR(pose[4].position.x, 5.0f, 1e-3f);
}

TEST_F(AnimationLayerFixture, TransitionNeverResamplesOutgoingClip) {
	SkeletonAsset* skeleton = MakeSpine();
	AnimationClip* outgoing = MakeClip();
	AddPositionTrack(*outgoing, "Chest", 10.0f, 10.0f);
	AnimationClip* incoming = MakeClip();
	AddPositionTrack(*incoming, "Chest", 0.0f, 0.0f);

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->Play(Handle<AnimationClip>(outgoing), 0.0f, 0);
	RunUpdate(0.1f);
	ASSERT_NEAR(rig->GetComponent<Skeleton>()->GetLocalPose()[3].position.x, 10.0f, 1e-3f);
	ASSERT_FALSE(animator->IsTransitioning());

	animator->Play(Handle<AnimationClip>(incoming), 0.4f, 0);
	RunUpdate(0.02f);
	ASSERT_TRUE(animator->IsTransitioning());

	EXPECT_NEAR(rig->GetComponent<Skeleton>()->GetLocalPose()[3].position.x, 10.0f, 1e-2f);

	outgoing->tracks[0].values[0] = Vec4(1000.0f, 0.0f, 0.0f, 0.0f);
	outgoing->tracks[0].values[1] = Vec4(1000.0f, 0.0f, 0.0f, 0.0f);

	f32 previous = 10.0f;
	for (i32 step = 0; step < 20; ++step) {
		RunUpdate(0.02f);
		const f32 at = rig->GetComponent<Skeleton>()->GetLocalPose()[3].position.x;
		EXPECT_LE(at, previous + 1e-3f);
		EXPECT_LE(at, 11.0f);
		previous = at;
	}

	EXPECT_FALSE(animator->IsTransitioning());
	EXPECT_NEAR(rig->GetComponent<Skeleton>()->GetLocalPose()[3].position.x, 0.0f, 1e-4f);
}

TEST_F(AnimationLayerFixture, PlayWithoutFadeUsesProjectDefault) {
	SkeletonAsset* skeleton = MakeSpine();
	AnimationClip* first = MakeClip();
	AddPositionTrack(*first, "Chest", 10.0f, 10.0f);
	AnimationClip* second = MakeClip();
	AddPositionTrack(*second, "Chest", 0.0f, 0.0f);

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->Play(Handle<AnimationClip>(first), 0.0f, 0);
	RunUpdate(0.05f);

	animator->Play(Handle<AnimationClip>(second));
	RunUpdate(0.01f);
	ASSERT_TRUE(animator->IsTransitioning());
	RunUpdate(0.19f);
	EXPECT_TRUE(animator->IsTransitioning());

	RunUpdate(0.1f);
	RunUpdate(0.0f);
	EXPECT_FALSE(animator->IsTransitioning());

	AnimationSettings* live = Settings::DeclareCategory<AnimationSettings>("Animation", SettingScope::Project, false);
	ASSERT_NE(live, nullptr);
	live->defaultTransitionDuration = 1.5f;
	Settings::NotifyChanged("Animation", "defaultTransitionDuration");

	animator->Play(Handle<AnimationClip>(first));
	RunUpdate(0.01f);
	ASSERT_TRUE(animator->IsTransitioning());
	RunUpdate(0.4f);

	RunUpdate(0.0f);
	EXPECT_TRUE(animator->IsTransitioning());
}

TEST_F(AnimationLayerFixture, LayersComposeInOrderRemovalIsolated) {
	SkeletonAsset* skeleton = MakeSpine();
	AnimationClip* base = MakeClip();
	AddPositionTrack(*base, "Chest", 1.0f, 1.0f);
	AnimationClip* firstAdd = MakeClip();
	AddPositionTrack(*firstAdd, "Chest", 2.0f, 2.0f);
	firstAdd->loop = AnimationLoopMode::None;
	AnimationClip* secondAdd = MakeClip();
	AddPositionTrack(*secondAdd, "Chest", 4.0f, 4.0f);
	secondAdd->loop = AnimationLoopMode::None;

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(base));
	for (AnimationClip* clip : {firstAdd, secondAdd}) {
		const i32 index = animator->AddLayer();
		AnimationLayer* layer = animator->GetLayer(index);
		layer->blend = AnimationBlendMode::Additive;
		clip->additiveReferenceTime = 0.0f;
		animator->Play(Handle<AnimationClip>(clip), 0.0f, index);
	}
	ASSERT_EQ(animator->GetLayerCount(), 3u);

	RunUpdate(0.1f);
	EXPECT_NEAR(rig->GetComponent<Skeleton>()->GetLocalPose()[3].position.x, 1.0f, 1e-3f);

	firstAdd->tracks[0].values[1] = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
	secondAdd->tracks[0].values[1] = Vec4(0.0f, 0.0f, 0.0f, 0.0f);
	++firstAdd->generation;
	++secondAdd->generation;

	animator->GetLayer(1)->SetTime(1.0f);
	animator->GetLayer(2)->SetTime(1.0f);
	RunUpdate(0.0f);
	EXPECT_NEAR(rig->GetComponent<Skeleton>()->GetLocalPose()[3].position.x, -5.0f, 1e-3f);

	EXPECT_FALSE(animator->RemoveLayer(0));
	ASSERT_TRUE(animator->RemoveLayer(1));
	EXPECT_EQ(animator->GetLayerCount(), 2u);
	animator->GetLayer(1)->SetTime(1.0f);
	RunUpdate(0.0f);
	EXPECT_NEAR(rig->GetComponent<Skeleton>()->GetLocalPose()[3].position.x, -3.0f, 1e-3f);
}

TEST_F(AnimationLayerFixture, MaskWithForeignRootRefused) {
	SkeletonAsset* skeleton = MakeSpine();
	AnimationLayer layer;

	EXPECT_FALSE(layer.SetMaskSubtree(*skeleton, "Sh0ulder", 1.0f));
	EXPECT_TRUE(layer.mask.empty());
	EXPECT_FLOAT_EQ(layer.WeightForBone(3), 1.0f);

	ASSERT_TRUE(layer.SetMaskSubtree(*skeleton, "Spine2", 1.0f));
	EXPECT_EQ(layer.mask.size(), 5u);
	EXPECT_FLOAT_EQ(layer.WeightForBone(0), 0.0f);
	EXPECT_FLOAT_EQ(layer.WeightForBone(2), 1.0f);
	EXPECT_FLOAT_EQ(layer.WeightForBone(4), 1.0f);

	layer.weight = 0.5f;
	EXPECT_FLOAT_EQ(layer.WeightForBone(99), 0.5f);
	EXPECT_FLOAT_EQ(layer.WeightForBone(-1), 0.5f);

	layer.ClearMask();
	EXPECT_FLOAT_EQ(layer.WeightForBone(0), 0.5f);
}

#endif
