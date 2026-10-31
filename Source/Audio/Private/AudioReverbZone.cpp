#include "Shalltear/Audio/AudioReverbZone.h"

#include "AudioScenePass.h"

namespace Shalltear {

	AudioReverbZone::~AudioReverbZone() {
		AudioImpl::UnregisterZone(Handle<AudioReverbZone>(this));
	}

	void AudioReverbZone::OnEnable() {
		if (AudioImpl::IsInWorldScene(*this)) {
			AudioImpl::RegisterZone(Handle<AudioReverbZone>(this));
		}
	}

	void AudioReverbZone::OnDisable() {
		AudioImpl::UnregisterZone(Handle<AudioReverbZone>(this));
	}

} // namespace Shalltear
