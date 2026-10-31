#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioLiveFixture.h"
#include "AudioOfflineMixer.h"

#include "AudioSettings.h"
#include "AudioVoiceManager.h"

#include "Shalltear/Audio/Audio.h"
#include "Shalltear/Audio/AudioConcurrency.h"
#include "Shalltear/Audio/AudioListener.h"
#include "Shalltear/Audio/AudioSource.h"

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Settings.h"

#include <algorithm>
#include <filesystem>
#include <vector>

using namespace AudioTesting;

namespace {

	constexpr u32 BLOCK = OfflineMixer::BLOCK;

	std::vector<f32> Ramp(u32 frames) {
		std::vector<f32> samples(frames);
		for (u32 frame = 0; frame < frames; ++frame) {
			samples[frame] = 1.0e-5f * static_cast<f32>(frame + 1);
		}
		return samples;
	}

	std::vector<f32> Constant(u32 frames, f32 value) {
		return std::vector<f32>(frames, value);
	}

	AudioCommand Virtual(AudioCommand play) {
		play.flag = true;
		return play;
	}

	AudioCommand Looping(AudioCommand play) {
		play.loop = true;
		return play;
	}

	AudioCommand MakeReal(const AudioCommand& play, bool real) {
		AudioCommand command = OfflineMixer::To(play, AudioCommandKind::SetVoiceReal);
		command.flag = real;
		return command;
	}

	bool AllSilent(const std::vector<f32>& samples) {
		return std::all_of(samples.begin(), samples.end(), [](f32 sample) { return sample == 0.0f; });
	}

	bool HasEnded(const std::vector<AudioEvent>& events, const AudioCommand& play) {
		return std::any_of(events.begin(), events.end(), [&](const AudioEvent& event) {
			return event.kind == AudioEventKind::VoiceEnded && event.slot == play.voice && event.generation == play.generation;
		});
	}

	u32 RealVoicesIn(OfflineMixer& mixer) { return mixer.Get().GetCounters().realVoices.load(); }

} // namespace

TEST(AudioVoiceTest, VirtualVoiceAdvancesWithoutSound) {
	ClipBlock clip(MakePcm(Ramp(48000), 1, 48000));
	OfflineMixer mixer;
	ASSERT_TRUE(mixer.Push(Virtual(mixer.Play(0, clip.Get()))));

	EXPECT_TRUE(AllSilent(mixer.Render(4 * BLOCK)));
	EXPECT_EQ(mixer.Get().GetVoicePosition(0), 4u * BLOCK);
	EXPECT_EQ(mixer.Get().GetCounters().voices.load(), 1u);
	EXPECT_EQ(RealVoicesIn(mixer), 0u);
}

TEST(AudioVoiceTest, RealVoiceFadesInAtClock) {
	ClipBlock clip(MakePcm(Ramp(48000), 1, 48000));
	OfflineMixer heard;
	OfflineMixer virtualised;
	ASSERT_TRUE(heard.Push(heard.Play(0, clip.Get())));
	const AudioCommand play = Virtual(virtualised.Play(0, clip.Get()));
	ASSERT_TRUE(virtualised.Push(play));
	heard.Render(4 * BLOCK);
	virtualised.Render(4 * BLOCK);

	ASSERT_TRUE(virtualised.Push(MakeReal(play, true)));
	const std::vector<f32> fullFade = heard.Render(BLOCK);
	const std::vector<f32> fadeIn = virtualised.Render(BLOCK);
	for (u32 frame = 0; frame < BLOCK; ++frame) {
		const f32 gain = static_cast<f32>(frame + 1) / static_cast<f32>(BLOCK);
		ASSERT_FLOAT_EQ(At(fadeIn, 2, frame, 0), At(fullFade, 2, frame, 0) * gain) << "frame " << frame;
	}
	EXPECT_GT(At(fadeIn, 2, BLOCK - 1, 0), 0.0f);
	EXPECT_EQ(heard.Render(BLOCK), virtualised.Render(BLOCK));
	EXPECT_EQ(RealVoicesIn(virtualised), 1u);
}

TEST(AudioVoiceTest, VirtualVoiceFadesOverOneBlock) {
	ClipBlock clip(MakePcm(Constant(4800, 0.5f), 1, 48000));
	OfflineMixer mixer;
	const AudioCommand play = Looping(mixer.Play(0, clip.Get()));
	ASSERT_TRUE(mixer.Push(play));
	const f32 level = At(mixer.Render(BLOCK), 2, BLOCK - 1, 0);
	ASSERT_GT(level, 0.0f);

	ASSERT_TRUE(mixer.Push(MakeReal(play, false)));
	const std::vector<f32> fade = mixer.Render(BLOCK);
	EXPECT_FLOAT_EQ(At(fade, 2, 0, 0), level * static_cast<f32>(BLOCK - 1) / static_cast<f32>(BLOCK));
	EXPECT_FLOAT_EQ(At(fade, 2, BLOCK - 1, 0), 0.0f);
	EXPECT_TRUE(AllSilent(mixer.Render(4 * BLOCK)));
	EXPECT_EQ(RealVoicesIn(mixer), 0u);
	EXPECT_EQ(mixer.Get().GetVoicePosition(0), 6u * BLOCK);
}

