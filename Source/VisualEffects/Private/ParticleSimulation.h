#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/FramePass.h"
#include "Shalltear/Core/Graphics/ProceduralRenderer.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/Vector.h"

#include <array>
#include <span>

namespace Shalltear {

	class MaterialAsset;
	class MeshAsset;
	class ShaderAsset;

	namespace VisualEffectsImpl {

		enum class ParticleSort : u8 {
			None,
			Frame,
			View
		};

		struct ParticleSortStep {
			u32 output = 0;
			u32 step = 0;
		};

		struct ParticleOutputDesc {
			static constexpr u32 NO_MESH = ~0u;
			Handle<MaterialAsset> material;
			u32 vertices = 6;
			bool instanced = false;
			u32 mesh = NO_MESH;
			ParticleSort sort = ParticleSort::None;
			f32 sortSign = 1.0f;
			Handle<ShaderAsset> sortKernel;
			bool castShadows = false;
			bool strip = false;
		};

		// addresses valid only for the frame that read them
		struct ParticleMesh {
			u32 offset = 0;
			Handle<MeshAsset> mesh;
		};

		class ParticleSystem;

		struct ParticleEventTarget {
			ParticleSystem* child = nullptr;
			u32 lists = 0;
			u32 arguments = 0;
		};

		struct ParticleBatchKey {
			u64 owner = 0;
			u64 version = 0;
			u32 system = 0;
			bool operator==(const ParticleBatchKey&) const = default;
		};

		struct ParticleSystemDesc {
			u32 capacity = 0;
			u32 strips = 0;
			u32 attributeCount = 0;
			Handle<ShaderAsset> initialize;
			Handle<ShaderAsset> update;
			Vector<ComputeTexture> initializeTextures{GetMemoryResource(MemTag::Graphics)};
			Vector<ComputeTexture> updateTextures{GetMemoryResource(MemTag::Graphics)};
			static constexpr u32 NO_DEPTH = ~0u;
			u32 initializeDepth = NO_DEPTH;
			u32 updateDepth = NO_DEPTH;
			u32 cameraOffset = 0;
			bool world = false;
			Vector<ParticleMesh> meshes{GetMemoryResource(MemTag::Graphics)};
			Vector<ParticleOutputDesc> outputs{GetMemoryResource(MemTag::Graphics)};
			ParticleSystem* parent = nullptr;
			u32 parentBinding = NO_DEPTH;
			Vector<ParticleEventTarget> initializeEvents{GetMemoryResource(MemTag::Graphics)};
			Vector<ParticleEventTarget> updateEvents{GetMemoryResource(MemTag::Graphics)};
			bool cpu = false;
			bool measuresBounds = false;
			ParticleBatchKey batch;
			u32 batchSlots = 1;
		};

		struct ParticleCounts {
			u32 alive = 0;
			u32 dropped = 0;
			bool known = false;
		};

		struct ParticleBounds {
			AABB box = AABB::Empty();
			bool known = false;
		};

		struct ParticleReadback {
			static constexpr u32 SLOTS = RHI::FRAMES_IN_FLIGHT + 1;
			static constexpr u64 NO_FRAME = ~0ull;
			RHI::BufferHandle ring;
			u64 bytes = 0;
			std::array<u64, SLOTS> frames{};
			u64 read = 0;
			bool any = false;

			ParticleReadback() { frames.fill(NO_FRAME); }
			bool Make(u64 size, const char* debugName);
			static u32 SlotOf(u64 frame) { return static_cast<u32>(frame % SLOTS); }
			const u8* TakeNewest(u64 frame);
			void Release();
		};

		class ParticleSimulation;
		class ParticleBatch;

		class ParticleSystem {
		public:
			ParticleSystem() = default;
			~ParticleSystem();
			ParticleSystem(const ParticleSystem&) = delete;
			ParticleSystem& operator=(const ParticleSystem&) = delete;

			bool Create(const ParticleSystemDesc& desc);
			void Release();
			bool IsCreated() const { return _batch != nullptr; }

			void Advance(f32 deltaTime, u32 spawnCount);
			void BeginUpdates();

			void Feed(std::span<const u32> alive, std::span<const u32> attributes, u32 dropped);

			void SetFrameData(std::span<const u8> bytes);

