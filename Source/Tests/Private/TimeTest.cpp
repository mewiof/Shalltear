#include "TestsPCH.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Time.h"
#include "Shalltear/Core/Window/FrameLoop.h"

#include "TimeSettings.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>

using namespace Shalltear;

TEST(TimeTest, TicksAreMonotonicAndIncreasing) {
	u64 first = Time::GetTicks();
	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	u64 second = Time::GetTicks();

	EXPECT_GT(second, first);
}

TEST(TimeTest, RealtimeSinceStartupIncreases) {
	f64 first = Time::GetRealtimeSinceStartup();
	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	f64 second = Time::GetRealtimeSinceStartup();

	EXPECT_GT(second, first);
}

TEST(TimeTest, TicksPerSecondIsReasonable) {
	u64 freq = Time::GetTicksPerSecond();
	EXPECT_GT(freq, 1000u);
}

TEST(StopwatchTest, DefaultConstructedIsStoppedAtZero) {
	Stopwatch sw;
	EXPECT_FALSE(sw.IsRunning());
	EXPECT_EQ(sw.GetElapsedSeconds(), 0.0);
	EXPECT_EQ(sw.GetElapsedMilliseconds(), 0.0);
}

TEST(StopwatchTest, StartBeginsAccumulatingTime) {
	Stopwatch sw;
	sw.Start();
	EXPECT_TRUE(sw.IsRunning());

	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	EXPECT_GT(sw.GetElapsedMilliseconds(), 0.0);
}

TEST(StopwatchTest, StopPausesAccumulation) {
	Stopwatch sw;
	sw.Start();
	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	sw.Stop();
	EXPECT_FALSE(sw.IsRunning());

	f64 elapsed = sw.GetElapsedMilliseconds();
	std::this_thread::sleep_for(std::chrono::milliseconds(5));

	EXPECT_EQ(sw.GetElapsedMilliseconds(), elapsed);
}

TEST(StopwatchTest, ResumeAccumulatesCorrectly) {
	Stopwatch sw;
	sw.Start();
	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	sw.Stop();

	f64 firstSegment = sw.GetElapsedMilliseconds();

	sw.Start();
	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	sw.Stop();

	f64 secondSegment = sw.GetElapsedMilliseconds();
	EXPECT_GT(secondSegment, firstSegment);
}

TEST(StopwatchTest, ResetClearsTimeAndStops) {
	Stopwatch sw;
	sw.Start();
	std::this_thread::sleep_for(std::chrono::milliseconds(5));
	sw.Reset();

	EXPECT_FALSE(sw.IsRunning());
	EXPECT_EQ(sw.GetElapsedMilliseconds(), 0.0);
}

TEST(StopwatchTest, RestartClearsTimeAndStarts) {
	Stopwatch sw;
	sw.Start();
	std::this_thread::sleep_for(std::chrono::milliseconds(5));

	sw.Restart();
	EXPECT_TRUE(sw.IsRunning());

	EXPECT_LT(sw.GetElapsedMilliseconds(), 2.0);
}

TEST(FrameTimerTest, InitialState) {
	FrameTimer ft;
	EXPECT_EQ(ft.GetUnscaledDeltaTime(), 0.0f);
}

TEST(FrameTimerTest, TickMeasuresTheWallDelta) {
	FrameTimer ft;
	std::this_thread::sleep_for(std::chrono::milliseconds(10));
	ft.Tick();

	EXPECT_GT(ft.GetUnscaledDeltaTime(), 0.0f);
}

TEST(FrameTimerTest, LongStallIsMeasuredNotClamped) {
	FrameTimer ft;
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	ft.Tick();

	EXPECT_GT(ft.GetUnscaledDeltaTime(), 0.14f);
}

namespace {

	const u8 _CLOCK_OWNER = 0;

	constexpr f64 STEP = 0.125;

	constexpr f32 UNBOUNDED = 1000.0f;

	FrameTime UnboundedClock() noexcept {
		FrameTime frame;
		frame.fixedDeltaTime = static_cast<f32>(STEP);
		frame.maximumDeltaTime = UNBOUNDED;
		return frame;
	}

