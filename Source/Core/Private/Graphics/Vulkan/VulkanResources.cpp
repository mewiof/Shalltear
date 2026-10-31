#include "Shalltear/Core/Platform.h"

#if defined(SLT_PLATFORM_WINDOWS)

#include "Graphics/Vulkan/VulkanInternal.h"

#include "Shalltear/Core/Profiler.h"

#include <algorithm>
#include <cstring>
#include <cstddef>

namespace Shalltear::RHI {

	namespace {

		VkBufferUsageFlags ToVkBufferUsage(BufferUsage usage) {
			VkBufferUsageFlags flags = 0;
			if (HasFlag(usage, BufferUsage::Vertex)) {
				flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			}
			if (HasFlag(usage, BufferUsage::Index)) {
				flags |= VK_BUFFER_USAGE_INDEX_BUFFER_BIT;
			}
			if (HasFlag(usage, BufferUsage::Uniform)) {
				flags |= VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT;
			}
			if (HasFlag(usage, BufferUsage::Storage)) {
				flags |= VK_BUFFER_USAGE_STORAGE_BUFFER_BIT;
			}
			if (HasFlag(usage, BufferUsage::TransferSrc)) {
				flags |= VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			}
			if (HasFlag(usage, BufferUsage::TransferDst)) {
				flags |= VK_BUFFER_USAGE_TRANSFER_DST_BIT;
			}
			if (HasFlag(usage, BufferUsage::DeviceAddress)) {
				flags |= VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
			}
			if (HasFlag(usage, BufferUsage::Indirect)) {
				flags |= VK_BUFFER_USAGE_INDIRECT_BUFFER_BIT;
			}
			return flags;
		}

	} // anonymous namespace

	VkImageUsageFlags ToVkImageUsage(TextureUsage usage) {
		VkImageUsageFlags flags = 0;
		if (HasFlag(usage, TextureUsage::Sampled)) {
			flags |= VK_IMAGE_USAGE_SAMPLED_BIT;
		}
		if (HasFlag(usage, TextureUsage::RenderTarget)) {
			flags |= VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT;
		}
		if (HasFlag(usage, TextureUsage::DepthStencil)) {
			flags |= VK_IMAGE_USAGE_DEPTH_STENCIL_ATTACHMENT_BIT;
		}
		if (HasFlag(usage, TextureUsage::TransferSrc)) {
			flags |= VK_IMAGE_USAGE_TRANSFER_SRC_BIT;
		}
		if (HasFlag(usage, TextureUsage::TransferDst)) {
			flags |= VK_IMAGE_USAGE_TRANSFER_DST_BIT;
		}
		return flags;
	}

	VkSampleCountFlagBits ToVkSampleCount(u32 samples) {
		switch (samples) {
		case 2:
			return VK_SAMPLE_COUNT_2_BIT;
		case 4:
			return VK_SAMPLE_COUNT_4_BIT;
		case 8:
			return VK_SAMPLE_COUNT_8_BIT;
		case 16:
			return VK_SAMPLE_COUNT_16_BIT;
		default:
			return VK_SAMPLE_COUNT_1_BIT;
		}
	}

	BufferHandle Device::CreateBufferResource(const BufferDesc& desc) {
		DeviceImpl* impl = _impl;
		SLT_RHI_ASSERT_MAIN_THREAD(impl);
		if (desc.size == 0) {
			SLT_LOG_ERROR("Graphics", "CreateBufferResource: 0 size ('{}')", desc.debugName ? desc.debugName : "<unnamed>");
			return {};
		}

		VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		bufferInfo.size = desc.size;
		bufferInfo.usage = ToVkBufferUsage(desc.usage);

		VmaAllocationCreateInfo allocInfo{};
		switch (desc.access) {
		case MemoryAccess::GPUOnly:
			allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;
			break;
		case MemoryAccess::CPUToGPU:
			allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
			allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
			break;
		case MemoryAccess::Readback:
			allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
			allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
			allocInfo.requiredFlags = VK_MEMORY_PROPERTY_HOST_COHERENT_BIT;
			break;
		}

		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		VmaAllocationInfo resultInfo{};
		VkResult result = vmaCreateBuffer(impl->allocator, &bufferInfo, &allocInfo, &buffer, &allocation, &resultInfo);
		if (result != VK_SUCCESS) {
			SLT_LOG_ERROR("Graphics", "CreateBufferResource failed ({}): {}", desc.debugName ? desc.debugName : "<unnamed>", VkResultName(result));
			return {};
		}

		auto [index, generation] = impl->buffers.Acquire();
		BufferResource& resource = impl->buffers.slots[index].resource;
		resource.buffer = buffer;
		resource.allocation = allocation;
		resource.size = desc.size;
		resource.usage = desc.usage;
		resource.access = desc.access;
		resource.mapped = resultInfo.pMappedData;
		if (HasFlag(desc.usage, BufferUsage::DeviceAddress)) {
			VkBufferDeviceAddressInfo addressInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
			addressInfo.buffer = buffer;
			resource.deviceAddress = impl->fd.vkGetBufferDeviceAddress(impl->device, &addressInfo);
		}

		impl->SetDebugName(reinterpret_cast<u64>(buffer), VK_OBJECT_TYPE_BUFFER, desc.debugName);
		return BufferHandle{index, generation};
	}