TEST(AudioVoiceTest, VirtualOneShotEndsOnTime) {
	ClipBlock clip(MakePcm(Ramp(1000), 1, 48000));
	OfflineMixer heard;
	OfflineMixer virtualised;
	const AudioCommand heardPlay = heard.Play(0, clip.Get());
	const AudioCommand virtualPlay = Virtual(virtualised.Play(0, clip.Get()));
	ASSERT_TRUE(heard.Push(heardPlay));
	ASSERT_TRUE(virtualised.Push(virtualPlay));

	i32 heardEnd = -1;
	i32 virtualEnd = -1;
	for (i32 block = 0; block < 8; ++block) {
		heard.Render(BLOCK);
		virtualised.Render(BLOCK);
		if (heardEnd < 0 && HasEnded(heard.TakeEvents(), heardPlay)) {
			heardEnd = block;
		}
		if (virtualEnd < 0 && HasEnded(virtualised.TakeEvents(), virtualPlay)) {
			virtualEnd = block;
		}
	}
	EXPECT_EQ(heardEnd, 3);
	EXPECT_EQ(virtualEnd, heardEnd);
}

TEST(AudioVoiceTest, VirtualLoopKeepsLoopingAndNeverEnds) {
	ClipBlock clip(MakePcm(Ramp(300), 1, 48000));
	OfflineMixer mixer;
	const AudioCommand play = Virtual(Looping(mixer.Play(0, clip.Get())));
	ASSERT_TRUE(mixer.Push(play));
	mixer.Render(10 * BLOCK);
	EXPECT_FALSE(HasEnded(mixer.TakeEvents(), play));
	EXPECT_EQ(mixer.Get().GetVoicePosition(0), (10u * BLOCK) % 300u);
}

TEST(AudioVoiceTest, VirtualVoiceTracksPitch) {
	ClipBlock clip(MakePcm(Ramp(48000), 1, 48000));
	OfflineMixer heard;
	OfflineMixer virtualised;
	const AudioCommand heardPlay = heard.Play(0, clip.Get());
	const AudioCommand virtualPlay = Virtual(virtualised.Play(0, clip.Get()));
	ASSERT_TRUE(heard.Push(heardPlay));
	ASSERT_TRUE(virtualised.Push(virtualPlay));
	const auto both = [&](AudioCommandKind kind, f32 value) {
		heard.Push(OfflineMixer::To(heardPlay, kind, value));
		virtualised.Push(OfflineMixer::To(virtualPlay, kind, value));
	};
	heard.Render(BLOCK);
	virtualised.Render(BLOCK);
	both(AudioCommandKind::SetPitch, 1.7f);
	heard.Render(2 * BLOCK);
	virtualised.Render(2 * BLOCK);
	both(AudioCommandKind::SetPitch, 1.3f);
	heard.Render(5 * BLOCK);
	virtualised.Render(5 * BLOCK);

	const i64 heardAt = static_cast<i64>(heard.Get().GetVoicePosition(0));
	const i64 virtualAt = static_cast<i64>(virtualised.Get().GetVoicePosition(0));
	EXPECT_GT(heardAt, static_cast<i64>(8 * BLOCK));
	EXPECT_LE(std::abs(heardAt - virtualAt), 1) << virtualAt << " against " << heardAt;
}

TEST(AudioVoiceTest, StealReleaseFadesThenEnds) {
	ClipBlock clip(MakePcm(Constant(4800, 0.5f), 1, 48000));
	OfflineMixer mixer;
	const AudioCommand play = Looping(mixer.Play(0, clip.Get()));
	ASSERT_TRUE(mixer.Push(play));
	const f32 level = At(mixer.Render(BLOCK), 2, BLOCK - 1, 0);

	AudioCommand stop = OfflineMixer::To(play, AudioCommandKind::Stop);
	stop.frames = 4 * BLOCK;
	ASSERT_TRUE(mixer.Push(stop));
	const f32 expected[4] = {0.75f, 0.5f, 0.25f, 0.0f};
	for (u32 block = 0; block < 4; ++block) {
		const std::vector<f32> out = mixer.Render(BLOCK);
		EXPECT_NEAR(At(out, 2, BLOCK - 1, 0), level * expected[block], 1.0e-6f) << "block " << block;
		EXPECT_EQ(HasEnded(mixer.TakeEvents(), play), block == 3) << "block " << block;
	}
	EXPECT_TRUE(AllSilent(mixer.Render(BLOCK)));
}

TEST(AudioVoiceTest, EarlyRealVoiceStartsUnfaded) {
	ClipBlock clip(MakePcm(Constant(500, 0.5f), 1, 48000));
	OfflineMixer heard;
	OfflineMixer virtualised;
	const u64 onset = heard.Now() + 2 * BLOCK + 100;
	ASSERT_TRUE(heard.Push(heard.Play(0, clip.Get(), onset)));
	const AudioCommand play = Virtual(virtualised.Play(0, clip.Get(), onset));
	ASSERT_TRUE(virtualised.Push(play));
	heard.Render(BLOCK);
	virtualised.Render(BLOCK);

	ASSERT_TRUE(virtualised.Push(MakeReal(play, true)));
	const std::vector<f32> expected = heard.Render(2 * BLOCK);
	const std::vector<f32> actual = virtualised.Render(2 * BLOCK);
	EXPECT_EQ(At(actual, 2, BLOCK + 99, 0), 0.0f);
	EXPECT_GT(At(actual, 2, BLOCK + 100, 0), 0.0f);
	EXPECT_EQ(actual, expected);
}

