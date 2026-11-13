#include "Graphics/PresentPolicy.h"

#include "Graphics/Vulkan/VulkanInternal.h"
#include "Graphics/Vulkan/VulkanPlatform.h"

#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Time.h"

#include <algorithm>

namespace Shalltear::RHI {

	namespace {

		bool SurfaceWaitsForPresent(const SwapChainImpl& impl) {
			const DeviceImpl& d = *impl.device;
			if (d.presentWait != PresentWait::V2) {
				return d.presentWait == PresentWait::V1;
			}
			auto getCapabilities = reinterpret_cast<PFN_vkGetPhysicalDeviceSurfaceCapabilities2KHR>(d.fi.vkGetPhysicalDeviceSurfaceCapabilities2KHR);
			if (!getCapabilities) {
				return false;
			}
			VkSurfaceCapabilitiesPresentWait2KHR wait{VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_WAIT_2_KHR};
			VkSurfaceCapabilitiesPresentId2KHR id{VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_PRESENT_ID_2_KHR};
			id.pNext = &wait;
			VkSurfaceCapabilities2KHR capabilities{VK_STRUCTURE_TYPE_SURFACE_CAPABILITIES_2_KHR};
			capabilities.pNext = &id;
			VkPhysicalDeviceSurfaceInfo2KHR surface{VK_STRUCTURE_TYPE_PHYSICAL_DEVICE_SURFACE_INFO_2_KHR};
			surface.surface = impl.surface;
			return getCapabilities(d.physicalDevice, &surface, &capabilities) == VK_SUCCESS && id.presentId2Supported && wait.presentWait2Supported;
		}

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

			impl.presentWait = WaitsForDisplay(policy, SurfaceWaitsForPresent(impl));
			impl.presentId = 0;
			impl.presentWaitRetryTicks = 0;
			impl.presentWaitRetrySeconds = PRESENT_WAIT_RETRY_SECONDS;

