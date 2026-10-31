#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioOfflineMixer.h"

#include <vector>

using namespace AudioTesting;

namespace {

	constexpr u32 BLOCK = OfflineMixer::BLOCK;

	std::vector<f32> Click() {
		std::vector<f32> samples(64, 0.0f);
		samples[0] = 1.0f;
		return samples;
	}

	std::vector<u64> SoundingFrames(const std::vector<f32>& out, u64 firstFrame) {
		std::vector<u64> frames;
		for (size_t index = 0; index < out.size(); ++index) {
			if (out[index] != 0.0f) {
				frames.push_back(firstFrame + index);
			}
		}
		return frames;
	}

} // namespace

TEST(AudioScheduleTest, PlayScheduledInsideBlockStartsOnFrame) {
	OfflineMixer mixer(1);
	ClipBlock click(MakePcm(Click(), 1, 48000));
	const u64 blockStart = mixer.Now();
	const u64 at = blockStart + BLOCK + 137;
	ASSERT_TRUE(mixer.Push(mixer.Play(0, click.Get(), at)));

	const std::vector<u64> sounding = SoundingFrames(mixer.Render(3 * BLOCK), blockStart);
	ASSERT_EQ(sounding.size(), 1u);
	EXPECT_EQ(sounding[0], at);
}

TEST(AudioScheduleTest, BarApartVoicesStayApart) {
	OfflineMixer mixer(1);
	ClipBlock click(MakePcm(Click(), 1, 48000));
	constexpr u64 BAR = 96000;
	constexpr u32 BARS = 31;
	const u64 origin = mixer.Now() + 1000;
	for (u32 bar = 0; bar < BARS; ++bar) {
		ASSERT_TRUE(mixer.Push(mixer.Play(bar, click.Get(), origin + bar * BAR)));
	}

	std::vector<u64> sounding;
	u64 rendered = mixer.Now();
	const u32 slices[] = {480, 441, 1024, 17, 256, 960, 3};
	u32 turn = 0;
	while (mixer.Get().GetCounters().blocks.load() < 11301) {
		const u32 slice = slices[turn++ % (sizeof(slices) / sizeof(slices[0]))];
		const std::vector<u64> found = SoundingFrames(mixer.Render(slice), rendered);
		sounding.insert(sounding.end(), found.begin(), found.end());
		rendered += slice;
	}

	ASSERT_EQ(sounding.size(), BARS);
	for (u32 bar = 0; bar < BARS; ++bar) {
		EXPECT_EQ(sounding[bar], origin + bar * BAR) << "bar " << bar << " drifted";
	}
	EXPECT_EQ(mixer.Get().GetCounters().lateStarts.load(), 0u);
}

TEST(AudioScheduleTest, ScheduledEndStopsOnFrameUncut) {
	OfflineMixer mixer(1);
	ClipBlock tone(MakePcm(std::vector<f32>(2000, 0.5f), 1, 48000));
	const u64 start = mixer.Now();
	AudioCommand play = mixer.Play(0, tone.Get());
	play.loop = true;
	ASSERT_TRUE(mixer.Push(play));
	const u64 end = start + BLOCK + 99;
	ASSERT_TRUE(mixer.Push(OfflineMixer::To(play, AudioCommandKind::Stop, 0.0f, end)));

	const std::vector<f32> out = mixer.Render(3 * BLOCK);
	for (u64 frame = start; frame < end; ++frame) {
		ASSERT_EQ(out[frame - start], 0.5f) << frame;
	}
	for (u64 frame = end; frame < start + 3 * BLOCK; ++frame) {
		ASSERT_EQ(out[frame - start], 0.0f) << frame;
	}
	EXPECT_EQ(mixer.TakeEvents().size(), 1u);
}

