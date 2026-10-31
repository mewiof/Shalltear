#include "TestsPCH.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Scene/Behavior.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Core/Time.h"
#include "Shalltear/Core/Window/FrameLoop.h"

using namespace Shalltear;

namespace {

	const u8 _OWNER_A = 0;
	const u8 _OWNER_B = 0;

	struct Probe {
		u32 runs = 0;
		Vector<i32>* trace = nullptr;
		i32 mark = 0;
	};

	void RunProbe(const FrameStageArgs&, void* user) {
		auto* probe = static_cast<Probe*>(user);
		++probe->runs;
		if (probe->trace) {
			probe->trace->push_back(probe->mark);
		}
	}

	class FsTickBehavior : public Behavior {
	public:
		i32 updates = 0;
		i32 fixedUpdates = 0;
		i32 lateUpdates = 0;
		f32 lastDelta = 0.0f;
		f64 lastStep = 0.0;
		f32 lastUnscaledDelta = 0.0f;

		SLT_REFLECT_COMPONENT(FsTickBehavior, Behavior)
		SLT_END_REFLECT()
	protected:
		void OnUpdate() override {
			++updates;
			lastDelta = Time::GetDeltaTime();
			lastUnscaledDelta = Time::GetUnscaledDeltaTime();
			lastInFixed = Time::IsInFixedTimeStep();
		}
		void OnFixedUpdate() override {
			++fixedUpdates;
			lastStep = Time::GetDeltaTime();
			lastInFixed = Time::IsInFixedTimeStep();
		}
		void OnLateUpdate() override {
			++lateUpdates;
			lastDelta = Time::GetDeltaTime();
			lastInFixed = Time::IsInFixedTimeStep();
		}
	public:
		bool lastInFixed = false;
	};

	SLT_REGISTER_TYPE(FsTickBehavior)

	class FrameStagesFixture : public ::testing::Test {
	protected:
		void TearDown() override {
			FrameStages::UnregisterOwnedBy(&_OWNER_A);
			FrameStages::UnregisterOwnedBy(&_OWNER_B);
		}

		static FrameStageArgs Args() {
			return FrameStageArgs{};
		}
	};

} // anonymous namespace

TEST(FrameStageListTest, StagesAre6InDocumentedOrder) {
	ASSERT_EQ(FRAME_STAGE_COUNT, 6u);
	EXPECT_EQ(FRAME_STAGES[0], FrameStage::FixedUpdate);
	EXPECT_EQ(FRAME_STAGES[1], FrameStage::Update);
	EXPECT_EQ(FRAME_STAGES[2], FrameStage::UIUpdate);
	EXPECT_EQ(FRAME_STAGES[3], FrameStage::LateUpdate);
	EXPECT_EQ(FRAME_STAGES[4], FrameStage::EndFrame);
	EXPECT_EQ(FRAME_STAGES[5], FrameStage::Render);

	i32 endFrameAt = -1;
	i32 renderAt = -1;
	for (u32 i = 0; i < FRAME_STAGE_COUNT; ++i) {
		if (FRAME_STAGES[i] == FrameStage::EndFrame) {
			endFrameAt = static_cast<i32>(i);
		}
		if (FRAME_STAGES[i] == FrameStage::Render) {
			renderAt = static_cast<i32>(i);
		}
	}
	ASSERT_NE(endFrameAt, -1);
	ASSERT_NE(renderAt, -1);
	EXPECT_LT(endFrameAt, renderAt);
}

TEST(FrameStageListTest, EveryStageRoundTripsThroughItsName) {
	for (FrameStage stage : FRAME_STAGES) {
		std::string_view name = GetFrameStageName(stage);
		EXPECT_FALSE(name.empty());
		FrameStage back = FrameStage::Render;
		ASSERT_TRUE(FindFrameStage(name, back)) << name << "'";
		EXPECT_EQ(back, stage);
	}
	EXPECT_EQ(GetFrameStageName(FrameStage::UIUpdate), "UIUpdate");
}

