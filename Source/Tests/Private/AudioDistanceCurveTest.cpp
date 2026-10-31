#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioLiveFixture.h"

#include "AudioPlacement.h"
#include "AudioVoiceEffects.h"

#include "Shalltear/Audio/AudioContainer.h"
#include "Shalltear/Audio/AudioFilters.h"
#include "Shalltear/Audio/AudioListener.h"
#include "Shalltear/Audio/AudioReverbZone.h"
#include "Shalltear/Audio/AudioSource.h"

#include "Shalltear/Core/Math/AnimationCurve.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"

#include <vector>

using namespace AudioTesting;

namespace {

	Keyframe Key(f32 time, f32 value, f32 inTangent = 0.0f, f32 outTangent = 0.0f) {
		Keyframe key;
		key.time = time;
		key.value = value;
		key.inTangent = inTangent;
		key.outTangent = outTangent;
		return key;
	}

	AnimationCurve Line(f32 from, f32 to) {
		const f32 slope = to - from;
		return AnimationCurve{Key(0.0f, from, slope, slope), Key(1.0f, to, slope, slope)};
	}

	std::vector<AudioListenerPose> OneListenerAtTheOrigin() {
		std::vector<AudioListenerPose> listeners(1);
		listeners[0].rotation = Quat::Identity();
		return listeners;
	}

} // anonymous namespace

TEST(AudioDistanceCurveTest, DistanceIsShareOfMax) {
	EXPECT_EQ(ComputeDistanceShare(0.0f, 10.0f), 0.0f);
	EXPECT_FLOAT_EQ(ComputeDistanceShare(2.5f, 10.0f), 0.25f);
	EXPECT_EQ(ComputeDistanceShare(10.0f, 10.0f), 1.0f);
	EXPECT_EQ(ComputeDistanceShare(40.0f, 10.0f), 1.0f);
	EXPECT_EQ(ComputeDistanceShare(3.0f, 0.0f), 1.0f);
	EXPECT_EQ(ComputeDistanceShare(-1.0f, 10.0f), 0.0f);
}

TEST(AudioDistanceCurveTest, CustomRolloffIsCurveOnly) {
	constexpr AudioRolloffMode CUSTOM = AudioRolloffMode::Custom;
	const AnimationCurve curve{Key(0.0f, 1.0f), Key(1.0f, 0.2f)};
	for (const f32 distance : {2.5f, 5.0f, 7.5f}) {
		EXPECT_FLOAT_EQ(ComputeDistanceAttenuation(CUSTOM, distance, 1.0f, 10.0f, 1.0f, &curve), curve.Evaluate(distance / 10.0f)) << distance;
	}
	EXPECT_FLOAT_EQ(ComputeDistanceAttenuation(CUSTOM, 2.5f, 1.0f, 10.0f, 1.0f, &curve), 0.875f);
	EXPECT_FLOAT_EQ(ComputeDistanceAttenuation(CUSTOM, 2.5f, 4.0f, 10.0f, 3.0f, &curve), 0.875f);
	EXPECT_FLOAT_EQ(ComputeDistanceAttenuation(CUSTOM, 30.0f, 1.0f, 10.0f, 1.0f, &curve), 0.2f);
	EXPECT_EQ(ComputeDistanceAttenuation(CUSTOM, 5.0f, 1.0f, 10.0f, 1.0f, nullptr), 1.0f);
	const AnimationCurve none;
	EXPECT_EQ(ComputeDistanceAttenuation(CUSTOM, 5.0f, 1.0f, 10.0f, 1.0f, &none), 1.0f);
}

TEST(AudioDistanceCurveTest, BlendSpreadCurvesReplaceSettings) {
	AudioSpatialSettings settings;
	settings.spatialBlend = 0.0f;
	settings.spread = 30.0f;
	settings.maxDistance = 10.0f;
	AudioDistanceCurves curves;
	curves.spatialBlend = Line(0.0f, 1.0f);
	curves.spread = Line(0.0f, 1.0f);
	const std::vector<AudioListenerPose> listeners = OneListenerAtTheOrigin();

	f32 share = -1.0f;
	const AudioVoicePlacement placed = ComputePlacement(settings, Vec3(5.0f, 0.0f, 0.0f), Vec3{}, listeners, 1.0f, 1.0f, false, &curves, &share);
	EXPECT_FLOAT_EQ(share, 0.5f);
	EXPECT_FLOAT_EQ(placed.blend, 0.5f);
	EXPECT_FLOAT_EQ(placed.params.spread, 180.0f);

	const AudioVoicePlacement plain = ComputePlacement(settings, Vec3(5.0f, 0.0f, 0.0f), Vec3{}, listeners, 1.0f, 1.0f, false);
	EXPECT_EQ(plain.blend, 0.0f);
	EXPECT_EQ(plain.params.spread, 30.0f);

	const AudioVoicePlacement alone = ComputePlacement(settings, Vec3(5.0f, 0.0f, 0.0f), Vec3{}, {}, 1.0f, 1.0f, false, &curves, &share);
	EXPECT_EQ(share, 0.0f);
	EXPECT_EQ(alone.blend, 0.0f);
}

