#include "AudioMixerEditOps.h"

#include "AudioBus.h"
#include "AudioEffectRegistry.h"
#include "AudioMixerLive.h"
#include "AudioRenderer.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace Shalltear::AudioImpl::AudioMixerEditOps {

	namespace {

		bool HasGroup(const AudioMixer& mixer, std::string_view id) { return FindGroup(mixer, id) >= 0; }

		str FreeId(const AudioMixer& mixer, std::string_view name) {
			str base(name.empty() ? NEW_GROUP_NAME : name);
			std::replace(base.begin(), base.end(), '/', '_');
			if (!HasGroup(mixer, base)) {
				return base;
			}
			for (u32 number = 2;; ++number) {
				str candidate = std::format("{} {}", base, number);
				if (!HasGroup(mixer, candidate)) {
					return candidate;
				}
			}
		}

		void Visit(const AudioMixer& mixer, u32 index, u32 depth, Vector<GroupEntry>& out) {
			if (depth > mixer.groups.size()) {
				return;
			}
			GroupEntry entry;
			entry.index = index;
			entry.depth = depth;
			const size_t at = out.size();
			out.push_back(entry);
			const str& id = mixer.groups[index].id;
			for (u32 child = 0; child < mixer.groups.size(); ++child) {
				if (child != index && mixer.groups[child].parent == id) {
					out[at].hasChildren = true;
					Visit(mixer, child, depth + 1, out);
				}
			}
		}

		void Subtree(const AudioMixer& mixer, std::string_view group, Vector<str>& out) {
			const i32 index = FindGroup(mixer, group);
			if (index < 0) {
				return;
			}
			Vector<GroupEntry> entries{GetMemoryResource(MemTag::Audio)};
			Visit(mixer, static_cast<u32>(index), 0, entries);
			for (const GroupEntry& entry : entries) {
				out.push_back(mixer.groups[entry.index].id);
			}
		}

		bool Contains(std::span<const str> ids, std::string_view id) {
			return std::find(ids.begin(), ids.end(), id) != ids.end();
		}

		bool WouldCycle(const AudioMixer& mixer, std::string_view group, std::string_view parent) {
			const size_t count = mixer.groups.size();
			Vector<u8> state(count, 0, GetMemoryResource(MemTag::Audio));
			Vector<u32> stack{GetMemoryResource(MemTag::Audio)};
			Vector<u32> next{GetMemoryResource(MemTag::Audio)};
			auto edges = [&](u32 from, Vector<u32>& to) {
				to.clear();
				const AudioMixer::Group& node = mixer.groups[from];
				const std::string_view up = node.id == group ? parent : std::string_view(node.parent);
				if (const i32 index = FindGroup(mixer, up); index >= 0) {
					to.push_back(static_cast<u32>(index));
				}
				for (const AudioMixer::Unit& unit : node.units) {
					if (unit.kind == AudioMixerUnitKind::Send) {
						if (const i32 index = FindGroup(mixer, unit.target); index >= 0) {
							to.push_back(static_cast<u32>(index));
						}
					}
				}
			};
			for (u32 root = 0; root < count; ++root) {
				if (state[root] != 0) {
					continue;
				}
				stack.push_back(root);
				while (!stack.empty()) {
					const u32 node = stack.back();
					if (state[node] == 0) {
						state[node] = 1;
					}
					edges(node, next);
					bool descended = false;
					for (const u32 target : next) {
						if (state[target] == 1) {
							return true;
						}
						if (state[target] == 0) {
							stack.push_back(target);
							descended = true;
							break;
						}
					}
					if (!descended) {
						state[node] = 2;
						stack.pop_back();
					}
				}
			}
			return false;
		}

	} // anonymous namespace

	void Flatten(const AudioMixer& mixer, Vector<GroupEntry>& out) {
		out.clear();
		const i32 top = FindTop(mixer);
		if (top >= 0) {
			Visit(mixer, static_cast<u32>(top), 0, out);
		}
	}

	i32 FindTop(const AudioMixer& mixer) {
		for (u32 index = 0; index < mixer.groups.size(); ++index) {
			if (mixer.groups[index].parent.empty()) {
				return static_cast<i32>(index);
			}
		}
		return -1;
	}

	bool IsUnder(const AudioMixer& mixer, std::string_view group, std::string_view ancestor) {
		std::string_view at = group;
		for (size_t steps = 0; steps <= mixer.groups.size(); ++steps) {
			if (at == ancestor) {
				return true;
			}
			const i32 index = FindGroup(mixer, at);
			if (index < 0) {
				return false;
			}
			at = mixer.groups[static_cast<size_t>(index)].parent;
		}
		return false;
	}

	str AddGroup(AudioMixer& mixer, std::string_view parent, std::string_view name) {
		const i32 above = FindGroup(mixer, parent);
		if (above < 0) {
			return {};
		}
		AudioMixer::Group group;
		group.id = FreeId(mixer, name);
		group.name = str(name.empty() ? NEW_GROUP_NAME : name);
		group.parent = str(parent);
		group.clock = mixer.groups[static_cast<size_t>(above)].clock;
		group.followTimeScale = mixer.groups[static_cast<size_t>(above)].followTimeScale;
		AudioMixer::Unit fader;
		fader.kind = AudioMixerUnitKind::Attenuation;
		group.units.push_back(fader);
		const str id = group.id;
		mixer.groups.push_back(std::move(group));
		return id;
	}

	void DuplicateGroups(AudioMixer& mixer, std::span<const str> groups, u32 view, Vector<str>& copies) {
		copies.clear();
		const i32 top = FindTop(mixer);
		for (const str& root : groups) {
			const i32 index = FindGroup(mixer, root);
			if (index < 0 || index == top) {
				continue;
			}
			bool nested = false;
			for (const str& other : groups) {
				if (other != root && IsUnder(mixer, root, other)) {
					nested = true;
					break;
				}
			}
			if (nested) {
				continue;
			}

			Vector<str> originals{GetMemoryResource(MemTag::Audio)};
			Subtree(mixer, root, originals);
			Vector<str> renamed{GetMemoryResource(MemTag::Audio)};
			Vector<AudioMixer::Group> made{GetMemoryResource(MemTag::Asset)};
			for (const str& original : originals) {
				const AudioMixer::Group& source = mixer.groups[static_cast<size_t>(FindGroup(mixer, original))];
				AudioMixer::Group copy = source;
				if (original == root) {
					copy.name = source.name + " - Copy";
				}
				copy.shownIn = view < 64 ? source.shownIn & (u64(1) << view) : 0;
				copy.id = FreeId(mixer, copy.name);
				mixer.groups.push_back(copy);
				made.push_back(std::move(copy));
				renamed.push_back(made.back().id);
			}
			mixer.groups.resize(mixer.groups.size() - made.size());

			auto mapped = [&](std::string_view id) -> std::string_view {
				for (size_t at = 0; at < originals.size(); ++at) {
					if (originals[at] == id) {
						return renamed[at];
					}
				}
				return id;
			};
			for (AudioMixer::Group& copy : made) {
				if (copy.id != renamed.front()) {
					copy.parent = str(mapped(copy.parent));
				}
				for (AudioMixer::Unit& unit : copy.units) {
					if (unit.kind == AudioMixerUnitKind::Send) {
						unit.target = str(mapped(unit.target));
					}
				}
			}
			for (AudioMixer::Snapshot& snapshot : mixer.snapshots) {
				const size_t count = snapshot.values.size();
				for (size_t value = 0; value < count; ++value) {
					const AudioMixer::Value& source = snapshot.values[value];
					if (!Contains(originals, source.group)) {
						continue;
					}
					AudioMixer::Value copy = source;
					copy.group = str(mapped(source.group));
					if (copy.parameter == AudioMixerParameter::SendLevel) {
						copy.target = str(mapped(source.target));
					}
					snapshot.values.push_back(std::move(copy));
				}
				const size_t overrides = snapshot.transitions.size();
				for (size_t transition = 0; transition < overrides; ++transition) {
					const AudioMixer::TransitionOverride& source = snapshot.transitions[transition];
					if (!Contains(originals, source.group)) {
						continue;
					}
					AudioMixer::TransitionOverride copy = source;
					copy.group = str(mapped(source.group));
					if (copy.parameter == AudioMixerParameter::SendLevel) {
						copy.target = str(mapped(source.target));
					}
					snapshot.transitions.push_back(std::move(copy));
				}
			}
			for (AudioMixer::Group& copy : made) {
				mixer.groups.push_back(std::move(copy));
			}
			copies.push_back(renamed.front());
		}
	}

	u32 RemoveGroups(AudioMixer& mixer, std::span<const str> groups) {
		const i32 top = FindTop(mixer);
		Vector<str> going{GetMemoryResource(MemTag::Audio)};
		for (const str& group : groups) {
			const i32 index = FindGroup(mixer, group);
			if (index < 0 || index == top || Contains(going, group)) {
				continue;
			}
			Subtree(mixer, group, going);
		}
		if (going.empty()) {
			return 0;
		}

		const size_t before = mixer.groups.size();
		std::erase_if(mixer.groups, [&](const AudioMixer::Group& group) { return Contains(going, group.id); });
		for (AudioMixer::Group& group : mixer.groups) {
			std::erase_if(group.units, [&](const AudioMixer::Unit& unit) {
				return unit.kind == AudioMixerUnitKind::Send && Contains(going, unit.target);
			});
		}
		auto names = [&](std::string_view group, AudioMixerParameter parameter, std::string_view target) {
			return Contains(going, group) || (parameter == AudioMixerParameter::SendLevel && Contains(going, target));
		};
		for (AudioMixer::Snapshot& snapshot : mixer.snapshots) {
			std::erase_if(snapshot.values, [&](const AudioMixer::Value& value) { return names(value.group, value.parameter, value.target); });
			std::erase_if(snapshot.transitions,
						  [&](const AudioMixer::TransitionOverride& transition) { return names(transition.group, transition.parameter, transition.target); });
		}
		std::erase_if(mixer.exposedParameters, [&](const AudioMixer::ExposedParameter& exposed) {
			return names(exposed.group, exposed.parameter, exposed.target);
		});
		return static_cast<u32>(before - mixer.groups.size());
	}

	bool CanMoveGroup(const AudioMixer& mixer, std::string_view group, std::string_view parent) {
		const i32 index = FindGroup(mixer, group);
		if (index < 0 || index == FindTop(mixer) || !HasGroup(mixer, parent) || IsUnder(mixer, parent, group)) {
			return false;
		}
		return !WouldCycle(mixer, group, parent);
	}

	bool MoveGroup(AudioMixer& mixer, std::string_view group, std::string_view parent, u32 slot) {
		if (!CanMoveGroup(mixer, group, parent)) {
			return false;
		}
		const i32 index = FindGroup(mixer, group);
		AudioMixer::Group moved = std::move(mixer.groups[static_cast<size_t>(index)]);
		mixer.groups.erase(mixer.groups.begin() + index);
		moved.parent = str(parent);

		size_t at = mixer.groups.size();
		u32 seen = 0;
		i32 last = -1;
		for (size_t candidate = 0; candidate < mixer.groups.size(); ++candidate) {
			if (mixer.groups[candidate].parent != parent) {
				continue;
			}
			if (seen == slot) {
				at = candidate;
				break;
			}
			++seen;
			last = static_cast<i32>(candidate);
		}
		if (at == mixer.groups.size() && last >= 0) {
			at = static_cast<size_t>(last) + 1;
		}
		mixer.groups.insert(mixer.groups.begin() + static_cast<std::ptrdiff_t>(at), std::move(moved));
		return true;
	}

	bool RenameGroup(AudioMixer& mixer, std::string_view group, std::string_view name) {
		const i32 index = FindGroup(mixer, group);
		if (index < 0 || name.empty()) {
			return false;
		}
		mixer.groups[static_cast<size_t>(index)].name = str(name);
		return true;
	}

	f32 DecibelsToFader(f32 decibels) {
		const f32 value = std::clamp(decibels, MIN_DECIBELS, MAX_DECIBELS);
		if (value > 0.0f) {
			return FADER_ZERO - std::pow(value / MAX_DECIBELS, 1.0f / FADER_WARP) * FADER_ZERO;
		}
		return FADER_ZERO + std::pow(value / MIN_DECIBELS, 1.0f / FADER_WARP) * (1.0f - FADER_ZERO);
	}

	f32 FaderToDecibels(f32 share) {
		const f32 at = std::clamp(share, 0.0f, 1.0f);
		if (at < FADER_ZERO) {
			return std::pow(1.0f - at / FADER_ZERO, FADER_WARP) * MAX_DECIBELS;
		}
		return std::pow((at - FADER_ZERO) / (1.0f - FADER_ZERO), FADER_WARP) * MIN_DECIBELS;
	}

	f32 GetGroupVolume(const AudioMixer& mixer, u32 snapshot, std::string_view group) {
		return GetValue(mixer, snapshot, {str(group), AudioMixerParameter::Volume, {}, {}});
	}

	bool SetGroupVolume(AudioMixer& mixer, u32 snapshot, std::string_view group, f32 decibels) {
		return SetValue(mixer, snapshot, {str(group), AudioMixerParameter::Volume, {}, {}}, decibels);
	}

	bool GetLiveGroupVolume(const AudioMixer& mixer, std::string_view group, f32& decibels) {
		return GetLiveValue(mixer, {str(group), AudioMixerParameter::Volume, {}, {}}, decibels);
	}

	f32 LevelToDecibels(f32 linear) {
		if (!(linear > 1.0e-4f)) {
			return MIN_DECIBELS;
		}
		return 20.0f * std::log10(linear);
	}

	namespace {

		AudioMixer::Group* GroupOf(AudioMixer& mixer, std::string_view id) {
			const i32 index = FindGroup(mixer, id);
			return index >= 0 ? &mixer.groups[static_cast<size_t>(index)] : nullptr;
		}

		const AudioMixer::Group* GroupOf(const AudioMixer& mixer, std::string_view id) {
			const i32 index = FindGroup(mixer, id);
			return index >= 0 ? &mixer.groups[static_cast<size_t>(index)] : nullptr;
		}

		bool IsSidechainTarget(std::string_view effect) {
			const IAudioEffectDefinition* definition = FindEffect(effect);
			return definition && definition->IsSidechainTarget();
		}

		template <typename Keyed>
		bool Names(const Keyed& keyed, const ParameterRef& ref) {
			return keyed.group == ref.group && keyed.parameter == ref.parameter && keyed.target == ref.target && keyed.effectParameter == ref.effectParameter;
		}

		str FreeUnitId(const AudioMixer::Group& group, std::string_view name) {
			str base(name);
			std::replace(base.begin(), base.end(), '/', '_');
			auto taken = [&group](std::string_view id) {
				for (const AudioMixer::Unit& unit : group.units) {
					if ((unit.kind == AudioMixerUnitKind::Effect || unit.kind == AudioMixerUnitKind::Receive) && unit.id == id) {
						return true;
					}
				}
				return false;
			};
			if (!taken(base)) {
				return base;
			}
			for (u32 number = 2;; ++number) {
				str candidate = std::format("{} {}", base, number);
				if (!taken(candidate)) {
					return candidate;
				}
			}
		}

		template <typename Predicate>
		void Prune(AudioMixer& mixer, Predicate drop) {
			for (AudioMixer::Snapshot& snapshot : mixer.snapshots) {
				std::erase_if(snapshot.values, [&](const AudioMixer::Value& value) { return drop(value.group, value.parameter, value.target); });
				std::erase_if(snapshot.transitions,
							  [&](const AudioMixer::TransitionOverride& transition) { return drop(transition.group, transition.parameter, transition.target); });
			}
			std::erase_if(mixer.exposedParameters,
						  [&](const AudioMixer::ExposedParameter& exposed) { return drop(exposed.group, exposed.parameter, exposed.target); });
		}

		void RekeySendLevel(AudioMixer& mixer, std::string_view group, std::string_view from, std::string_view to) {
			if (from == to || from.empty()) {
				return;
			}
			auto moves = [&](std::string_view valueGroup, AudioMixerParameter parameter, std::string_view target) {
				return valueGroup == group && parameter == AudioMixerParameter::SendLevel && target == from;
			};
			if (to.empty()) {
				Prune(mixer, moves);
				return;
			}
			for (AudioMixer::Snapshot& snapshot : mixer.snapshots) {
				for (AudioMixer::Value& value : snapshot.values) {
					if (moves(value.group, value.parameter, value.target)) {
						value.target = str(to);
					}
				}
				for (AudioMixer::TransitionOverride& transition : snapshot.transitions) {
					if (moves(transition.group, transition.parameter, transition.target)) {
						transition.target = str(to);
					}
				}
			}
			for (AudioMixer::ExposedParameter& exposed : mixer.exposedParameters) {
				if (moves(exposed.group, exposed.parameter, exposed.target)) {
					exposed.target = str(to);
				}
			}
		}

		bool Reaches(const AudioMixer& mixer, std::string_view from, std::string_view to, std::string_view skipGroup, u32 skip) {
			const size_t count = mixer.groups.size();
			Vector<u8> seen(count, 0, GetMemoryResource(MemTag::Audio));
			Vector<u32> pending{GetMemoryResource(MemTag::Audio)};
			const i32 start = FindGroup(mixer, from);
			if (start < 0) {
				return false;
			}
			pending.push_back(static_cast<u32>(start));
			seen[static_cast<size_t>(start)] = 1;
			while (!pending.empty()) {
				const AudioMixer::Group& node = mixer.groups[pending.back()];
				pending.pop_back();
				if (node.id == to) {
					return true;
				}
				auto visit = [&](std::string_view id) {
					const i32 index = FindGroup(mixer, id);
					if (index >= 0 && seen[static_cast<size_t>(index)] == 0) {
						seen[static_cast<size_t>(index)] = 1;
						pending.push_back(static_cast<u32>(index));
					}
				};
				visit(node.parent);
				for (u32 unit = 0; unit < node.units.size(); ++unit) {
					if (node.units[unit].kind == AudioMixerUnitKind::Send && !(node.id == skipGroup && unit == skip)) {
						visit(node.units[unit].target);
					}
				}
			}
			return false;
		}

		void UnitParameters(const AudioMixer& mixer, u32 snapshot, std::string_view group, const AudioMixer::Unit& unit, bool wet, Vector<ParameterRef>& out) {
			switch (unit.kind) {
			case AudioMixerUnitKind::Attenuation:
				out.push_back({str(group), AudioMixerParameter::Volume, {}, {}});
				return;
			case AudioMixerUnitKind::Send:
				if (!unit.target.empty()) {
					out.push_back({str(group), AudioMixerParameter::SendLevel, unit.target, {}});
				}
				return;
			case AudioMixerUnitKind::Receive:
				return;
			case AudioMixerUnitKind::Effect:
				break;
			}
			if (wet && unit.wetMixing) {
				out.push_back({str(group), AudioMixerParameter::WetLevel, unit.id, {}});
			}
			const size_t first = out.size();
			if (const IAudioEffectDefinition* definition = FindEffect(unit.effect)) {
				for (const AudioParameterDefinition& parameter : definition->GetParameters()) {
					out.push_back({str(group), AudioMixerParameter::EffectParameter, unit.id, str(parameter.name)});
				}
			}
			for (const AudioMixer::Value& value : mixer.snapshots[snapshot].values) {
				if (value.group != group || value.parameter != AudioMixerParameter::EffectParameter || value.target != unit.id) {
					continue;
				}
				const bool listed = std::any_of(out.begin() + static_cast<std::ptrdiff_t>(first), out.end(),
												[&value](const ParameterRef& ref) { return ref.effectParameter == value.effectParameter; });
				if (!listed) {
					out.push_back({str(group), AudioMixerParameter::EffectParameter, unit.id, value.effectParameter});
				}
			}
		}

		void Write(AudioMixer& mixer, u32 snapshot, const ParameterRef& ref, f32 value) {
			for (AudioMixer::Value& existing : mixer.snapshots[snapshot].values) {
				if (Names(existing, ref)) {
					existing.value = value;
					return;
				}
			}
			AudioMixer::Value added;
			added.group = ref.group;
			added.parameter = ref.parameter;
			added.target = ref.target;
			added.effectParameter = ref.effectParameter;
			added.value = value;
			mixer.snapshots[snapshot].values.push_back(std::move(added));
		}

		void CopyToAll(AudioMixer& mixer, u32 snapshot, std::span<const ParameterRef> refs) {
			for (const ParameterRef& ref : refs) {
				const f32 value = GetValue(mixer, snapshot, ref);
				for (u32 other = 0; other < mixer.snapshots.size(); ++other) {
					if (other != snapshot) {
						Write(mixer, other, ref, value);
					}
				}
			}
		}

	} // anonymous namespace

	bool GetRange(const AudioMixer& mixer, const ParameterRef& ref, ParameterRange& out) {
		out = ParameterRange{};
		switch (ref.parameter) {
		case AudioMixerParameter::Volume:
			out = {MIN_DECIBELS, MAX_DECIBELS, 0.0f, 1.0f, "dB"};
			return true;
		case AudioMixerParameter::Pitch:
			out = {MIN_PITCH, MAX_PITCH, 1.0f, 100.0f, "%"};
			return true;
		case AudioMixerParameter::SendLevel:
			out = {MIN_DECIBELS, MAX_SEND_DECIBELS, MIN_DECIBELS, 1.0f, "dB"};
			return true;
		case AudioMixerParameter::WetLevel:
			out = {0.0f, 100.0f, 100.0f, 1.0f, "%"};
			return true;
		case AudioMixerParameter::EffectParameter:
			break;
		}
		const AudioMixer::Group* group = GroupOf(mixer, ref.group);
		if (!group) {
			return false;
		}
		for (const AudioMixer::Unit& unit : group->units) {
			if (unit.kind != AudioMixerUnitKind::Effect || unit.id != ref.target) {
				continue;
			}
			const IAudioEffectDefinition* definition = FindEffect(unit.effect);
			const u32 slot = definition ? FindEffectParameter(*definition, ref.effectParameter) : NO_EFFECT_PARAMETER;
			if (slot == NO_EFFECT_PARAMETER) {
				return false;
			}
			const AudioParameterDefinition& parameter = definition->GetParameters()[slot];
			out = {parameter.minimum, parameter.maximum, parameter.defaultValue, 1.0f, parameter.unit};
			return true;
		}
		return false;
	}

	f32 GetValue(const AudioMixer& mixer, u32 snapshot, const ParameterRef& ref) {
		return SnapshotValue(mixer, snapshot, ref.group, ref.parameter, ref.target, ref.effectParameter);
	}

	bool SetValue(AudioMixer& mixer, u32 snapshot, const ParameterRef& ref, f32 value) {
		ParameterRange range;
		if (snapshot >= mixer.snapshots.size() || !ParameterExists(mixer, ref.group, ref.parameter, ref.target, ref.effectParameter) ||
			!GetRange(mixer, ref, range)) {
			return false;
		}
		Write(mixer, snapshot, ref, value == value ? std::clamp(value, range.minimum, range.maximum) : range.defaultValue);
		return true;
	}

	bool GetLiveValue(const AudioMixer& mixer, const ParameterRef& ref, f32& value) {
		const AudioMixerLive* live = AudioMixerAccess::GetLive(mixer);
		if (!live || !live->live) {
			return false;
		}
		for (const AudioMixerLiveParameter& parameter : live->parameters) {
			if (Names(parameter, ref)) {
				value = parameter.Effective();
				return true;
			}
		}
		return false;
	}

	i32 AddUnit(AudioMixer& mixer, std::string_view group, u32 index, AudioMixerUnitKind kind, std::string_view effect) {
		AudioMixer::Group* into = GroupOf(mixer, group);
		if (!into || into->units.size() >= MAX_BUS_ROWS || kind == AudioMixerUnitKind::Attenuation || (kind == AudioMixerUnitKind::Effect && effect.empty())) {
			return -1;
		}
		AudioMixer::Unit unit;
		unit.kind = kind;
		if (kind == AudioMixerUnitKind::Effect) {
			unit.effect = str(effect);
			unit.id = FreeUnitId(*into, effect);
		} else if (kind == AudioMixerUnitKind::Receive) {
			unit.id = FreeUnitId(*into, "Receive");
		}
		const size_t at = std::min<size_t>(index, into->units.size());
		into->units.insert(into->units.begin() + static_cast<std::ptrdiff_t>(at), std::move(unit));
		return static_cast<i32>(at);
	}

	bool RemoveUnit(AudioMixer& mixer, std::string_view group, u32 index) {
		AudioMixer::Group* from = GroupOf(mixer, group);
		if (!from || index >= from->units.size() || from->units[index].kind == AudioMixerUnitKind::Attenuation) {
			return false;
		}
		const AudioMixer::Unit gone = from->units[index];
		from->units.erase(from->units.begin() + index);
		if (gone.kind == AudioMixerUnitKind::Send) {
			RekeySendLevel(mixer, group, gone.target, {});
			return true;
		}
		Prune(mixer, [&](std::string_view valueGroup, AudioMixerParameter parameter, std::string_view target) {
			return valueGroup == group && (parameter == AudioMixerParameter::WetLevel || parameter == AudioMixerParameter::EffectParameter) && target == gone.id;
		});
		for (AudioMixer::Group& sender : mixer.groups) {
			for (AudioMixer::Unit& unit : sender.units) {
				if (unit.kind == AudioMixerUnitKind::Send && unit.target == group && unit.targetUnit == gone.id) {
					const str target = unit.target;
					unit.target.clear();
					unit.targetUnit.clear();
					RekeySendLevel(mixer, sender.id, target, {});
				}
			}
		}
		return true;
	}

	bool MoveUnit(AudioMixer& mixer, std::string_view group, u32 from, u32 to) {
		AudioMixer::Group* in = GroupOf(mixer, group);
		if (!in || from >= in->units.size()) {
			return false;
		}
		size_t at = std::min<size_t>(to, in->units.size());
		if (at == from || at == static_cast<size_t>(from) + 1) {
			return false;
		}
		AudioMixer::Unit moved = std::move(in->units[from]);
		in->units.erase(in->units.begin() + from);
		at -= at > from ? 1 : 0;
		in->units.insert(in->units.begin() + static_cast<std::ptrdiff_t>(at), std::move(moved));
		return true;
	}

	bool SetWetMixing(AudioMixer& mixer, std::string_view group, u32 index, bool on) {
		AudioMixer::Group* in = GroupOf(mixer, group);
		if (!in || index >= in->units.size() || in->units[index].kind != AudioMixerUnitKind::Effect || in->units[index].wetMixing == on) {
			return false;
		}
		in->units[index].wetMixing = on;
		if (!on) {
			const str id = in->units[index].id;
			Prune(mixer, [&](std::string_view valueGroup, AudioMixerParameter parameter, std::string_view target) {
				return valueGroup == group && parameter == AudioMixerParameter::WetLevel && target == id;
			});
		}
		return true;
	}

	void ListSendTargets(const AudioMixer& mixer, std::string_view group, u32 index, Vector<SendTarget>& out) {
		out.clear();
		const AudioMixer::Group* from = GroupOf(mixer, group);
		if (!from || index >= from->units.size() || from->units[index].kind != AudioMixerUnitKind::Send) {
			return;
		}
		Vector<GroupEntry> order{GetMemoryResource(MemTag::Audio)};
		Flatten(mixer, order);
		for (const GroupEntry& entry : order) {
			const AudioMixer::Group& candidate = mixer.groups[entry.index];
			if (candidate.id == group) {
				continue;
			}
			bool taken = false;
			for (u32 other = 0; other < from->units.size(); ++other) {
				const AudioMixer::Unit& unit = from->units[other];
				taken = taken || (other != index && unit.kind == AudioMixerUnitKind::Send && unit.target == candidate.id);
			}
			if (taken) {
				continue;
			}
			const bool loops = Reaches(mixer, candidate.id, group, group, index);
			out.push_back({candidate.id, {}, loops});
			for (const AudioMixer::Unit& unit : candidate.units) {
				if (unit.kind == AudioMixerUnitKind::Receive || (unit.kind == AudioMixerUnitKind::Effect && IsSidechainTarget(unit.effect))) {
					out.push_back({candidate.id, unit.id, loops});
				}
			}
		}
	}

	bool RetargetSend(AudioMixer& mixer, std::string_view group, u32 index, std::string_view target, std::string_view targetUnit) {
		AudioMixer::Group* from = GroupOf(mixer, group);
		if (!from || index >= from->units.size() || from->units[index].kind != AudioMixerUnitKind::Send) {
			return false;
		}
		const AudioMixer::Unit& send = from->units[index];
		if (send.target == target && send.targetUnit == targetUnit) {
			return false;
		}
		if (target.empty()) {
			if (!targetUnit.empty()) {
				return false;
			}
		} else {
			Vector<SendTarget> allowed{GetMemoryResource(MemTag::Audio)};
			ListSendTargets(mixer, group, index, allowed);
			const bool found = std::any_of(allowed.begin(), allowed.end(),
										   [&](const SendTarget& place) { return place.group == target && place.unit == targetUnit && !place.loops; });
			if (!found) {
				return false;
			}
		}
		const str previous = send.target;
		from->units[index].target = str(target);
		from->units[index].targetUnit = str(targetUnit);
		RekeySendLevel(mixer, group, previous, target);
		return true;
	}

	void CopyUnitToAllSnapshots(AudioMixer& mixer, u32 snapshot, std::string_view group, u32 index, bool wet) {
		const AudioMixer::Group* in = GroupOf(mixer, group);
		if (!in || index >= in->units.size() || snapshot >= mixer.snapshots.size()) {
			return;
		}
		Vector<ParameterRef> refs{GetMemoryResource(MemTag::Audio)};
		UnitParameters(mixer, snapshot, group, in->units[index], wet, refs);
		CopyToAll(mixer, snapshot, refs);
	}

	void CopyGroupToAllSnapshots(AudioMixer& mixer, u32 snapshot, std::string_view group) {
		const AudioMixer::Group* in = GroupOf(mixer, group);
		if (!in || snapshot >= mixer.snapshots.size()) {
			return;
		}
		Vector<ParameterRef> refs{GetMemoryResource(MemTag::Audio)};
		refs.push_back({str(group), AudioMixerParameter::Pitch, {}, {}});
		for (const AudioMixer::Unit& unit : in->units) {
			UnitParameters(mixer, snapshot, group, unit, true, refs);
		}
		CopyToAll(mixer, snapshot, refs);
	}

	namespace {

		bool SnapshotNamed(const AudioMixer& mixer, std::string_view name, u32 except) {
			for (u32 index = 0; index < mixer.snapshots.size(); ++index) {
				if (index != except && mixer.snapshots[index].name == name) {
					return true;
				}
			}
			return false;
		}

		str FreeSnapshotName(const AudioMixer& mixer, std::string_view name, u32 except) {
			str candidate(name);
			for (u32 suffix = 1; SnapshotNamed(mixer, candidate, except); ++suffix) {
				candidate = std::format("{} ({:02})", name, suffix);
			}
			return candidate;
		}

		str FreeSnapshotId(const AudioMixer& mixer, std::string_view name) {
			str base(name.empty() ? std::string_view("Snapshot") : name);
			std::replace(base.begin(), base.end(), '/', '_');
			str candidate = base;
			for (u32 number = 2; FindSnapshot(mixer, candidate) < mixer.snapshots.size(); ++number) {
				candidate = std::format("{} {}", base, number);
			}
			return candidate;
		}

		void Renumber(AudioMixer& mixer, const Vector<u32>& moved) {
			AudioMixerLive* live = AudioMixerAccess::GetLive(mixer);
			if (!live) {
				return;
			}
			size_t kept = 0;
			for (size_t index = 0; index < live->heading.size() && index < live->headingWeights.size(); ++index) {
				const u32 was = live->heading[index];
				if (was < moved.size() && moved[was] < mixer.snapshots.size()) {
					live->heading[kept] = moved[was];
					live->headingWeights[kept] = live->headingWeights[index];
					++kept;
				}
			}
			live->heading.resize(kept);
			live->headingWeights.resize(kept);
		}

		void PinStart(AudioMixer& mixer) {
			if (mixer.startSnapshot.empty() && !mixer.snapshots.empty()) {
				mixer.startSnapshot = mixer.snapshots.front().id;
			}
		}

	} // anonymous namespace

	u32 FindSnapshot(const AudioMixer& mixer, std::string_view id) {
		for (u32 index = 0; index < mixer.snapshots.size(); ++index) {
			if (mixer.snapshots[index].id == id) {
				return index;
			}
		}
		return static_cast<u32>(mixer.snapshots.size());
	}

	i32 DuplicateSnapshot(AudioMixer& mixer, u32 from) {
		if (from >= mixer.snapshots.size()) {
			return -1;
		}
		AudioMixer::Snapshot copy = mixer.snapshots[from];
		const str name = FreeSnapshotName(mixer, std::format("{}{}", copy.name, COPY_SUFFIX), ~0u);
		copy.id = FreeSnapshotId(mixer, name);
		copy.name = name;
		mixer.snapshots.push_back(std::move(copy));
		return static_cast<i32>(mixer.snapshots.size() - 1);
	}

	bool RenameSnapshot(AudioMixer& mixer, u32 index, std::string_view name) {
		if (index >= mixer.snapshots.size() || name.empty() || mixer.snapshots[index].name == name) {
			return false;
		}
		mixer.snapshots[index].name = FreeSnapshotName(mixer, name, index);
		return true;
	}

	bool RemoveSnapshot(AudioMixer& mixer, u32 index) {
		if (index >= mixer.snapshots.size() || mixer.snapshots.size() < 2) {
			return false;
		}
		if (StartSnapshotIndex(mixer) == index) {
			mixer.startSnapshot.clear();
		} else {
			PinStart(mixer);
		}
		const u32 count = static_cast<u32>(mixer.snapshots.size());
		mixer.snapshots.erase(mixer.snapshots.begin() + index);
		Vector<u32> moved(count, 0, GetMemoryResource(MemTag::Audio));
		for (u32 old = 0; old < count; ++old) {
			moved[old] = old < index ? old : (old == index ? count : old - 1);
		}
		Renumber(mixer, moved);
		return true;
	}

	bool MoveSnapshot(AudioMixer& mixer, u32 from, u32 to) {
		const u32 count = static_cast<u32>(mixer.snapshots.size());
		if (from >= count) {
			return false;
		}
		const u32 before = std::min(to, count);
		if (before == from || before == from + 1) {
			return false;
		}
		PinStart(mixer);
		AudioMixer::Snapshot moved = std::move(mixer.snapshots[from]);
		mixer.snapshots.erase(mixer.snapshots.begin() + from);
		const u32 at = before > from ? before - 1 : before;
		mixer.snapshots.insert(mixer.snapshots.begin() + at, std::move(moved));
		Vector<u32> places(count, 0, GetMemoryResource(MemTag::Audio));
		for (u32 old = 0; old < count; ++old) {
			const u32 without = old > from ? old - 1 : old;
			places[old] = old == from ? at : (without >= at ? without + 1 : without);
		}
		Renumber(mixer, places);
		return true;
	}

	bool SetStartSnapshot(AudioMixer& mixer, u32 index) {
		if (index >= mixer.snapshots.size() || StartSnapshotIndex(mixer) == index) {
			return false;
		}
		mixer.startSnapshot = mixer.snapshots[index].id;
		return true;
	}

	namespace {

		void OwnViews(AudioMixer& mixer) {
			if (!mixer.views.empty()) {
				return;
			}
			mixer.views.push_back(str(DEFAULT_VIEW_NAME));
			for (AudioMixer::Group& group : mixer.groups) {
				group.shownIn = 1;
			}
		}

		u64 Bit(u32 view) { return u64(1) << view; }

		void Rearrange(AudioMixer& mixer, const Vector<u32>& from) {
			for (AudioMixer::Group& group : mixer.groups) {
				u64 bits = 0;
				for (u32 view = 0; view < from.size(); ++view) {
					bits |= ((group.shownIn >> from[view]) & 1u) << view;
				}
				group.shownIn = bits;
			}
		}

	} // anonymous namespace

	u32 CountViews(const AudioMixer& mixer) { return mixer.views.empty() ? 1u : static_cast<u32>(mixer.views.size()); }

	std::string_view ViewName(const AudioMixer& mixer, u32 view) {
		if (mixer.views.empty()) {
			return view == 0 ? DEFAULT_VIEW_NAME : std::string_view();
		}
		return view < mixer.views.size() ? std::string_view(mixer.views[view]) : std::string_view();
	}

	bool IsVisible(const AudioMixer& mixer, u32 view, std::string_view group) {
		if (mixer.views.empty()) {
			return view == 0;
		}
		const i32 index = FindGroup(mixer, group);
		return view < mixer.views.size() && index >= 0 && (mixer.groups[static_cast<size_t>(index)].shownIn & Bit(view)) != 0;
	}

	i32 DuplicateView(AudioMixer& mixer, u32 from) {
		if (from >= CountViews(mixer) || mixer.views.size() >= MAX_VIEWS) {
			return -1;
		}
		OwnViews(mixer);
		const u32 copy = static_cast<u32>(mixer.views.size());
		mixer.views.push_back(mixer.views[from] + str(COPY_SUFFIX));
		for (AudioMixer::Group& group : mixer.groups) {
			group.shownIn |= ((group.shownIn >> from) & 1u) << copy;
		}
		return static_cast<i32>(copy);
	}

	bool RenameView(AudioMixer& mixer, u32 view, std::string_view name) {
		if (view >= CountViews(mixer) || name.empty() || ViewName(mixer, view) == name) {
			return false;
		}
		OwnViews(mixer);
		mixer.views[view] = str(name);
		return true;
	}

	bool RemoveView(AudioMixer& mixer, u32 view) {
		const u32 count = static_cast<u32>(mixer.views.size());
		if (view >= count || count < 2) {
			return false;
		}
		mixer.views.erase(mixer.views.begin() + view);
		Vector<u32> from{GetMemoryResource(MemTag::Audio)};
		for (u32 place = 0; place + 1 < count; ++place) {
			from.push_back(place < view ? place : place + 1);
		}
		Rearrange(mixer, from);
		return true;
	}

	bool MoveView(AudioMixer& mixer, u32 from, u32 to) {
		const u32 count = static_cast<u32>(mixer.views.size());
		const u32 before = std::min(to, count);
		if (from >= count || before == from || before == from + 1) {
			return false;
		}
		const u32 at = before > from ? before - 1 : before;
		str moved = std::move(mixer.views[from]);
		mixer.views.erase(mixer.views.begin() + from);
		mixer.views.insert(mixer.views.begin() + at, std::move(moved));
		Vector<u32> places{GetMemoryResource(MemTag::Audio)};
		for (u32 place = 0; place < count; ++place) {
			places.push_back(place);
		}
		places.erase(places.begin() + from);
		places.insert(places.begin() + at, from);
		Rearrange(mixer, places);
		return true;
	}

	bool SetVisible(AudioMixer& mixer, u32 view, std::span<const str> groups, bool visible) {
		if (view >= CountViews(mixer) || (visible && mixer.views.empty())) {
			return false;
		}
		OwnViews(mixer);
		bool changed = false;
		for (const str& id : groups) {
			const i32 index = FindGroup(mixer, id);
			if (index < 0) {
				continue;
			}
			u64& bits = mixer.groups[static_cast<size_t>(index)].shownIn;
			const u64 was = bits;
			bits = visible ? bits | Bit(view) : bits & ~Bit(view);
			changed = changed || bits != was;
		}
		return changed;
	}

	bool SetColor(AudioMixer& mixer, std::span<const str> groups, AudioMixerGroupColor color) {
		bool changed = false;
		for (const str& id : groups) {
			const i32 index = FindGroup(mixer, id);
			if (index >= 0 && mixer.groups[static_cast<size_t>(index)].color != color) {
				mixer.groups[static_cast<size_t>(index)].color = color;
				changed = true;
			}
		}
		return changed;
	}

	namespace {

		bool IsDigit(char c) { return c >= '0' && c <= '9'; }

		char Lower(char c) { return c >= 'A' && c <= 'Z' ? static_cast<char>(c + 32) : c; }

		bool NaturalLess(std::string_view a, std::string_view b) {
			size_t i = 0;
			size_t j = 0;
			while (i < a.size() && j < b.size()) {
				if (IsDigit(a[i]) && IsDigit(b[j])) {
					size_t endA = i;
					size_t endB = j;
					while (endA < a.size() && IsDigit(a[endA])) {
						++endA;
					}
					while (endB < b.size() && IsDigit(b[endB])) {
						++endB;
					}
					while (i + 1 < endA && a[i] == '0') {
						++i;
					}
					while (j + 1 < endB && b[j] == '0') {
						++j;
					}
					if (endA - i != endB - j) {
						return endA - i < endB - j;
					}
					if (const int order = a.substr(i, endA - i).compare(b.substr(j, endB - j)); order != 0) {
						return order < 0;
					}
					i = endA;
					j = endB;
					continue;
				}
				if (Lower(a[i]) != Lower(b[j])) {
					return Lower(a[i]) < Lower(b[j]);
				}
				++i;
				++j;
			}
			return a.size() - i < b.size() - j;
		}

		i32 FindListing(std::span<const MixerListing> mixers, const Guid& guid) {
			for (u32 index = 0; index < mixers.size(); ++index) {
				if (mixers[index].guid == guid) {
					return static_cast<i32>(index);
				}
			}
			return -1;
		}

		i32 OutputOf(std::span<const MixerListing> mixers, u32 index) {
			const AudioMixer* data = mixers[index].data;
			return data && data->outputMixer.IsValid() ? FindListing(mixers, data->outputMixer) : -1;
		}

		void Place(std::span<const MixerListing> mixers, std::span<const u32> order, Vector<u8>& placed, Vector<MixerRow>& out,
				   u32 index, u32 depth) {
			placed[index] = 1;
			const size_t at = out.size();
			out.push_back(MixerRow{index, depth, false});
			for (const u32 child : order) {
				if (!placed[child] && OutputOf(mixers, child) == static_cast<i32>(index)) {
					out[at].hasChildren = true;
					Place(mixers, order, placed, out, child, depth + 1);
				}
			}
		}

	} // anonymous namespace

	void ArrangeMixers(std::span<const MixerListing> mixers, Vector<MixerRow>& out) {
		out.clear();
		Vector<u32> order{GetMemoryResource(MemTag::Audio)};
		for (u32 index = 0; index < mixers.size(); ++index) {
			size_t at = order.size();
			while (at > 0 && NaturalLess(mixers[index].name, mixers[order[at - 1]].name)) {
				--at;
			}
			order.insert(order.begin() + static_cast<std::ptrdiff_t>(at), index);
		}
		Vector<u8> placed(mixers.size(), 0, GetMemoryResource(MemTag::Audio));
		for (const u32 index : order) {
			const i32 parent = OutputOf(mixers, index);
			if (!placed[index] && (parent < 0 || parent == static_cast<i32>(index))) {
				Place(mixers, order, placed, out, index, 0);
			}
		}
		for (const u32 index : order) {
			if (!placed[index]) {
				Place(mixers, order, placed, out, index, 0);
			}
		}
	}

	str DescribeOutput(std::span<const MixerListing> mixers, const MixerListing& mixer) {
		const AudioMixer* data = mixer.data;
		if (!data || !data->outputMixer.IsValid()) {
			return "(Audio Listener)";
		}
		const i32 at = FindListing(mixers, data->outputMixer);
		if (at < 0) {
			return std::format("({} of a missing mixer)", data->outputGroup);
		}
		const MixerListing& owner = mixers[static_cast<size_t>(at)];
		std::string_view group = data->outputGroup;
		if (owner.data) {
			if (const i32 found = FindGroup(*owner.data, group); found >= 0) {
				group = owner.data->groups[static_cast<size_t>(found)].name;
			}
		}
		return std::format("({} of {})", group, owner.name);
	}

	bool CanRouteInto(std::span<const MixerListing> mixers, const Guid& dragged, const Guid& onto) {
		i32 at = FindListing(mixers, onto);
		for (size_t steps = 0; at >= 0 && steps <= mixers.size(); ++steps) {
			if (mixers[static_cast<size_t>(at)].guid == dragged) {
				return false;
			}
			at = OutputOf(mixers, static_cast<u32>(at));
		}
		return dragged != onto;
	}

	namespace {

		std::string_view NameOfGroup(const AudioMixer& mixer, std::string_view id) {
			const i32 index = FindGroup(mixer, id);
			return index >= 0 ? std::string_view(mixer.groups[static_cast<size_t>(index)].name) : id;
		}

		str FreeExposedName(const AudioMixer& mixer, std::string_view name, i32 except) {
			auto taken = [&](std::string_view candidate) {
				for (size_t index = 0; index < mixer.exposedParameters.size(); ++index) {
					if (static_cast<i32>(index) != except && mixer.exposedParameters[index].name == candidate) {
						return true;
					}
				}
				return false;
			};
			str candidate(name);
			for (u32 counter = 1; taken(candidate); ++counter) {
				candidate = std::format("{} {}", name, counter);
			}
			return candidate;
		}

	} // anonymous namespace

	str DescribeParameter(const AudioMixer& mixer, const ParameterRef& ref, bool baseOnly) {
		const std::string_view group = NameOfGroup(mixer, ref.group);
		const bool unit = ref.parameter == AudioMixerParameter::WetLevel || ref.parameter == AudioMixerParameter::EffectParameter;
		const str base = unit ? std::format(" (of {}\xE2\x86\x92{})", group, ref.target) : std::format(" (of {})", group);
		if (baseOnly) {
			return base;
		}
		switch (ref.parameter) {
		case AudioMixerParameter::Volume:
			return "Volume" + base;
		case AudioMixerParameter::Pitch:
			return "Pitch" + base;
		case AudioMixerParameter::SendLevel:
			return std::format("Send level{} to {}", base, NameOfGroup(mixer, ref.target));
		case AudioMixerParameter::WetLevel:
			return "Mix Level" + base;
		case AudioMixerParameter::EffectParameter:
			return ref.effectParameter + base;
		}
		return base;
	}

	i32 FindExposed(const AudioMixer& mixer, const ParameterRef& ref) {
		for (size_t index = 0; index < mixer.exposedParameters.size(); ++index) {
			if (Names(mixer.exposedParameters[index], ref)) {
				return static_cast<i32>(index);
			}
		}
		return -1;
	}

	str ExposeParameter(AudioMixer& mixer, const ParameterRef& ref) {
		if (FindExposed(mixer, ref) >= 0 || !ParameterExists(mixer, ref.group, ref.parameter, ref.target, ref.effectParameter)) {
			return {};
		}
		AudioMixer::ExposedParameter exposed;
		exposed.name = FreeExposedName(mixer, NEW_EXPOSED_NAME, -1);
		exposed.group = ref.group;
		exposed.parameter = ref.parameter;
		exposed.target = ref.target;
		exposed.effectParameter = ref.effectParameter;
		const str name = exposed.name;
		mixer.exposedParameters.push_back(std::move(exposed));
		std::stable_sort(mixer.exposedParameters.begin(), mixer.exposedParameters.end(),
						 [&](const AudioMixer::ExposedParameter& a, const AudioMixer::ExposedParameter& b) {
							 return DescribeParameter(mixer, {a.group, a.parameter, a.target, a.effectParameter}, true) <
									DescribeParameter(mixer, {b.group, b.parameter, b.target, b.effectParameter}, true);
						 });
		return name;
	}

	bool UnexposeParameter(AudioMixer& mixer, const ParameterRef& ref) {
		const i32 index = FindExposed(mixer, ref);
		if (index < 0) {
			return false;
		}
		mixer.exposedParameters.erase(mixer.exposedParameters.begin() + index);
		return true;
	}

	bool RenameExposed(AudioMixer& mixer, u32 index, std::string_view name) {
		if (index >= mixer.exposedParameters.size() || name.empty()) {
			return false;
		}
		if (name.size() > MAX_EXPOSED_NAME) {
			name = name.substr(0, MAX_EXPOSED_NAME);
			SLT_LOG_WARN("Audio", "Maximum name length of an exposed parameter is {} characters (truncated to '{}')", MAX_EXPOSED_NAME, name);
		}
		const str free = FreeExposedName(mixer, name, static_cast<i32>(index));
		if (mixer.exposedParameters[index].name == free) {
			return false;
		}
		mixer.exposedParameters[index].name = free;
		return true;
	}

	AudioMixerTransitionType GetTransition(const AudioMixer& mixer, u32 snapshot, const ParameterRef& ref) {
		if (snapshot < mixer.snapshots.size()) {
			for (const AudioMixer::TransitionOverride& transition : mixer.snapshots[snapshot].transitions) {
				if (Names(transition, ref)) {
					return transition.type;
				}
			}
		}
		return AudioMixerTransitionType::Lerp;
	}

	bool SetTransition(AudioMixer& mixer, u32 snapshot, const ParameterRef& ref, AudioMixerTransitionType type) {
		if (snapshot >= mixer.snapshots.size() || type > AudioMixerTransitionType::Attenuation ||
			!ParameterExists(mixer, ref.group, ref.parameter, ref.target, ref.effectParameter) || GetTransition(mixer, snapshot, ref) == type) {
			return false;
		}
		Vector<AudioMixer::TransitionOverride>& transitions = mixer.snapshots[snapshot].transitions;
		std::erase_if(transitions, [&](const AudioMixer::TransitionOverride& transition) { return Names(transition, ref); });
		if (type != AudioMixerTransitionType::Lerp) {
			AudioMixer::TransitionOverride transition;
			transition.group = ref.group;
			transition.parameter = ref.parameter;
			transition.target = ref.target;
			transition.effectParameter = ref.effectParameter;
			transition.type = type;
			transitions.push_back(std::move(transition));
		}
		return true;
	}

	namespace {

		void Hold(const AudioMixer& mixer, AudioMixerLive& live, u32 snapshot, bool holding) {
			for (AudioMixerLiveParameter& parameter : live.parameters) {
				parameter.holding = holding;
				if (holding) {
					parameter.hold = SnapshotValue(mixer, snapshot, parameter.group, parameter.parameter, parameter.target, parameter.effectParameter);
				}
			}
			PushMixer(live, AudioRenderer::BLOCK_FRAMES);
		}

		void Resettle(const AudioMixer& mixer, AudioMixerLive& live) {
			const u32 start = StartSnapshotIndex(mixer);
			for (AudioMixerLiveParameter& parameter : live.parameters) {
				f32 sum = 0.0f;
				f32 total = 0.0f;
				for (size_t index = 0; index < live.heading.size() && index < live.headingWeights.size(); ++index) {
					const u32 at = live.heading[index];
					const f32 weight = live.headingWeights[index] > 0.0f ? live.headingWeights[index] : 0.0f;
					if (at < mixer.snapshots.size() && weight > 0.0f) {
						sum += weight * SnapshotValue(mixer, at, parameter.group, parameter.parameter, parameter.target, parameter.effectParameter);
						total += weight;
					}
				}
				parameter.to = total > 0.0f ? sum / total
											: SnapshotValue(mixer, start, parameter.group, parameter.parameter, parameter.target, parameter.effectParameter);
				if (!live.transitioning) {
					parameter.snapshot = parameter.to;
				}
			}
		}

	} // anonymous namespace

	bool AcquireHold(AudioMixer& mixer, u32 snapshot) {
		AudioMixerLive* live = AudioMixerAccess::GetLive(mixer);
		if (!live || !live->live || snapshot >= mixer.snapshots.size()) {
			return false;
		}
		++live->holds;
		Hold(mixer, *live, snapshot, true);
		return true;
	}

	void ReleaseHold(AudioMixer& mixer) {
		AudioMixerLive* live = AudioMixerAccess::GetLive(mixer);
		if (!live || live->holds == 0) {
			return;
		}
		if (--live->holds == 0) {
			Resettle(mixer, *live);
			Hold(mixer, *live, 0, false);
		}
	}

	void RefreshHold(AudioMixer& mixer, u32 snapshot) {
		AudioMixerLive* live = AudioMixerAccess::GetLive(mixer);
		if (live && live->holds > 0 && snapshot < mixer.snapshots.size()) {
			Hold(mixer, *live, snapshot, true);
		}
	}

	u32 CountHolds(const AudioMixer& mixer) {
		const AudioMixerLive* live = AudioMixerAccess::GetLive(mixer);
		return live && live->live ? live->holds : 0u;
	}

} // namespace Shalltear::AudioImpl::AudioMixerEditOps
