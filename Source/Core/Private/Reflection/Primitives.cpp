#include "Shalltear/Core/Reflection/Primitives.h"

#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Reflection/Variant.h"

namespace Shalltear {

#define SLT_DEFINE_PRIMITIVE_INFO(T, HashedName, ExtraFlags)     \
	namespace {                                                  \
		TypeInfo _primitive_##T = {                              \
			StringHash(HashedName),                              \
			sizeof(T),                                           \
			alignof(T),                                          \
			nullptr,                                             \
			0,                                                   \
			0,                                                   \
			nullptr,                                             \
			nullptr,                                             \
			nullptr,                                             \
			0,                                                   \
			nullptr,                                             \
			0,                                                   \
			nullptr,                                             \
			0,                                                   \
			0,                                                   \
			static_cast<u32>(TypeFlagPrimitive) | (ExtraFlags)}; \
	}                                                            \
	const TypeInfo* GetTypeInfoFor<T>::Get() {                   \
		static bool registered = []() {                          \
			TypeRegistry::Register(&_primitive_##T);             \
			(void)EnsureVariantVTable<T>();                      \
			return true;                                         \
		}();                                                     \
		(void)registered;                                        \
		return &_primitive_##T;                                  \
	}

	SLT_DEFINE_PRIMITIVE_INFO(bool, "bool", TypeFlagTriviallyCopyable)
	SLT_DEFINE_PRIMITIVE_INFO(i8, "i8", TypeFlagTriviallyCopyable)
	SLT_DEFINE_PRIMITIVE_INFO(i16, "i16", TypeFlagTriviallyCopyable)
	SLT_DEFINE_PRIMITIVE_INFO(i32, "i32", TypeFlagTriviallyCopyable)
	SLT_DEFINE_PRIMITIVE_INFO(i64, "i64", TypeFlagTriviallyCopyable)
	SLT_DEFINE_PRIMITIVE_INFO(u8, "u8", TypeFlagTriviallyCopyable)
	SLT_DEFINE_PRIMITIVE_INFO(u16, "u16", TypeFlagTriviallyCopyable)
	SLT_DEFINE_PRIMITIVE_INFO(u32, "u32", TypeFlagTriviallyCopyable)
	SLT_DEFINE_PRIMITIVE_INFO(u64, "u64", TypeFlagTriviallyCopyable)
	SLT_DEFINE_PRIMITIVE_INFO(f32, "f32", TypeFlagTriviallyCopyable)
	SLT_DEFINE_PRIMITIVE_INFO(f64, "f64", TypeFlagTriviallyCopyable)

	SLT_DEFINE_PRIMITIVE_INFO(str, "str", 0u)

	SLT_DEFINE_PRIMITIVE_INFO(AttrRangeData, "AttrRangeData", TypeFlagTriviallyCopyable)

	SLT_DEFINE_PRIMITIVE_INFO(Guid, "Guid", TypeFlagTriviallyCopyable)

#undef SLT_DEFINE_PRIMITIVE_INFO

	void RegisterMathPrimitives();

	void RegisterEnginePrimitives() {
#define SLT_TOUCH_PRIMITIVE(T) (void)GetTypeInfoFor<T>::Get();

		SLT_TOUCH_PRIMITIVE(bool)
		SLT_TOUCH_PRIMITIVE(i8)
		SLT_TOUCH_PRIMITIVE(i16)
		SLT_TOUCH_PRIMITIVE(i32)
		SLT_TOUCH_PRIMITIVE(i64)
		SLT_TOUCH_PRIMITIVE(u8)
		SLT_TOUCH_PRIMITIVE(u16)
		SLT_TOUCH_PRIMITIVE(u32)
		SLT_TOUCH_PRIMITIVE(u64)
		SLT_TOUCH_PRIMITIVE(f32)
		SLT_TOUCH_PRIMITIVE(f64)
		SLT_TOUCH_PRIMITIVE(str)
		SLT_TOUCH_PRIMITIVE(AttrRangeData)
		SLT_TOUCH_PRIMITIVE(Guid)

#undef SLT_TOUCH_PRIMITIVE

		RegisterMathPrimitives();
	}

} // namespace Shalltear
