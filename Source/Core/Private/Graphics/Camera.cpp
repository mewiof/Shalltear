#include "Shalltear/Core/Graphics/Camera.h"

#include "Shalltear/Core/Graphics/RenderSystem.h"

namespace Shalltear {

	Camera::~Camera() = default;

	void Camera::OnEnable() {
		RenderSystem::RegisterCamera(Handle<Camera>(this));
	}

	void Camera::OnDisable() {
		RenderSystem::UnregisterCamera(Handle<Camera>(this));
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::Camera)
