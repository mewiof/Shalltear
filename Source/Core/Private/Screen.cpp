#include "Shalltear/Core/Screen.h"

#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Input/Input.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Window/WindowEvents.h"
#include "Shalltear/Core/Window/WindowSystem.h"

#include "ScreenInternal.h"
#include "UI/UIScreenInternal.h"
#include "Window/WindowInternal.h"

#include <algorithm>
#include <format>

namespace Shalltear {

	namespace {

		const u8 _FRAME_STAGE_OWNER = 0;

		constexpr std::string_view PLAYER_CATEGORY = "Player";
		constexpr std::string_view SCREEN_CATEGORY = "Screen";

		std::string_view ModeName(FullScreenMode mode) {
			switch (mode) {
			case FullScreenMode::FullScreenWindow:
				return "Fullscreen Window";
			case FullScreenMode::MaximizedWindow:
				return "Maximized Window";
			case FullScreenMode::Windowed:
				return "Windowed";
			}
			return "?";
		}

		str DescribeSize(FullScreenMode mode, bool native, Vec2I resolution) {
			if (mode != FullScreenMode::Windowed && native) {
				return "native size";
			}
			return std::format("{}x{}", resolution.x, resolution.y);
		}

		PlayerSettings* Project() {
			return Settings::DeclareCategory<PlayerSettings>(PLAYER_CATEGORY, SettingScope::Project);
		}

		ScreenSettings* Saved() {
			return Settings::DeclareCategory<ScreenSettings>(SCREEN_CATEGORY, SettingScope::UserProject);
		}

		class ScreenRuntime : public Object {
		public:
			Handle<Window> window;
			FullScreenMode mode = FullScreenMode::FullScreenWindow;
			bool nativeResolution = true;
			Vec2I resolution{};
			Vec2I frame{};

			bool pending = false;
			FullScreenMode pendingMode = FullScreenMode::FullScreenWindow;
			bool pendingNative = true;
			Vec2I pendingResolution{};

			bool warnedNowhere = false;

			void Subscribe() {
				EventBus& bus = EventBus::GetGlobal();
				Handle<ScreenRuntime> self(this);
				WindowEvents::Resized.Subscribe(bus, self, &ScreenRuntime::OnResized);
				WindowEvents::Destroying.Subscribe(bus, self, &ScreenRuntime::OnDestroying);
			}

			void OnResized(const WindowResizedEvent& event) {
				Window* game = window.Get();
				if (event.window != window || !game || event.live || mode != FullScreenMode::Windowed) {
					return;
				}
				if (game->GetPlacement().state == WindowState::Normal && event.clientSizePx.x > 0 && event.clientSizePx.y > 0) {
					resolution = event.clientSizePx;
				}
			}

			void OnDestroying(const WindowDestroyingEvent& event) {
				if (event.window != window) {
					return;
				}
				Persist();
				window = {};
			}

			void Persist() {
				Window* game = window.Get();
				ScreenSettings* saved = Saved();
				const PlayerSettings* project = Project();
				if (!game || !saved || !project) {
					return;
				}
				saved->saved = true;
				saved->fullScreenMode = mode;
				saved->nativeResolution = nativeResolution;
				saved->width = resolution.x;
				saved->height = resolution.y;
				const WindowPlacement placement = game->GetPlacement();
				saved->hasPosition = true;
				saved->positionX = placement.position.x;
				saved->positionY = placement.position.y;
				saved->projectDefaults = ScreenInternal::DescribeProjectDefaults(*project);
				if (!Settings::Save()) {
					SLT_LOG_WARN("Screen", "the screen settings couldn't be saved");
				}
			}

			Vec2I FitWindowed(const Window& game, Vec2I wanted) const {
				const RectI work = game.GetMonitor().workArea;
				if (work.size.x <= 0 || work.size.y <= 0) {
					return wanted;
				}
				return Vec2I{std::clamp(wanted.x, 1, std::max(1, work.size.x - frame.x)),
							 std::clamp(wanted.y, 1, std::max(1, work.size.y - frame.y))};
			}

