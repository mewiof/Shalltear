#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioLiveFixture.h"
#include "AudioOfflineMixer.h"
#include "AudioOutputs.h"
#include "AudioPlayback.h"

#include "Shalltear/Audio/Audio.h"

#include <vector>

using namespace AudioTesting;

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

} // namespace

TEST(AudioTimeTest, StoppedGameHoldsBuses) {
	OfflineMixer mixer(1);
	const std::vector<f32> samples = Counter(20000);
	ClipBlock clip(MakePcm(samples, 1, 48000));
	const OfflineBus wall = mixer.Bus(1, {}, 1.0f, 1.0f, AudioBusClock::Wall);
	const AudioCommand onWall = OfflineMixer::Into(mixer.Play(0, clip.Get()), wall);
	ASSERT_TRUE(mixer.Push(onWall));
	mixer.Render(BLOCK);

	ASSERT_TRUE(mixer.GameClock(false, 1.0f));
	const std::vector<f32> fading = mixer.Render(BLOCK);
	for (u32 frame = 0; frame < BLOCK; ++frame) {
		const f32 gain = 1.0f + (-1.0f / static_cast<f32>(BLOCK)) * static_cast<f32>(frame + 1);
		ASSERT_EQ(fading[frame], samples[256 + frame] * gain) << frame;
	}
	const u64 held = mixer.Get().GetVoicePosition(0);
	for (f32 sample : mixer.Render(3 * BLOCK)) {
		ASSERT_EQ(sample, 0.0f);
	}
	EXPECT_EQ(mixer.Get().GetVoicePosition(0), held);

	ASSERT_TRUE(mixer.GameClock(true, 1.0f));
	const std::vector<f32> resumed = mixer.Render(2 * BLOCK);
	for (u32 frame = 0; frame < BLOCK; ++frame) {
		const f32 gain = (1.0f / static_cast<f32>(BLOCK)) * static_cast<f32>(frame + 1);
		ASSERT_EQ(resumed[frame], samples[512 + frame] * gain) << frame;
	}
	for (u32 frame = BLOCK; frame < 2 * BLOCK; ++frame) {
		ASSERT_EQ(resumed[frame], samples[512 + frame]) << frame;
	}
}

TEST(AudioTimeTest, UngroupedVoiceHoldsWithGame) {
	OfflineMixer mixer(1);
	ClipBlock clip(MakePcm(Constant(20000, 0.5f), 1, 48000));
	ASSERT_TRUE(mixer.Push(mixer.Play(0, clip.Get())));
	mixer.Render(BLOCK);
	ASSERT_TRUE(mixer.GameClock(false, 1.0f));
	mixer.Render(BLOCK);
	for (f32 sample : mixer.Render(BLOCK)) {
		ASSERT_EQ(sample, 0.0f);
	}
}

TEST(AudioTimeTest, ZeroTimeScaleHoldsGameplayBus) {
	OfflineMixer mixer(1);
	ClipBlock world(MakePcm(Constant(20000, 0.25f), 1, 48000));
	ClipBlock menu(MakePcm(Constant(20000, 0.125f), 1, 48000));
	const OfflineBus gameplay = mixer.Bus(1, {}, 1.0f, 1.0f, AudioBusClock::Gameplay);
	const OfflineBus wall = mixer.Bus(2, {}, 1.0f, 1.0f, AudioBusClock::Wall);
	ASSERT_TRUE(mixer.Push(OfflineMixer::Into(mixer.Play(0, world.Get()), gameplay)));
	ASSERT_TRUE(mixer.Push(OfflineMixer::Into(mixer.Play(1, menu.Get()), wall)));
	for (f32 sample : mixer.Render(BLOCK)) {
		ASSERT_EQ(sample, 0.375f);
	}

	ASSERT_TRUE(mixer.GameClock(true, 0.0f));
	mixer.Render(BLOCK);
	for (f32 sample : mixer.Render(2 * BLOCK)) {
		ASSERT_EQ(sample, 0.125f);
	}

	ASSERT_TRUE(mixer.GameClock(true, 1.0f));
	mixer.Render(BLOCK);
	for (f32 sample : mixer.Render(BLOCK)) {
		ASSERT_EQ(sample, 0.375f);
	}
}