TEST(AudioVoiceTest, DoubleDecoderPoolNoShortfall) {
	ClipBlock clip(MakePcm(Constant(4800, 0.1f), 1, 48000));
	OfflineMixer mixer(2, 48000, true, 32, 16);
	std::vector<AudioCommand> plays;
	for (u32 slot = 0; slot < 16; ++slot) {
		AudioCommand play = Looping(mixer.Play(slot, clip.Get()));
		play.flag = slot >= 8;
		plays.push_back(play);
		ASSERT_TRUE(mixer.Push(play));
	}
	mixer.Render(BLOCK);
	ASSERT_EQ(RealVoicesIn(mixer), 8u);

	for (u32 slot = 0; slot < 8; ++slot) {
		ASSERT_TRUE(mixer.Push(MakeReal(plays[slot], false)));
	}
	for (u32 slot = 8; slot < 16; ++slot) {
		ASSERT_TRUE(mixer.Push(MakeReal(plays[slot], true)));
	}
	mixer.Render(BLOCK);
	EXPECT_EQ(RealVoicesIn(mixer), 16u);
	mixer.Render(BLOCK);
	EXPECT_EQ(RealVoicesIn(mixer), 8u);
	EXPECT_EQ(mixer.Get().GetCounters().decoderShortfalls.load(), 0u);
}

TEST(AudioVoiceTest, DecoderShortfallCounted) {
	ClipBlock clip(MakePcm(Constant(4800, 0.1f), 1, 48000));
	OfflineMixer mixer(2, 48000, true, 32, 4);
	for (u32 slot = 0; slot < 8; ++slot) {
		ASSERT_TRUE(mixer.Push(Looping(mixer.Play(slot, clip.Get()))));
	}
	mixer.Render(BLOCK);
	EXPECT_EQ(RealVoicesIn(mixer), 4u);
	EXPECT_EQ(mixer.Get().GetCounters().decoderShortfalls.load(), 4u);
}

TEST(AudioVoiceTest, AVirtualVoiceStoppedEndsAtOnce) {
	ClipBlock clip(MakePcm(Constant(4800, 0.1f), 1, 48000));
	OfflineMixer mixer;
	const AudioCommand play = Virtual(Looping(mixer.Play(0, clip.Get())));
	ASSERT_TRUE(mixer.Push(play));
	mixer.Render(BLOCK);
	ASSERT_TRUE(mixer.Push(OfflineMixer::To(play, AudioCommandKind::Stop)));
	mixer.Render(BLOCK);
	EXPECT_TRUE(HasEnded(mixer.TakeEvents(), play));
	EXPECT_EQ(mixer.Get().GetCounters().voices.load(), 0u);
}

TEST(AudioVoiceTest, BudgetRanksImportanceThenLoudness) {
	std::vector<AudioVoiceRank> voices = {
		{0, 128, 0.9f, 1, false, false},
		{1, 128, 0.2f, 2, false, false},
		{2, 10, 0.01f, 3, false, false},
		{3, 128, 0.5f, 4, false, false},
		{4, 200, 1.0f, 5, false, false},
	};
	std::vector<u32> order(voices.size());
	EXPECT_EQ(SelectRealVoices(voices, 3, order), 3u);
	const bool expected[5] = {true, false, true, true, false};
	for (u32 index = 0; index < voices.size(); ++index) {
		EXPECT_EQ(voices[index].wantReal, expected[index]) << "voice " << index;
	}

	EXPECT_EQ(SelectRealVoices(voices, 8, order), 5u);
	EXPECT_TRUE(std::all_of(voices.begin(), voices.end(), [](const AudioVoiceRank& voice) { return voice.wantReal; }));
}

TEST(AudioVoiceTest, HeardVoiceHasHysteresis) {
	std::vector<u32> order(2);
	std::vector<AudioVoiceRank> nearlyLouder = {{0, 128, 1.0f, 1, true, false}, {1, 128, 1.1f, 2, false, false}};
	SelectRealVoices(nearlyLouder, 1, order);
	EXPECT_TRUE(nearlyLouder[0].wantReal);

	std::vector<AudioVoiceRank> louder = {{0, 128, 1.0f, 1, true, false}, {1, 128, 1.13f, 2, false, false}};
	SelectRealVoices(louder, 1, order);
	EXPECT_TRUE(louder[1].wantReal);

	std::vector<AudioVoiceRank> equal = {{0, 128, 0.5f, 7, false, false}, {1, 128, 0.5f, 3, false, false}};
	SelectRealVoices(equal, 1, order);
	EXPECT_TRUE(equal[1].wantReal);
}

