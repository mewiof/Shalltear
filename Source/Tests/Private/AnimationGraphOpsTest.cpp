#include "TestsPCH.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
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
#include <cstring>
#include <filesystem>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <limits>

#ifndef SHALLTEAR_SHARED
#include "Shalltear/Animation/AnimationClip.h"
#include "Shalltear/Animation/AnimationLayer.h"
#include "Shalltear/Animation/Animator.h"
#include "Shalltear/Animation/AnimatorController.h"

#include "Controller/AnimatorGraphOps.h"
#endif

#ifdef SHALLTEAR_SHARED
#include "UITestUtil.h"

#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/AssetOpeners.h"
#include "Shalltear/Editor/EditorAssetOps.h"
#include "Shalltear/Editor/EditorDocuments.h"
#include "Shalltear/Editor/EditorExtensions.h"
#include "Shalltear/Editor/EditorGizmos.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/PropertyDrawers.h"
#include "Shalltear/Editor/UndoSystem.h"

#include "Shalltear/Core/Reflection/Walk.h"
#include "Shalltear/Core/Time.h"
#endif

using namespace Shalltear;

namespace {
	constexpr const char* PACKAGE_TARGET = "Animation";
}

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

	class AnimatorGraphOpsFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		Handle<Scene> _scene;
		char _token = 0;
		std::vector<Asset*> _owned;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_graph_ops_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			SLT_CALL_LINKED_PACKAGE(Animation, &_token);

			_scene = SceneSystem::CreateScene("graph-ops");
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

		AnimationClip* MakeClip(const char* path) {
			AnimationClip* clip = New<AnimationClip>(MemTag::Asset);
			clip->virtualPath = path;
			_owned.push_back(clip);
			return clip;
		}

		AnimatorController* MakeController() {
			AnimatorController* controller = New<AnimatorController>(MemTag::Asset);
			controller->virtualPath = "project://Assets/Graph.controller";
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

	void MakeBareGraph(AnimatorController& controller) {
		AnimatorController::StateMachine root;
		root.name = "Base Layer";
		controller.machines.push_back(std::move(root));
	}

} // anonymous namespace

TEST_F(AnimatorGraphOpsFixture, WireCreatesTakenTransition) {
	SkeletonAsset* skeleton = MakeSpine();
	AnimationClip* idle = MakeClip("project://idle.clip");
	AddConstantRotation(*idle, {"Chest"}, 0.0f);
	AnimationClip* walk = MakeClip("project://walk.clip");
	AddConstantRotation(*walk, {"Chest"}, 90.0f);

	AnimatorController* controller = MakeController();

	const i32 idleState = AnimatorGraphOps::AddState(*controller, 0, "Idle", Vec2(10.0f, 20.0f));
	const i32 walkState = AnimatorGraphOps::AddState(*controller, 0, "Walk", Vec2(300.0f, 20.0f));
	ASSERT_EQ(idleState, 0);
	ASSERT_EQ(walkState, 1);
	EXPECT_EQ(controller->machines[0].defaultState, idleState);

	ASSERT_EQ(AnimatorGraphOps::SetStateClip(*controller, idleState, Guid{}, Handle<AnimationClip>(idle)), 0);
	ASSERT_EQ(AnimatorGraphOps::SetStateClip(*controller, walkState, Guid{}, Handle<AnimationClip>(walk)), 1);
	ASSERT_GE(AnimatorGraphOps::AddParameter(*controller, "Speed", AnimatorParameterKind::Float), 0);

	const i32 edge = AnimatorGraphOps::AddTransition(*controller, idleState, walkState);
	ASSERT_GE(edge, 0);
	AnimatorController::Transition& wired =
		controller->states[static_cast<size_t>(idleState)].transitions[static_cast<size_t>(edge)];
	wired.duration = 0.0f;
	AnimatorController::Condition condition;
	condition.parameter = "Speed";
	condition.mode = AnimatorConditionMode::Greater;
	condition.threshold = 0.5f;
	wired.conditions.push_back(std::move(condition));

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetApplyRootMotion(false);
	Handle<AnimatorStateMachine> driver = rig->AddComponent<AnimatorStateMachine>();
	driver->SetController(Handle<AnimatorController>(controller));

	RunUpdate(0.05f);
	ASSERT_EQ(driver->GetCurrentState(0), idleState);
	EXPECT_NEAR(BoneAngle(rig, 3), 0.0f, 1e-2f);

	EXPECT_TRUE(driver->SetFloat("Speed", 1.0f));
	RunUpdate(0.05f);
	EXPECT_EQ(driver->GetCurrentState(0), walkState);
	RunUpdate(0.05f);
	EXPECT_NEAR(BoneAngle(rig, 3), 90.0f, 1e-2f);
}

TEST_F(AnimatorGraphOpsFixture, StateDeleteDropsAndShiftsEdges) {
	AnimatorController controller;
	MakeBareGraph(controller);

	const i32 idle = AnimatorGraphOps::AddState(controller, 0, "Idle", Vec2{});
	const i32 walk = AnimatorGraphOps::AddState(controller, 0, "Walk", Vec2{});
	const i32 run = AnimatorGraphOps::AddState(controller, 0, "Run", Vec2{});
	ASSERT_EQ(run, 2);

	AnimatorGraphOps::AddTransition(controller, idle, walk);
	AnimatorGraphOps::AddTransition(controller, idle, run);
	AnimatorGraphOps::AddTransition(controller, walk, run);
	AnimatorGraphOps::AddAnyStateTransition(controller, 0, walk);
	AnimatorGraphOps::AddAnyStateTransition(controller, 0, run);

	ASSERT_TRUE(AnimatorGraphOps::RemoveState(controller, walk));

	ASSERT_EQ(controller.states.size(), 2u);
	EXPECT_EQ(controller.states[0].name, "Idle");
	EXPECT_EQ(controller.states[1].name, "Run");

	ASSERT_EQ(controller.states[0].transitions.size(), 1u);
	EXPECT_EQ(controller.states[0].transitions[0].destination, 1);

	ASSERT_EQ(controller.machines[0].anyState.size(), 1u);
	EXPECT_EQ(controller.machines[0].anyState[0].destination, 1);
}

TEST_F(AnimatorGraphOpsFixture, DefaultStateDeleteKeepsMachineEnterable) {
	AnimatorController controller;
	MakeBareGraph(controller);

	const i32 idle = AnimatorGraphOps::AddState(controller, 0, "Idle", Vec2{});
	AnimatorGraphOps::AddState(controller, 0, "Walk", Vec2{});
	ASSERT_EQ(controller.machines[0].defaultState, idle);

	ASSERT_TRUE(AnimatorGraphOps::RemoveState(controller, idle));
	EXPECT_EQ(controller.machines[0].defaultState, 0);
	EXPECT_EQ(controller.states[0].name, "Walk");

	ASSERT_TRUE(AnimatorGraphOps::RemoveState(controller, 0));
	EXPECT_EQ(controller.machines[0].defaultState, -1);
}

TEST_F(AnimatorGraphOpsFixture, DefaultPastDeletedStateShiftsNotStaying) {
	AnimatorController controller;
	MakeBareGraph(controller);

	AnimatorGraphOps::AddState(controller, 0, "Idle", Vec2{});
	AnimatorGraphOps::AddState(controller, 0, "Walk", Vec2{});
	ASSERT_TRUE(AnimatorGraphOps::SetDefaultState(controller, 0, 1));

	ASSERT_TRUE(AnimatorGraphOps::RemoveState(controller, 0));
	EXPECT_EQ(controller.machines[0].defaultState, 0);
	EXPECT_EQ(controller.states[0].name, "Walk");
}

TEST_F(AnimatorGraphOpsFixture, StateDeleteCollectsOrphanMotion) {
	AnimatorController controller;
	MakeBareGraph(controller);

	AnimatorController::MotionNode doomedLeaf;
	doomedLeaf.kind = AnimatorBlendKind::Clip;
	controller.nodes.push_back(std::move(doomedLeaf));
	AnimatorController::MotionNode keptLeaf;
	keptLeaf.kind = AnimatorBlendKind::Clip;
	controller.nodes.push_back(std::move(keptLeaf));
	AnimatorController::MotionNode tree;
	tree.kind = AnimatorBlendKind::Simple1D;
	tree.children.push_back(AnimatorController::MotionChild{1, Vec2{}, 1.0f, str{}});
	controller.nodes.push_back(std::move(tree));

	const i32 doomed = AnimatorGraphOps::AddState(controller, 0, "Doomed", Vec2{});
	const i32 kept = AnimatorGraphOps::AddState(controller, 0, "Kept", Vec2{});
	controller.states[static_cast<size_t>(doomed)].motion = 0;
	controller.states[static_cast<size_t>(kept)].motion = 2;

	ASSERT_TRUE(AnimatorGraphOps::RemoveState(controller, doomed));

	ASSERT_EQ(controller.nodes.size(), 2u);
	ASSERT_EQ(controller.states.size(), 1u);
	EXPECT_EQ(controller.states[0].motion, 1);
	ASSERT_EQ(controller.nodes[1].children.size(), 1u);
	EXPECT_EQ(controller.nodes[1].children[0].node, 0);
}

TEST_F(AnimatorGraphOpsFixture, NodeSweepEndsOnSelfReference) {
	AnimatorController controller;
	MakeBareGraph(controller);

	AnimatorController::MotionNode loop;
	loop.kind = AnimatorBlendKind::Simple1D;
	loop.children.push_back(AnimatorController::MotionChild{0, Vec2{}, 1.0f, str{}});
	controller.nodes.push_back(std::move(loop));

	const i32 held = AnimatorGraphOps::AddState(controller, 0, "Held", Vec2{});
	const i32 doomed = AnimatorGraphOps::AddState(controller, 0, "Doomed", Vec2{});
	controller.states[static_cast<size_t>(held)].motion = 0;

	ASSERT_TRUE(AnimatorGraphOps::RemoveState(controller, doomed));
	EXPECT_EQ(controller.nodes.size(), 1u);
	EXPECT_EQ(controller.states[0].motion, 0);
}

TEST_F(AnimatorGraphOpsFixture, StateClipRePointsLeaf) {
	AnimatorController controller;
	MakeBareGraph(controller);
	const i32 state = AnimatorGraphOps::AddState(controller, 0, "Idle", Vec2{});

	const Guid first(0x1111u, 0x2222u);
	const Guid second(0x3333u, 0x4444u);
	const i32 node = AnimatorGraphOps::SetStateClip(controller, state, first, {});
	ASSERT_EQ(node, 0);
	ASSERT_EQ(controller.nodes.size(), 1u);

	EXPECT_EQ(AnimatorGraphOps::SetStateClip(controller, state, second, {}), node);
	EXPECT_EQ(controller.nodes.size(), 1u);
	EXPECT_EQ(controller.nodes[0].clipGuid, second);

	controller.nodes[0].kind = AnimatorBlendKind::FreeformDirectional2D;
	const i32 minted = AnimatorGraphOps::SetStateClip(controller, state, first, {});
	EXPECT_EQ(minted, 1);
	EXPECT_EQ(controller.nodes.size(), 2u);
	EXPECT_EQ(controller.nodes[0].kind, AnimatorBlendKind::FreeformDirectional2D);
}

