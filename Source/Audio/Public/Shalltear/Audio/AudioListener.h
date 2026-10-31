#pragma once

#include "Shalltear/Audio/Audio.h"
#include "Shalltear/Audio/AudioAPI.h"
#include "Shalltear/Audio/AudioMixer.h"

#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Scene/Component.h"

namespace Shalltear {

	class SHALLTEAR_AUDIO_API AudioListener : public Component {
	public:
		SLT_REFLECT_COMPONENT(AudioListener, Component)
		SLT_REFLECT_FIELD(_output)
		SLT_END_REFLECT()

		AudioListener() = default;
		~AudioListener() override;

		Handle<AudioMixerGroup> GetOutputAudioMixerGroup() const { return _output; }
		void SetOutputAudioMixerGroup(Handle<AudioMixerGroup> output) { _output = output; }

		AudioVelocityUpdateMode GetVelocityUpdateMode() const { return _velocityUpdateMode; }
		void SetVelocityUpdateMode(AudioVelocityUpdateMode mode) { _velocityUpdateMode = mode; }
	protected:
		void OnEnable() override;
		void OnDisable() override;
	private:
		Handle<AudioMixerGroup> _output;
		AudioVelocityUpdateMode _velocityUpdateMode = AudioVelocityUpdateMode::Auto;
	};

} // namespace Shalltear
