#include "Shalltear/Core/Platform.h"

#ifdef SLT_PLATFORM_WINDOWS

#include "Window/Win32/Win32Internal.h"

#include <shellscalingapi.h>

#include "Shalltear/Core/Assert.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/StringUtil.h"

#include <algorithm>
#include <cstring>

namespace Shalltear::Win32 {

	namespace {
		constexpr const wchar_t* WINDOW_CLASS_NAME = L"ShalltearWindow";
	}

	BackendState& GetBackendState() {
		static BackendState state;
		return state;
	}

} // namespace Shalltear::Win32

namespace Shalltear::WindowBackend {

	using namespace Shalltear::Win32;

	void Initialize() {
		BackendState& backend = GetBackendState();
		backend.hinstance = ::GetModuleHandleW(nullptr);

		::SetProcessDpiAwarenessContext(DPI_AWARENESS_CONTEXT_PER_MONITOR_AWARE_V2);

		WNDCLASSEXW windowClass{};
		windowClass.cbSize = sizeof(windowClass);
		windowClass.lpfnWndProc = WndProc;
		windowClass.hInstance = backend.hinstance;
		windowClass.hCursor = ::LoadCursorW(nullptr, IDC_ARROW);
		windowClass.hbrBackground = static_cast<HBRUSH>(::GetStockObject(BLACK_BRUSH));
		windowClass.lpszClassName = WINDOW_CLASS_NAME;
		backend.classAtom = ::RegisterClassExW(&windowClass);
		SLT_CORE_ASSERT_MSG(backend.classAtom != 0, "RegisterClassExW failed");

		// system cursors are shared, never destroy
		auto load = [](const wchar_t* id) { return ::LoadCursorW(nullptr, id); };
		HCURSOR* cursors = backend.cursors;
		cursors[static_cast<size_t>(SystemCursor::Arrow)] = load(IDC_ARROW);
		cursors[static_cast<size_t>(SystemCursor::IBeam)] = load(IDC_IBEAM);
		cursors[static_cast<size_t>(SystemCursor::Crosshair)] = load(IDC_CROSS);
		cursors[static_cast<size_t>(SystemCursor::Hand)] = load(IDC_HAND);
		cursors[static_cast<size_t>(SystemCursor::ResizeEW)] = load(IDC_SIZEWE);
		cursors[static_cast<size_t>(SystemCursor::ResizeNS)] = load(IDC_SIZENS);
		cursors[static_cast<size_t>(SystemCursor::ResizeNWSE)] = load(IDC_SIZENWSE);
		cursors[static_cast<size_t>(SystemCursor::ResizeNESW)] = load(IDC_SIZENESW);
		cursors[static_cast<size_t>(SystemCursor::ResizeAll)] = load(IDC_SIZEALL);
		cursors[static_cast<size_t>(SystemCursor::NotAllowed)] = load(IDC_NO);
	}

	void Terminate() {
		BackendState& backend = GetBackendState();
		if (backend.classAtom != 0) {
			::UnregisterClassW(MAKEINTATOM(backend.classAtom), backend.hinstance);
			backend.classAtom = 0;
		}
		backend.monitorHandles.clear();
		backend.monitorHandles.shrink_to_fit();
		backend.lockedWindow = nullptr;
		if (backend.waitTimer) {
			::CloseHandle(backend.waitTimer);
			backend.waitTimer = nullptr;
		}
		backend.waitTimerTried = false;
	}

	void Pump() {
		MSG msg;
		while (::PeekMessageW(&msg, nullptr, 0, 0, PM_REMOVE)) {
			if (msg.message == WM_QUIT) {
				continue;
			}
			::TranslateMessage(&msg);
			::DispatchMessageW(&msg);
		}
	}