TEST(AudioTimeTest, FollowingBusPitchesWithTimeScale) {
	OfflineMixer mixer(1);
	ClipBlock clip(MakePcm(Counter(40000), 1, 48000));
	const OfflineBus slow = mixer.Bus(1, {}, 1.0f, 1.0f, AudioBusClock::Wall, true);
	AudioCommand play = OfflineMixer::Into(mixer.Play(0, clip.Get()), slow);
	play.loop = true;
	ASSERT_TRUE(mixer.Push(play));
	mixer.Render(BLOCK);
	EXPECT_EQ(mixer.Get().GetVoicePosition(0), BLOCK);

	ASSERT_TRUE(mixer.GameClock(true, 0.5f));
	mixer.Render(BLOCK);
	const u64 walked = mixer.Get().GetVoicePosition(0);
	mixer.Render(2 * BLOCK);
	EXPECT_EQ(mixer.Get().GetVoicePosition(0) - walked, BLOCK);

	ASSERT_TRUE(mixer.GameClock(true, 0.0f));
	mixer.Render(BLOCK);
	const u64 held = mixer.Get().GetVoicePosition(0);
	for (f32 sample : mixer.Render(2 * BLOCK)) {
		ASSERT_EQ(sample, 0.0f);
	}
	EXPECT_EQ(mixer.Get().GetVoicePosition(0), held);
}

TEST(AudioTimeTest, WallGroupIgnoresTimeScale) {
	OfflineMixer mixer(1);
	const std::vector<f32> samples = Counter(40000);
	ClipBlock clip(MakePcm(samples, 1, 48000));
	const OfflineBus parent = mixer.Bus(1, {}, 1.0f, 1.0f, AudioBusClock::Wall, true);
	const OfflineBus child = mixer.Bus(2, parent, 1.0f, 1.0f, AudioBusClock::Wall, false);
	AudioCommand play = OfflineMixer::Into(mixer.Play(0, clip.Get()), child);
	play.loop = true;
	ASSERT_TRUE(mixer.Push(play));
	mixer.Render(BLOCK);

	ASSERT_TRUE(mixer.GameClock(true, 0.5f));
	mixer.Render(BLOCK);
	EXPECT_EQ(mixer.Get().GetVoicePosition(0), 2 * BLOCK);

	ASSERT_TRUE(mixer.GameClock(true, 0.0f));
	const std::vector<f32> out = mixer.Render(2 * BLOCK);
	for (u32 frame = 0; frame < 2 * BLOCK; ++frame) {
		ASSERT_EQ(out[frame], samples[2 * BLOCK + frame]);
	}
}

TEST(AudioTimeTest, NestedFollowingGroupsApplyTimeScaleOnce) {
	OfflineMixer mixer(1);
	ClipBlock clip(MakePcm(Counter(40000), 1, 48000));
	const OfflineBus parent = mixer.Bus(1, {}, 1.0f, 1.0f, AudioBusClock::Wall, true);
	const OfflineBus child = mixer.Bus(2, parent, 1.0f, 1.0f, AudioBusClock::Wall, true);
	AudioCommand play = OfflineMixer::Into(mixer.Play(0, clip.Get()), child);
	play.loop = true;
	ASSERT_TRUE(mixer.Push(play));
	mixer.Render(BLOCK);

	ASSERT_TRUE(mixer.GameClock(true, 0.5f));
	mixer.Render(BLOCK);
	const u64 walked = mixer.Get().GetVoicePosition(0);
	mixer.Render(2 * BLOCK);
	EXPECT_EQ(mixer.Get().GetVoicePosition(0) - walked, BLOCK);
}

