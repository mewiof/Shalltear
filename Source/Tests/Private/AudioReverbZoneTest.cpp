#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioLiveFixture.h"
#include "AudioOfflineMixer.h"

#include "AudioEffectRegistry.h"
#include "AudioFilterAccess.h"
#include "AudioOutputs.h"
#include "AudioPlacement.h"
#include "AudioReverbPresets.h"
#include "AudioScenePass.h"
#include "Effects/BuiltinEffects.h"

#include "Shalltear/Audio/AudioFilters.h"
#include "Shalltear/Audio/AudioListener.h"
#include "Shalltear/Audio/AudioReverbZone.h"
#include "Shalltear/Audio/AudioSource.h"

#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Module/ModuleEvents.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"

#include <cmath>
#include <vector>

using namespace AudioTesting;

namespace {

	constexpr f32 CENTRE = 0.70710678f;

	f32 Millibels(f32 amplitude) { return 2000.0f * std::log10(amplitude); }

	f32 Amplitude(f32 millibels) { return std::pow(10.0f, millibels / 2000.0f); }

	AudioReverbZoneShape ZoneAt(const Vec3& position, AudioReverbPreset preset, f32 minDistance = 10.0f, f32 maxDistance = 15.0f) {
		AudioReverbZoneShape zone;
		zone.position = position;
		zone.minDistance = minDistance;
		zone.maxDistance = maxDistance;
		zone.values = GetReverbPreset(preset);
		return zone;
	}

} // namespace

TEST(AudioReverbZoneMathTest, ZoneWeightFallsMinToMax) {
	EXPECT_EQ(ComputeZoneWeight(5.0f, 10.0f, 15.0f), 1.0f);
	EXPECT_EQ(ComputeZoneWeight(10.0f, 10.0f, 15.0f), 1.0f);
	EXPECT_FLOAT_EQ(ComputeZoneWeight(12.5f, 10.0f, 15.0f), 0.5f);
	EXPECT_FLOAT_EQ(ComputeZoneWeight(14.0f, 10.0f, 15.0f), 0.2f);
	EXPECT_EQ(ComputeZoneWeight(15.0f, 10.0f, 15.0f), 0.0f);
	EXPECT_EQ(ComputeZoneWeight(40.0f, 10.0f, 15.0f), 0.0f);
	EXPECT_EQ(ComputeZoneWeight(11.0f, 10.0f, 10.0f), 0.0f);
	EXPECT_EQ(ComputeZoneWeight(std::nanf(""), 10.0f, 15.0f), 0.0f);
}

TEST(AudioReverbZoneMathTest, BlendNeverLouderThanZones) {
	AudioReverbValues blend;
	const AudioReverbZoneShape half[] = {ZoneAt(Vec3(12.5f, 0.0f, 0.0f), AudioReverbPreset::Cave)};
	ASSERT_TRUE(BlendReverbZones(half, Vec3{}, blend));
	const AudioReverbValues& cave = GetReverbPreset(AudioReverbPreset::Cave);
	EXPECT_NEAR(blend.room, Millibels(0.5f * Amplitude(cave.room)), 0.01f);
	EXPECT_NEAR(blend.decayTime, cave.decayTime, 1e-5f);
	EXPECT_NEAR(blend.reflections, cave.reflections, 0.01f);
	EXPECT_NEAR(blend.reverbDelay, cave.reverbDelay, 1e-6f);

	const AudioReverbZoneShape two[] = {ZoneAt(Vec3{}, AudioReverbPreset::Cave), ZoneAt(Vec3{}, AudioReverbPreset::Hangar)};
	ASSERT_TRUE(BlendReverbZones(two, Vec3{}, blend));
	const AudioReverbValues& hangar = GetReverbPreset(AudioReverbPreset::Hangar);
	EXPECT_NEAR(blend.room, cave.room, 0.01f);
	EXPECT_NEAR(blend.decayTime, (cave.decayTime + hangar.decayTime) / 2.0f, 1e-4f);
	EXPECT_NEAR(blend.roomHF, Millibels((Amplitude(cave.roomHF) + Amplitude(hangar.roomHF)) / 2.0f), 0.01f);

	const AudioReverbZoneShape away[] = {ZoneAt(Vec3(40.0f, 0.0f, 0.0f), AudioReverbPreset::Cave)};
	EXPECT_FALSE(BlendReverbZones(away, Vec3{}, blend));
	EXPECT_EQ(blend.room, -10000.0f);
	EXPECT_FALSE(BlendReverbZones({}, Vec3{}, blend));
}

