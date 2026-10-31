#include "TestsPCH.h"

#include "ProductLookup.h"

#include "Asset/ImportArtifact.h"
#include "ModelImport.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics/Camera.h"
#include "Shalltear/Core/Graphics/SkeletonAsset.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/Skeleton.h"
#include "Shalltear/Core/Graphics/SkinnedMeshRenderer.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Reflection/Walk.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Scene/Prefab.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Settings.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <limits>

#ifndef SHALLTEAR_SHARED
#include "AnimationPass.h"
#include "ClipSampler.h"

#include "Shalltear/Animation/AnimationClip.h"
#include "Shalltear/Animation/Animator.h"
#endif

using namespace Shalltear;

namespace {

	constexpr const char* PACKAGE_TARGET = "Animation";
	constexpr f32 FIXED_STEP = 1.0f / 50.0f;

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

	bool SetHandleField(Object* object, StringHash fieldName, u64 instanceId) {
		const TypeInfo* type = object ? object->GetType() : nullptr;
		const FieldInfo* field = type ? FindField(type, fieldName) : nullptr;
		if (!field || !field->addrFn || (field->flags & FieldFlagHandle) == 0) {
			return false;
		}
		std::memcpy(field->addrFn(object), &instanceId, sizeof(u64));
		return true;
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

	void RunFixedSteps(u32 count) {
		FrameTime clockState;
		clockState.fixedDeltaTime = FIXED_STEP;
		clockState.maximumDeltaTime = 1000.0f;
		Time::FrameScope clock{clockState};
		Time::BeginFrame(FIXED_STEP);

		FrameStageArgs args;
		for (u32 index = 0; index < count; ++index) {
			FrameStages::Run(FrameStage::FixedUpdate, args);
		}
	}

} // anonymous namespace

#ifndef SHALLTEAR_SHARED

SLT_DECLARE_LINKED_PACKAGE(Animation)

namespace {

	AnimationClip::Track MakeTrack(AnimationTrackKind kind, AnimationInterpolation interp,
								   std::initializer_list<f32> times, std::initializer_list<Vec4> values) {
		AnimationClip::Track track;
		track.kind = kind;
		track.interp = interp;
		for (f32 time : times) {
			track.times.push_back(time);
		}
		for (const Vec4& value : values) {
			track.values.push_back(value);
		}
		return track;
	}

	f32 SampleX(const AnimationClip::Track& track, f32 time) {
		return AnimationImpl::SampleTrack(track, time).x;
	}

} // anonymous namespace

TEST(ClipSamplerTest, StepCurveHoldsKeyUntilNextOne) {
	AnimationClip::Track track = MakeTrack(AnimationTrackKind::BonePosition, AnimationInterpolation::Step,
										   {0.0f, 1.0f, 2.0f},
										   {Vec4(10.0f, 0, 0, 0), Vec4(20.0f, 0, 0, 0), Vec4(30.0f, 0, 0, 0)});
	EXPECT_FLOAT_EQ(SampleX(track, 0.0f), 10.0f);
	EXPECT_FLOAT_EQ(SampleX(track, 0.99f), 10.0f);
	EXPECT_FLOAT_EQ(SampleX(track, 1.0f), 20.0f);
	EXPECT_FLOAT_EQ(SampleX(track, 1.5f), 20.0f);
	EXPECT_FLOAT_EQ(SampleX(track, 2.0f), 30.0f);
}

TEST(ClipSamplerTest, LinearExactAtKeysClampedBeyond) {
	AnimationClip::Track track = MakeTrack(AnimationTrackKind::BonePosition, AnimationInterpolation::Linear,
										   {0.2f, 1.0f}, {Vec4(0.0f, 0, 0, 0), Vec4(8.0f, 0, 0, 0)});
	EXPECT_FLOAT_EQ(SampleX(track, -5.0f), 0.0f);
	EXPECT_FLOAT_EQ(SampleX(track, 0.2f), 0.0f);
	EXPECT_FLOAT_EQ(SampleX(track, 0.6f), 4.0f);
	EXPECT_FLOAT_EQ(SampleX(track, 1.0f), 8.0f);
	EXPECT_FLOAT_EQ(SampleX(track, 99.0f), 8.0f);
}

TEST(ClipSamplerTest, SingleKeyConstantEmptyIs0) {
	AnimationClip::Track single = MakeTrack(AnimationTrackKind::BonePosition, AnimationInterpolation::Linear,
											{0.5f}, {Vec4(3.0f, 0, 0, 0)});
	EXPECT_FLOAT_EQ(SampleX(single, -1.0f), 3.0f);
	EXPECT_FLOAT_EQ(SampleX(single, 0.5f), 3.0f);
	EXPECT_FLOAT_EQ(SampleX(single, 900.0f), 3.0f);

	AnimationClip::Track empty = MakeTrack(AnimationTrackKind::BonePosition, AnimationInterpolation::Linear, {}, {});
	EXPECT_FLOAT_EQ(SampleX(empty, 0.0f), 0.0f);

	AnimationClip::Track ragged = MakeTrack(AnimationTrackKind::BonePosition, AnimationInterpolation::Linear,
											{0.0f, 1.0f, 2.0f}, {Vec4(5.0f, 0, 0, 0)});
	EXPECT_FLOAT_EQ(SampleX(ragged, 1.5f), 5.0f);
}

TEST(ClipSamplerTest, CubicTangentsScaleWithSegment) {
	AnimationClip::Track track = MakeTrack(AnimationTrackKind::BonePosition, AnimationInterpolation::CubicSpline,
										   {0.0f, 2.0f},
										   {Vec4(0, 0, 0, 0), Vec4(0, 0, 0, 0), Vec4(1.0f, 0, 0, 0),
											Vec4(1.0f, 0, 0, 0), Vec4(0, 0, 0, 0), Vec4(0, 0, 0, 0)});
	EXPECT_FLOAT_EQ(SampleX(track, 0.0f), 0.0f);
	EXPECT_FLOAT_EQ(SampleX(track, 2.0f), 0.0f);

	EXPECT_NEAR(SampleX(track, 1.0f), 0.0f, 1e-5f);

	AnimationClip::Track oneSided = MakeTrack(AnimationTrackKind::BonePosition, AnimationInterpolation::CubicSpline,
											  {0.0f, 2.0f},
											  {Vec4(0, 0, 0, 0), Vec4(0, 0, 0, 0), Vec4(1.0f, 0, 0, 0),
											   Vec4(0, 0, 0, 0), Vec4(0, 0, 0, 0), Vec4(0, 0, 0, 0)});
	EXPECT_NEAR(SampleX(oneSided, 1.0f), 0.25f, 1e-5f);
}

TEST(ClipSamplerTest, RotationUnitLengthHalfwayAtMidpoint) {
	const Quat start = Quat::AngleAxis(0.0f, Vec3::Up());
	const Quat end = Quat::AngleAxis(170.0f, Vec3::Up());
	AnimationClip::Track track = MakeTrack(AnimationTrackKind::BoneRotation, AnimationInterpolation::Linear,
										   {0.0f, 1.0f},
										   {Vec4(start.x, start.y, start.z, start.w), Vec4(end.x, end.y, end.z, end.w)});

	const Vec4 sampled = AnimationImpl::SampleTrack(track, 0.5f);
	const Quat mid(sampled.x, sampled.y, sampled.z, sampled.w);
	EXPECT_NEAR(mid.LengthSquared(), 1.0f, 1e-4f);
	EXPECT_NEAR(Quat::Angle(start, mid), 85.0f, 0.5f);

	const Quat far = Quat::AngleAxis(350.0f, Vec3::Up());
	AnimationClip::Track wide = MakeTrack(AnimationTrackKind::BoneRotation, AnimationInterpolation::Linear,
										  {0.0f, 1.0f},
										  {Vec4(start.x, start.y, start.z, start.w), Vec4(far.x, far.y, far.z, far.w)});
	const Vec4 wideSample = AnimationImpl::SampleTrack(wide, 0.5f);
	const Quat wideMid(wideSample.x, wideSample.y, wideSample.z, wideSample.w);
	EXPECT_NEAR(Quat::Angle(start, wideMid), 5.0f, 0.5f);
}

TEST(ClipSamplerTest, ACubicRotationIsRenormalized) {
	AnimationClip::Track track = MakeTrack(AnimationTrackKind::BoneRotation, AnimationInterpolation::CubicSpline,
										   {0.0f, 1.0f},
										   {Vec4(0, 0, 0, 0), Vec4(0, 0, 0, 1.0f), Vec4(0.9f, 0, 0, 0),
											Vec4(0.9f, 0, 0, 0), Vec4(0, 0.7071f, 0, 0.7071f), Vec4(0, 0, 0, 0)});
	const Vec4 sampled = AnimationImpl::SampleTrack(track, 0.5f);
	const Quat mid(sampled.x, sampled.y, sampled.z, sampled.w);
	EXPECT_NEAR(mid.LengthSquared(), 1.0f, 1e-4f);
}

TEST(ClipSamplerTest, LoopWrapsPingPongReverses) {
	constexpr f32 LENGTH = 2.0f;
	using AnimationImpl::WrapTime;

	EXPECT_FLOAT_EQ(WrapTime(0.5f, LENGTH, AnimationLoopMode::None), 0.5f);
	EXPECT_FLOAT_EQ(WrapTime(5.0f, LENGTH, AnimationLoopMode::None), 2.0f);
	EXPECT_FLOAT_EQ(WrapTime(-5.0f, LENGTH, AnimationLoopMode::None), 0.0f);

	EXPECT_FLOAT_EQ(WrapTime(2.5f, LENGTH, AnimationLoopMode::Loop), 0.5f);
	EXPECT_FLOAT_EQ(WrapTime(-0.5f, LENGTH, AnimationLoopMode::Loop), 1.5f);

	EXPECT_FLOAT_EQ(WrapTime(0.5f, LENGTH, AnimationLoopMode::PingPong), 0.5f);
	EXPECT_FLOAT_EQ(WrapTime(2.5f, LENGTH, AnimationLoopMode::PingPong), 1.5f);
	EXPECT_FLOAT_EQ(WrapTime(3.5f, LENGTH, AnimationLoopMode::PingPong), 0.5f);
	EXPECT_FLOAT_EQ(WrapTime(-0.5f, LENGTH, AnimationLoopMode::PingPong), 0.5f);

	EXPECT_FLOAT_EQ(WrapTime(1.0f, 0.0f, AnimationLoopMode::Loop), 0.0f);
	EXPECT_FLOAT_EQ(WrapTime(std::numeric_limits<f32>::quiet_NaN(), LENGTH, AnimationLoopMode::Loop), 0.0f);
}

namespace {

