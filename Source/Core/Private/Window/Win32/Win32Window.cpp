#include "Shalltear/Core/Platform.h"

#ifdef SLT_PLATFORM_WINDOWS

#include "Window/Win32/Win32Internal.h"

#include <shellapi.h>
#include <dwmapi.h>
#include <shellscalingapi.h>

#include "Shalltear/Core/Assert.h"
#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/StringUtil.h"
#include "Shalltear/Core/Window/WindowEvents.h"

#include "Input/InputInternal.h"

#include <vector>

namespace Shalltear::Win32 {

	namespace {

		bool CanPublish(const Window::Impl& impl) {
			return impl.constructed && !impl.destroying;
		}

		Vec2I CursorScreenPos() {
			POINT pt{};
			::GetCursorPos(&pt);
			return {pt.x, pt.y};
		}

		Vec2I ClientSizeOf(HWND hwnd) {
			RECT rect{};
			::GetClientRect(hwnd, &rect);
			return {rect.right, rect.bottom};
		}

		RectI WindowRectOf(HWND hwnd) {
			RECT rect{};
			::GetWindowRect(hwnd, &rect);
			return {rect.left, rect.top, rect.right - rect.left, rect.bottom - rect.top};
		}

		bool IsBorderless(const Window::Impl& impl) {
			return HasFlag(impl.flags, WindowFlags::Borderless);
		}

		int FrameThickness(HWND hwnd) {
			UINT dpi = ::GetDpiForWindow(hwnd);
			return ::GetSystemMetricsForDpi(SM_CXSIZEFRAME, dpi) + ::GetSystemMetricsForDpi(SM_CXPADDEDBORDER, dpi);
		}

		LONG_PTR WindowedStyleOf(HWND hwnd, const Window::Impl& impl) {
			return impl.fullscreen ? impl.windowedStyle : ::GetWindowLongPtrW(hwnd, GWL_STYLE);
		}

