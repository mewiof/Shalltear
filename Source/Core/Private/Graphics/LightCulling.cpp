#include "Graphics/LightCulling.h"

#include "Shalltear/Core/Math/MathUtil.h"

#include <algorithm>
#include <cmath>

namespace Shalltear::GraphicsInternal {

	namespace {

		f32 Luminance(const Vec3& color) {
			return 0.2126f * color.x + 0.7152f * color.y + 0.0722f * color.z;
		}

	} // namespace

	u32 SelectViewLights(std::span<LightCandidate> candidates, const Vec3& cameraPosition,
						 u32 lightsPerView, u32& outDirectionalCount, u32& outDropped) {
		std::sort(candidates.begin(), candidates.end(), [&](const LightCandidate& a, const LightCandidate& b) {
			if (a.IsDirectional() != b.IsDirectional()) {
				return a.IsDirectional();
			}
			if (a.IsDirectional()) {
				f32 luminanceA = Luminance(a.color);
				f32 luminanceB = Luminance(b.color);
				if (luminanceA != luminanceB) {
					return luminanceA > luminanceB;
				}
				return a.stableKey < b.stableKey;
			}
			f32 distanceA = (a.position - cameraPosition).LengthSquared();
			f32 distanceB = (b.position - cameraPosition).LengthSquared();
			if (distanceA != distanceB) {
				return distanceA < distanceB;
			}
			return a.stableKey < b.stableKey;
		});

		const u32 total = static_cast<u32>(candidates.size());
		const u32 kept = Math::Min(total, lightsPerView);
		outDropped = total - kept;

		outDirectionalCount = 0;
		for (u32 i = 0; i < kept; ++i) {
			if (!candidates[i].IsDirectional()) {
				break;
			}
			++outDirectionalCount;
		}
		return kept;
	}

	void SolveSpotCone(f32 outerAngleDeg, f32 innerAngleDeg, f32& outScale, f32& outOffset) {
		const f32 cosOuter = std::cos(outerAngleDeg * 0.5f * Math::DEG_TO_RAD);
		const f32 cosInner = std::cos(innerAngleDeg * 0.5f * Math::DEG_TO_RAD);
		outScale = 1.0f / Math::Max(cosInner - cosOuter, 1e-4f);
		outOffset = -cosOuter * outScale;
	}

	f32 SolveLightIntensity(LightType type, f32 intensity, bool physicalUnits) {
		if (!physicalUnits) {
			return intensity;
		}
		switch (type) {
		case LightType::Directional:
			return intensity;
		case LightType::Point:
			return intensity / (4.0f * Math::PI);
		case LightType::Spot:
			return intensity / Math::PI;
		}
		return intensity;
	}

	namespace {

		f32 SliceDepth(const ClusterGrid& grid, u32 slice) {
			return std::exp2((static_cast<f32>(slice) - grid.sliceBias) / grid.sliceScale);
		}

		void ClusterBounds(const ClusterGrid& grid, u32 x, u32 y, u32 z, Vec3& outMin, Vec3& outMax) {
			const f32 z0 = z == 0 ? grid.zCameraNear : SliceDepth(grid, z);
			const f32 z1 = SliceDepth(grid, z + 1);

			const f32 ndcX0 = -1.0f + 2.0f * static_cast<f32>(x) / static_cast<f32>(grid.dimX);
			const f32 ndcX1 = -1.0f + 2.0f * static_cast<f32>(x + 1) / static_cast<f32>(grid.dimX);
			const f32 ndcY0 = 1.0f - 2.0f * static_cast<f32>(y) / static_cast<f32>(grid.dimY);
			const f32 ndcY1 = 1.0f - 2.0f * static_cast<f32>(y + 1) / static_cast<f32>(grid.dimY);

			const f32 xs[4] = {ndcX0 * grid.tanHalfX * z0, ndcX0 * grid.tanHalfX * z1,
							   ndcX1 * grid.tanHalfX * z0, ndcX1 * grid.tanHalfX * z1};
			const f32 ys[4] = {ndcY0 * grid.tanHalfY * z0, ndcY0 * grid.tanHalfY * z1,
							   ndcY1 * grid.tanHalfY * z0, ndcY1 * grid.tanHalfY * z1};

			outMin = Vec3(xs[0], ys[0], z0);
			outMax = Vec3(xs[0], ys[0], z1);
			for (u32 i = 1; i < 4; ++i) {
				outMin.x = Math::Min(outMin.x, xs[i]);
				outMax.x = Math::Max(outMax.x, xs[i]);
				outMin.y = Math::Min(outMin.y, ys[i]);
				outMax.y = Math::Max(outMax.y, ys[i]);
			}
		}