			bool DeclareOutput(ProceduralPlan& plan, const Vec3& sortPoint, u32 output) const;
			bool RecordOutput(ProceduralContext& context, u32 output) const;

			void DeclareViewSorts(ProceduralPlan& plan, Vector<ParticleSortStep>& steps);
			bool RecordSort(ComputeContext& context, u32 output, u32 step, const Vec3& viewPosition, const Vec3& viewForward) const;

			void WatchCounts(bool watch);
			const ParticleCounts& GetCounts() const { return _counts; }
			const ParticleBounds& GetMeasuredBounds() const { return _bounds; }

			RHI::BufferHandle GetListsBuffer() const;
			RHI::BufferHandle GetArgumentsBuffer() const;
			RHI::BufferHandle GetAttributesBuffer() const;
			u64 GetListsOffset() const;
			u64 GetArgumentsOffset() const;
			u64 GetAttributesOffset() const;
			const ParticleBatch* GetBatch() const { return _batch; }
			u32 GetSlot() const { return _slot; }

			static u64 SlotBytes(const ParticleSystemDesc& desc);
			static constexpr u32 NO_ORDER = ~0u;
			RHI::BufferHandle GetOrderBuffer() const { return _order; }
			u32 GetOrderOffset(u32 output) const { return output < _orderOffsets.size() ? _orderOffsets[output] : NO_ORDER; }
			u32 GetSortCount() const { return _sortCount; }
			u32 GetCapacity() const { return _desc.capacity; }
			u32 GetStrips() const { return _desc.strips; }
			u32 GetParity() const { return _parity; }
			u32 GetSimulatedFrames() const { return _frame; }
		private:
			friend class ParticleSimulation;
			friend class ParticleBatch;
			static constexpr u32 FEED_SLOTS = RHI::FRAMES_IN_FLIGHT + 1;

			void ReadCounts(u64 frame);
			void ReadBounds(const u8* boxes, u64 frame);
			bool Sorts(u32 output, ParticleSort when) const;
			u32 SortSteps() const;
			template <typename Plan>
			void DeclareSortStep(Plan& plan, u32 step) const;

			ParticleSystemDesc _desc;
			ParticleBatch* _batch = nullptr;
			u32 _slot = 0;
			RHI::BufferHandle _order;
			RHI::BufferHandle _sortKeys;
			u64 _orderAddress = 0;
			u32 _sortCount = 0;
			Vector<u32> _orderOffsets{GetMemoryResource(MemTag::Graphics)};
			Vector<u8> _ordered{GetMemoryResource(MemTag::Graphics)};
			bool _listsReady = false;
			ParticleReadback _countRing;
			ParticleCounts _counts;
			bool _watched = false;
			ParticleBounds _bounds;
			u64 _boundsFrom = ParticleReadback::NO_FRAME;
			RHI::BufferHandle _feed;
			u64 _feedSlotBytes = 0;
			u32 _fedSlot = 0;
			u32 _fedAlive = 0;
			bool _fed = false;
			Vector<u8> _frameData{GetMemoryResource(MemTag::Graphics)};
			u64 _effectAddress = 0;
			u32 _drawTable = 0;
			struct Update {
				f32 deltaTime = 0.0f;
				u32 spawnCount = 0;
				u32 block = 0;
				u32 size = 0;
				u64 effect = 0;
				u32 drawTable = 0;
			};
			Vector<Update> _updates{GetMemoryResource(MemTag::Graphics)};
			Vector<u8> _updateBlocks{GetMemoryResource(MemTag::Graphics)};
			Vector<Update> _declared{GetMemoryResource(MemTag::Graphics)};
			Vector<u8> _declaredBlocks{GetMemoryResource(MemTag::Graphics)};
			u32 _frame = 0;
			u32 _parity = 0;
			u32 _rounds = 0;
			bool _advanced = false;
			bool _fresh = false;
		};

		// slot layout must match ParticleSystem.hlsli
		class ParticleBatch {
		public:
			ParticleBatchKey key;
			ParticleSystemDesc desc;
			u32 slots = 0;
			RHI::BufferHandle lists;
			RHI::BufferHandle arguments;
			RHI::BufferHandle attributes;
			u64 listsAddress = 0;
			u64 attributesAddress = 0;
			u64 listsStride = 0;
			u64 argumentsStride = 0;
			u64 attributesStride = 0;
			Vector<ParticleSystem*> members{GetMemoryResource(MemTag::Graphics)};
			u32 claimed = 0;
			u32 watermark = 0;
			u64 emptiedAt = 0;
			bool fresh = true;
			bool orphan = false;
			ParticleReadback boundsRing;
			Vector<u64> rounds{GetMemoryResource(MemTag::Graphics)};
			u64 standing = 0;

