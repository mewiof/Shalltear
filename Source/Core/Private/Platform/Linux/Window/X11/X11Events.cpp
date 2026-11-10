#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Window/WindowEvents.h"

#include "Input/InputInternal.h"
#include "Window/CursorWrap.h"

#include <cmath>
#include <cstring>

#include "Platform/Linux/Window/X11/X11Server.h"

namespace Shalltear::Linux {

	namespace {

		constexpr long MOVERESIZE_MOVE = 8;
		constexpr unsigned long DOUBLE_CLICK_MS = 400;
		constexpr i32 DOUBLE_CLICK_SLOP_PX = 4;
		constexpr i32 RESIZE_BORDER_DIP = 8;

		InputEvent MakeEvent(const Window::Impl& impl, InputEventType type) {
			InputEvent event;
			event.type = type;
			event.window = impl.self;
			event.mods = InputInternal::CurrentMods();
			return event;
		}

		Vec2I WrapAdjusted(const Window::Impl& impl, const X11Window& w, Vec2I posPx) {
			return impl.cursorMode == CursorMode::Wrapped ? posPx + w.wrapOffset : posPx;
		}

		bool IsResizeEdge(HitTestResult hit) {
			return hit >= HitTestResult::Left && hit <= HitTestResult::BottomRight;
		}

		// _NET_WM_MOVERESIZE directions
		long ResizeDirection(HitTestResult hit) {
			switch (hit) {
			case HitTestResult::TopLeft:
				return 0;
			case HitTestResult::Top:
				return 1;
			case HitTestResult::TopRight:
				return 2;
			case HitTestResult::Right:
				return 3;
			case HitTestResult::BottomRight:
				return 4;
			case HitTestResult::Bottom:
				return 5;
			case HitTestResult::BottomLeft:
				return 6;
			default:
				return 7;
			}
		}

		SystemCursor EdgeCursor(HitTestResult hit) {
			switch (hit) {
			case HitTestResult::Left:
			case HitTestResult::Right:
				return SystemCursor::ResizeEW;
			case HitTestResult::Top:
			case HitTestResult::Bottom:
				return SystemCursor::ResizeNS;
			case HitTestResult::TopLeft:
			case HitTestResult::BottomRight:
				return SystemCursor::ResizeNWSE;
			default:
				return SystemCursor::ResizeNESW;
			}
		}

		bool IsKeypadKeysym(KeySym keysym) {
			return keysym >= 0xff80 && keysym <= 0xffbd;
		}

		// 0 at end or on bad lead byte
		size_t DecodeUTF8(const char* text, size_t length, char32_t& out) {
			if (length == 0) {
				return 0;
			}
			const u8 lead = static_cast<u8>(text[0]);
			size_t extra = lead < 0x80 ? 0 : (lead & 0xE0) == 0xC0 ? 1
										 : (lead & 0xF0) == 0xE0   ? 2
										 : (lead & 0xF8) == 0xF0   ? 3
																   : 4;
			if (extra == 4 || extra >= length) {
				return extra == 0 ? (out = lead, 1) : 0;
			}
			char32_t cp = extra == 0 ? lead : extra == 1 ? (lead & 0x1F)
										  : extra == 2	 ? (lead & 0x0F)
														 : (lead & 0x07);
			for (size_t i = 1; i <= extra; ++i) {
				cp = (cp << 6) | (static_cast<u8>(text[i]) & 0x3F);
			}
			out = cp;
			return extra + 1;
		}

	} // anonymous namespace

