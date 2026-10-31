#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioOfflineMixer.h"

#include "AudioQoa.h"

#include "ThirdParty/qoa.h"

#include <cmath>
#include <cstring>
#include <vector>

using namespace AudioTesting;

extern "C" void SltAudioQoaFree(void* block);

namespace {

	constexpr u32 BLOCK = OfflineMixer::BLOCK;

	std::vector<f32> Counter(u32 frames) {
		std::vector<f32> samples(frames);
		for (u32 frame = 0; frame < frames; ++frame) {
			samples[frame] = static_cast<f32>(frame + 1) * 1e-4f;
		}
		return samples;
	}

	std::vector<f32> Constant(u32 frames, f32 value) { return std::vector<f32>(frames, value); }

	std::vector<f32> StereoNoise(u32 frames, u32 seed) {
		std::vector<f32> samples(static_cast<size_t>(frames) * 2);
		u32 state = seed;
		for (f32& sample : samples) {
			state = state * 1664525u + 1013904223u;
			sample = static_cast<f32>(static_cast<i32>(state >> 8) - (1 << 23)) / static_cast<f32>(1 << 24);
		}
		return samples;
	}

} // namespace

TEST(AudioMixTest, UnpitchedVoiceIsBitExact) {
	OfflineMixer mixer;
	const std::vector<f32> samples = StereoNoise(500, 11);
	ClipBlock clip(MakePcm(samples, 2, 48000));
	ASSERT_TRUE(clip);

	ASSERT_TRUE(mixer.Push(mixer.Play(0, clip.Get())));
	const std::vector<f32> out = mixer.Render(3 * BLOCK);
	u32 mismatches = 0;
	for (size_t index = 0; index < samples.size(); ++index) {
		if (out[index] != samples[index]) {
			++mismatches;
		}
	}
	EXPECT_EQ(mismatches, 0u);
	for (size_t index = samples.size(); index < out.size(); ++index) {
		ASSERT_EQ(out[index], 0.0f) << index;
	}

	const std::vector<AudioEvent> events = mixer.TakeEvents();
	ASSERT_EQ(events.size(), 1u);
	EXPECT_EQ(events[0].slot, 0u);
}

TEST(AudioMixTest, MonoIsMinus3DbEachSide) {
	OfflineMixer mixer;
	const std::vector<f32> samples = Counter(256);
	ClipBlock clip(MakePcm(samples, 1, 48000));
	ASSERT_TRUE(mixer.Push(mixer.Play(0, clip.Get())));
	const std::vector<f32> out = mixer.Render(BLOCK);
	const f32 fold = std::sqrt(0.5f);
	for (u32 frame = 0; frame < 256; ++frame) {
		ASSERT_EQ(At(out, 2, frame, 0), samples[frame] * fold) << "left, frame " << frame;
		ASSERT_EQ(At(out, 2, frame, 1), samples[frame] * fold) << "right, frame " << frame;
	}
}

TEST(AudioMixTest, OtherRateKeepsPitchAndDuration) {
	OfflineMixer mixer(1);
	std::vector<f32> samples(2400);
	for (u32 frame = 0; frame < 2400; ++frame) {
		samples[frame] = 0.5f * static_cast<f32>(std::sin(2.0 * 3.14159265358979 * 1000.0 * static_cast<f64>(frame) / 24000.0));
	}
	ClipBlock clip(MakePcm(samples, 1, 24000));
	ASSERT_TRUE(mixer.Push(mixer.Play(0, clip.Get())));
	const std::vector<f32> out = mixer.Render(20 * BLOCK);

	u32 crossings = 0;
	u64 last = 0;
	for (size_t index = 1; index < out.size(); ++index) {
		if ((out[index - 1] < 0.0f) != (out[index] < 0.0f)) {
			++crossings;
		}
		if (out[index] != 0.0f) {
			last = index;
		}
	}
	EXPECT_NEAR(static_cast<f64>(crossings), 200.0, 2.0);
	EXPECT_NEAR(static_cast<f64>(last), 4800.0, 3.0);
}