	class AnimationPlaybackFixture : public ::testing::Test {
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
					("shalltear_animation_play_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
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

		SkeletonAsset* MakeSkeleton() {
			SkeletonAsset* skeleton = New<SkeletonAsset>(MemTag::Asset);
			skeleton->boneNames.push_back("Root");
			skeleton->parents.push_back(-1);
			skeleton->restPose.push_back(BoneTransform{});

			skeleton->boneNames.push_back("Tip");
			skeleton->parents.push_back(0);
			BoneTransform tip;
			tip.position = Vec3(0.0f, 1.0f, 0.0f);
			tip.scale = Vec3(2.0f, 3.0f, 4.0f);
			skeleton->restPose.push_back(tip);

			_owned.push_back(skeleton);
			return skeleton;
		}

		AnimationClip* MakeClip() {
			AnimationClip* clip = New<AnimationClip>(MemTag::Asset);
			clip->virtualPath = "project://test.clip";
			_owned.push_back(clip);
			return clip;
		}

		Handle<SceneObject> MakeRig(SkeletonAsset* skeleton) {
			Handle<SceneObject> object = SceneSystem::CreateObject("rig", _scene);
			Handle<Skeleton> component = object->AddComponent<Skeleton>();
			component->SetSkeleton(Handle<SkeletonAsset>(skeleton));
			return object;
		}

		Handle<SceneObject> MakeRigWithExposedTip(SkeletonAsset* skeleton, Handle<SceneObject>& outTip) {
			Handle<SceneObject> object = MakeRig(skeleton);
			outTip = SceneSystem::CreateObject("Tip", _scene);
			outTip->SetParent(object);
			Handle<Skeleton> component = object->GetComponent<Skeleton>();
			component->AddExposedBone(1, outTip);
			return object;
		}
	};

	void AddPositionTrack(AnimationClip& clip, std::string_view bone, f32 to) {
		AnimationClip::Track track;
		track.kind = AnimationTrackKind::BonePosition;
		track.interp = AnimationInterpolation::Linear;
		track.name = str(bone);
		track.times.push_back(0.0f);
		track.times.push_back(1.0f);
		track.values.push_back(Vec4(0.0f, 0.0f, 0.0f, 0.0f));
		track.values.push_back(Vec4(to, 0.0f, 0.0f, 0.0f));
		clip.tracks.push_back(std::move(track));
		clip.length = 1.0f;
	}

} // anonymous namespace

TEST_F(AnimationPlaybackFixture, ClipDrivesOnlyNamedBones) {
	SkeletonAsset* skeleton = MakeSkeleton();
	AnimationClip* clip = MakeClip();
	AddPositionTrack(*clip, "Tip", 4.0f);

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(clip));

	RunUpdate(0.5f);

	Handle<Skeleton> pose = rig->GetComponent<Skeleton>();
	ASSERT_EQ(pose->GetLocalPose().size(), 2u);
	EXPECT_NEAR(pose->GetLocalPose()[1].position.x, 2.0f, 1e-4f);
	EXPECT_NEAR(pose->GetLocalPose()[1].scale.x, 2.0f, 1e-4f);
	EXPECT_NEAR(pose->GetLocalPose()[1].scale.y, 3.0f, 1e-4f);
	EXPECT_NEAR(pose->GetLocalPose()[1].scale.z, 4.0f, 1e-4f);
	EXPECT_NEAR(pose->GetLocalPose()[0].position.x, 0.0f, 1e-4f);
	EXPECT_EQ(animator->GetBoundTrackCount(), 1u);
}

TEST_F(AnimationPlaybackFixture, BoneReachesExposedObjectUnasked) {
	SkeletonAsset* skeleton = MakeSkeleton();
	AnimationClip* clip = MakeClip();
	AddPositionTrack(*clip, "Tip", 4.0f);

	Handle<SceneObject> tip;
	Handle<SceneObject> rig = MakeRigWithExposedTip(skeleton, tip);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(clip));

	RunUpdate(0.25f);

	EXPECT_NEAR(tip->GetLocalPosition().x, 1.0f, 1e-4f);
	Handle<Skeleton> pose = rig->GetComponent<Skeleton>();
	ASSERT_EQ(pose->GetModelPose().size(), 2u);
	EXPECT_NEAR(pose->GetModelPose()[1](0, 3), 1.0f, 1e-4f);
}

TEST_F(AnimationPlaybackFixture, TrackForMissingBoneDropped) {
	SkeletonAsset* skeleton = MakeSkeleton();
	AnimationClip* clip = MakeClip();
	AddPositionTrack(*clip, "NoSuchBone", 9.0f);
	AddPositionTrack(*clip, "Tip", 4.0f);

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(clip));

	RunUpdate(1.0f);

	EXPECT_EQ(animator->GetBoundTrackCount(), 1u);
	Handle<Skeleton> pose = rig->GetComponent<Skeleton>();
	EXPECT_NEAR(pose->GetLocalPose()[1].position.x, 4.0f, 1e-4f);
}

