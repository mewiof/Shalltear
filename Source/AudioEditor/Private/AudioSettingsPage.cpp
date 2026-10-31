#include "AudioEditor.h"

#include "AudioEditorPreferences.h"
#include "AudioSettings.h"

#include "Shalltear/Editor/SettingsPresentation.h"

namespace Shalltear::AudioEditor {

	namespace {

		void Field(std::string_view name, std::string_view label, std::string_view tooltip, std::string_view keywords, const void* owner) {
			SettingsFieldPresentation field;
			field.label = str(label);
			field.tooltip = str(tooltip);
			field.keywords = str(keywords);
			SettingsPresentation::RegisterField(AudioImpl::SETTINGS_CATEGORY_PATH, name, std::move(field), owner);
		}

	} // anonymous namespace

	void RegisterSettingsPage(const void* owner) {
		SettingsCategoryPresentation category;
		category.label = "Audio";
		category.tooltip = "How loud the game is, how sound fades with distance, how many sounds play at once.";
		category.audience = SettingsAudience::EditorAndPlayer;
		category.keywords = "sound volume master doppler rolloff latency buffer dsp voices spatializer speaker output streaming";
		category.order = 11;
		SettingsPresentation::RegisterCategory(AudioImpl::SETTINGS_CATEGORY_PATH, std::move(category), owner);

		Field("globalVolume", "Global Volume", "Volume of all sounds the game plays.", "master loudness", owner);
		Field("volumeRolloffScale", "Volume Rolloff Scale",
			  "How fast a source with Logarithmic rolloff fades with distance. Higher fades faster, lower slower.", "attenuation distance falloff",
			  owner);
		Field("dopplerFactor", "Doppler Factor", "How audible the Doppler effect is. 0 turns it off, 1 makes it audible on fast-moving objects.",
			  "pitch shift velocity", owner);
		Field("periodMilliseconds", "DSP Buffer Size",
			  "Requested length of 1 block of sound (in milliseconds). Shorter lowers latency, longer is more reliable on a "
			  "busy machine.",
			  "latency period block callback", owner);
		Field("obtainedPeriod", "Obtained Buffer", "Buffer length the device actually uses.", "latency period", owner);
		Field("maxVirtualVoices", "Max Virtual Voices",
			  "How many sounds the audio system tracks, audible or not. Keep above the number of sounds the game plays at once.",
			  "voices limit", owner);
		Field("maxRealVoices", "Max Real Voices", "How many sounds are heard at once. Loudest are chosen per frame.", "voices limit", owner);
		Field("spatializer", "Spatializer Plugin",
			  "Registered spatializer used for spatialized 3D sounds. Empty uses the built-in one.",
			  "3d hrtf binaural steam", owner);
		Field("virtualizeEffects", "Virtualize Effect", "Disables effects and spatialization on inaudible sounds to save CPU.",
			  "virtual culled cpu", owner);
		Field("device", "Output Device", "Audio output: the system's default device, or none.", "speaker headphones output",
			  owner);
		Field("streamAheadBlocks", "Stream Ahead", "How far ahead streamed sounds are read (in blocks of 256 frames). Higher values tolerate slower disks.",
			  "streaming disk buffer", owner);
		Field("primeFrames", "Stream Prime",
			  "Frames decoded in advance from the start of a streamed sound (so it starts/loops without delay).",
			  "streaming preload", owner);

		SettingsCategoryPresentation preferences;
		preferences.label = "Audio";
		preferences.tooltip = "Clip previews and muting in the editor.";
		preferences.audience = SettingsAudience::EditorOnly;
		preferences.keywords = "sound preview auto play loop mute";
		preferences.order = 15;
		SettingsPresentation::RegisterCategory(PREFERENCES, std::move(preferences), owner);
	}

} // namespace Shalltear::AudioEditor
