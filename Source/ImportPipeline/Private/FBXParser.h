#pragma once

#include "ModelImport.h"

namespace Shalltear {

	SHALLTEAR_IMPORTPIPELINE_API ModelParseResult ParseFBX(const Path& sourcePhysicalPath, const ModelParseOptions& options = {});

} // namespace Shalltear
