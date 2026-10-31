#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {
	namespace RHI {
		class CommandList;
		class Device;
	} //namespace RHI
} // namespace Shalltear

namespace Shalltear::GraphicsInternal {

	using GraphResourceId = u16;
	inline constexpr GraphResourceId INVALID_GRAPH_RESOURCE = 0xFFFF;

	enum class GraphResourceKind : u8 {
		Imported,
		Transient,
		Virtual,
		Buffer
	};

	struct GraphResource {
		GraphResourceKind kind = GraphResourceKind::Imported;
		bool sink = false;
		RHI::TextureHandle texture;
		RHI::BufferHandle buffer;
		RHI::ResourceState finalState = RHI::ResourceState::Undefined;
		u16 refCount = 0;
		RHI::TextureDesc desc;
		i32 firstPass = -1;
		i32 lastPass = -1;
		RHI::ResourceState lastState = RHI::ResourceState::Undefined;
		bool lastWrite = false;
		bool accessed = false;
	};

	inline constexpr u32 MAX_PASS_ACCESSES = 7;

	struct GraphAccess {
		GraphResourceId resource = INVALID_GRAPH_RESOURCE;
		RHI::ResourceState state = RHI::ResourceState::Undefined;
		bool write = false;
		bool unorderedBarrier = false;
	};

	struct PassWork {
		u64 key = 0;
		u32 index = 0;
		u32 subIndex = 0;
		u32 element = 0;
		bool flag = false;
		u8 step = 0;
		bool produced = false;
	};

	struct GraphPass;

	struct GraphExecution {
		RHI::CommandList* cmd = nullptr;
		RHI::Device* device = nullptr;
		u32 drawCalls = 0;
		u32 triangles = 0;
	};

	using PassRecordFn = void (*)(GraphExecution& exec, const GraphPass& pass);

	struct GraphPass {
		const char* name = nullptr;
		PassRecordFn record = nullptr;
		PassWork work{};
		u16 firstAccess = 0;
		u16 accessCount = 0;
		u16 liveOutputs = 0;
		u32 zoneSite = 0;
		RHI::TextureHandle textures[MAX_PASS_ACCESSES];
		RHI::BufferHandle buffers[MAX_PASS_ACCESSES];
	};

	class SHALLTEAR_CORE_API RenderGraph {
	public:
		RenderGraph();

		void Begin(RHI::Device* device);

		void DestroyPool();

		GraphResourceId Import(RHI::TextureHandle texture, bool sink = false,
							   RHI::ResourceState finalState = RHI::ResourceState::Undefined);
		GraphResourceId CreateTransient(const RHI::TextureDesc& desc);
		GraphResourceId CreateVirtual(bool sink = false);
		GraphResourceId ImportBuffer(RHI::BufferHandle buffer, bool sink = false);

		void AddPass(const char* name, u32 zoneSite, PassRecordFn record, const PassWork& work);
		void Read(GraphResourceId resource, RHI::ResourceState state);
		void Write(GraphResourceId resource, RHI::ResourceState state);

		void Compile();

		void Execute(GraphExecution& exec);

		u32 GetPassCount() const { return static_cast<u32>(_passes.size()); }
		u32 GetPoolSize() const { return static_cast<u32>(_pool.size()); }
		u32 GetPoolCreatesThisFrame() const { return _poolCreatesThisFrame; }
		u32 GetExecutedPassCount() const { return _executedPasses; }
		u32 GetCulledPassCount() const { return _culledPasses; }
		const GraphPass& GetPass(u32 index) const { return _passes[index]; }
		bool IsPassLive(u32 index) const { return _passes[index].liveOutputs > 0; }
		const GraphAccess& GetAccess(u32 index) const { return _accesses[index]; }
		RHI::BufferHandle GetAccessBuffer(u32 index) const {
			const GraphResource& resource = _resources[_accesses[index].resource];
			return resource.kind == GraphResourceKind::Buffer ? resource.buffer : RHI::BufferHandle{};
		}
		RHI::TextureHandle GetAccessTexture(u32 index) const {
			const GraphResource& resource = _resources[_accesses[index].resource];
			return resource.kind == GraphResourceKind::Imported ? resource.texture : RHI::TextureHandle{};
		}

		static constexpr u32 TRANSIENT_IDLE_FRAMES = 120;

		void SetCullingDisabled(bool disabled) { _cullingDisabled = disabled; }
	private:
		struct PoolEntry {
			RHI::TextureDesc desc;
			RHI::TextureHandle texture;
			i32 lastPassThisFrame = -1;
			u32 idleFrames = 0;
		};

		RHI::Device* _device = nullptr;
		Vector<PoolEntry> _pool;
		u32 _poolCreatesThisFrame = 0;
		Vector<GraphResource> _resources;
		Vector<GraphPass> _passes;
		Vector<GraphAccess> _accesses;
		Vector<i32> _sinkLastWriter;
		Vector<GraphResourceId> _deadResources;
		u32 _executedPasses = 0;
		u32 _culledPasses = 0;
		bool _cullingDisabled = false;
	};

} // namespace Shalltear::GraphicsInternal
