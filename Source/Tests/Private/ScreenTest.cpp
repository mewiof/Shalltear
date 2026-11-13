#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Screen.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/UI.h"
#include "Shalltear/Core/Window.h"

#include "ScreenInternal.h"
#include "ScreenSettings.h"
#include "Window/WindowInternal.h"

#include "TestPlatform.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>
#include <vector>

using namespace Shalltear;

namespace {

	PlayerSettings Project(FullScreenMode mode, bool native, Vec2I size) {
		PlayerSettings project;
		project.fullScreenMode = mode;
		project.defaultIsNativeResolution = native;
		project.defaultScreenWidth = size.x;
		project.defaultScreenHeight = size.y;
		return project;
	}

	ScreenSettings SavedAgainst(const PlayerSettings& project, FullScreenMode mode, Vec2I size) {
		ScreenSettings saved;
		saved.saved = true;
		saved.fullScreenMode = mode;
		saved.nativeResolution = false;
		saved.width = size.x;
		saved.height = size.y;
		saved.projectDefaults = ScreenInternal::DescribeProjectDefaults(project);
		return saved;
	}

	MonitorInfo Monitor(RectI bounds, bool primary) {
		MonitorInfo monitor;
		monitor.id = static_cast<u64>(bounds.position.x + 1);
		monitor.bounds = bounds;
		monitor.workArea = RectI(bounds.position, bounds.size - Vec2I(0, 40));
		monitor.primary = primary;
		return monitor;
	}

} // namespace

TEST(ScreenStartupTest, DefaultsComeFromProject) {
	const PlayerSettings project = Project(FullScreenMode::Windowed, true, {1024, 768});
	const ScreenInternal::ScreenChoice choice = ScreenInternal::ResolveStartup(project, ScreenSettings{}, ScreenLaunch{});
	EXPECT_EQ(choice.mode, FullScreenMode::Windowed);
	EXPECT_EQ(choice.resolution, Vec2I(1024, 768));
}

TEST(ScreenStartupTest, SavedChoiceStandsUntilProjectsSettingsChange) {
	const PlayerSettings project = Project(FullScreenMode::FullScreenWindow, true, {1024, 768});
	const ScreenSettings saved = SavedAgainst(project, FullScreenMode::Windowed, {1600, 900});
	ScreenInternal::ScreenChoice choice = ScreenInternal::ResolveStartup(project, saved, ScreenLaunch{});
	EXPECT_EQ(choice.mode, FullScreenMode::Windowed);
	EXPECT_EQ(choice.resolution, Vec2I(1600, 900));

	const PlayerSettings changed = Project(FullScreenMode::FullScreenWindow, true, {1280, 720});
	choice = ScreenInternal::ResolveStartup(changed, saved, ScreenLaunch{});
	EXPECT_EQ(choice.mode, FullScreenMode::FullScreenWindow);
	EXPECT_EQ(choice.resolution, Vec2I(1280, 720));
	EXPECT_TRUE(choice.nativeResolution);
}

TEST(ScreenStartupTest, TheCommandLineOutranksEverySetting) {
	const PlayerSettings project = Project(FullScreenMode::MaximizedWindow, true, {1024, 768});
	const ScreenSettings saved = SavedAgainst(project, FullScreenMode::Windowed, {1600, 900});

	ScreenLaunch launch;
	launch.width = 800;
	launch.height = 600;
	launch.fullScreen = true;
	ScreenInternal::ScreenChoice choice = ScreenInternal::ResolveStartup(project, saved, launch);
	EXPECT_EQ(choice.mode, FullScreenMode::MaximizedWindow);
	EXPECT_EQ(choice.resolution, Vec2I(800, 600));
	EXPECT_FALSE(choice.nativeResolution);

	const PlayerSettings windowedProject = Project(FullScreenMode::Windowed, true, {1024, 768});
	choice = ScreenInternal::ResolveStartup(windowedProject, ScreenSettings{}, launch);
	EXPECT_EQ(choice.mode, FullScreenMode::FullScreenWindow);

	ScreenLaunch windowed;
	windowed.fullScreen = false;
	choice = ScreenInternal::ResolveStartup(project, ScreenSettings{}, windowed);
	EXPECT_EQ(choice.mode, FullScreenMode::Windowed);
}

