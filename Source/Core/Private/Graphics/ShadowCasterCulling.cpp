#include "ShadowCasterCulling.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Shalltear::GraphicsInternal {

	namespace {

		bool IsFinite(const Vec3& v) {
			return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z);
		}

		Vec2 ClipDepthRange(const Mat4& projection) {
			const f32 infinity = std::numeric_limits<f32>::infinity();
			const f32 atZero = -projection(2, 3) / projection(2, 2);
			const f32 atOne = (projection(3, 3) - projection(2, 3)) / (projection(2, 2) - projection(3, 2));
			const bool zeroFinite = std::isfinite(atZero);
			const bool oneFinite = std::isfinite(atOne);
			if (zeroFinite && oneFinite) {
				return Vec2(std::min(atZero, atOne), std::max(atZero, atOne));
			}
			if (zeroFinite || oneFinite) {
				return Vec2(zeroFinite ? atZero : atOne, infinity);
			}
			return Vec2(-infinity, infinity);
		}

		bool IntersectPlanes(const Plane& a, const Plane& b, const Plane& c, Vec3& out) {
			const Vec3 bc = Vec3::Cross(b.normal, c.normal);
			const f32 determinant = Vec3::Dot(a.normal, bc);
			if (!(std::abs(determinant) > 1e-6f)) {
				return false;
			}
			out = (bc * -a.distance + Vec3::Cross(c.normal, a.normal) * -b.distance +
				   Vec3::Cross(a.normal, b.normal) * -c.distance) /
				  determinant;
			return IsFinite(out);
		}

	} // namespace

	f32 ShadowFilterReachTexels(u32 pcfTaps) {
		return static_cast<f32>(std::max(pcfTaps, 1u) + 1) * 0.5f + 1.0f;
	}

	f32 ShadowReceiverMarginTexels(u32 pcfTaps, f32 normalBias) {
		return ShadowFilterReachTexels(pcfTaps) * std::sqrt(2.0f) + std::abs(normalBias) + 1.0f;
	}

	CullVolume BuildCasterVolume(const Mat4& mapViewProjection, const Mat4& view, const Mat4& projection,
								 Vec2 receiverDepths, const Vec4& light, f32 margin) {
		const CullVolume map(Frustum::FromMatrix(mapViewProjection));
		const Vec2 clip = ClipDepthRange(projection);
		const f32 nearDepth = std::max(receiverDepths.x, clip.x);
		const f32 farDepth = std::min(receiverDepths.y, clip.y);
		if (!std::isfinite(nearDepth) || !std::isfinite(farDepth) || !std::isfinite(margin)) {
			return map;
		}
		CullVolume volume;
		if (nearDepth >= farDepth) {
			volume.planes[0] = Plane(Vec3::Zero(), -1.0f);
			volume.planeCount = 1;
			return volume;
		}

		// face = axis * 2 + side, corner bit k = side on axis k
		const Frustum camera = Frustum::FromMatrix(projection * view);
		const Vec3 forward(view(2, 0), view(2, 1), view(2, 2));
		const Plane faces[6] = {camera.planes[Frustum::Left], camera.planes[Frustum::Right],
								camera.planes[Frustum::Bottom], camera.planes[Frustum::Top],
								Plane(forward, view(2, 3) - nearDepth).Normalized(),
								Plane(-forward, farDepth - view(2, 3)).Normalized()};
		Vec3 corners[8];
		for (u32 corner = 0; corner < 8; ++corner) {
			if (!IntersectPlanes(faces[corner & 1u], faces[2 + ((corner >> 1) & 1u)], faces[4 + (corner >> 2)], corners[corner])) {
				return map;
			}
		}

		auto add = [&volume, margin](const Plane& plane) {
			volume.planes[volume.planeCount++] = Plane(plane.normal, plane.distance + margin);
		};
		const Vec3 lightPoint = light.XYZ();
		bool kept[6];
		for (u32 face = 0; face < 6; ++face) {
			kept[face] = Vec3::Dot(faces[face].normal, lightPoint) + faces[face].distance * light.w >= 0.0f;
			if (kept[face]) {
				add(faces[face]);
			}
		}
		for (u32 a = 0; a < 6; ++a) {
			for (u32 b = a + 1; b < 6; ++b) {
				const u32 axisA = a / 2;
				const u32 axisB = b / 2;
				if (axisA == axisB || kept[a] == kept[b]) {
					continue;
				}
				const u32 first = ((a & 1u) << axisA) | ((b & 1u) << axisB);
				const Vec3& p0 = corners[first];
				const Vec3& p1 = corners[first | (1u << (3 - axisA - axisB))];
				const Vec3 edge = p1 - p0;
				const Vec3 toLight = lightPoint - p0 * light.w;
				const Vec3 normal = Vec3::Cross(edge, toLight);
				if (!(normal.Length() > 1e-6f * edge.Length() * toLight.Length())) {
					continue;
				}
				Plane plane = Plane::FromPointNormal(p0, normal);
				f32 farthest = 0.0f;
				for (const Vec3& corner : corners) {
					const f32 distance = plane.SignedDistance(corner);
					if (std::abs(distance) > std::abs(farthest)) {
						farthest = distance;
					}
				}
				if (farthest < 0.0f) {
					plane = Plane(-plane.normal, -plane.distance);
				}
				add(plane);
			}
		}

		for (u32 i = 0; i < map.planeCount; ++i) {
			volume.planes[volume.planeCount++] = map.planes[i];
		}
		return volume;
	}

} // namespace Shalltear::GraphicsInternal
