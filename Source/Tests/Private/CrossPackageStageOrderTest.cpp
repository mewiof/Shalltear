#include "TestsPCH.h"

#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics/SkeletonAsset.h"
#include "Shalltear/Core/Graphics/Skeleton.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/Behavior.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Time.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

#ifndef SHALLTEAR_SHARED

#include "Shalltear/Animation/AnimationClip.h"
#include "Shalltear/Animation/Animator.h"
#include "Shalltear/Physics3D/Rigidbody.h"

SLT_DECLARE_LINKED_PACKAGE(Animation)
SLT_DECLARE_LINKED_PACKAGE(Physics3D)

namespace {

	constexpr f32 FIXED_STEP = 1.0f / 50.0f;

	class OrderWitness : public Behavior {
	public:
		Handle<Animator> animator;
		Handle<Rigidbody> body;

		i32 ticks = 0;
		f32 sawAnimatorTime = -1.0f;
		f32 sawBodyVelocityY = 1.0f;

		SLT_REFLECT_COMPONENT(OrderWitness, Behavior)
		SLT_END_REFLECT()
	protected:
		void OnFixedUpdate() override {
			++ticks;
			if (ticks > 1) {
				return;
			}
			sawAnimatorTime = animator.IsValid() ? animator->GetTime() : -1.0f;
			sawBodyVelocityY = body.IsValid() ? body->GetLinearVelocity().y : 1.0f;
		}
	};

	SLT_REGISTER_TYPE(OrderWitness)

	class CrossPackageStageOrderFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		Handle<Scene> _scene;
		char _animationToken = 0;
		char _physicsToken = 0;
		std::vector<Asset*> _owned;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_stage_order_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
		}

		void TearDown() override {
			SceneSystem::Terminate();
			for (Asset* asset : _owned) {
				Delete(asset);
			}
			_owned.clear();
			FrameStages::UnregisterOwnedBy(&_animationToken);
			FrameStages::UnregisterOwnedBy(&_physicsToken);
			ModuleSystem::Terminate();
			Settings::Terminate();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		void StandUp(bool physicsFirst) {
			if (physicsFirst) {
				SLT_CALL_LINKED_PACKAGE(Physics3D, &_physicsToken);
				SLT_CALL_LINKED_PACKAGE(Animation, &_animationToken);
			} else {
				SLT_CALL_LINKED_PACKAGE(Animation, &_animationToken);
				SLT_CALL_LINKED_PACKAGE(Physics3D, &_physicsToken);
			}
			_scene = SceneSystem::CreateScene("stage-order");
			ASSERT_TRUE(_scene.IsValid());
		}

		Handle<Animator> MakeFixedAnimator() {
			auto* skeleton = New<SkeletonAsset>(MemTag::Asset);
			skeleton->boneNames.push_back("Root");
			skeleton->parents.push_back(-1);
			skeleton->restPose.push_back(BoneTransform{});
			skeleton->boneNames.push_back("Tip");
			skeleton->parents.push_back(0);
			skeleton->restPose.push_back(BoneTransform{});
			_owned.push_back(skeleton);

			auto* clip = New<AnimationClip>(MemTag::Asset);
			clip->virtualPath = "project://stage-order.clip";
			AnimationClip::Track track;
			track.kind = AnimationTrackKind::BonePosition;
			track.interp = AnimationInterpolation::Linear;
			track.name = "Tip";
			track.times.push_back(0.0f);
			track.times.push_back(1.0f);
			track.values.push_back(Vec4(0.0f, 0.0f, 0.0f, 0.0f));
			track.values.push_back(Vec4(4.0f, 0.0f, 0.0f, 0.0f));
			clip->tracks.push_back(std::move(track));
			clip->length = 1.0f;
			clip->loop = AnimationLoopMode::Loop;
			_owned.push_back(clip);

			Handle<SceneObject> rig = SceneSystem::CreateObject("rig", _scene);
			rig->AddComponent<Skeleton>()->SetSkeleton(Handle<SkeletonAsset>(skeleton));
			Handle<Animator> animator = rig->AddComponent<Animator>();
			animator->SetClip(Handle<AnimationClip>(clip));
			animator->SetUpdateMode(AnimatorUpdateMode::AnimatePhysics);
			return animator;
		}

		Handle<Rigidbody> MakeBody() {
			Handle<SceneObject> object = SceneSystem::CreateObject("body", _scene);
			return object->AddComponent<Rigidbody>();
		}

		static void RunFixedSteps(u32 count) {
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
	};

} // namespace