	void Device::DestroyResource(BufferHandle handle) {
		DeviceImpl* impl = _impl;
		SLT_RHI_ASSERT_MAIN_THREAD(impl);
		BufferResource* resource = impl->buffers.Resolve(handle);
		if (!resource) {
			if (handle.IsValid()) {
				SLT_LOG_WARN("Graphics", "DestroyResource: stale buffer handle ({}, gen {})", handle.index, handle.generation);
			}
			return;
		}
		for (size_t i = impl->pendingUploads.size(); i-- > 0;) {
			if (impl->pendingUploads[i].target == resource->buffer) {
				vmaDestroyBuffer(impl->allocator, impl->pendingUploads[i].staging, impl->pendingUploads[i].stagingAllocation);
				impl->pendingUploads.erase(impl->pendingUploads.begin() + static_cast<std::ptrdiff_t>(i));
			}
		}
		impl->DeletionQueueForDestroy().buffers.push_back({resource->buffer, resource->allocation});
		*resource = BufferResource{};
		impl->buffers.Release(handle.index);
	}

	u64 Device::GetBufferDeviceAddress(BufferHandle buffer) const {
		BufferResource* resource = _impl->buffers.Resolve(buffer);
		if (!resource || resource->deviceAddress == 0) {
			SLT_LOG_WARN("Graphics", "GetBufferDeviceAddress: invalid handle or missing DeviceAddress usage");
			return 0;
		}
		return resource->deviceAddress;
	}

	void* Device::GetMappedPointer(BufferHandle buffer) const {
		BufferResource* resource = _impl->buffers.Resolve(buffer);
		return resource ? resource->mapped : nullptr;
	}