		Vec2I OuterFromClient(HWND hwnd, const Window::Impl& impl, Vec2I clientPx) {
			if (IsBorderless(impl)) {
				return clientPx;
			}
			RECT rect{0, 0, clientPx.x, clientPx.y};
			DWORD style = static_cast<DWORD>(WindowedStyleOf(hwnd, impl));
			DWORD exStyle = static_cast<DWORD>(::GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
			::AdjustWindowRectExForDpi(&rect, style, FALSE, exStyle, ::GetDpiForWindow(hwnd));
			return {rect.right - rect.left, rect.bottom - rect.top};
		}

		Vec2I ClientFromOuter(HWND hwnd, const Window::Impl& impl, Vec2I outerPx) {
			if (IsBorderless(impl)) {
				return outerPx;
			}
			RECT rect{0, 0, 0, 0};
			DWORD style = static_cast<DWORD>(WindowedStyleOf(hwnd, impl));
			DWORD exStyle = static_cast<DWORD>(::GetWindowLongPtrW(hwnd, GWL_EXSTYLE));
			::AdjustWindowRectExForDpi(&rect, style, FALSE, exStyle, ::GetDpiForWindow(hwnd));
			return {outerPx.x - (rect.right - rect.left), outerPx.y - (rect.bottom - rect.top)};
		}

		// rcNormalPosition is in workspace coordinates
		Vec2I WorkspaceOrigin() {
			MONITORINFO info{};
			info.cbSize = sizeof(info);
			HMONITOR primary = ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
			if (!::GetMonitorInfoW(primary, &info)) {
				return {};
			}
			return {info.rcWork.left - info.rcMonitor.left, info.rcWork.top - info.rcMonitor.top};
		}

		bool ShowCommandSatisfied(HWND hwnd, int command) {
			switch (command) {
			case SW_HIDE:
				return !::IsWindowVisible(hwnd);
			case SW_MINIMIZE:
			case SW_SHOWMINIMIZED:
			case SW_SHOWMINNOACTIVE:
				return ::IsIconic(hwnd) != FALSE;
			case SW_MAXIMIZE:
				return ::IsZoomed(hwnd) != FALSE;
			case SW_RESTORE:
			case SW_SHOWNORMAL:
			case SW_SHOWNOACTIVATE:
				return ::IsWindowVisible(hwnd) && !::IsIconic(hwnd) && !::IsZoomed(hwnd);
			default:
				return ::IsWindowVisible(hwnd) != FALSE;
			}
		}

		// 1st show command of a process uses STARTUPINFO. god damn windows
		void ApplyShowCommand(HWND hwnd, int command) {
			::ShowWindow(hwnd, command);
			if (!ShowCommandSatisfied(hwnd, command)) {
				::ShowWindow(hwnd, command);
			}
		}

		void ApplyWindowPlacement(HWND hwnd, const WINDOWPLACEMENT& placement) {
			::SetWindowPlacement(hwnd, &placement);
			if (!ShowCommandSatisfied(hwnd, static_cast<int>(placement.showCmd))) {
				::SetWindowPlacement(hwnd, &placement);
			}
		}

		LRESULT MapHitTestResult(HitTestResult result) {
			switch (result) {
			case HitTestResult::Client:
				return HTCLIENT;
			case HitTestResult::Caption:
				return HTCAPTION;
			case HitTestResult::Left:
				return HTLEFT;
			case HitTestResult::Right:
				return HTRIGHT;
			case HitTestResult::Top:
				return HTTOP;
			case HitTestResult::Bottom:
				return HTBOTTOM;
			case HitTestResult::TopLeft:
				return HTTOPLEFT;
			case HitTestResult::TopRight:
				return HTTOPRIGHT;
			case HitTestResult::BottomLeft:
				return HTBOTTOMLEFT;
			case HitTestResult::BottomRight:
				return HTBOTTOMRIGHT;
			case HitTestResult::MinimizeButton:
				return HTMINBUTTON;
			case HitTestResult::MaximizeButton:
				return HTMAXBUTTON;
			case HitTestResult::CloseButton:
				return HTCLOSE;
			case HitTestResult::Content:
				return HTCLIENT;
			}
			return HTCLIENT;
		}

		bool IsResizeEdge(LRESULT hit) {
			switch (hit) {
			case HTLEFT:
			case HTRIGHT:
			case HTTOP:
			case HTBOTTOM:
			case HTTOPLEFT:
			case HTTOPRIGHT:
			case HTBOTTOMLEFT:
			case HTBOTTOMRIGHT:
				return true;
			default:
				return false;
			}
		}

		LRESULT BorderlessEdgeHitTest(HWND hwnd, Vec2I screenPt) {
			RECT rect{};
			::GetWindowRect(hwnd, &rect);
			int frame = FrameThickness(hwnd);
			bool left = screenPt.x < rect.left + frame;
			bool right = screenPt.x >= rect.right - frame;
			bool top = screenPt.y < rect.top + frame;
			bool bottom = screenPt.y >= rect.bottom - frame;
			if (top && left)
				return HTTOPLEFT;
			if (top && right)
				return HTTOPRIGHT;
			if (bottom && left)
				return HTBOTTOMLEFT;
			if (bottom && right)
				return HTBOTTOMRIGHT;
			if (left)
				return HTLEFT;
			if (right)
				return HTRIGHT;
			if (top)
				return HTTOP;
			if (bottom)
				return HTBOTTOM;
			return HTCLIENT;
		}

		void FlushPendingDragMove(Window::Impl& impl) {
			if (!impl.pendingDragMove) {
				return;
			}
			// before the publish (subscriber may destroy the window)
			impl.pendingDragMove = false;
			if (!CanPublish(impl)) {
				return;
			}
			WindowEvents::DragMoved.Publish(
				EventBus::GetGlobal(),
				WindowDragMovedEvent{impl.self, impl.pendingDragCursor, impl.pendingDragRect});
		}

		void FlushModalPending(Window::Impl& impl) {
			if (!CanPublish(impl)) {
				return;
			}
			if (impl.pendingLiveResize) {
				impl.pendingLiveResize = false;
				WindowEvents::Resized.Publish(EventBus::GetGlobal(),
											  WindowResizedEvent{impl.self, impl.pendingLiveSize, true});
			}
			if (impl.pendingMove) {
				impl.pendingMove = false;
				RectI rect = WindowRectOf(impl.hwnd);
				impl.lastPosition = rect.position;
				WindowEvents::Moved.Publish(EventBus::GetGlobal(),
											WindowMovedEvent{impl.self, rect.position});
			}
			FlushPendingDragMove(impl);
		}

		void HandleSizeMessage(Window::Impl& impl, WPARAM wParam, LPARAM lParam) {
			WindowState newState = impl.stateCache;
			if (wParam == SIZE_MINIMIZED) {
				newState = WindowState::Minimized;
			} else if (wParam == SIZE_MAXIMIZED) {
				newState = WindowState::Maximized;
			} else if (wParam == SIZE_RESTORED) {
				newState = impl.fullscreen ? WindowState::Fullscreen : WindowState::Normal;
			}
			if (newState != impl.stateCache) {
				impl.stateCache = newState;
				if (CanPublish(impl)) {
					WindowEvents::StateChanged.Publish(EventBus::GetGlobal(),
													   WindowStateEvent{impl.self, newState});
				}
			}

			if (wParam == SIZE_MINIMIZED) {
				return;
			}

			Vec2I size{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
			if (size == impl.lastClientSize) {
				return;
			}
			if (impl.inSizeMove) {
				impl.pendingLiveResize = true;
				impl.pendingLiveSize = size;
				return;
			}
			impl.lastClientSize = size;
			if (CanPublish(impl)) {
				WindowEvents::Resized.Publish(EventBus::GetGlobal(),
											  WindowResizedEvent{impl.self, size, false});
			}
		}

		constexpr LONG_PTR FRAME_STYLE = WS_OVERLAPPEDWINDOW;
		constexpr LONG_PTR SHOW_STATE_STYLE = WS_VISIBLE | WS_MINIMIZE | WS_MAXIMIZE;

		void ApplyStyle(HWND hwnd, LONG_PTR style) {
			const LONG_PTR live = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
			::SetWindowLongPtrW(hwnd, GWL_STYLE, (style & ~SHOW_STATE_STYLE) | (live & SHOW_STATE_STYLE));
		}

		RECT MonitorRectOf(HMONITOR monitor) {
			MONITORINFO info{};
			info.cbSize = sizeof(info);
			::GetMonitorInfoW(monitor, &info);
			return info.rcMonitor;
		}

		RECT ToWorkspace(const RECT& rect) {
			const Vec2I origin = WorkspaceOrigin();
			return RECT{rect.left - origin.x, rect.top - origin.y, rect.right - origin.x, rect.bottom - origin.y};
		}

		UINT KeepShowState(HWND hwnd, UINT wanted) {
			if (!::IsWindowVisible(hwnd)) {
				return SW_HIDE;
			}
			if (::IsIconic(hwnd)) {
				return SW_SHOWMINNOACTIVE;
			}
			return wanted;
		}

		void SyncState(Window::Impl& impl) {
			HWND hwnd = impl.hwnd;
			WindowState state = WindowState::Normal;
			if (::IsIconic(hwnd)) {
				state = WindowState::Minimized;
			} else if (impl.fullscreen) {
				state = WindowState::Fullscreen;
			} else if (::IsZoomed(hwnd)) {
				state = WindowState::Maximized;
			}
			if (state == impl.stateCache) {
				return;
			}
			impl.stateCache = state;
			if (CanPublish(impl)) {
				WindowEvents::StateChanged.Publish(EventBus::GetGlobal(), WindowStateEvent{impl.self, state});
			}
		}

		void CoverMonitor(Window::Impl& impl, const RECT& monitor) {
			::SetWindowPos(impl.hwnd, nullptr, monitor.left, monitor.top,
						   monitor.right - monitor.left, monitor.bottom - monitor.top,
						   SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
		}

		void EnterFullscreen(Window::Impl& impl) {
			HWND hwnd = impl.hwnd;
			const RECT monitor = MonitorRectOf(::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST));
			impl.windowedPlacement = WINDOWPLACEMENT{};
			impl.windowedPlacement.length = sizeof(WINDOWPLACEMENT);
			::GetWindowPlacement(hwnd, &impl.windowedPlacement);
			impl.windowedStyle = ::GetWindowLongPtrW(hwnd, GWL_STYLE);
			impl.fullscreen = true;
			ApplyStyle(hwnd, (impl.windowedStyle & ~FRAME_STYLE) | WS_POPUP);

			WINDOWPLACEMENT placement = impl.windowedPlacement;
			placement.flags = 0;
			placement.rcNormalPosition = ToWorkspace(monitor);
			placement.showCmd = KeepShowState(hwnd, SW_SHOWNOACTIVATE);
			ApplyWindowPlacement(hwnd, placement);
			if (!::IsIconic(hwnd)) {
				CoverMonitor(impl, monitor);
			}
			SyncState(impl);
		}

		void LeaveFullscreen(Window::Impl& impl, const WINDOWPLACEMENT& windowed) {
			HWND hwnd = impl.hwnd;
			impl.fullscreen = false;
			ApplyStyle(hwnd, impl.windowedStyle);
			WINDOWPLACEMENT placement = windowed;
			placement.flags = 0;
			placement.showCmd = KeepShowState(hwnd, placement.showCmd == SW_SHOWMAXIMIZED ? SW_SHOWMAXIMIZED : SW_SHOWNOACTIVATE);
			ApplyWindowPlacement(hwnd, placement);
			::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
						   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOOWNERZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
			SyncState(impl);
		}

	} // anonymous namespace

	Window* WindowFromHwnd(HWND hwnd) {
		if (!hwnd) {
			return nullptr;
		}
		for (Window* window : WindowInternal::GetState().windows) {
			const Window::Impl* impl = WindowInternalAccess::GetImpl(*window);
			if (impl && impl->hwnd == hwnd) {
				return window;
			}
		}
		return nullptr;
	}

	void ApplyCursorLock(Window::Impl& impl) {
		BackendState& backend = GetBackendState();
		if (backend.lockedWindow != &impl) {
			impl.haveCursorPosBeforeLock = ::GetCursorPos(&impl.cursorPosBeforeLock) != FALSE;
		}
		backend.lockedWindow = &impl;

		RECT client{};
		::GetClientRect(impl.hwnd, &client);
		POINT center{client.right / 2, client.bottom / 2};
		::ClientToScreen(impl.hwnd, &center);
		RECT clip{center.x, center.y, center.x + 1, center.y + 1};
		::ClipCursor(&clip);
		::SetCursor(nullptr);

		RAWINPUTDEVICE device{};
		device.usUsagePage = 0x01;
		device.usUsage = 0x02;
		device.hwndTarget = impl.hwnd;
		::RegisterRawInputDevices(&device, 1, sizeof(device));
		InputInternal::SetRawDeltaActive(true);
	}

	void ReleaseCursorLock(Window::Impl& impl) {
		BackendState& backend = GetBackendState();
		if (backend.lockedWindow != &impl) {
			return;
		}
		backend.lockedWindow = nullptr;
		::ClipCursor(nullptr);
		if (impl.haveCursorPosBeforeLock) {
			::SetCursorPos(impl.cursorPosBeforeLock.x, impl.cursorPosBeforeLock.y);
			impl.haveCursorPosBeforeLock = false;
		}

		RAWINPUTDEVICE device{};
		device.usUsagePage = 0x01;
		device.usUsage = 0x02;
		device.dwFlags = RIDEV_REMOVE;
		device.hwndTarget = nullptr;
		::RegisterRawInputDevices(&device, 1, sizeof(device));
		InputInternal::SetRawDeltaActive(false);
	}

	void EndCursorWrap(Window::Impl& impl) {
		if (impl.wrapOffset.x == 0 && impl.wrapOffset.y == 0) {
			return;
		}
		POINT pt{};
		if (::GetCursorPos(&pt)) {
			InputInternal::SetMouseScreenPosition({pt.x, pt.y});
		}
		InputInternal::ShiftMouseOrigin(-impl.wrapOffset);
		impl.wrapOffset = {};
	}

	LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam) {
		if (msg == WM_NCCREATE) {
			auto* create = reinterpret_cast<CREATESTRUCTW*>(lParam);
			auto* impl = static_cast<Window::Impl*>(create->lpCreateParams);
			::SetWindowLongPtrW(hwnd, GWLP_USERDATA, reinterpret_cast<LONG_PTR>(impl));
			if (impl) {
				impl->hwnd = hwnd;
			}
			return ::DefWindowProcW(hwnd, msg, wParam, lParam);
		}

		auto* impl = reinterpret_cast<Window::Impl*>(::GetWindowLongPtrW(hwnd, GWLP_USERDATA));
		if (!impl) {
			return ::DefWindowProcW(hwnd, msg, wParam, lParam);
		}

		WindowInternal::SystemState& state = WindowInternal::GetState();
		EventBus& bus = EventBus::GetGlobal();

		if (!impl->destroying && HandleInputMessage(*impl, msg, wParam, lParam)) {
			return 0;
		}

		switch (msg) {
		case WM_CLOSE:
			if (CanPublish(*impl)) {
				WindowEvents::CloseRequested.Publish(bus, WindowCloseRequestedEvent{impl->self});
			}
			return 0;

		case WM_SIZE:
			if (!impl->destroying) {
				HandleSizeMessage(*impl, wParam, lParam);
			}
			return 0;

		case WM_MOVE: {
			if (impl->destroying || ::IsIconic(hwnd)) {
				return 0;
			}
			if (impl->inSizeMove) {
				impl->pendingMove = true;
				return 0;
			}
			RectI rect = WindowRectOf(hwnd);
			if (rect.position != impl->lastPosition) {
				impl->lastPosition = rect.position;
				if (CanPublish(*impl)) {
					WindowEvents::Moved.Publish(bus, WindowMovedEvent{impl->self, rect.position});
				}
			}
			return 0;
		}

		case WM_ENTERSIZEMOVE:
			impl->inSizeMove = true;
			impl->pendingDragMove = false;
			::SetTimer(hwnd, MODAL_TICK_TIMER_ID, USER_TIMER_MINIMUM, nullptr);
			if (CanPublish(*impl)) {
				WindowEvents::DragBegan.Publish(bus, WindowDragBeganEvent{impl->self, CursorScreenPos()});
			}
			return 0;

		case WM_EXITSIZEMOVE: {
			::KillTimer(hwnd, MODAL_TICK_TIMER_ID);
			impl->inSizeMove = false;
			impl->pendingLiveResize = false;
			impl->pendingMove = false;
			FlushPendingDragMove(*impl);
			if (CanPublish(*impl)) {
				WindowEvents::DragEnded.Publish(bus, WindowDragEndedEvent{impl->self, CursorScreenPos()});
				Vec2I size = ClientSizeOf(hwnd);
				if (size != impl->lastClientSize) {
					impl->lastClientSize = size;
					WindowEvents::Resized.Publish(bus, WindowResizedEvent{impl->self, size, false});
				}
				RectI rect = WindowRectOf(hwnd);
				if (rect.position != impl->lastPosition) {
					impl->lastPosition = rect.position;
					WindowEvents::Moved.Publish(bus, WindowMovedEvent{impl->self, rect.position});
				}
			}
			return 0;
		}

		case WM_MOVING:
			if (CanPublish(*impl)) {
				auto* rect = reinterpret_cast<RECT*>(lParam);
				impl->pendingDragCursor = CursorScreenPos();
				impl->pendingDragRect = RectI{rect->left, rect->top,
											  rect->right - rect->left, rect->bottom - rect->top};
				if (impl->inSizeMove) {
					impl->pendingDragMove = true;
				} else {
					impl->pendingDragMove = false;
					WindowEvents::DragMoved.Publish(
						bus, WindowDragMovedEvent{impl->self, impl->pendingDragCursor, impl->pendingDragRect});
				}
			}
			return TRUE;

		case WM_TIMER:
			if (wParam == MODAL_TICK_TIMER_ID) {
				FlushModalPending(*impl);
				WindowInternal::InvokeModalTick();
				return 0;
			}
			break;

		case WM_PAINT: {
			PAINTSTRUCT paint{};
			::BeginPaint(hwnd, &paint);
			::EndPaint(hwnd, &paint);
			if (impl->inSizeMove && !impl->destroying) {
				FlushModalPending(*impl);
				WindowInternal::InvokeModalTick();
			}
			return 0;
		}

		case WM_DPICHANGED: {
			impl->scale = static_cast<f32>(LOWORD(wParam)) / 96.0f;
			auto* suggested = reinterpret_cast<RECT*>(lParam);
			if (impl->fullscreen) {
				CoverMonitor(*impl, MonitorRectOf(::MonitorFromRect(suggested, MONITOR_DEFAULTTONEAREST)));
			} else {
				::SetWindowPos(hwnd, nullptr, suggested->left, suggested->top,
							   suggested->right - suggested->left, suggested->bottom - suggested->top,
							   SWP_NOZORDER | SWP_NOACTIVATE);
			}
			if (CanPublish(*impl)) {
				RectI rect{suggested->left, suggested->top,
						   suggested->right - suggested->left, suggested->bottom - suggested->top};
				WindowEvents::DPIChanged.Publish(bus, WindowDPIChangedEvent{impl->self, impl->scale, rect});
			}
			return 0;
		}

		case WM_DISPLAYCHANGE:
			state.monitorsDirty = true;
			if (impl->fullscreen && !::IsIconic(hwnd)) {
				CoverMonitor(*impl, MonitorRectOf(::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST)));
			}
			return 0;

		case WM_SETFOCUS:
			impl->focused = true;
			state.focused = impl->self;
			if (impl->cursorMode == CursorMode::Locked) {
				ApplyCursorLock(*impl);
			}
			if (CanPublish(*impl)) {
				WindowEvents::Focus.Publish(bus, WindowFocusEvent{impl->self, true});
			}
			return 0;

		case WM_KILLFOCUS:
			impl->focused = false;
			if (state.focused == impl->self) {
				state.focused = nullptr;
			}
			if (impl->cursorMode == CursorMode::Locked) {
				::ClipCursor(nullptr);
			}
			if (CanPublish(*impl)) {
				WindowEvents::Focus.Publish(bus, WindowFocusEvent{impl->self, false});
			}
			return 0;

		case WM_GETMINMAXINFO: {
			auto* info = reinterpret_cast<MINMAXINFO*>(lParam);
			Vec2I minOuter = OuterFromClient(hwnd, *impl, impl->minClientSize);
			info->ptMinTrackSize = {minOuter.x, minOuter.y};
			if (impl->maxClientSize.x > 0 && impl->maxClientSize.y > 0) {
				Vec2I maxOuter = OuterFromClient(hwnd, *impl, impl->maxClientSize);
				info->ptMaxTrackSize = {maxOuter.x, maxOuter.y};
			}
			MONITORINFO monitor{};
			monitor.cbSize = sizeof(monitor);
			if (::GetMonitorInfoW(::MonitorFromWindow(hwnd, MONITOR_DEFAULTTONEAREST), &monitor)) {
				int frame = FrameThickness(hwnd);
				info->ptMaxPosition = {monitor.rcWork.left - monitor.rcMonitor.left - frame,
									   monitor.rcWork.top - monitor.rcMonitor.top - frame};
				info->ptMaxSize = {monitor.rcWork.right - monitor.rcWork.left + frame * 2,
								   monitor.rcWork.bottom - monitor.rcWork.top + frame * 2};
			}
			return 0;
		}

		case WM_NCCALCSIZE:
			if (IsBorderless(*impl) && wParam == TRUE) {
				if (::IsZoomed(hwnd)) {
					auto* params = reinterpret_cast<NCCALCSIZE_PARAMS*>(lParam);
					int frame = FrameThickness(hwnd);
					::InflateRect(&params->rgrc[0], -frame, -frame);
				}
				return 0;
			}
			break;

		case WM_NCHITTEST: {
			Vec2I screenPt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
			const bool borderless = IsBorderless(*impl);
			LRESULT hit;
			if (borderless) {
				hit = HTCLIENT;
				if (HasFlag(impl->flags, WindowFlags::Resizable) && !::IsZoomed(hwnd) && !impl->fullscreen) {
					hit = BorderlessEdgeHitTest(hwnd, screenPt);
				}
			} else {
				hit = ::DefWindowProcW(hwnd, msg, wParam, lParam);
			}
			const bool edgeOverContent = borderless && IsResizeEdge(hit);
			if ((hit == HTCLIENT || edgeOverContent) && impl->hitTest) {
				if (Window* window = impl->self.Get()) {
					POINT client{screenPt.x, screenPt.y};
					::ScreenToClient(hwnd, &client);
					HitTestResult result = impl->hitTest(*window, Vec2I{client.x, client.y}, impl->hitTestUser);
					if (result == HitTestResult::Content) {
						return HTCLIENT;
					}
					if (edgeOverContent) {
						return hit;
					}
					if (result == HitTestResult::Client && borderless) {
						return HTCLIENT;
					}
					return MapHitTestResult(result);
				}
			}
			return hit;
		}

		case WM_NCLBUTTONDOWN:
			if (IsBorderless(*impl)) {
				switch (wParam) {
				case HTMINBUTTON:
					ApplyShowCommand(hwnd, SW_MINIMIZE);
					return 0;
				case HTMAXBUTTON:
					ApplyShowCommand(hwnd, ::IsZoomed(hwnd) ? SW_RESTORE : SW_MAXIMIZE);
					return 0;
				case HTCLOSE:
					::PostMessageW(hwnd, WM_CLOSE, 0, 0);
					return 0;
				default:
					break;
				}
			}
			break;

		case WM_SETCURSOR:
			if (LOWORD(lParam) == HTCLIENT) {
				if (impl->cursorMode == CursorMode::Hidden || impl->cursorMode == CursorMode::Locked) {
					::SetCursor(nullptr);
				} else {
					::SetCursor(GetBackendState().cursors[static_cast<size_t>(impl->cursor)]);
				}
				return TRUE;
			}
			break;

		case WM_DROPFILES: {
			auto drop = reinterpret_cast<HDROP>(wParam);
			POINT dropPoint{};
			::DragQueryPoint(drop, &dropPoint);
			UINT count = ::DragQueryFileW(drop, 0xFFFFFFFF, nullptr, 0);
			std::vector<str> paths;
			paths.reserve(count);
			for (UINT i = 0; i < count; ++i) {
				UINT length = ::DragQueryFileW(drop, i, nullptr, 0);
				std::wstring wide(length, L'\0');
				::DragQueryFileW(drop, i, wide.data(), length + 1);
				paths.push_back(StringUtil::WideToUTF8(wide));
			}
			::DragFinish(drop);
			if (CanPublish(*impl) && !paths.empty()) {
				WindowEvents::FilesDropped.Publish(bus, FilesDroppedEvent{impl->self, Vec2I{dropPoint.x, dropPoint.y}, std::move(paths)});
			}
			return 0;
		}

		case WM_ERASEBKGND:
			return 1;

		default:
			break;
		}

