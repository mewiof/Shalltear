#include "Shalltear/Editor/EditorIdleOps.h"

#include <algorithm>

namespace Shalltear::EditorIdleOps {

	namespace {

		constexpr f32 FALLBACK_REFRESH_RATE = 60.0f;

	} // anonymous namespace

	f64 IdleSeconds(const IdleInputs& inputs) {
		if (inputs.playing || inputs.interacting) {
			return 0.0;
		}

		i32 milliseconds = 0;
		switch (inputs.mode) {
		case InteractionMode::Default:
			if (inputs.lastFrameSeconds >= DEFAULT_NO_IDLE_FRAME_SECONDS) {
				return 0.0;
			}
			milliseconds = DEFAULT_IDLE_MILLISECONDS;
			break;
		case InteractionMode::NoThrottling:
			return 0.0;
		case InteractionMode::MonitorRefreshRate: {
			f32 hz = inputs.refreshRate > 0.0f ? inputs.refreshRate : FALLBACK_REFRESH_RATE;
			milliseconds = std::min(1000, static_cast<i32>(1000.0 / static_cast<f64>(hz)));
			break;
		}
		case InteractionMode::Custom:
			milliseconds = std::clamp(inputs.customMilliseconds, 0, CUSTOM_MAX_MILLISECONDS);
			break;
		}
		return static_cast<f64>(milliseconds) / 1000.0;
	}

} // namespace Shalltear::EditorIdleOps
