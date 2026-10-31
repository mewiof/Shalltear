#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioLiveFixture.h"

#include "AudioDevice.h"
#include "AudioProfilerOps.h"

#include "Shalltear/Audio/Audio.h"
#include "Shalltear/Audio/AudioContainer.h"
#include "Shalltear/Audio/AudioListener.h"
#include "Shalltear/Audio/AudioMixer.h"
#include "Shalltear/Audio/AudioSource.h"

#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"

#include <algorithm>
#include <map>

using namespace AudioTesting;
namespace Ops = Shalltear::AudioImpl::AudioProfilerOps;

namespace {

	class AudioProfilerFixture : public LivePackageFixture {
	protected:
		Handle<Scene> _scene;
		std::vector<AudioMixer*> _mixers;
		std::vector<AudioContainer*> _containers;

		str ProjectSettings() const override { return str("[Audio]\ndevice = None\nmaxRealVoices = 2\n"); }

		void SetUp() override {
			LivePackageFixture::SetUp();
			_scene = SceneSystem::CreateScene("profiled");
			ASSERT_TRUE(_scene.IsValid());
		}

		void TearDown() override {
			for (AudioMixer* mixer : _mixers) {
				Delete(mixer);
			}
			_mixers.clear();
			LivePackageFixture::TearDown();
			for (AudioContainer* container : _containers) {
				Delete(container);
			}
			_containers.clear();
		}

		AudioClip* NamedClip(const char* name, u64 frames) {
			AudioClip* clip = MakeClip(frames, 1, 0.1f);
			clip->virtualPath = str("project://Assets/Sound/") + name + ".wav";
			return clip;
		}

		Handle<AudioSource> AddSource(const char* name, AudioClip* clip, const Vec3& position = {}) {
			Handle<SceneObject> object = SceneSystem::CreateObject(name, _scene);
			object->SetWorldPosition(position);
			Handle<AudioSource> source = object->AddComponent<AudioSource>();
			source->SetResource(ResourceOf(clip));
			return source;
		}

		void AddListener() {
			SceneSystem::CreateObject("listener", _scene)->AddComponent<AudioListener>();
		}

		static std::vector<Ops::Voice> Rows() {
			Vector<Ops::Voice> rows{GetMemoryResource(MemTag::Audio)};
			Ops::CollectVoices(rows);
			return std::vector<Ops::Voice>(rows.begin(), rows.end());
		}

		static const Ops::Voice* RowOf(const std::vector<Ops::Voice>& rows, std::string_view object, std::string_view asset) {
			for (const Ops::Voice& row : rows) {
				if (row.object == object && row.asset == asset) {
					return &row;
				}
			}
			return nullptr;
		}

		static Ops::Summary Summary() {
			Ops::Counts counts;
			Ops::ReadCounts(counts);
			Ops::Summary summary;
			Ops::Summarize(counts, counts, summary);
			return summary;
		}
	};

} // namespace

