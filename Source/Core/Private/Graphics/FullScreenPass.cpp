#include "Shalltear/Core/Graphics/FullScreenPass.h"

#include "Graphics/SceneRequests.h"

#include "Shalltear/Core/Log.h"

SLT_REGISTER_TYPE(Shalltear::FullScreenPass)

namespace Shalltear {

	FullScreenPass::~FullScreenPass() = default;

	PassRequests FullScreenPass::GetRequests() const {
		const MaterialAsset* held = material.Get();
		const ShaderAsset* shader = held ? held->shader.Get() : nullptr;
		return shader ? SceneRequests::DeclaredBy(*shader) : PassRequests::None;
	}

	bool FullScreenPass::Draws(const ShaderAsset& shader) {
		return !shader.IsKernel() && shader.pushConstantSize == 0 && !SceneRequests::DeclaresViewSet(shader);
	}

	void FullScreenPass::Record(PassContext& context) {
		if (!material.IsValid()) {
			if (!_warnedNoMaterial) {
				_warnedNoMaterial = true;
				SLT_LOG_WARN("Graphics", "FullScreenPass '{}' has no material (draws nothing)", virtualPath);
			}
			return;
		}
		context.DrawFullScreen(material);
	}

} // namespace Shalltear