	TextureHandle Device::CreateTextureResource(const TextureDesc& desc) {
		DeviceImpl* impl = _impl;
		SLT_RHI_ASSERT_MAIN_THREAD(impl);
		if (desc.size.x <= 0 || desc.size.y <= 0 || desc.format == Format::Unknown) {
			SLT_LOG_ERROR("Graphics", "CreateTextureResource: invalid desc ('{}')", desc.debugName ? desc.debugName : "<unnamed>");
			return {};
		}

		const TextureDimension dimension = ResolveTextureDimension(desc);
		if (desc.dimension) {
			const char* contradiction = nullptr;
			if (desc.cube && dimension != TextureDimension::Cube && dimension != TextureDimension::CubeArray) {
				contradiction = "its cube flag";
			} else if (desc.cubeArray && dimension != TextureDimension::CubeArray) {
				contradiction = "its cube-array flag";
			} else if (desc.depth > 1 && dimension != TextureDimension::Texture3D) {
				contradiction = "its depth";
			} else if (desc.arrayLayers > 1 && (dimension == TextureDimension::Texture2D || dimension == TextureDimension::Texture3D)) {
				contradiction = "its layers";
			}
			if (contradiction) {
				SLT_LOG_ERROR("Graphics", "CreateTextureResource: a {} texture contradicts {} ('{}')", TextureDimensionName(dimension), contradiction,
							  desc.debugName ? desc.debugName : "<unnamed>");
				return {};
			}
		}
		const bool cube = dimension == TextureDimension::Cube || dimension == TextureDimension::CubeArray;
		const bool volume = dimension == TextureDimension::Texture3D;
		if (desc.depth > 1 && !volume) {
			SLT_LOG_ERROR("Graphics", "CreateTextureResource: a volume (depth {}) can't combine with layers or a cube ('{}')", desc.depth,
						  desc.debugName ? desc.debugName : "<unnamed>");
			return {};
		}

		const bool cubeArray = dimension == TextureDimension::CubeArray;
		u32 arrayLayers = cube ? (cubeArray ? std::max(6u, desc.arrayLayers) : 6) : std::max(1u, desc.arrayLayers);
		if (cubeArray && arrayLayers % 6 != 0) {
			u32 rounded = ((arrayLayers + 5) / 6) * 6;
			SLT_LOG_WARN("Graphics", "CreateTextureResource: cube array layers should be a multiple of 6, not {} (using {} for '{}')",
						 desc.arrayLayers, rounded, desc.debugName ? desc.debugName : "<unnamed>");
			arrayLayers = rounded;
		}
		if (!cubeArray && cube && desc.arrayLayers > 1 && desc.arrayLayers != 6) {
			SLT_LOG_WARN("Graphics", "CreateTextureResource: a cube has 6 layers, not {} ('{}')", desc.arrayLayers,
						 desc.debugName ? desc.debugName : "<unnamed>");
		}
		if (cubeArray && !impl->imageCubeArray) {
			SLT_LOG_ERROR("Graphics", "CreateTextureResource: the adapter has no imageCubeArray ('{}')",
						  desc.debugName ? desc.debugName : "<unnamed>");
			return {};
		}
		const u32 depth = std::max(1u, desc.depth);
		if (volume) {
			const char* refusal = nullptr;
			if (arrayLayers > 1) {
				refusal = "layers or a cube";
			} else if (desc.samples > 1) {
				refusal = "multisampling";
			} else if (desc.mipLevels > 1) {
				refusal = "mip levels";
			} else if (HasFlag(desc.usage, TextureUsage::RenderTarget) || HasFlag(desc.usage, TextureUsage::DepthStencil)) {
				refusal = "being an attachment";
			}
			if (refusal) {
				SLT_LOG_ERROR("Graphics", "CreateTextureResource: a volume (depth {}) can't combine with {} ('{}')",
							  depth, refusal, desc.debugName ? desc.debugName : "<unnamed>");
				return {};
			}
		}

		if (IsBlockCompressed(desc.format)) {
			const char* refusal = nullptr;
			if (!impl->textureCompressionBC) {
				refusal = "an adapter without textureCompressionBC";
			} else if (HasFlag(desc.usage, TextureUsage::RenderTarget) || HasFlag(desc.usage, TextureUsage::DepthStencil)) {
				refusal = "being an attachment";
			} else if (desc.samples > 1) {
				refusal = "multisampling";
			} else if (volume) {
				refusal = "a volume";
			}
			if (refusal) {
				SLT_LOG_ERROR("Graphics", "CreateTextureResource: a block-compressed texture can't combine with {} ('{}')", refusal,
							  desc.debugName ? desc.debugName : "<unnamed>");
				return {};
			}
		}

		u32 samples = std::max(1u, desc.samples);
		if (samples > 1 && (desc.mipLevels > 1 || arrayLayers > 1)) {
			SLT_LOG_ERROR("Graphics", "CreateTextureResource: {}x multisampling needs 1 mip and 1 layer ('{}')", samples,
						  desc.debugName ? desc.debugName : "<unnamed>");
			return {};
		}

		bool isDepth = IsDepthFormat(desc.format);
		VkImageCreateInfo imageInfo{VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO};
		imageInfo.imageType = volume ? VK_IMAGE_TYPE_3D : VK_IMAGE_TYPE_2D;
		imageInfo.format = ToVkFormat(desc.format);
		imageInfo.extent = {static_cast<u32>(desc.size.x), static_cast<u32>(desc.size.y), depth};
		imageInfo.mipLevels = desc.mipLevels;
		imageInfo.arrayLayers = arrayLayers;
		imageInfo.samples = ToVkSampleCount(samples);
		imageInfo.tiling = VK_IMAGE_TILING_OPTIMAL;
		imageInfo.usage = ToVkImageUsage(desc.usage);
		imageInfo.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
		if (cube) {
			imageInfo.flags |= VK_IMAGE_CREATE_CUBE_COMPATIBLE_BIT;
		}

		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_DEVICE;

		VkImage image = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		VkResult result = vmaCreateImage(impl->allocator, &imageInfo, &allocInfo, &image, &allocation, nullptr);
		if (result != VK_SUCCESS) {
			SLT_LOG_ERROR("Graphics", "CreateTextureResource failed ({}): {}", desc.debugName ? desc.debugName : "<unnamed>", VkResultName(result));
			return {};
		}

		VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
		viewInfo.image = image;
		switch (dimension) {
		case TextureDimension::Texture2DArray:
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D_ARRAY;
			break;
		case TextureDimension::Texture3D:
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_3D;
			break;
		case TextureDimension::Cube:
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE;
			break;
		case TextureDimension::CubeArray:
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_CUBE_ARRAY;
			break;
		case TextureDimension::Texture2D:
		case TextureDimension::Count:
			viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
			break;
		}
		viewInfo.format = imageInfo.format;
		viewInfo.subresourceRange.aspectMask = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
		viewInfo.subresourceRange.levelCount = desc.mipLevels;
		viewInfo.subresourceRange.layerCount = arrayLayers;
		VkImageView view = VK_NULL_HANDLE;
		result = impl->fd.vkCreateImageView(impl->device, &viewInfo, nullptr, &view);
		if (result != VK_SUCCESS) {
			vmaDestroyImage(impl->allocator, image, allocation);
			SLT_LOG_ERROR("Graphics", "CreateTextureResource: view creation failed: {}", VkResultName(result));
			return {};
		}

		const bool drawnIntoLevels = desc.mipLevels > 1 && HasFlag(desc.usage, TextureUsage::RenderTarget);
		const u32 viewLevels = drawnIntoLevels ? desc.mipLevels : 1u;
		Vector<VkImageView> layerViews{GetMemoryResource(MemTag::Graphics)};
		if (arrayLayers > 1 || drawnIntoLevels) {
			layerViews.reserve(static_cast<size_t>(arrayLayers) * viewLevels);
			for (u32 level = 0; level < viewLevels && result == VK_SUCCESS; ++level) {
				for (u32 layer = 0; layer < arrayLayers && result == VK_SUCCESS; ++layer) {
					VkImageViewCreateInfo layerInfo = viewInfo;
					layerInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
					layerInfo.subresourceRange.baseMipLevel = level;
					layerInfo.subresourceRange.levelCount = 1;
					layerInfo.subresourceRange.baseArrayLayer = layer;
					layerInfo.subresourceRange.layerCount = 1;
					VkImageView layerView = VK_NULL_HANDLE;
					result = impl->fd.vkCreateImageView(impl->device, &layerInfo, nullptr, &layerView);
					if (result == VK_SUCCESS) {
						layerViews.push_back(layerView);
					}
				}
			}
			if (result != VK_SUCCESS) {
				for (VkImageView layerView : layerViews) {
					impl->fd.vkDestroyImageView(impl->device, layerView, nullptr);
				}
				impl->fd.vkDestroyImageView(impl->device, view, nullptr);
				vmaDestroyImage(impl->allocator, image, allocation);
				SLT_LOG_ERROR("Graphics", "CreateTextureResource: layer view creation failed: {}", VkResultName(result));
				return {};
			}
		}

		auto [index, generation] = impl->textures.Acquire();
		TextureResource& resource = impl->textures.slots[index].resource;
		resource.layerViews.assign(layerViews.begin(), layerViews.end());
		resource.image = image;
		resource.view = view;
		resource.allocation = allocation;
		resource.size = desc.size;
		resource.format = desc.format;
		resource.usage = desc.usage;
		resource.mipLevels = desc.mipLevels;
		resource.samples = samples;
		resource.arrayLayers = arrayLayers;
		resource.depth = depth;
		resource.state = ResourceState::Undefined;

		impl->SetDebugName(reinterpret_cast<u64>(image), VK_OBJECT_TYPE_IMAGE, desc.debugName);
		return TextureHandle{index, generation};
	}