	HitTestResult X11Server::ResolveHit(Window::Impl& impl, Vec2I clientPx) {
		X11Window& w = Native(impl);
		const bool borderless = HasFlag(impl.flags, WindowFlags::Borderless);
		HitTestResult hit = HitTestResult::Client;
		if (borderless && HasFlag(impl.flags, WindowFlags::Resizable) && !w.maximized && !w.fullscreen) {
			const i32 frame = static_cast<i32>(std::lround(RESIZE_BORDER_DIP * impl.scale));
			const bool left = clientPx.x < frame;
			const bool right = clientPx.x >= w.clientSize.x - frame;
			const bool top = clientPx.y < frame;
			const bool bottom = clientPx.y >= w.clientSize.y - frame;
			hit = top && left		? HitTestResult::TopLeft
				  : top && right	? HitTestResult::TopRight
				  : bottom && left	? HitTestResult::BottomLeft
				  : bottom && right ? HitTestResult::BottomRight
				  : left			? HitTestResult::Left
				  : right			? HitTestResult::Right
				  : top				? HitTestResult::Top
				  : bottom			? HitTestResult::Bottom
									: HitTestResult::Client;
		}
		const bool edgeOverContent = borderless && IsResizeEdge(hit);
		if ((hit == HitTestResult::Client || edgeOverContent) && impl.hitTest) {
			if (Window* window = impl.self.Get()) {
				const HitTestResult result = impl.hitTest(*window, clientPx, impl.hitTestUser);
				if (result == HitTestResult::Content) {
					return HitTestResult::Client;
				}
				if (edgeOverContent) {
					return hit;
				}
				return result;
			}
		}
		return hit;
	}

	void X11Server::BeginMoveResize(Window::Impl& impl, int direction, const XButtonEvent& event) {
		X11Window& w = Native(impl);
		// release press's implicit grab first (window manager needs the pointer)
		x.XUngrabPointer(display, event.time);
		SendRootMessage(w.handle, atoms.netWmMoveResize, event.x_root, event.y_root, direction, Button1, 1);
		if (direction != MOVERESIZE_MOVE) {
			return;
		}
		w.moving = true;
		w.lastDragCursor = {event.x_root, event.y_root};
		if (CanPublish(impl)) {
			WindowEvents::DragBegan.Publish(EventBus::GetGlobal(), WindowDragBeganEvent{impl.self, w.lastDragCursor});
		}
	}

	bool X11Server::HandleNonClientPress(Window::Impl& impl, HitTestResult hit, const XButtonEvent& event) {
		X11Window& w = Native(impl);
		switch (hit) {
		case HitTestResult::Caption: {
			const Vec2I at{event.x_root, event.y_root};
			const bool repeated = event.time - w.lastCaptionPressTime <= DOUBLE_CLICK_MS &&
								  std::abs(at.x - w.lastCaptionPressPos.x) <= DOUBLE_CLICK_SLOP_PX &&
								  std::abs(at.y - w.lastCaptionPressPos.y) <= DOUBLE_CLICK_SLOP_PX;
			w.lastCaptionPressTime = repeated ? 0 : event.time;
			w.lastCaptionPressPos = at;
			if (repeated && HasFlag(impl.flags, WindowFlags::Resizable) && !HasFlag(impl.flags, WindowFlags::NoMaximize)) {
				if (w.maximized) {
					Restore(impl);
				} else {
					Maximize(impl);
				}
			} else if (!repeated) {
				BeginMoveResize(impl, MOVERESIZE_MOVE, event);
			}
			return true;
		}
		case HitTestResult::MinimizeButton:
			Minimize(impl);
			return true;
		case HitTestResult::MaximizeButton:
			if (w.maximized) {
				Restore(impl);
			} else {
				Maximize(impl);
			}
			return true;
		case HitTestResult::CloseButton:
			if (CanPublish(impl)) {
				WindowEvents::CloseRequested.Publish(EventBus::GetGlobal(), WindowCloseRequestedEvent{impl.self});
			}
			return true;
		default:
			if (IsResizeEdge(hit)) {
				BeginMoveResize(impl, static_cast<int>(ResizeDirection(hit)), event);
				return true;
			}
			return false;
		}
	}