TEST(AudioTimeTest, InheritingBusFollowsParentClock) {
	OfflineMixer mixer(1);
	ClipBlock clip(MakePcm(Counter(40000), 1, 48000));
	const OfflineBus parent = mixer.Bus(1, {}, 1.0f, 1.0f, AudioBusClock::Wall, true);
	const OfflineBus inheriting = mixer.Bus(2, parent, 1.0f, 1.0f, AudioBusClock::Inherit);
	AudioCommand play = OfflineMixer::Into(mixer.Play(0, clip.Get()), inheriting);
	play.loop = true;
	ASSERT_TRUE(mixer.Push(play));
	mixer.Render(BLOCK);

	ASSERT_TRUE(mixer.GameClock(true, 0.5f));
	mixer.Render(BLOCK);
	const u64 walked = mixer.Get().GetVoicePosition(0);
	mixer.Render(2 * BLOCK);
	EXPECT_EQ(mixer.Get().GetVoicePosition(0) - walked, BLOCK);

	ASSERT_TRUE(mixer.GameClock(true, 0.0f));
	mixer.Render(BLOCK);
	const u64 held = mixer.Get().GetVoicePosition(0);
	for (f32 sample : mixer.Render(2 * BLOCK)) {
		ASSERT_EQ(sample, 0.0f);
	}
	EXPECT_EQ(mixer.Get().GetVoicePosition(0), held);
}

TEST(AudioTimeTest, HeldBusDelaysScheduledStart) {
	OfflineMixer mixer(1);
	ClipBlock clip(MakePcm(Constant(4000, 0.5f), 1, 48000));
	const OfflineBus gameplay = mixer.Bus(1, {}, 1.0f, 1.0f, AudioBusClock::Gameplay);
	mixer.Render(BLOCK);

	const u64 origin = mixer.Now();
	const u64 at = origin + 3 * BLOCK + 100;
	ASSERT_TRUE(mixer.Push(OfflineMixer::Into(mixer.Play(0, clip.Get(), at), gameplay)));
	ASSERT_TRUE(mixer.GameClock(true, 0.0f));
	std::vector<f32> out = mixer.Render(2 * BLOCK);
	ASSERT_TRUE(mixer.GameClock(true, 1.0f));
	const std::vector<f32> rest = mixer.Render(6 * BLOCK);
	out.insert(out.end(), rest.begin(), rest.end());

	size_t first = out.size();
	for (size_t index = 0; index < out.size(); ++index) {
		if (out[index] != 0.0f) {
			first = index;
			break;
		}
	}
	EXPECT_EQ(origin + first, at + 2 * BLOCK);
	EXPECT_EQ(out[first], 0.5f);
}

TEST(AudioTimeTest, HeldBusSoundWaitsForClock) {
	OfflineMixer mixer(1);
	ClipBlock clip(MakePcm(Constant(4000, 0.5f), 1, 48000));
	const OfflineBus gameplay = mixer.Bus(1, {}, 1.0f, 1.0f, AudioBusClock::Gameplay);
	ASSERT_TRUE(mixer.GameClock(true, 0.0f));
	mixer.Render(BLOCK);
	ASSERT_TRUE(mixer.Push(OfflineMixer::Into(mixer.Play(0, clip.Get()), gameplay)));
	for (f32 sample : mixer.Render(2 * BLOCK)) {
		ASSERT_EQ(sample, 0.0f);
	}
	ASSERT_TRUE(mixer.GameClock(true, 1.0f));
	const std::vector<f32> out = mixer.Render(BLOCK);
	EXPECT_EQ(out[0], 0.5f);
}

TEST(AudioTimeTest, AHeldVoiceThatIsStoppedEnds) {
	OfflineMixer mixer(1);
	ClipBlock clip(MakePcm(Constant(20000, 0.5f), 1, 48000));
	const AudioCommand play = mixer.Play(0, clip.Get());
	ASSERT_TRUE(mixer.Push(play));
	mixer.Render(BLOCK);
	ASSERT_TRUE(mixer.GameClock(false, 1.0f));
	mixer.Render(2 * BLOCK);
	ASSERT_TRUE(mixer.TakeEvents().empty());
	ASSERT_TRUE(mixer.Push(OfflineMixer::To(play, AudioCommandKind::Stop)));
	mixer.Render(BLOCK);
	const std::vector<AudioEvent> events = mixer.TakeEvents();
	ASSERT_EQ(events.size(), 1u);
	EXPECT_EQ(events[0].kind, AudioEventKind::VoiceEnded);
}

