#include "TestsPCH.h"

#include "Shalltear/Core/Math/MathUtil.h"

#ifndef SHALLTEAR_SHARED

#include "Shalltear/Animation/AnimationClip.h"
#include "Shalltear/Animation/Animator.h"

#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics/Camera.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Graphics/SkeletonAsset.h"
#include "Shalltear/Core/Graphics/Skeleton.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Time.h"
#include "Shalltear/Core/Window.h"

#include "AnimationBudget.h"
#include "AnimationSettings.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;
using namespace Shalltear::AnimationImpl;

SLT_DECLARE_LINKED_PACKAGE(Animation)

namespace {

	constexpr f32 FIXED_STEP = 1.0f / 60.0f;

	BudgetViewpoint ViewAt(const Vec3& position, const Vec3& forward = Vec3::Forward(), f32 fovDeg = 60.0f) {
		BudgetViewpoint view;
		view.position = position;
		view.forward = forward.Normalized();
		view.tanHalfFOV = std::tan(fovDeg * 0.5f * Math::DEG_TO_RAD);
		return view;
	}

	f32 Significance(const std::vector<BudgetViewpoint>& views, const Vec3& at, f32 radius = 1.0f, f32 priority = 0.0f) {
		return BudgetSignificance(std::span<const BudgetViewpoint>(views.data(), views.size()), at, radius, priority);
	}

	BudgetCandidate Candidate(u32 row, u32 cost, f32 significance, u32 sinceUpdate = 0) {
		BudgetCandidate candidate;
		candidate.row = row;
		candidate.cost = cost;
		candidate.significance = significance;
		candidate.sinceUpdate = sinceUpdate;
		return candidate;
	}

	std::vector<u32> Select(const std::vector<BudgetCandidate>& candidates, u32 budget, u32 maxInterval = 10) {
		Vector<u32> out{GetMemoryResource(MemTag::Scene)};
		BudgetSelect(std::span<const BudgetCandidate>(candidates.data(), candidates.size()), budget, maxInterval, out);
		return std::vector<u32>(out.begin(), out.end());
	}

} // anonymous namespace

TEST(AnimationBudgetTest, RigFurtherFromCameraIsLessSignificant) {
	std::vector<BudgetViewpoint> views{ViewAt(Vec3{0.0f, 0.0f, 0.0f})};

	const f32 near = Significance(views, Vec3{0.0f, 0.0f, 2.0f});
	const f32 mid = Significance(views, Vec3{0.0f, 0.0f, 10.0f});
	const f32 far = Significance(views, Vec3{0.0f, 0.0f, 50.0f});

	EXPECT_GT(near, mid);
	EXPECT_GT(mid, far);
	EXPECT_GT(far, 0.0f);

	EXPECT_NEAR(mid / far, 5.0f, 0.01f);
}

TEST(AnimationBudgetTest, BiggerRigMoreSignificant) {
	std::vector<BudgetViewpoint> views{ViewAt(Vec3::Zero())};
	const f32 small = Significance(views, Vec3{0.0f, 0.0f, 10.0f}, 0.5f);
	const f32 large = Significance(views, Vec3{0.0f, 0.0f, 10.0f}, 2.0f);
	EXPECT_NEAR(large / small, 4.0f, 0.01f);
}

TEST(AnimationBudgetTest, RigBehindCameraRanksLast) {
	std::vector<BudgetViewpoint> views{ViewAt(Vec3::Zero())};

	const f32 inFront = Significance(views, Vec3{0.0f, 0.0f, 10.0f});
	const f32 behind = Significance(views, Vec3{0.0f, 0.0f, -10.0f});
	EXPECT_GT(inFront, behind);
	EXPECT_GT(behind, 0.0f);

	EXPECT_GT(behind, Significance(views, Vec3{0.0f, 0.0f, -40.0f}));
}

TEST(AnimationBudgetTest, WiderFovLowersSignificance) {
	std::vector<BudgetViewpoint> narrow{ViewAt(Vec3::Zero(), Vec3::Forward(), 30.0f)};
	std::vector<BudgetViewpoint> wide{ViewAt(Vec3::Zero(), Vec3::Forward(), 120.0f)};
	EXPECT_GT(Significance(narrow, Vec3{0.0f, 0.0f, 10.0f}), Significance(wide, Vec3{0.0f, 0.0f, 10.0f}));
}

