#pragma once

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

namespace Shalltear::RHI {

#define SLT_VK_GLOBAL_FUNCS(X)            \
	X(vkCreateInstance)                   \
	X(vkEnumerateInstanceVersion)         \
	X(vkEnumerateInstanceLayerProperties) \
	X(vkEnumerateInstanceExtensionProperties)

#define SLT_VK_INSTANCE_FUNCS(X)                 \
	X(vkDestroyInstance)                         \
	X(vkEnumeratePhysicalDevices)                \
	X(vkGetPhysicalDeviceProperties2)            \
	X(vkGetPhysicalDeviceImageFormatProperties2) \
	X(vkGetPhysicalDeviceFeatures2)              \
	X(vkGetPhysicalDeviceMemoryProperties2)      \
	X(vkGetPhysicalDeviceQueueFamilyProperties)  \
	X(vkEnumerateDeviceExtensionProperties)      \
	X(vkCreateDevice)                            \
	X(vkGetDeviceProcAddr)                       \
	X(vkDestroySurfaceKHR)                       \
	X(vkGetPhysicalDeviceSurfaceCapabilitiesKHR) \
	X(vkGetPhysicalDeviceSurfaceFormatsKHR)      \
	X(vkGetPhysicalDeviceSurfacePresentModesKHR) \
	X(vkGetPhysicalDeviceSurfaceSupportKHR)

#define SLT_VK_INSTANCE_OPTIONAL_FUNCS(X) \
	X(vkCreateDebugUtilsMessengerEXT)     \
	X(vkDestroyDebugUtilsMessengerEXT)    \
	X(vkCreateWin32SurfaceKHR)            \
	X(vkGetPhysicalDeviceWin32PresentationSupportKHR)

#define SLT_VK_DEVICE_FUNCS(X)      \
	X(vkDestroyDevice)              \
	X(vkGetDeviceQueue)             \
	X(vkDeviceWaitIdle)             \
	X(vkQueueWaitIdle)              \
	X(vkCreateCommandPool)          \
	X(vkDestroyCommandPool)         \
	X(vkResetCommandPool)           \
	X(vkAllocateCommandBuffers)     \
	X(vkFreeCommandBuffers)         \
	X(vkBeginCommandBuffer)         \
	X(vkEndCommandBuffer)           \
	X(vkQueueSubmit2)               \
	X(vkQueuePresentKHR)            \
	X(vkCreateSemaphore)            \
	X(vkDestroySemaphore)           \
	X(vkWaitSemaphores)             \
	X(vkGetSemaphoreCounterValue)   \
	X(vkCreateFence)                \
	X(vkDestroyFence)               \
	X(vkWaitForFences)              \
	X(vkResetFences)                \
	X(vkCreateSwapchainKHR)         \
	X(vkDestroySwapchainKHR)        \
	X(vkGetSwapchainImagesKHR)      \
	X(vkAcquireNextImageKHR)        \
	X(vkCreateImageView)            \
	X(vkDestroyImageView)           \
	X(vkGetBufferDeviceAddress)     \
	X(vkCmdPipelineBarrier2)        \
	X(vkCmdBeginRendering)          \
	X(vkCmdEndRendering)            \
	X(vkCmdBindPipeline)            \
	X(vkCmdSetViewport)             \
	X(vkCmdSetScissor)              \
	X(vkCmdBindDescriptorSets)      \
	X(vkCmdPushConstants)           \
	X(vkCmdBindIndexBuffer)         \
	X(vkCmdDraw)                    \
	X(vkCmdDrawIndexed)             \
	X(vkCmdDrawIndirect)            \
	X(vkCmdDispatch)                \
	X(vkCmdDispatchIndirect)        \
	X(vkCmdCopyBuffer2)             \
	X(vkCmdCopyBufferToImage2)      \
	X(vkCmdCopyImageToBuffer2)      \
	X(vkCmdBlitImage2)              \
	X(vkCmdResolveImage2)           \
	X(vkCreateDescriptorSetLayout)  \
	X(vkDestroyDescriptorSetLayout) \
	X(vkCreateDescriptorPool)       \
	X(vkDestroyDescriptorPool)      \
	X(vkResetDescriptorPool)        \
	X(vkAllocateDescriptorSets)     \
	X(vkUpdateDescriptorSets)       \
	X(vkCreatePipelineLayout)       \
	X(vkDestroyPipelineLayout)      \
	X(vkCreateGraphicsPipelines)    \
	X(vkCreateComputePipelines)     \
	X(vkDestroyPipeline)            \
	X(vkCreatePipelineCache)        \
	X(vkDestroyPipelineCache)       \
	X(vkGetPipelineCacheData)       \
	X(vkCreateShaderModule)         \
	X(vkDestroyShaderModule)        \
	X(vkCreateSampler)              \
	X(vkDestroySampler)

#define SLT_VK_DEVICE_OPTIONAL_FUNCS(X) \
	X(vkSetDebugUtilsObjectNameEXT)

	struct VulkanInstanceFunctions {
#define SLT_VK_DECLARE(name) PFN_##name name = nullptr;
		SLT_VK_INSTANCE_FUNCS(SLT_VK_DECLARE)
#undef SLT_VK_DECLARE
#define SLT_VK_DECLARE(name) PFN_vkVoidFunction name = nullptr;
		SLT_VK_INSTANCE_OPTIONAL_FUNCS(SLT_VK_DECLARE)
#undef SLT_VK_DECLARE
	};

	struct VulkanDeviceFunctions {
#define SLT_VK_DECLARE(name) PFN_##name name = nullptr;
		SLT_VK_DEVICE_FUNCS(SLT_VK_DECLARE)
		SLT_VK_DEVICE_OPTIONAL_FUNCS(SLT_VK_DECLARE)
#undef SLT_VK_DECLARE
	};

	namespace VulkanLoader {

		bool EnsureLoaded();

		PFN_vkGetInstanceProcAddr GetInstanceProcAddrFn();

#define SLT_VK_DECLARE(name) PFN_##name Get_##name();
		SLT_VK_GLOBAL_FUNCS(SLT_VK_DECLARE)
#undef SLT_VK_DECLARE

		bool LoadInstanceTable(VkInstance instance, VulkanInstanceFunctions& out);
		bool LoadDeviceTable(VkDevice device, PFN_vkGetDeviceProcAddr getDeviceProcAddr, VulkanDeviceFunctions& out);

	} // namespace VulkanLoader

} // namespace Shalltear::RHI