TEST(AudioVoiceTest, EachConcurrencyRuleStopsVoiceNameSays) {
	const std::vector<AudioConcurrencyMember> members = {
		{10, 128, 0.5f, 5.0f, 1},
		{11, 200, 0.9f, 2.0f, 2},
		{12, 128, 0.7f, 9.0f, 3},
		{13, 128, 0.1f, 4.0f, 4},
	};
	const AudioConcurrencyMember incoming{NO_VOICE_SLOT, 128, 0.6f, 3.0f, ~0ull};
	const auto victim = [&](AudioConcurrencyRule rule, const AudioConcurrencyMember& newcomer) { return ChooseConcurrencyVictim(rule, members, newcomer); };

	EXPECT_EQ(victim(AudioConcurrencyRule::PreventNew, incoming), NO_VOICE_SLOT);
	EXPECT_EQ(victim(AudioConcurrencyRule::StopOldest, incoming), 10u);
	EXPECT_EQ(victim(AudioConcurrencyRule::StopFarthestThenPreventNew, incoming), 12u);
	EXPECT_EQ(victim(AudioConcurrencyRule::StopFarthestThenOldest, incoming), 12u);
	EXPECT_EQ(victim(AudioConcurrencyRule::StopLowestPriority, incoming), 11u);
	EXPECT_EQ(victim(AudioConcurrencyRule::StopLowestPriorityThenPreventNew, incoming), 11u);
	EXPECT_EQ(victim(AudioConcurrencyRule::StopQuietest, incoming), 13u);

	AudioConcurrencyMember far = incoming;
	far.distance = 20.0f;
	EXPECT_EQ(victim(AudioConcurrencyRule::StopFarthestThenOldest, far), NO_VOICE_SLOT);
	EXPECT_EQ(victim(AudioConcurrencyRule::StopFarthestThenPreventNew, far), NO_VOICE_SLOT);
	AudioConcurrencyMember low = incoming;
	low.priority = 250;
	EXPECT_EQ(victim(AudioConcurrencyRule::StopLowestPriority, low), NO_VOICE_SLOT);
	AudioConcurrencyMember quiet = incoming;
	quiet.audibility = 0.01f;
	EXPECT_EQ(victim(AudioConcurrencyRule::StopQuietest, quiet), NO_VOICE_SLOT);

	AudioConcurrencyMember level = incoming;
	level.distance = 9.0f;
	level.priority = 200;
	EXPECT_EQ(victim(AudioConcurrencyRule::StopFarthestThenPreventNew, level), NO_VOICE_SLOT);
	EXPECT_EQ(victim(AudioConcurrencyRule::StopFarthestThenOldest, level), 12u);
	EXPECT_EQ(victim(AudioConcurrencyRule::StopLowestPriorityThenPreventNew, level), NO_VOICE_SLOT);
	EXPECT_EQ(victim(AudioConcurrencyRule::StopLowestPriority, level), 11u);

	const std::vector<AudioConcurrencyMember> alike = {{20, 128, 0.5f, 5.0f, 8}, {21, 128, 0.5f, 5.0f, 6}, {22, 128, 0.5f, 5.0f, 9}};
	const AudioConcurrencyMember same{NO_VOICE_SLOT, 128, 0.5f, 5.0f, ~0ull};
	EXPECT_EQ(ChooseConcurrencyVictim(AudioConcurrencyRule::StopFarthestThenOldest, alike, same), 21u);
	EXPECT_EQ(ChooseConcurrencyVictim(AudioConcurrencyRule::StopFarthestThenPreventNew, alike, same), NO_VOICE_SLOT);
	EXPECT_EQ(ChooseConcurrencyVictim(AudioConcurrencyRule::StopLowestPriority, alike, same), 21u);
	EXPECT_EQ(ChooseConcurrencyVictim(AudioConcurrencyRule::StopLowestPriorityThenPreventNew, alike, same), NO_VOICE_SLOT);
	EXPECT_EQ(ChooseConcurrencyVictim(AudioConcurrencyRule::StopQuietest, alike, same), 21u);
}

TEST(AudioVoiceTest, GroupTableSurvivesRemovals) {
	AudioConcurrencyGroups groups;
	groups.Initialize(64);
	for (u64 key = 1; key <= 64; ++key) {
		AudioConcurrencyGroup* group = groups.FindOrAdd(key, key % 3);
		ASSERT_NE(group, nullptr) << key;
		group->count = key % 2;
	}
	ASSERT_EQ(groups.CountGroups(), 64u);
	groups.Sweep(~0ull);
	EXPECT_EQ(groups.CountGroups(), 32u);
	for (u64 key = 1; key <= 64; ++key) {
		EXPECT_EQ(groups.Find(key, key % 3) != nullptr, key % 2 == 1) << key;
	}

	AudioConcurrencyGroup* lingering = groups.FindOrAdd(1000, 0);
	lingering->started = true;
	lingering->lastStart = 500;
	lingering->retriggerFrames = 100;
	groups.Sweep(550);
	EXPECT_NE(groups.Find(1000, 0), nullptr);
	groups.Sweep(600);
	EXPECT_EQ(groups.Find(1000, 0), nullptr);
}

TEST(AudioVoiceTest, ConcurrencyRoundTripsByName) {
	const std::filesystem::path path = std::filesystem::temp_directory_path() / "shalltear_audio_voice_test.concurrency";
	AudioConcurrency written;
	written.maxCount = 5;
	written.limitToOwner = true;
	written.resolutionRule = AudioConcurrencyRule::StopQuietest;
	written.retriggerTime = 0.25f;
	written.voiceStealReleaseTime = 0.5f;
	ASSERT_TRUE(written.Export(Path(path.generic_string())));
	const str text = FileSystem::ReadTextFile(Path(path.generic_string()), MemTag::Asset);
	for (const char* name : {"maxCount", "limitToOwner", "resolutionRule", "retriggerTime", "voiceStealReleaseTime"}) {
		EXPECT_NE(text.find(name), str::npos) << name << "': " << text;
	}

	AudioConcurrency read;
	ASSERT_TRUE(read.Import(Path(path.generic_string())));
	EXPECT_EQ(read.maxCount, 5);
	EXPECT_TRUE(read.limitToOwner);
	EXPECT_EQ(read.resolutionRule, AudioConcurrencyRule::StopQuietest);
	EXPECT_FLOAT_EQ(read.retriggerTime, 0.25f);
	EXPECT_FLOAT_EQ(read.voiceStealReleaseTime, 0.5f);
	std::error_code ec;
	std::filesystem::remove(path, ec);
}

namespace {

	class VoiceBudgetFixture : public LivePackageFixture {
	protected:
		Handle<Scene> _scene;
		std::vector<AudioConcurrency*> _concurrencies;

		str ProjectSettings() const override { return str("[Audio]\ndevice = None\nmaxVirtualVoices = 8\nmaxRealVoices = 2\n"); }

