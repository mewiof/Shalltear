#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioLiveFixture.h"

#include "AudioProfilerOps.h"
#include "AudioScenePass.h"

#include "Shalltear/Audio/AudioListener.h"
#include "Shalltear/Audio/AudioSource.h"

#include "Shalltear/Core/Asset/AssetCues.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"

#include <cmath>

using namespace AudioTesting;

namespace {

	class AudioSourceFixture : public LivePackageFixture {
	protected:
		Handle<Scene> _scene;

		void SetUp() override {
			LivePackageFixture::SetUp();
			_scene = SceneSystem::CreateScene("audio");
			ASSERT_TRUE(_scene.IsValid());
		}

		Handle<AudioSource> AddSource(AudioClip* clip, Handle<Scene> scene = {}) {
			Handle<SceneObject> object = SceneSystem::CreateObject("source", scene.IsValid() ? scene : _scene);
			Handle<AudioSource> source = object->AddComponent<AudioSource>();
			source->SetResource(ResourceOf(clip));
			return source;
		}

		Handle<AudioListener> AddListener(Handle<Scene> scene = {}) {
			Handle<SceneObject> object = SceneSystem::CreateObject("listener", scene.IsValid() ? scene : _scene);
			return object->AddComponent<AudioListener>();
		}
	};

	f32 CentredPeak(f32 value) { return value * std::sqrt(0.5f); }

} // namespace

TEST_F(AudioSourceFixture, PlayOnAwakeWaitsAndPlaysOnce) {
	AudioClip* clip = MakeClip(96000, 1, 0.1f);
	Handle<AudioSource> source = AddSource(clip);
	EXPECT_FALSE(source->IsPlaying());
	EXPECT_EQ(ClaimedVoices(), 0u);

	RunFrame();
	EXPECT_TRUE(source->IsPlaying());
	RunFrame();
	RunFrame();
	EXPECT_EQ(ClaimedVoices(), 1u);
}

TEST_F(AudioSourceFixture, PlayOnAwakeOffWaitsForPlay) {
	AudioClip* clip = MakeClip(96000, 1, 0.1f);
	Handle<AudioSource> source = AddSource(clip);
	source->SetPlayOnAwake(false);
	RunFrame();
	EXPECT_FALSE(source->IsPlaying());

	source->Play();
	EXPECT_TRUE(source->IsPlaying());
}

TEST_F(AudioSourceFixture, IsolatedSceneStaysSilent) {
	Handle<Scene> isolated = SceneSystem::CreateIsolatedScene("preview");
	ASSERT_TRUE(isolated.IsValid());
	AudioClip* clip = MakeClip(96000, 1, 0.1f);
	Handle<AudioSource> source = AddSource(clip, isolated);
	AddListener(isolated);
	EXPECT_EQ(GetRegisteredSourceCount(), 0u);
	EXPECT_EQ(GetRegisteredListenerCount(), 0u);
	EXPECT_FALSE(GetAudioSystem().IsListenerPresent());

	RunFrame();
	EXPECT_FALSE(source->IsPlaying());

	Handle<AudioSource> world = AddSource(clip);
	AddListener();
	EXPECT_EQ(GetRegisteredSourceCount(), 1u);
	EXPECT_TRUE(GetAudioSystem().IsListenerPresent());
	RunFrame();
	EXPECT_TRUE(world->IsPlaying());
}

TEST_F(AudioSourceFixture, OneShotFollowsSource) {
	AudioClip* music = MakeClip(96000, 1, 0.1f);
	AudioClip* shot = MakeClip(96000, 1, 0.1f);
	Handle<AudioSource> source = AddSource(music);
	source->SetPlayOnAwake(false);

	source->PlayOneShot(ResourceOf(shot));
	EXPECT_TRUE(source->IsPlaying());
	source->Play();
	EXPECT_EQ(ClaimedVoices(), 2u);

	source->Pause();
	EXPECT_FALSE(source->IsPlaying());
	RunFrame();
	EXPECT_EQ(ClaimedVoices(), 2u);

	source->UnPause();
	EXPECT_TRUE(source->IsPlaying());

	source->Stop();
	EXPECT_FALSE(source->IsPlaying());
	EXPECT_TRUE(WaitFor([]() { return ClaimedVoices() == 0; }));
}

TEST_F(AudioSourceFixture, PlayResumesPausedSource) {
	AudioClip* clip = MakeClip(480000, 1, 0.1f);
	Handle<AudioSource> source = AddSource(clip);
	RunFrame();
	ASSERT_TRUE(source->IsPlaying());
	ASSERT_TRUE(WaitFor([&]() { return source->GetTimeSamples() > 4800; }));

	source->Pause();
	u64 held = 0;
	ASSERT_TRUE(WaitFor([&]() {
		const u64 earlier = source->GetTimeSamples();
		WaitForBlocks(2);
		held = source->GetTimeSamples();
		return held == earlier;
	}));

	source->Play();
	EXPECT_TRUE(source->IsPlaying());
	EXPECT_EQ(ClaimedVoices(), 1u);
	EXPECT_GE(source->GetTimeSamples(), held);
}

TEST_F(AudioSourceFixture, UnPauseOnUnpausedDoesNothing) {
	AudioClip* clip = MakeClip(96000, 1, 0.1f);
	Handle<AudioSource> source = AddSource(clip);
	source->SetPlayOnAwake(false);
	source->UnPause();
	EXPECT_FALSE(source->IsPlaying());

	source->Play();
	source->Stop();
	source->UnPause();
	EXPECT_FALSE(source->IsPlaying());
	EXPECT_TRUE(WaitFor([]() { return ClaimedVoices() == 0; }));
}

