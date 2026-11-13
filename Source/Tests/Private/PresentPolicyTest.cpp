#include "TestsPCH.h"

#include "Graphics/GraphicsSettings.h"
#include "Graphics/PresentPolicy.h"
#include "ScreenInternal.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Time.h"
#include "Shalltear/Core/UI.h"
#include "Shalltear/Core/Window.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

TEST(PresentPolicyTest, VsyncOnIsAlwaysFifo) {
	EXPECT_EQ(RHI::ChoosePresentPolicy(true, false), RHI::PresentPolicy::Fifo);
	EXPECT_EQ(RHI::ChoosePresentPolicy(true, true), RHI::PresentPolicy::Fifo);
}

TEST(PresentPolicyTest, VsyncOffTakesMailbox) {
	EXPECT_EQ(RHI::ChoosePresentPolicy(false, true), RHI::PresentPolicy::Mailbox);
}

TEST(PresentPolicyTest, VsyncOffFallsBackToFifo) {
	EXPECT_EQ(RHI::ChoosePresentPolicy(false, false), RHI::PresentPolicy::Fifo);
}

TEST(PresentPolicyTest, OnlyFifoWaitsForDisplay) {
	EXPECT_TRUE(RHI::WaitsForDisplay(RHI::PresentPolicy::Fifo, true));
	EXPECT_FALSE(RHI::WaitsForDisplay(RHI::PresentPolicy::Fifo, false));
	EXPECT_FALSE(RHI::WaitsForDisplay(RHI::PresentPolicy::Mailbox, true));
	EXPECT_FALSE(RHI::WaitsForDisplay(RHI::PresentPolicy::Mailbox, false));
}

TEST(PresentPolicyTest, WaitTimeoutFollowsRefreshInsideBounds) {
	EXPECT_DOUBLE_EQ(RHI::PresentWaitTimeout(1.0 / 60.0), 4.0 / 60.0);
	EXPECT_DOUBLE_EQ(RHI::PresentWaitTimeout(1.0 / 144.0), 4.0 / 144.0);
	EXPECT_DOUBLE_EQ(RHI::PresentWaitTimeout(1.0 / 360.0), RHI::PRESENT_WAIT_TIMEOUT_MIN_SECONDS);
	EXPECT_DOUBLE_EQ(RHI::PresentWaitTimeout(1.0 / 24.0), RHI::PRESENT_WAIT_TIMEOUT_MAX_SECONDS);
}

TEST(PresentPolicyTest, WaitRetryBacksOffToCap) {
	f64 retry = RHI::PRESENT_WAIT_RETRY_SECONDS;
	retry = RHI::NextPresentWaitRetry(retry);
	EXPECT_DOUBLE_EQ(retry, RHI::PRESENT_WAIT_RETRY_SECONDS * 2.0);
	for (u32 round = 0; round < 32; ++round) {
		const f64 next = RHI::NextPresentWaitRetry(retry);
		EXPECT_GE(next, retry);
		retry = next;
	}
	EXPECT_DOUBLE_EQ(retry, RHI::PRESENT_WAIT_RETRY_MAX_SECONDS);
}

namespace {

	constexpr f64 REFRESH144 = 1.0 / 144.0;
	constexpr f64 REFRESH60 = 1.0 / 60.0;
	constexpr f64 ON_TIME = 0.0;

	RHI::PresentQueue Advance(RHI::PresentQueue queue, u32 frames, f64 work, f64 latency, f64 refresh) {
		for (u32 frame = 0; frame < frames; ++frame) {
			queue = RHI::AdvancePresentQueue(queue, work, latency, refresh);
		}
		return queue;
	}

} // namespace

TEST(PresentQueueTest, StartsOneFrameDeep) {
	EXPECT_EQ(RHI::PresentQueue{}.depth, 1u);
}

TEST(PresentQueueTest, LightFramesStayAtOne) {
	const RHI::PresentQueue queue = Advance({}, 1000, 0.001, REFRESH144, REFRESH144);
	EXPECT_EQ(queue.depth, 1u);
	EXPECT_EQ(queue.late, 0u);
}

TEST(PresentQueueTest, HeavyFramesQueueTwo) {
	const f64 heavy = REFRESH144 - RHI::PRESENT_LATE_SLACK_SECONDS + 0.0005;
	EXPECT_EQ(Advance({}, 2, heavy, ON_TIME, REFRESH144).depth, 1u);
	EXPECT_EQ(Advance({}, 3, heavy, ON_TIME, REFRESH144).depth, 2u);
}

