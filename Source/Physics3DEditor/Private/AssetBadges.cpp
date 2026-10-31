#include "Physics3DEditor.h"

#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorPreviews.h"
#include "Shalltear/Editor/EditorTheme.h"

namespace Shalltear::Physics3DEditor {

	void RegisterAssetBadges(const void* owner) {
		EditorPreviews::AssetBadges::Register(StringHash("PhysicsMaterial"),
											  EditorIcons::Glyph::AssetPhysicsMaterial,
											  EditorTheme::ColorToken::AssetMaterial, owner);
	}

} // namespace Shalltear::Physics3DEditor
