#pragma once

#include "Shalltear/Audio/Audio.h"
#include "Shalltear/Audio/AudioAPI.h"
#include "Shalltear/Audio/AudioTime.h"

#include "Shalltear/Core/Types.h"

namespace Shalltear {

	enum class AudioQuantization : u8 {
		Immediate = 0,
		NextBeat,
		NextBar,
	};

	struct SHALLTEAR_AUDIO_API AudioMusicClock {
		AudioTime origin;
		f64 beatsPerMinute = 0.0;
		u32 beatsPerBar = 4;
		u32 sampleRate = 0;

		bool HasTempo() const { return beatsPerMinute > 0.0 && sampleRate != 0; }

		i64 GetBeat(AudioTime time) const;
		i64 GetBar(AudioTime time) const;

		AudioTime GetBeatTime(i64 beat) const;

		AudioTime NextBoundary(AudioQuantization boundary, AudioTime after) const;
	};

	namespace Audio {

		SHALLTEAR_AUDIO_API AudioMusicClock CreateClock(f64 beatsPerMinute, u32 beatsPerBar);

		SHALLTEAR_AUDIO_API AudioVoice PlayQuantized(Handle<AudioResource> resource, const AudioMusicClock& clock,
													 AudioQuantization boundary, AudioPlayParams params = {});

		SHALLTEAR_AUDIO_API AudioMusicClock GetMusicClock(AudioVoice voice);

	} // namespace Audio

} // namespace Shalltear