TEST_F(AnimatorGraphOpsFixture, ParameterRenameRewritesAllReaders) {
	AnimatorController controller;
	MakeBareGraph(controller);
	ASSERT_EQ(AnimatorGraphOps::AddParameter(controller, "Speed", AnimatorParameterKind::Float), 0);

	AnimatorController::MotionNode tree;
	tree.kind = AnimatorBlendKind::FreeformCartesian2D;
	tree.parameterX = "Speed";
	tree.parameterY = "Speed";
	tree.children.push_back(AnimatorController::MotionChild{-1, Vec2{}, 1.0f, str("Speed")});
	controller.nodes.push_back(std::move(tree));

	const i32 from = AnimatorGraphOps::AddState(controller, 0, "Idle", Vec2{});
	const i32 to = AnimatorGraphOps::AddState(controller, 0, "Walk", Vec2{});
	const i32 edge = AnimatorGraphOps::AddTransition(controller, from, to);
	controller.states[static_cast<size_t>(from)].transitions[static_cast<size_t>(edge)].conditions.push_back(
		AnimatorController::Condition{str("Speed"), AnimatorConditionMode::Greater, 0.5f});
	const i32 anyEdge = AnimatorGraphOps::AddAnyStateTransition(controller, 0, to);
	controller.machines[0].anyState[static_cast<size_t>(anyEdge)].conditions.push_back(
		AnimatorController::Condition{str("Speed"), AnimatorConditionMode::Less, 0.1f});

	ASSERT_TRUE(AnimatorGraphOps::RenameParameter(controller, 0, "Velocity"));

	EXPECT_EQ(controller.parameters[0].name, "Velocity");
	EXPECT_EQ(controller.states[static_cast<size_t>(from)].transitions[0].conditions[0].parameter, "Velocity");
	EXPECT_EQ(controller.machines[0].anyState[0].conditions[0].parameter, "Velocity");
	EXPECT_EQ(controller.nodes[0].parameterX, "Velocity");
	EXPECT_EQ(controller.nodes[0].parameterY, "Velocity");
	EXPECT_EQ(controller.nodes[0].children[0].parameter, "Velocity");
}

TEST_F(AnimatorGraphOpsFixture, RenameToTakenNameRefused) {
	AnimatorController controller;
	MakeBareGraph(controller);
	ASSERT_EQ(AnimatorGraphOps::AddParameter(controller, "Speed", AnimatorParameterKind::Float), 0);
	ASSERT_EQ(AnimatorGraphOps::AddParameter(controller, "Grounded", AnimatorParameterKind::Bool), 1);
	EXPECT_EQ(AnimatorGraphOps::AddParameter(controller, "Speed", AnimatorParameterKind::Int), -1);

	EXPECT_FALSE(AnimatorGraphOps::RenameParameter(controller, 1, "Speed"));
	EXPECT_EQ(controller.parameters[1].name, "Grounded");
	EXPECT_TRUE(AnimatorGraphOps::RenameParameter(controller, 1, "Grounded"));
}

TEST_F(AnimatorGraphOpsFixture, ParameterRemovalDropsReaders) {
	AnimatorController controller;
	MakeBareGraph(controller);
	ASSERT_EQ(AnimatorGraphOps::AddParameter(controller, "Speed", AnimatorParameterKind::Float), 0);
	ASSERT_EQ(AnimatorGraphOps::AddParameter(controller, "Grounded", AnimatorParameterKind::Bool), 1);

	AnimatorController::MotionNode tree;
	tree.kind = AnimatorBlendKind::Simple1D;
	tree.parameterX = "Speed";
	controller.nodes.push_back(std::move(tree));

	const i32 from = AnimatorGraphOps::AddState(controller, 0, "Idle", Vec2{});
	const i32 to = AnimatorGraphOps::AddState(controller, 0, "Walk", Vec2{});
	const i32 edge = AnimatorGraphOps::AddTransition(controller, from, to);
	Vector<AnimatorController::Condition>& conditions =
		controller.states[static_cast<size_t>(from)].transitions[static_cast<size_t>(edge)].conditions;
	conditions.push_back(AnimatorController::Condition{str("Speed"), AnimatorConditionMode::Greater, 0.5f});
	conditions.push_back(AnimatorController::Condition{str("Grounded"), AnimatorConditionMode::If, 0.0f});

	ASSERT_TRUE(AnimatorGraphOps::RemoveParameter(controller, 0));

	ASSERT_EQ(controller.parameters.size(), 1u);
	ASSERT_EQ(conditions.size(), 1u);
	EXPECT_EQ(conditions[0].parameter, "Grounded");
	ASSERT_EQ(controller.nodes.size(), 1u);
	EXPECT_TRUE(controller.nodes[0].parameterX.empty());
}

TEST_F(AnimatorGraphOpsFixture, ParameterMoveRestartsPlayers) {
	AnimatorController controller;
	MakeBareGraph(controller);
	ASSERT_EQ(AnimatorGraphOps::AddParameter(controller, "A", AnimatorParameterKind::Float), 0);
	ASSERT_EQ(AnimatorGraphOps::AddParameter(controller, "B", AnimatorParameterKind::Int), 1);
	ASSERT_EQ(AnimatorGraphOps::AddParameter(controller, "C", AnimatorParameterKind::Bool), 2);
	auto order = [&controller]() {
		str names;
		for (const AnimatorController::Parameter& parameter : controller.parameters) {
			names += parameter.name;
		}
		return names;
	};

	const u32 before = controller.graphRevision;
	ASSERT_TRUE(AnimatorGraphOps::MoveParameter(controller, 0, 2));
	EXPECT_EQ(order(), "BCA");
	EXPECT_NE(controller.graphRevision, before);
	ASSERT_TRUE(AnimatorGraphOps::MoveParameter(controller, 2, 0));
	EXPECT_EQ(order(), "ABC");
	ASSERT_TRUE(AnimatorGraphOps::MoveParameter(controller, 1, 2));
	EXPECT_EQ(order(), "ACB");
	EXPECT_EQ(controller.parameters[2].kind, AnimatorParameterKind::Int);

	const u32 settled = controller.graphRevision;
	EXPECT_FALSE(AnimatorGraphOps::MoveParameter(controller, 1, 1));
	EXPECT_FALSE(AnimatorGraphOps::MoveParameter(controller, 0, 3));
	EXPECT_FALSE(AnimatorGraphOps::MoveParameter(controller, -1, 0));
	EXPECT_EQ(order(), "ACB");
	EXPECT_EQ(controller.graphRevision, settled);
}

TEST_F(AnimatorGraphOpsFixture, EntryWireRefusesStateInAnotherMachine) {
	AnimatorController controller;
	MakeBareGraph(controller);
	AnimatorController::StateMachine sub;
	sub.name = "Locomotion";
	sub.parent = 0;
	controller.machines.push_back(std::move(sub));

	const i32 outer = AnimatorGraphOps::AddState(controller, 0, "Idle", Vec2{});
	const i32 inner = AnimatorGraphOps::AddState(controller, 1, "Walk", Vec2{});

	EXPECT_TRUE(AnimatorGraphOps::SetDefaultState(controller, 0, outer));
	EXPECT_FALSE(AnimatorGraphOps::SetDefaultState(controller, 0, inner));
	EXPECT_EQ(controller.machines[0].defaultState, outer);
	EXPECT_TRUE(AnimatorGraphOps::SetDefaultState(controller, 1, inner));
}

TEST_F(AnimatorGraphOpsFixture, AutoLayoutSpreadsPileKeepsAuthored) {
	AnimatorController piled;
	MakeBareGraph(piled);
	AnimatorGraphOps::AddState(piled, 0, "Idle", Vec2{});
	AnimatorGraphOps::AddState(piled, 0, "Walk", Vec2{});
	AnimatorGraphOps::AddState(piled, 0, "Run", Vec2{});
	EXPECT_TRUE(AnimatorGraphOps::NeedsAutoLayout(piled, 0));

	ASSERT_TRUE(AnimatorGraphOps::AutoLayout(piled, 0));
	EXPECT_FALSE(AnimatorGraphOps::NeedsAutoLayout(piled, 0));
	EXPECT_FALSE(piled.states[0].position == piled.states[1].position);
	EXPECT_FALSE(piled.states[1].position == piled.states[2].position);
	EXPECT_FALSE(piled.machines[0].entryPosition == piled.machines[0].anyStatePosition);

	AnimatorController authored;
	MakeBareGraph(authored);
	AnimatorGraphOps::AddState(authored, 0, "Idle", Vec2(11.0f, 22.0f));
	AnimatorGraphOps::AddState(authored, 0, "Walk", Vec2(333.0f, 22.0f));
	EXPECT_TRUE(AnimatorGraphOps::NeedsAutoLayout(authored, 0));

	ASSERT_TRUE(AnimatorGraphOps::AutoLayout(authored, 0));
	EXPECT_FALSE(authored.machines[0].entryPosition == authored.machines[0].anyStatePosition);
	EXPECT_TRUE(authored.states[0].position == Vec2(11.0f, 22.0f));
	EXPECT_TRUE(authored.states[1].position == Vec2(333.0f, 22.0f));

	AnimatorController single;
	MakeBareGraph(single);
	AnimatorGraphOps::AddState(single, 0, "Idle", Vec2{});
	ASSERT_TRUE(AnimatorGraphOps::AutoLayout(single, 0));
	EXPECT_FALSE(AnimatorGraphOps::NeedsAutoLayout(single, 0));
	EXPECT_FALSE(AnimatorGraphOps::AutoLayout(single, 0));
	EXPECT_TRUE(single.states[0].position == Vec2{});
}

TEST_F(AnimatorGraphOpsFixture, NodeLayoutRoundTripsFileAndCook) {
	AnimatorController controller;
	AnimatorController::StateMachine root;
	root.name = "Base Layer";
	root.entryPosition = Vec2(-260.0f, 0.0f);
	root.anyStatePosition = Vec2(-260.0f, 144.0f);
	controller.machines.push_back(std::move(root));
	AnimatorController::StateMachine sub;
	sub.name = "Locomotion";
	sub.parent = 0;
	sub.position = Vec2(520.0f, 96.0f);
	sub.upPosition = Vec2(-77.5f, 188.25f);
	controller.machines.push_back(std::move(sub));
	AnimatorController::Layer base;
	base.name = "Base Layer";
	base.machine = 0;
	controller.layers.push_back(std::move(base));

	AnimatorGraphOps::AddState(controller, 0, "Idle", Vec2(11.5f, 22.25f));
	AnimatorGraphOps::AddState(controller, 0, "Walk", Vec2(-303.75f, 404.5f));

	auto assertLayout = [](const AnimatorController& read, const char* where) {
		ASSERT_EQ(read.states.size(), 2u) << where;
		ASSERT_EQ(read.machines.size(), 2u) << where;
		EXPECT_FLOAT_EQ(read.states[0].position.x, 11.5f) << where;
		EXPECT_FLOAT_EQ(read.states[0].position.y, 22.25f) << where;
		EXPECT_FLOAT_EQ(read.states[1].position.x, -303.75f) << where;
		EXPECT_FLOAT_EQ(read.states[1].position.y, 404.5f) << where;
		EXPECT_FLOAT_EQ(read.machines[0].entryPosition.x, -260.0f) << where;
		EXPECT_FLOAT_EQ(read.machines[0].anyStatePosition.y, 144.0f) << where;
		EXPECT_FLOAT_EQ(read.machines[1].position.x, 520.0f) << where;
		EXPECT_FLOAT_EQ(read.machines[1].position.y, 96.0f) << where;
		EXPECT_FLOAT_EQ(read.machines[1].upPosition.x, -77.5f) << where;
		EXPECT_FLOAT_EQ(read.machines[1].upPosition.y, 188.25f) << where;
	};

	const Path file((_root / "Locomotion.controller").generic_string());
	ASSERT_TRUE(controller.Export(file));
	AnimatorController fromFile;
	ASSERT_TRUE(fromFile.Import(file));
	assertLayout(fromFile, "through the .controller file");

	Vector<u8> descriptorBytes{GetMemoryResource(MemTag::Asset)};
	Vector<u8> payloadBytes{GetMemoryResource(MemTag::Asset)};
	PakStringPool pool;
	CookWriter descriptor(descriptorBytes, pool);
	CookWriter payload(payloadBytes, pool);
	ASSERT_TRUE(controller.Cook(descriptor, payload));

	Vector<u8> stringBytes{GetMemoryResource(MemTag::Asset)};
	PakWriteStringBlob(stringBytes, pool.Texts());
	PakStringTable table;
	ASSERT_TRUE(PakReadStringBlob(stringBytes.data(), stringBytes.size(), table));
	CookReader descriptorReader(descriptorBytes.data(), descriptorBytes.size(), table);
	CookReader payloadReader(payloadBytes.data(), payloadBytes.size(), table);
	AnimatorController fromCook;
	ASSERT_TRUE(fromCook.LoadCooked(descriptorReader, payloadReader));
	assertLayout(fromCook, "through the cook (what the artifact cache round-trips)");
}