TEST(AudioReverbZoneMathTest, PresetsAreI3DL2Rooms) {
	const AudioReverbValues& cave = GetReverbPreset(AudioReverbPreset::Cave);
	const f32 caveRow[] = {cave.room, cave.roomHF, cave.decayTime, cave.decayHFRatio, cave.reflections, cave.reflectionsDelay,
						   cave.reverb, cave.reverbDelay, cave.diffusion, cave.density, cave.hfReference};
	const f32 caveI3DL2[] = {-1000.0f, 0.0f, 2.91f, 1.30f, -602.0f, 0.015f, -302.0f, 0.022f, 100.0f, 100.0f, 5000.0f};
	for (size_t index = 0; index < sizeof(caveRow) / sizeof(caveRow[0]); ++index) {
		EXPECT_EQ(caveRow[index], caveI3DL2[index]) << "Cave's value " << index;
	}
	const AudioReverbValues& sewer = GetReverbPreset(AudioReverbPreset::SewerPipe);
	EXPECT_EQ(sewer.reverb, 648.0f);
	EXPECT_EQ(sewer.reverbDelay, 0.011f);
	EXPECT_EQ(sewer.density, 60.0f);
	EXPECT_EQ(GetReverbPreset(AudioReverbPreset::Underwater).roomHF, -4000.0f);
	EXPECT_EQ(GetReverbPreset(AudioReverbPreset::Psychotic).density, 6.25f);
	EXPECT_EQ(GetReverbPreset(AudioReverbPreset::Off).room, -10000.0f);
	EXPECT_EQ(GetReverbPreset(AudioReverbPreset::User).room, -10000.0f);
	for (u32 preset = 0; preset < static_cast<u32>(AudioReverbPreset::User); ++preset) {
		EXPECT_EQ(GetReverbPreset(static_cast<AudioReverbPreset>(preset)).roomLF, 0.0f);
	}
}

TEST(AudioReverbZoneMathTest, ZoneMixLinearThen10DbMore) {
	EXPECT_EQ(ComputeZoneSendLevel(0.0f), 0.0f);
	EXPECT_EQ(ComputeZoneSendLevel(0.5f), 0.5f);
	EXPECT_EQ(ComputeZoneSendLevel(1.0f), 1.0f);
	EXPECT_NEAR(ComputeZoneSendLevel(1.05f), std::pow(10.0f, 0.25f), 1e-4f);
	EXPECT_NEAR(ComputeZoneSendLevel(1.1f), std::pow(10.0f, 0.5f), 1e-4f);
	EXPECT_NEAR(ComputeZoneSendLevel(5.0f), std::pow(10.0f, 0.5f), 1e-4f);
	EXPECT_EQ(ComputeZoneSendLevel(-1.0f), 0.0f);
}

TEST(AudioVoiceSendTest, SendAddsSoundAtLevel) {
	OfflineMixer mixer;
	const OfflineBus returned = mixer.Bus(1);
	ClipBlock clip(MakePcm(std::vector<f32>(48000, 0.5f), 1, 48000));
	const auto level = [&mixer]() {
		mixer.Render(OfflineMixer::BLOCK);
		return At(mixer.Render(OfflineMixer::BLOCK), mixer.GetChannels(), OfflineMixer::BLOCK - 1);
	};
	AudioCommand play = mixer.Play(0, clip.Get());
	play.loop = true;
	play.other = returned.slot;
	play.otherGeneration = returned.generation;
	play.value = 0.25f;
	ASSERT_TRUE(mixer.Push(play));
	EXPECT_NEAR(level(), 0.5f * CENTRE * 1.25f, 1e-5f);

	AudioCommand send = OfflineMixer::To(play, AudioCommandKind::SetVoiceSend, 0.0f);
	send.bus = returned.slot;
	send.busGeneration = returned.generation;
	ASSERT_TRUE(mixer.Push(send));
	EXPECT_NEAR(level(), 0.5f * CENTRE, 1e-5f);

	send.value = 1.0f;
	ASSERT_TRUE(mixer.Push(send));
	EXPECT_NEAR(level(), 0.5f * CENTRE * 2.0f, 1e-5f);
	ASSERT_TRUE(mixer.Push(OfflineMixer::BusCommand(AudioCommandKind::DestroyBus, returned)));
	EXPECT_NEAR(level(), 0.5f * CENTRE, 1e-5f);
}

