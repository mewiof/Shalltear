#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/Light.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear::GraphicsInternal {

	struct LightCandidate {
		Vec3 position;
		f32 range = 0.0f;
		Vec3 color;
		f32 spotScale = 0.0f;
		Vec3 direction;
		f32 spotOffset = 1.0f;
		LightType type = LightType::Point;
		u64 stableKey = 0;

		bool castShadows = false;
		f32 shadowStrength = 1.0f;
		f32 shadowBias = 0.0f;
		f32 shadowNormalBias = 0.0f;
		// full angle (not half)
		f32 outerAngleDeg = 0.0f;

		bool IsDirectional() const { return type == LightType::Directional; }
	};

	SHALLTEAR_CORE_API u32 SelectViewLights(std::span<LightCandidate> candidates, const Vec3& cameraPosition,
											u32 lightsPerView, u32& outDirectionalCount, u32& outDropped);

	SHALLTEAR_CORE_API void SolveSpotCone(f32 outerAngleDeg, f32 innerAngleDeg, f32& outScale, f32& outOffset);

	SHALLTEAR_CORE_API f32 SolveLightIntensity(LightType type, f32 intensity, bool physicalUnits);

	struct ClusterGrid {
		u32 dimX = 16;
		u32 dimY = 9;
		u32 dimZ = 24;
		f32 sliceScale = 1.0f;
		f32 sliceBias = 0.0f;
		f32 tanHalfX = 1.0f;
		f32 tanHalfY = 1.0f;
		f32 zNear = 0.1f;
		f32 zFar = 1000.0f;
		f32 zCameraNear = 0.1f;

		u32 ClusterCount() const { return dimX * dimY * dimZ; }
	};

	SHALLTEAR_CORE_API ClusterGrid BuildClusterGrid(const Mat4& projection, f32 lightsNearDepth, f32 lightsFarDepth);

	SHALLTEAR_CORE_API bool PunctualDepthSpan(const Mat4& view, std::span<const LightCandidate> punctual,
											  f32& outNearDepth, f32& outFarDepth);

	SHALLTEAR_CORE_API u32 ClusterSliceOf(const ClusterGrid& grid, f32 viewZ);

	SHALLTEAR_CORE_API u32 ClusterIndexOf(const ClusterGrid& grid, const Vec3& viewPosition);

	// outTable = (offset, count) pairs into outIndices
	SHALLTEAR_CORE_API u32 BinLights(const ClusterGrid& grid, const Mat4& view,
									 std::span<const LightCandidate> punctual, u32 firstLightIndex,
									 u32 lightsPerCluster, Vector<u32>& outTable, Vector<u32>& outIndices,
									 Vector<f32>& scratchKeys);

} // namespace Shalltear::GraphicsInternal