	struct StampProbe {
		FrameTime seen;
		u32 runs = 0;
	};

	void RecordStamp(const FrameStageArgs&, void* user) {
		auto* probe = static_cast<StampProbe*>(user);
		probe->seen = Time::GetFrame();
		++probe->runs;
	}

	struct ProbeThrew {};

	void ThrowFromStage(const FrameStageArgs&, void*) {
		throw ProbeThrew{};
	}

	void SetScaleFromStage(const FrameStageArgs&, void* user) {
		Time::SetTimeScale(*static_cast<f32*>(user));
	}

	class FrameClockFixture : public ::testing::Test {
	protected:
		void TearDown() override { FrameStages::UnregisterOwnedBy(&_CLOCK_OWNER); }

		static FrameStageArgs Args() { return FrameStageArgs{}; }

		Time::FrameScope _clock{UnboundedClock()};
	};

} // namespace

TEST_F(FrameClockFixture, BeginFramePublishesWallClock) {
	Time::BeginFrame(0.25);

	EXPECT_FLOAT_EQ(Time::GetUnscaledDeltaTime(), 0.25f);
	EXPECT_DOUBLE_EQ(Time::GetUnscaledTime(), 0.25);
	EXPECT_EQ(Time::GetFrameCount(), 1u);

	EXPECT_FLOAT_EQ(Time::GetDeltaTime(), 0.0f);
	EXPECT_DOUBLE_EQ(Time::GetTime(), 0.0);

	Time::BeginFrame(0.25);
	EXPECT_DOUBLE_EQ(Time::GetUnscaledTime(), 0.5);
	EXPECT_EQ(Time::GetFrameCount(), 2u);
	EXPECT_DOUBLE_EQ(Time::GetTime(), 0.0);
}

TEST_F(FrameClockFixture, WallClockRunsWhileFrozen) {
	const f64 realtimeBefore = Time::GetRealtimeSinceStartup();
	Time::BeginFrame(0.25);
	std::this_thread::sleep_for(std::chrono::milliseconds(5));

	EXPECT_GT(Time::GetRealtimeSinceStartup(), realtimeBefore);
	EXPECT_DOUBLE_EQ(Time::GetTime(), 0.0);
}

TEST_F(FrameClockFixture, FirstStageAdvancesGameClock) {
	Time::BeginFrame(0.25);

	FrameStages::Run(FrameStage::UIUpdate, Args());
	EXPECT_DOUBLE_EQ(Time::GetTime(), 0.0);
	EXPECT_FLOAT_EQ(Time::GetDeltaTime(), 0.0f);

	FrameStages::Run(FrameStage::Update, Args());
	EXPECT_FLOAT_EQ(Time::GetDeltaTime(), 0.25f);
	EXPECT_DOUBLE_EQ(Time::GetTime(), 0.25);

	FrameStages::Run(FrameStage::LateUpdate, Args());
	EXPECT_DOUBLE_EQ(Time::GetTime(), 0.25);
	EXPECT_FLOAT_EQ(Time::GetDeltaTime(), 0.25f);
}

TEST_F(FrameClockFixture, FixedStepRestampsSnapshot) {
	StampProbe probe;
	ASSERT_TRUE(FrameStages::Register(FrameStage::FixedUpdate, FrameStagePosition::Normal, &RecordStamp, &probe, &_CLOCK_OWNER));

	Time::BeginFrame(0.25);
	FrameStages::Run(FrameStage::FixedUpdate, Args());
	ASSERT_EQ(probe.runs, 1u);

	EXPECT_FLOAT_EQ(probe.seen.deltaTime, static_cast<f32>(STEP));
	EXPECT_TRUE(probe.seen.inFixedTimeStep);
	EXPECT_FLOAT_EQ(probe.seen.unscaledDeltaTime, static_cast<f32>(STEP));
	EXPECT_DOUBLE_EQ(probe.seen.time, STEP);
	EXPECT_DOUBLE_EQ(probe.seen.unscaledTime, STEP);

	EXPECT_FALSE(Time::IsInFixedTimeStep());
	EXPECT_FLOAT_EQ(Time::GetDeltaTime(), 0.25f);
	EXPECT_FLOAT_EQ(Time::GetUnscaledDeltaTime(), 0.25f);
	EXPECT_DOUBLE_EQ(Time::GetTime(), 0.25);
	EXPECT_DOUBLE_EQ(Time::GetUnscaledTime(), 0.25);
}

