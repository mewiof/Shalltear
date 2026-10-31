#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioLiveFixture.h"
#include "AudioOutputs.h"
#include "AudioPlayback.h"

#include "Containers/AudioContainerPlayback.h"
#include "Containers/AudioContainerState.h"

#include "Shalltear/Audio/AudioContainer.h"
#include "Shalltear/Audio/AudioMusicClock.h"
#include "Shalltear/Audio/AudioSource.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"

#include <algorithm>
#include <optional>
#include <set>
#include <vector>

using namespace AudioTesting;

namespace {

	struct PickRig {
		AudioRandomContainer container;
		std::vector<AudioClip*> resources;
		AudioRandomPickState state;

		explicit PickRig(u32 count, AudioRandomContainerPlaybackMode mode, i32 avoid = 0) {
			container.playbackMode = mode;
			container.avoidRepeatingLast = avoid;
			for (u32 index = 0; index < count; ++index) {
				resources.push_back(New<AudioClip>(MemTag::Asset));
				AudioRandomContainerElement element;
				element.resource = Handle<AudioResource>(resources.back());
				container.elements.push_back(element);
			}
		}
		~PickRig() {
			for (AudioClip* resource : resources) {
				Delete(resource);
			}
		}

		i32 Next() { return PickRandomElement(container, state); }
	};

} // namespace

TEST(AudioContainerTest, AShuffleNeverRepeatsWithinACycle) {
	constexpr u32 COUNT = 5;
	PickRig rig(COUNT, AudioRandomContainerPlaybackMode::Shuffle);
	for (u32 cycle = 0; cycle < 200; ++cycle) {
		std::set<i32> seen;
		for (u32 pick = 0; pick < COUNT; ++pick) {
			const i32 index = rig.Next();
			ASSERT_GE(index, 0);
			EXPECT_TRUE(seen.insert(index).second) << "cycle " << cycle << " repeated element " << index;
		}
	}
}

TEST(AudioContainerTest, AvoidRepeatingLastHoldsAcrossShufflesRefill) {
	PickRig rig(4, AudioRandomContainerPlaybackMode::Shuffle, 1);
	i32 previous = -1;
	for (u32 pick = 0; pick < 4000; ++pick) {
		const i32 index = rig.Next();
		ASSERT_GE(index, 0);
		EXPECT_NE(index, previous) << "pick " << pick;
		previous = index;
	}
}

TEST(AudioContainerTest, RandomAvoidsRecentReachesAll) {
	PickRig rig(4, AudioRandomContainerPlaybackMode::Random, 2);
	std::vector<i32> picks;
	for (u32 pick = 0; pick < 4000; ++pick) {
		picks.push_back(rig.Next());
	}
	for (size_t index = 2; index < picks.size(); ++index) {
		EXPECT_NE(picks[index], picks[index - 1]);
		EXPECT_NE(picks[index], picks[index - 2]);
	}
	EXPECT_EQ(std::set<i32>(picks.begin(), picks.end()).size(), 4u);

	PickRig pair(2, AudioRandomContainerPlaybackMode::Random, 5);
	const i32 first = pair.Next();
	EXPECT_NE(pair.Next(), first);
}

TEST(AudioContainerTest, SequentialWalksListInOrderSkippingDisabled) {
	PickRig rig(4, AudioRandomContainerPlaybackMode::Sequential);
	rig.container.elements[2].enabled = false;
	std::vector<i32> picks;
	for (u32 pick = 0; pick < 6; ++pick) {
		picks.push_back(rig.Next());
	}
	EXPECT_EQ(picks, (std::vector<i32>{0, 1, 3, 0, 1, 3}));
}

TEST(AudioContainerTest, ListOfAnotherLengthStartsWalkOver) {
	PickRig rig(3, AudioRandomContainerPlaybackMode::Sequential);
	EXPECT_EQ(rig.Next(), 0);
	EXPECT_EQ(rig.Next(), 1);
	AudioRandomContainerElement added;
	added.resource = Handle<AudioResource>(rig.resources[0]);
	rig.container.elements.push_back(added);
	EXPECT_EQ(rig.Next(), 0);
}

TEST(AudioContainerTest, NothingEnabledPicksNothing) {
	PickRig rig(3, AudioRandomContainerPlaybackMode::Shuffle);
	for (AudioRandomContainerElement& element : rig.container.elements) {
		element.enabled = false;
	}
	EXPECT_EQ(rig.Next(), -1);
}

TEST(AudioContainerTest, AssetsFieldsRoundTripThroughText) {
	AudioRandomContainer source;
	source.volume = 0.5f;
	source.pitch = -200.0f;
	source.triggerMode = AudioRandomContainerTriggerMode::Automatic;
	source.playbackMode = AudioRandomContainerPlaybackMode::Random;
	source.automaticTriggerMode = AudioRandomContainerAutomaticTriggerMode::Offset;
	source.loopMode = AudioRandomContainerLoopMode::Cycles;
	source.loopCount = 3;
	source.volumeRandomizationRange = Vec2{-6.0f, -1.0f};
	source.loopCountRandomizationRange = Vec2I{-1, 2};
	source.elements.resize(2);
	source.elements[0].volume = 0.25f;
	source.elements[1].enabled = false;

	const str text = TextSerializer::Serialize(&source, AudioRandomContainer::GetReflectType());
	AudioRandomContainer read;
	ASSERT_TRUE(TextSerializer::Deserialize(&read, AudioRandomContainer::GetReflectType(), text));
	EXPECT_EQ(read.volume, 0.5f);
	EXPECT_EQ(read.pitch, -200.0f);
	EXPECT_EQ(read.triggerMode, AudioRandomContainerTriggerMode::Automatic);
	EXPECT_EQ(read.playbackMode, AudioRandomContainerPlaybackMode::Random);
	EXPECT_EQ(read.automaticTriggerMode, AudioRandomContainerAutomaticTriggerMode::Offset);
	EXPECT_EQ(read.loopMode, AudioRandomContainerLoopMode::Cycles);
	EXPECT_EQ(read.loopCount, 3);
	EXPECT_EQ(read.volumeRandomizationRange.x, -6.0f);
	EXPECT_EQ(read.loopCountRandomizationRange.y, 2);
	ASSERT_EQ(read.elements.size(), 2u);
	EXPECT_EQ(read.elements[0].volume, 0.25f);
	EXPECT_FALSE(read.elements[1].enabled);
}

