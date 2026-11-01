#include "Shalltear/Core/Platform.h"

#if defined(SLT_PLATFORM_WINDOWS)

#include "Graphics/Vulkan/VulkanInternal.h"

#include "Shalltear/Core/StringHash.h"

#include <algorithm>

namespace Shalltear::RHI {

	namespace {

		VkDescriptorType ToVkDescriptorType(DescriptorType type) {
			switch (type) {
			case DescriptorType::UniformBuffer:
				return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			case DescriptorType::StorageBuffer:
				return VK_DESCRIPTOR_TYPE_STORAGE_BUFFER;
			case DescriptorType::SampledTexture:
				return VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE;
			case DescriptorType::Sampler:
				return VK_DESCRIPTOR_TYPE_SAMPLER;
			default:
				return VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER;
			}
		}

		const char* ResourceStateName(ResourceState state) {
			switch (state) {
			case ResourceState::Undefined:
				return "Undefined";
			case ResourceState::RenderTarget:
				return "RenderTarget";
			case ResourceState::DepthWrite:
				return "DepthWrite";
			case ResourceState::DepthRead:
				return "DepthRead";
			case ResourceState::ShaderResource:
				return "ShaderResource";
			case ResourceState::TransferSrc:
				return "TransferSrc";
			case ResourceState::TransferDst:
				return "TransferDst";
			case ResourceState::Present:
				return "Present";
			case ResourceState::UnorderedAccess:
				return "UnorderedAccess";
			case ResourceState::IndirectArgument:
				return "IndirectArgument";
			default:
				return "?";
			}
		}

		VkShaderStageFlags ToVkStages(ShaderStage stages) {
			VkShaderStageFlags flags = 0;
			if (HasFlag(stages, ShaderStage::Vertex)) {
				flags |= VK_SHADER_STAGE_VERTEX_BIT;
			}
			if (HasFlag(stages, ShaderStage::Pixel)) {
				flags |= VK_SHADER_STAGE_FRAGMENT_BIT;
			}
			if (HasFlag(stages, ShaderStage::Compute)) {
				flags |= VK_SHADER_STAGE_COMPUTE_BIT;
			}
			return flags ? flags : VK_SHADER_STAGE_ALL_GRAPHICS;
		}

		bool BuildPipelineLayout(DeviceImpl& impl, std::span<const DescriptorBindingDesc> bindings, u32 pushConstantSize,
								 VkShaderStageFlags pushStages, VkDescriptorSetLayout (&setLayouts)[MAX_DESCRIPTOR_SETS],
								 u32& setCount, VkPipelineLayout& layout) {
			setCount = 0;
			for (const DescriptorBindingDesc& binding : bindings) {
				setCount = std::max(setCount, binding.set + 1);
			}
			if (setCount > MAX_DESCRIPTOR_SETS) {
				return false;
			}

			Vector<DescriptorBindingDesc> sorted{GetMemoryResource(MemTag::Graphics)};
			for (u32 set = 0; set < setCount; ++set) {
				sorted.clear();
				for (const DescriptorBindingDesc& binding : bindings) {
					if (binding.set == set) {
						sorted.push_back(binding);
					}
				}
				std::sort(sorted.begin(), sorted.end(), [](const DescriptorBindingDesc& a, const DescriptorBindingDesc& b) {
					return a.binding < b.binding;
				});
				setLayouts[set] = impl.GetOrCreateSetLayout({sorted.data(), sorted.size()});
			}

			VkPushConstantRange pushRange{};
			pushRange.stageFlags = pushStages;
			pushRange.size = pushConstantSize;

			VkPipelineLayoutCreateInfo layoutInfo{VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO};
			layoutInfo.setLayoutCount = setCount;
			layoutInfo.pSetLayouts = setLayouts;
			if (pushConstantSize > 0) {
				layoutInfo.pushConstantRangeCount = 1;
				layoutInfo.pPushConstantRanges = &pushRange;
			}
			SLT_VK_CHECK(impl.fd.vkCreatePipelineLayout(impl.device, &layoutInfo, nullptr, &layout));
			return true;
		}

