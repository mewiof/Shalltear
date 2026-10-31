#include "Shalltear/Core/Platform.h"

#if defined(SLT_PLATFORM_WINDOWS)

#include "Graphics/PresentPolicy.h"

#include "Graphics/Vulkan/VulkanInternal.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <vulkan/vulkan_win32.h>

#include "Shalltear/Core/Memory.h"

#include <algorithm>

namespace Shalltear::RHI {

	namespace {

		void ReleaseSwapChainImages(SwapChainImpl& impl) {
			DeviceImpl& d = *impl.device;

			for (TextureHandle handle : impl.imageHandles) {
				TextureResource* resource = d.textures.Resolve(handle);
				if (resource) {
					d.fd.vkDestroyImageView(d.device, resource->view, nullptr);
					*resource = TextureResource{};
					d.textures.Release(handle.index);
				}
			}
			impl.imageHandles.clear();

			for (VkSemaphore semaphore : impl.renderFinished) {
				d.fd.vkDestroySemaphore(d.device, semaphore, nullptr);
			}
			impl.renderFinished.clear();
		}

		// callers guarantee the GPU is idle
		void DestroySwapChainObjects(SwapChainImpl& impl) {
			DeviceImpl& d = *impl.device;

			ReleaseSwapChainImages(impl);

			if (impl.swapchain) {
				d.fd.vkDestroySwapchainKHR(d.device, impl.swapchain, nullptr);
				impl.swapchain = VK_NULL_HANDLE;
			}
			if (impl.surface) {
				d.fi.vkDestroySurfaceKHR(d.instance, impl.surface, nullptr);
				impl.surface = VK_NULL_HANDLE;
			}
		}

