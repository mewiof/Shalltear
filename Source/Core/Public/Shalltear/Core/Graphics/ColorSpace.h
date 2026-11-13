#pragma once

#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	// Gamma: textures sampled as stored, shading on stored values, no display encode
	enum class ColorSpace : u8 {
		Linear = 0,
		Gamma = 1,
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::ColorSpace)
SLT_ENUM_VALUE(Linear)
SLT_ENUM_VALUE(Gamma)
SLT_END_ENUM()