TEST(AudioMixTest, ALoopingVoiceWrapsSeamlessly) {
	OfflineMixer mixer(1);
	const std::vector<f32> samples = Counter(100);
	ClipBlock clip(MakePcm(samples, 1, 48000));
	AudioCommand play = mixer.Play(0, clip.Get());
	play.loop = true;
	ASSERT_TRUE(mixer.Push(play));
	const std::vector<f32> out = mixer.Render(4 * BLOCK);
	for (size_t index = 0; index < out.size(); ++index) {
		ASSERT_EQ(out[index], samples[index % 100]) << index;
	}
	EXPECT_TRUE(mixer.TakeEvents().empty());
}

TEST(AudioMixTest, PauseAndResumeFade) {
	OfflineMixer mixer(1);
	const std::vector<f32> samples = Counter(5000);
	ClipBlock clip(MakePcm(samples, 1, 48000));
	const AudioCommand play = mixer.Play(0, clip.Get());
	ASSERT_TRUE(mixer.Push(play));
	mixer.Render(BLOCK);

	ASSERT_TRUE(mixer.Push(OfflineMixer::To(play, AudioCommandKind::Pause)));
	const std::vector<f32> fading = mixer.Render(BLOCK);
	for (u32 frame = 0; frame < BLOCK; ++frame) {
		const f32 gain = 1.0f + (-1.0f / static_cast<f32>(BLOCK)) * static_cast<f32>(frame + 1);
		ASSERT_EQ(fading[frame], samples[256 + frame] * gain) << frame;
	}
	EXPECT_EQ(fading[BLOCK - 1], 0.0f);

	const std::vector<f32> held = mixer.Render(2 * BLOCK);
	for (f32 sample : held) {
		ASSERT_EQ(sample, 0.0f);
	}

	ASSERT_TRUE(mixer.Push(OfflineMixer::To(play, AudioCommandKind::Resume)));
	const std::vector<f32> resumed = mixer.Render(2 * BLOCK);
	for (u32 frame = 0; frame < BLOCK; ++frame) {
		const f32 gain = (1.0f / static_cast<f32>(BLOCK)) * static_cast<f32>(frame + 1);
		ASSERT_EQ(resumed[frame], samples[512 + frame] * gain) << frame;
	}
	for (u32 frame = BLOCK; frame < 2 * BLOCK; ++frame) {
		ASSERT_EQ(resumed[frame], samples[512 + frame]);
	}
}

TEST(AudioMixTest, StopFadesOverOneBlock) {
	OfflineMixer mixer(1);
	ClipBlock clip(MakePcm(Constant(1000, 0.5f), 1, 48000));
	AudioCommand play = mixer.Play(0, clip.Get());
	play.loop = true;
	ASSERT_TRUE(mixer.Push(play));
	mixer.Render(BLOCK);

	ASSERT_TRUE(mixer.Push(OfflineMixer::To(play, AudioCommandKind::Stop)));
	const std::vector<f32> fading = mixer.Render(BLOCK);
	EXPECT_EQ(fading[0], 0.5f * (1.0f - 1.0f / static_cast<f32>(BLOCK)));
	EXPECT_EQ(fading[BLOCK - 1], 0.0f);
	const std::vector<AudioEvent> events = mixer.TakeEvents();
	ASSERT_EQ(events.size(), 1u);
	EXPECT_EQ(events[0].generation, play.generation);

	for (f32 sample : mixer.Render(BLOCK)) {
		ASSERT_EQ(sample, 0.0f);
	}
}

TEST(AudioMixTest, VolumeRampsAcrossBlock) {
	OfflineMixer mixer(1);
	ClipBlock clip(MakePcm(Constant(1000, 0.5f), 1, 48000));
	AudioCommand play = mixer.Play(0, clip.Get());
	play.loop = true;
	ASSERT_TRUE(mixer.Push(play));
	mixer.Render(BLOCK);

	ASSERT_TRUE(mixer.Push(OfflineMixer::To(play, AudioCommandKind::SetVolume, 0.25f)));
	const std::vector<f32> ramp = mixer.Render(BLOCK);
	const f32 step = (0.25f - 1.0f) / static_cast<f32>(BLOCK);
	for (u32 frame = 0; frame < BLOCK; ++frame) {
		ASSERT_EQ(ramp[frame], 0.5f * (1.0f + step * static_cast<f32>(frame + 1))) << "frame " << frame;
	}
	for (f32 sample : mixer.Render(BLOCK)) {
		ASSERT_EQ(sample, 0.125f);
	}
}