		f32 SquaredDistanceToBox(const Vec3& center, const Vec3& boxMin, const Vec3& boxMax) {
			f32 distanceSquared = 0.0f;
			const f32 c[3] = {center.x, center.y, center.z};
			const f32 lo[3] = {boxMin.x, boxMin.y, boxMin.z};
			const f32 hi[3] = {boxMax.x, boxMax.y, boxMax.z};
			for (u32 axis = 0; axis < 3; ++axis) {
				const f32 outside = Math::Max(lo[axis] - c[axis], c[axis] - hi[axis]);
				if (outside > 0.0f) {
					distanceSquared += outside * outside;
				}
			}
			return distanceSquared;
		}

		struct ClusterSpan {
			u32 x0, x1, y0, y1, z0, z1;
		};

		bool SpanOfSphere(const ClusterGrid& grid, const Vec3& center, f32 radius, ClusterSpan& out) {
			const f32 zMin = center.z - radius;
			const f32 zMax = center.z + radius;
			if (zMax <= grid.zCameraNear || zMin >= grid.zFar) {
				return false;
			}
			out.z0 = ClusterSliceOf(grid, Math::Max(zMin, grid.zCameraNear));
			out.z1 = ClusterSliceOf(grid, Math::Min(zMax, grid.zFar));

			if (zMin <= grid.zCameraNear) {
				out.x0 = 0;
				out.x1 = grid.dimX - 1;
				out.y0 = 0;
				out.y1 = grid.dimY - 1;
				return true;
			}

			f32 minTileX = static_cast<f32>(grid.dimX);
			f32 maxTileX = 0.0f;
			f32 minTileY = static_cast<f32>(grid.dimY);
			f32 maxTileY = 0.0f;
			for (u32 corner = 0; corner < 8; ++corner) {
				const f32 x = center.x + ((corner & 1) ? radius : -radius);
				const f32 y = center.y + ((corner & 2) ? radius : -radius);
				const f32 z = (corner & 4) ? zMax : zMin;
				const f32 ndcX = x / (grid.tanHalfX * z);
				const f32 ndcY = y / (grid.tanHalfY * z);
				const f32 tileX = (ndcX + 1.0f) * 0.5f * static_cast<f32>(grid.dimX);
				const f32 tileY = (1.0f - ndcY) * 0.5f * static_cast<f32>(grid.dimY);
				minTileX = Math::Min(minTileX, tileX);
				maxTileX = Math::Max(maxTileX, tileX);
				minTileY = Math::Min(minTileY, tileY);
				maxTileY = Math::Max(maxTileY, tileY);
			}

			auto clampTile = [](f32 value, u32 dim) -> u32 {
				const f32 floored = std::floor(value);
				if (floored <= 0.0f) {
					return 0;
				}
				return Math::Min(static_cast<u32>(floored), dim - 1);
			};
			out.x0 = clampTile(minTileX, grid.dimX);
			out.x1 = clampTile(maxTileX, grid.dimX);
			out.y0 = clampTile(minTileY, grid.dimY);
			out.y1 = clampTile(maxTileY, grid.dimY);
			return true;
		}

	} // namespace

	ClusterGrid BuildClusterGrid(const Mat4& projection, f32 lightsNearDepth, f32 lightsFarDepth) {
		ClusterGrid grid;
		const f32 m00 = projection(0, 0);
		const f32 m11 = projection(1, 1);
		grid.tanHalfX = m00 != 0.0f ? 1.0f / std::abs(m00) : 1.0f;
		grid.tanHalfY = m11 != 0.0f ? 1.0f / std::abs(m11) : 1.0f;

		const f32 a = projection(2, 2);
		const f32 b = projection(2, 3);
		f32 cameraNear = 0.1f;
		if (a != 0.0f && b != 0.0f && a != 1.0f) {
			cameraNear = Math::Min(-b / a, b / (1.0f - a));
		}
		grid.zCameraNear = Math::Max(cameraNear, 1e-3f);

		const f32 farDepth = Math::Max(lightsFarDepth, grid.zCameraNear * 1.001f);
		const f32 tileAngle = Math::Max(2.0f * grid.tanHalfX / static_cast<f32>(grid.dimX),
										2.0f * grid.tanHalfY / static_cast<f32>(grid.dimY));
		const f32 spanCap = std::pow(1.0f + tileAngle, static_cast<f32>(grid.dimZ));
		grid.zNear = Math::Max(Math::Max(lightsNearDepth, grid.zCameraNear), farDepth / spanCap);
		grid.zFar = Math::Max(farDepth, grid.zNear * 1.001f);

		const f32 logRange = std::log2(grid.zFar / grid.zNear);
		grid.sliceScale = static_cast<f32>(grid.dimZ) / logRange;
		grid.sliceBias = -std::log2(grid.zNear) * grid.sliceScale;
		return grid;
	}

