#pragma once

#include "Shalltear/Core/Types.h"

namespace Shalltear::ModuleInternal {

	void* ImageBaseOf(const void* address);

	size_t ImageSizeOf(const void* imageBase);

	bool IsAddressInImage(const void* address, const void* imageBase);

} // namespace Shalltear::ModuleInternal
