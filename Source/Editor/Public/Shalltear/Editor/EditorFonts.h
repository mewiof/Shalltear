#pragma once

#include "Shalltear/Core/Handle.h"

#include "Shalltear/Editor/EditorAPI.h"

namespace Shalltear {
	class FontAsset;
}

namespace Shalltear::EditorFonts {

	SHALLTEAR_EDITOR_API Handle<FontAsset> GetMonospace();

	SHALLTEAR_EDITOR_API void Shutdown();

} // namespace Shalltear::EditorFonts