	void X11Server::ApplyHoverCursor(Window::Impl& impl, Vec2I clientPx) {
		X11Window& w = Native(impl);
		int wanted = static_cast<int>(impl.cursor);
		if (impl.cursorMode == CursorMode::Hidden || impl.cursorMode == CursorMode::Locked) {
			wanted = -1;
		} else if (HasFlag(impl.flags, WindowFlags::Borderless)) {
			const HitTestResult hit = ResolveHit(impl, clientPx);
			if (IsResizeEdge(hit)) {
				wanted = static_cast<int>(EdgeCursor(hit));
			}
		}
		if (wanted == w.shownCursor) {
			return;
		}
		w.shownCursor = wanted;
		x.XDefineCursor(display, w.handle, wanted < 0 ? blankCursor : cursors[wanted]);
	}

	void X11Server::SetCursor(Window::Impl& impl) {
		ApplyHoverCursor(impl, Native(impl).lastClientPointer);
	}

	void X11Server::ApplyCursorLock(Window::Impl& impl) {
		X11Window& w = Native(impl);
		if (lockedWindow != &impl) {
			cursorBeforeLock = QueryPointer();
		}
		lockedWindow = &impl;
		x.XGrabPointer(display, w.handle, True, ButtonPressMask | ButtonReleaseMask | PointerMotionMask, GrabModeAsync, GrabModeAsync,
					   w.handle, blankCursor, CurrentTime);
		if (haveRawMotion) {
			unsigned char bits[XIMaskLen(XI_RawMotion)] = {};
			XISetMask(bits, XI_RawMotion);
			XIEventMask mask{XIAllMasterDevices, static_cast<int>(sizeof(bits)), bits};
			x.XISelectEvents(display, root, &mask, 1);
		}
		InputInternal::SetRawDeltaActive(true);
	}

	void X11Server::ReleaseCursorLock(Window::Impl& impl) {
		if (lockedWindow != &impl) {
			return;
		}
		lockedWindow = nullptr;
		x.XUngrabPointer(display, CurrentTime);
		x.XWarpPointer(display, X11_NONE, root, 0, 0, 0, 0, cursorBeforeLock.x, cursorBeforeLock.y);
		if (haveRawMotion) {
			unsigned char bits[XIMaskLen(XI_RawMotion)] = {};
			XIEventMask mask{XIAllMasterDevices, static_cast<int>(sizeof(bits)), bits};
			x.XISelectEvents(display, root, &mask, 1);
		}
		InputInternal::SetRawDeltaActive(false);
	}

	void X11Server::EndCursorWrap(Window::Impl& impl) {
		X11Window& w = Native(impl);
		if (w.wrapOffset.x == 0 && w.wrapOffset.y == 0) {
			return;
		}
		InputInternal::SetMouseScreenPosition(QueryPointer());
		InputInternal::ShiftMouseOrigin(-w.wrapOffset);
		w.wrapOffset = {};
	}

	void X11Server::WrapCursorAtScreenEdge(Window::Impl& impl, Vec2I screenPx) {
		if (!impl.focused) {
			return;
		}
		const MonitorInfo* monitor = nullptr;
		for (const MonitorInfo& candidate : WindowInternal::GetState().monitors) {
			const RectI& bounds = candidate.bounds;
			if (screenPx.x >= bounds.position.x && screenPx.x < bounds.position.x + bounds.size.x &&
				screenPx.y >= bounds.position.y && screenPx.y < bounds.position.y + bounds.size.y) {
				monitor = &candidate;
			}
		}
		if (!monitor) {
			return;
		}
		const Vec2I landing = WindowInternal::CursorWrapLanding(screenPx, monitor->bounds);
		if (landing.x == screenPx.x && landing.y == screenPx.y) {
			return;
		}
		Native(impl).wrapOffset += screenPx - landing;
		x.XWarpPointer(display, X11_NONE, root, 0, 0, 0, 0, landing.x, landing.y);
	}