TEST(FrameStageListTest, UnknownStageNameRefused) {
	FrameStage found = FrameStage::EndFrame;
	EXPECT_FALSE(FindFrameStage("PostUpdate", found));
	EXPECT_EQ(found, FrameStage::EndFrame);
	EXPECT_FALSE(FindFrameStage("update", found));
	EXPECT_FALSE(FindFrameStage("", found));
}

TEST_F(FrameStagesFixture, RegistrationRunsWithStage) {
	Probe probe;
	ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Normal, &RunProbe, &probe, &_OWNER_A));

	FrameStages::Run(FrameStage::LateUpdate, Args());
	EXPECT_EQ(probe.runs, 1u);

	FrameStages::Run(FrameStage::FixedUpdate, Args());
	FrameStages::Run(FrameStage::EndFrame, Args());
	EXPECT_EQ(probe.runs, 1u);
}

TEST_F(FrameStagesFixture, NullFunctionOrNullOwnerIsRefused) {
	Probe probe;
	EXPECT_FALSE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Normal, nullptr, &probe, &_OWNER_A));
	EXPECT_FALSE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Normal, &RunProbe, &probe, nullptr));

	FrameStages::Run(FrameStage::LateUpdate, Args());
	EXPECT_EQ(probe.runs, 0u);
}

TEST_F(FrameStagesFixture, StageRunsRegistrationsInOrder) {
	Vector<i32> trace{GetMemoryResource(MemTag::Core)};
	Probe first;
	first.trace = &trace;
	first.mark = 1;
	Probe second;
	second.trace = &trace;
	second.mark = 2;

	ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Normal, &RunProbe, &first, &_OWNER_A));
	ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Normal, &RunProbe, &second, &_OWNER_B));

	FrameStages::Run(FrameStage::LateUpdate, Args());
	ASSERT_EQ(trace.size(), 2u);
	EXPECT_EQ(trace[0], 1);
	EXPECT_EQ(trace[1], 2);
}

TEST_F(FrameStagesFixture, PositionsOrderStage) {
	auto run = [this](bool reversed, Vector<i32>& trace) {
		Probe early;
		early.trace = &trace;
		early.mark = 1;
		Probe normal;
		normal.trace = &trace;
		normal.mark = 2;
		Probe late;
		late.trace = &trace;
		late.mark = 3;

		if (reversed) {
			ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Late, &RunProbe, &late, &_OWNER_A));
			ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Normal, &RunProbe, &normal, &_OWNER_B));
			ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Early, &RunProbe, &early, &_OWNER_A));
		} else {
			ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Early, &RunProbe, &early, &_OWNER_A));
			ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Normal, &RunProbe, &normal, &_OWNER_B));
			ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Late, &RunProbe, &late, &_OWNER_A));
		}

		FrameStages::Run(FrameStage::LateUpdate, Args());
		FrameStages::UnregisterOwnedBy(&_OWNER_A);
		FrameStages::UnregisterOwnedBy(&_OWNER_B);
	};

	Vector<i32> forward{GetMemoryResource(MemTag::Core)};
	run(false, forward);
	ASSERT_EQ(forward.size(), 3u);
	EXPECT_EQ(forward[0], 1);
	EXPECT_EQ(forward[1], 2);
	EXPECT_EQ(forward[2], 3);

	Vector<i32> backward{GetMemoryResource(MemTag::Core)};
	run(true, backward);
	ASSERT_EQ(backward.size(), 3u);
	EXPECT_EQ(backward[0], 1);
	EXPECT_EQ(backward[1], 2);
	EXPECT_EQ(backward[2], 3);
}

