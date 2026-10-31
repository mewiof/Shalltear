#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioBus.h"
#include "AudioLiveFixture.h"
#include "AudioMixerEditOps.h"
#include "AudioMixerLive.h"
#include "ScopedImportCostThreshold.h"

#include "Shalltear/Audio/Audio.h"
#include "Shalltear/Audio/AudioListener.h"
#include "Shalltear/Audio/AudioMixer.h"
#include "Shalltear/Audio/AudioSource.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"

#include <cmath>
#include <filesystem>
#include <format>

using namespace AudioTesting;

namespace {

	constexpr const char* AUTHORED = R"({
	"startSnapshot": "Explore",
	"groups": [
		{ "id": "Master" },
		{ "id": "Music", "parent": "Master", "clock": "Wall" },
		{ "id": "SFX", "parent": "Master", "clock": "Gameplay",
		  "units": [ { "kind": "Send", "target": "Reverb" }, { "kind": "Attenuation" } ] },
		{ "id": "Reverb", "parent": "Master" }
	],
	"snapshots": [
		{ "name": "Explore", "values": [ { "group": "Music", "parameter": "Volume", "value": -6 } ] },
		{ "name": "Combat", "values": [
			{ "group": "Music", "parameter": "Volume", "value": -20 },
			{ "group": "SFX", "parameter": "SendLevel", "target": "Reverb", "value": -10 } ] }
	],
	"exposedParameters": [ { "name": "MusicVolume", "group": "Music", "parameter": "Volume" } ]
})";

	AudioMixer::Group MakeGroup(const char* id, const char* parent = "", AudioMixerGroupClock clock = AudioMixerGroupClock::Wall) {
		AudioMixer::Group group;
		group.id = id;
		group.name = id;
		group.parent = parent;
		group.clock = clock;
		group.units.push_back(AudioMixer::Unit{});
		return group;
	}

	AudioMixer::Value MakeValue(const char* group, AudioMixerParameter parameter, f32 value, const char* target = "") {
		AudioMixer::Value row;
		row.group = group;
		row.parameter = parameter;
		row.target = target;
		row.value = value;
		return row;
	}

	void BuildAuthored(AudioMixer& mixer) {
		mixer.groups.push_back(MakeGroup("Master"));
		mixer.groups.push_back(MakeGroup("Music", "Master", AudioMixerGroupClock::Wall));
		AudioMixer::Group sfx = MakeGroup("SFX", "Master", AudioMixerGroupClock::Gameplay);
		AudioMixer::Unit send;
		send.kind = AudioMixerUnitKind::Send;
		send.target = "Reverb";
		sfx.units.insert(sfx.units.begin(), send);
		mixer.groups.push_back(std::move(sfx));
		mixer.groups.push_back(MakeGroup("Reverb", "Master"));

		AudioMixer::Snapshot explore;
		explore.id = "Explore";
		explore.name = "Explore";
		explore.values.push_back(MakeValue("Music", AudioMixerParameter::Volume, -6.0f));
		mixer.snapshots.push_back(std::move(explore));
		AudioMixer::Snapshot combat;
		combat.id = "Combat";
		combat.name = "Combat";
		combat.values.push_back(MakeValue("SFX", AudioMixerParameter::SendLevel, -10.0f, "Reverb"));
		combat.values.push_back(MakeValue("Music", AudioMixerParameter::Volume, -20.0f));
		mixer.snapshots.push_back(std::move(combat));
		mixer.startSnapshot = "Explore";

		AudioMixer::ExposedParameter exposed;
		exposed.name = "MusicVolume";
		exposed.group = "Music";
		exposed.parameter = AudioMixerParameter::Volume;
		mixer.exposedParameters.push_back(std::move(exposed));
	}

	class ScratchDirectory {
	public:
		ScratchDirectory() {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_audio_mixer_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
		}
		~ScratchDirectory() {
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}
		Path File(const char* name) const { return Path((_root / name).generic_string()); }
	private:
		std::filesystem::path _root;
	};

	str Saved(const AudioMixer& mixer, const ScratchDirectory& scratch, const char* name) {
		const Path path = scratch.File(name);
		if (!mixer.Export(path)) {
			ADD_FAILURE() << "mixer didn't export";
			return {};
		}
		return FileSystem::ReadTextFile(path);
	}

	struct CookedMixer {
		Vector<u8> descriptor{GetMemoryResource(MemTag::Asset)};
		Vector<u8> payload{GetMemoryResource(MemTag::Asset)};
		PakStringPool pool;
		bool cooked = false;
	};

	void CookMixer(const Asset& asset, CookedMixer& out) {
		CookWriter descriptor(out.descriptor, out.pool);
		CookWriter payload(out.payload, out.pool);
		out.cooked = asset.Cook(descriptor, payload);
	}

	bool LoadMixer(const CookedMixer& cooked, Asset& into) {
		Vector<u8> strings{GetMemoryResource(MemTag::Asset)};
		PakWriteStringBlob(strings, cooked.pool.Texts());
		PakStringTable table;
		if (!PakReadStringBlob(strings.data(), strings.size(), table)) {
			return false;
		}
		CookReader descriptor(cooked.descriptor.data(), cooked.descriptor.size(), table);
		CookReader payload(cooked.payload.data(), cooked.payload.size(), table);
		return into.LoadCooked(descriptor, payload);
	}

	f32 Gain(f32 decibels) { return std::pow(10.0f, decibels / 20.0f); }

	f32 CentredPeak(f32 value) { return value * std::sqrt(0.5f); }

} // namespace

TEST(AudioMixerDocumentTest, DocumentRoundTripIsStable) {
	ScratchDirectory scratch;
	FileSystem::WriteTextFile(scratch.File("authored.mixer"), str(AUTHORED));
	AudioMixer first;
	ASSERT_TRUE(first.Import(scratch.File("authored.mixer")));
	const str once = Saved(first, scratch, "once.mixer");
	AudioMixer second;
	ASSERT_TRUE(second.Import(scratch.File("once.mixer")));
	EXPECT_EQ(Saved(second, scratch, "twice.mixer"), once);

	EXPECT_EQ(second.groups.size(), 4u);
	EXPECT_EQ(second.groups[2].units.size(), 2u);
	EXPECT_EQ(second.groups[2].units[0].kind, AudioMixerUnitKind::Send);
	EXPECT_EQ(SnapshotValue(second, 0, "Music", AudioMixerParameter::Volume, {}, {}), -6.0f);
	EXPECT_EQ(SnapshotValue(second, 0, "SFX", AudioMixerParameter::SendLevel, "Reverb", {}), -80.0f);
	EXPECT_EQ(SnapshotValue(second, 1, "SFX", AudioMixerParameter::SendLevel, "Reverb", {}), -10.0f);
	EXPECT_EQ(second.snapshots[1].id, "Combat");
}

TEST(AudioMixerDocumentTest, BuiltMixerSavesAuthoredBytes) {
	ScratchDirectory scratch;
	FileSystem::WriteTextFile(scratch.File("authored.mixer"), str(AUTHORED));
	AudioMixer authored;
	ASSERT_TRUE(authored.Import(scratch.File("authored.mixer")));
	AudioMixer built;
	BuildAuthored(built);
	const str fromDocument = Saved(authored, scratch, "fromDocument.mixer");
	ASSERT_FALSE(fromDocument.empty());
	EXPECT_EQ(Saved(built, scratch, "fromCode.mixer"), fromDocument);
}

TEST(AudioMixerDocumentTest, CookIsStableAndLoadsBack) {
	ScratchDirectory scratch;
	AudioMixer built;
	BuildAuthored(built);
	CookedMixer first;
	CookedMixer again;
	CookMixer(built, first);
	CookMixer(built, again);
	ASSERT_TRUE(first.cooked);
	EXPECT_EQ(first.descriptor, again.descriptor);
	EXPECT_TRUE(first.payload.empty());

	AudioMixer loaded;
	ASSERT_TRUE(LoadMixer(first, loaded));
	EXPECT_EQ(Saved(loaded, scratch, "loaded.mixer"), Saved(built, scratch, "built.mixer"));

	FileSystem::WriteTextFile(scratch.File("authored.mixer"), str(AUTHORED));
	AudioMixer authored;
	ASSERT_TRUE(authored.Import(scratch.File("authored.mixer")));
	CookedMixer fromDocument;
	CookMixer(authored, fromDocument);
	EXPECT_EQ(fromDocument.descriptor, first.descriptor);
}

