#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Audio/AudioClip.h"
#include "Shalltear/Audio/AudioResource.h"

#include "Shalltear/Core/Handle.h"

namespace Shalltear {

	namespace AudioImpl {

		struct AudioClipData;

		struct AudioClipAccess {
			static AudioClip* Resolve(Handle<AudioResource> resource);

			static AudioClipData* GetPlayback(const AudioClip& clip) { return clip._playback; }

			static AudioClipData* GetCooked(const AudioClip& clip) { return clip._cooked; }

			static bool Adopt(AudioClip& clip, AudioClipData* cooked, AudioClipLoadType loadType);

			static void Clear(AudioClip& clip);
		};

	} // namespace AudioImpl

} // namespace Shalltear