TEST(AudioMixTest, ListenerGateSilencesKeepsTime) {
	OfflineMixer mixer(1, 48000, false);
	ClipBlock loop(MakePcm(Constant(1000, 0.5f), 1, 48000));
	ClipBlock shortClip(MakePcm(Constant(300, 0.25f), 1, 48000));
	AudioCommand looping = mixer.Play(0, loop.Get());
	looping.loop = true;
	ASSERT_TRUE(mixer.Push(looping));
	ASSERT_TRUE(mixer.Push(mixer.Play(1, shortClip.Get())));

	for (f32 sample : mixer.Render(2 * BLOCK)) {
		ASSERT_EQ(sample, 0.0f);
	}
	const std::vector<AudioEvent> events = mixer.TakeEvents();
	ASSERT_EQ(events.size(), 1u);
	EXPECT_EQ(events[0].slot, 1u);

	mixer.SetListener(true);
	const std::vector<f32> opening = mixer.Render(BLOCK);
	for (u32 frame = 0; frame < BLOCK; ++frame) {
		ASSERT_EQ(opening[frame], 0.5f * (static_cast<f32>(frame + 1) / static_cast<f32>(BLOCK))) << "frame " << frame;
	}
	for (f32 sample : mixer.Render(BLOCK)) {
		ASSERT_EQ(sample, 0.5f);
	}
}

TEST(AudioMixTest, TwoVoicesSum) {
	const std::vector<f32> a = StereoNoise(700, 1);
	const std::vector<f32> b = StereoNoise(900, 2);
	ClipBlock clipA(MakePcm(a, 2, 48000));
	ClipBlock clipB(MakePcm(b, 2, 48000));

	auto render = [](std::initializer_list<const AudioClipData*> clips) {
		OfflineMixer mixer;
		u32 slot = 0;
		for (const AudioClipData* clip : clips) {
			mixer.Push(mixer.Play(slot++, clip));
		}
		return mixer.Render(4 * BLOCK);
	};
	const std::vector<f32> alone0 = render({clipA.Get()});
	const std::vector<f32> alone1 = render({clipB.Get()});
	const std::vector<f32> together = render({clipA.Get(), clipB.Get()});
	for (size_t index = 0; index < together.size(); ++index) {
		ASSERT_EQ(together[index], alone0[index] + alone1[index]) << "sample " << index;
	}
}

TEST(AudioMixTest, CompressedVoiceMatchesDecode) {
	const u32 frames = 5120 * 2 + 311;
	std::vector<i16> signal(static_cast<size_t>(frames) * 2);
	for (u32 frame = 0; frame < frames; ++frame) {
		signal[frame * 2] = static_cast<i16>(12000.0 * std::sin(static_cast<f64>(frame) * 0.021));
		signal[frame * 2 + 1] = static_cast<i16>(9000.0 * std::sin(static_cast<f64>(frame) * 0.047));
	}
	qoa_desc description;
	std::memset(&description, 0, sizeof(description));
	description.channels = 2;
	description.samplerate = 44100;
	description.samples = frames;
	unsigned int size = 0;
	void* bytes = qoa_encode(signal.data(), &description, &size);
	ASSERT_NE(bytes, nullptr);
	ClipBlock compressed(CreateQoaClipData(static_cast<const u8*>(bytes), size, MakeStandardLayout(2)));
	SltAudioQoaFree(bytes);
	ASSERT_TRUE(compressed);
	ClipBlock decoded(DecodeToPcm(*compressed.Get()));
	ASSERT_TRUE(decoded);

	for (f32 pitch : {1.0f, 0.73f, 1.91f}) {
		auto render = [pitch](const AudioClipData* clip) {
			OfflineMixer mixer;
			AudioCommand play = mixer.Play(0, clip);
			play.pitch = pitch;
			play.loop = true;
			mixer.Push(play);
			return mixer.Render(100 * BLOCK);
		};
		const std::vector<f32> fromCompressed = render(compressed.Get());
		const std::vector<f32> fromDecoded = render(decoded.Get());
		u32 mismatches = 0;
		bool heard = false;
		for (size_t index = 0; index < fromDecoded.size(); ++index) {
			mismatches += fromCompressed[index] != fromDecoded[index] ? 1u : 0u;
			heard = heard || fromDecoded[index] != 0.0f;
		}
		EXPECT_TRUE(heard) << "pitch " << pitch;
		EXPECT_EQ(mismatches, 0u) << "pitch " << pitch;
	}
}

