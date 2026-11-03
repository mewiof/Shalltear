#include "AudioEditor.h"

#include "Shalltear/Audio/AudioConcurrency.h"
#include "Shalltear/Audio/AudioContainer.h"
#include "Shalltear/Audio/AudioMixer.h"

#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorPreviews.h"
#include "Shalltear/Editor/EditorTheme.h"

namespace Shalltear::AudioEditor {

	void RegisterAssetBadges(const void* owner) {
		using EditorPreviews::AssetBadges::Register;
		using EditorTheme::ColorToken;

		Register(AudioMixer::GetReflectType()->name, EditorIcons::Glyph::AssetAudioMixer, ColorToken::AssetAudio, owner);
		Register(AudioConcurrency::GetReflectType()->name, EditorIcons::Glyph::AssetAudioConcurrency, ColorToken::AssetAudio, owner);
		Register(AudioRandomContainer::GetReflectType()->name, EditorIcons::Glyph::AssetAudioRandomContainer,
				 ColorToken::AssetAudio, owner);
		Register(AudioLayeredContainer::GetReflectType()->name, EditorIcons::Glyph::AssetAudioLayeredContainer,
				 ColorToken::AssetAudio, owner);
		Register(AudioInteractiveContainer::GetReflectType()->name, EditorIcons::Glyph::AssetAudioInteractiveContainer,
				 ColorToken::AssetAudio, owner);
	}

} // namespace Shalltear::AudioEditor