TEST_F(FrameStagesFixture, SharedPositionKeepsArrivalOrder) {
	Vector<i32> trace{GetMemoryResource(MemTag::Core)};
	Probe first;
	first.trace = &trace;
	first.mark = 1;
	Probe second;
	second.trace = &trace;
	second.mark = 2;
	Probe early;
	early.trace = &trace;
	early.mark = 3;

	ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Late, &RunProbe, &first, &_OWNER_A));
	ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Late, &RunProbe, &second, &_OWNER_B));
	ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Early, &RunProbe, &early, &_OWNER_A));

	FrameStages::Run(FrameStage::LateUpdate, Args());
	ASSERT_EQ(trace.size(), 3u);
	EXPECT_EQ(trace[0], 3);
	EXPECT_EQ(trace[1], 1);
	EXPECT_EQ(trace[2], 2);
}

TEST_F(FrameStagesFixture, InWalkRegistrationWaitsNextWalk) {
	static Vector<i32>* _trace = nullptr;
	static Probe _added;
	Vector<i32> trace{GetMemoryResource(MemTag::Core)};
	_trace = &trace;
	_added = Probe{};
	_added.trace = &trace;
	_added.mark = 9;

	Probe sitting;
	sitting.trace = &trace;
	sitting.mark = 1;

	static bool _addedOnce = false;
	_addedOnce = false;
	struct Adder {
		static void Run(const FrameStageArgs&, void*) {
			_trace->push_back(0);
			if (_addedOnce) {
				return;
			}
			_addedOnce = true;
			FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Early, &RunProbe, &_added, &_OWNER_B);
		}
	};
	ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Normal, &Adder::Run, nullptr, &_OWNER_A));
	ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Late, &RunProbe, &sitting, &_OWNER_A));

	FrameStages::Run(FrameStage::LateUpdate, Args());
	ASSERT_EQ(trace.size(), 3u);
	EXPECT_EQ(trace[0], 0);
	EXPECT_EQ(trace[1], 1);
	EXPECT_EQ(trace[2], 9);

	trace.clear();
	FrameStages::Run(FrameStage::LateUpdate, Args());
	ASSERT_EQ(trace.size(), 3u);
	EXPECT_EQ(trace[0], 9);
	EXPECT_EQ(trace[1], 0);
	EXPECT_EQ(trace[2], 1);
	_trace = nullptr;
}

TEST_F(FrameStagesFixture, SweepTakesOwnersStages) {
	Probe fixedProbe;
	Probe lateProbe;
	ASSERT_TRUE(FrameStages::Register(FrameStage::FixedUpdate, FrameStagePosition::Normal, &RunProbe, &fixedProbe, &_OWNER_A));
	ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Normal, &RunProbe, &lateProbe, &_OWNER_A));

	FrameStages::Run(FrameStage::FixedUpdate, Args());
	FrameStages::Run(FrameStage::LateUpdate, Args());
	EXPECT_EQ(fixedProbe.runs, 1u);
	EXPECT_EQ(lateProbe.runs, 1u);

	EXPECT_EQ(FrameStages::UnregisterOwnedBy(&_OWNER_A), 2u);
	FrameStages::Run(FrameStage::FixedUpdate, Args());
	FrameStages::Run(FrameStage::LateUpdate, Args());
	EXPECT_EQ(fixedProbe.runs, 1u);
	EXPECT_EQ(lateProbe.runs, 1u);
}

TEST_F(FrameStagesFixture, SweepLeavesOtherOwners) {
	Probe kept;
	Probe swept;
	ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Normal, &RunProbe, &kept, &_OWNER_A));
	ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Normal, &RunProbe, &swept, &_OWNER_B));

	EXPECT_EQ(FrameStages::UnregisterOwnedBy(&_OWNER_B), 1u);
	FrameStages::Run(FrameStage::LateUpdate, Args());
	EXPECT_EQ(kept.runs, 1u);
	EXPECT_EQ(swept.runs, 0u);
}