		bool CreateSwapChainObjects(SwapChainImpl& impl, Vec2I sizePx, bool vsync, VkSwapchainKHR oldChain) {
			DeviceImpl& d = *impl.device;

			VkSurfaceCapabilitiesKHR caps{};
			if (d.fi.vkGetPhysicalDeviceSurfaceCapabilitiesKHR(d.physicalDevice, impl.surface, &caps) != VK_SUCCESS) {
				return false;
			}

			VkExtent2D extent = caps.currentExtent;
			if (extent.width == 0xFFFFFFFF) {
				extent.width = std::clamp(static_cast<u32>(sizePx.x), caps.minImageExtent.width, caps.maxImageExtent.width);
				extent.height = std::clamp(static_cast<u32>(sizePx.y), caps.minImageExtent.height, caps.maxImageExtent.height);
			}
			if (extent.width == 0 || extent.height == 0) {
				return false;
			}

			// UNORM (not SRGB), blending happens in display space
			u32 formatCount = 0;
			d.fi.vkGetPhysicalDeviceSurfaceFormatsKHR(d.physicalDevice, impl.surface, &formatCount, nullptr);
			Vector<VkSurfaceFormatKHR> formats{GetMemoryResource(MemTag::Graphics)};
			formats.resize(formatCount);
			d.fi.vkGetPhysicalDeviceSurfaceFormatsKHR(d.physicalDevice, impl.surface, &formatCount, formats.data());

			VkSurfaceFormatKHR chosen = formats.empty() ? VkSurfaceFormatKHR{VK_FORMAT_B8G8R8A8_UNORM, VK_COLOR_SPACE_SRGB_NONLINEAR_KHR} : formats[0];
			for (const VkSurfaceFormatKHR& format : formats) {
				if (format.format == VK_FORMAT_B8G8R8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
					chosen = format;
					break;
				}
				if (format.format == VK_FORMAT_R8G8B8A8_UNORM && format.colorSpace == VK_COLOR_SPACE_SRGB_NONLINEAR_KHR) {
					chosen = format;
				}
			}
			switch (chosen.format) {
			case VK_FORMAT_B8G8R8A8_SRGB:
				impl.format = Format::BGRA8SRGB;
				break;
			case VK_FORMAT_R8G8B8A8_SRGB:
				impl.format = Format::RGBA8SRGB;
				break;
			case VK_FORMAT_B8G8R8A8_UNORM:
				impl.format = Format::BGRA8Unorm;
				break;
			default:
				impl.format = Format::RGBA8Unorm;
				break;
			}
			impl.vkFormat = chosen.format;

			bool mailboxOffered = false;
			if (!vsync) {
				u32 modeCount = 0;
				d.fi.vkGetPhysicalDeviceSurfacePresentModesKHR(d.physicalDevice, impl.surface, &modeCount, nullptr);
				Vector<VkPresentModeKHR> modes{GetMemoryResource(MemTag::Graphics)};
				modes.resize(modeCount);
				d.fi.vkGetPhysicalDeviceSurfacePresentModesKHR(d.physicalDevice, impl.surface, &modeCount, modes.data());
				for (VkPresentModeKHR mode : modes) {
					if (mode == VK_PRESENT_MODE_MAILBOX_KHR) {
						mailboxOffered = true;
						break;
					}
				}
			}
			const PresentPolicy policy = ChoosePresentPolicy(vsync, mailboxOffered);
			VkPresentModeKHR presentMode = policy == PresentPolicy::Mailbox ? VK_PRESENT_MODE_MAILBOX_KHR : VK_PRESENT_MODE_FIFO_KHR;

			u32 imageCount = std::max(caps.minImageCount + 1, 3u);
			if (caps.maxImageCount > 0) {
				imageCount = std::min(imageCount, caps.maxImageCount);
			}

			VkSwapchainCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
			createInfo.surface = impl.surface;
			createInfo.minImageCount = imageCount;
			createInfo.imageFormat = chosen.format;
			createInfo.imageColorSpace = chosen.colorSpace;
			createInfo.imageExtent = extent;
			createInfo.imageArrayLayers = 1;
			createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT;
			createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			createInfo.preTransform = caps.currentTransform;
			createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
			createInfo.presentMode = presentMode;
			createInfo.clipped = VK_TRUE;
			createInfo.oldSwapchain = oldChain;

			VkResult result = d.fd.vkCreateSwapchainKHR(d.device, &createInfo, nullptr, &impl.swapchain);
			if (result != VK_SUCCESS) {
				SLT_LOG_ERROR("Graphics", "vkCreateSwapchainKHR failed: {}", VkResultName(result));
				impl.swapchain = VK_NULL_HANDLE;
				return false;
			}

			u32 actualCount = 0;
			d.fd.vkGetSwapchainImagesKHR(d.device, impl.swapchain, &actualCount, nullptr);
			Vector<VkImage> images{GetMemoryResource(MemTag::Graphics)};
			images.resize(actualCount);
			d.fd.vkGetSwapchainImagesKHR(d.device, impl.swapchain, &actualCount, images.data());

			for (VkImage image : images) {
				VkImageViewCreateInfo viewInfo{VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO};
				viewInfo.image = image;
				viewInfo.viewType = VK_IMAGE_VIEW_TYPE_2D;
				viewInfo.format = chosen.format;
				viewInfo.subresourceRange.aspectMask = VK_IMAGE_ASPECT_COLOR_BIT;
				viewInfo.subresourceRange.levelCount = 1;
				viewInfo.subresourceRange.layerCount = 1;
				VkImageView view = VK_NULL_HANDLE;
				SLT_VK_CHECK(d.fd.vkCreateImageView(d.device, &viewInfo, nullptr, &view));

				auto [index, generation] = d.textures.Acquire();
				TextureResource& resource = d.textures.slots[index].resource;
				resource.image = image;
				resource.view = view;
				resource.allocation = VK_NULL_HANDLE;
				resource.size = Vec2I{static_cast<i32>(extent.width), static_cast<i32>(extent.height)};
				resource.format = impl.format;
				resource.usage = TextureUsage::RenderTarget | TextureUsage::TransferDst;
				resource.external = true;
				resource.state = ResourceState::Undefined;
				impl.imageHandles.push_back(TextureHandle{index, generation});

				VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
				VkSemaphore semaphore = VK_NULL_HANDLE;
				SLT_VK_CHECK(d.fd.vkCreateSemaphore(d.device, &semaphoreInfo, nullptr, &semaphore));
				impl.renderFinished.push_back(semaphore);
			}

			impl.size = Vec2I{static_cast<i32>(extent.width), static_cast<i32>(extent.height)};
			impl.vsync = vsync;
			impl.needsRecreate = false;
			return true;
		}

