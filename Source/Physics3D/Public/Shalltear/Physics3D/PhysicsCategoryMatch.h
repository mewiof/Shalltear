#pragma once

#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	enum class PhysicsCategoryMatch : u8 {
		AllExcept = 0,

		Only = 1,
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::PhysicsCategoryMatch)
SLT_ENUM_VALUE(AllExcept)
SLT_ENUM_VALUE(Only)
SLT_END_ENUM()
