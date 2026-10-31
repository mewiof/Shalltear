#pragma once

#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear::WindowInternal {

	inline constexpr i32 CURSOR_WRAP_MARGIN_PX = 2;
	inline constexpr i32 CURSOR_WRAP_LANDING_PX = 4;

	[[nodiscard]] constexpr i32 CursorWrapAxis(i32 pos, i32 low, i32 high, i32 marginPx, i32 landingPx) {
		if (high - low + 1 <= marginPx + landingPx + 1) {
			return pos;
		}
		if (pos <= low + marginPx) {
			return high - landingPx;
		}
		if (pos >= high - marginPx) {
			return low + landingPx;
		}
		return pos;
	}

	[[nodiscard]] constexpr Vec2I CursorWrapLanding(Vec2I posPx, RectI bounds,
													i32 marginPx = CURSOR_WRAP_MARGIN_PX,
													i32 landingPx = CURSOR_WRAP_LANDING_PX) {
		return {CursorWrapAxis(posPx.x, bounds.position.x, bounds.position.x + bounds.size.x - 1, marginPx, landingPx),
				CursorWrapAxis(posPx.y, bounds.position.y, bounds.position.y + bounds.size.y - 1, marginPx, landingPx)};
	}

} // namespace Shalltear::WindowInternal