			VkSwapchainCreateInfoKHR createInfo{VK_STRUCTURE_TYPE_SWAPCHAIN_CREATE_INFO_KHR};
			if (impl.presentWait && d.presentWait == PresentWait::V2) {
				createInfo.flags = VK_SWAPCHAIN_CREATE_PRESENT_ID_2_BIT_KHR | VK_SWAPCHAIN_CREATE_PRESENT_WAIT_2_BIT_KHR;
			}
			createInfo.surface = impl.surface;
			createInfo.minImageCount = imageCount;
			createInfo.imageFormat = chosen.format;
			createInfo.imageColorSpace = chosen.colorSpace;
			createInfo.imageExtent = extent;
			createInfo.imageArrayLayers = 1;
			createInfo.imageUsage = VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT |
									(caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
			impl.copyable = (caps.supportedUsageFlags & VK_IMAGE_USAGE_TRANSFER_SRC_BIT) != 0;
			createInfo.imageSharingMode = VK_SHARING_MODE_EXCLUSIVE;
			createInfo.preTransform = caps.currentTransform;
			createInfo.compositeAlpha = VK_COMPOSITE_ALPHA_OPAQUE_BIT_KHR;
			createInfo.presentMode = presentMode;
			createInfo.clipped = VK_TRUE;
			createInfo.oldSwapchain = oldChain;

			VkResult result = d.fd.vkCreateSwapchainKHR(d.device, &createInfo, nullptr, &impl.swapchain);
			if (result != VK_SUCCESS) {
				if (!d.NoteLoss(result, "vkCreateSwapchainKHR")) {
					SLT_LOG_ERROR("Graphics", "vkCreateSwapchainKHR failed: {}", VkResultName(result));
				}
				impl.swapchain = VK_NULL_HANDLE;
				impl.presentWait = false;
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
				SLT_VK_CHECK(d, d.fd.vkCreateImageView(d.device, &viewInfo, nullptr, &view));

				auto [index, generation] = d.textures.Acquire();
				TextureResource& resource = d.textures.slots[index].resource;
				resource.image = image;
				resource.view = view;
				resource.allocation = VK_NULL_HANDLE;
				resource.size = Vec2I{static_cast<i32>(extent.width), static_cast<i32>(extent.height)};
				resource.format = impl.format;
				resource.usage = TextureUsage::RenderTarget | TextureUsage::TransferDst |
								 (impl.copyable ? TextureUsage::TransferSrc : TextureUsage::None);
				resource.external = true;
				resource.state = ResourceState::Undefined;
				impl.imageHandles.push_back(TextureHandle{index, generation});

				VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
				VkSemaphore semaphore = VK_NULL_HANDLE;
				SLT_VK_CHECK(d, d.fd.vkCreateSemaphore(d.device, &semaphoreInfo, nullptr, &semaphore));
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

		bool CreateSurfaceObjects(SwapChainImpl& impl) {
			DeviceImpl& d = *impl.device;
			VkResult result = VulkanPlatform::CreateSurface(d.instance, impl.window, &impl.surface);
			if (result != VK_SUCCESS) {
				SLT_LOG_ERROR("Graphics", "CreateSwapChainForWindow: surface creation failed: {}", VkResultName(result));
				impl.surface = VK_NULL_HANDLE;
				return false;
			}

			VkBool32 supported = VK_FALSE;
			d.fi.vkGetPhysicalDeviceSurfaceSupportKHR(d.physicalDevice, d.queueFamily, impl.surface, &supported);
			if (!supported) {
				SLT_LOG_ERROR("Graphics", "surface doesn't support presentation on the graphics queue family");
				d.fi.vkDestroySurfaceKHR(d.instance, impl.surface, nullptr);
				impl.surface = VK_NULL_HANDLE;
				return false;
			}

			for (u32 i = 0; i < FRAMES_IN_FLIGHT; ++i) {
				VkSemaphoreCreateInfo semaphoreInfo{VK_STRUCTURE_TYPE_SEMAPHORE_CREATE_INFO};
				SLT_VK_CHECK(d, d.fd.vkCreateSemaphore(d.device, &semaphoreInfo, nullptr, &impl.acquireSemaphores[i]));
			}
			return true;
		}

	} // anonymous namespace

	void ReleaseSwapChainObjects(SwapChainImpl& impl) {
		DeviceImpl& d = *impl.device;
		impl.acquiredThisFrame = false;
		if (!d.device) {
			return;
		}
		DestroySwapChainObjects(impl);
		for (u32 i = 0; i < FRAMES_IN_FLIGHT; ++i) {
			if (impl.acquireSemaphores[i]) {
				d.fd.vkDestroySemaphore(d.device, impl.acquireSemaphores[i], nullptr);
				impl.acquireSemaphores[i] = VK_NULL_HANDLE;
			}
		}
	}

	void RestoreSwapChainObjects(SwapChainImpl& impl) {
		if (!CreateSurfaceObjects(impl) || !CreateSwapChainObjects(impl, impl.size, impl.vsync, VK_NULL_HANDLE)) {
			impl.needsRecreate = true;
		}
	}

	bool DeviceImpl::WaitForPresent(SwapChainImpl& swapChain, u64 presentId, f64 refreshSeconds) {
		const u64 timeoutNanoseconds = static_cast<u64>(PresentWaitTimeout(refreshSeconds) * 1.0e9);
		VkResult result = VK_SUCCESS;
		if (presentWait == PresentWait::V2) {
			VkPresentWait2InfoKHR info{VK_STRUCTURE_TYPE_PRESENT_WAIT_2_INFO_KHR};
			info.presentId = presentId;
			info.timeout = timeoutNanoseconds;
			result = fd.vkWaitForPresent2KHR(device, swapChain.swapchain, &info);
		} else {
			result = fd.vkWaitForPresentKHR(device, swapChain.swapchain, presentId, timeoutNanoseconds);
		}
		if (result == VK_SUCCESS) {
			swapChain.presentWaitRetrySeconds = PRESENT_WAIT_RETRY_SECONDS;
			return true;
		}
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR) {
			swapChain.needsRecreate = true;
			return false;
		}
		if (result != VK_TIMEOUT && !NoteLoss(result, "vkWaitForPresentKHR")) {
			SLT_LOG_WARN("Graphics", "vkWaitForPresentKHR failed: {}", VkResultName(result));
		}
		swapChain.presentWaitRetryTicks = Time::GetTicks() + static_cast<u64>(swapChain.presentWaitRetrySeconds * static_cast<f64>(Time::GetTicksPerSecond()));
		swapChain.presentWaitRetrySeconds = NextPresentWaitRetry(swapChain.presentWaitRetrySeconds);
		return false;
	}

