#include "Shalltear/Core/Graphics/ShaderAsset.h"

#include "Graphics/ShaderInterfaces.h"

#include "Shalltear/Core/Log.h"

namespace Shalltear {

	bool ShaderAsset::AdoptVariant(ShaderAsset& compiled, u64 keywordHash) {
		if (keywordHash == 0 || FindVariant(keywordHash) != SHADER_BASE_VARIANT) {
			return false;
		}
		ShaderVariant* variant = nullptr;
		for (ShaderVariant& candidate : compiled.variants) {
			variant = candidate.keywordHash == keywordHash ? &candidate : variant;
		}
		if (!variant) {
			return false;
		}
		str error;
		if (!ShaderInterfacesMatch(*this, compiled.bindings, compiled.pushConstantSize, compiled.uniformBlocks, compiled.resources, error)) {
			SLT_LOG_WARN("Graphics", "shader '{}': a variant compiled off the frame no longer matches its interface ({}) -> drawing the base variant",
						 virtualPath, error);
			return false;
		}
		variants.push_back(std::move(*variant));
		return true;
	}

} // namespace Shalltear
