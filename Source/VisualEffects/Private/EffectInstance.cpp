#include "EffectInstance.h"

#include "EffectAsset.h"

#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"

#include <algorithm>
#include <bit>
#include <chrono>
#include <cmath>
#include <cstring>

namespace Shalltear::VisualEffectsImpl {

	namespace {

		struct EffectFrameHeader {
			f32 time[4] = {};
			f32 smoothDelta = 0.0f;
			u32 batchCount = 0;
			u32 idBase = 0;
			u32 seed = 0;
			f32 localToWorld[16] = {};
		};
		static_assert(sizeof(EffectFrameHeader) == 96, "the header is ParticleEffect.hlsli's layout");

		bool Names(const Vector<str>& names, std::string_view name) {
			return std::find(names.begin(), names.end(), name) != names.end();
		}

		void Put(Vector<u8>& bytes, u32 offset, const f32* values, u32 count) {
			std::memcpy(bytes.data() + offset, values, count * sizeof(f32));
		}

		EffectValue Key(u32 key) { return EffectValue{{std::bit_cast<f32>(key), 0.0f, 0.0f, 0.0f}}; }

		constexpr std::string_view ON_PLAY = "OnPlay";

		u32 SpawnCountOf(const VisualEffectEventAttributes& attributes) {
			Vec4 carried;
			if (!attributes.Find("spawnCount", carried)) {
				return 1;
			}
			return carried.x >= 1.0f ? static_cast<u32>(std::min(std::floor(carried.x), 4294967040.0f)) : 0u;
		}

		u32 NewSeed() {
			static u32 draws = 0;
			const u64 ticks = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			return EffectHash(static_cast<u32>(ticks) ^ EffectHash(static_cast<u32>(ticks >> 32) ^ EffectHash(++draws)));
		}

	} // anonymous namespace

	u32 EffectBatchSlots(const EffectInstancing& instancing, u64 instanceBytes) {
		if (instancing.mode == EFFECT_INSTANCING_CUSTOM) {
			return std::clamp(instancing.capacity, 1u, EFFECT_MAX_BATCH_SLOTS);
		}
		if (instancing.mode != EFFECT_INSTANCING_AUTOMATIC) {
			return 1;
		}
		const u64 fit = EFFECT_AUTOMATIC_BATCH_BYTES / std::max<u64>(instanceBytes, 1);
		return static_cast<u32>(std::clamp<u64>(fit, 1, EFFECT_AUTOMATIC_BATCH_SLOTS));
	}

	EffectInstance::~EffectInstance() {
		Release();
	}

	void EffectInstance::Release() {
		for (ParticleSystem* system : _systems) {
			Delete(system);
		}
		_systems.clear();
		for (ParticleCpuSystem* cpu : _cpu) {
			if (cpu) {
				Delete(cpu);
			}
		}
		_cpu.clear();
		_spawners.clear();
		_spawnerAttributes.clear();
		_bounds.clear();
		_padding.clear();
		_spawned.clear();
		_items.clear();
		_viewSteps.clear();
		_meshes.clear();
		_meshViews.clear();
		_asset = {};
		_generation = 0;
		_batchSlots = 1;
		_time = 0.0f;
	}

	bool EffectInstance::IsBuiltFrom(const VisualEffectAsset& asset) const {
		return _asset.Get() == &asset && _generation == EffectAssetAccess::Generation(asset);
	}

	void EffectInstance::TakeSent(Vector<EffectSent>& events, Vector<f32>& values) {
		events.clear();
		values.clear();
		std::swap(events, _sent);
		std::swap(values, _sentValues);
	}

	void EffectInstance::TakeFlowSent(Vector<EffectFlowSent>& events) {
		events.clear();
		std::swap(events, _flowSent);
	}

	const ParticleCpuSystem* EffectInstance::FindCpuSystem(std::string_view name, const EffectSystem*& described) const {
		described = nullptr;
		const VisualEffectAsset* asset = _asset.Get();
		if (!asset || !IsBuiltFrom(*asset)) {
			return nullptr;
		}
		const EffectDescription& description = EffectAssetAccess::Description(*asset);
		for (u32 s = 0; s < description.systems.size() && s < _cpu.size(); ++s) {
			if (description.systems[s].name == name && _cpu[s]) {
				described = &description.systems[s];
				return _cpu[s];
			}
		}
		return nullptr;
	}

