#include "Platform/Linux/Window/LinuxWindow.h"

#include "Platform/Linux/Window/LinuxDisplayServers.h"

#include "Shalltear/Core/Assert.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"

#include <cerrno>
#include <cstdlib>

#include <time.h>

namespace Shalltear::Linux {

	namespace {

		DisplayServer* _server = nullptr;

		DisplayServer* Connect() {
			const char* forced = std::getenv("SLT_DISPLAY_SERVER");
			const std::string_view wanted = forced ? std::string_view(forced) : std::string_view();

			for (const ServerCandidate& candidate : GetServerCandidates()) {
				if (!wanted.empty() && wanted != candidate.name) {
					continue;
				}
				str why;
				if (DisplayServer* server = candidate.connect(why)) {
					SLT_LOG_INFO("Platform", "display server: {}", server->GetName());
					return server;
				}
				if (!wanted.empty()) {
					SLT_LOG_ERROR("Platform", "SLT_DISPLAY_SERVER={} couldn't start ({})", wanted, why);
				} else {
					SLT_LOG_DEBUG("Platform", "display server {} unavailable ({})", candidate.name, why);
				}
			}
			if (!wanted.empty()) {
				SLT_LOG_ERROR("Platform", "SLT_DISPLAY_SERVER={} is not a display server this build has", wanted);
			} else {
				SLT_LOG_INFO("Platform", "no display server (windows can't be created)");
			}
			return nullptr;
		}

		void SleepUntil(f64 seconds) {
			timespec due{};
			::clock_gettime(CLOCK_MONOTONIC, &due);
			const f64 whole = static_cast<f64>(static_cast<i64>(seconds));
			due.tv_sec += static_cast<time_t>(whole);
			due.tv_nsec += static_cast<long>((seconds - whole) * 1.0e9);
			if (due.tv_nsec >= 1000000000l) {
				due.tv_nsec -= 1000000000l;
				++due.tv_sec;
			}
			while (::clock_nanosleep(CLOCK_MONOTONIC, TIMER_ABSTIME, &due, nullptr) == EINTR) {
			}
		}

	} // anonymous namespace

	DisplayServer* GetDisplayServer() {
		return _server;
	}

	Window* FindWindow(const Window::Impl* impl) {
		if (!impl) {
			return nullptr;
		}
		for (Window* window : WindowInternal::GetState().windows) {
			if (WindowInternalAccess::GetImpl(*window) == impl) {
				return window;
			}
		}
		return nullptr;
	}

	bool CanPublish(const Window::Impl& impl) {
		return impl.constructed && !impl.destroying;
	}

} // namespace Shalltear::Linux

namespace Shalltear::WindowBackend {

	using namespace Shalltear::Linux;

	void Initialize() {
		_server = Connect();
	}

	void Terminate() {
		if (_server) {
			_server->~DisplayServer();
			Free(_server);
			_server = nullptr;
		}
	}

	bool CreateNative(Window& window, const WindowDesc& desc) {
		if (!_server) {
			return false;
		}
		auto* impl = New<Window::Impl>(MemTag::Platform);
		impl->flags = desc.flags;
		impl->title = desc.title;
		impl->minClientSize = desc.minClientSize;
		impl->maxClientSize = desc.maxClientSize;
		impl->self = Handle<Window>(&window);
		WindowInternalAccess::GetImpl(window) = impl;

		if (!_server->CreateWindow(*impl, desc)) {
			WindowInternalAccess::GetImpl(window) = nullptr;
			Delete(impl);
			return false;
		}
		impl->constructed = true;
		if (!HasFlag(desc.flags, WindowFlags::Hidden)) {
			_server->Show(*impl);
		}
		return true;
	}

	void DestroyNative(Window& window) {
		Window::Impl* impl = WindowInternalAccess::GetImpl(window);
		if (!impl) {
			return;
		}
		impl->destroying = true;
		if (_server) {
			_server->DestroyWindow(*impl);
		}
		WindowInternalAccess::GetImpl(window) = nullptr;
		Delete(impl);
	}

	void Pump() {
		if (_server) {
			_server->Pump();
		}
	}

	void Wait(f64 timeoutSeconds) {
		if (_server) {
			_server->Wait(timeoutSeconds);
		} else if (timeoutSeconds > 0.0) {
			SleepUntil(timeoutSeconds);
		}
	}

	void Sleep(f64 seconds) {
		if (seconds > 0.0) {
			SleepUntil(seconds);
		}
	}

	void EnumerateMonitors(Vector<MonitorInfo>& outMonitors) {
		outMonitors.clear();
		if (_server) {
			_server->EnumerateMonitors(outMonitors);
		}
	}

	void EnumerateDisplayModes(u64 monitorId, Vector<WindowInternal::DisplayMode>& outModes) {
		if (_server) {
			_server->EnumerateDisplayModes(monitorId, outModes);
		}
	}

	bool SetDisplayMode(u64, Vec2I, f32, bool) {
		return false;
	}

	void RestoreDisplayMode(u64) {
	}

	Handle<Window> WindowAtPoint(Vec2I screenPx, Handle<Window> ignore) {
		return _server ? _server->WindowAtPoint(screenPx, ignore) : Handle<Window>{};
	}

	bool SetClipboardText(std::string_view utf8) {
		return _server && _server->SetClipboardText(utf8);
	}

	str GetClipboardText() {
		return _server ? _server->GetClipboardText() : str{};
	}

} // namespace Shalltear::WindowBackend

namespace Shalltear {

	using namespace Shalltear::Linux;

#define SLT_WINDOW_ASSERT_MAIN_THREAD() \
	SLT_CORE_ASSERT_MSG(WindowInternal::IsMainThread(), "Window mutators are main-thread-only")