TEST_F(AudioProfilerFixture, VoiceRowNamesSourceAndClip) {
	AddListener();
	AudioClip* music = NamedClip("Music", 480000);
	AudioClip* shot = NamedClip("Shot", 96000);
	Handle<AudioSource> radio = AddSource("Radio", music, Vec3(3.0f, 4.0f, 0.0f));
	radio->SetLoop(true);
	radio->SetSpatialBlend(1.0f);
	radio->SetMinDistance(2.0f);
	radio->SetMaxDistance(40.0f);
	radio->SetPriority(64);
	Handle<AudioSource> gun = AddSource("Gun", music);
	gun->SetPlayOnAwake(false);
	gun->SetMute(true);
	RunFrame();
	gun->PlayOneShot(ResourceOf(shot));
	Audio::Play(ResourceOf(shot));
	RunFrame();

	std::vector<Ops::Voice> rows = Rows();
	ASSERT_EQ(rows.size(), 3u);
	EXPECT_TRUE(std::is_sorted(rows.begin(), rows.end(), [](const Ops::Voice& a, const Ops::Voice& b) { return a.audibility > b.audibility; }));

	const Ops::Voice* played = RowOf(rows, "Radio", "Music");
	ASSERT_NE(played, nullptr);
	EXPECT_TRUE(played->looped);
	EXPECT_FALSE(played->oneShot);
	EXPECT_TRUE(played->is3D);
	EXPECT_NEAR(played->distance, 5.0f, 1e-4f);
	EXPECT_EQ(played->minDistance, 2.0f);
	EXPECT_EQ(played->maxDistance, 40.0f);
	EXPECT_EQ(played->priority, 64);
	EXPECT_NEAR(played->duration, 10.0, 1e-9);
	EXPECT_LT(played->volume, 1.0f);
	EXPECT_GT(played->volume, 0.0f);
	EXPECT_FALSE(played->paused || played->muted || played->isVirtual || played->streamed);

	const Ops::Voice* shotRow = RowOf(rows, "Gun", "Shot");
	ASSERT_NE(shotRow, nullptr);
	EXPECT_TRUE(shotRow->oneShot);
	EXPECT_FALSE(shotRow->looped);
	EXPECT_TRUE(shotRow->muted);
	EXPECT_FALSE(shotRow->is3D);
	EXPECT_EQ(shotRow->minDistance, 0.0f);
	EXPECT_NEAR(shotRow->duration, 2.0, 1e-9);

	const Ops::Voice* loose = RowOf(rows, "", "Shot");
	ASSERT_NE(loose, nullptr);
	EXPECT_FALSE(loose->oneShot);

	radio->Pause();
	RunFrame();
	rows = Rows();
	played = RowOf(rows, "Radio", "Music");
	ASSERT_NE(played, nullptr);
	EXPECT_TRUE(played->paused);
}

TEST_F(AudioProfilerFixture, EachVoiceContainerStartsIsSourcesRow) {
	AudioClip* low = NamedClip("Low", 96000);
	AudioClip* high = NamedClip("High", 96000);
	auto* layered = New<AudioLayeredContainer>(MemTag::Asset);
	_containers.push_back(layered);
	for (AudioClip* clip : {low, high}) {
		AudioLayeredContainerLayer layer;
		layer.resource = ResourceOf(clip);
		layered->layers.push_back(layer);
	}
	Handle<SceneObject> object = SceneSystem::CreateObject("Engine", _scene);
	Handle<AudioSource> source = object->AddComponent<AudioSource>();
	source->SetResource(Handle<AudioResource>(layered));
	RunFrame();
	ASSERT_TRUE(source->IsPlaying());

	const std::vector<Ops::Voice> rows = Rows();
	EXPECT_EQ(rows.size(), 2u);
	EXPECT_NE(RowOf(rows, "Engine", "Low"), nullptr);
	EXPECT_NE(RowOf(rows, "Engine", "High"), nullptr);
}

TEST_F(AudioProfilerFixture, GroupedVoiceNamesGroupAndMixer) {
	auto* mixer = New<AudioMixer>(MemTag::Asset);
	_mixers.push_back(mixer);
	mixer->virtualPath = "project://Assets/Game.mixer";
	for (const char* id : {"Master", "Music"}) {
		AudioMixer::Group group;
		group.id = id;
		group.name = id;
		group.parent = str(id) == "Music" ? "Master" : "";
		group.units.push_back(AudioMixer::Unit{});
		mixer->groups.push_back(std::move(group));
	}
	AudioMixer::Snapshot snapshot;
	snapshot.id = "Start";
	snapshot.name = "Start";
	mixer->snapshots.push_back(std::move(snapshot));
	mixer->startSnapshot = "Start";
	ASSERT_TRUE(mixer->ApplyChanges());
	AudioPlayParams params;
	params.output = mixer->GetBus("Music");
	ASSERT_TRUE(params.output.IsValid());
	Audio::Play(ResourceOf(NamedClip("Theme", 96000)), params);
	Audio::Play(ResourceOf(NamedClip("Loose", 96000)));

	const std::vector<Ops::Voice> rows = Rows();
	const Ops::Voice* sent = RowOf(rows, "", "Theme");
	ASSERT_NE(sent, nullptr);
	EXPECT_EQ(sent->group, "Music (Game)");
	const Ops::Voice* loose = RowOf(rows, "", "Loose");
	ASSERT_NE(loose, nullptr);
	EXPECT_EQ(loose->group, "");
}

