#include "TestsPCH.h"

#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
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
#include <cstring>
#include <initializer_list>

#ifndef SHALLTEAR_SHARED
#include "Shalltear/Animation/AnimationClip.h"
#include "Shalltear/Animation/AnimationLayer.h"
#include "Shalltear/Animation/Animator.h"
#include "Shalltear/Animation/AnimatorController.h"
#endif

using namespace Shalltear;

#ifndef SHALLTEAR_SHARED

SLT_DECLARE_LINKED_PACKAGE(Animation)

namespace {

	constexpr f32 FIXED_STEP = 1.0f / 50.0f;

	void RunUpdate(f32 delta) {
		FrameTime clockState;
		clockState.fixedDeltaTime = FIXED_STEP;
		clockState.maximumDeltaTime = 1000.0f;
		Time::FrameScope clock{clockState};
		Time::BeginFrame(delta);

		FrameStageArgs args;
		FrameStages::Run(FrameStage::Update, args);
	}

	class ControllerWorld : public ::testing::Test {
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
					("shalltear_animation_controller_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			SLT_CALL_LINKED_PACKAGE(Animation, &_token);

			_scene = SceneSystem::CreateScene("controller");
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

		AnimationClip* MakeClip(const char* path = "project://controller.clip") {
			AnimationClip* clip = New<AnimationClip>(MemTag::Asset);
			clip->virtualPath = path;
			_owned.push_back(clip);
			return clip;
		}

		AnimatorController* MakeController() {
			AnimatorController* controller = New<AnimatorController>(MemTag::Asset);
			controller->virtualPath = "project://Assets/Test.controller";
			AnimatorController::StateMachine root;
			root.name = "Base Layer";
			controller->machines.push_back(std::move(root));
			AnimatorController::Layer base;
			base.name = "Base Layer";
			base.machine = 0;
			controller->layers.push_back(std::move(base));
			_owned.push_back(controller);
			return controller;
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

	i32 AddLeaf(AnimatorController& controller, AnimationClip* clip) {
		AnimatorController::MotionNode node;
		node.kind = AnimatorBlendKind::Clip;
		node.clip = Handle<AnimationClip>(clip);
		controller.nodes.push_back(std::move(node));
		return static_cast<i32>(controller.nodes.size()) - 1;
	}

	i32 AddState(AnimatorController& controller, const char* name, i32 motion, i32 machine = 0) {
		AnimatorController::State state;
		state.name = name;
		state.motion = motion;
		state.machine = machine;
		controller.states.push_back(std::move(state));
		return static_cast<i32>(controller.states.size()) - 1;
	}

	AnimatorController::Condition Greater(const char* parameter, f32 threshold) {
		AnimatorController::Condition condition;
		condition.parameter = parameter;
		condition.mode = AnimatorConditionMode::Greater;
		condition.threshold = threshold;
		return condition;
	}

	void AddParameter(AnimatorController& controller, const char* name, AnimatorParameterKind kind, f32 value = 0.0f) {
		AnimatorController::Parameter parameter;
		parameter.name = name;
		parameter.kind = kind;
		parameter.defaultValue = value;
		controller.parameters.push_back(std::move(parameter));
	}

	struct BlendResult {
		u32 count = 0;
		f32 weight[8]{};
		f32 Sum() const {
			f32 total = 0.0f;
			for (u32 index = 0; index < count; ++index) {
				total += weight[index];
			}
			return total;
		}
	};

	BlendResult WeighAt(const AnimatorController& controller, i32 node, f32 x, f32 y, const f32* values = nullptr) {
		BlendResult out;
		out.count = AnimatorBlend::Weights(controller, node, Vec2(x, y), values, out.weight, 8);
		return out;
	}

} // anonymous namespace

TEST(AnimatorBlendTest, Blend1DLinearAndClamped) {
	AnimatorController controller;
	AnimatorController::MotionNode tree;
	tree.kind = AnimatorBlendKind::Simple1D;
	tree.parameterX = "Speed";
	for (f32 threshold : {4.0f, 0.0f, 2.0f}) {
		AnimatorController::MotionChild child;
		child.position = Vec2(threshold, 0.0f);
		tree.children.push_back(child);
	}
	controller.nodes.push_back(std::move(tree));

	const BlendResult below = WeighAt(controller, 0, -5.0f, 0.0f);
	EXPECT_EQ(below.count, 3u);
	EXPECT_FLOAT_EQ(below.weight[1], 1.0f);
	EXPECT_FLOAT_EQ(below.weight[0], 0.0f);

	const BlendResult above = WeighAt(controller, 0, 99.0f, 0.0f);
	EXPECT_FLOAT_EQ(above.weight[0], 1.0f);

	const BlendResult onPeak = WeighAt(controller, 0, 2.0f, 0.0f);
	EXPECT_FLOAT_EQ(onPeak.weight[2], 1.0f);

	const BlendResult between = WeighAt(controller, 0, 2.5f, 0.0f);
	EXPECT_NEAR(between.weight[2], 0.75f, 1e-5f);
	EXPECT_NEAR(between.weight[0], 0.25f, 1e-5f);
	EXPECT_NEAR(between.Sum(), 1.0f, 1e-5f);
}

TEST(AnimatorBlendTest, DirectionalBlendSeparatesMotionsOnRay) {
	AnimatorController controller;
	for (AnimatorBlendKind kind : {AnimatorBlendKind::FreeformDirectional2D, AnimatorBlendKind::FreeformCartesian2D}) {
		AnimatorController::MotionNode tree;
		tree.kind = kind;
		tree.parameterX = "X";
		tree.parameterY = "Y";
		for (f32 forward : {0.0f, 1.0f, 4.0f}) {
			AnimatorController::MotionChild child;
			child.position = Vec2(0.0f, forward);
			tree.children.push_back(child);
		}
		controller.nodes.push_back(std::move(tree));
	}

	const BlendResult directionalAtWalk = WeighAt(controller, 0, 0.0f, 1.0f);
	EXPECT_NEAR(directionalAtWalk.weight[1], 1.0f, 1e-4f);
	EXPECT_NEAR(directionalAtWalk.Sum(), 1.0f, 1e-5f);

	const BlendResult directionalBetween = WeighAt(controller, 0, 0.0f, 2.5f);
	EXPECT_GT(directionalBetween.weight[1], 0.05f);
	EXPECT_GT(directionalBetween.weight[2], 0.05f);
	EXPECT_NEAR(directionalBetween.weight[0], 0.0f, 1e-4f);
	EXPECT_NEAR(directionalBetween.Sum(), 1.0f, 1e-5f);

	const BlendResult cartesianBetween = WeighAt(controller, 1, 0.0f, 2.5f);
	EXPECT_NEAR(cartesianBetween.Sum(), 1.0f, 1e-5f);
	EXPECT_GT(Math::Abs(cartesianBetween.weight[1] - directionalBetween.weight[1]), 1e-3f);
}

TEST(AnimatorBlendTest, CartesianBlendCornersAndCentre) {
	AnimatorController controller;
	AnimatorController::MotionNode tree;
	tree.kind = AnimatorBlendKind::FreeformCartesian2D;
	const Vec2 corners[4] = {Vec2(-1.0f, -1.0f), Vec2(1.0f, -1.0f), Vec2(-1.0f, 1.0f), Vec2(1.0f, 1.0f)};
	for (const Vec2& corner : corners) {
		AnimatorController::MotionChild child;
		child.position = corner;
		tree.children.push_back(child);
	}
	controller.nodes.push_back(std::move(tree));

	for (u32 corner = 0; corner < 4; ++corner) {
		const BlendResult at = WeighAt(controller, 0, corners[corner].x, corners[corner].y);
		EXPECT_NEAR(at.weight[corner], 1.0f, 1e-4f) << corner;
	}
	const BlendResult centre = WeighAt(controller, 0, 0.0f, 0.0f);
	for (u32 corner = 0; corner < 4; ++corner) {
		EXPECT_NEAR(centre.weight[corner], 0.25f, 1e-4f);
	}
}

TEST(AnimatorBlendTest, BlendWeightsSumTo1NeverNegative) {
	AnimatorController controller;
	const Vec2 points[5] = {Vec2(0.0f, 0.0f), Vec2(1.0f, 0.0f), Vec2(-1.0f, 0.3f), Vec2(0.2f, 2.0f), Vec2(-1.5f, -0.8f)};
	for (AnimatorBlendKind kind :
		 {AnimatorBlendKind::Simple1D, AnimatorBlendKind::FreeformCartesian2D, AnimatorBlendKind::FreeformDirectional2D}) {
		AnimatorController::MotionNode tree;
		tree.kind = kind;
		for (const Vec2& point : points) {
			AnimatorController::MotionChild child;
			child.position = point;
			tree.children.push_back(child);
		}
		controller.nodes.push_back(std::move(tree));
	}

	for (i32 node = 0; node < 3; ++node) {
		for (i32 stepX = -10; stepX <= 10; ++stepX) {
			for (i32 stepY = -10; stepY <= 10; ++stepY) {
				const f32 x = static_cast<f32>(stepX) * 0.3f;
				const f32 y = static_cast<f32>(stepY) * 0.3f;
				const BlendResult at = WeighAt(controller, node, x, y);
				ASSERT_EQ(at.count, 5u) << "node " << node << " at (" << x << ", " << y << ")";
				ASSERT_NEAR(at.Sum(), 1.0f, 1e-4f) << "node " << node << ' ' << x << ", " << y << ")";
				for (u32 index = 0; index < at.count; ++index) {
					ASSERT_GE(at.weight[index], 0.0f) << "node " << node << " gave motion " << index;
				}
			}
		}
	}
}

TEST(AnimatorBlendTest, DirectBlendAllZeroIsSilence) {
	AnimatorController controller;
	AnimatorController::MotionNode tree;
	tree.kind = AnimatorBlendKind::Direct;
	for (i32 index = 0; index < 3; ++index) {
		tree.children.push_back(AnimatorController::MotionChild{});
	}
	controller.nodes.push_back(std::move(tree));

	const f32 values[3] = {1.0f, 3.0f, 0.0f};
	const BlendResult at = WeighAt(controller, 0, 0.0f, 0.0f, values);
	EXPECT_EQ(at.count, 3u);
	EXPECT_NEAR(at.weight[0], 0.25f, 1e-5f);
	EXPECT_NEAR(at.weight[1], 0.75f, 1e-5f);
	EXPECT_NEAR(at.weight[2], 0.0f, 1e-5f);

	const f32 silent[3] = {0.0f, 0.0f, 0.0f};
	const BlendResult nothing = WeighAt(controller, 0, 0.0f, 0.0f, silent);
	EXPECT_NEAR(nothing.Sum(), 0.0f, 1e-5f);

	const f32 negative[3] = {-2.0f, 1.0f, 0.0f};
	const BlendResult clamped = WeighAt(controller, 0, 0.0f, 0.0f, negative);
	EXPECT_NEAR(clamped.weight[0], 0.0f, 1e-5f);
	EXPECT_NEAR(clamped.weight[1], 1.0f, 1e-5f);
}

namespace {
	class AnimatorControllerFixture : public ControllerWorld {};
	class AnimationLayeringFixture : public ControllerWorld {};
} // anonymous namespace

TEST_F(AnimatorControllerFixture, ScriptParameterCrossFadesClips) {
	SkeletonAsset* skeleton = MakeSpine();
	AnimationClip* idle = MakeClip("project://idle.clip");
	AddConstantRotation(*idle, {"Chest"}, 0.0f);
	AnimationClip* wave = MakeClip("project://wave.clip");
	AddConstantRotation(*wave, {"Chest"}, 90.0f);

	AnimatorController* controller = MakeController();
	AddParameter(*controller, "Wave", AnimatorParameterKind::Float);
	const i32 idleState = AddState(*controller, "Idle", AddLeaf(*controller, idle));
	const i32 waveState = AddState(*controller, "Wave", AddLeaf(*controller, wave));
	controller->machines[0].defaultState = idleState;

	AnimatorController::Transition toWave;
	toWave.destination = waveState;
	toWave.duration = 0.4f;
	toWave.conditions.push_back(Greater("Wave", 0.5f));
	controller->states[static_cast<size_t>(idleState)].transitions.push_back(std::move(toWave));

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetApplyRootMotion(false);
	Handle<AnimatorStateMachine> driver = rig->AddComponent<AnimatorStateMachine>();
	driver->SetController(Handle<AnimatorController>(controller));

	RunUpdate(0.05f);
	EXPECT_EQ(driver->GetCurrentState(0), idleState);
	EXPECT_NEAR(BoneAngle(rig, 3), 0.0f, 1e-2f);

	EXPECT_TRUE(driver->SetFloat("Wave", 1.0f));
	RunUpdate(0.05f);
	EXPECT_EQ(driver->GetCurrentState(0), waveState);
	EXPECT_TRUE(animator->IsTransitioning());

	RunUpdate(0.1f);
	const f32 midway = BoneAngle(rig, 3);
	EXPECT_GT(midway, 1.0f);
	EXPECT_LT(midway, 89.0f);

	for (i32 frame = 0; frame < 20; ++frame) {
		RunUpdate(0.05f);
	}
	EXPECT_FALSE(animator->IsTransitioning());
	EXPECT_NEAR(BoneAngle(rig, 3), 90.0f, 1e-2f);

	EXPECT_FALSE(driver->SetFloat("Wav", 1.0f));
}

TEST_F(AnimatorControllerFixture, ExitTimeGatesThenConditions) {
	SkeletonAsset* skeleton = MakeSpine();
	AnimationClip* first = MakeClip("project://first.clip");
	AddConstantRotation(*first, {"Chest"}, 0.0f);
	first->loop = AnimationLoopMode::None;
	AnimationClip* second = MakeClip("project://second.clip");
	AddConstantRotation(*second, {"Chest"}, 45.0f);

	AnimatorController* controller = MakeController();
	AddParameter(*controller, "Go", AnimatorParameterKind::Bool);
	const i32 a = AddState(*controller, "A", AddLeaf(*controller, first));
	const i32 b = AddState(*controller, "B", AddLeaf(*controller, second));
	controller->machines[0].defaultState = a;

	AnimatorController::Transition edge;
	edge.destination = b;
	edge.duration = 0.0f;
	edge.exitTime = 0.75f;
	AnimatorController::Condition go;
	go.parameter = "Go";
	go.mode = AnimatorConditionMode::If;
	edge.conditions.push_back(std::move(go));
	controller->states[static_cast<size_t>(a)].transitions.push_back(std::move(edge));

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetApplyRootMotion(false);
	Handle<AnimatorStateMachine> driver = rig->AddComponent<AnimatorStateMachine>();
	driver->SetController(Handle<AnimatorController>(controller));

	RunUpdate(0.05f);
	ASSERT_EQ(driver->GetCurrentState(0), a);

	EXPECT_TRUE(driver->SetBool("Go", true));
	for (i32 frame = 0; frame < 10; ++frame) {
		RunUpdate(0.05f);
		ASSERT_EQ(driver->GetCurrentState(0), a) << frame;
	}
	ASSERT_LT(driver->GetNormalizedTime(0), 0.75f);

	for (i32 frame = 0; frame < 10; ++frame) {
		RunUpdate(0.05f);
	}
	EXPECT_EQ(driver->GetCurrentState(0), b);

	Handle<SceneObject> secondRig = MakeRig(skeleton);
	Handle<Animator> secondAnimator = secondRig->AddComponent<Animator>();
	secondAnimator->SetApplyRootMotion(false);
	Handle<AnimatorStateMachine> quiet = secondRig->AddComponent<AnimatorStateMachine>();
	quiet->SetController(Handle<AnimatorController>(controller));
	RunUpdate(0.05f);
	ASSERT_EQ(quiet->GetCurrentState(0), a);
	for (i32 frame = 0; frame < 40; ++frame) {
		RunUpdate(0.05f);
		ASSERT_EQ(quiet->GetCurrentState(0), a);
	}
}

TEST_F(AnimatorControllerFixture, TriggerClearedOnlyByConsumer) {
	SkeletonAsset* skeleton = MakeSpine();
	AnimationClip* clip = MakeClip();
	AddConstantRotation(*clip, {"Chest"}, 0.0f);

	AnimatorController* controller = MakeController();
	AddParameter(*controller, "Jump", AnimatorParameterKind::Trigger);
	AddParameter(*controller, "Grounded", AnimatorParameterKind::Bool, 1.0f);
	const i32 leaf = AddLeaf(*controller, clip);
	const i32 idle = AddState(*controller, "Idle", leaf);
	const i32 jump = AddState(*controller, "Jump", leaf);
	const i32 blocked = AddState(*controller, "Blocked", leaf);
	controller->machines[0].defaultState = idle;

	AnimatorController::Transition never;
	never.destination = blocked;
	never.duration = 0.0f;
	AnimatorController::Condition jumpIf;
	jumpIf.parameter = "Jump";
	jumpIf.mode = AnimatorConditionMode::If;
	never.conditions.push_back(jumpIf);
	AnimatorController::Condition impossible;
	impossible.parameter = "Grounded";
	impossible.mode = AnimatorConditionMode::IfNot;
	never.conditions.push_back(std::move(impossible));
	controller->states[static_cast<size_t>(idle)].transitions.push_back(std::move(never));

	AnimatorController::Transition takes;
	takes.destination = jump;
	takes.duration = 0.0f;
	takes.conditions.push_back(jumpIf);
	controller->states[static_cast<size_t>(idle)].transitions.push_back(std::move(takes));

	AnimatorController::Transition back;
	back.destination = idle;
	back.duration = 0.0f;
	back.exitTime = 0.0f;
	controller->states[static_cast<size_t>(jump)].transitions.push_back(std::move(back));

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetApplyRootMotion(false);
	Handle<AnimatorStateMachine> driver = rig->AddComponent<AnimatorStateMachine>();
	driver->SetController(Handle<AnimatorController>(controller));

	RunUpdate(0.05f);
	ASSERT_EQ(driver->GetCurrentState(0), idle);
	EXPECT_FALSE(driver->GetBool("Jump"));

	EXPECT_TRUE(driver->SetTrigger("Jump"));
	RunUpdate(0.05f);
	EXPECT_EQ(driver->GetCurrentState(0), jump);
	EXPECT_FALSE(driver->GetBool("Jump"));

	RunUpdate(0.05f);
	ASSERT_EQ(driver->GetCurrentState(0), idle);
	for (i32 frame = 0; frame < 10; ++frame) {
		RunUpdate(0.05f);
		ASSERT_EQ(driver->GetCurrentState(0), idle) << frame;
	}
}

TEST_F(AnimatorControllerFixture, TransitionStartsAtOffsetOrPhase) {
	SkeletonAsset* skeleton = MakeSpine();
	AnimationClip* clip = MakeClip();
	AddConstantRotation(*clip, {"Chest"}, 0.0f);
	clip->length = 2.0f;

	AnimatorController* controller = MakeController();
	AddParameter(*controller, "Go", AnimatorParameterKind::Trigger);
	const i32 leaf = AddLeaf(*controller, clip);
	const i32 from = AddState(*controller, "From", leaf);
	const i32 offsetTo = AddState(*controller, "Offset", leaf);
	const i32 syncTo = AddState(*controller, "Sync", leaf);
	controller->machines[0].defaultState = from;

	auto makeEdge = [&](i32 destination, f32 offset, bool sync) {
		AnimatorController::Transition edge;
		edge.destination = destination;
		edge.duration = 0.0f;
		edge.offset = offset;
		edge.syncPhase = sync;
		AnimatorController::Condition go;
		go.parameter = "Go";
		go.mode = AnimatorConditionMode::If;
		edge.conditions.push_back(std::move(go));
		return edge;
	};

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetApplyRootMotion(false);
	Handle<AnimatorStateMachine> driver = rig->AddComponent<AnimatorStateMachine>();
	driver->SetController(Handle<AnimatorController>(controller));

	controller->states[static_cast<size_t>(from)].transitions.push_back(makeEdge(offsetTo, 0.25f, false));
	RunUpdate(0.05f);
	ASSERT_EQ(driver->GetCurrentState(0), from);
	for (i32 frame = 0; frame < 24; ++frame) {
		RunUpdate(0.05f);
	}
	const f32 sourcePhase = driver->GetNormalizedTime(0);
	ASSERT_GT(Math::Abs(sourcePhase - 0.25f), 0.1f);
	driver->SetTrigger("Go");
	RunUpdate(0.0f);
	ASSERT_EQ(driver->GetCurrentState(0), offsetTo);
	EXPECT_NEAR(driver->GetNormalizedTime(0), 0.25f, 1e-3f);

	controller->states[static_cast<size_t>(from)].transitions.clear();
	controller->states[static_cast<size_t>(from)].transitions.push_back(makeEdge(syncTo, 0.25f, true));
	Handle<SceneObject> secondRig = MakeRig(skeleton);
	Handle<Animator> secondAnimator = secondRig->AddComponent<Animator>();
	secondAnimator->SetApplyRootMotion(false);
	Handle<AnimatorStateMachine> synced = secondRig->AddComponent<AnimatorStateMachine>();
	synced->SetController(Handle<AnimatorController>(controller));
	RunUpdate(0.05f);
	ASSERT_EQ(synced->GetCurrentState(0), from);
	for (i32 frame = 0; frame < 24; ++frame) {
		RunUpdate(0.05f);
	}
	const f32 secondPhase = synced->GetNormalizedTime(0);
	ASSERT_GT(Math::Abs(secondPhase - 0.25f), 0.1f);
	synced->SetTrigger("Go");
	RunUpdate(0.0f);
	ASSERT_EQ(synced->GetCurrentState(0), syncTo);
	EXPECT_NEAR(synced->GetNormalizedTime(0), secondPhase, 1e-3f);
	EXPECT_GT(Math::Abs(synced->GetNormalizedTime(0) - 0.25f), 0.05f);
}

TEST_F(AnimatorControllerFixture, AnyStateScopedNoReEnter) {
	SkeletonAsset* skeleton = MakeSpine();
	AnimationClip* clip = MakeClip();
	AddConstantRotation(*clip, {"Chest"}, 0.0f);

	AnimatorController* controller = MakeController();
	AddParameter(*controller, "Die", AnimatorParameterKind::Trigger);
	AddParameter(*controller, "Stop", AnimatorParameterKind::Trigger);

	AnimatorController::StateMachine locomotion;
	locomotion.name = "Locomotion";
	locomotion.parent = 0;
	controller->machines.push_back(std::move(locomotion));

	const i32 leaf = AddLeaf(*controller, clip);
	const i32 walk = AddState(*controller, "Walk", leaf, 1);
	const i32 idle = AddState(*controller, "Idle", leaf, 0);
	const i32 dead = AddState(*controller, "Dead", leaf, 0);
	controller->machines[0].defaultState = walk;
	controller->machines[1].defaultState = walk;

	auto edge = [&](i32 destination, const char* trigger) {
		AnimatorController::Transition out;
		out.destination = destination;
		out.duration = 0.0f;
		AnimatorController::Condition condition;
		condition.parameter = trigger;
		condition.mode = AnimatorConditionMode::If;
		out.conditions.push_back(std::move(condition));
		return out;
	};
	controller->machines[0].anyState.push_back(edge(dead, "Die"));
	controller->machines[1].anyState.push_back(edge(idle, "Stop"));

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetApplyRootMotion(false);
	Handle<AnimatorStateMachine> driver = rig->AddComponent<AnimatorStateMachine>();
	driver->SetController(Handle<AnimatorController>(controller));

	RunUpdate(0.05f);
	ASSERT_EQ(driver->GetCurrentState(0), walk);

	driver->SetTrigger("Stop");
	RunUpdate(0.05f);
	EXPECT_EQ(driver->GetCurrentState(0), idle);

	driver->SetTrigger("Stop");
	for (i32 frame = 0; frame < 5; ++frame) {
		RunUpdate(0.05f);
		ASSERT_EQ(driver->GetCurrentState(0), idle);
	}

	driver->SetTrigger("Die");
	RunUpdate(0.05f);
	EXPECT_EQ(driver->GetCurrentState(0), dead);

	driver->SetTrigger("Die");
	RunUpdate(0.05f);
	const f32 before = driver->GetNormalizedTime(0);
	driver->SetTrigger("Die");
	RunUpdate(0.05f);
	EXPECT_GT(driver->GetNormalizedTime(0), before);

	Handle<SceneObject> nested = MakeRig(skeleton);
	Handle<Animator> nestedAnimator = nested->AddComponent<Animator>();
	nestedAnimator->SetApplyRootMotion(false);
	Handle<AnimatorStateMachine> deep = nested->AddComponent<AnimatorStateMachine>();
	deep->SetController(Handle<AnimatorController>(controller));
	RunUpdate(0.05f);
	ASSERT_EQ(deep->GetCurrentState(0), walk);
	deep->SetTrigger("Die");
	RunUpdate(0.05f);
	EXPECT_EQ(deep->GetCurrentState(0), dead);
}

TEST_F(AnimatorControllerFixture, BlendTreeMissingLeafPlaysRest) {
	SkeletonAsset* skeleton = MakeSpine();
	AnimationClip* base = MakeClip("project://base.clip");
	AddConstantRotation(*base, {"Chest"}, 0.0f);
	AnimationClip* driven = MakeClip("project://driven.clip");
	AddConstantRotation(*driven, {"Chest"}, 80.0f);

	AnimatorController* controller = MakeController();
	AddParameter(*controller, "Speed", AnimatorParameterKind::Float);

	AnimatorController::MotionNode tree;
	tree.kind = AnimatorBlendKind::Simple1D;
	tree.parameterX = "Speed";
	const i32 present = AddLeaf(*controller, driven);
	AnimatorController::MotionNode missing;
	missing.kind = AnimatorBlendKind::Clip;
	controller->nodes.push_back(std::move(missing));
	const i32 absent = static_cast<i32>(controller->nodes.size()) - 1;
	for (i32 node : {present, absent}) {
		AnimatorController::MotionChild child;
		child.node = node;
		child.position = Vec2(node == present ? 0.0f : 1.0f, 0.0f);
		tree.children.push_back(child);
	}
	controller->nodes.push_back(std::move(tree));
	const i32 state = AddState(*controller, "Move", static_cast<i32>(controller->nodes.size()) - 1);
	controller->machines[0].defaultState = state;

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetApplyRootMotion(false);
	animator->SetClip(Handle<AnimationClip>(base));
	Handle<AnimatorStateMachine> driver = rig->AddComponent<AnimatorStateMachine>();
	driver->SetController(Handle<AnimatorController>(controller));

	ASSERT_TRUE(driver->SetFloat("Speed", 0.5f));
	RunUpdate(0.05f);
	EXPECT_NEAR(BoneAngle(rig, 3), 80.0f, 1e-2f);

	ASSERT_TRUE(driver->SetFloat("Speed", 1.0f));
	RunUpdate(0.05f);
	EXPECT_NEAR(BoneAngle(rig, 3), 0.0f, 1e-2f);
}

TEST_F(AnimatorControllerFixture, TravelWalksAuthoredEdges) {
	SkeletonAsset* skeleton = MakeSpine();
	AnimationClip* clip = MakeClip();
	AddConstantRotation(*clip, {"Chest"}, 0.0f);

	AnimatorController* controller = MakeController();
	AddParameter(*controller, "Never", AnimatorParameterKind::Bool);
	const i32 leaf = AddLeaf(*controller, clip);
	const i32 idle = AddState(*controller, "Idle", leaf);
	const i32 crouch = AddState(*controller, "Crouch", leaf);
	const i32 prone = AddState(*controller, "Prone", leaf);
	const i32 island = AddState(*controller, "Island", leaf);
	controller->machines[0].defaultState = idle;

	auto chain = [&](i32 fromState, i32 toState) {
		AnimatorController::Transition out;
		out.destination = toState;
		out.duration = 0.0f;
		AnimatorController::Condition never;
		never.parameter = "Never";
		never.mode = AnimatorConditionMode::If;
		out.conditions.push_back(std::move(never));
		controller->states[static_cast<size_t>(fromState)].transitions.push_back(std::move(out));
	};
	chain(idle, crouch);
	chain(crouch, prone);

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetApplyRootMotion(false);
	Handle<AnimatorStateMachine> driver = rig->AddComponent<AnimatorStateMachine>();
	driver->SetController(Handle<AnimatorController>(controller));

	RunUpdate(0.05f);
	ASSERT_EQ(driver->GetCurrentState(0), idle);

	EXPECT_TRUE(driver->Travel("Prone"));
	EXPECT_EQ(driver->GetTravelRemaining(0), 2u);

	RunUpdate(0.05f);
	EXPECT_EQ(driver->GetCurrentState(0), crouch);
	RunUpdate(0.05f);
	EXPECT_EQ(driver->GetCurrentState(0), prone);
	EXPECT_EQ(driver->GetTravelRemaining(0), 0u);

	EXPECT_FALSE(driver->Travel("Island"));
	RunUpdate(0.05f);
	EXPECT_EQ(driver->GetCurrentState(0), prone);
	EXPECT_FALSE(driver->Travel("Nowhere"));
	(void)island;
}

TEST_F(AnimatorControllerFixture, ControllerRoundTripsFileAndCook) {
	AnimationClip* clip = MakeClip();
	AnimatorController* controller = MakeController();
	AddParameter(*controller, "Speed", AnimatorParameterKind::Float, 1.5f);
	AddParameter(*controller, "Jump", AnimatorParameterKind::Trigger);

	const i32 leaf = AddLeaf(*controller, clip);
	controller->nodes[static_cast<size_t>(leaf)].clipGuid = Guid(0x0123456789abcdefULL, 0xfedcba9876543210ULL);

	AnimatorController::MotionNode inner;
	inner.kind = AnimatorBlendKind::Simple1D;
	inner.parameterX = "Speed";
	AnimatorController::MotionChild innerChild;
	innerChild.node = leaf;
	innerChild.position = Vec2(2.0f, 0.0f);
	innerChild.speed = 1.75f;
	inner.children.push_back(innerChild);
	controller->nodes.push_back(std::move(inner));
	const i32 innerIndex = static_cast<i32>(controller->nodes.size()) - 1;

	AnimatorController::MotionNode outer;
	outer.kind = AnimatorBlendKind::FreeformDirectional2D;
	outer.parameterX = "X";
	outer.parameterY = "Y";
	AnimatorController::MotionChild outerChild;
	outerChild.node = innerIndex;
	outerChild.position = Vec2(-0.5f, 0.25f);
	outerChild.parameter = "Direct";
	outer.children.push_back(outerChild);
	controller->nodes.push_back(std::move(outer));

	AnimatorController::StateMachine child;
	child.name = "Locomotion";
	child.parent = 0;
	child.defaultState = 0;
	AnimatorController::Transition any;
	any.destination = 0;
	any.exitTime = 0.5f;
	any.duration = 0.125f;
	any.offset = 0.75f;
	any.syncPhase = true;
	any.conditions.push_back(Greater("Speed", 3.25f));
	AnimatorController::Condition trigger;
	trigger.parameter = "Jump";
	trigger.mode = AnimatorConditionMode::IfNot;
	any.conditions.push_back(std::move(trigger));
	child.anyState.push_back(std::move(any));
	controller->machines.push_back(std::move(child));

	const i32 state = AddState(*controller, "Walk", innerIndex, 1);
	controller->states[static_cast<size_t>(state)].speed = 2.5f;
	controller->machines[0].defaultState = state;

	controller->layers[0].weight = 0.625f;
	controller->layers[0].blend = AnimationBlendMode::Additive;
	controller->layers[0].mask.push_back(AnimatorController::Layer::MaskEntry{"Spine2", 0.5f});
	controller->layers[0].mask.push_back(AnimatorController::Layer::MaskEntry{"Chest", 1.0f});

	auto expectSame = [&](const AnimatorController& read, const char* what) {
		ASSERT_EQ(read.parameters.size(), controller->parameters.size()) << what;
		EXPECT_EQ(read.parameters[0].name, "Speed") << what;
		EXPECT_FLOAT_EQ(read.parameters[0].defaultValue, 1.5f) << what;
		EXPECT_EQ(read.parameters[1].kind, AnimatorParameterKind::Trigger) << what;

		ASSERT_EQ(read.nodes.size(), controller->nodes.size()) << what;
		EXPECT_EQ(read.nodes[0].clipGuid, controller->nodes[0].clipGuid) << what;
		EXPECT_EQ(read.nodes[1].kind, AnimatorBlendKind::Simple1D) << what;
		ASSERT_EQ(read.nodes[1].children.size(), 1u) << what;
		EXPECT_FLOAT_EQ(read.nodes[1].children[0].position.x, 2.0f) << what;
		EXPECT_FLOAT_EQ(read.nodes[1].children[0].speed, 1.75f) << what;
		EXPECT_EQ(read.nodes[2].kind, AnimatorBlendKind::FreeformDirectional2D) << what;
		EXPECT_EQ(read.nodes[2].parameterY, "Y") << what;
		EXPECT_EQ(read.nodes[2].children[0].parameter, "Direct") << what;
		EXPECT_FLOAT_EQ(read.nodes[2].children[0].position.y, 0.25f) << what;

		ASSERT_EQ(read.machines.size(), 2u) << what;
		EXPECT_EQ(read.machines[1].parent, 0) << what;
		ASSERT_EQ(read.machines[1].anyState.size(), 1u) << what;
		const AnimatorController::Transition& edge = read.machines[1].anyState[0];
		EXPECT_FLOAT_EQ(edge.exitTime, 0.5f) << what;
		EXPECT_FLOAT_EQ(edge.duration, 0.125f) << what;
		EXPECT_FLOAT_EQ(edge.offset, 0.75f) << what;
		EXPECT_TRUE(edge.syncPhase) << what;
		ASSERT_EQ(edge.conditions.size(), 2u) << what;
		EXPECT_FLOAT_EQ(edge.conditions[0].threshold, 3.25f) << what;
		EXPECT_EQ(edge.conditions[1].mode, AnimatorConditionMode::IfNot) << what;

		ASSERT_EQ(read.states.size(), 1u) << what;
		EXPECT_EQ(read.states[0].machine, 1) << what;
		EXPECT_FLOAT_EQ(read.states[0].speed, 2.5f) << what;

		ASSERT_EQ(read.layers.size(), 1u) << what;
		EXPECT_FLOAT_EQ(read.layers[0].weight, 0.625f) << what;
		EXPECT_EQ(read.layers[0].blend, AnimationBlendMode::Additive) << what;
		ASSERT_EQ(read.layers[0].mask.size(), 2u) << what;
		EXPECT_EQ(read.layers[0].mask[1].bone, "Chest") << what;
		EXPECT_FLOAT_EQ(read.layers[0].mask[0].weight, 0.5f) << what;
	};

	const Path file((_root / "round.controller").generic_string());
	ASSERT_TRUE(controller->Export(file));
	AnimatorController fromFile;
	ASSERT_TRUE(fromFile.Import(file));
	expectSame(fromFile, "through the .controller file");

	Vector<u8> descriptorBytes{GetMemoryResource(MemTag::Asset)};
	Vector<u8> payloadBytes{GetMemoryResource(MemTag::Asset)};
	PakStringPool pool;
	CookWriter descriptor(descriptorBytes, pool);
	CookWriter payload(payloadBytes, pool);
	ASSERT_TRUE(controller->Cook(descriptor, payload));
	EXPECT_TRUE(payloadBytes.empty());

	Vector<u8> stringBytes{GetMemoryResource(MemTag::Asset)};
	PakWriteStringBlob(stringBytes, pool.Texts());
	PakStringTable table;
	ASSERT_TRUE(PakReadStringBlob(stringBytes.data(), stringBytes.size(), table));

	CookReader descriptorReader(descriptorBytes.data(), descriptorBytes.size(), table);
	CookReader payloadReader(payloadBytes.data(), payloadBytes.size(), table);
	AnimatorController fromCook;
	ASSERT_TRUE(fromCook.LoadCooked(descriptorReader, payloadReader));
	expectSame(fromCook, "through the cook");

	for (size_t keep = 0; keep < descriptorBytes.size(); keep += 4) {
		Vector<u8> truncated{GetMemoryResource(MemTag::Asset)};
		truncated.assign(descriptorBytes.begin(), descriptorBytes.begin() + static_cast<ptrdiff_t>(keep));
		CookReader shortReader(truncated.data(), truncated.size(), table);
		CookReader emptyPayload(payloadBytes.data(), payloadBytes.size(), table);
		AnimatorController broken;
		EXPECT_FALSE(broken.LoadCooked(shortReader, emptyPayload)) << keep << " of " << descriptorBytes.size();
		EXPECT_TRUE(broken.states.empty()) << keep << " bytes";
	}

	{
		Vector<u8> corrupt{GetMemoryResource(MemTag::Asset)};
		corrupt.assign(descriptorBytes.begin(), descriptorBytes.end());
		ASSERT_GE(corrupt.size(), sizeof(u32));
		const u32 absurd = 0xFFFFFFF0u;
		std::memcpy(corrupt.data(), &absurd, sizeof(absurd));
		CookReader corruptReader(corrupt.data(), corrupt.size(), table);
		CookReader emptyPayload(payloadBytes.data(), payloadBytes.size(), table);
		AnimatorController broken;
		EXPECT_FALSE(broken.LoadCooked(corruptReader, emptyPayload));
		EXPECT_TRUE(broken.parameters.empty());
	}
}

TEST_F(AnimationLayeringFixture, ControllerAndScriptCompose) {
	SkeletonAsset* skeleton = MakeSpine();
	AnimationClip* locomotion = MakeClip("project://locomotion.clip");
	AddConstantRotation(*locomotion, {"Hips", "Spine1", "Spine2", "Chest", "Arm"}, 20.0f);
	AnimationClip* wave = MakeClip("project://wave.clip");
	AddConstantRotation(*wave, {"Hips", "Spine1", "Spine2", "Chest", "Arm"}, 80.0f);

	AnimatorController* controller = MakeController();
	const i32 walk = AddState(*controller, "Walk", AddLeaf(*controller, locomotion));
	controller->machines[0].defaultState = walk;

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetApplyRootMotion(false);
	Handle<AnimatorStateMachine> driver = rig->AddComponent<AnimatorStateMachine>();
	driver->SetController(Handle<AnimatorController>(controller));

	RunUpdate(0.05f);
	ASSERT_EQ(driver->GetCurrentState(0), walk);
	EXPECT_EQ(animator->GetLayerCount(), 1u);
	EXPECT_NEAR(BoneAngle(rig, 0), 20.0f, 1e-2f);

	const i32 scriptLayer = animator->AddLayer();
	ASSERT_EQ(scriptLayer, 1);
	AnimationLayer* upper = animator->GetLayer(scriptLayer);
	ASSERT_NE(upper, nullptr);
	ASSERT_TRUE(upper->SetMaskSubtree(*skeleton, "Chest", 1.0f));
	animator->Play(Handle<AnimationClip>(wave), 0.0f, scriptLayer);

	RunUpdate(0.05f);
	EXPECT_NEAR(BoneAngle(rig, 0), 20.0f, 1e-2f);
	EXPECT_NEAR(BoneAngle(rig, 2), 20.0f, 1e-2f);
	EXPECT_NEAR(BoneAngle(rig, 3), 80.0f, 1e-2f);
	EXPECT_NEAR(BoneAngle(rig, 4), 80.0f, 1e-2f);
	EXPECT_EQ(driver->GetCurrentState(0), walk);

	for (i32 frame = 0; frame < 5; ++frame) {
		RunUpdate(0.05f);
	}
	EXPECT_NEAR(BoneAngle(rig, 0), 20.0f, 1e-2f);
	EXPECT_NEAR(BoneAngle(rig, 4), 80.0f, 1e-2f);
	EXPECT_EQ(animator->GetLayerCount(), 2u);
}

TEST_F(AnimationLayeringFixture, MaskedBlendTreeIsConvexBlend) {
	SkeletonAsset* skeleton = MakeSpine();
	AnimationClip* base = MakeClip("project://base.clip");
	AddConstantRotation(*base, {"Hips", "Spine1", "Spine2", "Chest", "Arm"}, 0.0f);
	AnimationClip* low = MakeClip("project://low.clip");
	AddConstantRotation(*low, {"Hips", "Spine1", "Spine2", "Chest", "Arm"}, 30.0f);
	AnimationClip* high = MakeClip("project://high.clip");
	AddConstantRotation(*high, {"Hips", "Spine1", "Spine2", "Chest", "Arm"}, 90.0f);

	AnimatorController* controller = MakeController();
	AddParameter(*controller, "Speed", AnimatorParameterKind::Float);

	const i32 baseState = AddState(*controller, "Base", AddLeaf(*controller, base));
	controller->machines[0].defaultState = baseState;

	AnimatorController::MotionNode tree;
	tree.kind = AnimatorBlendKind::Simple1D;
	tree.parameterX = "Speed";
	for (AnimationClip* clip : {low, high}) {
		AnimatorController::MotionChild child;
		child.node = AddLeaf(*controller, clip);
		child.position = Vec2(clip == low ? 0.0f : 1.0f, 0.0f);
		tree.children.push_back(child);
	}
	controller->nodes.push_back(std::move(tree));
	const i32 treeIndex = static_cast<i32>(controller->nodes.size()) - 1;

	AnimatorController::StateMachine upperMachine;
	upperMachine.name = "Upper";
	upperMachine.parent = -1;
	controller->machines.push_back(std::move(upperMachine));
	const i32 upperState = AddState(*controller, "Upper", treeIndex, 1);
	controller->machines[1].defaultState = upperState;

	AnimatorController::Layer upper;
	upper.name = "Upper";
	upper.machine = 1;
	upper.weight = 0.5f;
	upper.mask.push_back(AnimatorController::Layer::MaskEntry{"Spine2", 0.5f});
	upper.mask.push_back(AnimatorController::Layer::MaskEntry{"Chest", 1.0f});
	controller->layers.push_back(std::move(upper));

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetApplyRootMotion(false);
	Handle<AnimatorStateMachine> driver = rig->AddComponent<AnimatorStateMachine>();
	driver->SetController(Handle<AnimatorController>(controller));
	ASSERT_TRUE(driver->SetFloat("Speed", 0.5f));

	RunUpdate(0.05f);
	RunUpdate(0.05f);

	ASSERT_EQ(animator->GetLayerCount(), 2u);
	const AnimationLayer* layer = animator->GetLayer(1);
	ASSERT_NE(layer, nullptr);
	EXPECT_EQ(layer->GetMotionCount(), 2u);

	EXPECT_NEAR(BoneAngle(rig, 0), 0.0f, 1e-2f);
	EXPECT_NEAR(BoneAngle(rig, 1), 0.0f, 1e-2f);
	EXPECT_NEAR(BoneAngle(rig, 2), 15.0f, 2e-2f);
	EXPECT_NEAR(BoneAngle(rig, 3), 30.0f, 2e-2f);
	EXPECT_NEAR(BoneAngle(rig, 4), 30.0f, 2e-2f);

	const u32 boundBefore = animator->GetBoundTrackCount();
	ASSERT_GT(boundBefore, 0u);
	ASSERT_TRUE(driver->SetFloat("Speed", 1.0f));
	RunUpdate(0.05f);
	EXPECT_EQ(animator->GetBoundTrackCount(), boundBefore);
	EXPECT_EQ(layer->GetMotionCount(), 2u);
	EXPECT_NEAR(BoneAngle(rig, 3), 45.0f, 2e-2f);
}

TEST_F(AnimationLayeringFixture, NestedBlendTreeFlattensShares) {
	SkeletonAsset* skeleton = MakeSpine();
	AnimationClip* clips[4];
	const f32 angles[4] = {0.0f, 40.0f, 80.0f, 120.0f};
	for (i32 index = 0; index < 4; ++index) {
		clips[index] = MakeClip("project://leaf.clip");
		AddConstantRotation(*clips[index], {"Chest"}, angles[index]);
	}

	AnimatorController* controller = MakeController();
	AddParameter(*controller, "Outer", AnimatorParameterKind::Float);
	AddParameter(*controller, "Inner", AnimatorParameterKind::Float);

	auto makeInner = [&](i32 first, i32 second) {
		AnimatorController::MotionNode inner;
		inner.kind = AnimatorBlendKind::Simple1D;
		inner.parameterX = "Inner";
		AnimatorController::MotionChild a;
		a.node = AddLeaf(*controller, clips[first]);
		a.position = Vec2(0.0f, 0.0f);
		inner.children.push_back(a);
		AnimatorController::MotionChild b;
		b.node = AddLeaf(*controller, clips[second]);
		b.position = Vec2(1.0f, 0.0f);
		inner.children.push_back(b);
		controller->nodes.push_back(std::move(inner));
		return static_cast<i32>(controller->nodes.size()) - 1;
	};
	const i32 innerA = makeInner(0, 1);
	const i32 innerB = makeInner(2, 3);

	AnimatorController::MotionNode outer;
	outer.kind = AnimatorBlendKind::Simple1D;
	outer.parameterX = "Outer";
	for (i32 node : {innerA, innerB}) {
		AnimatorController::MotionChild child;
		child.node = node;
		child.position = Vec2(node == innerA ? 0.0f : 1.0f, 0.0f);
		outer.children.push_back(child);
	}
	controller->nodes.push_back(std::move(outer));
	const i32 outerIndex = static_cast<i32>(controller->nodes.size()) - 1;

	const i32 state = AddState(*controller, "Move", outerIndex);
	controller->machines[0].defaultState = state;

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetApplyRootMotion(false);
	Handle<AnimatorStateMachine> driver = rig->AddComponent<AnimatorStateMachine>();
	driver->SetController(Handle<AnimatorController>(controller));
	driver->SetFloat("Outer", 0.5f);
	driver->SetFloat("Inner", 0.5f);

	RunUpdate(0.05f);
	const AnimationLayer* layer = animator->GetLayer(0);
	ASSERT_NE(layer, nullptr);
	EXPECT_EQ(layer->GetMotionCount(), 4u);
	for (u32 index = 0; index < 4; ++index) {
		EXPECT_NEAR(layer->GetMotion(index).weight, 0.25f, 1e-4f) << "leaf " << index;
	}
	EXPECT_NEAR(BoneAngle(rig, 3), 60.0f, 5e-2f);

	driver->SetFloat("Outer", 1.0f);
	RunUpdate(0.05f);
	EXPECT_NEAR(layer->GetMotion(0).weight, 0.0f, 1e-4f);
	EXPECT_NEAR(layer->GetMotion(2).weight, 0.5f, 1e-4f);
	EXPECT_NEAR(BoneAngle(rig, 3), 100.0f, 5e-2f);
}

TEST_F(AnimationLayeringFixture, ControllerTakesOverFromClip) {
	SkeletonAsset* skeleton = MakeSpine();
	AnimationClip* authored = MakeClip("project://authored.clip");
	AddConstantRotation(*authored, {"Chest"}, 25.0f);
	AnimationClip* driven = MakeClip("project://driven.clip");
	AddConstantRotation(*driven, {"Chest"}, 70.0f);

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetApplyRootMotion(false);
	animator->SetClip(Handle<AnimationClip>(authored));

	RunUpdate(0.05f);
	EXPECT_NEAR(BoneAngle(rig, 3), 25.0f, 1e-2f);

	AnimatorController* controller = MakeController();
	const i32 state = AddState(*controller, "Driven", AddLeaf(*controller, driven));
	controller->machines[0].defaultState = state;
	Handle<AnimatorStateMachine> driver = rig->AddComponent<AnimatorStateMachine>();
	driver->SetController(Handle<AnimatorController>(controller));

	RunUpdate(0.05f);
	EXPECT_NEAR(BoneAngle(rig, 3), 70.0f, 1e-2f);
	EXPECT_EQ(animator->GetClip().GetInstanceId(), Handle<AnimationClip>(driven).GetInstanceId());

	for (i32 frame = 0; frame < 5; ++frame) {
		RunUpdate(0.05f);
		ASSERT_NEAR(BoneAngle(rig, 3), 70.0f, 1e-2f) << frame;
	}

	driver->SetEnabled(false);
	animator->Play(Handle<AnimationClip>(authored), 0.0f, 0);
	RunUpdate(0.05f);
	EXPECT_NEAR(BoneAngle(rig, 3), 25.0f, 1e-2f);
}

TEST_F(AnimationLayeringFixture, BlendFiresDominantMotionEvents) {
	SkeletonAsset* skeleton = MakeSpine();

	auto makeLocomotion = [&](f32 metres, const char* eventName) {
		AnimationClip* clip = MakeClip("project://locomotion.clip");
		AnimationClip::Track track;
		track.kind = AnimationTrackKind::BonePosition;
		track.interp = AnimationInterpolation::Linear;
		track.name = "Hips";
		track.times.push_back(0.0f);
		track.times.push_back(1.0f);
		track.values.push_back(Vec4(0.0f, 0.0f, 0.0f, 0.0f));
		track.values.push_back(Vec4(0.0f, 0.0f, metres, 0.0f));
		clip->tracks.push_back(std::move(track));
		AnimationClip::Track event;
		event.kind = AnimationTrackKind::Event;
		event.name = eventName;
		event.times.push_back(0.5f);
		event.values.push_back(Vec4{});
		clip->tracks.push_back(std::move(event));
		clip->length = 1.0f;
		clip->loop = AnimationLoopMode::Loop;
		clip->bakeRootMotionIntoPose = false;
		return clip;
	};
	AnimationClip* walk = makeLocomotion(1.0f, "WalkStep");
	AnimationClip* run = makeLocomotion(4.0f, "RunStep");

	AnimatorController* controller = MakeController();
	AddParameter(*controller, "Speed", AnimatorParameterKind::Float);
	AnimatorController::MotionNode tree;
	tree.kind = AnimatorBlendKind::Simple1D;
	tree.parameterX = "Speed";
	for (AnimationClip* clip : {walk, run}) {
		AnimatorController::MotionChild child;
		child.node = AddLeaf(*controller, clip);
		child.position = Vec2(clip == walk ? 0.0f : 1.0f, 0.0f);
		tree.children.push_back(child);
	}
	controller->nodes.push_back(std::move(tree));
	const i32 state = AddState(*controller, "Move", static_cast<i32>(controller->nodes.size()) - 1);
	controller->machines[0].defaultState = state;

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetApplyRootMotion(false);
	Handle<AnimatorStateMachine> driver = rig->AddComponent<AnimatorStateMachine>();
	driver->SetController(Handle<AnimatorController>(controller));

	static u32 _walkSteps = 0;
	static u32 _runSteps = 0;
	_walkSteps = 0;
	_runSteps = 0;
	animator->Subscribe(
		[](const AnimationEvent& event, void*) {
			if (event.name == StringHash("WalkStep")) {
				++_walkSteps;
			} else if (event.name == StringHash("RunStep")) {
				++_runSteps;
			}
		},
		nullptr);

	ASSERT_TRUE(driver->SetFloat("Speed", 0.5f));
	RunUpdate(0.05f);
	RunUpdate(0.1f);
	const Vec3 blended = animator->GetRootMotionDelta();
	EXPECT_NEAR(blended.z, 0.25f, 5e-3f);

	ASSERT_TRUE(driver->SetFloat("Speed", 0.0f));
	RunUpdate(0.1f);
	EXPECT_NEAR(animator->GetRootMotionDelta().z, 0.1f, 5e-3f);
	ASSERT_TRUE(driver->SetFloat("Speed", 1.0f));
	RunUpdate(0.1f);
	EXPECT_NEAR(animator->GetRootMotionDelta().z, 0.4f, 5e-3f);

	_walkSteps = 0;
	_runSteps = 0;
	for (i32 frame = 0; frame < 20; ++frame) {
		RunUpdate(0.05f);
	}
	EXPECT_GT(_runSteps, 0u);
	EXPECT_EQ(_walkSteps, 0u);
}

#endif
