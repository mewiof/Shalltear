#include "Shalltear/Core/Graphics/ReflectionProbe.h"

#include "Shalltear/Core/Graphics/RenderSystem.h"

namespace Shalltear {

	ReflectionProbe::~ReflectionProbe() = default;

	void ReflectionProbe::OnEnable() {
		RenderSystem::RegisterReflectionProbe(Handle<ReflectionProbe>(this));
	}

	void ReflectionProbe::OnDisable() {
		RenderSystem::UnregisterReflectionProbe(Handle<ReflectionProbe>(this));
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::ReflectionProbe)
