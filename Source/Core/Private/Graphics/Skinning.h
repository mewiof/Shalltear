#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/SkinAsset.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear::GraphicsInternal {

	struct SkinBounds {
		// bind shape, idx = influence joint
		Vector<AABB> joints{GetMemoryResource(MemTag::Asset)};
		// 1/blend shape frame in shape order, each holds the origin
		Vector<AABB> frameDeltas{GetMemoryResource(MemTag::Asset)};
		AABB unjointed = AABB::Empty();
		bool convex = true;

		SHALLTEAR_CORE_API static void Build(MeshAsset& mesh);
		SHALLTEAR_CORE_API static void Release(MeshAsset& mesh);

		SHALLTEAR_CORE_API static AABB Pose(const MeshAsset& mesh, std::span<const Mat4> skinMatrices, std::span<const f32> weights);
	};

	SHALLTEAR_CORE_API void BuildSkinMatrices(const SkinAsset& skin, std::span<const Mat4> modelPose, Mat4* out, u32 outCount);

	SHALLTEAR_CORE_API void SkinVertices(std::span<const MeshVertex> vertices,
										 std::span<const MeshSkinInfluence> influences,
										 std::span<const Mat4> skinMatrices,
										 MeshVertex* out);

	SHALLTEAR_CORE_API void ApplyBlendShapes(const MeshAsset& mesh, std::span<const f32> weights, MeshVertex* out);

	SHALLTEAR_CORE_API bool HasActiveBlendShapes(const MeshAsset& mesh, std::span<const f32> weights);

} // namespace Shalltear::GraphicsInternal