		void SetUp() override {
			LivePackageFixture::SetUp();
			_scene = SceneSystem::CreateScene("voices");
			ASSERT_TRUE(_scene.IsValid());
			ASSERT_EQ(GetAudioSystem().GetMaxRealVoices(), 2u);
			ASSERT_EQ(GetAudioSystem().GetVoices().GetLimit(), 8u);
		}

		void TearDown() override {
			LivePackageFixture::TearDown();
			for (AudioConcurrency* concurrency : _concurrencies) {
				Delete(concurrency);
			}
		}

		AudioConcurrency* MakeConcurrency(i32 maxCount, AudioConcurrencyRule rule, bool limitToOwner = false) {
			auto* concurrency = New<AudioConcurrency>(MemTag::Asset);
			concurrency->maxCount = maxCount;
			concurrency->resolutionRule = rule;
			concurrency->limitToOwner = limitToOwner;
			_concurrencies.push_back(concurrency);
			return concurrency;
		}

		static AudioPlayParams Loop(f32 volume = 1.0f, i32 priority = 128) {
			AudioPlayParams params;
			params.loop = true;
			params.volume = volume;
			params.priority = priority;
			return params;
		}

		static AudioPlayParams InGroup(AudioConcurrency* concurrency, Handle<Object> owner = {}) {
			AudioPlayParams params = Loop();
			params.concurrency = Handle<AudioConcurrency>(concurrency);
			params.owner = owner;
			return params;
		}

		static u32 RealVoicesRendered() { return GetAudioSystem().GetRenderer().GetCounters().realVoices.load(std::memory_order_relaxed); }

		static const AudioSystem::BudgetCounts& Counts() { return GetAudioSystem().GetBudgetCounts(); }

		Handle<AudioSource> AddSource(AudioClip* clip, const Vec3& position = {}) {
			Handle<SceneObject> object = SceneSystem::CreateObject("source", _scene);
			object->SetWorldPosition(position);
			Handle<AudioSource> source = object->AddComponent<AudioSource>();
			source->SetResource(ResourceOf(clip));
			source->SetLoop(true);
			source->SetPlayOnAwake(false);
			return source;
		}

		Handle<AudioListener> AddListener(const Vec3& position = {}) {
			Handle<SceneObject> object = SceneSystem::CreateObject("listener", _scene);
			object->SetWorldPosition(position);
			return object->AddComponent<AudioListener>();
		}

		static void SetVoiceCounts(i32 playing, i32 heard) {
			AudioSettings* settings = GetDeclaredSettings();
			settings->maxVirtualVoices = playing;
			Settings::NotifyChanged(SETTINGS_CATEGORY_PATH, "maxVirtualVoices");
			settings->maxRealVoices = heard;
			Settings::NotifyChanged(SETTINGS_CATEGORY_PATH, "maxRealVoices");
		}
	};

} // namespace

TEST_F(VoiceBudgetFixture, OutrankedVoiceReturnsAtClock) {
	AudioClip* clip = MakeClip(480000, 1, 0.1f);
	const AudioVoice ordinary = Audio::Play(ResourceOf(clip), Loop(0.5f, 128));
	const AudioVoice first = Audio::Play(ResourceOf(clip), Loop(1.0f, 0));
	ASSERT_TRUE(ordinary.IsValid());
	ASSERT_TRUE(first.IsValid());
	EXPECT_FALSE(Audio::IsVirtual(ordinary));

	const AudioVoice second = Audio::Play(ResourceOf(clip), Loop(1.0f, 0));
	ASSERT_TRUE(second.IsValid());
	EXPECT_TRUE(Audio::IsVirtual(ordinary));
	EXPECT_TRUE(Audio::IsPlaying(ordinary));
	EXPECT_FALSE(Audio::IsVirtual(second));
	EXPECT_TRUE(IsAdvancing(ordinary));
	EXPECT_TRUE(WaitFor([]() { return RealVoicesRendered() == 2; }));

	const u64 reached = PositionOf(ordinary);
	Audio::Stop(first);
	Audio::Stop(second);
	RunFrame();
	EXPECT_FALSE(Audio::IsVirtual(ordinary));
	EXPECT_GE(PositionOf(ordinary), reached);
	EXPECT_TRUE(WaitFor([]() { return RealVoicesRendered() == 1; }));
}

TEST_F(VoiceBudgetFixture, PriorityOutranksLoudness) {
	AudioClip* clip = MakeClip(48000, 1, 0.1f);
	const AudioVoice a = Audio::Play(ResourceOf(clip), Loop(1.0f, 200));
	const AudioVoice b = Audio::Play(ResourceOf(clip), Loop(1.0f, 200));
	const AudioVoice important = Audio::Play(ResourceOf(clip), Loop(0.01f, 10));
	EXPECT_FALSE(Audio::IsVirtual(important));
	EXPECT_EQ((Audio::IsVirtual(a) ? 1 : 0) + (Audio::IsVirtual(b) ? 1 : 0), 1);
	RunFrame();
	EXPECT_FALSE(Audio::IsVirtual(important));
}

TEST_F(VoiceBudgetFixture, AtOnePriorityTheLoudestAreHeard) {
	GetAudioSystem().SetListenerPresent(true);
	RunFrame();
	AudioClip* clip = MakeClip(48000, 1, 0.1f);
	const AudioVoice quiet = Audio::Play(ResourceOf(clip), Loop(0.2f));
	const AudioVoice loud = Audio::Play(ResourceOf(clip), Loop(0.9f));
	const AudioVoice middle = Audio::Play(ResourceOf(clip), Loop(0.5f));
	const AudioVoice softer = Audio::Play(ResourceOf(clip), Loop(0.3f));
	EXPECT_TRUE(Audio::IsVirtual(quiet));
	EXPECT_FALSE(Audio::IsVirtual(loud));
	EXPECT_FALSE(Audio::IsVirtual(middle));
	EXPECT_TRUE(Audio::IsVirtual(softer));
	RunFrame();
	EXPECT_FALSE(Audio::IsVirtual(loud));
	EXPECT_FALSE(Audio::IsVirtual(middle));
	EXPECT_TRUE(Audio::IsVirtual(quiet) && Audio::IsVirtual(softer));
}