	void Device::DestroyResource(TextureHandle handle) {
		DeviceImpl* impl = _impl;
		SLT_RHI_ASSERT_MAIN_THREAD(impl);
		TextureResource* resource = impl->textures.Resolve(handle);
		if (!resource) {
			if (handle.IsValid()) {
				SLT_LOG_WARN("Graphics", "DestroyResource: stale texture handle ({}, gen {})", handle.index, handle.generation);
			}
			return;
		}
		DeletionQueue& queue = impl->DeletionQueueForDestroy();
		queue.textures.push_back(
			{resource->external ? VK_NULL_HANDLE : resource->image, resource->view, resource->allocation});
		for (VkImageView layerView : resource->layerViews) {
			queue.textures.push_back({VK_NULL_HANDLE, layerView, VK_NULL_HANDLE});
		}
		*resource = TextureResource{};
		impl->textures.Release(handle.index);
	}

	bool Device::IsAlive(TextureHandle texture) const {
		return _impl->textures.Resolve(texture) != nullptr;
	}

	Vec2I Device::GetTextureSize(TextureHandle texture) const {
		TextureResource* resource = _impl->textures.Resolve(texture);
		return resource ? resource->size : Vec2I{};
	}

	Format Device::GetTextureFormat(TextureHandle texture) const {
		TextureResource* resource = _impl->textures.Resolve(texture);
		return resource ? resource->format : Format::Unknown;
	}

	ResourceState Device::GetTextureState(TextureHandle texture) const {
		TextureResource* resource = _impl->textures.Resolve(texture);
		return resource ? resource->state : ResourceState::Undefined;
	}

	SamplerHandle Device::CreateSamplerResource(const SamplerDesc& desc) {
		DeviceImpl* impl = _impl;
		SLT_RHI_ASSERT_MAIN_THREAD(impl);

		VkFilter filter = desc.filter == FilterMode::Linear ? VK_FILTER_LINEAR : VK_FILTER_NEAREST;
		VkSamplerAddressMode address = VK_SAMPLER_ADDRESS_MODE_REPEAT;
		switch (desc.addressMode) {
		case AddressMode::Repeat:
			address = VK_SAMPLER_ADDRESS_MODE_REPEAT;
			break;
		case AddressMode::ClampToEdge:
			address = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE;
			break;
		case AddressMode::MirroredRepeat:
			address = VK_SAMPLER_ADDRESS_MODE_MIRRORED_REPEAT;
			break;
		}

		VkSamplerCreateInfo samplerInfo{VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO};
		samplerInfo.magFilter = filter;
		samplerInfo.minFilter = filter;
		samplerInfo.mipmapMode = desc.filter == FilterMode::Linear ? VK_SAMPLER_MIPMAP_MODE_LINEAR : VK_SAMPLER_MIPMAP_MODE_NEAREST;
		samplerInfo.addressModeU = address;
		samplerInfo.addressModeV = address;
		samplerInfo.addressModeW = address;
		samplerInfo.maxLod = VK_LOD_CLAMP_NONE;
		if (desc.anisotropy && impl->maxAnisotropy > 1.0f) {
			samplerInfo.anisotropyEnable = VK_TRUE;
			samplerInfo.maxAnisotropy = std::min(16.0f, impl->maxAnisotropy);
		}
		if (desc.comparison) {
			samplerInfo.compareEnable = VK_TRUE;
			samplerInfo.compareOp = VK_COMPARE_OP_GREATER_OR_EQUAL;
		}

		VkSampler sampler = VK_NULL_HANDLE;
		VkResult result = impl->fd.vkCreateSampler(impl->device, &samplerInfo, nullptr, &sampler);
		if (result != VK_SUCCESS) {
			SLT_LOG_ERROR("Graphics", "CreateSamplerResource failed: {}", VkResultName(result));
			return {};
		}

		auto [index, generation] = impl->samplers.Acquire();
		impl->samplers.slots[index].resource.sampler = sampler;
		impl->SetDebugName(reinterpret_cast<u64>(sampler), VK_OBJECT_TYPE_SAMPLER, desc.debugName);
		return SamplerHandle{index, generation};
	}

	void Device::DestroyResource(SamplerHandle handle) {
		DeviceImpl* impl = _impl;
		SLT_RHI_ASSERT_MAIN_THREAD(impl);
		SamplerResource* resource = impl->samplers.Resolve(handle);
		if (!resource) {
			if (handle.IsValid()) {
				SLT_LOG_WARN("Graphics", "DestroyResource: stale sampler handle ({}, gen {})", handle.index, handle.generation);
			}
			return;
		}
		impl->DeletionQueueForDestroy().samplers.push_back(resource->sampler);
		*resource = SamplerResource{};
		impl->samplers.Release(handle.index);
	}

	void Device::DestroyResource(PipelineHandle handle) {
		DeviceImpl* impl = _impl;
		SLT_RHI_ASSERT_MAIN_THREAD(impl);
		PipelineResource* resource = impl->pipelines.Resolve(handle);
		if (!resource) {
			if (handle.IsValid()) {
				SLT_LOG_WARN("Graphics", "DestroyResource: stale pipeline handle ({}, gen {})", handle.index, handle.generation);
			}
			return;
		}
		DeletionQueue& queue = impl->DeletionQueueForDestroy();
		queue.pipelines.push_back(resource->pipeline);
		queue.pipelineLayouts.push_back(resource->layout);
		*resource = PipelineResource{};
		impl->pipelines.Release(handle.index);
	}

	namespace {

		struct StagingBuffer {
			VkBuffer buffer = VK_NULL_HANDLE;
			VmaAllocation allocation = VK_NULL_HANDLE;
			void* mapped = nullptr;
		};

