#pragma once

#include "Platform/Linux/Window/LinuxDisplayServers.h"

#include "Shalltear/Core/Memory.h"

#include "Platform/Linux/Window/X11/X11API.h"

namespace Shalltear::Linux {

	struct X11Window {
		::Window handle = 0;
		XIC inputContext = nullptr;

		Vec2I clientSize;
		// root coordinates
		Vec2I clientOrigin;
		Vec2I frameLeftTop;
		Vec2I frameRightBottom;

		bool shown = false;
		bool mapped = false;
		bool iconic = false;
		bool maximized = false;
		bool fullscreen = false;

		Vec2I normalPosition;
		Vec2I normalSize;
		u32 configures = 0;

		Vec2I wrapOffset;
		Vec2I lastClientPointer;

		bool moving = false;
		Vec2I lastDragCursor;
		unsigned long lastCaptionPressTime = 0;
		Vec2I lastCaptionPressPos;
		bool swallowRelease[10] = {};
		bool keyDown[256] = {};
		int shownCursor = -2;

		::Window dropSource = 0;
		bool dropAccepts = false;
		Vec2I dropClientPos;
	};

	struct X11Atoms {
		Atom wmProtocols, wmDeleteWindow, wmState, wmChangeState;
		Atom netWmPing, netWmName, netWmPid, netWmIcon, netWmState, netWmStateMaximizedVert, netWmStateMaximizedHorz;
		Atom netWmStateFullscreen, netWmStateHidden, netWmStateAbove, netWmStateSkipTaskbar;
		Atom netActiveWindow, netFrameExtents, netWmMoveResize, netWorkArea, netClientListStacking;
		Atom netSupportingWmCheck;
		Atom motifWmHints, gtkShowWindowMenu, resourceManager, utf8String;
		Atom clipboard, targets, incr, selectionProperty, textPlainUTF8;
		Atom xdndAware, xdndEnter, xdndPosition, xdndStatus, xdndLeave, xdndDrop, xdndFinished;
		Atom xdndSelection, xdndActionCopy, xdndTypeList, textUriList;
	};

	class X11Server final : public DisplayServer {
	public:
		X11API x;
		Display* display = nullptr;
		int screen = 0;
		::Window root = 0;
		::Window helper = 0;
		X11Atoms atoms{};
		XIM inputMethod = nullptr;

		int xiOpcode = 0;
		bool haveRawMotion = false;
		int randrEventBase = 0;
		bool haveRandr = false;

		f32 scale = 1.0f;
		Cursor cursors[static_cast<size_t>(SystemCursor::Count)] = {};
		Cursor blankCursor = 0;

		Window::Impl* lockedWindow = nullptr;
		Vec2I cursorBeforeLock;
		Window::Impl* focusLost = nullptr;
		bool hasWindowManager = false;

		str clipboardText;
		bool ownsClipboard = false;

		struct MonitorModes {
			u64 monitorId = 0;
			Vector<WindowInternal::DisplayMode> modes{GetMemoryResource(MemTag::Platform)};
		};
		Vector<MonitorModes> monitorModes{GetMemoryResource(MemTag::Platform)};

		bool Connect(str& outWhy);
		~X11Server() override;

		const char* GetName() const override { return "X11"; }

		bool CreateWindow(Window::Impl& impl, const WindowDesc& desc) override;
		void DestroyWindow(Window::Impl& impl) override;

		void Pump() override;
		void Wait(f64 timeoutSeconds) override;

		void EnumerateMonitors(Vector<MonitorInfo>& out) override;
		void EnumerateDisplayModes(u64 monitorId, Vector<WindowInternal::DisplayMode>& out) override;
		Handle<Window> WindowAtPoint(Vec2I screenPx, Handle<Window> ignore) override;

		bool SetClipboardText(std::string_view utf8) override;
		str GetClipboardText() override;