TEST_F(AnimationPlaybackFixture, NoSkeletonBindsNothing) {
	AnimationClip* clip = MakeClip();
	AddPositionTrack(*clip, "Tip", 4.0f);

	Handle<SceneObject> bare = SceneSystem::CreateObject("bare", _scene);
	Handle<Animator> animator = bare->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(clip));

	RunUpdate(0.5f);
	EXPECT_EQ(animator->GetBoundTrackCount(), 0u);
	EXPECT_TRUE(animator->IsPlaying());
	EXPECT_NEAR(animator->GetTime(), 0.5f, 1e-5f);
}

TEST_F(AnimationPlaybackFixture, UpdateModesTickInTheirStages) {
	SkeletonAsset* skeleton = MakeSkeleton();
	AnimationClip* clip = MakeClip();
	AddPositionTrack(*clip, "Tip", 1.0f);
	clip->loop = AnimationLoopMode::Loop;

	Handle<SceneObject> normalRig = MakeRig(skeleton);
	Handle<Animator> normal = normalRig->AddComponent<Animator>();
	normal->SetClip(Handle<AnimationClip>(clip));

	Handle<SceneObject> physicsRig = MakeRig(skeleton);
	Handle<Animator> physics = physicsRig->AddComponent<Animator>();
	physics->SetClip(Handle<AnimationClip>(clip));
	physics->SetUpdateMode(AnimatorUpdateMode::AnimatePhysics);

	RunUpdate(0.25f);
	EXPECT_NEAR(normal->GetTime(), 0.25f, 1e-5f);
	EXPECT_NEAR(physics->GetTime(), 0.0f, 1e-5f);

	RunFixedSteps(2);
	EXPECT_NEAR(normal->GetTime(), 0.25f, 1e-5f);
	EXPECT_NEAR(physics->GetTime(), 2.0f * FIXED_STEP, 1e-5f);
}

TEST_F(AnimationPlaybackFixture, StepFrameAdvancesAnimationByOneStep) {
	SkeletonAsset* skeleton = MakeSkeleton();
	AnimationClip* clip = MakeClip();
	AddPositionTrack(*clip, "Tip", 10.0f);
	clip->loop = AnimationLoopMode::Loop;

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(clip));
	animator->SetUpdateMode(AnimatorUpdateMode::AnimatePhysics);

	RunFixedSteps(1);
	EXPECT_NEAR(animator->GetTime(), FIXED_STEP, 1e-6f);
	Handle<Skeleton> pose = rig->GetComponent<Skeleton>();
	EXPECT_NEAR(pose->GetLocalPose()[1].position.x, 10.0f * FIXED_STEP, 1e-4f);
}

TEST_F(AnimationPlaybackFixture, UnscaledAnimatorIgnoresTimeScale) {
	SkeletonAsset* skeleton = MakeSkeleton();
	AnimationClip* clip = MakeClip();
	AddPositionTrack(*clip, "Tip", 1.0f);
	clip->loop = AnimationLoopMode::Loop;

	Handle<SceneObject> normalRig = MakeRig(skeleton);
	Handle<Animator> normal = normalRig->AddComponent<Animator>();
	normal->SetClip(Handle<AnimationClip>(clip));

	Handle<SceneObject> unscaledRig = MakeRig(skeleton);
	Handle<Animator> unscaled = unscaledRig->AddComponent<Animator>();
	unscaled->SetClip(Handle<AnimationClip>(clip));
	unscaled->SetUpdateMode(AnimatorUpdateMode::UnscaledTime);

	FrameTime clockState;
	clockState.fixedDeltaTime = FIXED_STEP;
	clockState.maximumDeltaTime = 1000.0f;
	clockState.timeScale = 0.25f;
	{
		Time::FrameScope clock{clockState};
		Time::BeginFrame(1.0);
		FrameStageArgs args;
		FrameStages::Run(FrameStage::Update, args);
	}

	EXPECT_NEAR(normal->GetTime(), 0.25f, 1e-5f);
	EXPECT_NEAR(unscaled->GetTime(), 1.0f, 1e-5f);
}

TEST_F(AnimationPlaybackFixture, SharedClipEditsReachBoth) {
	SkeletonAsset* skeleton = MakeSkeleton();
	AnimationClip* clip = MakeClip();
	AddPositionTrack(*clip, "Tip", 4.0f);
	clip->loop = AnimationLoopMode::Loop;

	Handle<SceneObject> firstRig = MakeRig(skeleton);
	Handle<Animator> first = firstRig->AddComponent<Animator>();
	first->SetClip(Handle<AnimationClip>(clip));

	Handle<SceneObject> secondRig = MakeRig(skeleton);
	Handle<Animator> second = secondRig->AddComponent<Animator>();
	second->SetClip(Handle<AnimationClip>(clip));

	RunUpdate(0.5f);
	EXPECT_NEAR(firstRig->GetComponent<Skeleton>()->GetLocalPose()[1].position.x, 2.0f, 1e-4f);
	EXPECT_NEAR(secondRig->GetComponent<Skeleton>()->GetLocalPose()[1].position.x, 2.0f, 1e-4f);

	clip->tracks[0].values[1] = Vec4(9.0f, 0.0f, 0.0f, 0.0f);
	first->SetTime(0.5f);
	second->SetTime(0.5f);
	RunUpdate(0.0f);
	EXPECT_NEAR(firstRig->GetComponent<Skeleton>()->GetLocalPose()[1].position.x, 4.5f, 1e-4f);
	EXPECT_NEAR(secondRig->GetComponent<Skeleton>()->GetLocalPose()[1].position.x, 4.5f, 1e-4f);

	AnimationClip::Track inserted;
	inserted.kind = AnimationTrackKind::BonePosition;
	inserted.interp = AnimationInterpolation::Linear;
	inserted.name = "Root";
	inserted.times.push_back(0.0f);
	inserted.times.push_back(1.0f);
	inserted.values.push_back(Vec4(0.0f, 0.0f, 0.0f, 0.0f));
	inserted.values.push_back(Vec4(-3.0f, 0.0f, 0.0f, 0.0f));
	clip->tracks.insert(clip->tracks.begin(), std::move(inserted));
	++clip->generation;

	first->SetTime(0.5f);
	second->SetTime(0.5f);
	RunUpdate(0.0f);
	for (Handle<SceneObject> rig : {firstRig, secondRig}) {
		Handle<Skeleton> pose = rig->GetComponent<Skeleton>();
		EXPECT_NEAR(pose->GetLocalPose()[0].position.x, -1.5f, 1e-4f);
		EXPECT_NEAR(pose->GetLocalPose()[1].position.x, 4.5f, 1e-4f);
	}
	EXPECT_EQ(first->GetBoundTrackCount(), 2u);
	EXPECT_EQ(second->GetBoundTrackCount(), 2u);
}

TEST_F(AnimationPlaybackFixture, StalePlayHandleSteersNothing) {
	SkeletonAsset* skeleton = MakeSkeleton();
	AnimationClip* first = MakeClip();
	AddPositionTrack(*first, "Tip", 4.0f);
	first->loop = AnimationLoopMode::Loop;
	AnimationClip* second = MakeClip();
	AddPositionTrack(*second, "Tip", 8.0f);
	second->loop = AnimationLoopMode::Loop;

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();

	PlayHandle handle = animator->Play(Handle<AnimationClip>(first));
	EXPECT_TRUE(handle.IsValid());
	EXPECT_EQ(handle.GetClip().GetInstanceId(), first->GetInstanceId());

	handle.SetSpeed(2.0f);
	EXPECT_FLOAT_EQ(animator->GetLayer(0)->speed, 2.0f);
	EXPECT_FLOAT_EQ(animator->GetSpeed(), 1.0f);

	PlayHandle replaced = animator->Play(Handle<AnimationClip>(second));
	EXPECT_TRUE(replaced.IsValid());
	EXPECT_FALSE(handle.IsValid());

	handle.SetSpeed(0.5f);
	EXPECT_FLOAT_EQ(animator->GetLayer(0)->speed, 2.0f);
	handle.Stop();
	EXPECT_TRUE(animator->IsPlaying());

	SceneSystem::Destroy(rig);
	SceneSystem::EndFrame();
	EXPECT_FALSE(replaced.IsValid());
	EXPECT_FLOAT_EQ(replaced.GetSpeed(), 0.0f);
}

