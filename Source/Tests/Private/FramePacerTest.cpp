#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Time.h"
#include "Shalltear/Core/Window.h"

#include "Graphics/PresentPolicy.h"
#include "ScreenSettings.h"
#include "Window/FramePacing.h"

#include "TestPlatform.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>

using namespace Shalltear;
using FramePacingInternal::PacingInputs;
using FramePacingInternal::ResolvePacing;

TEST(FramePacingTest, UncappedVisibleFrameRunsAtOnce) {
	PacingInputs inputs;
	inputs.windowsPresented = 1;
	const FramePacingInternal::Pacing pacing = ResolvePacing(inputs);
	EXPECT_FALSE(pacing.paused);
	EXPECT_DOUBLE_EQ(pacing.intervalSeconds, 0.0);
}

TEST(FramePacingTest, TargetSetsInterval) {
	PacingInputs inputs;
	inputs.windowsPresented = 1;
	inputs.targetFrameRate = 50;
	EXPECT_DOUBLE_EQ(ResolvePacing(inputs).intervalSeconds, 0.02);
}

TEST(FramePacingTest, VSyncPresentIgnoresTarget) {
	PacingInputs inputs;
	inputs.windowsPresented = 2;
	inputs.vsyncWindowsPresented = 1;
	inputs.targetFrameRate = 30;
	EXPECT_DOUBLE_EQ(ResolvePacing(inputs).intervalSeconds, 0.0);
}

TEST(FramePacingTest, NothingPresentedWaitsRefresh) {
	PacingInputs inputs;
	inputs.refreshRate = 144.0f;
	EXPECT_DOUBLE_EQ(ResolvePacing(inputs).intervalSeconds, 1.0 / 144.0);
	inputs.targetFrameRate = 30;
	EXPECT_DOUBLE_EQ(ResolvePacing(inputs).intervalSeconds, 1.0 / 30.0);
	inputs.targetFrameRate = 500;
	EXPECT_DOUBLE_EQ(ResolvePacing(inputs).intervalSeconds, 1.0 / 144.0);
}

TEST(FramePacingTest, UnknownRefreshFallsBack) {
	PacingInputs inputs;
	EXPECT_DOUBLE_EQ(ResolvePacing(inputs).intervalSeconds, 1.0 / 60.0);
}

TEST(FramePacingTest, UnfocusedPausesUnlessRunInBackground) {
	PacingInputs inputs;
	inputs.windowsPresented = 1;
	inputs.focused = false;
	inputs.runInBackground = false;
	EXPECT_TRUE(ResolvePacing(inputs).paused);
	inputs.runInBackground = true;
	EXPECT_FALSE(ResolvePacing(inputs).paused);
	inputs.focused = true;
	inputs.runInBackground = false;
	EXPECT_FALSE(ResolvePacing(inputs).paused);
}

namespace {

	std::filesystem::path RepoDir(const char* relative) {
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

	f64 Seconds(u64 ticks) {
		return static_cast<f64>(ticks) / static_cast<f64>(Time::GetTicksPerSecond());
	}

	class FramePacerFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		bool _engineMounted = false;
		Handle<Window> _window;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_pacer_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			std::filesystem::path engineAssets = RepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_engineMounted = true;
			}
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			Settings::Initialize(_paths);
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			SceneSystem::Initialize();

			_window = WindowSystem::Create(HiddenDesc());
		}

		void TearDown() override {
			Time::SetTargetFrameRate(-1);
			SceneSystem::Terminate();
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

		static WindowDesc HiddenDesc() {
			WindowDesc desc;
			desc.title = "FramePacerTest";
			desc.clientSize = {64, 64};
			desc.flags = WindowFlags::Hidden;
			return desc;
		}

		static PlayerSettings& Player() {
			PlayerSettings* project = Settings::DeclareCategory<PlayerSettings>("Player", SettingScope::Project);
			SLT_CORE_ASSERT(project != nullptr);
			return *project;
		}

		static void RenderFrame() {
			SceneSystem::UpdateAll();
			SceneSystem::EndFrame();
			RenderSystem::RenderAll();
		}

		static f64 TimeNextFrame(FramePacer& pacer) {
			const u64 start = Time::GetTicks();
			EXPECT_TRUE(pacer.BeginFrame());
			return Seconds(Time::GetTicks() - start);
		}
	};

} // namespace

TEST_F(FramePacerFixture, UnfocusedGamePausesUntilFocused) {
	ASSERT_TRUE(_window.IsValid());
	ASSERT_FALSE(WindowSystem::GetFocusedWindow().IsValid());
	EXPECT_FALSE(Player().runInBackground);
	FramePacer pacer;
	EXPECT_FALSE(pacer.BeginFrame());
	EXPECT_FALSE(pacer.BeginFrame());
	std::this_thread::sleep_for(std::chrono::milliseconds(300));

	TestPlatform::PostFocus(_window, true);
	EXPECT_FALSE(pacer.BeginFrame());
	ASSERT_EQ(WindowSystem::GetFocusedWindow(), _window);
	EXPECT_TRUE(pacer.BeginFrame());
	EXPECT_LT(pacer.GetUnscaledDeltaTime(), 0.1f);

	TestPlatform::PostFocus(_window, false);
	EXPECT_TRUE(pacer.BeginFrame());
	EXPECT_FALSE(pacer.BeginFrame());

	Handle<Window> second = WindowSystem::Create(HiddenDesc());
	ASSERT_TRUE(second.IsValid());
	TestPlatform::PostFocus(second, true);
	EXPECT_FALSE(pacer.BeginFrame());
	EXPECT_TRUE(pacer.BeginFrame());
}

