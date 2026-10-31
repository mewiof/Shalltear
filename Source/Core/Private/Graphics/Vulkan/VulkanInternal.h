#pragma once

#include "Graphics/Vulkan/VulkanLoader.h"

#include "Shalltear/Core/Assert.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/RHICommandList.h"
#include "Shalltear/Core/Graphics/RHIDevice.h"
#include "Shalltear/Core/Graphics/RHISwapChain.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Log.h"

// must match VmaUsage.cpp
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1
#include <vk_mem_alloc.h>

#include <thread>

namespace Shalltear::RHI {

	template <typename TResource>
	struct ResourcePool {
		struct Slot {
			TResource resource{};
			u32 generation = 1;
			bool alive = false;
		};

		Vector<Slot> slots{GetMemoryResource(MemTag::Graphics)};
		Vector<u32> freeIndices{GetMemoryResource(MemTag::Graphics)};
		u32 liveCount = 0;

		ResourcePool() {
			slots.emplace_back();
		}

		std::pair<u32, u32> Acquire() {
			u32 index;
			if (!freeIndices.empty()) {
				index = freeIndices.back();
				freeIndices.pop_back();
			} else {
				index = static_cast<u32>(slots.size());
				slots.emplace_back();
			}
			Slot& slot = slots[index];
			slot.resource = TResource{};
			slot.alive = true;
			++liveCount;
			return {index, slot.generation};
		}

		void Release(u32 index) {
			Slot& slot = slots[index];
			SLT_CORE_ASSERT(slot.alive);
			slot.alive = false;
			++slot.generation;
			--liveCount;
			freeIndices.push_back(index);
		}

		template <typename THandle>
		TResource* Resolve(THandle handle) {
			if (handle.index == 0 || handle.index >= slots.size()) {
				return nullptr;
			}
			Slot& slot = slots[handle.index];
			if (!slot.alive || slot.generation != handle.generation) {
				return nullptr;
			}
			return &slot.resource;
		}
	};

	struct BufferResource {
		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		u64 size = 0;
		BufferUsage usage = BufferUsage::None;
		MemoryAccess access = MemoryAccess::GPUOnly;
		void* mapped = nullptr;
		u64 deviceAddress = 0;
		ResourceState state = ResourceState::Undefined;
	};

	struct TextureResource {
		VkImage image = VK_NULL_HANDLE;
		VkImageView view = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		// 1 2D view per slice/mip, at mip * arrayLayers + layer
		Vector<VkImageView> layerViews{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		Format format = Format::Unknown;
		TextureUsage usage = TextureUsage::None;
		u32 mipLevels = 1;
		u32 samples = 1;
		u32 arrayLayers = 1;
		u32 depth = 1;
		bool external = false;
		ResourceState state = ResourceState::Undefined;

		VkImageView AttachmentView(u32 layer, u32 mip = 0) const {
			const size_t index = static_cast<size_t>(mip) * arrayLayers + layer;
			return index < layerViews.size() ? layerViews[index] : view;
		}
	};

	struct SamplerResource {
		VkSampler sampler = VK_NULL_HANDLE;
	};

	struct PipelineResource {
		VkPipeline pipeline = VK_NULL_HANDLE;
		VkPipelineLayout layout = VK_NULL_HANDLE;
		VkPipelineBindPoint bindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS;
		VkShaderStageFlags pushStages = VK_SHADER_STAGE_VERTEX_BIT | VK_SHADER_STAGE_FRAGMENT_BIT;
		VkDescriptorSetLayout setLayouts[MAX_DESCRIPTOR_SETS] = {};
		u32 setCount = 0;
		Vector<DescriptorBindingDesc> bindings{GetMemoryResource(MemTag::Graphics)};
		u32 pushConstantSize = 0;
		Format colorFormats[MAX_COLOR_ATTACHMENTS] = {};
		u32 colorFormatCount = 0;
		Format depthFormat = Format::Unknown;
		u32 samples = 1;
	};

	struct DeletionQueue {
		struct DeferredBuffer {
			VkBuffer buffer;
			VmaAllocation allocation;
		};
		struct DeferredTexture {
			VkImage image;
			VkImageView view;
			VmaAllocation allocation;
		};
		Vector<DeferredBuffer> buffers{GetMemoryResource(MemTag::Graphics)};
		Vector<DeferredTexture> textures{GetMemoryResource(MemTag::Graphics)};
		Vector<VkSampler> samplers{GetMemoryResource(MemTag::Graphics)};
		Vector<VkPipeline> pipelines{GetMemoryResource(MemTag::Graphics)};
		Vector<VkPipelineLayout> pipelineLayouts{GetMemoryResource(MemTag::Graphics)};

		bool IsEmpty() const {
			return buffers.empty() && textures.empty() && samplers.empty() && pipelines.empty() && pipelineLayouts.empty();
		}
	};

	struct ResolvedWrite {
		u64 object = 0;
		u64 offset = 0;
		u64 range = 0;
		u32 binding = 0;
		DescriptorType type = DescriptorType::UniformBuffer;
		VkImageLayout imageLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	};