			void Apply(FullScreenMode toMode, bool toNative, Vec2I toResolution) {
				Window* game = window.Get();
				if (!game) {
					return;
				}
				mode = toMode;
				nativeResolution = toNative;
				resolution = toResolution;
				WindowPlacement placement = game->GetPlacement();
				switch (toMode) {
				case FullScreenMode::Windowed:
					WindowInternal::SetRenderSize(*game, {});
					placement.clientSize = FitWindowed(*game, toResolution);
					placement.state = WindowState::Normal;
					game->SetPlacement(placement);
					resolution = placement.clientSize;
					break;
				case FullScreenMode::FullScreenWindow:
					placement.state = WindowState::Fullscreen;
					game->SetPlacement(placement);
					WindowInternal::SetRenderSize(*game, toNative ? Vec2I{} : toResolution);
					break;
				case FullScreenMode::MaximizedWindow:
					placement.state = WindowState::Maximized;
					game->SetPlacement(placement);
					WindowInternal::SetRenderSize(*game, toNative ? Vec2I{} : toResolution);
					break;
				}
			}
		};

		ScreenRuntime* _runtime = nullptr;

		Window* GameWindow() {
			if (!_runtime || UIScreenInternal::GetTarget() != UIScreenDesc::Target::Window) {
				return nullptr;
			}
			return _runtime->window.Get();
		}

		bool CanChange(std::string_view what) {
			if (GameWindow()) {
				return true;
			}
			if (_runtime && !_runtime->warnedNowhere) {
				_runtime->warnedNowhere = true;
				SLT_LOG_WARN("Screen", "Screen::{} changes nothing here. The screen isn't a game window (the editor's "
									   "Runtime View, or none). Applies in a built game only",
							 what);
			}
			return false;
		}

		void Request(FullScreenMode mode, bool native, Vec2I resolution) {
			_runtime->pending = true;
			_runtime->pendingMode = mode;
			_runtime->pendingNative = native;
			_runtime->pendingResolution = resolution;
		}

		void EndFramePass(const FrameStageArgs&, void*) {
			Window* game = GameWindow();
			if (!game) {
				return;
			}
			const PlayerSettings* project = Project();
			if (project && project->allowFullscreenSwitch) {
				for (const InputEvent& event : Input::GetFrameEvents()) {
					if (event.type == InputEventType::KeyDown && !event.repeat && event.window == _runtime->window &&
						(event.key == Key::Enter || event.key == Key::KpEnter) && HasFlag(event.mods, KeyMod::Alt)) {
						Screen::SetFullScreen(!Screen::IsFullScreen());
					}
				}
			}
			if (!_runtime->pending) {
				return;
			}
			_runtime->pending = false;
			_runtime->Apply(_runtime->pendingMode, _runtime->pendingNative, _runtime->pendingResolution);
			SLT_LOG_INFO("Screen", "{} at {}", ModeName(_runtime->mode),
						 DescribeSize(_runtime->mode, _runtime->nativeResolution, _runtime->resolution));
			_runtime->Persist();
		}

	} // anonymous namespace

	namespace ScreenInternal {

		void Initialize() {
			if (_runtime) {
				return;
			}
			_runtime = New<ScreenRuntime>(MemTag::Platform);
			_runtime->Subscribe();
			Project();
			FrameStages::Register(FrameStage::EndFrame, FrameStagePosition::Late, &EndFramePass, nullptr, &_FRAME_STAGE_OWNER);
		}

		void Terminate() {
			if (!_runtime) {
				return;
			}
			FrameStages::UnregisterOwnedBy(&_FRAME_STAGE_OWNER);
			EventBus::GetGlobal().UnsubscribeAll(_runtime->GetInstanceId());
			Delete(_runtime);
			_runtime = nullptr;
		}

		str DescribeProjectDefaults(const PlayerSettings& project) {
			return std::format("{}|{}|{}|{}", static_cast<u32>(project.fullScreenMode), project.defaultIsNativeResolution ? 1 : 0,
							   project.defaultScreenWidth, project.defaultScreenHeight);
		}

		ScreenChoice ResolveStartup(const PlayerSettings& project, const ScreenSettings& saved, const ScreenLaunch& launch) {
			ScreenChoice choice;
			choice.mode = project.fullScreenMode;
			choice.nativeResolution = project.defaultIsNativeResolution;
			choice.resolution = {project.defaultScreenWidth, project.defaultScreenHeight};

			if (saved.saved && saved.projectDefaults == DescribeProjectDefaults(project) && saved.width > 0 && saved.height > 0) {
				choice.mode = saved.fullScreenMode;
				choice.nativeResolution = saved.nativeResolution;
				choice.resolution = {saved.width, saved.height};
			}

			if (launch.width && *launch.width > 0) {
				choice.resolution.x = *launch.width;
				choice.nativeResolution = false;
			}
			if (launch.height && *launch.height > 0) {
				choice.resolution.y = *launch.height;
				choice.nativeResolution = false;
			}
			if (launch.fullScreen) {
				if (!*launch.fullScreen) {
					choice.mode = FullScreenMode::Windowed;
				} else if (choice.mode == FullScreenMode::Windowed) {
					choice.mode = project.fullScreenMode != FullScreenMode::Windowed ? project.fullScreenMode : FullScreenMode::FullScreenWindow;
				}
			}
			if (choice.resolution.x <= 0 || choice.resolution.y <= 0) {
				choice.resolution = {1024, 768};
			}
			return choice;
		}