TEST(ScreenStartupTest, MonitorPriorityOrder) {
	const MonitorInfo monitors[] = {Monitor(RectI(0, 0, 1920, 1080), true), Monitor(RectI(1920, 0, 2560, 1440), false)};
	ScreenSettings saved;
	EXPECT_EQ(ScreenInternal::SelectMonitor(monitors, saved, ScreenLaunch{}), 0u);

	saved.hasPosition = true;
	saved.positionX = 2400;
	saved.positionY = 300;
	EXPECT_EQ(ScreenInternal::SelectMonitor(monitors, saved, ScreenLaunch{}), 1u);

	ScreenLaunch launch;
	launch.monitor = 1;
	EXPECT_EQ(ScreenInternal::SelectMonitor(monitors, saved, launch), 0u);
	launch.monitor = 7;
	EXPECT_EQ(ScreenInternal::SelectMonitor(monitors, saved, launch), 1u);
}

TEST(ScreenStartupTest, SavedPositionMustBeOnMonitor) {
	const MonitorInfo monitors[] = {Monitor(RectI(0, 0, 1920, 1080), true)};
	EXPECT_TRUE(ScreenInternal::IsReachable({200, 100}, {1040, 800}, monitors));
	EXPECT_FALSE(ScreenInternal::IsReachable({2400, 300}, {1040, 800}, monitors));
	EXPECT_FALSE(ScreenInternal::IsReachable({200, 1060}, {1040, 800}, monitors));
}

namespace {

	std::filesystem::path FindRepoDir(const char* relative) {
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

	class ScreenFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		bool _mounted = false;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_screen_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			std::filesystem::path engineAssets = FindRepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mounted = true;
			}
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.userProject = Path((_root / "Player" / "Settings.ini").generic_string());
			Settings::Initialize(_paths);
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			UISystem::Initialize();
		}

		void TearDown() override {
			UISystem::Terminate();
			Settings::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mounted) {
				FileSystem::Unmount("engine");
			}
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		static PlayerSettings& Player() {
			PlayerSettings* project = Settings::DeclareCategory<PlayerSettings>("Player", SettingScope::Project);
			SLT_CORE_ASSERT(project != nullptr);
			return *project;
		}

		static Handle<Window> CreateHidden(const ScreenLaunch& launch = {}) {
			return ScreenInternal::CreateGameWindow(launch, WindowFlags::Hidden);
		}

		static void EndFrame() {
			WindowSystem::PumpEvents();
			UISystem::Update();
			FrameStages::Run(FrameStage::EndFrame, FrameStageArgs{});
		}

		static void Close(Handle<Window> window) {
			WindowSystem::RequestDestroy(window);
			WindowSystem::PumpEvents();
		}

		str PlayerDocument() const { return FileSystem::ReadTextFile(_paths.userProject); }
	};

} // namespace

TEST_F(ScreenFixture, WindowedLaunchOpensCenteredAtProjectsSize) {
	Player().fullScreenMode = FullScreenMode::Windowed;
	Player().defaultScreenWidth = 800;
	Player().defaultScreenHeight = 600;
	Handle<Window> window = CreateHidden();
	ASSERT_TRUE(window.IsValid());
	EXPECT_EQ(window->GetClientSize(), Vec2I(800, 600));
	EXPECT_EQ(window->GetPlacement().state, WindowState::Normal);
	const RectI work = window->GetMonitor().workArea;
	EXPECT_EQ(window->GetPosition(), ScreenInternal::CenterIn(work, window->GetRect().size));
	EXPECT_EQ(Screen::GetWidth(), 800);
	EXPECT_EQ(Screen::GetHeight(), 600);
	EXPECT_EQ(Screen::GetFullScreenMode(), FullScreenMode::Windowed);
	EXPECT_FALSE(Screen::IsFullScreen());
	Close(window);
}