	void X11Server::SetCursorMode(Window::Impl& impl, CursorMode previous) {
		if (previous == CursorMode::Locked) {
			ReleaseCursorLock(impl);
		}
		if (previous == CursorMode::Wrapped) {
			EndCursorWrap(impl);
		}
		if (impl.cursorMode == CursorMode::Locked) {
			ApplyCursorLock(impl);
		}
		ApplyHoverCursor(impl, Native(impl).lastClientPointer);
	}

	void X11Server::HandleKey(Window::Impl& impl, XKeyEvent& event) {
		X11Window& w = Native(impl);
		const bool down = event.type == KeyPress;
		const unsigned int keycode = event.keycode & 0xFF;

		const unsigned int group = XkbGroupForCoreState(event.state);
		const KeySym base = x.XkbKeycodeToKeysym(display, static_cast<KeyCode>(keycode), group, 0);
		KeySym effective = 0;
		unsigned int consumed = 0;
		x.XkbLookupKeySym(display, static_cast<KeyCode>(keycode), event.state, &consumed, &effective);
		Key key = KeyFromKeysym(static_cast<u32>(IsKeypadKeysym(effective) ? effective : base));
		if (key == Key::Unknown) {
			// layout without Latin letters still has key's position
			key = KeyFromScancode(keycode - 8);
		}

		// text committed by input method = KeyPress with keycode 0
		if (keycode != 0) {
			InputEvent input = MakeEvent(impl, down ? InputEventType::KeyDown : InputEventType::KeyUp);
			input.key = key;
			input.scancode = static_cast<u16>(keycode - 8);
			input.repeat = down && w.keyDown[keycode];
			input.mousePosPx = WrapAdjusted(impl, w, w.lastClientPointer);
			w.keyDown[keycode] = down;
			InputInternal::PushEvent(input);
		}

		if (!down) {
			return;
		}
		char text[64];
		int length = 0;
		if (w.inputContext) {
			int status = 0;
			KeySym ignored = 0;
			length = x.Xutf8LookupString(w.inputContext, &event, text, sizeof(text), &ignored, &status);
			if (status == XBufferOverflow) {
				length = 0;
			}
		} else {
			length = x.XLookupString(&event, text, sizeof(text), nullptr, nullptr);
		}
		const KeyMod mods = InputInternal::CurrentMods();
		if (HasFlag(mods, KeyMod::Ctrl) && !HasFlag(mods, KeyMod::Alt)) {
			return;
		}
		for (size_t at = 0; at < static_cast<size_t>(length);) {
			char32_t codepoint = 0;
			const size_t read = DecodeUTF8(text + at, static_cast<size_t>(length) - at, codepoint);
			if (read == 0) {
				break;
			}
			at += read;
			if (codepoint < 0x20 || codepoint == 0x7F) {
				continue;
			}
			InputEvent typed = MakeEvent(impl, InputEventType::Text);
			typed.codepoint = codepoint;
			InputInternal::PushEvent(typed);
		}
	}

	void X11Server::HandleButton(Window::Impl& impl, const XButtonEvent& event) {
		X11Window& w = Native(impl);
		const bool down = event.type == ButtonPress;
		const Vec2I clientPos{event.x, event.y};
		w.lastClientPointer = clientPos;

		if (event.button >= 4 && event.button <= 7) {
			if (down) {
				InputEvent wheel = MakeEvent(impl, InputEventType::MouseWheel);
				wheel.mousePosPx = WrapAdjusted(impl, w, clientPos);
				wheel.wheel = event.button == 4	  ? Vec2(0.0f, 1.0f)
							  : event.button == 5 ? Vec2(0.0f, -1.0f)
							  : event.button == 6 ? Vec2(-1.0f, 0.0f)
												  : Vec2(1.0f, 0.0f);
				InputInternal::PushEvent(wheel);
			}
			return;
		}

		MouseButton button;
		switch (event.button) {
		case Button1:
			button = MouseButton::Left;
			break;
		case Button2:
			button = MouseButton::Middle;
			break;
		case Button3:
			button = MouseButton::Right;
			break;
		case 8:
			button = MouseButton::X1;
			break;
		case 9:
			button = MouseButton::X2;
			break;
		default:
			return;
		}

		if (!down && w.swallowRelease[event.button]) {
			w.swallowRelease[event.button] = false;
			return;
		}
		if (down && impl.cursorMode != CursorMode::Locked && (event.button == Button1 || event.button == Button3)) {
			const HitTestResult hit = ResolveHit(impl, clientPos);
			if (event.button == Button1 && HandleNonClientPress(impl, hit, event)) {
				w.swallowRelease[Button1] = true;
				return;
			}
			if (event.button == Button3 && hit == HitTestResult::Caption) {
				x.XUngrabPointer(display, event.time);
				SendRootMessage(w.handle, atoms.gtkShowWindowMenu, 0, event.x_root, event.y_root, 0, 0);
				w.swallowRelease[Button3] = true;
				return;
			}
		}

		InputEvent input = MakeEvent(impl, down ? InputEventType::MouseDown : InputEventType::MouseUp);
		input.button = button;
		input.mousePosPx = WrapAdjusted(impl, w, clientPos);
		InputInternal::PushEvent(input);
	}