TEST_F(AnimationPlaybackFixture, NonLoopingHoldsLastPose) {
	SkeletonAsset* skeleton = MakeSkeleton();
	AnimationClip* clip = MakeClip();
	AddPositionTrack(*clip, "Tip", 4.0f);

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(clip));

	RunUpdate(5.0f);
	EXPECT_FALSE(animator->IsPlaying());
	EXPECT_NEAR(animator->GetTime(), 1.0f, 1e-5f);
	EXPECT_NEAR(rig->GetComponent<Skeleton>()->GetLocalPose()[1].position.x, 4.0f, 1e-4f);

	RunUpdate(5.0f);
	EXPECT_NEAR(rig->GetComponent<Skeleton>()->GetLocalPose()[1].position.x, 4.0f, 1e-4f);
}

TEST_F(AnimationPlaybackFixture, FieldPathAndBonePathAgree) {
	SkeletonAsset* skeleton = MakeSkeleton();
	AnimationClip* clip = MakeClip();
	AddPositionTrack(*clip, "Tip", 4.0f);
	clip->loop = AnimationLoopMode::Loop;

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<SceneObject> child = SceneSystem::CreateObject("Lens", _scene);
	child->SetParent(rig);
	child->AddComponent<Camera>();

	AnimationClip::Track property;
	property.kind = AnimationTrackKind::Property;
	property.interp = AnimationInterpolation::Linear;
	property.objectPath = "Lens";
	property.componentType = StringHash("Camera");
	property.name = "_fieldOfViewDeg";
	property.times.push_back(0.0f);
	property.times.push_back(1.0f);
	property.values.push_back(Vec4(0.0f, 0.0f, 0.0f, 0.0f));
	property.values.push_back(Vec4(4.0f, 0.0f, 0.0f, 0.0f));
	clip->tracks.push_back(std::move(property));

	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(clip));
	ASSERT_EQ(animator->GetBoundTrackCount(), 0u);

	RunUpdate(0.375f);
	ASSERT_EQ(animator->GetBoundTrackCount(), 2u);

	const f32 fast = rig->GetComponent<Skeleton>()->GetLocalPose()[1].position.x;
	const f32 general = child->GetComponent<Camera>()->GetFieldOfViewDeg();
	EXPECT_NEAR(fast, 1.5f, 1e-4f);
	EXPECT_NEAR(general, fast, 1e-4f);
}

TEST_F(AnimationPlaybackFixture, TransformFieldMovesWorldMatrix) {
	AnimationClip* clip = MakeClip();
	AnimationClip::Track track;
	track.kind = AnimationTrackKind::Property;
	track.interp = AnimationInterpolation::Linear;
	track.componentType = StringHash{};
	track.name = "_localPosition";
	track.times.push_back(0.0f);
	track.times.push_back(1.0f);
	track.values.push_back(Vec4(0.0f, 0.0f, 0.0f, 0.0f));
	track.values.push_back(Vec4(6.0f, 0.0f, 0.0f, 0.0f));
	clip->tracks.push_back(std::move(track));
	clip->length = 1.0f;

	Handle<SceneObject> object = SceneSystem::CreateObject("mover", _scene);
	Handle<SceneObject> child = SceneSystem::CreateObject("carried", _scene);
	child->SetParent(object);
	ASSERT_NEAR(object->GetWorldMatrix()(0, 3), 0.0f, 1e-5f);
	ASSERT_NEAR(child->GetWorldMatrix()(0, 3), 0.0f, 1e-5f);

	Handle<Animator> animator = object->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(clip));
	RunUpdate(0.5f);

	EXPECT_NEAR(object->GetLocalPosition().x, 3.0f, 1e-4f);
	EXPECT_NEAR(object->GetWorldMatrix()(0, 3), 3.0f, 1e-4f);
	EXPECT_NEAR(child->GetWorldMatrix()(0, 3), 3.0f, 1e-4f);
}

TEST_F(AnimationPlaybackFixture, ScalarCurveWritesOneChannel) {
	AnimationClip* clip = MakeClip();
	AnimationClip::Track track;
	track.kind = AnimationTrackKind::Property;
	track.interp = AnimationInterpolation::Linear;
	track.componentType = StringHash{};
	track.name = "_localPosition";
	track.component = 1;
	track.times.push_back(0.0f);
	track.times.push_back(1.0f);
	track.values.push_back(Vec4(0.0f, 0.0f, 0.0f, 0.0f));
	track.values.push_back(Vec4(5.0f, 0.0f, 0.0f, 0.0f));
	clip->tracks.push_back(std::move(track));
	clip->length = 1.0f;

	Handle<SceneObject> object = SceneSystem::CreateObject("mover", _scene);
	object->SetLocalPosition(Vec3(7.0f, 0.0f, -2.0f));
	Handle<Animator> animator = object->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(clip));

	RunUpdate(1.0f);
	EXPECT_NEAR(object->GetLocalPosition().y, 5.0f, 1e-4f);
	EXPECT_NEAR(object->GetLocalPosition().x, 7.0f, 1e-4f);
	EXPECT_NEAR(object->GetLocalPosition().z, -2.0f, 1e-4f);
}

TEST_F(AnimationPlaybackFixture, PassTracksAnimatorEnableState) {
	SkeletonAsset* skeleton = MakeSkeleton();
	AnimationClip* clip = MakeClip();
	AddPositionTrack(*clip, "Tip", 4.0f);
	clip->loop = AnimationLoopMode::Loop;

	const u32 before = AnimationImpl::GetRegisteredAnimatorCount();

	Handle<SceneObject> firstRig = MakeRig(skeleton);
	Handle<Animator> first = firstRig->AddComponent<Animator>();
	first->SetClip(Handle<AnimationClip>(clip));
	Handle<SceneObject> secondRig = MakeRig(skeleton);
	Handle<Animator> second = secondRig->AddComponent<Animator>();
	second->SetClip(Handle<AnimationClip>(clip));
	EXPECT_EQ(AnimationImpl::GetRegisteredAnimatorCount(), before + 2u);

	second->SetEnabled(false);
	EXPECT_EQ(AnimationImpl::GetRegisteredAnimatorCount(), before + 1u);
	second->SetEnabled(true);
	EXPECT_EQ(AnimationImpl::GetRegisteredAnimatorCount(), before + 2u);

	SceneSystem::Destroy(secondRig);
	SceneSystem::EndFrame();
	EXPECT_EQ(AnimationImpl::GetRegisteredAnimatorCount(), before + 1u);

	RunUpdate(0.5f);
	EXPECT_NEAR(firstRig->GetComponent<Skeleton>()->GetLocalPose()[1].position.x, 2.0f, 1e-4f);
}

