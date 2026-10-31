#pragma once

#include "AudioVoiceInternals.h"

#include "ThirdParty/miniaudio.h"

#include "AudioChannels.h"

namespace Shalltear {

	namespace AudioImpl {

		ma_allocation_callbacks MakeMiniaudioAllocationCallbacks();

		SpeakerPosition ToSpeakerPosition(ma_channel channel);

	} // namespace AudioImpl

} // namespace Shalltear
