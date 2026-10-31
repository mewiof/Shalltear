#include "TestsPCH.h"

#include "ThreadAllocationWatch.h"

#ifndef SHALLTEAR_SHARED

#include <atomic>
#include <cstdlib>
#include <malloc.h>
#include <new>

namespace {

	std::atomic<std::thread::id> _watched{};
	std::atomic<Shalltear::u64> _count{0};

	void Note() noexcept {
		const std::thread::id watched = _watched.load(std::memory_order_relaxed);
		if (watched != std::thread::id{} && watched == std::this_thread::get_id()) {
			_count.fetch_add(1, std::memory_order_relaxed);
		}
	}

	void* AllocateOrThrow(std::size_t size) {
		Note();
		if (size == 0) {
			size = 1;
		}
		for (;;) {
			if (void* block = std::malloc(size)) {
				return block;
			}
			std::new_handler handler = std::get_new_handler();
			if (!handler) {
				throw std::bad_alloc();
			}
			handler();
		}
	}

	void* AllocateAlignedOrThrow(std::size_t size, std::align_val_t alignment) {
		Note();
		if (size == 0) {
			size = 1;
		}
		for (;;) {
			if (void* block = _aligned_malloc(size, static_cast<std::size_t>(alignment))) {
				return block;
			}
			std::new_handler handler = std::get_new_handler();
			if (!handler) {
				throw std::bad_alloc();
			}
			handler();
		}
	}

} // namespace

namespace ThreadAllocationWatch {

	void Watch(std::thread::id thread) {
		_count.store(0, std::memory_order_relaxed);
		_watched.store(thread, std::memory_order_release);
	}

	void Stop() {
		_watched.store(std::thread::id{}, std::memory_order_release);
	}

	Shalltear::u64 GetCount() {
		return _count.load(std::memory_order_acquire);
	}

} // namespace ThreadAllocationWatch

void* operator new(std::size_t size) { return AllocateOrThrow(size); }
void* operator new[](std::size_t size) { return AllocateOrThrow(size); }

void* operator new(std::size_t size, const std::nothrow_t&) noexcept {
	try {
		return AllocateOrThrow(size);
	} catch (...) {
		return nullptr;
	}
}

void* operator new[](std::size_t size, const std::nothrow_t&) noexcept {
	try {
		return AllocateOrThrow(size);
	} catch (...) {
		return nullptr;
	}
}

void* operator new(std::size_t size, std::align_val_t alignment) { return AllocateAlignedOrThrow(size, alignment); }
void* operator new[](std::size_t size, std::align_val_t alignment) { return AllocateAlignedOrThrow(size, alignment); }

void* operator new(std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
	try {
		return AllocateAlignedOrThrow(size, alignment);
	} catch (...) {
		return nullptr;
	}
}

void* operator new[](std::size_t size, std::align_val_t alignment, const std::nothrow_t&) noexcept {
	try {
		return AllocateAlignedOrThrow(size, alignment);
	} catch (...) {
		return nullptr;
	}
}

void operator delete(void* block) noexcept { std::free(block); }
void operator delete[](void* block) noexcept { std::free(block); }
void operator delete(void* block, std::size_t) noexcept { std::free(block); }
void operator delete[](void* block, std::size_t) noexcept { std::free(block); }
void operator delete(void* block, const std::nothrow_t&) noexcept { std::free(block); }
void operator delete[](void* block, const std::nothrow_t&) noexcept { std::free(block); }

void operator delete(void* block, std::align_val_t) noexcept { _aligned_free(block); }
void operator delete[](void* block, std::align_val_t) noexcept { _aligned_free(block); }
void operator delete(void* block, std::size_t, std::align_val_t) noexcept { _aligned_free(block); }
void operator delete[](void* block, std::size_t, std::align_val_t) noexcept { _aligned_free(block); }
void operator delete(void* block, std::align_val_t, const std::nothrow_t&) noexcept { _aligned_free(block); }
void operator delete[](void* block, std::align_val_t, const std::nothrow_t&) noexcept { _aligned_free(block); }

#endif