TEST_F(ScreenFixture, DefaultLaunchCoversMonitorAtSize) {
	Handle<Window> window = CreateHidden();
	ASSERT_TRUE(window.IsValid());
	const RectI monitor = window->GetMonitor().bounds;
	EXPECT_EQ(window->GetPlacement().state, WindowState::Fullscreen);
	EXPECT_EQ(window->GetClientSize(), monitor.size);
	EXPECT_EQ(WindowInternal::GetRenderSizeOverride(*window.Get()), Vec2I());
	EXPECT_EQ(Screen::GetWidth(), monitor.size.x);
	EXPECT_TRUE(Screen::IsFullScreen());
	Close(window);
}

TEST_F(ScreenFixture, ResolutionAppliesAtEndFrame) {
	Handle<Window> window = CreateHidden();
	ASSERT_TRUE(window.IsValid());
	const Vec2I monitor = window->GetMonitor().bounds.size;

	Screen::SetResolution(1280, 720, FullScreenMode::FullScreenWindow);
	EXPECT_EQ(Screen::GetWidth(), monitor.x);
	EndFrame();
	EXPECT_EQ(WindowInternal::GetRenderSizeOverride(*window.Get()), Vec2I(1280, 720));
	EXPECT_EQ(Screen::GetWidth(), 1280);
	EXPECT_EQ(Screen::GetHeight(), 720);
	EXPECT_EQ(window->GetClientSize(), monitor);

	const str document = PlayerDocument();
	EXPECT_NE(document.find("[Screen]"), str::npos) << document;
	EXPECT_NE(document.find("1280"), str::npos) << document;
	EXPECT_EQ(FileSystem::ReadTextFile(_paths.project).find("1280"), str::npos);

	Screen::SetResolution(640, 480, FullScreenMode::Windowed);
	EndFrame();
	EXPECT_EQ(window->GetPlacement().state, WindowState::Normal);
	EXPECT_EQ(window->GetClientSize(), Vec2I(640, 480));
	EXPECT_EQ(WindowInternal::GetRenderSizeOverride(*window.Get()), Vec2I());
	EXPECT_EQ(Screen::GetWidth(), 640);
	Close(window);
}

TEST_F(ScreenFixture, RelaunchRestoresChoiceUntilProjectChanges) {
	Player().fullScreenMode = FullScreenMode::FullScreenWindow;
	Handle<Window> window = CreateHidden();
	ASSERT_TRUE(window.IsValid());
	Screen::SetResolution(700, 500, FullScreenMode::Windowed);
	EndFrame();
	window->SetPosition({150, 120});
	WindowSystem::PumpEvents();
	Close(window);

	Settings::Terminate();
	Settings::Initialize(_paths);
	window = CreateHidden();
	ASSERT_TRUE(window.IsValid());
	EXPECT_EQ(window->GetPlacement().state, WindowState::Normal);
	EXPECT_EQ(window->GetClientSize(), Vec2I(700, 500));
	EXPECT_EQ(window->GetPosition(), Vec2I(150, 120));
	Close(window);

	Player().defaultScreenWidth = 1100;
	window = CreateHidden();
	ASSERT_TRUE(window.IsValid());
	EXPECT_EQ(window->GetPlacement().state, WindowState::Fullscreen);
	Close(window);
}