	FrameSync DeviceImpl::SyncFrame(std::span<SwapChain* const> presented, f64 refreshSeconds, u64 frameStartTicks) {
		FrameSync sync;
		if (presented.empty()) {
			return sync;
		}
		DeviceImpl& d = *presented[0]->_impl->device;
		SLT_RHI_ASSERT_MAIN_THREAD(&d);
		SLT_CORE_ASSERT_MSG(!d.inFrame, "SyncFrame must be called after EndFrame");

		const f64 frequency = static_cast<f64>(Time::GetTicksPerSecond());
		const u64 now = Time::GetTicks();
		const auto waits = [now](const SwapChainImpl& s) { return s.presentWait && s.swapchain && now >= s.presentWaitRetryTicks; };

		bool anyWaits = false;
		for (SwapChain* swapChain : presented) {
			const SwapChainImpl& s = *swapChain->_impl;
			sync.waitable += s.presentWait ? 1 : 0;
			anyWaits = anyWaits || waits(s);
		}
		const u32 depth = anyWaits ? d.presentQueue.depth : 2;
		const u32 behind = depth - 1;

		if (d.timelineCounter > behind) {
			d.WaitForSubmission(d.timelineCounter - behind, UINT64_MAX);
		}
		for (SwapChain* swapChain : presented) {
			SwapChainImpl& s = *swapChain->_impl;
			if (!d.lost && waits(s) && s.presentId > behind && d.WaitForPresent(s, s.presentId - behind, refreshSeconds)) {
				++sync.synced;
			}
		}
		if (sync.synced > 0 && frameStartTicks != 0 && frameStartTicks <= now) {
			const f64 cpu = static_cast<f64>(now - frameStartTicks) / frequency;
			const f64 work = d.gpuFrameSeconds >= 0.0 ? cpu + d.gpuFrameSeconds : PRESENT_WORK_UNKNOWN;
			const f64 latency = depth <= 1 ? static_cast<f64>(Time::GetTicks() - frameStartTicks) / frequency : 0.0;
			d.presentQueue = AdvancePresentQueue(d.presentQueue, work, latency, refreshSeconds);
		}
		return sync;
	}

	FrameSync SyncFrame(std::span<SwapChain* const> presented, f64 refreshSeconds, u64 frameStartTicks) {
		return DeviceImpl::SyncFrame(presented, refreshSeconds, frameStartTicks);
	}