namespace {

	class ContainerFixture : public LivePackageFixture {
	protected:
		std::vector<AudioContainer*> _containers;

		void TearDown() override {
			LivePackageFixture::TearDown();
			for (AudioContainer* container : _containers) {
				Delete(container);
			}
			_containers.clear();
		}

		AudioRandomContainer* MakeContainer(std::initializer_list<AudioResource*> resources) {
			auto* container = New<AudioRandomContainer>(MemTag::Asset);
			for (AudioResource* resource : resources) {
				AudioRandomContainerElement element;
				element.resource = Handle<AudioResource>(resource);
				container->elements.push_back(element);
			}
			_containers.push_back(container);
			return container;
		}

		AudioLayeredContainer* MakeLayered(std::initializer_list<AudioResource*> resources) {
			auto* container = New<AudioLayeredContainer>(MemTag::Asset);
			for (AudioResource* resource : resources) {
				AudioLayeredContainerLayer layer;
				layer.resource = Handle<AudioResource>(resource);
				container->layers.push_back(layer);
			}
			_containers.push_back(container);
			return container;
		}

		AudioInteractiveContainer* MakeInteractive(f32 bpm, std::initializer_list<std::pair<const char*, AudioResource*>> clips) {
			auto* container = New<AudioInteractiveContainer>(MemTag::Asset);
			container->beatsPerMinute = bpm;
			container->beatsPerBar = 4;
			for (const auto& [name, resource] : clips) {
				AudioInteractiveContainerClip clip;
				clip.name = name;
				clip.resource = Handle<AudioResource>(resource);
				clip.loop = true;
				container->clips.push_back(clip);
			}
			_containers.push_back(container);
			return container;
		}

		static AudioInteractiveContainerTransition& AddTransition(AudioInteractiveContainer* container, const char* from, const char* to,
																  AudioInteractiveTransitionFromTime fromTime, AudioInteractiveTransitionToTime toTime,
																  AudioInteractiveFadeMode fade) {
			AudioInteractiveContainerTransition row;
			row.fromClip = from;
			row.toClip = to;
			row.fromTime = fromTime;
			row.toTime = toTime;
			row.fade = fade;
			container->transitions.push_back(row);
			return container->transitions.back();
		}

		static std::optional<AudioContainerChild> NewestTagged(AudioVoice voice, u32 tag) {
			std::optional<AudioContainerChild> found;
			if (AudioContainerPlayback* playback = PlaybackOf(voice)) {
				for (const AudioContainerChild& child : playback->GetChildren()) {
					if (child.tag == tag) {
						found = child;
					}
				}
			}
			return found;
		}

		static u32 CountTagged(AudioVoice voice, u32 tag) {
			u32 count = 0;
			if (AudioContainerPlayback* playback = PlaybackOf(voice)) {
				for (const AudioContainerChild& child : playback->GetChildren()) {
					count += child.tag == tag ? 1 : 0;
				}
			}
			return count;
		}

		static u64 OutputFramesOf48k(u64 samples) { return samples * Audio::GetOutputSampleRate() / 48000; }

		static u64 Lead() { return GetAudioSystem().GetDevice().GetFormat().periodFrames + 2 * 256 + Audio::GetOutputSampleRate() / 30; }

		static Handle<AudioResource> Of(AudioResource* resource) { return Handle<AudioResource>(resource); }

		static AudioContainerPlayback* PlaybackOf(AudioVoice voice) {
			return static_cast<AudioContainerPlayback*>(FindComposite(voice));
		}

		static void Gather(AudioVoice voice, std::set<u64>& starts) {
			if (AudioContainerPlayback* playback = PlaybackOf(voice)) {
				for (const AudioContainerChild& child : playback->GetChildren()) {
					starts.insert(child.start.frames);
				}
			}
		}

		template <typename Condition>
		static bool RunUntil(Condition&& condition, std::chrono::milliseconds deadline = std::chrono::milliseconds(3000)) {
			return WaitFor([&]() {
				RunFrame();
				return condition();
			},
						   deadline);
		}

		static bool RunPast(AudioTime time) {
			const bool reached = RunUntil([&]() { return Audio::GetTime() > time; });
			RunFrame();
			return reached;
		}

		static u64 FramesOf(f64 seconds) {
			return static_cast<u64>(seconds * static_cast<f64>(Audio::GetOutputSampleRate()) + 0.5);
		}

		static const AudioVoiceRecord* RecordOf(AudioVoice voice) {
			return GetAudioSystem().GetVoices().Find(AudioVoiceAccess::GetSlot(voice), AudioVoiceAccess::GetGeneration(voice));
		}
	};

} // namespace