		bool RebuildSwapChainObjects(SwapChainImpl& impl, Vec2I sizePx, bool vsync) {
			DeviceImpl& d = *impl.device;

			ReleaseSwapChainImages(impl);
			VkSwapchainKHR retired = impl.swapchain;
			impl.swapchain = VK_NULL_HANDLE;

			bool created = CreateSwapChainObjects(impl, sizePx, vsync, retired);
			if (retired) {
				d.fd.vkDestroySwapchainKHR(d.device, retired, nullptr);
			}
			return created;
		}

	} // anonymous namespace

	SwapChain* Device::CreateSwapChainForWindow(const NativeSwapChainDesc& desc) {
		DeviceImpl* impl = _impl;
		SLT_RHI_ASSERT_MAIN_THREAD(impl);
		if (!desc.hwnd || !desc.hinstance) {
			SLT_LOG_ERROR("Graphics", "CreateSwapChainForWindow: null native handles");
			return nullptr;
		}
		auto createSurface = reinterpret_cast<PFN_vkCreateWin32SurfaceKHR>(impl->fi.vkCreateWin32SurfaceKHR);
		if (!createSurface) {
			SLT_LOG_ERROR("Graphics", "CreateSwapChainForWindow: VK_KHR_win32_surface not available");
			return nullptr;
		}

		SwapChainImpl* swapImpl = New<SwapChainImpl>(MemTag::Graphics);
		swapImpl->device = impl;

		VkWin32SurfaceCreateInfoKHR surfaceInfo{VK_STRUCTURE_TYPE_WIN32_SURFACE_CREATE_INFO_KHR};
		surfaceInfo.hinstance = static_cast<HINSTANCE>(desc.hinstance);
		surfaceInfo.hwnd = static_cast<HWND>(desc.hwnd);
		VkResult result = createSurface(impl->instance, &surfaceInfo, nullptr, &swapImpl->surface);
		if (result != VK_SUCCESS) {
			SLT_LOG_ERROR("Graphics", "vkCreateWin32SurfaceKHR failed: {}", VkResultName(result));
			Delete(swapImpl);
			return nullptr;
		}

		VkBool32 supported = VK_FALSE;
		impl->fi.vkGetPhysicalDeviceSurfaceSupportKHR(impl->physicalDevice, impl->queueFamily, swapImpl->surface, &supported);
		if (!supported) {
			SLT_LOG_ERROR("Graphics", "surface doesn't support presentation on the graphics queue family");
			impl->fi.vkDestroySurfaceKHR(impl->instance, swapImpl->surface, nullptr);
			Delete(swapImpl);
			return nullptr;
		}

		for (u32 i = 0; i < FRAMES_IN_FLIGHT; ++i) {
			VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
			SLT_VK_CHECK(impl->fd.vkCreateSemaphore(impl->device, &semaphoreInfo, nullptr, &swapImpl->acquireSemaphores[i]));
		}

		if (!CreateSwapChainObjects(*swapImpl, desc.sizePx, desc.vsync, VK_NULL_HANDLE)) {
			swapImpl->needsRecreate = true;
		}

		SwapChain* swapChain = New<SwapChain>(MemTag::Graphics);
		swapChain->_impl = swapImpl;
		impl->swapChains.push_back(swapChain);
		return swapChain;
	}

	void Device::DestroySwapChain(SwapChain* swapChain) {
		DeviceImpl* impl = _impl;
		SLT_RHI_ASSERT_MAIN_THREAD(impl);
		if (!swapChain) {
			return;
		}
		SwapChainImpl* swapImpl = swapChain->_impl;

		impl->fd.vkDeviceWaitIdle(impl->device);
		DestroySwapChainObjects(*swapImpl);
		for (u32 i = 0; i < FRAMES_IN_FLIGHT; ++i) {
			if (swapImpl->acquireSemaphores[i]) {
				impl->fd.vkDestroySemaphore(impl->device, swapImpl->acquireSemaphores[i], nullptr);
			}
		}

		auto it = std::find(impl->swapChains.begin(), impl->swapChains.end(), swapChain);
		if (it != impl->swapChains.end()) {
			impl->swapChains.erase(it);
		}
		Delete(swapImpl);
		Delete(swapChain);
	}

