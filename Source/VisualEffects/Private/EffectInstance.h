#pragma once

#include "EffectDescription.h"
#include "ParticleCpuSystem.h"
#include "ParticleSimulation.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/VisualEffects/VisualEffect.h"

#include <span>
#include <string_view>

namespace Shalltear {

	class VisualEffectAsset;

	namespace VisualEffectsImpl {

		struct EffectSent {
			u32 system = 0;
			ParticleCpuEvent event;
		};

		struct EffectFlowSent {
			str name;
			VisualEffectEventAttributes attributes;
		};

		class EffectInstance {
		public:
			EffectInstance() = default;
			~EffectInstance();
			EffectInstance(const EffectInstance&) = delete;
			EffectInstance& operator=(const EffectInstance&) = delete;

			bool Build(const VisualEffectAsset& asset);
			void Release();
			bool IsBuiltFrom(const VisualEffectAsset& asset) const;

			void QueueEvent(std::string_view name, const VisualEffectEventAttributes* attributes);

			void SetSeeding(u32 startSeed, bool reseedOnPlay) {
				_startSeed = startSeed;
				_reseedOnPlay = reseedOnPlay;
			}
			u32 GetSeed() const { return _seed; }
			void Reset();

			void SetInstancing(bool allowed) { _allowInstancing = allowed; }
			bool AllowsInstancing() const { return _allowInstancing; }
			u32 GetBatchSlots() const { return _batchSlots; }

			u32 Advance(std::span<const f32> updates, const Mat4& localToWorld, std::span<const VisualEffectPropertyOverride> overrides,
						bool recomputeBounds = true);
			void Hold(const Mat4& localToWorld, std::span<const VisualEffectPropertyOverride> overrides, bool recomputeBounds);
			bool IsFresh() const { return _prewarm; }

			void SetPaused(bool paused) { _paused = paused; }
			bool IsPaused() const { return _paused; }
			void Step();
			bool TakeAdvance();
			void SetControlRate(f32 rate) { _controlRate = rate; }
			f32 GetControlRate() const { return _controlRate; }

			void WatchCounts(bool watch);
			bool IsWatchingCounts() const { return _watchCounts; }

			void Declare(ProceduralPlan& plan, const Mat4& localToWorld);
			bool Record(ProceduralContext& context) const;
			bool RecordView(ComputeContext& context, const ProceduralView& view) const;
			AABB GetWorldBounds(const Mat4& localToWorld) const;
			ParticleBounds GetMeasuredBounds(u32 index) const;
			Vec3 GetBoundsPadding(u32 index) const { return index < _padding.size() ? _padding[index] : Vec3(0.0f, 0.0f, 0.0f); }

			void TakeSent(Vector<EffectSent>& events, Vector<f32>& values);
			void TakeFlowSent(Vector<EffectFlowSent>& events);
			const Handle<VisualEffectAsset>& GetAsset() const { return _asset; }

			const ParticleCpuSystem* FindCpuSystem(std::string_view name, const EffectSystem*& described) const;

			u32 GetSystemCount() const { return static_cast<u32>(_systems.size()); }
			const ParticleSystem& GetSystem(u32 index) const { return *_systems[index]; }
			const ParticleCpuSystem* GetCpuSystem(u32 index) const { return index < _cpu.size() ? _cpu[index] : nullptr; }
			f32 GetTime() const { return _time; }
		private:
			struct PendingEvent {
				str name;
				bool hasAttributes = false;
				VisualEffectEventAttributes attributes;
			};
			struct DeclaredItem {
				u32 system = 0;
				u32 output = 0;
			};
			struct DeclaredStep {
				u32 system = 0;
				ParticleSortStep sort;
			};
			struct EventBatch {
				u32 system = 0;
				u32 count = 0;
				VisualEffectEventAttributes attributes;
			};

			void Update(const EffectDescription& description, f32 deltaTime, const Mat4& localToWorld, bool first);
			void BeginFrame(const EffectDescription& description, std::span<const VisualEffectPropertyOverride> overrides);
			void TakeEvents(const EffectDescription& description, bool spawn);
			void SendFlow(const EffectSpawner& spawner, u32 index);
			void RecomputeBounds(const EffectDescription& description, const Mat4& localToWorld);
			void WriteFrame(u32 system, const EffectSystem& description, f32 deltaTime, const Mat4& localToWorld,
							std::span<const u32> spawned);

			Handle<VisualEffectAsset> _asset;
			u32 _generation = 0;
			Vector<ParticleSystem*> _systems{GetMemoryResource(MemTag::Graphics)};
			Vector<ParticleCpuSystem*> _cpu{GetMemoryResource(MemTag::Graphics)};
			Vector<EffectSpawnerState> _spawners{GetMemoryResource(MemTag::Graphics)};
			Vector<VisualEffectEventAttributes> _spawnerAttributes{GetMemoryResource(MemTag::Graphics)};
			Vector<AABB> _bounds{GetMemoryResource(MemTag::Graphics)};
			Vector<Vec3> _padding{GetMemoryResource(MemTag::Graphics)};
			Vector<PendingEvent> _events{GetMemoryResource(MemTag::Graphics)};
			Vector<EventBatch> _eventBatches{GetMemoryResource(MemTag::Graphics)};
			Vector<EffectValue> _slots{GetMemoryResource(MemTag::Graphics)};
			Vector<ParticleMesh> _meshes{GetMemoryResource(MemTag::Graphics)};
			Vector<EffectMeshView> _meshViews{GetMemoryResource(MemTag::Graphics)};
			Vector<u8> _frame{GetMemoryResource(MemTag::Graphics)};
			Vector<DeclaredItem> _items{GetMemoryResource(MemTag::Graphics)};
			Vector<DeclaredStep> _viewSteps{GetMemoryResource(MemTag::Graphics)};
			Vector<ParticleSortStep> _sortSteps{GetMemoryResource(MemTag::Graphics)};
			Vector<u32> _spawnedNow{GetMemoryResource(MemTag::Graphics)};
			Vector<u32> _spawned{GetMemoryResource(MemTag::Graphics)};
			Vector<EffectSent> _sent{GetMemoryResource(MemTag::Graphics)};
			Vector<f32> _sentValues{GetMemoryResource(MemTag::Graphics)};
			Vector<EffectFlowSent> _flowSent{GetMemoryResource(MemTag::Graphics)};
			EffectMachine _machine;
			f32 _time = 0.0f;
			u32 _seed = 0;
			u32 _startSeed = 0;
			bool _reseedOnPlay = true;
			bool _allowInstancing = true;
			u32 _batchSlots = 1;
			u32 _frames = 0;
			bool _prewarm = false;
			f32 _controlRate = 1.0f;
			u32 _steps = 0;
			bool _paused = false;
			bool _watchCounts = false;
		};

		inline constexpr u64 EFFECT_AUTOMATIC_BATCH_BYTES = 1ull << 20;
		inline constexpr u32 EFFECT_AUTOMATIC_BATCH_SLOTS = 64;
		inline constexpr u32 EFFECT_MAX_BATCH_SLOTS = 65535;
		u32 EffectBatchSlots(const EffectInstancing& instancing, u64 instanceBytes);

		struct EffectComponentAccess {
			static EffectInstance* Instance(VisualEffect& effect) { return effect._instance; }
			static void SetInstance(VisualEffect& effect, EffectInstance* instance) { effect._instance = instance; }
		};

	} // namespace VisualEffectsImpl

} // namespace Shalltear
