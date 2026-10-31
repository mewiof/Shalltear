#pragma once

#include "Shalltear/Core/Reflection/Reflect.h"

namespace Shalltear {

	class SHALLTEAR_CORE_API Object {
	public:
		Object();
		virtual ~Object();

		u64 GetInstanceId() const { return _instanceId; }

		SLT_REFLECT_CLASS(Object)
		SLT_END_REFLECT()
	private:
		friend class ObjectRegistry;

		u64 _instanceId;
	};

	class SHALLTEAR_CORE_API ObjectRegistry {
	public:
		static u64 Register(Object* obj);

		static void Unregister(u64 id);

		static Object* Resolve(u64 id);

		// visitor must not mutate the registry
		static void ForEachLiveObject(void (*visitor)(Object* object, u64 id, void* user), void* user);

		static bool DetachPreservingSlot(Object& object);

		static bool RebindPreservedSlot(Object& replacement, u64 preservedId);

		static void ReleaseReservedSlot(u64 id);
	};
} //namespace Shalltear
