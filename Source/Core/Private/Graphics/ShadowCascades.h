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

} // namespace Shalltear::GraphicsInternal