TEST(AudioMixTest, ASeekContinuesFromTheFrameAsked) {
	OfflineMixer mixer(1);
	const std::vector<f32> samples = Counter(8000);
	ClipBlock clip(MakePcm(samples, 1, 48000));
	const AudioCommand play = mixer.Play(0, clip.Get());
	ASSERT_TRUE(mixer.Push(play));
	mixer.Render(BLOCK);

	AudioCommand seek = OfflineMixer::To(play, AudioCommandKind::Seek);
	seek.frame = 4000;
	ASSERT_TRUE(mixer.Push(seek));
	const std::vector<f32> out = mixer.Render(BLOCK);
	for (u32 frame = 0; frame < BLOCK; ++frame) {
		ASSERT_EQ(out[frame], samples[4000 + frame]) << "frame " << frame;
	}
}

TEST(AudioMixTest, EndedVoiceCommandSkipsNextOccupant) {
	OfflineMixer mixer(1);
	ClipBlock first(MakePcm(Constant(100, 0.25f), 1, 48000));
	ClipBlock second(MakePcm(Constant(1000, 0.5f), 1, 48000));
	const AudioCommand playFirst = mixer.Play(0, first.Get());
	ASSERT_TRUE(mixer.Push(playFirst));
	mixer.Render(BLOCK);
	ASSERT_EQ(mixer.TakeEvents().size(), 1u);

	AudioCommand playSecond = mixer.Play(0, second.Get());
	playSecond.loop = true;
	ASSERT_TRUE(mixer.Push(playSecond));
	ASSERT_TRUE(mixer.Push(OfflineMixer::To(playFirst, AudioCommandKind::SetVolume, 0.0f)));
	ASSERT_TRUE(mixer.Push(OfflineMixer::To(playFirst, AudioCommandKind::Stop)));
	for (f32 sample : mixer.Render(2 * BLOCK)) {
		ASSERT_EQ(sample, 0.5f);
	}
	EXPECT_TRUE(mixer.TakeEvents().empty());
}

TEST(AudioMixTest, ANegativePitchPlaysTheClipBackwards) {
	OfflineMixer mixer(1);
	const std::vector<f32> samples = Counter(600);
	ClipBlock clip(MakePcm(samples, 1, 48000));
	AudioCommand play = mixer.Play(0, clip.Get(), 0, 599);
	play.pitch = -1.0f;
	ASSERT_TRUE(mixer.Push(play));
	const std::vector<f32> out = mixer.Render(3 * BLOCK);
	for (u32 frame = 0; frame < 600; ++frame) {
		ASSERT_EQ(out[frame], samples[599 - frame]) << "frame " << frame;
	}
	EXPECT_EQ(out[600], 0.0f);
	EXPECT_EQ(mixer.TakeEvents().size(), 1u);
}

TEST(AudioMixTest, ReopenedDeviceContinuesVoices) {
	OfflineMixer mixer(1);
	const std::vector<f32> samples = Counter(4000);
	ClipBlock clip(MakePcm(samples, 1, 48000));
	ASSERT_TRUE(mixer.Push(mixer.Play(0, clip.Get())));
	mixer.Render(BLOCK);

	mixer.Get().Release();
	ASSERT_TRUE(mixer.Get().Prepare(MakeStandardLayout(1), 24000));
	std::vector<f32> out(BLOCK, 0.0f);
	mixer.Get().Render(out.data(), BLOCK);
	for (u32 frame = 0; frame < BLOCK; ++frame) {
		ASSERT_EQ(out[frame], samples[256 + 2 * frame]) << "frame " << frame;
	}
}

TEST(AudioMixTest, GainOneBusesAreBitExact) {
	OfflineMixer mixer;
	const std::vector<f32> samples = StereoNoise(700, 5);
	ClipBlock clip(MakePcm(samples, 2, 48000));
	const OfflineBus outer = mixer.Bus(3);
	const OfflineBus inner = mixer.Bus(7, outer);
	ASSERT_TRUE(mixer.Push(OfflineMixer::Into(mixer.Play(0, clip.Get()), inner)));
	const std::vector<f32> out = mixer.Render(3 * BLOCK);
	for (size_t index = 0; index < samples.size(); ++index) {
		ASSERT_EQ(out[index], samples[index]) << "sample " << index;
	}
}

