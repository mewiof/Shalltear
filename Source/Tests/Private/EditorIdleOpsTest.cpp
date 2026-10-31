#include "TestsPCH.h"

#include "Shalltear/Editor/EditorIdleOps.h"

using namespace Shalltear;
using namespace Shalltear::EditorIdleOps;

namespace {

	IdleInputs Idle(InteractionMode mode) {
		IdleInputs inputs;
		inputs.mode = mode;
		return inputs;
	}

	constexpr InteractionMode THROTTLED_MODES[] = {InteractionMode::Default, InteractionMode::MonitorRefreshRate,
												   InteractionMode::Custom};

} // anonymous namespace

TEST(EditorIdleOpsTest, DefaultIdlesUpToFourMilliseconds) {
	EXPECT_DOUBLE_EQ(IdleSeconds(Idle(InteractionMode::Default)), 0.004);
}

TEST(EditorIdleOpsTest, DefaultStopsIdlingAt5Ms) {
	IdleInputs inputs = Idle(InteractionMode::Default);
	inputs.lastFrameSeconds = 0.0049;
	EXPECT_DOUBLE_EQ(IdleSeconds(inputs), 0.004);
	inputs.lastFrameSeconds = 0.005;
	EXPECT_EQ(IdleSeconds(inputs), 0.0);
}

TEST(EditorIdleOpsTest, NoThrottlingNeverIdles) {
	EXPECT_EQ(IdleSeconds(Idle(InteractionMode::NoThrottling)), 0.0);
}

TEST(EditorIdleOpsTest, RefreshRateIdlesWholeMs) {
	IdleInputs inputs = Idle(InteractionMode::MonitorRefreshRate);
	inputs.refreshRate = 144.0f;
	EXPECT_DOUBLE_EQ(IdleSeconds(inputs), 0.006);
	inputs.refreshRate = 60.0f;
	EXPECT_DOUBLE_EQ(IdleSeconds(inputs), 0.016);
	inputs.refreshRate = 0.5f;
	EXPECT_DOUBLE_EQ(IdleSeconds(inputs), 1.0);
}

TEST(EditorIdleOpsTest, RefreshRateDefaultsTo60Hz) {
	EXPECT_DOUBLE_EQ(IdleSeconds(Idle(InteractionMode::MonitorRefreshRate)), 0.016);
}

TEST(EditorIdleOpsTest, CustomIsClampedToTheSliderRange) {
	IdleInputs inputs = Idle(InteractionMode::Custom);
	inputs.customMilliseconds = 10;
	EXPECT_DOUBLE_EQ(IdleSeconds(inputs), 0.010);
	inputs.customMilliseconds = 0;
	EXPECT_EQ(IdleSeconds(inputs), 0.0);
	inputs.customMilliseconds = -5;
	EXPECT_EQ(IdleSeconds(inputs), 0.0);
	inputs.customMilliseconds = 50;
	EXPECT_DOUBLE_EQ(IdleSeconds(inputs), 0.033);
}

TEST(EditorIdleOpsTest, OnlyDefaultStopsIdlingAfterSlowFrame) {
	IdleInputs inputs = Idle(InteractionMode::Custom);
	inputs.customMilliseconds = 20;
	inputs.lastFrameSeconds = 0.010;
	EXPECT_DOUBLE_EQ(IdleSeconds(inputs), 0.020);
}

TEST(EditorIdleOpsTest, PlayModeIgnoresThrottlingInEveryMode) {
	for (InteractionMode mode : THROTTLED_MODES) {
		IdleInputs inputs = Idle(mode);
		inputs.customMilliseconds = 20;
		ASSERT_GT(IdleSeconds(inputs), 0.0);
		inputs.playing = true;
		EXPECT_EQ(IdleSeconds(inputs), 0.0) << static_cast<int>(mode);
	}
}

TEST(EditorIdleOpsTest, HeldInputStopsIdling) {
	for (InteractionMode mode : THROTTLED_MODES) {
		IdleInputs inputs = Idle(mode);
		inputs.customMilliseconds = 20;
		ASSERT_GT(IdleSeconds(inputs), 0.0);
		inputs.interacting = true;
		EXPECT_EQ(IdleSeconds(inputs), 0.0) << static_cast<int>(mode);
	}
}
