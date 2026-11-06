#pragma once

#include "Shalltear/Core/Graphics/TextureAsset.h"

#include "Shalltear/Editor/EditorAPI.h"

namespace Shalltear {
	class Path;
}

namespace Shalltear::TextureInspectorOps {

	SHALLTEAR_EDITOR_API TextureAsset::Settings ReadImportSettings(const Path& texturePath);

	SHALLTEAR_EDITOR_API bool ApplyImportSettings(const Path& texturePath, const TextureAsset::Settings& settings);

} // namespace Shalltear::TextureInspectorOps