TEST_F(AnimatorGraphOpsFixture, ReadoutNamesStateTwoMachinesDeep) {
	SkeletonAsset* skeleton = MakeSpine();
	AnimationClip* idle = MakeClip("project://idle.clip");
	AddConstantRotation(*idle, {"Chest"}, 0.0f);
	AnimationClip* attack = MakeClip("project://attack.clip");
	AddConstantRotation(*attack, {"Chest"}, 90.0f);

	AnimatorController* controller = MakeController();

	const i32 locomotion = AnimatorGraphOps::AddMachine(*controller, 0, "Locomotion", Vec2(200.0f, 0.0f));
	const i32 combat = AnimatorGraphOps::AddMachine(*controller, locomotion, "Combat", Vec2(400.0f, 0.0f));
	ASSERT_GE(locomotion, 0);
	ASSERT_GE(combat, 0);

	const i32 idleState = AnimatorGraphOps::AddState(*controller, 0, "Idle", Vec2{});
	const i32 attackState = AnimatorGraphOps::AddState(*controller, combat, "Attack", Vec2{});
	ASSERT_EQ(AnimatorGraphOps::SetStateClip(*controller, idleState, Guid{}, Handle<AnimationClip>(idle)), 0);
	ASSERT_EQ(AnimatorGraphOps::SetStateClip(*controller, attackState, Guid{}, Handle<AnimationClip>(attack)), 1);
	ASSERT_GE(AnimatorGraphOps::AddParameter(*controller, "Fight", AnimatorParameterKind::Bool), 0);

	const i32 edge = AnimatorGraphOps::AddAnyStateTransition(*controller, 0, attackState);
	ASSERT_GE(edge, 0);
	AnimatorController::Transition& wired = controller->machines[0].anyState[static_cast<size_t>(edge)];
	wired.duration = 0.0f;
	AnimatorController::Condition condition;
	condition.parameter = "Fight";
	condition.mode = AnimatorConditionMode::If;
	wired.conditions.push_back(std::move(condition));

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetApplyRootMotion(false);
	Handle<AnimatorStateMachine> driver = rig->AddComponent<AnimatorStateMachine>();
	driver->SetController(Handle<AnimatorController>(controller));

	RunUpdate(0.05f);
	str path;
	ASSERT_TRUE(AnimatorGraphOps::StatePath(*controller, driver->GetCurrentState(0), path));
	EXPECT_EQ(path, "Base Layer/Idle");

	EXPECT_TRUE(driver->SetBool("Fight", true));
	RunUpdate(0.05f);
	RunUpdate(0.05f);

	const i32 running = driver->GetCurrentState(0);
	ASSERT_EQ(running, attackState);
	ASSERT_TRUE(AnimatorGraphOps::StatePath(*controller, running, path));
	EXPECT_EQ(path, "Base Layer/Locomotion/Combat/Attack");
	EXPECT_NEAR(BoneAngle(rig, 3), 90.0f, 1e-2f);
}

TEST_F(AnimatorGraphOpsFixture, AncestryRootFirstEndsOnSelfReference) {
	AnimatorController controller;
	MakeBareGraph(controller);
	const i32 middle = AnimatorGraphOps::AddMachine(controller, 0, "Locomotion", Vec2{});
	const i32 leaf = AnimatorGraphOps::AddMachine(controller, middle, "Combat", Vec2{});

	i32 chain[8];
	ASSERT_EQ(AnimatorGraphOps::MachineAncestry(controller, leaf, chain, 8), 3u);
	EXPECT_EQ(chain[0], 0);
	EXPECT_EQ(chain[1], middle);
	EXPECT_EQ(chain[2], leaf);

	EXPECT_TRUE(AnimatorGraphOps::MachineContains(controller, 0, leaf));
	EXPECT_TRUE(AnimatorGraphOps::MachineContains(controller, middle, leaf));
	EXPECT_FALSE(AnimatorGraphOps::MachineContains(controller, leaf, middle));

	controller.machines[static_cast<size_t>(middle)].parent = leaf;
	i32 cycle[8];
	const u32 written = AnimatorGraphOps::MachineAncestry(controller, leaf, cycle, 8);
	EXPECT_LE(written, 8u);
	EXPECT_FALSE(AnimatorGraphOps::MachineContains(controller, 0, leaf));
}

TEST_F(AnimatorGraphOpsFixture, SubMachineDeleteShiftsIndices) {
	AnimatorController controller;
	MakeBareGraph(controller);
	const i32 doomed = AnimatorGraphOps::AddMachine(controller, 0, "Doomed", Vec2{});
	const i32 nested = AnimatorGraphOps::AddMachine(controller, doomed, "Nested", Vec2{});
	const i32 survivor = AnimatorGraphOps::AddMachine(controller, 0, "Survivor", Vec2{});
	ASSERT_EQ(doomed, 1);
	ASSERT_EQ(nested, 2);
	ASSERT_EQ(survivor, 3);

	const i32 outside = AnimatorGraphOps::AddState(controller, 0, "Outside", Vec2{});
	const i32 inside = AnimatorGraphOps::AddState(controller, doomed, "Inside", Vec2{});
	const i32 deeper = AnimatorGraphOps::AddState(controller, nested, "Deeper", Vec2{});
	const i32 kept = AnimatorGraphOps::AddState(controller, survivor, "Kept", Vec2{});
	ASSERT_EQ(kept, 3);

	ASSERT_GE(AnimatorGraphOps::AddTransition(controller, outside, deeper), 0);
	ASSERT_GE(AnimatorGraphOps::AddTransition(controller, outside, kept), 0);

	AnimatorController::Layer base;
	base.name = "Base Layer";
	base.machine = 0;
	controller.layers.push_back(std::move(base));
	AnimatorController::Layer upper;
	upper.name = "Upper";
	upper.machine = survivor;
	controller.layers.push_back(std::move(upper));

	ASSERT_TRUE(AnimatorGraphOps::RemoveMachine(controller, doomed));

	ASSERT_EQ(controller.machines.size(), 2u);
	EXPECT_EQ(controller.machines[1].name, "Survivor");
	EXPECT_EQ(controller.machines[1].parent, 0);

	ASSERT_EQ(controller.states.size(), 2u);
	EXPECT_EQ(controller.states[0].name, "Outside");
	EXPECT_EQ(controller.states[1].name, "Kept");
	EXPECT_EQ(controller.states[1].machine, 1);

	ASSERT_EQ(controller.states[0].transitions.size(), 1u);
	EXPECT_EQ(controller.states[0].transitions[0].destination, 1);

	ASSERT_EQ(controller.layers.size(), 2u);
	EXPECT_EQ(controller.layers[0].machine, 0);
	EXPECT_EQ(controller.layers[1].machine, 1);
}

TEST_F(AnimatorGraphOpsFixture, LayersRootMachineRefusesToBeDeleted) {
	AnimatorController controller;
	MakeBareGraph(controller);
	AnimatorController::Layer base;
	base.name = "Base Layer";
	base.machine = 0;
	controller.layers.push_back(std::move(base));

	EXPECT_FALSE(AnimatorGraphOps::RemoveMachine(controller, 0));
	EXPECT_EQ(controller.machines.size(), 1u);
	EXPECT_EQ(controller.layers[0].machine, 0);
}

TEST_F(AnimatorGraphOpsFixture, NewLayerHasRootAndUniqueName) {
	AnimatorController controller;
	MakeBareGraph(controller);
	AnimatorController::Layer base;
	base.name = "Base Layer";
	base.machine = 0;
	controller.layers.push_back(std::move(base));

	const i32 upper = AnimatorGraphOps::AddLayer(controller, "Upper Body");
	ASSERT_EQ(upper, 1);
	ASSERT_EQ(controller.layers.size(), 2u);
	ASSERT_EQ(controller.machines.size(), 2u);
	EXPECT_EQ(controller.layers[1].machine, 1);
	EXPECT_EQ(controller.machines[1].parent, -1);
	EXPECT_EQ(controller.layers[1].weight, 1.0f);
	EXPECT_EQ(controller.layers[1].blend, AnimationBlendMode::Override);

	EXPECT_EQ(AnimatorGraphOps::AddLayer(controller, "Upper Body"), -1);
	EXPECT_EQ(AnimatorGraphOps::AddLayer(controller, ""), -1);
	EXPECT_EQ(controller.layers.size(), 2u);
}

TEST_F(AnimatorGraphOpsFixture, LayerRemovalFixesEdges) {
	AnimatorController controller;
	MakeBareGraph(controller);
	AnimatorController::Layer base;
	base.name = "Base Layer";
	base.machine = 0;
	controller.layers.push_back(std::move(base));

	const i32 keep = AnimatorGraphOps::AddState(controller, 0, "Idle", {0.0f, 0.0f});
	ASSERT_GE(keep, 0);

	const i32 upper = AnimatorGraphOps::AddLayer(controller, "Upper Body");
	ASSERT_EQ(upper, 1);
	const i32 doomedMachine = controller.layers[1].machine;
	const i32 doomedState = AnimatorGraphOps::AddState(controller, doomedMachine, "Aim", {0.0f, 0.0f});
	ASSERT_GE(doomedState, 0);
	ASSERT_GE(AnimatorGraphOps::AddTransition(controller, keep, doomedState), 0);
	ASSERT_EQ(controller.states[static_cast<size_t>(keep)].transitions.size(), 1u);

	ASSERT_TRUE(AnimatorGraphOps::RemoveLayer(controller, upper));
	EXPECT_EQ(controller.layers.size(), 1u);
	EXPECT_EQ(controller.machines.size(), 1u);
	EXPECT_EQ(controller.states.size(), 1u);
	EXPECT_TRUE(controller.states[0].transitions.empty());
	EXPECT_EQ(controller.layers[0].machine, 0);
}

TEST_F(AnimatorGraphOpsFixture, TheBaseLayerRefusesToBeRemoved) {
	AnimatorController controller;
	MakeBareGraph(controller);
	AnimatorController::Layer base;
	base.name = "Base Layer";
	base.machine = 0;
	controller.layers.push_back(std::move(base));
	ASSERT_EQ(AnimatorGraphOps::AddLayer(controller, "Upper Body"), 1);

	EXPECT_FALSE(AnimatorGraphOps::RemoveLayer(controller, 0));
	EXPECT_EQ(controller.layers.size(), 2u);
	EXPECT_FALSE(AnimatorGraphOps::RemoveLayer(controller, 7));
	EXPECT_EQ(controller.layers.size(), 2u);
}