TEST(AnimationBudgetTest, TwoViewpointsTakeLarger) {
	std::vector<BudgetViewpoint> both{ViewAt(Vec3::Zero()), ViewAt(Vec3{0.0f, 0.0f, 48.0f})};
	std::vector<BudgetViewpoint> firstOnly{both[0]};
	std::vector<BudgetViewpoint> secondOnly{both[1]};

	const f32 at = 50.0f;
	EXPECT_NEAR(Significance(both, Vec3{0.0f, 0.0f, at}), Significance(secondOnly, Vec3{0.0f, 0.0f, at}), 1e-5f);
	EXPECT_GT(Significance(both, Vec3{0.0f, 0.0f, at}), Significance(firstOnly, Vec3{0.0f, 0.0f, at}));
}

TEST(AnimationBudgetTest, PriorityOutranksScreenSize) {
	std::vector<BudgetViewpoint> views{ViewAt(Vec3::Zero())};
	const f32 nearNoPriority = Significance(views, Vec3{0.0f, 0.0f, 1.5f});
	const f32 farWithPriority = Significance(views, Vec3{0.0f, 0.0f, 400.0f}, 1.0f, 1000.0f);
	EXPECT_GT(farWithPriority, nearNoPriority);
}

TEST(AnimationBudgetTest, NoViewpointOrdersByPriority) {
	std::vector<BudgetViewpoint> none;
	EXPECT_FLOAT_EQ(Significance(none, Vec3{0.0f, 0.0f, 1.0f}, 1.0f, 7.0f), 7.0f);
	EXPECT_FLOAT_EQ(Significance(none, Vec3{0.0f, 0.0f, 900.0f}, 1.0f, 7.0f), 7.0f);
	EXPECT_GT(Significance(none, Vec3::Zero(), 1.0f, 7.0f), Significance(none, Vec3::Zero(), 1.0f, 1.0f));
}

TEST(AnimationBudgetTest, CostCountsBonesAndTracks) {
	EXPECT_EQ(BudgetCost(0, 0), 1u);
	EXPECT_EQ(BudgetCost(120, 0), 120u);
	EXPECT_EQ(BudgetCost(0, 40), 40u);
	EXPECT_EQ(BudgetCost(120, 40), 160u);
	EXPECT_GT(BudgetCost(200, 200), BudgetCost(2, 2));
}

TEST(AnimationBudgetTest, ASkeletonRadiusComposesTheParentsScale) {
	SkeletonAsset metres;
	metres.boneNames.push_back("Root");
	metres.boneNames.push_back("Tip");
	metres.parents.push_back(-1);
	metres.parents.push_back(0);
	metres.restPose.push_back(BoneTransform{});
	BoneTransform tip;
	tip.position = Vec3{0.0f, 2.0f, 0.0f};
	metres.restPose.push_back(tip);
	EXPECT_NEAR(BudgetSkeletonRadius(metres), 2.0f, 1e-4f);

	SkeletonAsset centimetres = {};
	centimetres.boneNames.push_back("Root");
	centimetres.boneNames.push_back("Tip");
	centimetres.parents.push_back(-1);
	centimetres.parents.push_back(0);
	BoneTransform root;
	root.scale = Vec3{0.01f, 0.01f, 0.01f};
	centimetres.restPose.push_back(root);
	BoneTransform tipCm;
	tipCm.position = Vec3{0.0f, 200.0f, 0.0f};
	centimetres.restPose.push_back(tipCm);
	EXPECT_NEAR(BudgetSkeletonRadius(centimetres), 2.0f, 1e-4f);

	SkeletonAsset empty;
	EXPECT_FLOAT_EQ(BudgetSkeletonRadius(empty), 1.0f);
}

TEST(AnimationBudgetTest, ZeroBudgetEvaluatesAllInOrder) {
	std::vector<BudgetCandidate> candidates{
		Candidate(0, 500, 0.0f), Candidate(1, 500, 0.0f), Candidate(2, 500, 0.0f)};
	EXPECT_EQ(Select(candidates, 0), (std::vector<u32>{0, 1, 2}));
}

