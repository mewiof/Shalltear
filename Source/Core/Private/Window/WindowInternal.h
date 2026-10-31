#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/Window/Monitor.h"
#include "Shalltear/Core/Window/Window.h"

#include <cmath>
#include <new>
#include <string_view>
#include <thread>

namespace Shalltear {

	struct WindowInternalAccess {
		using Impl = Window::Impl;

		static Impl*& GetImpl(Window& window) { return window._impl; }
		static Impl* GetImpl(const Window& window) { return window._impl; }

		static Window* NewWindow() {
			void* memory = Allocate(sizeof(Window), MemTag::Platform, alignof(Window));
			return new (memory) Window();
		}
		static void DeleteWindow(Window* window) {
			if (window) {
				window->~Window();
				Free(window);
			}
		}
	};

	namespace WindowInternal {

		using WindowImpl = WindowInternalAccess::Impl;

		struct SystemState {
			bool initialized = false;
			std::thread::id mainThread;

			bool inPump = false;
			bool inModalTick = false;

			Vector<Window*> windows{GetMemoryResource(MemTag::Platform)};
			Vector<u64> destroyQueue{GetMemoryResource(MemTag::Platform)};

			Vector<MonitorInfo> monitors{GetMemoryResource(MemTag::Platform)};
			bool monitorsDirty = false;

			Handle<Window> focused;

			void (*modalTick)(void* user) = nullptr;
			void* modalTickUser = nullptr;
		};

		SystemState& GetState();

		bool IsMainThread();

		void RefreshMonitorsIfDirty();

		void DestroyWindowNow(Window* window);

		void ProcessDestroyQueue();

		void InvokeModalTick();

		SHALLTEAR_CORE_API void SetRenderSize(Window& window, Vec2I sizePx);
		SHALLTEAR_CORE_API Vec2I GetRenderSizeOverride(const Window& window);

		inline Vec2I GetRenderSize(const Window& window) {
			const Vec2I set = GetRenderSizeOverride(window);
			return set.x > 0 && set.y > 0 ? set : window.GetClientSize();
		}

		inline RectI LetterboxRect(Vec2I targetPx, Vec2I contentPx) {
			if (contentPx.x <= 0 || contentPx.y <= 0 || targetPx.x <= 0 || targetPx.y <= 0) {
				return RectI{0, 0, targetPx.x, targetPx.y};
			}
			const f32 scale = Math::Min(static_cast<f32>(targetPx.x) / static_cast<f32>(contentPx.x),
										static_cast<f32>(targetPx.y) / static_cast<f32>(contentPx.y));
			const i32 width = Math::Min(targetPx.x, static_cast<i32>(std::lround(contentPx.x * scale)));
			const i32 height = Math::Min(targetPx.y, static_cast<i32>(std::lround(contentPx.y * scale)));
			return RectI{(targetPx.x - width) / 2, (targetPx.y - height) / 2, width, height};
		}

		inline Vec2I MapIntoContent(Vec2I pointPx, Vec2I targetPx, Vec2I contentPx) {
			const RectI shown = LetterboxRect(targetPx, contentPx);
			if (shown.size.x <= 0 || shown.size.y <= 0) {
				return pointPx;
			}
			const f32 x = static_cast<f32>(pointPx.x - shown.position.x) * static_cast<f32>(contentPx.x) / static_cast<f32>(shown.size.x);
			const f32 y = static_cast<f32>(pointPx.y - shown.position.y) * static_cast<f32>(contentPx.y) / static_cast<f32>(shown.size.y);
			return Vec2I{static_cast<i32>(std::floor(x)), static_cast<i32>(std::floor(y))};
		}

		inline Vec2I ClientToRender(const Window& window, Vec2I clientPx) {
			const Vec2I set = GetRenderSizeOverride(window);
			if (set.x <= 0 || set.y <= 0) {
				return clientPx;
			}
			return MapIntoContent(clientPx, window.GetClientSize(), set);
		}

		// content pxs per px shown
		inline f32 RenderScaleOf(const Window& window) {
			const Vec2I set = GetRenderSizeOverride(window);
			if (set.x <= 0 || set.y <= 0) {
				return 1.0f;
			}
			const RectI shown = LetterboxRect(window.GetClientSize(), set);
			return shown.size.x > 0 ? static_cast<f32>(set.x) / static_cast<f32>(shown.size.x) : 1.0f;
		}

		struct DisplayMode {
			Vec2I sizePx;
			f32 refreshRate = 0.0f;
		};
	} // namespace WindowInternal

	namespace WindowBackend {
		void Initialize();
		void Terminate();

		bool CreateNative(Window& window, const WindowDesc& desc);
		void DestroyNative(Window& window);

		void Pump();
		void Wait(f64 timeoutSeconds);

		void EnumerateMonitors(Vector<MonitorInfo>& outMonitors);
		void EnumerateDisplayModes(u64 monitorId, Vector<WindowInternal::DisplayMode>& outModes);
		Handle<Window> WindowAtPoint(Vec2I screenPx, Handle<Window> ignore);

		bool SetClipboardText(std::string_view utf8);
		str GetClipboardText();
	} // namespace WindowBackend

} // namespace Shalltear