TEST(AudioTimeTest, DspClockStopsWithGame) {
	OfflineMixer mixer(1);
	AudioClockStamp stamp;
	ASSERT_TRUE(mixer.Get().ReadClock(stamp));
	const u64 origin = mixer.Now();
	EXPECT_EQ(stamp.time, origin);
	EXPECT_TRUE(stamp.running);

	ASSERT_TRUE(mixer.GameClock(false, 1.0f));
	mixer.Render(3 * BLOCK);
	ASSERT_TRUE(mixer.Get().ReadClock(stamp));
	EXPECT_EQ(stamp.time, origin);
	EXPECT_EQ(stamp.frames, origin + 3 * BLOCK);
	EXPECT_FALSE(stamp.running);

	ASSERT_TRUE(mixer.GameClock(true, 0.0f));
	mixer.Render(2 * BLOCK);
	ASSERT_TRUE(mixer.Get().ReadClock(stamp));
	EXPECT_EQ(stamp.time, origin + 2 * BLOCK);
	EXPECT_TRUE(stamp.running);
}

TEST(AudioTimeTest, DspStartLandsAcrossPause) {
	OfflineMixer mixer(1);
	ClipBlock early(MakePcm(Constant(4000, 0.5f), 1, 48000));
	ClipBlock late(MakePcm(Constant(4000, 0.25f), 1, 48000));
	const u64 origin = mixer.Now();

	const u64 named = origin + 3 * BLOCK + 100;
	ASSERT_TRUE(mixer.Push(mixer.Play(0, early.Get(), named)));
	ASSERT_TRUE(mixer.GameClock(false, 1.0f));
	std::vector<f32> out = mixer.Render(2 * BLOCK);

	AudioClockStamp stamp;
	ASSERT_TRUE(mixer.Get().ReadClock(stamp));
	ASSERT_EQ(stamp.time, origin);
	const u64 namedWhileStill = stamp.time + 300;
	ASSERT_TRUE(mixer.Push(mixer.Play(1, late.Get(), namedWhileStill)));
	ASSERT_TRUE(mixer.GameClock(true, 1.0f));
	const std::vector<f32> more = mixer.Render(6 * BLOCK);
	out.insert(out.end(), more.begin(), more.end());

	const u64 stopped = 2 * BLOCK;
	const size_t lateStarts = static_cast<size_t>(namedWhileStill + stopped - origin);
	const size_t earlyStarts = static_cast<size_t>(named + stopped - origin);
	EXPECT_EQ(out[lateStarts - 1], 0.0f);
	EXPECT_EQ(out[lateStarts], 0.25f);
	EXPECT_EQ(out[earlyStarts - 1], 0.25f);
	EXPECT_EQ(out[earlyStarts], 0.75f);
}

namespace {

	using AudioClockFixture = LivePackageFixture;

	struct ThreeVoices {
		AudioVoice gameplay;
		AudioVoice wall;
		AudioVoice loose;
	};

} // namespace

TEST_F(AudioClockFixture, NonGameplayFrameHoldsVoices) {
	GetAudioSystem().SetListenerPresent(true);
	AudioClip* clip = MakeClip(96000, 1, 0.1f);

	AudioBusDesc gameplayDesc;
	gameplayDesc.clock = AudioBusClock::Gameplay;
	AudioBusDesc wallDesc;
	wallDesc.clock = AudioBusClock::Wall;
	const AudioBus gameplay = GetAudioSystem().CreateBus(gameplayDesc);
	const AudioBus wall = GetAudioSystem().CreateBus(wallDesc);
	ASSERT_TRUE(gameplay.IsValid());
	ASSERT_TRUE(wall.IsValid());

	ThreeVoices voices;
	AudioPlayParams params;
	params.loop = true;
	params.output = gameplay;
	voices.gameplay = Audio::Play(ResourceOf(clip), params);
	params.output = wall;
	voices.wall = Audio::Play(ResourceOf(clip), params);
	params.output = {};
	voices.loose = Audio::Play(ResourceOf(clip), params);

	RunFrame();
	ASSERT_TRUE(IsAdvancing(voices.gameplay));
	ASSERT_TRUE(IsAdvancing(voices.wall));
	ASSERT_TRUE(IsAdvancing(voices.loose));

	RunFrame(1.0f, false);
	WaitForBlocks(3);
	EXPECT_FALSE(IsAdvancing(voices.gameplay));
	EXPECT_FALSE(IsAdvancing(voices.wall));
	EXPECT_FALSE(IsAdvancing(voices.loose));

	RunFrame();
	WaitForBlocks(3);
	EXPECT_TRUE(IsAdvancing(voices.gameplay));
	EXPECT_TRUE(IsAdvancing(voices.wall));
	EXPECT_TRUE(IsAdvancing(voices.loose));
}