		return ::DefWindowProcW(hwnd, msg, wParam, lParam);
	}

} // namespace Shalltear::Win32

namespace Shalltear::WindowBackend {

	using namespace Shalltear::Win32;

	bool CreateNative(Window& window, const WindowDesc& desc) {
		BackendState& backend = GetBackendState();

		auto* impl = New<Window::Impl>(MemTag::Platform);
		impl->flags = desc.flags;
		impl->title = desc.title;
		impl->minClientSize = desc.minClientSize;
		impl->maxClientSize = desc.maxClientSize;
		impl->self = Handle<Window>(&window);
		WindowInternalAccess::GetImpl(window) = impl;

		HMONITOR monitor;
		if (desc.position.has_value()) {
			monitor = ::MonitorFromPoint(POINT{desc.position->x, desc.position->y}, MONITOR_DEFAULTTONEAREST);
		} else {
			monitor = ::MonitorFromPoint(POINT{0, 0}, MONITOR_DEFAULTTOPRIMARY);
		}
		UINT dpiX = 96;
		UINT dpiY = 96;
		::GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);
		impl->scale = static_cast<f32>(dpiX) / 96.0f;

		Vec2I clientSize = desc.clientSize;
		if (HasFlag(desc.flags, WindowFlags::ScaleToMonitor)) {
			clientSize = Vec2I(static_cast<i32>(clientSize.x * impl->scale + 0.5f),
							   static_cast<i32>(clientSize.y * impl->scale + 0.5f));
		}