TEST_F(AnimatorGraphOpsFixture, LayerWeightClampedRenameKeepsMachine) {
	AnimatorController controller;
	MakeBareGraph(controller);
	AnimatorController::Layer base;
	base.name = "Base Layer";
	base.machine = 0;
	controller.layers.push_back(std::move(base));
	ASSERT_EQ(AnimatorGraphOps::AddLayer(controller, "Upper Body"), 1);

	EXPECT_TRUE(AnimatorGraphOps::SetLayerWeight(controller, 1, 0.25f));
	EXPECT_FLOAT_EQ(controller.layers[1].weight, 0.25f);
	EXPECT_TRUE(AnimatorGraphOps::SetLayerWeight(controller, 1, 4.0f));
	EXPECT_FLOAT_EQ(controller.layers[1].weight, 1.0f);
	EXPECT_TRUE(AnimatorGraphOps::SetLayerWeight(controller, 1, -3.0f));
	EXPECT_FLOAT_EQ(controller.layers[1].weight, 0.0f);
	EXPECT_TRUE(AnimatorGraphOps::SetLayerWeight(controller, 1, std::numeric_limits<f32>::quiet_NaN()));
	EXPECT_FLOAT_EQ(controller.layers[1].weight, 0.0f);

	EXPECT_TRUE(AnimatorGraphOps::SetLayerBlend(controller, 1, AnimationBlendMode::Additive));
	EXPECT_EQ(controller.layers[1].blend, AnimationBlendMode::Additive);

	EXPECT_TRUE(AnimatorGraphOps::RenameLayer(controller, 1, "Aiming"));
	EXPECT_EQ(controller.layers[1].name, "Aiming");
	EXPECT_EQ(controller.machines[static_cast<size_t>(controller.layers[1].machine)].name, "Aiming");
	EXPECT_FALSE(AnimatorGraphOps::RenameLayer(controller, 1, "Base Layer"));
	EXPECT_EQ(controller.layers[1].name, "Aiming");
	EXPECT_TRUE(AnimatorGraphOps::RenameLayer(controller, 1, "Aiming"));
}

TEST_F(AnimatorGraphOpsFixture, MaskHasClampedWeightPerBone) {
	AnimatorController controller;
	MakeBareGraph(controller);
	AnimatorController::Layer base;
	base.name = "Base Layer";
	base.machine = 0;
	controller.layers.push_back(std::move(base));
	ASSERT_EQ(AnimatorGraphOps::AddLayer(controller, "Upper Body"), 1);

	EXPECT_EQ(AnimatorGraphOps::AddLayerMaskEntry(controller, 1, "Chest", 1.0f), 0);
	EXPECT_EQ(AnimatorGraphOps::AddLayerMaskEntry(controller, 1, "Head", 0.5f), 1);
	ASSERT_EQ(controller.layers[1].mask.size(), 2u);
	EXPECT_EQ(controller.layers[1].mask[0].bone, "Chest");
	EXPECT_FLOAT_EQ(controller.layers[1].mask[1].weight, 0.5f);

	EXPECT_EQ(AnimatorGraphOps::AddLayerMaskEntry(controller, 1, "Chest", 0.25f), -1);
	EXPECT_EQ(controller.layers[1].mask.size(), 2u);
	EXPECT_EQ(AnimatorGraphOps::AddLayerMaskEntry(controller, 1, "", 1.0f), -1);

	EXPECT_TRUE(AnimatorGraphOps::SetLayerMaskEntryWeight(controller, 1, 1, 9.0f));
	EXPECT_FLOAT_EQ(controller.layers[1].mask[1].weight, 1.0f);

	EXPECT_TRUE(AnimatorGraphOps::RemoveLayerMaskEntry(controller, 1, 0));
	ASSERT_EQ(controller.layers[1].mask.size(), 1u);
	EXPECT_EQ(controller.layers[1].mask[0].bone, "Head");
	EXPECT_FALSE(AnimatorGraphOps::RemoveLayerMaskEntry(controller, 1, 4));
}

TEST_F(AnimatorGraphOpsFixture, StructuralEditReAdoptsRunningCharacter) {
	SkeletonAsset* skeleton = MakeSpine();
	AnimationClip* clip = MakeClip("project://pose.clip");
	AddConstantRotation(*clip, {"Chest"}, 0.0f);

	AnimatorController* controller = MakeController();
	const i32 idle = AnimatorGraphOps::AddState(*controller, 0, "Idle", Vec2{});
	const i32 walk = AnimatorGraphOps::AddState(*controller, 0, "Walk", Vec2{});
	const i32 run = AnimatorGraphOps::AddState(*controller, 0, "Run", Vec2{});
	const i32 jump = AnimatorGraphOps::AddState(*controller, 0, "Jump", Vec2{});
	ASSERT_EQ(jump, 3);
	for (i32 state : {idle, walk, run, jump}) {
		ASSERT_GE(AnimatorGraphOps::SetStateClip(*controller, state, Guid{}, Handle<AnimationClip>(clip)), 0);
	}

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetApplyRootMotion(false);
	Handle<AnimatorStateMachine> driver = rig->AddComponent<AnimatorStateMachine>();
	driver->SetController(Handle<AnimatorController>(controller));

	RunUpdate(0.05f);
	ASSERT_TRUE(driver->Play("Run", 0.0f, 0));
	RunUpdate(0.05f);
	ASSERT_EQ(driver->GetCurrentState(0), run);

	ASSERT_TRUE(AnimatorGraphOps::RemoveState(*controller, idle));
	RunUpdate(0.05f);

	const i32 after = driver->GetCurrentState(0);
	str path;
	ASSERT_TRUE(AnimatorGraphOps::StatePath(*controller, after, path));
	EXPECT_NE(path, "Base Layer/Jump");
	EXPECT_EQ(path, "Base Layer/Walk");
}

TEST_F(AnimatorGraphOpsFixture, LayoutKeepsCharacterPlaying) {
	SkeletonAsset* skeleton = MakeSpine();
	AnimationClip* clip = MakeClip("project://pose.clip");
	AddConstantRotation(*clip, {"Chest"}, 0.0f);

	AnimatorController* controller = MakeController();
	const i32 idle = AnimatorGraphOps::AddState(*controller, 0, "Idle", Vec2{});
	const i32 walk = AnimatorGraphOps::AddState(*controller, 0, "Walk", Vec2{});
	ASSERT_GE(AnimatorGraphOps::SetStateClip(*controller, idle, Guid{}, Handle<AnimationClip>(clip)), 0);
	ASSERT_GE(AnimatorGraphOps::SetStateClip(*controller, walk, Guid{}, Handle<AnimationClip>(clip)), 0);
	controller->states[0].position = Vec2{};
	controller->states[1].position = Vec2{};

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetApplyRootMotion(false);
	Handle<AnimatorStateMachine> driver = rig->AddComponent<AnimatorStateMachine>();
	driver->SetController(Handle<AnimatorController>(controller));

	RunUpdate(0.05f);
	ASSERT_TRUE(driver->Play("Walk", 0.0f, 0));
	RunUpdate(0.05f);
	ASSERT_EQ(driver->GetCurrentState(0), walk);

	ASSERT_TRUE(AnimatorGraphOps::NeedsAutoLayout(*controller, 0));
	ASSERT_TRUE(AnimatorGraphOps::AutoLayout(*controller, 0));
	RunUpdate(0.05f);

	EXPECT_EQ(driver->GetCurrentState(0), walk);
}

TEST_F(AnimatorGraphOpsFixture, NewParameterSettableNextTick) {
	SkeletonAsset* skeleton = MakeSpine();
	AnimationClip* clip = MakeClip("project://pose.clip");
	AddConstantRotation(*clip, {"Chest"}, 0.0f);

	AnimatorController* controller = MakeController();
	const i32 idle = AnimatorGraphOps::AddState(*controller, 0, "Idle", Vec2{});
	ASSERT_GE(AnimatorGraphOps::SetStateClip(*controller, idle, Guid{}, Handle<AnimationClip>(clip)), 0);

	Handle<SceneObject> rig = MakeRig(skeleton);
	Handle<Animator> animator = rig->AddComponent<Animator>();
	animator->SetApplyRootMotion(false);
	Handle<AnimatorStateMachine> driver = rig->AddComponent<AnimatorStateMachine>();
	driver->SetController(Handle<AnimatorController>(controller));

	RunUpdate(0.05f);
	ASSERT_EQ(driver->GetCurrentState(0), idle);
	EXPECT_FALSE(driver->SetFloat("Speed", 1.0f));

	ASSERT_GE(AnimatorGraphOps::AddParameter(*controller, "Speed", AnimatorParameterKind::Float), 0);
	RunUpdate(0.05f);
	EXPECT_TRUE(driver->SetFloat("Speed", 1.0f));
	EXPECT_NEAR(driver->GetFloat("Speed"), 1.0f, 1e-5f);
}

TEST_F(AnimatorGraphOpsFixture, SnapshotRestartsOnShapeChange) {
	AnimatorController* controller = MakeController();
	const i32 idle = AnimatorGraphOps::AddState(*controller, 0, "Idle", Vec2(40.0f, 40.0f));
	const i32 walk = AnimatorGraphOps::AddState(*controller, 0, "Walk", Vec2(300.0f, 40.0f));
	ASSERT_GE(AnimatorGraphOps::AddTransition(*controller, idle, walk), 0);
	ASSERT_GE(AnimatorGraphOps::AddParameter(*controller, "Speed", AnimatorParameterKind::Float), 0);
	controller->states[static_cast<size_t>(walk)].speed = 1.37f;
	const str snapshot = AnimatorGraphOps::CaptureSnapshot(*controller);

	ASSERT_TRUE(AnimatorGraphOps::RemoveState(*controller, idle));
	const u32 afterDelete = controller->graphRevision;
	ASSERT_TRUE(AnimatorGraphOps::RestoreSnapshot(*controller, snapshot));
	ASSERT_EQ(controller->states.size(), 2u);
	EXPECT_EQ(controller->states[0].name, "Idle");
	ASSERT_EQ(controller->states[0].transitions.size(), 1u);
	EXPECT_EQ(controller->states[0].transitions[0].destination, 1);
	EXPECT_EQ(controller->states[1].speed, 1.37f);
	EXPECT_NE(controller->graphRevision, afterDelete);
	EXPECT_EQ(AnimatorGraphOps::CaptureSnapshot(*controller), snapshot);

	const u32 beforeMove = controller->graphRevision;
	controller->states[0].position = Vec2(500.0f, 500.0f);
	controller->machines[0].entryPosition = Vec2(-900.0f, 0.0f);
	ASSERT_TRUE(AnimatorGraphOps::RestoreSnapshot(*controller, snapshot));
	EXPECT_EQ(controller->states[0].position.x, 40.0f);
	EXPECT_EQ(controller->graphRevision, beforeMove);

	const u32 kept = controller->graphRevision;
	EXPECT_FALSE(AnimatorGraphOps::RestoreSnapshot(*controller, "[1, 2"));
	EXPECT_EQ(controller->states.size(), 2u);
	EXPECT_EQ(controller->graphRevision, kept);
}

#endif

#ifdef SHALLTEAR_SHARED

namespace {

	using namespace UITest;

	class AnimatorGraphPanelFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		ModuleHandle _module;
		const void* _moduleBase = nullptr;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_graph_panel_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			EditorSelection::Initialize();
			PropertyDrawers::Initialize();
			EditorGizmos::Initialize();
			EditorSceneOps::Initialize();
			EditorAssetOps::Initialize();
			UndoSystem::Initialize();
			PanelManager::Initialize();

