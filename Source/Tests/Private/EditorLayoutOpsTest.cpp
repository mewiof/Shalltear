#include "TestsPCH.h"

#include "Shalltear/Editor/EditorLayoutOps.h"

using namespace Shalltear;

namespace {

	MonitorInfo MakeMonitor(RectI workArea, bool primary, u64 id = 1) {
		MonitorInfo monitor;
		monitor.id = id;
		monitor.bounds = workArea;
		monitor.workArea = workArea;
		monitor.primary = primary;
		return monitor;
	}

	std::vector<MonitorInfo> DualMonitors() {
		return {MakeMonitor(RectI{0, 0, 1920, 1040}, true, 1),
				MakeMonitor(RectI{-1280, 0, 1280, 984}, false, 2)};
	}

	std::vector<MonitorInfo> SingleMonitor() {
		return {MakeMonitor(RectI{0, 0, 1920, 1040}, true, 1)};
	}

	WindowPlacement Placement(Vec2I position, Vec2I clientSize, WindowState state = WindowState::Normal) {
		WindowPlacement placement;
		placement.position = position;
		placement.clientSize = clientSize;
		placement.state = state;
		return placement;
	}

} // anonymous namespace

TEST(EditorLayoutOpsTest, MinimizedParkingSpotResolvesBackOnScreen) {
	std::vector<MonitorInfo> monitors = SingleMonitor();
	WindowPlacement saved = Placement({-32000, -32000}, {0, 0});

	WindowPlacement resolved = EditorLayoutOps::ResolvePlacement(saved, monitors);

	EXPECT_TRUE(EditorLayoutOps::IsPlacementVisible(resolved, monitors));
	EXPECT_GE(resolved.clientSize.x, EditorLayoutOps::MinRestoredClientSize.x);
	EXPECT_GE(resolved.clientSize.y, EditorLayoutOps::MinRestoredClientSize.y);
	EXPECT_GE(resolved.position.x, monitors[0].workArea.Left());
	EXPECT_GE(resolved.position.y, monitors[0].workArea.Top());
	EXPECT_LE(resolved.position.x + resolved.clientSize.x, monitors[0].workArea.Right());
	EXPECT_LE(resolved.position.y + resolved.clientSize.y, monitors[0].workArea.Bottom());
}

TEST(EditorLayoutOpsTest, SubMinimumSizeGrowsToEditorFloor) {
	WindowPlacement resolved = EditorLayoutOps::ResolvePlacement(Placement({100, 100}, {160, 90}), SingleMonitor());

	EXPECT_EQ(resolved.clientSize, EditorLayoutOps::MinRestoredClientSize);
	EXPECT_EQ(resolved.position, (Vec2I{100, 100}));
}

TEST(EditorLayoutOpsTest, PlacementFullyOnScreenSurvivesUntouched) {
	WindowPlacement saved = Placement({200, 150}, {1280, 720});

	WindowPlacement resolved = EditorLayoutOps::ResolvePlacement(saved, DualMonitors());

	EXPECT_EQ(resolved.position, saved.position);
	EXPECT_EQ(resolved.clientSize, saved.clientSize);
	EXPECT_EQ(resolved.state, WindowState::Normal);
}

TEST(EditorLayoutOpsTest, MaximizedStateAndRestoredRectBothSurvive) {
	WindowPlacement saved = Placement({300, 200}, {1024, 768}, WindowState::Maximized);

	WindowPlacement resolved = EditorLayoutOps::ResolvePlacement(saved, SingleMonitor());

	EXPECT_EQ(resolved.state, WindowState::Maximized);
	EXPECT_EQ(resolved.position, saved.position);
	EXPECT_EQ(resolved.clientSize, saved.clientSize);
}

TEST(EditorLayoutOpsTest, WindowStraddlingTwoMonitorsIsNotRecentered) {
	WindowPlacement saved = Placement({-200, 100}, {800, 600});

	WindowPlacement resolved = EditorLayoutOps::ResolvePlacement(saved, DualMonitors());

	EXPECT_EQ(resolved.position, saved.position);
}

TEST(EditorLayoutOpsTest, SlightlyOffEdgeButGrabbableIsKept) {
	WindowPlacement saved = Placement({1700, 40}, {800, 600});

	WindowPlacement resolved = EditorLayoutOps::ResolvePlacement(saved, SingleMonitor());

	EXPECT_EQ(resolved.position, saved.position);
}