TEST_F(FrameClockFixture, FixedStampAgreesWithTwin) {
	FrameTime slow = UnboundedClock();
	slow.timeScale = 0.25f;
	Time::FrameScope quarterSpeed(slow);

	StampProbe probe;
	ASSERT_TRUE(FrameStages::Register(FrameStage::FixedUpdate, FrameStagePosition::Normal, &RecordStamp, &probe, &_CLOCK_OWNER));

	Time::BeginFrame(0.25);
	FrameStages::Run(FrameStage::FixedUpdate, Args());

	EXPECT_FLOAT_EQ(probe.seen.deltaTime, static_cast<f32>(STEP));
	EXPECT_FLOAT_EQ(probe.seen.unscaledDeltaTime, static_cast<f32>(STEP / 0.25));
	EXPECT_DOUBLE_EQ(probe.seen.time, STEP);
	EXPECT_DOUBLE_EQ(probe.seen.unscaledTime, STEP / 0.25);
	EXPECT_FLOAT_EQ(probe.seen.timeScale, 0.25f);
	EXPECT_FLOAT_EQ(Time::GetDeltaTime(), 0.0625f);
}

TEST_F(FrameClockFixture, HitchStepsCarryTimestamps) {
	StampProbe probe;
	ASSERT_TRUE(FrameStages::Register(FrameStage::FixedUpdate, FrameStagePosition::Normal, &RecordStamp, &probe, &_CLOCK_OWNER));

	Time::BeginFrame(3.0 * STEP);
	for (u32 i = 1; i <= 3; ++i) {
		FrameStages::Run(FrameStage::FixedUpdate, Args());
		EXPECT_EQ(probe.runs, i);
		EXPECT_DOUBLE_EQ(probe.seen.time, static_cast<f64>(i) * STEP) << "step " << i;
		EXPECT_FLOAT_EQ(probe.seen.deltaTime, static_cast<f32>(STEP));
	}

	EXPECT_DOUBLE_EQ(Time::GetTime(), probe.seen.time);
}

TEST_F(FrameClockFixture, StageThatThrowsStillUnwindsFixedStamp) {
	ASSERT_TRUE(FrameStages::Register(FrameStage::FixedUpdate, FrameStagePosition::Normal, &ThrowFromStage, nullptr, &_CLOCK_OWNER));

	Time::BeginFrame(0.25);
	EXPECT_THROW(FrameStages::Run(FrameStage::FixedUpdate, Args()), ProbeThrew);

	EXPECT_FALSE(Time::IsInFixedTimeStep());
	EXPECT_FLOAT_EQ(Time::GetDeltaTime(), 0.25f);
	EXPECT_DOUBLE_EQ(Time::GetTime(), 0.25);
}

TEST_F(FrameClockFixture, FrameScopeRestoresOnReturn) {
	Time::BeginFrame(0.25);
	FrameStages::Run(FrameStage::Update, Args());
	const FrameTime before = Time::GetFrame();

	[&]() {
		FrameTime other;
		other.deltaTime = 9.0f;
		other.time = 9.0;
		other.timeScale = 0.5f;
		Time::FrameScope scope(other);
		EXPECT_FLOAT_EQ(Time::GetDeltaTime(), 9.0f);
		EXPECT_FLOAT_EQ(Time::GetTimeScale(), 0.5f);
		return;
	}();

	EXPECT_FLOAT_EQ(Time::GetDeltaTime(), before.deltaTime);
	EXPECT_DOUBLE_EQ(Time::GetTime(), before.time);
	EXPECT_FLOAT_EQ(Time::GetTimeScale(), before.timeScale);
	EXPECT_EQ(Time::GetFrameCount(), before.frameCount);
}