TEST_F(VoiceBudgetFixture, PausedVoiceYieldsSlot) {
	AudioClip* clip = MakeClip(48000, 1, 0.1f);
	const AudioVoice paused = Audio::Play(ResourceOf(clip), Loop());
	const AudioVoice other = Audio::Play(ResourceOf(clip), Loop());
	Audio::Pause(paused);
	const AudioVoice next = Audio::Play(ResourceOf(clip), Loop(0.1f));
	EXPECT_FALSE(Audio::IsVirtual(next));
	EXPECT_TRUE(Audio::IsVirtual(paused));
	EXPECT_FALSE(Audio::IsVirtual(other));

	Audio::UnPause(paused);
	EXPECT_TRUE(Audio::IsPlaying(paused));
	EXPECT_FALSE(Audio::IsVirtual(paused));
	EXPECT_TRUE(Audio::IsVirtual(next));
}

TEST_F(VoiceBudgetFixture, MutedGroupVoiceYieldsFirst) {
	AudioSystem& system = GetAudioSystem();
	const AudioBus muted = system.CreateBus(AudioBusDesc{});
	ASSERT_TRUE(muted.IsValid());
	system.SetBusMute(muted, true);
	AudioClip* clip = MakeClip(48000, 1, 0.1f);
	AudioPlayParams onMuted = Loop(1.0f);
	onMuted.output = muted;
	const AudioVoice silenced = Audio::Play(ResourceOf(clip), onMuted);
	const AudioVoice a = Audio::Play(ResourceOf(clip), Loop(0.5f));
	const AudioVoice b = Audio::Play(ResourceOf(clip), Loop(0.5f));
	EXPECT_TRUE(Audio::IsVirtual(silenced));
	EXPECT_FALSE(Audio::IsVirtual(a));
	EXPECT_FALSE(Audio::IsVirtual(b));
}

TEST_F(VoiceBudgetFixture, HeldVoiceYieldsToWallVoice) {
	AudioSystem& system = GetAudioSystem();
	AudioBusDesc gameplay;
	gameplay.clock = AudioBusClock::Gameplay;
	const AudioBus bus = system.CreateBus(gameplay);
	ASSERT_TRUE(bus.IsValid());
	AudioClip* clip = MakeClip(48000, 1, 0.1f);
	AudioPlayParams footsteps = Loop();
	footsteps.output = bus;
	const AudioVoice step = Audio::Play(ResourceOf(clip), footsteps);
	const AudioVoice music = Audio::Play(ResourceOf(clip), Loop(0.3f));
	RunFrame(0.0f);
	const AudioVoice click = Audio::Play(ResourceOf(clip), Loop(0.3f));
	EXPECT_FALSE(Audio::IsVirtual(click));
	EXPECT_TRUE(Audio::IsVirtual(step));
	EXPECT_FALSE(Audio::IsVirtual(music));
}

TEST_F(VoiceBudgetFixture, FullPoolStopsLeastImportant) {
	AudioClip* clip = MakeClip(48000, 1, 0.1f);
	std::vector<AudioVoice> voices;
	for (u32 index = 0; index < 8; ++index) {
		voices.push_back(Audio::Play(ResourceOf(clip), Loop(1.0f, index == 5 ? 250 : 128)));
		ASSERT_TRUE(voices.back().IsValid());
	}
	EXPECT_FALSE(Audio::Play(ResourceOf(clip), Loop(1.0f, 250)).IsValid());
	const AudioVoice important = Audio::Play(ResourceOf(clip), Loop(1.0f, 64));
	EXPECT_TRUE(important.IsValid());
	EXPECT_FALSE(Audio::IsPlaying(voices[5]));
	EXPECT_EQ(Counts().stoppedByPool, 1u);
	EXPECT_EQ(Counts().refusedByPool, 1u);
	EXPECT_EQ(GetAudioSystem().GetVoices().CountLive(), 8u);
}

TEST_F(VoiceBudgetFixture, Priority0NeverStopped) {
	AudioClip* clip = MakeClip(48000, 1, 0.1f);
	std::vector<AudioVoice> voices;
	for (u32 index = 0; index < 8; ++index) {
		voices.push_back(Audio::Play(ResourceOf(clip), Loop(0.1f, 0)));
	}
	EXPECT_FALSE(Audio::Play(ResourceOf(clip), Loop(1.0f, 0)).IsValid());
	for (const AudioVoice& voice : voices) {
		EXPECT_TRUE(Audio::IsPlaying(voice));
	}
}

TEST_F(VoiceBudgetFixture, ConcurrencyGroupCountsVirtualVoicesToo) {
	AudioClip* clip = MakeClip(48000, 1, 0.1f);
	AudioConcurrency* three = MakeConcurrency(3, AudioConcurrencyRule::PreventNew);
	std::vector<AudioVoice> voices;
	for (u32 index = 0; index < 3; ++index) {
		voices.push_back(Audio::Play(ResourceOf(clip), InGroup(three)));
		ASSERT_TRUE(voices.back().IsValid());
	}
	ASSERT_EQ(std::count_if(voices.begin(), voices.end(), [](AudioVoice voice) { return Audio::IsVirtual(voice); }), 1);
	EXPECT_FALSE(Audio::Play(ResourceOf(clip), InGroup(three)).IsValid());
	EXPECT_EQ(Counts().refusedByConcurrency, 1u);
}

