#include "AudioBusTable.h"

#include "AudioRenderer.h"

#include "Shalltear/Core/Memory.h"

#include <cstring>
#include <new>

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			constexpr size_t BUFFER_BYTES = BUS_BUFFER_SAMPLES * sizeof(f32);
			constexpr size_t BLOCK_BYTES = BUS_ROWS_BYTES + BUFFER_BYTES;

			void FreeBlocks(AudioBusRecord& record) {
				if (record.block) {
					Free(record.block);
				}
				if (record.receives) {
					Free(record.receives);
				}
				record.block = nullptr;
				record.receives = nullptr;
				record.receiveCapacity = 0;
			}

		} // anonymous namespace

		void* AudioBusTable::AllocateReceives(u32 count) {
			void* receives = Allocate(BUFFER_BYTES * count, MemTag::Audio, 64);
			std::memset(receives, 0, BUFFER_BYTES * count);
			return receives;
		}

		void AudioBusTable::Initialize(u32 capacity) {
			Shutdown();
			if (capacity == 0) {
				return;
			}
			_records = static_cast<AudioBusRecord*>(Allocate(sizeof(AudioBusRecord) * capacity, MemTag::Audio, alignof(AudioBusRecord)));
			for (u32 slot = 0; slot < capacity; ++slot) {
				new (&_records[slot]) AudioBusRecord();
			}
			_capacity = capacity;
			_cursor = 1;
			AudioBusRecord& root = _records[AudioRenderer::ROOT_BUS];
			root.claimed = true;
			root.live = true;
			root.generation = 1;
			root.parent = AudioRenderer::ROOT_BUS;
		}

		void AudioBusTable::Shutdown() {
			if (!_records) {
				return;
			}
			for (u32 slot = 0; slot < _capacity; ++slot) {
				FreeBlocks(_records[slot]);
				_records[slot].~AudioBusRecord();
			}
			Free(_records);
			_records = nullptr;
			_capacity = 0;
			_cursor = 1;
		}

		bool AudioBusTable::Claim(u32& slot, u32& generation, void*& block) {
			for (u32 step = 0; step + 1 < _capacity; ++step) {
				const u32 candidate = 1 + (_cursor - 1 + step) % (_capacity - 1);
				AudioBusRecord& record = _records[candidate];
				if (record.claimed) {
					continue;
				}
				record.block = Allocate(BLOCK_BYTES, MemTag::Audio, 64);
				std::memset(record.block, 0, BLOCK_BYTES);
				for (u32 row = 0; row < MAX_BUS_ROWS; ++row) {
					new (&BusRowsOf(record.block)[row]) AudioBusRow();
				}
				record.generation = record.generation + 1 == 0 ? 1 : record.generation + 1;
				record.claimed = true;
				record.live = true;
				record.parent = AudioRenderer::ROOT_BUS;
				record.sendCount = 0;
				record.fader = 1.0f;
				record.mute = false;
				record.solo = false;
				record.clock = AudioBusClock::Wall;
				record.followTimeScale = false;
				_cursor = 1 + candidate % (_capacity - 1);
				slot = candidate;
				generation = record.generation;
				block = record.block;
				return true;
			}
			return false;
		}

		void AudioBusTable::Abandon(u32 slot) {
			if (slot == AudioRenderer::ROOT_BUS || slot >= _capacity) {
				return;
			}
			AudioBusRecord& record = _records[slot];
			FreeBlocks(record);
			record.claimed = false;
			record.live = false;
			record.sendCount = 0;
		}

		void AudioBusTable::OnReleased(u32 slot, u32 generation) {
			if (slot == AudioRenderer::ROOT_BUS || slot >= _capacity) {
				return;
			}
			AudioBusRecord& record = _records[slot];
			if (!record.claimed || record.generation != generation) {
				return;
			}
			Abandon(slot);
		}

		AudioBusRecord* AudioBusTable::Find(u32 slot, u32 generation) {
			if (!_records || slot >= _capacity || generation == 0) {
				return nullptr;
			}
			AudioBusRecord& record = _records[slot];
			return record.live && record.generation == generation ? &record : nullptr;
		}

		const AudioBusRecord* AudioBusTable::Find(u32 slot, u32 generation) const {
			return const_cast<AudioBusTable*>(this)->Find(slot, generation);
		}

		bool AudioBusTable::WouldCycle(u32 from, u32 to) const {
			if (from == to) {
				return true;
			}
			bool visited[AudioRenderer::MAX_BUSES]{};
			u32 stack[AudioRenderer::MAX_BUSES];
			u32 depth = 0;
			stack[depth++] = to;
			visited[to] = true;
			while (depth > 0) {
				const u32 slot = stack[--depth];
				if (slot == from) {
					return true;
				}
				const AudioBusRecord& record = _records[slot];
				if (slot == AudioRenderer::ROOT_BUS || !record.live) {
					continue;
				}
				auto visit = [&](u32 next) {
					if (next < _capacity && !visited[next]) {
						visited[next] = true;
						stack[depth++] = next;
					}
				};
				visit(record.parent);
				for (u32 send = 0; send < record.sendCount; ++send) {
					visit(record.sendTargets[send]);
				}
			}
			return false;
		}

		void AudioBusTable::SetParent(u32 slot, u32 parent) {
			if (slot < _capacity && slot != AudioRenderer::ROOT_BUS) {
				_records[slot].parent = parent;
			}
		}

		void AudioBusTable::SetSends(u32 slot, const u32* targets, u32 count) {
			if (slot >= _capacity || slot == AudioRenderer::ROOT_BUS) {
				return;
			}
			AudioBusRecord& record = _records[slot];
			record.sendCount = count < MAX_BUS_ROWS ? count : MAX_BUS_ROWS;
			for (u32 send = 0; send < record.sendCount; ++send) {
				record.sendTargets[send] = targets[send];
			}
		}

		void AudioBusTable::MarkDestroyed(u32 slot) {
			if (slot == AudioRenderer::ROOT_BUS || slot >= _capacity || !_records[slot].live) {
				return;
			}
			AudioBusRecord& gone = _records[slot];
			for (u32 other = 0; other < _capacity; ++other) {
				AudioBusRecord& record = _records[other];
				if (!record.live || other == slot) {
					continue;
				}
				if (record.parent == slot) {
					record.parent = gone.parent;
				}
				u32 kept = 0;
				for (u32 send = 0; send < record.sendCount; ++send) {
					if (record.sendTargets[send] != slot) {
						record.sendTargets[kept++] = record.sendTargets[send];
					}
				}
				record.sendCount = kept;
			}
			gone.live = false;
			gone.sendCount = 0;
		}

		u32 AudioBusTable::CountLive() const {
			u32 live = 0;
			for (u32 slot = 1; slot < _capacity; ++slot) {
				live += _records[slot].live ? 1u : 0u;
			}
			return live;
		}

	} // namespace AudioImpl

} // namespace Shalltear