TEST_F(FrameClockFixture, ScopeStepsDontLeak) {
	StampProbe probe;
	ASSERT_TRUE(FrameStages::Register(FrameStage::FixedUpdate, FrameStagePosition::Normal, &RecordStamp, &probe, &_CLOCK_OWNER));

	Time::BeginFrame(0.25);
	{
		FrameTime preview = UnboundedClock();
		preview.time = 100.0;
		preview.unscaledTime = 100.0;
		Time::FrameScope scope(preview);
		FrameStages::Run(FrameStage::FixedUpdate, Args());
		EXPECT_DOUBLE_EQ(probe.seen.time, 100.0 + STEP);
	}

	FrameStages::Run(FrameStage::FixedUpdate, Args());
	EXPECT_DOUBLE_EQ(probe.seen.time, STEP);
}

TEST_F(FrameClockFixture, InterpolationIsPastLastStep) {
	StampProbe probe;
	ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Normal, &RecordStamp, &probe, &_CLOCK_OWNER));

	Time::BeginFrame(STEP / 4.0);
	FrameStages::Run(FrameStage::LateUpdate, Args());
	EXPECT_FLOAT_EQ(probe.seen.fixedAlpha, 0.25f);

	Time::BeginFrame(STEP / 4.0);
	FrameStages::Run(FrameStage::LateUpdate, Args());
	EXPECT_FLOAT_EQ(probe.seen.fixedAlpha, 0.5f);

	FrameStages::Run(FrameStage::LateUpdate, Args());
	EXPECT_FLOAT_EQ(probe.seen.fixedAlpha, 0.5f);
}

TEST_F(FrameClockFixture, FixedPassReadsCurrentStep) {
	StampProbe inStep;
	StampProbe afterStep;
	ASSERT_TRUE(FrameStages::Register(FrameStage::FixedUpdate, FrameStagePosition::Normal, &RecordStamp, &inStep, &_CLOCK_OWNER));
	ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Normal, &RecordStamp, &afterStep, &_CLOCK_OWNER));

	Time::BeginFrame(STEP * 1.25);
	FrameStages::Run(FrameStage::FixedUpdate, Args());
	FrameStages::Run(FrameStage::LateUpdate, Args());

	ASSERT_EQ(inStep.runs, 1u);
	EXPECT_TRUE(inStep.seen.inFixedTimeStep);
	EXPECT_FLOAT_EQ(inStep.seen.fixedAlpha, 0.0f);

	EXPECT_FLOAT_EQ(afterStep.seen.fixedAlpha, 0.25f);
}

TEST_F(FrameClockFixture, NonGameplayFrameKeepsPosition) {
	StampProbe gameplay;
	StampProbe frozen;
	ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Normal, &RecordStamp, &gameplay, &_CLOCK_OWNER));
	ASSERT_TRUE(FrameStages::Register(FrameStage::Render, FrameStagePosition::Normal, &RecordStamp, &frozen, &_CLOCK_OWNER));

	Time::BeginFrame(STEP / 4.0);
	FrameStages::Run(FrameStage::LateUpdate, Args());
	ASSERT_FLOAT_EQ(gameplay.seen.fixedAlpha, 0.25f);

	for (int i = 0; i < 10; ++i) {
		Time::BeginFrame(STEP / 4.0);
		FrameStages::Run(FrameStage::Render, Args());
		ASSERT_FLOAT_EQ(frozen.seen.fixedAlpha, 0.25f) << "frozen frame " << i;
	}
}

