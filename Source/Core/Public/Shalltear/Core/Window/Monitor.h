#pragma once

#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	struct MonitorInfo {
		u64 id = 0;
		str name;
		RectI bounds;
		RectI workArea;
		f32 scale = 1.0f;
		bool primary = false;
		f32 refreshRate = 0.0f;
	};

} // namespace Shalltear