TEST(AudioMixerDocumentTest, NonMixerRefusedMixerUnchanged) {
	ScratchDirectory scratch;
	FileSystem::WriteTextFile(scratch.File("good.mixer"), str(AUTHORED));
	AudioMixer mixer;
	ASSERT_TRUE(mixer.Import(scratch.File("good.mixer")));
	const str good = Saved(mixer, scratch, "good.out.mixer");

	const char* broken[] = {
		R"({"groups": [{"id": "Master"}, {"id": "A", "parent": "Master"}, {"id": "A", "parent": "Master"}], "snapshots": [{"name": "S"}]})",
		R"({"groups": [{"id": "Master"}, {"id": "Other"}], "snapshots": [{"name": "S"}]})",
		R"({"groups": [{"id": "Master"}, {"id": "A", "parent": "Nowhere"}], "snapshots": [{"name": "S"}]})",
		R"({"groups": [{"id": "Master", "units": [{"kind": "Attenuation"}, {"kind": "Send", "target": "Master"}]}], "snapshots": [{"name": "S"}]})",
		R"({"groups": [{"id": "Master", "units": [{"kind": "Attenuation"}, {"kind": "Send", "target": "Nowhere"}]}], "snapshots": [{"name": "S"}]})",
		R"({"groups": [{"id": "Master"}, {"id": "A", "parent": "Master", "units": [{"kind": "Attenuation"}, {"kind": "Send", "target": "B"}]},
			{"id": "B", "parent": "A"}], "snapshots": [{"name": "S"}]})",
		R"({"groups": [{"id": "Master", "units": [{"kind": "Attenuation"}, {"kind": "Attenuation"}]}], "snapshots": [{"name": "S"}]})",
		R"({"startSnapshot": "Missing", "groups": [{"id": "Master"}], "snapshots": [{"name": "S"}]})",
		R"({"groups": [{"id": "Master"}], "snapshots": [{"name": "S"}],
			"exposedParameters": [{"name": "X", "group": "Master", "parameter": "SendLevel", "target": "Master"}]})",
		R"({"groups": [{"id": "Mas/ter"}], "snapshots": [{"name": "S"}]})",
		R"({"groups": [{"id": "Master"}]})",
		R"({"groups": [{"id": "Master"}], "snapshots": [{"name": "S", "values": [{"group": "Nowhere", "parameter": "Volume", "value": 0}]}]})",
		R"({"groups": [{"id": "Master"}], "snapshots": [{"id": "S", "name": "A"}, {"id": "S", "name": "B"}]})",
		R"({"groups": [{"id": "Master"}], "snapshots": [{"id": "S/1", "name": "S"}]})",
		R"({"groups": [{"id": "Master"}], "snapshots": [{"id": "A", "name": "S"}, {"id": "B", "name": "S"}]})",
		R"({"groups": [{"id": "Master", "units": [{"kind": "Effect", "id": "E"}, {"kind": "Attenuation"}]}], "snapshots": [{"name": "S"}]})",
		R"({"groups": [{"id": "Master", "units": [{"kind": "Effect", "id": "E", "effect": "X"}, {"kind": "Effect", "id": "E", "effect": "Y"},
			{"kind": "Attenuation"}]}], "snapshots": [{"name": "S"}]})",
		R"({"groups": [{"id": "Master", "units": [{"kind": "Effect", "id": "E/1", "effect": "X"}, {"kind": "Attenuation"}]}], "snapshots": [{"name": "S"}]})",
		R"({"groups": [{"id": "Master", "units": [{"kind": "Effect", "id": "E", "effect": "X"}, {"kind": "Attenuation"}]}],
			"snapshots": [{"name": "S", "values": [{"group": "Master", "parameter": "WetLevel", "target": "E", "value": 50}]}]})",
		R"({"groups": [{"id": "Master", "units": [{"kind": "Effect", "id": "E", "effect": "X"}, {"kind": "Attenuation"}]}],
			"snapshots": [{"name": "S", "values": [{"group": "Master", "parameter": "EffectParameter", "target": "E", "value": 1}]}]})",
		R"({"groups": [{"id": "Master"}], "snapshots": [{"name": "S"}],
			"exposedParameters": [{"name": "X", "group": "Master", "parameter": "EffectParameter", "target": "E", "effectParameter": "Gain"}]})",
		R"({"groups": [{"id": "Master"}, {"id": "A", "parent": "Master", "units": [{"kind": "Send", "target": "Master", "targetUnit": "Nowhere"},
			{"kind": "Attenuation"}]}], "snapshots": [{"name": "S"}]})",
		R"({"groups": [{"id": "Master", "units": [{"kind": "Receive", "id": "E"}, {"kind": "Effect", "id": "E", "effect": "X"}, {"kind": "Attenuation"}]}],
			"snapshots": [{"name": "S"}]})",
	};
	for (size_t index = 0; index < std::size(broken); ++index) {
		const Path path = scratch.File(std::format("broken{}.mixer", index).c_str());
		FileSystem::WriteTextFile(path, str(broken[index]));
		EXPECT_FALSE(mixer.Import(path)) << "document " << index;
		EXPECT_EQ(Saved(mixer, scratch, "after.mixer"), good) << "document " << index;
	}

	str units;
	for (u32 index = 0; index < MAX_BUS_ROWS; ++index) {
		units += std::format(R"({{"kind": "Effect", "id": "E{}", "effect": "X"}}, )", index);
	}
	const Path path = scratch.File("long.mixer");
	FileSystem::WriteTextFile(path, std::format(R"({{"groups": [{{"id": "Master", "units": [{}{{"kind": "Attenuation"}}]}}], "snapshots": [{{"name": "S"}}]}})", units));
	EXPECT_FALSE(mixer.Import(path));
	EXPECT_EQ(Saved(mixer, scratch, "after.mixer"), good);
}

namespace {

	constexpr const char* WITH_EFFECTS = R"({
	"startSnapshot": "Dry",
	"groups": [
		{ "id": "Master", "bypassEffects": true,
		  "units": [ { "kind": "Effect", "effect": "Test.Filter" }, { "kind": "Attenuation" },
					 { "kind": "Effect", "id": "Room", "effect": "Test.Reverb", "wetMixing": true, "bypass": true } ] }
	],
	"snapshots": [
		{ "name": "Dry", "values": [
			{ "group": "Master", "parameter": "EffectParameter", "target": "Room", "effectParameter": "Size", "value": 0.25 },
			{ "group": "Master", "parameter": "EffectParameter", "target": "Test.Filter", "effectParameter": "Cutoff", "value": 800 },
			{ "group": "Master", "parameter": "EffectParameter", "target": "Room", "effectParameter": "Damping", "value": 0.5 } ] },
		{ "name": "Wet", "values": [ { "group": "Master", "parameter": "WetLevel", "target": "Room", "value": 25 } ] }
	],
	"exposedParameters": [ { "name": "Cutoff", "group": "Master", "parameter": "EffectParameter", "target": "Test.Filter", "effectParameter": "Cutoff" } ]
})";

	AudioMixer::Value MakeEffectValue(const char* unit, const char* parameter, f32 value) {
		AudioMixer::Value row = MakeValue("Master", AudioMixerParameter::EffectParameter, value, unit);
		row.effectParameter = parameter;
		return row;
	}

	void BuildWithEffects(AudioMixer& mixer) {
		AudioMixer::Group master = MakeGroup("Master");
		master.bypassEffects = true;
		AudioMixer::Unit filter;
		filter.kind = AudioMixerUnitKind::Effect;
		filter.id = "Test.Filter";
		filter.effect = "Test.Filter";
		AudioMixer::Unit room;
		room.kind = AudioMixerUnitKind::Effect;
		room.id = "Room";
		room.effect = "Test.Reverb";
		room.wetMixing = true;
		room.bypass = true;
		master.units.insert(master.units.begin(), filter);
		master.units.push_back(room);
		mixer.groups.push_back(std::move(master));

		AudioMixer::Snapshot dry;
		dry.id = "Dry";
		dry.name = "Dry";
		dry.values.push_back(MakeEffectValue("Room", "Damping", 0.5f));
		dry.values.push_back(MakeEffectValue("Test.Filter", "Cutoff", 800.0f));
		dry.values.push_back(MakeEffectValue("Room", "Size", 0.25f));
		mixer.snapshots.push_back(std::move(dry));
		AudioMixer::Snapshot wet;
		wet.id = "Wet";
		wet.name = "Wet";
		wet.values.push_back(MakeValue("Master", AudioMixerParameter::WetLevel, 25.0f, "Room"));
		mixer.snapshots.push_back(std::move(wet));
		mixer.startSnapshot = "Dry";

		AudioMixer::ExposedParameter exposed;
		exposed.name = "Cutoff";
		exposed.group = "Master";
		exposed.parameter = AudioMixerParameter::EffectParameter;
		exposed.target = "Test.Filter";
		exposed.effectParameter = "Cutoff";
		mixer.exposedParameters.push_back(std::move(exposed));
	}

} // namespace