TEST(AudioMixTest, BusFaderRampsOneBlock) {
	OfflineMixer mixer(1);
	ClipBlock clip(MakePcm(Constant(4000, 0.5f), 1, 48000));
	const OfflineBus bus = mixer.Bus(1);
	AudioCommand play = OfflineMixer::Into(mixer.Play(0, clip.Get()), bus);
	play.loop = true;
	ASSERT_TRUE(mixer.Push(play));
	mixer.Render(BLOCK);

	ASSERT_TRUE(mixer.Push(OfflineMixer::BusCommand(AudioCommandKind::SetBusFader, bus, 0.5f, BLOCK)));
	const std::vector<f32> walk = mixer.Render(BLOCK);
	for (u32 frame = 0; frame < BLOCK; ++frame) {
		const f32 gain = 1.0f + (0.5f - 1.0f) * (static_cast<f32>(frame + 1) / static_cast<f32>(BLOCK));
		ASSERT_EQ(walk[frame], 0.5f * gain) << "frame " << frame;
	}
	for (f32 sample : mixer.Render(BLOCK)) {
		ASSERT_EQ(sample, 0.25f);
	}
}

TEST(AudioMixTest, BusesSumFadersMultiply) {
	OfflineMixer mixer;
	const std::vector<f32> a = StereoNoise(900, 21);
	const std::vector<f32> b = StereoNoise(900, 22);
	const std::vector<f32> c = StereoNoise(900, 23);
	ClipBlock clipA(MakePcm(a, 2, 48000));
	ClipBlock clipB(MakePcm(b, 2, 48000));
	ClipBlock clipC(MakePcm(c, 2, 48000));
	const OfflineBus left = mixer.Bus(1, {}, 0.5f);
	const OfflineBus right = mixer.Bus(2, {}, 0.25f);
	const OfflineBus inside = mixer.Bus(3, left, 0.5f);
	ASSERT_TRUE(mixer.Push(OfflineMixer::Into(mixer.Play(0, clipA.Get()), left)));
	ASSERT_TRUE(mixer.Push(OfflineMixer::Into(mixer.Play(1, clipB.Get()), right)));
	ASSERT_TRUE(mixer.Push(OfflineMixer::Into(mixer.Play(2, clipC.Get()), inside)));
	const std::vector<f32> out = mixer.Render(4 * BLOCK);
	ASSERT_GE(out.size(), a.size());
	for (size_t index = 0; index < a.size(); ++index) {
		const f32 leftBus = (a[index] + c[index] * 0.5f) * 0.5f;
		const f32 expected = leftBus + b[index] * 0.25f;
		ASSERT_NEAR(out[index], expected, 1e-7f) << "sample " << index;
	}
}

TEST(AudioMixTest, PostFaderSendCarriesFader) {
	for (const bool preFader : {false, true}) {
		OfflineMixer mixer(1);
		ClipBlock clip(MakePcm(Constant(2000, 0.5f), 1, 48000));
		const OfflineBus reverb = mixer.Bus(2);
		const OfflineBus dry = mixer.Bus(1, {}, 0.5f);
		ASSERT_TRUE(mixer.Send(dry, reverb, preFader, 0.5f));
		ASSERT_TRUE(mixer.Push(OfflineMixer::Into(mixer.Play(0, clip.Get()), dry)));
		const std::vector<f32> out = mixer.Render(BLOCK);
		const f32 expected = preFader ? 0.5f * 0.5f + 0.5f * 0.5f : 0.5f * 0.5f + (0.5f * 0.5f) * 0.5f;
		for (f32 sample : out) {
			ASSERT_EQ(sample, expected) << (preFader ? "pre" : "post") << "-fader send";
		}
	}
}

TEST(AudioMixTest, SendToLowerSlotHeard) {
	OfflineMixer mixer(1);
	ClipBlock clip(MakePcm(Constant(2000, 0.5f), 1, 48000));
	const OfflineBus target = mixer.Bus(1, {}, 1.0f);
	const OfflineBus sender = mixer.Bus(200, {}, 0.0f);
	ASSERT_TRUE(mixer.Send(sender, target, true, 1.0f));
	ASSERT_TRUE(mixer.Push(OfflineMixer::Into(mixer.Play(0, clip.Get()), sender)));
	for (f32 sample : mixer.Render(BLOCK)) {
		ASSERT_EQ(sample, 0.5f);
	}
}

