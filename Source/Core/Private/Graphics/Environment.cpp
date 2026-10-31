#include "Shalltear/Core/Graphics/Environment.h"

#include "Shalltear/Core/Graphics/RenderSystem.h"

namespace Shalltear {

	Environment::~Environment() = default;

	void Environment::OnEnable() {
		RenderSystem::RegisterEnvironment(Handle<Environment>(this));
	}

	void Environment::OnDisable() {
		RenderSystem::UnregisterEnvironment(Handle<Environment>(this));
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::Environment)