TEST_F(VoiceBudgetFixture, StopOldestAndPreventNew) {
	AudioClip* clip = MakeClip(48000, 1, 0.1f);
	AudioConcurrency* oldest = MakeConcurrency(3, AudioConcurrencyRule::StopOldest);
	const AudioVoice outside = Audio::Play(ResourceOf(clip), Loop());
	std::vector<AudioVoice> voices;
	for (u32 index = 0; index < 4; ++index) {
		voices.push_back(Audio::Play(ResourceOf(clip), InGroup(oldest)));
		ASSERT_TRUE(voices.back().IsValid());
	}
	EXPECT_FALSE(Audio::IsPlaying(voices[0]));
	EXPECT_TRUE(Audio::IsPlaying(voices[1]) && Audio::IsPlaying(voices[2]) && Audio::IsPlaying(voices[3]));
	EXPECT_TRUE(Audio::IsPlaying(outside));
	EXPECT_EQ(Counts().stoppedByConcurrency, 1u);

	for (const AudioVoice& voice : voices) {
		Audio::Stop(voice);
	}
	AudioConcurrency* prevent = MakeConcurrency(1, AudioConcurrencyRule::PreventNew);
	const AudioVoice kept = Audio::Play(ResourceOf(clip), InGroup(prevent));
	EXPECT_TRUE(kept.IsValid());
	EXPECT_FALSE(Audio::Play(ResourceOf(clip), InGroup(prevent)).IsValid());
	EXPECT_TRUE(Audio::IsPlaying(kept));
}

TEST_F(VoiceBudgetFixture, StopFarthestStopsVoiceFarthestFromListener) {
	AddListener();
	AudioClip* clip = MakeClip(48000, 1, 0.1f);
	AudioConcurrency* farthest = MakeConcurrency(2, AudioConcurrencyRule::StopFarthestThenOldest);
	AudioPlayParams params = InGroup(farthest);
	params.position = Vec3{10.0f, 0.0f, 0.0f};
	const AudioVoice near = Audio::Play(ResourceOf(clip), params);
	params.position = Vec3{0.0f, 0.0f, -30.0f};
	const AudioVoice far = Audio::Play(ResourceOf(clip), params);
	params.position = Vec3{0.0f, 20.0f, 0.0f};
	const AudioVoice between = Audio::Play(ResourceOf(clip), params);
	EXPECT_TRUE(between.IsValid());
	EXPECT_FALSE(Audio::IsPlaying(far));
	EXPECT_TRUE(Audio::IsPlaying(near));
}

TEST_F(VoiceBudgetFixture, LimitToOwnerCapsEachOwnerApart) {
	AudioClip* clip = MakeClip(48000, 1, 0.1f);
	AudioClip* ownerA = MakeClip(16, 1, 0.0f);
	AudioClip* ownerB = MakeClip(16, 1, 0.0f);
	AudioConcurrency* perOwner = MakeConcurrency(1, AudioConcurrencyRule::PreventNew, true);
	EXPECT_TRUE(Audio::Play(ResourceOf(clip), InGroup(perOwner, Handle<Object>(ownerA))).IsValid());
	EXPECT_TRUE(Audio::Play(ResourceOf(clip), InGroup(perOwner, Handle<Object>(ownerB))).IsValid());
	EXPECT_FALSE(Audio::Play(ResourceOf(clip), InGroup(perOwner, Handle<Object>(ownerA))).IsValid());
}

TEST_F(VoiceBudgetFixture, RetriggerTimeRefusesEarlyStart) {
	AudioClip* clip = MakeClip(48000, 1, 0.1f);
	AudioConcurrency* spaced = MakeConcurrency(8, AudioConcurrencyRule::PreventNew);
	spaced->retriggerTime = 0.1f;
	const u64 started = Audio::GetTime().frames;
	EXPECT_TRUE(Audio::Play(ResourceOf(clip), InGroup(spaced)).IsValid());
	EXPECT_FALSE(Audio::Play(ResourceOf(clip), InGroup(spaced)).IsValid());
	const u64 window = static_cast<u64>(0.1 * static_cast<f64>(Audio::GetOutputSampleRate()));
	ASSERT_TRUE(WaitFor([&]() { return Audio::GetTime().frames > started + window + AudioRenderer::BLOCK_FRAMES; }));
	EXPECT_TRUE(Audio::Play(ResourceOf(clip), InGroup(spaced)).IsValid());
}

TEST_F(VoiceBudgetFixture, GroupStopFadesOverStealRelease) {
	AudioClip* clip = MakeClip(48000, 1, 0.1f);
	AudioConcurrency* single = MakeConcurrency(1, AudioConcurrencyRule::StopOldest);
	single->voiceStealReleaseTime = 0.2f;
	const AudioVoice first = Audio::Play(ResourceOf(clip), InGroup(single));
	ASSERT_TRUE(WaitForBlocks(4));
	const u64 before = BlocksRendered();
	ASSERT_TRUE(Audio::Play(ResourceOf(clip), InGroup(single)).IsValid());
	EXPECT_FALSE(Audio::IsPlaying(first));
	ASSERT_TRUE(WaitFor([]() { return ClaimedVoices() == 1; }));
	const u64 releaseBlocks = static_cast<u64>(0.2 * static_cast<f64>(Audio::GetOutputSampleRate())) / AudioRenderer::BLOCK_FRAMES;
	EXPECT_GE(BlocksRendered() - before, releaseBlocks);
}