TEST_F(FrameClockFixture, PositionTracksAccumulator) {
	FrameTime bounded = UnboundedClock();
	bounded.maximumDeltaTime = static_cast<f32>(STEP * 4.0);
	Time::FrameScope clamped(bounded);

	StampProbe probe;
	ASSERT_TRUE(FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Normal, &RecordStamp, &probe, &_CLOCK_OWNER));

	FixedTimestep fixed(STEP, static_cast<f64>(bounded.maximumDeltaTime));

	auto RunFrame = [&](f64 wallDeltaSeconds) {
		Time::BeginFrame(wallDeltaSeconds);
		const u32 steps = fixed.Advance(static_cast<f64>(Time::GetFrame().ComputeGameDelta()));
		for (u32 i = 0; i < steps; ++i) {
			FrameStages::Run(FrameStage::FixedUpdate, Args());
		}
		FrameStages::Run(FrameStage::LateUpdate, Args());
	};

	for (int i = 0; i < 40; ++i) {
		RunFrame(STEP / 8.0);
		ASSERT_NEAR(probe.seen.fixedAlpha, fixed.GetAlpha(), 1e-5f) << "healthy frame " << i;
	}

	RunFrame(30.0);
	EXPECT_NEAR(probe.seen.fixedAlpha, fixed.GetAlpha(), 1e-5f);
	EXPECT_LT(probe.seen.fixedAlpha, 1.0f);

	for (int i = 0; i < 200; ++i) {
		RunFrame(STEP / 8.0);
		ASSERT_NEAR(probe.seen.fixedAlpha, fixed.GetAlpha(), 1e-5f) << "frame " << i;
		ASSERT_LT(probe.seen.fixedAlpha, 1.0f) << "frame " << i;
	}
}

TEST_F(FrameClockFixture, ScaleSlowsGameClockOnly) {
	Time::SetTimeScale(0.5f);
	EXPECT_FLOAT_EQ(Time::GetTimeScale(), 0.5f);

	Time::BeginFrame(0.25);
	FrameStages::Run(FrameStage::Update, Args());

	EXPECT_FLOAT_EQ(Time::GetDeltaTime(), 0.125f);
	EXPECT_DOUBLE_EQ(Time::GetTime(), 0.125);
	EXPECT_FLOAT_EQ(Time::GetUnscaledDeltaTime(), 0.25f);
	EXPECT_DOUBLE_EQ(Time::GetUnscaledTime(), 0.25);

	Time::BeginFrame(0.25);
	FrameStages::Run(FrameStage::Update, Args());
	EXPECT_DOUBLE_EQ(Time::GetTime(), 0.25);
	EXPECT_DOUBLE_EQ(Time::GetUnscaledTime(), 0.5);
}

TEST_F(FrameClockFixture, NegativeScaleIgnored) {
	Time::SetTimeScale(0.5f);
	Time::SetTimeScale(-1.0f);
	EXPECT_FLOAT_EQ(Time::GetTimeScale(), 0.5f);

	FixedTimestep fixed(STEP, UNBOUNDED);
	Time::BeginFrame(0.25);
	EXPECT_EQ(fixed.Advance(Time::GetFrame().ComputeGameDelta()), 1u);
	EXPECT_GE(fixed.GetAlpha(), 0.0f);
	EXPECT_LT(fixed.GetAlpha(), 1.0f);
}

TEST_F(FrameClockFixture, ZeroScaleStopsFixedPass) {
	StampProbe fixedProbe;
	StampProbe updateProbe;
	ASSERT_TRUE(FrameStages::Register(FrameStage::FixedUpdate, FrameStagePosition::Normal, &RecordStamp, &fixedProbe, &_CLOCK_OWNER));
	ASSERT_TRUE(FrameStages::Register(FrameStage::Update, FrameStagePosition::Normal, &RecordStamp, &updateProbe, &_CLOCK_OWNER));

	FixedTimestep fixed(STEP, UNBOUNDED);
	Time::SetTimeScale(0.0f);
	Time::BeginFrame(4.0 * STEP);

	const u32 steps = fixed.Advance(Time::GetFrame().ComputeGameDelta());
	EXPECT_EQ(steps, 0u);
	for (u32 i = 0; i < steps; ++i) {
		FrameStages::Run(FrameStage::FixedUpdate, Args());
	}
	FrameStages::Run(FrameStage::Update, Args());

	EXPECT_EQ(fixedProbe.runs, 0u);
	EXPECT_EQ(updateProbe.runs, 1u);
	EXPECT_FLOAT_EQ(updateProbe.seen.deltaTime, 0.0f);
	EXPECT_DOUBLE_EQ(updateProbe.seen.time, 0.0);
	EXPECT_FLOAT_EQ(updateProbe.seen.unscaledDeltaTime, static_cast<f32>(4.0 * STEP));
	EXPECT_DOUBLE_EQ(updateProbe.seen.unscaledTime, 4.0 * STEP);
}