TEST_F(ContainerFixture, ManualPlaysOneElementPerPlay) {
	AudioClip* a = MakeClip(48000, 1, 0.1f);
	AudioClip* b = MakeClip(48000, 1, 0.1f);
	AudioRandomContainer* container = MakeContainer({a, b});
	container->playbackMode = AudioRandomContainerPlaybackMode::Sequential;

	const AudioVoice first = Audio::Play(Of(container));
	ASSERT_TRUE(first.IsValid());
	ASSERT_NE(PlaybackOf(first), nullptr);
	EXPECT_TRUE(Audio::IsPlaying(first));
	ASSERT_EQ(PlaybackOf(first)->GetChildren().size(), 1u);
	EXPECT_EQ(PlaybackOf(first)->GetChildren()[0].tag, 0u);

	const AudioVoice second = Audio::Play(Of(container));
	ASSERT_NE(PlaybackOf(second), nullptr);
	EXPECT_EQ(PlaybackOf(second)->GetChildren()[0].tag, 1u);
	EXPECT_EQ(ClaimedVoices(), 2u);
}

TEST_F(ContainerFixture, StopEndsStartedVoices) {
	AudioClip* clip = MakeClip(96000, 1, 0.1f);
	AudioRandomContainer* container = MakeContainer({clip});
	AudioPlayParams params;
	params.loop = true;
	const AudioVoice voice = Audio::Play(Of(container), params);
	ASSERT_NE(PlaybackOf(voice), nullptr);
	const AudioVoice element = PlaybackOf(voice)->GetChildren()[0].voice;
	ASSERT_TRUE(Audio::IsPlaying(element));

	Audio::Stop(voice);
	EXPECT_FALSE(Audio::IsPlaying(voice));
	EXPECT_FALSE(Audio::IsPlaying(element));
	EXPECT_TRUE(RunUntil([&]() { return PlaybackOf(voice) == nullptr; }));
	EXPECT_TRUE(WaitFor([]() { return ClaimedVoices() == 0; }));
}

TEST_F(ContainerFixture, VolumeReachesEveryElement) {
	AudioClip* clip = MakeClip(96000, 1, 0.1f);
	AudioRandomContainer* container = MakeContainer({clip, clip});
	container->volume = 0.8f;
	container->playbackMode = AudioRandomContainerPlaybackMode::Sequential;
	container->elements[1].volume = 0.5f;
	container->triggerMode = AudioRandomContainerTriggerMode::Automatic;
	container->automaticTriggerTime = 0.05f;

	const AudioVoice voice = Audio::Play(Of(container));
	ASSERT_NE(PlaybackOf(voice), nullptr);
	Audio::SetVolume(voice, 0.5f);
	ASSERT_TRUE(RunUntil([&]() { return PlaybackOf(voice)->GetStartedCount() >= 2; }));
	for (const AudioContainerChild& child : PlaybackOf(voice)->GetChildren()) {
		const AudioVoiceRecord* record = RecordOf(child.voice);
		ASSERT_NE(record, nullptr);
		const f32 element = child.tag == 1 ? 0.5f : 1.0f;
		EXPECT_FLOAT_EQ(record->volume, 0.5f * 0.8f * element) << "element " << child.tag;
	}
}

TEST_F(ContainerFixture, PulseStartsAtInterval) {
	AudioClip* clip = MakeClip(96000, 1, 0.1f);
	AudioRandomContainer* container = MakeContainer({clip});
	container->triggerMode = AudioRandomContainerTriggerMode::Automatic;
	container->automaticTriggerMode = AudioRandomContainerAutomaticTriggerMode::Pulse;
	container->automaticTriggerTime = 0.05f;

	const AudioVoice voice = Audio::Play(Of(container));
	std::set<u64> starts;
	ASSERT_TRUE(RunUntil([&]() {
		Gather(voice, starts);
		return starts.size() >= 6;
	}));
	const std::vector<u64> ordered(starts.begin(), starts.end());
	for (size_t index = 1; index < ordered.size(); ++index) {
		EXPECT_EQ(ordered[index] - ordered[index - 1], FramesOf(0.05)) << "start " << index;
	}
}

TEST_F(ContainerFixture, OffsetStartsAfterLengthAndPause) {
	AudioClip* clip = MakeClip(1440, 1, 0.1f);
	AudioRandomContainer* container = MakeContainer({clip});
	container->triggerMode = AudioRandomContainerTriggerMode::Automatic;
	container->automaticTriggerMode = AudioRandomContainerAutomaticTriggerMode::Offset;
	container->automaticTriggerTime = 0.02f;

	const AudioVoice voice = Audio::Play(Of(container));
	std::set<u64> starts;
	ASSERT_TRUE(RunUntil([&]() {
		Gather(voice, starts);
		return starts.size() >= 4;
	}));
	const u64 length = 1440ull * Audio::GetOutputSampleRate() / 48000;
	const std::vector<u64> ordered(starts.begin(), starts.end());
	for (size_t index = 1; index < ordered.size(); ++index) {
		EXPECT_EQ(ordered[index] - ordered[index - 1], length + FramesOf(0.02)) << "start " << index;
	}
}

TEST_F(ContainerFixture, AutomaticContainerStartsCountAndThenFinishes) {
	AudioClip* clip = MakeClip(480, 1, 0.1f);
	AudioRandomContainer* container = MakeContainer({clip, clip, clip});
	container->triggerMode = AudioRandomContainerTriggerMode::Automatic;
	container->automaticTriggerTime = 0.02f;

	container->loopMode = AudioRandomContainerLoopMode::Clips;
	container->loopCount = 3;
	const AudioVoice clips = Audio::Play(Of(container));
	ASSERT_NE(PlaybackOf(clips), nullptr);
	AudioContainerPlayback* clipsPlayback = PlaybackOf(clips);
	u32 clipsStarted = 0;
	ASSERT_TRUE(RunUntil([&]() {
		if (AudioContainerPlayback* playback = PlaybackOf(clips)) {
			clipsStarted = playback->GetStartedCount();
		}
		return PlaybackOf(clips) == nullptr;
	}));
	(void)clipsPlayback;
	EXPECT_EQ(clipsStarted, 3u);

	container->loopMode = AudioRandomContainerLoopMode::Cycles;
	container->loopCount = 2;
	const AudioVoice cycles = Audio::Play(Of(container));
	u32 cyclesStarted = 0;
	ASSERT_TRUE(RunUntil([&]() {
		if (AudioContainerPlayback* playback = PlaybackOf(cycles)) {
			cyclesStarted = playback->GetStartedCount();
		}
		return PlaybackOf(cycles) == nullptr;
	}));
	EXPECT_EQ(cyclesStarted, 6u);
	EXPECT_FALSE(Audio::IsPlaying(cycles));
}