	Vec2I Window::GetClientSize() const {
		return _impl ? GetDisplayServer()->GetClientSize(*_impl) : Vec2I{};
	}

	void Window::SetClientSize(Vec2I sizePx) {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (_impl) {
			GetDisplayServer()->SetClientSize(*_impl, sizePx);
		}
	}

	Vec2I Window::GetPosition() const {
		return _impl ? GetDisplayServer()->GetPosition(*_impl) : Vec2I{};
	}

	void Window::SetPosition(Vec2I posPx) {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (_impl) {
			GetDisplayServer()->SetPosition(*_impl, posPx);
		}
	}

	RectI Window::GetRect() const {
		return _impl ? GetDisplayServer()->GetRect(*_impl) : RectI{};
	}

	f32 Window::GetContentScale() const {
		return _impl ? _impl->scale : 1.0f;
	}

	Vec2I Window::ClientToScreen(Vec2I clientPx) const {
		return _impl ? GetDisplayServer()->ClientToScreen(*_impl, clientPx) : clientPx;
	}

	Vec2I Window::ScreenToClient(Vec2I screenPx) const {
		return _impl ? GetDisplayServer()->ScreenToClient(*_impl, screenPx) : screenPx;
	}

	const MonitorInfo& Window::GetMonitor() const {
		static const MonitorInfo fallback{};
		if (!_impl) {
			return fallback;
		}
		if (const MonitorInfo* monitor = GetDisplayServer()->GetMonitor(*_impl)) {
			return *monitor;
		}
		const Vector<MonitorInfo>& monitors = WindowInternal::GetState().monitors;
		return monitors.empty() ? fallback : monitors.front();
	}

	WindowPlacement Window::GetPlacement() const {
		return _impl ? GetDisplayServer()->GetPlacement(*_impl) : WindowPlacement{};
	}

	void Window::SetPlacement(const WindowPlacement& placement) {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (_impl) {
			GetDisplayServer()->SetPlacement(*_impl, placement);
		}
	}

	void Window::Show() {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (_impl) {
			GetDisplayServer()->Show(*_impl);
		}
	}

	void Window::Hide() {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (_impl) {
			GetDisplayServer()->Hide(*_impl);
		}
	}

	void Window::Minimize() {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (_impl) {
			GetDisplayServer()->Minimize(*_impl);
		}
	}

	void Window::Maximize() {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (_impl) {
			GetDisplayServer()->Maximize(*_impl);
		}
	}

	void Window::Restore() {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (_impl) {
			GetDisplayServer()->Restore(*_impl);
		}
	}

	void Window::Focus() {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (_impl) {
			GetDisplayServer()->Focus(*_impl);
		}
	}

	bool Window::IsVisible() const {
		return _impl && GetDisplayServer()->IsVisible(*_impl);
	}

	bool Window::IsFocused() const {
		return _impl && _impl->focused;
	}

	bool Window::IsMinimized() const {
		return _impl && GetDisplayServer()->IsMinimized(*_impl);
	}

	bool Window::IsMaximized() const {
		return _impl && GetDisplayServer()->IsMaximized(*_impl);
	}

	void Window::SetTitle(std::string_view utf8) {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (!_impl) {
			return;
		}
		_impl->title = str(utf8);
		GetDisplayServer()->SetTitle(*_impl);
	}

	void Window::SetIcon(std::span<const WindowIconImage> images) {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (_impl) {
			GetDisplayServer()->SetIcon(*_impl, images);
		}
	}

	str Window::GetTitle() const {
		return _impl ? _impl->title : str{};
	}

	WindowFlags Window::GetFlags() const {
		return _impl ? _impl->flags : WindowFlags::None;
	}

	void Window::SetCursor(SystemCursor cursor) {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (!_impl || cursor >= SystemCursor::Count) {
			return;
		}
		_impl->cursor = cursor;
		GetDisplayServer()->SetCursor(*_impl);
	}

	SystemCursor Window::GetCursor() const {
		return _impl ? _impl->cursor : SystemCursor::Arrow;
	}

	void Window::SetCursorMode(CursorMode mode) {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (!_impl || _impl->cursorMode == mode) {
			return;
		}
		const CursorMode previous = _impl->cursorMode;
		_impl->cursorMode = mode;
		GetDisplayServer()->SetCursorMode(*_impl, previous);
	}

	CursorMode Window::GetCursorMode() const {
		return _impl ? _impl->cursorMode : CursorMode::Normal;
	}

	void Window::SetHitTestCallback(HitTestFn fn, void* user) {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (_impl) {
			_impl->hitTest = fn;
			_impl->hitTestUser = user;
		}
	}

	void WindowInternal::SetRenderSize(Window& window, Vec2I sizePx) {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (Window::Impl* impl = WindowInternalAccess::GetImpl(window)) {
			impl->renderSize = sizePx.x > 0 && sizePx.y > 0 ? sizePx : Vec2I{};
		}
	}

	Vec2I WindowInternal::GetRenderSizeOverride(const Window& window) {
		const Window::Impl* impl = WindowInternalAccess::GetImpl(window);
		return impl ? impl->renderSize : Vec2I{};
	}

	NativeWindowHandle Window::GetNativeHandle() const {
		return _impl ? GetDisplayServer()->GetNativeHandle(*_impl) : NativeWindowHandle{};
	}

	void Window::SetVSyncHint(bool on) {
		if (_impl) {
			_impl->vsyncHint = on;
		}
	}

	bool Window::GetVSyncHint() const {
		return _impl && _impl->vsyncHint;
	}

#undef SLT_WINDOW_ASSERT_MAIN_THREAD

} // namespace Shalltear
