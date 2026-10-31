#include "AudioVoiceTable.h"

#include "AudioClipData.h"
#include "AudioVoiceEffects.h"

#include "Shalltear/Core/Memory.h"

#include <new>

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			bool IsLive(const AudioVoiceRecord& record) {
				return record.claim == VoiceClaim::Playing || record.claim == VoiceClaim::Paused;
			}

			bool IsBudgeted(const AudioVoiceRecord& record) {
				return record.real && (IsLive(record) || (record.claim == VoiceClaim::Stopping && record.releasing));
			}

		} // anonymous namespace

		void AudioVoiceTable::Initialize(u32 limit, u32 headroom) {
			Shutdown();
			const u32 capacity = limit + headroom;
			if (capacity == 0) {
				return;
			}
			_records = static_cast<AudioVoiceRecord*>(Allocate(sizeof(AudioVoiceRecord) * capacity, MemTag::Audio, alignof(AudioVoiceRecord)));
			for (u32 slot = 0; slot < capacity; ++slot) {
				new (&_records[slot]) AudioVoiceRecord();
			}
			_realSlots = static_cast<u32*>(Allocate(sizeof(u32) * capacity, MemTag::Audio, alignof(u32)));
			_realCount = 0;
			_capacity = capacity;
			_limit = limit;
			_live = 0;
			_budgeted = 0;
			_generators = 0;
			_orphans = 0;
			_cursor = 0;
			_groups.Initialize(capacity);
		}

		void AudioVoiceTable::Shutdown() {
			if (!_records) {
				return;
			}
			for (u32 slot = 0; slot < _capacity; ++slot) {
				Release(slot);
				_records[slot].~AudioVoiceRecord();
			}
			Free(_records);
			Free(_realSlots);
			_records = nullptr;
			_realSlots = nullptr;
			_realCount = 0;
			_capacity = 0;
			_limit = 0;
			_live = 0;
			_budgeted = 0;
			_cursor = 0;
			_groups.Shutdown();
		}

		void AudioVoiceTable::Resize(u32 limit, u32 headroom) {
			if (!_records) {
				Initialize(limit, headroom);
				return;
			}
			const u32 capacity = limit + headroom;
			for (u32 slot = capacity; slot < _capacity; ++slot) {
				Account(_records[slot], -1);
				LeaveGroup(slot);
				Release(slot);
				_records[slot].claim = VoiceClaim::Free;
			}
			const u32 kept = capacity < _capacity ? capacity : _capacity;
			AudioVoiceRecord* records = static_cast<AudioVoiceRecord*>(Allocate(sizeof(AudioVoiceRecord) * (capacity > 0 ? capacity : 1), MemTag::Audio,
																				alignof(AudioVoiceRecord)));
			for (u32 slot = 0; slot < capacity; ++slot) {
				new (&records[slot]) AudioVoiceRecord(slot < kept ? _records[slot] : AudioVoiceRecord{});
			}
			for (u32 slot = 0; slot < _capacity; ++slot) {
				_records[slot].~AudioVoiceRecord();
			}
			Free(_records);
			Free(_realSlots);
			_records = records;
			_realSlots = static_cast<u32*>(Allocate(sizeof(u32) * (capacity > 0 ? capacity : 1), MemTag::Audio, alignof(u32)));
			_realCount = 0;
			_capacity = capacity;
			_limit = limit;
			_cursor = _cursor < capacity ? _cursor : 0;

			_groups.Initialize(capacity);
			for (u32 slot = 0; slot < capacity; ++slot) {
				_records[slot].groupNext = NO_VOICE_SLOT;
				_records[slot].groupPrevious = NO_VOICE_SLOT;
			}
			_live = 0;
			_budgeted = 0;
			for (u32 slot = 0; slot < capacity; ++slot) {
				AudioVoiceRecord& record = _records[slot];
				record.real = false;
				record.realIndex = NO_VOICE_SLOT;
				record.releasing = false;
				Account(record, +1);
				if (IsLive(record) && record.groupAsset != 0) {
					if (AudioConcurrencyGroup* group = _groups.FindOrAdd(record.groupAsset, record.groupOwner)) {
						record.groupNext = group->head;
						if (group->head != NO_VOICE_SLOT) {
							_records[group->head].groupPrevious = slot;
						}
						group->head = slot;
						++group->count;
					}
				} else {
					record.groupAsset = 0;
					record.groupOwner = 0;
				}
			}
		}

		void AudioVoiceTable::Account(const AudioVoiceRecord& record, i32 sign) {
			if (IsLive(record)) {
				_live = static_cast<u32>(static_cast<i32>(_live) + sign);
			}
			if (IsBudgeted(record)) {
				_budgeted = static_cast<u32>(static_cast<i32>(_budgeted) + sign);
			}
		}

		void AudioVoiceTable::Release(u32 slot) {
			AudioVoiceRecord& record = _records[slot];
			ReleaseClipData(record.clip);
			record.clip = nullptr;
			if (record.spatializer && record.spatializerState) {
				record.spatializer->DestroyVoiceState(record.spatializerState);
			}
			record.spatializer = nullptr;
			record.spatializerState = nullptr;
			record.placement = AudioVoicePlacement{};
			if (record.curves) {
				Delete(record.curves);
				record.curves = nullptr;
			}
			record.distanceShare = 0.0f;
			if (record.generatorInstance) {
				record.generatorInstance->Release();
			}
			record.generatorInstance = nullptr;
			if (record.effects) {
				ReleaseVoiceChain(record.effects->chain);
				Delete(record.effects);
				record.effects = nullptr;
			}
			record.generatorImage = nullptr;
			record.resource = {};
			record.loop = false;
			if (record.orphaned) {
				record.orphaned = false;
				--_orphans;
			}
			if (record.isGenerator) {
				record.isGenerator = false;
				--_generators;
			}
		}

		void AudioVoiceTable::SetGenerator(u32 slot, IAudioGeneratorInstance* instance, const void* image) {
			AudioVoiceRecord& record = _records[slot];
			if (!record.isGenerator) {
				record.isGenerator = true;
				++_generators;
			}
			record.generatorInstance = instance;
			record.generatorImage = image;
		}

		void AudioVoiceTable::SetOrphaned(u32 slot, bool orphaned) {
			AudioVoiceRecord& record = _records[slot];
			if (record.orphaned == orphaned) {
				return;
			}
			record.orphaned = orphaned;
			if (orphaned) {
				++_orphans;
			} else {
				--_orphans;
			}
		}

		bool AudioVoiceTable::Claim(AudioClipData* clip, u32& slot, u32& generation) {
			for (u32 step = 0; step < _capacity; ++step) {
				const u32 candidate = (_cursor + step) % _capacity;
				AudioVoiceRecord& record = _records[candidate];
				if (record.claim != VoiceClaim::Free) {
					continue;
				}
				const u32 next = record.generation + 1 == 0 ? 1 : record.generation + 1;
				record = AudioVoiceRecord{};
				record.generation = next;
				record.claim = VoiceClaim::Playing;
				record.clip = clip;
				record.sequence = ++_sequence;
				RetainClipData(clip);
				Account(record, +1);
				_cursor = (candidate + 1) % _capacity;
				slot = candidate;
				generation = record.generation;
				return true;
			}
			return false;
		}

		void AudioVoiceTable::Abandon(u32 slot) {
			if (slot >= _capacity) {
				return;
			}
			AudioVoiceRecord& record = _records[slot];
			Account(record, -1);
			LeaveGroup(slot);
			Release(slot);
			MarkReal(slot, false);
			record.claim = VoiceClaim::Free;
		}

		void AudioVoiceTable::OnEnded(u32 slot, u32 generation) {
			if (slot >= _capacity) {
				return;
			}
			AudioVoiceRecord& record = _records[slot];
			if (record.generation != generation || record.claim == VoiceClaim::Free) {
				return;
			}
			Account(record, -1);
			LeaveGroup(slot);
			Release(slot);
			MarkReal(slot, false);
			record.claim = VoiceClaim::Free;
			record.releasing = false;
		}

		AudioVoiceRecord* AudioVoiceTable::Find(u32 slot, u32 generation) {
			if (!_records || slot >= _capacity || generation == 0 || _records[slot].generation != generation) {
				return nullptr;
			}
			return &_records[slot];
		}

		void AudioVoiceTable::SetClaim(u32 slot, VoiceClaim claim) {
			AudioVoiceRecord& record = _records[slot];
			Account(record, -1);
			record.claim = claim;
			Account(record, +1);
		}

		void AudioVoiceTable::BeginStop(u32 slot, bool releasing) {
			AudioVoiceRecord& record = _records[slot];
			Account(record, -1);
			LeaveGroup(slot);
			record.claim = VoiceClaim::Stopping;
			record.releasing = releasing;
			Account(record, +1);
		}

		void AudioVoiceTable::SetReal(u32 slot, bool real) {
			AudioVoiceRecord& record = _records[slot];
			Account(record, -1);
			MarkReal(slot, real);
			Account(record, +1);
		}

		void AudioVoiceTable::MarkReal(u32 slot, bool real) {
			AudioVoiceRecord& record = _records[slot];
			if (record.real == real) {
				return;
			}
			record.real = real;
			if (real) {
				record.realIndex = _realCount;
				_realSlots[_realCount++] = slot;
				return;
			}
			const u32 last = _realSlots[--_realCount];
			_realSlots[record.realIndex] = last;
			_records[last].realIndex = record.realIndex;
			record.realIndex = NO_VOICE_SLOT;
		}

		void AudioVoiceTable::JoinGroup(u32 slot, AudioConcurrencyGroup& group, u64 now, u64 retriggerFrames) {
			AudioVoiceRecord& record = _records[slot];
			LeaveGroup(slot);
			record.groupAsset = group.asset;
			record.groupOwner = group.owner;
			record.groupPrevious = NO_VOICE_SLOT;
			record.groupNext = group.head;
			if (group.head != NO_VOICE_SLOT) {
				_records[group.head].groupPrevious = slot;
			}
			group.head = slot;
			++group.count;
			group.lastStart = now;
			group.retriggerFrames = retriggerFrames;
			group.started = true;
		}

		void AudioVoiceTable::LeaveGroup(u32 slot) {
			AudioVoiceRecord& record = _records[slot];
			if (record.groupAsset == 0) {
				return;
			}
			if (AudioConcurrencyGroup* group = _groups.Find(record.groupAsset, record.groupOwner)) {
				if (record.groupPrevious != NO_VOICE_SLOT) {
					_records[record.groupPrevious].groupNext = record.groupNext;
				} else {
					group->head = record.groupNext;
				}
				if (record.groupNext != NO_VOICE_SLOT) {
					_records[record.groupNext].groupPrevious = record.groupPrevious;
				}
				--group->count;
			}
			record.groupAsset = 0;
			record.groupOwner = 0;
			record.groupNext = NO_VOICE_SLOT;
			record.groupPrevious = NO_VOICE_SLOT;
		}

		u32 AudioVoiceTable::CountClaimed() const {
			u32 claimed = 0;
			for (u32 slot = 0; slot < _capacity; ++slot) {
				if (_records[slot].claim != VoiceClaim::Free) {
					++claimed;
				}
			}
			return claimed;
		}

	} // namespace AudioImpl

} // namespace Shalltear
