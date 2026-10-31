#include "AnimationEditor.h"

#include "Shalltear/Editor/EditorExtensions.h"

namespace Shalltear::AnimationEditor {

	void RegisterEditorExtensions(const void* owner) {
		RegisterAnimatorControllerInspector(owner);
		RegisterModelRigInspector(owner);
		RegisterModelAnimationInspector(owner);
		RegisterComponentPresentation(owner);
		RegisterAssetBadges(owner);
		RegisterAnimatorOpener(owner);
	}

} // namespace Shalltear::AnimationEditor

SLT_IMPLEMENT_EDITOR_EXTENSIONS(&Shalltear::AnimationEditor::RegisterEditorExtensions)