TEST_F(AnimationPlaybackFixture, ClipCookRoundTripNewGeneration) {
	AnimationClip* clip = MakeClip();
	AddPositionTrack(*clip, "Tip", 4.0f);
	clip->loop = AnimationLoopMode::PingPong;
	clip->frameRate = 24.0f;
	clip->skeletonGuid = Guid{0x1234, 0x5678};
	AnimationClip::Track cubic = MakeTrack(AnimationTrackKind::BoneRotation, AnimationInterpolation::CubicSpline,
										   {0.0f, 0.5f},
										   {Vec4(1, 0, 0, 0), Vec4(0, 0, 0, 1), Vec4(2, 0, 0, 0),
											Vec4(3, 0, 0, 0), Vec4(0, 1, 0, 0), Vec4(4, 0, 0, 0)});
	cubic.objectPath = "Lens";
	cubic.componentType = StringHash("Camera");
	cubic.name = "fieldOfViewDeg";
	cubic.component = 2;
	clip->tracks.push_back(std::move(cubic));

	Vector<u8> descriptorBytes{GetMemoryResource(MemTag::Asset)};
	Vector<u8> payloadBytes{GetMemoryResource(MemTag::Asset)};
	PakStringPool pool;
	CookWriter descriptorWriter(descriptorBytes, pool);
	CookWriter payloadWriter(payloadBytes, pool);
	ASSERT_TRUE(clip->Cook(descriptorWriter, payloadWriter));

	Vector<u8> stringBytes{GetMemoryResource(MemTag::Asset)};
	PakWriteStringBlob(stringBytes, pool.Texts());
	PakStringTable table;
	ASSERT_TRUE(PakReadStringBlob(stringBytes.data(), stringBytes.size(), table));

	AnimationClip* loaded = MakeClip();
	const u32 generationBefore = loaded->generation;
	CookReader descriptorReader(descriptorBytes.data(), descriptorBytes.size(), table);
	CookReader payloadReader(payloadBytes.data(), payloadBytes.size(), table);
	ASSERT_TRUE(loaded->LoadCooked(descriptorReader, payloadReader));

	EXPECT_EQ(loaded->skeletonGuid, clip->skeletonGuid);
	EXPECT_FLOAT_EQ(loaded->length, clip->length);
	EXPECT_FLOAT_EQ(loaded->frameRate, 24.0f);
	EXPECT_EQ(loaded->loop, AnimationLoopMode::PingPong);
	ASSERT_EQ(loaded->tracks.size(), 2u);
	EXPECT_EQ(loaded->tracks[0].name, "Tip");
	EXPECT_EQ(loaded->tracks[1].kind, AnimationTrackKind::BoneRotation);
	EXPECT_EQ(loaded->tracks[1].interp, AnimationInterpolation::CubicSpline);
	EXPECT_EQ(loaded->tracks[1].objectPath, "Lens");
	EXPECT_EQ(loaded->tracks[1].componentType, StringHash("Camera"));
	EXPECT_EQ(loaded->tracks[1].component, 2);
	ASSERT_EQ(loaded->tracks[1].times.size(), 2u);
	ASSERT_EQ(loaded->tracks[1].values.size(), 6u);
	EXPECT_FLOAT_EQ(loaded->tracks[1].values[2].x, 2.0f);
	EXPECT_GT(loaded->generation, generationBefore);
}

TEST_F(AnimationPlaybackFixture, AuthoredClipRoundTripsThroughAnimFile) {
	AnimationClip* clip = MakeClip();
	AddPositionTrack(*clip, "Tip", 4.0f);
	clip->loop = AnimationLoopMode::PingPong;
	clip->frameRate = 24.0f;
	clip->additiveReferenceTime = 0.25f;
	clip->bakeRootMotionIntoPose = false;
	clip->skeletonGuid = Guid{0x1234, 0x5678};

	AnimationClip::Track cubic = MakeTrack(AnimationTrackKind::Property, AnimationInterpolation::CubicSpline,
										   {0.0f, 0.5f},
										   {Vec4(1, 0, 0, 0), Vec4(0, 0, 0, 1), Vec4(2, 0, 0, 0),
											Vec4(3, 0, 0, 0), Vec4(0, 1, 0, 0), Vec4(4, 0, 0, 0)});
	cubic.objectPath = "Lens";
	cubic.componentType = StringHash("Camera");
	cubic.name = "fieldOfViewDeg";
	cubic.component = 2;
	clip->tracks.push_back(std::move(cubic));

	AnimationClip::Track slider = MakeTrack(AnimationTrackKind::BlendShape, AnimationInterpolation::Linear, {0.0f, 1.0f},
											{Vec4(0, 0, 0, 0), Vec4(1, 0, 0, 0)});
	slider.objectPath = "Face";
	slider.name = "Smile";
	clip->tracks.push_back(std::move(slider));

	const Path file((_root / "authored.anim").generic_string());
	ASSERT_TRUE(clip->Export(file));

	AnimationClip* loaded = MakeClip();
	const u32 generationBefore = loaded->generation;
	ASSERT_TRUE(loaded->Import(file));

	EXPECT_FLOAT_EQ(loaded->length, clip->length);
	EXPECT_FLOAT_EQ(loaded->frameRate, 24.0f);
	EXPECT_EQ(loaded->loop, AnimationLoopMode::PingPong);
	EXPECT_FLOAT_EQ(loaded->additiveReferenceTime, 0.25f);
	EXPECT_FALSE(loaded->bakeRootMotionIntoPose);
	EXPECT_EQ(loaded->skeletonGuid, (Guid{0x1234, 0x5678}));

	ASSERT_EQ(loaded->tracks.size(), 3u);
	EXPECT_EQ(loaded->tracks[0].kind, AnimationTrackKind::BonePosition);
	EXPECT_EQ(loaded->tracks[0].name, "Tip");
	EXPECT_EQ(loaded->tracks[0].componentType, StringHash(static_cast<u64>(0)));

	const AnimationClip::Track& property = loaded->tracks[1];
	EXPECT_EQ(property.kind, AnimationTrackKind::Property);
	EXPECT_EQ(property.interp, AnimationInterpolation::CubicSpline);
	EXPECT_EQ(property.objectPath, "Lens");
	EXPECT_EQ(property.componentType, StringHash("Camera"));
	EXPECT_EQ(property.name, "fieldOfViewDeg");
	EXPECT_EQ(property.component, 2);
	ASSERT_EQ(property.times.size(), 2u);
	ASSERT_EQ(property.values.size(), 6u);
	EXPECT_FLOAT_EQ(property.times[1], 0.5f);
	EXPECT_FLOAT_EQ(property.values[2].x, 2.0f);
	EXPECT_FLOAT_EQ(property.values[4].y, 1.0f);
	EXPECT_EQ(loaded->tracks[2].kind, AnimationTrackKind::BlendShape);
	EXPECT_EQ(loaded->tracks[2].name, "Smile");

	EXPECT_GT(loaded->generation, generationBefore);
}

TEST_F(AnimationPlaybackFixture, AnimValueKeyMismatchRefused) {
	const Path file((_root / "broken.anim").generic_string());
	ASSERT_TRUE(FileSystem::WriteTextFile(file, R"({
		"length": 1.0, "frameRate": 60.0, "loop": "None",
		"tracks": [{
			"kind": "BonePosition", "interp": "Linear", "component": 255,
			"objectPath": "", "componentType": "", "name": "Tip",
			"times": [0.0, 1.0],
			"values": [[0,0,0,0]]
		}]
	})"));

	AnimationClip* clip = MakeClip();
	EXPECT_FALSE(clip->Import(file));
	EXPECT_TRUE(clip->tracks.empty());

	const Path future((_root / "future.anim").generic_string());
	ASSERT_TRUE(FileSystem::WriteTextFile(future, R"({
		"length": 1.0, "tracks": [{
			"kind": "BonePosition", "interp": "Bezier", "component": 255,
			"objectPath": "", "componentType": "", "name": "Tip",
			"times": [0.0], "values": [[0,0,0,0]]
		}]
	})"));
	EXPECT_FALSE(clip->Import(future));
}

TEST_F(AnimationPlaybackFixture, EmptyAnimFileIsEmptyClip) {
	const Path file((_root / "empty.anim").generic_string());
	ASSERT_TRUE(FileSystem::WriteTextFile(file, ""));
	AnimationClip* clip = MakeClip();
	EXPECT_TRUE(clip->Import(file));
	EXPECT_TRUE(clip->tracks.empty());
}

class AnimationClipCreateFixture : public ::testing::Test {
protected:
	std::filesystem::path _root;
	SettingsPaths _paths;
	char _token = 0;

	void SetUp() override {
		static std::atomic<u64> counter{0};
		u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
		_root = std::filesystem::temp_directory_path() /
				("shalltear_anim_create_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
		std::filesystem::create_directories(_root / "Assets");
		FileSystem::Mount("project", Path(_root.generic_string()));
		_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
		_paths.user = Path((_root / "UserSettings.ini").generic_string());
		_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());

		TypeRegistry::InitializePrimitives();
		Settings::Initialize(_paths);
		SceneSystem::Initialize();
		ModuleSystem::Initialize();
		SLT_CALL_LINKED_PACKAGE(Animation, &_token);
		AssetDatabase::Initialize();
	}

