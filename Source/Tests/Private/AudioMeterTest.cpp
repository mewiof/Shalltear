#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioLiveFixture.h"
#include "AudioOfflineMixer.h"

#include <bit>
#include <cmath>
#include <vector>

using namespace AudioTesting;

namespace {

	constexpr u32 BLOCK = OfflineMixer::BLOCK;
	constexpr u32 RATE = 48000;
	constexpr f64 BLOCK_SECONDS = static_cast<f64>(BLOCK) / static_cast<f64>(RATE);

	std::vector<f32> Sine(u32 frames, f32 left, f32 right) {
		std::vector<f32> samples(static_cast<size_t>(frames) * 2);
		for (u32 frame = 0; frame < frames; ++frame) {
			const f32 wave = static_cast<f32>(std::sin(2.0 * 3.14159265358979323846 * static_cast<f64>(frame % 64) / 64.0));
			samples[static_cast<size_t>(frame) * 2] = left * wave;
			samples[static_cast<size_t>(frame) * 2 + 1] = right * wave;
		}
		return samples;
	}

	f32 MeanSquare(const AudioRenderer& renderer, u32 slot, u32 channel) {
		return std::bit_cast<f32>(renderer.GetMeters()[slot].output.meanSquare[channel].load(std::memory_order_relaxed));
	}

	f32 Peak(const AudioRenderer& renderer, u32 slot, u32 channel) {
		return std::bit_cast<f32>(renderer.GetMeters()[slot].output.peak[channel].load(std::memory_order_relaxed));
	}

	f32 FaderPeak(const AudioRenderer& renderer, u32 slot, u32 channel) {
		return std::bit_cast<f32>(renderer.GetMeters()[slot].fader.peak[channel].load(std::memory_order_relaxed));
	}

	f64 Average() { return 1.0 - std::exp(-BLOCK_SECONDS / 0.3); }
	f64 Release() { return std::pow(10.0, -(20.0 / 1.7) * BLOCK_SECONDS / 20.0); }

} // namespace

TEST(AudioMeterTest, NothingIsMeasuredOrAllocatedUntilAsked) {
	OfflineMixer mixer;
	const OfflineBus bus = mixer.Bus(1);
	ClipBlock clip(MakePcm(Sine(RATE, 0.5f, 0.5f), 2, RATE));
	AudioCommand play = mixer.Play(0, clip.Get());
	play.loop = true;
	ASSERT_TRUE(mixer.Push(OfflineMixer::Into(play, bus)));
	mixer.Render(8 * BLOCK);
	EXPECT_EQ(mixer.Get().GetMeters(), nullptr);

	ASSERT_TRUE(mixer.Get().EnableMetering(true));
	mixer.Render(BLOCK);
	ASSERT_NE(mixer.Get().GetMeters(), nullptr);
	EXPECT_EQ(Peak(mixer.Get(), bus.slot, 0), 0.5f);
}

TEST(AudioMeterTest, MeterReadsRmsAndPeak) {
	OfflineMixer mixer;
	const OfflineBus bus = mixer.Bus(1, {}, 0.5f);
	ClipBlock clip(MakePcm(Sine(RATE, 0.8f, 0.4f), 2, RATE));
	AudioCommand play = mixer.Play(0, clip.Get());
	play.loop = true;
	ASSERT_TRUE(mixer.Push(OfflineMixer::Into(play, bus)));
	ASSERT_TRUE(mixer.Get().EnableMetering(true));
	mixer.Render(3 * RATE / BLOCK * BLOCK);

	EXPECT_EQ(Peak(mixer.Get(), bus.slot, 0), 0.4f);
	EXPECT_EQ(Peak(mixer.Get(), bus.slot, 1), 0.2f);
	EXPECT_NEAR(std::sqrt(MeanSquare(mixer.Get(), bus.slot, 0)), 0.4f / std::sqrt(2.0f), 5e-5f);
	EXPECT_NEAR(std::sqrt(MeanSquare(mixer.Get(), bus.slot, 1)), 0.2f / std::sqrt(2.0f), 5e-5f);
	EXPECT_EQ(Peak(mixer.Get(), bus.slot, 2), 0.0f);
}

