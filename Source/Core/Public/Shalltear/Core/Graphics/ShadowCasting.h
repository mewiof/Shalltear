#pragma once

#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	enum class ShadowCasting : u8 {
		On = 0,
		Off,
		TwoSided,
		ShadowsOnly
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::ShadowCasting)
SLT_ENUM_VALUE(On)
SLT_ENUM_VALUE(Off)
SLT_ENUM_VALUE(TwoSided)
SLT_ENUM_VALUE(ShadowsOnly)
SLT_END_ENUM()