		DWORD style = WS_OVERLAPPED | WS_CAPTION | WS_SYSMENU | WS_MINIMIZEBOX;
		if (HasFlag(desc.flags, WindowFlags::Resizable)) {
			style |= WS_THICKFRAME;
			if (!HasFlag(desc.flags, WindowFlags::NoMaximize)) {
				style |= WS_MAXIMIZEBOX;
			}
		}
		if (HasFlag(desc.flags, WindowFlags::Borderless)) {
			style |= WS_THICKFRAME;
		}
		DWORD exStyle = 0;
		if (HasFlag(desc.flags, WindowFlags::AlwaysOnTop)) {
			exStyle |= WS_EX_TOPMOST;
		}
		if (HasFlag(desc.flags, WindowFlags::NoTaskbarIcon)) {
			exStyle |= WS_EX_TOOLWINDOW;
		}

		RECT rect{0, 0, clientSize.x, clientSize.y};
		if (!HasFlag(desc.flags, WindowFlags::Borderless)) {
			::AdjustWindowRectExForDpi(&rect, style, FALSE, exStyle, dpiX);
		}
		int x = CW_USEDEFAULT;
		int y = CW_USEDEFAULT;
		if (desc.position.has_value()) {
			x = desc.position->x;
			y = desc.position->y;
		}

