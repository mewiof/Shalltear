#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Types.h"

#include <optional>

namespace Shalltear {

	class Window;

	enum class FullScreenMode : u8 {
		FullScreenWindow = 0,
		MaximizedWindow,
		Windowed,
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::FullScreenMode)
SLT_ENUM_VALUE(FullScreenWindow)
SLT_ENUM_VALUE(MaximizedWindow)
SLT_ENUM_VALUE(Windowed)
SLT_END_ENUM()

namespace Shalltear {

	struct ScreenResolution {
		i32 width = 0;
		i32 height = 0;
		f32 refreshRate = 0.0f;
	};

	struct ScreenLaunch {
		str title;
		std::optional<i32> width;
		std::optional<i32> height;
		std::optional<bool> fullScreen;
		std::optional<i32> monitor;
	};

	namespace Screen {

		SHALLTEAR_CORE_API i32 GetWidth();
		SHALLTEAR_CORE_API i32 GetHeight();

		SHALLTEAR_CORE_API FullScreenMode GetFullScreenMode();
		SHALLTEAR_CORE_API void SetFullScreenMode(FullScreenMode mode);

		SHALLTEAR_CORE_API bool IsFullScreen();
		SHALLTEAR_CORE_API void SetFullScreen(bool fullScreen);

		SHALLTEAR_CORE_API void SetResolution(i32 width, i32 height, FullScreenMode mode);

		SHALLTEAR_CORE_API ScreenResolution GetCurrentResolution();
		SHALLTEAR_CORE_API void GetResolutions(Vector<ScreenResolution>& out);

		SHALLTEAR_CORE_API Handle<Window> CreateGameWindow(const ScreenLaunch& launch);

	} // namespace Screen

} // namespace Shalltear
