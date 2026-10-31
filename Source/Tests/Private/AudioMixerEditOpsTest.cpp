#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioBus.h"
#include "AudioEffectRegistry.h"
#include "AudioLiveFixture.h"
#include "AudioMixerEditOps.h"
#include "AudioMixerLive.h"
#include "Effects/BuiltinEffects.h"

#include "Shalltear/Audio/AudioMixer.h"

#include "Shalltear/Core/FileSystem.h"

#include <chrono>
#include <cmath>
#include <filesystem>

using namespace Shalltear;
using namespace Shalltear::AudioImpl;

namespace Ops = Shalltear::AudioImpl::AudioMixerEditOps;

namespace {

	AudioMixer::Group Group(const char* id, const char* parent = "", AudioMixerGroupClock clock = AudioMixerGroupClock::Wall) {
		AudioMixer::Group group;
		group.id = id;
		group.name = id;
		group.parent = parent;
		group.clock = clock;
		group.units.push_back(AudioMixer::Unit{});
		return group;
	}

	AudioMixer::Value Value(const char* group, AudioMixerParameter parameter, f32 value, const char* target = "") {
		AudioMixer::Value row;
		row.group = group;
		row.parameter = parameter;
		row.target = target;
		row.value = value;
		return row;
	}

	void Build(AudioMixer& mixer) {
		mixer.groups.push_back(Group("Master"));
		mixer.groups.push_back(Group("Music", "Master"));
		AudioMixer::Group sfx = Group("SFX", "Master", AudioMixerGroupClock::Gameplay);
		AudioMixer::Unit send;
		send.kind = AudioMixerUnitKind::Send;
		send.target = "Reverb";
		sfx.units.insert(sfx.units.begin(), send);
		mixer.groups.push_back(std::move(sfx));
		mixer.groups.push_back(Group("Reverb", "Master"));
		mixer.groups.push_back(Group("Footsteps", "SFX", AudioMixerGroupClock::Gameplay));
		mixer.groups.push_back(Group("UI", "Master"));

		AudioMixer::Snapshot explore;
		explore.id = "Explore";
		explore.name = "Explore";
		explore.values.push_back(Value("Music", AudioMixerParameter::Volume, -6.0f));
		explore.values.push_back(Value("Footsteps", AudioMixerParameter::Volume, -3.0f));
		mixer.snapshots.push_back(std::move(explore));
		AudioMixer::Snapshot combat;
		combat.id = "Combat";
		combat.name = "Combat";
		combat.values.push_back(Value("SFX", AudioMixerParameter::SendLevel, -10.0f, "Reverb"));
		combat.values.push_back(Value("Music", AudioMixerParameter::Volume, -20.0f));
		mixer.snapshots.push_back(std::move(combat));
		mixer.startSnapshot = "Explore";

		AudioMixer::ExposedParameter music;
		music.name = "MusicVolume";
		music.group = "Music";
		mixer.exposedParameters.push_back(music);
		AudioMixer::ExposedParameter wet;
		wet.name = "ReverbSend";
		wet.group = "SFX";
		wet.parameter = AudioMixerParameter::SendLevel;
		wet.target = "Reverb";
		mixer.exposedParameters.push_back(wet);
	}

	::testing::AssertionResult Valid(const AudioMixer& mixer) {
		str error;
		if (ValidateMixer(mixer, error)) {
			return ::testing::AssertionSuccess();
		}
		return ::testing::AssertionFailure() << "edit left a non mixer: " << error;
	}

	str Tree(const AudioMixer& mixer) {
		Vector<Ops::GroupEntry> entries{GetMemoryResource(MemTag::Audio)};
		Ops::Flatten(mixer, entries);
		str out;
		for (const Ops::GroupEntry& entry : entries) {
			out += std::format("{}{}@{}", out.empty() ? "" : " ", mixer.groups[entry.index].id, entry.depth);
		}
		return out;
	}

	const AudioMixer::Group* Find(const AudioMixer& mixer, std::string_view id) {
		const i32 index = FindGroup(mixer, id);
		return index >= 0 ? &mixer.groups[static_cast<size_t>(index)] : nullptr;
	}

	u32 ValuesOf(const AudioMixer& mixer, std::string_view group) {
		u32 count = 0;
		for (const AudioMixer::Snapshot& snapshot : mixer.snapshots) {
			for (const AudioMixer::Value& value : snapshot.values) {
				count += value.group == group ? 1u : 0u;
			}
		}
		return count;
	}

} // namespace

TEST(AudioMixerEditOpsTest, TreeIsDepthFirstInDatasOrder) {
	AudioMixer mixer;
	Build(mixer);
	EXPECT_EQ(Tree(mixer), "Master@0 Music@1 SFX@1 Footsteps@2 Reverb@1 UI@1");
	Vector<Ops::GroupEntry> entries{GetMemoryResource(MemTag::Audio)};
	Ops::Flatten(mixer, entries);
	ASSERT_EQ(entries.size(), 6u);
	EXPECT_TRUE(entries[0].hasChildren);
	EXPECT_TRUE(entries[2].hasChildren);
	EXPECT_FALSE(entries[1].hasChildren);
}

TEST(AudioMixerEditOpsTest, AddedGroupIsLastChild) {
	AudioMixer mixer;
	Build(mixer);
	const str first = Ops::AddGroup(mixer, "SFX");
	const str second = Ops::AddGroup(mixer, "SFX");
	EXPECT_EQ(first, "New Group");
	EXPECT_EQ(second, "New Group 2");
	EXPECT_EQ(Find(mixer, second)->name, "New Group");
	EXPECT_EQ(Tree(mixer), "Master@0 Music@1 SFX@1 Footsteps@2 New Group@2 New Group 2@2 Reverb@1 UI@1");
	ASSERT_EQ(Find(mixer, first)->units.size(), 1u);
	EXPECT_EQ(Find(mixer, first)->units[0].kind, AudioMixerUnitKind::Attenuation);
	EXPECT_TRUE(Valid(mixer));

	EXPECT_TRUE(Ops::AddGroup(mixer, "Nowhere").empty());
	const str slashed = Ops::AddGroup(mixer, "Master", "Voice/Chat");
	EXPECT_EQ(slashed.find('/'), str::npos);
	EXPECT_EQ(Find(mixer, slashed)->name, "Voice/Chat");
	EXPECT_TRUE(Valid(mixer));
}

TEST(AudioMixerEditOpsTest, AnAddedGroupFollowsItsParentsClock) {
	AudioMixer mixer;
	Build(mixer);
	mixer.groups[static_cast<size_t>(FindGroup(mixer, "SFX"))].followTimeScale = true;
	const str under = Ops::AddGroup(mixer, "SFX");
	EXPECT_EQ(Find(mixer, under)->clock, AudioMixerGroupClock::Gameplay);
	EXPECT_TRUE(Find(mixer, under)->followTimeScale);
	const str beside = Ops::AddGroup(mixer, "Master");
	EXPECT_EQ(Find(mixer, beside)->clock, AudioMixerGroupClock::Wall);
}