	struct DescriptorAllocatorGrowable {
		Vector<VkDescriptorPool> readyPools{GetMemoryResource(MemTag::Graphics)};
		Vector<VkDescriptorPool> fullPools{GetMemoryResource(MemTag::Graphics)};
		u32 setsPerPool = 128;

		static constexpr u32 CACHE_SLOTS = 256;
		static constexpr u32 CACHE_MAX_PROBES = 8;
		static constexpr u32 CACHE_EMPTY = 0xFFFFFFFFu;

		struct CachedSet {
			u64 hash = 0;
			VkDescriptorSetLayout layout = VK_NULL_HANDLE;
			VkDescriptorSet set = VK_NULL_HANDLE;
			u32 firstWrite = 0;
			u32 writeCount = 0;
		};
		u32 cacheSlots[CACHE_SLOTS];
		Vector<CachedSet> cachedSets{GetMemoryResource(MemTag::Graphics)};
		Vector<ResolvedWrite> cachedWrites{GetMemoryResource(MemTag::Graphics)};

		DescriptorAllocatorGrowable() {
			ClearContentCache();
		}
		void ClearContentCache() {
			for (u32& slot : cacheSlots) {
				slot = CACHE_EMPTY;
			}
			cachedSets.clear();
			cachedWrites.clear();
		}
	};

	struct UploadRingBlock {
		VkBuffer buffer = VK_NULL_HANDLE;
		VmaAllocation allocation = VK_NULL_HANDLE;
		BufferHandle handle;
		u8* mapped = nullptr;
		u64 capacity = 0;
		u64 used = 0;
	};

	struct CommandListImpl {
		DeviceImpl* device = nullptr;
		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		u32 boundPipelineIndex = 0;
		bool inRenderPass = false;
		RectI passArea{};
		Format passColorFormats[MAX_COLOR_ATTACHMENTS] = {};
		u32 passColorCount = 0;
		Format passDepthFormat = Format::Unknown;
		u32 passSamples = 1;
	};

	struct FrameContext {
		VkCommandPool commandPool = VK_NULL_HANDLE;
		VkCommandBuffer commandBuffer = VK_NULL_HANDLE;
		VkCommandBuffer uploadCommandBuffer = VK_NULL_HANDLE;
		CommandList commandList;
		CommandListImpl commandListImpl;
		DescriptorAllocatorGrowable descriptors;
		DeletionQueue deletionQueue;
		Vector<UploadRingBlock> uploadRing{GetMemoryResource(MemTag::Graphics)};
		u32 uploadRingActiveBlock = 0;
		u64 timelineValue = 0;

		Vector<VkSemaphore> waitSemaphores{GetMemoryResource(MemTag::Graphics)};
		Vector<VkSemaphore> signalSemaphores{GetMemoryResource(MemTag::Graphics)};
	};

	struct SwapChainImpl {
		DeviceImpl* device = nullptr;
		VkSurfaceKHR surface = VK_NULL_HANDLE;
		VkSwapchainKHR swapchain = VK_NULL_HANDLE;
		Format format = Format::Unknown;
		VkFormat vkFormat = VK_FORMAT_UNDEFINED;
		Vec2I size{};
		bool vsync = true;

		Vector<TextureHandle> imageHandles{GetMemoryResource(MemTag::Graphics)};
		Vector<VkSemaphore> renderFinished{GetMemoryResource(MemTag::Graphics)};
		VkSemaphore acquireSemaphores[FRAMES_IN_FLIGHT] = {};

		u32 currentImageIndex = 0;
		bool acquiredThisFrame = false;
		bool needsRecreate = false;
	};

	struct DeviceImpl {
		VulkanInstanceFunctions fi{};
		VulkanDeviceFunctions fd{};

		VkInstance instance = VK_NULL_HANDLE;
		VkDebugUtilsMessengerEXT debugMessenger = VK_NULL_HANDLE;
		VkPhysicalDevice physicalDevice = VK_NULL_HANDLE;
		VkDevice device = VK_NULL_HANDLE;
		VkQueue queue = VK_NULL_HANDLE;
		u32 queueFamily = 0;
		VmaAllocator allocator = VK_NULL_HANDLE;

		Vector<AdapterInfo> adapters{GetMemoryResource(MemTag::Graphics)};
		i32 selectedAdapter = -1;
		bool validation = false;
		bool headless = false;
		f32 maxAnisotropy = 1.0f;
		bool fillModeNonSolid = false;
		bool warnedNoFillModeNonSolid = false;
		bool imageCubeArray = false;
		bool textureCompressionBC = false;
		u32 framebufferColorSamples = 1;
		u32 framebufferDepthSamples = 1;

		ResourcePool<BufferResource> buffers;
		ResourcePool<TextureResource> textures;
		ResourcePool<SamplerResource> samplers;
		ResourcePool<PipelineResource> pipelines;

