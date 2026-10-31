#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	inline constexpr u32 CUBE_FACE_COUNT = 6;

	SHALLTEAR_CORE_API Mat4 CubeFaceView(u32 face, const Vec3& eye);

} // namespace Shalltear
