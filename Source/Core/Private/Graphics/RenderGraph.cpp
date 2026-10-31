#include "Graphics/RenderGraph.h"

#include "Shalltear/Core/Assert.h"
#include "Shalltear/Core/Graphics/RHICommandList.h"
#include "Shalltear/Core/Graphics/RHIDevice.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Profiler.h"

namespace Shalltear::GraphicsInternal {

	namespace {

		bool IsWriteOnlyState(RHI::ResourceState state) {
			switch (state) {
			case RHI::ResourceState::RenderTarget:
			case RHI::ResourceState::DepthWrite:
			case RHI::ResourceState::TransferDst:
				return true;
			default:
				return false;
			}
		}

		bool IsWritableState(RHI::ResourceState state) {
			return IsWriteOnlyState(state) || state == RHI::ResourceState::UnorderedAccess;
		}

		bool SameImage(const RHI::TextureDesc& a, const RHI::TextureDesc& b) {
			return a.size == b.size && a.format == b.format && a.usage == b.usage &&
				   a.mipLevels == b.mipLevels && a.samples == b.samples &&
				   a.arrayLayers == b.arrayLayers && a.depth == b.depth &&
				   RHI::ResolveTextureDimension(a) == RHI::ResolveTextureDimension(b);
		}

#if defined(SHALLTEAR_CONFIG_RELEASE)
#define SLT_GRAPH_PASS_ZONE(site) ((void)(site))
#else
#define SLT_GRAPH_PASS_ZONE(site) ::Shalltear::Profiler::ScopedZone sltGraphPassZone(site)
#endif

	} // anonymous namespace

	RenderGraph::RenderGraph()
		: _resources{GetMemoryResource(MemTag::Graphics)},
		  _passes{GetMemoryResource(MemTag::Graphics)},
		  _accesses{GetMemoryResource(MemTag::Graphics)},
		  _sinkLastWriter{GetMemoryResource(MemTag::Graphics)},
		  _deadResources{GetMemoryResource(MemTag::Graphics)},
		  _pool{GetMemoryResource(MemTag::Graphics)} {
	}

	void RenderGraph::Begin(RHI::Device* device) {
		_device = device;

		for (size_t i = _pool.size(); i > 0; --i) {
			PoolEntry& entry = _pool[i - 1];
			entry.lastPassThisFrame = -1;
			if (++entry.idleFrames <= TRANSIENT_IDLE_FRAMES) {
				continue;
			}
			if (_device && entry.texture.IsValid()) {
				_device->DestroyResource(entry.texture);
			}
			_pool[i - 1] = _pool.back();
			_pool.pop_back();
		}
		_poolCreatesThisFrame = 0;

		_resources.clear();
		_passes.clear();
		_accesses.clear();
		_sinkLastWriter.clear();
		_deadResources.clear();
		_executedPasses = 0;
		_culledPasses = 0;
	}

	GraphResourceId RenderGraph::Import(RHI::TextureHandle texture, bool sink, RHI::ResourceState finalState) {
		SLT_CORE_ASSERT_MSG(texture.IsValid(), "an invalid texture imported into the graph");
		for (GraphResourceId id = 0; id < static_cast<GraphResourceId>(_resources.size()); ++id) {
			GraphResource& existing = _resources[id];
			if (existing.kind != GraphResourceKind::Imported || !(existing.texture == texture)) {
				continue;
			}
			existing.sink = existing.sink || sink;
			if (finalState != RHI::ResourceState::Undefined) {
				SLT_CORE_ASSERT_MSG(existing.finalState == RHI::ResourceState::Undefined || existing.finalState == finalState,
									"1 texture imported twice asking for 2 different final states");
				existing.finalState = finalState;
			}
			return id;
		}
		GraphResource resource;
		resource.kind = GraphResourceKind::Imported;
		resource.sink = sink;
		resource.texture = texture;
		resource.finalState = finalState;
		_resources.push_back(resource);
		return static_cast<GraphResourceId>(_resources.size() - 1);
	}

	GraphResourceId RenderGraph::CreateTransient(const RHI::TextureDesc& desc) {
		GraphResource resource;
		resource.kind = GraphResourceKind::Transient;
		resource.desc = desc;
		_resources.push_back(resource);
		return static_cast<GraphResourceId>(_resources.size() - 1);
	}

	void RenderGraph::DestroyPool() {
		if (_device) {
			for (PoolEntry& entry : _pool) {
				if (entry.texture.IsValid()) {
					_device->DestroyResource(entry.texture);
				}
			}
		}
		_pool.clear();
	}

	GraphResourceId RenderGraph::ImportBuffer(RHI::BufferHandle buffer, bool sink) {
		SLT_CORE_ASSERT_MSG(buffer.IsValid(), "an invalid buffer imported into the graph");
		for (GraphResourceId id = 0; id < static_cast<GraphResourceId>(_resources.size()); ++id) {
			GraphResource& existing = _resources[id];
			if (existing.kind == GraphResourceKind::Buffer && existing.buffer == buffer) {
				existing.sink = existing.sink || sink;
				return id;
			}
		}
		GraphResource resource;
		resource.kind = GraphResourceKind::Buffer;
		resource.sink = sink;
		resource.buffer = buffer;
		_resources.push_back(resource);
		return static_cast<GraphResourceId>(_resources.size() - 1);
	}