	bool EffectInstance::Build(const VisualEffectAsset& asset) {
		Release();
		const EffectDescription& description = EffectAssetAccess::Description(asset);
		EffectAssetAccess::Meshes(asset, _meshes);
		for (u32 s = 0; s < description.systems.size(); ++s) {
			_systems.push_back(New<ParticleSystem>(MemTag::Graphics));
			_cpu.push_back(nullptr);
		}
		const auto targets = [this](const Vector<EffectEventTarget>& compiled, Vector<ParticleEventTarget>& out) {
			for (const EffectEventTarget& target : compiled) {
				out.push_back({_systems[target.child], target.lists, target.arguments});
			}
		};
		Vector<ParticleSystemDesc> descs{GetMemoryResource(MemTag::Graphics)};
		descs.resize(description.systems.size());
		for (u32 s = 0; s < description.systems.size(); ++s) {
			const EffectSystem& compiled = description.systems[s];
			ParticleSystemDesc& desc = descs[s];
			desc.capacity = compiled.capacity;
			desc.strips = compiled.strips;
			desc.attributeCount = compiled.attributeCount;
			desc.initialize = EffectAssetAccess::Shader(asset, compiled.initialize);
			desc.update = EffectAssetAccess::Shader(asset, compiled.update);
			EffectAssetAccess::KernelTextures(asset, s, false, desc.initializeTextures);
			EffectAssetAccess::KernelTextures(asset, s, true, desc.updateTextures);
			desc.initializeDepth = compiled.initializeDepthBinding;
			desc.updateDepth = compiled.updateDepthBinding;
			desc.cameraOffset = description.cameraOffset;
			desc.meshes.assign(_meshes.begin(), _meshes.end());
			desc.world = compiled.world;
			desc.measuresBounds = compiled.measuresBounds;
			desc.cpu = compiled.cpu;
			if (compiled.cpu) {
				_cpu[s] = New<ParticleCpuSystem>(MemTag::Graphics);
				if (!_cpu[s]->Build(description, s)) {
					Release();
					return false;
				}
			}
			for (u32 o = 0; o < compiled.outputs.size(); ++o) {
				const EffectOutput& output = compiled.outputs[o];
				ParticleOutputDesc drawn;
				drawn.material = EffectAssetAccess::OutputMaterial(asset, s, o);
				drawn.vertices = output.vertices;
				drawn.instanced = output.instanced;
				drawn.mesh = output.mesh == EFFECT_NO_BINDING ? ParticleOutputDesc::NO_MESH : output.mesh;
				drawn.castShadows = output.castShadows;
				drawn.strip = output.strip;
				if (output.sort != EffectSort::None) {
					const bool perView = output.sort == EffectSort::DistanceToCamera || output.sort == EffectSort::CameraDepth;
					drawn.sort = perView ? ParticleSort::View : ParticleSort::Frame;
					drawn.sortSign = output.revertSorting ? -1.0f : 1.0f;
					drawn.sortKernel = EffectAssetAccess::Shader(asset, output.sortKernel);
				}
				desc.outputs.push_back(drawn);
			}
			if (compiled.parent != EFFECT_NO_BINDING) {
				desc.parent = _systems[compiled.parent];
				desc.parentBinding = compiled.parentBinding;
			}
			targets(compiled.initializeEvents, desc.initializeEvents);
			targets(compiled.updateEvents, desc.updateEvents);
		}
		u64 instanceBytes = 0;
		for (const ParticleSystemDesc& desc : descs) {
			instanceBytes += ParticleSystem::SlotBytes(desc);
		}
		_batchSlots = _allowInstancing ? EffectBatchSlots(description.instancing, instanceBytes) : 1u;
		for (u32 s = 0; s < description.systems.size(); ++s) {
			ParticleSystemDesc& desc = descs[s];
			if (_batchSlots > 1) {
				desc.batch = {asset.GetInstanceId(), EffectAssetAccess::Generation(asset), s};
				desc.batchSlots = _batchSlots;
			}
			if (!_systems[s]->Create(desc)) {
				Release();
				return false;
			}
			_systems[s]->WatchCounts(_watchCounts);
		}
		_spawners.resize(description.spawners.size());
		_spawnerAttributes.resize(description.spawners.size());
		_bounds.assign(description.systems.size(), AABB{});
		_padding.assign(description.systems.size(), Vec3(0.0f, 0.0f, 0.0f));
		_spawned.assign(description.systems.size(), 0);
		_slots.assign(EffectSlotCount(description), EffectValue{});
		_asset = Handle<VisualEffectAsset>(const_cast<VisualEffectAsset*>(&asset));
		_generation = EffectAssetAccess::Generation(asset);
		_time = 0.0f;
		_frames = 0;
		_seed = _startSeed;
		_prewarm = true;
		return true;
	}