namespace {

	class AudioDistanceCurveFixture : public LivePackageFixture {
	protected:
		Handle<Scene> _scene;
		std::vector<AudioContainer*> _containers;

		void SetUp() override {
			LivePackageFixture::SetUp();
			_scene = SceneSystem::CreateScene("curves");
			ASSERT_TRUE(_scene.IsValid());
		}

		void TearDown() override {
			LivePackageFixture::TearDown();
			for (AudioContainer* container : _containers) {
				Delete(container);
			}
			_containers.clear();
		}

		Handle<SceneObject> AddListener(const Vec3& position) {
			Handle<SceneObject> object = SceneSystem::CreateObject("listener", _scene);
			object->SetWorldPosition(position);
			object->AddComponent<AudioListener>();
			return object;
		}

		Handle<AudioSource> AddSource(const Vec3& position, f32 blend = 1.0f, bool playOnAwake = true) {
			Handle<SceneObject> object = SceneSystem::CreateObject("source", _scene);
			object->SetWorldPosition(position);
			Handle<AudioSource> source = object->AddComponent<AudioSource>();
			source->SetResource(ResourceOf(MakeClip(48000, 1, 0.1f)));
			source->SetLoop(true);
			source->SetSpatialBlend(blend);
			source->SetMaxDistance(10.0f);
			source->SetPlayOnAwake(playOnAwake);
			return source;
		}

		static SceneObject* ObjectOf(Handle<AudioSource> source) { return source->GetOwner().Get(); }

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

		static f32 CutoffOf(const AudioVoiceRecord* record) {
			if (!record || !record->effects || record->effects->rows.empty() || record->effects->rows[0].values.empty()) {
				return -1.0f;
			}
			return record->effects->rows[0].values[0];
		}
	};

} // namespace

TEST_F(AudioDistanceCurveFixture, CustomRolloffAttenuates) {
	AddListener(Vec3{});
	Handle<AudioSource> source = AddSource(Vec3(5.0f, 0.0f, 0.0f));
	source->SetRolloffMode(AudioRolloffMode::Custom);
	RunFrame();
	ASSERT_NE(TheVoice(), nullptr);
	EXPECT_FLOAT_EQ(TheVoice()->placement.gain, 0.5f);

	source->SetCustomCurve(AudioSourceCurveType::CustomRolloff, AnimationCurve{Key(0.0f, 1.0f), Key(1.0f, 0.2f)});
	ObjectOf(source)->SetWorldPosition(Vec3(2.5f, 0.0f, 0.0f));
	RunFrame();
	EXPECT_FLOAT_EQ(TheVoice()->placement.gain, 0.875f);

	source->SetRolloffMode(AudioRolloffMode::Logarithmic);
	RunFrame();
	EXPECT_FLOAT_EQ(TheVoice()->placement.gain, 0.4f);
	EXPECT_EQ(TheVoice()->curves, nullptr);
}

TEST_F(AudioDistanceCurveFixture, BlendCurveFollowsDistance) {
	AddListener(Vec3{});
	Handle<AudioSource> source = AddSource(Vec3(5.0f, 0.0f, 0.0f), 0.0f);
	source->SetCustomCurve(AudioSourceCurveType::SpatialBlend, Line(0.0f, 1.0f));
	RunFrame();
	ASSERT_NE(TheVoice(), nullptr);
	EXPECT_NE(TheVoice()->spatializerState, nullptr);
	EXPECT_FLOAT_EQ(TheVoice()->placement.blend, 0.5f);

	ObjectOf(source)->SetWorldPosition(Vec3(10.0f, 0.0f, 0.0f));
	RunFrame();
	EXPECT_FLOAT_EQ(TheVoice()->placement.blend, 1.0f);
	ObjectOf(source)->SetWorldPosition(Vec3(25.0f, 0.0f, 0.0f));
	RunFrame();
	EXPECT_FLOAT_EQ(TheVoice()->placement.blend, 1.0f);

	source->SetSpatialBlend(0.25f);
	RunFrame();
	EXPECT_FLOAT_EQ(TheVoice()->placement.blend, 0.25f);
}