		u32 SelectMonitor(std::span<const MonitorInfo> monitors, const ScreenSettings& saved, const ScreenLaunch& launch) {
			if (monitors.empty()) {
				return 0;
			}
			if (launch.monitor && *launch.monitor >= 1 && static_cast<size_t>(*launch.monitor) <= monitors.size()) {
				return static_cast<u32>(*launch.monitor - 1);
			}
			if (saved.hasPosition) {
				for (u32 i = 0; i < monitors.size(); ++i) {
					const RectI& work = monitors[i].workArea;
					if (saved.positionX >= work.position.x && saved.positionX < work.position.x + work.size.x &&
						saved.positionY >= work.position.y && saved.positionY < work.position.y + work.size.y) {
						return i;
					}
				}
			}
			for (u32 i = 0; i < monitors.size(); ++i) {
				if (monitors[i].primary) {
					return i;
				}
			}
			return 0;
		}

		bool IsReachable(Vec2I position, Vec2I outerSize, std::span<const MonitorInfo> monitors) {
			const Vec2I grip{position.x + outerSize.x / 2, position.y + 8};
			return std::any_of(monitors.begin(), monitors.end(), [&](const MonitorInfo& monitor) {
				const RectI& work = monitor.workArea;
				return grip.x >= work.position.x && grip.x < work.position.x + work.size.x &&
					   grip.y >= work.position.y && grip.y < work.position.y + work.size.y;
			});
		}

		Handle<Window> CreateGameWindow(const ScreenLaunch& launch, WindowFlags extraFlags) {
			const PlayerSettings defaults;
			const PlayerSettings* declared = Project();
			const PlayerSettings& project = declared ? *declared : defaults;
			const ScreenSettings noChoice;
			const ScreenSettings* stored = Saved();
			const ScreenSettings& saved = stored ? *stored : noChoice;

			const ScreenChoice choice = ResolveStartup(project, saved, launch);
			std::span<const MonitorInfo> monitors = WindowSystem::GetMonitors();
			const u32 monitorIndex = SelectMonitor(monitors, saved, launch);
			const RectI work = monitors.empty() ? RectI{} : monitors[monitorIndex].workArea;

			WindowDesc desc;
			desc.title = launch.title.empty() ? str("Shalltear") : launch.title;
			desc.clientSize = choice.resolution;
			if (!monitors.empty()) {
				desc.position = work.position;
			}
			desc.flags = WindowFlags::Hidden | extraFlags;
			if (project.resizableWindow) {
				desc.flags = desc.flags | WindowFlags::Resizable;
			}
			Handle<Window> created = WindowSystem::Create(desc);
			Window* game = created.Get();
			if (!game) {
				return {};
			}

			if (_runtime) {
				_runtime->window = created;
				_runtime->frame = game->GetRect().size - game->GetClientSize();
			}
			const Vec2I frame = game->GetRect().size - game->GetClientSize();
			Vec2I client = choice.resolution;
			if (!monitors.empty()) {
				client.x = std::clamp(client.x, 1, std::max(1, work.size.x - frame.x));
				client.y = std::clamp(client.y, 1, std::max(1, work.size.y - frame.y));
			}
			const Vec2I outer = client + frame;
			Vec2I position = monitors.empty() ? Vec2I{} : CenterIn(work, outer);
			const Vec2I savedPosition{saved.positionX, saved.positionY};
			if (saved.hasPosition && !launch.monitor && IsReachable(savedPosition, outer, monitors)) {
				position = savedPosition;
			}

			WindowPlacement placement;
			placement.position = position;
			placement.clientSize = client;
			placement.state = choice.mode == FullScreenMode::FullScreenWindow ? WindowState::Fullscreen : WindowState::Normal;
			game->SetPlacement(placement);
			if (choice.mode != FullScreenMode::Windowed && !choice.nativeResolution) {
				WindowInternal::SetRenderSize(*game, choice.resolution);
			}
			if (_runtime) {
				_runtime->mode = choice.mode;
				_runtime->nativeResolution = choice.nativeResolution;
				_runtime->resolution = choice.mode == FullScreenMode::Windowed ? client : choice.resolution;
			}

			if (!HasFlag(extraFlags, WindowFlags::Hidden)) {
				if (choice.mode == FullScreenMode::MaximizedWindow) {
					game->Maximize();
				} else {
					game->Show();
				}
			}
			SLT_LOG_INFO("Screen", "{} at {} on '{}'", ModeName(choice.mode),
						 DescribeSize(choice.mode, choice.nativeResolution, _runtime ? _runtime->resolution : choice.resolution),
						 monitors.empty() ? str("no monitor") : monitors[monitorIndex].name);
			return created;
		}

	} // namespace ScreenInternal

