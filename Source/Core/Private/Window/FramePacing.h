#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear::FramePacingInternal {

	inline constexpr f32 FALLBACK_REFRESH_RATE = 60.0f;
	inline constexpr f64 PAUSED_WAIT_SECONDS = 0.25;

	struct PacingInputs {
		i32 targetFrameRate = -1;
		i32 backgroundFrameRate = 0;
		u32 windowsPresented = 0;
		u32 vsyncWindowsPresented = 0;
		f32 refreshRate = 0.0f;
		bool focused = true;
		bool runInBackground = true;
	};

	struct Pacing {
		bool paused = false;
		f64 intervalSeconds = 0.0;
	};

	SHALLTEAR_CORE_API Pacing ResolvePacing(const PacingInputs& inputs);

} // namespace Shalltear::FramePacingInternal