TEST(PresentQueueTest, MissedRefreshesQueueTwo) {
	const f64 missed = REFRESH144 * 2.0;
	EXPECT_EQ(Advance({}, 2, 0.001, missed, REFRESH144).depth, 1u);
	EXPECT_EQ(Advance({}, 3, 0.001, missed, REFRESH144).depth, 2u);
}

TEST(PresentQueueTest, SingleHitchKeepsOne) {
	RHI::PresentQueue queue = Advance({}, 1, 0.050, 0.060, REFRESH144);
	EXPECT_EQ(queue.depth, 1u);
	EXPECT_GT(queue.late, 0u);
	queue = Advance(queue, 2, 0.001, REFRESH144, REFRESH144);
	EXPECT_EQ(queue.depth, 1u);
	EXPECT_EQ(queue.late, 0u);
}

TEST(PresentQueueTest, SpacedHitchesKeepOne) {
	RHI::PresentQueue queue;
	for (u32 round = 0; round < 50; ++round) {
		queue = Advance(queue, 1, 0.050, 0.060, REFRESH144);
		queue = Advance(queue, 4, 0.001, REFRESH144, REFRESH144);
	}
	EXPECT_EQ(queue.depth, 1u);
}

TEST(PresentQueueTest, ReturnsToOneAfterCalmRun) {
	const RHI::PresentQueue queued{2, 0, 0};
	EXPECT_EQ(Advance(queued, RHI::PRESENT_EARLY_RUN - 1, 0.001, ON_TIME, REFRESH144).depth, 2u);
	const RHI::PresentQueue calm = Advance(queued, RHI::PRESENT_EARLY_RUN, 0.001, ON_TIME, REFRESH144);
	EXPECT_EQ(calm.depth, 1u);
	EXPECT_EQ(calm.late, 0u);
}

TEST(PresentQueueTest, SlowFrameRestartsCalmRun) {
	RHI::PresentQueue queue = Advance({2, 0, 0}, RHI::PRESENT_EARLY_RUN - 1, 0.001, ON_TIME, REFRESH144);
	queue = Advance(queue, 1, REFRESH144, ON_TIME, REFRESH144);
	EXPECT_EQ(queue.early, 0u);
	EXPECT_EQ(Advance(queue, RHI::PRESENT_EARLY_RUN - 1, 0.001, ON_TIME, REFRESH144).depth, 2u);
	EXPECT_EQ(Advance(queue, RHI::PRESENT_EARLY_RUN, 0.001, ON_TIME, REFRESH144).depth, 1u);
}

TEST(PresentQueueTest, UnknownWorkNeverReturns) {
	EXPECT_EQ(Advance({2, 0, 0}, 10000, RHI::PRESENT_WORK_UNKNOWN, ON_TIME, REFRESH144).depth, 2u);
	EXPECT_EQ(Advance({}, 10000, RHI::PRESENT_WORK_UNKNOWN, REFRESH144, REFRESH144).depth, 1u);
}

TEST(PresentQueueTest, WorkBetweenSlacksHoldsDepth) {
	const f64 between = REFRESH60 - (RHI::PRESENT_LATE_SLACK_SECONDS + RHI::PRESENT_EARLY_SLACK_SECONDS) * 0.5;
	EXPECT_EQ(Advance({}, 10000, between, REFRESH60, REFRESH60).depth, 1u);
	EXPECT_EQ(Advance({2, 0, 0}, 10000, between, ON_TIME, REFRESH60).depth, 2u);
}

TEST(PresentQueueTest, SlacksScaleWithRefresh) {
	const f64 work = 0.008;
	EXPECT_EQ(Advance({}, 100, work, REFRESH60, REFRESH60).depth, 1u);
	EXPECT_EQ(Advance({}, 100, work, REFRESH144, REFRESH144).depth, 2u);
	EXPECT_EQ(Advance({2, 0, 0}, RHI::PRESENT_EARLY_RUN, work, ON_TIME, REFRESH60).depth, 1u);
	EXPECT_EQ(Advance({2, 0, 0}, RHI::PRESENT_EARLY_RUN, work, ON_TIME, REFRESH144).depth, 2u);
}

