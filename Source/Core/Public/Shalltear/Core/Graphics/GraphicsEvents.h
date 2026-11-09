#pragma once

#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/StringHash.h"

namespace Shalltear {

	// re-upload own buffers/textures, re-read device addresses + recreate pipelines (assets already restored)
	struct GraphicsDeviceRestoredEvent {};

	namespace GraphicsEvents {
		inline constexpr Event<GraphicsDeviceRestoredEvent> DeviceRestored{"Graphics.DeviceRestored"_hash};
	} // namespace GraphicsEvents

} // namespace Shalltear
