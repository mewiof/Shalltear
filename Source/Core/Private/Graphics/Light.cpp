#include "Shalltear/Core/Graphics/Light.h"

#include "Shalltear/Core/Graphics/RenderSystem.h"

namespace Shalltear {

	Light::~Light() = default;

	void Light::OnEnable() {
		RenderSystem::RegisterLight(Handle<Light>(this));
	}

	void Light::OnDisable() {
		RenderSystem::UnregisterLight(Handle<Light>(this));
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::Light)
