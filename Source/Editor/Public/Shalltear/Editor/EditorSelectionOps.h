#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <span>

namespace Shalltear::EditorSelectionOps {

	SHALLTEAR_EDITOR_API bool ResolveRange(std::span<const u64> ordered, u64 anchor, u64 target, u32& first, u32& last);

	SHALLTEAR_EDITOR_API void ResolveTargets(std::span<const u64> selection, u64 actedOn, Vector<u64>& out);

} // namespace Shalltear::EditorSelectionOps
