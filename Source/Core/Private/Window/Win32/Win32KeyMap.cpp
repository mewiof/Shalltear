#include "Shalltear/Core/Platform.h"

#ifdef SLT_PLATFORM_WINDOWS

#include "Window/Win32/Win32Internal.h"

#include "Shalltear/Core/Input/InputEvent.h"

#include "Input/InputInternal.h"
#include "Window/CursorWrap.h"

namespace Shalltear::Win32 {

	u16 ExtractScancode(LPARAM lParam) {
		u16 scancode = static_cast<u16>((lParam >> 16) & 0xFF);
		if ((lParam >> 24) & 1) {
			scancode |= 0xE000;
		}
		return scancode;
	}

	Key TranslateVirtualKey(WPARAM wParam, LPARAM lParam) {
		const bool extended = ((lParam >> 24) & 1) != 0;

		if (wParam >= 'A' && wParam <= 'Z') {
			return static_cast<Key>(static_cast<u16>(Key::A) + static_cast<u16>(wParam - 'A'));
		}
		if (wParam >= '0' && wParam <= '9') {
			return static_cast<Key>(static_cast<u16>(Key::D0) + static_cast<u16>(wParam - '0'));
		}
		if (wParam >= VK_F1 && wParam <= VK_F24) {
			return static_cast<Key>(static_cast<u16>(Key::F1) + static_cast<u16>(wParam - VK_F1));
		}
		if (wParam >= VK_NUMPAD0 && wParam <= VK_NUMPAD9) {
			return static_cast<Key>(static_cast<u16>(Key::Kp0) + static_cast<u16>(wParam - VK_NUMPAD0));
		}

		switch (wParam) {
		case VK_SHIFT: {
			UINT vk = ::MapVirtualKeyW((lParam >> 16) & 0xFF, MAPVK_VSC_TO_VK_EX);
			return vk == VK_RSHIFT ? Key::RightShift : Key::LeftShift;
		}
		case VK_CONTROL:
			return extended ? Key::RightCtrl : Key::LeftCtrl;
		case VK_MENU:
			return extended ? Key::RightAlt : Key::LeftAlt;
		case VK_LWIN:
			return Key::LeftSuper;
		case VK_RWIN:
			return Key::RightSuper;

		case VK_RETURN:
			return extended ? Key::KpEnter : Key::Enter;

		case VK_DECIMAL:
			return Key::KpDecimal;
		case VK_DIVIDE:
			return Key::KpDivide;
		case VK_MULTIPLY:
			return Key::KpMultiply;
		case VK_SUBTRACT:
			return Key::KpSubtract;
		case VK_ADD:
			return Key::KpAdd;
		case VK_OEM_NEC_EQUAL:
			return Key::KpEqual;

		case VK_ESCAPE:
			return Key::Escape;
		case VK_TAB:
			return Key::Tab;
		case VK_BACK:
			return Key::Backspace;
		case VK_INSERT:
			return Key::Insert;
		case VK_DELETE:
			return Key::Delete;
		case VK_HOME:
			return Key::Home;
		case VK_END:
			return Key::End;
		case VK_PRIOR:
			return Key::PageUp;
		case VK_NEXT:
			return Key::PageDown;
		case VK_LEFT:
			return Key::Left;
		case VK_RIGHT:
			return Key::Right;
		case VK_UP:
			return Key::Up;
		case VK_DOWN:
			return Key::Down;

		case VK_SPACE:
			return Key::Space;
		case VK_CAPITAL:
			return Key::CapsLock;
		case VK_SCROLL:
			return Key::ScrollLock;
		case VK_NUMLOCK:
			return Key::NumLock;
		case VK_SNAPSHOT:
			return Key::PrintScreen;
		case VK_PAUSE:
			return Key::Pause;
		case VK_APPS:
			return Key::Menu;

		case VK_OEM_7:
			return Key::Apostrophe;
		case VK_OEM_COMMA:
			return Key::Comma;
		case VK_OEM_MINUS:
			return Key::Minus;
		case VK_OEM_PERIOD:
			return Key::Period;
		case VK_OEM_2:
			return Key::Slash;
		case VK_OEM_1:
			return Key::Semicolon;
		case VK_OEM_PLUS:
			return Key::Equal;
		case VK_OEM_4:
			return Key::LeftBracket;
		case VK_OEM_5:
			return Key::Backslash;
		case VK_OEM_6:
			return Key::RightBracket;
		case VK_OEM_3:
			return Key::GraveAccent;

		default:
			return Key::Unknown;
		}
	}

	namespace {