TEST(AudioMixerEditOpsTest, DuplicateIsWholeSubtreeWithValues) {
	AudioMixer mixer;
	Build(mixer);
	AudioMixer::Group& footsteps = mixer.groups[static_cast<size_t>(FindGroup(mixer, "Footsteps"))];
	AudioMixer::Unit out;
	out.kind = AudioMixerUnitKind::Send;
	out.target = "Reverb";
	footsteps.units.push_back(out);
	ASSERT_TRUE(Valid(mixer));

	Vector<str> copies{GetMemoryResource(MemTag::Audio)};
	const str roots[] = {str("SFX"), str("Footsteps")};
	Ops::DuplicateGroups(mixer, roots, 0, copies);
	ASSERT_EQ(copies.size(), 1u);
	const AudioMixer::Group* copy = Find(mixer, copies[0]);
	ASSERT_NE(copy, nullptr);
	EXPECT_EQ(copy->name, "SFX - Copy");
	EXPECT_EQ(copy->parent, "Master");
	EXPECT_EQ(copy->clock, AudioMixerGroupClock::Gameplay);
	EXPECT_EQ(Tree(mixer), "Master@0 Music@1 SFX@1 Footsteps@2 Reverb@1 UI@1 SFX - Copy@1 Footsteps 2@2");

	const AudioMixer::Group* inner = Find(mixer, "Footsteps 2");
	ASSERT_NE(inner, nullptr);
	EXPECT_EQ(inner->name, "Footsteps");
	EXPECT_EQ(inner->parent, copies[0]);
	ASSERT_EQ(copy->units.size(), 2u);
	EXPECT_EQ(copy->units[0].target, "Reverb");
	EXPECT_EQ(inner->units.back().target, "Reverb");

	EXPECT_EQ(ValuesOf(mixer, copies[0]), 1u);
	EXPECT_EQ(ValuesOf(mixer, "Footsteps 2"), 1u);
	EXPECT_EQ(SnapshotValue(mixer, 0, "Footsteps 2", AudioMixerParameter::Volume, {}, {}), -3.0f);
	EXPECT_EQ(SnapshotValue(mixer, 1, copies[0], AudioMixerParameter::SendLevel, "Reverb", {}), -10.0f);
	EXPECT_EQ(mixer.exposedParameters.size(), 2u);
	EXPECT_TRUE(Valid(mixer));

	const str top[] = {str("Master")};
	Ops::DuplicateGroups(mixer, top, 0, copies);
	EXPECT_TRUE(copies.empty());
}

TEST(AudioMixerEditOpsTest, CopiedSendPointsIntoCopy) {
	AudioMixer mixer;
	Build(mixer);
	mixer.groups.push_back(Group("Impacts", "SFX", AudioMixerGroupClock::Gameplay));
	AudioMixer::Group& footsteps = mixer.groups[static_cast<size_t>(FindGroup(mixer, "Footsteps"))];
	AudioMixer::Unit inside;
	inside.kind = AudioMixerUnitKind::Send;
	inside.target = "Impacts";
	footsteps.units.push_back(inside);
	ASSERT_TRUE(Valid(mixer));

	Vector<str> copies{GetMemoryResource(MemTag::Audio)};
	const str roots[] = {str("SFX")};
	Ops::DuplicateGroups(mixer, roots, 0, copies);
	ASSERT_EQ(copies.size(), 1u);
	EXPECT_EQ(Find(mixer, "Footsteps 2")->units.back().target, "Impacts 2");
	EXPECT_EQ(Find(mixer, "Footsteps")->units.back().target, "Impacts");
	EXPECT_TRUE(Valid(mixer));
}

TEST(AudioMixerEditOpsTest, GroupRemovalTakesDependents) {
	AudioMixer mixer;
	Build(mixer);
	const str going[] = {str("Reverb"), str("SFX")};
	EXPECT_EQ(Ops::RemoveGroups(mixer, going), 3u);
	EXPECT_EQ(Tree(mixer), "Master@0 Music@1 UI@1");
	EXPECT_EQ(ValuesOf(mixer, "Footsteps"), 0u);
	EXPECT_EQ(ValuesOf(mixer, "SFX"), 0u);
	ASSERT_EQ(mixer.exposedParameters.size(), 1u);
	EXPECT_EQ(mixer.exposedParameters[0].name, "MusicVolume");
	EXPECT_TRUE(Valid(mixer));

	const str top[] = {str("Master")};
	EXPECT_EQ(Ops::RemoveGroups(mixer, top), 0u);
}

TEST(AudioMixerEditOpsTest, TargetRemovalDropsSend) {
	AudioMixer mixer;
	Build(mixer);
	const str going[] = {str("Reverb")};
	EXPECT_EQ(Ops::RemoveGroups(mixer, going), 1u);
	const AudioMixer::Group* sfx = Find(mixer, "SFX");
	ASSERT_NE(sfx, nullptr);
	ASSERT_EQ(sfx->units.size(), 1u);
	EXPECT_EQ(sfx->units[0].kind, AudioMixerUnitKind::Attenuation);
	EXPECT_EQ(mixer.exposedParameters.size(), 1u);
	EXPECT_TRUE(Valid(mixer));
}

TEST(AudioMixerEditOpsTest, MovedGroupNeverUnderItself) {
	AudioMixer mixer;
	Build(mixer);
	EXPECT_TRUE(Ops::MoveGroup(mixer, "UI", "Master", 0));
	EXPECT_EQ(Tree(mixer), "Master@0 UI@1 Music@1 SFX@1 Footsteps@2 Reverb@1");
	EXPECT_TRUE(Ops::MoveGroup(mixer, "UI", "Master", 99));
	EXPECT_EQ(Tree(mixer), "Master@0 Music@1 SFX@1 Footsteps@2 Reverb@1 UI@1");
	EXPECT_TRUE(Ops::MoveGroup(mixer, "Music", "SFX", 0));
	EXPECT_EQ(Tree(mixer), "Master@0 SFX@1 Music@2 Footsteps@2 Reverb@1 UI@1");
	EXPECT_TRUE(Valid(mixer));

	EXPECT_FALSE(Ops::MoveGroup(mixer, "SFX", "Footsteps", 0));
	EXPECT_FALSE(Ops::MoveGroup(mixer, "SFX", "SFX", 0));
	EXPECT_FALSE(Ops::MoveGroup(mixer, "Master", "UI", 0));
	EXPECT_FALSE(Ops::MoveGroup(mixer, "Reverb", "SFX", 0));
	EXPECT_EQ(Tree(mixer), "Master@0 SFX@1 Music@2 Footsteps@2 Reverb@1 UI@1");
	EXPECT_TRUE(Valid(mixer));
}

TEST(AudioMixerEditOpsTest, ARenameKeepsTheIdentity) {
	AudioMixer mixer;
	Build(mixer);
	EXPECT_TRUE(Ops::RenameGroup(mixer, "Music", "Score"));
	EXPECT_EQ(Find(mixer, "Music")->name, "Score");
	EXPECT_FALSE(Ops::RenameGroup(mixer, "Music", ""));
	EXPECT_FALSE(Ops::RenameGroup(mixer, "Nowhere", "x"));
	EXPECT_TRUE(Valid(mixer));
}

TEST(AudioMixerEditOpsTest, TheFaderScaleInvertsItself) {
	EXPECT_EQ(Ops::DecibelsToFader(20.0f), 0.0f);
	EXPECT_EQ(Ops::DecibelsToFader(0.0f), Ops::FADER_ZERO);
	EXPECT_EQ(Ops::DecibelsToFader(-80.0f), 1.0f);
	EXPECT_EQ(Ops::DecibelsToFader(-200.0f), 1.0f);
	EXPECT_NEAR(Ops::DecibelsToFader(-20.0f), 0.25f + std::pow(0.25f, 1.0f / 1.7f) * 0.75f, 1e-6f);
	f32 previous = -1.0f;
	for (f32 decibels = 20.0f; decibels >= -80.0f; decibels -= 0.5f) {
		const f32 share = Ops::DecibelsToFader(decibels);
		EXPECT_GT(share, previous) << decibels;
		previous = share;
		EXPECT_NEAR(Ops::FaderToDecibels(share), decibels, 1e-3f);
	}
}

TEST(AudioMixerEditOpsTest, TheFaderWritesTheSnapshotBeingEdited) {
	AudioMixer mixer;
	Build(mixer);
	EXPECT_EQ(Ops::GetGroupVolume(mixer, 0, "UI"), 0.0f);
	EXPECT_TRUE(Ops::SetGroupVolume(mixer, 1, "UI", -12.5f));
	EXPECT_EQ(Ops::GetGroupVolume(mixer, 1, "UI"), -12.5f);
	EXPECT_EQ(Ops::GetGroupVolume(mixer, 0, "UI"), 0.0f);
	EXPECT_TRUE(Ops::SetGroupVolume(mixer, 0, "Music", 40.0f));
	EXPECT_EQ(Ops::GetGroupVolume(mixer, 0, "Music"), 20.0f);
	EXPECT_EQ(ValuesOf(mixer, "Music"), 2u);
	EXPECT_FALSE(Ops::SetGroupVolume(mixer, 2, "UI", 0.0f));
	EXPECT_FALSE(Ops::SetGroupVolume(mixer, 0, "Nowhere", 0.0f));
	EXPECT_TRUE(Valid(mixer));
}