	void TearDown() override {
		AssetDatabase::Terminate();
		SceneSystem::Terminate();
		FrameStages::UnregisterOwnedBy(&_token);
		ModuleSystem::Terminate();
		Settings::Terminate();
		FileSystem::Unmount("project");
		std::error_code ec;
		std::filesystem::remove_all(_root, ec);
	}
};

TEST_F(AnimationClipCreateFixture, NewClipImportsWithFrameRate) {
	const Path virtualPath("project://Assets/Slide.anim");
	const Guid guid = AssetDatabase::CreateAsset(virtualPath, StringHash("AnimationClip"));
	ASSERT_TRUE(guid.IsValid());
	EXPECT_TRUE(FileSystem::Exists(FileSystem::Resolve(virtualPath)));

	Asset* asset = AssetDatabase::LoadAssetByGuid(guid);
	ASSERT_NE(asset, nullptr);
	auto* clip = static_cast<AnimationClip*>(asset);

	EXPECT_FLOAT_EQ(clip->frameRate, 60.0f);
	EXPECT_FLOAT_EQ(clip->length, 0.0f);
	EXPECT_TRUE(clip->tracks.empty());
	EXPECT_EQ(clip->loop, AnimationLoopMode::None);
	AssetDatabase::UnloadAsset(asset);

	EXPECT_FALSE(AssetDatabase::CreateAsset(virtualPath, StringHash("AnimationClip")).IsValid());
}

namespace {

	MeshAsset* MakeSliderMesh(std::vector<Asset*>& owned, std::string_view first, std::string_view second) {
		MeshAsset* mesh = New<MeshAsset>(MemTag::Asset);
		MeshVertex vertex;
		mesh->vertices.push_back(vertex);
		for (std::string_view name : {first, second}) {
			MeshBlendShape shape;
			shape.name = str(name);
			MeshBlendShapeFrame frame;
			frame.weight = 100.0f;
			frame.vertices.push_back(0);
			frame.positionDeltas.push_back(Vec3(0.0f, 1.0f, 0.0f));
			shape.frames.push_back(std::move(frame));
			mesh->blendShapes.push_back(std::move(shape));
		}
		owned.push_back(mesh);
		return mesh;
	}

	void AddBlendShapeTrack(AnimationClip& clip, std::string_view shape, f32 to) {
		AnimationClip::Track track;
		track.kind = AnimationTrackKind::BlendShape;
		track.interp = AnimationInterpolation::Linear;
		track.name = str(shape);
		track.times.push_back(0.0f);
		track.times.push_back(1.0f);
		track.values.push_back(Vec4(0.0f, 0.0f, 0.0f, 0.0f));
		track.values.push_back(Vec4(to, 0.0f, 0.0f, 0.0f));
		clip.tracks.push_back(std::move(track));
		clip.length = 1.0f;
	}

} // anonymous namespace

TEST_F(AnimationPlaybackFixture, ClipDrivesOnlyNamedShapes) {
	SkeletonAsset* skeleton = MakeSkeleton();
	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<SkinnedMeshRenderer> renderer = rig->AddComponent<SkinnedMeshRenderer>();
	renderer->SetMesh(Handle<MeshAsset>(MakeSliderMesh(_owned, "Smile", "Blink")));
	renderer->SetBlendShapeWeight(0, 0.0f);
	renderer->SetBlendShapeWeight(1, 0.0f);

	AnimationClip* clip = MakeClip();
	AddBlendShapeTrack(*clip, "Blink", 80.0f);

	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(clip));

	RunUpdate(0.5f);
	EXPECT_NEAR(renderer->GetBlendShapeWeight(static_cast<u32>(1)), 40.0f, 0.01f);
	EXPECT_FLOAT_EQ(renderer->GetBlendShapeWeight(static_cast<u32>(0)), 0.0f);

	RunUpdate(0.5f);
	EXPECT_NEAR(renderer->GetBlendShapeWeight(static_cast<u32>(1)), 80.0f, 0.01f);
}

TEST_F(AnimationPlaybackFixture, ShapesRebindByName) {
	SkeletonAsset* skeleton = MakeSkeleton();
	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<SkinnedMeshRenderer> renderer = rig->AddComponent<SkinnedMeshRenderer>();
	renderer->SetMesh(Handle<MeshAsset>(MakeSliderMesh(_owned, "Smile", "Blink")));
	renderer->SetBlendShapeWeight(0, 0.0f);
	renderer->SetBlendShapeWeight(1, 0.0f);

	AnimationClip* clip = MakeClip();
	AddBlendShapeTrack(*clip, "Blink", 100.0f);

	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(clip));
	RunUpdate(1.0f);
	ASSERT_NEAR(renderer->GetBlendShapeWeight(static_cast<u32>(1)), 100.0f, 0.01f);

	renderer->SetMesh(Handle<MeshAsset>(MakeSliderMesh(_owned, "Blink", "Smile")));
	renderer->SetBlendShapeWeight(0, 0.0f);
	renderer->SetBlendShapeWeight(1, 0.0f);
	++clip->generation;

	RunUpdate(1.0f);
	EXPECT_NEAR(renderer->GetBlendShapeWeight(static_cast<u32>(0)), 100.0f, 0.01f);
	EXPECT_FLOAT_EQ(renderer->GetBlendShapeWeight(static_cast<u32>(1)), 0.0f);
}

TEST_F(AnimationPlaybackFixture, RendererFoundOnModelChild) {
	SkeletonAsset* skeleton = MakeSkeleton();
	Handle<SceneObject> rig = MakeRig(skeleton);

	Handle<SceneObject> meshNode = SceneSystem::CreateObject("Body", _scene);
	meshNode->SetParent(rig);
	Handle<SkinnedMeshRenderer> renderer = meshNode->AddComponent<SkinnedMeshRenderer>();
	renderer->SetMesh(Handle<MeshAsset>(MakeSliderMesh(_owned, "Smile", "Blink")));
	renderer->SetBlendShapeWeight(0, 0.0f);
	renderer->SetBlendShapeWeight(1, 0.0f);

	AnimationClip* clip = MakeClip();
	AddBlendShapeTrack(*clip, "Smile", 60.0f);

	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(clip));
	RunUpdate(1.0f);

	EXPECT_NEAR(renderer->GetBlendShapeWeight(static_cast<u32>(0)), 60.0f, 0.01f);
}

TEST_F(AnimationPlaybackFixture, TrackForMissingShapeDropped) {
	SkeletonAsset* skeleton = MakeSkeleton();
	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<SkinnedMeshRenderer> renderer = rig->AddComponent<SkinnedMeshRenderer>();
	renderer->SetMesh(Handle<MeshAsset>(MakeSliderMesh(_owned, "Smile", "Blink")));
	renderer->SetBlendShapeWeight(0, 0.0f);
	renderer->SetBlendShapeWeight(1, 0.0f);

	AnimationClip* clip = MakeClip();
	AddBlendShapeTrack(*clip, "Frown", 100.0f);
	AddBlendShapeTrack(*clip, "Smile", 50.0f);

	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(clip));
	RunUpdate(1.0f);

	EXPECT_NEAR(renderer->GetBlendShapeWeight(static_cast<u32>(0)), 50.0f, 0.01f);
	EXPECT_FLOAT_EQ(renderer->GetBlendShapeWeight(static_cast<u32>(1)), 0.0f);
	EXPECT_EQ(animator->GetLayer(0)->GetBoundTrackCount(), 1u);
}