TEST(PresentQueueTest, RefreshShorterThanSlackStaysQueued) {
	const f64 refresh = 1.0 / 360.0;
	ASSERT_LT(refresh, RHI::PRESENT_LATE_SLACK_SECONDS);
	EXPECT_EQ(Advance({}, 3, 0.0, refresh, refresh).depth, 2u);
	EXPECT_EQ(Advance({2, 0, 0}, 10000, 0.0, ON_TIME, refresh).depth, 2u);
}

class PresentDefaultTest : public ::testing::Test {
protected:
	void SetUp() override { WindowSystem::Initialize(); }
	void TearDown() override { WindowSystem::Terminate(); }
};

TEST_F(PresentDefaultTest, AWindowIsBornAskingForVsync) {
	WindowDesc desc;
	desc.title = "PresentDefaultTest";
	desc.clientSize = {320, 240};
	desc.flags = WindowFlags::Hidden;
	Handle<Window> window = WindowSystem::Create(desc);
	ASSERT_TRUE(window.IsValid());
	EXPECT_TRUE(window.Get()->GetVSyncHint());

	WindowSystem::RequestDestroy(window);
	WindowSystem::PumpEvents();
}

namespace {

	std::filesystem::path PresentRepoDir(const char* relative) {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
			std::filesystem::path candidate = probe / relative;
			if (std::filesystem::exists(candidate, ec)) {
				return candidate;
			}
			std::filesystem::path parent = probe.parent_path();
			if (parent == probe) {
				break;
			}
			probe = parent;
		}
		return {};
	}

	class PresentSyncFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		bool _engineMounted = false;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_present_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			std::filesystem::path engineAssets = PresentRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_engineMounted = true;
			}
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			Settings::Initialize(_paths);
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			UISystem::Initialize();
			SceneSystem::Initialize();
		}

		void TearDown() override {
			SceneSystem::Terminate();
			UISystem::Terminate();
			Settings::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_engineMounted) {
				FileSystem::Unmount("engine");
			}
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		static Handle<Window> CreatePlainWindow() {
			WindowDesc desc;
			desc.title = "PresentSyncTest";
			desc.clientSize = {64, 64};
			desc.flags = WindowFlags::Hidden;
			return WindowSystem::Create(desc);
		}

		static void AddCamera() {
			Handle<Scene> scene = SceneSystem::CreateScene("Present");
			SceneSystem::CreateObject("Camera", scene).Get()->AddComponent<Camera>();
		}

		static GraphicsSettings& Graphics() {
			GraphicsSettings* graphics = Settings::DeclareCategory<GraphicsSettings>("Graphics", SettingScope::Project);
			SLT_CORE_ASSERT(graphics != nullptr);
			return *graphics;
		}

		static GraphicsInternal::PresentCounts RenderFrame() {
			WindowSystem::PumpEvents();
			SceneSystem::UpdateAll();
			SceneSystem::EndFrame();
			RenderSystem::RenderAll();
			return GraphicsInternal::GetLastFramePresents();
		}
	};

} // namespace

TEST_F(PresentSyncFixture, VSyncHintAppliesNextFrame) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Handle<Window> window = CreatePlainWindow();
	ASSERT_TRUE(window.IsValid());
	AddCamera();

	GraphicsInternal::PresentCounts presents = RenderFrame();
	ASSERT_EQ(presents.windows, 1u);
	EXPECT_EQ(presents.vsyncWindows, 1u);

	window->SetVSyncHint(false);
	presents = RenderFrame();
	EXPECT_EQ(presents.windows, 1u);
	EXPECT_EQ(presents.vsyncWindows, 0u);

	window->SetVSyncHint(true);
	presents = RenderFrame();
	EXPECT_EQ(presents.windows, 1u);
	EXPECT_EQ(presents.vsyncWindows, 1u);
}

TEST_F(PresentSyncFixture, ProjectVSyncSkipsWindowsWithoutGame) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Handle<Window> window = CreatePlainWindow();
	ASSERT_TRUE(window.IsValid());
	AddCamera();

	Graphics().vsync = VSyncMode::Off;
	GraphicsInternal::PresentCounts presents = RenderFrame();
	ASSERT_EQ(presents.windows, 1u);
	EXPECT_EQ(presents.vsyncWindows, 1u);

	window->SetVSyncHint(false);
	Graphics().vsync = VSyncMode::On;
	presents = RenderFrame();
	EXPECT_EQ(presents.windows, 1u);
	EXPECT_EQ(presents.vsyncWindows, 0u);
}

