#include "Graphics/Vulkan/VulkanLoader.h"

#include "Graphics/Vulkan/VulkanPlatform.h"
#include "Platform/Library.h"

#include "Shalltear/Core/Log.h"

#include <mutex>

namespace Shalltear::RHI {

	namespace {

		struct LoaderState {
			void* module = nullptr;
			PFN_vkGetInstanceProcAddr getInstanceProcAddr = nullptr;
#define SLT_VK_DECLARE(name) PFN_##name name = nullptr;
			SLT_VK_GLOBAL_FUNCS(SLT_VK_DECLARE)
#undef SLT_VK_DECLARE
			bool attempted = false;
			bool loaded = false;
		};

		LoaderState& GetState() {
			static LoaderState state;
			return state;
		}

		std::once_flag _loadOnce;

	} // anonymous namespace

	bool VulkanLoader::EnsureLoaded() {
		std::call_once(_loadOnce, []() {
			LoaderState& s = GetState();
			s.attempted = true;

			const char* library = VulkanPlatform::GetLoaderLibraryName();
			s.module = Platform::OpenLibrary(library);
			if (!s.module) {
				SLT_LOG_INFO("Graphics", "{} not found", library);
				return;
			}

			s.getInstanceProcAddr = reinterpret_cast<PFN_vkGetInstanceProcAddr>(
				Platform::FindSymbol(s.module, "vkGetInstanceProcAddr"));
			if (!s.getInstanceProcAddr) {
				SLT_LOG_ERROR("Graphics", "{} is present but vkGetInstanceProcAddr is missing (loader is broken)", library);
				return;
			}

#define SLT_VK_LOAD(name)                                                                        \
	s.name = reinterpret_cast<PFN_##name>(s.getInstanceProcAddr(nullptr, #name));                \
	if (!s.name) {                                                                               \
		SLT_LOG_ERROR("Graphics", "Vulkan global function '{}' missing from the loader", #name); \
		return;                                                                                  \
	}
			SLT_VK_GLOBAL_FUNCS(SLT_VK_LOAD)
#undef SLT_VK_LOAD

			s.loaded = true;
		});
		return GetState().loaded;
	}

	PFN_vkGetInstanceProcAddr VulkanLoader::GetInstanceProcAddrFn() {
		return GetState().getInstanceProcAddr;
	}

#define SLT_VK_DEFINE_GETTER(name)          \
	PFN_##name VulkanLoader::Get_##name() { \
		return GetState().name;             \
	}
	SLT_VK_GLOBAL_FUNCS(SLT_VK_DEFINE_GETTER)
#undef SLT_VK_DEFINE_GETTER

	bool VulkanLoader::LoadInstanceTable(VkInstance instance, VulkanInstanceFunctions& out) {
		LoaderState& s = GetState();
		if (!s.loaded) {
			return false;
		}

		bool complete = true;
#define SLT_VK_LOAD(name)                                                            \
	out.name = reinterpret_cast<PFN_##name>(s.getInstanceProcAddr(instance, #name)); \
	if (!out.name) {                                                                 \
		SLT_LOG_ERROR("Graphics", "Vulkan instance function '{}' missing", #name);   \
		complete = false;                                                            \
	}
		SLT_VK_INSTANCE_FUNCS(SLT_VK_LOAD)
#undef SLT_VK_LOAD

#define SLT_VK_LOAD(name) out.name = s.getInstanceProcAddr(instance, #name);
		SLT_VK_INSTANCE_OPTIONAL_FUNCS(SLT_VK_LOAD)
#undef SLT_VK_LOAD

		return complete;
	}

	bool VulkanLoader::LoadDeviceTable(VkDevice device, PFN_vkGetDeviceProcAddr getDeviceProcAddr, VulkanDeviceFunctions& out) {
		if (!getDeviceProcAddr) {
			return false;
		}

		bool complete = true;
#define SLT_VK_LOAD(name)                                                        \
	out.name = reinterpret_cast<PFN_##name>(getDeviceProcAddr(device, #name));   \
	if (!out.name) {                                                             \
		SLT_LOG_ERROR("Graphics", "Vulkan device function '{}' missing", #name); \
		complete = false;                                                        \
	}
		SLT_VK_DEVICE_FUNCS(SLT_VK_LOAD)
#undef SLT_VK_LOAD

#define SLT_VK_LOAD(name) out.name = reinterpret_cast<PFN_##name>(getDeviceProcAddr(device, #name));
		SLT_VK_DEVICE_OPTIONAL_FUNCS(SLT_VK_LOAD)
#undef SLT_VK_LOAD

		return complete;
	}

} // namespace Shalltear::RHI