TEST(AnimationBudgetTest, UnderBudgetEvaluatesAllUnsorted) {
	std::vector<BudgetCandidate> candidates{
		Candidate(0, 10, 1.0f), Candidate(1, 10, 2.0f), Candidate(2, 10, 3.0f)};
	EXPECT_EQ(Select(candidates, 30), (std::vector<u32>{0, 1, 2}));
	EXPECT_EQ(Select(candidates, 31), (std::vector<u32>{0, 1, 2}));
}

TEST(AnimationBudgetTest, OverBudgetSpendsOnMostSignificant) {
	std::vector<BudgetCandidate> candidates{
		Candidate(0, 10, 1.0f), Candidate(1, 10, 5.0f), Candidate(2, 10, 3.0f)};
	const std::vector<u32> chosen = Select(candidates, 29);
	ASSERT_EQ(chosen.size(), 2u);
	EXPECT_EQ(chosen[0], 1u);
	EXPECT_EQ(chosen[1], 2u);
}

TEST(AnimationBudgetTest, CheapCandidateBehindUnaffordableOneStillRuns) {
	std::vector<BudgetCandidate> candidates{
		Candidate(0, 10, 9.0f), Candidate(1, 1000, 5.0f), Candidate(2, 5, 1.0f)};
	EXPECT_EQ(Select(candidates, 20), (std::vector<u32>{0, 2}));
}

TEST(AnimationBudgetTest, OverdueCandidateRunsPastBudget) {
	std::vector<BudgetCandidate> candidates{
		Candidate(0, 100, 9.0f, 0),
		Candidate(1, 100, 0.1f, 9)};

	const std::vector<u32> chosen = Select(candidates, 100, 10);
	ASSERT_EQ(chosen.size(), 2u);
	EXPECT_EQ(chosen[0], 0u);

	candidates[1].sinceUpdate = 7;
	EXPECT_EQ(Select(candidates, 100, 10), (std::vector<u32>{0}));
}

TEST(AnimationBudgetTest, OverdueCrowdLeavesVisibleRowBudget) {
	std::vector<BudgetCandidate> candidates{
		Candidate(0, 100, 40.0f, 0),
		Candidate(1, 100, 0.2f, 9),
		Candidate(2, 100, 0.2f, 9),
		Candidate(3, 100, 0.2f, 9)};

	const std::vector<u32> chosen = Select(candidates, 100, 10);
	ASSERT_FALSE(chosen.empty());
	EXPECT_EQ(chosen[0], 0u);
	EXPECT_EQ(chosen.size(), 4u);
}

TEST(AnimationBudgetTest, LongestWaitIsServedFirstAmongOverdue) {
	std::vector<BudgetCandidate> candidates{
		Candidate(0, 10, 9.0f, 9),
		Candidate(1, 10, 9.0f, 12),
		Candidate(2, 10, 9.0f, 10)};
	EXPECT_EQ(Select(candidates, 1), (std::vector<u32>{1, 2, 0}));
}

namespace {

	class AnimationBudgetFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		Handle<Scene> _scene;
		char _token = 0;
		std::vector<Asset*> _owned;
		AnimationSettings* _settings = nullptr;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_animation_budget_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			SLT_CALL_LINKED_PACKAGE(Animation, &_token);

			_scene = SceneSystem::CreateScene("budget");
			ASSERT_TRUE(_scene.IsValid());