		StagingBuffer CreateStaging(DeviceImpl& impl, u64 size, bool readback) {
			StagingBuffer staging;
			VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
			bufferInfo.size = size;
			bufferInfo.usage = readback ? VK_BUFFER_USAGE_TRANSFER_DST_BIT : VK_BUFFER_USAGE_TRANSFER_SRC_BIT;
			VmaAllocationCreateInfo allocInfo{};
			allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
			allocInfo.flags = (readback ? VMA_ALLOCATION_CREATE_HOST_ACCESS_RANDOM_BIT
										: VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT) |
							  VMA_ALLOCATION_CREATE_MAPPED_BIT;
			VmaAllocationInfo resultInfo{};
			SLT_VK_CHECK(vmaCreateBuffer(impl.allocator, &bufferInfo, &allocInfo, &staging.buffer, &staging.allocation, &resultInfo));
			staging.mapped = resultInfo.pMappedData;
			return staging;
		}

	} // anonymous namespace

	VkCommandBuffer DeviceImpl::BeginImmediate() {
		SLT_VK_CHECK(fd.vkResetFences(device, 1, &immediateFence));
		SLT_VK_CHECK(fd.vkResetCommandPool(device, immediatePool, 0));

		VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		SLT_VK_CHECK(fd.vkBeginCommandBuffer(immediateBuffer, &begin));

		// queued work first (device operations must stay in order)
		RecordPendingUploads(immediateBuffer, immediateDeletions);
		return immediateBuffer;
	}