			_module = ModuleSystem::Load(Path(str(PACKAGE_TARGET) + ".dll"));
			ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
			_moduleBase = ModuleSystem::GetNativeHandle(_module);
			ASSERT_NE(_moduleBase, nullptr);
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			EditorExtensions::Unload(_moduleBase);
			PanelManager::Terminate();
			EditorDocuments::Terminate();
			UndoSystem::Terminate();
			EditorAssetOps::Terminate();
			EditorSceneOps::Terminate();
			EditorGizmos::Terminate();
			PropertyDrawers::Terminate();
			EditorSelection::Terminate();
			AssetDatabase::Terminate();
			SceneSystem::Terminate();
			ModuleSystem::Terminate();
			Settings::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		Guid StageController(const char* name, std::string_view json) {
			std::filesystem::create_directories(_root / "Assets");
			const std::filesystem::path file = _root / "Assets" / name;
			std::ofstream out(file, std::ios::binary);
			out.write(json.data(), static_cast<std::streamsize>(json.size()));
			out.close();
			AssetDatabase::ScanProjectDirectory();
			return AssetDatabase::LookupGuid(Path(str("project://Assets/") + name));
		}

		str ReadController(const char* name) const {
			std::ifstream in(_root / "Assets" / name, std::ios::binary);
			return str((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
		}
	};

	constexpr std::string_view TwoStateJSON = R"({
"parameters":[{"name":"Speed","kind":0,"default":0}],
"nodes":[],
"machines":[{"name":"Base Layer","parent":-1,"defaultState":0,"x":0,"y":0,"entryX":-260,"entryY":0,"anyX":-260,"anyY":144,"anyState":[]}],
"states":[
{"name":"Idle","motion":-1,"speed":1,"machine":0,"x":40,"y":40,"transitions":[]},
{"name":"Walk","motion":-1,"speed":1,"machine":0,"x":360,"y":40,"transitions":[]}],
"layers":[{"name":"Base Layer","machine":0,"weight":1,"blend":0,"mask":[]}]
})";

	struct PanelHarness {
		MockFontSystem fonts;
		UI::Context context;
		UI::ElementHandle root;
		Handle<EditorPanel> panel;

		explicit PanelHarness(const UI::ContextDesc& desc) : context(desc, &fonts) {}

		bool Open(const TypeInfo* type) {
			panel = PanelManager::OpenPanel(type);
			if (!panel.IsValid()) {
				return false;
			}
			EditorTheme::ApplyToContext(context);
			root = context.CreateElement();
			context.AppendChild(context.GetRoot(), root);
			panel->BuildContent(context, root);
			context.Update();
			panel->OnTick(0.016f);
			context.Update();
			return true;
		}

		void Settle() {
			panel->OnTick(0.016f);
			context.Update();
		}
	};

	UI::ElementHandle BoardAddButton(UI::Context& context, UI::ElementHandle root, std::string_view title) {
		const UI::Element* heading = context.GetElement(FindByText(context, root, title));
		if (!heading || heading->nextSibling == UI::INVALID_UI_INDEX) {
			return {};
		}
		return context.MakeHandle(heading->nextSibling);
	}

	UI::ElementHandle BoardRoot(UI::Context& context, UI::ElementHandle root, std::string_view title) {
		const UI::Element* heading = context.GetElement(FindByText(context, root, title));
		const UI::Element* bar = heading ? context.GetElement(context.MakeHandle(heading->parent)) : nullptr;
		return bar ? context.MakeHandle(bar->parent) : UI::ElementHandle{};
	}

	u32 CountOf(std::string_view text, std::string_view needle) {
		u32 count = 0;
		for (size_t at = text.find(needle); at != std::string_view::npos; at = text.find(needle, at + 1)) {
			++count;
		}
		return count;
	}

	constexpr f32 NO_NUMBER = -99999.0f;

	f32 NumberAfter(std::string_view text, std::string_view key, u32 occurrence) {
		const str needle = str("\"") + str(key) + "\": ";
		size_t at = std::string_view::npos;
		size_t from = 0;
		for (u32 index = 0; index <= occurrence; ++index) {
			at = text.find(needle, from);
			if (at == std::string_view::npos) {
				return NO_NUMBER;
			}
			from = at + needle.size();
		}
		const size_t start = at + needle.size();
		size_t end = start;
		while (end < text.size() && (text[end] == '-' || text[end] == '.' || (text[end] >= '0' && text[end] <= '9'))) {
			++end;
		}
		if (end == start) {
			return NO_NUMBER;
		}
		return static_cast<f32>(std::atof(str(text.substr(start, end - start)).c_str()));
	}

} // anonymous namespace

TEST_F(AnimatorGraphPanelFixture, PackageContributesAnimatorPanelTypeThatOpens) {
	const TypeInfo* type = TypeRegistry::FindType(StringHash("AnimatorGraphPanel"));
	ASSERT_NE(type, nullptr);

	Handle<EditorPanel> panel = PanelManager::OpenPanel(type);
	ASSERT_TRUE(panel.IsValid());
	EXPECT_EQ(panel->GetTitleBase(), "Animator");

	const AttributeInfo* floating = FindTypeAttribute(type, StringHash("EditorPanelFloatingSize"));
	ASSERT_NE(floating, nullptr);
	EXPECT_TRUE(floating->value.Is<Vec2>());
}

TEST_F(AnimatorGraphPanelFixture, RightClickCreatesStateClicksWire) {
	const Guid guid = StageController("Locomotion.controller", TwoStateJSON);
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);

	const TypeInfo* type = TypeRegistry::FindType(StringHash("AnimatorGraphPanel"));
	ASSERT_NE(type, nullptr);

	UI::ContextDesc desc;
	desc.sizePx = {1100, 760};
	PanelHarness harness(desc);
	ASSERT_TRUE(harness.Open(type));

	ASSERT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "Idle")));

	UI::ElementHandle canvas = FindByClass(harness.context, harness.root, StringHash("ed-graph"), 0);
	ASSERT_TRUE(harness.context.IsAlive(canvas));
	const UI::Rect area = harness.context.GetLayout(canvas).borderBox;
	RightClickAt(harness.context, static_cast<i32>(area.x + area.w - 40.0f),
				 static_cast<i32>(area.y + area.h - 40.0f));

	UI::ElementHandle create = FindByText(harness.context, harness.context.GetRoot(), "Create State");
	ASSERT_TRUE(harness.context.IsAlive(create));
	ClickElement(harness.context, create);
	harness.Settle();

	UI::ElementHandle source = FindByText(harness.context, harness.root, "New State");
	ASSERT_TRUE(harness.context.IsAlive(source));

	RightClickElement(harness.context, source);
	UI::ElementHandle make = FindByText(harness.context, harness.context.GetRoot(), "Make Transition");
	ASSERT_TRUE(harness.context.IsAlive(make));
	ClickElement(harness.context, make);
	harness.context.Update();

	UI::ElementHandle target = FindByText(harness.context, harness.root, "Walk");
	ASSERT_TRUE(harness.context.IsAlive(target));
	ClickElement(harness.context, target);
	harness.Settle();

	UI::ElementHandle save = FindByText(harness.context, harness.root, "Save");
	ASSERT_TRUE(harness.context.IsAlive(save));
	ClickElement(harness.context, save);

	const str written = ReadController("Locomotion.controller");
	ASSERT_FALSE(written.empty());
	EXPECT_NE(written.find("\"New State\""), str::npos);
	EXPECT_EQ(CountOf(written, "\"destination\""), 1u);
	EXPECT_NE(written.find("\"destination\": 1"), str::npos);
	EXPECT_GT(written.find("\"destination\""), written.find("\"New State\""));
}

TEST_F(AnimatorGraphPanelFixture, NodeDragWritesPositionToAsset) {
	const Guid guid = StageController("Drag.controller", TwoStateJSON);
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);

	const TypeInfo* type = TypeRegistry::FindType(StringHash("AnimatorGraphPanel"));
	ASSERT_NE(type, nullptr);

	UI::ContextDesc desc;
	desc.sizePx = {1100, 760};
	PanelHarness harness(desc);
	ASSERT_TRUE(harness.Open(type));

	UI::ElementHandle node = FindByText(harness.context, harness.root, "Idle");
	ASSERT_TRUE(harness.context.IsAlive(node));
	const UI::Rect before = harness.context.GetLayout(node).borderBox;

	DragElementBy(harness.context, node, 96, 48);
	harness.context.Update();

	const UI::Rect after = harness.context.GetLayout(node).borderBox;
	EXPECT_GT(after.x, before.x);
	EXPECT_GT(after.y, before.y);

	UI::ElementHandle save = FindByText(harness.context, harness.root, "Save");
	ASSERT_TRUE(harness.context.IsAlive(save));
	ClickElement(harness.context, save);

	const str written = ReadController("Drag.controller");
	ASSERT_FALSE(written.empty());

	const f32 x = NumberAfter(written, "x", 1);
	const f32 y = NumberAfter(written, "y", 1);
	ASSERT_NE(x, NO_NUMBER);
	ASSERT_NE(y, NO_NUMBER);
	EXPECT_GT(x, 40.0f);
	EXPECT_GT(y, 40.0f);
}

TEST_F(AnimatorGraphPanelFixture, ConditionAndParameterRowsEditColumn) {
	const Guid guid = StageController("Rows.controller", TwoStateJSON);
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);

	const TypeInfo* type = TypeRegistry::FindType(StringHash("AnimatorGraphPanel"));
	ASSERT_NE(type, nullptr);

	UI::ContextDesc desc;
	desc.sizePx = {1100, 760};
	PanelHarness harness(desc);
	ASSERT_TRUE(harness.Open(type));

	UI::ElementHandle plus = BoardAddButton(harness.context, harness.root, "Parameters");
	ASSERT_TRUE(harness.context.IsAlive(plus));
	ClickElement(harness.context, plus);
	UI::ElementHandle addBool = FindByText(harness.context, harness.context.GetRoot(), "Bool");
	ASSERT_TRUE(harness.context.IsAlive(addBool));
	ClickElement(harness.context, addBool);
	harness.Settle();
	ASSERT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "New Bool")));

	UI::ElementHandle source = FindByText(harness.context, harness.root, "Idle");
	ASSERT_TRUE(harness.context.IsAlive(source));
	RightClickElement(harness.context, source);
	UI::ElementHandle make = FindByText(harness.context, harness.context.GetRoot(), "Make Transition");
	ASSERT_TRUE(harness.context.IsAlive(make));
	ClickElement(harness.context, make);
	harness.context.Update();
	ClickElement(harness.context, FindByText(harness.context, harness.root, "Walk"));
	harness.Settle();

	UI::ElementHandle add = FindByText(harness.context, harness.root, "Add Condition");
	ASSERT_TRUE(harness.context.IsAlive(add));
	ClickElement(harness.context, add);
	harness.Settle();

	UI::ElementHandle drop = FindByText(harness.context, harness.root, "x");
	ASSERT_TRUE(harness.context.IsAlive(drop));
	ClickElement(harness.context, drop);
	harness.Settle();

	ClickElement(harness.context, FindByText(harness.context, harness.root, "Save"));
	const str written = ReadController("Rows.controller");
	ASSERT_FALSE(written.empty());
	EXPECT_NE(written.find("\"New Bool\""), str::npos);
	EXPECT_EQ(CountOf(written, "\"destination\""), 1u);
	EXPECT_EQ(CountOf(written, "\"threshold\""), 0u);
}