TEST(AudioMixerEditOpsTest, WindowAndScriptSaveSameBytes) {
	AudioMixer windowed;
	windowed.groups.push_back(Group("Master"));
	AudioMixer::Snapshot first;
	first.id = "Snapshot";
	first.name = "Snapshot";
	windowed.snapshots.push_back(first);
	const str music = Ops::AddGroup(windowed, "Master");
	ASSERT_TRUE(Ops::RenameGroup(windowed, music, "Music"));
	const str stings = Ops::AddGroup(windowed, music);
	ASSERT_TRUE(Ops::RenameGroup(windowed, stings, "Stings"));
	ASSERT_TRUE(Ops::SetGroupVolume(windowed, 0, stings, -12.0f));
	ASSERT_TRUE(Valid(windowed));

	AudioMixer scripted;
	scripted.groups.push_back(Group("Master"));
	AudioMixer::Group musicGroup = Group("New Group", "Master");
	musicGroup.name = "Music";
	scripted.groups.push_back(std::move(musicGroup));
	AudioMixer::Group stingsGroup = Group("New Group 2", "New Group");
	stingsGroup.name = "Stings";
	scripted.groups.push_back(std::move(stingsGroup));
	AudioMixer::Snapshot snapshot;
	snapshot.id = "Snapshot";
	snapshot.name = "Snapshot";
	snapshot.values.push_back(Value("New Group 2", AudioMixerParameter::Volume, -12.0f));
	scripted.snapshots.push_back(std::move(snapshot));

	const std::filesystem::path scratch = std::filesystem::temp_directory_path() /
										  ("shalltear_mixer_ops_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(scratch);
	const Path windowPath((scratch / "window.mixer").generic_string());
	const Path scriptPath((scratch / "script.mixer").generic_string());
	ASSERT_TRUE(windowed.Export(windowPath));
	ASSERT_TRUE(scripted.Export(scriptPath));
	const str windowBytes = FileSystem::ReadTextFile(windowPath);
	EXPECT_EQ(windowBytes, FileSystem::ReadTextFile(scriptPath));
	EXPECT_NE(windowBytes.find("\"name\": \"Stings\""), str::npos);
	std::error_code ec;
	std::filesystem::remove_all(scratch, ec);
}

TEST(AudioMixerEditOpsTest, MeterDecibelsFlooredAtFader) {
	EXPECT_EQ(Ops::LevelToDecibels(1.0f), 0.0f);
	EXPECT_NEAR(Ops::LevelToDecibels(0.5f), -6.0206f, 1e-4f);
	EXPECT_EQ(Ops::LevelToDecibels(0.0f), -80.0f);
	EXPECT_EQ(Ops::LevelToDecibels(1e-9f), -80.0f);
}

namespace {

	class AudioMixerChainFixture : public AudioTesting::LivePackageFixture {};

	Ops::ParameterRef Ref(std::string_view group, AudioMixerParameter parameter, std::string_view target = {}, std::string_view effectParameter = {}) {
		return {str(group), parameter, str(target), str(effectParameter)};
	}

	str Chain(const AudioMixer& mixer, std::string_view group) {
		str out;
		for (const AudioMixer::Unit& unit : Find(mixer, group)->units) {
			str part;
			switch (unit.kind) {
			case AudioMixerUnitKind::Attenuation:
				part = "Attenuation";
				break;
			case AudioMixerUnitKind::Send:
				part = "Send>" + unit.target + (unit.targetUnit.empty() ? str() : "/" + unit.targetUnit);
				break;
			case AudioMixerUnitKind::Receive:
				part = unit.id;
				break;
			case AudioMixerUnitKind::Effect:
				part = unit.id;
				break;
			}
			out += (out.empty() ? "" : " ") + part;
		}
		return out;
	}

	u32 CountValues(const AudioMixer& mixer, std::string_view group, AudioMixerParameter parameter) {
		u32 count = 0;
		for (const AudioMixer::Snapshot& snapshot : mixer.snapshots) {
			for (const AudioMixer::Value& value : snapshot.values) {
				count += value.group == group && value.parameter == parameter ? 1u : 0u;
			}
		}
		return count;
	}

	bool Exposes(const AudioMixer& mixer, std::string_view name) {
		for (const AudioMixer::ExposedParameter& exposed : mixer.exposedParameters) {
			if (exposed.name == name) {
				return true;
			}
		}
		return false;
	}

} // namespace

TEST(AudioMixerEditOpsTest, NewSendFeedsNothingUntilAimed) {
	AudioMixer mixer;
	Build(mixer);
	ASSERT_EQ(Ops::AddUnit(mixer, "Music", 0, AudioMixerUnitKind::Send), 0);
	ASSERT_EQ(Ops::AddUnit(mixer, "Music", 1, AudioMixerUnitKind::Send), 1);
	EXPECT_EQ(Chain(mixer, "Music"), "Send> Send> Attenuation");
	EXPECT_TRUE(Valid(mixer));
	EXPECT_FALSE(ParameterExists(mixer, "Music", AudioMixerParameter::SendLevel, "", ""));
	EXPECT_FALSE(Ops::SetValue(mixer, 0, Ref("Music", AudioMixerParameter::SendLevel), -6.0f));

	const std::filesystem::path scratch = std::filesystem::temp_directory_path() /
										  ("shalltear_mixer_send_" + std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
	std::filesystem::create_directories(scratch);
	const Path path((scratch / "sends.mixer").generic_string());
	ASSERT_TRUE(mixer.Export(path));
	AudioMixer read;
	ASSERT_TRUE(read.Import(path));
	EXPECT_EQ(Chain(read, "Music"), "Send> Send> Attenuation");
	std::error_code ec;
	std::filesystem::remove_all(scratch, ec);
}

TEST(AudioMixerEditOpsTest, AddedUnitPlacedWithFreeId) {
	AudioMixer mixer;
	Build(mixer);
	EXPECT_EQ(Ops::AddUnit(mixer, "UI", 0, AudioMixerUnitKind::Effect, "Echo"), 0);
	EXPECT_EQ(Ops::AddUnit(mixer, "UI", 99, AudioMixerUnitKind::Effect, "Echo"), 2);
	EXPECT_EQ(Ops::AddUnit(mixer, "UI", 1, AudioMixerUnitKind::Receive), 1);
	EXPECT_EQ(Chain(mixer, "UI"), "Echo Receive Attenuation Echo 2");
	EXPECT_EQ(Ops::AddUnit(mixer, "UI", 0, AudioMixerUnitKind::Attenuation), -1);
	EXPECT_EQ(Ops::AddUnit(mixer, "UI", 0, AudioMixerUnitKind::Effect, ""), -1);
	EXPECT_EQ(Ops::AddUnit(mixer, "Nobody", 0, AudioMixerUnitKind::Receive), -1);
	while (Find(mixer, "UI")->units.size() < MAX_BUS_ROWS) {
		ASSERT_GE(Ops::AddUnit(mixer, "UI", 0, AudioMixerUnitKind::Receive), 0);
	}
	EXPECT_EQ(Ops::AddUnit(mixer, "UI", 0, AudioMixerUnitKind::Receive), -1);
	EXPECT_TRUE(Valid(mixer));
}

TEST(AudioMixerEditOpsTest, MovedUnitLandsBeforeTarget) {
	AudioMixer mixer;
	Build(mixer);
	ASSERT_EQ(Ops::AddUnit(mixer, "SFX", 2, AudioMixerUnitKind::Effect, "Echo"), 2);
	EXPECT_EQ(Chain(mixer, "SFX"), "Send>Reverb Attenuation Echo");
	EXPECT_TRUE(Ops::MoveUnit(mixer, "SFX", 2, 0));
	EXPECT_EQ(Chain(mixer, "SFX"), "Echo Send>Reverb Attenuation");
	EXPECT_FALSE(Ops::MoveUnit(mixer, "SFX", 0, 0));
	EXPECT_FALSE(Ops::MoveUnit(mixer, "SFX", 0, 1));
	EXPECT_TRUE(Ops::MoveUnit(mixer, "SFX", 0, 3));
	EXPECT_EQ(Chain(mixer, "SFX"), "Send>Reverb Attenuation Echo");
	EXPECT_TRUE(Valid(mixer));
}

TEST(AudioMixerEditOpsTest, UnitRemovalDisconnectsSends) {
	AudioMixer mixer;
	Build(mixer);
	ASSERT_EQ(Ops::AddUnit(mixer, "Reverb", 0, AudioMixerUnitKind::Receive), 0);
	ASSERT_TRUE(Ops::RetargetSend(mixer, "SFX", 0, "Reverb", "Receive"));
	ASSERT_EQ(Ops::AddUnit(mixer, "Music", 1, AudioMixerUnitKind::Effect, "Echo"), 1);
	ASSERT_TRUE(Ops::SetWetMixing(mixer, "Music", 1, true));
	mixer.snapshots[0].values.push_back(Value("Music", AudioMixerParameter::WetLevel, 40.0f, "Echo"));
	AudioMixer::Value delay = Value("Music", AudioMixerParameter::EffectParameter, 250.0f, "Echo");
	delay.effectParameter = "Delay";
	mixer.snapshots[1].values.push_back(delay);
	AudioMixer::ExposedParameter wet;
	wet.name = "EchoWet";
	wet.group = "Music";
	wet.parameter = AudioMixerParameter::WetLevel;
	wet.target = "Echo";
	mixer.exposedParameters.push_back(wet);
	ASSERT_TRUE(Valid(mixer));

	EXPECT_FALSE(Ops::RemoveUnit(mixer, "Music", 0));
	ASSERT_TRUE(Ops::RemoveUnit(mixer, "Music", 1));
	EXPECT_EQ(Chain(mixer, "Music"), "Attenuation");
	EXPECT_EQ(CountValues(mixer, "Music", AudioMixerParameter::WetLevel), 0u);
	EXPECT_EQ(CountValues(mixer, "Music", AudioMixerParameter::EffectParameter), 0u);
	EXPECT_FALSE(Exposes(mixer, "EchoWet"));

	ASSERT_TRUE(Ops::RemoveUnit(mixer, "Reverb", 0));
	EXPECT_EQ(Chain(mixer, "SFX"), "Send> Attenuation");
	EXPECT_EQ(CountValues(mixer, "SFX", AudioMixerParameter::SendLevel), 0u);
	EXPECT_FALSE(Exposes(mixer, "ReverbSend"));
	EXPECT_TRUE(Valid(mixer));
}

TEST(AudioMixerEditOpsTest, RetargetedSendTakesLevelAndRefusesLoop) {
	AudioMixer mixer;
	Build(mixer);
	ASSERT_TRUE(Ops::RetargetSend(mixer, "SFX", 0, "UI", ""));
	EXPECT_EQ(Ops::GetValue(mixer, 1, Ref("SFX", AudioMixerParameter::SendLevel, "UI")), -10.0f);
	EXPECT_EQ(mixer.exposedParameters[1].target, "UI");
	EXPECT_TRUE(Valid(mixer));

	Vector<Ops::SendTarget> targets{GetMemoryResource(MemTag::Audio)};
	Ops::ListSendTargets(mixer, "SFX", 0, targets);
	str listed;
	for (const Ops::SendTarget& place : targets) {
		listed += (listed.empty() ? "" : " ") + place.group + (place.loops ? "!" : "");
	}
	EXPECT_EQ(listed, "Master Music Footsteps! Reverb UI");
	EXPECT_FALSE(Ops::RetargetSend(mixer, "SFX", 0, "Footsteps", ""));
	EXPECT_FALSE(Ops::RetargetSend(mixer, "SFX", 0, "SFX", ""));

	ASSERT_EQ(Ops::AddUnit(mixer, "SFX", 0, AudioMixerUnitKind::Send), 0);
	EXPECT_FALSE(Ops::RetargetSend(mixer, "SFX", 0, "UI", ""));
	ASSERT_TRUE(Ops::RetargetSend(mixer, "SFX", 1, {}, {}));
	EXPECT_EQ(Chain(mixer, "SFX"), "Send> Send> Attenuation");
	EXPECT_EQ(CountValues(mixer, "SFX", AudioMixerParameter::SendLevel), 0u);
	EXPECT_FALSE(Exposes(mixer, "ReverbSend"));
	EXPECT_TRUE(Valid(mixer));
}

TEST(AudioMixerEditOpsTest, TurningWetMixingOffTakesWetShare) {
	AudioMixer mixer;
	Build(mixer);
	ASSERT_EQ(Ops::AddUnit(mixer, "Music", 1, AudioMixerUnitKind::Effect, "Echo"), 1);
	ASSERT_TRUE(Ops::SetWetMixing(mixer, "Music", 1, true));
	EXPECT_FALSE(Ops::SetWetMixing(mixer, "Music", 1, true));
	mixer.snapshots[0].values.push_back(Value("Music", AudioMixerParameter::WetLevel, 40.0f, "Echo"));
	AudioMixer::ExposedParameter wet;
	wet.name = "EchoWet";
	wet.group = "Music";
	wet.parameter = AudioMixerParameter::WetLevel;
	wet.target = "Echo";
	mixer.exposedParameters.push_back(wet);
	ASSERT_TRUE(Valid(mixer));
	ASSERT_TRUE(Ops::SetWetMixing(mixer, "Music", 1, false));
	EXPECT_EQ(CountValues(mixer, "Music", AudioMixerParameter::WetLevel), 0u);
	EXPECT_FALSE(Exposes(mixer, "EchoWet"));
	EXPECT_TRUE(Valid(mixer));
	EXPECT_FALSE(Ops::SetWetMixing(mixer, "Music", 0, true));
}

TEST_F(AudioMixerChainFixture, SendOffersReceivesAndSidechains) {
	ASSERT_NE(FindEffect("Duck Volume"), nullptr);
	EXPECT_TRUE(FindEffect("Duck Volume")->IsSidechainTarget());
	EXPECT_FALSE(FindEffect("Echo")->IsSidechainTarget());
	AudioMixer mixer;
	Build(mixer);
	ASSERT_GE(Ops::AddUnit(mixer, "Music", 0, AudioMixerUnitKind::Effect, "Duck Volume"), 0);
	ASSERT_GE(Ops::AddUnit(mixer, "Music", 0, AudioMixerUnitKind::Effect, "Echo"), 0);
	ASSERT_GE(Ops::AddUnit(mixer, "UI", 0, AudioMixerUnitKind::Receive), 0);
	Vector<Ops::SendTarget> targets{GetMemoryResource(MemTag::Audio)};
	Ops::ListSendTargets(mixer, "SFX", 0, targets);
	str listed;
	for (const Ops::SendTarget& place : targets) {
		listed += (listed.empty() ? "" : " ") + place.group + (place.unit.empty() ? str() : "/" + place.unit);
	}
	EXPECT_EQ(listed, "Master Music Music/Duck Volume Footsteps Reverb UI UI/Receive");
	EXPECT_TRUE(Ops::RetargetSend(mixer, "SFX", 0, "Music", "Duck Volume"));
	EXPECT_FALSE(Ops::RetargetSend(mixer, "SFX", 0, "Music", "Echo"));
	EXPECT_TRUE(Valid(mixer));
}

TEST_F(AudioMixerChainFixture, RegistryKeepsRegistrationOrder) {
	ASSERT_GE(GetRegisteredEffectCount(), GetBuiltinEffects().size());
	for (u32 index = 0; index < GetBuiltinEffects().size(); ++index) {
		EXPECT_EQ(GetRegisteredEffectName(index), GetBuiltinEffects()[index].name);
	}
	EXPECT_TRUE(GetRegisteredEffectName(GetRegisteredEffectCount()).empty());
}

TEST_F(AudioMixerChainFixture, ValueIsWrittenInParametersRange) {
	AudioMixer mixer;
	Build(mixer);
	ASSERT_EQ(Ops::AddUnit(mixer, "Music", 1, AudioMixerUnitKind::Effect, "Echo"), 1);
	ASSERT_TRUE(Ops::SetWetMixing(mixer, "Music", 1, true));
	const AudioParameterDefinition& delay = FindEffect("Echo")->GetParameters()[0];

	EXPECT_TRUE(Ops::SetValue(mixer, 0, Ref("Music", AudioMixerParameter::Pitch), 20.0f));
	EXPECT_EQ(Ops::GetValue(mixer, 0, Ref("Music", AudioMixerParameter::Pitch)), 10.0f);
	EXPECT_TRUE(Ops::SetValue(mixer, 0, Ref("SFX", AudioMixerParameter::SendLevel, "Reverb"), 6.0f));
	EXPECT_EQ(Ops::GetValue(mixer, 0, Ref("SFX", AudioMixerParameter::SendLevel, "Reverb")), 0.0f);
	EXPECT_TRUE(Ops::SetValue(mixer, 0, Ref("Music", AudioMixerParameter::WetLevel, "Echo"), 150.0f));
	EXPECT_EQ(Ops::GetValue(mixer, 0, Ref("Music", AudioMixerParameter::WetLevel, "Echo")), 100.0f);
	EXPECT_TRUE(Ops::SetValue(mixer, 0, Ref("Music", AudioMixerParameter::EffectParameter, "Echo", delay.name), delay.maximum + 1000.0f));
	EXPECT_EQ(Ops::GetValue(mixer, 0, Ref("Music", AudioMixerParameter::EffectParameter, "Echo", delay.name)), delay.maximum);
	EXPECT_TRUE(Ops::SetValue(mixer, 0, Ref("Music", AudioMixerParameter::Volume), std::nanf("")));
	EXPECT_EQ(Ops::GetValue(mixer, 0, Ref("Music", AudioMixerParameter::Volume)), 0.0f);

	EXPECT_FALSE(Ops::SetValue(mixer, 0, Ref("Music", AudioMixerParameter::SendLevel, "Reverb"), -6.0f));
	EXPECT_FALSE(Ops::SetValue(mixer, 0, Ref("Music", AudioMixerParameter::EffectParameter, "Echo", "Nothing"), 1.0f));
	Ops::ParameterRange pitch;
	ASSERT_TRUE(Ops::GetRange(mixer, Ref("Music", AudioMixerParameter::Pitch), pitch));
	EXPECT_EQ(pitch.displayScale, 100.0f);
	EXPECT_TRUE(Valid(mixer));
}

TEST_F(AudioMixerChainFixture, UnitSettingsCopyToAllSnapshots) {
	AudioMixer mixer;
	Build(mixer);
	AudioMixer::Snapshot calm;
	calm.id = "Calm";
	calm.name = "Calm";
	mixer.snapshots.push_back(std::move(calm));
	ASSERT_EQ(Ops::AddUnit(mixer, "Music", 1, AudioMixerUnitKind::Effect, "Echo"), 1);
	ASSERT_TRUE(Ops::SetWetMixing(mixer, "Music", 1, true));
	const str delay(FindEffect("Echo")->GetParameters()[0].name);
	const auto delayRef = Ref("Music", AudioMixerParameter::EffectParameter, "Echo", delay);
	const auto wetRef = Ref("Music", AudioMixerParameter::WetLevel, "Echo");
	const auto volumeRef = Ref("Music", AudioMixerParameter::Volume);
	const auto pitchRef = Ref("Music", AudioMixerParameter::Pitch);
	ASSERT_TRUE(Ops::SetValue(mixer, 0, delayRef, 300.0f));
	ASSERT_TRUE(Ops::SetValue(mixer, 0, wetRef, 30.0f));
	ASSERT_TRUE(Ops::SetValue(mixer, 0, volumeRef, -12.0f));
	ASSERT_TRUE(Ops::SetValue(mixer, 0, pitchRef, 0.5f));

	Ops::CopyUnitToAllSnapshots(mixer, 0, "Music", 1, false);
	for (u32 snapshot = 1; snapshot < 3; ++snapshot) {
		EXPECT_EQ(Ops::GetValue(mixer, snapshot, delayRef), 300.0f) << snapshot;
		EXPECT_EQ(Ops::GetValue(mixer, snapshot, wetRef), 100.0f) << snapshot;
	}
	EXPECT_EQ(Ops::GetValue(mixer, 1, volumeRef), -20.0f);
	Ops::CopyUnitToAllSnapshots(mixer, 0, "Music", 1, true);
	EXPECT_EQ(Ops::GetValue(mixer, 2, wetRef), 30.0f);
	Ops::CopyUnitToAllSnapshots(mixer, 0, "Music", 0, false);
	EXPECT_EQ(Ops::GetValue(mixer, 1, volumeRef), -12.0f);
	EXPECT_EQ(Ops::GetValue(mixer, 2, pitchRef), 1.0f);
	Ops::CopyGroupToAllSnapshots(mixer, 0, "Music");
	EXPECT_EQ(Ops::GetValue(mixer, 2, pitchRef), 0.5f);
	EXPECT_EQ(Ops::GetValue(mixer, 1, Ref("Footsteps", AudioMixerParameter::Volume)), 0.0f);
	EXPECT_TRUE(Valid(mixer));
}

namespace {

	str Snapshots(const AudioMixer& mixer) {
		str out;
		for (const AudioMixer::Snapshot& snapshot : mixer.snapshots) {
			out += (out.empty() ? "" : " | ") + snapshot.id + "=" + snapshot.name;
		}
		return out;
	}

	f32 Sent(const AudioMixer& mixer, std::string_view group, AudioMixerParameter parameter, std::string_view target = {}) {
		if (const AudioMixerLive* live = AudioMixerAccess::GetLive(mixer)) {
			for (const AudioMixerLiveParameter& entry : live->parameters) {
				if (entry.group == group && entry.parameter == parameter && entry.target == target && entry.effectParameter.empty()) {
					return entry.everSent ? entry.sent : std::nanf("");
				}
			}
		}
		return std::nanf("");
	}

} // namespace

TEST(AudioMixerEditOpsTest, DuplicatedSnapshotNamedAfterSource) {
	AudioMixer mixer;
	Build(mixer);
	ASSERT_EQ(Ops::DuplicateSnapshot(mixer, 1), 2);
	ASSERT_EQ(Ops::DuplicateSnapshot(mixer, 1), 3);
	EXPECT_EQ(Snapshots(mixer), "Explore=Explore | Combat=Combat | Combat - Copy=Combat - Copy | Combat - Copy (01)=Combat - Copy (01)");
	EXPECT_EQ(Ops::GetValue(mixer, 3, Ref("Music", AudioMixerParameter::Volume)), -20.0f);
	EXPECT_EQ(Ops::GetValue(mixer, 3, Ref("SFX", AudioMixerParameter::SendLevel, "Reverb")), -10.0f);
	EXPECT_EQ(Ops::DuplicateSnapshot(mixer, 9), -1);
	EXPECT_TRUE(Valid(mixer));
}

TEST(AudioMixerEditOpsTest, SnapshotRenameKeepsIdentity) {
	AudioMixer mixer;
	Build(mixer);
	EXPECT_TRUE(Ops::RenameSnapshot(mixer, 1, "Explore"));
	EXPECT_TRUE(Ops::RenameSnapshot(mixer, 0, "Calm"));
	EXPECT_EQ(Snapshots(mixer), "Explore=Calm | Combat=Explore (01)");
	EXPECT_FALSE(Ops::RenameSnapshot(mixer, 0, ""));
	EXPECT_FALSE(Ops::RenameSnapshot(mixer, 0, "Calm"));
	EXPECT_EQ(mixer.startSnapshot, "Explore");
	EXPECT_TRUE(Valid(mixer));
}

TEST(AudioMixerEditOpsTest, SnapshotDeleteKeepsOneAndStart) {
	AudioMixer mixer;
	Build(mixer);
	ASSERT_EQ(Ops::DuplicateSnapshot(mixer, 0), 2);
	mixer.startSnapshot.clear();
	EXPECT_TRUE(Ops::RemoveSnapshot(mixer, 1));
	EXPECT_EQ(mixer.snapshots[StartSnapshotIndex(mixer)].id, "Explore");
	EXPECT_TRUE(Ops::RemoveSnapshot(mixer, 0));
	EXPECT_EQ(Snapshots(mixer), "Explore - Copy=Explore - Copy");
	EXPECT_EQ(StartSnapshotIndex(mixer), 0u);
	EXPECT_FALSE(Ops::RemoveSnapshot(mixer, 0));
	EXPECT_TRUE(Valid(mixer));

	AudioMixer named;
	Build(named);
	named.startSnapshot = "Combat";
	EXPECT_TRUE(Ops::RemoveSnapshot(named, 0));
	EXPECT_EQ(named.snapshots[StartSnapshotIndex(named)].id, "Combat");
	EXPECT_TRUE(Valid(named));

	AudioMixer first;
	Build(first);
	first.startSnapshot.clear();
	EXPECT_TRUE(Ops::RemoveSnapshot(first, 0));
	EXPECT_EQ(first.snapshots[StartSnapshotIndex(first)].id, "Combat");
	EXPECT_TRUE(Valid(first));
}

TEST(AudioMixerEditOpsTest, MovingASnapshotNeverMovesTheStart) {
	AudioMixer mixer;
	Build(mixer);
	mixer.startSnapshot.clear();
	EXPECT_FALSE(Ops::MoveSnapshot(mixer, 0, 1));
	EXPECT_TRUE(Ops::MoveSnapshot(mixer, 1, 0));
	EXPECT_EQ(Snapshots(mixer), "Combat=Combat | Explore=Explore");
	EXPECT_EQ(mixer.snapshots[StartSnapshotIndex(mixer)].id, "Explore");
	EXPECT_TRUE(Ops::MoveSnapshot(mixer, 0, 99));
	EXPECT_EQ(Snapshots(mixer), "Explore=Explore | Combat=Combat");
	EXPECT_TRUE(Ops::SetStartSnapshot(mixer, 1));
	EXPECT_EQ(mixer.startSnapshot, "Combat");
	EXPECT_FALSE(Ops::SetStartSnapshot(mixer, 1));
	EXPECT_TRUE(Valid(mixer));
}

TEST_F(AudioMixerChainFixture, HeldMixerLandsWhereGameIs) {
	AudioMixer mixer;
	Build(mixer);
	AudioMixer::Snapshot calm;
	calm.id = "Calm";
	calm.name = "Calm";
	calm.values.push_back(Value("Music", AudioMixerParameter::Volume, -40.0f));
	mixer.snapshots.push_back(std::move(calm));
	EXPECT_FALSE(Ops::AcquireHold(mixer, 1));
	ASSERT_TRUE(mixer.ApplyChanges());
	EXPECT_EQ(Sent(mixer, "Music", AudioMixerParameter::Volume), -6.0f);

	ASSERT_TRUE(Ops::AcquireHold(mixer, 1));
	EXPECT_EQ(Sent(mixer, "Music", AudioMixerParameter::Volume), -20.0f);
	ASSERT_TRUE(mixer.TransitionToSnapshot("Calm", 0.0f));
	EXPECT_EQ(Sent(mixer, "Music", AudioMixerParameter::Volume), -20.0f);

	ASSERT_TRUE(mixer.SetFloat("MusicVolume", -3.0f));
	EXPECT_EQ(Sent(mixer, "Music", AudioMixerParameter::Volume), -3.0f);
	ASSERT_TRUE(mixer.ClearFloat("MusicVolume"));
	EXPECT_EQ(Sent(mixer, "Music", AudioMixerParameter::Volume), -20.0f);

	ASSERT_TRUE(Ops::SetValue(mixer, 1, Ref("Music", AudioMixerParameter::Volume), -12.0f));
	Ops::RefreshHold(mixer, 1);
	EXPECT_EQ(Sent(mixer, "Music", AudioMixerParameter::Volume), -12.0f);

	ASSERT_TRUE(Ops::AcquireHold(mixer, 1));
	EXPECT_EQ(Ops::CountHolds(mixer), 2u);
	Ops::ReleaseHold(mixer);
	EXPECT_EQ(Sent(mixer, "Music", AudioMixerParameter::Volume), -12.0f);
	ASSERT_TRUE(Ops::RemoveSnapshot(mixer, Ops::FindSnapshot(mixer, "Explore")));
	ASSERT_TRUE(Ops::MoveSnapshot(mixer, Ops::FindSnapshot(mixer, "Calm"), 0));
	ASSERT_TRUE(Ops::SetValue(mixer, Ops::FindSnapshot(mixer, "Calm"), Ref("Music", AudioMixerParameter::Volume), -30.0f));
	Ops::ReleaseHold(mixer);
	EXPECT_EQ(Ops::CountHolds(mixer), 0u);
	EXPECT_EQ(Sent(mixer, "Music", AudioMixerParameter::Volume), -30.0f);
	Ops::ReleaseHold(mixer);
	EXPECT_EQ(Ops::CountHolds(mixer), 0u);
}

TEST(AudioMixerEditOpsTest, AParameterIsDescribedByItsPath) {
	AudioMixer mixer;
	Build(mixer);
	mixer.groups[static_cast<size_t>(FindGroup(mixer, "Music"))].name = "Score";
	ASSERT_EQ(Ops::AddUnit(mixer, "Music", 1, AudioMixerUnitKind::Effect, "Echo"), 1);
	EXPECT_EQ(Ops::DescribeParameter(mixer, Ref("Music", AudioMixerParameter::Volume)), "Volume (of Score)");
	EXPECT_EQ(Ops::DescribeParameter(mixer, Ref("Music", AudioMixerParameter::Pitch)), "Pitch (of Score)");
	EXPECT_EQ(Ops::DescribeParameter(mixer, Ref("SFX", AudioMixerParameter::SendLevel, "Reverb")), "Send level (of SFX) to Reverb");
	EXPECT_EQ(Ops::DescribeParameter(mixer, Ref("Music", AudioMixerParameter::WetLevel, "Echo")), "Mix Level (of Score\xE2\x86\x92"
																								  "Echo)");
	EXPECT_EQ(Ops::DescribeParameter(mixer, Ref("Music", AudioMixerParameter::EffectParameter, "Echo", "Delay")), "Delay (of Score\xE2\x86\x92"
																												  "Echo)");
	EXPECT_EQ(Ops::DescribeParameter(mixer, Ref("Music", AudioMixerParameter::Pitch), true), " (of Score)");
}

TEST(AudioMixerEditOpsTest, ExposedParameterGetsFreeName) {
	AudioMixer mixer;
	Build(mixer);
	EXPECT_EQ(Ops::ExposeParameter(mixer, Ref("UI", AudioMixerParameter::Volume)), "MyExposedParam");
	EXPECT_EQ(Ops::ExposeParameter(mixer, Ref("Master", AudioMixerParameter::Pitch)), "MyExposedParam 1");
	EXPECT_TRUE(Ops::ExposeParameter(mixer, Ref("UI", AudioMixerParameter::Volume)).empty());
	EXPECT_TRUE(Ops::ExposeParameter(mixer, Ref("UI", AudioMixerParameter::SendLevel, "Reverb")).empty());
	str order;
	for (const AudioMixer::ExposedParameter& exposed : mixer.exposedParameters) {
		order += (order.empty() ? "" : " ") + exposed.name;
	}
	EXPECT_EQ(order, "MyExposedParam 1 MusicVolume ReverbSend MyExposedParam");
	EXPECT_EQ(Ops::FindExposed(mixer, Ref("Master", AudioMixerParameter::Pitch)), 0);
	EXPECT_TRUE(Ops::UnexposeParameter(mixer, Ref("Master", AudioMixerParameter::Pitch)));
	EXPECT_FALSE(Ops::UnexposeParameter(mixer, Ref("Master", AudioMixerParameter::Pitch)));
	EXPECT_EQ(Ops::FindExposed(mixer, Ref("Master", AudioMixerParameter::Pitch)), -1);
	EXPECT_TRUE(Valid(mixer));
}

TEST(AudioMixerEditOpsTest, ExposedRenameStaysUnique) {
	AudioMixer mixer;
	Build(mixer);
	EXPECT_TRUE(Ops::RenameExposed(mixer, 1, "MusicVolume"));
	EXPECT_EQ(mixer.exposedParameters[1].name, "MusicVolume 1");
	EXPECT_TRUE(Ops::RenameExposed(mixer, 0, str(100, 'x')));
	EXPECT_EQ(mixer.exposedParameters[0].name, str(64, 'x'));
	EXPECT_FALSE(Ops::RenameExposed(mixer, 0, ""));
	EXPECT_FALSE(Ops::RenameExposed(mixer, 7, "Nothing"));
	EXPECT_TRUE(Valid(mixer));
}

TEST(AudioMixerEditOpsTest, TransitionOverridePerSnapshot) {
	AudioMixer mixer;
	Build(mixer);
	const auto music = Ref("Music", AudioMixerParameter::Volume);
	EXPECT_EQ(Ops::GetTransition(mixer, 1, music), AudioMixerTransitionType::Lerp);
	EXPECT_TRUE(Ops::SetTransition(mixer, 1, music, AudioMixerTransitionType::Smoothstep));
	EXPECT_TRUE(Ops::SetTransition(mixer, 1, music, AudioMixerTransitionType::BrickwallEnd));
	EXPECT_FALSE(Ops::SetTransition(mixer, 1, music, AudioMixerTransitionType::BrickwallEnd));
	EXPECT_EQ(mixer.snapshots[1].transitions.size(), 1u);
	EXPECT_EQ(Ops::GetTransition(mixer, 1, music), AudioMixerTransitionType::BrickwallEnd);
	EXPECT_EQ(Ops::GetTransition(mixer, 0, music), AudioMixerTransitionType::Lerp);
	EXPECT_FALSE(Ops::SetTransition(mixer, 1, Ref("UI", AudioMixerParameter::SendLevel, "Reverb"), AudioMixerTransitionType::Squared));
	EXPECT_TRUE(Valid(mixer));
	EXPECT_TRUE(Ops::SetTransition(mixer, 1, music, AudioMixerTransitionType::Lerp));
	EXPECT_TRUE(mixer.snapshots[1].transitions.empty());
}

TEST(AudioMixerEditOpsTest, TransitionOverrideFollowsParameterItNames) {
	AudioMixer mixer;
	Build(mixer);
	ASSERT_EQ(Ops::AddUnit(mixer, "Music", 1, AudioMixerUnitKind::Effect, "Echo"), 1);
	ASSERT_TRUE(Ops::SetWetMixing(mixer, "Music", 1, true));
	ASSERT_TRUE(Ops::SetTransition(mixer, 1, Ref("Music", AudioMixerParameter::WetLevel, "Echo"), AudioMixerTransitionType::Squared));
	ASSERT_TRUE(Ops::SetTransition(mixer, 1, Ref("SFX", AudioMixerParameter::SendLevel, "Reverb"), AudioMixerTransitionType::SquareRoot));
	ASSERT_TRUE(Ops::SetTransition(mixer, 1, Ref("Footsteps", AudioMixerParameter::Volume), AudioMixerTransitionType::BrickwallStart));
	const auto overrides = [&]() {
		str out;
		for (const AudioMixer::TransitionOverride& transition : mixer.snapshots[1].transitions) {
			out += (out.empty() ? "" : " ") + transition.group + ":" + transition.target;
		}
		return out;
	};

	Vector<str> copies{GetMemoryResource(MemTag::Audio)};
	const str footsteps[] = {"Footsteps"};
	Ops::DuplicateGroups(mixer, footsteps, 0, copies);
	ASSERT_EQ(copies.size(), 1u);
	EXPECT_EQ(Ops::GetTransition(mixer, 1, Ref(copies[0], AudioMixerParameter::Volume)), AudioMixerTransitionType::BrickwallStart);
	EXPECT_TRUE(Valid(mixer));

	ASSERT_TRUE(Ops::RetargetSend(mixer, "SFX", 0, "UI", {}));
	EXPECT_EQ(Ops::GetTransition(mixer, 1, Ref("SFX", AudioMixerParameter::SendLevel, "UI")), AudioMixerTransitionType::SquareRoot);
	EXPECT_TRUE(Valid(mixer));

	ASSERT_TRUE(Ops::SetWetMixing(mixer, "Music", 1, false));
	EXPECT_TRUE(Valid(mixer));
	const str gone[] = {"Footsteps"};
	EXPECT_EQ(Ops::RemoveGroups(mixer, gone), 1u);
	EXPECT_TRUE(Valid(mixer));
	EXPECT_EQ(overrides(), "SFX:UI " + copies[0] + ":");
	ASSERT_TRUE(Ops::RemoveUnit(mixer, "SFX", 0));
	EXPECT_EQ(overrides(), copies[0] + ":");
	EXPECT_TRUE(Valid(mixer));
}

namespace {

	str Shown(const AudioMixer& mixer, u32 view) {
		str out;
		for (const AudioMixer::Group& group : mixer.groups) {
			if (Ops::IsVisible(mixer, view, group.id)) {
				out += (out.empty() ? "" : " ") + group.id;
			}
		}
		return out;
	}

} // namespace

TEST(AudioMixerEditOpsTest, ViewlessMixerHasDefaultView) {
	AudioMixer mixer;
	Build(mixer);
	EXPECT_EQ(Ops::CountViews(mixer), 1u);
	EXPECT_EQ(Ops::ViewName(mixer, 0), "View");
	EXPECT_TRUE(Ops::IsVisible(mixer, 0, "UI"));
	const str music[] = {"Music"};
	EXPECT_FALSE(Ops::SetVisible(mixer, 0, music, true));
	EXPECT_TRUE(mixer.views.empty());

	ASSERT_TRUE(Ops::SetVisible(mixer, 0, music, false));
	ASSERT_EQ(mixer.views.size(), 1u);
	EXPECT_EQ(mixer.views[0], "View");
	EXPECT_EQ(Shown(mixer, 0), "Master SFX Reverb Footsteps UI");
	EXPECT_FALSE(Ops::IsVisible(mixer, 0, "Music"));
	ASSERT_TRUE(Ops::SetVisible(mixer, 0, music, true));
	EXPECT_EQ(Shown(mixer, 0), "Master Music SFX Reverb Footsteps UI");
	const str ghost[] = {"Ghost"};
	EXPECT_FALSE(Ops::SetVisible(mixer, 0, ghost, false));
	EXPECT_FALSE(Ops::SetVisible(mixer, 1, music, false));
}

TEST(AudioMixerEditOpsTest, ViewEditsKeepLastView) {
	AudioMixer mixer;
	Build(mixer);
	ASSERT_EQ(Ops::DuplicateView(mixer, 0), 1);
	ASSERT_EQ(mixer.views.size(), 2u);
	EXPECT_EQ(mixer.views[1], "View - Copy");
	EXPECT_EQ(Shown(mixer, 1), Shown(mixer, 0));
	const str ui[] = {"UI"};
	ASSERT_TRUE(Ops::SetVisible(mixer, 1, ui, false));
	EXPECT_TRUE(Ops::IsVisible(mixer, 0, "UI"));
	EXPECT_EQ(Ops::DuplicateView(mixer, 5), -1);

	EXPECT_TRUE(Ops::RenameView(mixer, 1, "Mix"));
	EXPECT_FALSE(Ops::RenameView(mixer, 1, ""));
	EXPECT_FALSE(Ops::RenameView(mixer, 1, "Mix"));
	EXPECT_TRUE(Ops::RenameView(mixer, 0, "Mix"));

	ASSERT_TRUE(Ops::RenameView(mixer, 0, "View"));
	ASSERT_TRUE(Ops::MoveView(mixer, 1, 0));
	EXPECT_EQ(mixer.views[0], "Mix");
	EXPECT_FALSE(Ops::IsVisible(mixer, 0, "UI"));
	EXPECT_TRUE(Ops::IsVisible(mixer, 1, "UI"));
	EXPECT_FALSE(Ops::MoveView(mixer, 0, 1));
	ASSERT_TRUE(Ops::MoveView(mixer, 0, 2));
	EXPECT_EQ(mixer.views[1], "Mix");
	EXPECT_FALSE(Ops::IsVisible(mixer, 1, "UI"));

	ASSERT_TRUE(Ops::RemoveView(mixer, 0));
	ASSERT_EQ(mixer.views.size(), 1u);
	EXPECT_EQ(mixer.views[0], "Mix");
	EXPECT_FALSE(Ops::IsVisible(mixer, 0, "UI"));
	EXPECT_EQ(Shown(mixer, 0), "Master Music SFX Reverb Footsteps");
	EXPECT_FALSE(Ops::RemoveView(mixer, 0));
	for (u32 made = 0; made < 2 * Ops::MAX_VIEWS && Ops::DuplicateView(mixer, 0) >= 0; ++made) {
	}
	EXPECT_EQ(mixer.views.size(), static_cast<size_t>(Ops::MAX_VIEWS));
}

TEST(AudioMixerEditOpsTest, CopyShownWithSource) {
	AudioMixer mixer;
	Build(mixer);
	ASSERT_EQ(Ops::DuplicateView(mixer, 0), 1);
	const str footsteps[] = {"Footsteps"};
	ASSERT_TRUE(Ops::SetVisible(mixer, 1, footsteps, false));

	Vector<str> copies{GetMemoryResource(MemTag::Audio)};
	const str sfx[] = {"SFX"};
	Ops::DuplicateGroups(mixer, sfx, 1, copies);
	ASSERT_EQ(copies.size(), 1u);
	EXPECT_TRUE(Ops::IsVisible(mixer, 1, copies[0]));
	EXPECT_FALSE(Ops::IsVisible(mixer, 1, "Footsteps 2"));
	EXPECT_FALSE(Ops::IsVisible(mixer, 0, copies[0]));

	EXPECT_EQ(Ops::RemoveGroups(mixer, sfx), 2u);
	EXPECT_FALSE(Ops::IsVisible(mixer, 0, "SFX"));
	EXPECT_EQ(Shown(mixer, 0), "Master Music Reverb UI");
	EXPECT_EQ(Shown(mixer, 1), "Master Music Reverb UI " + copies[0]);
	EXPECT_TRUE(Valid(mixer));
}

TEST(AudioMixerEditOpsTest, ColorTagsNamedGroups) {
	AudioMixer mixer;
	Build(mixer);
	const str two[] = {"Music", "UI"};
	ASSERT_TRUE(Ops::SetColor(mixer, two, AudioMixerGroupColor::Red));
	EXPECT_EQ(Find(mixer, "Music")->color, AudioMixerGroupColor::Red);
	EXPECT_EQ(Find(mixer, "UI")->color, AudioMixerGroupColor::Red);
	EXPECT_EQ(Find(mixer, "SFX")->color, AudioMixerGroupColor::None);
	EXPECT_FALSE(Ops::SetColor(mixer, two, AudioMixerGroupColor::Red));
	const str ghost[] = {"Ghost"};
	EXPECT_FALSE(Ops::SetColor(mixer, ghost, AudioMixerGroupColor::Blue));
}

namespace {

	AudioMixer* Listed(Vector<AudioMixer*>& owned, const char* top, Guid output = {}, const char* group = "") {
		AudioMixer* mixer = New<AudioMixer>(MemTag::Audio);
		mixer->groups.push_back(Group(top));
		mixer->groups.push_back(Group("Music", top));
		mixer->groups.back().name = "Score";
		mixer->outputMixer = output;
		mixer->outputGroup = group;
		owned.push_back(mixer);
		return mixer;
	}

	Guid Id(u64 value) { return Guid{value, value}; }

	str Rows(std::span<const Ops::MixerListing> listings) {
		Vector<Ops::MixerRow> rows{GetMemoryResource(MemTag::Audio)};
		Ops::ArrangeMixers(listings, rows);
		str out;
		for (const Ops::MixerRow& row : rows) {
			out += std::format("{}{}@{}", out.empty() ? "" : " ", listings[row.index].name, row.depth);
		}
		return out;
	}

} // namespace

TEST(AudioMixerEditOpsTest, MixersNestUnderOutput) {
	Vector<AudioMixer*> owned{GetMemoryResource(MemTag::Audio)};
	Vector<Ops::MixerListing> listings{GetMemoryResource(MemTag::Audio)};
	auto add = [&](const char* name, u64 id, AudioMixer* data) { listings.push_back(Ops::MixerListing{Id(id), str(name), data}); };
	add("Mixer 10", 1, Listed(owned, "Master"));
	add("Game", 2, Listed(owned, "Master"));
	add("Mixer 2", 3, Listed(owned, "Master"));
	add("sub", 4, Listed(owned, "Master", Id(2), "Music"));
	add("Orphan", 5, Listed(owned, "Master", Id(99), "Music"));
	add("Loop A", 6, Listed(owned, "Master", Id(7), "Master"));
	add("Loop B", 7, Listed(owned, "Master", Id(6), "Master"));
	EXPECT_EQ(Rows(listings), "Game@0 sub@1 Mixer 2@0 Mixer 10@0 Orphan@0 Loop A@0 Loop B@1");

	EXPECT_EQ(Ops::DescribeOutput(listings, listings[3]), "(Score of Game)");
	EXPECT_EQ(Ops::DescribeOutput(listings, listings[1]), "(Audio Listener)");
	EXPECT_EQ(Ops::DescribeOutput(listings, listings[4]), "(Music of a missing mixer)");
	for (AudioMixer* mixer : owned) {
		Delete(mixer);
	}
}

TEST(AudioMixerEditOpsTest, MixerRoutingRefusesCycles) {
	Vector<AudioMixer*> owned{GetMemoryResource(MemTag::Audio)};
	Vector<Ops::MixerListing> listings{GetMemoryResource(MemTag::Audio)};
	listings.push_back(Ops::MixerListing{Id(1), "Game", Listed(owned, "Master")});
	listings.push_back(Ops::MixerListing{Id(2), "Sub", Listed(owned, "Master", Id(1), "Music")});
	listings.push_back(Ops::MixerListing{Id(3), "Deep", Listed(owned, "Master", Id(2), "Master")});
	EXPECT_TRUE(Ops::CanRouteInto(listings, Id(2), Id(1)));
	EXPECT_FALSE(Ops::CanRouteInto(listings, Id(1), Id(1)));
	EXPECT_FALSE(Ops::CanRouteInto(listings, Id(1), Id(3)));
	EXPECT_TRUE(Ops::CanRouteInto(listings, Id(3), Id(1)));
	for (AudioMixer* mixer : owned) {
		Delete(mixer);
	}
}

#endif
