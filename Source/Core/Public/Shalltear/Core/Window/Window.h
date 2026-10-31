#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/EnumFlags.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/Window/Monitor.h"

#include <optional>
#include <string_view>

namespace Shalltear {

	class Window;

	enum class WindowFlags : u32 {
		None = 0,
		Resizable = 1 << 0,
		Borderless = 1 << 1,
		Hidden = 1 << 2,
		AlwaysOnTop = 1 << 3,
		NoTaskbarIcon = 1 << 4,
		AcceptFileDrops = 1 << 5,
		ScaleToMonitor = 1 << 6, // clientSize in logical units
		NoMaximize = 1 << 7,
	};
	SLT_ENUM_CLASS_FLAGS(WindowFlags)

	struct WindowDesc {
		str title = "Shalltear";
		Vec2I clientSize = {1280, 720};
		std::optional<Vec2I> position;
		Vec2I minClientSize = {160, 90};
		Vec2I maxClientSize = {0, 0};
		WindowFlags flags = WindowFlags::Resizable | WindowFlags::AcceptFileDrops;
		Handle<Window> owner;
	};

	struct NativeWindowHandle {
		void* hwnd = nullptr;
		void* hinstance = nullptr;
	};

	enum class HitTestResult : u8 {
		Client = 0,
		Caption,
		Left,
		Right,
		Top,
		Bottom,
		TopLeft,
		TopRight,
		BottomLeft,
		BottomRight,
		MinimizeButton,
		MaximizeButton,
		CloseButton,
		Content,
	};

	enum class SystemCursor : u8 {
		Arrow = 0,
		IBeam,
		Crosshair,
		Hand,
		ResizeEW,
		ResizeNS,
		ResizeNWSE,
		ResizeNESW,
		ResizeAll,
		NotAllowed,
		Count
	};

	enum class CursorMode : u8 {
		Normal = 0,
		Hidden,
		Locked,
		Wrapped,
	};

	enum class WindowState : u8 {
		Normal = 0,
		Minimized,
		Maximized,
		Fullscreen,
	};

	struct WindowPlacement {
		Vec2I position;
		Vec2I clientSize = {1280, 720};
		WindowState state = WindowState::Normal;
	};

	class SHALLTEAR_CORE_API Window : public Object {
	public:
		~Window() override;

		// physical pxs
		Vec2I GetClientSize() const;
		void SetClientSize(Vec2I sizePx);
		Vec2I GetPosition() const;
		void SetPosition(Vec2I posPx);
		RectI GetRect() const;
		f32 GetContentScale() const;
		Vec2I ClientToScreen(Vec2I clientPx) const;
		Vec2I ScreenToClient(Vec2I screenPx) const;
		const MonitorInfo& GetMonitor() const;

		WindowPlacement GetPlacement() const;
		void SetPlacement(const WindowPlacement& placement);

		void Show();
		void Hide();
		void Minimize();
		void Maximize();
		void Restore();
		void Focus();
		bool IsVisible() const;
		bool IsFocused() const;
		bool IsMinimized() const;
		bool IsMaximized() const;
		void SetTitle(std::string_view utf8);
		str GetTitle() const;
		WindowFlags GetFlags() const;

		void SetCursor(SystemCursor cursor);
		SystemCursor GetCursor() const;
		void SetCursorMode(CursorMode mode);
		CursorMode GetCursorMode() const;

		// must not mutate windows
		using HitTestFn = HitTestResult (*)(Window& window, Vec2I clientPosPx, void* user);
		void SetHitTestCallback(HitTestFn fn, void* user);

		NativeWindowHandle GetNativeHandle() const;
		void SetVSyncHint(bool on);
		bool GetVSyncHint() const;

		struct Impl;
	private:
		friend class WindowSystem;
		friend struct WindowInternalAccess;

		Window();
		Window(const Window&) = delete;
		Window& operator=(const Window&) = delete;

		Impl* _impl;
	};

} // namespace Shalltear
