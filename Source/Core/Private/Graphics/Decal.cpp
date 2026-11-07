#include "Shalltear/Core/Graphics/Decal.h"

#include "Shalltear/Core/Graphics/RenderSystem.h"

namespace Shalltear {

	Decal::~Decal() = default;

	void Decal::OnEnable() {
		RenderSystem::RegisterDecal(Handle<Decal>(this));
	}

	void Decal::OnDisable() {
		RenderSystem::UnregisterDecal(Handle<Decal>(this));
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::Decal)
