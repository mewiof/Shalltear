#include "Shalltear/Core/Graphics/SkinnedMeshRenderer.h"

#include "Shalltear/Core/Graphics/RenderSystem.h"

namespace Shalltear {

	SkinnedMeshRenderer::~SkinnedMeshRenderer() = default;

	void SkinnedMeshRenderer::OnEnable() {
		RenderSystem::RegisterSkinnedMeshRenderer(Handle<SkinnedMeshRenderer>(this));
	}

	void SkinnedMeshRenderer::OnDisable() {
		RenderSystem::UnregisterSkinnedMeshRenderer(Handle<SkinnedMeshRenderer>(this));
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::SkinnedMeshRenderer)
