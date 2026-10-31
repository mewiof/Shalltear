#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioLiveFixture.h"

#include "AudioEffectRegistry.h"
#include "AudioFilterAccess.h"
#include "AudioScenePass.h"

#include "Shalltear/Audio/AudioEffect.h"
#include "Shalltear/Audio/AudioFilters.h"
#include "Shalltear/Audio/AudioListener.h"
#include "Shalltear/Audio/AudioSource.h"

#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"

#include <cmath>
#include <cstring>
#include <string_view>
#include <vector>

using namespace AudioTesting;

namespace {

	constexpr f32 CENTRE = 0.70710678f;

	f32 Distorted(f32 level) { return 3.0f * level / (1.0f + 2.0f * level); }

	class AudioFilterFixture : public LivePackageFixture {
	protected:
		Handle<Scene> _scene;
		Handle<SceneObject> _listener;
		Handle<SceneObject> _object;
		Handle<AudioSource> _source;

		void SetUp() override {
			LivePackageFixture::SetUp();
			_scene = SceneSystem::CreateScene("audio");
			ASSERT_TRUE(_scene.IsValid());
			_listener = SceneSystem::CreateObject("listener", _scene);
			_listener->AddComponent<AudioListener>();
			_object = SceneSystem::CreateObject("source", _scene);
			_source = _object->AddComponent<AudioSource>();
		}

		void PlayLevel(f32 level) {
			_source->SetResource(ResourceOf(MakeClip(9600, 1, level)));
			_source->SetLoop(true);
			_source->Play();
		}

		static bool HeardAt(f32 expected) {
			RunFrame();
			return WaitFor([expected]() { return std::fabs(LastBlockPeak() - expected) < 1e-3f; });
		}

		Handle<AudioEchoFilter> AddDoubler() {
			Handle<AudioEchoFilter> echo = _object->AddComponent<AudioEchoFilter>();
			echo->SetDelay(10.0f);
			echo->SetDecayRatio(0.0f);
			return echo;
		}
	};

	class NamedInPlaceFilter : public AudioFilter {
	public:
		SLT_REFLECT_COMPONENT(NamedInPlaceFilter, AudioFilter)
		SLT_END_REFLECT()

		char name[16] = "Distortion";
	private:
		std::string_view Describe(f32* values, u32& count) const override {
			values[0] = 0.5f;
			count = 1;
			return std::string_view(name);
		}
	};

	struct Described {
		std::string_view effect;
		std::vector<f32> values;
	};

	Described Describe(const AudioFilter& filter) {
		f32 values[AudioImpl::MAX_FILTER_PARAMETERS]{};
		u32 count = 0;
		const std::string_view effect = AudioImpl::AudioFilterAccess::Describe(filter, values, count);
		return Described{effect, std::vector<f32>(values, values + count)};
	}

} // namespace

TEST_F(AudioFilterFixture, FiltersMatchEffectDefaults) {
	struct Expected {
		Handle<AudioFilter> filter;
		std::string_view effect;
		std::vector<f32> values;
	};
	Handle<SceneObject> holder = SceneSystem::CreateObject("filters", _scene);
	const Expected expected[] = {
		{Handle<AudioFilter>(holder->AddComponent<AudioLowPassFilter>().Get()), "LowPass", {5000.0f, 1.0f}},
		{Handle<AudioFilter>(holder->AddComponent<AudioHighPassFilter>().Get()), "HighPass", {5000.0f, 1.0f}},
		{Handle<AudioFilter>(holder->AddComponent<AudioEchoFilter>().Get()), "Echo", {500.0f, 50.0f, 100.0f, 100.0f}},
		{Handle<AudioFilter>(holder->AddComponent<AudioDistortionFilter>().Get()), "Distortion", {0.5f}},
		{Handle<AudioFilter>(holder->AddComponent<AudioReverbFilter>().Get()),
		 "SFX Reverb",
		 {0.0f, 0.0f, 0.0f, 1.0f, 0.5f, -10000.0f, 0.0f, 0.0f, 0.04f, 100.0f, 100.0f, 5000.0f, 0.0f, 250.0f}},
		{Handle<AudioFilter>(holder->AddComponent<AudioChorusFilter>().Get()), "Chorus", {0.5f, 0.5f, 0.5f, 0.5f, 40.0f, 0.8f, 0.03f, 0.0f}},
	};
	for (const Expected& row : expected) {
		ASSERT_TRUE(row.filter.IsValid());
		const Described described = Describe(*row.filter);
		EXPECT_EQ(described.effect, row.effect);
		EXPECT_EQ(described.values, row.values) << row.effect;
		IAudioEffectDefinition* definition = AudioImpl::FindEffect(described.effect);
		ASSERT_NE(definition, nullptr) << row.effect;
		const std::span<const AudioParameterDefinition> table = definition->GetParameters();
		ASSERT_EQ(table.size(), described.values.size()) << row.effect;
		for (size_t index = 0; index < table.size(); ++index) {
			EXPECT_GE(described.values[index], table[index].minimum) << row.effect << "'s " << table[index].name;
			EXPECT_LE(described.values[index], table[index].maximum) << row.effect << "'s " << table[index].name;
		}
	}
}

