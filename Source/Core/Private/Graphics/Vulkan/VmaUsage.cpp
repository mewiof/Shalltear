#include "Shalltear/Core/Platform.h"

#if defined(SLT_PLATFORM_WINDOWS)

#define VK_NO_PROTOTYPES
#include <vulkan/vulkan.h>

#define VMA_IMPLEMENTATION
#define VMA_STATIC_VULKAN_FUNCTIONS 0
#define VMA_DYNAMIC_VULKAN_FUNCTIONS 1

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include <vk_mem_alloc.h>

#if defined(__clang__)
#pragma clang diagnostic pop
#endif

#endif
