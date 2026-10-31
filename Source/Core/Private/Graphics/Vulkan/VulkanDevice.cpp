#include "Shalltear/Core/Platform.h"

#if defined(SLT_PLATFORM_WINDOWS)

#include "Graphics/Vulkan/VulkanInternal.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <vulkan/vulkan_win32.h>

#include "Shalltear/Core/Memory.h"

#include <algorithm>
#include <cstring>
#include <span>
#include <thread>

namespace Shalltear::RHI {

	VkFormat ToVkFormat(Format format) {
		switch (format) {
		case Format::R8Unorm:
			return VK_FORMAT_R8_UNORM;
		case Format::RGBA8Unorm:
			return VK_FORMAT_R8G8B8A8_UNORM;
		case Format::RGBA8SRGB:
			return VK_FORMAT_R8G8B8A8_SRGB;
		case Format::BGRA8Unorm:
			return VK_FORMAT_B8G8R8A8_UNORM;
		case Format::BGRA8SRGB:
			return VK_FORMAT_B8G8R8A8_SRGB;
		case Format::RGBA16Float:
			return VK_FORMAT_R16G16B16A16_SFLOAT;
		case Format::RG16Float:
			return VK_FORMAT_R16G16_SFLOAT;
		case Format::R32Uint:
			return VK_FORMAT_R32_UINT;
		case Format::D32Float:
			return VK_FORMAT_D32_SFLOAT;
		case Format::BC5Unorm:
			return VK_FORMAT_BC5_UNORM_BLOCK;
		case Format::BC7Unorm:
			return VK_FORMAT_BC7_UNORM_BLOCK;
		case Format::BC7SRGB:
			return VK_FORMAT_BC7_SRGB_BLOCK;
		case Format::BC6HUFloat:
			return VK_FORMAT_BC6H_UFLOAT_BLOCK;
		default:
			return VK_FORMAT_UNDEFINED;
		}
	}

	bool IsDepthFormat(Format format) {
		return format == Format::D32Float;
	}

