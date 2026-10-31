#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioLiveFixture.h"

#include "AudioPlacement.h"
#include "AudioSettings.h"

#include "Shalltear/Audio/AudioEffect.h"
#include "Shalltear/Audio/AudioFilters.h"
#include "Shalltear/Audio/AudioListener.h"
#include "Shalltear/Audio/AudioMixer.h"
#include "Shalltear/Audio/AudioSource.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Module/ModuleEvents.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"

#include <atomic>
#include <cmath>
#include <string_view>

using namespace AudioTesting;

namespace {

	constexpr f32 CENTRE = 0.70710678f;

	f32 Distorted(f32 level) { return 3.0f * level / (1.0f + 2.0f * level); }

	f32 Gain(f32 decibels) { return std::pow(10.0f, decibels / 20.0f); }

	std::atomic<u32> _made{0};
	std::atomic<u32> _alive{0};

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
			return new CountedGain(context.parameters.empty() ? 1.0f : context.parameters[0]);
		}
	};

	class ListenerGainFilter : public AudioFilter {
	public:
		SLT_REFLECT_COMPONENT(ListenerGainFilter, AudioFilter)
		SLT_END_REFLECT()

		f32 gain = 2.0f;
	private:
		std::string_view Describe(f32* values, u32& count) const override {
			values[0] = gain;
			count = 1;
			return "Listener Gain";
		}
	};

	class ListenerMixFixture : public LivePackageFixture {
	protected:
		Handle<Scene> _scene;
		Handle<SceneObject> _hearer;
		Handle<AudioListener> _listener;
		Handle<SceneObject> _object;
		Handle<AudioSource> _source;
		CountedGainDefinition _definition;
		char _image = 0;

		void SetUp() override {
			LivePackageFixture::SetUp();
			_made.store(0);
			_alive.store(0);
			_scene = SceneSystem::CreateScene("audio");
			ASSERT_TRUE(_scene.IsValid());
			_hearer = SceneSystem::CreateObject("listener", _scene);
			_listener = _hearer->AddComponent<AudioListener>();
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

		static Handle<AudioEchoFilter> AddDoubler(Handle<SceneObject> object) {
			Handle<AudioEchoFilter> echo = object->AddComponent<AudioEchoFilter>();
			echo->SetDelay(10.0f);
			echo->SetDecayRatio(0.0f);
			return echo;
		}

		Handle<AudioListener> AddListener(const char* name, const Vec3& position) {
			Handle<SceneObject> object = SceneSystem::CreateObject(name, _scene);
			object->SetWorldPosition(position);
			return object->AddComponent<AudioListener>();
		}

		static u32 LiveBuses() {
			GetAudioSystem().DrainEvents();
			return GetAudioSystem().GetBuses().CountLive();
		}
	};

	class ListenerOutputFixture : public ListenerMixFixture {
	protected:
		std::vector<Asset*> _held;

		void SetUp() override {
			ListenerMixFixture::SetUp();
			std::filesystem::create_directories(_root / "Assets");
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			for (Asset* asset : _held) {
				AssetDatabase::UnloadAsset(asset);
			}
			_held.clear();
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			ListenerMixFixture::TearDown();
		}

		Handle<AudioMixerGroup> Group(const char* id) {
			const Path path("project://Assets/Main.mixer");
			if (!FileSystem::Exists(path)) {
				FileSystem::WriteTextFile(path, str(R"({
	"groups": [ { "id": "Master" }, { "id": "Players", "parent": "Master", "clock": "Gameplay" }, { "id": "Music", "parent": "Master" } ],
	"snapshots": [ { "name": "Snapshot", "values": [ { "group": "Players", "parameter": "Volume", "value": -6 } ] } ]
})"));
				Asset* mixer = AssetDatabase::LoadAsset(path);
				EXPECT_NE(mixer, nullptr);
				_held.push_back(mixer);
			}
			Vector<AssetProduct> products{GetMemoryResource(MemTag::Asset)};
			AssetDatabase::GetProducts(AssetDatabase::LookupGuid(path), products);
			for (const AssetProduct& product : products) {
				if (product.key == str("Groups/") + id) {
					Asset* group = AssetDatabase::LoadAssetByGuid(product.guid);
					EXPECT_NE(group, nullptr);
					_held.push_back(group);
					EXPECT_TRUE(static_cast<AudioMixerGroup*>(group)->GetBus().IsValid());
					return Handle<AudioMixerGroup>(static_cast<AudioMixerGroup*>(group));
				}
			}
			ADD_FAILURE() << "no group " << id;
			return {};
		}
	};

	class ListenerOutputBudgetFixture : public ListenerOutputFixture {
	protected:
		str ProjectSettings() const override { return str("[Audio]\ndevice = None\nmaxVirtualVoices = 8\nmaxRealVoices = 1\n"); }
	};

} // namespace

