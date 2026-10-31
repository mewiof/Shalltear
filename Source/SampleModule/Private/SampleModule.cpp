#include "Shalltear/Core/Export.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Graphics/RenderPass.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Reflection.h"
#include "Shalltear/Core/Serialization/BinarySerializer.h"
#include "Shalltear/Core/Types.h"

namespace SampleModuleImpl {

	using namespace Shalltear;

	class SampleReflected : public Object {
	public:
		i32 health = 0;
		str name;

		SLT_REFLECT_CLASS(SampleReflected)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(health)
		SLT_REFLECT_FIELD(name)
		SLT_END_REFLECT()
	};

	class SamplePass : public RenderPass {
	public:
		SLT_REFLECT_CLASS(SamplePass)
		SLT_REFLECT_PARENT(RenderPass)
		SLT_REFLECT_CREATABLE()
		SLT_REFLECT_FIELD(tint)
		SLT_END_REFLECT()

		u32 tint = 0;

		void Record(PassContext& context) override { (void)context; }
	};

	struct SamplePayload {
		i32 value;
		i32 salt;
	};

} // namespace SampleModuleImpl

SLT_REGISTER_TYPE(SampleModuleImpl::SampleReflected)
SLT_REGISTER_TYPE(SampleModuleImpl::SamplePass)

namespace SampleModuleImpl {

	bool _shutdownRan = false;

} // namespace SampleModuleImpl

SLT_IMPLEMENT_MODULE_WITH_SHUTDOWN(SampleModule, +[]() { SampleModuleImpl::_shutdownRan = true; })

namespace Shalltear {

	template <>
	struct VariantPrimitiveTraits<SampleModuleImpl::SamplePayload> {
		static StringHash TypeId() { return StringHash("SamplePayload"); }
	};

} // namespace Shalltear

extern "C" {

using namespace Shalltear;
using SampleModuleImpl::SamplePayload;
using SampleModuleImpl::SampleReflected;

SLT_API_EXPORT bool SampleModuleRegisterTypes() {
	return SampleReflected::GetReflectType() != nullptr;
}

SLT_API_EXPORT void SampleModuleUnregisterTypes() {
	VariantDispatch::Unregister(StringHash("SamplePayload"));
	TypeRegistry::Unregister(StringHash("SampleReflected"));
}

SLT_API_EXPORT u32 SampleModuleGetTypeCount() {
	return TypeRegistry::GetTypeCount();
}

SLT_API_EXPORT u64 SampleModuleGetPayloadTypeId() {
	return StringHash("SamplePayload").GetValue();
}

SLT_API_EXPORT void SampleModuleBoxPayload(Variant* out, i32 value) {
	*out = SamplePayload{value, ~value};
}

SLT_API_EXPORT i32 SampleModuleReadPayload(const Variant* v) {
	const SamplePayload* payload = v->TryGet<SamplePayload>();
	return payload ? payload->value : -1;
}

SLT_API_EXPORT void* SampleModuleCreateSample(i32 health, const char* name) {
	SampleReflected* obj = New<SampleReflected>(MemTag::Scripting);
	obj->health = health;
	obj->name = name;
	return obj;
}

SLT_API_EXPORT void SampleModuleDestroySample(void* instance) {
	Delete(static_cast<SampleReflected*>(instance));
}

SLT_API_EXPORT i32 SampleModuleGetSampleHealth(const void* instance) {
	return static_cast<const SampleReflected*>(instance)->health;
}

SLT_API_EXPORT bool SampleModuleSerializeSample(Buffer* out, const void* instance) {
	*out = BinarySerializer::Serialize(instance, SampleReflected::GetReflectType());
	return out->data != nullptr;
}

SLT_API_EXPORT size_t SampleModuleAllocateBlock(size_t bytes, void** outPtr) {
	*outPtr = Allocate(bytes, MemTag::Scripting);
	return MemoryManager::GetBytesForTag(MemTag::Scripting);
}

SLT_API_EXPORT void SampleModuleFreeBlock(void* p) {
	Free(p);
}

SLT_API_EXPORT bool SampleModuleShutdownRan() {
	return SampleModuleImpl::_shutdownRan;
}
}
