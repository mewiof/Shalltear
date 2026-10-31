#pragma once

#include "Shalltear/Core/Assert.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Types.h"

#include <cstddef>
#include <functional>

namespace Shalltear {

	// pointer from Get() is valid only until the frame's sync point
	template <typename T>
	class Handle {
	public:
		Handle() : _id(0) {}

		Handle(std::nullptr_t) : _id(0) {}

		Handle(T* obj) : _id(obj ? obj->GetInstanceId() : 0) {}

		u64 GetInstanceId() const { return _id; }

		T* Get() const {
			if (_id == 0)
				return nullptr;

			Object* baseObj = ObjectRegistry::Resolve(_id);
			return static_cast<T*>(baseObj);
		}

		T* operator->() const {
			T* ptr = Get();
			SLT_CORE_ASSERT_MSG(ptr != nullptr, "Attempted to dereference a null or destroyed Handle!");
			return ptr;
		}

		T& operator*() const {
			T* ptr = Get();
			SLT_CORE_ASSERT_MSG(ptr != nullptr, "Attempted to dereference a null or destroyed Handle!");
			return *ptr;
		}

		bool IsValid() const { return Get() != nullptr; }
		explicit operator bool() const { return IsValid(); }

		bool operator==(const Handle& other) const { return _id == other._id; }
		bool operator!=(const Handle& other) const { return _id != other._id; }
		bool operator==(std::nullptr_t) const { return !IsValid(); }
		bool operator!=(std::nullptr_t) const { return IsValid(); }
	private:
		u64 _id;
	};
} //namespace Shalltear

template <typename T>
struct std::hash<Shalltear::Handle<T>> {
	std::size_t operator()(const Shalltear::Handle<T>& handle) const noexcept {
		return static_cast<std::size_t>(handle.GetInstanceId());
	}
};