TEST_F(AnimatorGraphPanelFixture, LayerStripAddsSettingsAuthor) {
	const Guid guid = StageController("Layers.controller", TwoStateJSON);
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);

	const TypeInfo* type = TypeRegistry::FindType(StringHash("AnimatorGraphPanel"));
	ASSERT_NE(type, nullptr);

	UI::ContextDesc desc;
	desc.sizePx = {1100, 760};
	PanelHarness harness(desc);
	ASSERT_TRUE(harness.Open(type));

	UI::ElementHandle add = FindByText(harness.context, harness.root, "+");
	ASSERT_TRUE(harness.context.IsAlive(add));
	ClickElement(harness.context, add);
	harness.Settle();

	ASSERT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "Blending")));

	UI::ElementHandle addMask = FindByText(harness.context, harness.root, "Add Masked Bone");
	ASSERT_TRUE(harness.context.IsAlive(addMask));
	ClickElement(harness.context, addMask);
	harness.Settle();
	ASSERT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "x")));

	ClickElement(harness.context, FindByText(harness.context, harness.root, "Save"));
	const str written = ReadController("Layers.controller");
	ASSERT_FALSE(written.empty());
	EXPECT_EQ(CountOf(written, "\"blend\""), 2u);
	EXPECT_EQ(CountOf(written, "\"anyState\""), 2u);
	EXPECT_NE(written.find("\"New Layer\""), str::npos);
	EXPECT_NE(written.find("\"Bone\""), str::npos);

	ASSERT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "Remove Layer")));
	ClickElement(harness.context, FindByText(harness.context, harness.root, "Remove Layer"));
	harness.Settle();
	ClickElement(harness.context, FindByText(harness.context, harness.root, "Settings"));
	harness.Settle();
	EXPECT_TRUE(harness.context.IsAlive(FindByTextContaining(harness.context, harness.root, "cannot be removed")));

	ClickElement(harness.context, FindByText(harness.context, harness.root, "Save"));
	const str afterRemove = ReadController("Layers.controller");
	ASSERT_FALSE(afterRemove.empty());
	EXPECT_EQ(CountOf(afterRemove, "\"blend\""), 1u);
	EXPECT_EQ(CountOf(afterRemove, "\"anyState\""), 1u);
}

constexpr std::string_view NestedJSON = R"({
"parameters":[{"name":"Fight","kind":2,"default":0}],
"nodes":[],
"machines":[
{"name":"Base Layer","parent":-1,"defaultState":0,"x":0,"y":0,"entryX":-260,"entryY":0,"anyX":-260,"anyY":144,"upX":0,"upY":0,"anyState":[]},
{"name":"Locomotion","parent":0,"defaultState":1,"x":420,"y":40,"entryX":-260,"entryY":0,"anyX":-260,"anyY":144,"upX":-260,"upY":288,"anyState":[]}],
"states":[
{"name":"Idle","motion":-1,"speed":1,"machine":0,"x":40,"y":40,"transitions":[]},
{"name":"Attack","motion":-1,"speed":1,"machine":1,"x":80,"y":260,"transitions":[
{"destination":0,"exitTime":-1,"duration":0.25,"offset":0,"syncPhase":false,"conditions":[]}]}],
"layers":[{"name":"Base Layer","machine":0,"weight":1,"blend":0,"mask":[]}]
})";

TEST_F(AnimatorGraphPanelFixture, SubMachineDoubleClickAndBreadcrumb) {
	const Guid guid = StageController("Nested.controller", NestedJSON);
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);

	const TypeInfo* type = TypeRegistry::FindType(StringHash("AnimatorGraphPanel"));
	ASSERT_NE(type, nullptr);
	UI::ContextDesc desc;
	desc.sizePx = {1100, 760};
	PanelHarness harness(desc);
	ASSERT_TRUE(harness.Open(type));

	ASSERT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "Idle")));
	UI::ElementHandle pill = FindByText(harness.context, harness.root, "Locomotion");
	ASSERT_TRUE(harness.context.IsAlive(pill));
	EXPECT_FALSE(harness.context.IsAlive(FindByText(harness.context, harness.root, "Attack")));

	DoubleClickElement(harness.context, pill);
	harness.Settle();

	EXPECT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "Attack")));
	EXPECT_FALSE(harness.context.IsAlive(FindByText(harness.context, harness.root, "Idle")));
	EXPECT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "(Up) Base Layer")));

	UI::ElementHandle crumb = FindByText(harness.context, harness.root, "Base Layer");
	ASSERT_TRUE(harness.context.IsAlive(crumb));
	ClickElement(harness.context, crumb);
	harness.Settle();

	EXPECT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "Idle")));
	EXPECT_FALSE(harness.context.IsAlive(FindByText(harness.context, harness.root, "(Up) Base Layer")));
}

TEST_F(AnimatorGraphPanelFixture, PillRightClickOpensSubMachineMenu) {
	const Guid guid = StageController("PillMenu.controller", NestedJSON);
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);

	const TypeInfo* type = TypeRegistry::FindType(StringHash("AnimatorGraphPanel"));
	ASSERT_NE(type, nullptr);
	UI::ContextDesc desc;
	desc.sizePx = {1100, 760};
	PanelHarness harness(desc);
	ASSERT_TRUE(harness.Open(type));

	UI::ElementHandle pill = FindByText(harness.context, harness.root, "Locomotion");
	ASSERT_TRUE(harness.context.IsAlive(pill));
	RightClickElement(harness.context, pill);

	UI::ElementHandle open = FindByText(harness.context, harness.context.GetRoot(), "Open");
	ASSERT_TRUE(harness.context.IsAlive(open));
	EXPECT_FALSE(harness.context.IsAlive(FindByText(harness.context, harness.context.GetRoot(), "Create State")));
	ClickElement(harness.context, open);
	harness.Settle();

	EXPECT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "Attack")));
}

TEST_F(AnimatorGraphPanelFixture, SubMachineEdgeReachesUpNode) {
	const Guid guid = StageController("Nested.controller", NestedJSON);
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);

	const TypeInfo* type = TypeRegistry::FindType(StringHash("AnimatorGraphPanel"));
	ASSERT_NE(type, nullptr);
	UI::ContextDesc desc;
	desc.sizePx = {1100, 760};
	PanelHarness harness(desc);
	ASSERT_TRUE(harness.Open(type));

	DoubleClickElement(harness.context, FindByText(harness.context, harness.root, "Locomotion"));
	harness.Settle();

	UI::ElementHandle source = FindByText(harness.context, harness.root, "Attack");
	UI::ElementHandle up = FindByText(harness.context, harness.root, "(Up) Base Layer");
	ASSERT_TRUE(harness.context.IsAlive(source));
	ASSERT_TRUE(harness.context.IsAlive(up));

	const UI::Rect from = harness.context.GetLayout(source).borderBox;
	const UI::Rect to = harness.context.GetLayout(up).borderBox;
	const i32 x = static_cast<i32>((from.x + from.w * 0.5f + to.x + to.w * 0.5f) * 0.5f);
	const i32 y = static_cast<i32>((from.y + from.h * 0.5f + to.y + to.h * 0.5f) * 0.5f);

	InputEvent move{};
	move.type = InputEventType::MouseMove;
	move.mousePosPx = {x, y};
	harness.context.InjectInput(move);
	InputEvent down{};
	down.type = InputEventType::MouseDown;
	down.button = MouseButton::Left;
	down.mousePosPx = {x, y};
	harness.context.InjectInput(down);
	InputEvent release{};
	release.type = InputEventType::MouseUp;
	release.button = MouseButton::Left;
	release.mousePosPx = {x, y};
	harness.context.InjectInput(release);
	harness.context.Update();
	harness.Settle();

	EXPECT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "Exit Time")));
}

TEST_F(AnimatorGraphPanelFixture, GridMenuCreatesSavedSubMachine) {
	const Guid guid = StageController("Locomotion.controller", TwoStateJSON);
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);

	const TypeInfo* type = TypeRegistry::FindType(StringHash("AnimatorGraphPanel"));
	ASSERT_NE(type, nullptr);
	UI::ContextDesc desc;
	desc.sizePx = {1100, 760};
	PanelHarness harness(desc);
	ASSERT_TRUE(harness.Open(type));
	ASSERT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "Idle")));

	UI::ElementHandle canvas = FindByClass(harness.context, harness.root, StringHash("ed-graph"), 0);
	ASSERT_TRUE(harness.context.IsAlive(canvas));
	const UI::Rect area = harness.context.GetLayout(canvas).borderBox;
	RightClickAt(harness.context, static_cast<i32>(area.x + area.w - 40.0f),
				 static_cast<i32>(area.y + area.h - 40.0f));

	UI::ElementHandle nest = FindByText(harness.context, harness.context.GetRoot(), "Create Sub-State Machine");
	ASSERT_TRUE(harness.context.IsAlive(nest));
	ClickElement(harness.context, nest);
	harness.Settle();

	UI::ElementHandle pill = FindByText(harness.context, harness.root, "New Sub-State Machine");
	ASSERT_TRUE(harness.context.IsAlive(pill));

	DoubleClickElement(harness.context, pill);
	harness.Settle();
	EXPECT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "(Up) Base Layer")));

	ClickElement(harness.context, FindByText(harness.context, harness.root, "Save"));
	const str written = ReadController("Locomotion.controller");
	ASSERT_FALSE(written.empty());
	EXPECT_NE(written.find("\"New Sub-State Machine\""), str::npos);
	EXPECT_NE(written.find("\"parent\": 0"), str::npos);
}

namespace {

	[[nodiscard]] bool MakePlayingHero(const Guid& controllerGuid, Handle<Scene>& scene, str& why) {
		const TypeInfo* animatorType = TypeRegistry::FindType(StringHash("Animator"));
		const TypeInfo* driverType = TypeRegistry::FindType(StringHash("AnimatorStateMachine"));
		if (!animatorType || !driverType) {
			why = "no Animator or AnimatorStateMachine type";
			return false;
		}

		scene = SceneSystem::CreateScene("debugger");
		Handle<SceneObject> hero = scene.IsValid() ? SceneSystem::CreateObject("Hero", scene) : Handle<SceneObject>();
		if (!hero.IsValid()) {
			why = "scene or object not created";
			return false;
		}
		if (!hero->AddComponent(animatorType).IsValid()) {
			why = "Animator didn't attach";
			return false;
		}
		Handle<Component> driver = hero->AddComponent(driverType);
		Asset* asset = AssetDatabase::LoadAssetByGuid(controllerGuid);
		if (!driver.IsValid() || !asset) {
			why = ".controller didn't load (no importer)";
			return false;
		}

		Variant boxed;
		if (!GetField(driver.Get(), driverType, StringHash("_controller"), &boxed)) {
			why = "no reflected controller field";
			return false;
		}
		const Handle<Asset> assigned(asset);
		static_assert(sizeof(Handle<Asset>) == sizeof(u64));
		std::memcpy(boxed.GetData(), &assigned, sizeof(assigned));
		if (!SetField(driver.Get(), driverType, StringHash("_controller"), &boxed)) {
			why = "field refused its getter's Variant";
			return false;
		}

		FrameTime clockState;
		clockState.fixedDeltaTime = 1.0f / 50.0f;
		clockState.maximumDeltaTime = 1000.0f;
		Time::FrameScope clock{clockState};
		Time::BeginFrame(0.05f);
		SceneSystem::UpdateAll();
		return true;
	}

} // namespace