	void X11Server::HandleMotion(Window::Impl& impl, const XMotionEvent& event) {
		X11Window& w = Native(impl);
		const Vec2I clientPos{event.x, event.y};
		const Vec2I screenPos{event.x_root, event.y_root};
		w.lastClientPointer = clientPos;

		if (!impl.mouseInside) {
			impl.mouseInside = true;
			InputEvent enter = MakeEvent(impl, InputEventType::MouseEnter);
			enter.mousePosPx = WrapAdjusted(impl, w, clientPos);
			InputInternal::PushEvent(enter);
		}
		InputInternal::SetMouseScreenPosition(WrapAdjusted(impl, w, screenPos));
		if (impl.cursorMode != CursorMode::Locked) {
			InputEvent move = MakeEvent(impl, InputEventType::MouseMove);
			move.mousePosPx = WrapAdjusted(impl, w, clientPos);
			InputInternal::PushEvent(move);
		}
		if (impl.cursorMode == CursorMode::Wrapped) {
			WrapCursorAtScreenEdge(impl, screenPos);
		}
		ApplyHoverCursor(impl, clientPos);
	}

	void X11Server::HandleConfigure(Window::Impl& impl, const XConfigureEvent& event) {
		X11Window& w = Native(impl);
		++w.configures;
		Vec2I origin{event.x, event.y};
		// real events relative to WM's frame
		if (!event.send_event) {
			::Window child = 0;
			x.XTranslateCoordinates(display, w.handle, root, 0, 0, &origin.x, &origin.y, &child);
		}
		const Vec2I size{event.width, event.height};
		const bool resized = size != w.clientSize;
		const bool moved = origin != w.clientOrigin;
		// maximized/fullscreen state may be announced after its resize
		if (resized && !event.send_event && !w.maximized && !w.fullscreen) {
			RefreshState(impl);
		}
		w.clientSize = size;
		w.clientOrigin = origin;
		if (!w.maximized && !w.fullscreen && !w.iconic) {
			w.normalSize = size;
			w.normalPosition = origin - w.frameLeftTop;
		}
		if (!CanPublish(impl)) {
			return;
		}
		EventBus& bus = EventBus::GetGlobal();
		if (resized) {
			WindowEvents::Resized.Publish(bus, WindowResizedEvent{impl.self, size, false});
		}
		if (moved && CanPublish(impl)) {
			WindowEvents::Moved.Publish(bus, WindowMovedEvent{impl.self, GetPosition(impl)});
		}
	}