		HWND ownerHwnd = nullptr;
		if (Window* owner = desc.owner.Get()) {
			if (Window::Impl* ownerImpl = WindowInternalAccess::GetImpl(*owner)) {
				ownerHwnd = ownerImpl->hwnd;
			}
		}

		std::wstring wideTitle = StringUtil::UTF8ToWide(desc.title);
		HWND hwnd = ::CreateWindowExW(
			exStyle,
			MAKEINTATOM(backend.classAtom),
			wideTitle.c_str(),
			style,
			x, y,
			rect.right - rect.left, rect.bottom - rect.top,
			ownerHwnd,
			nullptr,
			backend.hinstance,
			impl);
		if (!hwnd) {
			SLT_LOG_ERROR("Platform", "CreateWindowExW failed (error {})", ::GetLastError());
			WindowInternalAccess::GetImpl(window) = nullptr;
			Delete(impl);
			return false;
		}

		impl->scale = static_cast<f32>(::GetDpiForWindow(hwnd)) / 96.0f;

		if (HasFlag(desc.flags, WindowFlags::Borderless)) {
			MARGINS margins{0, 0, 1, 0};
			::DwmExtendFrameIntoClientArea(hwnd, &margins);
			::SetWindowPos(hwnd, nullptr, 0, 0, 0, 0,
						   SWP_NOMOVE | SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE | SWP_FRAMECHANGED);
		}
		if (HasFlag(desc.flags, WindowFlags::AcceptFileDrops)) {
			::DragAcceptFiles(hwnd, TRUE);
		}

