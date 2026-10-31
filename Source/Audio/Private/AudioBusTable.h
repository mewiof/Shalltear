#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Core/Types.h"

#include "AudioBus.h"

namespace Shalltear {

	namespace AudioImpl {

		struct AudioBusRecord {
			bool claimed = false;
			bool live = false;
			u32 generation = 0;
			u32 parent = 0;
			u32 sendCount = 0;
			u32 sendTargets[MAX_BUS_ROWS]{};
			void* block = nullptr;
			// never shrunk while the bus lives
			void* receives = nullptr;
			u32 receiveCapacity = 0;

			f32 fader = 1.0f;
			bool mute = false;
			bool solo = false;
			AudioBusClock clock = AudioBusClock::Wall;
			bool followTimeScale = false;
		};

		class AudioBusTable {
		public:
			AudioBusTable() = default;
			~AudioBusTable() { Shutdown(); }
			AudioBusTable(const AudioBusTable&) = delete;
			AudioBusTable& operator=(const AudioBusTable&) = delete;

			void Initialize(u32 capacity);

			// only after device close + render thread join
			void Shutdown();

			bool IsInitialized() const { return _records != nullptr; }
			u32 GetCapacity() const { return _capacity; }

			bool Claim(u32& slot, u32& generation, void*& block);

			void Abandon(u32 slot);

			static void* AllocateReceives(u32 count);

			void OnReleased(u32 slot, u32 generation);

			AudioBusRecord* Find(u32 slot, u32 generation);
			const AudioBusRecord* Find(u32 slot, u32 generation) const;

			const AudioBusRecord* FindLive(u32 slot) const {
				return _records && slot < _capacity && _records[slot].live ? &_records[slot] : nullptr;
			}

			bool WouldCycle(u32 from, u32 to) const;

			void SetParent(u32 slot, u32 parent);
			void SetSends(u32 slot, const u32* targets, u32 count);

			void MarkDestroyed(u32 slot);

			u32 CountLive() const;
		private:
			AudioBusRecord* _records = nullptr;
			u32 _capacity = 0;
			u32 _cursor = 1;
		};

	} // namespace AudioImpl

} // namespace Shalltear
