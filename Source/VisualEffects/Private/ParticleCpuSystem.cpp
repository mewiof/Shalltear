#include "ParticleCpuSystem.h"

#include "Shalltear/Core/Jobs/JobSystem.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstring>

namespace Shalltear::VisualEffectsImpl {

	namespace {

		constexpr u32 BATCH_COUNT = 20;
		constexpr u32 ID_BASE = 24;
		constexpr u32 SEED = 28;
		constexpr u32 LOCAL_TO_WORLD = 32;
		constexpr u32 HEADER_BYTES = 96;
		constexpr u32 EFFECT_KEY_MIX = 0x9e3779b9u;

		u32 Word(std::span<const u8> frame, u32 offset) {
			u32 word = 0;
			if (offset + 4 <= frame.size()) {
				std::memcpy(&word, frame.data() + offset, 4);
			}
			return word;
		}

		f32 Float(std::span<const u8> frame, u32 offset) { return std::bit_cast<f32>(Word(frame, offset)); }

		EffectValue Key(u32 key) { return EffectValue{{std::bit_cast<f32>(key), 0.0f, 0.0f, 0.0f}}; }

		struct Float3 {
			f32 x = 0.0f;
			f32 y = 0.0f;
			f32 z = 0.0f;
		};

		Float3 Cross(const Float3& a, const Float3& b) { return {a.y * b.z - a.z * b.y, a.z * b.x - a.x * b.z, a.x * b.y - a.y * b.x}; }
		f32 Dot(const Float3& a, const Float3& b) { return a.x * b.x + a.y * b.y + a.z * b.z; }

		EffectValue SpaceRow(std::span<const u8> frame, bool world, u32 row) {
			const auto at = [&](u32 r, u32 c) { return Float(frame, LOCAL_TO_WORLD + 16 * r + 4 * c); };
			if (world) {
				return EffectValue{{at(row, 0), at(row, 1), at(row, 2), at(row, 3)}};
			}
			const Float3 r0{at(0, 0), at(0, 1), at(0, 2)};
			const Float3 r1{at(1, 0), at(1, 1), at(1, 2)};
			const Float3 r2{at(2, 0), at(2, 1), at(2, 2)};
			const Float3 t{at(0, 3), at(1, 3), at(2, 3)};
			const Float3 c0 = Cross(r1, r2);
			const Float3 c1 = Cross(r2, r0);
			const Float3 c2 = Cross(r0, r1);
			const f32 inverseDeterminant = 1.0f / Dot(r0, c0);
			const auto column = [row](const Float3& c) { return row == 0 ? c.x : (row == 1 ? c.y : c.z); };
			const Float3 inverse{column(c0) * inverseDeterminant, column(c1) * inverseDeterminant, column(c2) * inverseDeterminant};
			return EffectValue{{inverse.x, inverse.y, inverse.z, -Dot(inverse, t)}};
		}

	} // anonymous namespace

	bool ParticleCpuSystem::Build(const EffectDescription& description, u32 system) {
		*this = ParticleCpuSystem{};
		if (system >= description.systems.size() || !description.systems[system].cpu) {
			return false;
		}
		const EffectSystem& built = description.systems[system];
		_system = system;
		_capacity = built.capacity;
		_base = EffectSlotCount(description);
		_words.assign(static_cast<size_t>(built.capacity) * built.attributeCount, 0u);
		_alive.reserve(built.capacity);
		_next.reserve(built.capacity);
		_spawns.reserve(built.capacity);
		_lives.reserve(built.capacity);
		_dead.resize(built.capacity);
		for (u32 i = 0; i < built.capacity; ++i) {
			_dead[i] = built.capacity - 1 - i;
		}
		for (const EffectStored& stored : built.stored) {
			_idSlot = stored.attribute == "particleId" ? stored.slot : _idSlot;
		}
		ReadsOf(description, built.initializeProgram, _initializeReads);
		ReadsOf(description, built.updateProgram, _updateReads);
		_slots.assign(EffectParticleSlotCount(description, built), EffectValue{});
		return true;
	}

