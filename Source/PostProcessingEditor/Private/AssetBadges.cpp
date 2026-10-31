#include "PostProcessingEditor.h"

#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorPreviews.h"
#include "Shalltear/Editor/EditorTheme.h"

namespace Shalltear::PostProcessingEditor {

	void RegisterAssetBadges(const void* owner) {
		EditorPreviews::AssetBadges::Register(StringHash("PostProcessProfile"), EditorIcons::Glyph::Effects,
											  EditorTheme::ColorToken::AssetProfile, owner);
	}

} // namespace Shalltear::PostProcessingEditor