TEST_F(ScreenFixture, AltEnterTogglesFullscreen) {
	Handle<Window> window = CreateHidden();
	ASSERT_TRUE(window.IsValid());
	auto altEnter = [&]() {
		TestPlatform::PostKey(window, Key::LeftAlt, true);
		TestPlatform::PostKey(window, Key::Enter, true);
		EndFrame();
		TestPlatform::PostKey(window, Key::Enter, false);
		TestPlatform::PostKey(window, Key::LeftAlt, false);
		EndFrame();
	};

	ASSERT_TRUE(Screen::IsFullScreen());
	altEnter();
	EXPECT_FALSE(Screen::IsFullScreen());
	EXPECT_EQ(window->GetPlacement().state, WindowState::Normal);
	altEnter();
	EXPECT_TRUE(Screen::IsFullScreen());
	EXPECT_EQ(window->GetPlacement().state, WindowState::Fullscreen);

	Player().allowFullscreenSwitch = false;
	altEnter();
	EXPECT_TRUE(Screen::IsFullScreen());
	Close(window);
}

TEST_F(ScreenFixture, TextureScreenUsesViewSize) {
	Handle<Window> window = CreateHidden();
	ASSERT_TRUE(window.IsValid());
	UIScreenDesc texture;
	texture.target = UIScreenDesc::Target::Texture;
	texture.sizePx = {640, 360};
	UISystem::SetScreen(texture);
	EXPECT_EQ(Screen::GetWidth(), 640);
	EXPECT_EQ(Screen::GetHeight(), 360);

	const Vec2I before = window->GetClientSize();
	Screen::SetResolution(320, 200, FullScreenMode::Windowed);
	EndFrame();
	EXPECT_EQ(window->GetClientSize(), before);
	EXPECT_EQ(window->GetPlacement().state, WindowState::Fullscreen);
	Close(window);
}

TEST_F(ScreenFixture, ResolutionsIncludeMonitorsOnceEach) {
	if (WindowSystem::GetMonitors().empty()) {
		GTEST_SKIP() << "no monitors visible";
	}
	const ScreenResolution current = Screen::GetCurrentResolution();
	ASSERT_GT(current.width, 0);
	Vector<ScreenResolution> resolutions{GetMemoryResource(MemTag::Debug)};
	Screen::GetResolutions(resolutions);
	ASSERT_FALSE(resolutions.empty());
	EXPECT_TRUE(std::any_of(resolutions.begin(), resolutions.end(), [&](const ScreenResolution& r) {
		return r.width == current.width && r.height == current.height;
	}));
	for (size_t i = 1; i < resolutions.size(); ++i) {
		const ScreenResolution& a = resolutions[i - 1];
		const ScreenResolution& b = resolutions[i];
		EXPECT_FALSE(a.width == b.width && a.height == b.height && a.refreshRate == b.refreshRate);
		EXPECT_GE(a.width, b.width);
	}
}

namespace {

	struct ModeCalls {
		std::vector<Vec2I> sets;
		u32 restores = 0;
		bool refuse = false;
	};
	ModeCalls* _modeCalls = nullptr;

	bool RecordSet(u64, Vec2I size, f32) {
		_modeCalls->sets.push_back(size);
		return !_modeCalls->refuse;
	}

	void RecordRestore(u64) {
		++_modeCalls->restores;
	}

	struct StandInModeSwitch {
		ModeCalls calls;
		StandInModeSwitch() {
			_modeCalls = &calls;
			ScreenInternal::SetDisplayModeSwitch({&RecordSet, &RecordRestore});
		}
		~StandInModeSwitch() {
			ScreenInternal::SetDisplayModeSwitch({});
			_modeCalls = nullptr;
		}
	};

	void Focus(Handle<Window> window, bool focused) {
		WindowEvents::Focus.Publish(EventBus::GetGlobal(), WindowFocusEvent{window, focused});
	}

} // namespace

