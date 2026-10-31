#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioLiveFixture.h"
#include "AudioSettings.h"

#include "Shalltear/Audio/Audio.h"
#include "Shalltear/Audio/AudioEffect.h"

#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Module/ModuleEvents.h"

#include <atomic>
#include <cmath>

using namespace AudioTesting;

namespace {

	constexpr f32 CENTRE = 0.70710678f;

	f32 Distorted(f32 level) { return 3.0f * level / (1.0f + 2.0f * level); }

	std::atomic<u32> _made{0};
	std::atomic<u32> _alive{0};
	std::atomic<u32> _lastChannels{0};

	class CountedGain final : public IAudioEffect {
	public:
		explicit CountedGain(f32 gain) : _gain(gain) {
			_made.fetch_add(1);
			_alive.fetch_add(1);
		}

		void Process(AudioBlock& block, const AudioBlock* sidechain) [[clang::nonblocking]] override {
			(void)sidechain;
			for (u32 channel = 0; channel < block.channels; ++channel) {
				for (u32 frame = 0; frame < block.frames; ++frame) {
					block.data[static_cast<size_t>(channel) * block.stride + frame] *= _gain;
				}
			}
		}

		void SetParameter(u32 index, f32 value) [[clang::nonblocking]] override {
			if (index == 0) {
				_gain = value;
			}
		}

		void Release() override {
			_alive.fetch_sub(1);
			delete this;
		}
	private:
		~CountedGain() = default;
		f32 _gain;
	};

	class CountedGainDefinition final : public IAudioEffectDefinition {
	public:
		static constexpr AudioParameterDefinition PARAMETERS[] = {{"Gain", "x", 0.0f, 4.0f, 1.0f}};

		std::span<const AudioParameterDefinition> GetParameters() const override { return PARAMETERS; }

		IAudioEffect* CreateEffect(const AudioEffectContext& context) override {
			_lastChannels.store(context.channels);
			return new CountedGain(context.parameters.empty() ? 1.0f : context.parameters[0]);
		}
	};

	class VoiceEffectFixture : public LivePackageFixture {
	protected:
		void SetUp() override {
			LivePackageFixture::SetUp();
			GetAudioSystem().SetListenerPresent(true);
			_made.store(0);
			_alive.store(0);
		}

		AudioVoice PlayLevel(f32 level, const AudioSpatialSettings& spatial = {}, i32 priority = 128) {
			AudioPlayParams params;
			params.loop = true;
			params.spatial = spatial;
			params.priority = priority;
			return Audio::Play(ResourceOf(MakeClip(9600, 1, level)), params);
		}

		static bool PeakNear(f32 expected) {
			return WaitFor([expected]() { return std::fabs(LastBlockPeak() - expected) < 1e-3f; });
		}
	};

	class VoiceEffectBudgetFixture : public VoiceEffectFixture {
	protected:
		str ProjectSettings() const override { return str("[Audio]\ndevice = None\nmaxVirtualVoices = 8\nmaxRealVoices = 1\n"); }
	};

} // namespace

TEST_F(VoiceEffectFixture, VoicesEffectsShapeSoundInOrderGiven) {
	const f32 distortion[] = {0.5f};
	const f32 compressor[] = {0.0f, 50.0f, 50.0f, 6.0206f};
	const AudioVoice voice = PlayLevel(0.25f);
	EXPECT_TRUE(PeakNear(0.25f * CENTRE));

	const AudioVoiceEffect distortedFirst[] = {{"Distortion", distortion}, {"Compressor", compressor}};
	Audio::SetEffects(voice, distortedFirst);
	EXPECT_TRUE(PeakNear(2.0f * Distorted(0.25f) * CENTRE)) << LastBlockPeak();

	const AudioVoiceEffect liftedFirst[] = {{"Compressor", compressor}, {"Distortion", distortion}};
	Audio::SetEffects(voice, liftedFirst);
	EXPECT_TRUE(PeakNear(Distorted(0.5f) * CENTRE)) << LastBlockPeak();

	Audio::SetEffects(voice, {});
	EXPECT_TRUE(PeakNear(0.25f * CENTRE)) << LastBlockPeak();
}