namespace {

	class ReverbZoneFixture : public LivePackageFixture {
	protected:
		Handle<Scene> _scene;
		Handle<SceneObject> _hearer;
		Handle<AudioListener> _listener;
		Handle<SceneObject> _object;
		Handle<AudioSource> _source;

		void SetUp() override {
			LivePackageFixture::SetUp();
			_scene = SceneSystem::CreateScene("audio");
			ASSERT_TRUE(_scene.IsValid());
			_hearer = SceneSystem::CreateObject("listener", _scene);
			_listener = _hearer->AddComponent<AudioListener>();
			_object = SceneSystem::CreateObject("source", _scene);
			_source = _object->AddComponent<AudioSource>();
			_source->SetPlayOnAwake(false);
		}

		Handle<AudioReverbZone> AddZone(const Vec3& position) {
			Handle<SceneObject> object = SceneSystem::CreateObject("zone", _scene);
			object->SetWorldPosition(position);
			return object->AddComponent<AudioReverbZone>();
		}

		template <typename Play, typename Ended>
		f32 TailAfter(Play&& play, Ended&& ended) {
			RunFrame();
			play(ResourceOf(MakeClip(4800, 1, 0.5f)));
			EXPECT_TRUE(WaitFor(ended));
			WaitForBlocks(2);
			f32 loudest = 0.0f;
			for (u32 block = 0; block < 8; ++block) {
				WaitForBlocks(1);
				loudest = std::max(loudest, LastBlockPeak());
			}
			return loudest;
		}

		f32 TailAfter() {
			return TailAfter(
				[this](Handle<AudioResource> clip) {
					_source->SetResource(clip);
					_source->Play();
				},
				[this]() { return !_source->IsPlaying(); });
		}

		static const AudioVoiceRecord* TheVoice() {
			GetAudioSystem().DrainEvents();
			AudioVoiceTable& voices = GetAudioSystem().GetVoices();
			const AudioVoiceRecord* found = nullptr;
			for (u32 slot = 0; slot < voices.GetCapacity(); ++slot) {
				const AudioVoiceRecord* record = voices.Find(slot, voices.GetGeneration(slot));
				if (record && record->claim != VoiceClaim::Free) {
					if (found) {
						return nullptr;
					}
					found = record;
				}
			}
			return found;
		}

		static constexpr f32 TAIL = 1e-4f;
	};

} // namespace

TEST_F(ReverbZoneFixture, EveryPresetIsInsideSfxReverbsRanges) {
	IAudioEffectDefinition* reverb = FindEffect("SFX Reverb");
	ASSERT_NE(reverb, nullptr);
	const std::span<const AudioParameterDefinition> table = reverb->GetParameters();
	ASSERT_EQ(table.size(), SFX_REVERB_PARAMETERS);
	for (u32 preset = 0; preset < static_cast<u32>(AudioReverbPreset::User); ++preset) {
		f32 values[SFX_REVERB_PARAMETERS];
		WriteSfxReverb(GetReverbPreset(static_cast<AudioReverbPreset>(preset)), 0.0f, values);
		for (u32 index = 0; index < SFX_REVERB_PARAMETERS; ++index) {
			if (static_cast<AudioReverbPreset>(preset) == AudioReverbPreset::City && table[index].name == "Reverb") {
				EXPECT_EQ(values[index], 2217.0f);
				continue;
			}
			EXPECT_GE(values[index], table[index].minimum) << "preset " << preset << "'s " << table[index].name;
			EXPECT_LE(values[index], table[index].maximum) << "preset " << preset << "'s " << table[index].name;
		}
	}
}