	void ParticleCpuSystem::ReadsOf(const EffectDescription& description, const EffectProgram& program, Reads& out) const {
		const EffectSystem& system = description.systems[_system];
		const u32 stored = EFFECT_PARTICLE_SLOT_STORED;
		const u32 source = stored + static_cast<u32>(system.stored.size());
		for (const EffectInput& input : program.inputs) {
			if (input.slot < _base) {
				continue;
			}
			const u32 slot = input.slot - _base;
			out.keys = out.keys || slot < EFFECT_PARTICLE_SLOT_ROWS;
			out.id = out.id || slot == EFFECT_PARTICLE_SLOT_ID;
			out.spawnIndex = out.spawnIndex || slot == EFFECT_PARTICLE_SLOT_SPAWN_INDEX;
			out.spawnCount = out.spawnCount || slot == EFFECT_PARTICLE_SLOT_SPAWN_COUNT;
			if (slot >= stored && slot < source) {
				out.stored.push_back(slot - stored);
			} else if (slot >= source) {
				out.source.push_back(slot - source);
			}
		}
	}

	f32 ParticleCpuSystem::Read(u32 slot, u32 index) const {
		const size_t at = static_cast<size_t>(slot) * _capacity + index;
		return at < _words.size() ? std::bit_cast<f32>(_words[at]) : 0.0f;
	}

	void ParticleCpuSystem::FillKeys(Lane& lane, u32 id) const {
		lane.slots[_base + EFFECT_PARTICLE_SLOT_RANDOM] = Key(EffectHash(id ^ _frameHash));
		lane.slots[_base + EFFECT_PARTICLE_SLOT_RANDOM + 1] = Key(EffectHash(id ^ _seedHash));
	}

	void ParticleCpuSystem::FillStored(const EffectSystem& system, const Reads& reads, Lane& lane, u32 index) const {
		for (const u32 i : reads.stored) {
			const EffectStored& stored = system.stored[i];
			EffectValue value;
			for (u32 c = 0; c < stored.components; ++c) {
				value.v[c] = std::bit_cast<f32>(_words[static_cast<size_t>(stored.slot + c) * _capacity + index]);
			}
			lane.slots[_base + EFFECT_PARTICLE_SLOT_STORED + i] = value;
		}
	}

	void ParticleCpuSystem::Store(const EffectSystem& system, const EffectProgram& program, const EffectMachine& machine, u32 index, u32 id) {
		for (u32 i = 0; i < system.stored.size(); ++i) {
			const EffectStored& stored = system.stored[i];
			if (stored.slot == _idSlot) {
				_words[static_cast<size_t>(stored.slot) * _capacity + index] = id;
				continue;
			}
			const EffectValue& value = machine.Root(program, i + 1);
			for (u32 c = 0; c < stored.components; ++c) {
				_words[static_cast<size_t>(stored.slot + c) * _capacity + index] = std::bit_cast<u32>(value.v[c]);
			}
		}
	}

	void ParticleCpuSystem::Send(Lane& lane, const EffectProgram& program, std::span<const EffectOutputEvent> events,
								 std::span<const EffectCarried> carried, bool update) {
		for (u32 e = 0; e < events.size(); ++e) {
			const f32 sent = lane.machine.Root(program, events[e].root).v[0];
			if (!(sent >= 1.0f)) {
				continue;
			}
			lane.events.push_back({update, e, static_cast<u32>(std::min(std::floor(sent), 4294967040.0f)), static_cast<u32>(lane.values.size())});
			for (const EffectCarried& entry : carried) {
				const EffectValue& value = lane.machine.Root(program, entry.root);
				lane.values.insert(lane.values.end(), value.v, value.v + entry.components);
			}
		}
	}

	void ParticleCpuSystem::Measure(Lane& lane, const EffectProgram& program, u32 root) {
		if (root == EFFECT_NO_BINDING) {
			return;
		}
		const EffectValue& center = lane.machine.Root(program, root);
		const f32 radius = lane.machine.Root(program, root + 1).v[0];
		const Vec3 low(center.v[0] - radius, center.v[1] - radius, center.v[2] - radius);
		const Vec3 high(center.v[0] + radius, center.v[1] + radius, center.v[2] + radius);
		const auto finite = [](const Vec3& v) { return std::isfinite(v.x) && std::isfinite(v.y) && std::isfinite(v.z); };
		if (radius >= 0.0f && finite(low) && finite(high)) {
			lane.box.Encapsulate(AABB(low, high));
		}
	}