	GraphResourceId RenderGraph::CreateVirtual(bool sink) {
		GraphResource resource;
		resource.kind = GraphResourceKind::Virtual;
		resource.sink = sink;
		_resources.push_back(resource);
		return static_cast<GraphResourceId>(_resources.size() - 1);
	}

	void RenderGraph::AddPass(const char* name, u32 zoneSite, PassRecordFn record, const PassWork& work) {
		SLT_CORE_ASSERT_MSG(record != nullptr, "a pass needs a recorder");
		GraphPass pass;
		pass.name = name;
		pass.zoneSite = zoneSite;
		pass.record = record;
		pass.work = work;
		pass.firstAccess = static_cast<u16>(_accesses.size());
		_passes.push_back(pass);
	}

	void RenderGraph::Read(GraphResourceId resource, RHI::ResourceState state) {
		SLT_CORE_ASSERT_MSG(!_passes.empty(), "Read before any AddPass");
		SLT_CORE_ASSERT_MSG(resource < _resources.size(), "Read of a resource that was never declared");
		SLT_CORE_ASSERT_MSG(!IsWriteOnlyState(state), "a read declared in a write-only state");
		GraphPass& pass = _passes.back();
		SLT_CORE_ASSERT_MSG(pass.accessCount < MAX_PASS_ACCESSES || _resources[resource].kind == GraphResourceKind::Buffer,
							"a pass declared more non-buffer resources than MAX_PASS_ACCESSES");
		_accesses.push_back(GraphAccess{resource, state, false});
		++pass.accessCount;
	}

	void RenderGraph::Write(GraphResourceId resource, RHI::ResourceState state) {
		SLT_CORE_ASSERT_MSG(!_passes.empty(), "Write before any AddPass");
		SLT_CORE_ASSERT_MSG(resource < _resources.size(), "Write to a resource that was never declared");
		SLT_CORE_ASSERT_MSG(_resources[resource].kind == GraphResourceKind::Virtual || IsWritableState(state),
							"a write declared in a state no writer can produce");
		GraphPass& pass = _passes.back();
		SLT_CORE_ASSERT_MSG(pass.accessCount < MAX_PASS_ACCESSES || _resources[resource].kind == GraphResourceKind::Buffer,
							"a pass declared more non-buffer resources than MAX_PASS_ACCESSES");
		_accesses.push_back(GraphAccess{resource, state, true});
		++pass.accessCount;
		++pass.liveOutputs;
	}