TEST(AudioMixerDocumentTest, EffectsDocumentRoundTripIsStable) {
	ScratchDirectory scratch;
	FileSystem::WriteTextFile(scratch.File("effects.mixer"), str(WITH_EFFECTS));
	AudioMixer first;
	ASSERT_TRUE(first.Import(scratch.File("effects.mixer")));
	const str once = Saved(first, scratch, "once.mixer");
	AudioMixer second;
	ASSERT_TRUE(second.Import(scratch.File("once.mixer")));
	EXPECT_EQ(Saved(second, scratch, "twice.mixer"), once);

	ASSERT_EQ(second.groups.size(), 1u);
	const AudioMixer::Group& master = second.groups[0];
	EXPECT_TRUE(master.bypassEffects);
	ASSERT_EQ(master.units.size(), 3u);
	EXPECT_EQ(master.units[0].kind, AudioMixerUnitKind::Effect);
	EXPECT_EQ(master.units[0].id, "Test.Filter");
	EXPECT_EQ(master.units[0].effect, "Test.Filter");
	EXPECT_FALSE(master.units[0].bypass);
	EXPECT_FALSE(master.units[0].wetMixing);
	EXPECT_EQ(master.units[2].id, "Room");
	EXPECT_EQ(master.units[2].effect, "Test.Reverb");
	EXPECT_TRUE(master.units[2].bypass);
	EXPECT_TRUE(master.units[2].wetMixing);
	EXPECT_EQ(SnapshotValue(second, 0, "Master", AudioMixerParameter::EffectParameter, "Room", "Size"), 0.25f);
	EXPECT_EQ(SnapshotValue(second, 0, "Master", AudioMixerParameter::EffectParameter, "Test.Filter", "Cutoff"), 800.0f);
	EXPECT_EQ(SnapshotValue(second, 0, "Master", AudioMixerParameter::WetLevel, "Room", {}), 100.0f);
	EXPECT_EQ(SnapshotValue(second, 1, "Master", AudioMixerParameter::WetLevel, "Room", {}), 25.0f);
	ASSERT_EQ(second.exposedParameters.size(), 1u);
	EXPECT_EQ(second.exposedParameters[0].parameter, AudioMixerParameter::EffectParameter);
	EXPECT_EQ(second.exposedParameters[0].target, "Test.Filter");
	EXPECT_EQ(second.exposedParameters[0].effectParameter, "Cutoff");
	EXPECT_EQ(second.snapshots[1].values.size(), 3u);
}

TEST(AudioMixerDocumentTest, BuiltEffectsMixerMatchesAuthored) {
	ScratchDirectory scratch;
	FileSystem::WriteTextFile(scratch.File("effects.mixer"), str(WITH_EFFECTS));
	AudioMixer authored;
	ASSERT_TRUE(authored.Import(scratch.File("effects.mixer")));
	AudioMixer built;
	BuildWithEffects(built);
	const str fromDocument = Saved(authored, scratch, "fromDocument.mixer");
	ASSERT_FALSE(fromDocument.empty());
	EXPECT_EQ(Saved(built, scratch, "fromCode.mixer"), fromDocument);

	CookedMixer fromCode;
	CookedMixer fromFile;
	CookMixer(built, fromCode);
	CookMixer(authored, fromFile);
	ASSERT_TRUE(fromCode.cooked);
	EXPECT_EQ(fromCode.descriptor, fromFile.descriptor);
	AudioMixer loaded;
	ASSERT_TRUE(LoadMixer(fromCode, loaded));
	EXPECT_EQ(Saved(loaded, scratch, "loaded.mixer"), fromDocument);
}

namespace {

	constexpr const char* WITH_RECEIVES = R"({
	"groups": [
		{ "id": "Master" },
		{ "id": "Music", "parent": "Master",
		  "units": [ { "kind": "Effect", "id": "Duck", "effect": "Duck Volume" }, { "kind": "Receive" }, { "kind": "Attenuation" } ] },
		{ "id": "SFX", "parent": "Master", "units": [ { "kind": "Send", "target": "Music", "targetUnit": "Duck" }, { "kind": "Attenuation" } ] },
		{ "id": "Voice", "parent": "Master", "units": [ { "kind": "Attenuation" }, { "kind": "Send", "target": "Music", "targetUnit": "Receive" } ] }
	],
	"snapshots": [ { "name": "S" } ]
})";

} // namespace

TEST(AudioMixerDocumentTest, UnitSendsRoundTrip) {
	ScratchDirectory scratch;
	FileSystem::WriteTextFile(scratch.File("receives.mixer"), str(WITH_RECEIVES));
	AudioMixer first;
	ASSERT_TRUE(first.Import(scratch.File("receives.mixer")));
	const str once = Saved(first, scratch, "once.mixer");
	AudioMixer second;
	ASSERT_TRUE(second.Import(scratch.File("once.mixer")));
	EXPECT_EQ(Saved(second, scratch, "twice.mixer"), once);

	ASSERT_EQ(second.groups.size(), 4u);
	ASSERT_EQ(second.groups[1].units.size(), 3u);
	EXPECT_EQ(second.groups[1].units[1].kind, AudioMixerUnitKind::Receive);
	EXPECT_EQ(second.groups[1].units[1].id, "Receive");
	EXPECT_EQ(second.groups[2].units[0].targetUnit, "Duck");
	EXPECT_EQ(second.groups[3].units[1].targetUnit, "Receive");

	CookedMixer cooked;
	CookMixer(second, cooked);
	ASSERT_TRUE(cooked.cooked);
	AudioMixer loaded;
	ASSERT_TRUE(LoadMixer(cooked, loaded));
	EXPECT_EQ(Saved(loaded, scratch, "loaded.mixer"), once);
}

TEST(AudioMixerDocumentTest, AnEmptyDocumentIsANewMixer) {
	ScratchDirectory scratch;
	FileSystem::WriteTextFile(scratch.File("empty.mixer"), str("{}"));
	AudioMixer mixer;
	ASSERT_TRUE(mixer.Import(scratch.File("empty.mixer")));
	ASSERT_EQ(mixer.groups.size(), 1u);
	EXPECT_EQ(mixer.groups[0].id, "Master");
	ASSERT_EQ(mixer.snapshots.size(), 1u);
	EXPECT_EQ(mixer.snapshots[0].name, "Snapshot");
}

namespace {

	class AudioMixerFixture : public LivePackageFixture {
	protected:
		Vector<AudioMixer*> _mixers{GetMemoryResource(MemTag::Asset)};

		void SetUp() override {
			LivePackageFixture::SetUp();
			GetAudioSystem().SetListenerPresent(true);
		}

		void TearDown() override {
			for (AudioMixer* mixer : _mixers) {
				Delete(mixer);
			}
			_mixers.clear();
			LivePackageFixture::TearDown();
		}

		AudioMixer* LiveMixer(AudioMixerUpdateMode mode = AudioMixerUpdateMode::Normal) {
			AudioMixer* mixer = New<AudioMixer>(MemTag::Asset);
			_mixers.push_back(mixer);
			BuildAuthored(*mixer);
			mixer->updateMode = mode;
			EXPECT_TRUE(mixer->ApplyChanges());
			return mixer;
		}

		AudioVoice LoopInto(f32 value, AudioBus bus) {
			AudioPlayParams params;
			params.loop = true;
			params.output = bus;
			return Audio::Play(ResourceOf(MakeClip(96000, 1, value)), params);
		}

		static bool PeakNear(f32 expected) {
			return WaitFor([expected]() { return std::fabs(LastBlockPeak() - expected) < 1e-4f; });
		}

		static f32 Parameter(AudioMixer& mixer, const char* name) {
			f32 value = 0.0f;
			EXPECT_TRUE(mixer.GetFloat(name, value)) << name;
			return value;
		}
	};

} // namespace

TEST_F(AudioMixerFixture, LiveMixerSoundsGroupsAtStartSnapshot) {
	AudioMixer* mixer = LiveMixer();
	ASSERT_TRUE(mixer->GetBus("Music").IsValid());
	ASSERT_TRUE(mixer->GetBus("SFX").IsValid());
	EXPECT_NE(mixer->GetBus("Music"), mixer->GetBus("SFX"));
	EXPECT_FALSE(mixer->GetBus("Nowhere").IsValid());
	EXPECT_EQ(Parameter(*mixer, "MusicVolume"), -6.0f);

	LoopInto(0.5f, mixer->GetBus("Music"));
	EXPECT_TRUE(PeakNear(CentredPeak(0.5f) * Gain(-6.0f))) << "heard at " << LastBlockPeak();
}