	void EffectInstance::QueueEvent(std::string_view name, const VisualEffectEventAttributes* attributes) {
		PendingEvent event;
		event.name = str(name);
		if (attributes) {
			event.hasAttributes = true;
			event.attributes = *attributes;
		}
		_events.push_back(std::move(event));
	}

	void EffectInstance::Reset() {
		const VisualEffectAsset* asset = _asset.Get();
		if (asset) {
			Build(*asset);
		}
		_events.clear();
	}

	void EffectInstance::Step() {
		_paused = true;
		++_steps;
	}

	bool EffectInstance::TakeAdvance() {
		if (!_paused) {
			return true;
		}
		if (_steps == 0) {
			return false;
		}
		--_steps;
		return true;
	}

	void EffectInstance::WatchCounts(bool watch) {
		_watchCounts = watch;
		for (ParticleSystem* system : _systems) {
			system->WatchCounts(watch);
		}
	}

	void EffectInstance::BeginFrame(const EffectDescription& description, std::span<const VisualEffectPropertyOverride> overrides) {
		_sent.clear();
		_sentValues.clear();
		_flowSent.clear();

		for (const PendingEvent& event : _events) {
			if (_reseedOnPlay && event.name == ON_PLAY) {
				_seed = NewSeed();
			}
		}

		_slots[EFFECT_SLOT_CONSTANT_RANDOM] = Key(EffectHash(_seed));
		for (u32 i = 0; i < description.properties.size(); ++i) {
			const EffectProperty& property = description.properties[i];
			EffectValue value;
			std::memcpy(value.v, property.defaultValue, sizeof(value.v));
			for (const VisualEffectPropertyOverride& entry : overrides) {
				if (entry.overridden && entry.name == property.name) {
					value = EffectValue{{entry.value.x, entry.value.y, entry.value.z, entry.value.w}};
				}
			}
			_slots[EFFECT_SLOT_PROPERTIES + i] = value;
		}
		_meshViews.assign(description.meshes.size(), EffectMeshView{});
		for (u32 i = 0; i < _meshes.size() && i < description.meshes.size(); ++i) {
			const MeshAsset* mesh = _meshes[i].mesh.Get();
			_slots[EffectMeshSlot(description, i, false)] = EffectValue{{mesh ? static_cast<f32>(mesh->vertices.size()) : 0.0f, 0.0f, 0.0f, 0.0f}};
			_slots[EffectMeshSlot(description, i, true)] = EffectValue{{mesh ? static_cast<f32>(mesh->indices.size()) : 0.0f, 0.0f, 0.0f, 0.0f}};
			if (mesh) {
				_meshViews[i] = {mesh->vertices.data(), static_cast<u32>(mesh->vertices.size()), mesh->indices.data(), static_cast<u32>(mesh->indices.size())};
			}
		}
	}

	u32 EffectInstance::Advance(std::span<const f32> updates, const Mat4& localToWorld, std::span<const VisualEffectPropertyOverride> overrides,
								bool recomputeBounds) {
		const VisualEffectAsset* asset = _asset.Get();
		if (!asset) {
			return 0;
		}
		const EffectDescription& description = EffectAssetAccess::Description(*asset);
		BeginFrame(description, overrides);

		for (ParticleSystem* system : _systems) {
			system->BeginUpdates();
		}
		u32 run = 0;
		if (_prewarm) {
			_prewarm = false;
			const EffectTiming& timing = description.timing;
			for (u32 i = 0; i < timing.prewarmStepCount; ++i) {
				Update(description, timing.prewarmDeltaTime, localToWorld, run++ == 0);
			}
			_sent.clear();
			_sentValues.clear();
			_flowSent.clear();
		}
		for (f32 update : updates) {
			Update(description, update, localToWorld, run++ == 0);
		}

		for (u32 s = 0; s < _systems.size(); ++s) {
			if (const ParticleCpuSystem* cpu = _cpu[s]; cpu && run > 0) {
				_systems[s]->Feed(cpu->GetAlive(), cpu->GetAttributes(), cpu->GetDropped());
			}
		}
		if (recomputeBounds) {
			RecomputeBounds(description, localToWorld);
		}
		return run;
	}