		HashMap<u64, VkDescriptorSetLayout> setLayoutCache{GetMemoryResource(MemTag::Graphics)};

		VkPipelineCache pipelineCache = VK_NULL_HANDLE;

		VkSemaphore timeline = VK_NULL_HANDLE;
		u64 timelineCounter = 0;
		FrameContext frames[FRAMES_IN_FLIGHT];
		u32 frameIndex = 0;
		bool inFrame = false;

		VkCommandPool immediatePool = VK_NULL_HANDLE;
		VkCommandBuffer immediateBuffer = VK_NULL_HANDLE;
		VkFence immediateFence = VK_NULL_HANDLE;

		struct PendingBufferUpload {
			VkBuffer staging = VK_NULL_HANDLE;
			VmaAllocation stagingAllocation = VK_NULL_HANDLE;
			VkBuffer target = VK_NULL_HANDLE;
			u64 targetOffset = 0;
			u64 size = 0;
		};
		Vector<PendingBufferUpload> pendingUploads{GetMemoryResource(MemTag::Graphics)};
		struct PendingTextureUpload {
			VkBuffer staging = VK_NULL_HANDLE;
			VmaAllocation stagingAllocation = VK_NULL_HANDLE;
			TextureHandle target;
			ResourceState from = ResourceState::Undefined;
			RectI region{};
			u32 mipLevel = 0;
			bool recorded = false;
		};
		Vector<PendingTextureUpload> pendingTextureUploads{GetMemoryResource(MemTag::Graphics)};
		DeletionQueue immediateDeletions;

		Vector<SwapChain*> swapChains{GetMemoryResource(MemTag::Graphics)};

		std::thread::id mainThread;

		void SetDebugName(u64 objectHandle, u32 objectType, const char* name);
		void DestroyDeferred(BufferResource& resource);
		void DestroyDeferred(TextureResource& resource);
		void FlushDeletionQueue(DeletionQueue& queue);
		DeletionQueue& DeletionQueueForDestroy();
		VkDescriptorSetLayout GetOrCreateSetLayout(std::span<const DescriptorBindingDesc> setBindings);
		VkDescriptorSet AllocateTransientDescriptorSet(VkDescriptorSetLayout layout);
		VkDescriptorSet FindTransientSetWithContent(VkDescriptorSetLayout layout, u64 hash, const ResolvedWrite* writes, u32 writeCount);
		void RememberTransientSetContent(VkDescriptorSetLayout layout, u64 hash, const ResolvedWrite* writes, u32 writeCount, VkDescriptorSet set);
		void ResetFrameDescriptors(FrameContext& frame);
		void DestroyFrameDescriptors(FrameContext& frame);
		UploadRingBlock& EnsureUploadSpace(u64 size, u64 alignment);
		bool HasPendingUploads() const { return !pendingUploads.empty() || !pendingTextureUploads.empty(); }
		void RecordPendingUploads(VkCommandBuffer cmd, DeletionQueue& staging);
		void RecordBufferUploads(VkCommandBuffer cmd, DeletionQueue& staging);
		void RecordTextureUploads(VkCommandBuffer cmd, DeletionQueue& staging);
		void FlushPendingUploads();
		void DiscardPendingUploads();

		template <typename TRecord>
		void ImmediateSubmit(TRecord&& record);
		VkCommandBuffer BeginImmediate();
		void EndImmediate();
	};

#define SLT_RHI_ASSERT_MAIN_THREAD(impl) \
	SLT_CORE_ASSERT_MSG(std::this_thread::get_id() == (impl)->mainThread, "RHI calls are main-thread-only in v1")

	VkFormat ToVkFormat(Format format);
	bool IsDepthFormat(Format format);
	VkImageUsageFlags ToVkImageUsage(TextureUsage usage);
	VkSampleCountFlagBits ToVkSampleCount(u32 samples);

	struct StateMasks {
		VkPipelineStageFlags2 stages;
		VkAccessFlags2 access;
		VkImageLayout layout;
	};
	StateMasks ToStateMasks(ResourceState state, bool isDepth);

	void CmdTransitionImage(DeviceImpl& d, VkCommandBuffer cmd, VkImage image, ResourceState from, ResourceState to, bool isDepth);

	void CmdGenerateMips(DeviceImpl& d, VkCommandBuffer cmd, TextureResource& resource);

	const char* VkResultName(VkResult result);

#define SLT_VK_CHECK(expr)                                                                 \
	do {                                                                                   \
		VkResult _sltVkResult = (expr);                                                    \
		if (_sltVkResult != VK_SUCCESS) {                                                  \
			SLT_LOG_ERROR("Graphics", "{} failed: {}", #expr, VkResultName(_sltVkResult)); \
			SLT_CORE_ASSERT_MSG(false, "Vulkan call failed: {}", #expr);                   \
		}                                                                                  \
	} while (0)

	template <typename TRecord>
	void DeviceImpl::ImmediateSubmit(TRecord&& record) {
		record(BeginImmediate());
		EndImmediate();
	}

} // namespace Shalltear::RHI