			_settings = Settings::DeclareCategory<AnimationSettings>("Animation", SettingScope::Project);
			ASSERT_NE(_settings, nullptr);
			ASSERT_EQ(_settings, &GetSettings());
		}

		void TearDown() override {
			SceneSystem::Terminate();
			for (Asset* asset : _owned) {
				Delete(asset);
			}
			_owned.clear();
			FrameStages::UnregisterOwnedBy(&_token);
			ModuleSystem::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			Settings::Terminate();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		SkeletonAsset* MakeChain(u32 boneCount) {
			SkeletonAsset* skeleton = New<SkeletonAsset>(MemTag::Asset);
			for (u32 bone = 0; bone < boneCount; ++bone) {
				skeleton->boneNames.push_back(str("B") + str(std::to_string(bone).c_str()));
				skeleton->parents.push_back(bone == 0 ? static_cast<i16>(-1) : static_cast<i16>(bone - 1));
				BoneTransform rest;
				if (bone > 0) {
					rest.position = Vec3{0.0f, 1.0f, 0.0f};
				}
				skeleton->restPose.push_back(rest);
			}
			_owned.push_back(skeleton);
			return skeleton;
		}

		AnimationClip* MakeClip(const SkeletonAsset& skeleton) {
			AnimationClip* clip = New<AnimationClip>(MemTag::Asset);
			clip->virtualPath = "project://budget.clip";
			clip->length = 100.0f;
			clip->loop = AnimationLoopMode::Loop;
			for (const str& bone : skeleton.boneNames) {
				AnimationClip::Track track;
				track.kind = AnimationTrackKind::BonePosition;
				track.interp = AnimationInterpolation::Linear;
				track.name = bone;
				track.times.push_back(0.0f);
				track.times.push_back(100.0f);
				track.values.push_back(Vec4(0.0f, 0.0f, 0.0f, 0.0f));
				track.values.push_back(Vec4(100.0f, 0.0f, 0.0f, 0.0f));
				clip->tracks.push_back(std::move(track));
			}
			_owned.push_back(clip);
			return clip;
		}

		Handle<Animator> MakeAnimator(SkeletonAsset* skeleton, AnimationClip* clip, const Vec3& at) {
			Handle<SceneObject> object = SceneSystem::CreateObject("rig", _scene);
			object->SetLocalPosition(at);
			object->AddComponent<Skeleton>()->SetSkeleton(Handle<SkeletonAsset>(skeleton));
			Handle<Animator> animator = object->AddComponent<Animator>();
			animator->SetClip(Handle<AnimationClip>(clip));
			return animator;
		}

		f32 BonePoseX(Handle<Animator> animator, u32 bone) {
			const Skeleton* rig = animator->GetSkeleton().Get();
			if (!rig || rig->GetLocalPose().size() <= bone) {
				return -1.0f;
			}
			return rig->GetLocalPose()[bone].position.x;
		}

		Handle<Camera> MakeCamera(const Vec3& at) {
			Handle<SceneObject> object = SceneSystem::CreateObject("camera", _scene);
			object->SetLocalPosition(at);
			return object->AddComponent<Camera>();
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
	};

} // anonymous namespace

TEST_F(AnimationBudgetFixture, VisibleAnimatorEvaluates) {
	SkeletonAsset* skeleton = MakeChain(20);
	AnimationClip* clip = MakeClip(*skeleton);

	Handle<Animator> far = MakeAnimator(skeleton, clip, Vec3{0.0f, 0.0f, 400.0f});
	Handle<Animator> near = MakeAnimator(skeleton, clip, Vec3{0.0f, 0.0f, 5.0f});
	MakeCamera(Vec3::Zero());

	_settings->evaluationBudget = 40;

	RunUpdate(0.25f);
	EXPECT_NEAR(near->GetTime(), 0.25f, 1e-4f);
	EXPECT_FLOAT_EQ(far->GetTime(), 0.0f);
}

TEST_F(AnimationBudgetFixture, ThrottledClockCatchesUpOwedTime) {
	SkeletonAsset* skeleton = MakeChain(20);
	AnimationClip* clip = MakeClip(*skeleton);
	Handle<Animator> near = MakeAnimator(skeleton, clip, Vec3{0.0f, 0.0f, 5.0f});
	Handle<Animator> far = MakeAnimator(skeleton, clip, Vec3{0.0f, 0.0f, 400.0f});
	MakeCamera(Vec3::Zero());
	_settings->evaluationBudget = 40;
	constexpr i32 MAX_INTERVAL = 4;
	_settings->maxThrottleInterval = MAX_INTERVAL;

	constexpr f32 STEP = 0.1f;
	constexpr int FRAMES = 40;
	for (int frame = 0; frame < FRAMES; ++frame) {
		RunUpdate(STEP);
	}

	const f32 wall = STEP * FRAMES;
	EXPECT_NEAR(near->GetTime(), wall, 1e-3f);

	EXPECT_LE(far->GetTime(), wall + 1e-3f);
	EXPECT_GE(far->GetTime(), wall - STEP * static_cast<f32>(MAX_INTERVAL));
}