TEST_F(FramePacerFixture, RunInBackgroundNeverPauses) {
	ASSERT_TRUE(_window.IsValid());
	Player().runInBackground = true;
	FramePacer pacer;
	for (int frame = 0; frame < 3; ++frame) {
		EXPECT_TRUE(pacer.BeginFrame());
	}
	EXPECT_FALSE(WindowSystem::GetFocusedWindow().IsValid());
}

TEST_F(FramePacerFixture, UnattendedRunNeverPauses) {
	ASSERT_TRUE(_window.IsValid());
	FramePacer pacer(true);
	for (int frame = 0; frame < 3; ++frame) {
		EXPECT_TRUE(pacer.BeginFrame());
	}
	EXPECT_FALSE(Player().runInBackground);
	EXPECT_FALSE(WindowSystem::GetFocusedWindow().IsValid());
}

TEST_F(FramePacerFixture, CapHoldsTargetRate) {
	ASSERT_TRUE(_window.IsValid());
	Time::SetTargetFrameRate(40);
	FramePacer pacer(true);
	ASSERT_TRUE(pacer.BeginFrame());
	const u64 start = Time::GetTicks();
	for (int frame = 0; frame < 8; ++frame) {
		ASSERT_TRUE(pacer.BeginFrame());
	}
	const f64 elapsed = Seconds(Time::GetTicks() - start);
	EXPECT_GE(elapsed, 8.0 / 40.0 - 0.002);
	EXPECT_LT(elapsed, 8.0 / 40.0 * 1.5);
}

TEST_F(FramePacerFixture, NothingPresentedPacesAtRefresh) {
	ASSERT_TRUE(_window.IsValid());
	EXPECT_EQ(GraphicsInternal::GetLastFramePresents().windows, 0u);
	const f32 monitorHz = _window->GetMonitor().refreshRate;
	const f64 interval = 1.0 / static_cast<f64>(monitorHz > 0.0f ? monitorHz : FramePacingInternal::FALLBACK_REFRESH_RATE);
	FramePacer pacer(true);
	ASSERT_TRUE(pacer.BeginFrame());
	const u64 start = Time::GetTicks();
	for (int frame = 0; frame < 4; ++frame) {
		ASSERT_TRUE(pacer.BeginFrame());
	}
	const f64 elapsed = Seconds(Time::GetTicks() - start);
	EXPECT_GE(elapsed, 4.0 * interval - 0.002);
	EXPECT_LT(elapsed, 4.0 * interval * 1.5 + 0.02);
}

TEST_F(FramePacerFixture, PacesAtFallbackWithoutWindow) {
	WindowSystem::RequestDestroy(_window);
	WindowSystem::PumpEvents();
	ASSERT_EQ(WindowSystem::GetWindowCount(), 0u);
	FramePacer pacer(true);
	ASSERT_TRUE(pacer.BeginFrame());
	EXPECT_GE(TimeNextFrame(pacer), 1.0 / static_cast<f64>(FramePacingInternal::FALLBACK_REFRESH_RATE) - 0.002);
}

TEST_F(FramePacerFixture, VSyncPresentLiftsCap) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_window.IsValid());
	ASSERT_TRUE(_window->GetVSyncHint());
	Handle<Scene> scene = SceneSystem::CreateScene("Pacer");
	SceneSystem::CreateObject("Camera", scene).Get()->AddComponent<Camera>();

	RenderFrame();
	const GraphicsInternal::PresentCounts presents = GraphicsInternal::GetLastFramePresents();
	EXPECT_EQ(presents.windows, 1u);
	EXPECT_EQ(presents.vsyncWindows, 1u);

	Time::SetTargetFrameRate(1);
	FramePacer pacer(true);
	ASSERT_TRUE(pacer.BeginFrame());
	RenderFrame();
	EXPECT_LT(TimeNextFrame(pacer), 0.25);
}

TEST_F(FramePacerFixture, VSyncOffKeepsCap) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	ASSERT_TRUE(_window.IsValid());
	_window->SetVSyncHint(false);
	Handle<Scene> scene = SceneSystem::CreateScene("Pacer");
	SceneSystem::CreateObject("Camera", scene).Get()->AddComponent<Camera>();

	RenderFrame();
	const GraphicsInternal::PresentCounts presents = GraphicsInternal::GetLastFramePresents();
	EXPECT_EQ(presents.windows, 1u);
	EXPECT_EQ(presents.vsyncWindows, 0u);

	Time::SetTargetFrameRate(4);
	FramePacer pacer(true);
	const u64 start = Time::GetTicks();
	ASSERT_TRUE(pacer.BeginFrame());
	RenderFrame();
	ASSERT_TRUE(pacer.BeginFrame());
	EXPECT_GE(Seconds(Time::GetTicks() - start), 0.25 - 0.002);
}
