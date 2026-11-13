#include "Shalltear/Core/Graphics/ObjectShadow.h"

#include "Shalltear/Core/Graphics/RenderSystem.h"

namespace Shalltear {

	ObjectShadow::~ObjectShadow() = default;

	void ObjectShadow::OnEnable() {
		RenderSystem::RegisterObjectShadow(Handle<ObjectShadow>(this));
	}

	void ObjectShadow::OnDisable() {
		RenderSystem::UnregisterObjectShadow(Handle<ObjectShadow>(this));
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::ObjectShadow)