	void X11Server::PollMoves() {
		for (Window* window : WindowInternal::GetState().windows) {
			Window::Impl* impl = WindowInternalAccess::GetImpl(*window);
			if (!impl || !impl->native || !Native(*impl).moving) {
				continue;
			}
			X11Window& w = Native(*impl);
			unsigned int mask = 0;
			const Vec2I cursor = QueryPointer(&mask);
			EventBus& bus = EventBus::GetGlobal();
			if ((mask & Button1Mask) == 0) {
				w.moving = false;
				if (CanPublish(*impl)) {
					WindowEvents::DragEnded.Publish(bus, WindowDragEndedEvent{impl->self, cursor});
				}
				continue;
			}
			if (cursor != w.lastDragCursor) {
				w.lastDragCursor = cursor;
				if (CanPublish(*impl)) {
					WindowEvents::DragMoved.Publish(bus, WindowDragMovedEvent{impl->self, cursor, GetRect(*impl)});
				}
			}
		}
	}

	void X11Server::HandleRootProperty(Atom property) {
		if (property == atoms.netSupportingWmCheck) {
			Vector<u8> owner{GetMemoryResource(MemTag::Platform)};
			hasWindowManager = ReadProperty(root, atoms.netSupportingWmCheck, XA_WINDOW, owner) && !owner.empty();
			return;
		}
		if (property != atoms.resourceManager) {
			return;
		}
		const f32 fresh = ReadScale();
		if (fresh == scale) {
			return;
		}
		scale = fresh;
		WindowInternal::GetState().monitorsDirty = true;
		for (Window* window : WindowInternal::GetState().windows) {
			Window::Impl* impl = WindowInternalAccess::GetImpl(*window);
			if (!impl || !impl->native) {
				continue;
			}
			impl->scale = scale;
			if (CanPublish(*impl)) {
				WindowEvents::DPIChanged.Publish(EventBus::GetGlobal(), WindowDPIChangedEvent{impl->self, scale, GetRect(*impl)});
			}
		}
	}

	void X11Server::SettleFocus() {
		if (!focusLost) {
			return;
		}
		Window::Impl& lost = *focusLost;
		focusLost = nullptr;
		::Window focused = 0;
		int revert = 0;
		x.XGetInputFocus(display, &focused, &revert);
		if (!FindImpl(focused)) {
			const X11Window& w = Native(lost);
			InputInternal::ReleaseHeldInput(lost.self, WrapAdjusted(lost, w, w.lastClientPointer));
		}
	}

