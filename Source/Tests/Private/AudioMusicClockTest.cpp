#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "Shalltear/Audio/AudioMusicClock.h"

#include <cmath>

using namespace Shalltear;

namespace {

	AudioMusicClock Clock(f64 bpm, u32 beatsPerBar, u32 rate, u64 origin) {
		AudioMusicClock clock;
		clock.origin = AudioTime{origin};
		clock.beatsPerMinute = bpm;
		clock.beatsPerBar = beatsPerBar;
		clock.sampleRate = rate;
		return clock;
	}

} // namespace

TEST(AudioMusicClockTest, BeatIsFrameBarIsMeter) {
	const AudioMusicClock clock = Clock(120.0, 4, 48000, 1000);
	EXPECT_EQ(clock.GetBeatTime(0).frames, 1000u);
	EXPECT_EQ(clock.GetBeatTime(1).frames, 25000u);
	EXPECT_EQ(clock.GetBeatTime(4).frames, 97000u);
	EXPECT_EQ(clock.GetBeat(AudioTime{25000}), 1);
	EXPECT_EQ(clock.GetBeat(AudioTime{24999}), 0);
	EXPECT_EQ(clock.GetBar(AudioTime{97000}), 1);
	EXPECT_EQ(clock.GetBar(AudioTime{96999}), 0);
	EXPECT_EQ(clock.GetBeat(AudioTime{999}), -1);
}

TEST(AudioMusicClockTest, FractionalBeatDoesNotDrift) {
	const AudioMusicClock clock = Clock(130.0, 4, 44100, 5);
	const f64 perBeat = 60.0 * 44100.0 / 130.0;
	for (i64 beat = 0; beat <= 10000; beat += 7) {
		const u64 expected = 5 + static_cast<u64>(std::llround(static_cast<f64>(beat) * perBeat));
		ASSERT_EQ(clock.GetBeatTime(beat).frames, expected) << "beat " << beat;
		ASSERT_EQ(clock.GetBeat(AudioTime{expected}), beat) << "beat " << beat;
		ASSERT_EQ(clock.GetBeat(AudioTime{expected - 1}), beat - 1) << beat;
	}
}

TEST(AudioMusicClockTest, NextBoundaryOnBeatOrBar) {
	const AudioMusicClock clock = Clock(120.0, 4, 48000, 0);
	EXPECT_EQ(clock.NextBoundary(AudioQuantization::NextBar, AudioTime{96000}).frames, 96000u);
	EXPECT_EQ(clock.NextBoundary(AudioQuantization::NextBar, AudioTime{96001}).frames, 192000u);
	EXPECT_EQ(clock.NextBoundary(AudioQuantization::NextBeat, AudioTime{96001}).frames, 120000u);
	EXPECT_EQ(clock.NextBoundary(AudioQuantization::NextBeat, AudioTime{1}).frames, 24000u);
	EXPECT_EQ(clock.NextBoundary(AudioQuantization::Immediate, AudioTime{96001}).frames, 96001u);

	const AudioMusicClock none = Clock(0.0, 4, 48000, 0);
	EXPECT_FALSE(none.HasTempo());
	EXPECT_EQ(none.NextBoundary(AudioQuantization::NextBar, AudioTime{12345}).frames, 12345u);
	EXPECT_EQ(none.GetBeat(AudioTime{12345}), 0);
}

#endif