TEST(AudioMixTest, MuteSilencesBusAndSends) {
	OfflineMixer mixer(1);
	ClipBlock clip(MakePcm(Constant(4000, 0.5f), 1, 48000));
	const OfflineBus reverb = mixer.Bus(2);
	const OfflineBus dry = mixer.Bus(1);
	ASSERT_TRUE(mixer.Send(dry, reverb, false, 1.0f));
	AudioCommand play = OfflineMixer::Into(mixer.Play(0, clip.Get()), dry);
	play.loop = true;
	ASSERT_TRUE(mixer.Push(play));
	for (f32 sample : mixer.Render(BLOCK)) {
		ASSERT_EQ(sample, 1.0f);
	}
	ASSERT_TRUE(mixer.Push(OfflineMixer::Flag(AudioCommandKind::SetBusMute, dry, true)));
	const std::vector<f32> walk = mixer.Render(BLOCK);
	EXPECT_GT(walk[0], 0.0f);
	EXPECT_EQ(walk[BLOCK - 1], 0.0f);
	for (f32 sample : mixer.Render(BLOCK)) {
		ASSERT_EQ(sample, 0.0f);
	}
}

TEST(AudioMixTest, SoloHearsOnlySoloed) {
	OfflineMixer mixer(1);
	ClipBlock soloed(MakePcm(Constant(4000, 0.25f), 1, 48000));
	ClipBlock other(MakePcm(Constant(4000, 0.125f), 1, 48000));
	ClipBlock loose(MakePcm(Constant(4000, 0.0625f), 1, 48000));
	const OfflineBus reverb = mixer.Bus(3);
	const OfflineBus a = mixer.Bus(1);
	const OfflineBus b = mixer.Bus(2);
	ASSERT_TRUE(mixer.Send(a, reverb, false, 1.0f));
	const auto loop = [&](u32 slot, const ClipBlock& clip, OfflineBus bus) {
		AudioCommand play = mixer.Play(slot, clip.Get());
		play.loop = true;
		return mixer.Push(bus.slot == AudioRenderer::ROOT_BUS ? play : OfflineMixer::Into(play, bus));
	};
	ASSERT_TRUE(loop(0, soloed, a));
	ASSERT_TRUE(loop(1, other, b));
	ASSERT_TRUE(loop(2, loose, OfflineBus{}));
	for (f32 sample : mixer.Render(BLOCK)) {
		ASSERT_EQ(sample, 0.25f * 2.0f + 0.125f + 0.0625f);
	}

	ASSERT_TRUE(mixer.Push(OfflineMixer::Flag(AudioCommandKind::SetBusSolo, a, true)));
	mixer.Render(BLOCK);
	for (f32 sample : mixer.Render(BLOCK)) {
		ASSERT_EQ(sample, 0.25f * 2.0f);
	}

	ASSERT_TRUE(mixer.Push(OfflineMixer::Flag(AudioCommandKind::SetBusSolo, a, false)));
	mixer.Render(BLOCK);
	for (f32 sample : mixer.Render(BLOCK)) {
		ASSERT_EQ(sample, 0.25f * 2.0f + 0.125f + 0.0625f);
	}
}

TEST(AudioMixTest, BusPitchMultipliesRate) {
	OfflineMixer mixer(1);
	const std::vector<f32> samples = Counter(4000);
	ClipBlock clip(MakePcm(samples, 1, 48000));
	const OfflineBus doubled = mixer.Bus(1, {}, 1.0f, 2.0f);
	ASSERT_TRUE(mixer.Push(OfflineMixer::Into(mixer.Play(0, clip.Get()), doubled)));
	const std::vector<f32> fast = mixer.Render(BLOCK);
	for (u32 frame = 0; frame < BLOCK; ++frame) {
		ASSERT_EQ(fast[frame], samples[2 * frame]) << "frame " << frame;
	}

	OfflineMixer nested(1);
	const OfflineBus outer = nested.Bus(1, {}, 1.0f, 2.0f);
	const OfflineBus inner = nested.Bus(2, outer, 1.0f, 1.5f);
	ASSERT_TRUE(nested.Push(OfflineMixer::Into(nested.Play(0, clip.Get()), inner)));
	const std::vector<f32> faster = nested.Render(BLOCK);
	for (u32 frame = 0; frame < BLOCK; ++frame) {
		ASSERT_EQ(faster[frame], samples[3 * frame]) << "frame " << frame;
	}
}