	void EffectInstance::Hold(const Mat4& localToWorld, std::span<const VisualEffectPropertyOverride> overrides, bool recomputeBounds) {
		const VisualEffectAsset* asset = _asset.Get();
		if (!asset) {
			return;
		}
		const EffectDescription& description = EffectAssetAccess::Description(*asset);
		BeginFrame(description, overrides);
		TakeEvents(description, false);
		if (recomputeBounds) {
			RecomputeBounds(description, localToWorld);
		}
	}

	void EffectInstance::RecomputeBounds(const EffectDescription& description, const Mat4& localToWorld) {
		for (u32 s = 0; s < _systems.size(); ++s) {
			const EffectSystem& system = description.systems[s];
			if (!_machine.Run(system.bounds, _slots, _meshViews)) {
				continue;
			}
			AABB box;
			if (system.boundsMode == EffectBoundsMode::Automatic) {
				const ParticleBounds measured = GetMeasuredBounds(s);
				const Vec3 origin = system.world ? localToWorld.TransformPoint(Vec3(0.0f, 0.0f, 0.0f)) : Vec3(0.0f, 0.0f, 0.0f);
				box = measured.known ? measured.box : AABB(origin, origin);
			} else {
				const EffectValue& center = _machine.Root(system.bounds, 0);
				const EffectValue& size = _machine.Root(system.bounds, 1);
				Vec3 at(center.v[0], center.v[1], center.v[2]);
				Mat4 worldToLocal;
				if (system.world && !system.boundsWorld) {
					at = localToWorld.TransformPoint(at);
				} else if (!system.world && system.boundsWorld && localToWorld.TryInvert(worldToLocal)) {
					at = worldToLocal.TransformPoint(at);
				}
				box = AABB::FromCenterExtents(at, Vec3(std::fabs(size.v[0]), std::fabs(size.v[1]), std::fabs(size.v[2])) * 0.5f);
			}
			const EffectValue& padding = _machine.Root(system.bounds, 2);
			_padding[s] = Vec3(padding.v[0], padding.v[1], padding.v[2]);
			if (system.boundsMode != EffectBoundsMode::Manual && box.IsValid()) {
				const Vec3 middle = box.Center();
				const Vec3 half = box.Extents();
				const Vec3 grown(std::max(half.x + padding.v[0], 0.0f), std::max(half.y + padding.v[1], 0.0f), std::max(half.z + padding.v[2], 0.0f));
				box = AABB::FromCenterExtents(middle, grown);
			}
			_bounds[s] = box;
		}
	}

	ParticleBounds EffectInstance::GetMeasuredBounds(u32 index) const {
		if (index >= _systems.size()) {
			return {};
		}
		return _cpu[index] ? _cpu[index]->GetMeasuredBounds() : _systems[index]->GetMeasuredBounds();
	}

	void EffectInstance::TakeEvents(const EffectDescription& description, bool spawn) {
		for (const PendingEvent& event : _events) {
			for (const EffectRelay& relay : description.relays) {
				if (spawn && relay.event == event.name) {
					EffectFlowSent& sent = _flowSent.emplace_back();
					sent.name = relay.output;
					sent.attributes = event.attributes;
				}
			}
			for (u32 i = 0; i < description.spawners.size(); ++i) {
				const EffectSpawner& spawner = description.spawners[i];
				if (Names(spawner.start, event.name)) {
					EffectSpawners::Start(spawner, _spawners[i], _slots, _machine, _meshViews);
					_spawnerAttributes[i] = event.hasAttributes ? event.attributes : VisualEffectEventAttributes{};
				}
				if (Names(spawner.stop, event.name)) {
					EffectSpawners::Stop(_spawners[i]);
				}
			}
			for (u32 s = 0; spawn && s < description.systems.size(); ++s) {
				for (const str& linked : description.systems[s].events) {
					const u32 count = linked == event.name ? SpawnCountOf(event.attributes) : 0u;
					if (count > 0) {
						_eventBatches.push_back({s, count, event.attributes});
					}
				}
			}
		}
		_events.clear();
	}

