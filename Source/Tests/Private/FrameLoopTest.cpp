#include "TestsPCH.h"

#include "Shalltear/Core/Window/FrameLoop.h"
#include "Shalltear/Core/Time.h"
#include <thread>
#include <chrono>

using namespace Shalltear;

namespace {
	constexpr f64 STEP = 1.0 / 60.0;
	constexpr f64 UNBOUNDED = 1000.0;
	constexpr f64 FIVE_STEPS = 5.0 * STEP;
} // namespace

TEST(FrameLoopTest, ExactStepProducesOneStep) {
	FixedTimestep fixed(STEP, UNBOUNDED);
	EXPECT_EQ(fixed.Advance(STEP), 1u);
	EXPECT_NEAR(fixed.GetAlpha(), 0.0f, 1e-6f);
}

TEST(FrameLoopTest, PartialStepAccumulates) {
	FixedTimestep fixed(STEP, UNBOUNDED);
	EXPECT_EQ(fixed.Advance(STEP * 0.5), 0u);
	EXPECT_NEAR(fixed.GetAlpha(), 0.5f, 1e-4f);

	EXPECT_EQ(fixed.Advance(STEP * 0.5), 1u);
	EXPECT_NEAR(fixed.GetAlpha(), 0.0f, 1e-4f);
}

TEST(FrameLoopTest, LargeDeltaProducesMultipleSteps) {
	FixedTimestep fixed(STEP, UNBOUNDED);
	EXPECT_EQ(fixed.Advance(STEP * 3.5), 3u);
	EXPECT_NEAR(fixed.GetAlpha(), 0.5f, 1e-4f);
}

TEST(FrameLoopTest, SpiralOfDeathClampDropsSurplus) {
	FixedTimestep fixed(STEP, FIVE_STEPS);
	EXPECT_EQ(fixed.Advance(1.0), 5u);
	EXPECT_NEAR(fixed.GetAlpha(), 0.0f, 1e-4f);
	EXPECT_EQ(fixed.Advance(STEP), 1u);
}

TEST(FrameLoopTest, NegativeDeltaIsIgnored) {
	FixedTimestep fixed(STEP, UNBOUNDED);
	EXPECT_EQ(fixed.Advance(STEP * 0.5), 0u);
	EXPECT_EQ(fixed.Advance(-10.0), 0u);
	EXPECT_NEAR(fixed.GetAlpha(), 0.5f, 1e-4f);
}

TEST(FrameLoopTest, ResetDropsBankedTime) {
	FixedTimestep fixed(STEP, UNBOUNDED);
	fixed.Advance(STEP * 0.75);
	fixed.Reset();
	EXPECT_NEAR(fixed.GetAlpha(), 0.0f, 1e-6f);
	EXPECT_EQ(fixed.Advance(STEP * 0.5), 0u);
}

TEST(FrameLoopTest, AlphaStaysBelowOne) {
	FixedTimestep fixed(STEP, FIVE_STEPS);
	f64 deltas[] = {0.001, 0.02, 0.16, 0.007, 1.5, 0.033};
	for (f64 delta : deltas) {
		fixed.Advance(delta);
		EXPECT_GE(fixed.GetAlpha(), 0.0f);
		EXPECT_LT(fixed.GetAlpha(), 1.0f);
	}
}

TEST(FrameLoopTest, StepCapDerivedFromMaximum) {
	FixedTimestep fixed(0.02, 0.16);
	EXPECT_DOUBLE_EQ(fixed.GetStepSeconds(), 0.02);
	EXPECT_EQ(fixed.GetMaxStepsPerFrame(), 8u);
}

TEST(FrameLoopTest, AMaximumBelowOneStepStillSimulates) {
	FixedTimestep fixed(0.02, 0.005);
	EXPECT_EQ(fixed.GetMaxStepsPerFrame(), 1u);
	EXPECT_EQ(fixed.Advance(1.0), 1u);
}

TEST(FrameLoopTest, DerivedCapSurvivesF32Values) {
	FixedTimestep fixed(static_cast<f64>(1.0f / 60.0f), static_cast<f64>(0.1f));
	EXPECT_EQ(fixed.GetMaxStepsPerFrame(), 6u);
}

TEST(FrameLoopTest, HitchBoundHasOneOwner) {
	FrameTimer timer;
	std::this_thread::sleep_for(std::chrono::milliseconds(150));
	timer.Tick();
	ASSERT_GT(timer.GetUnscaledDeltaTime(), 0.1f);

	FrameTime frame;
	frame.unscaledDeltaTime = timer.GetUnscaledDeltaTime();
	const f64 gameDelta = static_cast<f64>(frame.ComputeGameDelta());
	EXPECT_FLOAT_EQ(static_cast<f32>(gameDelta), frame.maximumDeltaTime);

	FixedTimestep boundOnly(STEP, UNBOUNDED);
	FixedTimestep shipped(STEP, static_cast<f64>(frame.maximumDeltaTime));

	const u32 allowedByTheBound = boundOnly.Advance(gameDelta);
	EXPECT_EQ(shipped.Advance(gameDelta), allowedByTheBound);
	EXPECT_EQ(allowedByTheBound, shipped.GetMaxStepsPerFrame());
}
