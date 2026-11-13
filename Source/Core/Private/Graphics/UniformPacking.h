#pragma once

#include "Shalltear/Core/Graphics/MaterialAsset.h"

#include <span>

namespace Shalltear::GraphicsInternal {

	// shorter of 2 float kinds copied, Int/Bool written only on match
	void WriteUniformMember(const UniformMember& member, const MaterialAsset::Param& value, std::span<u8> block);

} // namespace Shalltear::GraphicsInternal
