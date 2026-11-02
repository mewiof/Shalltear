#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Graphics/SkinAsset.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear::RHI {
	class CommandList;
	class Device;
} // namespace Shalltear::RHI

namespace Shalltear::GraphicsInternal {

	// must match Skinning.hlsl
	inline constexpr u32 SKINNING_GROUP_SIZE = 64;
	inline constexpr u32 SKINNING_MAX_GROUPS_X = 65535;
	inline constexpr u32 SKINNING_RENORMALIZE = 1u;

	// must match ShalltearSkinningPush in Skinning.hlsl
	struct GPUSkinningPush {
		u64 source = 0;
		u64 output = 0;
		u64 influences = 0;
		u64 matrices = 0;
		u64 morphRanges = 0;
		u64 morphDeltas = 0;
		u64 frameScales = 0;
		u32 vertexCount = 0;
		u32 influenceCount = 0;
		u32 jointCount = 0;
		u32 flags = 0;
	};
	static_assert(sizeof(GPUSkinningPush) == 72);

	// must match Skinning.hlsl
	struct GPUMorphDelta {
		Vec3 position;
		u32 frame = 0;
		Vec3 normal;
		f32 unused = 0.0f;
	};
	static_assert(sizeof(GPUMorphDelta) == 32);

	struct SkinBounds {
		// bind shape, idx = influence joint
		Vector<AABB> joints{GetMemoryResource(MemTag::Asset)};
		// 1/blend shape frame in shape order, each holds the origin
		Vector<AABB> frameDeltas{GetMemoryResource(MemTag::Asset)};
		AABB unjointed = AABB::Empty();
		bool convex = true;

		SHALLTEAR_CORE_API static AABB Pose(const MeshAsset& mesh, std::span<const Mat4> skinMatrices, std::span<const f32> weights);
	};

	struct MeshDeform {
		SkinBounds bounds;
		RHI::BufferHandle influences;
		RHI::BufferHandle morphRanges;
		RHI::BufferHandle morphDeltas;
		u64 influencesAddress = 0;
		u64 morphRangesAddress = 0;
		u64 morphDeltasAddress = 0;
		u32 frameCount = 0;

		SHALLTEAR_CORE_API static void Build(MeshAsset& mesh, RHI::Device* device);
		SHALLTEAR_CORE_API static void Release(MeshAsset& mesh, RHI::Device* device);

		static const MeshDeform* Of(const MeshAsset& mesh) { return mesh._deform; }
	};

	class SHALLTEAR_CORE_API SkinDeformer {
	public:
		void Begin(RHI::Device& device, u64 maxVertices);

		// address valid until next Begin
		u64 Add(RHI::Device& device, const MeshAsset& mesh, std::span<const Mat4> skinMatrices, std::span<const f32> weights);

		// output must be in UnorderedAccess
		void Record(RHI::CommandList& cmd, RHI::PipelineHandle kernel) const;

		void Destroy(RHI::Device& device);

		RHI::BufferHandle GetOutput() const { return _output; }
		u64 GetOutputAddress() const { return _outputAddress; }
	private:
		Vector<GPUSkinningPush> _jobs{GetMemoryResource(MemTag::Graphics)};
		RHI::BufferHandle _output;
		u64 _outputAddress = 0;
		u64 _capacity = 0;
		u64 _used = 0;
	};

	SHALLTEAR_CORE_API void BuildSkinMatrices(const SkinAsset& skin, std::span<const Mat4> modelPose, Mat4* out, u32 outCount);

	SHALLTEAR_CORE_API bool HasActiveBlendShapes(const MeshAsset& mesh, std::span<const f32> weights);

	// scales = 1/blend shape frame in shape order
	SHALLTEAR_CORE_API void BlendShapeFrameScales(const MeshAsset& mesh, std::span<const f32> weights, std::span<f32> scales, bool& outMovesNormals);

} // namespace Shalltear::GraphicsInternal