TEST_F(AudioSourceFixture, DisableOrDestroyStopsAll) {
	AudioClip* clip = MakeClip(96000, 1, 0.1f);
	Handle<AudioSource> disabled = AddSource(clip);
	Handle<AudioSource> destroyed = AddSource(clip);
	RunFrame();
	disabled->PlayOneShot(ResourceOf(clip));
	destroyed->PlayOneShot(ResourceOf(clip));
	ASSERT_EQ(ClaimedVoices(), 4u);

	disabled->SetEnabled(false);
	EXPECT_FALSE(disabled->IsPlaying());
	SceneSystem::Destroy(Handle<Component>(destroyed.Get()));
	RunFrame();
	EXPECT_TRUE(WaitFor([]() { return ClaimedVoices() == 0; }));

	disabled->SetEnabled(true);
	RunFrame();
	EXPECT_TRUE(disabled->IsPlaying());
	EXPECT_LT(disabled->GetTimeSamples(), 48000u);
}

TEST_F(AudioSourceFixture, FieldWriteReachesVoiceNextPass) {
	AddListener();
	AudioClip* clip = MakeClip(4800, 1, 0.5f);
	Handle<AudioSource> source = AddSource(clip);
	source->SetLoop(true);
	RunFrame();
	ASSERT_TRUE(WaitFor([]() { return std::fabs(LastBlockPeak() - CentredPeak(0.5f)) < 1e-6f; })) << LastBlockPeak();

	source->SetVolume(0.5f);
	ASSERT_TRUE(WaitForBlocks(8));
	EXPECT_NEAR(LastBlockPeak(), CentredPeak(0.5f), 1e-6f);

	RunFrame();
	EXPECT_TRUE(WaitFor([]() { return std::fabs(LastBlockPeak() - CentredPeak(0.25f)) < 1e-6f; })) << LastBlockPeak();

	source->SetMute(true);
	RunFrame();
	EXPECT_TRUE(WaitFor([]() { return LastBlockPeak() == 0.0f; }));
	EXPECT_TRUE(source->IsPlaying());
}

TEST_F(AudioSourceFixture, ListenerGateFollowsEnabled) {
	EXPECT_FALSE(GetAudioSystem().IsListenerPresent());
	Handle<AudioListener> listener = AddListener();
	EXPECT_TRUE(GetAudioSystem().IsListenerPresent());

	listener->SetEnabled(false);
	EXPECT_FALSE(GetAudioSystem().IsListenerPresent());
	listener->SetEnabled(true);
	EXPECT_TRUE(GetAudioSystem().IsListenerPresent());

	Handle<AudioListener> second = AddListener();
	SceneSystem::Destroy(Handle<Component>(listener.Get()));
	RunFrame();
	EXPECT_TRUE(GetAudioSystem().IsListenerPresent());
	SceneSystem::Destroy(Handle<Component>(second.Get()));
	RunFrame();
	EXPECT_FALSE(GetAudioSystem().IsListenerPresent());
}

TEST_F(AudioSourceFixture, StoppedTimeSamplesIsPlayStart) {
	AudioClip* clip = MakeClip(96000, 1, 0.1f);
	Handle<AudioSource> source = AddSource(clip);
	source->SetPlayOnAwake(false);
	source->SetTimeSamples(24000);
	EXPECT_EQ(source->GetTimeSamples(), 24000u);
	EXPECT_NEAR(source->GetTime(), 0.5f, 1e-6f);

	source->Play();
	EXPECT_EQ(source->GetTimeSamples(), 24000u);
	EXPECT_TRUE(WaitFor([&]() { return source->GetTimeSamples() > 24000u + 256; }));

	source->Stop();
	EXPECT_EQ(source->GetTimeSamples(), 0u);
}

TEST_F(AudioSourceFixture, ADisabledSourceRefusesToPlay) {
	AudioClip* clip = MakeClip(96000, 1, 0.1f);
	Handle<AudioSource> source = AddSource(clip);
	source->SetEnabled(false);
	source->Play();
	source->PlayOneShot(ResourceOf(clip));
	EXPECT_FALSE(source->IsPlaying());
	EXPECT_EQ(ClaimedVoices(), 0u);
}

TEST_F(AudioSourceFixture, CueIsOneShotThroughSource) {
	AudioClip* clip = MakeClip(96000, 1, 0.1f);
	Handle<AudioSource> source = AddSource(clip);
	source->SetPlayOnAwake(false);
	source->SetVolume(0.8f);

	AssetCue cue;
	cue.asset = Handle<Asset>(clip);
	cue.object = source->GetOwner();
	cue.volume = 0.5f;
	EXPECT_TRUE(AssetCues::Cue(cue));
	EXPECT_TRUE(source->IsPlaying());
	Vector<AudioImpl::AudioProfilerOps::Voice> rows{GetMemoryResource(MemTag::Audio)};
	AudioImpl::AudioProfilerOps::CollectVoices(rows);
	ASSERT_EQ(rows.size(), 1u);
	EXPECT_TRUE(rows[0].oneShot);
	EXPECT_NEAR(rows[0].volume, 0.4f, 1e-5f);

	AssetCue elsewhere = cue;
	elsewhere.object = SceneSystem::CreateObject("silent", _scene);
	EXPECT_FALSE(AssetCues::Cue(elsewhere));
	EXPECT_EQ(ClaimedVoices(), 1u);
}

#endif
