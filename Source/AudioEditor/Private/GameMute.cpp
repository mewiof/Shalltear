#include "AudioEditor.h"

#include "AudioEditorPreferences.h"
#include "AudioPlayback.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Settings.h"

#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/RuntimeViewToolbar.h"
#include "Shalltear/Editor/SettingsPresentation.h"

namespace Shalltear::AudioEditor {

	namespace {

		bool Muted() {
			const AudioEditorPreferences* preferences = Preferences();
			return preferences && preferences->muteAudio;
		}

		void SetMuted(bool muted) {
			AudioEditorPreferences* preferences = Preferences();
			if (!preferences || preferences->muteAudio == muted) {
				return;
			}
			preferences->muteAudio = muted;
			Settings::NotifyChanged(PREFERENCES, "muteAudio");
			Settings::Save();
			AudioImpl::SetGameMuted(muted);
		}

	} // anonymous namespace

	void RegisterGameMute(const void* owner) {
		RuntimeViewToggle toggle;
		toggle.icon = EditorIcons::Glyph::MuteAudio;
		toggle.label = "Mute";
		toggle.tooltip = "Mute Audio: silence the game's sound";
		toggle.isOn = &Muted;
		toggle.set = &SetMuted;
		if (!RuntimeViewToolbar::RegisterToggle(StringHash("MuteAudio"), std::move(toggle), owner)) {
			SLT_LOG_ERROR("Audio", "couldn't register the Mute Audio toggle");
		}
		SettingsFieldPresentation words;
		words.label = "Mute Audio";
		words.tooltip = "Silences the game's sound. Clip previews still play. Also on the Runtime View toolbar.";
		words.keywords = "mute silence game sound";
		SettingsPresentation::RegisterField(PREFERENCES, "muteAudio", std::move(words), owner);
		UpdateGameMute();
	}

	void UpdateGameMute() { AudioImpl::SetGameMuted(Muted()); }

} // namespace Shalltear::AudioEditor
