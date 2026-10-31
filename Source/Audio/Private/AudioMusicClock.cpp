#include "Shalltear/Audio/AudioMusicClock.h"

#include "AudioComposite.h"

#include <cmath>

namespace Shalltear {

	namespace {

		f64 FramesPerBeat(const AudioMusicClock& clock) {
			return clock.HasTempo() ? 60.0 * static_cast<f64>(clock.sampleRate) / clock.beatsPerMinute : 0.0;
		}

		u32 BeatsPerBar(const AudioMusicClock& clock) { return clock.beatsPerBar > 0 ? clock.beatsPerBar : 1; }

		i64 FloorDiv(i64 value, i64 by) {
			const i64 quotient = value / by;
			return (value % by != 0 && ((value < 0) != (by < 0))) ? quotient - 1 : quotient;
		}

	} // anonymous namespace

	AudioTime AudioMusicClock::GetBeatTime(i64 beat) const {
		const f64 perBeat = FramesPerBeat(*this);
		const i64 frames = static_cast<i64>(origin.frames) + static_cast<i64>(std::llround(static_cast<f64>(beat) * perBeat));
		return AudioTime{frames > 0 ? static_cast<u64>(frames) : 0};
	}

	i64 AudioMusicClock::GetBeat(AudioTime time) const {
		const f64 perBeat = FramesPerBeat(*this);
		if (perBeat <= 0.0) {
			return 0;
		}
		i64 beat = static_cast<i64>(std::floor((static_cast<f64>(time.frames) - static_cast<f64>(origin.frames)) / perBeat));
		while (GetBeatTime(beat) > time) {
			--beat;
		}
		while (GetBeatTime(beat + 1) <= time) {
			++beat;
		}
		return beat;
	}

	i64 AudioMusicClock::GetBar(AudioTime time) const {
		return HasTempo() ? FloorDiv(GetBeat(time), static_cast<i64>(BeatsPerBar(*this))) : 0;
	}

	AudioTime AudioMusicClock::NextBoundary(AudioQuantization boundary, AudioTime after) const {
		if (boundary == AudioQuantization::Immediate || FramesPerBeat(*this) <= 0.0) {
			return after;
		}
		const i64 step = boundary == AudioQuantization::NextBar ? static_cast<i64>(BeatsPerBar(*this)) : 1;
		i64 beat = FloorDiv(GetBeat(after), step) * step;
		while (GetBeatTime(beat) < after) {
			beat += step;
		}
		return GetBeatTime(beat);
	}

	namespace Audio {

		using namespace AudioImpl;

		AudioMusicClock CreateClock(f64 beatsPerMinute, u32 beatsPerBar) {
			AudioMusicClock clock;
			clock.origin = GetEarliestExactStart();
			clock.beatsPerMinute = beatsPerMinute > 0.0 ? beatsPerMinute : 0.0;
			clock.beatsPerBar = beatsPerBar > 0 ? beatsPerBar : 1;
			clock.sampleRate = GetOutputSampleRate();
			return clock;
		}

		AudioVoice PlayQuantized(Handle<AudioResource> resource, const AudioMusicClock& clock, AudioQuantization boundary,
								 AudioPlayParams params) {
			params.at = clock.NextBoundary(boundary, GetEarliestExactStart());
			return Play(resource, params);
		}

		AudioMusicClock GetMusicClock(AudioVoice voice) {
			AudioMusicClock clock;
			if (IAudioComposite* composite = FindComposite(voice)) {
				composite->GetMusicClock(clock);
			}
			return clock;
		}

	} // namespace Audio

} // namespace Shalltear