	void DeviceImpl::EndImmediate() {
		SLT_PROFILE_ZONE("RHI.ImmediateSubmit");
		SLT_VK_CHECK(fd.vkEndCommandBuffer(immediateBuffer));

		VkCommandBufferSubmitInfo cmdInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO};
		cmdInfo.commandBuffer = immediateBuffer;
		VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
		submit.commandBufferInfoCount = 1;
		submit.pCommandBufferInfos = &cmdInfo;
		SLT_VK_CHECK(fd.vkQueueSubmit2(queue, 1, &submit, immediateFence));
		SLT_VK_CHECK(fd.vkWaitForFences(device, 1, &immediateFence, VK_TRUE, UINT64_MAX));
		FlushDeletionQueue(immediateDeletions);
	}

	void Device::UploadBuffer(BufferHandle target, std::span<const u8> data, u64 offset) {
		DeviceImpl* impl = _impl;
		SLT_RHI_ASSERT_MAIN_THREAD(impl);
		BufferResource* resource = impl->buffers.Resolve(target);
		if (!resource || data.empty()) {
			SLT_LOG_WARN("Graphics", "UploadBuffer: invalid handle or empty data");
			return;
		}
		if (offset + data.size() > resource->size) {
			SLT_LOG_ERROR("Graphics", "UploadBuffer: range {}..{} exceeds buffer size {}", offset, offset + data.size(), resource->size);
			return;
		}

		if (resource->mapped) {
			std::memcpy(static_cast<u8*>(resource->mapped) + offset, data.data(), data.size());
			return;
		}

		StagingBuffer staging = CreateStaging(*impl, data.size(), false);
		std::memcpy(staging.mapped, data.data(), data.size());

		impl->pendingUploads.push_back({staging.buffer, staging.allocation, resource->buffer, offset, data.size()});
	}

	void DeviceImpl::RecordPendingUploads(VkCommandBuffer cmd, DeletionQueue& staging) {
		if (!pendingUploads.empty()) {
			RecordBufferUploads(cmd, staging);
		}
		if (!pendingTextureUploads.empty()) {
			RecordTextureUploads(cmd, staging);
		}
	}

	void DeviceImpl::RecordBufferUploads(VkCommandBuffer cmd, DeletionQueue& staging) {
		for (const PendingBufferUpload& upload : pendingUploads) {
			VkBufferCopy2 region{VK_STRUCTURE_TYPE_BUFFER_COPY_2};
			region.srcOffset = 0;
			region.dstOffset = upload.targetOffset;
			region.size = upload.size;
			VkCopyBufferInfo2 copy{VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2};
			copy.srcBuffer = upload.staging;
			copy.dstBuffer = upload.target;
			copy.regionCount = 1;
			copy.pRegions = &region;
			fd.vkCmdCopyBuffer2(cmd, &copy);
		}

		VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
		barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
		barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
		barrier.dstStageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
		barrier.dstAccessMask = VK_ACCESS_2_MEMORY_READ_BIT;
		VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
		dependency.memoryBarrierCount = 1;
		dependency.pMemoryBarriers = &barrier;
		fd.vkCmdPipelineBarrier2(cmd, &dependency);

		for (const PendingBufferUpload& upload : pendingUploads) {
			staging.buffers.push_back({upload.staging, upload.stagingAllocation});
		}
		pendingUploads.clear();
	}

	void DeviceImpl::RecordTextureUploads(VkCommandBuffer cmd, DeletionQueue& staging) {
		const size_t count = pendingTextureUploads.size();
		for (size_t first = 0; first < count; ++first) {
			if (pendingTextureUploads[first].recorded) {
				continue;
			}
			const TextureHandle target = pendingTextureUploads[first].target;
			TextureResource* resource = textures.Resolve(target);
			const bool isDepth = resource && IsDepthFormat(resource->format);
			if (resource) {
				CmdTransitionImage(*this, cmd, resource->image, pendingTextureUploads[first].from, ResourceState::TransferDst, isDepth);
			}
			size_t sinceBarrier = first;
			for (size_t k = first; k < count; ++k) {
				PendingTextureUpload& upload = pendingTextureUploads[k];
				if (upload.recorded || !(upload.target == target)) {
					continue;
				}
				upload.recorded = true;
				staging.buffers.push_back({upload.staging, upload.stagingAllocation});
				if (!resource) {
					continue;
				}
				bool overlaps = false;
				for (size_t j = sinceBarrier; j < k && !overlaps; ++j) {
					const PendingTextureUpload& earlier = pendingTextureUploads[j];
					overlaps = earlier.target == target && earlier.mipLevel == upload.mipLevel && earlier.region.Intersects(upload.region);
				}
				if (overlaps) {
					VkMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_MEMORY_BARRIER_2};
					barrier.srcStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
					barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
					barrier.dstStageMask = VK_PIPELINE_STAGE_2_COPY_BIT;
					barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
					VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
					dependency.memoryBarrierCount = 1;
					dependency.pMemoryBarriers = &barrier;
					fd.vkCmdPipelineBarrier2(cmd, &dependency);
					sinceBarrier = k;
				}

				VkBufferImageCopy2 copyRegion{VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2};
				copyRegion.imageSubresource.aspectMask = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
				copyRegion.imageSubresource.mipLevel = upload.mipLevel;
				copyRegion.imageSubresource.layerCount = 1;
				copyRegion.imageOffset = {upload.region.position.x, upload.region.position.y, 0};
				copyRegion.imageExtent = {static_cast<u32>(upload.region.size.x), static_cast<u32>(upload.region.size.y), 1};
				VkCopyBufferToImageInfo2 copy{VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2};
				copy.srcBuffer = upload.staging;
				copy.dstImage = resource->image;
				copy.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				copy.regionCount = 1;
				copy.pRegions = &copyRegion;
				fd.vkCmdCopyBufferToImage2(cmd, &copy);
			}
			if (resource) {
				CmdTransitionImage(*this, cmd, resource->image, ResourceState::TransferDst, ResourceState::ShaderResource, isDepth);
			}
		}
		pendingTextureUploads.clear();
	}

	void DeviceImpl::FlushPendingUploads() {
		if (!HasPendingUploads()) {
			return;
		}
		ImmediateSubmit([](VkCommandBuffer) {});
	}

	void DeviceImpl::DiscardPendingUploads() {
		for (const PendingBufferUpload& upload : pendingUploads) {
			vmaDestroyBuffer(allocator, upload.staging, upload.stagingAllocation);
		}
		pendingUploads.clear();
		for (const PendingTextureUpload& upload : pendingTextureUploads) {
			vmaDestroyBuffer(allocator, upload.staging, upload.stagingAllocation);
		}
		pendingTextureUploads.clear();
	}

	void Device::UploadTexture(TextureHandle target, std::span<const u8> data, u32 mipLevel, u32 layer) {
		DeviceImpl* impl = _impl;
		SLT_RHI_ASSERT_MAIN_THREAD(impl);
		TextureResource* resource = impl->textures.Resolve(target);
		if (!resource || data.empty()) {
			SLT_LOG_WARN("Graphics", "UploadTexture: invalid handle or empty data");
			return;
		}
		if (mipLevel >= resource->mipLevels) {
			SLT_LOG_ERROR("Graphics", "UploadTexture: mip {} out of range ({})", mipLevel, resource->mipLevels);
			return;
		}
		if (layer >= resource->arrayLayers) {
			SLT_LOG_ERROR("Graphics", "UploadTexture: layer {} out of range ({})", layer, resource->arrayLayers);
			return;
		}

		u32 mipWidth = std::max(1, resource->size.x >> mipLevel);
		u32 mipHeight = std::max(1, resource->size.y >> mipLevel);
		u32 mipDepth = resource->depth;
		u64 expected = FormatLevelBytes(resource->format, Vec2I(static_cast<i32>(mipWidth), static_cast<i32>(mipHeight))) * mipDepth;
		if (data.size() != expected) {
			SLT_LOG_ERROR("Graphics", "UploadTexture: {} bytes provided, mip {} expects {}", data.size(), mipLevel, expected);
			return;
		}

		StagingBuffer staging = CreateStaging(*impl, data.size(), false);
		std::memcpy(staging.mapped, data.data(), data.size());

		bool isDepth = IsDepthFormat(resource->format);
		ResourceState previous = resource->state;
		impl->ImmediateSubmit([&](VkCommandBuffer cmd) {
			CmdTransitionImage(*impl, cmd, resource->image, previous, ResourceState::TransferDst, isDepth);

			VkBufferImageCopy2 region{VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2};
			region.imageSubresource.aspectMask = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.mipLevel = mipLevel;
			region.imageSubresource.baseArrayLayer = layer;
			region.imageSubresource.layerCount = 1;
			region.imageExtent = {mipWidth, mipHeight, mipDepth};
			VkCopyBufferToImageInfo2 copy{VK_STRUCTURE_TYPE_COPY_BUFFER_TO_IMAGE_INFO_2};
			copy.srcBuffer = staging.buffer;
			copy.dstImage = resource->image;
			copy.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			copy.regionCount = 1;
			copy.pRegions = &region;
			impl->fd.vkCmdCopyBufferToImage2(cmd, &copy);

			CmdTransitionImage(*impl, cmd, resource->image, ResourceState::TransferDst, ResourceState::ShaderResource, isDepth);
		});
		resource->state = ResourceState::ShaderResource;

		vmaDestroyBuffer(impl->allocator, staging.buffer, staging.allocation);
	}

	void Device::UploadTextureRegion(TextureHandle target, std::span<const u8> data, RectI region, u32 mipLevel) {
		DeviceImpl* impl = _impl;
		SLT_RHI_ASSERT_MAIN_THREAD(impl);
		TextureResource* resource = impl->textures.Resolve(target);
		if (!resource || data.empty()) {
			SLT_LOG_WARN("Graphics", "UploadTextureRegion: invalid handle or empty data");
			return;
		}
		if (mipLevel >= resource->mipLevels) {
			SLT_LOG_ERROR("Graphics", "UploadTextureRegion: mip {} out of range ({})", mipLevel, resource->mipLevels);
			return;
		}
		if (resource->depth > 1) {
			SLT_LOG_ERROR("Graphics", "UploadTextureRegion: the target is a volume (depth {}, a rect can't address it)", resource->depth);
			return;
		}
		if (IsBlockCompressed(resource->format)) {
			SLT_LOG_ERROR("Graphics", "UploadTextureRegion: the target is block-compressed (upload a whole level at a time)");
			return;
		}

		i32 mipWidth = std::max(1, resource->size.x >> mipLevel);
		i32 mipHeight = std::max(1, resource->size.y >> mipLevel);
		if (region.size.x <= 0 || region.size.y <= 0 ||
			region.position.x < 0 || region.position.y < 0 ||
			region.position.x + region.size.x > mipWidth || region.position.y + region.size.y > mipHeight) {
			SLT_LOG_ERROR("Graphics", "UploadTextureRegion: region {}x{}+{}+{} exceeds mip {} ({}x{})",
						  region.size.x, region.size.y, region.position.x, region.position.y, mipLevel, mipWidth, mipHeight);
			return;
		}
		u64 expected = FormatLevelBytes(resource->format, region.size);
		if (data.size() != expected) {
			SLT_LOG_ERROR("Graphics", "UploadTextureRegion: {} bytes provided, region expects {}", data.size(), expected);
			return;
		}

		StagingBuffer staging = CreateStaging(*impl, data.size(), false);
		std::memcpy(staging.mapped, data.data(), data.size());

		impl->pendingTextureUploads.push_back({staging.buffer, staging.allocation, target, resource->state, region, mipLevel});
		resource->state = ResourceState::ShaderResource;
	}

	void CmdGenerateMips(DeviceImpl& d, VkCommandBuffer cmd, TextureResource& texture) {
		DeviceImpl* impl = &d;
		TextureResource* resource = &texture;
		const ResourceState previous = resource->state;
		{
			CmdTransitionImage(*impl, cmd, resource->image, previous, ResourceState::TransferDst, false);

			i32 width = resource->size.x;
			i32 height = resource->size.y;
			for (u32 level = 1; level < resource->mipLevels; ++level) {
				VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
				barrier.srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
				barrier.srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
				barrier.dstStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
				barrier.dstAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
				barrier.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				barrier.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				barrier.image = resource->image;
				barrier.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				barrier.subresourceRange.baseMipLevel = level - 1;
				barrier.subresourceRange.levelCount = 1;
				barrier.subresourceRange.layerCount = resource->arrayLayers;
				VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
				dependency.imageMemoryBarrierCount = 1;
				dependency.pImageMemoryBarriers = &barrier;
				impl->fd.vkCmdPipelineBarrier2(cmd, &dependency);

				i32 nextWidth = std::max(1, width / 2);
				i32 nextHeight = std::max(1, height / 2);

				VkImageBlit2 blit{VK_STRUCTURE_TYPE_IMAGE_BLIT_2};
				blit.srcSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				blit.srcSubresource.mipLevel = level - 1;
				blit.srcSubresource.layerCount = resource->arrayLayers;
				blit.srcOffsets[1] = {width, height, 1};
				blit.dstSubresource.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				blit.dstSubresource.mipLevel = level;
				blit.dstSubresource.layerCount = resource->arrayLayers;
				blit.dstOffsets[1] = {nextWidth, nextHeight, 1};

				VkBlitImageInfo2 blitInfo{VK_STRUCTURE_TYPE_BLIT_IMAGE_INFO_2};
				blitInfo.srcImage = resource->image;
				blitInfo.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
				blitInfo.dstImage = resource->image;
				blitInfo.dstImageLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
				blitInfo.filter = VK_FILTER_LINEAR;
				blitInfo.regionCount = 1;
				blitInfo.pRegions = &blit;
				impl->fd.vkCmdBlitImage2(cmd, &blitInfo);

				width = nextWidth;
				height = nextHeight;
			}

			VkImageMemoryBarrier2 barriers[2]{};
			barriers[0].sType = VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2;
			barriers[0].srcStageMask = VK_PIPELINE_STAGE_2_TRANSFER_BIT;
			barriers[0].srcAccessMask = VK_ACCESS_2_TRANSFER_READ_BIT;
			barriers[0].dstStageMask = VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT;
			barriers[0].dstAccessMask = VK_ACCESS_2_SHADER_SAMPLED_READ_BIT;
			barriers[0].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			barriers[0].newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
			barriers[0].image = resource->image;
			barriers[0].subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
			barriers[0].subresourceRange.levelCount = resource->mipLevels - 1;
			barriers[0].subresourceRange.layerCount = resource->arrayLayers;
			barriers[1] = barriers[0];
			barriers[1].srcAccessMask = VK_ACCESS_2_TRANSFER_WRITE_BIT;
			barriers[1].oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL;
			barriers[1].subresourceRange.baseMipLevel = resource->mipLevels - 1;
			barriers[1].subresourceRange.levelCount = 1;
			VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
			dependency.imageMemoryBarrierCount = 2;
			dependency.pImageMemoryBarriers = barriers;
			impl->fd.vkCmdPipelineBarrier2(cmd, &dependency);
		}
		resource->state = ResourceState::ShaderResource;
	}

	void Device::GenerateMips(TextureHandle target) {
		DeviceImpl* impl = _impl;
		SLT_RHI_ASSERT_MAIN_THREAD(impl);
		TextureResource* resource = impl->textures.Resolve(target);
		if (!resource) {
			SLT_LOG_WARN("Graphics", "GenerateMips: invalid handle");
			return;
		}
		if (resource->mipLevels <= 1) {
			return;
		}
		SLT_CORE_ASSERT_MSG(!IsDepthFormat(resource->format), "GenerateMips on a depth texture");
		if (IsBlockCompressed(resource->format)) {
			SLT_LOG_ERROR("Graphics", "GenerateMips: a block-compressed texture has its chain");
			return;
		}
		impl->ImmediateSubmit([&](VkCommandBuffer cmd) { CmdGenerateMips(*impl, cmd, *resource); });
	}

	void Device::ReadbackTexture(TextureHandle source, std::span<u8> out, u32 mipLevel, u32 layer) {
		DeviceImpl* impl = _impl;
		SLT_RHI_ASSERT_MAIN_THREAD(impl);
		TextureResource* resource = impl->textures.Resolve(source);
		if (!resource) {
			SLT_LOG_WARN("Graphics", "ReadbackTexture: invalid handle");
			return;
		}
		if (layer >= resource->arrayLayers) {
			SLT_LOG_ERROR("Graphics", "ReadbackTexture: layer {} out of range ({})", layer, resource->arrayLayers);
			return;
		}

		u32 mipWidth = std::max(1, resource->size.x >> mipLevel);
		u32 mipHeight = std::max(1, resource->size.y >> mipLevel);
		u32 mipDepth = resource->depth;
		u64 expected = FormatLevelBytes(resource->format, Vec2I(static_cast<i32>(mipWidth), static_cast<i32>(mipHeight))) * mipDepth;
		if (out.size() < expected) {
			SLT_LOG_ERROR("Graphics", "ReadbackTexture: {} bytes provided, need {}", out.size(), expected);
			return;
		}

		StagingBuffer staging = CreateStaging(*impl, expected, true);

		bool isDepth = IsDepthFormat(resource->format);
		ResourceState previous = resource->state;
		impl->ImmediateSubmit([&](VkCommandBuffer cmd) {
			CmdTransitionImage(*impl, cmd, resource->image, previous, ResourceState::TransferSrc, isDepth);

			VkBufferImageCopy2 region{VK_STRUCTURE_TYPE_BUFFER_IMAGE_COPY_2};
			region.imageSubresource.aspectMask = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
			region.imageSubresource.mipLevel = mipLevel;
			region.imageSubresource.baseArrayLayer = layer;
			region.imageSubresource.layerCount = 1;
			region.imageExtent = {mipWidth, mipHeight, mipDepth};
			VkCopyImageToBufferInfo2 copy{VK_STRUCTURE_TYPE_COPY_IMAGE_TO_BUFFER_INFO_2};
			copy.srcImage = resource->image;
			copy.srcImageLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL;
			copy.dstBuffer = staging.buffer;
			copy.regionCount = 1;
			copy.pRegions = &region;
			impl->fd.vkCmdCopyImageToBuffer2(cmd, &copy);

			if (HasFlag(resource->usage, TextureUsage::Sampled)) {
				CmdTransitionImage(*impl, cmd, resource->image, ResourceState::TransferSrc, ResourceState::ShaderResource, isDepth);
			}
		});
		resource->state = HasFlag(resource->usage, TextureUsage::Sampled) ? ResourceState::ShaderResource : ResourceState::TransferSrc;

		vmaInvalidateAllocation(impl->allocator, staging.allocation, 0, VK_WHOLE_SIZE);
		std::memcpy(out.data(), staging.mapped, expected);
		vmaDestroyBuffer(impl->allocator, staging.buffer, staging.allocation);
	}

	void Device::ReadbackBuffer(BufferHandle source, std::span<u8> out, u64 offset) {
		DeviceImpl* impl = _impl;
		SLT_RHI_ASSERT_MAIN_THREAD(impl);
		BufferResource* resource = impl->buffers.Resolve(source);
		if (!resource) {
			SLT_LOG_WARN("Graphics", "ReadbackBuffer: invalid handle");
			return;
		}
		if (out.empty()) {
			return;
		}
		if (offset + out.size() > resource->size) {
			SLT_LOG_ERROR("Graphics", "ReadbackBuffer: {} bytes at {} run past a {}-byte buffer", out.size(), offset, resource->size);
			return;
		}
		if (!HasFlag(resource->usage, BufferUsage::TransferSrc)) {
			SLT_LOG_ERROR("Graphics", "ReadbackBuffer: the buffer wasn't created with TransferSrc usage");
			return;
		}

		StagingBuffer staging = CreateStaging(*impl, out.size(), true);
		ResourceState previous = resource->state;
		impl->ImmediateSubmit([&](VkCommandBuffer cmd) {
			StateMasks src = ToStateMasks(previous, false);
			StateMasks dst = ToStateMasks(ResourceState::TransferSrc, false);
			VkBufferMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER_2};
			barrier.srcStageMask = src.stages;
			barrier.srcAccessMask = src.access;
			barrier.dstStageMask = dst.stages;
			barrier.dstAccessMask = dst.access;
			barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
			barrier.buffer = resource->buffer;
			barrier.size = VK_WHOLE_SIZE;
			VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
			dependency.bufferMemoryBarrierCount = 1;
			dependency.pBufferMemoryBarriers = &barrier;
			impl->fd.vkCmdPipelineBarrier2(cmd, &dependency);

			VkBufferCopy2 region{VK_STRUCTURE_TYPE_BUFFER_COPY_2};
			region.srcOffset = offset;
			region.dstOffset = 0;
			region.size = out.size();
			VkCopyBufferInfo2 copy{VK_STRUCTURE_TYPE_COPY_BUFFER_INFO_2};
			copy.srcBuffer = resource->buffer;
			copy.dstBuffer = staging.buffer;
			copy.regionCount = 1;
			copy.pRegions = &region;
			impl->fd.vkCmdCopyBuffer2(cmd, &copy);
		});
		resource->state = ResourceState::TransferSrc;

		vmaInvalidateAllocation(impl->allocator, staging.allocation, 0, VK_WHOLE_SIZE);
		std::memcpy(out.data(), staging.mapped, out.size());
		vmaDestroyBuffer(impl->allocator, staging.buffer, staging.allocation);
	}

} // namespace Shalltear::RHI

#endif