TEST_F(AnimationBudgetFixture, ZeroBudgetEvaluatesAllEveryFrame) {
	SkeletonAsset* skeleton = MakeChain(20);
	AnimationClip* clip = MakeClip(*skeleton);
	Handle<Animator> near = MakeAnimator(skeleton, clip, Vec3{0.0f, 0.0f, 5.0f});
	Handle<Animator> far = MakeAnimator(skeleton, clip, Vec3{0.0f, 0.0f, 400.0f});
	MakeCamera(Vec3::Zero());

	ASSERT_EQ(_settings->evaluationBudget, 0);

	for (int frame = 0; frame < 4; ++frame) {
		RunUpdate(0.1f);
	}
	EXPECT_NEAR(near->GetTime(), 0.4f, 1e-4f);
	EXPECT_NEAR(far->GetTime(), 0.4f, 1e-4f);
}

TEST_F(AnimationBudgetFixture, ThrottledPoseMovesBetweenEvaluations) {
	SkeletonAsset* skeleton = MakeChain(20);
	AnimationClip* clip = MakeClip(*skeleton);
	Handle<Animator> near = MakeAnimator(skeleton, clip, Vec3{0.0f, 0.0f, 5.0f});
	Handle<Animator> far = MakeAnimator(skeleton, clip, Vec3{0.0f, 0.0f, 400.0f});
	MakeCamera(Vec3::Zero());

	_settings->evaluationBudget = 40;
	_settings->maxThrottleInterval = 5;

	constexpr f32 STEP = 0.05f;
	for (int frame = 0; frame < 20; ++frame) {
		RunUpdate(STEP);
	}

	u32 movedFrames = 0;
	u32 heldFrames = 0;
	f32 previous = BonePoseX(far, 1);
	for (int frame = 0; frame < 20; ++frame) {
		RunUpdate(STEP);
		const f32 now = BonePoseX(far, 1);
		if (Math::Abs(now - previous) > 1e-6f) {
			++movedFrames;
		} else {
			++heldFrames;
		}
		previous = now;
	}

	EXPECT_EQ(heldFrames, 0u) << heldFrames << " of 20 frames";
	EXPECT_EQ(movedFrames, 20u);

	EXPECT_LT(BonePoseX(far, 1), BonePoseX(near, 1));
}

TEST_F(AnimationBudgetFixture, ThrottledRigReachesPoseEvaluationProduced) {
	SkeletonAsset* skeleton = MakeChain(20);
	AnimationClip* clip = MakeClip(*skeleton);
	Handle<Animator> near = MakeAnimator(skeleton, clip, Vec3{0.0f, 0.0f, 5.0f});
	Handle<Animator> far = MakeAnimator(skeleton, clip, Vec3{0.0f, 0.0f, 400.0f});
	MakeCamera(Vec3::Zero());

	_settings->evaluationBudget = 40;
	_settings->maxThrottleInterval = 4;

	constexpr f32 STEP = 0.05f;
	constexpr int FRAMES = 60;
	for (int frame = 0; frame < FRAMES; ++frame) {
		RunUpdate(STEP);
	}

	const f32 drawn = BonePoseX(far, 1);
	const f32 evaluated = far->GetTime();
	EXPECT_LE(drawn, evaluated + 1e-4f);
	EXPECT_GE(drawn, evaluated - STEP * 4.0f - 1e-4f);

	EXPECT_NEAR(BonePoseX(near, 1), near->GetTime(), 1e-4f);
}

