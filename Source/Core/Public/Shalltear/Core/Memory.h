#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Types.h"

#include <new>
#include <utility>
#include <memory>

namespace Shalltear {

	enum class MemTag : u8 {
		Unknown = 0,

		Core,
		Editor,

		Graphics,
		UI,
		Physics,
		Audio,
		Scripting,
		Game,
		Scene,

		Platform,

		String,
		Container,
		Asset,

		Debug,

		Count
	};

	// alignment must be a power of 2
	inline void* AlignForward(void* ptr, size_t alignment) {
		uintptr_t p = reinterpret_cast<uintptr_t>(ptr);
		uintptr_t a = static_cast<uintptr_t>(alignment);
		uintptr_t aligned = (p + a - 1) & ~(a - 1);
		return reinterpret_cast<void*>(aligned);
	}

	inline constexpr bool IsPowerOfTwo(size_t x) {
		return x > 0 && (x & (x - 1)) == 0;
	}

	class SHALLTEAR_CORE_API MemoryManager {
	public:
		static void TrackAlloc(MemTag tag, size_t bytes) noexcept;
		static void TrackFree(MemTag tag, size_t bytes) noexcept;

		static size_t GetBytesForTag(MemTag tag) noexcept;
		static size_t GetAllocationCountForTag(MemTag tag) noexcept;

		static size_t GetTotalBytes() noexcept;
		static size_t GetTotalAllocations() noexcept;

		static const char* GetTagName(MemTag tag) noexcept;

		using VramProviderFn = bool (*)(u64& outUsedBytes, u64& outBudgetBytes);
		static void SetVramProvider(VramProviderFn provider) noexcept;

		static void LogReport();
	};

	SHALLTEAR_CORE_API void* Allocate(size_t size, MemTag tag, size_t alignment = 16);

	SHALLTEAR_CORE_API void Free(void* p) noexcept;

	template <typename T, typename... Args>
	T* New(MemTag tag, Args&&... args) {
		void* memory = Allocate(sizeof(T), tag, alignof(T));
		return new (memory) T(std::forward<Args>(args)...);
	}

	template <typename T>
	void Delete(T* p) noexcept {
		if (p) {
			p->~T();
			Free(p);
		}
	}

	class SHALLTEAR_CORE_API LinearAllocator {
	public:
		LinearAllocator(size_t capacity, MemTag tag = MemTag::Unknown);
		~LinearAllocator();

		LinearAllocator(const LinearAllocator&) = delete;
		LinearAllocator& operator=(const LinearAllocator&) = delete;
		LinearAllocator(LinearAllocator&&) = delete;
		LinearAllocator& operator=(LinearAllocator&&) = delete;

		void* Allocate(size_t size, size_t alignment = 16);

		template <typename T>
		T* AllocateAs(size_t count = 1) {
			return static_cast<T*>(Allocate(sizeof(T) * count, alignof(T)));
		}

		// destructor is never called
		template <typename T, typename... Args>
		T* Construct(Args&&... args) {
			static_assert(std::is_trivially_destructible_v<T>, "LinearAllocator only supports trivially destructible types");
			void* mem = Allocate(sizeof(T), alignof(T));
			return mem ? new (mem) T(std::forward<Args>(args)...) : nullptr;
		}

		void Reset() noexcept;

		size_t GetUsedBytes() const noexcept { return _used; }
		size_t GetCapacity() const noexcept { return _capacity; }
		size_t GetRemainingBytes() const noexcept { return _capacity - _used; }
		MemTag GetTag() const noexcept { return _tag; }
	private:
		u8* _buffer;
		size_t _capacity;
		size_t _used;
		MemTag _tag;
	};

	template <typename T>
	using SharedPtr = std::shared_ptr<T>;

	template <typename T>
	using UniquePtr = std::unique_ptr<T>;

	template <typename T>
	using WeakPtr = std::weak_ptr<T>;

	template <typename T, typename... Args>
	SharedPtr<T> MakeShared(Args&&... args) {
		return std::make_shared<T>(std::forward<Args>(args)...);
	}

	template <typename T, typename... Args>
	UniquePtr<T> MakeUnique(Args&&... args) {
		return std::make_unique<T>(std::forward<Args>(args)...);
	}

} //namespace Shalltear