TEST_F(AudioDistanceCurveFixture, SpreadCurveSharesOf360) {
	AddListener(Vec3{});
	Handle<AudioSource> source = AddSource(Vec3(2.5f, 0.0f, 0.0f));
	source->SetCustomCurve(AudioSourceCurveType::Spread, Line(0.0f, 1.0f));
	RunFrame();
	ASSERT_NE(TheVoice(), nullptr);
	EXPECT_FLOAT_EQ(TheVoice()->placement.params.spread, 90.0f);
}

TEST_F(AudioDistanceCurveFixture, ReverbMixCurveSendsAtDistance) {
	AddListener(Vec3{});
	SceneSystem::CreateObject("zone", _scene)->AddComponent<AudioReverbZone>();
	Handle<AudioSource> source = AddSource(Vec3(5.0f, 0.0f, 0.0f), 0.0f);
	source->SetCustomCurve(AudioSourceCurveType::ReverbZoneMix, Line(1.0f, 0.0f));
	RunFrame();
	RunFrame();
	ASSERT_NE(TheVoice(), nullptr);
	ASSERT_NE(TheVoice()->sendGeneration, 0u);
	EXPECT_FLOAT_EQ(TheVoice()->sendLevel, 0.5f);
	ObjectOf(source)->SetWorldPosition(Vec3(2.5f, 0.0f, 0.0f));
	RunFrame();
	EXPECT_FLOAT_EQ(TheVoice()->sendLevel, 0.75f);

	source->SetBypassReverbZones(true);
	RunFrame();
	EXPECT_EQ(TheVoice()->sendGeneration, 0u);
}

TEST_F(AudioDistanceCurveFixture, LowPassCurveFollowsSource) {
	AddListener(Vec3{});
	Handle<AudioSource> source = AddSource(Vec3(5.0f, 0.0f, 0.0f), 1.0f, false);
	Handle<AudioLowPassFilter> filter = ObjectOf(source)->AddComponent<AudioLowPassFilter>();
	filter->SetCustomCutoffCurve(Line(1.0f, 0.0f));
	RunFrame();
	source->Play();
	EXPECT_FLOAT_EQ(CutoffOf(TheVoice()), 11005.0f);

	ObjectOf(source)->SetWorldPosition(Vec3(7.5f, 0.0f, 0.0f));
	RunFrame();
	EXPECT_FLOAT_EQ(CutoffOf(TheVoice()), 10.0f + 21990.0f * 0.25f);

	filter->SetCutoffFrequency(3000.0f);
	RunFrame();
	EXPECT_FLOAT_EQ(CutoffOf(TheVoice()), 3000.0f);
}

TEST_F(AudioDistanceCurveFixture, VoiceApiTakesCurves) {
	AddListener(Vec3{});
	RunFrame();
	AudioDistanceCurves curves;
	curves.spatialBlend = Line(0.0f, 1.0f);
	AudioPlayParams params;
	params.loop = true;
	params.position = Vec3(5.0f, 0.0f, 0.0f);
	params.spatial.maxDistance = 10.0f;
	params.curves = &curves;
	const AudioVoice voice = Audio::Play(ResourceOf(MakeClip(48000, 1, 0.1f)), params);
	ASSERT_TRUE(voice.IsValid());
	curves.spatialBlend.keys.clear();
	RunFrame();
	ASSERT_NE(TheVoice(), nullptr);
	EXPECT_FLOAT_EQ(TheVoice()->placement.blend, 0.5f);

	Audio::SetDistanceCurves(voice, nullptr);
	RunFrame();
	EXPECT_EQ(TheVoice()->curves, nullptr);
	EXPECT_EQ(TheVoice()->placement.blend, 0.0f);
}

TEST_F(AudioDistanceCurveFixture, ContainersElementsTakeCurvesVoiceWasGiven) {
	AddListener(Vec3{});
	RunFrame();
	auto* container = New<AudioRandomContainer>(MemTag::Asset);
	_containers.push_back(container);
	AudioRandomContainerElement element;
	element.resource = ResourceOf(MakeClip(48000, 1, 0.1f));
	container->elements.push_back(element);

	AudioDistanceCurves curves;
	curves.spatialBlend = Line(0.0f, 1.0f);
	AudioPlayParams params;
	params.position = Vec3(5.0f, 0.0f, 0.0f);
	params.spatial.maxDistance = 10.0f;
	params.curves = &curves;
	const AudioVoice voice = Audio::Play(Handle<AudioResource>(container), params);
	ASSERT_TRUE(voice.IsValid());
	RunFrame();
	ASSERT_NE(TheVoice(), nullptr);
	EXPECT_FLOAT_EQ(TheVoice()->placement.blend, 0.5f);

	curves.spatialBlend = Line(1.0f, 1.0f);
	Audio::SetDistanceCurves(voice, &curves);
	RunFrame();
	EXPECT_FLOAT_EQ(TheVoice()->placement.blend, 1.0f);
}