TEST_F(AudioMixerFixture, TransitionReachesTargetAtEnd) {
	AudioMixer* mixer = LiveMixer();
	LoopInto(0.5f, mixer->GetBus("Music"));
	ASSERT_TRUE(mixer->TransitionToSnapshot("Combat", 0.45f));
	for (u32 frame = 1; frame <= 4; ++frame) {
		RunFrame(1.0f, true, 0.1f);
		const f32 expected = -6.0f + (-20.0f + 6.0f) * (static_cast<f32>(frame) * 0.1f / 0.45f);
		EXPECT_NEAR(Parameter(*mixer, "MusicVolume"), expected, 1e-3f) << "frame " << frame;
		EXPECT_NE(Parameter(*mixer, "MusicVolume"), -20.0f) << frame;
	}
	RunFrame(1.0f, true, 0.1f);
	EXPECT_EQ(Parameter(*mixer, "MusicVolume"), -20.0f);
	EXPECT_TRUE(PeakNear(CentredPeak(0.5f) * Gain(-20.0f))) << LastBlockPeak();
}

TEST_F(AudioMixerFixture, TransitionToSnapshotsGoesToWeightedAverage) {
	AudioMixer* mixer = LiveMixer();
	const std::string_view names[] = {"Explore", "Combat"};
	const f32 weights[] = {1.0f, 3.0f};
	ASSERT_TRUE(mixer->TransitionToSnapshots(names, weights, 0.0f));
	EXPECT_EQ(Parameter(*mixer, "MusicVolume"), (-6.0f * 1.0f + -20.0f * 3.0f) / 4.0f);

	const f32 none[] = {0.0f, 0.0f};
	EXPECT_FALSE(mixer->TransitionToSnapshots(names, none, 1.0f));
	const std::string_view unknown[] = {"Explore", "Nowhere"};
	EXPECT_FALSE(mixer->TransitionToSnapshots(unknown, weights, 1.0f));
	EXPECT_FALSE(mixer->TransitionToSnapshot("Nowhere", 1.0f));
}

TEST_F(AudioMixerFixture, SetFloatPinsClearFloatReleases) {
	AudioMixer* mixer = LiveMixer();
	LoopInto(0.5f, mixer->GetBus("Music"));
	ASSERT_TRUE(mixer->SetFloat("MusicVolume", -40.0f));
	EXPECT_EQ(Parameter(*mixer, "MusicVolume"), -40.0f);
	ASSERT_TRUE(mixer->TransitionToSnapshot("Combat", 0.0f));
	EXPECT_EQ(Parameter(*mixer, "MusicVolume"), -40.0f);
	EXPECT_TRUE(PeakNear(CentredPeak(0.5f) * Gain(-40.0f))) << "heard at " << LastBlockPeak();

	ASSERT_TRUE(mixer->ClearFloat("MusicVolume"));
	EXPECT_EQ(Parameter(*mixer, "MusicVolume"), -20.0f);
	EXPECT_TRUE(PeakNear(CentredPeak(0.5f) * Gain(-20.0f))) << "heard at " << LastBlockPeak();

	f32 value = 0.0f;
	EXPECT_FALSE(mixer->SetFloat("Nowhere", 0.0f));
	EXPECT_FALSE(mixer->GetFloat("Nowhere", value));
	EXPECT_FALSE(mixer->ClearFloat("Nowhere"));
}

TEST_F(AudioMixerFixture, TransitionFollowsGameTime) {
	AudioMixer* normal = LiveMixer(AudioMixerUpdateMode::Normal);
	AudioMixer* unscaled = LiveMixer(AudioMixerUpdateMode::UnscaledTime);
	ASSERT_TRUE(normal->TransitionToSnapshot("Combat", 0.45f));
	ASSERT_TRUE(unscaled->TransitionToSnapshot("Combat", 0.45f));

	RunFrame(0.0f, true, 0.1f);
	RunFrame(0.0f, true, 0.1f);
	EXPECT_EQ(Parameter(*normal, "MusicVolume"), -6.0f);
	const f32 moved = Parameter(*unscaled, "MusicVolume");
	EXPECT_NEAR(moved, -6.0f + (-14.0f) * (0.2f / 0.45f), 1e-3f);

	RunFrame(1.0f, false, 0.1f);
	EXPECT_EQ(Parameter(*normal, "MusicVolume"), -6.0f);
	EXPECT_EQ(Parameter(*unscaled, "MusicVolume"), moved);
}

TEST_F(AudioMixerFixture, ApplyChangesRebuildsBuses) {
	AudioMixer* mixer = LiveMixer();
	const u32 before = GetAudioSystem().GetBuses().CountLive();
	mixer->groups.push_back(MakeGroup("Voice", "Master"));
	ASSERT_TRUE(mixer->ApplyChanges());
	const AudioBus voiceBus = mixer->GetBus("Voice");
	ASSERT_TRUE(voiceBus.IsValid());
	EXPECT_EQ(GetAudioSystem().GetBuses().CountLive(), before + 1);

	const AudioVoice voice = LoopInto(0.5f, voiceBus);
	EXPECT_TRUE(PeakNear(CentredPeak(0.5f)));

	mixer->groups.pop_back();
	ASSERT_TRUE(mixer->ApplyChanges());
	EXPECT_FALSE(mixer->GetBus("Voice").IsValid());
	WaitForBlocks(4);
	EXPECT_TRUE(Audio::IsPlaying(voice));
	EXPECT_TRUE(PeakNear(CentredPeak(0.5f)));

	mixer->groups.push_back(MakeGroup("Loop", "Loop"));
	EXPECT_FALSE(mixer->ApplyChanges());
	mixer->groups.pop_back();
	EXPECT_TRUE(mixer->GetBus("Music").IsValid());
}

TEST_F(AudioMixerFixture, LiveMixersKeepSeparateGroups) {
	AudioMixer* first = LiveMixer();
	AudioMixer* second = LiveMixer();
	ASSERT_TRUE(second->SetFloat("MusicVolume", -20.0f));
	EXPECT_NE(first->GetBus("Music"), second->GetBus("Music"));
	LoopInto(0.5f, first->GetBus("Music"));
	LoopInto(0.5f, second->GetBus("Music"));
	EXPECT_TRUE(PeakNear(CentredPeak(0.5f) * (Gain(-6.0f) + Gain(-20.0f)))) << "heard at " << LastBlockPeak();
	EXPECT_EQ(Parameter(*first, "MusicVolume"), -6.0f);
}

TEST_F(AudioMixerFixture, MixerBuiltWhenPackageReturns) {
	ShutdownPackage();
	FrameStages::UnregisterOwnedBy(&_token);
	AudioMixer* mixer = New<AudioMixer>(MemTag::Asset);
	_mixers.push_back(mixer);
	BuildAuthored(*mixer);
	ASSERT_TRUE(mixer->ApplyChanges());
	EXPECT_FALSE(mixer->GetBus("Music").IsValid());

	SLT_CALL_LINKED_PACKAGE(Audio, &_token);
	EXPECT_TRUE(mixer->GetBus("Music").IsValid());
}

TEST_F(AudioMixerFixture, ResetReturnsToStartSnapshot) {
	AudioMixer* mixer = LiveMixer();
	ASSERT_TRUE(mixer->TransitionToSnapshot("Combat", 0.0f));
	ASSERT_TRUE(mixer->SetFloat("MusicVolume", -40.0f));
	ResetMixers();
	EXPECT_EQ(Parameter(*mixer, "MusicVolume"), -6.0f);
	ASSERT_TRUE(mixer->TransitionToSnapshot("Combat", 0.0f));
	EXPECT_EQ(Parameter(*mixer, "MusicVolume"), -20.0f);
}

namespace {

	class AudioMixerAssetFixture : public AudioMixerFixture {
	protected:
		void SetUp() override {
			AudioMixerFixture::SetUp();
			std::filesystem::create_directories(_root / "Assets");
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			AudioMixerFixture::TearDown();
		}

		static Path AssetPath(const char* name) { return Path(str("project://Assets/") + name); }

		static void Write(const char* name, const str& text) { FileSystem::WriteTextFile(AssetPath(name), text); }

		static Guid ProductOf(const Guid& mixer, const char* key) {
			Vector<AssetProduct> products{GetMemoryResource(MemTag::Asset)};
			AssetDatabase::GetProducts(mixer, products);
			for (const AssetProduct& product : products) {
				if (product.key == key) {
					return product.guid;
				}
			}
			return {};
		}

		static str NameOf(const Guid& mixer, const char* key) {
			Vector<AssetProduct> products{GetMemoryResource(MemTag::Asset)};
			AssetDatabase::GetProducts(mixer, products);
			for (const AssetProduct& product : products) {
				if (product.key == key) {
					return product.name;
				}
			}
			return "<no product>";
		}
	};

