#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear::GraphicsInternal {

	struct CascadeSplits {
		u32 count = 0;
		f32 depths[MAX_SHADOW_CASCADES + 1] = {};
	};

	SHALLTEAR_CORE_API CascadeSplits SolveCascadeSplits(f32 nearDepth, f32 shadowDistance, u32 count,
														f32 firstCascadeDistance);

	struct CascadeView {
		Mat4 viewProjection;
		Mat4 view;
		Mat4 projection;
		f32 worldTexelSize = 0.0f;
		f32 farDepth = 0.0f;
	};

	SHALLTEAR_CORE_API u32 BuildCascadeViews(const Mat4& view, const Mat4& projection, const Vec3& lightDirection,
											 const CascadeSplits& splits, u32 tileResolution, f32 casterDistance,
											 std::span<CascadeView> outViews);

	SHALLTEAR_CORE_API u32 CascadeTilesPerAxis(u32 count);

	// texels, shared by caster viewport + integrator
	SHALLTEAR_CORE_API RectI CascadeAtlasRect(u32 index, u32 atlasSize, u32 count);

	SHALLTEAR_CORE_API Mat4 CascadeAtlasMatrix(const Mat4& viewProjection, const RectI& tile, u32 atlasSize);

	SHALLTEAR_CORE_API u32 SelectCascade(const CascadeSplits& splits, f32 viewDepth);

	SHALLTEAR_CORE_API Vec2 CascadeReceiverDepths(const CascadeSplits& splits, u32 index);

	struct CascadeDepthFit {
		f32 applied = 0.0f;
		u32 shrinkFrames = 0;
		u64 seen = 0;
	};

	SHALLTEAR_CORE_API void UpdateCascadeDepthFit(CascadeDepthFit& fit, f32 farthest);

	// 2^(1/4) steps, shrunk only when 2 steps under previous
	SHALLTEAR_CORE_API f32 ObjectShadowRadius(f32 needed, f32 previous);

	// unsnapped (shadow should move with object), reach past sphere for receivers
	SHALLTEAR_CORE_API CascadeView BuildObjectShadowView(const Vec3& lightDirection, const Vec3& center, f32 radius,
														 f32 marginTexels, f32 reach, u32 tileResolution);
	SHALLTEAR_CORE_API f32 GetCameraDepthFit(u64 cameraInstanceId);

} // namespace Shalltear::GraphicsInternal