TEST_F(AudioProfilerFixture, StreamedVoiceShowsLead) {
	AudioClip* clip = MakeClip(48000 * 3, 2, 0.25f, AudioCompressionFormat::QOA, AudioClipLoadType::Streaming);
	ASSERT_EQ(clip->GetLoadType(), AudioClipLoadType::Streaming);
	clip->virtualPath = "project://Assets/Sound/Ambience.ogg";
	ASSERT_TRUE(Audio::Play(ResourceOf(clip)).IsValid());
	Audio::Play(ResourceOf(NamedClip("Resident", 96000)));

	u64 ahead = 0;
	EXPECT_TRUE(WaitFor([&ahead]() {
		const std::vector<Ops::Voice> rows = Rows();
		const Ops::Voice* row = RowOf(rows, "", "Ambience");
		ahead = row && row->streamed ? row->streamedAhead : 0;
		return ahead > 0;
	}));
	const std::vector<Ops::Voice> rows = Rows();
	const Ops::Voice* resident = RowOf(rows, "", "Resident");
	ASSERT_NE(resident, nullptr);
	EXPECT_FALSE(resident->streamed);
	EXPECT_EQ(resident->streamedAhead, 0u);
	EXPECT_EQ(Summary().streams, 1u);
}

TEST_F(AudioProfilerFixture, SummaryCountsSourcesAndVoices) {
	AudioClip* clip = NamedClip("Loop", 480000);
	Handle<AudioSource> playing = AddSource("Playing", clip);
	Handle<AudioSource> paused = AddSource("Paused", clip);
	Handle<AudioSource> idle = AddSource("Idle", clip);
	idle->SetPlayOnAwake(false);
	RunFrame();
	paused->Pause();
	for (int count = 0; count < 3; ++count) {
		AudioPlayParams quiet;
		quiet.priority = 250;
		Audio::Play(ResourceOf(clip), quiet);
	}
	RunFrame();

	const Ops::Summary summary = Summary();
	EXPECT_EQ(summary.sources, 3u);
	EXPECT_EQ(summary.playingSources, 1u);
	EXPECT_EQ(summary.pausedSources, 1u);
	EXPECT_EQ(summary.voices, 5u);
	EXPECT_EQ(summary.maxRealVoices, 2u);
	EXPECT_LE(summary.realVoices, 2u);
	EXPECT_EQ(summary.realVoices + summary.virtualVoices, summary.voices);
	EXPECT_GE(summary.virtualVoices, 3u);
	EXPECT_EQ(summary.sampleRate, GetAudioSystem().GetRenderer().GetSampleRate());
	EXPECT_GT(summary.memoryBytes, 0u);
}

TEST_F(AudioProfilerFixture, VoiceCountedOnceAsPlay) {
	AudioClip* a = NamedClip("A", 96000);
	AudioClip* b = NamedClip("B", 96000);
	std::map<u64, u32> plays;
	const auto count = [&plays](u64 id) { ++plays[id]; };

	Audio::Play(ResourceOf(a));
	Audio::Play(ResourceOf(a));
	const AudioVoice stopped = Audio::Play(ResourceOf(b));
	Audio::Stop(stopped);
	u64 last = Ops::CountPlays(0, count);
	EXPECT_EQ(plays[a->GetInstanceId()], 2u);
	EXPECT_EQ(plays[b->GetInstanceId()], 1u);

	last = Ops::CountPlays(last, count);
	last = Ops::CountPlays(last, count);
	EXPECT_EQ(plays[a->GetInstanceId()], 2u);

	Audio::Play(ResourceOf(a));
	Ops::CountPlays(last, count);
	EXPECT_EQ(plays[a->GetInstanceId()], 3u);
	EXPECT_EQ(plays[b->GetInstanceId()], 1u);
}

TEST_F(AudioProfilerFixture, OpensStartIsNotRestart) {
	ASSERT_TRUE(WaitFor([]() { return GetAudioSystem().GetDevice().GetEvents().started > 0; }));
	Ops::Counts counts;
	Ops::ReadCounts(counts);
	EXPECT_EQ(counts.restarts, 0u);
	EXPECT_EQ(counts.stops, 0u);
}

#endif