TEST_F(ReverbZoneFixture, FilterUsesPresetUnlessUser) {
	Handle<AudioReverbFilter> filter = _object->AddComponent<AudioReverbFilter>();
	filter->SetDryLevel(-500.0f);
	f32 values[MAX_FILTER_PARAMETERS]{};
	u32 count = 0;
	AudioFilterAccess::Describe(*filter, values, count);
	EXPECT_EQ(values[0], -500.0f);
	filter->SetReverbPreset(AudioReverbPreset::Cave);
	AudioFilterAccess::Describe(*filter, values, count);
	f32 cave[SFX_REVERB_PARAMETERS];
	WriteSfxReverb(GetReverbPreset(AudioReverbPreset::Cave), 0.0f, cave);
	ASSERT_EQ(count, SFX_REVERB_PARAMETERS);
	for (u32 index = 0; index < SFX_REVERB_PARAMETERS; ++index) {
		EXPECT_EQ(values[index], cave[index]) << "parameter " << index;
	}
}

TEST_F(ReverbZoneFixture, ZoneSoundRingsAfterEnd) {
	EXPECT_EQ(TailAfter(), 0.0f);
	AddZone(Vec3{});
	EXPECT_GT(TailAfter(), TAIL);
}

TEST_F(ReverbZoneFixture, BypassAndNoneSendNothing) {
	AddZone(Vec3{});
	_source->SetBypassReverbZones(true);
	EXPECT_EQ(TailAfter(), 0.0f);
	_source->SetBypassReverbZones(false);
	_source->SetReverbZoneMix(0.0f);
	EXPECT_EQ(TailAfter(), 0.0f);
	_source->SetReverbZoneMix(1.0f);
	EXPECT_GT(TailAfter(), TAIL);
}

TEST_F(ReverbZoneFixture, FieldWrittenWhileSoundPlaysMovesSend) {
	AddZone(Vec3{});
	RunFrame();
	_source->SetResource(ResourceOf(MakeClip(9600, 1, 0.25f)));
	_source->SetLoop(true);
	_source->Play();
	RunFrame();
	ASSERT_NE(TheVoice(), nullptr);
	EXPECT_NE(TheVoice()->sendGeneration, 0u);
	EXPECT_EQ(TheVoice()->sendLevel, 1.0f);
	_source->SetBypassReverbZones(true);
	RunFrame();
	EXPECT_EQ(TheVoice()->sendGeneration, 0u);
	_source->SetBypassReverbZones(false);
	_source->SetReverbZoneMix(1.1f);
	RunFrame();
	EXPECT_NEAR(TheVoice()->sendLevel, std::pow(10.0f, 0.5f), 1e-4f);
}

TEST_F(ReverbZoneFixture, ListenerOutsideEveryZoneHearsNoReverb) {
	AddZone(Vec3{});
	_hearer->SetWorldPosition(Vec3(20.0f, 0.0f, 0.0f));
	EXPECT_EQ(TailAfter(), 0.0f);
	_source->SetResource(ResourceOf(MakeClip(9600, 1, 0.25f)));
	_source->SetLoop(true);
	_source->Play();
	RunFrame();
	ASSERT_NE(TheVoice(), nullptr);
	EXPECT_EQ(TheVoice()->sendGeneration, 0u);
	_source->Stop();
	_source->SetLoop(false);
	_hearer->SetWorldPosition(Vec3(12.5f, 0.0f, 0.0f));
	EXPECT_GT(TailAfter(), TAIL);
}