TEST_F(AudioFilterFixture, EveryFilterFieldLandsOnParameter) {
	Handle<AudioReverbFilter> reverb = _object->AddComponent<AudioReverbFilter>();
	reverb->SetDryLevel(-1.0f);
	reverb->SetRoom(-2.0f);
	reverb->SetRoomHF(-3.0f);
	reverb->SetRoomLF(-4.0f);
	reverb->SetDecayTime(5.0f);
	reverb->SetDecayHFRatio(0.6f);
	reverb->SetReflectionsLevel(-7.0f);
	reverb->SetReflectionsDelay(0.08f);
	reverb->SetReverbLevel(-9.0f);
	reverb->SetReverbDelay(0.01f);
	reverb->SetHfReference(1100.0f);
	reverb->SetLfReference(120.0f);
	reverb->SetDiffusion(13.0f);
	reverb->SetDensity(14.0f);
	const std::vector<f32> reverbValues = {-1.0f, -2.0f, -3.0f, 5.0f, 0.6f, -7.0f, 0.08f, -9.0f, 0.01f, 13.0f, 14.0f, 1100.0f, -4.0f, 120.0f};
	EXPECT_EQ(Describe(*reverb).values, reverbValues);

	Handle<AudioEchoFilter> echo = _object->AddComponent<AudioEchoFilter>();
	echo->SetDelay(250.0f);
	echo->SetDecayRatio(0.25f);
	echo->SetDryMix(0.75f);
	echo->SetWetMix(0.5f);
	const std::vector<f32> echoValues = {250.0f, 25.0f, 75.0f, 50.0f};
	EXPECT_EQ(Describe(*echo).values, echoValues);

	Handle<AudioChorusFilter> chorus = _object->AddComponent<AudioChorusFilter>();
	chorus->SetDryMix(0.1f);
	chorus->SetWetMix1(0.2f);
	chorus->SetWetMix2(0.3f);
	chorus->SetWetMix3(0.4f);
	chorus->SetDelay(50.0f);
	chorus->SetRate(6.0f);
	chorus->SetDepth(0.7f);
	chorus->SetFeedback(0.8f);
	const std::vector<f32> chorusValues = {0.1f, 0.2f, 0.3f, 0.4f, 50.0f, 6.0f, 0.7f, 0.8f};
	EXPECT_EQ(Describe(*chorus).values, chorusValues);
}

TEST_F(AudioFilterFixture, FiltersApplyInOrder) {
	_object->AddComponent<AudioDistortionFilter>();
	Handle<AudioEchoFilter> echo = AddDoubler();
	PlayLevel(0.25f);
	EXPECT_TRUE(HeardAt(2.0f * Distorted(0.25f) * CENTRE)) << LastBlockPeak();

	_object->SetComponentIndex(Handle<Component>(echo.Get()), 1);
	EXPECT_TRUE(HeardAt(Distorted(0.5f) * CENTRE)) << LastBlockPeak();
}

TEST_F(AudioFilterFixture, DisabledFilterSilentFieldStays) {
	Handle<AudioDistortionFilter> distortion = _object->AddComponent<AudioDistortionFilter>();
	PlayLevel(0.25f);
	EXPECT_TRUE(HeardAt(Distorted(0.25f) * CENTRE)) << LastBlockPeak();
	distortion->SetEnabled(false);
	EXPECT_TRUE(HeardAt(0.25f * CENTRE)) << LastBlockPeak();
	distortion->SetEnabled(true);
	EXPECT_TRUE(HeardAt(Distorted(0.25f) * CENTRE)) << LastBlockPeak();
	distortion->SetDistortionLevel(0.0f);
	EXPECT_TRUE(HeardAt(0.25f * CENTRE)) << LastBlockPeak();
}

