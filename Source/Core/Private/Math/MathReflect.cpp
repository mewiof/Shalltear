#include "Shalltear/Core/Math/MathReflect.h"

#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Reflection/Variant.h"

namespace Shalltear {

#define SLT_DEFINE_MATH_PRIMITIVE_INFO(T, HashedName)                                           \
	namespace {                                                                                 \
		TypeInfo _primitive_##T = {                                                             \
			StringHash(HashedName),                                                             \
			sizeof(T),                                                                          \
			alignof(T),                                                                         \
			nullptr,                                                                            \
			0,                                                                                  \
			0,                                                                                  \
			nullptr,                                                                            \
			nullptr,                                                                            \
			nullptr,                                                                            \
			0,                                                                                  \
			nullptr,                                                                            \
			0,                                                                                  \
			nullptr,                                                                            \
			0,                                                                                  \
			0,                                                                                  \
			static_cast<u32>(TypeFlagPrimitive) | static_cast<u32>(TypeFlagTriviallyCopyable)}; \
	}                                                                                           \
	const TypeInfo* GetTypeInfoFor<T>::Get() {                                                  \
		static bool registered = []() {                                                         \
			TypeRegistry::Register(&_primitive_##T);                                            \
			(void)EnsureVariantVTable<T>();                                                     \
			return true;                                                                        \
		}();                                                                                    \
		(void)registered;                                                                       \
		return &_primitive_##T;                                                                 \
	}

	SLT_DEFINE_MATH_PRIMITIVE_INFO(Vec2, "Vec2")
	SLT_DEFINE_MATH_PRIMITIVE_INFO(Vec3, "Vec3")
	SLT_DEFINE_MATH_PRIMITIVE_INFO(Vec4, "Vec4")
	SLT_DEFINE_MATH_PRIMITIVE_INFO(Vec2I, "Vec2I")
	SLT_DEFINE_MATH_PRIMITIVE_INFO(Vec3I, "Vec3I")
	SLT_DEFINE_MATH_PRIMITIVE_INFO(Vec4I, "Vec4I")
	SLT_DEFINE_MATH_PRIMITIVE_INFO(Quat, "Quat")
	SLT_DEFINE_MATH_PRIMITIVE_INFO(Color, "Color")
	SLT_DEFINE_MATH_PRIMITIVE_INFO(Color32, "Color32")
	SLT_DEFINE_MATH_PRIMITIVE_INFO(RectI, "RectI")

#undef SLT_DEFINE_MATH_PRIMITIVE_INFO

	void RegisterMathPrimitives() {
#define SLT_TOUCH_PRIMITIVE(T) (void)GetTypeInfoFor<T>::Get();

		SLT_TOUCH_PRIMITIVE(Vec2)
		SLT_TOUCH_PRIMITIVE(Vec3)
		SLT_TOUCH_PRIMITIVE(Vec4)
		SLT_TOUCH_PRIMITIVE(Vec2I)
		SLT_TOUCH_PRIMITIVE(Vec3I)
		SLT_TOUCH_PRIMITIVE(Vec4I)
		SLT_TOUCH_PRIMITIVE(Quat)
		SLT_TOUCH_PRIMITIVE(Color)
		SLT_TOUCH_PRIMITIVE(Color32)
		SLT_TOUCH_PRIMITIVE(RectI)

#undef SLT_TOUCH_PRIMITIVE
	}

} // namespace Shalltear
