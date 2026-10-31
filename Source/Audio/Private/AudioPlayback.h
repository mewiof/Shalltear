#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Core/Types.h"

namespace Shalltear {

	namespace AudioImpl {

		u32 StopGameVoices();

		void SetGameMuted(bool muted);

	} // namespace AudioImpl

} // namespace Shalltear
