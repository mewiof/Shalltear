#pragma once

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Input.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Profiler.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Time.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/Window/Window.h"
#include "Shalltear/Core/Window/WindowEvents.h"
#include "Shalltear/Core/Window/WindowSystem.h"

#include "EditorConfig.h"
#include "EditorShell.h"
#include "GameModuleHost.h"
#include "PackageHost.h"
#include "Shalltear/Editor/BuildPipeline.h"
#include "Shalltear/Editor/CompileService.h"
#include "Shalltear/Editor/EditorFrameClock.h"
#include "Shalltear/Editor/EditorFrameCost.h"
#include "Shalltear/Editor/EditorInputOps.h"
#include "Shalltear/Editor/EditorPresentPolicy.h"
#include "Shalltear/Editor/PlayModeController.h"

#include <vector>

namespace Shalltear {

	class EditorPanel;

	class EditorApplication : public Object {
	public:
		EditorApplication(int argc, char* argv[]);

		bool MountRequestedProject();

		static void MigrateLegacySettingsFiles(const SettingsPaths& paths);

		[[nodiscard]] bool Initialize();
		void Terminate();
		void Run();

		const str& GetRelaunchCommandLine() const { return _relaunchCommandLine; }

		void ArmShaderHotReload();

		void OnCloseRequested(const WindowCloseRequestedEvent& event);
		void OnWindowDestroyed(const WindowDestroyedEvent& event);
		void OnWindowResized(const WindowResizedEvent& event);
		void OnFilesDropped(const FilesDroppedEvent& event);
		void OnWindowStateChanged(const WindowStateEvent& event);
	private:
		void RunFrame();
		f64 ComputeIdleSeconds() const;
		void DumpZonesIfDue();
		void UIUpdate(const FrameStageArgs& stageArgs);
		void RunFrameBoundary();
		void BuildSmokeScene();
		void RestoreStartupScene();
		void BuildSmokeUIDocument();
		void CloseSmokeUIBuilder();
		void ReleaseSmokeUIDocument();
		bool ResolveProject();
		void AdoptProject(const str& path);
		u32 CaptureSmokeViewPixels() const;
		void FocusRuntimeView();
		void RestoreViewAfterPlay();
		void RouteGameplayInput();
		bool RuntimeViewHasInputFocus() const;
		struct RuntimeViewPointer {
			bool aimed = false;
			bool owned = false;
		};
		RuntimeViewPointer AdvanceRuntimeViewPointer();
		void RequestQuit();
		void RequestRestart();
		void QuitInternal();
		void ReportFrameCostGestures();
		void ReportFrameCostModalPeriods();
		void ReportFrameCostSummary() const;

		void OnFrameCostDragBegan(const WindowDragBeganEvent& event);
		void OnFrameCostDragMoved(const WindowDragMovedEvent& event);
		void OnFrameCostDragEnded(const WindowDragEndedEvent& event);
		EditorFrameCost::Counters SampleFrameCostCounters() const;
		void ReportFrameCostEpochs();

		Handle<Window> _mainWindow;
		u64 _mainWindowId = 0;
		EditorPresentPolicy _presentPolicy;

		FrameTimer _frameTimer;
		EditorFrameClock _clock;
		EditorInputOps::EditorShortcuts _shortcuts;
		bool _quit = false;
		bool _projectMounted = false;
		bool _wasPlaying = false;
		Vector<StringHash> _tabsBeforePlay{GetMemoryResource(MemTag::Editor)};
		bool _tickGameplay = false;
		bool _layoutSaved = false;
		bool _quitConfirmed = false;
		i32 _smokeFramesRemaining = -1;
		i32 _frameDepth = 0;
		str _modulePath;
		str _projectPath;
		Vector<str> _arguments{GetMemoryResource(MemTag::Editor)};
		str _relaunchCommandLine;
		str _projectName;
		CompileService _compileService;
		BuildPipeline _buildPipeline;
		GameModuleHost _moduleHost;
		PackageHost _packageHost;
		PlayModeController _playMode;
		EditorShell _shell;
		EditorConfig _config;
		std::vector<FileWatcher> _shaderWatchers;
		struct FloatingChrome {
			Handle<Window> window;
			UI::ElementHandle maximizeFace;
		};
		std::vector<FloatingChrome> _floatingChrome;
		Handle<Scene> _smokeScene;
		Handle<MeshAsset> _smokeMesh;
		str _smokeUIDirectory;
		Handle<EditorPanel> _smokeUIPanel;
		GraphicsStats _smokeStats;
		UISystem::Stats _smokeUIStats;
		u32 _smokeViewPixels = 0;

		bool _frameCostEnabled = false;
		u64 _frameCostUITicks = 0;
		u64 _frameCostModalTicks = 0;
		EditorFrameCost::Recorder _frameCost;

		static constexpr u32 ZONE_DUMP_PERIOD = 60;
		static constexpr const char* ZONE_FRAME_NAME = "Editor.Frame";
		bool _zoneDumpEnabled = false;
		u32 _zoneDumpCountdown = ZONE_DUMP_PERIOD;
		static constexpr f64 THROTTLE_WAIT_SECONDS = 0.25;
		static constexpr u32 THROTTLE_ANNOUNCE_FRAMES = 4;
		bool _throttleWhenUnfocused = true;
		bool _throttleAnnounced = false;
		u32 _throttledFrames = 0;
		f64 _lastFrameWorkSeconds = 0.0;
		f64 _lastIdleSeconds = 0.0;
		std::vector<Profiler::ZoneResult> _zoneWorst;
		f64 _zoneWorstMs = -1.0;
		u32 _zoneWorstFrame = 0;
		u32 _zoneWindowFrames = 0;
		u32 _zoneWorstDroppedSites = 0;
		u32 _zoneWorstDroppedDepth = 0;
	};

} // namespace Shalltear
