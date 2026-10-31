#pragma once

#include "Shalltear/Core/Reflection/Primitives.h"

namespace Shalltear {

	template <typename T>
	struct OffsetPtr {
		i64 _offset = 0;

		OffsetPtr() = default;
		OffsetPtr(std::nullptr_t) : _offset(0) {}
		explicit OffsetPtr(T* ptr) { Set(ptr); }

		void Set(T* ptr) {
			if (!ptr) {
				_offset = 0;
			} else {
				_offset = reinterpret_cast<const u8*>(ptr) - reinterpret_cast<const u8*>(this);
				SLT_CORE_ASSERT_MSG(_offset != 0, "OffsetPtr can't point to itself!");
			}
		}
		T* Get() const {
			if (_offset == 0)
				return nullptr;
			return reinterpret_cast<T*>(const_cast<u8*>(reinterpret_cast<const u8*>(this)) + _offset);
		}

		OffsetPtr& operator=(T* ptr) {
			Set(ptr);
			return *this;
		}

		T* operator->() const {
			T* ptr = Get();
			SLT_CORE_ASSERT_MSG(ptr != nullptr, "Dereferencing a null OffsetPtr");
			return ptr;
		}
		T& operator*() const {
			T* ptr = Get();
			SLT_CORE_ASSERT_MSG(ptr != nullptr, "Dereferencing a null OffsetPtr");
			return *ptr;
		}

		explicit operator bool() const { return _offset != 0; }
	};

	template <typename T>
	struct VariantPrimitiveTraits<OffsetPtr<T>> {
		static StringHash TypeId() {
			static const StringHash id = []() -> StringHash {
				const TypeInfo* pointeeInfo = GetStaticType<T>();
				constexpr u64 SALT = 0x4F46465345543C3EULL;
				return StringHash((pointeeInfo ? pointeeInfo->name.GetValue() : 0) ^ SALT);
			}();
			return id;
		}
	};

	template <typename T>
	struct GetTypeInfoFor<OffsetPtr<T>> {
		static const TypeInfo* Get() {
			static TypeInfo info = []() {
				TypeInfo ti{};
				ti.name = VariantPrimitiveTraits<OffsetPtr<T>>::TypeId();
				ti.size = sizeof(OffsetPtr<T>);
				ti.alignment = alignof(OffsetPtr<T>);
				ti.flags = TypeFlagPrimitive | TypeFlagTriviallyCopyable;
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