	void Wait(f64 timeoutSeconds) {
		if (timeoutSeconds < 0.0) {
			::MsgWaitForMultipleObjectsEx(0, nullptr, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
			return;
		}

		BackendState& backend = GetBackendState();
		if (!backend.waitTimerTried) {
			backend.waitTimerTried = true;
			backend.waitTimer = ::CreateWaitableTimerExW(nullptr, nullptr, CREATE_WAITABLE_TIMER_HIGH_RESOLUTION,
														 TIMER_ALL_ACCESS);
		}
		if (backend.waitTimer) {
			LARGE_INTEGER due;
			due.QuadPart = -static_cast<LONGLONG>(timeoutSeconds * 1.0e7);
			if (::SetWaitableTimer(backend.waitTimer, &due, 0, nullptr, nullptr, FALSE)) {
				HANDLE timer = backend.waitTimer;
				::MsgWaitForMultipleObjectsEx(1, &timer, INFINITE, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
				return;
			}
		}
		DWORD milliseconds = static_cast<DWORD>(timeoutSeconds * 1000.0);
		::MsgWaitForMultipleObjectsEx(0, nullptr, milliseconds, QS_ALLINPUT, MWMO_INPUTAVAILABLE);
	}

	namespace {

		struct MonitorEnumContext {
			Vector<MonitorInfo>* monitors;
			Vector<void*>* handles;
		};

		BOOL CALLBACK MonitorEnumProc(HMONITOR monitor, HDC, LPRECT, LPARAM param) {
			auto* context = reinterpret_cast<MonitorEnumContext*>(param);

			MONITORINFOEXW info{};
			info.cbSize = sizeof(info);
			if (!::GetMonitorInfoW(monitor, &info)) {
				return TRUE;
			}

			UINT dpiX = 96;
			UINT dpiY = 96;
			::GetDpiForMonitor(monitor, MDT_EFFECTIVE_DPI, &dpiX, &dpiY);

			MonitorInfo result;
			str devicePath = StringUtil::WideToUTF8(info.szDevice);
			result.id = StringHash(devicePath).GetValue();

			DISPLAY_DEVICEW device{};
			device.cb = sizeof(device);
			if (::EnumDisplayDevicesW(info.szDevice, 0, &device, 0)) {
				result.name = StringUtil::WideToUTF8(device.DeviceString);
			} else {
				result.name = devicePath;
			}

			result.bounds = RectI(info.rcMonitor.left, info.rcMonitor.top,
								  info.rcMonitor.right - info.rcMonitor.left,
								  info.rcMonitor.bottom - info.rcMonitor.top);
			result.workArea = RectI(info.rcWork.left, info.rcWork.top,
									info.rcWork.right - info.rcWork.left,
									info.rcWork.bottom - info.rcWork.top);
			result.scale = static_cast<f32>(dpiX) / 96.0f;
			result.primary = (info.dwFlags & MONITORINFOF_PRIMARY) != 0;

			DEVMODEW mode{};
			mode.dmSize = sizeof(mode);
			if (::EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &mode) && mode.dmDisplayFrequency > 1) {
				result.refreshRate = static_cast<f32>(mode.dmDisplayFrequency);
			}

			context->monitors->push_back(std::move(result));
			context->handles->push_back(monitor);
			return TRUE;
		}

	} // anonymous namespace

	void EnumerateMonitors(Vector<MonitorInfo>& outMonitors) {
		BackendState& backend = GetBackendState();
		outMonitors.clear();
		backend.monitorHandles.clear();
		MonitorEnumContext context{&outMonitors, &backend.monitorHandles};
		::EnumDisplayMonitors(nullptr, nullptr, MonitorEnumProc, reinterpret_cast<LPARAM>(&context));
	}

	void EnumerateDisplayModes(u64 monitorId, Vector<WindowInternal::DisplayMode>& outModes) {
		using WindowInternal::DisplayMode;
		const Vector<MonitorInfo>& monitors = WindowInternal::GetState().monitors;
		const Vector<void*>& handles = GetBackendState().monitorHandles;
		HMONITOR monitor = nullptr;
		for (size_t i = 0; i < monitors.size() && i < handles.size(); ++i) {
			if (monitors[i].id == monitorId) {
				monitor = static_cast<HMONITOR>(handles[i]);
			}
		}
		MONITORINFOEXW info{};
		info.cbSize = sizeof(info);
		if (!monitor || !::GetMonitorInfoW(monitor, &info)) {
			return;
		}
		DEVMODEW current{};
		current.dmSize = sizeof(current);
		if (!::EnumDisplaySettingsW(info.szDevice, ENUM_CURRENT_SETTINGS, &current)) {
			return;
		}
		DEVMODEW mode{};
		mode.dmSize = sizeof(mode);
		for (DWORD index = 0; ::EnumDisplaySettingsW(info.szDevice, index, &mode); ++index) {
			if (mode.dmBitsPerPel != current.dmBitsPerPel ||
				((mode.dmFields & DM_DISPLAYORIENTATION) && mode.dmDisplayOrientation != current.dmDisplayOrientation)) {
				continue;
			}
			DisplayMode entry;
			entry.sizePx = {static_cast<i32>(mode.dmPelsWidth), static_cast<i32>(mode.dmPelsHeight)};
			entry.refreshRate = mode.dmDisplayFrequency > 1 ? static_cast<f32>(mode.dmDisplayFrequency) : 0.0f;
			const bool seen = std::any_of(outModes.begin(), outModes.end(), [&](const DisplayMode& other) {
				return other.sizePx == entry.sizePx && other.refreshRate == entry.refreshRate;
			});
			if (!seen) {
				outModes.push_back(entry);
			}
		}
		std::sort(outModes.begin(), outModes.end(), [](const DisplayMode& a, const DisplayMode& b) {
			if (a.sizePx.x != b.sizePx.x) {
				return a.sizePx.x > b.sizePx.x;
			}
			if (a.sizePx.y != b.sizePx.y) {
				return a.sizePx.y > b.sizePx.y;
			}
			return a.refreshRate > b.refreshRate;
		});
	}

	Handle<Window> WindowAtPoint(Vec2I screenPx, Handle<Window> ignore) {
		POINT pt{screenPx.x, screenPx.y};
		for (HWND hwnd = ::GetTopWindow(nullptr); hwnd; hwnd = ::GetWindow(hwnd, GW_HWNDNEXT)) {
			Window* window = WindowFromHwnd(hwnd);
			if (!window) {
				continue;
			}
			Handle<Window> handle(window);
			if (handle == ignore) {
				continue;
			}
			if (!::IsWindowVisible(hwnd) || ::IsIconic(hwnd)) {
				continue;
			}
			RECT rect{};
			::GetWindowRect(hwnd, &rect);
			if (::PtInRect(&rect, pt)) {
				return handle;
			}
		}
		return {};
	}

	namespace {

		bool OpenClipboardWithRetry() {
			for (int attempt = 0; attempt < 5; ++attempt) {
				if (::OpenClipboard(nullptr)) {
					return true;
				}
				::Sleep(5);
			}
			return false;
		}

	} // anonymous namespace

	bool SetClipboardText(std::string_view utf8) {
		std::wstring wide = StringUtil::UTF8ToWide(utf8);
		if (!OpenClipboardWithRetry()) {
			return false;
		}
		bool ok = false;
		if (::EmptyClipboard()) {
			size_t bytes = (wide.size() + 1) * sizeof(wchar_t);
			if (HGLOBAL global = ::GlobalAlloc(GMEM_MOVEABLE, bytes)) {
				if (void* memory = ::GlobalLock(global)) {
					std::memcpy(memory, wide.c_str(), bytes);
					::GlobalUnlock(global);
					// on success the system owns the allocation
					ok = ::SetClipboardData(CF_UNICODETEXT, global) != nullptr;
				}
				if (!ok) {
					::GlobalFree(global);
				}
			}
		}
		::CloseClipboard();
		return ok;
	}

	str GetClipboardText() {
		if (!::IsClipboardFormatAvailable(CF_UNICODETEXT)) {
			return {};
		}
		if (!OpenClipboardWithRetry()) {
			return {};
		}
		str result;
		if (HANDLE data = ::GetClipboardData(CF_UNICODETEXT)) {
			if (const wchar_t* wide = static_cast<const wchar_t*>(::GlobalLock(data))) {
				result = StringUtil::WideToUTF8(wide);
				::GlobalUnlock(data);
			}
		}
		::CloseClipboard();
		return result;
	}

} // namespace Shalltear::WindowBackend

#endif
