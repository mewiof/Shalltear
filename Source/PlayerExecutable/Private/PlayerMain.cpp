#include "BootManifest.h"
#include "LinkedPackages.h"

#include "Game/BuildStamp.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Core/Screen.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Time.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/Window/FrameLoop.h"
#include "Shalltear/Core/Window/Window.h"
#include "Shalltear/Core/Window/WindowEvents.h"
#include "Shalltear/Core/Window/WindowSystem.h"

#include <cstdlib>
#include <cstring>
#include <exception>
#include <filesystem>
#include <string>

#if defined(_WIN32)
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace Shalltear {

	namespace {

		struct PlayerOptions {
			i32 frames = -1;
			bool console = false;
			ScreenLaunch screen;
		};

		PlayerOptions ParseOptions(int argc, char* argv[]) {
			PlayerOptions options;
			auto number = [](const char* text) { return static_cast<i32>(std::strtol(text, nullptr, 10)); };
			for (int i = 1; i < argc; ++i) {
				if (std::strcmp(argv[i], "--frames") == 0 && i + 1 < argc) {
					long parsed = std::strtol(argv[++i], nullptr, 10);
					options.frames = parsed > 0 ? static_cast<i32>(parsed) : 0;
				} else if (std::strcmp(argv[i], "--console") == 0) {
					options.console = true;
				} else if (std::strcmp(argv[i], "--screen-width") == 0 && i + 1 < argc) {
					options.screen.width = number(argv[++i]);
				} else if (std::strcmp(argv[i], "--screen-height") == 0 && i + 1 < argc) {
					options.screen.height = number(argv[++i]);
				} else if (std::strcmp(argv[i], "--screen-fullscreen") == 0 && i + 1 < argc) {
					options.screen.fullScreen = number(argv[++i]) != 0;
				} else if (std::strcmp(argv[i], "--monitor") == 0 && i + 1 < argc) {
					options.screen.monitor = number(argv[++i]);
				}
			}
			return options;
		}

		class PlayerWindowPolicy : public Object {
		public:
			void Subscribe() {
				WindowEvents::CloseRequested.Subscribe(EventBus::GetGlobal(), Handle<PlayerWindowPolicy>(this),
													   &PlayerWindowPolicy::OnCloseRequested);
			}
			void Unsubscribe() { EventBus::GetGlobal().UnsubscribeAll(GetInstanceId()); }

			void OnCloseRequested(const WindowCloseRequestedEvent& event) {
				SLT_LOG_INFO("Player", "close requested. Shutting down");
				WindowSystem::RequestDestroy(event.window);
			}
		};

		str GetExecutableDirectory() {
#if defined(_WIN32)
			wchar_t buffer[MAX_PATH * 2] = {};
			DWORD length = ::GetModuleFileNameW(nullptr, buffer, static_cast<DWORD>(std::size(buffer)));
			if (length == 0 || length >= std::size(buffer)) {
				return {};
			}
			return std::filesystem::path(std::wstring(buffer, length)).parent_path().generic_string();
#else
			return {};
#endif
		}

	} // anonymous namespace

	static int RunGame(const PlayerOptions& options) {
		BootManifest manifest;
		str error;
		if (!ReadBootManifest(manifest, error)) {
			SLT_LOG_FATAL("Player", "{}", error);
			return 1;
		}

		SettingsPaths paths = Settings::GetDefaultPaths();
		paths.user = {};
		paths.userProject = {};
		UsePackagedProjectSettings(paths);
		const Path userData = FileSystem::GetUserDataDirectory();
		if (!manifest.productName.empty() && !userData.ToString().empty()) {
			paths.userProject = userData / manifest.productName / "Settings.ini";
		} else {
			SLT_LOG_WARN("Player", "this build has no product name (player preferences can't be saved)");
		}
		Settings::Initialize(paths);

		WindowSystem::Initialize();
		RenderSystem::Initialize();
		UISystem::Initialize();
		SceneSystem::Initialize();

		InitializeLinkedPackages();
		RegisterGameCoreExtensions();

		SLT_LOG_INFO("Player", "{} {} (build {}, {}), {} scene(s)",
					 manifest.productName.empty() ? "Shalltear game" : manifest.productName,
					 BuildStamp::Version, BuildStamp::Number,
					 BuildStamp::TimeIso.empty() ? std::string_view("no build time") : BuildStamp::TimeIso,
					 manifest.scenes.size());

		ScreenLaunch launch = options.screen;
		launch.title = manifest.productName.empty() ? str("Shalltear") : manifest.productName;
		Handle<Window> window = Screen::CreateGameWindow(launch);
		if (!window.IsValid()) {
			SLT_LOG_FATAL("Player", "main window creation failed");
			return 1;
		}

		PlayerWindowPolicy quitPolicy;
		quitPolicy.Subscribe();

		if (manifest.scenes.empty()) {
			SLT_LOG_WARN("Player", "this build stages no scenes (window will be empty)");
		} else {
			str what;
			if (!LoadBootScene(manifest.scenes[0], what)) {
				SLT_LOG_FATAL("Player", "the boot scene didn't load: {}", what);
				return 1;
			}
		}

		FrameTimer frameTimer;
		FixedTimestep fixed(Time::GetFrame().fixedDeltaTime, Time::GetFrame().maximumDeltaTime);
		i32 framesRemaining = options.frames;
		while (WindowSystem::GetWindowCount() > 0 && framesRemaining != 0) {
			WindowSystem::PumpEvents();
			frameTimer.Tick();
			Time::BeginFrame(frameTimer.GetUnscaledDeltaTime());

			SceneSystem::BeginFrame();

			const FrameTime& frame = Time::GetFrame();
			fixed.Configure(frame.fixedDeltaTime, frame.maximumDeltaTime);

			// not Time::GetDeltaTime (clock advances during the walk)
			const f32 gameDeltaSeconds = frame.ComputeGameDelta();

			u32 steps = fixed.Advance(gameDeltaSeconds);
			FrameStageArgs stageArgs;

			for (FrameStage stage : FRAME_STAGES) {
				switch (stage) {
				case FrameStage::FixedUpdate:
					for (u32 i = 0; i < steps; ++i) {
						FrameStages::Run(stage, stageArgs);
					}
					break;
				case FrameStage::Update:
				case FrameStage::UIUpdate:
				case FrameStage::LateUpdate:
				case FrameStage::EndFrame:
					FrameStages::Run(stage, stageArgs);
					break;
				case FrameStage::Render:
					AssetDatabase::CollectGarbage();
					FrameStages::Run(stage, stageArgs);
					break;
				}
			}

			if (framesRemaining > 0 && --framesRemaining == 0) {
				SLT_LOG_INFO("Player", "--frames run complete after {} frame(s). Closing",
							 Time::GetFrameCount());
				WindowSystem::RequestDestroy(window);
			}
		}
		// bus has a handle to this stack local
		quitPolicy.Unsubscribe();
		SLT_LOG_INFO("Player", "player exited after {} frame(s)", Time::GetFrameCount());
		return 0;
	}

	static int RunPlayer(int argc, char* argv[]) {
		PlayerOptions options = ParseOptions(argc, argv);

		JobSystem::Initialize();
		LogInitConfig logConfig;
		logConfig.createConsole = options.console;
		LogManager::Initialize(logConfig);
		TypeRegistry::InitializePrimitives();

		str root = GetExecutableDirectory();
		if (root.empty()) {
			SLT_LOG_FATAL("Player", "couldn't resolve this exe's directory");
			LogManager::Terminate();
			JobSystem::Terminate();
			return 1;
		}

		FileSystem::Mount("project", Path(root));
		SLT_LOG_INFO("Player", "project:// -> {}", root);

		int exitCode = 0;
		try {
			AssetDatabase::Initialize();

			str mountError;
			if (!MountStagedContent(root, mountError)) {
				SLT_LOG_FATAL("Player", "{}", mountError);
				exitCode = 1;
			} else {
				exitCode = RunGame(options);
			}
		} catch (const std::exception& e) {
			SLT_LOG_FATAL("Core", "Exception caught: {}", e.what());
			exitCode = -1;
		} catch (...) {
			SLT_LOG_FATAL("Core", "Unknown exception caught");
			exitCode = -1;
		}

		// not the reverse of init
		SceneSystem::Terminate();
		Settings::Terminate();
		UISystem::Terminate();
		AssetDatabase::Terminate();
		RenderSystem::Terminate();
		WindowSystem::Terminate();
		FileSystem::Unmount("project");
		FileSystem::Unmount("engine");

		// MemTag::Editor must be 0 in a player
		MemoryManager::LogReport();

		LogManager::Terminate();
		JobSystem::Terminate();
		return exitCode;
	}

} //namespace Shalltear

int main(int argc, char* argv[]) {
	return Shalltear::RunPlayer(argc, argv);
}