TEST(AudioMeterTest, AverageSettlesOverVuMetersIntegrationTime) {
	OfflineMixer mixer;
	const OfflineBus bus = mixer.Bus(1);
	ASSERT_TRUE(mixer.Get().EnableMetering(true));
	mixer.Render(BLOCK);
	ClipBlock clip(MakePcm(Sine(RATE, 0.5f, 0.5f), 2, RATE));
	AudioCommand play = mixer.Play(0, clip.Get());
	play.loop = true;
	ASSERT_TRUE(mixer.Push(OfflineMixer::Into(play, bus)));

	const f64 full = 0.125;
	for (u32 blocks = 1; blocks <= 60; ++blocks) {
		mixer.Render(BLOCK);
		const f64 expected = full * (1.0 - std::pow(1.0 - Average(), static_cast<f64>(blocks)));
		ASSERT_NEAR(MeanSquare(mixer.Get(), bus.slot, 0), expected, full * 1e-4) << "after " << blocks << " blocks";
	}
	const f64 atThreeHundred = 1.0 - std::exp(-60.0 * BLOCK_SECONDS / 0.3);
	EXPECT_NEAR(MeanSquare(mixer.Get(), bus.slot, 0) / full, atThreeHundred, 1e-3);
}

TEST(AudioMeterTest, PeakFallsOnSkippedBus) {
	OfflineMixer mixer;
	const OfflineBus bus = mixer.Bus(1);
	ClipBlock clip(MakePcm(Sine(RATE, 0.5f, 0.5f), 2, RATE));
	AudioCommand play = mixer.Play(0, clip.Get());
	play.loop = true;
	ASSERT_TRUE(mixer.Push(OfflineMixer::Into(play, bus)));
	ASSERT_TRUE(mixer.Get().EnableMetering(true));
	mixer.Render(16 * BLOCK);
	ASSERT_TRUE(mixer.Push(OfflineMixer::To(play, AudioCommandKind::Stop)));
	mixer.Render(2 * BLOCK);
	const f32 start = Peak(mixer.Get(), bus.slot, 0);
	const f32 average = MeanSquare(mixer.Get(), bus.slot, 0);
	ASSERT_GT(start, 0.0f);
	ASSERT_GT(average, 0.0f);

	const u32 blocks = 319;
	mixer.Render(blocks * BLOCK);
	const f64 fell = 20.0 * std::log10(static_cast<f64>(Peak(mixer.Get(), bus.slot, 0)) / static_cast<f64>(start));
	EXPECT_NEAR(fell, 20.0 * std::log10(std::pow(Release(), static_cast<f64>(blocks))), 1e-3);
	EXPECT_NEAR(fell, -20.0, 0.05);
	EXPECT_NEAR(MeanSquare(mixer.Get(), bus.slot, 0) / average, std::pow(1.0 - Average(), static_cast<f64>(blocks)), 1e-4);
}

TEST(AudioMeterTest, TurnedOffMeterKeepsLastReading) {
	OfflineMixer mixer;
	const OfflineBus bus = mixer.Bus(1);
	ClipBlock clip(MakePcm(Sine(RATE, 0.5f, 0.5f), 2, RATE));
	AudioCommand play = mixer.Play(0, clip.Get());
	play.loop = true;
	ASSERT_TRUE(mixer.Push(OfflineMixer::Into(play, bus)));
	ASSERT_TRUE(mixer.Get().EnableMetering(true));
	mixer.Render(8 * BLOCK);
	const f32 measured = MeanSquare(mixer.Get(), bus.slot, 0);
	ASSERT_GT(measured, 0.0f);

	ASSERT_TRUE(mixer.Get().EnableMetering(false));
	mixer.Render(5 * BLOCK);
	EXPECT_EQ(MeanSquare(mixer.Get(), bus.slot, 0), measured);
	EXPECT_NE(mixer.Get().GetMeters(), nullptr);
}

TEST(AudioMeterTest, BusNewToSlotStartsAtSilence) {
	OfflineMixer mixer;
	const OfflineBus first = mixer.Bus(1);
	ClipBlock clip(MakePcm(Sine(RATE, 0.5f, 0.5f), 2, RATE));
	AudioCommand play = mixer.Play(0, clip.Get());
	play.loop = true;
	ASSERT_TRUE(mixer.Push(OfflineMixer::Into(play, first)));
	ASSERT_TRUE(mixer.Get().EnableMetering(true));
	mixer.Render(8 * BLOCK);
	ASSERT_TRUE(mixer.Push(OfflineMixer::To(play, AudioCommandKind::Stop)));
	ASSERT_TRUE(mixer.Push(OfflineMixer::BusCommand(AudioCommandKind::DestroyBus, first)));
	mixer.Render(4 * BLOCK);
	mixer.TakeEvents();
	ASSERT_GT(Peak(mixer.Get(), first.slot, 0), 0.0f);
	ASSERT_GT(FaderPeak(mixer.Get(), first.slot, 0), 0.0f);

	const OfflineBus second = mixer.Bus(1);
	mixer.Render(BLOCK);
	EXPECT_EQ(Peak(mixer.Get(), second.slot, 0), 0.0f);
	EXPECT_EQ(MeanSquare(mixer.Get(), second.slot, 0), 0.0f);
	EXPECT_EQ(FaderPeak(mixer.Get(), second.slot, 0), 0.0f);
}

