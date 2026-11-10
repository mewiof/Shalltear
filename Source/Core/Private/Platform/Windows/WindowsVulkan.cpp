#include "Graphics/Vulkan/VulkanPlatform.h"

#include "Platform/Windows/WindowsHeaders.h"
#include <vulkan/vulkan_win32.h>

namespace Shalltear::RHI::VulkanPlatform {

	namespace {

		constexpr const char* SURFACE_EXTENSIONS[] = {"VK_KHR_win32_surface"};

		template <typename TFunction>
		TFunction Resolve(VkInstance instance, const char* name) {
			PFN_vkGetInstanceProcAddr getProc = VulkanLoader::GetInstanceProcAddrFn();
			return getProc ? reinterpret_cast<TFunction>(getProc(instance, name)) : nullptr;
		}

	} // anonymous namespace

	const char* GetLoaderLibraryName() {
		return "vulkan-1.dll";
	}

	std::span<const char* const> GetSurfaceExtensions() {
		return SURFACE_EXTENSIONS;
	}

	bool QueueFamilyPresents(VkInstance instance, VkPhysicalDevice physical, u32 family) {
		auto presentSupport = Resolve<PFN_vkGetPhysicalDeviceWin32PresentationSupportKHR>(
			instance, "vkGetPhysicalDeviceWin32PresentationSupportKHR");
		return presentSupport && presentSupport(physical, family);
	}

	VkResult CreateSurface(VkInstance instance, const NativeWindowHandle& window, VkSurfaceKHR* outSurface) {
		auto createSurface = Resolve<PFN_vkCreateWin32SurfaceKHR>(instance, "vkCreateWin32SurfaceKHR");
		if (window.system != NativeWindowSystem::Win32 || !createSurface) {
			return VK_ERROR_EXTENSION_NOT_PRESENT;
		}

		VkWin32SurfaceCreateInfoKHR surfaceInfo{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
		surfaceInfo.hinstance = static_cast<HINSTANCE>(window.display);
		surfaceInfo.hwnd = static_cast<HWND>(window.window);
		return createSurface(instance, &surfaceInfo, nullptr, outSurface);
	}

} // namespace Shalltear::RHI::VulkanPlatform