TEST_F(ScreenFixture, ExclusiveSwitchesModeWhileFocused) {
	if (!TestPlatform::ChangesDisplayModes()) {
		GTEST_SKIP() << "this host can't change display modes";
	}
	StandInModeSwitch modes;
	Player().fullScreenMode = FullScreenMode::ExclusiveFullScreen;
	Player().defaultIsNativeResolution = false;
	Player().defaultScreenWidth = 1280;
	Player().defaultScreenHeight = 720;
	Handle<Window> window = CreateHidden();
	ASSERT_TRUE(window.IsValid());
	EXPECT_TRUE(modes.calls.sets.empty());
	EXPECT_EQ(window->GetPlacement().state, WindowState::Fullscreen);
	EXPECT_EQ(WindowInternal::GetRenderSizeOverride(*window.Get()), Vec2I(1280, 720));

	Focus(window, true);
	ASSERT_EQ(modes.calls.sets.size(), 1u);
	EXPECT_EQ(modes.calls.sets[0], Vec2I(1280, 720));
	EXPECT_EQ(WindowInternal::GetRenderSizeOverride(*window.Get()), Vec2I{});
	EXPECT_EQ(window->GetPlacement().state, WindowState::Fullscreen);
	EXPECT_TRUE(Screen::IsFullScreen());

	Focus(window, false);
	EXPECT_EQ(modes.calls.restores, 1u);
	EXPECT_EQ(window->GetPlacement().state, WindowState::Minimized);
	Focus(window, true);
	ASSERT_EQ(modes.calls.sets.size(), 2u);
	EXPECT_EQ(window->GetPlacement().state, WindowState::Fullscreen);

	Screen::SetFullScreenMode(FullScreenMode::Windowed);
	EndFrame();
	EXPECT_EQ(modes.calls.restores, 2u);
	Focus(window, false);
	Focus(window, true);
	EXPECT_EQ(modes.calls.restores, 2u);
	EXPECT_EQ(modes.calls.sets.size(), 2u);
	Close(window);
	EXPECT_EQ(modes.calls.restores, 2u);
}

TEST_F(ScreenFixture, ExclusiveRestoresDisplayOnClose) {
	StandInModeSwitch modes;
	Player().fullScreenMode = FullScreenMode::ExclusiveFullScreen;
	Handle<Window> window = CreateHidden();
	ASSERT_TRUE(window.IsValid());
	Focus(window, true);
	ASSERT_EQ(modes.calls.sets.size(), 1u);
	EXPECT_EQ(modes.calls.sets[0], window->GetMonitor().bounds.size);
	Close(window);
	EXPECT_EQ(modes.calls.restores, 1u);
	EXPECT_NE(PlayerDocument().find("ExclusiveFullScreen"), str::npos) << PlayerDocument();
}

TEST_F(ScreenFixture, RefusedExclusiveCoversMonitorAtRenderSize) {
	StandInModeSwitch modes;
	modes.calls.refuse = true;
	Player().fullScreenMode = FullScreenMode::ExclusiveFullScreen;
	Player().defaultIsNativeResolution = false;
	Player().defaultScreenWidth = 1280;
	Player().defaultScreenHeight = 720;
	Handle<Window> window = CreateHidden();
	ASSERT_TRUE(window.IsValid());
	Focus(window, true);
	EXPECT_EQ(modes.calls.sets.size(), 1u);
	EXPECT_EQ(window->GetPlacement().state, WindowState::Fullscreen);
	EXPECT_EQ(WindowInternal::GetRenderSizeOverride(*window.Get()), Vec2I(1280, 720));
	EXPECT_EQ(Screen::GetFullScreenMode(), FullScreenMode::ExclusiveFullScreen);
	Close(window);
	EXPECT_EQ(modes.calls.restores, 0u);
}

TEST_F(ScreenFixture, DriverAcceptsCurrentDisplayMode) {
	std::span<const MonitorInfo> monitors = WindowSystem::GetMonitors();
	ASSERT_FALSE(monitors.empty());
	const MonitorInfo& monitor = monitors[0];
	EXPECT_EQ(ScreenInternal::CanSwitchDisplayMode(monitor.id, monitor.bounds.size, monitor.refreshRate),
			  TestPlatform::ChangesDisplayModes())
		<< monitor.bounds.size.x << "x" << monitor.bounds.size.y << " at " << monitor.refreshRate;
	EXPECT_FALSE(ScreenInternal::CanSwitchDisplayMode(monitor.id, {7, 3}, 0.0f));
}