TEST_F(AnimatorGraphPanelFixture, ReadoutNamesPlayingCharactersState) {
	const Guid guid = StageController("Nested.controller", NestedJSON);
	ASSERT_TRUE(guid.IsValid());

	Handle<Scene> scene;
	str why;
	ASSERT_TRUE(MakePlayingHero(guid, scene, why)) << why;

	EditorSelection::SelectAsset(guid);
	const TypeInfo* type = TypeRegistry::FindType(StringHash("AnimatorGraphPanel"));
	ASSERT_NE(type, nullptr);
	UI::ContextDesc desc;
	desc.sizePx = {1100, 760};
	PanelHarness harness(desc);
	ASSERT_TRUE(harness.Open(type));
	harness.Settle();

	UI::ElementHandle readout = FindByTextContaining(harness.context, harness.root, "Base Layer/Idle");
	ASSERT_TRUE(harness.context.IsAlive(readout));
	const str line = harness.context.GetTextString(readout);
	EXPECT_NE(line.find("Hero"), str::npos);

	EXPECT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "Parameters (live)")));
	EXPECT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "false")));

	UI::ElementHandle running = FindByText(harness.context, harness.root, "Idle");
	ASSERT_TRUE(harness.context.IsAlive(running));
	EXPECT_EQ(harness.context.GetStyle(running).borderColor,
			  EditorTheme::GetColor(EditorTheme::ColorToken::GraphNodeActive));

	UI::ElementHandle canvas = FindByClass(harness.context, harness.root, StringHash("ed-graph"), 0);
	ASSERT_TRUE(harness.context.IsAlive(canvas));
	const UI::Rect area = harness.context.GetLayout(canvas).borderBox;
	RightClickAt(harness.context, static_cast<i32>(area.x + area.w - 40.0f),
				 static_cast<i32>(area.y + area.h - 40.0f));
	UI::ElementHandle create = FindByText(harness.context, harness.context.GetRoot(), "Create State");
	ASSERT_TRUE(harness.context.IsAlive(create));
	ClickElement(harness.context, create);
	harness.Settle();
	running = FindByText(harness.context, harness.root, "Idle");
	ASSERT_TRUE(harness.context.IsAlive(running));
	EXPECT_EQ(harness.context.GetStyle(running).borderColor,
			  EditorTheme::GetColor(EditorTheme::ColorToken::GraphNodeActive));
}

constexpr std::string_view TwoLayerJSON = R"({
"parameters":[],
"nodes":[],
"machines":[
{"name":"Base Layer","parent":-1,"defaultState":0,"x":0,"y":0,"entryX":-260,"entryY":0,"anyX":-260,"anyY":144,"upX":0,"upY":0,"anyState":[]},
{"name":"Upper Body","parent":-1,"defaultState":1,"x":0,"y":0,"entryX":-260,"entryY":0,"anyX":-260,"anyY":144,"upX":0,"upY":0,"anyState":[]}],
"states":[
{"name":"Idle","motion":-1,"speed":1,"machine":0,"x":40,"y":40,"transitions":[]},
{"name":"Aim","motion":-1,"speed":1,"machine":1,"x":40,"y":40,"transitions":[]}],
"layers":[
{"name":"Base Layer","machine":0,"weight":1,"blend":0,"mask":[]},
{"name":"Upper Body","machine":1,"weight":1,"blend":1,"mask":[{"bone":"Chest","weight":1}]}]
})";

TEST_F(AnimatorGraphPanelFixture, ReadoutNamesEveryLayersState) {
	const Guid guid = StageController("Layered.controller", TwoLayerJSON);
	ASSERT_TRUE(guid.IsValid());

	Handle<Scene> scene;
	str why;
	ASSERT_TRUE(MakePlayingHero(guid, scene, why)) << why;

	EditorSelection::SelectAsset(guid);
	const TypeInfo* type = TypeRegistry::FindType(StringHash("AnimatorGraphPanel"));
	ASSERT_NE(type, nullptr);
	UI::ContextDesc desc;
	desc.sizePx = {1100, 760};
	PanelHarness harness(desc);
	ASSERT_TRUE(harness.Open(type));
	harness.Settle();

	UI::ElementHandle readout = FindByTextContaining(harness.context, harness.root, "Base Layer/Idle");
	ASSERT_TRUE(harness.context.IsAlive(readout));
	const str line = harness.context.GetTextString(readout);
	EXPECT_NE(line.find("Upper Body/Aim"), str::npos);
	EXPECT_NE(line.find("Upper Body:"), str::npos);

	SceneSystem::UnloadScene(scene);
}

namespace {

	void DragBetween(UI::Context& context, Vec2I from, Vec2I to, KeyMod mods = KeyMod::None) {
		auto inject = [&](InputEventType type, Vec2I at) {
			InputEvent event{};
			event.type = type;
			event.button = MouseButton::Left;
			event.mousePosPx = at;
			event.mods = mods;
			context.InjectInput(event);
			context.Update();
		};
		inject(InputEventType::MouseMove, from);
		inject(InputEventType::MouseDown, from);
		for (i32 step = 1; step <= 3; ++step) {
			inject(InputEventType::MouseMove, Vec2I(from.x + (to.x - from.x) * step / 3, from.y + (to.y - from.y) * step / 3));
		}
		inject(InputEventType::MouseUp, to);
	}

	Vec2I CentreOf(UI::Context& context, UI::ElementHandle element) {
		const UI::Rect rect = context.GetLayout(element).borderBox;
		return Vec2I(static_cast<i32>(rect.x + rect.w * 0.5f), static_cast<i32>(rect.y + rect.h * 0.5f));
	}

	void WireIdleToWalk(PanelHarness& harness) {
		RightClickElement(harness.context, FindByText(harness.context, harness.root, "Idle"));
		UI::ElementHandle make = FindByText(harness.context, harness.context.GetRoot(), "Make Transition");
		ASSERT_TRUE(harness.context.IsAlive(make));
		ClickElement(harness.context, make);
		harness.context.Update();
		ClickElement(harness.context, FindByText(harness.context, harness.root, "Walk"));
		harness.Settle();
	}

} // anonymous namespace

TEST_F(AnimatorGraphPanelFixture, WireUndoRedoHoldsClipOnce) {
	std::filesystem::create_directories(_root / "Assets");
	const Guid clip = AssetDatabase::CreateAsset(Path("project://Assets/Walk.anim"), StringHash("AnimationClip"));
	ASSERT_TRUE(clip.IsValid());
	str json(TwoStateJSON);
	const str node = str(R"("nodes":[{"kind":0,"clip":")") + clip.ToString() + R"(","parameterX":"","parameterY":"","children":[]}])";
	json.replace(json.find(R"("nodes":[])"), 10, node);
	json.replace(json.find(R"({"name":"Walk","motion":-1)"), 26, R"({"name":"Walk","motion":0)");
	const Guid guid = StageController("Undo.controller", json);
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);

	UI::ContextDesc desc;
	desc.sizePx = {1100, 760};
	PanelHarness harness(desc);
	ASSERT_TRUE(harness.Open(TypeRegistry::FindType(StringHash("AnimatorGraphPanel"))));
	ASSERT_TRUE(harness.context.IsAlive(FindByText(harness.context, harness.root, "Idle")));
	Asset* held = AssetDatabase::FindLoadedByGuid(clip);
	ASSERT_NE(held, nullptr);
	const int references = held->GetRefCount();
	EXPECT_EQ(UndoSystem::GetUndoDepth(), 0u);

	WireIdleToWalk(harness);
	ASSERT_EQ(UndoSystem::GetUndoDepth(), 1u);
	EXPECT_EQ(UndoSystem::GetUndoLabel(), "Make Transition");
	UI::ElementHandle save = FindByText(harness.context, harness.root, "Save");
	ClickElement(harness.context, save);
	EXPECT_EQ(CountOf(ReadController("Undo.controller"), "\"destination\""), 1u);

	ASSERT_TRUE(UndoSystem::Undo());
	harness.Settle();
	ClickElement(harness.context, save);
	EXPECT_EQ(CountOf(ReadController("Undo.controller"), "\"destination\""), 0u);
	EXPECT_EQ(held->GetRefCount(), references);

	ASSERT_TRUE(UndoSystem::Redo());
	harness.Settle();
	ClickElement(harness.context, save);
	const str redone = ReadController("Undo.controller");
	EXPECT_EQ(CountOf(redone, "\"destination\": 1"), 1u);
	EXPECT_EQ(held->GetRefCount(), references);
}

TEST_F(AnimatorGraphPanelFixture, DraggingTwoStatesIsOneUndo) {
	const Guid guid = StageController("Move.controller", TwoStateJSON);
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);
	UI::ContextDesc desc;
	desc.sizePx = {1100, 760};
	PanelHarness harness(desc);
	ASSERT_TRUE(harness.Open(TypeRegistry::FindType(StringHash("AnimatorGraphPanel"))));

	UI::ElementHandle idle = FindByText(harness.context, harness.root, "Idle");
	UI::ElementHandle walk = FindByText(harness.context, harness.root, "Walk");
	ClickElement(harness.context, idle);
	const Vec2I walkAt = CentreOf(harness.context, walk);
	DragBetween(harness.context, walkAt, walkAt, KeyMod::Ctrl);
	const Vec2I idleAt = CentreOf(harness.context, idle);
	DragBetween(harness.context, idleAt, Vec2I(idleAt.x + 90, idleAt.y + 60));
	harness.Settle();
	ASSERT_EQ(UndoSystem::GetUndoDepth(), 1u);
	EXPECT_EQ(UndoSystem::GetUndoLabel(), "Move");

	UI::ElementHandle save = FindByText(harness.context, harness.root, "Save");
	ClickElement(harness.context, save);
	const str moved = ReadController("Move.controller");
	EXPECT_GT(NumberAfter(moved, "x", 1), 40.0f);
	EXPECT_GT(NumberAfter(moved, "x", 2), 360.0f);

	ASSERT_TRUE(UndoSystem::Undo());
	harness.Settle();
	ClickElement(harness.context, save);
	const str undone = ReadController("Move.controller");
	EXPECT_EQ(NumberAfter(undone, "x", 1), 40.0f);
	EXPECT_EQ(NumberAfter(undone, "x", 2), 360.0f);
}

TEST_F(AnimatorGraphPanelFixture, MarqueeDeleteIsOneStep) {
	const Guid guid = StageController("Marquee.controller", TwoStateJSON);
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);
	UI::ContextDesc desc;
	desc.sizePx = {1100, 760};
	PanelHarness harness(desc);
	ASSERT_TRUE(harness.Open(TypeRegistry::FindType(StringHash("AnimatorGraphPanel"))));
	WireIdleToWalk(harness);
	const u32 depth = UndoSystem::GetUndoDepth();

	const UI::Rect idle = harness.context.GetLayout(FindByText(harness.context, harness.root, "Idle")).borderBox;
	const UI::Rect walk = harness.context.GetLayout(FindByText(harness.context, harness.root, "Walk")).borderBox;
	const Vec2I from(static_cast<i32>(Math::Min(idle.x, walk.x) - 12.0f), static_cast<i32>(Math::Min(idle.y, walk.y) - 12.0f));
	const Vec2I to(static_cast<i32>(Math::Max(idle.x + idle.w, walk.x + walk.w) + 12.0f),
				   static_cast<i32>(Math::Max(idle.y + idle.h, walk.y + walk.h) + 12.0f));
	DragBetween(harness.context, from, to);
	harness.Settle();

	ASSERT_TRUE(harness.panel->OnCommand(PanelCommand::Delete));
	harness.Settle();
	ASSERT_EQ(UndoSystem::GetUndoDepth(), depth + 1u);
	UI::ElementHandle save = FindByText(harness.context, harness.root, "Save");
	ClickElement(harness.context, save);
	const str deleted = ReadController("Marquee.controller");
	EXPECT_EQ(deleted.find("\"Idle\""), str::npos);
	EXPECT_EQ(deleted.find("\"Walk\""), str::npos);

	ASSERT_TRUE(UndoSystem::Undo());
	harness.Settle();
	ClickElement(harness.context, save);
	const str restored = ReadController("Marquee.controller");
	EXPECT_NE(restored.find("\"Idle\""), str::npos);
	EXPECT_NE(restored.find("\"Walk\""), str::npos);
	EXPECT_EQ(CountOf(restored, "\"destination\": 1"), 1u);

	EXPECT_TRUE(harness.panel->OnCommand(PanelCommand::SelectAll));
}

