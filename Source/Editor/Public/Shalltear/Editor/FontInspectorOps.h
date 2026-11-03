#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/AssetBrowserOps.h"
#include "Shalltear/Editor/EditorAPI.h"

#include <span>

namespace Shalltear {
	class Path;
}

namespace Shalltear::FontInspectorOps {

	// invalid ids, font itself + repeats dropped
	SHALLTEAR_EDITOR_API bool ApplyFallbacks(const Path& fontPath, std::span<const Guid> fallbacks);

	SHALLTEAR_EDITOR_API void CollectCandidates(const Guid& font, std::span<const Guid> listed,
												Vector<AssetBrowserOps::AssignableAsset>& out);

} // namespace Shalltear::FontInspectorOps