TEST_F(AudioFilterFixture, TheSourcesBypassEffectsSkipsItsFilters) {
	_object->AddComponent<AudioDistortionFilter>();
	PlayLevel(0.25f);
	EXPECT_TRUE(HeardAt(Distorted(0.25f) * CENTRE));
	_source->SetBypassEffects(true);
	EXPECT_TRUE(HeardAt(0.25f * CENTRE)) << LastBlockPeak();
	_source->SetBypassEffects(false);
	EXPECT_TRUE(HeardAt(Distorted(0.25f) * CENTRE)) << LastBlockPeak();
}

TEST_F(AudioFilterFixture, OneShotIsHeardThroughSourcesFilters) {
	_object->AddComponent<AudioDistortionFilter>();
	RunFrame();
	_source->PlayOneShot(ResourceOf(MakeClip(96000, 1, 0.25f)));
	EXPECT_TRUE(WaitFor([]() { return std::fabs(LastBlockPeak() - Distorted(0.25f) * CENTRE) < 1e-3f; })) << LastBlockPeak();
}

TEST_F(AudioFilterFixture, MidPassVoiceUsesSourceFilters) {
	_object->AddComponent<AudioDistortionFilter>();
	PlayLevel(0.25f);
	EXPECT_TRUE(HeardAt(Distorted(0.25f) * CENTRE));
	_source->Stop();
	EXPECT_TRUE(WaitFor([]() { return LastBlockPeak() == 0.0f; }));
	_source->Play();
	EXPECT_TRUE(WaitFor([]() { return std::fabs(LastBlockPeak() - Distorted(0.25f) * CENTRE) < 1e-3f; })) << LastBlockPeak();
}

TEST_F(AudioFilterFixture, SourcesSpatializePostEffectPutsFiltersFirst) {
	_object->AddComponent<AudioDistortionFilter>();
	_source->SetSpatialize(true);
	PlayLevel(0.25f);
	EXPECT_TRUE(HeardAt(Distorted(0.25f * CENTRE))) << LastBlockPeak();
	_source->SetSpatializePostEffects(true);
	EXPECT_TRUE(HeardAt(Distorted(0.25f) * CENTRE)) << LastBlockPeak();
}

TEST_F(AudioFilterFixture, SourceComparesFilterNameCopy) {
	Handle<NamedInPlaceFilter> filter = _object->AddComponent<NamedInPlaceFilter>();
	PlayLevel(0.25f);
	EXPECT_TRUE(HeardAt(Distorted(0.25f) * CENTRE)) << LastBlockPeak();
	std::memcpy(filter->name, "SFX Reverb", sizeof("SFX Reverb"));
	EXPECT_TRUE(HeardAt(0.25f * CENTRE)) << LastBlockPeak();
}

TEST_F(AudioFilterFixture, TheEnabledFilterCountFollowsEveryFilter) {
	const u32 before = AudioImpl::GetEnabledFilterCount();
	Handle<AudioDistortionFilter> distortion = _object->AddComponent<AudioDistortionFilter>();
	EXPECT_EQ(AudioImpl::GetEnabledFilterCount(), before + 1);
	distortion->SetEnabled(false);
	EXPECT_EQ(AudioImpl::GetEnabledFilterCount(), before);
	distortion->SetEnabled(true);
	EXPECT_EQ(AudioImpl::GetEnabledFilterCount(), before + 1);
	SceneSystem::Destroy(Handle<Component>(distortion.Get()));
	RunFrame();
	EXPECT_EQ(AudioImpl::GetEnabledFilterCount(), before);
}

namespace {

	class SlowEffect final : public IAudioEffectDefinition {
	public:
		explicit SlowEffect(bool silences) : _silences(silences) {}

		IAudioEffect* CreateEffect(const AudioEffectContext& context) override {
			(void)context;
			const AudioRenderCounters& counters = GetAudioSystem().GetRenderer().GetCounters();
			const u64 target = counters.blocks.load(std::memory_order_relaxed) + 2;
			const auto giveUp = std::chrono::steady_clock::now() + std::chrono::seconds(3);
			while (counters.blocks.load(std::memory_order_relaxed) < target && std::chrono::steady_clock::now() < giveUp) {
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			return new Instance(_silences);
		}
	private:
		class Instance final : public IAudioEffect {
		public:
			explicit Instance(bool silences) : _silences(silences) {}

			void Process(AudioBlock& block, const AudioBlock* sidechain) override {
				(void)sidechain;
				if (!_silences) {
					return;
				}
				for (u32 channel = 0; channel < block.channels; ++channel) {
					f32* row = block.data + static_cast<size_t>(channel) * block.stride;
					for (u32 frame = 0; frame < block.frames; ++frame) {
						row[frame] = 0.0f;
					}
				}
			}
			bool IsQuiet() const override { return true; }
			void Release() override { delete this; }
		private:
			bool _silences;
		};

		bool _silences;
	};