TEST(AudioListenerRouteTest, SoundRoutesThroughOrPastListener) {
	AudioListenerPose first;
	first.position = Vec3(-10.0f, 0.0f, 0.0f);
	first.filters = AudioBusAccess::Make(3, 7);
	first.output = AudioBusAccess::Make(4, 1);
	AudioListenerPose second;
	second.position = Vec3(10.0f, 0.0f, 0.0f);
	second.filters = AudioBusAccess::Make(5, 2);
	const AudioListenerPose listeners[] = {first, second};

	EXPECT_EQ(ChooseListenerRoute(listeners, false, Vec3(9.0f, 0.0f, 0.0f), false), first.filters);
	EXPECT_EQ(ChooseListenerRoute(listeners, true, Vec3(9.0f, 0.0f, 0.0f), false), second.filters);
	EXPECT_EQ(ChooseListenerRoute(listeners, false, Vec3{}, true), first.output);
	EXPECT_FALSE(ChooseListenerRoute(listeners, true, Vec3(9.0f, 0.0f, 0.0f), true).IsValid());
	EXPECT_FALSE(ChooseListenerRoute({}, true, Vec3{}, false).IsValid());
	EXPECT_EQ(FindHearingListener(listeners, true, Vec3(-1.0f, 0.0f, 0.0f)), 0);
	EXPECT_EQ(FindHearingListener({}, false, Vec3{}), -1);
}

TEST_F(ListenerMixFixture, ListenerFiltersShapeGrouplessSound) {
	Handle<AudioDistortionFilter> distortion = _hearer->AddComponent<AudioDistortionFilter>();
	PlayLevel(0.25f);
	EXPECT_TRUE(HeardAt(Distorted(0.25f * CENTRE))) << LastBlockPeak();
	distortion->SetDistortionLevel(0.0f);
	EXPECT_TRUE(HeardAt(0.25f * CENTRE)) << LastBlockPeak();
}

TEST_F(ListenerMixFixture, BypassesSkipListenerFilters) {
	_hearer->AddComponent<AudioDistortionFilter>();
	PlayLevel(0.25f);
	EXPECT_TRUE(HeardAt(Distorted(0.25f * CENTRE)));
	_source->SetBypassListenerEffects(true);
	EXPECT_TRUE(HeardAt(0.25f * CENTRE)) << LastBlockPeak();
	_source->SetBypassListenerEffects(false);
	EXPECT_TRUE(HeardAt(Distorted(0.25f * CENTRE))) << LastBlockPeak();
	_source->SetBypassEffects(true);
	EXPECT_TRUE(HeardAt(0.25f * CENTRE)) << LastBlockPeak();
}

TEST_F(ListenerMixFixture, MidPassVoiceRoutedAtStart) {
	_hearer->AddComponent<AudioDistortionFilter>();
	PlayLevel(0.25f);
	EXPECT_TRUE(HeardAt(Distorted(0.25f * CENTRE)));
	const auto restartHeardAt = [this](f32 expected) {
		_source->Stop();
		EXPECT_TRUE(WaitFor([]() { return LastBlockPeak() == 0.0f; }));
		_source->Play();
		return WaitFor([expected]() { return std::fabs(LastBlockPeak() - expected) < 1e-3f; });
	};
	EXPECT_TRUE(restartHeardAt(Distorted(0.25f * CENTRE))) << LastBlockPeak();

	_source->SetBypassListenerEffects(true);
	EXPECT_TRUE(HeardAt(0.25f * CENTRE));
	EXPECT_TRUE(restartHeardAt(0.25f * CENTRE)) << LastBlockPeak();
}

TEST_F(ListenerMixFixture, Sound2DUsesFirstListener) {
	AddDoubler(_hearer);
	AddListener("second", Vec3{});
	PlayLevel(0.25f);
	EXPECT_TRUE(HeardAt(2.0f * 0.25f * CENTRE)) << LastBlockPeak();
	_listener->SetEnabled(false);
	_listener->SetEnabled(true);
	EXPECT_TRUE(HeardAt(0.25f * CENTRE)) << LastBlockPeak();
}