	constexpr const char* TWO_GROUPS = R"({
	"groups": [ { "id": "Master" }, { "id": "SFX", "name": "SFX", "parent": "Master" } ],
	"snapshots": [ { "name": "Snapshot", "values": [ { "group": "SFX", "parameter": "Volume", "value": -6 } ] } ]
})";

	constexpr const char* TWO_SNAPSHOTS = R"({
	"startSnapshot": "Quiet",
	"groups": [ { "id": "Master" }, { "id": "SFX", "name": "SFX", "parent": "Master" } ],
	"snapshots": [
		{ "id": "Quiet", "name": "Quiet", "values": [ { "group": "SFX", "parameter": "Volume", "value": -20 } ] },
		{ "id": "Loud", "name": "Loud", "values": [ { "group": "SFX", "parameter": "Volume", "value": -2 } ] }
	],
	"exposedParameters": [ { "name": "SFXVolume", "group": "SFX", "parameter": "Volume" } ]
})";

	void CollectAll() {
		while (AssetDatabase::CollectGarbage() != 0) {
		}
	}

	f32 SfxVolume(AudioMixer& mixer) {
		f32 value = 0.0f;
		EXPECT_TRUE(mixer.GetFloat("SFXVolume", value));
		return value;
	}

} // namespace

TEST_F(AudioMixerAssetFixture, GroupProductsSurviveRename) {
	Write("Main.mixer", str(TWO_GROUPS));
	Asset* mixer = AssetDatabase::LoadAsset(AssetPath("Main.mixer"));
	ASSERT_NE(mixer, nullptr);
	const Guid guid = AssetDatabase::LookupGuid(AssetPath("Main.mixer"));
	const Guid master = ProductOf(guid, "Groups/Master");
	const Guid sfx = ProductOf(guid, "Groups/SFX");
	EXPECT_TRUE(master.IsValid());
	EXPECT_TRUE(sfx.IsValid());

	AssetDatabase::UnloadAsset(mixer);
	str renamed(TWO_GROUPS);
	renamed.replace(renamed.find("\"name\": \"SFX\""), std::string_view("\"name\": \"SFX\"").size(), "\"name\": \"Effects\"");
	Write("Main.mixer", renamed);
	ASSERT_TRUE(AssetDatabase::ReimportAsset(AssetPath("Main.mixer")));
	EXPECT_EQ(ProductOf(guid, "Groups/SFX"), sfx);
	EXPECT_EQ(NameOf(guid, "Groups/SFX"), "Effects");
	EXPECT_EQ(NameOf(guid, "Groups/Master"), "Master");
	auto* reloaded = static_cast<AudioMixer*>(AssetDatabase::LoadAsset(AssetPath("Main.mixer")));
	ASSERT_NE(reloaded, nullptr);
	EXPECT_EQ(reloaded->groups[1].name, "Effects");
	AssetDatabase::UnloadAsset(reloaded);
}

TEST_F(AudioMixerAssetFixture, SourcePlaysThroughOutputGroup) {
	Write("Main.mixer", str(TWO_GROUPS));
	Asset* first = AssetDatabase::LoadAsset(AssetPath("Main.mixer"));
	ASSERT_NE(first, nullptr);
	const Guid mixerGuid = AssetDatabase::LookupGuid(AssetPath("Main.mixer"));
	const Guid productGuid = ProductOf(mixerGuid, "Groups/SFX");
	ASSERT_TRUE(productGuid.IsValid());
	AssetDatabase::UnloadAsset(first);

	Asset* loaded = AssetDatabase::LoadAssetByGuid(productGuid);
	ASSERT_NE(loaded, nullptr);
	ASSERT_EQ(loaded->GetAssetTypeId(), AudioMixerGroup::GetReflectType()->name);
	auto* group = static_cast<AudioMixerGroup*>(loaded);
	EXPECT_EQ(group->GetGroupId(), "SFX");
	ASSERT_TRUE(group->GetAudioMixer().IsValid());
	ASSERT_TRUE(group->GetBus().IsValid());

	Handle<Scene> scene = SceneSystem::CreateScene("audio");
	Handle<SceneObject> object = SceneSystem::CreateObject("source", scene);
	Handle<AudioSource> source = object->AddComponent<AudioSource>();
	source->SetResource(ResourceOf(MakeClip(96000, 1, 0.5f)));
	source->SetLoop(true);
	source->SetOutputAudioMixerGroup(Handle<AudioMixerGroup>(group));
	RunFrame();
	ASSERT_TRUE(source->IsPlaying());
	EXPECT_TRUE(PeakNear(CentredPeak(0.5f) * Gain(-6.0f))) << LastBlockPeak();

	source->SetOutputAudioMixerGroup({});
	RunFrame();
	EXPECT_TRUE(PeakNear(CentredPeak(0.5f))) << LastBlockPeak();
	AssetDatabase::UnloadAsset(group);
}

TEST_F(AudioMixerAssetFixture, MixerOutputFeedsOtherMixer) {
	Write("Music.mixer", str(R"({
	"groups": [ { "id": "Master" }, { "id": "Music", "parent": "Master" } ],
	"snapshots": [ { "name": "Snapshot", "values": [ { "group": "Music", "parameter": "Volume", "value": -6 } ] } ]
})"));
	Asset* music = AssetDatabase::LoadAsset(AssetPath("Music.mixer"));
	ASSERT_NE(music, nullptr);
	const Guid musicGuid = AssetDatabase::LookupGuid(AssetPath("Music.mixer"));

	Write("Level.mixer", std::format(R"({{
	"output": {{ "mixer": "{}", "group": "Music" }},
	"groups": [ {{ "id": "Master" }} ],
	"snapshots": [ {{ "name": "Snapshot", "values": [ {{ "group": "Master", "parameter": "Volume", "value": -6 }} ] }} ]
}})",
									 musicGuid.ToString()));
	Asset* level = AssetDatabase::LoadAsset(AssetPath("Level.mixer"));
	ASSERT_NE(level, nullptr);
	auto* levelMixer = static_cast<AudioMixer*>(level);
	LoopInto(0.5f, levelMixer->GetBus("Master"));
	EXPECT_TRUE(PeakNear(CentredPeak(0.5f) * Gain(-6.0f) * Gain(-6.0f)));

	AssetDatabase::UnloadAsset(level);
	AssetDatabase::UnloadAsset(music);
}

TEST_F(AudioMixerAssetFixture, LiveMixerGroupLoadsAfterDrain) {
	ScopedImportCostThreshold noCache{ScopedImportCostThreshold::CacheNothing};
	Write("Main.mixer", str(TWO_GROUPS));
	Asset* first = AssetDatabase::LoadAsset(AssetPath("Main.mixer"));
	ASSERT_NE(first, nullptr);
	const Guid mixerGuid = AssetDatabase::LookupGuid(AssetPath("Main.mixer"));
	const Guid masterGuid = ProductOf(mixerGuid, "Groups/Master");
	const Guid sfxGuid = ProductOf(mixerGuid, "Groups/SFX");
	AssetDatabase::UnloadAsset(first);

	auto* master = static_cast<AudioMixerGroup*>(AssetDatabase::LoadAssetByGuid(masterGuid));
	ASSERT_NE(master, nullptr);
	ASSERT_TRUE(master->GetBus().IsValid());
	while (AssetDatabase::CollectGarbage() != 0) {
	}

	Asset* sfx = AssetDatabase::LoadAssetByGuid(sfxGuid);
	ASSERT_NE(sfx, nullptr);
	auto* group = static_cast<AudioMixerGroup*>(sfx);
	EXPECT_TRUE(group->GetBus().IsValid());
	EXPECT_EQ(group->GetAudioMixer(), master->GetAudioMixer());
	AssetDatabase::UnloadAsset(sfx);
	AssetDatabase::UnloadAsset(master);
}

TEST_F(AudioMixerAssetFixture, SnapshotProductsSurviveRename) {
	Write("Main.mixer", str(TWO_SNAPSHOTS));
	Asset* mixer = AssetDatabase::LoadAsset(AssetPath("Main.mixer"));
	ASSERT_NE(mixer, nullptr);
	const Guid guid = AssetDatabase::LookupGuid(AssetPath("Main.mixer"));
	const Guid loud = ProductOf(guid, "Snapshots/Loud");
	ASSERT_TRUE(loud.IsValid());
	EXPECT_TRUE(ProductOf(guid, "Snapshots/Quiet").IsValid());
	AssetDatabase::UnloadAsset(mixer);

	str renamed(TWO_SNAPSHOTS);
	renamed.replace(renamed.find("\"name\": \"Loud\""), std::string_view("\"name\": \"Loud\"").size(), "\"name\": \"Combat\"");
	Write("Main.mixer", renamed);
	ASSERT_TRUE(AssetDatabase::ReimportAsset(AssetPath("Main.mixer")));
	EXPECT_EQ(ProductOf(guid, "Snapshots/Loud"), loud);
	auto* reloaded = static_cast<AudioMixer*>(AssetDatabase::LoadAsset(AssetPath("Main.mixer")));
	ASSERT_NE(reloaded, nullptr);
	const Handle<AudioMixerSnapshot> found = reloaded->FindSnapshot("Combat");
	ASSERT_TRUE(found.IsValid());
	EXPECT_EQ(found->assetId, loud);
	EXPECT_FALSE(reloaded->FindSnapshot("Loud").IsValid());
	AssetDatabase::UnloadAsset(reloaded);
}