TEST_F(AudioClockFixture, ZeroTimeScaleSparesWallBus) {
	GetAudioSystem().SetListenerPresent(true);
	AudioClip* clip = MakeClip(96000, 1, 0.1f);
	AudioBusDesc gameplayDesc;
	gameplayDesc.clock = AudioBusClock::Gameplay;
	const AudioBus gameplay = GetAudioSystem().CreateBus(gameplayDesc);
	const AudioBus wall = GetAudioSystem().CreateBus(AudioBusDesc{});

	AudioPlayParams params;
	params.loop = true;
	params.output = gameplay;
	const AudioVoice world = Audio::Play(ResourceOf(clip), params);
	params.output = wall;
	const AudioVoice menu = Audio::Play(ResourceOf(clip), params);

	RunFrame(0.0f);
	WaitForBlocks(3);
	EXPECT_FALSE(IsAdvancing(world));
	EXPECT_TRUE(IsAdvancing(menu));

	RunFrame(1.0f);
	WaitForBlocks(3);
	EXPECT_TRUE(IsAdvancing(world));
}

TEST_F(AudioClockFixture, GetTimeStopsWithoutGameplay) {
	RunFrame();
	WaitForBlocks(3);
	const AudioTime running = Audio::GetTime();

	RunFrame(1.0f, false);
	WaitForBlocks(3);
	const AudioTime still = Audio::GetTime();
	EXPECT_GE(still.frames, running.frames);
	WaitForBlocks(8);
	EXPECT_EQ(Audio::GetTime().frames, still.frames);

	RunFrame(0.0f);
	WaitForBlocks(3);
	const AudioTime scaled = Audio::GetTime();
	EXPECT_GT(scaled.frames, still.frames);
	WaitForBlocks(4);
	EXPECT_GT(Audio::GetTime().frames, scaled.frames);
}

TEST(AudioTimeTest, DeviceBusPlaysWithoutGame) {
	OfflineMixer mixer(1);
	const OfflineBus device = mixer.Bus(1, {}, 1.0f, 1.0f, AudioBusClock::Device);
	ClipBlock outside(MakePcm(Constant(40000, 0.5f), 1, 48000));
	ClipBlock game(MakePcm(Constant(40000, 0.25f), 1, 48000));
	ASSERT_TRUE(mixer.Push(OfflineMixer::Into(mixer.Play(0, outside.Get()), device)));
	ASSERT_TRUE(mixer.Push(mixer.Play(1, game.Get())));
	mixer.Render(BLOCK);
	ASSERT_TRUE(mixer.GameClock(false, 1.0f));
	mixer.Render(BLOCK);
	const std::vector<f32> out = mixer.Render(2 * BLOCK);
	for (size_t frame = 0; frame < out.size(); ++frame) {
		ASSERT_FLOAT_EQ(out[frame], 0.5f) << "frame " << frame;
	}
}

TEST(AudioTimeTest, DeviceBusIgnoresListenerAndVolume) {
	OfflineMixer mixer(1, 48000, false);
	const OfflineBus device = mixer.Bus(1, {}, 1.0f, 1.0f, AudioBusClock::Device);
	AudioCommand volume;
	volume.kind = AudioCommandKind::SetGlobalVolume;
	volume.value = 0.0f;
	ASSERT_TRUE(mixer.Push(volume));
	ClipBlock outside(MakePcm(Constant(40000, 0.5f), 1, 48000));
	ClipBlock game(MakePcm(Constant(40000, 0.25f), 1, 48000));
	ASSERT_TRUE(mixer.Push(OfflineMixer::Into(mixer.Play(0, outside.Get()), device)));
	ASSERT_TRUE(mixer.Push(mixer.Play(1, game.Get())));
	mixer.Render(BLOCK);
	const std::vector<f32> out = mixer.Render(2 * BLOCK);
	for (size_t frame = 0; frame < out.size(); ++frame) {
		ASSERT_FLOAT_EQ(out[frame], 0.5f);
	}
}

