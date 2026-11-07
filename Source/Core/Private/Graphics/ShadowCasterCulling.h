#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear::GraphicsInternal {

	struct CullVolume {
		static constexpr u32 MAX_PLANES = 24;

		Plane planes[MAX_PLANES];
		u32 planeCount = 0;
		u32 layerMask = ~0u;

		constexpr CullVolume() noexcept = default;

		constexpr explicit CullVolume(const Frustum& frustum) noexcept {
			for (const Plane& plane : frustum.planes) {
				planes[planeCount++] = plane;
			}
		}

		[[nodiscard]] constexpr bool Intersects(const AABB& box) const noexcept {
			for (u32 i = 0; i < planeCount; ++i) {
				const Plane& plane = planes[i];
				const Vec3 positive(
					plane.normal.x >= 0.0f ? box.max.x : box.min.x,
					plane.normal.y >= 0.0f ? box.max.y : box.min.y,
					plane.normal.z >= 0.0f ? box.max.z : box.min.z);
				if (plane.SignedDistance(positive) < 0.0f) {
					return false;
				}
			}
			return true;
		}
	};

	// per axis (receiver's projected point -> farthest texel center)
	SHALLTEAR_CORE_API f32 ShadowFilterReachTexels(u32 pcfTaps);

	SHALLTEAR_CORE_API f32 ShadowReceiverMarginTexels(u32 pcfTaps, f32 normalBias);

	// light xyz = direction toward light (w = 0) or position (w = 1)
	SHALLTEAR_CORE_API CullVolume BuildCasterVolume(const Mat4& mapViewProjection, const Mat4& view, const Mat4& projection,
													Vec2 receiverDepths, const Vec4& light, f32 margin);

} // namespace Shalltear::GraphicsInternal