	void RenderGraph::Compile() {
		for (const GraphPass& pass : _passes) {
			SLT_CORE_ASSERT_MSG(pass.liveOutputs > 0,
								"a pass declared no output. Name what it produces (a virtual resource if the graph doesn't own it)");
		}

		for (GraphResource& resource : _resources) {
			resource.refCount = resource.sink ? 1u : 0u;
		}
		for (const GraphAccess& access : _accesses) {
			if (!access.write) {
				++_resources[access.resource].refCount;
			}
		}

		if (!_cullingDisabled) {
			for (GraphResourceId id = 0; id < static_cast<GraphResourceId>(_resources.size()); ++id) {
				if (_resources[id].refCount == 0) {
					_deadResources.push_back(id);
				}
			}
			while (!_deadResources.empty()) {
				GraphResourceId dead = _deadResources.back();
				_deadResources.pop_back();
				for (GraphPass& pass : _passes) {
					if (pass.liveOutputs == 0) {
						continue;
					}
					bool writesDead = false;
					for (u16 i = 0; i < pass.accessCount; ++i) {
						const GraphAccess& access = _accesses[pass.firstAccess + i];
						if (access.write && access.resource == dead) {
							writesDead = true;
							break;
						}
					}
					if (!writesDead) {
						continue;
					}
					--pass.liveOutputs;
					if (pass.liveOutputs > 0) {
						continue;
					}
					for (u16 i = 0; i < pass.accessCount; ++i) {
						const GraphAccess& access = _accesses[pass.firstAccess + i];
						if (access.write) {
							continue;
						}
						GraphResource& source = _resources[access.resource];
						SLT_CORE_ASSERT(source.refCount > 0);
						if (--source.refCount == 0) {
							_deadResources.push_back(access.resource);
						}
					}
				}
			}
		}

		_sinkLastWriter.assign(_resources.size(), -1);
		for (u32 passIndex = 0; passIndex < _passes.size(); ++passIndex) {
			const GraphPass& pass = _passes[passIndex];
			if (pass.liveOutputs == 0) {
				continue;
			}
			for (u16 i = 0; i < pass.accessCount; ++i) {
				const GraphAccess& access = _accesses[pass.firstAccess + i];
				const GraphResource& resource = _resources[access.resource];
				if (access.write && resource.sink && resource.kind == GraphResourceKind::Imported &&
					resource.finalState != RHI::ResourceState::Undefined) {
					_sinkLastWriter[access.resource] = static_cast<i32>(passIndex);
				}
			}
		}

		for (u32 passIndex = 0; passIndex < _passes.size(); ++passIndex) {
			const GraphPass& pass = _passes[passIndex];
			if (pass.liveOutputs == 0) {
				continue;
			}
			for (u16 i = 0; i < pass.accessCount; ++i) {
				GraphResource& resource = _resources[_accesses[pass.firstAccess + i].resource];
				if (resource.firstPass < 0) {
					resource.firstPass = static_cast<i32>(passIndex);
				}
				resource.lastPass = static_cast<i32>(passIndex);
			}
		}

		for (;;) {
			GraphResource* next = nullptr;
			for (GraphResource& resource : _resources) {
				if (resource.kind != GraphResourceKind::Transient || resource.firstPass < 0 ||
					resource.texture.IsValid()) {
					continue;
				}
				if (!next || resource.firstPass < next->firstPass) {
					next = &resource;
				}
			}
			if (!next) {
				break;
			}
			PoolEntry* claimed = nullptr;
			for (PoolEntry& entry : _pool) {
				if (entry.lastPassThisFrame < next->firstPass && SameImage(entry.desc, next->desc)) {
					claimed = &entry;
					break;
				}
			}
			if (!claimed) {
				SLT_CORE_ASSERT(_device != nullptr);
				PoolEntry entry;
				entry.desc = next->desc;
				entry.texture = _device->CreateTextureResource(next->desc);
				_pool.push_back(entry);
				claimed = &_pool.back();
				++_poolCreatesThisFrame;
			}
			claimed->lastPassThisFrame = next->lastPass;
			claimed->idleFrames = 0;
			next->texture = claimed->texture;
		}

		for (GraphPass& pass : _passes) {
			if (pass.liveOutputs == 0) {
				continue;
			}
			for (u16 i = 0; i < pass.accessCount; ++i) {
				GraphAccess& access = _accesses[pass.firstAccess + i];
				GraphResource& resource = _resources[access.resource];
				access.unorderedBarrier = false;
				if (resource.kind != GraphResourceKind::Buffer) {
					continue;
				}
				if (access.state == RHI::ResourceState::UnorderedAccess) {
					const bool afterUnordered = resource.lastState == RHI::ResourceState::UnorderedAccess &&
												(resource.lastWrite || access.write);
					access.unorderedBarrier = !resource.accessed || afterUnordered;
				}
				resource.accessed = true;
				resource.lastState = access.state;
				resource.lastWrite = access.write;
			}
		}

		for (GraphPass& pass : _passes) {
			if (pass.liveOutputs == 0) {
				++_culledPasses;
				continue;
			}
			const u16 slots = static_cast<u16>(pass.accessCount < MAX_PASS_ACCESSES ? pass.accessCount : MAX_PASS_ACCESSES);
			for (u16 i = 0; i < slots; ++i) {
				const GraphResource& resource = _resources[_accesses[pass.firstAccess + i].resource];
				const bool buffer = resource.kind == GraphResourceKind::Buffer;
				pass.textures[i] = buffer ? RHI::TextureHandle{} : resource.texture;
				pass.buffers[i] = buffer ? resource.buffer : RHI::BufferHandle{};
			}
		}
	}

	void RenderGraph::Execute(GraphExecution& exec) {
		SLT_CORE_ASSERT(exec.cmd != nullptr);
		RHI::CommandList& cmd = *exec.cmd;

		for (u32 passIndex = 0; passIndex < _passes.size(); ++passIndex) {
			GraphPass& pass = _passes[passIndex];
			if (pass.liveOutputs == 0) {
				continue;
			}
			{
				SLT_GRAPH_PASS_ZONE(pass.zoneSite);
				for (u16 i = 0; i < pass.accessCount; ++i) {
					const GraphAccess& access = _accesses[pass.firstAccess + i];
					const GraphResource& resource = _resources[access.resource];
					if (resource.kind == GraphResourceKind::Virtual) {
						continue;
					}
					if (resource.kind == GraphResourceKind::Buffer) {
						cmd.TransitionBuffer(resource.buffer, access.state);
						if (access.unorderedBarrier) {
							cmd.UnorderedAccessBarrier(resource.buffer);
						}
						continue;
					}
					cmd.TransitionTexture(pass.textures[i], access.state);
				}
				pass.record(exec, pass);
			}
			++_executedPasses;

			for (GraphResourceId id = 0; id < static_cast<GraphResourceId>(_resources.size()); ++id) {
				if (_sinkLastWriter[id] == static_cast<i32>(passIndex)) {
					cmd.TransitionTexture(_resources[id].texture, _resources[id].finalState);
				}
			}
		}
	}

} // namespace Shalltear::GraphicsInternal