TEST_F(AnimationBudgetFixture, NoBudgetNoInterpolation) {
	SkeletonAsset* skeleton = MakeChain(20);
	AnimationClip* clip = MakeClip(*skeleton);
	Handle<Animator> one = MakeAnimator(skeleton, clip, Vec3{0.0f, 0.0f, 5.0f});
	Handle<Animator> two = MakeAnimator(skeleton, clip, Vec3{0.0f, 0.0f, 400.0f});
	MakeCamera(Vec3::Zero());

	ASSERT_EQ(_settings->evaluationBudget, 0);
	for (int frame = 0; frame < 10; ++frame) {
		RunUpdate(0.05f);
	}
	EXPECT_NEAR(BonePoseX(one, 1), one->GetTime(), 1e-4f);
	EXPECT_NEAR(BonePoseX(two, 1), two->GetTime(), 1e-4f);
}

TEST_F(AnimationBudgetFixture, ThrottledEventsFireOnEvaluation) {
	SkeletonAsset* skeleton = MakeChain(20);
	AnimationClip* clip = MakeClip(*skeleton);

	for (u32 index = 1; index <= 8; ++index) {
		AnimationClip::Track track;
		track.kind = AnimationTrackKind::Event;
		track.interp = AnimationInterpolation::Step;
		track.name = "tick";
		track.times.push_back(static_cast<f32>(index));
		track.values.push_back(Vec4(0.0f, 0.0f, 0.0f, 0.0f));
		clip->tracks.push_back(std::move(track));
	}

	Handle<Animator> near = MakeAnimator(skeleton, clip, Vec3{0.0f, 0.0f, 5.0f});
	Handle<Animator> far = MakeAnimator(skeleton, clip, Vec3{0.0f, 0.0f, 400.0f});
	MakeCamera(Vec3::Zero());

	u32 nearEvents = 0;
	u32 farEvents = 0;
	near->Subscribe([](const AnimationEvent&, void* user) { ++*static_cast<u32*>(user); }, &nearEvents);
	far->Subscribe([](const AnimationEvent&, void* user) { ++*static_cast<u32*>(user); }, &farEvents);

	_settings->evaluationBudget = static_cast<i32>(BudgetCost(20, static_cast<u32>(clip->tracks.size())));
	_settings->maxThrottleInterval = 4;

	for (int frame = 0; frame < 48; ++frame) {
		RunUpdate(0.25f);
	}

	EXPECT_EQ(nearEvents, 8u);
	EXPECT_EQ(farEvents, 8u);
}

TEST_F(AnimationBudgetFixture, TurningWalkOffHoldsThrottledPoseInstead) {
	SkeletonAsset* skeleton = MakeChain(20);
	AnimationClip* clip = MakeClip(*skeleton);
	MakeAnimator(skeleton, clip, Vec3{0.0f, 0.0f, 5.0f});
	Handle<Animator> far = MakeAnimator(skeleton, clip, Vec3{0.0f, 0.0f, 400.0f});
	MakeCamera(Vec3::Zero());

	_settings->evaluationBudget = 40;
	_settings->maxThrottleInterval = 5;
	_settings->interpolateThrottled = false;

	constexpr f32 STEP = 0.05f;
	for (int frame = 0; frame < 20; ++frame) {
		RunUpdate(STEP);
	}

	u32 heldFrames = 0;
	f32 previous = BonePoseX(far, 1);
	for (int frame = 0; frame < 20; ++frame) {
		RunUpdate(STEP);
		const f32 now = BonePoseX(far, 1);
		if (Math::Abs(now - previous) <= 1e-6f) {
			++heldFrames;
		}
		previous = now;
	}
	EXPECT_GT(heldFrames, 0u);

	EXPECT_GT(far->GetTime(), 0.0f);
}