TEST_F(ContainerFixture, StartWaitsPause) {
	AudioClip* clip = MakeClip(96000, 1, 0.1f);
	AudioRandomContainer* container = MakeContainer({clip});
	container->triggerMode = AudioRandomContainerTriggerMode::Automatic;
	container->automaticTriggerTime = 0.08f;

	const AudioVoice voice = Audio::Play(Of(container));
	ASSERT_TRUE(RunUntil([&]() {
		const AudioTime now = Audio::GetTime();
		for (const AudioContainerChild& child : PlaybackOf(voice)->GetChildren()) {
			if (child.start > now) {
				return true;
			}
		}
		return false;
	}));
	const AudioTime pausedAt = Audio::GetTime();
	Audio::Pause(voice);
	std::vector<u64> before;
	for (const AudioContainerChild& child : PlaybackOf(voice)->GetChildren()) {
		before.push_back(child.start.frames);
	}
	for (u32 frame = 0; frame < 10; ++frame) {
		RunFrame();
		WaitForBlocks(2);
	}
	Audio::UnPause(voice);
	const std::span<const AudioContainerChild> after = PlaybackOf(voice)->GetChildren();
	ASSERT_EQ(after.size(), before.size());
	i64 moved = 0;
	for (size_t index = 0; index < after.size(); ++index) {
		if (before[index] <= pausedAt.frames) {
			continue;
		}
		const i64 by = static_cast<i64>(after[index].start.frames) - static_cast<i64>(before[index]);
		EXPECT_GT(by, 0);
		if (moved == 0) {
			moved = by;
		}
		EXPECT_EQ(by, moved);
	}
	EXPECT_NE(moved, 0);
}

TEST_F(ContainerFixture, ManualLoopsUntilLoopOff) {
	AudioClip* clip = MakeClip(960, 1, 0.1f);
	AudioRandomContainer* container = MakeContainer({clip});
	AudioPlayParams params;
	params.loop = true;
	const AudioVoice voice = Audio::Play(Of(container), params);
	const AudioVoice element = PlaybackOf(voice)->GetChildren()[0].voice;
	WaitForBlocks(40);
	EXPECT_TRUE(Audio::IsPlaying(element));

	Audio::SetLoop(voice, false);
	EXPECT_TRUE(WaitFor([&]() { return !Audio::IsPlaying(element); }));
}

TEST_F(ContainerFixture, AudioSourcePlaysContainerThroughResource) {
	AudioClip* clip = MakeClip(96000, 1, 0.1f);
	AudioRandomContainer* container = MakeContainer({clip});
	Handle<Scene> scene = SceneSystem::CreateScene("containers");
	Handle<SceneObject> object = SceneSystem::CreateObject("source", scene);
	Handle<AudioSource> source = object->AddComponent<AudioSource>();
	source->SetPlayOnAwake(false);
	source->SetResource(Of(container));

	source->Play();
	EXPECT_TRUE(source->IsPlaying());
	source->PlayOneShot(Of(container));
	EXPECT_EQ(ClaimedVoices(), 2u);

	source->Stop();
	EXPECT_FALSE(source->IsPlaying());
	EXPECT_TRUE(WaitFor([]() { return ClaimedVoices() == 0; }));
}

TEST_F(ContainerFixture, OuterStopReachesNested) {
	AudioClip* clip = MakeClip(96000, 1, 0.1f);
	AudioRandomContainer* inner = MakeContainer({clip});
	AudioRandomContainer* outer = MakeContainer({inner});
	AudioPlayParams params;
	params.loop = true;
	const AudioVoice voice = Audio::Play(Of(outer), params);
	ASSERT_NE(PlaybackOf(voice), nullptr);
	const AudioVoice innerVoice = PlaybackOf(voice)->GetChildren()[0].voice;
	ASSERT_NE(PlaybackOf(innerVoice), nullptr);
	const AudioVoice element = PlaybackOf(innerVoice)->GetChildren()[0].voice;
	EXPECT_TRUE(Audio::IsPlaying(element));

	Audio::Stop(voice);
	EXPECT_FALSE(Audio::IsPlaying(element));
}

TEST_F(ContainerFixture, ContainerThatContainsItselfIsNotPlayed) {
	AudioClip* clip = MakeClip(480, 1, 0.1f);
	AudioRandomContainer* a = MakeContainer({clip});
	AudioRandomContainer* b = MakeContainer({a});
	AudioRandomContainerElement back;
	back.resource = Of(b);
	a->elements.push_back(back);
	EXPECT_FALSE(Audio::Play(Of(a)).IsValid());
	EXPECT_FALSE(Audio::Play(Of(b)).IsValid());
	EXPECT_EQ(ClaimedVoices(), 0u);
}