TEST_F(AnimationPlaybackFixture, DriveGrowsWeightListShorterThanMesh) {
	SkeletonAsset* skeleton = MakeSkeleton();
	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<SkinnedMeshRenderer> renderer = rig->AddComponent<SkinnedMeshRenderer>();
	renderer->SetMesh(Handle<MeshAsset>(MakeSliderMesh(_owned, "Smile", "Blink")));
	renderer->SetBlendShapeWeightCount(0);

	AnimationClip* clip = MakeClip();
	AddBlendShapeTrack(*clip, "Blink", 70.0f);

	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetClip(Handle<AnimationClip>(clip));
	RunUpdate(1.0f);

	ASSERT_EQ(renderer->GetBlendShapeWeightCount(), 2u);
	EXPECT_NEAR(renderer->GetBlendShapeWeight(static_cast<u32>(1)), 70.0f, 0.01f);
	EXPECT_FLOAT_EQ(renderer->GetBlendShapeWeight(static_cast<u32>(0)), 0.0f);
}

#endif

#ifdef SHALLTEAR_SHARED

namespace {

	class AnimationImportFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		ModuleHandle _module;
		bool _mountedEngine = false;
		Handle<Scene> _scene;
		const TypeInfo* _animatorType = nullptr;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_animation_import_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			std::filesystem::path engineAssets = RepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mountedEngine = true;
			}

			TypeRegistry::InitializePrimitives();
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			_module = ModuleSystem::Load(Path(str(PACKAGE_TARGET) + ".dll"));
			ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
			AssetDatabase::Initialize();

			_animatorType = TypeRegistry::FindType(StringHash("Animator"));
			ASSERT_NE(_animatorType, nullptr);

			_scene = SceneSystem::CreateScene("fox");
			ASSERT_TRUE(_scene.IsValid());
		}

		void TearDown() override {
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			ModuleSystem::Terminate();
			if (_mountedEngine) {
				FileSystem::Unmount("engine");
			}
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		bool StageFixture(const char* name) {
			std::filesystem::path fixtures = RepoDir("Source/Tests/Fixtures/Graphics");
			if (fixtures.empty()) {
				return false;
			}
			std::error_code ec;
			std::filesystem::copy_file(fixtures / name, _root / name,
									   std::filesystem::copy_options::overwrite_existing, ec);
			return !ec;
		}

		bool HasAnimator(const Handle<SceneObject>& object) const {
			if (!object.IsValid() || !_animatorType) {
				return false;
			}
			for (u32 i = 0; i < object->GetComponentCount(); ++i) {
				Handle<Component> component = object->GetComponentAt(i);
				if (component.IsValid() && component->GetType() == _animatorType) {
					return true;
				}
			}
			return false;
		}

		bool WriteAnimationBlock(const char* modelVirtualPath, std::string_view blockJSON) {
			Path physMeta = FileSystem::Resolve(AssetDatabase::GetMetaPath(Path(modelVirtualPath)));
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

		bool ReadProductSkeletonRef(const Guid& modelGuid, std::string_view key, Guid& out) const {
			Buffer bytes = FileSystem::ReadFile(ImportArtifactPath(modelGuid), MemTag::Asset);
			ImportArtifactView view;
			if (!ImportArtifactRead(bytes.data, bytes.size, view)) {
				return false;
			}
			ImportArtifactProductView product;
			if (!ImportArtifactFindProduct(view, key, product)) {
				return false;
			}
			CookReader descriptor(product.desc, product.descSize, view.strings);
			return descriptor.Read(out) && !descriptor.Failed();
		}
	};

	size_t WalkDrivenNodeCount() {
		std::filesystem::path fixtures = RepoDir("Source/Tests/Fixtures/Graphics");
		if (fixtures.empty()) {
			return 0;
		}
		ModelParseOptions options;
		options.tracks = true;
		ModelParseResult parsed = ParseModel(Path((fixtures / "Fox.glb").generic_string()), options);
		if (!parsed.success) {
			return 0;
		}
		std::vector<i32> nodes;
		for (const ModelTakeRange& take : parsed.takes) {
			if (take.name != "Walk") {
				continue;
			}
			for (i32 trackIndex : take.tracks) {
				if (trackIndex < 0 || static_cast<size_t>(trackIndex) >= parsed.tracks.size()) {
					continue;
				}
				const i32 node = parsed.tracks[static_cast<size_t>(trackIndex)].node;
				if (std::find(nodes.begin(), nodes.end(), node) == nodes.end()) {
					nodes.push_back(node);
				}
			}
		}
		return nodes.size();
	}

} // anonymous namespace

TEST_F(AnimationImportFixture, TakesBecomeClipsOnMintedSkeleton) {
	ASSERT_TRUE(StageFixture("Fox.glb"));

	const Path model("project://Fox.glb");
	Asset* prefab = AssetDatabase::LoadAsset(model);
	ASSERT_NE(prefab, nullptr);

	const Guid skeletonGuid = ProductLookup::Find(model, "Skeletons/Fox");
	ASSERT_TRUE(skeletonGuid.IsValid());

	for (const char* take : {"Survey", "Walk", "Run"}) {
		const Guid clipGuid = ProductLookup::Find(model, str("Clips/") + take);
		ASSERT_TRUE(clipGuid.IsValid()) << take << "'";

		Asset* clip = AssetDatabase::LoadAssetByGuid(clipGuid);
		ASSERT_NE(clip, nullptr);
		EXPECT_EQ(clip->GetAssetTypeId(), StringHash("AnimationClip"));

		Variant value;
		ASSERT_TRUE(GetField(clip, clip->GetType(), StringHash("length"), &value));
		EXPECT_GT(value.As<f32>(), 0.0f) << "clip '" << take;

		Guid recorded;
		ASSERT_TRUE(ReadProductSkeletonRef(prefab->assetId, str("Clips/") + take, recorded)) << "clip '" << take;
		EXPECT_EQ(recorded, skeletonGuid) << "clip '" << take;
	}
}

TEST_F(AnimationImportFixture, FoxWalkMovesBones) {
	ASSERT_TRUE(StageFixture("Fox.glb"));

	const Path model("project://Fox.glb");
	Asset* asset = AssetDatabase::LoadAsset(model);
	ASSERT_NE(asset, nullptr);
	const Guid modelGuid = asset->assetId;
	Handle<SceneObject> fox = SceneSystem::Instantiate(modelGuid, _scene);
	ASSERT_TRUE(fox.IsValid());

	Handle<Skeleton> rig = fox->GetComponent<Skeleton>();
	ASSERT_TRUE(rig.IsValid());

	const Guid walkGuid = ProductLookup::Find(model, "Clips/Walk");
	ASSERT_TRUE(walkGuid.IsValid());
	Asset* walk = AssetDatabase::LoadAssetByGuid(walkGuid);
	ASSERT_NE(walk, nullptr);

	Handle<Component> animator = fox->AddComponent(_animatorType);
	ASSERT_TRUE(animator.IsValid());
	ASSERT_TRUE(SetHandleField(animator.Get(), StringHash("_clip"), walk->GetInstanceId()));

	Vector<BoneTransform> rest{GetMemoryResource(MemTag::Scene)};
	rig->UpdatePose();
	for (const BoneTransform& bone : rig->GetLocalPose()) {
		rest.push_back(bone);
	}
	ASSERT_GT(rest.size(), 1u);

	const size_t expectedMovers = WalkDrivenNodeCount();
	ASSERT_GT(expectedMovers, 0u);

	const std::span<const BoneTransform> posed = rig->GetLocalPose();
	ASSERT_EQ(posed.size(), rest.size());
	std::vector<bool> everMoved(posed.size(), false);
	Vector<BoneTransform> previous{GetMemoryResource(MemTag::Scene)};
	u32 advancedBetweenFrames = 0;
	for (u32 frame = 0; frame < 8; ++frame) {
		previous.clear();
		for (const BoneTransform& bone : posed) {
			previous.push_back(bone);
		}
		RunUpdate(0.08f);
		for (size_t i = 0; i < posed.size(); ++i) {
			if (Quat::Angle(posed[i].rotation, rest[i].rotation) > 0.01f ||
				(posed[i].position - rest[i].position).LengthSquared() > 1e-9f ||
				(posed[i].scale - rest[i].scale).LengthSquared() > 1e-9f) {
				everMoved[i] = true;
			}
			if (frame > 0 && Quat::Angle(posed[i].rotation, previous[i].rotation) > 0.01f) {
				++advancedBetweenFrames;
			}
		}
	}

	size_t moved = 0;
	for (bool bone : everMoved) {
		moved += bone ? 1u : 0u;
	}
	EXPECT_EQ(moved, expectedMovers);
	EXPECT_GT(advancedBetweenFrames, 0u);
}

