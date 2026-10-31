#pragma once

#include "NodeShaderAPI.h"

namespace Shalltear {

	SHALLTEAR_NODESHADER_API void RegisterNodeShader(const void* owner);
	SHALLTEAR_NODESHADER_API void ShutdownNodeShader();

} // namespace Shalltear
