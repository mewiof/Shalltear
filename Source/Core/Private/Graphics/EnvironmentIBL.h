#pragma once

#include "Shalltear/Core/Math/HalfFloat.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/CubeFace.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear::GraphicsInternal {

	using Shalltear::CUBE_FACE_COUNT;

	// mip m = roughness m / (mipCount-1)
	inline constexpr u32 IBL_MIP_COUNT = 6;
	inline constexpr u32 IBL_MIN_FACE_SIZE = 8;

	// convolved with the cosine lobe, divided by PI
	struct IrradianceSH {
		Vec3 coefficients[9]{};
	};

	struct RadianceCube {
		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		u32 faceSize = 0;
		u32 mipCount = 0;

		bool IsValid() const { return faceSize > 0 && mipCount > 0 && !pixels.empty(); }
	};

	SHALLTEAR_CORE_API u32 RadianceCubeMipCount(u32 faceSize);

	SHALLTEAR_CORE_API u64 RadianceCubeBytes(u32 faceSize, u32 mipCount);
	SHALLTEAR_CORE_API u64 RadianceCubeFaceOffset(u32 faceSize, u32 mipCount, u32 mip, u32 face);

	// D3D cube convention, normalized texel centers, v down
	SHALLTEAR_CORE_API Vec3 CubeTexelDirection(u32 face, f32 u, f32 v);

	struct SkyGradient {
		Vec3 sky{};
		Vec3 equator{};
		Vec3 ground{};
		bool flat = false;
	};

	SHALLTEAR_CORE_API Vec3 EvaluateSkyGradient(const SkyGradient& gradient, const Vec3& direction);

	SHALLTEAR_CORE_API void BuildCubeFromGradient(const SkyGradient& gradient, u32 faceSize, RadianceCube& out);

	SHALLTEAR_CORE_API bool BuildCubeFromEquirect(std::span<const u8> source, Vec2I sourceSize, RHI::Format sourceFormat,
												  u32 faceSize, RadianceCube& out);

	SHALLTEAR_CORE_API u32 StripFaceSize(Vec2I sourceSize);

	SHALLTEAR_CORE_API bool BuildCubeFromStrip(std::span<const u8> source, Vec2I sourceSize, RHI::Format sourceFormat,
											   u32 faceSize, RadianceCube& out);

	SHALLTEAR_CORE_API void PrefilterRadianceCube(RadianceCube& cube, IrradianceSH& outSH);

	inline u32 TextureMipCount(Vec2I size) {
		u32 largest = static_cast<u32>(size.x > size.y ? size.x : size.y);
		u32 count = 1;
		while (largest > 1) {
			largest >>= 1;
			++count;
		}
		return count;
	}

	SHALLTEAR_CORE_API u32 BoxChainMipCount(u32 faceSize);

	SHALLTEAR_CORE_API u32 IrradianceSourceMip(u32 faceSize);

	// face major RGBA16Float, rows packed
	SHALLTEAR_CORE_API void ProjectIrradianceFromFaces(std::span<const u8> faces, u32 side, IrradianceSH& outSH);

	SHALLTEAR_CORE_API Vec3 EvaluateIrradianceSH(const IrradianceSH& sh, const Vec3& normal);

} // namespace Shalltear::GraphicsInternal