TEST_F(FrameClockFixture, TickScaleAppliesNextFrame) {
	f32 target = 0.0f;
	ASSERT_TRUE(FrameStages::Register(FrameStage::Update, FrameStagePosition::Normal, &SetScaleFromStage, &target, &_CLOCK_OWNER));

	Time::BeginFrame(0.25);
	FrameStages::Run(FrameStage::Update, Args());

	EXPECT_FLOAT_EQ(Time::GetTimeScale(), 0.0f);
	EXPECT_FLOAT_EQ(Time::GetDeltaTime(), 0.25f);
	EXPECT_DOUBLE_EQ(Time::GetTime(), 0.25);

	FrameStages::Run(FrameStage::LateUpdate, Args());
	EXPECT_FLOAT_EQ(Time::GetDeltaTime(), 0.25f);
	EXPECT_DOUBLE_EQ(Time::GetTime(), 0.25);

	Time::BeginFrame(0.25);
	FrameStages::Run(FrameStage::Update, Args());
	EXPECT_FLOAT_EQ(Time::GetDeltaTime(), 0.0f);
	EXPECT_DOUBLE_EQ(Time::GetTime(), 0.25);
	EXPECT_DOUBLE_EQ(Time::GetUnscaledTime(), 0.5);
}

TEST_F(FrameClockFixture, MaximumBoundsGameClock) {
	FrameTime bounded = UnboundedClock();
	bounded.maximumDeltaTime = 0.1f;
	Time::FrameScope scope(bounded);

	Time::BeginFrame(2.5);
	FrameStages::Run(FrameStage::Update, Args());

	EXPECT_FLOAT_EQ(Time::GetDeltaTime(), 0.1f);
	EXPECT_DOUBLE_EQ(Time::GetTime(), static_cast<f64>(0.1f));
	EXPECT_FLOAT_EQ(Time::GetUnscaledDeltaTime(), 2.5f);
	EXPECT_DOUBLE_EQ(Time::GetUnscaledTime(), 2.5);
}

TEST_F(FrameClockFixture, MaximumBoundsScaledDelta) {
	FrameTime bounded = UnboundedClock();
	bounded.maximumDeltaTime = 0.1f;
	bounded.timeScale = 2.0f;
	Time::FrameScope scope(bounded);

	Time::BeginFrame(0.06);
	FrameStages::Run(FrameStage::Update, Args());

	EXPECT_FLOAT_EQ(Time::GetDeltaTime(), 0.1f);
	EXPECT_FLOAT_EQ(Time::GetUnscaledDeltaTime(), 0.06f);
}

TEST_F(FrameClockFixture, ResetRestoresInitialClock) {
	StampProbe probe;
	ASSERT_TRUE(FrameStages::Register(FrameStage::FixedUpdate, FrameStagePosition::Normal, &RecordStamp, &probe, &_CLOCK_OWNER));

	Time::SetTimeScale(0.2f);
	Time::BeginFrame(0.25);
	FrameStages::Run(FrameStage::FixedUpdate, Args());
	FrameStages::Run(FrameStage::Update, Args());
	ASSERT_GT(Time::GetTime(), 0.0);
	ASSERT_GT(Time::GetUnscaledTime(), 0.0);
	ASSERT_EQ(Time::GetFrameCount(), 1u);

	Time::ResetFrameClock();

	EXPECT_DOUBLE_EQ(Time::GetTime(), 0.0);
	EXPECT_DOUBLE_EQ(Time::GetUnscaledTime(), 0.0);
	EXPECT_EQ(Time::GetFrameCount(), 0u);
	EXPECT_FLOAT_EQ(Time::GetTimeScale(), 1.0f);
	EXPECT_FLOAT_EQ(Time::GetFrame().maximumDeltaTime, UNBOUNDED);

	Time::BeginFrame(0.25);
	FrameStages::Run(FrameStage::FixedUpdate, Args());
	EXPECT_EQ(probe.runs, 2u);
	EXPECT_DOUBLE_EQ(probe.seen.time, STEP);
}