	constexpr std::string_view SLOW_SILENCE = "TestSlowSilence";
	constexpr std::string_view SLOW_PASS = "TestSlowPass";

	class SlowFilter : public AudioFilter {
	public:
		SLT_REFLECT_COMPONENT(SlowFilter, AudioFilter)
		SLT_END_REFLECT()

		std::string_view effect = SLOW_SILENCE;
	private:
		std::string_view Describe(f32* values, u32& count) const override {
			(void)values;
			count = 0;
			return effect;
		}
	};

	class AudioStartFixture : public AudioFilterFixture {
	protected:
		char _effectsToken = 0;
		SlowEffect _silence{true};
		SlowEffect _pass{false};

		void SetUp() override {
			AudioFilterFixture::SetUp();
			ASSERT_TRUE(AudioEffects::Register(SLOW_SILENCE, &_silence, &_effectsToken));
			ASSERT_TRUE(AudioEffects::Register(SLOW_PASS, &_pass, &_effectsToken));
			_source->SetPlayOnAwake(false);
		}

		void TearDown() override {
			AudioEffects::UnregisterOwnedBy(&_effectsToken);
			AudioFilterFixture::TearDown();
		}

		void AddSlow(std::string_view effect) { _object->AddComponent<SlowFilter>()->effect = effect; }

		void LoopLevel(f32 level) {
			_source->SetResource(ResourceOf(MakeClip(96000, 1, level)));
			_source->SetLoop(true);
		}

		static f32 Peak() { return std::bit_cast<f32>(GetAudioSystem().GetRenderer().GetCounters().peak.load(std::memory_order_relaxed)); }
	};

} // namespace

TEST_F(AudioStartFixture, MidPassVoiceFilteredFromStart) {
	AddSlow(SLOW_SILENCE);
	LoopLevel(0.25f);
	RunFrame();
	ASSERT_EQ(Peak(), 0.0f);
	_source->Play();
	ASSERT_TRUE(WaitForBlocks(4));
	EXPECT_TRUE(_source->IsPlaying());
	EXPECT_EQ(Peak(), 0.0f);
}

TEST_F(AudioStartFixture, SameFrameFilterHeardFromStart) {
	LoopLevel(0.25f);
	RunFrame();
	AddSlow(SLOW_SILENCE);
	_source->Play();
	ASSERT_TRUE(WaitForBlocks(4));
	EXPECT_EQ(Peak(), 0.0f);
}

TEST_F(AudioStartFixture, OneShotFilteredFromStart) {
	AddSlow(SLOW_SILENCE);
	RunFrame();
	_source->PlayOneShot(ResourceOf(MakeClip(96000, 1, 0.25f)));
	ASSERT_TRUE(WaitForBlocks(4));
	EXPECT_TRUE(_source->IsPlaying());
	EXPECT_EQ(Peak(), 0.0f);
}

TEST_F(AudioStartFixture, BypassListenerEffectsFromStart) {
	_listener->AddComponent<AudioDistortionFilter>();
	AddSlow(SLOW_PASS);
	LoopLevel(0.25f);
	_source->SetBypassListenerEffects(true);
	RunFrame();
	_source->Play();
	ASSERT_TRUE(WaitForBlocks(4));
	EXPECT_NEAR(Peak(), 0.25f * CENTRE, 1e-3f);
}

TEST_F(AudioStartFixture, BypassEffectsSkipsFiltersFromStart) {
	AddSlow(SLOW_SILENCE);
	LoopLevel(0.25f);
	_source->SetBypassEffects(true);
	RunFrame();
	_source->Play();
	ASSERT_TRUE(WaitForBlocks(4));
	EXPECT_NEAR(Peak(), 0.25f * CENTRE, 1e-3f);
}

TEST_F(AudioStartFixture, BypassEffectsSkipsListenerFromStart) {
	_listener->AddComponent<AudioDistortionFilter>();
	AddSlow(SLOW_PASS);
	LoopLevel(0.25f);
	_source->SetBypassEffects(true);
	RunFrame();
	_source->Play();
	ASSERT_TRUE(WaitForBlocks(4));
	EXPECT_NEAR(Peak(), 0.25f * CENTRE, 1e-3f);
}

#endif