	void EffectInstance::SendFlow(const EffectSpawner& spawner, u32 index) {
		const EffectSpawnerState& state = _spawners[index];
		for (const str& name : spawner.outputEvents) {
			EffectFlowSent& sent = _flowSent.emplace_back();
			sent.name = name;
			sent.attributes = _spawnerAttributes[index];
			for (size_t b = 0; b < spawner.blocks.size() && b < state.values.size(); ++b) {
				const EffectSpawnBlock& block = spawner.blocks[b];
				const f32* v = state.values[b].v;
				if (block.kind != EffectSpawnKind::SetAttribute && block.kind != EffectSpawnKind::IncrementStripIndex) {
					continue;
				}
				if (block.components > 1) {
					sent.attributes.SetVector3(block.attribute, Vec3(v[0], v[1], v[2]));
				} else {
					sent.attributes.SetFloat(block.attribute, v[0]);
				}
			}
			sent.attributes.SetFloat("spawnCount", static_cast<f32>(_spawnedNow[index]));
		}
	}

	void EffectInstance::Update(const EffectDescription& description, f32 deltaTime, const Mat4& localToWorld, bool first) {
		_time += deltaTime;
		++_frames;

		_slots[EFFECT_SLOT_TIME] = EffectValue{{_time, std::sin(_time), std::cos(_time), deltaTime}};
		_slots[EFFECT_SLOT_SMOOTH_DELTA] = EffectValue{{deltaTime, 0.0f, 0.0f, 0.0f}};
		_slots[EFFECT_SLOT_RANDOM] = Key(EffectHash(_seed ^ EffectHash(_frames)));

		_eventBatches.clear();
		if (first) {
			TakeEvents(description, true);
		}

		_spawnedNow.assign(description.spawners.size(), 0);
		for (u32 i = 0; i < description.spawners.size(); ++i) {
			_spawnedNow[i] = EffectSpawners::Advance(description.spawners[i], _spawners[i], deltaTime, _slots, _machine, _meshViews);
			if (_spawnedNow[i] > 0) {
				SendFlow(description.spawners[i], i);
			}
		}

		for (u32 s = 0; s < _systems.size(); ++s) {
			const EffectSystem& system = description.systems[s];
			u32 total = 0;
			for (u32 spawner : system.spawners) {
				total += _spawnedNow[spawner];
			}
			for (const EventBatch& batch : _eventBatches) {
				total += batch.system == s ? batch.count : 0u;
			}
			WriteFrame(s, system, deltaTime, localToWorld, _spawnedNow);
			_systems[s]->Advance(deltaTime, total);
			if (ParticleCpuSystem* cpu = _cpu[s]) {
				cpu->Advance(description, _frame, _slots, total, _meshViews);
				const u32 base = static_cast<u32>(_sentValues.size());
				for (ParticleCpuEvent event : cpu->GetEvents()) {
					event.values += base;
					_sent.push_back({s, event});
				}
				_sentValues.insert(_sentValues.end(), cpu->GetEventValues().begin(), cpu->GetEventValues().end());
			}
			_spawned[s] += total;
		}
	}

