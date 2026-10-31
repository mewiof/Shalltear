#include "TestsPCH.h"
#include "Shalltear/Core/Time.h"

#include "Shalltear/Editor/EditorFrameClock.h"

using namespace Shalltear;

namespace {

	using FrameKind = EditorFrameClock::FrameKind;

	constexpr f64 STEP = 1.0 / 60.0;
	constexpr f32 MAXIMUM = 0.1f;
	constexpr f64 PAUSED_EDITOR_FRAME = 0.0015;

	FrameTime PolicyClock() noexcept {
		FrameTime frame;
		frame.fixedDeltaTime = static_cast<f32>(STEP);
		frame.maximumDeltaTime = MAXIMUM;
		return frame;
	}

	class EditorFrameClockFixture : public ::testing::Test {
	protected:
		EditorFrameClock _clock;
		Time::FrameScope _installed{PolicyClock()};
	};

} // namespace

TEST_F(EditorFrameClockFixture, StepFrameRunsOneFixedStep) {
	for (u32 press = 0; press < 5; ++press) {
		EXPECT_EQ(_clock.BeginFrame(FrameKind::Step, PAUSED_EDITOR_FRAME), 1u) << "press " << press;
	}
}

TEST_F(EditorFrameClockFixture, StepFrameIsOneFixedStep) {
	ASSERT_EQ(_clock.BeginFrame(FrameKind::Step, PAUSED_EDITOR_FRAME), 1u);

	const FrameTime& frame = Time::GetFrame();
	EXPECT_FLOAT_EQ(frame.unscaledDeltaTime, static_cast<f32>(STEP));
	EXPECT_FLOAT_EQ(frame.ComputeGameDelta(), static_cast<f32>(STEP));
}

TEST_F(EditorFrameClockFixture, StepLandsWherePauseLeftInterpolationFactor) {
	ASSERT_EQ(_clock.BeginFrame(FrameKind::Gameplay, STEP * 0.5), 0u);
	const f32 alphaAtPause = _clock.GetAlpha();
	ASSERT_GT(alphaAtPause, 0.0f);

	ASSERT_EQ(_clock.BeginFrame(FrameKind::Step, PAUSED_EDITOR_FRAME), 1u);
	EXPECT_FLOAT_EQ(_clock.GetAlpha(), alphaAtPause);
	ASSERT_EQ(_clock.BeginFrame(FrameKind::Step, PAUSED_EDITOR_FRAME), 1u);
	EXPECT_FLOAT_EQ(_clock.GetAlpha(), alphaAtPause);
}

TEST_F(EditorFrameClockFixture, AFrozenFrameBanksNoGameTime) {
	ASSERT_EQ(_clock.BeginFrame(FrameKind::Gameplay, STEP * 0.5), 0u);
	const f32 alphaAtPause = _clock.GetAlpha();

	for (u32 i = 0; i < 8000; ++i) {
		ASSERT_EQ(_clock.BeginFrame(FrameKind::Frozen, PAUSED_EDITOR_FRAME), 0u) << "frozen frame " << i;
	}
	EXPECT_FLOAT_EQ(_clock.GetAlpha(), alphaAtPause);

	EXPECT_EQ(_clock.BeginFrame(FrameKind::Step, PAUSED_EDITOR_FRAME), 1u);
}

TEST_F(EditorFrameClockFixture, GameplayFrameStillIntegratesWallClock) {
	EXPECT_EQ(_clock.BeginFrame(FrameKind::Gameplay, STEP * 0.9), 0u);
	EXPECT_EQ(_clock.BeginFrame(FrameKind::Gameplay, STEP * 0.9), 1u);

	EXPECT_EQ(_clock.BeginFrame(FrameKind::Gameplay, 1.0), 6u);
}

TEST_F(EditorFrameClockFixture, StepComposesWithAGamePausingItself) {
	Time::SetTimeScale(0.0f);
	EXPECT_EQ(_clock.BeginFrame(FrameKind::Step, PAUSED_EDITOR_FRAME), 0u);

	Time::SetTimeScale(0.5f);
	EXPECT_EQ(_clock.BeginFrame(FrameKind::Step, PAUSED_EDITOR_FRAME), 0u);
	EXPECT_EQ(_clock.BeginFrame(FrameKind::Step, PAUSED_EDITOR_FRAME), 1u);
}

TEST_F(EditorFrameClockFixture, DropBankedTimeEmptiesTheAccumulator) {
	ASSERT_EQ(_clock.BeginFrame(FrameKind::Gameplay, STEP * 0.75), 0u);
	ASSERT_GT(_clock.GetAlpha(), 0.0f);

	_clock.DropBankedTime();

	EXPECT_FLOAT_EQ(_clock.GetAlpha(), 0.0f);
}

TEST(EditorFrameClockTest, GateNamesFrameAndNothingElseDoes) {
	EXPECT_EQ(EditorFrameClock::KindFromGate(false, false), FrameKind::Frozen);
	EXPECT_EQ(EditorFrameClock::KindFromGate(false, true), FrameKind::Frozen);
	EXPECT_EQ(EditorFrameClock::KindFromGate(true, false), FrameKind::Gameplay);
	EXPECT_EQ(EditorFrameClock::KindFromGate(true, true), FrameKind::Step);
}
