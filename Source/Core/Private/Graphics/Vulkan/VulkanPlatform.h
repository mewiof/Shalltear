#pragma once

#include "Graphics/Vulkan/VulkanLoader.h"

#include "Shalltear/Core/NativeWindow.h"

#include <span>

namespace Shalltear::RHI::VulkanPlatform {

	const char* GetLoaderLibraryName();

	std::span<const char* const> GetSurfaceExtensions();

	bool QueueFamilyPresents(VkInstance instance, VkPhysicalDevice physical, u32 family);

	// VK_ERROR_EXTENSION_NOT_PRESENT when window's system has no enabled extension
	VkResult CreateSurface(VkInstance instance, const NativeWindowHandle& window, VkSurfaceKHR* outSurface);

} // namespace Shalltear::RHI::VulkanPlatform
