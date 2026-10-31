#include "Shalltear/Core/Graphics/MeshRenderer.h"

#include "Shalltear/Core/Graphics/RenderSystem.h"

namespace Shalltear {

	MeshRenderer::~MeshRenderer() = default;

	void MeshRenderer::OnEnable() {
		RenderSystem::RegisterMeshRenderer(Handle<MeshRenderer>(this));
	}

	void MeshRenderer::OnDisable() {
		RenderSystem::UnregisterMeshRenderer(Handle<MeshRenderer>(this));
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::MeshRenderer)
