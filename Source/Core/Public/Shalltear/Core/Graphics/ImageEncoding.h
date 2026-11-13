#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear::ImageEncoding {

	// rgba8 rows top first, appended to out
	SHALLTEAR_CORE_API bool EncodePNG(std::span<const u8> rgba8, Vec2I size, Vector<u8>& out);

} // namespace Shalltear::ImageEncoding