TEST(AudioTimeTest, DeviceBusStartUsesDeviceFrame) {
	OfflineMixer mixer(1);
	const OfflineBus device = mixer.Bus(1, {}, 1.0f, 1.0f, AudioBusClock::Device);
	ASSERT_TRUE(mixer.GameClock(false, 1.0f));
	mixer.Render(3 * BLOCK);
	const u64 origin = mixer.Now();
	ClipBlock clip(MakePcm(Constant(4000, 0.5f), 1, 48000));
	ASSERT_TRUE(mixer.Push(OfflineMixer::Into(mixer.Play(0, clip.Get(), origin + 300), device)));
	const std::vector<f32> out = mixer.Render(2 * BLOCK);
	EXPECT_EQ(out[299], 0.0f);
	EXPECT_EQ(out[300], 0.5f);
}

TEST(AudioTimeTest, SoloElsewhereLeavesDeviceClockedBusHeard) {
	OfflineMixer mixer(1);
	const OfflineBus soloed = mixer.Bus(1);
	const OfflineBus device = mixer.Bus(2, {}, 1.0f, 1.0f, AudioBusClock::Device);
	ASSERT_TRUE(mixer.Push(OfflineMixer::Flag(AudioCommandKind::SetBusSolo, soloed, true)));
	ClipBlock outside(MakePcm(Constant(40000, 0.5f), 1, 48000));
	ASSERT_TRUE(mixer.Push(OfflineMixer::Into(mixer.Play(0, outside.Get()), device)));
	mixer.Render(BLOCK);
	const std::vector<f32> out = mixer.Render(2 * BLOCK);
	for (size_t frame = 0; frame < out.size(); ++frame) {
		ASSERT_FLOAT_EQ(out[frame], 0.5f);
	}
}

TEST_F(AudioClockFixture, DeviceOutputOutlivesPlaySession) {
	const AudioBus output = GetDeviceOutput();
	ASSERT_TRUE(output.IsValid());
	EXPECT_EQ(GetDeviceOutput(), output);
	EXPECT_TRUE(GetAudioSystem().IsDeviceClocked(output));

	AudioClip* clip = MakeClip(96000, 1, 0.1f);
	AudioPlayParams params;
	params.loop = true;
	const AudioVoice game = Audio::Play(ResourceOf(clip), params);
	params.output = output;
	const AudioVoice outside = Audio::Play(ResourceOf(clip), params);

	RunFrame(1.0f, false);
	WaitForBlocks(3);
	EXPECT_FALSE(IsAdvancing(game));
	EXPECT_TRUE(IsAdvancing(outside));

	EXPECT_EQ(StopGameVoices(), 1u);
	RunFrame(1.0f, false);
	WaitForBlocks(3);
	EXPECT_TRUE(Audio::IsPlaying(outside));
	EXPECT_FALSE(Audio::IsPlaying(game));
}

TEST_F(AudioClockFixture, MutingGameSilencesSoundAndNotPreviews) {
	GetAudioSystem().SetListenerPresent(true);
	AudioClip* clip = MakeClip(96000, 1, 0.1f);
	AudioPlayParams params;
	params.loop = true;
	const AudioVoice game = Audio::Play(ResourceOf(clip), params);
	ASSERT_TRUE(game.IsValid());
	RunFrame();
	ASSERT_TRUE(WaitFor([]() { return LastBlockPeak() > 0.01f; }));
	WaitForBlocks(3);
	const f32 heard = LastBlockPeak();
	const auto peakNear = [](f32 expected) { return WaitFor([expected]() { return std::fabs(LastBlockPeak() - expected) < 1e-4f; }); };

	SetGameMuted(true);
	EXPECT_TRUE(peakNear(0.0f)) << LastBlockPeak();
	Audio::SetGlobalVolume(0.5f);
	RunFrame();
	WaitForBlocks(3);
	EXPECT_TRUE(peakNear(0.0f)) << LastBlockPeak();

	params.output = GetDeviceOutput();
	const AudioVoice preview = Audio::Play(ResourceOf(clip), params);
	ASSERT_TRUE(preview.IsValid());
	EXPECT_TRUE(peakNear(heard)) << LastBlockPeak() << " against " << heard;
	Audio::Stop(preview);
	EXPECT_TRUE(peakNear(0.0f));

	SetGameMuted(false);
	EXPECT_TRUE(peakNear(heard * 0.5f));
}

#endif
