#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

namespace Shalltear::TypeNameOps {

	SHALLTEAR_EDITOR_API void Refresh();

	SHALLTEAR_EDITOR_API str Lookup(StringHash typeName);

	SHALLTEAR_EDITOR_API str MissingTypeLabel(StringHash typeName);

} // namespace Shalltear::TypeNameOps
