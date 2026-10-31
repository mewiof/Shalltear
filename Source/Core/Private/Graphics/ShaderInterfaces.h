#pragma once

#include "Shalltear/Core/Graphics/ShaderAsset.h"

namespace Shalltear {

	bool ShaderInterfacesMatch(const ShaderAsset& shader, const Vector<RHI::DescriptorBindingDesc>& bindings,
							   u32 pushConstantSize, const Vector<UniformBlockLayout>& blocks,
							   const Vector<ShaderResourceInfo>& resources, str& outDifference);

} // namespace Shalltear