		impl->lastClientSize = ClientSizeOf(hwnd);
		impl->lastPosition = WindowRectOf(hwnd).position;
		impl->constructed = true;

		if (!HasFlag(desc.flags, WindowFlags::Hidden)) {
			ApplyShowCommand(hwnd, SW_SHOW);
		}
		return true;
	}

	void DestroyNative(Window& window) {
		Window::Impl* impl = WindowInternalAccess::GetImpl(window);
		if (!impl) {
			return;
		}
		impl->destroying = true;
		ReleaseCursorLock(*impl);
		EndCursorWrap(*impl);
		if (impl->hwnd) {
			::KillTimer(impl->hwnd, MODAL_TICK_TIMER_ID);
			::DestroyWindow(impl->hwnd);
		}
		WindowInternalAccess::GetImpl(window) = nullptr;
		Delete(impl);
	}

} // namespace Shalltear::WindowBackend

namespace Shalltear {

	using namespace Shalltear::Win32;

#define SLT_WINDOW_ASSERT_MAIN_THREAD() \
	SLT_CORE_ASSERT_MSG(WindowInternal::IsMainThread(), "Window mutators are main-thread-only")

	Vec2I Window::GetClientSize() const {
		return _impl ? ClientSizeOf(_impl->hwnd) : Vec2I{};
	}

