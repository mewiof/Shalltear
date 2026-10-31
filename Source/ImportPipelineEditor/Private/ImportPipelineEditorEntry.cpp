#include "ImportPipelineEditor.h"

#include "Shalltear/Editor/EditorExtensions.h"

namespace Shalltear::ImportPipelineEditor {

	void RegisterEditorExtensions(const void* owner) {
		RegisterModelRigSection(owner);
	}

} // namespace Shalltear::ImportPipelineEditor

SLT_IMPLEMENT_EDITOR_EXTENSIONS(&Shalltear::ImportPipelineEditor::RegisterEditorExtensions)