TEST_F(ContainerFixture, StopAllHaltsAutomatic) {
	AudioClip* clip = MakeClip(480, 1, 0.1f);
	AudioRandomContainer* container = MakeContainer({clip});
	container->triggerMode = AudioRandomContainerTriggerMode::Automatic;
	container->automaticTriggerTime = 0.02f;
	const AudioVoice voice = Audio::Play(Of(container));
	ASSERT_TRUE(RunUntil([&]() { return PlaybackOf(voice)->GetStartedCount() >= 2; }));

	EXPECT_GE(StopGameVoices(), 1u);
	EXPECT_TRUE(RunUntil([&]() { return PlaybackOf(voice) == nullptr; }));
	for (u32 frame = 0; frame < 10; ++frame) {
		RunFrame();
		WaitForBlocks(2);
	}
	EXPECT_EQ(ClaimedVoices(), 0u);
}

TEST_F(ContainerFixture, LayersStartOnOneFrame) {
	AudioClip* a = MakeClip(96000, 1, 0.1f);
	AudioClip* b = MakeClip(96000, 1, 0.1f);
	AudioClip* c = MakeClip(96000, 1, 0.1f);
	const AudioVoice voice = Audio::Play(Of(MakeLayered({a, b, c})));
	ASSERT_NE(PlaybackOf(voice), nullptr);
	const std::span<const AudioContainerChild> layers = PlaybackOf(voice)->GetChildren();
	ASSERT_EQ(layers.size(), 3u);
	EXPECT_NE(layers[0].start.frames, 0u);
	EXPECT_EQ(layers[1].start, layers[0].start);
	EXPECT_EQ(layers[2].start, layers[0].start);
}

TEST_F(ContainerFixture, ALayersVolumeMovesThatLayerAlone) {
	AudioClip* clip = MakeClip(96000, 1, 0.1f);
	const AudioVoice voice = Audio::Play(Of(MakeLayered({clip, clip})));
	ASSERT_NE(PlaybackOf(voice), nullptr);
	Audio::SetLayerVolume(voice, 1, 0.25f);
	EXPECT_FLOAT_EQ(RecordOf(NewestTagged(voice, 0)->voice)->volume, 1.0f);
	EXPECT_FLOAT_EQ(RecordOf(NewestTagged(voice, 1)->voice)->volume, 0.25f);
	Audio::SetVolume(voice, 0.5f);
	EXPECT_FLOAT_EQ(RecordOf(NewestTagged(voice, 1)->voice)->volume, 0.125f);
}

TEST_F(ContainerFixture, SeekRestartsEveryLayerOnOneFrame) {
	AudioClip* clip = MakeClip(96000, 1, 0.1f);
	const AudioVoice voice = Audio::Play(Of(MakeLayered({clip, clip})));
	const AudioTime first = PlaybackOf(voice)->GetChildren()[0].start;
	Audio::SetTimeSamples(voice, 20000);
	const std::span<const AudioContainerChild> layers = PlaybackOf(voice)->GetChildren();
	ASSERT_EQ(layers.size(), 2u);
	EXPECT_GE(layers[0].start, first);
	EXPECT_EQ(layers[1].start, layers[0].start);
	EXPECT_EQ(layers[0].startSample, 20000u);
	EXPECT_EQ(layers[1].startSample, 20000u);
}

TEST_F(ContainerFixture, LayeredContainerEndsWithLastLayer) {
	AudioClip* shortClip = MakeClip(480, 1, 0.1f);
	AudioClip* longClip = MakeClip(9600, 1, 0.1f);
	const AudioVoice voice = Audio::Play(Of(MakeLayered({shortClip, longClip})));
	ASSERT_TRUE(RunUntil([&]() { return CountTagged(voice, 0) == 0; }));
	EXPECT_TRUE(Audio::IsPlaying(voice));
	EXPECT_TRUE(RunUntil([&]() { return PlaybackOf(voice) == nullptr; }));
}

TEST_F(ContainerFixture, PlayQuantizedStartsOnBarOfClock) {
	AudioClip* clip = MakeClip(96000, 1, 0.1f);
	AudioRandomContainer* container = MakeContainer({clip});
	const AudioMusicClock clock = Audio::CreateClock(960.0, 4);
	ASSERT_TRUE(clock.HasTempo());
	RunFrame();
	const AudioVoice voice = Audio::PlayQuantized(Of(container), clock, AudioQuantization::NextBar);
	ASSERT_NE(PlaybackOf(voice), nullptr);
	const AudioTime start = PlaybackOf(voice)->GetChildren()[0].start;
	EXPECT_EQ(clock.GetBeatTime(clock.GetBar(start) * 4), start);
}

TEST_F(ContainerFixture, SwitchLandsOnNextBar) {
	AudioClip* a = MakeClip(96000, 1, 0.1f);
	AudioClip* b = MakeClip(96000, 1, 0.2f);
	AudioInteractiveContainer* container = MakeInteractive(960.0f, {{"A", a}, {"B", b}});
	AddTransition(container, "A", "B", AudioInteractiveTransitionFromTime::NextBar, AudioInteractiveTransitionToTime::Start,
				  AudioInteractiveFadeMode::Disabled);
	const AudioVoice voice = Audio::Play(Of(container));
	ASSERT_NE(PlaybackOf(voice), nullptr);
	const AudioVoice first = NewestTagged(voice, 0)->voice;
	for (u32 frame = 0; frame < 5; ++frame) {
		RunFrame();
	}
	const AudioMusicClock before = Audio::GetMusicClock(voice);
	ASSERT_TRUE(before.HasTempo());
	const AudioTime asked = Audio::GetTime();
	Audio::SwitchToClip(voice, "B");
	ASSERT_TRUE(RunUntil([&]() { return NewestTagged(voice, 1).has_value(); }));

	const AudioTime landed = NewestTagged(voice, 1)->start;
	const u64 bar = before.GetBeatTime(4).frames - before.GetBeatTime(0).frames;
	EXPECT_EQ(before.GetBeatTime(before.GetBar(landed) * 4), landed);
	EXPECT_GE(landed, asked);
	EXPECT_LT(landed.frames, asked.frames + Lead() + bar);
	EXPECT_EQ(NewestTagged(voice, 1)->startSample, 0u);

	ASSERT_TRUE(RunPast(AudioTime{landed.frames + 2 * 256}));
	EXPECT_FALSE(Audio::IsPlaying(first));
	EXPECT_EQ(Audio::GetMusicClock(voice).origin, landed);
}

