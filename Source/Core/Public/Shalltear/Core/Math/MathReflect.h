#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Reflection/Variant.h"
#include "Shalltear/Core/StringHash.h"

namespace Shalltear {

#define SLT_DECLARE_MATH_PRIMITIVE(T, HashedName)        \
	template <>                                          \
	struct VariantPrimitiveTraits<T> {                   \
		static StringHash TypeId() {                     \
			return StringHash(HashedName);               \
		}                                                \
	};                                                   \
	template <>                                          \
	struct GetTypeInfoFor<T> {                           \
		SHALLTEAR_CORE_API static const TypeInfo* Get(); \
	};

	SLT_DECLARE_MATH_PRIMITIVE(Vec2, "Vec2")
	SLT_DECLARE_MATH_PRIMITIVE(Vec3, "Vec3")
	SLT_DECLARE_MATH_PRIMITIVE(Vec4, "Vec4")
	SLT_DECLARE_MATH_PRIMITIVE(Vec2I, "Vec2I")
	SLT_DECLARE_MATH_PRIMITIVE(Vec3I, "Vec3I")
	SLT_DECLARE_MATH_PRIMITIVE(Vec4I, "Vec4I")
	SLT_DECLARE_MATH_PRIMITIVE(Quat, "Quat")
	SLT_DECLARE_MATH_PRIMITIVE(Color, "Color")
	SLT_DECLARE_MATH_PRIMITIVE(Color32, "Color32")
	SLT_DECLARE_MATH_PRIMITIVE(RectI, "RectI")

#undef SLT_DECLARE_MATH_PRIMITIVE

} // namespace Shalltear
