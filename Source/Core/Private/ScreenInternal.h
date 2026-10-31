#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Screen.h"
#include "Shalltear/Core/Window/Monitor.h"
#include "Shalltear/Core/Window/Window.h"

#include "ScreenSettings.h"

#include <span>

namespace Shalltear::ScreenInternal {

	void Initialize();
	void Terminate();

	struct ScreenChoice {
		FullScreenMode mode = FullScreenMode::FullScreenWindow;
		bool nativeResolution = true;
		Vec2I resolution{};
	};

	SHALLTEAR_CORE_API str DescribeProjectDefaults(const PlayerSettings& project);

	SHALLTEAR_CORE_API ScreenChoice ResolveStartup(const PlayerSettings& project, const ScreenSettings& saved, const ScreenLaunch& launch);

	// 0 for an empty list (caller must not index)
	SHALLTEAR_CORE_API u32 SelectMonitor(std::span<const MonitorInfo> monitors, const ScreenSettings& saved, const ScreenLaunch& launch);

	SHALLTEAR_CORE_API bool IsReachable(Vec2I position, Vec2I outerSize, std::span<const MonitorInfo> monitors);

	inline Vec2I CenterIn(const RectI& workArea, Vec2I outerSize) {
		return Vec2I{workArea.position.x + (workArea.size.x - outerSize.x) / 2,
					 workArea.position.y + (workArea.size.y - outerSize.y) / 2};
	}

	SHALLTEAR_CORE_API Handle<Window> CreateGameWindow(const ScreenLaunch& launch, WindowFlags extraFlags);

} // namespace Shalltear::ScreenInternal
