#pragma once

#include "Shalltear/Core/Reflection/TypeInfo.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Reflection/Variant.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	// reflecting Handle fields requires including Handle.h
	template <typename T>
	class Handle;

	struct AttrRangeData {
		f32 min;
		f32 max;
	};

#define SLT_DECLARE_PRIMITIVE_TRAITS(T, HashedName)      \
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

	SLT_DECLARE_PRIMITIVE_TRAITS(bool, "bool")
	SLT_DECLARE_PRIMITIVE_TRAITS(i8, "i8")
	SLT_DECLARE_PRIMITIVE_TRAITS(i16, "i16")
	SLT_DECLARE_PRIMITIVE_TRAITS(i32, "i32")
	SLT_DECLARE_PRIMITIVE_TRAITS(i64, "i64")
	SLT_DECLARE_PRIMITIVE_TRAITS(u8, "u8")
	SLT_DECLARE_PRIMITIVE_TRAITS(u16, "u16")
	SLT_DECLARE_PRIMITIVE_TRAITS(u32, "u32")
	SLT_DECLARE_PRIMITIVE_TRAITS(u64, "u64")
	SLT_DECLARE_PRIMITIVE_TRAITS(f32, "f32")
	SLT_DECLARE_PRIMITIVE_TRAITS(f64, "f64")
	SLT_DECLARE_PRIMITIVE_TRAITS(str, "str")
	SLT_DECLARE_PRIMITIVE_TRAITS(AttrRangeData, "AttrRangeData")

	SLT_DECLARE_PRIMITIVE_TRAITS(Guid, "Guid")

#undef SLT_DECLARE_PRIMITIVE_TRAITS

	template <typename T>
	struct VariantPrimitiveTraits<Handle<T>> {
		static StringHash TypeId() {
			static const StringHash id = []() -> StringHash {
				const TypeInfo* pointeeInfo = GetStaticType<T>();
				if (!pointeeInfo) {
					return StringHash("Handle<unknown>");
				}
				constexpr u64 SALT = 0x48414E444C453C3EULL;
				return StringHash(pointeeInfo->name.GetValue() ^ SALT);
			}();
			return id;
		}
	};

	inline StringHash HandlePointeeName(const TypeInfo& handleType) {
		constexpr u64 SALT = 0x48414E444C453C3EULL;
		return StringHash(handleType.name.GetValue() ^ SALT);
	}

	template <typename T>
	struct GetTypeInfoFor<Handle<T>> {
		static const TypeInfo* Get() {
			static TypeInfo info = []() {
				TypeInfo ti{};
				ti.name = VariantPrimitiveTraits<Handle<T>>::TypeId();
				ti.size = sizeof(Handle<T>);
				ti.alignment = alignof(Handle<T>);
				ti.flags = TypeFlagPrimitive | TypeFlagHandle | TypeFlagTriviallyCopyable;
				return ti;
			}();

			static bool registered = []() {
				TypeRegistry::Register(&info);
				return true;
			}();
			(void)registered;
			return &info;
		}
	};

} // namespace Shalltear