TEST_F(FrameStagesFixture, NullOwnerSweepsNothing) {
	Probe probe;
	ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Normal, &RunProbe, &probe, &_OWNER_A));

	EXPECT_EQ(FrameStages::UnregisterOwnedBy(nullptr), 0u);
	EXPECT_EQ(FrameStages::UnregisterOwnedBy(&_OWNER_B), 0u);

	FrameStages::Run(FrameStage::LateUpdate, Args());
	EXPECT_EQ(probe.runs, 1u);
}

TEST_F(FrameStagesFixture, SweepTakesBothRegistrations) {
	Probe first;
	Probe second;
	ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Normal, &RunProbe, &first, &_OWNER_A));
	ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Normal, &RunProbe, &second, &_OWNER_A));

	EXPECT_EQ(FrameStages::UnregisterOwnedBy(&_OWNER_A), 2u);
	FrameStages::Run(FrameStage::LateUpdate, Args());
	EXPECT_EQ(first.runs, 0u);
	EXPECT_EQ(second.runs, 0u);
}

TEST_F(FrameStagesFixture, InStageSweepSkipsRemoved) {
	static Probe tail;
	tail = Probe{};

	struct Sweeper {
		static void Run(const FrameStageArgs&, void*) {
			FrameStages::UnregisterOwnedBy(&_OWNER_B);
		}
	};

	ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Normal, &Sweeper::Run, nullptr, &_OWNER_A));
	ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Normal, &RunProbe, &tail, &_OWNER_B));

	FrameStages::Run(FrameStage::LateUpdate, Args());
	EXPECT_EQ(tail.runs, 0u);
}

TEST_F(FrameStagesFixture, InStageRegistrationRunsSameFrame) {
	static Probe added;
	added = Probe{};

	struct Adder {
		static void Run(const FrameStageArgs&, void*) {
			FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Normal, &RunProbe, &added, &_OWNER_B);
		}
	};

	ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Normal, &Adder::Run, nullptr, &_OWNER_A));
	FrameStages::Run(FrameStage::LateUpdate, Args());
	EXPECT_EQ(added.runs, 1u);
}

class FrameStageWiringFixture : public ::testing::Test {
protected:
	Handle<Scene> _scene;
	Handle<SceneObject> _object;

	void SetUp() override {
		SceneSystem::Initialize();
		_scene = SceneSystem::CreateScene("frame-stage-wiring");
		ASSERT_TRUE(_scene.IsValid());
		_object = SceneSystem::CreateObject("ticker", _scene);
		ASSERT_TRUE(_object.IsValid());
		ASSERT_NE(_object->AddComponent<FsTickBehavior>().Get(), nullptr);
	}

	void TearDown() override { SceneSystem::Terminate(); }

	FsTickBehavior* Probe() const { return _object->GetComponent<FsTickBehavior>().Get(); }
};

TEST_F(FrameStageWiringFixture, SceneStagesGetTimeValues) {
	FsTickBehavior* probe = Probe();
	ASSERT_NE(probe, nullptr);

	FrameTime unbounded;
	unbounded.fixedDeltaTime = 0.5f;
	unbounded.maximumDeltaTime = 1000.0f;
	Time::FrameScope clock{unbounded};
	Time::BeginFrame(0.25);

	FrameStageArgs args;

	FrameStages::Run(FrameStage::FixedUpdate, args);
	EXPECT_EQ(probe->fixedUpdates, 1);
	EXPECT_EQ(probe->updates, 0);
	EXPECT_FLOAT_EQ(probe->lastStep, 0.5f);
	EXPECT_TRUE(probe->lastInFixed);

	FrameStages::Run(FrameStage::Update, args);
	EXPECT_EQ(probe->updates, 1);
	EXPECT_EQ(probe->lateUpdates, 0);
	EXPECT_FLOAT_EQ(probe->lastDelta, 0.25f);
	EXPECT_FALSE(probe->lastInFixed);

	FrameStages::Run(FrameStage::LateUpdate, args);
	EXPECT_EQ(probe->lateUpdates, 1);
	EXPECT_EQ(probe->updates, 1);
	EXPECT_FLOAT_EQ(probe->lastDelta, 0.25f);
}