	void Window::SetClientSize(Vec2I sizePx) {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (!_impl) {
			return;
		}
		Vec2I outer = OuterFromClient(_impl->hwnd, *_impl, sizePx);
		if (_impl->fullscreen) {
			RECT& normal = _impl->windowedPlacement.rcNormalPosition;
			normal.right = normal.left + outer.x;
			normal.bottom = normal.top + outer.y;
			return;
		}
		::SetWindowPos(_impl->hwnd, nullptr, 0, 0, outer.x, outer.y,
					   SWP_NOMOVE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	Vec2I Window::GetPosition() const {
		return _impl ? WindowRectOf(_impl->hwnd).position : Vec2I{};
	}

	void Window::SetPosition(Vec2I posPx) {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (!_impl) {
			return;
		}
		if (_impl->fullscreen) {
			RECT& normal = _impl->windowedPlacement.rcNormalPosition;
			const RECT moved = ToWorkspace(RECT{posPx.x, posPx.y, posPx.x + (normal.right - normal.left),
												posPx.y + (normal.bottom - normal.top)});
			normal = moved;
			if (!::IsIconic(_impl->hwnd)) {
				const RECT screen{posPx.x, posPx.y, posPx.x + 1, posPx.y + 1};
				CoverMonitor(*_impl, MonitorRectOf(::MonitorFromRect(&screen, MONITOR_DEFAULTTONEAREST)));
			}
			return;
		}
		::SetWindowPos(_impl->hwnd, nullptr, posPx.x, posPx.y, 0, 0,
					   SWP_NOSIZE | SWP_NOZORDER | SWP_NOACTIVATE);
	}

	RectI Window::GetRect() const {
		return _impl ? WindowRectOf(_impl->hwnd) : RectI{};
	}

	f32 Window::GetContentScale() const {
		return _impl ? _impl->scale : 1.0f;
	}

	Vec2I Window::ClientToScreen(Vec2I clientPx) const {
		if (!_impl) {
			return clientPx;
		}
		POINT pt{clientPx.x, clientPx.y};
		::ClientToScreen(_impl->hwnd, &pt);
		return {pt.x, pt.y};
	}

	Vec2I Window::ScreenToClient(Vec2I screenPx) const {
		if (!_impl) {
			return screenPx;
		}
		POINT pt{screenPx.x, screenPx.y};
		::ScreenToClient(_impl->hwnd, &pt);
		return {pt.x, pt.y};
	}

	const MonitorInfo& Window::GetMonitor() const {
		static const MonitorInfo fallback{};
		if (!_impl) {
			return fallback;
		}
		HMONITOR monitor = ::MonitorFromWindow(_impl->hwnd, MONITOR_DEFAULTTONEAREST);
		WindowInternal::SystemState& state = WindowInternal::GetState();
		const Vector<void*>& handles = GetBackendState().monitorHandles;
		for (size_t i = 0; i < handles.size() && i < state.monitors.size(); ++i) {
			if (handles[i] == monitor) {
				return state.monitors[i];
			}
		}
		return state.monitors.empty() ? fallback : state.monitors.front();
	}

	WindowPlacement Window::GetPlacement() const {
		WindowPlacement placement;
		if (!_impl) {
			return placement;
		}
		WINDOWPLACEMENT native{};
		native.length = sizeof(native);
		if (_impl->fullscreen) {
			native = _impl->windowedPlacement;
		} else if (!::GetWindowPlacement(_impl->hwnd, &native)) {
			placement.position = WindowRectOf(_impl->hwnd).position;
			placement.clientSize = ClientSizeOf(_impl->hwnd);
			return placement;
		}

		Vec2I origin = WorkspaceOrigin();
		const RECT& normal = native.rcNormalPosition;
		placement.position = {normal.left + origin.x, normal.top + origin.y};
		placement.clientSize = ClientFromOuter(_impl->hwnd, *_impl,
											   {normal.right - normal.left, normal.bottom - normal.top});
		switch (native.showCmd) {
		case SW_SHOWMAXIMIZED:
			placement.state = WindowState::Maximized;
			break;
		case SW_SHOWMINIMIZED:
		case SW_SHOWMINNOACTIVE:
		case SW_MINIMIZE:
			placement.state = WindowState::Minimized;
			break;
		default:
			placement.state = WindowState::Normal;
			break;
		}
		if (_impl->fullscreen) {
			placement.state = ::IsIconic(_impl->hwnd) ? WindowState::Minimized : WindowState::Fullscreen;
		}
		return placement;
	}

	void Window::SetPlacement(const WindowPlacement& placement) {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (!_impl) {
			return;
		}
		WINDOWPLACEMENT native{};
		native.length = sizeof(native);
		if (!::GetWindowPlacement(_impl->hwnd, &native)) {
			return;
		}

		Vec2I outer = OuterFromClient(_impl->hwnd, *_impl, placement.clientSize);
		Vec2I origin = WorkspaceOrigin();
		native.rcNormalPosition = RECT{placement.position.x - origin.x,
									   placement.position.y - origin.y,
									   placement.position.x - origin.x + outer.x,
									   placement.position.y - origin.y + outer.y};
		native.flags = 0;

		if (placement.state == WindowState::Fullscreen) {
			if (_impl->fullscreen) {
				SetPosition(placement.position);
				SetClientSize(placement.clientSize);
				return;
			}
			native.showCmd = ::IsWindowVisible(_impl->hwnd) ? SW_SHOWNOACTIVATE : SW_HIDE;
			ApplyWindowPlacement(_impl->hwnd, native);
			EnterFullscreen(*_impl);
			return;
		}
		if (_impl->fullscreen) {
			native.showCmd = placement.state == WindowState::Maximized ? SW_SHOWMAXIMIZED : SW_SHOWNORMAL;
			LeaveFullscreen(*_impl, native);
			if (placement.state == WindowState::Minimized && ::IsWindowVisible(_impl->hwnd)) {
				ApplyShowCommand(_impl->hwnd, SW_SHOWMINNOACTIVE);
			}
			return;
		}
		if (!::IsWindowVisible(_impl->hwnd)) {
			native.showCmd = SW_HIDE;
		} else {
			switch (placement.state) {
			case WindowState::Minimized:
				native.showCmd = SW_SHOWMINNOACTIVE;
				break;
			case WindowState::Maximized:
				native.showCmd = SW_SHOWMAXIMIZED;
				break;
			default:
				native.showCmd = SW_SHOWNORMAL;
				break;
			}
		}
		ApplyWindowPlacement(_impl->hwnd, native);
	}

	void Window::Show() {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (_impl) {
			ApplyShowCommand(_impl->hwnd, SW_SHOW);
		}
	}

	void Window::Hide() {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (_impl) {
			ApplyShowCommand(_impl->hwnd, SW_HIDE);
		}
	}

	void Window::Minimize() {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (_impl) {
			ApplyShowCommand(_impl->hwnd, SW_MINIMIZE);
		}
	}

	void Window::Maximize() {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (_impl) {
			if (_impl->fullscreen) {
				LeaveFullscreen(*_impl, _impl->windowedPlacement);
			}
			ApplyShowCommand(_impl->hwnd, SW_MAXIMIZE);
		}
	}

	void Window::Restore() {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (_impl) {
			ApplyShowCommand(_impl->hwnd, SW_RESTORE);
		}
	}

	void Window::Focus() {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (_impl) {
			::SetForegroundWindow(_impl->hwnd);
			::SetFocus(_impl->hwnd);
		}
	}

	bool Window::IsVisible() const {
		return _impl && ::IsWindowVisible(_impl->hwnd);
	}

	bool Window::IsFocused() const {
		return _impl && _impl->focused;
	}

	bool Window::IsMinimized() const {
		return _impl && ::IsIconic(_impl->hwnd);
	}

	bool Window::IsMaximized() const {
		return _impl && ::IsZoomed(_impl->hwnd);
	}

	void Window::SetTitle(std::string_view utf8) {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (!_impl) {
			return;
		}
		_impl->title = str(utf8);
		::SetWindowTextW(_impl->hwnd, StringUtil::UTF8ToWide(utf8).c_str());
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
		if (_impl->mouseInside && (_impl->cursorMode == CursorMode::Normal || _impl->cursorMode == CursorMode::Wrapped)) {
			::SetCursor(GetBackendState().cursors[static_cast<size_t>(cursor)]);
		}
	}

	SystemCursor Window::GetCursor() const {
		return _impl ? _impl->cursor : SystemCursor::Arrow;
	}

	void Window::SetCursorMode(CursorMode mode) {
		SLT_WINDOW_ASSERT_MAIN_THREAD();
		if (!_impl || _impl->cursorMode == mode) {
			return;
		}
		CursorMode previous = _impl->cursorMode;
		_impl->cursorMode = mode;
		if (previous == CursorMode::Locked) {
			ReleaseCursorLock(*_impl);
		}
		if (previous == CursorMode::Wrapped) {
			EndCursorWrap(*_impl);
		}
		if (mode == CursorMode::Locked) {
			ApplyCursorLock(*_impl);
		} else if (_impl->mouseInside) {
			::SetCursor(mode == CursorMode::Hidden
							? nullptr
							: GetBackendState().cursors[static_cast<size_t>(_impl->cursor)]);
		}
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
		if (!_impl) {
			return {};
		}
		return {_impl->hwnd, GetBackendState().hinstance};
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

#endif