	bool PunctualDepthSpan(const Mat4& view, std::span<const LightCandidate> punctual,
						   f32& outNearDepth, f32& outFarDepth) {
		bool any = false;
		for (const LightCandidate& candidate : punctual) {
			const f32 depth = view.TransformPoint(candidate.position).z;
			const f32 farDepth = depth + candidate.range;
			if (farDepth <= 0.0f) {
				continue;
			}
			const f32 nearDepth = depth - candidate.range;
			outNearDepth = any ? Math::Min(outNearDepth, nearDepth) : nearDepth;
			outFarDepth = any ? Math::Max(outFarDepth, farDepth) : farDepth;
			any = true;
		}
		return any;
	}

	u32 ClusterSliceOf(const ClusterGrid& grid, f32 viewZ) {
		const f32 slice = std::log2(Math::Max(viewZ, 1e-4f)) * grid.sliceScale + grid.sliceBias;
		if (slice <= 0.0f) {
			return 0;
		}
		return Math::Min(static_cast<u32>(slice), grid.dimZ - 1);
	}

	u32 ClusterIndexOf(const ClusterGrid& grid, const Vec3& viewPosition) {
		const f32 z = Math::Max(viewPosition.z, 1e-4f);
		const f32 ndcX = viewPosition.x / (grid.tanHalfX * z);
		const f32 ndcY = viewPosition.y / (grid.tanHalfY * z);
		const f32 tileX = std::floor((ndcX + 1.0f) * 0.5f * static_cast<f32>(grid.dimX));
		const f32 tileY = std::floor((1.0f - ndcY) * 0.5f * static_cast<f32>(grid.dimY));
		const u32 x = tileX <= 0.0f ? 0u : Math::Min(static_cast<u32>(tileX), grid.dimX - 1);
		const u32 y = tileY <= 0.0f ? 0u : Math::Min(static_cast<u32>(tileY), grid.dimY - 1);
		return (ClusterSliceOf(grid, viewPosition.z) * grid.dimY + y) * grid.dimX + x;
	}

	u32 BinLights(const ClusterGrid& grid, const Mat4& view,
				  std::span<const LightCandidate> punctual, u32 firstLightIndex,
				  u32 lightsPerCluster, Vector<u32>& outTable, Vector<u32>& outIndices,
				  Vector<f32>& scratchKeys) {
		const u32 clusterCount = grid.ClusterCount();
		outTable.assign(static_cast<size_t>(clusterCount) * 2, 0u);
		outIndices.clear();
		if (punctual.empty() || lightsPerCluster == 0) {
			return 0;
		}

		const u32 stride = Math::Min(lightsPerCluster, static_cast<u32>(punctual.size()));
		const size_t slots = static_cast<size_t>(clusterCount) * stride;
		outIndices.assign(slots, 0u);
		scratchKeys.assign(slots, 0.0f);

		u32 dropped = 0;
		for (u32 light = 0; light < punctual.size(); ++light) {
			const LightCandidate& candidate = punctual[light];
			const Vec3 viewPosition = view.TransformPoint(candidate.position);
			ClusterSpan span;
			if (!SpanOfSphere(grid, viewPosition, candidate.range, span)) {
				continue;
			}
			const f32 radiusSquared = Math::Max(candidate.range * candidate.range, 1e-8f);
			for (u32 z = span.z0; z <= span.z1; ++z) {
				for (u32 y = span.y0; y <= span.y1; ++y) {
					for (u32 x = span.x0; x <= span.x1; ++x) {
						Vec3 boxMin;
						Vec3 boxMax;
						ClusterBounds(grid, x, y, z, boxMin, boxMax);
						const f32 distanceSquared = SquaredDistanceToBox(viewPosition, boxMin, boxMax);
						if (distanceSquared > radiusSquared) {
							continue;
						}
						const f32 key = distanceSquared / radiusSquared;
						const u32 cluster = (z * grid.dimY + y) * grid.dimX + x;
						const size_t base = static_cast<size_t>(cluster) * stride;
						u32& count = outTable[static_cast<size_t>(cluster) * 2 + 1];
						if (count < stride) {
							outIndices[base + count] = firstLightIndex + light;
							scratchKeys[base + count] = key;
							++count;
							continue;
						}
						++dropped;
						size_t worst = base;
						for (size_t slot = base + 1; slot < base + stride; ++slot) {
							if (scratchKeys[slot] > scratchKeys[worst]) {
								worst = slot;
							}
						}
						if (key < scratchKeys[worst]) {
							outIndices[worst] = firstLightIndex + light;
							scratchKeys[worst] = key;
						}
					}
				}
			}
		}

		u32 total = 0;
		for (u32 cluster = 0; cluster < clusterCount; ++cluster) {
			const u32 count = outTable[static_cast<size_t>(cluster) * 2 + 1];
			outTable[static_cast<size_t>(cluster) * 2] = total;
			const size_t base = static_cast<size_t>(cluster) * stride;
			for (u32 i = 0; i < count; ++i) {
				outIndices[total + i] = outIndices[base + i];
			}
			total += count;
		}
		outIndices.resize(total);
		return dropped;
	}

} // namespace Shalltear::GraphicsInternal
