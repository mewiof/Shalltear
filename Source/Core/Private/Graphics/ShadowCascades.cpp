#include "ShadowCascades.h"

#include <algorithm>
#include <cmath>

namespace Shalltear::GraphicsInternal {

	namespace {

		inline constexpr Vec3 LightUp = Vec3(0.0f, 1.0f, 0.0f);

		Mat4 LightBasis(const Vec3& direction) {
			return Mat4::LookAt(Vec3::Zero(), direction, LightUp);
		}

	} // namespace

	CascadeSplits SolveCascadeSplits(f32 nearDepth, f32 shadowDistance, u32 count, f32 firstCascadeDistance) {
		CascadeSplits splits;
		splits.count = std::clamp(count, 1u, MAX_SHADOW_CASCADES);

		const f32 nearZ = std::max(nearDepth, 1e-3f);
		const f32 farZ = std::max(shadowDistance, nearZ * 1.001f);

		splits.depths[0] = nearZ;
		splits.depths[splits.count] = farZ;
		if (splits.count > 1) {
			const f32 first = !(firstCascadeDistance >= nearZ) ? nearZ : std::min(firstCascadeDistance, farZ);
			const f32 ratio = std::pow(farZ / first, 1.0f / static_cast<f32>(splits.count - 1));
			f32 depth = first;
			for (u32 i = 1; i < splits.count; ++i) {
				splits.depths[i] = depth;
				depth *= ratio;
			}
		}

		// strictly ascending (SelectCascade + slice spheres assume it)
		const f32 step = std::pow(farZ / nearZ, 1e-3f);
		for (u32 i = 1; i < splits.count; ++i) {
			splits.depths[i] = std::max(splits.depths[i], splits.depths[i - 1] * step);
		}
		for (u32 i = splits.count - 1; i >= 1; --i) {
			splits.depths[i] = std::min(splits.depths[i], splits.depths[i + 1] / step);
		}
		return splits;
	}

	u32 BuildCascadeViews(const Mat4& view, const Mat4& projection, const Vec3& lightDirection,
						  const CascadeSplits& splits, u32 tileResolution, f32 casterDistance,
						  std::span<CascadeView> outViews) {
		const u32 written = std::min(splits.count, static_cast<u32>(outViews.size()));
		if (written == 0 || tileResolution == 0) {
			return 0;
		}

		const f32 diagonalX = std::abs(projection(0, 0));
		const f32 diagonalY = std::abs(projection(1, 1));
		const f32 tanHalfX = diagonalX > 1e-6f ? 1.0f / diagonalX : 1.0f;
		const f32 tanHalfY = diagonalY > 1e-6f ? 1.0f / diagonalY : 1.0f;

		const Mat4 inverseView = view.Inverse();
		const Vec3 direction = lightDirection.Normalized();
		const Mat4 basis = LightBasis(direction);
		const Mat4 inverseBasis = basis.Inverse();

		for (u32 i = 0; i < written; ++i) {
			const f32 sliceNear = splits.depths[i];
			const f32 sliceFar = splits.depths[i + 1];

			const f32 extentSquared = tanHalfX * tanHalfX + tanHalfY * tanHalfY;
			f32 centerDepth = (sliceFar + sliceNear) * (extentSquared + 1.0f) * 0.5f;
			centerDepth = std::min(centerDepth, sliceFar);

			f32 radius = 0.0f;
			for (u32 corner = 0; corner < 8; ++corner) {
				const f32 depth = (corner & 4) ? sliceFar : sliceNear;
				const f32 x = ((corner & 1) ? 1.0f : -1.0f) * depth * tanHalfX;
				const f32 y = ((corner & 2) ? 1.0f : -1.0f) * depth * tanHalfY;
				const f32 dz = depth - centerDepth;
				radius = std::max(radius, std::sqrt(x * x + y * y + dz * dz));
			}
			radius = std::max(radius, 1e-4f);

			const Vec4 centerView(0.0f, 0.0f, centerDepth, 1.0f);
			const Vec4 centerWorld = inverseView * centerView;

			const f32 texel = 2.0f * radius / static_cast<f32>(tileResolution);
			Vec4 centerLight = basis * Vec4(centerWorld.x, centerWorld.y, centerWorld.z, 1.0f);
			centerLight.x = std::floor(centerLight.x / texel) * texel;
			centerLight.y = std::floor(centerLight.y / texel) * texel;
			const Vec4 snapped = inverseBasis * centerLight;
			const Vec3 center(snapped.x, snapped.y, snapped.z);

			const f32 backOff = radius + std::max(casterDistance, 0.0f);
			const Vec3 eye = center - direction * backOff;

			CascadeView& out = outViews[i];
			out.view = Mat4::LookAt(eye, center, LightUp);
			out.projection = Mat4::OrthographicReverseZ(-radius, radius, -radius, radius, 0.0f, backOff + radius);
			out.viewProjection = out.projection * out.view;
			out.worldTexelSize = texel;
			out.farDepth = sliceFar;
		}
		return written;
	}

	u32 CascadeTilesPerAxis(u32 count) {
		const u32 clamped = std::clamp(count, 1u, MAX_SHADOW_CASCADES);
		return clamped <= 1 ? 1u : 2u;
	}

	RectI CascadeAtlasRect(u32 index, u32 atlasSize, u32 count) {
		const u32 perAxis = CascadeTilesPerAxis(count);
		const i32 tile = static_cast<i32>(atlasSize / perAxis);
		const u32 wrapped = index % (perAxis * perAxis);
		return RectI(static_cast<i32>(wrapped % perAxis) * tile,
					 static_cast<i32>(wrapped / perAxis) * tile,
					 tile, tile);
	}

	Mat4 CascadeAtlasMatrix(const Mat4& viewProjection, const RectI& tile, u32 atlasSize) {
		const f32 atlas = static_cast<f32>(std::max(atlasSize, 1u));
		const f32 halfWidth = static_cast<f32>(tile.size.x) * 0.5f;
		const f32 halfHeight = static_cast<f32>(tile.size.y) * 0.5f;

		Mat4 toAtlas = Mat4::Identity();
		toAtlas(0, 0) = halfWidth / atlas;
		toAtlas(0, 3) = (static_cast<f32>(tile.position.x) + halfWidth) / atlas;
		toAtlas(1, 1) = -halfHeight / atlas;
		toAtlas(1, 3) = (static_cast<f32>(tile.position.y) + halfHeight) / atlas;
		return toAtlas * viewProjection;
	}

	u32 SelectCascade(const CascadeSplits& splits, f32 viewDepth) {
		for (u32 i = 0; i < splits.count; ++i) {
			if (viewDepth <= splits.depths[i + 1]) {
				return i;
			}
		}
		return splits.count;
	}

} // namespace Shalltear::GraphicsInternal
