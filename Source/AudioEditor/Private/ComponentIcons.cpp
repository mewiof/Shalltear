#include "AudioEditor.h"

#include "Shalltear/Audio/AudioFilters.h"
#include "Shalltear/Audio/AudioListener.h"
#include "Shalltear/Audio/AudioReverbZone.h"
#include "Shalltear/Audio/AudioSource.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Reflection/TypeInfo.h"

#include "Shalltear/Editor/ComponentMenu.h"
#include "Shalltear/Editor/EditorGizmos.h"
#include "Shalltear/Editor/EditorIcons.h"

namespace Shalltear::AudioEditor {

	namespace {

		constexpr std::string_view SourceMark = "engine://Icons/Components/AudioSource.svg";
		constexpr std::string_view ListenerMark = "engine://Icons/Components/AudioListener.svg";
		constexpr std::string_view ZoneMark = "engine://Icons/Components/AudioReverbZone.svg";
		constexpr std::string_view FilterMark = "engine://Icons/Components/AudioFilter.svg";
		constexpr std::string_view LowPassMark = "engine://Icons/Components/AudioLowPassFilter.svg";
		constexpr std::string_view HighPassMark = "engine://Icons/Components/AudioHighPassFilter.svg";
		constexpr std::string_view EchoMark = "engine://Icons/Components/AudioEchoFilter.svg";
		constexpr std::string_view DistortionMark = "engine://Icons/Components/AudioDistortionFilter.svg";
		constexpr std::string_view ReverbMark = "engine://Icons/Components/AudioReverbFilter.svg";
		constexpr std::string_view ChorusMark = "engine://Icons/Components/AudioChorusFilter.svg";

		constexpr std::string_view Category = "Audio";

	} // anonymous namespace

	void RegisterComponentIcons(const void* owner) {
		using EditorIcons::ComponentIcons::Register;
		Register(AudioSource::GetReflectType()->name, SourceMark, owner);
		Register(AudioListener::GetReflectType()->name, ListenerMark, owner);
		Register(AudioReverbZone::GetReflectType()->name, ZoneMark, owner);
		Register(AudioFilter::GetReflectType()->name, FilterMark, owner);
		Register(AudioLowPassFilter::GetReflectType()->name, LowPassMark, owner);
		Register(AudioHighPassFilter::GetReflectType()->name, HighPassMark, owner);
		Register(AudioEchoFilter::GetReflectType()->name, EchoMark, owner);
		Register(AudioDistortionFilter::GetReflectType()->name, DistortionMark, owner);
		Register(AudioReverbFilter::GetReflectType()->name, ReverbMark, owner);
		Register(AudioChorusFilter::GetReflectType()->name, ChorusMark, owner);

		ComponentMenu::Register(AudioSource::GetReflectType()->name, Category, owner);
		ComponentMenu::Register(AudioListener::GetReflectType()->name, Category, owner);
		ComponentMenu::Register(AudioReverbZone::GetReflectType()->name, Category, owner);
		ComponentMenu::Register(AudioFilter::GetReflectType()->name, Category, owner);

		bool registered = EditorGizmos::RegisterSceneIcon(AudioSource::GetReflectType()->name, owner);
		registered &= EditorGizmos::RegisterSceneIcon(AudioListener::GetReflectType()->name, owner);
		registered &= EditorGizmos::RegisterSceneIcon(AudioReverbZone::GetReflectType()->name, owner);
		if (!registered) {
			SLT_LOG_ERROR("Audio", "couldn't register the scene icons");
		}
	}

} // namespace Shalltear::AudioEditor