	void ParticleCpuSystem::Run(const Chunk& chunk) {
		const EffectDescription& description = *chunk.description;
		const EffectSystem& system = description.systems[_system];
		const std::span<const u8> frame = chunk.frame;
		const u32 spawns = static_cast<u32>(_spawns.size());
		const u32 source = EFFECT_PARTICLE_SLOT_STORED + static_cast<u32>(system.stored.size());
		Lane& lane = _lanes[chunk.lane];
		for (u32 item = chunk.begin; item < chunk.end; ++item) {
			if (item < spawns) {
				const Spawn& spawn = _spawns[item];
				const u32 at = description.batchesOffset + spawn.batch * system.batchStride;
				if (_initializeReads.keys) {
					FillKeys(lane, spawn.id);
				}
				if (_initializeReads.id) {
					lane.slots[_base + EFFECT_PARTICLE_SLOT_ID] = EffectValue{{static_cast<f32>(spawn.id), 0.0f, 0.0f, 0.0f}};
				}
				if (_initializeReads.spawnIndex) {
					lane.slots[_base + EFFECT_PARTICLE_SLOT_SPAWN_INDEX] = EffectValue{{static_cast<f32>(spawn.spawnIndex), 0.0f, 0.0f, 0.0f}};
				}
				if (_initializeReads.spawnCount) {
					lane.slots[_base + EFFECT_PARTICLE_SLOT_SPAWN_COUNT] = EffectValue{{static_cast<f32>(Word(frame, at)), 0.0f, 0.0f, 0.0f}};
				}
				for (const u32 i : _initializeReads.source) {
					const EffectField& field = system.source[i];
					EffectValue value;
					for (u32 c = 0; c < field.components; ++c) {
						value.v[c] = Float(frame, at + field.offset + 4 * c);
					}
					lane.slots[_base + source + i] = value;
				}
				const bool ran = lane.machine.Run(system.initializeProgram, lane.slots, chunk.meshes);
				const bool lives = ran && lane.machine.Root(system.initializeProgram, 0).v[0] != 0.0f;
				if (lives) {
					Store(system, system.initializeProgram, lane.machine, spawn.index, spawn.id);
					Measure(lane, system.initializeProgram, system.initializeBoundsRoot);
				}
				if (ran && !system.initializeOutputEvents.empty()) {
					Send(lane, system.initializeProgram, system.initializeOutputEvents, system.initializeCarried, false);
				}
				_lives[item] = lives ? 1 : 0;
				continue;
			}
			const u32 index = _alive[item - spawns];
			const u32 id = _idSlot == NO_SLOT ? 0u : _words[static_cast<size_t>(_idSlot) * _capacity + index];
			if (_updateReads.keys) {
				FillKeys(lane, id);
			}
			if (_updateReads.id) {
				lane.slots[_base + EFFECT_PARTICLE_SLOT_ID] = EffectValue{{static_cast<f32>(id), 0.0f, 0.0f, 0.0f}};
			}
			FillStored(system, _updateReads, lane, index);
			const bool ran = lane.machine.Run(system.updateProgram, lane.slots, chunk.meshes);
			const bool lives = ran && lane.machine.Root(system.updateProgram, 0).v[0] != 0.0f;
			if (lives) {
				Store(system, system.updateProgram, lane.machine, index, id);
				Measure(lane, system.updateProgram, system.updateBoundsRoot);
			}
			if (ran && !system.updateOutputEvents.empty()) {
				Send(lane, system.updateProgram, system.updateOutputEvents, system.updateCarried, true);
			}
			_lives[item] = lives ? 1 : 0;
		}
	}