TEST_F(AudioDistanceCurveFixture, AVoicesCurvesGoWithTheVoice) {
	AddListener(Vec3{});
	AudioClip* clip = MakeClip(2400, 1, 0.1f);
	RunFrame();

	AudioDistanceCurves curves;
	curves.spatialBlend = Line(0.0f, 1.0f);
	const AnimationCurve cutoff = Line(1.0f, 0.0f);
	const f32 parameters[] = {5000.0f, 1.0f};
	const AudioVoiceEffect lowPass{"LowPass", parameters, &cutoff, 0};
	const auto playOut = [&]() {
		AudioPlayParams params;
		params.position = Vec3(5.0f, 0.0f, 0.0f);
		params.spatial.maxDistance = 10.0f;
		params.curves = &curves;
		params.effects = {&lowPass, 1};
		const AudioVoice voice = Audio::Play(ResourceOf(clip), params);
		ASSERT_TRUE(voice.IsValid());
		ASSERT_NE(TheVoice(), nullptr);
		ASSERT_NE(TheVoice()->curves, nullptr);
		EXPECT_FLOAT_EQ(CutoffOf(TheVoice()), 11005.0f);
		ASSERT_TRUE(WaitFor([voice]() { return !Audio::IsPlaying(voice); }));
		for (u32 pass = 0; pass < 4; ++pass) {
			ASSERT_TRUE(WaitForBlocks(3));
			RunFrame();
		}
		GetAudioSystem().DrainEvents();
		EXPECT_EQ(TheVoice(), nullptr);
	};
	playOut();
	const size_t before = MemoryManager::GetAllocationCountForTag(MemTag::Audio);
	playOut();
	EXPECT_EQ(MemoryManager::GetAllocationCountForTag(MemTag::Audio), before);
}

TEST_F(AudioDistanceCurveFixture, ValueUntilCurveHas2Keys) {
	Handle<AudioSource> source = AddSource(Vec3{}, 0.3f, false);
	AnimationCurve single = source->GetCustomCurve(AudioSourceCurveType::SpatialBlend);
	ASSERT_EQ(single.keys.size(), 1u);
	EXPECT_FLOAT_EQ(single.keys[0].value, 0.3f);

	source->SetCustomCurve(AudioSourceCurveType::Spread, AnimationCurve{Key(0.0f, 0.5f)});
	EXPECT_FLOAT_EQ(source->GetSpread(), 180.0f);
	EXPECT_FLOAT_EQ(source->GetCustomCurve(AudioSourceCurveType::Spread).keys[0].value, 0.5f);

	const AnimationCurve falling = Line(1.0f, 0.0f);
	source->SetCustomCurve(AudioSourceCurveType::ReverbZoneMix, falling);
	EXPECT_TRUE(source->GetCustomCurve(AudioSourceCurveType::ReverbZoneMix) == falling);
	source->SetReverbZoneMix(0.7f);
	single = source->GetCustomCurve(AudioSourceCurveType::ReverbZoneMix);
	ASSERT_EQ(single.keys.size(), 1u);
	EXPECT_FLOAT_EQ(single.keys[0].value, 0.7f);

	source->SetCustomCurve(AudioSourceCurveType::SpatialBlend, AnimationCurve{});
	EXPECT_FLOAT_EQ(source->GetSpatialBlend(), 0.3f);

	const AnimationCurve rolloff = source->GetCustomCurve(AudioSourceCurveType::CustomRolloff);
	EXPECT_TRUE(rolloff == (AnimationCurve{Key(0.0f, 1.0f), Key(1.0f, 0.0f)}));

	Handle<AudioLowPassFilter> filter = ObjectOf(source)->AddComponent<AudioLowPassFilter>();
	EXPECT_FLOAT_EQ(filter->GetCustomCutoffCurve().keys[0].value, (5000.0f - 10.0f) / 21990.0f);
	filter->SetCustomCutoffCurve(AnimationCurve{Key(0.0f, 0.5f)});
	EXPECT_FLOAT_EQ(filter->GetCutoffFrequency(), 11005.0f);
}

#endif