	StateMasks ToStateMasks(ResourceState state, bool isDepth) {
		switch (state) {
		case ResourceState::Undefined:
			return {VK_PIPELINE_STAGE_2_NONE, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_UNDEFINED};
		case ResourceState::RenderTarget:
			return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT,
					VK_ACCESS_2_COLOR_ATTACHMENT_READ_BIT | VK_ACCESS_2_COLOR_ATTACHMENT_WRITE_BIT,
					VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL};
		case ResourceState::DepthWrite:
			return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT,
					VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_WRITE_BIT,
					VK_IMAGE_LAYOUT_DEPTH_ATTACHMENT_OPTIMAL};
		case ResourceState::DepthRead:
			return {VK_PIPELINE_STAGE_2_EARLY_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_LATE_FRAGMENT_TESTS_BIT | VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT,
					VK_ACCESS_2_DEPTH_STENCIL_ATTACHMENT_READ_BIT | VK_ACCESS_2_SHADER_SAMPLED_READ_BIT,
					VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL};
		case ResourceState::ShaderResource:
			return {VK_PIPELINE_STAGE_2_FRAGMENT_SHADER_BIT | VK_PIPELINE_STAGE_2_VERTEX_SHADER_BIT | VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					VK_ACCESS_2_SHADER_SAMPLED_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_READ_BIT,
					isDepth ? VK_IMAGE_LAYOUT_DEPTH_READ_ONLY_OPTIMAL : VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
		case ResourceState::TransferSrc:
			return {VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_READ_BIT, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL};
		case ResourceState::TransferDst:
			return {VK_PIPELINE_STAGE_2_TRANSFER_BIT, VK_ACCESS_2_TRANSFER_WRITE_BIT, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL};
		case ResourceState::Present:
			return {VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT, VK_ACCESS_2_NONE, VK_IMAGE_LAYOUT_PRESENT_SRC_KHR};
		case ResourceState::UnorderedAccess:
			return {VK_PIPELINE_STAGE_2_COMPUTE_SHADER_BIT,
					VK_ACCESS_2_SHADER_STORAGE_READ_BIT | VK_ACCESS_2_SHADER_STORAGE_WRITE_BIT,
					VK_IMAGE_LAYOUT_GENERAL};
		case ResourceState::IndirectArgument:
			return {VK_PIPELINE_STAGE_2_DRAW_INDIRECT_BIT, VK_ACCESS_2_INDIRECT_COMMAND_READ_BIT, VK_IMAGE_LAYOUT_GENERAL};
		default:
			return {VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT, VK_ACCESS_2_MEMORY_READ_BIT | VK_ACCESS_2_MEMORY_WRITE_BIT, VK_IMAGE_LAYOUT_GENERAL};
		}
	}

	void CmdTransitionImage(DeviceImpl& d, VkCommandBuffer cmd, VkImage image, ResourceState from, ResourceState to, bool isDepth) {
		StateMasks src = ToStateMasks(from, isDepth);
		StateMasks dst = ToStateMasks(to, isDepth);

		VkImageMemoryBarrier2 barrier{VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER_2};
		barrier.srcStageMask = src.stages;
		barrier.srcAccessMask = src.access;
		barrier.dstStageMask = dst.stages;
		barrier.dstAccessMask = dst.access;
		barrier.oldLayout = src.layout;
		barrier.newLayout = dst.layout;
		barrier.srcQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED;
		barrier.image = image;
		barrier.subresourceRange.aspectMask = isDepth ? VK_IMAGE_ASPECT_DEPTH_BIT : VK_IMAGE_ASPECT_COLOR_BIT;
		barrier.subresourceRange.levelCount = VK_REMAINING_MIP_LEVELS;
		barrier.subresourceRange.layerCount = VK_REMAINING_ARRAY_LAYERS;

		VkDependencyInfo dependency{VK_STRUCTURE_TYPE_DEPENDENCY_INFO};
		dependency.imageMemoryBarrierCount = 1;
		dependency.pImageMemoryBarriers = &barrier;
		d.fd.vkCmdPipelineBarrier2(cmd, &dependency);
	}

	const char* VkResultName(VkResult result) {
		switch (result) {
		case VK_SUCCESS:
			return "VK_SUCCESS";
		case VK_NOT_READY:
			return "VK_NOT_READY";
		case VK_TIMEOUT:
			return "VK_TIMEOUT";
		case VK_SUBOPTIMAL_KHR:
			return "VK_SUBOPTIMAL_KHR";
		case VK_ERROR_OUT_OF_HOST_MEMORY:
			return "VK_ERROR_OUT_OF_HOST_MEMORY";
		case VK_ERROR_OUT_OF_DEVICE_MEMORY:
			return "VK_ERROR_OUT_OF_DEVICE_MEMORY";
		case VK_ERROR_INITIALIZATION_FAILED:
			return "VK_ERROR_INITIALIZATION_FAILED";
		case VK_ERROR_DEVICE_LOST:
			return "VK_ERROR_DEVICE_LOST";
		case VK_ERROR_LAYER_NOT_PRESENT:
			return "VK_ERROR_LAYER_NOT_PRESENT";
		case VK_ERROR_EXTENSION_NOT_PRESENT:
			return "VK_ERROR_EXTENSION_NOT_PRESENT";
		case VK_ERROR_FEATURE_NOT_PRESENT:
			return "VK_ERROR_FEATURE_NOT_PRESENT";
		case VK_ERROR_INCOMPATIBLE_DRIVER:
			return "VK_ERROR_INCOMPATIBLE_DRIVER";
		case VK_ERROR_SURFACE_LOST_KHR:
			return "VK_ERROR_SURFACE_LOST_KHR";
		case VK_ERROR_OUT_OF_DATE_KHR:
			return "VK_ERROR_OUT_OF_DATE_KHR";
		default:
			return "VK_ERROR_<unmapped>";
		}
	}

	namespace {

		struct HostAllocPrefix {
			void* original;
			size_t size;
		};

		void* HostAllocate(size_t size, size_t alignment) {
			if (size == 0) {
				return nullptr;
			}
			alignment = std::max<size_t>(alignment, alignof(HostAllocPrefix));
			size_t total = size + sizeof(HostAllocPrefix) + alignment;
			void* raw = Allocate(total, MemTag::Graphics, 16);
			uintptr_t payload = reinterpret_cast<uintptr_t>(raw) + sizeof(HostAllocPrefix);
			payload = (payload + alignment - 1) & ~(alignment - 1);
			HostAllocPrefix* prefix = reinterpret_cast<HostAllocPrefix*>(payload) - 1;
			prefix->original = raw;
			prefix->size = size;
			return reinterpret_cast<void*>(payload);
		}

		void HostFree(void* p) {
			if (!p) {
				return;
			}
			HostAllocPrefix* prefix = static_cast<HostAllocPrefix*>(p) - 1;
			Free(prefix->original);
		}

		VKAPI_ATTR void* VKAPI_CALL VmaHostAlloc(void*, size_t size, size_t alignment, VkSystemAllocationScope) {
			return HostAllocate(size, alignment);
		}

		VKAPI_ATTR void* VKAPI_CALL VmaHostRealloc(void*, void* original, size_t size, size_t alignment, VkSystemAllocationScope) {
			if (!original) {
				return HostAllocate(size, alignment);
			}
			if (size == 0) {
				HostFree(original);
				return nullptr;
			}
			HostAllocPrefix* prefix = static_cast<HostAllocPrefix*>(original) - 1;
			void* replacement = HostAllocate(size, alignment);
			if (replacement) {
				std::memcpy(replacement, original, std::min(prefix->size, size));
			}
			HostFree(original);
			return replacement;
		}

		VKAPI_ATTR void VKAPI_CALL VmaHostFree(void*, void* p) {
			HostFree(p);
		}

		const VkAllocationCallbacks* GetVmaHostCallbacks() {
			static const VkAllocationCallbacks callbacks{
				nullptr, &VmaHostAlloc, &VmaHostRealloc, &VmaHostFree, nullptr, nullptr};
			return &callbacks;
		}

		VKAPI_ATTR VkBool32 VKAPI_CALL DebugMessengerCallback(
			VkDebugUtilsMessageSeverityFlagBitsEXT severity,
			VkDebugUtilsMessageTypeFlagsEXT,
			const VkDebugUtilsMessengerCallbackDataEXT* data,
			void*) {
			const char* message = (data && data->pMessage) ? data->pMessage : "<no message>";
			if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT) {
				SLT_LOG_ERROR("Vulkan", "{}", message);
			} else if (severity & VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT) {
				SLT_LOG_WARN("Vulkan", "{}", message);
			} else {
				SLT_LOG_DEBUG("Vulkan", "{}", message);
			}
			return VK_FALSE;
		}

		struct CandidateInfo {
			VkPhysicalDevice physical = VK_NULL_HANDLE;
			AdapterInfo info;
			u32 graphicsFamily = 0;
			bool usable = false;
			str rejection;
		};

		bool HasExtension(std::span<const VkExtensionProperties> available, const char* name) {
			for (const VkExtensionProperties& ext : available) {
				if (std::strcmp(ext.extensionName, name) == 0) {
					return true;
				}
			}
			return false;
		}

		CandidateInfo EvaluateAdapter(const VulkanInstanceFunctions& fi, VkPhysicalDevice physical, bool headless) {
			CandidateInfo candidate;
			candidate.physical = physical;

			VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
			fi.vkGetPhysicalDeviceProperties2(physical, &props);
			candidate.info.name = props.properties.deviceName;
			candidate.info.discrete = props.properties.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU;
			candidate.info.apiVersion = props.properties.apiVersion;

			VkPhysicalDeviceMemoryProperties2 memory{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};
			fi.vkGetPhysicalDeviceMemoryProperties2(physical, &memory);
			for (u32 i = 0; i < memory.memoryProperties.memoryHeapCount; ++i) {
				if (memory.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
					candidate.info.dedicatedVideoMemory += memory.memoryProperties.memoryHeaps[i].size;
				}
			}

			if (props.properties.apiVersion < VK_API_VERSION_1_3) {
				candidate.rejection = "Vulkan 1.3 not supported";
				return candidate;
			}

			VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
			VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
			features12.pNext = &features13;
			VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
			features.pNext = &features12;
			fi.vkGetPhysicalDeviceFeatures2(physical, &features);

			if (!features13.dynamicRendering || !features13.synchronization2) {
				candidate.rejection = "dynamicRendering/synchronization2 missing";
				return candidate;
			}
			if (!features13.shaderDemoteToHelperInvocation) {
				candidate.rejection = "shaderDemoteToHelperInvocation missing";
				return candidate;
			}
			if (!features12.bufferDeviceAddress || !features12.timelineSemaphore || !features12.descriptorIndexing) {
				candidate.rejection = "bufferDeviceAddress/timelineSemaphore/descriptorIndexing missing";
				return candidate;
			}
			if (!features.features.shaderInt64) {
				candidate.rejection = "shaderInt64 missing";
				return candidate;
			}

			u32 extensionCount = 0;
			fi.vkEnumerateDeviceExtensionProperties(physical, nullptr, &extensionCount, nullptr);
			Vector<VkExtensionProperties> extensions{GetMemoryResource(MemTag::Graphics)};
			extensions.resize(extensionCount);
			fi.vkEnumerateDeviceExtensionProperties(physical, nullptr, &extensionCount, extensions.data());
			if (!headless && !HasExtension(extensions, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
				candidate.rejection = "VK_KHR_swapchain missing";
				return candidate;
			}

			u32 familyCount = 0;
			fi.vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, nullptr);
			Vector<VkQueueFamilyProperties> families{GetMemoryResource(MemTag::Graphics)};
			families.resize(familyCount);
			fi.vkGetPhysicalDeviceQueueFamilyProperties(physical, &familyCount, families.data());

			bool found = false;
			constexpr VkQueueFlags FRAME_QUEUE = VK_QUEUE_GRAPHICS_BIT | VK_QUEUE_COMPUTE_BIT;
			for (u32 i = 0; i < familyCount; ++i) {
				if ((families[i].queueFlags & FRAME_QUEUE) != FRAME_QUEUE) {
					continue;
				}
				if (!headless) {
					auto presentSupport = reinterpret_cast<PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR>(
						fi.vkGetPhysicalDeviceWin32PresentationSupportKHR);
					if (!presentSupport || !presentSupport(physical, i)) {
						continue;
					}
				}
				candidate.graphicsFamily = i;
				found = true;
				break;
			}
			if (!found) {
				candidate.rejection = headless ? "no graphics and compute queue family" : "no graphics and compute family with Win32 present support";
				return candidate;
			}

			candidate.usable = true;
			return candidate;
		}

		u64 ScoreAdapter(const CandidateInfo& candidate) {
			u64 score = candidate.info.dedicatedVideoMemory >> 20;
			if (candidate.info.discrete) {
				score += 1ull << 40;
			}
			return score;
		}

	} // anonymous namespace

	void DeviceImpl::SetDebugName(u64 objectHandle, u32 objectType, const char* name) {
		if (!fd.vkSetDebugUtilsObjectNameEXT || !name) {
			return;
		}
		VkDebugUtilsObjectNameInfoEXT info{VK_STRUCTURE_TYPE_DEBUG_UTILS_OBJECT_NAME_INFO_EXT};
		info.objectType = static_cast<VkObjectType>(objectType);
		info.objectHandle = objectHandle;
		info.pObjectName = name;
		fd.vkSetDebugUtilsObjectNameEXT(device, &info);
	}

	DeletionQueue& DeviceImpl::DeletionQueueForDestroy() {
		if (inFrame) {
			return frames[frameIndex].deletionQueue;
		}
		return frames[(frameIndex + FRAMES_IN_FLIGHT - 1) % FRAMES_IN_FLIGHT].deletionQueue;
	}

	void DeviceImpl::FlushDeletionQueue(DeletionQueue& queue) {
		for (const DeletionQueue::DeferredBuffer& entry : queue.buffers) {
			vmaDestroyBuffer(allocator, entry.buffer, entry.allocation);
		}
		for (const DeletionQueue::DeferredTexture& entry : queue.textures) {
			if (entry.view) {
				fd.vkDestroyImageView(device, entry.view, nullptr);
			}
			if (entry.allocation) {
				vmaDestroyImage(allocator, entry.image, entry.allocation);
			}
		}
		for (VkSampler sampler : queue.samplers) {
			fd.vkDestroySampler(device, sampler, nullptr);
		}
		for (VkPipeline pipeline : queue.pipelines) {
			fd.vkDestroyPipeline(device, pipeline, nullptr);
		}
		for (VkPipelineLayout layout : queue.pipelineLayouts) {
			fd.vkDestroyPipelineLayout(device, layout, nullptr);
		}
		queue.buffers.clear();
		queue.textures.clear();
		queue.samplers.clear();
		queue.pipelines.clear();
		queue.pipelineLayouts.clear();
	}

	UploadRingBlock& DeviceImpl::EnsureUploadSpace(u64 size, u64 alignment) {
		FrameContext& frame = frames[frameIndex];

		for (u32 i = frame.uploadRingActiveBlock; i < frame.uploadRing.size(); ++i) {
			UploadRingBlock& block = frame.uploadRing[i];
			u64 aligned = (block.used + alignment - 1) & ~(alignment - 1);
			if (aligned + size <= block.capacity) {
				frame.uploadRingActiveBlock = i;
				return block;
			}
		}

		constexpr u64 DEFAULT_BLOCK = 4ull * 1024 * 1024;
		u64 capacity = std::max(DEFAULT_BLOCK, size + alignment);

		UploadRingBlock block;
		VkBufferCreateInfo bufferInfo{VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO};
		bufferInfo.size = capacity;
		bufferInfo.usage = VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT | VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_SRC_BIT | VK_BUFFER_USAGE_SHADER_DEVICE_ADDRESS_BIT;
		VmaAllocationCreateInfo allocInfo{};
		allocInfo.usage = VMA_MEMORY_USAGE_AUTO_PREFER_HOST;
		allocInfo.flags = VMA_ALLOCATION_CREATE_HOST_ACCESS_SEQUENTIAL_WRITE_BIT | VMA_ALLOCATION_CREATE_MAPPED_BIT;
		VmaAllocationInfo resultInfo{};
		SLT_VK_CHECK(vmaCreateBuffer(allocator, &bufferInfo, &allocInfo, &block.buffer, &block.allocation, &resultInfo));
		block.mapped = static_cast<u8*>(resultInfo.pMappedData);
		block.capacity = capacity;
		block.used = 0;

		auto [index, generation] = buffers.Acquire();
		BufferResource& resource = buffers.slots[index].resource;
		resource.buffer = block.buffer;
		resource.allocation = VK_NULL_HANDLE;
		resource.size = capacity;
		resource.usage = BufferUsage::Uniform | BufferUsage::Storage | BufferUsage::TransferSrc | BufferUsage::DeviceAddress;
		resource.access = MemoryAccess::CPUToGPU;
		resource.mapped = block.mapped;
		{
			VkBufferDeviceAddressInfo addressInfo{VK_STRUCTURE_TYPE_BUFFER_DEVICE_ADDRESS_INFO};
			addressInfo.buffer = block.buffer;
			resource.deviceAddress = fd.vkGetBufferDeviceAddress(device, &addressInfo);
		}
		block.handle = BufferHandle{index, generation};

		SetDebugName(reinterpret_cast<u64>(block.buffer), VK_OBJECT_TYPE_BUFFER, "UploadRingBlock");

		frame.uploadRing.push_back(block);
		frame.uploadRingActiveBlock = static_cast<u32>(frame.uploadRing.size() - 1);
		return frame.uploadRing.back();
	}

	Device* Device::Create(const DeviceDesc& desc) {
		if (!VulkanLoader::EnsureLoaded()) {
			SLT_LOG_INFO("Graphics", "Device::Create failed: Vulkan loader unavailable");
			return nullptr;
		}

		DeviceImpl* impl = New<DeviceImpl>(MemTag::Graphics);
		impl->validation = desc.enableValidation;
		impl->headless = desc.headless;
		impl->mainThread = std::this_thread::get_id();

		auto fail = [&](const char* reason) -> Device* {
			SLT_LOG_ERROR("Graphics", "Device::Create failed: {}", reason);
			if (impl->device) {
				impl->fd.vkDestroyDevice(impl->device, nullptr);
			}
			if (impl->debugMessenger && impl->fi.vkDestroyDebugUtilsMessengerEXT) {
				reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(impl->fi.vkDestroyDebugUtilsMessengerEXT)(impl->instance, impl->debugMessenger, nullptr);
			}
			if (impl->instance && impl->fi.vkDestroyInstance) {
				impl->fi.vkDestroyInstance(impl->instance, nullptr);
			}
			Delete(impl);
			return nullptr;
		};

		u32 layerCount = 0;
		VulkanLoader::Get_vkEnumerateInstanceLayerProperties()(&layerCount, nullptr);
		Vector<VkLayerProperties> layers{GetMemoryResource(MemTag::Graphics)};
		layers.resize(layerCount);
		VulkanLoader::Get_vkEnumerateInstanceLayerProperties()(&layerCount, layers.data());

		bool validationAvailable = false;
		for (const VkLayerProperties& layer : layers) {
			if (std::strcmp(layer.layerName, "VK_LAYER_KHRONOS_validation") == 0) {
				validationAvailable = true;
				break;
			}
		}
		if (impl->validation && !validationAvailable) {
			SLT_LOG_DEBUG("Graphics", "validation requested but VK_LAYER_KHRONOS_validation isn't installed");
			impl->validation = false;
		}

		u32 instanceExtensionCount = 0;
		VulkanLoader::Get_vkEnumerateInstanceExtensionProperties()(nullptr, &instanceExtensionCount, nullptr);
		Vector<VkExtensionProperties> instanceExtensions{GetMemoryResource(MemTag::Graphics)};
		instanceExtensions.resize(instanceExtensionCount);
		VulkanLoader::Get_vkEnumerateInstanceExtensionProperties()(nullptr, &instanceExtensionCount, instanceExtensions.data());

		Vector<const char*> enabledExtensions{GetMemoryResource(MemTag::Graphics)};
		bool haveSurfaceExtensions = HasExtension(instanceExtensions, VK_KHR_SURFACE_EXTENSION_NAME) &&
									 HasExtension(instanceExtensions, "VK_KHR_win32_surface");
		if (!desc.headless && !haveSurfaceExtensions) {
			return fail("VK_KHR_surface / VK_KHR_win32_surface unavailable");
		}
		if (haveSurfaceExtensions) {
			enabledExtensions.push_back(VK_KHR_SURFACE_EXTENSION_NAME);
			enabledExtensions.push_back("VK_KHR_win32_surface");
		}
		bool haveDebugUtils = HasExtension(instanceExtensions, VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		if (impl->validation && haveDebugUtils) {
			enabledExtensions.push_back(VK_EXT_DEBUG_UTILS_EXTENSION_NAME);
		}

		VkApplicationInfo appInfo{VK_STRUCTURE_TYPE_APPLICATION_INFO};
		appInfo.pApplicationName = "Shalltear";
		appInfo.pEngineName = "Shalltear";
		appInfo.apiVersion = VK_API_VERSION_1_3;

		const char* validationLayer = "VK_LAYER_KHRONOS_validation";
		VkInstanceCreateInfo instanceInfo{VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO};
		instanceInfo.pApplicationInfo = &appInfo;
		instanceInfo.enabledExtensionCount = static_cast<u32>(enabledExtensions.size());
		instanceInfo.ppEnabledExtensionNames = enabledExtensions.data();
		if (impl->validation) {
			instanceInfo.enabledLayerCount = 1;
			instanceInfo.ppEnabledLayerNames = &validationLayer;
		}

		VkResult result = VulkanLoader::Get_vkCreateInstance()(&instanceInfo, nullptr, &impl->instance);
		if (result != VK_SUCCESS) {
			return fail(VkResultName(result));
		}
		if (!VulkanLoader::LoadInstanceTable(impl->instance, impl->fi)) {
			return fail("instance function table incomplete");
		}

		if (impl->validation && haveDebugUtils && impl->fi.vkCreateDebugUtilsMessengerEXT) {
			VkDebugUtilsMessengerCreateInfoEXT messengerInfo{VK_STRUCTURE_TYPE_DEBUG_UTILS_MESSENGER_CREATE_INFO_EXT};
			messengerInfo.messageSeverity = VK_DEBUG_UTILS_MESSAGE_SEVERITY_WARNING_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_SEVERITY_ERROR_BIT_EXT;
			messengerInfo.messageType = VK_DEBUG_UTILS_MESSAGE_TYPE_VALIDATION_BIT_EXT | VK_DEBUG_UTILS_MESSAGE_TYPE_PERFORMANCE_BIT_EXT;
			messengerInfo.pfnUserCallback = &DebugMessengerCallback;
			reinterpret_cast<PFN_vkCreateDebugUtilsMessengerEXT>(impl->fi.vkCreateDebugUtilsMessengerEXT)(
				impl->instance, &messengerInfo, nullptr, &impl->debugMessenger);
		}

		u32 physicalCount = 0;
		impl->fi.vkEnumeratePhysicalDevices(impl->instance, &physicalCount, nullptr);
		if (physicalCount == 0) {
			return fail("no Vulkan physical devices");
		}
		Vector<VkPhysicalDevice> physicals{GetMemoryResource(MemTag::Graphics)};
		physicals.resize(physicalCount);
		impl->fi.vkEnumeratePhysicalDevices(impl->instance, &physicalCount, physicals.data());

		Vector<CandidateInfo> candidates{GetMemoryResource(MemTag::Graphics)};
		for (VkPhysicalDevice physical : physicals) {
			candidates.push_back(EvaluateAdapter(impl->fi, physical, impl->headless));
			impl->adapters.push_back(candidates.back().info);
		}

		i32 selected = -1;
		if (desc.forceAdapterIndex >= 0) {
			if (desc.forceAdapterIndex < static_cast<i32>(candidates.size()) && candidates[desc.forceAdapterIndex].usable) {
				selected = desc.forceAdapterIndex;
			} else {
				SLT_LOG_WARN("Graphics", "forced adapter idx {} unusable (falling back to scored selection)", desc.forceAdapterIndex);
			}
		}
		if (selected < 0) {
			u64 bestScore = 0;
			for (size_t i = 0; i < candidates.size(); ++i) {
				if (!candidates[i].usable) {
					continue;
				}
				u64 score = ScoreAdapter(candidates[i]);
				if (selected < 0 || score > bestScore) {
					selected = static_cast<i32>(i);
					bestScore = score;
				}
			}
		}
		if (selected < 0) {
			for (size_t i = 0; i < candidates.size(); ++i) {
				SLT_LOG_ERROR("Graphics", "  adapter {}: '{}' rejected: {}", i, candidates[i].info.name, candidates[i].rejection);
			}
			return fail("no adapter satisfies the Vulkan 1.3 feature contract");
		}

		impl->selectedAdapter = selected;
		impl->physicalDevice = candidates[selected].physical;
		impl->queueFamily = candidates[selected].graphicsFamily;
		SLT_LOG_INFO("Graphics", "Selected adapter: '{}' ({} MiB VRAM, api {}.{}.{})",
					 impl->adapters[selected].name,
					 impl->adapters[selected].dedicatedVideoMemory >> 20,
					 VK_API_VERSION_MAJOR(impl->adapters[selected].apiVersion),
					 VK_API_VERSION_MINOR(impl->adapters[selected].apiVersion),
					 VK_API_VERSION_PATCH(impl->adapters[selected].apiVersion));

		{
			VkPhysicalDeviceProperties2 props{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_PROPERTIES_2};
			impl->fi.vkGetPhysicalDeviceProperties2(impl->physicalDevice, &props);
			impl->maxAnisotropy = props.properties.limits.maxSamplerAnisotropy;
			impl->framebufferColorSamples = static_cast<u32>(props.properties.limits.framebufferColorSampleCounts);
			impl->framebufferDepthSamples = static_cast<u32>(props.properties.limits.framebufferDepthSampleCounts);
		}

		f32 queuePriority = 1.0f;
		VkDeviceQueueCreateInfo queueInfo{VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO};
		queueInfo.queueFamilyIndex = impl->queueFamily;
		queueInfo.queueCount = 1;
		queueInfo.pQueuePriorities = &queuePriority;

		VkPhysicalDeviceVulkan13Features features13{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_3_FEATURES};
		features13.dynamicRendering = VK_TRUE;
		features13.synchronization2 = VK_TRUE;
		features13.shaderDemoteToHelperInvocation = VK_TRUE;
		VkPhysicalDeviceVulkan12Features features12{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_VULKAN_1_2_FEATURES};
		features12.pNext = &features13;
		features12.bufferDeviceAddress = VK_TRUE;
		features12.timelineSemaphore = VK_TRUE;
		features12.descriptorIndexing = VK_TRUE;
		VkPhysicalDeviceFeatures2 features{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
		features.pNext = &features12;
		features.features.samplerAnisotropy = VK_TRUE;
		features.features.shaderInt64 = VK_TRUE;
		{
			VkPhysicalDeviceFeatures2 supported{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_FEATURES_2};
			impl->fi.vkGetPhysicalDeviceFeatures2(impl->physicalDevice, &supported);
			impl->fillModeNonSolid = supported.features.fillModeNonSolid == VK_TRUE;
			impl->imageCubeArray = supported.features.imageCubeArray == VK_TRUE;
			impl->textureCompressionBC = supported.features.textureCompressionBC == VK_TRUE;
		}
		features.features.fillModeNonSolid = impl->fillModeNonSolid ? VK_TRUE : VK_FALSE;
		features.features.imageCubeArray = impl->imageCubeArray ? VK_TRUE : VK_FALSE;
		features.features.textureCompressionBC = impl->textureCompressionBC ? VK_TRUE : VK_FALSE;

		Vector<const char*> deviceExtensions{GetMemoryResource(MemTag::Graphics)};
		if (!impl->headless || haveSurfaceExtensions) {
			u32 count = 0;
			impl->fi.vkEnumerateDeviceExtensionProperties(impl->physicalDevice, nullptr, &count, nullptr);
			Vector<VkExtensionProperties> available{GetMemoryResource(MemTag::Graphics)};
			available.resize(count);
			impl->fi.vkEnumerateDeviceExtensionProperties(impl->physicalDevice, nullptr, &count, available.data());
			if (HasExtension(available, VK_KHR_SWAPCHAIN_EXTENSION_NAME)) {
				deviceExtensions.push_back(VK_KHR_SWAPCHAIN_EXTENSION_NAME);
			}
		}

		VkDeviceCreateInfo deviceInfo{VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO};
		deviceInfo.pNext = &features;
		deviceInfo.queueCreateInfoCount = 1;
		deviceInfo.pQueueCreateInfos = &queueInfo;
		deviceInfo.enabledExtensionCount = static_cast<u32>(deviceExtensions.size());
		deviceInfo.ppEnabledExtensionNames = deviceExtensions.data();

		result = impl->fi.vkCreateDevice(impl->physicalDevice, &deviceInfo, nullptr, &impl->device);
		if (result != VK_SUCCESS) {
			return fail(VkResultName(result));
		}
		if (!VulkanLoader::LoadDeviceTable(impl->device, impl->fi.vkGetDeviceProcAddr, impl->fd)) {
			return fail("device function table incomplete");
		}
		impl->fd.vkGetDeviceQueue(impl->device, impl->queueFamily, 0, &impl->queue);

		VmaVulkanFunctions vmaFunctions{};
		vmaFunctions.vkGetInstanceProcAddr = VulkanLoader::GetInstanceProcAddrFn();
		vmaFunctions.vkGetDeviceProcAddr = impl->fi.vkGetDeviceProcAddr;

		VmaAllocatorCreateInfo allocatorInfo{};
		allocatorInfo.flags = VMA_ALLOCATOR_CREATE_BUFFER_DEVICE_ADDRESS_BIT;
		allocatorInfo.physicalDevice = impl->physicalDevice;
		allocatorInfo.device = impl->device;
		allocatorInfo.instance = impl->instance;
		allocatorInfo.vulkanApiVersion = VK_API_VERSION_1_3;
		allocatorInfo.pVulkanFunctions = &vmaFunctions;
		allocatorInfo.pAllocationCallbacks = GetVmaHostCallbacks();
		result = vmaCreateAllocator(&allocatorInfo, &impl->allocator);
		if (result != VK_SUCCESS) {
			return fail("vmaCreateAllocator failed");
		}

		VkSemaphoreTypeCreateInfo timelineType{VK_STRUCTURE_TYPE_SEMAPHORE_TYPE_CREATE_INFO};
		timelineType.semaphoreType = VK_SEMAPHORE_TYPE_TIMELINE;
		VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
		semaphoreInfo.pNext = &timelineType;
		SLT_VK_CHECK(impl->fd.vkCreateSemaphore(impl->device, &semaphoreInfo, nullptr, &impl->timeline));

		for (FrameContext& frame : impl->frames) {
			VkCommandPoolCreateInfo poolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
			poolInfo.queueFamilyIndex = impl->queueFamily;
			SLT_VK_CHECK(impl->fd.vkCreateCommandPool(impl->device, &poolInfo, nullptr, &frame.commandPool));

			VkCommandBufferAllocateInfo allocInfo{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
			allocInfo.commandPool = frame.commandPool;
			allocInfo.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
			allocInfo.commandBufferCount = 1;
			SLT_VK_CHECK(impl->fd.vkAllocateCommandBuffers(impl->device, &allocInfo, &frame.commandBuffer));
			SLT_VK_CHECK(impl->fd.vkAllocateCommandBuffers(impl->device, &allocInfo, &frame.uploadCommandBuffer));

			frame.commandListImpl.device = impl;
			frame.commandListImpl.commandBuffer = frame.commandBuffer;
			frame.commandList._impl = &frame.commandListImpl;
		}

		VkCommandPoolCreateInfo immediatePoolInfo{VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO};
		immediatePoolInfo.queueFamilyIndex = impl->queueFamily;
		SLT_VK_CHECK(impl->fd.vkCreateCommandPool(impl->device, &immediatePoolInfo, nullptr, &impl->immediatePool));
		VkCommandBufferAllocateInfo immediateAlloc{VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO};
		immediateAlloc.commandPool = impl->immediatePool;
		immediateAlloc.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY;
		immediateAlloc.commandBufferCount = 1;
		SLT_VK_CHECK(impl->fd.vkAllocateCommandBuffers(impl->device, &immediateAlloc, &impl->immediateBuffer));
		VkFenceCreateInfo fenceInfo{VK_STRUCTURE_TYPE_FENCE_CREATE_INFO};
		SLT_VK_CHECK(impl->fd.vkCreateFence(impl->device, &fenceInfo, nullptr, &impl->immediateFence));

		VkPipelineCacheCreateInfo cacheInfo{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
		SLT_VK_CHECK(impl->fd.vkCreatePipelineCache(impl->device, &cacheInfo, nullptr, &impl->pipelineCache));

		Device* device = New<Device>(MemTag::Graphics);
		device->_impl = impl;
		return device;
	}

	void Device::Destroy(Device* device) {
		if (!device) {
			return;
		}
		DeviceImpl* impl = device->_impl;
		SLT_RHI_ASSERT_MAIN_THREAD(impl);
		SLT_CORE_ASSERT_MSG(!impl->inFrame, "Device::Destroy called inside a frame");

		impl->fd.vkDeviceWaitIdle(impl->device);

		impl->DiscardPendingUploads();

		if (!impl->swapChains.empty()) {
			SLT_LOG_ERROR("Graphics", "{} swapchain(s) still alive at device teardown. Destroying", impl->swapChains.size());
			while (!impl->swapChains.empty()) {
				device->DestroySwapChain(impl->swapChains.back());
			}
		}

		for (FrameContext& frame : impl->frames) {
			impl->FlushDeletionQueue(frame.deletionQueue);
			impl->DestroyFrameDescriptors(frame);
			for (UploadRingBlock& block : frame.uploadRing) {
				impl->buffers.Release(block.handle.index);
				vmaDestroyBuffer(impl->allocator, block.buffer, block.allocation);
			}
			frame.uploadRing.clear();
			impl->fd.vkDestroyCommandPool(impl->device, frame.commandPool, nullptr);
		}

		auto reportLeaks = [](const char* kind, u32 count) {
			if (count > 0) {
				SLT_LOG_ERROR("Graphics", "{} {} object(s) leaked at device teardown", count, kind);
			}
		};
		reportLeaks("buffer", impl->buffers.liveCount);
		reportLeaks("texture", impl->textures.liveCount);
		reportLeaks("sampler", impl->samplers.liveCount);
		reportLeaks("pipeline", impl->pipelines.liveCount);
		SLT_CORE_ASSERT_MSG(
			impl->buffers.liveCount == 0 && impl->textures.liveCount == 0 &&
				impl->samplers.liveCount == 0 && impl->pipelines.liveCount == 0,
			"GPU resources leaked at device teardown (see log)");

		for (auto& slot : impl->buffers.slots) {
			if (slot.alive && slot.resource.buffer) {
				vmaDestroyBuffer(impl->allocator, slot.resource.buffer, slot.resource.allocation);
			}
		}
		for (auto& slot : impl->textures.slots) {
			if (slot.alive) {
				if (slot.resource.view) {
					impl->fd.vkDestroyImageView(impl->device, slot.resource.view, nullptr);
				}
				if (slot.resource.allocation) {
					vmaDestroyImage(impl->allocator, slot.resource.image, slot.resource.allocation);
				}
			}
		}
		for (auto& slot : impl->samplers.slots) {
			if (slot.alive && slot.resource.sampler) {
				impl->fd.vkDestroySampler(impl->device, slot.resource.sampler, nullptr);
			}
		}
		for (auto& slot : impl->pipelines.slots) {
			if (slot.alive && slot.resource.pipeline) {
				impl->fd.vkDestroyPipeline(impl->device, slot.resource.pipeline, nullptr);
			}
			if (slot.alive && slot.resource.layout) {
				impl->fd.vkDestroyPipelineLayout(impl->device, slot.resource.layout, nullptr);
			}
		}

		for (auto& [hash, layout] : impl->setLayoutCache) {
			impl->fd.vkDestroyDescriptorSetLayout(impl->device, layout, nullptr);
		}

		impl->fd.vkDestroyPipelineCache(impl->device, impl->pipelineCache, nullptr);
		impl->fd.vkDestroyFence(impl->device, impl->immediateFence, nullptr);
		impl->fd.vkDestroyCommandPool(impl->device, impl->immediatePool, nullptr);
		impl->fd.vkDestroySemaphore(impl->device, impl->timeline, nullptr);

		vmaDestroyAllocator(impl->allocator);
		impl->fd.vkDestroyDevice(impl->device, nullptr);
		if (impl->debugMessenger && impl->fi.vkDestroyDebugUtilsMessengerEXT) {
			reinterpret_cast<PFN_vkDestroyDebugUtilsMessengerEXT>(impl->fi.vkDestroyDebugUtilsMessengerEXT)(impl->instance, impl->debugMessenger, nullptr);
		}
		impl->fi.vkDestroyInstance(impl->instance, nullptr);

		Delete(impl);
		Delete(device);
	}

	std::span<const AdapterInfo> Device::GetAdapters() const {
		return {_impl->adapters.data(), _impl->adapters.size()};
	}

	const AdapterInfo& Device::GetSelectedAdapter() const {
		return _impl->adapters[_impl->selectedAdapter];
	}

	u32 Device::GetSupportedSampleCounts(Format format, TextureUsage usage) const {
		DeviceImpl* impl = _impl;
		if (format == Format::Unknown) {
			return 1;
		}

		VkPhysicalDeviceImageFormatInfo2 info{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_IMAGE_FORMAT_INFO_2};
		info.format = ToVkFormat(format);
		info.type = VK_IMAGE_TYPE_2D;
		info.tiling = VK_IMAGE_TILING_OPTIMAL;
		info.usage = ToVkImageUsage(usage);
		VkImageFormatProperties2 props{VK_STRUCTURE_TYPE_IMAGE_FORMAT_PROPERTIES_2};
		if (impl->fi.vkGetPhysicalDeviceImageFormatProperties2(impl->physicalDevice, &info, &props) != VK_SUCCESS) {
			return 1;
		}

		u32 counts = static_cast<u32>(props.imageFormatProperties.sampleCounts);
		if (HasFlag(usage, TextureUsage::RenderTarget)) {
			counts &= impl->framebufferColorSamples;
		}
		if (HasFlag(usage, TextureUsage::DepthStencil)) {
			counts &= impl->framebufferDepthSamples;
		}
		return counts | 1u;
	}

	VideoMemoryInfo Device::GetVideoMemoryInfo() const {
		VmaBudget budgets[VK_MAX_MEMORY_HEAPS]{};
		vmaGetHeapBudgets(_impl->allocator, budgets);

		VkPhysicalDeviceMemoryProperties2 memory{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_MEMORY_PROPERTIES_2};
		_impl->fi.vkGetPhysicalDeviceMemoryProperties2(_impl->physicalDevice, &memory);

		VideoMemoryInfo info;
		for (u32 i = 0; i < memory.memoryProperties.memoryHeapCount; ++i) {
			if (memory.memoryProperties.memoryHeaps[i].flags & VK_MEMORY_HEAP_DEVICE_LOCAL_BIT) {
				info.usedBytes += budgets[i].usage;
				info.budgetBytes += budgets[i].budget;
			}
		}
		return info;
	}

	u32 Device::GetLiveBufferCount() const { return _impl->buffers.liveCount; }
	u32 Device::GetLiveTextureCount() const { return _impl->textures.liveCount; }
	u32 Device::GetLiveSamplerCount() const { return _impl->samplers.liveCount; }
	u32 Device::GetLivePipelineCount() const { return _impl->pipelines.liveCount; }

	void Device::WaitIdle() {
		SLT_RHI_ASSERT_MAIN_THREAD(_impl);
		_impl->FlushPendingUploads();
		_impl->fd.vkDeviceWaitIdle(_impl->device);
	}

	void Device::LoadPipelineCache(std::span<const u8> blob) {
		SLT_RHI_ASSERT_MAIN_THREAD(_impl);
		if (blob.empty()) {
			return;
		}
		VkPipelineCache replacement = VK_NULL_HANDLE;
		VkPipelineCacheCreateInfo info{VK_STRUCTURE_TYPE_PIPELINE_CACHE_CREATE_INFO};
		info.initialDataSize = blob.size();
		info.pInitialData = blob.data();
		if (_impl->fd.vkCreatePipelineCache(_impl->device, &info, nullptr, &replacement) == VK_SUCCESS) {
			_impl->fd.vkDestroyPipelineCache(_impl->device, _impl->pipelineCache, nullptr);
			_impl->pipelineCache = replacement;
		}
	}

	size_t Device::SavePipelineCache(std::span<u8> out) const {
		SLT_RHI_ASSERT_MAIN_THREAD(_impl);
		size_t size = 0;
		_impl->fd.vkGetPipelineCacheData(_impl->device, _impl->pipelineCache, &size, nullptr);
		if (out.empty() || size == 0) {
			return size;
		}
		if (out.size() < size) {
			return 0;
		}
		if (_impl->fd.vkGetPipelineCacheData(_impl->device, _impl->pipelineCache, &size, out.data()) != VK_SUCCESS) {
			return 0;
		}
		return size;
	}

	CommandList* Device::BeginFrame() {
		DeviceImpl* impl = _impl;
		SLT_RHI_ASSERT_MAIN_THREAD(impl);
		SLT_CORE_ASSERT_MSG(!impl->inFrame, "BeginFrame called twice without EndFrame");

		FrameContext& frame = impl->frames[impl->frameIndex];

		if (frame.timelineValue != 0) {
			VkSemaphoreWaitInfo waitInfo{VK_STRUCTURE_TYPE_SEMAPHORE_WAIT_INFO};
			waitInfo.semaphoreCount = 1;
			waitInfo.pSemaphores = &impl->timeline;
			waitInfo.pValues = &frame.timelineValue;
			SLT_VK_CHECK(impl->fd.vkWaitSemaphores(impl->device, &waitInfo, UINT64_MAX));
		}

		impl->FlushDeletionQueue(frame.deletionQueue);
		SLT_VK_CHECK(impl->fd.vkResetCommandPool(impl->device, frame.commandPool, 0));
		impl->ResetFrameDescriptors(frame);
		for (UploadRingBlock& block : frame.uploadRing) {
			block.used = 0;
		}
		frame.uploadRingActiveBlock = 0;
		frame.waitSemaphores.clear();
		frame.signalSemaphores.clear();

		VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
		begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		SLT_VK_CHECK(impl->fd.vkBeginCommandBuffer(frame.commandBuffer, &begin));

		frame.commandListImpl.boundPipelineIndex = 0;
		frame.commandListImpl.inRenderPass = false;

		impl->inFrame = true;
		return &frame.commandList;
	}

	void Device::EndFrame() {
		DeviceImpl* impl = _impl;
		SLT_RHI_ASSERT_MAIN_THREAD(impl);
		SLT_CORE_ASSERT_MSG(impl->inFrame, "EndFrame without BeginFrame");

		FrameContext& frame = impl->frames[impl->frameIndex];
		SLT_CORE_ASSERT_MSG(!frame.commandListImpl.inRenderPass, "EndFrame inside an open render pass");

		SLT_VK_CHECK(impl->fd.vkEndCommandBuffer(frame.commandBuffer));

		const bool prologue = impl->HasPendingUploads();
		if (prologue) {
			VkCommandBufferBeginInfo begin{VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO};
			begin.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
			SLT_VK_CHECK(impl->fd.vkBeginCommandBuffer(frame.uploadCommandBuffer, &begin));
			impl->RecordPendingUploads(frame.uploadCommandBuffer, frame.deletionQueue);
			SLT_VK_CHECK(impl->fd.vkEndCommandBuffer(frame.uploadCommandBuffer));
		}

		Vector<VkSemaphoreSubmitInfo> waits{GetMemoryResource(MemTag::Graphics)};
		for (VkSemaphore semaphore : frame.waitSemaphores) {
			VkSemaphoreSubmitInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
			info.semaphore = semaphore;
			info.stageMask = VK_PIPELINE_STAGE_2_COLOR_ATTACHMENT_OUTPUT_BIT;
			waits.push_back(info);
		}

		Vector<VkSemaphoreSubmitInfo> signals{GetMemoryResource(MemTag::Graphics)};
		for (VkSemaphore semaphore : frame.signalSemaphores) {
			VkSemaphoreSubmitInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
			info.semaphore = semaphore;
			info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			signals.push_back(info);
		}
		{
			VkSemaphoreSubmitInfo info{VK_STRUCTURE_TYPE_SEMAPHORE_SUBMIT_INFO};
			info.semaphore = impl->timeline;
			info.value = ++impl->timelineCounter;
			info.stageMask = VK_PIPELINE_STAGE_2_ALL_COMMANDS_BIT;
			signals.push_back(info);
		}

		VkCommandBufferSubmitInfo cmdInfos[2]{{VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO},
											  {VK_STRUCTURE_TYPE_COMMAND_BUFFER_SUBMIT_INFO}};
		u32 cmdCount = 0;
		if (prologue) {
			cmdInfos[cmdCount++].commandBuffer = frame.uploadCommandBuffer;
		}
		cmdInfos[cmdCount++].commandBuffer = frame.commandBuffer;

		VkSubmitInfo2 submit{VK_STRUCTURE_TYPE_SUBMIT_INFO_2};
		submit.waitSemaphoreInfoCount = static_cast<u32>(waits.size());
		submit.pWaitSemaphoreInfos = waits.data();
		submit.commandBufferInfoCount = cmdCount;
		submit.pCommandBufferInfos = cmdInfos;
		submit.signalSemaphoreInfoCount = static_cast<u32>(signals.size());
		submit.pSignalSemaphoreInfos = signals.data();
		SLT_VK_CHECK(impl->fd.vkQueueSubmit2(impl->queue, 1, &submit, VK_NULL_HANDLE));

		frame.timelineValue = impl->timelineCounter;
		impl->inFrame = false;
		impl->frameIndex = (impl->frameIndex + 1) % FRAMES_IN_FLIGHT;
	}

	bool Device::IsInFrame() const {
		return _impl->inFrame;
	}

	TransientAllocation Device::AllocateTransient(u64 size, u64 alignment) {
		DeviceImpl* impl = _impl;
		SLT_RHI_ASSERT_MAIN_THREAD(impl);
		SLT_CORE_ASSERT_MSG(impl->inFrame, "AllocateTransient outside a frame");
		SLT_CORE_ASSERT(size > 0 && IsPowerOfTwo(alignment));

		UploadRingBlock& block = impl->EnsureUploadSpace(size, alignment);
		u64 offset = (block.used + alignment - 1) & ~(alignment - 1);
		block.used = offset + size;

		TransientAllocation allocation;
		allocation.buffer = block.handle;
		allocation.offset = offset;
		allocation.cpu = block.mapped + offset;
		return allocation;
	}

} // namespace Shalltear::RHI

#endif