TEST_F(AudioMixerAssetFixture, SnapshotTransitionsMixerItBelongsTo) {
	Write("Main.mixer", str(TWO_SNAPSHOTS));
	Asset* first = AssetDatabase::LoadAsset(AssetPath("Main.mixer"));
	ASSERT_NE(first, nullptr);
	const Guid loudGuid = ProductOf(AssetDatabase::LookupGuid(AssetPath("Main.mixer")), "Snapshots/Loud");
	AssetDatabase::UnloadAsset(first);

	Asset* loaded = AssetDatabase::LoadAssetByGuid(loudGuid);
	ASSERT_NE(loaded, nullptr);
	ASSERT_EQ(loaded->GetAssetTypeId(), AudioMixerSnapshot::GetReflectType()->name);
	auto* loud = static_cast<AudioMixerSnapshot*>(loaded);
	EXPECT_EQ(loud->GetSnapshotId(), "Loud");
	ASSERT_TRUE(loud->TransitionTo(0.0f));
	AudioMixer* mixer = loud->GetAudioMixer().Get();
	ASSERT_NE(mixer, nullptr);
	EXPECT_EQ(SfxVolume(*mixer), -2.0f);
	AssetDatabase::UnloadAsset(loud);
}

TEST_F(AudioMixerAssetFixture, FoundMixerHeldUntilUnload) {
	Write("Main.mixer", str(TWO_SNAPSHOTS));
	auto* mixer = static_cast<AudioMixer*>(AssetDatabase::LoadAsset(AssetPath("Main.mixer")));
	ASSERT_NE(mixer, nullptr);
	const Guid mixerGuid = mixer->assetId;

	const Handle<AudioMixerSnapshot> loud = mixer->FindSnapshot("Loud");
	const Handle<AudioMixerSnapshot> quiet = mixer->FindSnapshot("Quiet");
	ASSERT_TRUE(loud.IsValid());
	ASSERT_TRUE(quiet.IsValid());
	EXPECT_FALSE(mixer->FindSnapshot("Nowhere").IsValid());
	const Guid loudGuid = loud->assetId;
	CollectAll();
	ASSERT_TRUE(loud.IsValid());
	EXPECT_TRUE(AudioMixerAccess::Keeps(*mixer, *loud.Get()));

	ASSERT_TRUE(loud->TransitionTo(0.0f));
	EXPECT_EQ(SfxVolume(*mixer), -2.0f);
	const Handle<AudioMixerSnapshot> both[] = {quiet, loud};
	const f32 weights[] = {1.0f, 1.0f};
	ASSERT_TRUE(mixer->TransitionToSnapshots(both, weights, 0.0f));
	EXPECT_EQ(SfxVolume(*mixer), -11.0f);

	Vector<Handle<AudioMixerGroup>> groups{GetMemoryResource(MemTag::Asset)};
	EXPECT_EQ(mixer->FindMatchingGroups("Master", groups), 2u);
	ASSERT_EQ(mixer->FindMatchingGroups("Master/SFX", groups), 1u);
	EXPECT_EQ(groups[0]->GetGroupId(), "SFX");
	EXPECT_EQ(mixer->FindMatchingGroups("Nowhere", groups), 0u);

	AssetDatabase::UnloadAsset(mixer);
	CollectAll();
	EXPECT_EQ(AssetDatabase::FindLoadedByGuid(mixerGuid), nullptr);
	EXPECT_EQ(AssetDatabase::FindLoadedByGuid(loudGuid), nullptr);
}

TEST_F(AudioMixerAssetFixture, KeptSnapshotHoldsMixerConditionally) {
	Write("Main.mixer", str(TWO_SNAPSHOTS));
	Asset* first = AssetDatabase::LoadAsset(AssetPath("Main.mixer"));
	ASSERT_NE(first, nullptr);
	const Guid mixerGuid = first->assetId;
	const Guid loudGuid = ProductOf(mixerGuid, "Snapshots/Loud");
	AssetDatabase::UnloadAsset(first);
	CollectAll();

	auto* held = static_cast<AudioMixerSnapshot*>(AssetDatabase::LoadAssetByGuid(loudGuid));
	ASSERT_NE(held, nullptr);
	ASSERT_TRUE(held->TransitionTo(0.0f));
	AudioMixer* mixer = held->GetAudioMixer().Get();
	ASSERT_NE(mixer, nullptr);
	EXPECT_EQ(mixer->FindSnapshot("Loud").Get(), held);
	EXPECT_TRUE(AudioMixerAccess::Keeps(*mixer, *held));
	RunFrame();
	EXPECT_TRUE(AudioMixerAccess::HoldsItsMixer(*held));
	AssetDatabase::UnloadAsset(held);
	RunFrame();
	CollectAll();
	EXPECT_EQ(AssetDatabase::FindLoadedByGuid(mixerGuid), nullptr);
	EXPECT_EQ(AssetDatabase::FindLoadedByGuid(loudGuid), nullptr);

	auto* loadedMixer = static_cast<AudioMixer*>(AssetDatabase::LoadAssetByGuid(mixerGuid));
	ASSERT_NE(loadedMixer, nullptr);
	const Handle<AudioMixerSnapshot> found = loadedMixer->FindSnapshot("Loud");
	ASSERT_TRUE(found.IsValid());
	ASSERT_TRUE(found->TransitionTo(0.0f));
	EXPECT_FALSE(AudioMixerAccess::HoldsItsMixer(*found.Get()));
	Asset* field = AssetDatabase::LoadAssetByGuid(loudGuid);
	ASSERT_EQ(field, found.Get());
	AssetDatabase::UnloadAsset(loadedMixer);
	RunFrame();
	CollectAll();
	EXPECT_NE(AssetDatabase::FindLoadedByGuid(mixerGuid), nullptr);
	ASSERT_TRUE(found.IsValid());
	EXPECT_TRUE(found->TransitionTo(0.0f));
	AssetDatabase::UnloadAsset(field);
	RunFrame();
	CollectAll();
	EXPECT_EQ(AssetDatabase::FindLoadedByGuid(mixerGuid), nullptr);
	EXPECT_EQ(AssetDatabase::FindLoadedByGuid(loudGuid), nullptr);
}

TEST_F(AudioMixerAssetFixture, FindResultOutlivesFirstHolder) {
	Write("Main.mixer", str(TWO_SNAPSHOTS));
	auto* mixer = static_cast<AudioMixer*>(AssetDatabase::LoadAsset(AssetPath("Main.mixer")));
	ASSERT_NE(mixer, nullptr);
	const Guid mixerGuid = mixer->assetId;
	const Guid sfxGuid = ProductOf(mixerGuid, "Groups/SFX");
	const Guid loudGuid = ProductOf(mixerGuid, "Snapshots/Loud");

	auto* output = static_cast<AudioMixerGroup*>(AssetDatabase::LoadAssetByGuid(sfxGuid));
	ASSERT_NE(output, nullptr);
	ASSERT_TRUE(output->GetBus().IsValid());
	auto* menu = static_cast<AudioMixerSnapshot*>(AssetDatabase::LoadAssetByGuid(loudGuid));
	ASSERT_NE(menu, nullptr);
	ASSERT_TRUE(menu->TransitionTo(0.0f));
	ASSERT_TRUE(AudioMixerAccess::HoldsItsMixer(*output));
	ASSERT_TRUE(AudioMixerAccess::HoldsItsMixer(*menu));

	Vector<Handle<AudioMixerGroup>> groups{GetMemoryResource(MemTag::Asset)};
	ASSERT_EQ(mixer->FindMatchingGroups("Master/SFX", groups), 1u);
	const Handle<AudioMixerGroup> foundGroup = groups[0];
	const Handle<AudioMixerSnapshot> foundSnapshot = mixer->FindSnapshot("Loud");
	ASSERT_EQ(foundGroup.Get(), output);
	ASSERT_EQ(foundSnapshot.Get(), menu);

	AssetDatabase::UnloadAsset(output);
	AssetDatabase::UnloadAsset(menu);
	RunFrame();
	CollectAll();
	EXPECT_TRUE(foundGroup.IsValid());
	EXPECT_TRUE(foundSnapshot.IsValid());

	AssetDatabase::UnloadAsset(mixer);
	RunFrame();
	CollectAll();
	EXPECT_EQ(AssetDatabase::FindLoadedByGuid(mixerGuid), nullptr);
	EXPECT_EQ(AssetDatabase::FindLoadedByGuid(sfxGuid), nullptr);
	EXPECT_EQ(AssetDatabase::FindLoadedByGuid(loudGuid), nullptr);
}