TEST(AudioMeterTest, FaderPointReadsFaderOutput) {
	OfflineMixer mixer;
	const OfflineBus target = mixer.Bus(2, {}, 0.5f);
	ASSERT_TRUE(mixer.Receives(target, 1));
	ASSERT_TRUE(mixer.Push(OfflineMixer::Row(target, 0, AudioBusRowKind::Fader)));
	ASSERT_TRUE(mixer.Push(OfflineMixer::Row(target, 1, AudioBusRowKind::Receive, {}, 0.0f, nullptr, false, false, 0)));
	ASSERT_TRUE(mixer.Push(OfflineMixer::RowCount(target, 2)));
	const OfflineBus sender = mixer.Bus(1);
	ASSERT_TRUE(mixer.Push(OfflineMixer::Row(sender, 0, AudioBusRowKind::Send, target, 1.0f, nullptr, false, false, 1)));
	ASSERT_TRUE(mixer.Push(OfflineMixer::Row(sender, 1, AudioBusRowKind::Fader)));
	ASSERT_TRUE(mixer.Push(OfflineMixer::RowCount(sender, 2)));
	ClipBlock own(MakePcm(Sine(RATE, 0.8f, 0.8f), 2, RATE));
	ClipBlock sent(MakePcm(Sine(RATE, 0.3f, 0.3f), 2, RATE));
	AudioCommand playOwn = mixer.Play(0, own.Get());
	playOwn.loop = true;
	AudioCommand playSent = mixer.Play(1, sent.Get());
	playSent.loop = true;
	ASSERT_TRUE(mixer.Push(OfflineMixer::Into(playOwn, target)));
	ASSERT_TRUE(mixer.Push(OfflineMixer::Into(playSent, sender)));
	ASSERT_TRUE(mixer.Get().EnableMetering(true));
	mixer.Render(16 * BLOCK);

	EXPECT_EQ(FaderPeak(mixer.Get(), target.slot, 0), 0.4f);
	EXPECT_NEAR(Peak(mixer.Get(), target.slot, 0), 0.7f, 1e-6f);
}

namespace {

	class AudioMeterSystemFixture : public LivePackageFixture {
	protected:
		void SetUp() override {
			LivePackageFixture::SetUp();
			GetAudioSystem().SetListenerPresent(true);
		}

		void TearDown() override {
			while (GetAudioSystem().IsMetering()) {
				GetAudioSystem().ReleaseMetering();
			}
			LivePackageFixture::TearDown();
		}

		AudioBus ToneBus() {
			AudioBusDesc desc;
			desc.fader = 0.5f;
			const AudioBus bus = GetAudioSystem().CreateBus(desc);
			AudioPlayParams params;
			params.loop = true;
			params.output = bus;
			Audio::Play(ResourceOf(MakeClip(96000, 2, 0.5f)), params);
			return bus;
		}

		static bool LevelNear(AudioBus bus, f32 expected) {
			return WaitFor([bus, expected]() {
				AudioBusLevel level;
				return GetAudioSystem().ReadBusLevel(bus, level) && std::fabs(level.peak[0] - expected) < 1e-4f &&
					   std::fabs(level.rms[0] - expected) < 1e-3f;
			});
		}
	};

} // namespace

TEST_F(AudioMeterSystemFixture, MeterSurvivesOneReleaser) {
	const AudioBus bus = ToneBus();
	AudioBusLevel level;
	EXPECT_FALSE(GetAudioSystem().ReadBusLevel(bus, level));

	GetAudioSystem().AcquireMetering();
	GetAudioSystem().AcquireMetering();
	GetAudioSystem().ReleaseMetering();
	EXPECT_TRUE(GetAudioSystem().IsMetering());
	EXPECT_TRUE(LevelNear(bus, 0.25f));
	ASSERT_TRUE(GetAudioSystem().ReadBusLevel(bus, level));
	EXPECT_EQ(level.channels, 2u);

	GetAudioSystem().ReleaseMetering();
	EXPECT_FALSE(GetAudioSystem().IsMetering());
	EXPECT_FALSE(GetAudioSystem().ReadBusLevel(bus, level));
	GetAudioSystem().ReleaseMetering();
	EXPECT_FALSE(GetAudioSystem().IsMetering());
}

TEST_F(AudioMeterSystemFixture, ABusThatHasGoneReadsNothing) {
	const AudioBus bus = ToneBus();
	GetAudioSystem().AcquireMetering();
	ASSERT_TRUE(LevelNear(bus, 0.25f));
	GetAudioSystem().DestroyBus(bus);
	AudioBusLevel level;
	EXPECT_FALSE(GetAudioSystem().ReadBusLevel(bus, level));
}

#endif