	namespace Screen {

		i32 GetWidth() {
			return UIScreenInternal::GetSize().x;
		}

		i32 GetHeight() {
			return UIScreenInternal::GetSize().y;
		}

		FullScreenMode GetFullScreenMode() {
			if (GameWindow()) {
				return _runtime->pending ? _runtime->pendingMode : _runtime->mode;
			}
			const PlayerSettings* project = Project();
			return project ? project->fullScreenMode : FullScreenMode::FullScreenWindow;
		}

		void SetFullScreenMode(FullScreenMode mode) {
			if (!CanChange("SetFullScreenMode")) {
				return;
			}
			const bool native = _runtime->pending ? _runtime->pendingNative : _runtime->nativeResolution;
			const Vec2I resolution = _runtime->pending ? _runtime->pendingResolution : _runtime->resolution;
			Request(mode, native, resolution);
		}

		bool IsFullScreen() {
			return GetFullScreenMode() != FullScreenMode::Windowed;
		}

		void SetFullScreen(bool fullScreen) {
			FullScreenMode mode = FullScreenMode::Windowed;
			if (fullScreen) {
				const PlayerSettings* project = Project();
				mode = project && project->fullScreenMode != FullScreenMode::Windowed ? project->fullScreenMode
																					  : FullScreenMode::FullScreenWindow;
			}
			SetFullScreenMode(mode);
		}

		void SetResolution(i32 width, i32 height, FullScreenMode mode) {
			if (width <= 0 || height <= 0) {
				SLT_LOG_WARN("Screen", "SetResolution({}, {}) ignored. Width and height should be positive", width, height);
				return;
			}
			if (!CanChange("SetResolution")) {
				return;
			}
			Request(mode, false, {width, height});
		}

		ScreenResolution GetCurrentResolution() {
			ScreenResolution current;
			const MonitorInfo* monitor = nullptr;
			if (Window* game = GameWindow()) {
				monitor = &game->GetMonitor();
			} else if (!WindowSystem::GetMonitors().empty()) {
				for (const MonitorInfo& candidate : WindowSystem::GetMonitors()) {
					if (!monitor || candidate.primary) {
						monitor = &candidate;
					}
				}
			}
			if (monitor) {
				current.width = monitor->bounds.size.x;
				current.height = monitor->bounds.size.y;
				current.refreshRate = monitor->refreshRate;
			}
			return current;
		}

		void GetResolutions(Vector<ScreenResolution>& out) {
			out.clear();
			u64 monitorId = 0;
			if (Window* game = GameWindow()) {
				monitorId = game->GetMonitor().id;
			} else {
				for (const MonitorInfo& candidate : WindowSystem::GetMonitors()) {
					if (monitorId == 0 || candidate.primary) {
						monitorId = candidate.id;
					}
				}
			}
			Vector<WindowInternal::DisplayMode> modes{GetMemoryResource(MemTag::Platform)};
			WindowBackend::EnumerateDisplayModes(monitorId, modes);
			out.reserve(modes.size());
			for (const WindowInternal::DisplayMode& mode : modes) {
				out.push_back({mode.sizePx.x, mode.sizePx.y, mode.refreshRate});
			}
		}

		Handle<Window> CreateGameWindow(const ScreenLaunch& launch) {
			return ScreenInternal::CreateGameWindow(launch, WindowFlags::None);
		}

	} // namespace Screen

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::PlayerSettings)
SLT_REGISTER_TYPE(Shalltear::ScreenSettings)