TEST_F(AudioMixerAssetFixture, PackageDownReleasesHandedOut) {
	Write("Main.mixer", str(TWO_SNAPSHOTS));
	auto* mixer = static_cast<AudioMixer*>(AssetDatabase::LoadAsset(AssetPath("Main.mixer")));
	ASSERT_NE(mixer, nullptr);
	const Guid mixerGuid = mixer->assetId;
	const Handle<AudioMixerSnapshot> found = mixer->FindSnapshot("Loud");
	ASSERT_TRUE(found.IsValid());
	const Guid loudGuid = found->assetId;

	ShutdownPackage();
	FrameStages::UnregisterOwnedBy(&_token);
	AssetDatabase::UnloadAsset(mixer);
	CollectAll();
	EXPECT_EQ(AssetDatabase::FindLoadedByGuid(mixerGuid), nullptr);
	EXPECT_EQ(AssetDatabase::FindLoadedByGuid(loudGuid), nullptr);
	SLT_CALL_LINKED_PACKAGE(Audio, &_token);
}

TEST(AudioMixerDocumentTest, GroupProductCookRoundTrips) {
	AudioMixerGroup group;
	const Guid mixer = Guid(0x0123456789abcdefull, 0xfedcba9876543210ull);
	AudioMixerAccess::InitGroup(group, mixer, "SFX");
	CookedMixer cooked;
	CookMixer(group, cooked);
	ASSERT_TRUE(cooked.cooked);
	AudioMixerGroup loaded;
	ASSERT_TRUE(LoadMixer(cooked, loaded));
	EXPECT_EQ(loaded.GetGroupId(), "SFX");
	CookedMixer again;
	CookMixer(loaded, again);
	EXPECT_EQ(again.descriptor, cooked.descriptor);
}

TEST(AudioMixerDocumentTest, SnapshotProductCookRoundTrips) {
	AudioMixerSnapshot snapshot;
	const Guid mixer = Guid(0x0123456789abcdefull, 0xfedcba9876543210ull);
	AudioMixerAccess::InitSnapshot(snapshot, mixer, "Paused");
	CookedMixer cooked;
	CookMixer(snapshot, cooked);
	ASSERT_TRUE(cooked.cooked);
	AudioMixerSnapshot loaded;
	ASSERT_TRUE(LoadMixer(cooked, loaded));
	EXPECT_EQ(loaded.GetSnapshotId(), "Paused");
	EXPECT_EQ(AudioMixerAccess::MixerOf(loaded), mixer);
}

TEST(AudioMixerDocumentTest, CookedMixerKeepsAssetsImportMade) {
	AudioMixer built;
	BuildAuthored(built);
	const Guid explore = Guid(0x1111111111111111ull, 0x2222222222222222ull);
	const Guid sfx = Guid(0x3333333333333333ull, 0x4444444444444444ull);
	AudioMixerAccess::AddProduct(built, "Snapshots/Explore", explore);
	AudioMixerAccess::AddProduct(built, "Groups/SFX", sfx);
	CookedMixer cooked;
	CookMixer(built, cooked);
	ASSERT_TRUE(cooked.cooked);
	AudioMixer loaded;
	ASSERT_TRUE(LoadMixer(cooked, loaded));
	EXPECT_EQ(AudioMixerAccess::CountProducts(loaded), 2u);
	EXPECT_EQ(AudioMixerAccess::FindProduct(loaded, "Snapshots/Explore"), explore);
	EXPECT_EQ(AudioMixerAccess::FindProduct(loaded, "Groups/SFX"), sfx);
}

namespace {

	AudioMixer::TransitionOverride MakeTransition(const char* group, AudioMixerParameter parameter, AudioMixerTransitionType type,
												  const char* target = "", const char* effectParameter = "") {
		AudioMixer::TransitionOverride row;
		row.group = group;
		row.parameter = parameter;
		row.target = target;
		row.effectParameter = effectParameter;
		row.type = type;
		return row;
	}

	AudioMixer::Snapshot& SnapshotNamed(AudioMixer& mixer, std::string_view id) {
		for (AudioMixer::Snapshot& snapshot : mixer.snapshots) {
			if (snapshot.id == id) {
				return snapshot;
			}
		}
		ADD_FAILURE() << "no snapshot " << id;
		return mixer.snapshots.front();
	}

} // namespace

TEST_F(AudioMixerFixture, TransitionOverrideShapesWalkToSnapshot) {
	AudioMixer* mixer = LiveMixer();
	const f32 from = -6.0f;
	const f32 to = -20.0f;
	const auto at = [&](f32 share) { return from + (to - from) * share; };
	const f32 quiet = std::pow(10.0f, from / 20.0f) + (std::pow(10.0f, to / 20.0f) - std::pow(10.0f, from / 20.0f)) * 0.25f;
	const std::pair<AudioMixerTransitionType, f32> cases[] = {
		{AudioMixerTransitionType::Lerp, at(0.25f)},
		{AudioMixerTransitionType::Smoothstep, at(0.25f * 0.25f * (3.0f - 0.5f))},
		{AudioMixerTransitionType::Squared, at(0.0625f)},
		{AudioMixerTransitionType::SquareRoot, at(0.5f)},
		{AudioMixerTransitionType::BrickwallStart, to},
		{AudioMixerTransitionType::BrickwallEnd, from},
		{AudioMixerTransitionType::Attenuation, 20.0f * std::log10(quiet)},
	};
	for (const auto& [type, quarter] : cases) {
		ASSERT_TRUE(mixer->TransitionToSnapshot("Explore", 0.0f));
		SnapshotNamed(*mixer, "Combat").transitions.clear();
		SnapshotNamed(*mixer, "Combat").transitions.push_back(MakeTransition("Music", AudioMixerParameter::Volume, type));
		ASSERT_TRUE(mixer->TransitionToSnapshot("Combat", 0.4f));
		RunFrame(1.0f, true, 0.1f);
		EXPECT_NEAR(Parameter(*mixer, "MusicVolume"), quarter, 1e-3f) << "type " << static_cast<u32>(type);
		for (u32 frame = 0; frame < 3; ++frame) {
			RunFrame(1.0f, true, 0.1f);
		}
		EXPECT_EQ(Parameter(*mixer, "MusicVolume"), to) << "type " << static_cast<u32>(type);
	}
}

TEST_F(AudioMixerFixture, WeightedTransitionMovesAsHeaviestSnapshotSays) {
	AudioMixer* mixer = LiveMixer();
	SnapshotNamed(*mixer, "Explore").transitions.push_back(MakeTransition("Music", AudioMixerParameter::Volume, AudioMixerTransitionType::BrickwallStart));
	SnapshotNamed(*mixer, "Combat").transitions.push_back(MakeTransition("Music", AudioMixerParameter::Volume, AudioMixerTransitionType::BrickwallEnd));
	const std::string_view both[] = {"Explore", "Combat"};
	const f32 combatHeavy[] = {0.25f, 0.75f};
	ASSERT_TRUE(mixer->TransitionToSnapshots(both, combatHeavy, 0.4f));
	RunFrame(1.0f, true, 0.1f);
	EXPECT_EQ(Parameter(*mixer, "MusicVolume"), -6.0f);
	for (u32 frame = 0; frame < 3; ++frame) {
		RunFrame(1.0f, true, 0.1f);
	}
	const f32 exploreHeavy[] = {0.75f, 0.25f};
	ASSERT_TRUE(mixer->TransitionToSnapshots(both, exploreHeavy, 0.4f));
	RunFrame(1.0f, true, 0.1f);
	EXPECT_NEAR(Parameter(*mixer, "MusicVolume"), -9.5f, 1e-3f);
}

