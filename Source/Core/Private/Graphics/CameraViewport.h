#pragma once

#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include <cmath>

namespace Shalltear::GraphicsInternal {

	// clamped to target, top left origin
	[[nodiscard]] inline RectI ViewportPixels(const RectF& normalized, Vec2I target) {
		const f32 left = Math::Clamp(normalized.position.x, 0.0f, 1.0f);
		const f32 right = Math::Clamp(normalized.position.x + normalized.size.x, 0.0f, 1.0f);
		const f32 bottom = Math::Clamp(normalized.position.y, 0.0f, 1.0f);
		const f32 top = Math::Clamp(normalized.position.y + normalized.size.y, 0.0f, 1.0f);
		auto pixel = [](f32 fraction, i32 extent) {
			return static_cast<i32>(std::lround(fraction * static_cast<f32>(extent)));
		};
		const i32 x0 = pixel(left, target.x);
		const i32 x1 = pixel(right, target.x);
		const i32 y0 = pixel(1.0f - top, target.y);
		const i32 y1 = pixel(1.0f - bottom, target.y);
		return RectI(x0, y0, Math::Max(x1 - x0, 0), Math::Max(y1 - y0, 0));
	}

} // namespace Shalltear::GraphicsInternal
