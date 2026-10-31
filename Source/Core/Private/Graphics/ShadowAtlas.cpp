#include "ShadowAtlas.h"

#include <algorithm>
#include <cmath>

namespace Shalltear::GraphicsInternal {

	namespace {

		u32 FloorPowerOfTwo(u32 value) {
			if (value == 0) {
				return 0;
			}
			u32 result = 1;
			while ((result << 1) != 0 && (result << 1) <= value) {
				result <<= 1;
			}
			return result;
		}

		constexpr Vec3 FaceDirection[POINT_SHADOW_FACES] = {
			Vec3(1.0f, 0.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f),
			Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f),
			Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 0.0f, -1.0f)};
		constexpr Vec3 FaceUp[POINT_SHADOW_FACES] = {
			Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f),
			Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 0.0f, 1.0f),
			Vec3(0.0f, 1.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)};

		constexpr f32 PUNCTUAL_NEAR = 0.2f;

		Mat4 PunctualProjection(f32 fovYDegrees, f32 range) {
			const f32 far = std::max(range, 0.01f);
			const f32 near = std::min(PUNCTUAL_NEAR, far * 0.5f);
			return Mat4::PerspectiveReverseZ(fovYDegrees, 1.0f, near, far);
		}

	} // namespace

	void ShadowAtlasAllocator::Reset(u32 atlasSize) {
		_atlasSize = FloorPowerOfTwo(atlasSize);
		_allocated = 0;
		_free.clear();
		if (_atlasSize > 0) {
			_free.emplace_back(0, 0, static_cast<i32>(_atlasSize), static_cast<i32>(_atlasSize));
		}
	}

	RectI ShadowAtlasAllocator::Allocate(u32 size) {
		if (_atlasSize == 0) {
			return RectI{};
		}
		const u32 want = std::max(FloorPowerOfTwo(std::min(size, _atlasSize)), 1u);

		for (;;) {
			size_t best = _free.size();
			i32 bestSize = 0;
			for (size_t i = 0; i < _free.size(); ++i) {
				const i32 nodeSize = _free[i].size.x;
				if (nodeSize < static_cast<i32>(want)) {
					continue;
				}
				if (best == _free.size() || nodeSize < bestSize) {
					best = i;
					bestSize = nodeSize;
				}
			}
			if (best == _free.size()) {
				return RectI{};
			}

			const RectI node = _free[best];
			_free.erase(_free.begin() + static_cast<ptrdiff_t>(best));
			if (node.size.x == static_cast<i32>(want)) {
				++_allocated;
				return node;
			}

			const i32 half = node.size.x / 2;
			_free.emplace_back(node.position.x, node.position.y, half, half);
			_free.emplace_back(node.position.x + half, node.position.y, half, half);
			_free.emplace_back(node.position.x, node.position.y + half, half, half);
			_free.emplace_back(node.position.x + half, node.position.y + half, half, half);
		}
	}

	f32 ShadowScreenCoverage(const Mat4& projection, const Vec3& cameraPosition,
							 const Vec3& lightPosition, f32 range) {
		if (range <= 0.0f) {
			return 0.0f;
		}
		const f32 diagonalY = std::abs(projection(1, 1));
		const f32 tanHalfY = diagonalY > 1e-6f ? 1.0f / diagonalY : 1.0f;

		const f32 distance = std::max((lightPosition - cameraPosition).Length(), range);
		return std::clamp((range / distance) / std::max(tanHalfY, 1e-4f), 0.0f, 1.0f);
	}

	f32 ShadowImportance(f32 coverage, const Vec3& intensity) {
		const f32 brightness = std::max(std::max(intensity.x, intensity.y), intensity.z);
		return std::max(coverage, 0.0f) * std::max(brightness, 0.0f);
	}

	u32 PunctualTileSize(f32 coverage, u32 atlasSize, u32 mapCount) {
		const u32 atlas = std::max(FloorPowerOfTwo(atlasSize), 1u);
		u32 areaCeiling = atlas;
		const u64 budget = static_cast<u64>(atlas) * atlas / 2;
		const u64 maps = std::max(mapCount, 1u);
		while (areaCeiling > 1 && maps * static_cast<u64>(areaCeiling) * areaCeiling > budget) {
			areaCeiling >>= 1;
		}
		const u32 ceiling = std::min(std::min(MAX_PUNCTUAL_TILE, atlas), areaCeiling);
		const u32 floor = std::min(MIN_PUNCTUAL_TILE, ceiling);
		const f32 wanted = static_cast<f32>(ceiling) * std::clamp(coverage, 0.0f, 1.0f);
		return std::clamp(FloorPowerOfTwo(static_cast<u32>(wanted)), floor, ceiling);
	}

	void SortShadowRequests(std::span<ShadowRequest> requests) {
		std::sort(requests.begin(), requests.end(), [](const ShadowRequest& a, const ShadowRequest& b) {
			if (a.importance != b.importance) {
				return a.importance > b.importance;
			}
			return a.stableKey < b.stableKey;
		});
	}

	PunctualView BuildSpotView(const Vec3& position, const Vec3& direction,
							   f32 outerAngleDeg, f32 range, u32 tileResolution) {
		PunctualView out;
		const u32 resolution = std::max(tileResolution, 1u);
		const f32 fov = std::clamp(outerAngleDeg, 1.0f, 179.0f);
		const Vec3 forward = direction.Normalized();
		out.view = Mat4::LookAt(position, position + forward, Vec3::Up());
		out.projection = PunctualProjection(fov, range);
		out.viewProjection = out.projection * out.view;
		out.worldTexelSizePerUnit = 2.0f * std::tan(fov * 0.5f * Math::DEG_TO_RAD) / static_cast<f32>(resolution);
		return out;
	}

	PunctualView BuildPointFaceView(const Vec3& position, u32 face, f32 range,
									u32 tileResolution, u32 pcfTaps) {
		PunctualView out;
		const u32 index = face % POINT_SHADOW_FACES;
		const u32 resolution = std::max(tileResolution, 1u);

		const f32 reachTexels = static_cast<f32>(std::max(pcfTaps, 1u) + 1) * 0.5f + 1.0f;
		const f32 halfTangent = 1.0f + 2.0f * reachTexels / static_cast<f32>(resolution);
		const f32 fov = 2.0f * std::atan(halfTangent) * (180.0f / Math::PI);

		out.view = Mat4::LookAt(position, position + FaceDirection[index], FaceUp[index]);
		out.projection = PunctualProjection(fov, range);
		out.viewProjection = out.projection * out.view;
		out.worldTexelSizePerUnit = 2.0f * halfTangent / static_cast<f32>(resolution);
		return out;
	}

	u32 PointFaceOf(const Vec3& fromLight) {
		const f32 ax = std::abs(fromLight.x);
		const f32 ay = std::abs(fromLight.y);
		const f32 az = std::abs(fromLight.z);
		if (ax >= ay && ax >= az) {
			return fromLight.x >= 0.0f ? 0u : 1u;
		}
		if (ay >= az) {
			return fromLight.y >= 0.0f ? 2u : 3u;
		}
		return fromLight.z >= 0.0f ? 4u : 5u;
	}

} // namespace Shalltear::GraphicsInternal