TEST(AudioMixerDocumentTest, TransitionOverridesSurviveDocumentAndCook) {
	ScratchDirectory scratch;
	AudioMixer first;
	BuildAuthored(first);
	first.groups[1].units.push_back(AudioMixer::Unit{});
	first.groups[1].units.back().kind = AudioMixerUnitKind::Effect;
	first.groups[1].units.back().id = "Echo";
	first.groups[1].units.back().effect = "Echo";
	Vector<AudioMixer::TransitionOverride>& combat = SnapshotNamed(first, "Combat").transitions;
	combat.push_back(MakeTransition("Music", AudioMixerParameter::EffectParameter, AudioMixerTransitionType::Attenuation, "Echo", "Delay"));
	combat.push_back(MakeTransition("SFX", AudioMixerParameter::SendLevel, AudioMixerTransitionType::SquareRoot, "Reverb"));
	combat.push_back(MakeTransition("Music", AudioMixerParameter::Volume, AudioMixerTransitionType::Smoothstep));
	AudioMixer second;
	BuildAuthored(second);
	second.groups[1].units.push_back(first.groups[1].units.back());
	Vector<AudioMixer::TransitionOverride>& reversed = SnapshotNamed(second, "Combat").transitions;
	reversed.assign(combat.rbegin(), combat.rend());
	const str document = Saved(first, scratch, "first.mixer");
	EXPECT_EQ(Saved(second, scratch, "second.mixer"), document);

	AudioMixer read;
	ASSERT_TRUE(read.Import(scratch.File("first.mixer")));
	const Vector<AudioMixer::TransitionOverride>& back = SnapshotNamed(read, "Combat").transitions;
	ASSERT_EQ(back.size(), 3u);
	EXPECT_EQ(back[0].parameter, AudioMixerParameter::Volume);
	EXPECT_EQ(back[0].type, AudioMixerTransitionType::Smoothstep);
	EXPECT_EQ(back[2].effectParameter, "Delay");
	EXPECT_EQ(back[2].type, AudioMixerTransitionType::Attenuation);
	EXPECT_TRUE(SnapshotNamed(read, "Explore").transitions.empty());

	CookedMixer cooked;
	CookMixer(read, cooked);
	ASSERT_TRUE(cooked.cooked);
	AudioMixer loaded;
	ASSERT_TRUE(LoadMixer(cooked, loaded));
	const Vector<AudioMixer::TransitionOverride>& played = SnapshotNamed(loaded, "Combat").transitions;
	ASSERT_EQ(played.size(), 3u);
	for (size_t index = 0; index < 3; ++index) {
		EXPECT_EQ(played[index].group, back[index].group);
		EXPECT_EQ(played[index].parameter, back[index].parameter);
		EXPECT_EQ(played[index].target, back[index].target);
		EXPECT_EQ(played[index].type, back[index].type);
	}

	SnapshotNamed(second, "Combat").transitions.push_back(MakeTransition("Nowhere", AudioMixerParameter::Volume, AudioMixerTransitionType::Squared));
	str error;
	EXPECT_FALSE(ValidateMixer(second, error));
}

TEST(AudioMixerDocumentTest, ColorsAndViewsSurviveDocumentAndCook) {
	ScratchDirectory scratch;
	AudioMixer plain;
	BuildAuthored(plain);
	const str plainDocument = Saved(plain, scratch, "plain.mixer");
	EXPECT_EQ(plainDocument.find("\"color\""), str::npos);
	EXPECT_EQ(plainDocument.find("\"views\""), str::npos);

	AudioMixer first;
	BuildAuthored(first);
	first.groups[1].color = AudioMixerGroupColor::Magenta;
	first.groups[2].color = AudioMixerGroupColor::Green;
	first.views.push_back("All");
	first.views.push_back("Music only");
	for (AudioMixer::Group& group : first.groups) {
		group.shownIn = 1;
	}
	first.groups[1].shownIn = 3;
	const str document = Saved(first, scratch, "first.mixer");
	EXPECT_NE(document.find("\"Music only\""), str::npos);

	AudioMixer read;
	ASSERT_TRUE(read.Import(scratch.File("first.mixer")));
	auto same = [&](const AudioMixer& mixer, const char* where) {
		ASSERT_EQ(mixer.groups.size(), first.groups.size()) << where;
		for (size_t index = 0; index < mixer.groups.size(); ++index) {
			EXPECT_EQ(mixer.groups[index].color, first.groups[index].color) << where << ": group " << index;
			EXPECT_EQ(mixer.groups[index].shownIn, first.groups[index].shownIn) << where << ": group " << index;
		}
		ASSERT_EQ(mixer.views.size(), 2u) << where;
		EXPECT_EQ(mixer.views[1], "Music only") << where;
	};
	same(read, "the document");
	CookedMixer cooked;
	CookMixer(read, cooked);
	ASSERT_TRUE(cooked.cooked);
	AudioMixer loaded;
	ASSERT_TRUE(LoadMixer(cooked, loaded));
	same(loaded, "the cook");

	const str stale = R"({ "groups": [ { "id": "Master" } ], "snapshots": [ { "name": "Snapshot" } ],
		"views": [ { "name": "V", "groups": [ "Gone", "Master" ] } ] })";
	FileSystem::WriteTextFile(scratch.File("stale.mixer"), stale);
	AudioMixer kept;
	ASSERT_TRUE(kept.Import(scratch.File("stale.mixer")));
	EXPECT_EQ(kept.groups[0].shownIn, 1u);
	EXPECT_EQ(Saved(kept, scratch, "stale-again.mixer").find("Gone"), str::npos);

	const str odd = R"({ "groups": [ { "id": "Master", "color": "Teal" } ], "snapshots": [ { "name": "Snapshot" } ] })";
	FileSystem::WriteTextFile(scratch.File("odd.mixer"), odd);
	AudioMixer refused;
	EXPECT_FALSE(refused.Import(scratch.File("odd.mixer")));
}

TEST_F(AudioMixerFixture, ParameterUnexposedGoesBackToSnapshots) {
	AudioMixer* mixer = LiveMixer();
	ASSERT_TRUE(mixer->SetFloat("MusicVolume", -3.0f));
	f32 heard = 0.0f;
	const AudioMixerEditOps::ParameterRef music{"Music", AudioMixerParameter::Volume, {}, {}};
	ASSERT_TRUE(AudioMixerEditOps::GetLiveValue(*mixer, music, heard));
	EXPECT_EQ(heard, -3.0f);
	mixer->exposedParameters.clear();
	ASSERT_TRUE(mixer->ApplyChanges());
	ASSERT_TRUE(AudioMixerEditOps::GetLiveValue(*mixer, music, heard));
	EXPECT_EQ(heard, -6.0f);
}

#else

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Core/Settings.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

	class AudioMixerPackageFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		ModuleHandle _module;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_audio_mixer_shared_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root / "Assets");
			FileSystem::Mount("project", Path(_root.generic_string()));
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());
			FileSystem::WriteTextFile(_paths.project, str("[Audio]\ndevice = None\n"));

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			_module = ModuleSystem::Load(Path("Audio.dll"));
			ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			AssetDatabase::Terminate();
			SceneSystem::Terminate();
			ModuleSystem::Terminate();
			Settings::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}
	};

} // namespace

TEST_F(AudioMixerPackageFixture, SharedPackageMintsGroupsAndSnapshots) {
	const Path path("project://Assets/Main.mixer");
	FileSystem::WriteTextFile(path, str(R"({
	"groups": [ { "id": "Master" }, { "id": "Music", "parent": "Master" }, { "id": "SFX", "parent": "Master" } ],
	"snapshots": [ { "name": "Snapshot", "values": [ { "group": "Music", "parameter": "Volume", "value": -6 } ] } ]
})"));
	Asset* mixer = AssetDatabase::LoadAsset(path);
	ASSERT_NE(mixer, nullptr);
	EXPECT_EQ(mixer->GetAssetTypeId(), StringHash("AudioMixer"));

	Vector<AssetProduct> products{GetMemoryResource(MemTag::Asset)};
	AssetDatabase::GetProducts(AssetDatabase::LookupGuid(path), products);
	ASSERT_EQ(products.size(), 4u);
	struct Expected {
		const char* key;
		const char* type;
	};
	for (const Expected expected : {Expected{"Groups/Master", "AudioMixerGroup"}, Expected{"Groups/Music", "AudioMixerGroup"},
									Expected{"Groups/SFX", "AudioMixerGroup"}, Expected{"Snapshots/Snapshot", "AudioMixerSnapshot"}}) {
		bool found = false;
		for (const AssetProduct& product : products) {
			if (product.key == expected.key) {
				found = true;
				EXPECT_EQ(product.type, StringHash(expected.type)) << expected.key;
			}
		}
		EXPECT_TRUE(found) << "no product " << expected.key;
	}

	const Path once("project://Assets/Once.mixer");
	ASSERT_TRUE(mixer->Export(once));
	const str first = FileSystem::ReadTextFile(once);
	AssetDatabase::UnloadAsset(mixer);
	Asset* again = AssetDatabase::LoadAsset(once);
	ASSERT_NE(again, nullptr);
	const Path twice("project://Assets/Twice.mixer");
	ASSERT_TRUE(again->Export(twice));
	EXPECT_EQ(FileSystem::ReadTextFile(twice), first);
	AssetDatabase::UnloadAsset(again);
}

#endif