TEST_F(PresentSyncFixture, ProjectVSyncDrivesGameWindow) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Handle<Window> game = ScreenInternal::CreateGameWindow({}, WindowFlags::Hidden);
	ASSERT_TRUE(game.IsValid());
	AddCamera();

	GraphicsInternal::PresentCounts presents = RenderFrame();
	ASSERT_EQ(presents.windows, 1u);
	EXPECT_EQ(presents.vsyncWindows, 1u);

	Graphics().vsync = VSyncMode::Off;
	presents = RenderFrame();
	EXPECT_EQ(presents.windows, 1u);
	EXPECT_EQ(presents.vsyncWindows, 0u);

	game->SetVSyncHint(false);
	Graphics().vsync = VSyncMode::On;
	presents = RenderFrame();
	EXPECT_EQ(presents.windows, 1u);
	EXPECT_EQ(presents.vsyncWindows, 1u);
}

TEST_F(PresentSyncFixture, VSyncFramesHoldDisplayCadence) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Handle<Window> window = CreatePlainWindow();
	ASSERT_TRUE(window.IsValid());
	AddCamera();

	for (u32 frame = 0; frame < 8; ++frame) {
		RenderFrame();
	}
	if (RenderFrame().displayWaitable == 0) {
		GTEST_SKIP() << "driver has no present wait";
	}

	constexpr u32 FRAMES = 24;
	const f32 hz = window->GetMonitor().refreshRate;
	const f64 refresh = 1.0 / static_cast<f64>(hz > 0.0f ? hz : 60.0f);
	const u64 start = Time::GetTicks();
	u32 synced = 0;
	for (u32 frame = 0; frame < FRAMES; ++frame) {
		const GraphicsInternal::PresentCounts presents = RenderFrame();
		EXPECT_EQ(presents.vsyncWindows, 1u);
		EXPECT_EQ(presents.displayWaitable, 1u);
		synced += presents.displaySynced;
	}
	const f64 elapsed = static_cast<f64>(Time::GetTicks() - start) / static_cast<f64>(Time::GetTicksPerSecond());
	EXPECT_EQ(synced, FRAMES);
	EXPECT_GE(elapsed, refresh * (FRAMES - 2));
	EXPECT_LT(elapsed, refresh * FRAMES * 3.0);
}

TEST_F(PresentSyncFixture, TwoWindowsShareOneRefresh) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	Handle<Window> first = CreatePlainWindow();
	Handle<Window> second = CreatePlainWindow();
	ASSERT_TRUE(first.IsValid());
	ASSERT_TRUE(second.IsValid());
	Handle<Scene> scene = SceneSystem::CreateScene("Present");
	SceneSystem::CreateObject("First", scene).Get()->AddComponent<Camera>()->SetTargetWindow(first);
	SceneSystem::CreateObject("Second", scene).Get()->AddComponent<Camera>()->SetTargetWindow(second);

	for (u32 frame = 0; frame < 8; ++frame) {
		RenderFrame();
	}
	const GraphicsInternal::PresentCounts warm = RenderFrame();
	ASSERT_EQ(warm.windows, 2u);
	ASSERT_EQ(warm.vsyncWindows, 2u);
	if (warm.displayWaitable == 0) {
		GTEST_SKIP() << "driver has no present wait";
	}

	constexpr u32 FRAMES = 24;
	const f32 hz = first->GetMonitor().refreshRate;
	const f64 refresh = 1.0 / static_cast<f64>(hz > 0.0f ? hz : 60.0f);
	const u64 start = Time::GetTicks();
	u32 synced = 0;
	for (u32 frame = 0; frame < FRAMES; ++frame) {
		const GraphicsInternal::PresentCounts presents = RenderFrame();
		EXPECT_EQ(presents.displayWaitable, 2u);
		synced += presents.displaySynced;
	}
	const f64 elapsed = static_cast<f64>(Time::GetTicks() - start) / static_cast<f64>(Time::GetTicksPerSecond());
	EXPECT_EQ(synced, FRAMES * 2);
	EXPECT_GE(elapsed, refresh * (FRAMES - 2));
	EXPECT_LT(elapsed, refresh * FRAMES * 1.5);
}
