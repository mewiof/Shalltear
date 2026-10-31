#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/Window/Monitor.h"
#include "Shalltear/Core/Window/Window.h"

#include <span>
#include <string_view>

namespace Shalltear {

	class SHALLTEAR_CORE_API WindowSystem {
	public:
		static void Initialize();
		static void Terminate();
		static bool IsInitialized();

		static Handle<Window> Create(const WindowDesc& desc);
		static void RequestDestroy(Handle<Window> window);

		static void PumpEvents();
		static void WaitEvents(f64 timeoutSeconds);

		static std::span<const MonitorInfo> GetMonitors();

		static Handle<Window> GetFocusedWindow();
		static Handle<Window> GetPrimaryWindow();
		static Handle<Window> GetWindowAtPoint(Vec2I screenPx, Handle<Window> ignore = {});
		static u32 GetWindowCount();

		static bool SetClipboardText(std::string_view utf8);
		static str GetClipboardText();

		static void SetModalTickCallback(void (*tick)(void* user), void* user);
	};

} // namespace Shalltear