		Vec2I ClientPosFromLParam(LPARAM lParam) {
			return {GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
		}

		Vec2I LastClientMousePos(const Window::Impl& impl) {
			POINT pt{};
			::GetCursorPos(&pt);
			::ScreenToClient(impl.hwnd, &pt);
			return {pt.x, pt.y};
		}

		bool IsOwnWindow(HWND hwnd) {
			if (!hwnd) {
				return false;
			}
			for (Window* window : WindowInternal::GetState().windows) {
				if (window && static_cast<HWND>(window->GetNativeHandle().hwnd) == hwnd) {
					return true;
				}
			}
			return false;
		}

		InputEvent MakeEvent(const Window::Impl& impl, InputEventType type) {
			InputEvent event;
			event.type = type;
			event.window = impl.self;
			event.mods = InputInternal::CurrentMods();
			return event;
		}

		void TrackMouseLeave(Window::Impl& impl) {
			TRACKMOUSEEVENT track{};
			track.cbSize = sizeof(track);
			track.dwFlags = TME_LEAVE;
			track.hwndTrack = impl.hwnd;
			::TrackMouseEvent(&track);
		}

		Vec2I WrapAdjusted(const Window::Impl& impl, Vec2I posPx) {
			return impl.cursorMode == CursorMode::Wrapped ? posPx + impl.wrapOffset : posPx;
		}

		void WrapCursorAtScreenEdge(Window::Impl& impl, Vec2I screenPx) {
			if (!impl.focused) {
				return;
			}
			MONITORINFO info{};
			info.cbSize = sizeof(info);
			HMONITOR monitor = ::MonitorFromPoint(POINT{screenPx.x, screenPx.y}, MONITOR_DEFAULTTONEAREST);
			if (!monitor || !::GetMonitorInfoW(monitor, &info)) {
				return;
			}
			RectI bounds{info.rcMonitor.left, info.rcMonitor.top,
						 info.rcMonitor.right - info.rcMonitor.left,
						 info.rcMonitor.bottom - info.rcMonitor.top};
			Vec2I landing = WindowInternal::CursorWrapLanding(screenPx, bounds);
			if (landing.x == screenPx.x && landing.y == screenPx.y) {
				return;
			}
			impl.wrapOffset += screenPx - landing;
			::SetCursorPos(landing.x, landing.y);
		}

		void HandleMouseButton(Window::Impl& impl, MouseButton button, bool down, Vec2I clientPos) {
			u8 mask = static_cast<u8>(1u << static_cast<u8>(button));
			if (down) {
				if (impl.buttonsDown == 0) {
					::SetCapture(impl.hwnd);
				}
				impl.buttonsDown |= mask;
			} else {
				impl.buttonsDown &= static_cast<u8>(~mask);
				if (impl.buttonsDown == 0 && ::GetCapture() == impl.hwnd) {
					::ReleaseCapture();
				}
			}

			InputEvent event = MakeEvent(impl, down ? InputEventType::MouseDown : InputEventType::MouseUp);
			event.button = button;
			event.mousePosPx = WrapAdjusted(impl, clientPos);
			InputInternal::PushEvent(event);
		}

	} // anonymous namespace

