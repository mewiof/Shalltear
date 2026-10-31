#include "Shalltear/Core/Window/WindowSystem.h"

#include "Shalltear/Core/Assert.h"
#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Window/WindowEvents.h"

#include "Input/InputInternal.h"
#include "Module/EngineStateTable.h"
#include "Window/WindowInternal.h"

#include <algorithm>

namespace Shalltear {

	Window::Window() : _impl(nullptr) {}

	Window::~Window() {
		SLT_CORE_ASSERT_MSG(_impl == nullptr, "Window destroyed outside the WindowSystem destruction pipeline");
	}

	namespace WindowInternal {

		namespace {
			SystemState* _boundState = nullptr;
		} // anonymous namespace

		SystemState& GetState() {
			if (_boundState)
				return *_boundState;
			static SystemState state;
			return state;
		}

		bool IsMainThread() {
			return std::this_thread::get_id() == GetState().mainThread;
		}

		void RefreshMonitorsIfDirty() {
			SystemState& state = GetState();
			if (!state.monitorsDirty) {
				return;
			}
			state.monitorsDirty = false;
			WindowBackend::EnumerateMonitors(state.monitors);
			WindowEvents::MonitorsChanged.Publish(EventBus::GetGlobal(), {});
		}

		void DestroyWindowNow(Window* window) {
			SystemState& state = GetState();

			Handle<Window> handle(window);
			WindowEvents::Destroying.Publish(EventBus::GetGlobal(), WindowDestroyingEvent{handle});

			u64 id = window->GetInstanceId();
			if (state.focused.GetInstanceId() == id) {
				state.focused = nullptr;
			}

			WindowBackend::DestroyNative(*window);

			auto it = std::find(state.windows.begin(), state.windows.end(), window);
			if (it != state.windows.end()) {
				state.windows.erase(it);
			}

			WindowInternalAccess::DeleteWindow(window);
			WindowEvents::Destroyed.Publish(EventBus::GetGlobal(), WindowDestroyedEvent{id});
		}

		void ProcessDestroyQueue() {
			SystemState& state = GetState();
			Vector<u64> queue{GetMemoryResource(MemTag::Platform)};
			queue.swap(state.destroyQueue);
			for (u64 id : queue) {
				if (Window* window = static_cast<Window*>(ObjectRegistry::Resolve(id))) {
					DestroyWindowNow(window);
				}
			}
		}

		void InvokeModalTick() {
			SystemState& state = GetState();
			if (!state.modalTick || state.inModalTick) {
				return;
			}
			state.inModalTick = true;
			state.modalTick(state.modalTickUser);
			InputInternal::BeginPump();
			state.inModalTick = false;
		}

	} // namespace WindowInternal

	using WindowInternal::GetState;

#define SLT_WINDOW_ASSERT_MAIN_THREAD() \
	SLT_CORE_ASSERT_MSG(WindowInternal::IsMainThread(), "WindowSystem operations are main-thread-only")

	void WindowSystem::Initialize() {
		WindowInternal::SystemState& state = GetState();
		SLT_CORE_ASSERT_MSG(!state.initialized, "WindowSystem::Initialize called twice");
		if (state.initialized) {
			return;
		}

		state.mainThread = std::this_thread::get_id();
		InputInternal::ResetState();
		WindowBackend::Initialize();
		WindowBackend::EnumerateMonitors(state.monitors);
		state.monitorsDirty = false;
		state.initialized = true;

		SLT_LOG_INFO("Platform", "WindowSystem initialized ({} monitor(s))", state.monitors.size());
	}

	void WindowSystem::Terminate() {
		WindowInternal::SystemState& state = GetState();
		if (!state.initialized) {
			return;
		}
		SLT_WINDOW_ASSERT_MAIN_THREAD();

		while (!state.windows.empty()) {
			WindowInternal::DestroyWindowNow(state.windows.back());
		}
		state.windows.clear();
		state.windows.shrink_to_fit();
		state.destroyQueue.clear();
		state.destroyQueue.shrink_to_fit();
		state.monitors.clear();
		state.monitors.shrink_to_fit();
		state.focused = nullptr;
		state.modalTick = nullptr;
		state.modalTickUser = nullptr;

		WindowBackend::Terminate();
		InputInternal::ResetState();
		state.initialized = false;

		SLT_LOG_INFO("Platform", "WindowSystem terminated");
	}

