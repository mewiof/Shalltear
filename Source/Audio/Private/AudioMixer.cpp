#include "Shalltear/Audio/AudioMixer.h"

#include "AudioBus.h"
#include "AudioEffectRegistry.h"
#include "AudioMixerLive.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include <algorithm>
#include <format>

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			constexpr std::string_view UPDATE_MODE_NAMES[] = {"Normal", "UnscaledTime"};
			constexpr std::string_view CLOCK_NAMES[] = {"Wall", "Gameplay"};
			constexpr std::string_view UNIT_NAMES[] = {"Attenuation", "Send", "Effect", "Receive"};
			constexpr std::string_view PARAMETER_NAMES[] = {"Volume", "Pitch", "SendLevel", "WetLevel", "EffectParameter"};
			constexpr std::string_view TRANSITION_NAMES[] = {"Lerp", "Smoothstep", "Squared", "SquareRoot", "BrickwallStart", "BrickwallEnd", "Attenuation"};
			constexpr std::string_view COLOR_NAMES[] = {"None", "Yellow", "Orange", "Red", "Magenta", "Violet", "Blue", "Cyan", "Green"};

			template <typename Enum, size_t N>
			bool ParseName(std::string_view text, const std::string_view (&names)[N], Enum& out) {
				for (size_t index = 0; index < N; ++index) {
					if (names[index] == text) {
						out = static_cast<Enum>(index);
						return true;
					}
				}
				return false;
			}

			template <typename Enum, size_t N>
			std::string_view NameOf(Enum value, const std::string_view (&names)[N]) {
				const size_t index = static_cast<size_t>(value);
				return index < N ? names[index] : std::string_view{};
			}

			bool IsValidId(std::string_view id) {
				return !id.empty() && id.find('/') == std::string_view::npos;
			}

			struct ParameterRef {
				std::string_view group;
				AudioMixerParameter parameter = AudioMixerParameter::Volume;
				std::string_view target;
				std::string_view effectParameter;
			};

			void CollectParameters(const AudioMixer& mixer, Vector<ParameterRef>& out) {
				out.clear();
				for (const AudioMixer::Group& group : mixer.groups) {
					out.push_back({group.id, AudioMixerParameter::Volume, {}, {}});
					out.push_back({group.id, AudioMixerParameter::Pitch, {}, {}});
					for (const AudioMixer::Unit& unit : group.units) {
						if (unit.kind == AudioMixerUnitKind::Send && !unit.target.empty()) {
							out.push_back({group.id, AudioMixerParameter::SendLevel, unit.target, {}});
						} else if (unit.kind == AudioMixerUnitKind::Effect && unit.wetMixing) {
							out.push_back({group.id, AudioMixerParameter::WetLevel, unit.id, {}});
						}
					}
				}
			}

			const AudioMixer::Unit* FindEffectUnit(const AudioMixer& mixer, std::string_view group, std::string_view id) {
				const i32 index = FindGroup(mixer, group);
				if (index < 0) {
					return nullptr;
				}
				for (const AudioMixer::Unit& unit : mixer.groups[static_cast<size_t>(index)].units) {
					if (unit.kind == AudioMixerUnitKind::Effect && unit.id == id) {
						return &unit;
					}
				}
				return nullptr;
			}

			void CollectEffectParameters(const AudioMixer& mixer, u32 index, Vector<const AudioMixer::Value*>& out) {
				out.clear();
				for (const AudioMixer::Group& group : mixer.groups) {
					for (const AudioMixer::Unit& unit : group.units) {
						if (unit.kind != AudioMixerUnitKind::Effect) {
							continue;
						}
						const size_t first = out.size();
						for (const AudioMixer::Value& value : mixer.snapshots[index].values) {
							if (value.parameter == AudioMixerParameter::EffectParameter && value.group == group.id && value.target == unit.id) {
								out.push_back(&value);
							}
						}
						std::sort(out.begin() + static_cast<std::ptrdiff_t>(first), out.end(),
								  [](const AudioMixer::Value* a, const AudioMixer::Value* b) { return a->effectParameter < b->effectParameter; });
					}
				}
			}

			void CollectTransitions(const AudioMixer& mixer, u32 index, Vector<const AudioMixer::TransitionOverride*>& out) {
				out.clear();
				const Vector<AudioMixer::TransitionOverride>& transitions = mixer.snapshots[index].transitions;
				if (transitions.empty()) {
					return;
				}
				Vector<ParameterRef> parameters{GetMemoryResource(MemTag::Asset)};
				CollectParameters(mixer, parameters);
				for (const ParameterRef& parameter : parameters) {
					for (const AudioMixer::TransitionOverride& transition : transitions) {
						if (transition.group == parameter.group && transition.parameter == parameter.parameter && transition.target == parameter.target) {
							out.push_back(&transition);
						}
					}
				}
				for (const AudioMixer::Group& group : mixer.groups) {
					for (const AudioMixer::Unit& unit : group.units) {
						if (unit.kind != AudioMixerUnitKind::Effect) {
							continue;
						}
						const size_t first = out.size();
						for (const AudioMixer::TransitionOverride& transition : transitions) {
							if (transition.parameter == AudioMixerParameter::EffectParameter && transition.group == group.id && transition.target == unit.id) {
								out.push_back(&transition);
							}
						}
						for (size_t at = first + 1; at < out.size(); ++at) {
							const AudioMixer::TransitionOverride* moving = out[at];
							size_t to = at;
							for (; to > first && moving->effectParameter < out[to - 1]->effectParameter; --to) {
								out[to] = out[to - 1];
							}
							out[to] = moving;
						}
					}
				}
			}

			[[gnu::noinline]] bool ReadKey(const JSONValue& row, str& group, AudioMixerParameter& parameter, str& target, str& effectParameter) {
				group = str(row["group"].AsString());
				target = str(row["target"].AsString());
				effectParameter = str(row["effectParameter"].AsString());
				return ParseName(row["parameter"].AsString(), PARAMETER_NAMES, parameter);
			}

			[[gnu::noinline]] void WriteKey(CookWriter& out, std::string_view group, AudioMixerParameter parameter, std::string_view target,
											std::string_view effectParameter) {
				out.WriteString(group);
				out.Write(static_cast<u8>(parameter));
				out.WriteString(target);
				out.WriteString(effectParameter);
			}

			[[gnu::noinline]] bool ReadKey(CookReader& in, str& group, AudioMixerParameter& parameter, str& target, str& effectParameter) {
				group = str(in.ReadString());
				u8 kind = 0;
				in.Read(kind);
				target = str(in.ReadString());
				effectParameter = str(in.ReadString());
				if (kind > static_cast<u8>(AudioMixerParameter::EffectParameter)) {
					return false;
				}
				parameter = static_cast<AudioMixerParameter>(kind);
				return true;
			}

			str DescribeValue(std::string_view group, AudioMixerParameter parameter, std::string_view target, std::string_view effectParameter) {
				if (parameter == AudioMixerParameter::EffectParameter) {
					return std::format("'{}' of effect '{}' of '{}'", effectParameter, target, group);
				}
				return std::format("{} of '{}'{}", NameOf(parameter, PARAMETER_NAMES), group, target.empty() ? str() : std::format(" to '{}'", target));
			}

		} // anonymous namespace

		i32 FindGroup(const AudioMixer& mixer, std::string_view id) {
			for (size_t index = 0; index < mixer.groups.size(); ++index) {
				if (mixer.groups[index].id == id) {
					return static_cast<i32>(index);
				}
			}
			return -1;
		}

		bool ParameterExists(const AudioMixer& mixer, std::string_view group, AudioMixerParameter parameter, std::string_view target,
							 std::string_view effectParameter) {
			const i32 index = FindGroup(mixer, group);
			if (index < 0) {
				return false;
			}
			switch (parameter) {
			case AudioMixerParameter::Volume:
			case AudioMixerParameter::Pitch:
				return target.empty() && effectParameter.empty();
			case AudioMixerParameter::SendLevel:
				if (target.empty()) {
					return false;
				}
				for (const AudioMixer::Unit& unit : mixer.groups[static_cast<size_t>(index)].units) {
					if (unit.kind == AudioMixerUnitKind::Send && unit.target == target) {
						return effectParameter.empty();
					}
				}
				return false;
			case AudioMixerParameter::WetLevel: {
				const AudioMixer::Unit* unit = FindEffectUnit(mixer, group, target);
				return unit && unit->wetMixing && effectParameter.empty();
			}
			case AudioMixerParameter::EffectParameter:
				return FindEffectUnit(mixer, group, target) && !effectParameter.empty();
			}
			return false;
		}

		f32 SnapshotValue(const AudioMixer& mixer, u32 index, std::string_view group, AudioMixerParameter parameter, std::string_view target,
						  std::string_view effectParameter) {
			if (index < mixer.snapshots.size()) {
				for (const AudioMixer::Value& value : mixer.snapshots[index].values) {
					if (value.group == group && value.parameter == parameter && value.target == target && value.effectParameter == effectParameter) {
						return value.value;
					}
				}
			}
			if (parameter == AudioMixerParameter::EffectParameter) {
				const AudioMixer::Unit* unit = FindEffectUnit(mixer, group, target);
				const IAudioEffectDefinition* definition = unit ? FindEffect(unit->effect) : nullptr;
				const u32 slot = definition ? FindEffectParameter(*definition, effectParameter) : NO_EFFECT_PARAMETER;
				return slot != NO_EFFECT_PARAMETER ? definition->GetParameters()[slot].defaultValue : 0.0f;
			}
			return AudioMixer::GetDefaultValue(parameter);
		}

		u32 StartSnapshotIndex(const AudioMixer& mixer) {
			for (size_t index = 0; index < mixer.snapshots.size(); ++index) {
				if (mixer.snapshots[index].id == mixer.startSnapshot) {
					return static_cast<u32>(index);
				}
			}
			return 0;
		}

		bool ValidateMixer(const AudioMixer& mixer, str& error) {
			i32 top = -1;
			for (size_t index = 0; index < mixer.groups.size(); ++index) {
				const AudioMixer::Group& group = mixer.groups[index];
				if (!IsValidId(group.id)) {
					error = std::format("group {} has no id, or one holding '/'", index);
					return false;
				}
				for (size_t other = 0; other < index; ++other) {
					if (mixer.groups[other].id == group.id) {
						error = std::format("two groups are both '{}'", group.id);
						return false;
					}
				}
				if (group.parent.empty()) {
					if (top >= 0) {
						error = std::format("'{}' and '{}' both have no parent (a mixer has 1 top group)", mixer.groups[static_cast<size_t>(top)].id,
											group.id);
						return false;
					}
					top = static_cast<i32>(index);
				}
			}
			if (top < 0) {
				error = mixer.groups.empty() ? str("it has no groups") : str("all groups have a parent (no top group)");
				return false;
			}
			for (const AudioMixer::Group& group : mixer.groups) {
				if (!group.parent.empty() && FindGroup(mixer, group.parent) < 0) {
					error = std::format("'{}' feeds '{}' (not a group of this mixer)", group.id, group.parent);
					return false;
				}
				u32 attenuation = 0;
				if (group.units.size() > MAX_BUS_ROWS) {
					error = std::format("'{}' has {} units, more than the {} a group may have", group.id, group.units.size(), MAX_BUS_ROWS);
					return false;
				}
				for (size_t unit = 0; unit < group.units.size(); ++unit) {
					const AudioMixer::Unit& row = group.units[unit];
					if (row.kind == AudioMixerUnitKind::Attenuation) {
						++attenuation;
						continue;
					}
					if (row.kind == AudioMixerUnitKind::Effect || row.kind == AudioMixerUnitKind::Receive) {
						const std::string_view what = row.kind == AudioMixerUnitKind::Effect ? "an effect" : "a Receive";
						if (!IsValidId(row.id)) {
							error = std::format("'{}' holds {} with no id, or one holding '/'", group.id, what);
							return false;
						}
						if (row.kind == AudioMixerUnitKind::Effect && row.effect.empty()) {
							error = std::format("'{}': effect unit '{}' has no effect", group.id, row.id);
							return false;
						}
						for (size_t earlier = 0; earlier < unit; ++earlier) {
							const AudioMixer::Unit& seen = group.units[earlier];
							if ((seen.kind == AudioMixerUnitKind::Effect || seen.kind == AudioMixerUnitKind::Receive) && seen.id == row.id) {
								error = std::format("'{}' holds 2 units with id '{}'", group.id, row.id);
								return false;
							}
						}
						continue;
					}
					if (row.target.empty()) {
						if (!row.targetUnit.empty()) {
							error = std::format("'{}' sends to '{}' of no group", group.id, row.targetUnit);
							return false;
						}
						continue;
					}
					if (row.target == group.id) {
						error = std::format("'{}' sends to itself", group.id);
						return false;
					}
					const i32 target = FindGroup(mixer, row.target);
					if (target < 0) {
						error = std::format("'{}' sends to '{}' (not a group of this mixer)", group.id, row.target);
						return false;
					}
					if (!row.targetUnit.empty()) {
						bool found = false;
						for (const AudioMixer::Unit& candidate : mixer.groups[static_cast<size_t>(target)].units) {
							found = found || ((candidate.kind == AudioMixerUnitKind::Receive || candidate.kind == AudioMixerUnitKind::Effect) &&
											  candidate.id == row.targetUnit);
						}
						if (!found) {
							error = std::format("'{}' sends to '{}' of '{}' (no Receive/effect there has that id)", group.id, row.targetUnit, row.target);
							return false;
						}
					}
					for (size_t earlier = 0; earlier < unit; ++earlier) {
						if (group.units[earlier].kind == AudioMixerUnitKind::Send && group.units[earlier].target == row.target) {
							error = std::format("'{}' sends to '{}' twice", group.id, row.target);
							return false;
						}
					}
				}
				if (attenuation != 1) {
					error = std::format("'{}' has {} Attenuation units (should have 1)", group.id, attenuation);
					return false;
				}
			}

			const size_t count = mixer.groups.size();
			Vector<u8> color(count, u8{0}, GetMemoryResource(MemTag::Audio));
			Vector<u32> stack{GetMemoryResource(MemTag::Audio)};
			Vector<u32> next{GetMemoryResource(MemTag::Audio)};
			for (size_t start = 0; start < count; ++start) {
				if (color[start] != 0) {
					continue;
				}
				stack.clear();
				next.clear();
				stack.push_back(static_cast<u32>(start));
				next.push_back(0);
				color[start] = 1;
				while (!stack.empty()) {
					const AudioMixer::Group& group = mixer.groups[stack.back()];
					Vector<i32> edges{GetMemoryResource(MemTag::Audio)};
					if (!group.parent.empty()) {
						edges.push_back(FindGroup(mixer, group.parent));
					}
					for (const AudioMixer::Unit& unit : group.units) {
						if (unit.kind == AudioMixerUnitKind::Send && !unit.target.empty()) {
							edges.push_back(FindGroup(mixer, unit.target));
						}
					}
					if (next.back() >= edges.size()) {
						color[stack.back()] = 2;
						stack.pop_back();
						next.pop_back();
						continue;
					}
					const i32 edge = edges[next.back()++];
					if (color[static_cast<size_t>(edge)] == 1) {
						error = std::format("'{}' feeds back into itself through '{}'", mixer.groups[static_cast<size_t>(edge)].id, group.id);
						return false;
					}
					if (color[static_cast<size_t>(edge)] == 0) {
						color[static_cast<size_t>(edge)] = 1;
						stack.push_back(static_cast<u32>(edge));
						next.push_back(0);
					}
				}
			}

			if (mixer.snapshots.empty()) {
				error = "it has no snapshot (a mixer starts at one)";
				return false;
			}
			for (size_t index = 0; index < mixer.snapshots.size(); ++index) {
				const AudioMixer::Snapshot& snapshot = mixer.snapshots[index];
				if (!IsValidId(snapshot.id)) {
					error = std::format("snapshot {} has no id, or one holding '/'", index);
					return false;
				}
				if (snapshot.name.empty()) {
					error = std::format("snapshot {} has no name", index);
					return false;
				}
				for (size_t other = 0; other < index; ++other) {
					if (mixer.snapshots[other].id == snapshot.id) {
						error = std::format("two snapshots are both '{}'", snapshot.id);
						return false;
					}
					if (mixer.snapshots[other].name == snapshot.name) {
						error = std::format("2 snapshots are named '{}'", snapshot.name);
						return false;
					}
				}
				for (size_t value = 0; value < snapshot.values.size(); ++value) {
					const AudioMixer::Value& row = snapshot.values[value];
					if (!ParameterExists(mixer, row.group, row.parameter, row.target, row.effectParameter)) {
						error = std::format("snapshot '{}' sets {} (this mixer doesn't have it)", snapshot.name,
											DescribeValue(row.group, row.parameter, row.target, row.effectParameter));
						return false;
					}
					for (size_t earlier = 0; earlier < value; ++earlier) {
						const AudioMixer::Value& seen = snapshot.values[earlier];
						if (seen.group == row.group && seen.parameter == row.parameter && seen.target == row.target &&
							seen.effectParameter == row.effectParameter) {
							error = std::format("snapshot '{}' sets {} twice", snapshot.name, DescribeValue(row.group, row.parameter, row.target, row.effectParameter));
							return false;
						}
					}
				}
				for (size_t entry = 0; entry < snapshot.transitions.size(); ++entry) {
					const AudioMixer::TransitionOverride& row = snapshot.transitions[entry];
					if (row.type > AudioMixerTransitionType::Attenuation || !ParameterExists(mixer, row.group, row.parameter, row.target, row.effectParameter)) {
						error = std::format("snapshot '{}' overrides the transition of {} (this mixer doesn't have it)", snapshot.name,
											DescribeValue(row.group, row.parameter, row.target, row.effectParameter));
						return false;
					}
					for (size_t earlier = 0; earlier < entry; ++earlier) {
						const AudioMixer::TransitionOverride& seen = snapshot.transitions[earlier];
						if (seen.group == row.group && seen.parameter == row.parameter && seen.target == row.target &&
							seen.effectParameter == row.effectParameter) {
							error = std::format("snapshot '{}' overrides the transition of {} twice", snapshot.name,
												DescribeValue(row.group, row.parameter, row.target, row.effectParameter));
							return false;
						}
					}
				}
			}
			if (!mixer.startSnapshot.empty()) {
				bool found = false;
				for (const AudioMixer::Snapshot& snapshot : mixer.snapshots) {
					found = found || snapshot.id == mixer.startSnapshot;
				}
				if (!found) {
					error = std::format("the start snapshot '{}' isn't one of its snapshots", mixer.startSnapshot);
					return false;
				}
			}

			for (size_t index = 0; index < mixer.exposedParameters.size(); ++index) {
				const AudioMixer::ExposedParameter& exposed = mixer.exposedParameters[index];
				if (exposed.name.empty()) {
					error = std::format("exposed parameter {} has no name", index);
					return false;
				}
				for (size_t other = 0; other < index; ++other) {
					if (mixer.exposedParameters[other].name == exposed.name) {
						error = std::format("2 exposed parameters are named '{}'", exposed.name);
						return false;
					}
				}
				if (!ParameterExists(mixer, exposed.group, exposed.parameter, exposed.target, exposed.effectParameter)) {
					error = std::format("'{}' exposes {} (this mixer doesn't have it)", exposed.name,
										DescribeValue(exposed.group, exposed.parameter, exposed.target, exposed.effectParameter));
					return false;
				}
			}
			return true;
		}

	} // namespace AudioImpl

	f32 AudioMixer::GetDefaultValue(AudioMixerParameter parameter) {
		switch (parameter) {
		case AudioMixerParameter::Volume:
			return 0.0f;
		case AudioMixerParameter::Pitch:
			return 1.0f;
		case AudioMixerParameter::SendLevel:
			return -80.0f;
		case AudioMixerParameter::WetLevel:
			return 100.0f;
		case AudioMixerParameter::EffectParameter:
			return 0.0f;
		}
		return 0.0f;
	}

	bool AudioMixer::Export(const Path& destPath) const {
		using namespace AudioImpl;

		JSONValue root = JSONValue::MakeObject();
		root.AddMember("updateMode", JSONValue::MakeString(NameOf(updateMode, UPDATE_MODE_NAMES)));

		const str outputText = outputMixer.IsValid() ? outputMixer.ToString() : str();
		JSONValue output = JSONValue::MakeObject();
		output.AddMember("mixer", JSONValue::MakeString(outputText));
		output.AddMember("group", JSONValue::MakeString(outputGroup));
		root.AddMember("output", std::move(output));
		root.AddMember("startSnapshot", JSONValue::MakeString(startSnapshot));

		JSONValue jsonGroups = JSONValue::MakeArray();
		for (const Group& group : groups) {
			JSONValue row = JSONValue::MakeObject();
			row.AddMember("id", JSONValue::MakeString(group.id));
			row.AddMember("name", JSONValue::MakeString(group.name));
			row.AddMember("parent", JSONValue::MakeString(group.parent));
			row.AddMember("clock", JSONValue::MakeString(NameOf(group.clock, CLOCK_NAMES)));
			row.AddMember("followTimeScale", JSONValue::MakeBool(group.followTimeScale));
			row.AddMember("mute", JSONValue::MakeBool(group.mute));
			row.AddMember("solo", JSONValue::MakeBool(group.solo));
			row.AddMember("bypassEffects", JSONValue::MakeBool(group.bypassEffects));
			if (group.color != AudioMixerGroupColor::None) {
				row.AddMember("color", JSONValue::MakeString(NameOf(group.color, COLOR_NAMES)));
			}
			JSONValue units = JSONValue::MakeArray();
			for (const Unit& unit : group.units) {
				JSONValue entry = JSONValue::MakeObject();
				entry.AddMember("kind", JSONValue::MakeString(NameOf(unit.kind, UNIT_NAMES)));
				if (unit.kind == AudioMixerUnitKind::Send) {
					entry.AddMember("target", JSONValue::MakeString(unit.target));
					if (!unit.targetUnit.empty()) {
						entry.AddMember("targetUnit", JSONValue::MakeString(unit.targetUnit));
					}
				} else if (unit.kind == AudioMixerUnitKind::Receive) {
					entry.AddMember("id", JSONValue::MakeString(unit.id));
				} else if (unit.kind == AudioMixerUnitKind::Effect) {
					entry.AddMember("id", JSONValue::MakeString(unit.id));
					entry.AddMember("effect", JSONValue::MakeString(unit.effect));
					entry.AddMember("bypass", JSONValue::MakeBool(unit.bypass));
					entry.AddMember("wetMixing", JSONValue::MakeBool(unit.wetMixing));
				}
				units.Push(std::move(entry));
			}
			row.AddMember("units", std::move(units));
			jsonGroups.Push(std::move(row));
		}
		root.AddMember("groups", std::move(jsonGroups));

		Vector<ParameterRef> parameters{GetMemoryResource(MemTag::Asset)};
		CollectParameters(*this, parameters);
		Vector<const Value*> effectValues{GetMemoryResource(MemTag::Asset)};
		Vector<const TransitionOverride*> transitionRows{GetMemoryResource(MemTag::Asset)};
		auto writeKey = [](JSONValue& entry, AudioMixerParameter parameter, std::string_view target, std::string_view effectParameter) {
			entry.AddMember("parameter", JSONValue::MakeString(NameOf(parameter, PARAMETER_NAMES)));
			if (parameter != AudioMixerParameter::Volume && parameter != AudioMixerParameter::Pitch) {
				entry.AddMember("target", JSONValue::MakeString(target));
			}
			if (parameter == AudioMixerParameter::EffectParameter) {
				entry.AddMember("effectParameter", JSONValue::MakeString(effectParameter));
			}
		};
		JSONValue jsonSnapshots = JSONValue::MakeArray();
		for (size_t index = 0; index < snapshots.size(); ++index) {
			JSONValue row = JSONValue::MakeObject();
			row.AddMember("id", JSONValue::MakeString(snapshots[index].id));
			row.AddMember("name", JSONValue::MakeString(snapshots[index].name));
			JSONValue values = JSONValue::MakeArray();
			for (const ParameterRef& parameter : parameters) {
				JSONValue entry = JSONValue::MakeObject();
				entry.AddMember("group", JSONValue::MakeString(parameter.group));
				writeKey(entry, parameter.parameter, parameter.target, parameter.effectParameter);
				const f32 value = SnapshotValue(*this, static_cast<u32>(index), parameter.group, parameter.parameter, parameter.target, {});
				entry.AddMember("value", JSONValue::MakeNumber(static_cast<f64>(value)));
				values.Push(std::move(entry));
			}
			CollectEffectParameters(*this, static_cast<u32>(index), effectValues);
			for (const Value* value : effectValues) {
				JSONValue entry = JSONValue::MakeObject();
				entry.AddMember("group", JSONValue::MakeString(value->group));
				writeKey(entry, value->parameter, value->target, value->effectParameter);
				entry.AddMember("value", JSONValue::MakeNumber(static_cast<f64>(value->value)));
				values.Push(std::move(entry));
			}
			row.AddMember("values", std::move(values));
			CollectTransitions(*this, static_cast<u32>(index), transitionRows);
			if (!transitionRows.empty()) {
				JSONValue transitions = JSONValue::MakeArray();
				for (const TransitionOverride* transition : transitionRows) {
					JSONValue entry = JSONValue::MakeObject();
					entry.AddMember("group", JSONValue::MakeString(transition->group));
					writeKey(entry, transition->parameter, transition->target, transition->effectParameter);
					entry.AddMember("type", JSONValue::MakeString(NameOf(transition->type, TRANSITION_NAMES)));
					transitions.Push(std::move(entry));
				}
				row.AddMember("transitions", std::move(transitions));
			}
			jsonSnapshots.Push(std::move(row));
		}
		root.AddMember("snapshots", std::move(jsonSnapshots));

		JSONValue jsonExposed = JSONValue::MakeArray();
		for (const ExposedParameter& exposed : exposedParameters) {
			JSONValue row = JSONValue::MakeObject();
			row.AddMember("name", JSONValue::MakeString(exposed.name));
			row.AddMember("group", JSONValue::MakeString(exposed.group));
			writeKey(row, exposed.parameter, exposed.target, exposed.effectParameter);
			jsonExposed.Push(std::move(row));
		}
		root.AddMember("exposedParameters", std::move(jsonExposed));

		if (!views.empty()) {
			JSONValue jsonViews = JSONValue::MakeArray();
			for (size_t index = 0; index < views.size(); ++index) {
				JSONValue row = JSONValue::MakeObject();
				row.AddMember("name", JSONValue::MakeString(views[index]));
				JSONValue shown = JSONValue::MakeArray();
				for (const Group& group : groups) {
					if ((group.shownIn >> index) & 1u) {
						shown.Push(JSONValue::MakeString(group.id));
					}
				}
				row.AddMember("groups", std::move(shown));
				jsonViews.Push(std::move(row));
			}
			root.AddMember("views", std::move(jsonViews));
		}

		return FileSystem::WriteTextFile(destPath, JSONSerialize(root));
	}

	bool AudioMixer::Import(const Path& sourcePath) {
		using namespace AudioImpl;

		str text = FileSystem::ReadTextFile(sourcePath, MemTag::Asset);
		LinearAllocator arena(text.size() + 64, MemTag::Asset);
		JSONValue root = JSONParse(text, arena);
		if (!root.IsObject()) {
			SLT_LOG_ERROR("Audio", "the mixer '{}' isn't a JSON object", sourcePath.ToString());
			return false;
		}

		AudioMixerUpdateMode readMode = AudioMixerUpdateMode::Normal;
		Vector<Group> readGroups{GetMemoryResource(MemTag::Asset)};
		Vector<Snapshot> readSnapshots{GetMemoryResource(MemTag::Asset)};
		Vector<ExposedParameter> readExposed{GetMemoryResource(MemTag::Asset)};
		str problem;
		auto refuse = [&](str why) {
			if (problem.empty()) {
				problem = std::move(why);
			}
		};

		if (root["updateMode"].IsString() && !ParseName(root["updateMode"].AsString(), UPDATE_MODE_NAMES, readMode)) {
			refuse(std::format("updateMode '{}' is neither Normal nor UnscaledTime", root["updateMode"].AsString()));
		}
		const Guid readOutputMixer = Guid::FromString(root["output"]["mixer"].AsString());
		const str readOutputGroup = str(root["output"]["group"].AsString());
		const str readStart = str(root["startSnapshot"].AsString());

		const JSONValue& jsonGroups = root["groups"];
		for (size_t index = 0; index < jsonGroups.Size(); ++index) {
			const JSONValue& row = jsonGroups.ChildAt(index);
			Group group;
			group.id = str(row["id"].AsString());
			group.name = str(row["name"].AsString());
			if (group.name.empty()) {
				group.name = group.id;
			}
			group.parent = str(row["parent"].AsString());
			if (row["clock"].IsString() && !ParseName(row["clock"].AsString(), CLOCK_NAMES, group.clock)) {
				refuse(std::format("group '{}' follows clock '{}' (neither Wall nor Gameplay)", group.id, row["clock"].AsString()));
			}
			group.followTimeScale = row["followTimeScale"].AsBool(false);
			group.mute = row["mute"].AsBool(false);
			group.solo = row["solo"].AsBool(false);
			group.bypassEffects = row["bypassEffects"].AsBool(false);
			if (row["color"].IsString() && !ParseName(row["color"].AsString(), COLOR_NAMES, group.color)) {
				refuse(std::format("group '{}' is colored '{}' (not one of the 9 colors)", group.id, row["color"].AsString()));
			}
			const JSONValue& units = row["units"];
			for (size_t entry = 0; entry < units.Size(); ++entry) {
				const JSONValue& unitRow = units.ChildAt(entry);
				Unit unit;
				if (!ParseName(unitRow["kind"].AsString(), UNIT_NAMES, unit.kind)) {
					refuse(std::format("group '{}' holds a unit of kind '{}'", group.id, unitRow["kind"].AsString()));
				}
				unit.target = str(unitRow["target"].AsString());
				unit.targetUnit = str(unitRow["targetUnit"].AsString());
				unit.effect = str(unitRow["effect"].AsString());
				const str fallback = unit.kind == AudioMixerUnitKind::Receive ? str("Receive") : unit.effect;
				unit.id = unitRow["id"].IsString() ? str(unitRow["id"].AsString()) : fallback;
				unit.bypass = unitRow["bypass"].AsBool(false);
				unit.wetMixing = unitRow["wetMixing"].AsBool(false);
				group.units.push_back(std::move(unit));
			}
			if (units.Size() == 0) {
				group.units.push_back(Unit{});
			}
			readGroups.push_back(std::move(group));
		}

		const JSONValue& jsonSnapshots = root["snapshots"];
		for (size_t index = 0; index < jsonSnapshots.Size(); ++index) {
			const JSONValue& row = jsonSnapshots.ChildAt(index);
			Snapshot snapshot;
			snapshot.name = str(row["name"].AsString());
			snapshot.id = row["id"].IsString() ? str(row["id"].AsString()) : snapshot.name;
			const JSONValue& values = row["values"];
			for (size_t entry = 0; entry < values.Size(); ++entry) {
				const JSONValue& valueRow = values.ChildAt(entry);
				Value value;
				if (!ReadKey(valueRow, value.group, value.parameter, value.target, value.effectParameter)) {
					refuse(std::format("snapshot '{}' has unknown parameter '{}'", snapshot.name, valueRow["parameter"].AsString()));
				}
				value.value = static_cast<f32>(valueRow["value"].AsNumber(static_cast<f64>(GetDefaultValue(value.parameter))));
				snapshot.values.push_back(std::move(value));
			}
			const JSONValue& transitions = row["transitions"];
			for (size_t entry = 0; entry < transitions.Size(); ++entry) {
				const JSONValue& transitionRow = transitions.ChildAt(entry);
				TransitionOverride transition;
				if (!ReadKey(transitionRow, transition.group, transition.parameter, transition.target, transition.effectParameter)) {
					refuse(std::format("snapshot '{}' overrides the transition of parameter '{}'", snapshot.name, transitionRow["parameter"].AsString()));
				}
				if (!ParseName(transitionRow["type"].AsString(), TRANSITION_NAMES, transition.type)) {
					refuse(std::format("snapshot '{}' has unknown transition '{}'", snapshot.name, transitionRow["type"].AsString()));
				}
				snapshot.transitions.push_back(std::move(transition));
			}
			readSnapshots.push_back(std::move(snapshot));
		}

		const JSONValue& jsonExposed = root["exposedParameters"];
		for (size_t index = 0; index < jsonExposed.Size(); ++index) {
			const JSONValue& row = jsonExposed.ChildAt(index);
			ExposedParameter exposed;
			exposed.name = str(row["name"].AsString());
			if (!ReadKey(row, exposed.group, exposed.parameter, exposed.target, exposed.effectParameter)) {
				refuse(std::format("'{}' exposes parameter '{}'", exposed.name, row["parameter"].AsString()));
			}
			readExposed.push_back(std::move(exposed));
		}

		Vector<str> readViews{GetMemoryResource(MemTag::Asset)};
		const JSONValue& jsonViews = root["views"];
		if (jsonViews.Size() > 64) {
			refuse(str("more than 64 views (a mixer holds at most 64)"));
		}
		for (size_t index = 0; index < jsonViews.Size() && index < 64; ++index) {
			const JSONValue& row = jsonViews.ChildAt(index);
			readViews.push_back(str(row["name"].AsString()));
			const JSONValue& shown = row["groups"];
			for (size_t entry = 0; entry < shown.Size(); ++entry) {
				for (Group& group : readGroups) {
					if (group.id == shown.ChildAt(entry).AsString()) {
						group.shownIn |= u64(1) << index;
					}
				}
			}
		}

		if (readGroups.empty() && readSnapshots.empty()) {
			Group master;
			master.id = "Master";
			master.name = "Master";
			master.units.push_back(Unit{});
			readGroups.push_back(std::move(master));
			Snapshot snapshot;
			snapshot.id = "Snapshot";
			snapshot.name = "Snapshot";
			readSnapshots.push_back(std::move(snapshot));
		}

		if (!problem.empty()) {
			SLT_LOG_ERROR("Audio", "the mixer '{}' wasn't read: {}", sourcePath.ToString(), problem);
			return false;
		}

		std::swap(groups, readGroups);
		std::swap(snapshots, readSnapshots);
		std::swap(exposedParameters, readExposed);
		std::swap(views, readViews);
		const AudioMixerUpdateMode previousMode = updateMode;
		const Guid previousOutput = outputMixer;
		str previousGroup = outputGroup;
		str previousStart = startSnapshot;
		updateMode = readMode;
		outputMixer = readOutputMixer;
		outputGroup = readOutputGroup;
		startSnapshot = readStart;
		str error;
		if (!ValidateMixer(*this, error)) {
			std::swap(groups, readGroups);
			std::swap(snapshots, readSnapshots);
			std::swap(exposedParameters, readExposed);
			std::swap(views, readViews);
			updateMode = previousMode;
			outputMixer = previousOutput;
			outputGroup = std::move(previousGroup);
			startSnapshot = std::move(previousStart);
			SLT_LOG_ERROR("Audio", "the mixer '{}' isn't a mixer: {}", sourcePath.ToString(), error);
			return false;
		}
		return true;
	}

	namespace {

		constexpr size_t BYTES_PER_GROUP = sizeof(u32) * 3 + sizeof(u8) * 3 + sizeof(u64) + sizeof(u32);
		constexpr size_t BYTES_PER_UNIT = sizeof(u8) + sizeof(u32) * 4 + sizeof(u8);
		constexpr size_t BYTES_PER_SNAPSHOT = sizeof(u32) * 3;
		constexpr size_t BYTES_PER_VALUE = sizeof(u32) + sizeof(u8) + sizeof(u32) * 2 + sizeof(f32);
		constexpr size_t BYTES_PER_TRANSITION = sizeof(u32) + sizeof(u8) + sizeof(u32) * 2 + sizeof(u8);
		constexpr size_t BYTES_PER_EXPOSED = sizeof(u32) * 2 + sizeof(u8) + sizeof(u32) * 2;
		constexpr size_t BYTES_PER_PRODUCT = sizeof(u32) + sizeof(Guid);
		constexpr size_t BYTES_PER_VIEW = sizeof(u32);

		constexpr u8 FLAG_FOLLOW_TIME_SCALE = 1u << 0;
		constexpr u8 FLAG_MUTE = 1u << 1;
		constexpr u8 FLAG_SOLO = 1u << 2;
		constexpr u8 FLAG_BYPASS_EFFECTS = 1u << 3;

		constexpr u8 UNIT_FLAG_BYPASS = 1u << 0;
		constexpr u8 UNIT_FLAG_WET_MIXING = 1u << 1;

	} // anonymous namespace

	bool AudioMixer::Cook(CookWriter& descriptor, CookWriter& payload) const {
		using namespace AudioImpl;
		(void)payload;

		descriptor.Write(static_cast<u8>(updateMode));
		descriptor.WriteAssetRef(outputMixer);
		descriptor.WriteString(outputGroup);
		descriptor.WriteString(startSnapshot);

		descriptor.Write(static_cast<u32>(groups.size()));
		for (const Group& group : groups) {
			descriptor.WriteString(group.id);
			descriptor.WriteString(group.name);
			descriptor.WriteString(group.parent);
			descriptor.Write(static_cast<u8>(group.clock));
			descriptor.Write(static_cast<u8>((group.followTimeScale ? FLAG_FOLLOW_TIME_SCALE : 0) | (group.mute ? FLAG_MUTE : 0) |
											 (group.solo ? FLAG_SOLO : 0) | (group.bypassEffects ? FLAG_BYPASS_EFFECTS : 0)));
			descriptor.Write(static_cast<u8>(group.color));
			descriptor.Write(group.shownIn);
			descriptor.Write(static_cast<u32>(group.units.size()));
			for (const Unit& unit : group.units) {
				descriptor.Write(static_cast<u8>(unit.kind));
				descriptor.WriteString(unit.target);
				descriptor.WriteString(unit.targetUnit);
				descriptor.WriteString(unit.id);
				descriptor.WriteString(unit.effect);
				descriptor.Write(static_cast<u8>((unit.bypass ? UNIT_FLAG_BYPASS : 0) | (unit.wetMixing ? UNIT_FLAG_WET_MIXING : 0)));
			}
		}

		Vector<ParameterRef> parameters{GetMemoryResource(MemTag::Asset)};
		CollectParameters(*this, parameters);
		Vector<const Value*> effectValues{GetMemoryResource(MemTag::Asset)};
		Vector<const TransitionOverride*> transitionRows{GetMemoryResource(MemTag::Asset)};
		descriptor.Write(static_cast<u32>(snapshots.size()));
		for (size_t index = 0; index < snapshots.size(); ++index) {
			descriptor.WriteString(snapshots[index].id);
			descriptor.WriteString(snapshots[index].name);
			CollectEffectParameters(*this, static_cast<u32>(index), effectValues);
			descriptor.Write(static_cast<u32>(parameters.size() + effectValues.size()));
			for (const ParameterRef& parameter : parameters) {
				WriteKey(descriptor, parameter.group, parameter.parameter, parameter.target, parameter.effectParameter);
				descriptor.Write(SnapshotValue(*this, static_cast<u32>(index), parameter.group, parameter.parameter, parameter.target, {}));
			}
			for (const Value* value : effectValues) {
				WriteKey(descriptor, value->group, value->parameter, value->target, value->effectParameter);
				descriptor.Write(value->value);
			}
			CollectTransitions(*this, static_cast<u32>(index), transitionRows);
			descriptor.Write(static_cast<u32>(transitionRows.size()));
			for (const TransitionOverride* transition : transitionRows) {
				WriteKey(descriptor, transition->group, transition->parameter, transition->target, transition->effectParameter);
				descriptor.Write(static_cast<u8>(transition->type));
			}
		}

		descriptor.Write(static_cast<u32>(exposedParameters.size()));
		for (const ExposedParameter& exposed : exposedParameters) {
			descriptor.WriteString(exposed.name);
			WriteKey(descriptor, exposed.group, exposed.parameter, exposed.target, exposed.effectParameter);
		}

		descriptor.Write(static_cast<u32>(_products.size()));
		for (const Product& product : _products) {
			descriptor.WriteString(product.key);
			descriptor.WriteAssetRef(product.guid);
		}

		descriptor.Write(static_cast<u32>(views.size()));
		for (const str& view : views) {
			descriptor.WriteString(view);
		}
		return true;
	}

	bool AudioMixer::LoadCooked(CookReader& descriptor, CookReader& payload) {
		using namespace AudioImpl;
		(void)payload;

		u8 mode = 0;
		descriptor.Read(mode);
		Guid readOutputMixer;
		descriptor.Read(readOutputMixer);
		const str readOutputGroup = str(descriptor.ReadString());
		const str readStart = str(descriptor.ReadString());
		if (descriptor.Failed() || mode > static_cast<u8>(AudioMixerUpdateMode::UnscaledTime)) {
			return false;
		}

		Vector<Group> readGroups{GetMemoryResource(MemTag::Asset)};
		u32 count = 0;
		descriptor.Read(count);
		if (descriptor.Failed() || descriptor.Remaining() < static_cast<size_t>(count) * BYTES_PER_GROUP) {
			return false;
		}
		readGroups.reserve(count);
		for (u32 index = 0; index < count; ++index) {
			Group group;
			group.id = str(descriptor.ReadString());
			group.name = str(descriptor.ReadString());
			group.parent = str(descriptor.ReadString());
			u8 clock = 0;
			u8 flags = 0;
			u8 color = 0;
			descriptor.Read(clock);
			descriptor.Read(flags);
			descriptor.Read(color);
			descriptor.Read(group.shownIn);
			if (clock > static_cast<u8>(AudioMixerGroupClock::Gameplay) || color > static_cast<u8>(AudioMixerGroupColor::Green)) {
				return false;
			}
			group.clock = static_cast<AudioMixerGroupClock>(clock);
			group.color = static_cast<AudioMixerGroupColor>(color);
			group.followTimeScale = (flags & FLAG_FOLLOW_TIME_SCALE) != 0;
			group.mute = (flags & FLAG_MUTE) != 0;
			group.solo = (flags & FLAG_SOLO) != 0;
			group.bypassEffects = (flags & FLAG_BYPASS_EFFECTS) != 0;
			u32 units = 0;
			descriptor.Read(units);
			if (descriptor.Failed() || descriptor.Remaining() < static_cast<size_t>(units) * BYTES_PER_UNIT) {
				return false;
			}
			group.units.reserve(units);
			for (u32 entry = 0; entry < units; ++entry) {
				Unit unit;
				u8 kind = 0;
				descriptor.Read(kind);
				if (kind > static_cast<u8>(AudioMixerUnitKind::Receive)) {
					return false;
				}
				unit.kind = static_cast<AudioMixerUnitKind>(kind);
				unit.target = str(descriptor.ReadString());
				unit.targetUnit = str(descriptor.ReadString());
				unit.id = str(descriptor.ReadString());
				unit.effect = str(descriptor.ReadString());
				u8 unitFlags = 0;
				descriptor.Read(unitFlags);
				unit.bypass = (unitFlags & UNIT_FLAG_BYPASS) != 0;
				unit.wetMixing = (unitFlags & UNIT_FLAG_WET_MIXING) != 0;
				group.units.push_back(std::move(unit));
			}
			readGroups.push_back(std::move(group));
		}

		Vector<Snapshot> readSnapshots{GetMemoryResource(MemTag::Asset)};
		descriptor.Read(count);
		if (descriptor.Failed() || descriptor.Remaining() < static_cast<size_t>(count) * BYTES_PER_SNAPSHOT) {
			return false;
		}
		readSnapshots.reserve(count);
		for (u32 index = 0; index < count; ++index) {
			Snapshot snapshot;
			snapshot.id = str(descriptor.ReadString());
			snapshot.name = str(descriptor.ReadString());
			u32 values = 0;
			descriptor.Read(values);
			if (descriptor.Failed() || descriptor.Remaining() < static_cast<size_t>(values) * BYTES_PER_VALUE) {
				return false;
			}
			snapshot.values.reserve(values);
			for (u32 entry = 0; entry < values; ++entry) {
				Value value;
				if (!ReadKey(descriptor, value.group, value.parameter, value.target, value.effectParameter)) {
					return false;
				}
				descriptor.Read(value.value);
				snapshot.values.push_back(std::move(value));
			}
			u32 transitions = 0;
			descriptor.Read(transitions);
			if (descriptor.Failed() || descriptor.Remaining() < static_cast<size_t>(transitions) * BYTES_PER_TRANSITION) {
				return false;
			}
			snapshot.transitions.reserve(transitions);
			for (u32 entry = 0; entry < transitions; ++entry) {
				TransitionOverride transition;
				u8 type = 0;
				const bool named = ReadKey(descriptor, transition.group, transition.parameter, transition.target, transition.effectParameter);
				descriptor.Read(type);
				if (!named || type > static_cast<u8>(AudioMixerTransitionType::Attenuation)) {
					return false;
				}
				transition.type = static_cast<AudioMixerTransitionType>(type);
				snapshot.transitions.push_back(std::move(transition));
			}
			readSnapshots.push_back(std::move(snapshot));
		}

		Vector<ExposedParameter> readExposed{GetMemoryResource(MemTag::Asset)};
		descriptor.Read(count);
		if (descriptor.Failed() || descriptor.Remaining() < static_cast<size_t>(count) * BYTES_PER_EXPOSED) {
			return false;
		}
		readExposed.reserve(count);
		for (u32 index = 0; index < count; ++index) {
			ExposedParameter exposed;
			exposed.name = str(descriptor.ReadString());
			if (!ReadKey(descriptor, exposed.group, exposed.parameter, exposed.target, exposed.effectParameter)) {
				return false;
			}
			readExposed.push_back(std::move(exposed));
		}

		Vector<Product> readProducts{GetMemoryResource(MemTag::Asset)};
		descriptor.Read(count);
		if (descriptor.Failed() || descriptor.Remaining() < static_cast<size_t>(count) * BYTES_PER_PRODUCT) {
			return false;
		}
		readProducts.reserve(count);
		for (u32 index = 0; index < count; ++index) {
			Product product;
			product.key = str(descriptor.ReadString());
			descriptor.Read(product.guid);
			readProducts.push_back(std::move(product));
		}

		Vector<str> readViews{GetMemoryResource(MemTag::Asset)};
		descriptor.Read(count);
		if (descriptor.Failed() || count > 64 || descriptor.Remaining() < static_cast<size_t>(count) * BYTES_PER_VIEW) {
			return false;
		}
		readViews.reserve(count);
		for (u32 index = 0; index < count; ++index) {
			readViews.push_back(str(descriptor.ReadString()));
		}
		if (descriptor.Failed()) {
			return false;
		}

		groups = std::move(readGroups);
		snapshots = std::move(readSnapshots);
		exposedParameters = std::move(readExposed);
		_products = std::move(readProducts);
		views = std::move(readViews);
		updateMode = static_cast<AudioMixerUpdateMode>(mode);
		outputMixer = readOutputMixer;
		outputGroup = readOutputGroup;
		startSnapshot = readStart;
		str error;
		if (!ValidateMixer(*this, error)) {
			SLT_LOG_ERROR("Audio", "a cooked mixer isn't a mixer: {}", error);
			groups.clear();
			snapshots.clear();
			exposedParameters.clear();
			_products.clear();
			views.clear();
			return false;
		}
		return true;
	}

	AudioMixerGroup::~AudioMixerGroup() = default;

	bool AudioMixerGroup::Cook(CookWriter& descriptor, CookWriter& payload) const {
		(void)payload;
		descriptor.WriteAssetRef(_mixer);
		descriptor.WriteString(_groupId);
		return true;
	}

	bool AudioMixerGroup::LoadCooked(CookReader& descriptor, CookReader& payload) {
		(void)payload;
		Guid mixer;
		descriptor.Read(mixer);
		const std::string_view id = descriptor.ReadString();
		if (descriptor.Failed() || !mixer.IsValid() || id.empty()) {
			return false;
		}
		_mixer = mixer;
		_groupId = str(id);
		return true;
	}

	namespace AudioImpl {

		AudioMixer* ResolveProductMixer(const Guid& mixer, Handle<AudioMixer>& held, bool& holds, bool& refused, std::string_view kind,
										std::string_view id, std::string_view consequence) {
			if (AudioMixer* live = held.Get()) {
				return live;
			}
			holds = false;
			if (!mixer.IsValid() || refused) {
				return nullptr;
			}
			Asset* resolved = AssetDatabase::LoadAssetByGuid(mixer);
			if (!resolved || resolved->GetAssetTypeId() != AudioMixer::GetReflectType()->name) {
				if (resolved) {
					AssetDatabase::UnloadAsset(resolved);
				}
				refused = true;
				SLT_LOG_WARN("Audio", "the mixer {} '{}' uses mixer {} (not a loadable mixer, {})", kind, id, mixer.ToString(), consequence);
				return nullptr;
			}
			auto* loaded = static_cast<AudioMixer*>(resolved);
			held = Handle<AudioMixer>(loaded);
			holds = true;
			return loaded;
		}

	} // namespace AudioImpl

	AudioMixer* AudioMixerGroup::ResolveMixer() const {
		return AudioImpl::ResolveProductMixer(_mixer, _audioMixer, _holdsMixer, _mixerRefused, "group", _groupId,
											  "routed sources play straight to the listener");
	}

	Handle<AudioMixer> AudioMixerGroup::GetAudioMixer() const {
		ResolveMixer();
		return _audioMixer;
	}

	void AudioMixerGroup::OnUnload() {
		AudioMixer* mixer = _audioMixer.Get();
		if (mixer && _holdsMixer) {
			AssetDatabase::UnloadAsset(mixer);
		}
		_audioMixer = {};
		_holdsMixer = false;
		_mixerRefused = false;
		_bus = {};
		_busRevision = 0;
	}

	AudioBus AudioMixerGroup::GetBus() const {
		const AudioMixer* mixer = ResolveMixer();
		const AudioImpl::AudioMixerLive* live = mixer ? AudioImpl::AudioMixerAccess::GetLive(*mixer) : nullptr;
		if (!live || !live->built) {
			return {};
		}
		if (_busRevision != live->revision) {
			_bus = mixer->GetBus(_groupId);
			_busRevision = live->revision;
		}
		return _bus;
	}

	AudioMixerSnapshot::~AudioMixerSnapshot() = default;

	bool AudioMixerSnapshot::Cook(CookWriter& descriptor, CookWriter& payload) const {
		(void)payload;
		descriptor.WriteAssetRef(_mixer);
		descriptor.WriteString(_snapshotId);
		return true;
	}

	bool AudioMixerSnapshot::LoadCooked(CookReader& descriptor, CookReader& payload) {
		(void)payload;
		Guid mixer;
		descriptor.Read(mixer);
		const std::string_view id = descriptor.ReadString();
		if (descriptor.Failed() || !mixer.IsValid() || id.empty()) {
			return false;
		}
		_mixer = mixer;
		_snapshotId = str(id);
		return true;
	}

	AudioMixer* AudioMixerSnapshot::ResolveMixer() const {
		return AudioImpl::ResolveProductMixer(_mixer, _audioMixer, _holdsMixer, _mixerRefused, "snapshot", _snapshotId,
											  "transitions do nothing");
	}

	Handle<AudioMixer> AudioMixerSnapshot::GetAudioMixer() const {
		ResolveMixer();
		return _audioMixer;
	}

	bool AudioMixerSnapshot::TransitionTo(f32 timeToReach) {
		AudioMixer* mixer = ResolveMixer();
		return mixer && mixer->TransitionToSnapshot(_snapshotId, timeToReach);
	}

	void AudioMixerSnapshot::OnUnload() {
		AudioMixer* mixer = _audioMixer.Get();
		if (mixer && _holdsMixer) {
			AssetDatabase::UnloadAsset(mixer);
		}
		_audioMixer = {};
		_holdsMixer = false;
		_mixerRefused = false;
	}

	namespace {

		class AudioMixerImporter final : public IAssetImporter {
		public:
			StringHash GetAssetType() const override { return AudioMixer::GetReflectType()->name; }

			Asset* Import(const Path& sourcePath) override {
				auto* mixer = New<AudioMixer>(MemTag::Asset);
				if (!mixer->Import(sourcePath)) {
					Delete(mixer);
					return nullptr;
				}
				return mixer;
			}

			Asset* ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue& settings,
									  AssetImportContext& context) override {
				(void)settings;
				Asset* imported = Import(sourcePath);
				auto* mixer = static_cast<AudioMixer*>(imported);
				if (!mixer) {
					return nullptr;
				}
				const Guid mixerGuid = AssetDatabase::LookupGuid(virtualPath);
				auto record = [&](const str& key, Asset* product, std::string_view what, std::string_view id, std::string_view name) {
					const Guid identity = context.AddProduct(key, product, name);
					if (identity.IsValid()) {
						AudioImpl::AudioMixerAccess::AddProduct(*mixer, key, identity);
					} else {
						SLT_LOG_WARN("Audio", "'{}': couldn't add the {} '{}' to the import", virtualPath.ToString(), what, id);
					}
				};
				for (const AudioMixer::Group& group : mixer->groups) {
					auto* product = New<AudioMixerGroup>(MemTag::Asset);
					AudioImpl::AudioMixerAccess::InitGroup(*product, mixerGuid, group.id);
					record(std::format("Groups/{}", group.id), product, "group", group.id, group.name.empty() ? group.id : group.name);
				}
				for (const AudioMixer::Snapshot& snapshot : mixer->snapshots) {
					auto* product = New<AudioMixerSnapshot>(MemTag::Asset);
					AudioImpl::AudioMixerAccess::InitSnapshot(*product, mixerGuid, snapshot.id);
					record(std::format("Snapshots/{}", snapshot.id), product, "snapshot", snapshot.id, snapshot.name.empty() ? snapshot.id : snapshot.name);
				}
				return mixer;
			}

			u32 GetImporterVersion() const override { return 7; }

			bool CanCreateAsset() const override { return true; }
			std::string_view GetCreateMenuLabel() const override { return "Audio Mixer"; }
			std::string_view GetCreateDefaultFileName() const override { return "New Audio Mixer.mixer"; }

			Asset* CreateAsset() override {
				auto* mixer = New<AudioMixer>(MemTag::Asset);
				AudioMixer::Group master;
				master.id = "Master";
				master.name = "Master";
				master.units.push_back(AudioMixer::Unit{});
				mixer->groups.push_back(std::move(master));
				AudioMixer::Snapshot snapshot;
				snapshot.id = "Snapshot";
				snapshot.name = "Snapshot";
				mixer->snapshots.push_back(std::move(snapshot));
				mixer->startSnapshot = "Snapshot";
				return mixer;
			}
		};

		AudioMixerImporter _importer;

	} // anonymous namespace

	namespace AudioImpl {

		void RegisterMixerImporter(const void* owner) {
			AssetImporterRegistry::Register(AudioMixer::GetReflectType()->name, &_importer, {".mixer"}, owner);
		}

	} // namespace AudioImpl

} // namespace Shalltear