TEST_F(ListenerMixFixture, NearestListenerFiltersSound) {
	_hearer->SetWorldPosition(Vec3(-50.0f, 0.0f, 0.0f));
	AddDoubler(_hearer);
	AddListener("plain", Vec3(50.0f, 0.0f, 0.0f));
	_object->SetWorldPosition(Vec3(50.0f, 0.0f, 1.0f));
	_source->SetSpatialBlend(1.0f);
	_source->SetMinDistance(1000.0f);
	_source->SetDopplerLevel(0.0f);
	PlayLevel(0.25f);
	RunFrame();
	ASSERT_TRUE(WaitFor([]() { return LastBlockPeak() > 0.0f; }));
	WaitForBlocks(8);
	const f32 plain = LastBlockPeak();

	_object->SetWorldPosition(Vec3(-50.0f, 0.0f, 1.0f));
	EXPECT_TRUE(HeardAt(2.0f * plain));
	_object->SetWorldPosition(Vec3(50.0f, 0.0f, 1.0f));
	EXPECT_TRUE(HeardAt(plain)) << LastBlockPeak();
}

TEST_F(ListenerMixFixture, ListenerBusFollowsFilters) {
	const u32 idle = LiveBuses();
	PlayLevel(0.25f);
	EXPECT_TRUE(HeardAt(0.25f * CENTRE));
	EXPECT_EQ(LiveBuses(), idle);

	Handle<AudioDistortionFilter> distortion = _hearer->AddComponent<AudioDistortionFilter>();
	EXPECT_TRUE(HeardAt(Distorted(0.25f * CENTRE)));
	EXPECT_EQ(LiveBuses(), idle + 1);

	distortion->SetEnabled(false);
	EXPECT_TRUE(HeardAt(0.25f * CENTRE)) << LastBlockPeak();
	EXPECT_EQ(LiveBuses(), idle);

	distortion->SetEnabled(true);
	EXPECT_TRUE(HeardAt(Distorted(0.25f * CENTRE)));
	_listener->SetEnabled(false);
	EXPECT_EQ(LiveBuses(), idle);
}

TEST_F(ListenerMixFixture, ListenerFilterKeepsInstance) {
	ASSERT_TRUE(AudioEffects::Register("Listener Gain", &_definition, &_image));
	Handle<ListenerGainFilter> gain = _hearer->AddComponent<ListenerGainFilter>();
	Handle<AudioDistortionFilter> distortion = _hearer->AddComponent<AudioDistortionFilter>();
	PlayLevel(0.125f);
	EXPECT_TRUE(HeardAt(Distorted(2.0f * 0.125f * CENTRE)));
	ASSERT_EQ(_made.load(), 1u);

	distortion->SetEnabled(false);
	EXPECT_TRUE(HeardAt(2.0f * 0.125f * CENTRE));
	_hearer->SetComponentIndex(Handle<Component>(distortion.Get()), 1);
	distortion->SetEnabled(true);
	gain->gain = 0.5f;
	EXPECT_TRUE(HeardAt(0.5f * Distorted(0.125f * CENTRE)));
	EXPECT_EQ(_made.load(), 1u);

	gain->SetEnabled(false);
	EXPECT_TRUE(HeardAt(Distorted(0.125f * CENTRE)));
	WaitForBlocks(4);
	RunFrame();
	EXPECT_EQ(_alive.load(), 0u);
	EXPECT_EQ(AudioEffects::UnregisterOwnedBy(&_image), 1u);
}

TEST_F(ListenerMixFixture, ListenerModuleEffectFollowsImage) {
	ASSERT_TRUE(AudioEffects::Register("Listener Gain", &_definition, &_image));
	_hearer->AddComponent<ListenerGainFilter>();
	PlayLevel(0.25f);
	EXPECT_TRUE(HeardAt(2.0f * 0.25f * CENTRE));

	ModuleEvents::ImageUnloading.Publish(EventBus::GetGlobal(), ImageUnloadingEvent{&_image});
	EXPECT_EQ(_alive.load(), 0u);
	EXPECT_TRUE(HeardAt(0.25f * CENTRE)) << LastBlockPeak();

	ASSERT_TRUE(AudioEffects::Register("Listener Gain", &_definition, &_image));
	EXPECT_TRUE(HeardAt(2.0f * 0.25f * CENTRE)) << LastBlockPeak();
	EXPECT_EQ(AudioEffects::UnregisterOwnedBy(&_image), 1u);
	EXPECT_EQ(_alive.load(), 0u);
}