TEST(AudioMixTest, DestroyedBusRoutesThroughParent) {
	OfflineMixer mixer(1);
	ClipBlock clip(MakePcm(Constant(8000, 0.5f), 1, 48000));
	const OfflineBus parent = mixer.Bus(1, {}, 0.5f);
	const OfflineBus middle = mixer.Bus(2, parent, 0.5f);
	const OfflineBus child = mixer.Bus(3, middle, 1.0f);
	ASSERT_TRUE(mixer.Push(OfflineMixer::Into(mixer.Play(0, clip.Get()), middle)));
	ASSERT_TRUE(mixer.Push(OfflineMixer::Into(mixer.Play(1, clip.Get()), child)));
	for (f32 sample : mixer.Render(BLOCK)) {
		ASSERT_EQ(sample, (0.5f + 0.5f) * 0.5f * 0.5f);
	}

	ASSERT_TRUE(mixer.Push(OfflineMixer::BusCommand(AudioCommandKind::DestroyBus, middle)));
	for (f32 sample : mixer.Render(BLOCK)) {
		ASSERT_EQ(sample, (0.5f + 0.5f) * 0.5f);
	}
	const std::vector<AudioEvent> events = mixer.TakeEvents();
	ASSERT_EQ(events.size(), 1u);
	EXPECT_EQ(events[0].kind, AudioEventKind::BusReleased);
	EXPECT_EQ(events[0].slot, middle.slot);
	EXPECT_EQ(events[0].generation, middle.generation);

	ASSERT_TRUE(mixer.Push(OfflineMixer::BusCommand(AudioCommandKind::SetBusFader, middle, 0.0f)));
	for (f32 sample : mixer.Render(BLOCK)) {
		ASSERT_EQ(sample, (0.5f + 0.5f) * 0.5f);
	}
}

TEST(AudioMixTest, MovedVoiceMixesNextBlock) {
	OfflineMixer mixer(1);
	ClipBlock clip(MakePcm(Constant(8000, 0.5f), 1, 48000));
	const OfflineBus quiet = mixer.Bus(1, {}, 0.5f);
	const OfflineBus quieter = mixer.Bus(2, {}, 0.25f);
	const AudioCommand play = OfflineMixer::Into(mixer.Play(0, clip.Get()), quiet);
	ASSERT_TRUE(mixer.Push(play));
	for (f32 sample : mixer.Render(BLOCK)) {
		ASSERT_EQ(sample, 0.25f);
	}
	AudioCommand move = OfflineMixer::To(play, AudioCommandKind::SetVoiceOutput);
	move.bus = quieter.slot;
	move.busGeneration = quieter.generation;
	ASSERT_TRUE(mixer.Push(move));
	for (f32 sample : mixer.Render(BLOCK)) {
		ASSERT_EQ(sample, 0.125f);
	}
}

TEST(AudioMixTest, GlobalVolumeScalesListenerInput) {
	OfflineMixer mixer(1);
	ClipBlock clip(MakePcm(Constant(8000, 0.5f), 1, 48000));
	AudioCommand play = mixer.Play(0, clip.Get());
	play.loop = true;
	ASSERT_TRUE(mixer.Push(play));
	mixer.Render(BLOCK);
	AudioCommand volume;
	volume.kind = AudioCommandKind::SetGlobalVolume;
	volume.value = 0.5f;
	ASSERT_TRUE(mixer.Push(volume));
	const std::vector<f32> walk = mixer.Render(BLOCK);
	for (u32 frame = 0; frame < BLOCK; ++frame) {
		const f32 gain = 1.0f + (0.5f - 1.0f) * (static_cast<f32>(frame + 1) / static_cast<f32>(BLOCK));
		ASSERT_EQ(walk[frame], 0.5f * gain) << "frame " << frame;
	}
	for (f32 sample : mixer.Render(BLOCK)) {
		ASSERT_EQ(sample, 0.25f);
	}
}

#endif