	void EffectInstance::WriteFrame(u32 index, const EffectSystem& system, f32 deltaTime, const Mat4& localToWorld, std::span<const u32> spawned) {
		const EffectDescription& description = EffectAssetAccess::Description(*_asset.Get());
		u32 batches = 0;
		for (u32 spawner : system.spawners) {
			batches += spawned[spawner] > 0 ? 1u : 0u;
		}
		for (const EventBatch& batch : _eventBatches) {
			batches += batch.system == index ? 1u : 0u;
		}
		_frame.assign(description.batchesOffset + batches * system.batchStride, 0);

		EffectFrameHeader header;
		std::memcpy(header.time, _slots[EFFECT_SLOT_TIME].v, sizeof(header.time));
		header.smoothDelta = deltaTime;
		header.batchCount = batches;
		header.idBase = _spawned[index];
		header.seed = _seed;
		for (u32 row = 0; row < 4; ++row) {
			for (u32 column = 0; column < 4; ++column) {
				header.localToWorld[row * 4 + column] = localToWorld.data[column * 4 + row];
			}
		}
		std::memcpy(_frame.data(), &header, sizeof(header));

		for (u32 i = 0; i < description.properties.size(); ++i) {
			const EffectProperty& property = description.properties[i];
			Put(_frame, property.offset, _slots[EFFECT_SLOT_PROPERTIES + i].v, property.components);
		}

		u32 at = description.batchesOffset;
		const auto source = [&](const EffectField& field, const EffectSpawner* spawner, const EffectSpawnerState* state,
								const VisualEffectEventAttributes& carried) {
			Vec4 value;
			f32 components[4] = {};
			EffectValue set;
			if (spawner && EffectSpawners::FindAttribute(*spawner, *state, field.name, set)) {
				std::memcpy(components, set.v, sizeof(components));
			} else if (carried.Find(field.name, value)) {
				components[0] = value.x;
				components[1] = value.y;
				components[2] = value.z;
				components[3] = value.w;
			} else {
				std::memcpy(components, field.defaultValue, sizeof(components));
			}
			Put(_frame, at + field.offset, components, field.components);
		};
		for (u32 spawner : system.spawners) {
			const u32 count = spawned[spawner];
			if (count == 0) {
				continue;
			}
			std::memcpy(_frame.data() + at, &count, sizeof(count));
			for (const EffectField& field : system.source) {
				source(field, &description.spawners[spawner], &_spawners[spawner], _spawnerAttributes[spawner]);
			}
			at += system.batchStride;
		}
		for (const EventBatch& batch : _eventBatches) {
			if (batch.system != index) {
				continue;
			}
			std::memcpy(_frame.data() + at, &batch.count, sizeof(batch.count));
			for (const EffectField& field : system.source) {
				source(field, nullptr, nullptr, batch.attributes);
			}
			at += system.batchStride;
		}
		_systems[index]->SetFrameData(_frame);
	}

	void EffectInstance::Declare(ProceduralPlan& plan, const Mat4& localToWorld) {
		_items.clear();
		_viewSteps.clear();
		const VisualEffectAsset* asset = _asset.Get();
		if (!asset || _systems.empty()) {
			return;
		}
		const EffectDescription& description = EffectAssetAccess::Description(*asset);
		const AABB world = GetWorldBounds(localToWorld);
		plan.SetBounds(world);
		for (u32 s = 0; s < _systems.size(); ++s) {
			for (u32 o = 0; o < description.systems[s].outputs.size(); ++o) {
				if (_systems[s]->DeclareOutput(plan, world.Center(), o)) {
					_items.push_back({s, o});
				}
			}
			_sortSteps.clear();
			_systems[s]->DeclareViewSorts(plan, _sortSteps);
			for (const ParticleSortStep& step : _sortSteps) {
				_viewSteps.push_back({s, step});
			}
		}
	}

	bool EffectInstance::RecordView(ComputeContext& context, const ProceduralView& view) const {
		const u32 step = context.GetStep();
		if (step >= _viewSteps.size()) {
			return false;
		}
		const DeclaredStep& declared = _viewSteps[step];
		const Mat4 inverse = view.view.Inverse();
		const Vec3 forward(inverse(0, 2), inverse(1, 2), inverse(2, 2));
		return _systems[declared.system]->RecordSort(context, declared.sort.output, declared.sort.step, view.cameraPosition, forward.Normalized());
	}

	AABB EffectInstance::GetWorldBounds(const Mat4& localToWorld) const {
		const VisualEffectAsset* asset = _asset.Get();
		if (!asset || _systems.empty()) {
			return AABB{};
		}
		const EffectDescription& description = EffectAssetAccess::Description(*asset);
		AABB world = AABB::Empty();
		for (u32 s = 0; s < _systems.size(); ++s) {
			if (_bounds[s].IsValid()) {
				world.Encapsulate(description.systems[s].world ? _bounds[s] : _bounds[s].Transformed(localToWorld));
			}
		}
		if (!world.IsValid()) {
			const Vec3 origin = localToWorld.TransformPoint(Vec3(0.0f, 0.0f, 0.0f));
			world = AABB(origin, origin);
		}
		return world;
	}

	bool EffectInstance::Record(ProceduralContext& context) const {
		const u32 item = context.GetItem();
		return item < _items.size() && _systems[_items[item].system]->RecordOutput(context, _items[item].output);
	}

} // namespace Shalltear::VisualEffectsImpl
