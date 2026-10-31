#include "Shalltear/Core/Memory.h"

#include <cstring>

namespace {

	void* StbMalloc(size_t size) {
		return size ? Shalltear::Allocate(size, Shalltear::MemTag::Asset) : nullptr;
	}

	void StbFree(void* p) {
		Shalltear::Free(p);
	}

	void* StbReallocSized(void* p, size_t oldSize, size_t newSize) {
		if (!p) {
			return StbMalloc(newSize);
		}
		if (newSize == 0) {
			StbFree(p);
			return nullptr;
		}
		void* replacement = StbMalloc(newSize);
		if (replacement) {
			std::memcpy(replacement, p, oldSize < newSize ? oldSize : newSize);
		}
		StbFree(p);
		return replacement;
	}

} // anonymous namespace

#define STBI_MALLOC(size) StbMalloc(size)
#define STBI_REALLOC_SIZED(p, oldSize, newSize) StbReallocSized(p, oldSize, newSize)
#define STBI_FREE(p) StbFree(p)
#define STBI_NO_STDIO
#define STB_IMAGE_IMPLEMENTATION

#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Weverything"
#endif

#include "Graphics/stb_image.h"

#if defined(__clang__)
#pragma clang diagnostic pop
#endif