		Vec2I GetClientSize(const Window::Impl& impl) override;
		void SetClientSize(Window::Impl& impl, Vec2I sizePx) override;
		Vec2I GetPosition(const Window::Impl& impl) override;
		void SetPosition(Window::Impl& impl, Vec2I posPx) override;
		RectI GetRect(const Window::Impl& impl) override;
		Vec2I ClientToScreen(const Window::Impl& impl, Vec2I clientPx) override;
		Vec2I ScreenToClient(const Window::Impl& impl, Vec2I screenPx) override;
		const MonitorInfo* GetMonitor(const Window::Impl& impl) override;

		WindowPlacement GetPlacement(const Window::Impl& impl) override;
		void SetPlacement(Window::Impl& impl, const WindowPlacement& placement) override;

		void Show(Window::Impl& impl) override;
		void Hide(Window::Impl& impl) override;
		void Minimize(Window::Impl& impl) override;
		void Maximize(Window::Impl& impl) override;
		void Restore(Window::Impl& impl) override;
		void Focus(Window::Impl& impl) override;
		bool IsVisible(const Window::Impl& impl) override;
		bool IsMinimized(const Window::Impl& impl) override;
		bool IsMaximized(const Window::Impl& impl) override;

		void SetTitle(Window::Impl& impl) override;
		void SetIcon(Window::Impl& impl, std::span<const WindowIconImage> images) override;
		void SetCursor(Window::Impl& impl) override;
		void SetCursorMode(Window::Impl& impl, CursorMode previous) override;

		NativeWindowHandle GetNativeHandle(const Window::Impl& impl) override;
		NativeWindowHandle GetPresentationProbe() override;

		static X11Window& Native(const Window::Impl& impl) { return *static_cast<X11Window*>(impl.native); }
		Window::Impl* FindImpl(::Window handle);

		void HandleEvent(XEvent& event);
		void HandleKey(Window::Impl& impl, XKeyEvent& event);
		void HandleButton(Window::Impl& impl, const XButtonEvent& event);
		void HandleMotion(Window::Impl& impl, const XMotionEvent& event);
		void HandleConfigure(Window::Impl& impl, const XConfigureEvent& event);
		void HandleClientMessage(Window::Impl& impl, const XClientMessageEvent& event);
		void HandleSelectionRequest(const XSelectionRequestEvent& event);
		void HandleDropSelection(Window::Impl& impl, const XSelectionEvent& event);
		void HandleRootProperty(Atom property);
		void SettleFocus();
		void PollMoves();

		// structure events handled until done() or a short wait
		template <typename TDone>
		void Settle(TDone&& done);
		void ApplyGeometry(Window::Impl& impl, Vec2I position, Vec2I clientSize);
		void MoveResize(Window::Impl& impl, Vec2I position, Vec2I clientSize);
		void ApplyUnmappedState(Window::Impl& impl);

		HitTestResult ResolveHit(Window::Impl& impl, Vec2I clientPx);
		bool HandleNonClientPress(Window::Impl& impl, HitTestResult hit, const XButtonEvent& event);
		void BeginMoveResize(Window::Impl& impl, int direction, const XButtonEvent& event);
		void ApplyHoverCursor(Window::Impl& impl, Vec2I clientPx);

		void RefreshState(Window::Impl& impl);
		void RefreshFrameExtents(Window::Impl& impl);
		void SetNetState(Window::Impl& impl, bool add, Atom first, Atom second);
		void SendRootMessage(::Window window, Atom type, long l0, long l1, long l2, long l3, long l4);
		Vec2I QueryPointer(unsigned int* outMask = nullptr);

		void ApplyCursorLock(Window::Impl& impl);
		void ReleaseCursorLock(Window::Impl& impl);
		void EndCursorWrap(Window::Impl& impl);
		void WrapCursorAtScreenEdge(Window::Impl& impl, Vec2I screenPx);

		f32 ReadScale();
		bool ReadProperty(::Window window, Atom property, Atom type, Vector<u8>& out, Atom* outType = nullptr);
	};

} // namespace Shalltear::Linux
