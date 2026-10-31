#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear {

	class MeshAsset;

	enum class PrimitiveShape : u8 {
		Cube = 0,
		Sphere,
		Plane,
		Quad,
		Cylinder,
		Capsule,
		Count
	};

	SHALLTEAR_CORE_API std::string_view PrimitiveShapeName(PrimitiveShape shape);
	SHALLTEAR_CORE_API bool ParsePrimitiveShape(std::string_view name, PrimitiveShape& outShape);

	SHALLTEAR_CORE_API bool BuildPrimitiveMesh(PrimitiveShape shape, MeshAsset& out);

	SHALLTEAR_CORE_API str GetPrimitiveMeshPath(PrimitiveShape shape);

} // namespace Shalltear