TEST_F(VoiceBudgetFixture, SourceVirtualWhenAllVoicesAre) {
	AudioClip* clip = MakeClip(48000, 1, 0.1f);
	Handle<AudioSource> a = AddSource(clip);
	Handle<AudioSource> b = AddSource(clip);
	Handle<AudioSource> c = AddSource(clip);
	Handle<AudioSource> idle = AddSource(clip);
	c->SetPriority(200);
	a->Play();
	b->Play();
	c->Play();
	EXPECT_TRUE(c->IsVirtual());
	EXPECT_TRUE(c->IsPlaying());
	EXPECT_FALSE(a->IsVirtual());
	EXPECT_FALSE(idle->IsVirtual());

	c->SetPriority(0);
	RunFrame();
	EXPECT_FALSE(c->IsVirtual());
	EXPECT_TRUE(a->IsVirtual() || b->IsVirtual());
}

TEST_F(VoiceBudgetFixture, SourcesConcurrencyGroupIsLimitedPerObject) {
	AudioClip* clip = MakeClip(48000, 1, 0.1f);
	AudioConcurrency* perObject = MakeConcurrency(1, AudioConcurrencyRule::PreventNew, true);
	Handle<AudioSource> first = AddSource(clip);
	Handle<AudioSource> second = AddSource(clip, Vec3{5.0f, 0.0f, 0.0f});
	first->SetConcurrency(Handle<AudioConcurrency>(perObject));
	second->SetConcurrency(Handle<AudioConcurrency>(perObject));
	first->PlayOneShot(ResourceOf(clip));
	first->PlayOneShot(ResourceOf(clip));
	EXPECT_EQ(Counts().refusedByConcurrency, 1u);
	second->PlayOneShot(ResourceOf(clip));
	EXPECT_EQ(Counts().refusedByConcurrency, 1u);
	EXPECT_TRUE(first->IsPlaying() && second->IsPlaying());
}

TEST_F(VoiceBudgetFixture, VoiceCountChangeKeepsFitting) {
	AudioClip* clip = MakeClip(480000, 1, 0.1f);
	std::vector<AudioVoice> voices;
	for (u32 index = 0; index < 6; ++index) {
		voices.push_back(Audio::Play(ResourceOf(clip), Loop()));
	}
	ASSERT_TRUE(WaitForBlocks(4));
	const u64 reached = PositionOf(voices[4]);

	SetVoiceCounts(64, 4);
	EXPECT_EQ(GetAudioSystem().GetVoices().GetLimit(), 64u);
	EXPECT_EQ(GetAudioSystem().GetMaxRealVoices(), 4u);
	for (const AudioVoice& voice : voices) {
		EXPECT_TRUE(Audio::IsPlaying(voice));
	}
	EXPECT_GE(PositionOf(voices[4]), reached);
	EXPECT_TRUE(WaitFor([]() { return RealVoicesRendered() == 4; }));

	SetVoiceCounts(3, 2);
	EXPECT_EQ(GetAudioSystem().GetVoices().CountLive(), 3u);
	for (u32 index = 0; index < 6; ++index) {
		EXPECT_EQ(Audio::IsPlaying(voices[index]), index >= 3) << "voice " << index;
	}
	EXPECT_TRUE(WaitFor([]() { return RealVoicesRendered() == 2; }));
}

namespace {

	class VoicesAtScaleFixture : public VoiceBudgetFixture {
	protected:
		str ProjectSettings() const override { return str("[Audio]\ndevice = None\nmaxVirtualVoices = 2048\nmaxRealVoices = 32\n"); }

		void SetUp() override {
			LivePackageFixture::SetUp();
			_scene = SceneSystem::CreateScene("scale");
			ASSERT_TRUE(_scene.IsValid());
		}
	};

} // namespace

TEST_F(VoicesAtScaleFixture, Nearest32Of2000AreHeard) {
	constexpr u32 SOURCES = 2000;
	constexpr u32 REAL = 32;
	AddListener();
	AudioClip* clip = MakeClip(480, 1, 0.1f);
	std::vector<Handle<AudioSource>> sources(SOURCES);
	for (u32 step = 0; step < SOURCES; ++step) {
		const u32 index = (step * 7919u) % SOURCES;
		Handle<AudioSource> source = AddSource(clip, Vec3{2.0f + 0.25f * static_cast<f32>(index), 0.0f, 0.0f});
		source->SetSpatialBlend(1.0f);
		source->SetPlayOnAwake(true);
		sources[index] = source;
	}
	RunFrame();
	RunFrame();

	u32 heard = 0;
	u32 misplaced = 0;
	for (u32 index = 0; index < SOURCES; ++index) {
		ASSERT_TRUE(sources[index]->IsPlaying()) << "source " << index;
		const bool isHeard = !sources[index]->IsVirtual();
		heard += isHeard ? 1u : 0u;
		misplaced += isHeard != (index < REAL) ? 1u : 0u;
	}
	EXPECT_EQ(heard, REAL);
	EXPECT_EQ(misplaced, 0u);
	EXPECT_TRUE(WaitFor([]() { return RealVoicesRendered() == REAL && GetAudioSystem().GetRenderer().GetCounters().voices.load() == SOURCES; }));
	EXPECT_EQ(GetAudioSystem().GetRenderer().GetCounters().voices.load(), SOURCES);
	EXPECT_EQ(GetAudioSystem().GetRenderer().GetCounters().decoderShortfalls.load(), 0u);
}

#endif
