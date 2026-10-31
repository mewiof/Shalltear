#include "AnimationEditor.h"

#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorPreviews.h"
#include "Shalltear/Editor/EditorTheme.h"

namespace Shalltear::AnimationEditor {

	void RegisterAssetBadges(const void* owner) {
		using EditorTheme::ColorToken;

		EditorPreviews::AssetBadges::Register(StringHash("AnimationClip"),
											  EditorIcons::Glyph::AssetAnimationClip,
											  ColorToken::AssetAnimation, owner);
		EditorPreviews::AssetBadges::Register(StringHash("AnimatorController"),
											  EditorIcons::Glyph::AssetAnimatorController,
											  ColorToken::AssetAnimation, owner);
	}

} // namespace Shalltear::AnimationEditor
