#pragma once

// include only under Private/Window/Win32 (Windows.h must not leak)

#include "Shalltear/Core/Platform.h"

#ifdef SLT_PLATFORM_WINDOWS

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include <windows.h>
#include <windowsx.h>

#undef IsMinimized
#undef IsMaximized

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Input/InputCodes.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/Window/Window.h"

#include "Window/WindowInternal.h"

namespace Shalltear {

	struct Window::Impl {
		HWND hwnd = nullptr;
		Handle<Window> self;
		WindowFlags flags = WindowFlags::None;
		str title;
		Vec2I minClientSize;
		Vec2I maxClientSize;
		f32 scale = 1.0f;
		SystemCursor cursor = SystemCursor::Arrow;
		CursorMode cursorMode = CursorMode::Normal;
		POINT cursorPosBeforeLock = {};
		bool haveCursorPosBeforeLock = false;
		Window::HitTestFn hitTest = nullptr;
		void* hitTestUser = nullptr;
		bool vsyncHint = true;

		bool constructed = false;
		bool destroying = false;

		Vec2I wrapOffset;

		bool fullscreen = false;
		LONG_PTR windowedStyle = 0;
		WINDOWPLACEMENT windowedPlacement{};
		Vec2I renderSize{};

		bool focused = false;
		WindowState stateCache = WindowState::Normal;
		bool inSizeMove = false;
		bool mouseInside = false;
		u8 buttonsDown = 0;

		Vec2I lastClientSize;
		Vec2I lastPosition;

		bool pendingLiveResize = false;
		Vec2I pendingLiveSize;
		bool pendingMove = false;
		bool pendingDragMove = false;
		Vec2I pendingDragCursor;
		RectI pendingDragRect;

		char16_t pendingHighSurrogate = 0;
	};

	namespace Win32 {

		inline constexpr UINT_PTR MODAL_TICK_TIMER_ID = 0x534C5401;

		struct BackendState {
			HINSTANCE hinstance = nullptr;
			ATOM classAtom = 0;
			HCURSOR cursors[static_cast<size_t>(SystemCursor::Count)] = {};
			Vector<void*> monitorHandles{GetMemoryResource(MemTag::Platform)};
			Window::Impl* lockedWindow = nullptr;
			HANDLE waitTimer = nullptr;
			bool waitTimerTried = false;
		};

		BackendState& GetBackendState();

		LRESULT CALLBACK WndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
		Window* WindowFromHwnd(HWND hwnd);
		void ApplyCursorLock(Window::Impl& impl);
		void ReleaseCursorLock(Window::Impl& impl);

		Key TranslateVirtualKey(WPARAM wParam, LPARAM lParam);
		u16 ExtractScancode(LPARAM lParam);
		bool HandleInputMessage(Window::Impl& impl, UINT msg, WPARAM wParam, LPARAM lParam);

	} // namespace Win32

} // namespace Shalltear

#endif