TEST_F(AnimationBudgetFixture, HandlerDisablingAnimatorKeepsRows) {
	SkeletonAsset* skeleton = MakeChain(20);
	AnimationClip* clip = MakeClip(*skeleton);

	AnimationClip::Track track;
	track.kind = AnimationTrackKind::Event;
	track.interp = AnimationInterpolation::Step;
	track.name = "disableTheOther";
	track.times.push_back(0.0f);
	track.values.push_back(Vec4(0.0f, 0.0f, 0.0f, 0.0f));
	clip->tracks.push_back(std::move(track));

	std::vector<Handle<Animator>> animators;
	for (int index = 0; index < 4; ++index) {
		animators.push_back(MakeAnimator(skeleton, clip, Vec3{0.0f, 0.0f, 5.0f + static_cast<f32>(index)}));
	}
	MakeCamera(Vec3::Zero());

	static Handle<Animator> victim;
	victim = animators[2];
	animators[0]->Subscribe([](const AnimationEvent&, void*) {
		if (Animator* target = victim.Get()) {
			target->SetEnabled(false);
		}
	});

	RunUpdate(0.1f);

	EXPECT_FALSE(animators[2]->IsEnabledSelf());
	EXPECT_NEAR(animators[0]->GetTime(), 0.1f, 1e-4f);
	EXPECT_NEAR(animators[1]->GetTime(), 0.1f, 1e-4f);
	EXPECT_NEAR(animators[3]->GetTime(), 0.1f, 1e-4f);

	RunUpdate(0.1f);
	EXPECT_NEAR(animators[0]->GetTime(), 0.2f, 1e-4f);
	EXPECT_NEAR(animators[3]->GetTime(), 0.2f, 1e-4f);
}

TEST_F(AnimationBudgetFixture, Animators200HoldBudgetNoneStarved) {
	constexpr u32 COUNT = 200;
	constexpr u32 BONES = 20;
	constexpr u32 COST_EACH = BONES * 2;
	constexpr i32 MAX_INTERVAL = 10;
	constexpr i32 BUDGET = static_cast<i32>(COST_EACH * COUNT / 5);

	SkeletonAsset* skeleton = MakeChain(BONES);
	AnimationClip* clip = MakeClip(*skeleton);
	MakeCamera(Vec3::Zero());

	std::vector<Handle<Animator>> animators;
	animators.reserve(COUNT);
	for (u32 index = 0; index < COUNT; ++index) {
		animators.push_back(MakeAnimator(skeleton, clip, Vec3{0.0f, 0.0f, 5.0f + static_cast<f32>(index) * 3.0f}));
	}

	_settings->evaluationBudget = BUDGET;
	_settings->maxThrottleInterval = MAX_INTERVAL;

	_settings->evaluationBudget = 0;
	RunUpdate(1.0f / 60.0f);
	_settings->evaluationBudget = BUDGET;

	std::vector<f32> previous(COUNT, 0.0f);
	for (u32 index = 0; index < COUNT; ++index) {
		previous[index] = animators[index]->GetTime();
	}
	std::vector<u32> evaluations(COUNT, 0);
	std::vector<u32> longestGap(COUNT, 0);
	std::vector<u32> sinceUpdate(COUNT, 0);

	constexpr u32 FRAMES = 60;
	u32 worstFrameCost = 0;
	for (u32 frame = 0; frame < FRAMES; ++frame) {
		RunUpdate(1.0f / 60.0f);

		u32 frameCost = 0;
		for (u32 index = 0; index < COUNT; ++index) {
			const f32 now = animators[index]->GetTime();
			if (now > previous[index] + 1e-6f) {
				previous[index] = now;
				++evaluations[index];
				frameCost += COST_EACH;
				sinceUpdate[index] = 0;
			} else {
				++sinceUpdate[index];
				longestGap[index] = Math::Max(longestGap[index], sinceUpdate[index]);
			}
		}
		worstFrameCost = Math::Max(worstFrameCost, frameCost);
	}

	EXPECT_LE(worstFrameCost, static_cast<u32>(BUDGET) * 2u) << worstFrameCost << ' ' << BUDGET;

	const u32 unthrottled = COST_EACH * COUNT;
	EXPECT_LT(worstFrameCost, unthrottled);

	for (u32 index = 0; index < COUNT; ++index) {
		EXPECT_GT(evaluations[index], 0u) << "animator " << index << ' ' << FRAMES << " frames";
		EXPECT_LE(longestGap[index], static_cast<u32>(MAX_INTERVAL)) << "animator " << index << " waited " << longestGap[index] << ' ' << MAX_INTERVAL;
	}

	EXPECT_EQ(evaluations.front(), FRAMES);
	EXPECT_LT(evaluations.back(), FRAMES);
}

#endif