TEST_F(FrameStageWiringFixture, SelfPauseStopsFixedPass) {
	FsTickBehavior* probe = Probe();
	ASSERT_NE(probe, nullptr);

	FrameTime clockState;
	clockState.fixedDeltaTime = 0.5f;
	Time::FrameScope clock{clockState};
	FixedTimestep fixed(0.5, 2.0);

	Time::SetTimeScale(0.0f);
	Time::BeginFrame(2.0);

	const u32 steps = fixed.Advance(Time::GetFrame().ComputeGameDelta());
	EXPECT_EQ(steps, 0u);

	FrameStageArgs args;
	for (u32 i = 0; i < steps; ++i) {
		FrameStages::Run(FrameStage::FixedUpdate, args);
	}
	FrameStages::Run(FrameStage::Update, args);
	FrameStages::Run(FrameStage::LateUpdate, args);

	EXPECT_EQ(probe->fixedUpdates, 0);
	EXPECT_EQ(probe->updates, 1);
	EXPECT_EQ(probe->lateUpdates, 1);
	EXPECT_FLOAT_EQ(probe->lastDelta, 0.0f);
	EXPECT_FLOAT_EQ(probe->lastUnscaledDelta, 2.0f);
}

TEST_F(FrameStageWiringFixture, EndFrameFreesDestroyedObject) {
	SceneSystem::Destroy(_object);
	EXPECT_NE(_object.Get(), nullptr);

	FrameStageArgs args;
	FrameStages::Run(FrameStage::EndFrame, args);
	EXPECT_EQ(_object.Get(), nullptr);
}

TEST_F(FrameStageWiringFixture, TerminateRemovesScenePasses) {
	FrameStageArgs args;
	FrameStages::Run(FrameStage::Update, args);
	ASSERT_EQ(Probe()->updates, 1);

	SceneSystem::Terminate();
	SceneSystem::Initialize();

	Handle<Scene> restarted = SceneSystem::CreateScene("frame-stage-restart");
	ASSERT_TRUE(restarted.IsValid());
	Handle<SceneObject> object = SceneSystem::CreateObject("ticker", restarted);
	ASSERT_TRUE(object.IsValid());
	FsTickBehavior* probe = object->AddComponent<FsTickBehavior>().Get();
	ASSERT_NE(probe, nullptr);

	FrameStages::Run(FrameStage::Update, args);
	EXPECT_EQ(probe->updates, 1);
}

TEST_F(FrameStageWiringFixture, EarlyAndLateBracketScenePass) {
	struct SawTicks {
		const FsTickBehavior* tick = nullptr;
		i32 fixedUpdates = -1;
	};
	struct Record {
		static void Run(const FrameStageArgs&, void* user) {
			auto* seen = static_cast<SawTicks*>(user);
			seen->fixedUpdates = seen->tick->fixedUpdates;
		}
	};
	static const u8 _owner = 0;

	FsTickBehavior* probe = Probe();
	ASSERT_NE(probe, nullptr);
	SawTicks early{probe, -1};
	SawTicks late{probe, -1};

	ASSERT_TRUE(FrameStages::Register(FrameStage::FixedUpdate, FrameStagePosition::Late, &Record::Run, &late, &_owner));
	ASSERT_TRUE(FrameStages::Register(FrameStage::FixedUpdate, FrameStagePosition::Early, &Record::Run, &early, &_owner));

	FrameStageArgs args;
	FrameStages::Run(FrameStage::FixedUpdate, args);

	EXPECT_EQ(probe->fixedUpdates, 1);
	EXPECT_EQ(early.fixedUpdates, 0);
	EXPECT_EQ(late.fixedUpdates, 1);

	EXPECT_EQ(FrameStages::UnregisterOwnedBy(&_owner), 2u);
}