TEST_F(ContainerFixture, SamePositionContinuesMusic) {
	AudioClip* a = MakeClip(96000, 1, 0.1f);
	AudioClip* b = MakeClip(96000, 1, 0.2f);
	AudioInteractiveContainer* container = MakeInteractive(960.0f, {{"A", a}, {"B", b}});
	AddTransition(container, "A", "B", AudioInteractiveTransitionFromTime::NextBeat, AudioInteractiveTransitionToTime::SamePosition,
				  AudioInteractiveFadeMode::Disabled);
	const AudioVoice voice = Audio::Play(Of(container));
	const AudioContainerChild left = *NewestTagged(voice, 0);
	for (u32 frame = 0; frame < 5; ++frame) {
		RunFrame();
	}
	Audio::SwitchToClip(voice, "B");
	ASSERT_TRUE(RunUntil([&]() { return NewestTagged(voice, 1).has_value(); }));
	const AudioContainerChild entered = *NewestTagged(voice, 1);
	const u64 played = (entered.start.frames - left.start.frames) * 48000 / Audio::GetOutputSampleRate();
	EXPECT_NEAR(static_cast<f64>(entered.startSample), static_cast<f64>(played % 96000), 1.0);
}

TEST_F(ContainerFixture, CrossFadeMovesBothClipsAcrossBeats) {
	AudioClip* a = MakeClip(96000, 1, 0.1f);
	AudioClip* b = MakeClip(96000, 1, 0.2f);
	AudioInteractiveContainer* container = MakeInteractive(960.0f, {{"A", a}, {"B", b}});
	AddTransition(container, "A", "B", AudioInteractiveTransitionFromTime::NextBeat, AudioInteractiveTransitionToTime::Start,
				  AudioInteractiveFadeMode::Cross)
		.fadeBeats = 8.0f;
	const AudioVoice voice = Audio::Play(Of(container));
	const AudioVoice left = NewestTagged(voice, 0)->voice;
	RunFrame();
	Audio::SwitchToClip(voice, "B");
	ASSERT_TRUE(RunUntil([&]() { return NewestTagged(voice, 1).has_value(); }));
	const AudioContainerChild entered = *NewestTagged(voice, 1);
	const u64 fade = 8ull * 60 * Audio::GetOutputSampleRate() / 960;

	ASSERT_TRUE(RunPast(AudioTime{entered.start.frames + fade / 2}));
	const f32 leaving = RecordOf(left)->volume;
	const f32 entering = RecordOf(entered.voice)->volume;
	EXPECT_GT(leaving, 0.05f);
	EXPECT_LT(leaving, 0.95f);
	EXPECT_GT(entering, 0.05f);
	EXPECT_LT(entering, 0.95f);

	ASSERT_TRUE(RunPast(AudioTime{entered.start.frames + fade + 2 * 256}));
	EXPECT_FALSE(Audio::IsPlaying(left));
	EXPECT_FLOAT_EQ(RecordOf(NewestTagged(voice, 1)->voice)->volume, 1.0f);
}

TEST_F(ContainerFixture, AutoAdvanceStartsAtEnd) {
	AudioClip* intro = MakeClip(4800, 1, 0.1f);
	AudioClip* loop = MakeClip(96000, 1, 0.2f);
	AudioInteractiveContainer* container = MakeInteractive(960.0f, {{"Intro", intro}, {"Loop", loop}});
	container->clips[0].loop = false;
	container->clips[0].autoAdvance = AudioInteractiveAutoAdvanceMode::Enabled;
	container->clips[0].nextClip = "Loop";
	const AudioVoice voice = Audio::Play(Of(container));
	const AudioTime introStart = NewestTagged(voice, 0)->start;
	ASSERT_TRUE(RunUntil([&]() { return NewestTagged(voice, 1).has_value(); }));
	EXPECT_EQ(NewestTagged(voice, 1)->start.frames, introStart.frames + OutputFramesOf48k(4800));
}

TEST_F(ContainerFixture, FillerPlaysOnBoundary) {
	AudioClip* a = MakeClip(96000, 1, 0.1f);
	AudioClip* filler = MakeClip(2400, 1, 0.3f);
	AudioClip* b = MakeClip(96000, 1, 0.2f);
	AudioInteractiveContainer* container = MakeInteractive(960.0f, {{"A", a}, {"Fill", filler}, {"B", b}});
	AddTransition(container, "A", "B", AudioInteractiveTransitionFromTime::NextBeat, AudioInteractiveTransitionToTime::Start,
				  AudioInteractiveFadeMode::Disabled)
		.fillerClip = "Fill";
	const AudioVoice voice = Audio::Play(Of(container));
	RunFrame();
	Audio::SwitchToClip(voice, "B");
	ASSERT_TRUE(RunUntil([&]() { return NewestTagged(voice, 2).has_value(); }));
	ASSERT_TRUE(NewestTagged(voice, 1).has_value());
	const AudioContainerChild fill = *NewestTagged(voice, 1);
	const AudioTime next = NewestTagged(voice, 2)->start;
	EXPECT_EQ(next.frames, fill.start.frames + OutputFramesOf48k(2400));

	ASSERT_TRUE(RunPast(AudioTime{next.frames + 2 * 256}));
	EXPECT_FALSE(Audio::IsPlaying(fill.voice));
}

