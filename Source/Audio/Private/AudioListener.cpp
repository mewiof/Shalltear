#include "Shalltear/Audio/AudioListener.h"

#include "AudioScenePass.h"

namespace Shalltear {

	AudioListener::~AudioListener() {
		AudioImpl::UnregisterListener(Handle<AudioListener>(this));
	}

	void AudioListener::OnEnable() {
		if (AudioImpl::IsInWorldScene(*this)) {
			AudioImpl::RegisterListener(Handle<AudioListener>(this));
		}
	}

	void AudioListener::OnDisable() {
		AudioImpl::UnregisterListener(Handle<AudioListener>(this));
	}

} // namespace Shalltear
