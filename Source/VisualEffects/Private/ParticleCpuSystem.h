#pragma once

// must match the kernel's layout

#include "EffectDescription.h"
#include "EffectProgram.h"
#include "ParticleSimulation.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear {

	namespace VisualEffectsImpl {

		struct ParticleCpuEvent {
			bool update = false;
			u32 event = 0;
			u32 count = 0;
			u32 values = 0;
		};

		class ParticleCpuSystem {
		public:
			bool Build(const EffectDescription& description, u32 system);

			void Advance(const EffectDescription& description, std::span<const u8> frame, std::span<const EffectValue> slots, u32 spawnCount,
						 std::span<const EffectMeshView> meshes = {});

			std::span<const u32> GetAlive() const { return {_alive.data(), _alive.size()}; }
			std::span<const u32> GetAttributes() const { return {_words.data(), _words.size()}; }
			f32 Read(u32 slot, u32 index) const;
			u32 GetDropped() const { return _dropped; }
			u32 GetFrame() const { return _frame; }
			u32 GetLanesUsed() const { return _lanesUsed; }
			std::span<const ParticleCpuEvent> GetEvents() const { return {_events.data(), _events.size()}; }
			std::span<const f32> GetEventValues() const { return {_eventValues.data(), _eventValues.size()}; }
			const ParticleBounds& GetMeasuredBounds() const { return _measured; }

			static constexpr u32 MIN_CHUNK = 128;
			static constexpr u32 MAX_LANES = 32;
		private:
			struct Reads {
				bool keys = false;
				bool id = false;
				bool spawnIndex = false;
				bool spawnCount = false;
				Vector<u32> stored{GetMemoryResource(MemTag::Graphics)};
				Vector<u32> source{GetMemoryResource(MemTag::Graphics)};
			};
			struct Spawn {
				u32 index = 0;
				u32 id = 0;
				u32 spawnIndex = 0;
				u32 batch = 0;
			};
			struct Lane {
				Vector<EffectValue> slots{GetMemoryResource(MemTag::Graphics)};
				EffectMachine machine;
				Vector<ParticleCpuEvent> events{GetMemoryResource(MemTag::Graphics)};
				Vector<f32> values{GetMemoryResource(MemTag::Graphics)};
				AABB box = AABB::Empty();
			};
			struct Chunk {
				ParticleCpuSystem* self = nullptr;
				const EffectDescription* description = nullptr;
				std::span<const u8> frame;
				std::span<const EffectMeshView> meshes;
				u32 lane = 0;
				u32 begin = 0;
				u32 end = 0;
			};

			void ReadsOf(const EffectDescription& description, const EffectProgram& program, Reads& out) const;
			void Run(const Chunk& chunk);
			void Store(const EffectSystem& system, const EffectProgram& program, const EffectMachine& machine, u32 index, u32 id);
			void FillStored(const EffectSystem& system, const Reads& reads, Lane& lane, u32 index) const;
			void FillKeys(Lane& lane, u32 id) const;
			static void Send(Lane& lane, const EffectProgram& program, std::span<const EffectOutputEvent> events, std::span<const EffectCarried> carried,
							 bool update);
			static void Measure(Lane& lane, const EffectProgram& program, u32 root);

			u32 _system = 0;
			u32 _capacity = 0;
			u32 _base = 0;
			Vector<u32> _words{GetMemoryResource(MemTag::Graphics)};
			Vector<u32> _alive{GetMemoryResource(MemTag::Graphics)};
			Vector<u32> _next{GetMemoryResource(MemTag::Graphics)};
			Vector<u32> _dead{GetMemoryResource(MemTag::Graphics)};
			static constexpr u32 NO_SLOT = ~0u;
			u32 _idSlot = NO_SLOT;
			Reads _initializeReads;
			Reads _updateReads;
			Vector<Spawn> _spawns{GetMemoryResource(MemTag::Graphics)};
			Vector<u8> _lives{GetMemoryResource(MemTag::Graphics)};
			Vector<EffectValue> _slots{GetMemoryResource(MemTag::Graphics)};
			Vector<Lane> _lanes{GetMemoryResource(MemTag::Graphics)};
			Vector<Chunk> _chunks{GetMemoryResource(MemTag::Graphics)};
			Vector<ParticleCpuEvent> _events{GetMemoryResource(MemTag::Graphics)};
			Vector<f32> _eventValues{GetMemoryResource(MemTag::Graphics)};
			u32 _frameHash = 0;
			u32 _seedHash = 0;
			u32 _dropped = 0;
			u32 _frame = 0;
			u32 _lanesUsed = 0;
			ParticleBounds _measured;
		};

	} // namespace VisualEffectsImpl

} // namespace Shalltear