TEST_F(AnimationImportFixture, MorphTargetDrivesFromImportedClip) {
	ASSERT_TRUE(StageFixture("morphed.gltf"));

	const Path model("project://morphed.gltf");
	Asset* asset = AssetDatabase::LoadAsset(model);
	ASSERT_NE(asset, nullptr);
	const Guid modelGuid = asset->assetId;

	Handle<SceneObject> root = SceneSystem::Instantiate(modelGuid, _scene);
	ASSERT_TRUE(root.IsValid());
	SceneSystem::EndFrame();

	Handle<SkinnedMeshRenderer> renderer = root->GetComponentInChildren<SkinnedMeshRenderer>();
	ASSERT_TRUE(renderer.IsValid());
	ASSERT_EQ(renderer->GetBlendShapeWeightCount(), 2u);

	const Guid clipGuid = ProductLookup::Find(model, "Clips/Raise");
	ASSERT_TRUE(clipGuid.IsValid());
	Asset* clip = AssetDatabase::LoadAssetByGuid(clipGuid);
	ASSERT_NE(clip, nullptr);

	Handle<Component> animator = root->AddComponent(_animatorType);
	ASSERT_TRUE(animator.IsValid());
	ASSERT_TRUE(SetHandleField(animator.Get(), StringHash("_clip"), clip->GetInstanceId()));

	RunUpdate(0.5f);
	EXPECT_NEAR(renderer->GetBlendShapeWeight(static_cast<u32>(0)), 50.0f, 0.01f);
	EXPECT_FLOAT_EQ(renderer->GetBlendShapeWeight(static_cast<u32>(1)), 0.0f);

	RunUpdate(0.5f);
	EXPECT_NEAR(renderer->GetBlendShapeWeight(static_cast<u32>(0)), 100.0f, 0.01f);
	EXPECT_FLOAT_EQ(renderer->GetBlendShapeWeight(static_cast<u32>(1)), 0.0f);
}

TEST_F(AnimationImportFixture, DisablingStopsClipsKeepsRig) {
	ASSERT_TRUE(StageFixture("Fox.glb"));
	const Path model("project://Fox.glb");

	Asset* enabled = AssetDatabase::LoadAsset(model);
	ASSERT_NE(enabled, nullptr);
	EXPECT_TRUE(ProductLookup::Find(model, "Clips/Walk").IsValid());
	const Guid skeletonEnabled = ProductLookup::Find(model, "Skeletons/Fox");
	ASSERT_TRUE(skeletonEnabled.IsValid());

	AssetDatabase::Terminate();
	ModuleSystem::Unload(_module);
	ASSERT_FALSE(ModuleSystem::IsLoaded(_module));

	std::filesystem::path second = _root / "disabled";
	std::filesystem::create_directories(second);
	std::error_code copyError;
	std::filesystem::copy_file(RepoDir("Source/Tests/Fixtures/Graphics") / "Fox.glb", second / "Fox.glb",
							   std::filesystem::copy_options::overwrite_existing, copyError);
	ASSERT_FALSE(copyError);
	FileSystem::Unmount("project");
	FileSystem::Mount("project", Path(second.generic_string()));
	AssetDatabase::Initialize();

	Asset* disabled = AssetDatabase::LoadAsset(model);
	ASSERT_NE(disabled, nullptr);
	EXPECT_FALSE(ProductLookup::Find(model, "Clips/Walk").IsValid());
	EXPECT_FALSE(ProductLookup::Find(model, "Clips/Run").IsValid());
	EXPECT_TRUE(ProductLookup::Find(model, "Skeletons/Fox").IsValid());
	EXPECT_TRUE(ProductLookup::Find(model, "Skins/Skin0").IsValid());

	Handle<SceneObject> fox = SceneSystem::Instantiate(disabled->assetId, _scene);
	ASSERT_TRUE(fox.IsValid());
	EXPECT_TRUE(fox->GetComponent<Skeleton>().IsValid());
	EXPECT_FALSE(HasAnimator(fox));
}

TEST_F(AnimationImportFixture, RiggedModelGetsAnimator) {
	ASSERT_TRUE(StageFixture("Fox.glb"));
	ASSERT_TRUE(StageFixture("cube.glb"));

	Asset* fox = AssetDatabase::LoadAsset(Path("project://Fox.glb"));
	ASSERT_NE(fox, nullptr);
	Handle<SceneObject> rigged = SceneSystem::Instantiate(fox->assetId, _scene);
	ASSERT_TRUE(rigged.IsValid());
	ASSERT_TRUE(rigged->GetComponent<Skeleton>().IsValid());
	EXPECT_TRUE(HasAnimator(rigged));

	EXPECT_TRUE(rigged->GetComponent<Skeleton>().IsValid());

	Asset* cube = AssetDatabase::LoadAsset(Path("project://cube.glb"));
	ASSERT_NE(cube, nullptr);
	Handle<SceneObject> prop = SceneSystem::Instantiate(cube->assetId, _scene);
	ASSERT_TRUE(prop.IsValid());
	ASSERT_FALSE(prop->GetComponent<Skeleton>().IsValid());
	EXPECT_FALSE(HasAnimator(prop));
}

TEST_F(AnimationImportFixture, AnimationTypeNoneImportsRigWithoutAnimator) {
	ASSERT_TRUE(StageFixture("Fox.glb"));
	const Path model("project://Fox.glb");

	Asset* before = AssetDatabase::LoadAsset(model);
	ASSERT_NE(before, nullptr);
	const Guid modelGuid = before->assetId;
	{
		Handle<SceneObject> fox = SceneSystem::Instantiate(modelGuid, _scene);
		ASSERT_TRUE(fox.IsValid());
		ASSERT_TRUE(HasAnimator(fox));
		SceneSystem::Destroy(fox);
		SceneSystem::EndFrame();
	}
	AssetDatabase::UnloadAsset(before);

	ASSERT_TRUE(WriteAnimationBlock("project://Fox.glb", R"("animation":{"animationType":"None"})"));
	ASSERT_TRUE(AssetDatabase::ReimportAsset(model));

	Handle<SceneObject> fox = SceneSystem::Instantiate(modelGuid, _scene);
	ASSERT_TRUE(fox.IsValid());
	EXPECT_FALSE(HasAnimator(fox));
	EXPECT_TRUE(fox->GetComponent<Skeleton>().IsValid());
}

TEST_F(AnimationImportFixture, MeshlessModelMakesClipsNoRig) {
	ASSERT_TRUE(StageFixture("animated.gltf"));

	const Path staged("project://animated.gltf");
	str document = FileSystem::ReadTextFile(FileSystem::Resolve(staged), MemTag::Asset);
	ASSERT_FALSE(document.empty());
	constexpr std::string_view MeshRef = "\"mesh\": 0,";
	const size_t at = document.find(MeshRef);
	ASSERT_NE(at, str::npos);
	document = document.substr(0, at) + document.substr(at + MeshRef.size());
	Buffer payload(document.size(), MemTag::Asset);
	std::memcpy(payload.data, document.data(), document.size());
	ASSERT_TRUE(FileSystem::WriteFile(FileSystem::Resolve(staged), payload));

	Asset* model = AssetDatabase::LoadAsset(staged);
	ASSERT_NE(model, nullptr);

	EXPECT_TRUE(ProductLookup::Find(staged, "Clips/Parity").IsValid());
	EXPECT_TRUE(ProductLookup::Find(staged, "Clips/Kinds").IsValid());

	EXPECT_FALSE(ProductLookup::Find(staged, "Skeletons/animated").IsValid());
	EXPECT_FALSE(ProductLookup::Find(staged, "Rigs/animated").IsValid());
}

#endif