		VkDescriptorPool CreateGrowablePool(DeviceImpl& impl, u32 sets) {
			VkDescriptorPoolSize sizes[] = {
				{VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER, sets * 2},
				{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, sets},
				{VK_DESCRIPTOR_TYPE_SAMPLED_IMAGE, sets * 4},
				{VK_DESCRIPTOR_TYPE_SAMPLER, sets * 2},
			};
			VkDescriptorPoolCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO};
			info.maxSets = sets;
			info.poolSizeCount = static_cast<u32>(std::size(sizes));
			info.pPoolSizes = sizes;
			VkDescriptorPool pool = VK_NULL_HANDLE;
			SLT_VK_CHECK(impl.fd.vkCreateDescriptorPool(impl.device, &info, nullptr, &pool));
			return pool;
		}

	} // anonymous namespace

	VkDescriptorSetLayout DeviceImpl::GetOrCreateSetLayout(std::span<const DescriptorBindingDesc> setBindings) {
		u64 hash = 1469598103934665603ull;
		auto mix = [&hash](u64 value) {
			hash ^= value;
			hash *= 1099511628211ull;
		};
		for (const DescriptorBindingDesc& binding : setBindings) {
			mix(binding.binding);
			mix(static_cast<u64>(binding.type) << 8);
			mix(static_cast<u64>(binding.stages) << 16);
		}

		auto it = setLayoutCache.find(hash);
		if (it != setLayoutCache.end()) {
			return it->second;
		}

		Vector<VkDescriptorSetLayoutBinding> vkBindings{GetMemoryResource(MemTag::Graphics)};
		for (const DescriptorBindingDesc& binding : setBindings) {
			VkDescriptorSetLayoutBinding vkBinding{};
			vkBinding.binding = binding.binding;
			vkBinding.descriptorType = ToVkDescriptorType(binding.type);
			vkBinding.descriptorCount = 1;
			vkBinding.stageFlags = ToVkStages(binding.stages);
			vkBindings.push_back(vkBinding);
		}

		VkDescriptorSetLayoutCreateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
		info.bindingCount = static_cast<u32>(vkBindings.size());
		info.pBindings = vkBindings.data();
		VkDescriptorSetLayout layout = VK_NULL_HANDLE;
		SLT_VK_CHECK(fd.vkCreateDescriptorSetLayout(device, &info, nullptr, &layout));

		setLayoutCache.emplace(hash, layout);
		return layout;
	}

	VkDescriptorSet DeviceImpl::AllocateTransientDescriptorSet(VkDescriptorSetLayout layout) {
		FrameContext& frame = frames[frameIndex];
		DescriptorAllocatorGrowable& allocator = frame.descriptors;

		auto tryAllocate = [&](VkDescriptorPool pool) -> VkDescriptorSet {
			VkDescriptorSetAllocateInfo info{VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO};
			info.descriptorPool = pool;
			info.descriptorSetCount = 1;
			info.pSetLayouts = &layout;
			VkDescriptorSet set = VK_NULL_HANDLE;
			VkResult result = fd.vkAllocateDescriptorSets(device, &info, &set);
			return result == VK_SUCCESS ? set : VK_NULL_HANDLE;
		};

		while (!allocator.readyPools.empty()) {
			VkDescriptorSet set = tryAllocate(allocator.readyPools.back());
			if (set) {
				return set;
			}
			allocator.fullPools.push_back(allocator.readyPools.back());
			allocator.readyPools.pop_back();
		}

		VkDescriptorPool fresh = CreateGrowablePool(*this, allocator.setsPerPool);
		allocator.readyPools.push_back(fresh);
		VkDescriptorSet set = tryAllocate(fresh);
		SLT_CORE_ASSERT_MSG(set != VK_NULL_HANDLE, "descriptor allocation failed from a fresh pool");
		return set;
	}

	namespace {

		bool SameContent(const ResolvedWrite* a, const ResolvedWrite* b, u32 count) {
			for (u32 i = 0; i < count; ++i) {
				if (a[i].object != b[i].object || a[i].offset != b[i].offset || a[i].range != b[i].range || a[i].binding != b[i].binding || a[i].type != b[i].type) {
					return false;
				}
			}
			return true;
		}

	} // anonymous namespace

	VkDescriptorSet DeviceImpl::FindTransientSetWithContent(VkDescriptorSetLayout layout, u64 hash, const ResolvedWrite* writes, u32 writeCount) {
		DescriptorAllocatorGrowable& allocator = frames[frameIndex].descriptors;
		u32 slot = static_cast<u32>(hash) & (DescriptorAllocatorGrowable::CACHE_SLOTS - 1);
		for (u32 probe = 0; probe < DescriptorAllocatorGrowable::CACHE_MAX_PROBES; ++probe) {
			u32 entryIndex = allocator.cacheSlots[slot];
			if (entryIndex == DescriptorAllocatorGrowable::CACHE_EMPTY) {
				return VK_NULL_HANDLE;
			}
			const DescriptorAllocatorGrowable::CachedSet& entry = allocator.cachedSets[entryIndex];
			if (entry.hash == hash && entry.layout == layout && entry.writeCount == writeCount && SameContent(allocator.cachedWrites.data() + entry.firstWrite, writes, writeCount)) {
				return entry.set;
			}
			slot = (slot + 1) & (DescriptorAllocatorGrowable::CACHE_SLOTS - 1);
		}
		return VK_NULL_HANDLE;
	}

	void DeviceImpl::RememberTransientSetContent(VkDescriptorSetLayout layout, u64 hash, const ResolvedWrite* writes, u32 writeCount, VkDescriptorSet set) {
		DescriptorAllocatorGrowable& allocator = frames[frameIndex].descriptors;
		u32 slot = static_cast<u32>(hash) & (DescriptorAllocatorGrowable::CACHE_SLOTS - 1);
		for (u32 probe = 0; probe < DescriptorAllocatorGrowable::CACHE_MAX_PROBES; ++probe) {
			if (allocator.cacheSlots[slot] == DescriptorAllocatorGrowable::CACHE_EMPTY) {
				DescriptorAllocatorGrowable::CachedSet entry;
				entry.hash = hash;
				entry.layout = layout;
				entry.set = set;
				entry.firstWrite = static_cast<u32>(allocator.cachedWrites.size());
				entry.writeCount = writeCount;
				for (u32 i = 0; i < writeCount; ++i) {
					allocator.cachedWrites.push_back(writes[i]);
				}
				allocator.cacheSlots[slot] = static_cast<u32>(allocator.cachedSets.size());
				allocator.cachedSets.push_back(entry);
				return;
			}
			slot = (slot + 1) & (DescriptorAllocatorGrowable::CACHE_SLOTS - 1);
		}
	}

	void DeviceImpl::ResetFrameDescriptors(FrameContext& frame) {
		frame.descriptors.ClearContentCache();

		for (VkDescriptorPool pool : frame.descriptors.fullPools) {
			fd.vkResetDescriptorPool(device, pool, 0);
			frame.descriptors.readyPools.push_back(pool);
		}
		frame.descriptors.fullPools.clear();
		for (VkDescriptorPool pool : frame.descriptors.readyPools) {
			fd.vkResetDescriptorPool(device, pool, 0);
		}
	}

	void DeviceImpl::DestroyFrameDescriptors(FrameContext& frame) {
		frame.descriptors.ClearContentCache();
		for (VkDescriptorPool pool : frame.descriptors.readyPools) {
			fd.vkDestroyDescriptorPool(device, pool, nullptr);
		}
		for (VkDescriptorPool pool : frame.descriptors.fullPools) {
			fd.vkDestroyDescriptorPool(device, pool, nullptr);
		}
		frame.descriptors.readyPools.clear();
		frame.descriptors.fullPools.clear();
	}

	TransientSetWriter Device::CreateTransientSet(PipelineHandle pipeline, u32 setIndex) {
		DeviceImpl* impl = _impl;
		SLT_RHI_ASSERT_MAIN_THREAD(impl);
		SLT_CORE_ASSERT_MSG(impl->inFrame, "CreateTransientSet outside a frame");
		SLT_CORE_ASSERT(setIndex < MAX_DESCRIPTOR_SETS);

		TransientSetWriter writer;
		PipelineResource* resource = impl->pipelines.Resolve(pipeline);
		if (!resource) {
			SLT_LOG_WARN("Graphics", "CreateTransientSet: invalid pipeline handle");
			return writer;
		}
		writer._device = impl;
		writer._pipelineIndex = pipeline.index;
		writer._setIndex = setIndex;
		return writer;
	}

	TransientSetWriter& TransientSetWriter::WriteUniformBuffer(u32 binding, BufferHandle buffer, u64 offset, u64 range) {
		if (_device && _writeCount < MAX_WRITES) {
			_writes[_writeCount++] = {binding, DescriptorType::UniformBuffer, offset, range, buffer.index, buffer.generation};
		}
		return *this;
	}

	TransientSetWriter& TransientSetWriter::WriteStorageBuffer(u32 binding, BufferHandle buffer, u64 offset, u64 range) {
		if (_device && _writeCount < MAX_WRITES) {
			_writes[_writeCount++] = {binding, DescriptorType::StorageBuffer, offset, range, buffer.index, buffer.generation};
		}
		return *this;
	}

	TransientSetWriter& TransientSetWriter::WriteTexture(u32 binding, TextureHandle texture) {
		if (_device && _writeCount < MAX_WRITES) {
			_writes[_writeCount++] = {binding, DescriptorType::SampledTexture, 0, 0, texture.index, texture.generation};
		}
		return *this;
	}

	TransientSetWriter& TransientSetWriter::WriteSampler(u32 binding, SamplerHandle sampler) {
		if (_device && _writeCount < MAX_WRITES) {
			_writes[_writeCount++] = {binding, DescriptorType::Sampler, 0, 0, sampler.index, sampler.generation};
		}
		return *this;
	}

	TransientSet TransientSetWriter::Commit() {
		if (!_device) {
			return {};
		}
		DeviceImpl& impl = *_device;
		PipelineResource& pipeline = impl.pipelines.slots[_pipelineIndex].resource;
		if (_setIndex >= pipeline.setCount) {
			SLT_LOG_WARN("Graphics", "TransientSetWriter: set {} not present on the pipeline", _setIndex);
			return {};
		}

		VkDescriptorSetLayout layout = pipeline.setLayouts[_setIndex];

		ResolvedWrite resolved[MAX_WRITES];
		u32 count = 0;
		u64 hash = 1469598103934665603ull;
		auto mix = [&hash](u64 value) {
			hash ^= value;
			hash *= 1099511628211ull;
		};
		mix(reinterpret_cast<u64>(layout));
		for (u32 i = 0; i < _writeCount; ++i) {
			const StagedWrite& staged = _writes[i];
			ResolvedWrite& out = resolved[count];
			out = ResolvedWrite{};
			out.binding = staged.binding;
			out.type = staged.type;

			switch (staged.type) {
			case DescriptorType::UniformBuffer:
			case DescriptorType::StorageBuffer: {
				BufferResource* buffer = impl.buffers.Resolve(BufferHandle{staged.resourceIndex, staged.resourceGeneration});
				if (!buffer) {
					SLT_LOG_WARN("Graphics", "TransientSetWriter: stale buffer at binding {}", staged.binding);
					continue;
				}
				if (staged.type == DescriptorType::StorageBuffer &&
					!HasFlag(buffer->usage, BufferUsage::Storage) && !HasFlag(buffer->usage, BufferUsage::Vertex)) {
					SLT_LOG_ERROR("Graphics", "TransientSetWriter: the buffer at binding {} wasn't created with Storage usage", staged.binding);
					continue;
				}
				out.object = reinterpret_cast<u64>(buffer->buffer);
				out.offset = staged.a;
				out.range = staged.b;
				break;
			}
			case DescriptorType::SampledTexture: {
				TextureResource* texture = impl.textures.Resolve(TextureHandle{staged.resourceIndex, staged.resourceGeneration});
				if (!texture) {
					SLT_LOG_WARN("Graphics", "TransientSetWriter: stale texture at binding {}", staged.binding);
					continue;
				}
				if (texture->state != ResourceState::ShaderResource) {
					SLT_LOG_ERROR("Graphics", "TransientSetWriter: texture at binding {} is in state {}, not ShaderResource (missing TransitionTexture)",
								  staged.binding, ResourceStateName(texture->state));
				}
				out.object = reinterpret_cast<u64>(texture->view);
				out.imageLayout = ToStateMasks(ResourceState::ShaderResource, IsDepthFormat(texture->format)).layout;
				break;
			}
			case DescriptorType::Sampler: {
				SamplerResource* sampler = impl.samplers.Resolve(SamplerHandle{staged.resourceIndex, staged.resourceGeneration});
				if (!sampler) {
					SLT_LOG_WARN("Graphics", "TransientSetWriter: stale sampler at binding {}", staged.binding);
					continue;
				}
				out.object = reinterpret_cast<u64>(sampler->sampler);
				break;
			}
			default:
				continue;
			}
			mix(out.object);
			mix(out.offset ^ (out.range << 1));
			mix(static_cast<u64>(out.binding) | (static_cast<u64>(out.type) << 32));
			++count;
		}
		mix(count);

		VkDescriptorSet set = impl.FindTransientSetWithContent(layout, hash, resolved, count);
		if (!set) {
			set = impl.AllocateTransientDescriptorSet(layout);

			VkWriteDescriptorSet writes[MAX_WRITES]{};
			VkDescriptorBufferInfo bufferInfos[MAX_WRITES]{};
			VkDescriptorImageInfo imageInfos[MAX_WRITES]{};
			for (u32 i = 0; i < count; ++i) {
				const ResolvedWrite& item = resolved[i];
				VkWriteDescriptorSet& write = writes[i];
				write.sType = VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET;
				write.dstSet = set;
				write.dstBinding = item.binding;
				write.descriptorCount = 1;
				write.descriptorType = ToVkDescriptorType(item.type);
				switch (item.type) {
				case DescriptorType::SampledTexture:
					imageInfos[i] = {VK_NULL_HANDLE, reinterpret_cast<VkImageView>(item.object), item.imageLayout};
					write.pImageInfo = &imageInfos[i];
					break;
				case DescriptorType::Sampler:
					imageInfos[i] = {reinterpret_cast<VkSampler>(item.object), VK_NULL_HANDLE, VK_IMAGE_LAYOUT_UNDEFINED};
					write.pImageInfo = &imageInfos[i];
					break;
				default:
					bufferInfos[i] = {reinterpret_cast<VkBuffer>(item.object), item.offset, item.range};
					write.pBufferInfo = &bufferInfos[i];
					break;
				}
			}
			if (count > 0) {
				impl.fd.vkUpdateDescriptorSets(impl.device, count, writes, 0, nullptr);
			}
			if (set) {
				impl.RememberTransientSetContent(layout, hash, resolved, count, set);
			}
		}

		TransientSet result;
		result.opaque = reinterpret_cast<u64>(set);
		return result;
	}

	PipelineHandle Device::CreateGraphicsPipeline(const GraphicsPipelineDesc& desc) {
		DeviceImpl* impl = _impl;
		SLT_RHI_ASSERT_MAIN_THREAD(impl);
		if (desc.vertexSpirv.empty()) {
			SLT_LOG_ERROR("Graphics", "CreateGraphicsPipeline: missing the vertex stage ('{}')", desc.debugName ? desc.debugName : "<unnamed>");
			return {};
		}
		const bool depthOnly = desc.pixelSpirv.empty();
		if (depthOnly && desc.colorFormatCount > 0) {
			SLT_LOG_ERROR("Graphics", "CreateGraphicsPipeline: a pipeline with color attachments needs a px stage ('{}')", desc.debugName ? desc.debugName : "<unnamed>");
			return {};
		}
		if (desc.pushConstantSize > 128) {
			SLT_LOG_ERROR("Graphics", "CreateGraphicsPipeline: push constants {} B exceed the 128 B budget", desc.pushConstantSize);
			return {};
		}

		auto createModule = [&](std::span<const u8> spirv) -> VkShaderModule {
			if (spirv.size() % 4 != 0) {
				return VK_NULL_HANDLE;
			}
			VkShaderModuleCreateInfo info{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
			info.codeSize = spirv.size();
			info.pCode = reinterpret_cast<const u32*>(spirv.data());
			VkShaderModule module = VK_NULL_HANDLE;
			if (impl->fd.vkCreateShaderModule(impl->device, &info, nullptr, &module) != VK_SUCCESS) {
				return VK_NULL_HANDLE;
			}
			return module;
		};
		VkShaderModule vertexModule = createModule(desc.vertexSpirv);
		VkShaderModule pixelModule = depthOnly ? VK_NULL_HANDLE : createModule(desc.pixelSpirv);
		if (!vertexModule || (!depthOnly && !pixelModule)) {
			if (vertexModule) {
				impl->fd.vkDestroyShaderModule(impl->device, vertexModule, nullptr);
			}
			if (pixelModule) {
				impl->fd.vkDestroyShaderModule(impl->device, pixelModule, nullptr);
			}
			SLT_LOG_ERROR("Graphics", "CreateGraphicsPipeline: shader module creation failed ('{}')", desc.debugName ? desc.debugName : "<unnamed>");
			return {};
		}

		VkDescriptorSetLayout setLayouts[MAX_DESCRIPTOR_SETS]{};
		u32 setCount = 0;
		VkPipelineLayout layout = VK_NULL_HANDLE;
		if (!BuildPipelineLayout(*impl, desc.bindings, desc.pushConstantSize, VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT,
								 setLayouts, setCount, layout)) {
			impl->fd.vkDestroyShaderModule(impl->device, vertexModule, nullptr);
			impl->fd.vkDestroyShaderModule(impl->device, pixelModule, nullptr);
			SLT_LOG_ERROR("Graphics", "CreateGraphicsPipeline: set idx exceeds the set-{} convention", MAX_DESCRIPTOR_SETS - 1);
			return {};
		}

		VkPipelineShaderStageCreateInfo stages[2]{};
		stages[0].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT;
		stages[0].module = vertexModule;
		stages[0].pName = "VSMain";
		stages[1].sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT;
		stages[1].module = pixelModule;
		stages[1].pName = "PSMain";
		const u32 stageCount = depthOnly ? 1u : 2u;

		VkPipelineVertexInputStateCreateInfo vertexInput{VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};

		VkPipelineInputAssemblyStateCreateInfo inputAssembly{VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO};
		inputAssembly.topology = desc.topology == PrimitiveTopology::LineList	 ? VK_PRIMITIVE_TOPOLOGY_LINE_LIST
								 : desc.topology == PrimitiveTopology::PointList ? VK_PRIMITIVE_TOPOLOGY_POINT_LIST
																				 : VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;

		VkPipelineViewportStateCreateInfo viewport{VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO};
		viewport.viewportCount = 1;
		viewport.scissorCount = 1;

		VkPipelineRasterizationStateCreateInfo rasterization{VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO};
		rasterization.polygonMode = VK_POLYGON_MODE_FILL;
		if (desc.fill == FillMode::Wireframe) {
			if (impl->fillModeNonSolid) {
				rasterization.polygonMode = VK_POLYGON_MODE_LINE;
			} else if (!impl->warnedNoFillModeNonSolid) {
				impl->warnedNoFillModeNonSolid = true;
				SLT_LOG_WARN("Graphics", "the selected adapter has no fillModeNonSolid (wireframe pipelines draw solid)");
			}
		}
		rasterization.lineWidth = 1.0f;
		if (desc.depthBiasConstant != 0.0f || desc.depthBiasSlope != 0.0f) {
			rasterization.depthBiasEnable = VK_TRUE;
			rasterization.depthBiasConstantFactor = desc.depthBiasConstant;
			rasterization.depthBiasSlopeFactor = desc.depthBiasSlope;
		}
		switch (desc.cull) {
		case CullMode::Back:
		case CullMode::Count:
			rasterization.cullMode = VK_CULL_MODE_BACK_BIT;
			break;
		case CullMode::Front:
			rasterization.cullMode = VK_CULL_MODE_FRONT_BIT;
			break;
		case CullMode::Off:
			rasterization.cullMode = VK_CULL_MODE_NONE;
			break;
		}

		VkPipelineMultisampleStateCreateInfo multisample{VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO};
		multisample.rasterizationSamples = ToVkSampleCount(desc.samples);

		VkPipelineDepthStencilStateCreateInfo depthStencil{VK_STRUCTURE_TYPE_PIPELINE_DEPTH_STENCIL_STATE_CREATE_INFO};
		switch (desc.depth) {
		case DepthMode::TestWrite:
			depthStencil.depthTestEnable = VK_TRUE;
			depthStencil.depthWriteEnable = VK_TRUE;
			break;
		case DepthMode::TestOnly:
			depthStencil.depthTestEnable = VK_TRUE;
			break;
		case DepthMode::Off:
			break;
		}
		switch (desc.depthCompare) {
		case DepthCompare::Nearer:
			depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER;
			break;
		case DepthCompare::Farther:
			depthStencil.depthCompareOp = VK_COMPARE_OP_LESS;
			break;
		case DepthCompare::FartherOrEqual:
			depthStencil.depthCompareOp = VK_COMPARE_OP_LESS_OR_EQUAL;
			break;
		case DepthCompare::Equal:
			depthStencil.depthCompareOp = VK_COMPARE_OP_EQUAL;
			break;
		case DepthCompare::NotEqual:
			depthStencil.depthCompareOp = VK_COMPARE_OP_NOT_EQUAL;
			break;
		case DepthCompare::Always:
			depthStencil.depthCompareOp = VK_COMPARE_OP_ALWAYS;
			break;
		case DepthCompare::NearerOrEqual:
		case DepthCompare::Count:
			depthStencil.depthCompareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
			break;
		}

		VkPipelineColorBlendAttachmentState blendAttachments[MAX_COLOR_ATTACHMENTS]{};
		for (u32 i = 0; i < desc.colorFormatCount; ++i) {
			VkPipelineColorBlendAttachmentState& attachment = blendAttachments[i];
			attachment.colorWriteMask = VK_COLOR_COMPONENT_R_BIT | VK_COLOR_COMPONENT_G_BIT | VK_COLOR_COMPONENT_B_BIT | VK_COLOR_COMPONENT_A_BIT;
			switch (desc.blend) {
			case BlendMode::Opaque:
			case BlendMode::Count:
				break;
			case BlendMode::Alpha:
				attachment.blendEnable = VK_TRUE;
				attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				attachment.colorBlendOp = VK_BLEND_OP_ADD;
				attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				attachment.alphaBlendOp = VK_BLEND_OP_ADD;
				break;
			case BlendMode::Additive:
				attachment.blendEnable = VK_TRUE;
				attachment.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA;
				attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE;
				attachment.colorBlendOp = VK_BLEND_OP_ADD;
				attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
				attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				attachment.alphaBlendOp = VK_BLEND_OP_ADD;
				break;
			case BlendMode::Premultiplied:
				attachment.blendEnable = VK_TRUE;
				attachment.srcColorBlendFactor = VK_BLEND_FACTOR_ONE;
				attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				attachment.colorBlendOp = VK_BLEND_OP_ADD;
				attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE;
				attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA;
				attachment.alphaBlendOp = VK_BLEND_OP_ADD;
				break;
			case BlendMode::Multiply:
				attachment.blendEnable = VK_TRUE;
				attachment.srcColorBlendFactor = VK_BLEND_FACTOR_DST_COLOR;
				attachment.dstColorBlendFactor = VK_BLEND_FACTOR_ZERO;
				attachment.colorBlendOp = VK_BLEND_OP_ADD;
				attachment.srcAlphaBlendFactor = VK_BLEND_FACTOR_DST_ALPHA;
				attachment.dstAlphaBlendFactor = VK_BLEND_FACTOR_ZERO;
				attachment.alphaBlendOp = VK_BLEND_OP_ADD;
				break;
			}
		}
		VkPipelineColorBlendStateCreateInfo blend{VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO};
		blend.attachmentCount = desc.colorFormatCount;
		blend.pAttachments = blendAttachments;

		VkDynamicState dynamicStates[] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR, VK_DYNAMIC_STATE_FRONT_FACE};
		VkPipelineDynamicStateCreateInfo dynamic{VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO};
		dynamic.dynamicStateCount = static_cast<u32>(std::size(dynamicStates));
		dynamic.pDynamicStates = dynamicStates;

		VkFormat colorFormats[MAX_COLOR_ATTACHMENTS]{};
		for (u32 i = 0; i < desc.colorFormatCount; ++i) {
			colorFormats[i] = ToVkFormat(desc.colorFormats[i]);
		}
		VkPipelineRenderingCreateInfo rendering{VK_STRUCTURE_TYPE_PIPELINE_RENDERING_CREATE_INFO};
		rendering.colorAttachmentCount = desc.colorFormatCount;
		rendering.pColorAttachmentFormats = colorFormats;
		rendering.depthAttachmentFormat = ToVkFormat(desc.depthFormat);

		VkGraphicsPipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO};
		pipelineInfo.pNext = &rendering;
		pipelineInfo.stageCount = stageCount;
		pipelineInfo.pStages = stages;
		pipelineInfo.pVertexInputState = &vertexInput;
		pipelineInfo.pInputAssemblyState = &inputAssembly;
		pipelineInfo.pViewportState = &viewport;
		pipelineInfo.pRasterizationState = &rasterization;
		pipelineInfo.pMultisampleState = &multisample;
		pipelineInfo.pDepthStencilState = &depthStencil;
		pipelineInfo.pColorBlendState = &blend;
		pipelineInfo.pDynamicState = &dynamic;
		pipelineInfo.layout = layout;

		VkPipeline pipeline = VK_NULL_HANDLE;
		VkResult result = impl->fd.vkCreateGraphicsPipelines(impl->device, impl->pipelineCache, 1, &pipelineInfo, nullptr, &pipeline);

		impl->fd.vkDestroyShaderModule(impl->device, vertexModule, nullptr);
		impl->fd.vkDestroyShaderModule(impl->device, pixelModule, nullptr);

		if (result != VK_SUCCESS) {
			impl->fd.vkDestroyPipelineLayout(impl->device, layout, nullptr);
			SLT_LOG_ERROR("Graphics", "CreateGraphicsPipeline failed ({}): {}", desc.debugName ? desc.debugName : "<unnamed>", VkResultName(result));
			return {};
		}

		auto [index, generation] = impl->pipelines.Acquire();
		PipelineResource& resource = impl->pipelines.slots[index].resource;
		resource.pipeline = pipeline;
		resource.layout = layout;
		for (u32 set = 0; set < setCount; ++set) {
			resource.setLayouts[set] = setLayouts[set];
		}
		resource.setCount = setCount;
		resource.bindings.assign(desc.bindings.begin(), desc.bindings.end());
		resource.pushConstantSize = desc.pushConstantSize;
		for (u32 i = 0; i < desc.colorFormatCount; ++i) {
			resource.colorFormats[i] = desc.colorFormats[i];
		}
		resource.colorFormatCount = desc.colorFormatCount;
		resource.depthFormat = desc.depthFormat;
		resource.samples = std::max(1u, desc.samples);

		impl->SetDebugName(reinterpret_cast<u64>(pipeline), VK_OBJECT_TYPE_PIPELINE, desc.debugName);
		return PipelineHandle{index, generation};
	}

	PipelineHandle Device::CreateComputePipeline(const ComputePipelineDesc& desc) {
		DeviceImpl* impl = _impl;
		SLT_RHI_ASSERT_MAIN_THREAD(impl);
		const char* name = desc.debugName ? desc.debugName : "<unnamed>";
		if (desc.spirv.empty() || desc.spirv.size() % 4 != 0) {
			SLT_LOG_ERROR("Graphics", "CreateComputePipeline: no kernel stage ('{}')", name);
			return {};
		}
		if (desc.pushConstantSize > 128) {
			SLT_LOG_ERROR("Graphics", "CreateComputePipeline: push constants {} B exceed the 128 B budget", desc.pushConstantSize);
			return {};
		}

		VkShaderModuleCreateInfo moduleInfo{VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO};
		moduleInfo.codeSize = desc.spirv.size();
		moduleInfo.pCode = reinterpret_cast<const u32*>(desc.spirv.data());
		VkShaderModule module = VK_NULL_HANDLE;
		if (impl->fd.vkCreateShaderModule(impl->device, &moduleInfo, nullptr, &module) != VK_SUCCESS) {
			SLT_LOG_ERROR("Graphics", "CreateComputePipeline: shader module creation failed ('{}')", name);
			return {};
		}

		VkDescriptorSetLayout setLayouts[MAX_DESCRIPTOR_SETS]{};
		u32 setCount = 0;
		VkPipelineLayout layout = VK_NULL_HANDLE;
		if (!BuildPipelineLayout(*impl, desc.bindings, desc.pushConstantSize, VK_SHADER_STAGE_COMPUTE_BIT, setLayouts, setCount, layout)) {
			impl->fd.vkDestroyShaderModule(impl->device, module, nullptr);
			SLT_LOG_ERROR("Graphics", "CreateComputePipeline: set idx exceeds the set-{} convention ('{}')", MAX_DESCRIPTOR_SETS - 1, name);
			return {};
		}

		VkComputePipelineCreateInfo pipelineInfo{VK_STRUCTURE_TYPE_COMPUTE_PIPELINE_CREATE_INFO};
		pipelineInfo.stage.sType = VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO;
		pipelineInfo.stage.stage = VK_SHADER_STAGE_COMPUTE_BIT;
		pipelineInfo.stage.module = module;
		pipelineInfo.stage.pName = "CSMain";
		pipelineInfo.layout = layout;
		VkPipeline pipeline = VK_NULL_HANDLE;
		VkResult result = impl->fd.vkCreateComputePipelines(impl->device, impl->pipelineCache, 1, &pipelineInfo, nullptr, &pipeline);
		impl->fd.vkDestroyShaderModule(impl->device, module, nullptr);
		if (result != VK_SUCCESS) {
			impl->fd.vkDestroyPipelineLayout(impl->device, layout, nullptr);
			SLT_LOG_ERROR("Graphics", "CreateComputePipeline failed ({}): {}", name, VkResultName(result));
			return {};
		}

		auto [index, generation] = impl->pipelines.Acquire();
		PipelineResource& resource = impl->pipelines.slots[index].resource;
		resource.pipeline = pipeline;
		resource.layout = layout;
		resource.bindPoint = VK_PIPELINE_BIND_POINT_COMPUTE;
		resource.pushStages = VK_SHADER_STAGE_COMPUTE_BIT;
		for (u32 set = 0; set < setCount; ++set) {
			resource.setLayouts[set] = setLayouts[set];
		}
		resource.setCount = setCount;
		resource.bindings.assign(desc.bindings.begin(), desc.bindings.end());
		resource.pushConstantSize = desc.pushConstantSize;

		impl->SetDebugName(reinterpret_cast<u64>(pipeline), VK_OBJECT_TYPE_PIPELINE, desc.debugName);
		return PipelineHandle{index, generation};
	}

} // namespace Shalltear::RHI

#endif