	void ParticleCpuSystem::Advance(const EffectDescription& description, std::span<const u8> frame, std::span<const EffectValue> slots,
									u32 spawnCount, std::span<const EffectMeshView> meshes) {
		_events.clear();
		_eventValues.clear();
		if (_system >= description.systems.size() || frame.size() < HEADER_BYTES) {
			return;
		}
		const EffectSystem& system = description.systems[_system];
		const u32 effectSlots = static_cast<u32>(std::min<size_t>(slots.size(), _base));
		std::copy(slots.begin(), slots.begin() + effectSlots, _slots.begin());

		const u32 seed = Word(frame, SEED);
		_frameHash = EffectHash(seed ^ EffectHash(_frame + 1u));
		_seedHash = EffectHash(seed);
		_slots[_base + EFFECT_PARTICLE_SLOT_RANDOM + 2] = Key(_frameHash ^ EFFECT_KEY_MIX);
		_slots[_base + EFFECT_PARTICLE_SLOT_RANDOM + 3] = Key(_seedHash ^ EFFECT_KEY_MIX);
		for (u32 row = 0; row < 3; ++row) {
			_slots[_base + EFFECT_PARTICLE_SLOT_ROWS + row] = SpaceRow(frame, system.world, row);
		}

		_spawns.clear();
		const u32 batches = Word(frame, BATCH_COUNT);
		const u32 idBase = Word(frame, ID_BASE);
		u32 batch = 0;
		u32 batchFirst = 0;
		u32 batchEnd = batches > 0 ? Word(frame, description.batchesOffset) : 0u;
		for (u32 spawn = 0; spawn < spawnCount; ++spawn) {
			while (spawn >= batchEnd && batch + 1 < batches) {
				++batch;
				batchFirst = batchEnd;
				batchEnd += Word(frame, description.batchesOffset + batch * system.batchStride);
			}
			if (_dead.empty()) {
				++_dropped;
				continue;
			}
			_spawns.push_back({_dead.back(), idBase + spawn, spawn - batchFirst, batch});
			_dead.pop_back();
		}

		const u32 items = static_cast<u32>(_spawns.size() + _alive.size());
		_lives.assign(items, 0);
		const u32 workers = JobSystem::GetWorkerCount();
		const u32 lanes = std::max(1u, std::min({MAX_LANES, workers + 1, items / MIN_CHUNK}));
		if (_lanes.size() < lanes) {
			_lanes.resize(lanes);
		}
		const u32 share = (items + lanes - 1) / lanes;
		_chunks.clear();
		for (u32 lane = 0; lane < lanes; ++lane) {
			_lanes[lane].slots.assign(_slots.begin(), _slots.end());
			_lanes[lane].events.clear();
			_lanes[lane].values.clear();
			_lanes[lane].box = AABB::Empty();
			_chunks.push_back({this, &description, frame, meshes, lane, std::min(items, lane * share), std::min(items, (lane + 1) * share)});
		}
		JobHandle jobs[MAX_LANES];
		for (u32 lane = 1; lane < lanes; ++lane) {
			const Chunk* chunk = &_chunks[lane];
			jobs[lane] = JobSystem::Submit([chunk]() { chunk->self->Run(*chunk); });
		}
		Run(_chunks[0]);
		if (lanes > 1) {
			JobSystem::WaitForAll(std::span<const JobHandle>(jobs + 1, lanes - 1));
		}
		_lanesUsed = lanes;
		if (system.measuresBounds) {
			_measured.box = AABB::Empty();
			for (u32 lane = 0; lane < lanes; ++lane) {
				_measured.box.Encapsulate(_lanes[lane].box);
			}
			_measured.known = true;
		}
		for (u32 lane = 0; lane < lanes; ++lane) {
			const u32 base = static_cast<u32>(_eventValues.size());
			for (ParticleCpuEvent event : _lanes[lane].events) {
				event.values += base;
				_events.push_back(event);
			}
			_eventValues.insert(_eventValues.end(), _lanes[lane].values.begin(), _lanes[lane].values.end());
		}

		_next.clear();
		const u32 spawns = static_cast<u32>(_spawns.size());
		for (u32 item = 0; item < items; ++item) {
			const u32 index = item < spawns ? _spawns[item].index : _alive[item - spawns];
			(_lives[item] != 0 ? _next : _dead).push_back(index);
		}
		std::swap(_alive, _next);
		++_frame;
	}

} // namespace Shalltear::VisualEffectsImpl