	bool HandleInputMessage(Window::Impl& impl, UINT msg, WPARAM wParam, LPARAM lParam) {
		switch (msg) {

		case WM_KEYDOWN:
		case WM_SYSKEYDOWN:
		case WM_KEYUP:
		case WM_SYSKEYUP: {
			const bool down = (msg == WM_KEYDOWN || msg == WM_SYSKEYDOWN);
			InputEvent event = MakeEvent(impl, down ? InputEventType::KeyDown : InputEventType::KeyUp);
			event.key = TranslateVirtualKey(wParam, lParam);
			event.scancode = ExtractScancode(lParam);
			event.repeat = down && ((lParam >> 30) & 1) != 0;
			event.mousePosPx = LastClientMousePos(impl);
			InputInternal::PushEvent(event);
			return msg == WM_KEYDOWN || msg == WM_KEYUP;
		}

		case WM_CHAR: {
			char32_t codepoint = 0;
			wchar_t unit = static_cast<wchar_t>(wParam);
			if (unit >= 0xD800 && unit <= 0xDBFF) {
				impl.pendingHighSurrogate = unit;
				return true;
			}
			if (unit >= 0xDC00 && unit <= 0xDFFF) {
				if (impl.pendingHighSurrogate == 0) {
					return true;
				}
				codepoint = 0x10000 +
							((static_cast<char32_t>(impl.pendingHighSurrogate) - 0xD800) << 10) +
							(static_cast<char32_t>(unit) - 0xDC00);
				impl.pendingHighSurrogate = 0;
			} else {
				impl.pendingHighSurrogate = 0;
				codepoint = static_cast<char32_t>(unit);
			}
			if (codepoint < 0x20 || codepoint == 0x7F) {
				return true;
			}
			KeyMod mods = InputInternal::CurrentMods();
			if (HasFlag(mods, KeyMod::Ctrl) && !HasFlag(mods, KeyMod::Alt)) {
				return true;
			}
			InputEvent event = MakeEvent(impl, InputEventType::Text);
			event.codepoint = codepoint;
			InputInternal::PushEvent(event);
			return true;
		}

		case WM_SYSCHAR:
			return true;

		case WM_MOUSEMOVE: {
			Vec2I clientPos = ClientPosFromLParam(lParam);

			if (!impl.mouseInside) {
				impl.mouseInside = true;
				TrackMouseLeave(impl);
				InputEvent enter = MakeEvent(impl, InputEventType::MouseEnter);
				enter.mousePosPx = WrapAdjusted(impl, clientPos);
				InputInternal::PushEvent(enter);
			}

			POINT screen{clientPos.x, clientPos.y};
			::ClientToScreen(impl.hwnd, &screen);
			Vec2I screenPos{screen.x, screen.y};
			InputInternal::SetMouseScreenPosition(WrapAdjusted(impl, screenPos));

			if (impl.cursorMode != CursorMode::Locked) {
				InputEvent event = MakeEvent(impl, InputEventType::MouseMove);
				event.mousePosPx = WrapAdjusted(impl, clientPos);
				InputInternal::PushEvent(event);
			}
			if (impl.cursorMode == CursorMode::Wrapped) {
				WrapCursorAtScreenEdge(impl, screenPos);
			}
			return true;
		}

		case WM_MOUSELEAVE: {
			impl.mouseInside = false;
			InputEvent event = MakeEvent(impl, InputEventType::MouseLeave);
			event.mousePosPx = WrapAdjusted(impl, LastClientMousePos(impl));
			InputInternal::PushEvent(event);
			return true;
		}

		case WM_LBUTTONDOWN:
			HandleMouseButton(impl, MouseButton::Left, true, ClientPosFromLParam(lParam));
			return true;
		case WM_LBUTTONUP:
			HandleMouseButton(impl, MouseButton::Left, false, ClientPosFromLParam(lParam));
			return true;
		case WM_RBUTTONDOWN:
			HandleMouseButton(impl, MouseButton::Right, true, ClientPosFromLParam(lParam));
			return true;
		case WM_RBUTTONUP:
			HandleMouseButton(impl, MouseButton::Right, false, ClientPosFromLParam(lParam));
			return true;
		case WM_MBUTTONDOWN:
			HandleMouseButton(impl, MouseButton::Middle, true, ClientPosFromLParam(lParam));
			return true;
		case WM_MBUTTONUP:
			HandleMouseButton(impl, MouseButton::Middle, false, ClientPosFromLParam(lParam));
			return true;
		case WM_XBUTTONDOWN:
		case WM_XBUTTONUP: {
			MouseButton button = (GET_XBUTTON_WPARAM(wParam) == XBUTTON1) ? MouseButton::X1 : MouseButton::X2;
			HandleMouseButton(impl, button, msg == WM_XBUTTONDOWN, ClientPosFromLParam(lParam));
			return true;
		}

		case WM_MOUSEWHEEL:
		case WM_MOUSEHWHEEL: {
			POINT pt{GET_X_LPARAM(lParam), GET_Y_LPARAM(lParam)};
			::ScreenToClient(impl.hwnd, &pt);
			f32 notches = static_cast<f32>(GET_WHEEL_DELTA_WPARAM(wParam)) / static_cast<f32>(WHEEL_DELTA);
			InputEvent event = MakeEvent(impl, InputEventType::MouseWheel);
			event.mousePosPx = WrapAdjusted(impl, Vec2I{pt.x, pt.y});
			event.wheel = (msg == WM_MOUSEWHEEL) ? Vec2(0.0f, notches) : Vec2(notches, 0.0f);
			InputInternal::PushEvent(event);
			return true;
		}

		case WM_INPUT: {
			if (GetBackendState().lockedWindow != &impl) {
				return false;
			}
			RAWINPUT raw{};
			UINT size = sizeof(raw);
			if (::GetRawInputData(reinterpret_cast<HRAWINPUT>(lParam), RID_INPUT, &raw, &size, sizeof(RAWINPUTHEADER)) != static_cast<UINT>(-1) &&
				raw.header.dwType == RIM_TYPEMOUSE &&
				(raw.data.mouse.usFlags & MOUSE_MOVE_ABSOLUTE) == 0) {
				InputInternal::AddRawMouseDelta(Vec2(static_cast<f32>(raw.data.mouse.lLastX),
													 static_cast<f32>(raw.data.mouse.lLastY)));
			}
			return false;
		}

		case WM_KILLFOCUS:
			if (!IsOwnWindow(reinterpret_cast<HWND>(wParam))) {
				InputInternal::ReleaseHeldInput(impl.self, WrapAdjusted(impl, LastClientMousePos(impl)));
			}
			return false;

		default:
			return false;
		}
	}

} // namespace Shalltear::Win32

#endif
