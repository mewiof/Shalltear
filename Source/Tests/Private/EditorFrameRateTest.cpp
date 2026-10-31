#include "TestsPCH.h"

#include "Shalltear/Editor/EditorFrameRate.h"

using namespace Shalltear;
using namespace Shalltear::EditorFrameRate;

namespace {

	constexpr f32 AHEAD_SECONDS = 0.00125f;
	constexpr f32 BLOCKED_SECONDS = 0.01263f;

	f32 SmoothedRateOf(const f32* deltas, u32 count) {
		f32 smoothed = 0.0f;
		for (u32 i = 0; i < count; ++i) {
			f32 rate = deltas[i] > 0.0001f ? 1.0f / deltas[i] : 0.0f;
			smoothed = smoothed <= 0.0f ? rate : smoothed * 0.95f + rate * 0.05f;
		}
		return smoothed;
	}

} // namespace

TEST(EditorFrameRateTest, VsyncedPacingReadsAsTheRefreshRate) {
	Meter meter;
	for (u32 i = 0; i < 18; ++i) {
		meter.Add(AHEAD_SECONDS);
		meter.Add(BLOCKED_SECONDS);
	}
	EXPECT_NEAR(meter.GetFps(), 144.0f, 1.0f);
	EXPECT_NEAR(meter.GetMeanMs(), 6.94f, 0.05f);
}

TEST(EditorFrameRateTest, ReportedRateAveragesFrames) {
	f32 deltas[36];
	for (u32 i = 0; i < 18; ++i) {
		deltas[i * 2] = AHEAD_SECONDS;
		deltas[i * 2 + 1] = BLOCKED_SECONDS;
	}
	EXPECT_GT(SmoothedRateOf(deltas, 36), 400.0f);
}

TEST(EditorFrameRateTest, SteadyRateIsReciprocalOfFrameTime) {
	Meter meter;
	for (u32 i = 0; i < 60; ++i) {
		meter.Add(1.0f / 60.0f);
	}
	EXPECT_NEAR(meter.GetFps(), 60.0f, 0.01f);
	EXPECT_NEAR(meter.GetMeanMs(), 1000.0f / 60.0f, 0.01f);
}

TEST(EditorFrameRateTest, EmptyWindowAnswersZeroNotDividing) {
	Meter meter;
	EXPECT_EQ(meter.GetWindowSeconds(), 0.0f);
	EXPECT_EQ(meter.GetFps(), 0.0f);
	EXPECT_EQ(meter.GetMeanMs(), 0.0f);

	meter.Add(0.0f);
	EXPECT_EQ(meter.GetFps(), 0.0f);
	EXPECT_EQ(meter.GetMeanMs(), 0.0f);
}

TEST(EditorFrameRateTest, ResetStartsTheNextWindowFromNothing) {
	Meter meter;
	for (u32 i = 0; i < 10; ++i) {
		meter.Add(0.1f);
	}
	EXPECT_NEAR(meter.GetFps(), 10.0f, 0.01f);
	EXPECT_NEAR(meter.GetWindowSeconds(), 1.0f, 0.001f);

	meter.Reset();
	EXPECT_EQ(meter.GetWindowSeconds(), 0.0f);
	EXPECT_EQ(meter.GetFps(), 0.0f);

	for (u32 i = 0; i < 10; ++i) {
		meter.Add(0.01f);
	}
	EXPECT_NEAR(meter.GetFps(), 100.0f, 0.01f);
}

TEST(EditorFrameRateTest, ABackwardsClockCannotShortenTheWindow) {
	Meter meter;
	for (u32 i = 0; i < 10; ++i) {
		meter.Add(0.01f);
	}
	meter.Add(-1.0f);
	EXPECT_NEAR(meter.GetWindowSeconds(), 0.1f, 0.001f);
	EXPECT_NEAR(meter.GetFps(), 110.0f, 0.1f);
}