TEST_F(ContainerFixture, ReturnToHoldResumesHeld) {
	AudioClip* music = MakeClip(96000, 1, 0.1f);
	AudioClip* stinger = MakeClip(2400, 1, 0.3f);
	AudioInteractiveContainer* container = MakeInteractive(960.0f, {{"Music", music}, {"Stinger", stinger}});
	container->clips[1].loop = false;
	container->clips[1].autoAdvance = AudioInteractiveAutoAdvanceMode::ReturnToHold;
	AddTransition(container, "Music", "Stinger", AudioInteractiveTransitionFromTime::Immediate, AudioInteractiveTransitionToTime::Start,
				  AudioInteractiveFadeMode::Disabled)
		.holdPrevious = true;
	const AudioVoice voice = Audio::Play(Of(container));
	const AudioVoice firstMusic = NewestTagged(voice, 0)->voice;
	RunFrame();
	Audio::SwitchToClip(voice, "Stinger");
	ASSERT_TRUE(RunUntil([&]() { return NewestTagged(voice, 1).has_value(); }));
	const AudioTime stingerStart = NewestTagged(voice, 1)->start;
	ASSERT_TRUE(RunUntil([&]() {
		const std::optional<AudioContainerChild> again = NewestTagged(voice, 0);
		return again && again->voice != firstMusic;
	}));
	EXPECT_EQ(NewestTagged(voice, 0)->start.frames, stingerStart.frames + OutputFramesOf48k(2400));
}

TEST_F(ContainerFixture, PreviousPositionResumesWhereClipWasLeft) {
	AudioClip* a = MakeClip(96000, 1, 0.1f);
	AudioClip* b = MakeClip(96000, 1, 0.2f);
	AudioInteractiveContainer* container = MakeInteractive(960.0f, {{"A", a}, {"B", b}});
	AddTransition(container, "A", "B", AudioInteractiveTransitionFromTime::NextBeat, AudioInteractiveTransitionToTime::Start,
				  AudioInteractiveFadeMode::Disabled);
	AddTransition(container, "B", "A", AudioInteractiveTransitionFromTime::NextBeat, AudioInteractiveTransitionToTime::PreviousPosition,
				  AudioInteractiveFadeMode::Disabled);
	const AudioVoice voice = Audio::Play(Of(container));
	const AudioContainerChild firstA = *NewestTagged(voice, 0);
	RunFrame();
	Audio::SwitchToClip(voice, "B");
	ASSERT_TRUE(RunUntil([&]() { return NewestTagged(voice, 1).has_value(); }));
	const AudioTime leftAt = NewestTagged(voice, 1)->start;
	ASSERT_TRUE(RunPast(leftAt));
	Audio::SwitchToClip(voice, "A");
	ASSERT_TRUE(RunUntil([&]() {
		const std::optional<AudioContainerChild> again = NewestTagged(voice, 0);
		return again && again->voice != firstA.voice;
	}));
	const u64 left = (leftAt.frames - firstA.start.frames) * 48000 / Audio::GetOutputSampleRate();
	EXPECT_NEAR(static_cast<f64>(NewestTagged(voice, 0)->startSample), static_cast<f64>(left % 96000), 1.0);
}

TEST_F(ContainerFixture, SwitchToPlayingRestartsNothing) {
	AudioClip* a = MakeClip(480000, 1, 0.1f);
	AudioClip* b = MakeClip(96000, 1, 0.2f);
	AudioInteractiveContainer* container = MakeInteractive(240.0f, {{"A", a}, {"B", b}});
	AddTransition(container, "", "", AudioInteractiveTransitionFromTime::NextBar, AudioInteractiveTransitionToTime::Start,
				  AudioInteractiveFadeMode::Disabled);
	const AudioVoice voice = Audio::Play(Of(container));
	const AudioTime start = NewestTagged(voice, 0)->start;
	ASSERT_TRUE(RunUntil([&]() { return Audio::GetTime() > start; }));
	Audio::SwitchToClip(voice, "B");
	ASSERT_TRUE(RunUntil([&]() {
		Audio::SwitchToClip(voice, "A");
		return Audio::GetTime() > AudioTime{start.frames + Audio::GetOutputSampleRate() + Lead()};
	}));
	EXPECT_FALSE(NewestTagged(voice, 1).has_value());
	EXPECT_EQ(PlaybackOf(voice)->GetStartedCount(), 1u);
}

TEST_F(ContainerFixture, AdvanceFromNestedContainerWaitsForEnd) {
	AudioClip* element = MakeClip(4800, 1, 0.1f);
	AudioClip* loop = MakeClip(96000, 1, 0.2f);
	AudioRandomContainer* intro = MakeContainer({element});
	AudioInteractiveContainer* container = MakeInteractive(960.0f, {{"Intro", intro}, {"Loop", loop}});
	container->clips[0].loop = false;
	container->clips[0].autoAdvance = AudioInteractiveAutoAdvanceMode::Enabled;
	container->clips[0].nextClip = "Loop";
	const AudioVoice voice = Audio::Play(Of(container));
	ASSERT_NE(PlaybackOf(NewestTagged(voice, 0)->voice), nullptr);
	const AudioTime introStart = PlaybackOf(NewestTagged(voice, 0)->voice)->GetChildren()[0].start;
	ASSERT_TRUE(RunUntil([&]() { return NewestTagged(voice, 1).has_value(); }));
	const AudioTime introEnd{introStart.frames + OutputFramesOf48k(4800)};
	EXPECT_GE(NewestTagged(voice, 1)->start, introEnd);
	EXPECT_LT(NewestTagged(voice, 1)->start.frames, introEnd.frames + 2 * Lead());
}