			bool Make(const ParticleSystemDesc& desc, u32 slots);
			void Free();
			bool Fits(const ParticleSystemDesc& desc) const;
			u64 FinishedBoxesOffset() const { return listsStride * slots; }
			u64 DispatchOffset() const { return argumentsStride * slots; }
		};

		class ParticleSimulation final : public FramePass {
		public:
			void Declare(FramePassPlan& plan) override;
			void Record(ComputeContext& context) override;

			u32 GetSystemCount() const { return static_cast<u32>(_systems.size()); }
			u32 GetBatchCount() const { return static_cast<u32>(_batches.size()); }

			void SetBatchEmptyLifetime(u32 frames) { _batchEmptyLifetime = frames; }

			struct FrameCounts {
				u32 dispatches = 0;
				u32 copies = 0;
				u32 draws = 0;
				u32 batches = 0;
				u32 alive = 0;
				u32 dropped = 0;
			};
			const FrameCounts& GetFrameCounts() const { return _frameCounts; }

			void ReleaseKernels();
			void FreeBatches();
		private:
			friend class ParticleSystem;
			bool Claim(ParticleSystem& system, const ParticleSystemDesc& desc);
			void Leave(ParticleSystem& system);
			void FreeEmptyBatches();
			void Add(ParticleSystem& system);
			void Remove(ParticleSystem& system);
			bool AcquireKernels();
			bool AcquireSortKernels();
			bool AcquireStripKernel();
			void DeclareReset(FramePassPlan& plan, ParticleBatch& batch);
			void DeclareRound(FramePassPlan& plan, ParticleBatch& batch, u32 round);
			void DeclareFed(FramePassPlan& plan, ParticleBatch& batch);
			void DeclareFrameSorts(FramePassPlan& plan, ParticleSystem& system);
			void DeclareEvents(FramePassPlan& plan, std::span<const ParticleEventTarget> targets);
			FrameImage ReadDepth(FramePassPlan& plan, ParticleBatch& batch, u32 round, u32 binding, bool& recorded);
			void WriteTables(RHI::Device& device);
			std::span<const ComputeTexture> WithDepth(std::span<const ComputeTexture> textures, u32 binding, FrameImage depth);

			enum class StepKind : u8 {
				Reset,
				Initialize,
				Update,
				Arguments,
				Counts,
				Sort,
				Strips,
				Upload,
				Bounds
			};
			struct Step {
				ParticleBatch* batch = nullptr;
				ParticleSystem* system = nullptr;
				StepKind kind = StepKind::Reset;
				static constexpr u32 NO_UPDATE = ~0u;
				u32 update = NO_UPDATE;
				FrameImage depth;
				ParticleSortStep sort;
			};

			Vector<ParticleSystem*> _systems{GetMemoryResource(MemTag::Graphics)};
			Vector<ParticleBatch*> _batches{GetMemoryResource(MemTag::Graphics)};
			u32 _batchEmptyLifetime = 300;
			Vector<u8> _records{GetMemoryResource(MemTag::Graphics)};
			FrameCounts _frameCounts;
			u64 _frame = 0;
			Vector<Step> _steps{GetMemoryResource(MemTag::Graphics)};
			Handle<ShaderAsset> _reset;
			Handle<ShaderAsset> _counts;
			Handle<ShaderAsset> _sortLocal;
			Handle<ShaderAsset> _sortMerge;
			Handle<ShaderAsset> _strips;
			bool _warnedNoSortKernels = false;
			Handle<Camera> _mainCamera;
			Vector<ComputeTexture> _textures{GetMemoryResource(MemTag::Graphics)};
			Vector<ComputeBinding> _bindings{GetMemoryResource(MemTag::Graphics)};
			bool _warnedNoKernels = false;
		};

		ParticleSimulation& GetSimulation();
		void RegisterSimulation(const void* owner);
		void ShutdownSimulation();

	} // namespace VisualEffectsImpl

} // namespace Shalltear