	SwapChain* Device::CreateSwapChainForWindow(const NativeSwapChainDesc& desc) {
		DeviceImpl* impl = _impl;
		SLT_RHI_ASSERT_MAIN_THREAD(impl);
		if (!desc.window.window || !desc.window.display) {
			SLT_LOG_ERROR("Graphics", "CreateSwapChainForWindow: null native handles");
			return nullptr;
		}

		SwapChainImpl* swapImpl = New<SwapChainImpl>(MemTag::Graphics);
		swapImpl->device = impl;
		swapImpl->window = desc.window;
		swapImpl->size = desc.sizePx;
		swapImpl->vsync = desc.vsync;

		if (!impl->lost && !CreateSurfaceObjects(*swapImpl)) {
			Delete(swapImpl);
			return nullptr;
		}
		if (impl->lost || !CreateSwapChainObjects(*swapImpl, desc.sizePx, desc.vsync, VK_NULL_HANDLE)) {
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

		if (!impl->lost) {
			impl->NoteLoss(impl->fd.vkDeviceWaitIdle(impl->device), "vkDeviceWaitIdle");
		}
		ReleaseSwapChainObjects(*swapImpl);

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

		if (d.lost || !impl.surface) {
			return {};
		}
		if (impl.needsRecreate || !impl.swapchain) {
			if (d.NoteLoss(d.fd.vkDeviceWaitIdle(d.device), "vkDeviceWaitIdle") || !RebuildSwapChainObjects(impl, impl.size, impl.vsync)) {
				return {};
			}
		}

		VkSemaphore acquireSemaphore = impl.acquireSemaphores[d.frameIndex];
		u32 imageIndex = 0;
		VkResult result = d.fd.vkAcquireNextImageKHR(d.device, impl.swapchain, UINT64_MAX, acquireSemaphore, VK_NULL_HANDLE, &imageIndex);
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_ERROR_SURFACE_LOST_KHR) {
			if (d.NoteLoss(d.fd.vkDeviceWaitIdle(d.device), "vkDeviceWaitIdle") || !RebuildSwapChainObjects(impl, impl.size, impl.vsync)) {
				return {};
			}
			result = d.fd.vkAcquireNextImageKHR(d.device, impl.swapchain, UINT64_MAX, acquireSemaphore, VK_NULL_HANDLE, &imageIndex);
		}
		if (result != VK_SUCCESS && result != VK_SUBOPTIMAL_KHR) {
			if (!d.NoteLoss(result, "vkAcquireNextImageKHR")) {
				SLT_LOG_WARN("Graphics", "vkAcquireNextImageKHR failed: {}", VkResultName(result));
				impl.needsRecreate = true;
			}
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
			resource->awaitsAcquire = true;
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
		if (d.lost) {
			return;
		}

		VkSemaphore waitSemaphore = impl.renderFinished[impl.currentImageIndex];
		VkPresentInfoKHR presentInfo{VK_STRUCTURE_TYPE_PRESENT_INFO_KHR};
		presentInfo.waitSemaphoreCount = 1;
		presentInfo.pWaitSemaphores = &waitSemaphore;
		presentInfo.swapchainCount = 1;
		presentInfo.pSwapchains = &impl.swapchain;
		presentInfo.pImageIndices = &impl.currentImageIndex;

		const u64 presentId = impl.presentId + 1;
		VkPresentIdKHR id{VK_STRUCTURE_TYPE_PRESENT_ID_KHR};
		VkPresentId2KHR id2{VK_STRUCTURE_TYPE_PRESENT_ID_2_KHR};
		if (impl.presentWait && d.presentWait == PresentWait::V2) {
			id2.swapchainCount = 1;
			id2.pPresentIds = &presentId;
			presentInfo.pNext = &id2;
		} else if (impl.presentWait) {
			id.swapchainCount = 1;
			id.pPresentIds = &presentId;
			presentInfo.pNext = &id;
		}

		VkResult result = d.fd.vkQueuePresentKHR(d.queue, &presentInfo);
		if (impl.presentWait && (result == VK_SUCCESS || result == VK_SUBOPTIMAL_KHR)) {
			impl.presentId = presentId;
		}
		if (result == VK_ERROR_OUT_OF_DATE_KHR || result == VK_SUBOPTIMAL_KHR || result == VK_ERROR_SURFACE_LOST_KHR) {
			impl.needsRecreate = true;
		} else if (result != VK_SUCCESS && !d.NoteLoss(result, "vkQueuePresentKHR")) {
			SLT_LOG_WARN("Graphics", "vkQueuePresentKHR failed: {}", VkResultName(result));
		}
	}

	void SwapChain::Recreate(Vec2I newSizePx, bool vsync) {
		SwapChainImpl& impl = *_impl;
		DeviceImpl& d = *impl.device;
		SLT_RHI_ASSERT_MAIN_THREAD(&d);
		SLT_CORE_ASSERT_MSG(!d.inFrame, "Recreate mid-frame is illegal");

		impl.size = newSizePx;
		if (d.lost || d.NoteLoss(d.fd.vkDeviceWaitIdle(d.device), "vkDeviceWaitIdle") || (!impl.surface && !CreateSurfaceObjects(impl)) ||
			!RebuildSwapChainObjects(impl, newSizePx, vsync)) {
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

	bool SwapChain::GetVSync() const {
		return _impl->vsync;
	}

} // namespace Shalltear::RHI
