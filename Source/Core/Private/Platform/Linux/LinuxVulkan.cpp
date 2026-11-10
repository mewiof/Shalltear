#include "Graphics/Vulkan/VulkanPlatform.h"

#include "Platform/Linux/Window/LinuxWindow.h"

#include <cstdint>

// after engine headers (Xlib.h macros None, Bool, Status + Success)
#include <X11/Xlib.h>
#include <vulkan/vulkan_wayland.h>
#include <vulkan/vulkan_xlib.h>

namespace Shalltear::RHI::VulkanPlatform {

	namespace {

		constexpr const char* SURFACE_EXTENSIONS[] = {"VK_KHR_xlib_surface", "VK_KHR_wayland_surface"};

		template <typename TFunction>
		TFunction Resolve(VkInstance instance, const char* name) {
			PFN_vkGetInstanceProcAddr getProc = VulkanLoader::GetInstanceProcAddrFn();
			return getProc ? reinterpret_cast<TFunction>(getProc(instance, name)) : nullptr;
		}

	} // anonymous namespace

	const char* GetLoaderLibraryName() {
		return "libvulkan.so.1";
	}

	std::span<const char* const> GetSurfaceExtensions() {
		return SURFACE_EXTENSIONS;
	}

	bool QueueFamilyPresents(VkInstance instance, VkPhysicalDevice physical, u32 family) {
		Linux::DisplayServer* server = Linux::GetDisplayServer();
		if (!server) {
			return true;
		}
		const NativeWindowHandle probe = server->GetPresentationProbe();
		if (probe.system == NativeWindowSystem::Xlib) {
			auto supports = Resolve<PFN_vkGetPhysicalDeviceXlibPresentationSupportKHR>(
				instance, "vkGetPhysicalDeviceXlibPresentationSupportKHR");
			return supports && supports(physical, family, static_cast<Display*>(probe.display),
										static_cast<VisualID>(reinterpret_cast<std::uintptr_t>(probe.window)));
		}
		if (probe.system == NativeWindowSystem::Wayland) {
			auto supports = Resolve<PFN_vkGetPhysicalDeviceWaylandPresentationSupportKHR>(
				instance, "vkGetPhysicalDeviceWaylandPresentationSupportKHR");
			return supports && supports(physical, family, static_cast<wl_display*>(probe.display));
		}
		return true;
	}

	VkResult CreateSurface(VkInstance instance, const NativeWindowHandle& window, VkSurfaceKHR* outSurface) {
		if (window.system == NativeWindowSystem::Xlib) {
			auto createSurface = Resolve<PFN_vkCreateXlibSurfaceKHR>(instance, "vkCreateXlibSurfaceKHR");
			if (!createSurface) {
				return VK_ERROR_EXTENSION_NOT_PRESENT;
			}
			VkXlibSurfaceCreateInfoKHR surfaceInfo{VK_STRUCTURE_TYPE_XLIB_SURFACE_CREATE_INFO_KHR};
			surfaceInfo.dpy = static_cast<Display*>(window.display);
			surfaceInfo.window = static_cast<::Window>(reinterpret_cast<std::uintptr_t>(window.window));
			return createSurface(instance, &surfaceInfo, nullptr, outSurface);
		}
		if (window.system == NativeWindowSystem::Wayland) {
			auto createSurface = Resolve<PFN_vkCreateWaylandSurfaceKHR>(instance, "vkCreateWaylandSurfaceKHR");
			if (!createSurface) {
				return VK_ERROR_EXTENSION_NOT_PRESENT;
			}
			VkWaylandSurfaceCreateInfoKHR surfaceInfo{VK_STRUCTURE_TYPE_WAYLAND_SURFACE_CREATE_INFO_KHR};
			surfaceInfo.display = static_cast<wl_display*>(window.display);
			surfaceInfo.surface = static_cast<wl_surface*>(window.window);
			return createSurface(instance, &surfaceInfo, nullptr, outSurface);
		}
		return VK_ERROR_EXTENSION_NOT_PRESENT;
	}

} // namespace Shalltear::RHI::VulkanPlatform