	bool WindowSystem::IsInitialized() {
		return GetState().initialized;
	}

	Handle<Window> WindowSystem::Create(const WindowDesc& desc) {
		WindowInternal::SystemState& state = GetState();
		SLT_CORE_ASSERT_MSG(state.initialized, "WindowSystem::Create before Initialize");
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (!state.initialized) {
			return {};
		}

		Window* window = WindowInternalAccess::NewWindow();
		if (!WindowBackend::CreateNative(*window, desc)) {
			SLT_LOG_ERROR("Platform", "Window creation failed (title: \"{}\")", desc.title);
			WindowInternalAccess::DeleteWindow(window);
			return {};
		}

		state.windows.push_back(window);
		Handle<Window> handle(window);
		WindowEvents::Created.Publish(EventBus::GetGlobal(), WindowCreatedEvent{handle});
		return handle;
	}

	void WindowSystem::RequestDestroy(Handle<Window> window) {
		WindowInternal::SystemState& state = GetState();
		SLT_WINDOW_ASSERT_MAIN_THREAD();

		Window* ptr = window.Get();
		if (!ptr) {
			return;
		}

		u64 id = ptr->GetInstanceId();
		if (std::find(state.destroyQueue.begin(), state.destroyQueue.end(), id) != state.destroyQueue.end()) {
			return;
		}

		ptr->Hide();
		state.destroyQueue.push_back(id);
	}

	void WindowSystem::PumpEvents() {
		WindowInternal::SystemState& state = GetState();
		SLT_CORE_ASSERT_MSG(state.initialized, "WindowSystem::PumpEvents before Initialize");
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (!state.initialized) {
			return;
		}

		if (state.inPump) {
			return;
		}
		state.inPump = true;

		WindowInternal::ProcessDestroyQueue();
		InputInternal::BeginPump();
		WindowBackend::Pump();
		WindowInternal::RefreshMonitorsIfDirty();
		InputInternal::PollGamepads(state.focused.IsValid());
		InputInternal::SetApplicationFocused(state.focused.IsValid());
		InputInternal::EndPump();

		state.inPump = false;
	}

	void WindowSystem::WaitEvents(f64 timeoutSeconds) {
		WindowInternal::SystemState& state = GetState();
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (!state.initialized || state.inPump) {
			return;
		}
		WindowBackend::Wait(timeoutSeconds);
		PumpEvents();
	}

	std::span<const MonitorInfo> WindowSystem::GetMonitors() {
		WindowInternal::SystemState& state = GetState();
		return {state.monitors.data(), state.monitors.size()};
	}

	Handle<Window> WindowSystem::GetFocusedWindow() {
		return GetState().focused;
	}

	Handle<Window> WindowSystem::GetPrimaryWindow() {
		WindowInternal::SystemState& state = GetState();
		return state.windows.empty() ? Handle<Window>{} : Handle<Window>(state.windows.front());
	}

	Handle<Window> WindowSystem::GetWindowAtPoint(Vec2I screenPx, Handle<Window> ignore) {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		return WindowBackend::WindowAtPoint(screenPx, ignore);
	}

	u32 WindowSystem::GetWindowCount() {
		return static_cast<u32>(GetState().windows.size());
	}

	bool WindowSystem::SetClipboardText(std::string_view utf8) {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		return WindowBackend::SetClipboardText(utf8);
	}

	str WindowSystem::GetClipboardText() {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		return WindowBackend::GetClipboardText();
	}

	void WindowSystem::SetModalTickCallback(void (*tick)(void* user), void* user) {
		WindowInternal::SystemState& state = GetState();
		state.modalTick = tick;
		state.modalTickUser = user;
	}

#undef SLT_WINDOW_ASSERT_MAIN_THREAD

	namespace ModuleInternal {

		void CaptureWindowSystemEngineState(EngineStateTable& table) {
			table.windowSystem = &WindowInternal::GetState();
		}

		void BindWindowSystemEngineState(const EngineStateTable& table) {
			WindowInternal::_boundState = static_cast<WindowInternal::SystemState*>(table.windowSystem);
		}

	} // namespace ModuleInternal

} // namespace Shalltear