	void X11Server::HandleEvent(XEvent& event) {
		WindowInternal::SystemState& state = WindowInternal::GetState();

		if (event.type == GenericEvent && haveRawMotion && event.xcookie.extension == xiOpcode) {
			if (x.XGetEventData(display, &event.xcookie)) {
				if (event.xcookie.evtype == XI_RawMotion && lockedWindow) {
					const auto* raw = static_cast<const XIRawEvent*>(event.xcookie.data);
					const double* value = raw->raw_values;
					Vec2 delta{0.0f, 0.0f};
					for (int axis = 0; axis < raw->valuators.mask_len * 8; ++axis) {
						if (!XIMaskIsSet(raw->valuators.mask, axis)) {
							continue;
						}
						if (axis == 0) {
							delta.x = static_cast<f32>(*value);
						} else if (axis == 1) {
							delta.y = static_cast<f32>(*value);
						}
						++value;
					}
					InputInternal::AddRawMouseDelta(delta);
				}
				x.XFreeEventData(display, &event.xcookie);
			}
			return;
		}
		if (haveRandr && event.type == randrEventBase + RRScreenChangeNotify) {
			x.XRRUpdateConfiguration(&event);
			state.monitorsDirty = true;
			return;
		}
		if (x.XFilterEvent(&event, X11_NONE)) {
			return;
		}

		if (event.type == SelectionRequest) {
			HandleSelectionRequest(event.xselectionrequest);
			return;
		}
		if (event.type == SelectionClear) {
			if (event.xselectionclear.selection == atoms.clipboard) {
				ownsClipboard = false;
				clipboardText.clear();
			}
			return;
		}
		if (event.type == PropertyNotify && event.xproperty.window == root) {
			HandleRootProperty(event.xproperty.atom);
			return;
		}
		if (event.type == MappingNotify) {
			x.XRefreshKeyboardMapping(&event.xmapping);
			return;
		}

		Window::Impl* impl = FindImpl(event.xany.window);
		if (!impl || impl->destroying) {
			return;
		}
		X11Window& w = Native(*impl);
		EventBus& bus = EventBus::GetGlobal();

		switch (event.type) {
		case KeyPress:
		case KeyRelease:
			HandleKey(*impl, event.xkey);
			break;

		case ButtonPress:
		case ButtonRelease:
			HandleButton(*impl, event.xbutton);
			break;

		case MotionNotify:
			HandleMotion(*impl, event.xmotion);
			break;

		case EnterNotify:
			if (!impl->mouseInside && event.xcrossing.mode == NotifyNormal) {
				impl->mouseInside = true;
				w.lastClientPointer = {event.xcrossing.x, event.xcrossing.y};
				InputEvent enter = MakeEvent(*impl, InputEventType::MouseEnter);
				enter.mousePosPx = WrapAdjusted(*impl, w, w.lastClientPointer);
				InputInternal::PushEvent(enter);
				ApplyHoverCursor(*impl, w.lastClientPointer);
			}
			break;

		case LeaveNotify:
			if (impl->mouseInside && event.xcrossing.mode != NotifyGrab) {
				impl->mouseInside = false;
				InputEvent leave = MakeEvent(*impl, InputEventType::MouseLeave);
				leave.mousePosPx = WrapAdjusted(*impl, w, w.lastClientPointer);
				InputInternal::PushEvent(leave);
			}
			break;

		case FocusIn:
			if (event.xfocus.mode == NotifyGrab || event.xfocus.mode == NotifyUngrab) {
				break;
			}
			impl->focused = true;
			state.focused = impl->self;
			focusLost = nullptr;
			if (w.inputContext) {
				x.XSetICFocus(w.inputContext);
			}
			if (impl->cursorMode == CursorMode::Locked) {
				ApplyCursorLock(*impl);
			}
			if (CanPublish(*impl)) {
				WindowEvents::Focus.Publish(bus, WindowFocusEvent{impl->self, true});
			}
			break;

		case FocusOut: {
			if (event.xfocus.mode == NotifyGrab || event.xfocus.mode == NotifyUngrab) {
				break;
			}
			impl->focused = false;
			if (state.focused == impl->self) {
				state.focused = nullptr;
			}
			if (w.inputContext) {
				x.XUnsetICFocus(w.inputContext);
			}
			if (impl->cursorMode == CursorMode::Locked && lockedWindow == impl) {
				x.XUngrabPointer(display, CurrentTime);
			}
			std::memset(w.keyDown, 0, sizeof(w.keyDown));
			focusLost = impl;
			if (CanPublish(*impl)) {
				WindowEvents::Focus.Publish(bus, WindowFocusEvent{impl->self, false});
			}
			break;
		}

		case ConfigureNotify:
			HandleConfigure(*impl, event.xconfigure);
			break;

		case MapNotify:
			w.mapped = true;
			w.iconic = false;
			RefreshFrameExtents(*impl);
			RefreshState(*impl);
			break;

		case UnmapNotify:
			w.mapped = false;
			// iconified window is unmapped too (still counts as shown)
			if (w.shown) {
				w.iconic = true;
			}
			RefreshState(*impl);
			break;

		case PropertyNotify:
			if (event.xproperty.atom == atoms.netWmState || event.xproperty.atom == atoms.wmState) {
				RefreshState(*impl);
			} else if (event.xproperty.atom == atoms.netFrameExtents) {
				RefreshFrameExtents(*impl);
			}
			break;

		case ClientMessage:
			HandleClientMessage(*impl, event.xclient);
			break;

		case SelectionNotify:
			if (event.xselection.selection == atoms.xdndSelection) {
				HandleDropSelection(*impl, event.xselection);
			}
			break;

		default:
			break;
		}
	}

} // namespace Shalltear::Linux