TEST_F(VoiceEffectFixture, BypassedVoiceKeepsEffects) {
	const f32 distortion[] = {0.5f};
	const AudioVoiceEffect effects[] = {{"Distortion", distortion}};
	const AudioVoice voice = PlayLevel(0.25f);
	Audio::SetEffects(voice, effects);
	EXPECT_TRUE(PeakNear(Distorted(0.25f) * CENTRE));
	Audio::SetBypassEffects(voice, true);
	EXPECT_TRUE(PeakNear(0.25f * CENTRE)) << LastBlockPeak();
	Audio::SetBypassEffects(voice, false);
	EXPECT_TRUE(PeakNear(Distorted(0.25f) * CENTRE)) << LastBlockPeak();
}

TEST_F(VoiceEffectFixture, ParameterReachesVoicesEffectClampedToRange) {
	const f32 distortion[] = {0.5f};
	const AudioVoiceEffect effects[] = {{"Distortion", distortion}};
	const AudioVoice voice = PlayLevel(0.25f);
	Audio::SetEffects(voice, effects);
	EXPECT_TRUE(PeakNear(Distorted(0.25f) * CENTRE));
	Audio::SetEffectParameter(voice, 0, 0, 0.0f);
	EXPECT_TRUE(PeakNear(0.25f * CENTRE)) << LastBlockPeak();
	Audio::SetEffectParameter(voice, 0, 0, 5.0f);
	const f32 k = 2.0f * 0.999f / (1.0f - 0.999f);
	EXPECT_TRUE(PeakNear((1.0f + k) * 0.25f / (1.0f + k * 0.25f) * CENTRE)) << LastBlockPeak();
}

TEST_F(VoiceEffectFixture, UnregisteredEffectPassesSound) {
	CountedGainDefinition definition;
	char image = 0;
	const f32 doubled[] = {2.0f};
	const AudioVoiceEffect effects[] = {{"Test Gain", doubled}};
	const AudioVoice voice = PlayLevel(0.25f);
	Audio::SetEffects(voice, effects);
	EXPECT_TRUE(PeakNear(0.25f * CENTRE));
	EXPECT_EQ(_made.load(), 0u);

	ASSERT_TRUE(AudioEffects::Register("Test Gain", &definition, &image));
	EXPECT_TRUE(PeakNear(0.5f * CENTRE)) << LastBlockPeak();
	EXPECT_EQ(_lastChannels.load(), 1u);

	ModuleEvents::ImageUnloading.Publish(EventBus::GetGlobal(), ImageUnloadingEvent{&image});
	EXPECT_EQ(_alive.load(), 0u);
	EXPECT_TRUE(PeakNear(0.25f * CENTRE));
	EXPECT_TRUE(Audio::IsPlaying(voice));

	ASSERT_TRUE(AudioEffects::Register("Test Gain", &definition, &image));
	EXPECT_TRUE(PeakNear(0.5f * CENTRE)) << LastBlockPeak();
	EXPECT_EQ(AudioEffects::UnregisterOwnedBy(&image), 1u);
	EXPECT_EQ(_alive.load(), 0u);
}

TEST_F(VoiceEffectFixture, EffectsAfterSpatializerHearIt) {
	const f32 distortion[] = {0.5f};
	const AudioVoiceEffect effects[] = {{"Distortion", distortion}};
	AudioSpatialSettings spatial;
	spatial.spatialize = true;
	const AudioVoice voice = PlayLevel(0.25f, spatial);
	Audio::SetEffects(voice, effects);
	EXPECT_TRUE(PeakNear(Distorted(0.25f * CENTRE))) << LastBlockPeak();

	spatial.spatializePostEffects = true;
	Audio::SetSpatialSettings(voice, spatial);
	EXPECT_TRUE(PeakNear(Distorted(0.25f) * CENTRE)) << LastBlockPeak();
}