TEST_F(CrossPackageStageOrderFixture, FixedTickSeesPoseBeforeStep) {
	StandUp(false);

	Handle<Animator> animator = MakeFixedAnimator();
	Handle<Rigidbody> body = MakeBody();
	Handle<SceneObject> scripted = SceneSystem::CreateObject("script", _scene);
	Handle<OrderWitness> witness = scripted->AddComponent<OrderWitness>();
	ASSERT_TRUE(witness.IsValid());
	witness->animator = animator;
	witness->body = body;

	RunFixedSteps(1);
	ASSERT_EQ(witness->ticks, 1);

	EXPECT_GT(animator->GetTime(), 0.0f);
	EXPECT_LT(body->GetLinearVelocity().y, 0.0f);

	EXPECT_NEAR(witness->sawAnimatorTime, FIXED_STEP, 1e-5f);
	EXPECT_FLOAT_EQ(witness->sawBodyVelocityY, 0.0f);
}

TEST_F(CrossPackageStageOrderFixture, ReversingEnableOrderChangesNothingAboutFrame) {
	StandUp(true);

	Handle<Animator> animator = MakeFixedAnimator();
	Handle<Rigidbody> body = MakeBody();
	Handle<SceneObject> scripted = SceneSystem::CreateObject("script", _scene);
	Handle<OrderWitness> witness = scripted->AddComponent<OrderWitness>();
	ASSERT_TRUE(witness.IsValid());
	witness->animator = animator;
	witness->body = body;

	RunFixedSteps(1);
	ASSERT_EQ(witness->ticks, 1);

	EXPECT_GT(animator->GetTime(), 0.0f);
	EXPECT_LT(body->GetLinearVelocity().y, 0.0f);

	EXPECT_NEAR(witness->sawAnimatorTime, FIXED_STEP, 1e-5f);
	EXPECT_FLOAT_EQ(witness->sawBodyVelocityY, 0.0f);
}

TEST_F(CrossPackageStageOrderFixture, LatePackageRunsBeforeStep) {
	StandUp(false);

	Handle<Rigidbody> body = MakeBody();

	struct Latecomer {
		static void Run(const FrameStageArgs&, void* user) {
			auto* seen = static_cast<f32*>(user);
			*seen = SeenBody()->GetLinearVelocity().y;
		}
		static Handle<Rigidbody>& SeenBody() {
			static Handle<Rigidbody> handle;
			return handle;
		}
	};
	Latecomer::SeenBody() = body;
	f32 sawVelocityY = 1.0f;
	static const u8 _latecomerOwner = 0;
	ASSERT_TRUE(FrameStages::Register(FrameStage::FixedUpdate, FrameStagePosition::Normal, &Latecomer::Run, &sawVelocityY, &_latecomerOwner));

	RunFixedSteps(1);

	EXPECT_LT(body->GetLinearVelocity().y, 0.0f);
	EXPECT_FLOAT_EQ(sawVelocityY, 0.0f);

	EXPECT_EQ(FrameStages::UnregisterOwnedBy(&_latecomerOwner), 1u);
	Latecomer::SeenBody() = {};
}

TEST_F(CrossPackageStageOrderFixture, ForwardedPoseHasNoLag) {
	StandUp(false);

	Handle<Animator> animator = MakeFixedAnimator();
	Handle<SceneObject> scripted = SceneSystem::CreateObject("script", _scene);
	Handle<OrderWitness> witness = scripted->AddComponent<OrderWitness>();
	ASSERT_TRUE(witness.IsValid());
	witness->animator = animator;

	RunFixedSteps(4);
	ASSERT_EQ(witness->ticks, 4);

	const f32 lagFrames = (animator->GetTime() > 0.0f) ? (FIXED_STEP - witness->sawAnimatorTime) / FIXED_STEP : -1.0f;
	EXPECT_NEAR(lagFrames, 0.0f, 1e-3f);
}

#endif