TEST_F(ReverbZoneFixture, EachListenerHearsTheZonesAroundItself) {
	AddZone(Vec3{});
	Handle<SceneObject> far = SceneSystem::CreateObject("far listener", _scene);
	far->SetWorldPosition(Vec3(100.0f, 0.0f, 0.0f));
	far->AddComponent<AudioListener>();
	_source->SetSpatialBlend(1.0f);
	_object->SetWorldPosition(Vec3(100.0f, 0.0f, 1.0f));
	EXPECT_EQ(TailAfter(), 0.0f);
	_object->SetWorldPosition(Vec3(0.0f, 0.0f, 1.0f));
	EXPECT_GT(TailAfter(), TAIL);
}

TEST_F(ReverbZoneFixture, SoundIntoGroupSendsToListenersZones) {
	AddZone(Vec3{});
	const AudioBus group = GetAudioSystem().CreateBus(AudioBusDesc{});
	ASSERT_TRUE(group.IsValid());
	AudioVoice voice;
	const f32 tail = TailAfter(
		[group, &voice](Handle<AudioResource> clip) {
			AudioPlayParams params;
			params.output = group;
			voice = Audio::Play(clip, params);
		},
		[&voice]() { return !Audio::IsPlaying(voice); });
	EXPECT_GT(tail, TAIL);
}

TEST_F(ReverbZoneFixture, DeviceOutputSendsToNoZone) {
	AddZone(Vec3{});
	const AudioBus output = GetDeviceOutput();
	ASSERT_TRUE(output.IsValid());
	AudioVoice voice;
	const f32 tail = TailAfter(
		[output, &voice](Handle<AudioResource> clip) {
			AudioPlayParams params;
			params.output = output;
			voice = Audio::Play(clip, params);
			const AudioVoiceRecord* record = TheVoice();
			EXPECT_NE(record, nullptr);
			EXPECT_EQ(record ? record->sendLevel : -1.0f, 0.0f);
		},
		[&voice]() {
			RunFrame();
			return !Audio::IsPlaying(voice);
		});
	EXPECT_LE(tail, TAIL);
}

TEST_F(ReverbZoneFixture, UserZoneIsHeardByFields) {
	Handle<AudioReverbZone> zone = AddZone(Vec3{});
	zone->SetReverbPreset(AudioReverbPreset::User);
	zone->SetRoom(-10000.0f);
	EXPECT_EQ(TailAfter(), 0.0f);
	zone->SetRoom(-1000.0f);
	EXPECT_GT(TailAfter(), TAIL);
}

TEST_F(ReverbZoneFixture, ZoneReturnFollowsZones) {
	GetAudioSystem().DrainEvents();
	const u32 idle = GetAudioSystem().GetBuses().CountLive();
	Handle<AudioReverbZone> zone = AddZone(Vec3(500.0f, 0.0f, 0.0f));
	RunFrame();
	EXPECT_EQ(GetAudioSystem().GetBuses().CountLive(), idle + 1);
	zone->SetEnabled(false);
	RunFrame();
	GetAudioSystem().DrainEvents();
	EXPECT_EQ(GetAudioSystem().GetBuses().CountLive(), idle);
	zone->SetEnabled(true);
	RunFrame();
	EXPECT_EQ(GetAudioSystem().GetBuses().CountLive(), idle + 1);
	_listener->SetEnabled(false);
	GetAudioSystem().DrainEvents();
	EXPECT_EQ(GetAudioSystem().GetBuses().CountLive(), idle);
}

TEST_F(ReverbZoneFixture, UnloadRemovesZoneReverb) {
	AddZone(Vec3{});
	ASSERT_GT(TailAfter(), TAIL);
	ModuleEvents::ImageUnloading.Publish(EventBus::GetGlobal(), ImageUnloadingEvent{&_token});
	RunFrame();
	EXPECT_EQ(TailAfter(), 0.0f);
	_source->SetResource(ResourceOf(MakeClip(9600, 1, 0.25f)));
	_source->SetLoop(true);
	_source->Play();
	RunFrame();
	EXPECT_TRUE(WaitFor([]() { return std::fabs(LastBlockPeak() - 0.25f * CENTRE) < 1e-4f; })) << LastBlockPeak();
	_source->Stop();
	_source->SetLoop(false);

	RegisterBuiltinEffects(&_token);
	EXPECT_GT(TailAfter(), TAIL);
}

#endif
