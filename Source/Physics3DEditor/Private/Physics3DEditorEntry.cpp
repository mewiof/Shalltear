#include "Physics3DEditor.h"

#include "Shalltear/Editor/EditorExtensions.h"

namespace Shalltear::Physics3DEditor {

	void RegisterEditorExtensions(const void* owner) {
		RegisterColliderGizmos(owner);
		RegisterPhysicsDebugView(owner);
		RegisterCategoryDrawers(owner);
		RegisterComponentIcons(owner);
		RegisterHeldBodies(owner);
		RegisterAssetBadges(owner);
	}

} // namespace Shalltear::Physics3DEditor

SLT_IMPLEMENT_EDITOR_EXTENSIONS(&Shalltear::Physics3DEditor::RegisterEditorExtensions)