TEST(AudioScheduleTest, PassedStartPlaysAtOnce) {
	OfflineMixer mixer(1);
	mixer.Render(4 * BLOCK);
	ClipBlock click(MakePcm(Click(), 1, 48000));
	const u64 now = mixer.Now();
	ASSERT_TRUE(mixer.Push(mixer.Play(0, click.Get(), now - 700)));

	const std::vector<u64> sounding = SoundingFrames(mixer.Render(2 * BLOCK), now);
	ASSERT_EQ(sounding.size(), 1u);
	EXPECT_EQ(sounding[0], now);
	EXPECT_EQ(mixer.Get().GetCounters().lateStarts.load(), 1u);
}

TEST(AudioScheduleTest, WaitingStartMovesBegunDoesNot) {
	OfflineMixer mixer(1);
	ClipBlock click(MakePcm(Click(), 1, 48000));
	const u64 origin = mixer.Now();
	const AudioCommand play = mixer.Play(0, click.Get(), origin + 300);
	ASSERT_TRUE(mixer.Push(play));
	ASSERT_TRUE(mixer.Push(OfflineMixer::To(play, AudioCommandKind::Reschedule, 0.0f, origin + 800)));
	std::vector<u64> sounding = SoundingFrames(mixer.Render(4 * BLOCK), origin);
	ASSERT_EQ(sounding.size(), 1u);
	EXPECT_EQ(sounding[0], origin + 800);

	ClipBlock tone(MakePcm(std::vector<f32>(4000, 0.25f), 1, 48000));
	const u64 now = mixer.Now();
	const AudioCommand playing = mixer.Play(1, tone.Get(), now);
	ASSERT_TRUE(mixer.Push(playing));
	mixer.Render(BLOCK);
	ASSERT_TRUE(mixer.Push(OfflineMixer::To(playing, AudioCommandKind::Reschedule, 0.0f, now + 10 * BLOCK)));
	for (f32 sample : mixer.Render(BLOCK)) {
		ASSERT_EQ(sample, 0.25f);
	}
}

TEST(AudioScheduleTest, VoiceStoppedBeforeStartEndsUnheard) {
	OfflineMixer mixer(1);
	ClipBlock click(MakePcm(Click(), 1, 48000));
	const u64 origin = mixer.Now();
	const AudioCommand play = mixer.Play(0, click.Get(), origin + 5 * BLOCK);
	ASSERT_TRUE(mixer.Push(play));
	mixer.Render(BLOCK);
	ASSERT_TRUE(mixer.Push(OfflineMixer::To(play, AudioCommandKind::Stop)));
	for (f32 sample : mixer.Render(8 * BLOCK)) {
		ASSERT_EQ(sample, 0.0f);
	}
	EXPECT_EQ(mixer.TakeEvents().size(), 1u);
}

TEST(AudioScheduleTest, StartEndInOneBlock) {
	OfflineMixer mixer(1);
	ClipBlock tone(MakePcm(std::vector<f32>(1000, 0.5f), 1, 48000));
	const u64 origin = mixer.Now();
	const AudioCommand play = mixer.Play(0, tone.Get(), origin + 40);
	ASSERT_TRUE(mixer.Push(play));
	ASSERT_TRUE(mixer.Push(OfflineMixer::To(play, AudioCommandKind::Stop, 0.0f, origin + 50)));
	const std::vector<u64> sounding = SoundingFrames(mixer.Render(2 * BLOCK), origin);
	ASSERT_EQ(sounding.size(), 10u);
	EXPECT_EQ(sounding.front(), origin + 40);
	EXPECT_EQ(sounding.back(), origin + 49);
}

TEST(AudioScheduleTest, AResampledVoiceStartsOnItsFrame) {
	OfflineMixer mixer(1);
	ClipBlock click(MakePcm(Click(), 1, 44100));
	const u64 origin = mixer.Now();
	ASSERT_TRUE(mixer.Push(mixer.Play(0, click.Get(), origin + 200)));
	const std::vector<f32> out = mixer.Render(2 * BLOCK);
	for (u64 frame = 0; frame < 200; ++frame) {
		ASSERT_EQ(out[frame], 0.0f) << frame;
	}
	EXPECT_EQ(out[200], 1.0f);
}

#endif