	TextureHandle SwapChain::AcquireNextImage() {
		SwapChainImpl& impl = *_impl;
		DeviceImpl& d = *impl.device;
		SLT_RHI_ASSERT_MAIN_THREAD(&d);
		SLT_CORE_ASSERT_MSG(d.inFrame, "AcquireNextImage outside BeginFrame/EndFrame");
		SLT_CORE_ASSERT_MSG(!impl.acquiredThisFrame, "AcquireNextImage called twice in one frame");

		if (impl.needsRecreate || !impl.swapchain) {
			d.fd.vkDeviceWaitIdle(d.device);
			if (!RebuildSwapChainObjects(impl, impl.size, impl.vsync)) {
				return {};
			}
		}

		VkSemaphore acquireSemaphore = impl.acquireSemaphores[d.frameIndex];
		u32 imageIndex = 0;
		VkResult result = d.fd.vkAcquireNextImageKHR(d.device, impl.swapchain, UINT64_MAX, acquireSemaphore, VK_NULL_HANDLE, &imageIndex);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR) {
			d.fd.vkDeviceWaitIdle(d.device);
			if (!RebuildSwapChainObjects(impl, impl.size, impl.vsync)) {
				return {};
			}
			result = d.fd.vkAcquireNextImageKHR(d.device, impl.swapchain, UINT64_MAX, acquireSemaphore, VK_NULL_HANDLE, &imageIndex);
		}
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
			SLT_LOG_WARN("Graphics", "vkAcquireNextImageKHR failed: {}", VkResultName(result));
			impl.needsRecreate = true;
			return {};
		}
		if (result == VK_SUBOPTIMAL_KHR) {
			impl.needsRecreate = true;
		}

		impl.currentImageIndex = imageIndex;
		impl.acquiredThisFrame = true;

		FrameContext& frame = d.frames[d.frameIndex];
		frame.waitSemaphores.push_back(acquireSemaphore);
		frame.signalSemaphores.push_back(impl.renderFinished[imageIndex]);

		TextureHandle handle = impl.imageHandles[imageIndex];
		TextureResource* resource = d.textures.Resolve(handle);
		if (resource) {
			resource->state = ResourceState::Undefined;
		}
		return handle;
	}

	void SwapChain::Present() {
		SwapChainImpl& impl = *_impl;
		DeviceImpl& d = *impl.device;
		SLT_RHI_ASSERT_MAIN_THREAD(&d);
		SLT_CORE_ASSERT_MSG(!d.inFrame, "Present must be called after EndFrame");
		if (!impl.acquiredThisFrame) {
			return;
		}
		impl.acquiredThisFrame = false;

		VkSemaphore waitSemaphore = impl.renderFinished[impl.currentImageIndex];
		VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &waitSemaphore;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &impl.swapchain;
		presentInfo.pImageIndices = &impl.currentImageIndex;

		VkResult result = d.fd.vkQueuePresentKHR(d.queue, &presentInfo);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_SURFACE_LOST_KHR) {
			impl.needsRecreate = true;
		} else if (result != VK_SUCCESS) {
			SLT_LOG_WARN("Graphics", "vkQueuePresentKHR failed: {}", VkResultName(result));
		}
	}

	void SwapChain::Recreate(Vec2I newSizePx, bool vsync) {
		SwapChainImpl& impl = *_impl;
		DeviceImpl& d = *impl.device;
		SLT_RHI_ASSERT_MAIN_THREAD(&d);
		SLT_CORE_ASSERT_MSG(!d.inFrame, "Recreate mid-frame is illegal");

		d.fd.vkDeviceWaitIdle(d.device);
		impl.size = newSizePx;
		if (!RebuildSwapChainObjects(impl, newSizePx, vsync)) {
			impl.vsync = vsync;
			impl.needsRecreate = true;
		}
	}

	Format SwapChain::GetFormat() const {
		return _impl->format;
	}

	Vec2I SwapChain::GetSize() const {
		return _impl->size;
	}

} // namespace Shalltear::RHI

#endif