namespace {

	class TimeSettingsFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_time_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
		}

		void TearDown() override {
			Settings::Terminate();
			Time::BeginFrame(0.0);
			Time::ResetFrameClock();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		void WriteProject(const char* text) { FileSystem::WriteTextFile(_paths.project, str(text)); }
	};

} // namespace

TEST_F(TimeSettingsFixture, ProjectNumbersReachClock) {
	WriteProject("[Time]\nfixedDeltaTime = 0.05\nmaximumDeltaTime = 0.2\n");
	Settings::Initialize(_paths);

	Time::BeginFrame(0.016);
	EXPECT_FLOAT_EQ(Time::GetFixedDeltaTime(), 0.05f);
	EXPECT_FLOAT_EQ(Time::GetFrame().maximumDeltaTime, 0.2f);

	FixedTimestep fixed(Time::GetFixedDeltaTime(), Time::GetFrame().maximumDeltaTime);
	EXPECT_EQ(fixed.GetMaxStepsPerFrame(), 4u);
}

TEST_F(TimeSettingsFixture, EditReachesNextFrame) {
	Settings::Initialize(_paths);
	Time::BeginFrame(0.016);
	ASSERT_FLOAT_EQ(Time::GetFixedDeltaTime(), DEFAULT_FIXED_DELTA_TIME);

	TimeSettings* live = Settings::DeclareCategory<TimeSettings>("Time", SettingScope::Project);
	ASSERT_NE(live, nullptr);
	live->fixedDeltaTime = 0.02f;

	Time::BeginFrame(0.016);
	EXPECT_FLOAT_EQ(Time::GetFixedDeltaTime(), 0.02f);
}

TEST_F(TimeSettingsFixture, MaximumBelowStepIsRaisedNotObeyed) {
	WriteProject("[Time]\nfixedDeltaTime = 0.05\nmaximumDeltaTime = 0.01\n");
	Settings::Initialize(_paths);

	Time::BeginFrame(0.016);
	EXPECT_FLOAT_EQ(Time::GetFrame().maximumDeltaTime, 0.05f);
}

TEST_F(TimeSettingsFixture, ZeroStepUsesFloor) {
	WriteProject("[Time]\nfixedDeltaTime = 0\nmaximumDeltaTime = 0.1\n");
	Settings::Initialize(_paths);

	Time::BeginFrame(0.016);
	ASSERT_FLOAT_EQ(Time::GetFixedDeltaTime(), MIN_FIXED_DELTA_TIME);
	FixedTimestep fixed(Time::GetFixedDeltaTime(), Time::GetFrame().maximumDeltaTime);
	EXPECT_GE(fixed.GetMaxStepsPerFrame(), 1u);
}

TEST_F(TimeSettingsFixture, OpenScopeKeepsPolicy) {
	WriteProject("[Time]\nfixedDeltaTime = 0.05\n");
	Settings::Initialize(_paths);
	Time::BeginFrame(0.016);
	ASSERT_FLOAT_EQ(Time::GetFixedDeltaTime(), 0.05f);

	{
		FrameTime preview;
		preview.fixedDeltaTime = 0.25f;
		Time::FrameScope scope(preview);
		Time::BeginFrame(0.016);
		EXPECT_FLOAT_EQ(Time::GetFixedDeltaTime(), 0.25f);
	}

	Time::BeginFrame(0.016);
	EXPECT_FLOAT_EQ(Time::GetFixedDeltaTime(), 0.05f);
}

TEST(ScopedTimerTest, MacroCompilesAndRuns) {
	{
		SLT_PROFILE_SCOPE("TestScope1");
		std::this_thread::sleep_for(std::chrono::milliseconds(2));

		SLT_PROFILE_SCOPE("TestScope2");
	}
	SUCCEED();
}