TEST_F(ContainerFixture, InteractiveSeekMovesClock) {
	AudioClip* a = MakeClip(96000, 1, 0.1f);
	AudioInteractiveContainer* container = MakeInteractive(960.0f, {{"A", a}});
	const AudioVoice voice = Audio::Play(Of(container));
	const AudioVoice first = NewestTagged(voice, 0)->voice;
	RunFrame();
	Audio::SetTimeSamples(voice, 1500);
	const AudioContainerChild sought = *NewestTagged(voice, 0);
	ASSERT_NE(sought.voice, first);
	EXPECT_EQ(sought.startSample, 1500u);
	EXPECT_EQ(Audio::GetMusicClock(voice).origin.frames, sought.start.frames - OutputFramesOf48k(1500));
}

TEST_F(ContainerFixture, AnUnknownClipNameChangesNothing) {
	AudioClip* a = MakeClip(96000, 1, 0.1f);
	AudioClip* b = MakeClip(96000, 1, 0.2f);
	AudioInteractiveContainer* container = MakeInteractive(960.0f, {{"A", a}, {"B", b}});
	const AudioVoice voice = Audio::Play(Of(container));
	Audio::SwitchToClip(voice, "Nowhere");
	for (u32 frame = 0; frame < 20; ++frame) {
		RunFrame();
		WaitForBlocks(1);
	}
	EXPECT_EQ(PlaybackOf(voice)->GetStartedCount(), 1u);
	EXPECT_TRUE(Audio::IsPlaying(NewestTagged(voice, 0)->voice));
}

TEST_F(ContainerFixture, ClipTempoOverridesPitchSpeedsBeats) {
	AudioClip* a = MakeClip(96000, 1, 0.1f);
	AudioClip* b = MakeClip(96000, 1, 0.2f);
	AudioInteractiveContainer* container = MakeInteractive(120.0f, {{"A", a}, {"B", b}});
	container->clips[1].beatsPerMinute = 90.0f;
	container->clips[1].beatsPerBar = 3;
	EXPECT_DOUBLE_EQ(Audio::GetMusicClock(Audio::Play(Of(container))).beatsPerMinute, 120.0);

	container->initialClip = "B";
	AudioPlayParams params;
	params.pitch = 2.0f;
	const AudioMusicClock clock = Audio::GetMusicClock(Audio::Play(Of(container), params));
	EXPECT_DOUBLE_EQ(clock.beatsPerMinute, 180.0);
	EXPECT_EQ(clock.beatsPerBar, 3u);
}

TEST_F(ContainerFixture, MutualContainersNotPlayed) {
	AudioClip* clip = MakeClip(480, 1, 0.1f);
	AudioLayeredContainer* layered = MakeLayered({clip});
	AudioInteractiveContainer* interactive = MakeInteractive(120.0f, {{"A", clip}, {"L", layered}});
	AudioLayeredContainerLayer back;
	back.resource = Of(interactive);
	layered->layers.push_back(back);
	EXPECT_FALSE(Audio::Play(Of(layered)).IsValid());
	EXPECT_FALSE(Audio::Play(Of(interactive)).IsValid());
	EXPECT_EQ(ClaimedVoices(), 0u);
}

TEST_F(ContainerFixture, SourcesVoiceIsContainersAndCarriesClock) {
	AudioClip* a = MakeClip(96000, 1, 0.1f);
	AudioInteractiveContainer* container = MakeInteractive(150.0f, {{"A", a}});
	Handle<Scene> scene = SceneSystem::CreateScene("music");
	Handle<AudioSource> source = SceneSystem::CreateObject("source", scene)->AddComponent<AudioSource>();
	source->SetPlayOnAwake(false);
	source->SetResource(Of(container));
	source->Play();
	const AudioMusicClock clock = Audio::GetMusicClock(source->GetVoice());
	EXPECT_TRUE(clock.HasTempo());
	EXPECT_DOUBLE_EQ(clock.beatsPerMinute, 150.0);
	EXPECT_EQ(clock.beatsPerBar, 4u);
	EXPECT_FALSE(Audio::GetMusicClock(AudioVoice{}).HasTempo());
}

TEST_F(ContainerFixture, AutomaticStepsWithoutGame) {
	AudioClip* clip = MakeClip(96000, 1, 0.1f);
	AudioRandomContainer* container = MakeContainer({clip});
	container->triggerMode = AudioRandomContainerTriggerMode::Automatic;
	container->automaticTriggerMode = AudioRandomContainerAutomaticTriggerMode::Pulse;
	container->automaticTriggerTime = 0.05f;

	AudioPlayParams params;
	params.output = GetDeviceOutput();
	ASSERT_TRUE(params.output.IsValid());
	const AudioVoice outside = Audio::Play(Of(container), params);
	const AudioVoice game = Audio::Play(Of(container));
	ASSERT_NE(PlaybackOf(outside), nullptr);
	ASSERT_NE(PlaybackOf(game), nullptr);

	std::set<u64> outsideStarts;
	std::set<u64> gameStarts;
	ASSERT_TRUE(WaitFor([&]() {
		RunFrame(1.0f, false);
		Gather(outside, outsideStarts);
		Gather(game, gameStarts);
		return outsideStarts.size() >= 4;
	}));
	EXPECT_LE(gameStarts.size(), 2u);
	const std::vector<u64> ordered(outsideStarts.begin(), outsideStarts.end());
	for (size_t index = 1; index < ordered.size(); ++index) {
		EXPECT_EQ(ordered[index] - ordered[index - 1], FramesOf(0.05)) << "start " << index;
	}

	StopGameVoices();
	RunFrame(1.0f, false);
	EXPECT_TRUE(Audio::IsPlaying(outside));
	EXPECT_FALSE(Audio::IsPlaying(game));
}

#endif
