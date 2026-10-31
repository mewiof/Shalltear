#pragma once

#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorEvents.h"

namespace Shalltear {

	class Asset;

	namespace AudioImpl {
		struct AudioEnvelope;
	}

	namespace AudioEditor {

		bool RegisterClipImporter(const void* owner);

		void RegisterEditorExtensions(const void* owner);

		void RegisterPlaySession(const void* owner);
		void ReleasePlaySession();

		PlayModeState GetPlayModeState();

		void RegisterAudioGizmos(const void* owner);

		void RegisterClipInspector(const void* owner);

		void RegisterPreviewPanes(const void* owner);

		void RegisterWaveforms(const void* owner);

		void ReleaseEditorState();

		void RegisterSourceGraph(const void* owner);

		void RegisterComponentIcons(const void* owner);

		void RegisterSettingsPage(const void* owner);

		void RegisterGameMute(const void* owner);
		void UpdateGameMute();

		bool PlayPreview(const Guid& guid, u64 startSample);
		void StopPreview();
		bool IsPreviewing(const Guid& guid);
		f32 GetPreviewProgress(const Guid& guid);
		void UpdatePreview();
		void ReleasePreview();

		const AudioImpl::AudioEnvelope* FindWaveform(const Guid& guid, const Asset* loaded = nullptr);
		const AudioImpl::AudioEnvelope* PeekWaveform(const Guid& guid);
		void ForgetWaveform(const Guid& guid);

		void ReleaseClipInspector();
		void ReleaseWaveforms();
		void ReleaseSourceGraph();
		void ReleaseMixerWindows();

		void RegisterProfilerModule(const void* owner);
		void ReleaseProfilerModule();

	} // namespace AudioEditor

} // namespace Shalltear
