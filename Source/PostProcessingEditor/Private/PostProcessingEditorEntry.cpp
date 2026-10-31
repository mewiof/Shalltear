#include "PostProcessingEditor.h"

#include "Shalltear/Editor/EditorExtensions.h"

namespace Shalltear::PostProcessingEditor {

	void RegisterEditorExtensions(const void* owner) {
		RegisterVolumeGizmo(owner);
		RegisterComponentPresentation(owner);
		RegisterAssetBadges(owner);
	}

} // namespace Shalltear::PostProcessingEditor

SLT_IMPLEMENT_EDITOR_EXTENSIONS(&Shalltear::PostProcessingEditor::RegisterEditorExtensions)