TEST_F(AnimatorGraphPanelFixture, UndoAfterPackageIsSweptChangesNothing) {
	const Guid guid = StageController("Swept.controller", TwoStateJSON);
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);
	UI::ContextDesc desc;
	desc.sizePx = {1100, 760};
	PanelHarness harness(desc);
	ASSERT_TRUE(harness.Open(TypeRegistry::FindType(StringHash("AnimatorGraphPanel"))));
	WireIdleToWalk(harness);
	ASSERT_EQ(UndoSystem::GetUndoDepth(), 1u);

	EditorExtensions::Unload(_moduleBase);
	EXPECT_FALSE(EditorDocuments::IsOpen(guid));
	EXPECT_TRUE(UndoSystem::Undo());
	harness.Settle();
	UI::ElementHandle save = FindByText(harness.context, harness.root, "Save");
	ClickElement(harness.context, save);
	EXPECT_EQ(CountOf(ReadController("Swept.controller"), "\"destination\""), 1u);
}

TEST_F(AnimatorGraphPanelFixture, EditUndoableAfterPanelCloses) {
	const Guid guid = StageController("Shared.controller", TwoStateJSON);
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);
	const TypeInfo* type = TypeRegistry::FindType(StringHash("AnimatorGraphPanel"));
	UI::ContextDesc desc;
	desc.sizePx = {1100, 760};
	PanelHarness first(desc);
	PanelHarness second(desc);
	ASSERT_TRUE(first.Open(type));
	ASSERT_TRUE(second.Open(type));
	WireIdleToWalk(first);
	ASSERT_EQ(UndoSystem::GetUndoDepth(), 1u);

	PanelManager::ClosePanel(first.panel);
	ASSERT_TRUE(EditorDocuments::IsOpen(guid));
	ASSERT_TRUE(UndoSystem::Undo());
	second.Settle();
	UI::ElementHandle save = FindByText(second.context, second.root, "Save");
	ClickElement(second.context, save);
	EXPECT_EQ(CountOf(ReadController("Shared.controller"), "\"destination\""), 0u);
}

TEST_F(AnimatorGraphPanelFixture, OpenerShowsControllerLeavesWithPackage) {
	ASSERT_TRUE(EditorExtensions::Load(_moduleBase));
	const Guid guid = StageController("Opened.controller", TwoStateJSON);
	ASSERT_TRUE(guid.IsValid());
	ASSERT_EQ(PanelManager::GetOpenPanelCount(), 0u);

	const Path file("project://Assets/Opened.controller");
	ASSERT_TRUE(AssetOpeners::Open(file));
	EXPECT_EQ(EditorSelection::GetSelectedAsset(), guid);
	Vector<Handle<EditorPanel>> open{GetMemoryResource(MemTag::Editor)};
	PanelManager::GetOpenPanels(open);
	ASSERT_EQ(open.size(), 1u);
	EXPECT_EQ(open[0]->GetType(), TypeRegistry::FindType(StringHash("AnimatorGraphPanel")));

	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {1100, 760};
	UI::Context context(desc, &fonts);
	EditorTheme::ApplyToContext(context);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	open[0]->BuildContent(context, root);
	context.Update();
	open[0]->OnTick(0.016f);
	context.Update();
	EXPECT_TRUE(context.IsAlive(FindByText(context, root, "Idle")));

	ASSERT_TRUE(AssetOpeners::Open(file));
	EXPECT_EQ(PanelManager::GetOpenPanelCount(), 1u);

	EditorExtensions::Unload(_moduleBase);
	EXPECT_FALSE(AssetOpeners::Open(file));
}

TEST_F(AnimatorGraphPanelFixture, SearcherBuildsStateMachineFromKeyboardAlone) {
	std::filesystem::create_directories(_root / "Assets");
	{
		std::ofstream clip(_root / "Assets" / "Sprint.anim", std::ios::binary);
		clip << R"({"length": 0, "frameRate": 60, "loop": "None", "additiveReferenceTime": 0, "bakeRootMotionIntoPose": true, "tracks": []})";
	}
	const Guid guid = StageController("Searched.controller", TwoStateJSON);
	ASSERT_TRUE(guid.IsValid());
	const Guid sprint = AssetDatabase::LookupGuid(Path("project://Assets/Sprint.anim"));
	ASSERT_TRUE(sprint.IsValid());
	EditorSelection::SelectAsset(guid);
	UI::ContextDesc desc;
	desc.sizePx = {1100, 760};
	PanelHarness harness(desc);
	ASSERT_TRUE(harness.Open(TypeRegistry::FindType(StringHash("AnimatorGraphPanel"))));

	UI::ElementHandle canvas = FindByClass(harness.context, harness.root, StringHash("ed-graph"), 0);
	ASSERT_TRUE(harness.context.IsAlive(canvas));
	const UI::Rect area = harness.context.GetLayout(canvas).borderBox;
	auto key = [&](Key pressed, char32_t character = 0) {
		InputEvent down{};
		down.type = InputEventType::KeyDown;
		down.key = pressed;
		harness.context.InjectInput(down);
		harness.context.Update();
		if (character != 0) {
			InputEvent text{};
			text.type = InputEventType::Text;
			text.codepoint = character;
			harness.context.InjectInput(text);
			harness.context.Update();
		}
	};
	auto make = [&](f32 x, f32 y, std::string_view typed) {
		InputEvent click{};
		click.mousePosPx = Vec2I(static_cast<i32>(area.x + area.w * x), static_cast<i32>(area.y + area.h * y));
		for (InputEventType type : {InputEventType::MouseMove, InputEventType::MouseDown, InputEventType::MouseUp}) {
			click.type = type;
			harness.context.InjectInput(click);
			harness.context.Update();
		}
		key(Key::Space, U' ');
		ASSERT_EQ(harness.context.GetOpenPopupCount(), 1u);
		for (const char c : typed) {
			InputEvent text{};
			text.type = InputEventType::Text;
			text.codepoint = static_cast<char32_t>(c);
			harness.context.InjectInput(text);
			harness.context.Update();
		}
		key(Key::Enter);
		harness.Settle();
	};
	make(0.30f, 0.80f, "state");
	make(0.55f, 0.80f, "sub");
	make(0.80f, 0.80f, "sprint");

	UI::ElementHandle save = FindByText(harness.context, harness.root, "Save");
	ASSERT_TRUE(harness.context.IsAlive(save));
	ClickElement(harness.context, save);
	const str written = ReadController("Searched.controller");
	EXPECT_NE(written.find("\"New State\""), str::npos);
	EXPECT_NE(written.find("\"New Sub-State Machine\""), str::npos);
	EXPECT_NE(written.find("\"Sprint\""), str::npos);
	EXPECT_NE(written.find(sprint.ToString()), str::npos);
}

TEST_F(AnimatorGraphPanelFixture, ParameterBoardEditsAndKeyboardDelete) {
	const Guid guid = StageController("Board.controller", TwoStateJSON);
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);
	UI::ContextDesc desc;
	desc.sizePx = {1100, 760};
	PanelHarness harness(desc);
	ASSERT_TRUE(harness.Open(TypeRegistry::FindType(StringHash("AnimatorGraphPanel"))));
	UI::Context& context = harness.context;
	const UI::ElementHandle board = BoardRoot(context, harness.root, "Parameters");
	ASSERT_TRUE(context.IsAlive(board));

	auto press = [&](Key key) {
		InputEvent down{};
		down.type = InputEventType::KeyDown;
		down.key = key;
		context.InjectInput(down);
		context.Update();
		InputEvent up = down;
		up.type = InputEventType::KeyUp;
		context.InjectInput(up);
		context.Update();
	};
	auto type = [&](std::string_view text) {
		for (const char c : text) {
			InputEvent event{};
			event.type = InputEventType::Text;
			event.codepoint = static_cast<char32_t>(c);
			context.InjectInput(event);
			context.Update();
		}
	};
	auto field = [&]() {
		const UI::ElementHandle focused = context.GetFocused();
		return context.IsAlive(focused) && context.HasTextInput(focused) ? focused : UI::ElementHandle{};
	};
	auto settle = [&]() {
		harness.Settle();
		harness.Settle();
	};

	ClickElement(context, FindByText(context, harness.root, "Idle"));
	settle();

	ClickElement(context, BoardAddButton(context, harness.root, "Parameters"));
	UI::ElementHandle trigger = FindByText(context, context.GetRoot(), "Trigger");
	ASSERT_TRUE(context.IsAlive(trigger));
	ClickElement(context, trigger);
	settle();
	ASSERT_TRUE(context.IsAlive(field()));
	EXPECT_EQ(context.GetTextString(field()), "New Trigger");
	type("Jump");
	press(Key::Enter);
	settle();
	ASSERT_TRUE(context.IsAlive(FindByText(context, board, "Jump")));

	ClickElement(context, FindByText(context, board, "Speed"));
	settle();
	ASSERT_TRUE(harness.panel->OnCommand(PanelCommand::Rename));
	settle();
	ASSERT_TRUE(context.IsAlive(field()));
	EXPECT_EQ(context.GetTextString(field()), "Speed");
	type("Velocity");
	press(Key::Enter);
	settle();
	ASSERT_TRUE(context.IsAlive(FindByText(context, board, "Velocity")));

	ClickElement(context, FindByText(context, board, "Jump"));
	settle();
	const UI::Rect jump = context.GetLayout(FindByText(context, board, "Jump")).borderBox;
	const Vec2I from(static_cast<i32>(jump.x + jump.w * 0.5f), static_cast<i32>(jump.y + jump.h * 0.5f));
	InputEvent drag{};
	drag.button = MouseButton::Left;
	UISystem::Initialize();
	for (i32 step = 0; step <= 4; ++step) {
		drag.type = step == 0 ? InputEventType::MouseDown : InputEventType::MouseMove;
		drag.mousePosPx = Vec2I(from.x, from.y - 6 * step);
		context.InjectInput(drag);
		context.Update();
	}
	const UI::DragPayload* began = UISystem::GetDragPayload();
	const UI::DragPayload payload = began ? *began : UI::DragPayload{};
	UISystem::CancelDragPayload();
	UISystem::Terminate();
	drag.type = InputEventType::MouseUp;
	context.InjectInput(drag);
	context.Update();
	ASSERT_NE(payload.type.GetValue(), 0u);
	context.SetHoverSuppressed(true);
	settle();
	const UI::ElementHandle top = FindByClass(context, board, EditorTheme::Class::InsertStrip, 0);
	ASSERT_TRUE(context.IsAlive(top));
	const UI::Rect band = context.GetLayout(top).borderBox;
	EXPECT_TRUE(context.DeliverDrop(payload, Vec2(band.x + band.w * 0.5f, band.y + band.h * 0.5f)));
	context.SetHoverSuppressed(false);
	settle();

	ClickElement(context, FindByText(context, harness.root, "Save"));
	str written = ReadController("Board.controller");
	const size_t jumpAt = written.find("\"Jump\"");
	const size_t velocityAt = written.find("\"Velocity\"");
	ASSERT_NE(jumpAt, str::npos);
	ASSERT_NE(velocityAt, str::npos);
	EXPECT_EQ(written.find("\"Speed\""), str::npos);
	EXPECT_LT(jumpAt, velocityAt);

	ClickElement(context, FindByText(context, board, "Velocity"));
	settle();
	ASSERT_TRUE(harness.panel->OnCommand(PanelCommand::Delete));
	settle();
	ClickElement(context, FindByText(context, harness.root, "Save"));
	written = ReadController("Board.controller");
	EXPECT_EQ(written.find("\"Velocity\""), str::npos);
	EXPECT_NE(written.find("\"Jump\""), str::npos);
	EXPECT_NE(written.find("\"Idle\""), str::npos);
}

#endif
