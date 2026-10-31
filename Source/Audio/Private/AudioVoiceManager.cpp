#include "AudioVoiceManager.h"

#include "Shalltear/Core/Memory.h"

#include <algorithm>
#include <new>

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			f32 Effective(const AudioVoiceRank& voice) {
				return voice.real ? voice.audibility * REAL_VOICE_HYSTERESIS : voice.audibility;
			}

			template <typename Pick>
			u32 OldestWhere(std::span<const AudioConcurrencyMember> members, Pick&& pick) {
				u32 victim = NO_VOICE_SLOT;
				u64 oldest = ~0ull;
				for (const AudioConcurrencyMember& member : members) {
					if (pick(member) && (victim == NO_VOICE_SLOT || member.sequence < oldest)) {
						victim = member.slot;
						oldest = member.sequence;
					}
				}
				return victim;
			}

			u32 Hash(u64 asset, u64 owner) {
				u64 mixed = asset * 0x9E3779B97F4A7C15ull ^ (owner + 0x632BE59BD9B4E019ull) * 0xC2B2AE3D27D4EB4Full;
				mixed ^= mixed >> 29;
				mixed *= 0xBF58476D1CE4E5B9ull;
				mixed ^= mixed >> 32;
				return static_cast<u32>(mixed);
			}

		} // anonymous namespace

		bool RanksAbove(const AudioVoiceRank& a, const AudioVoiceRank& b) {
			if (a.priority != b.priority) {
				return a.priority < b.priority;
			}
			const f32 left = Effective(a);
			const f32 right = Effective(b);
			if (left != right) {
				return left > right;
			}
			if (a.real != b.real) {
				return a.real;
			}
			if (a.sequence != b.sequence) {
				return a.sequence < b.sequence;
			}
			return a.slot < b.slot;
		}

		u32 SelectRealVoices(std::span<AudioVoiceRank> voices, u32 budget, std::span<u32> order) {
			const u32 count = static_cast<u32>(voices.size());
			if (count <= budget) {
				for (AudioVoiceRank& voice : voices) {
					voice.wantReal = true;
				}
				return count;
			}
			for (AudioVoiceRank& voice : voices) {
				voice.wantReal = false;
			}
			if (budget == 0) {
				return 0;
			}
			for (u32 index = 0; index < count; ++index) {
				order[index] = index;
			}
			std::nth_element(order.begin(), order.begin() + budget, order.begin() + count,
							 [&](u32 a, u32 b) { return RanksAbove(voices[a], voices[b]); });
			for (u32 index = 0; index < budget; ++index) {
				voices[order[index]].wantReal = true;
			}
			return budget;
		}

		u32 ChooseConcurrencyVictim(AudioConcurrencyRule rule, std::span<const AudioConcurrencyMember> members, const AudioConcurrencyMember& incoming) {
			if (members.empty()) {
				return NO_VOICE_SLOT;
			}
			switch (rule) {
			case AudioConcurrencyRule::PreventNew:
				return NO_VOICE_SLOT;

			case AudioConcurrencyRule::StopOldest:
				return OldestWhere(members, [](const AudioConcurrencyMember&) { return true; });

			case AudioConcurrencyRule::StopFarthestThenPreventNew:
			case AudioConcurrencyRule::StopFarthestThenOldest: {
				f32 farthest = members[0].distance;
				for (const AudioConcurrencyMember& member : members) {
					farthest = member.distance > farthest ? member.distance : farthest;
				}
				const bool preventOnTie = rule == AudioConcurrencyRule::StopFarthestThenPreventNew;
				if (incoming.distance > farthest || (preventOnTie && incoming.distance == farthest)) {
					return NO_VOICE_SLOT;
				}
				return OldestWhere(members, [farthest](const AudioConcurrencyMember& member) { return member.distance == farthest; });
			}

			case AudioConcurrencyRule::StopLowestPriority:
			case AudioConcurrencyRule::StopLowestPriorityThenPreventNew: {
				i32 lowest = members[0].priority;
				for (const AudioConcurrencyMember& member : members) {
					lowest = member.priority > lowest ? member.priority : lowest;
				}
				const bool preventOnTie = rule == AudioConcurrencyRule::StopLowestPriorityThenPreventNew;
				if (incoming.priority > lowest || (preventOnTie && incoming.priority == lowest)) {
					return NO_VOICE_SLOT;
				}
				return OldestWhere(members, [lowest](const AudioConcurrencyMember& member) { return member.priority == lowest; });
			}

			case AudioConcurrencyRule::StopQuietest: {
				f32 quietest = members[0].audibility;
				for (const AudioConcurrencyMember& member : members) {
					quietest = member.audibility < quietest ? member.audibility : quietest;
				}
				if (incoming.audibility < quietest) {
					return NO_VOICE_SLOT;
				}
				return OldestWhere(members, [quietest](const AudioConcurrencyMember& member) { return member.audibility == quietest; });
			}
			}
			return NO_VOICE_SLOT;
		}

		void AudioConcurrencyGroups::Initialize(u32 voiceCapacity) {
			Shutdown();
			u32 size = 16;
			while (size < 2 * voiceCapacity) {
				size <<= 1;
			}
			_entries = static_cast<AudioConcurrencyGroup*>(Allocate(sizeof(AudioConcurrencyGroup) * size, MemTag::Audio, alignof(AudioConcurrencyGroup)));
			for (u32 index = 0; index < size; ++index) {
				new (&_entries[index]) AudioConcurrencyGroup();
			}
			_mask = size - 1;
			_used = 0;
		}

		void AudioConcurrencyGroups::Shutdown() {
			if (!_entries) {
				return;
			}
			Free(_entries);
			_entries = nullptr;
			_mask = 0;
			_used = 0;
		}

		u32 AudioConcurrencyGroups::Probe(u64 asset, u64 owner) const {
			u32 index = Hash(asset, owner) & _mask;
			while (_entries[index].asset != 0 && !(_entries[index].asset == asset && _entries[index].owner == owner)) {
				index = (index + 1) & _mask;
			}
			return index;
		}

		AudioConcurrencyGroup* AudioConcurrencyGroups::Find(u64 asset, u64 owner) {
			if (!_entries || asset == 0) {
				return nullptr;
			}
			AudioConcurrencyGroup& entry = _entries[Probe(asset, owner)];
			return entry.asset != 0 ? &entry : nullptr;
		}

		AudioConcurrencyGroup* AudioConcurrencyGroups::FindOrAdd(u64 asset, u64 owner) {
			if (!_entries || asset == 0) {
				return nullptr;
			}
			if (AudioConcurrencyGroup* found = Find(asset, owner)) {
				return found;
			}
			if (_used >= (_mask + 1) / 2) {
				Sweep(~0ull);
			}
			if (_used + 1 >= _mask + 1) {
				return nullptr;
			}
			AudioConcurrencyGroup& entry = _entries[Probe(asset, owner)];
			entry = AudioConcurrencyGroup{};
			entry.asset = asset;
			entry.owner = owner;
			++_used;
			return &entry;
		}

		void AudioConcurrencyGroups::Remove(u32 index) {
			u32 hole = index;
			u32 next = (hole + 1) & _mask;
			while (_entries[next].asset != 0) {
				const u32 home = Hash(_entries[next].asset, _entries[next].owner) & _mask;
				const bool stays = hole <= next ? (hole < home && home <= next) : (hole < home || home <= next);
				if (!stays) {
					_entries[hole] = _entries[next];
					hole = next;
				}
				next = (next + 1) & _mask;
			}
			_entries[hole] = AudioConcurrencyGroup{};
			--_used;
		}

		void AudioConcurrencyGroups::Sweep(u64 now) {
			if (!_entries) {
				return;
			}
			for (u32 index = 0; index <= _mask;) {
				const AudioConcurrencyGroup& entry = _entries[index];
				const bool lapsed = now == ~0ull || !entry.started || now >= entry.lastStart + entry.retriggerFrames;
				if (entry.asset != 0 && entry.count == 0 && lapsed) {
					Remove(index);
					continue;
				}
				++index;
			}
		}

	} // namespace AudioImpl

} // namespace Shalltear