TEST(EditorLayoutOpsTest, PlacementOnVanishedMonitorMovesToPrimary) {
	WindowPlacement saved = Placement({-1100, 200}, {900, 700});
	ASSERT_TRUE(EditorLayoutOps::IsPlacementVisible(saved, DualMonitors()));

	std::vector<MonitorInfo> remaining = SingleMonitor();
	WindowPlacement resolved = EditorLayoutOps::ResolvePlacement(saved, remaining);

	EXPECT_TRUE(EditorLayoutOps::IsPlacementVisible(resolved, remaining));
	EXPECT_GE(resolved.position.x, remaining[0].workArea.Left());
	EXPECT_LE(resolved.position.x + resolved.clientSize.x, remaining[0].workArea.Right());
}

TEST(EditorLayoutOpsTest, OversizedPlacementShrinks) {
	std::vector<MonitorInfo> monitors = {MakeMonitor(RectI{0, 0, 1024, 768}, true)};

	WindowPlacement resolved = EditorLayoutOps::ResolvePlacement(Placement({-4000, -4000}, {2560, 1440}), monitors);

	EXPECT_EQ(resolved.clientSize, (Vec2I{1024, 768}));
	EXPECT_EQ(resolved.position, (Vec2I{0, 0}));
}

TEST(EditorLayoutOpsTest, SmallMonitorKeepsTopLeft) {
	std::vector<MonitorInfo> monitors = {MakeMonitor(RectI{0, 0, 400, 300}, true)};

	WindowPlacement resolved = EditorLayoutOps::ResolvePlacement(Placement({-9000, -9000}, {0, 0}), monitors);

	EXPECT_EQ(resolved.clientSize, EditorLayoutOps::MinRestoredClientSize);
	EXPECT_EQ(resolved.position, (Vec2I{0, 0}));
}

TEST(EditorLayoutOpsTest, MinimizedStateResolvesToNormal) {
	WindowPlacement saved = Placement({200, 150}, {1280, 720}, WindowState::Minimized);

	EXPECT_EQ(EditorLayoutOps::ResolvePlacement(saved, SingleMonitor()).state, WindowState::Normal);
}

TEST(EditorLayoutOpsTest, NoMonitorsClampSizeOnly) {
	WindowPlacement saved = Placement({-32000, -32000}, {0, 0});

	WindowPlacement resolved = EditorLayoutOps::ResolvePlacement(saved, {});

	EXPECT_EQ(resolved.position, saved.position);
	EXPECT_EQ(resolved.clientSize, EditorLayoutOps::MinRestoredClientSize);
}

TEST(EditorLayoutOpsTest, ExtremeCoordinatesStillResolveOnScreen) {
	std::vector<MonitorInfo> monitors = SingleMonitor();
	constexpr i32 I32_MAX = 2147483647;
	constexpr i32 I32_MIN = -2147483647 - 1;

	const WindowPlacement cases[] = {
		Placement({I32_MAX, I32_MAX}, {800, 600}),
		Placement({I32_MIN, I32_MIN}, {800, 600}),
		Placement({100, 100}, {I32_MAX, I32_MAX}),
		Placement({I32_MAX, 100}, {I32_MAX, 600}),
		Placement({I32_MIN, I32_MIN}, {I32_MAX, I32_MAX}),
	};

	const RectI& work = monitors[0].workArea;
	for (size_t i = 0; i < std::size(cases); ++i) {
		WindowPlacement resolved = EditorLayoutOps::ResolvePlacement(cases[i], monitors);
		EXPECT_TRUE(EditorLayoutOps::IsPlacementVisible(resolved, monitors)) << "case " << i;
		EXPECT_GE(resolved.clientSize.x, EditorLayoutOps::MinRestoredClientSize.x) << "case " << i;
		EXPECT_GE(resolved.clientSize.y, EditorLayoutOps::MinRestoredClientSize.y) << "case " << i;
		EXPECT_LE(resolved.clientSize.x, work.size.x) << "case " << i;
		EXPECT_LE(resolved.clientSize.y, work.size.y) << "case " << i;
	}
}
