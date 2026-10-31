#pragma once

#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Settings.h"

#include <string_view>

namespace Shalltear {

	struct AudioEditorPreferences {
		SLT_REFLECT_VALUE(AudioEditorPreferences)
		SLT_REFLECT_CONSTRUCTIBLE()
		SLT_REFLECT_FIELD(autoPlay)
		SLT_REFLECT_FIELD(loop)
		SLT_REFLECT_FIELD(muteAudio)
		SLT_REFLECT_FIELD(resetPlayCountOnPlay)
		SLT_END_REFLECT()

		bool autoPlay = false;
		bool loop = false;
		bool muteAudio = false;
		bool resetPlayCountOnPlay = true;
	};

} // namespace Shalltear

namespace Shalltear::AudioEditor {

	inline constexpr std::string_view PREFERENCES = "Editor/Audio";

	inline AudioEditorPreferences* Preferences() {
		return Settings::DeclareCategory<AudioEditorPreferences>(PREFERENCES, SettingScope::User);
	}

} // namespace Shalltear::AudioEditor
