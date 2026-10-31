#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/Light.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear::GraphicsInternal {

	inline constexpr u32 MAX_SHADOW_MAPS = 64;

	inline constexpr u32 POINT_SHADOW_FACES = 6;

	inline constexpr u32 MIN_PUNCTUAL_TILE = 256;
	inline constexpr u32 MAX_PUNCTUAL_TILE = 1024;

	class SHALLTEAR_CORE_API ShadowAtlasAllocator {
	public:
		explicit ShadowAtlasAllocator(u32 atlasSize = 0) { Reset(atlasSize); }

		void Reset(u32 atlasSize);

		RectI Allocate(u32 size);

		u32 AtlasSize() const { return _atlasSize; }
		u32 AllocatedCount() const { return _allocated; }
	private:
		Vector<RectI> _free{GetMemoryResource(MemTag::Graphics)};
		u32 _atlasSize = 0;
		u32 _allocated = 0;
	};

	struct ShadowRequest {
		u64 stableKey = 0;
		Vec3 position;
		Vec3 direction;
		f32 range = 0.0f;
		// full angle (not half)
		f32 outerAngleDeg = 0.0f;
		LightType type = LightType::Point;
		f32 strength = 1.0f;
		f32 depthBias = 0.0f;
		f32 normalBias = 0.0f;
		f32 importance = 0.0f;
		u32 tileSize = MIN_PUNCTUAL_TILE;

		u32 MapCount() const { return type == LightType::Point ? POINT_SHADOW_FACES : 1u; }
	};

	SHALLTEAR_CORE_API f32 ShadowScreenCoverage(const Mat4& projection, const Vec3& cameraPosition,
												const Vec3& lightPosition, f32 range);

	SHALLTEAR_CORE_API f32 ShadowImportance(f32 coverage, const Vec3& intensity);

	SHALLTEAR_CORE_API u32 PunctualTileSize(f32 coverage, u32 atlasSize, u32 mapCount);

	SHALLTEAR_CORE_API void SortShadowRequests(std::span<ShadowRequest> requests);

	struct PunctualView {
		Mat4 viewProjection;
		Mat4 view;
		Mat4 projection;
		f32 worldTexelSizePerUnit = 0.0f;
	};

	SHALLTEAR_CORE_API PunctualView BuildSpotView(const Vec3& position, const Vec3& direction,
												  f32 outerAngleDeg, f32 range, u32 tileResolution);

	SHALLTEAR_CORE_API PunctualView BuildPointFaceView(const Vec3& position, u32 face, f32 range,
													   u32 tileResolution, u32 pcfTaps);

	SHALLTEAR_CORE_API u32 PointFaceOf(const Vec3& fromLight);

} // namespace Shalltear::GraphicsInternal
