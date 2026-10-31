#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/SkinAsset.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear::GraphicsInternal {

	SHALLTEAR_CORE_API void BuildSkinMatrices(const SkinAsset& skin, std::span<const Mat4> modelPose, Mat4* out, u32 outCount);

	SHALLTEAR_CORE_API void SkinVertices(std::span<const MeshVertex> vertices,
										 std::span<const MeshSkinInfluence> influences,
										 std::span<const Mat4> skinMatrices,
										 MeshVertex* out);

	SHALLTEAR_CORE_API void ApplyBlendShapes(const MeshAsset& mesh, std::span<const f32> weights, MeshVertex* out);

	SHALLTEAR_CORE_API bool HasActiveBlendShapes(const MeshAsset& mesh, std::span<const f32> weights);

} // namespace Shalltear::GraphicsInternal
