#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Input/InputCodes.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/Window/Window.h"

#include "Window/WindowInternal.h"

#include <string_view>

namespace Shalltear {

	struct Window::Impl {
		Handle<Window> self;
		WindowFlags flags = WindowFlags::None;
		str title;
		Vec2I minClientSize;
		Vec2I maxClientSize;
		f32 scale = 1.0f;
		SystemCursor cursor = SystemCursor::Arrow;
		CursorMode cursorMode = CursorMode::Normal;
		Window::HitTestFn hitTest = nullptr;
		void* hitTestUser = nullptr;
		bool vsyncHint = true;

		bool constructed = false;
		bool destroying = false;

		Vec2I renderSize{};

		bool focused = false;
		bool mouseInside = false;
		WindowState stateCache = WindowState::Normal;

		// owned by window's display server
		void* native = nullptr;
	};

	namespace Linux {

		class DisplayServer {
		public:
			virtual ~DisplayServer() = default;

			virtual const char* GetName() const = 0;

			virtual bool CreateWindow(Window::Impl& impl, const WindowDesc& desc) = 0;
			virtual void DestroyWindow(Window::Impl& impl) = 0;

			virtual void Pump() = 0;
			virtual void Wait(f64 timeoutSeconds) = 0;

			virtual void EnumerateMonitors(Vector<MonitorInfo>& out) = 0;
			virtual void EnumerateDisplayModes(u64 monitorId, Vector<WindowInternal::DisplayMode>& out) = 0;
			virtual Handle<Window> WindowAtPoint(Vec2I screenPx, Handle<Window> ignore) = 0;

			virtual bool SetClipboardText(std::string_view utf8) = 0;
			virtual str GetClipboardText() = 0;

			virtual Vec2I GetClientSize(const Window::Impl& impl) = 0;
			virtual void SetClientSize(Window::Impl& impl, Vec2I sizePx) = 0;
			virtual Vec2I GetPosition(const Window::Impl& impl) = 0;
			virtual void SetPosition(Window::Impl& impl, Vec2I posPx) = 0;
			virtual RectI GetRect(const Window::Impl& impl) = 0;
			virtual Vec2I ClientToScreen(const Window::Impl& impl, Vec2I clientPx) = 0;
			virtual Vec2I ScreenToClient(const Window::Impl& impl, Vec2I screenPx) = 0;
			virtual const MonitorInfo* GetMonitor(const Window::Impl& impl) = 0;

			virtual WindowPlacement GetPlacement(const Window::Impl& impl) = 0;
			virtual void SetPlacement(Window::Impl& impl, const WindowPlacement& placement) = 0;

			virtual void Show(Window::Impl& impl) = 0;
			virtual void Hide(Window::Impl& impl) = 0;
			virtual void Minimize(Window::Impl& impl) = 0;
			virtual void Maximize(Window::Impl& impl) = 0;
			virtual void Restore(Window::Impl& impl) = 0;
			virtual void Focus(Window::Impl& impl) = 0;
			virtual bool IsVisible(const Window::Impl& impl) = 0;
			virtual bool IsMinimized(const Window::Impl& impl) = 0;
			virtual bool IsMaximized(const Window::Impl& impl) = 0;

			virtual void SetTitle(Window::Impl& impl) = 0;
			virtual void SetIcon(Window::Impl& impl, std::span<const WindowIconImage> images) = 0;
			virtual void SetCursor(Window::Impl& impl) = 0;
			virtual void SetCursorMode(Window::Impl& impl, CursorMode previous) = 0;

			virtual NativeWindowHandle GetNativeHandle(const Window::Impl& impl) = 0;
			// asked by Vulkan before a window exists
			virtual NativeWindowHandle GetPresentationProbe() = 0;
		};

		// null when no display server is reachable
		DisplayServer* GetDisplayServer();

		Window* FindWindow(const Window::Impl* impl);

		bool CanPublish(const Window::Impl& impl);

		// layout's symbol first (like Windows virtual keys), position as fallback
		Key KeyFromKeysym(u32 keysym);
		Key KeyFromScancode(u32 evdevScancode);

	} // namespace Linux

} // namespace Shalltear
