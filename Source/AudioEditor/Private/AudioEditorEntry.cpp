#include "AudioEditor.h"

#include "Shalltear/Editor/EditorExtensions.h"

namespace Shalltear::AudioEditor {

	void RegisterEditorExtensions(const void* owner) {
		RegisterPlaySession(owner);
		RegisterAudioGizmos(owner);
		RegisterClipInspector(owner);
		RegisterPreviewPanes(owner);
		RegisterWaveforms(owner);
		RegisterSourceGraph(owner);
		RegisterComponentIcons(owner);
		RegisterSettingsPage(owner);
		RegisterGameMute(owner);
		RegisterProfilerModule(owner);
	}

	void ReleaseEditorState() {
		ReleasePreview();
		ReleaseClipInspector();
		ReleaseWaveforms();
		ReleaseSourceGraph();
		ReleaseMixerWindows();
		ReleaseProfilerModule();
	}

} // namespace Shalltear::AudioEditor

SLT_IMPLEMENT_EDITOR_EXTENSIONS(&Shalltear::AudioEditor::RegisterEditorExtensions)