TEST_F(VoiceEffectFixture, NewDeviceRebuildsEffects) {
	CountedGainDefinition definition;
	char image = 0;
	ASSERT_TRUE(AudioEffects::Register("Test Gain", &definition, &image));
	const f32 doubled[] = {2.0f};
	const AudioVoiceEffect effects[] = {{"Test Gain", doubled}};
	const AudioVoice voice = PlayLevel(0.25f);
	Audio::SetEffects(voice, effects);
	EXPECT_TRUE(PeakNear(0.5f * CENTRE));
	ASSERT_EQ(_made.load(), 1u);

	AudioSettings* settings = GetDeclaredSettings();
	ASSERT_NE(settings, nullptr);
	settings->periodMilliseconds = settings->periodMilliseconds == 20 ? 10 : 20;
	Settings::NotifyChanged(SETTINGS_CATEGORY_PATH, "periodMilliseconds");
	EXPECT_EQ(_made.load(), 2u);
	EXPECT_EQ(_alive.load(), 1u);
	EXPECT_TRUE(PeakNear(0.5f * CENTRE)) << LastBlockPeak();
	EXPECT_TRUE(Audio::IsPlaying(voice));
	EXPECT_EQ(AudioEffects::UnregisterOwnedBy(&image), 1u);
}

TEST_F(VoiceEffectBudgetFixture, VirtualVoiceRebuildsEffects) {
	CountedGainDefinition definition;
	char image = 0;
	ASSERT_TRUE(AudioEffects::Register("Test Gain", &definition, &image));
	const f32 doubled[] = {2.0f};
	const AudioVoiceEffect effects[] = {{"Test Gain", doubled}};
	const AudioVoice quiet = PlayLevel(0.1f);
	Audio::SetEffects(quiet, effects);
	ASSERT_EQ(_made.load(), 1u);

	const AudioVoice important = PlayLevel(0.2f, {}, 0);
	ASSERT_TRUE(Audio::IsVirtual(quiet));
	WaitForBlocks(4);
	RunFrame();
	EXPECT_EQ(_alive.load(), 0u);

	Audio::Stop(important);
	WaitForBlocks(4);
	RunFrame();
	ASSERT_FALSE(Audio::IsVirtual(quiet));
	EXPECT_EQ(_made.load(), 2u);
	EXPECT_EQ(_alive.load(), 1u);
	EXPECT_TRUE(PeakNear(0.2f * CENTRE)) << LastBlockPeak();

	Audio::Stop(quiet);
	WaitForBlocks(4);
	ClaimedVoices();
	EXPECT_EQ(_alive.load(), 0u);
	EXPECT_EQ(AudioEffects::UnregisterOwnedBy(&image), 1u);
}

TEST_F(VoiceEffectBudgetFixture, VirtualizeOffKeepsEffects) {
	AudioSettings* settings = GetDeclaredSettings();
	ASSERT_NE(settings, nullptr);
	settings->virtualizeEffects = false;
	CountedGainDefinition definition;
	char image = 0;
	ASSERT_TRUE(AudioEffects::Register("Test Gain", &definition, &image));
	const f32 doubled[] = {2.0f};
	const AudioVoiceEffect effects[] = {{"Test Gain", doubled}};
	const AudioVoice quiet = PlayLevel(0.1f);
	Audio::SetEffects(quiet, effects);

	const AudioVoice important = PlayLevel(0.2f, {}, 0);
	ASSERT_TRUE(Audio::IsVirtual(quiet));
	WaitForBlocks(4);
	RunFrame();
	EXPECT_EQ(_alive.load(), 1u);

	Audio::Stop(important);
	WaitForBlocks(4);
	RunFrame();
	ASSERT_FALSE(Audio::IsVirtual(quiet));
	EXPECT_EQ(_made.load(), 1u);
	EXPECT_TRUE(PeakNear(0.2f * CENTRE)) << LastBlockPeak();
	EXPECT_EQ(AudioEffects::UnregisterOwnedBy(&image), 1u);
	settings->virtualizeEffects = true;
}

#endif