TEST_F(ListenerMixFixture, ReopenOfSameFormatKeepsListenersInstances) {
	ASSERT_TRUE(AudioEffects::Register("Listener Gain", &_definition, &_image));
	_hearer->AddComponent<ListenerGainFilter>();
	PlayLevel(0.25f);
	EXPECT_TRUE(HeardAt(2.0f * 0.25f * CENTRE));

	AudioSettings* settings = GetDeclaredSettings();
	ASSERT_NE(settings, nullptr);
	settings->periodMilliseconds = settings->periodMilliseconds == 20 ? 10 : 20;
	Settings::NotifyChanged(SETTINGS_CATEGORY_PATH, "periodMilliseconds");
	EXPECT_EQ(_made.load(), 1u);
	EXPECT_EQ(_alive.load(), 1u);
	EXPECT_TRUE(HeardAt(2.0f * 0.25f * CENTRE)) << LastBlockPeak();
	EXPECT_EQ(AudioEffects::UnregisterOwnedBy(&_image), 1u);
}

TEST_F(ListenerMixFixture, PackageGoingDownReleasesListenersInstances) {
	ASSERT_TRUE(AudioEffects::Register("Listener Gain", &_definition, &_image));
	_hearer->AddComponent<ListenerGainFilter>();
	PlayLevel(0.25f);
	EXPECT_TRUE(HeardAt(2.0f * 0.25f * CENTRE));
	ASSERT_EQ(_alive.load(), 1u);
	ShutdownPackage();
	EXPECT_EQ(_alive.load(), 0u);
}

TEST_F(ListenerOutputFixture, ListenerOutputFeedsGroup) {
	const Handle<AudioMixerGroup> players = Group("Players");
	_listener->SetOutputAudioMixerGroup(players);
	PlayLevel(0.25f);
	EXPECT_TRUE(HeardAt(0.25f * CENTRE * Gain(-6.0f))) << LastBlockPeak();

	_hearer->AddComponent<AudioDistortionFilter>();
	EXPECT_TRUE(HeardAt(Distorted(0.25f * CENTRE) * Gain(-6.0f))) << LastBlockPeak();
	_source->SetBypassListenerEffects(true);
	EXPECT_TRUE(HeardAt(0.25f * CENTRE * Gain(-6.0f))) << LastBlockPeak();
	_source->SetBypassListenerEffects(false);

	_source->SetOutputAudioMixerGroup(Group("Music"));
	EXPECT_TRUE(HeardAt(0.25f * CENTRE)) << LastBlockPeak();
	_source->SetOutputAudioMixerGroup({});
	EXPECT_TRUE(HeardAt(Distorted(0.25f * CENTRE) * Gain(-6.0f))) << LastBlockPeak();
}

TEST_F(ListenerOutputFixture, ListenerFiltersHeldAsGroup) {
	_listener->SetOutputAudioMixerGroup(Group("Players"));
	_hearer->AddComponent<AudioDistortionFilter>();
	PlayLevel(0.25f);
	EXPECT_TRUE(HeardAt(Distorted(0.25f * CENTRE) * Gain(-6.0f)));
	RunFrame(0.0f);
	EXPECT_TRUE(WaitFor([]() { return LastBlockPeak() == 0.0f; })) << LastBlockPeak();
	EXPECT_TRUE(HeardAt(Distorted(0.25f * CENTRE) * Gain(-6.0f))) << LastBlockPeak();
}

TEST_F(ListenerOutputBudgetFixture, BudgetCountsHeldSoundSilent) {
	_listener->SetOutputAudioMixerGroup(Group("Players"));
	_hearer->AddComponent<AudioDistortionFilter>();
	PlayLevel(0.5f);
	AudioPlayParams params;
	params.loop = true;
	params.volume = 0.2f;
	params.output = static_cast<AudioMixerGroup*>(Group("Music").Get())->GetBus();
	const AudioVoice music = Audio::Play(ResourceOf(MakeClip(9600, 1, 0.5f)), params);
	RunFrame();
	ASSERT_TRUE(Audio::IsVirtual(music));

	RunFrame(0.0f);
	RunFrame(0.0f);
	EXPECT_FALSE(Audio::IsVirtual(music));
	EXPECT_TRUE(WaitFor([]() { return std::fabs(LastBlockPeak() - 0.1f * CENTRE) < 1e-3f; })) << LastBlockPeak();
}

#endif
