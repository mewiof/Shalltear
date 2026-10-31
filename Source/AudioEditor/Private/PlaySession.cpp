#include "AudioEditor.h"

#include "AudioMixerLive.h"
#include "AudioPlayback.h"

#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Object.h"

#include "Shalltear/Editor/EditorEvents.h"

namespace Shalltear {

	namespace {

		class PlaySessionState : public Object {
		public:
			PlayModeState state = PlayModeState::Editing;

			void OnPlayModeChanged(const EditorPlayModeChangedEvent& event) {
				state = event.state;
				if (event.state == PlayModeState::Editing) {
					if (const u32 stopped = AudioImpl::StopGameVoices(); stopped > 0) {
						SLT_LOG_INFO("Audio", "Stopped {} sound(s) a script was still playing when play mode ended", stopped);
					}
					AudioImpl::ResetMixers();
				}
			}
		};

		PlaySessionState* _state = nullptr;

	} // anonymous namespace

	namespace AudioEditor {

		void RegisterPlaySession(const void*) {
			if (_state) {
				return;
			}
			_state = New<PlaySessionState>(MemTag::Audio);
			EditorEvents::PlayModeChanged.Subscribe(EventBus::GetGlobal(), Handle<PlaySessionState>(_state),
													&PlaySessionState::OnPlayModeChanged);
		}

		void ReleasePlaySession() {
			if (!_state) {
				return;
			}
			EventBus::GetGlobal().UnsubscribeAll(_state->GetInstanceId());
			Delete(_state);
			_state = nullptr;
		}

		PlayModeState GetPlayModeState() { return _state ? _state->state : PlayModeState::Editing; }

	} // namespace AudioEditor

} // namespace Shalltear
