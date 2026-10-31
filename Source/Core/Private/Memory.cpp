#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Assert.h"
#include "Shalltear/Core/Log.h"

#include "Module/EngineStateTable.h"

namespace Shalltear {

	namespace {

		// before each pointer (Free needs no size/tag)
		struct alignas(16) AllocationHeader {
			size_t size;
			u32 magic;
			MemTag tag;
			u8 offset;
			u8 _reserved[2];

			static constexpr u32 MAGIC = 0xA110C8ED;
		};
		static_assert(sizeof(AllocationHeader) == 16, "header must stay 16 bytes to preserve 16-byte user alignment");
		static_assert(alignof(AllocationHeader) == 16, "header alignment must match its size");

		inline AllocationHeader* HeaderOf(void* userPtr) noexcept {
			return reinterpret_cast<AllocationHeader*>(
				reinterpret_cast<uintptr_t>(userPtr) - sizeof(AllocationHeader));
		}

	} // anonymous namespace

	namespace {

		constexpr size_t TAG_COUNT = static_cast<size_t>(MemTag::Count);

		struct MemoryManagerData {
			std::atomic<size_t> bytesPerTag[TAG_COUNT]{};
			std::atomic<size_t> countPerTag[TAG_COUNT]{};
			std::atomic<MemoryManager::VramProviderFn> vramProvider{nullptr};
		};

		MemoryManagerData* _boundData = nullptr;

		MemoryManagerData& GetData() noexcept {
			if (_boundData)
				return *_boundData;
			static MemoryManagerData data;
			return data;
		}

		inline size_t TagIndex(MemTag tag) noexcept {
			size_t idx = static_cast<size_t>(tag);
			return (idx < TAG_COUNT) ? idx : static_cast<size_t>(MemTag::Unknown);
		}

		MemoryManager::VramProviderFn GetVramProvider() noexcept {
			return GetData().vramProvider.load(std::memory_order_relaxed);
		}

		str FormatBytes(size_t bytes) {
			constexpr double KB = 1024.0;
			constexpr double MB = 1024.0 * 1024.0;
			constexpr double GB = 1024.0 * 1024.0 * 1024.0;

			double value = static_cast<double>(bytes);
			if (value >= GB)
				return std::format("{:.2f} GB", value / GB);
			if (value >= MB)
				return std::format("{:.2f} MB", value / MB);
			if (value >= KB)
				return std::format("{:.2f} KB", value / KB);
			return std::format("{} B", bytes);
		}

	} // anonymous namespace

	void MemoryManager::TrackAlloc(MemTag tag, size_t bytes) noexcept {
		auto& data = GetData();
		size_t idx = TagIndex(tag);

		data.bytesPerTag[idx].fetch_add(bytes, std::memory_order_relaxed);
		data.countPerTag[idx].fetch_add(1, std::memory_order_relaxed);
	}

	void MemoryManager::TrackFree(MemTag tag, size_t bytes) noexcept {
		auto& data = GetData();
		size_t idx = TagIndex(tag);

		data.bytesPerTag[idx].fetch_sub(bytes, std::memory_order_relaxed);
		data.countPerTag[idx].fetch_sub(1, std::memory_order_relaxed);
	}

	size_t MemoryManager::GetBytesForTag(MemTag tag) noexcept {
		return GetData().bytesPerTag[TagIndex(tag)].load(std::memory_order_relaxed);
	}

	size_t MemoryManager::GetAllocationCountForTag(MemTag tag) noexcept {
		return GetData().countPerTag[TagIndex(tag)].load(std::memory_order_relaxed);
	}

	size_t MemoryManager::GetTotalBytes() noexcept {
		auto& data = GetData();
		size_t total = 0;
		for (size_t i = 0; i < TAG_COUNT; ++i)
			total += data.bytesPerTag[i].load(std::memory_order_relaxed);
		return total;
	}

	size_t MemoryManager::GetTotalAllocations() noexcept {
		auto& data = GetData();
		size_t total = 0;
		for (size_t i = 0; i < TAG_COUNT; ++i)
			total += data.countPerTag[i].load(std::memory_order_relaxed);
		return total;
	}

	void MemoryManager::SetVramProvider(VramProviderFn provider) noexcept {
		GetData().vramProvider.store(provider, std::memory_order_relaxed);
	}

	const char* MemoryManager::GetTagName(MemTag tag) noexcept {
		switch (tag) {
		case MemTag::Unknown:
			return "Unknown";
		case MemTag::Core:
			return "Core";
		case MemTag::Editor:
			return "Editor";
		case MemTag::Graphics:
			return "Graphics";
		case MemTag::UI:
			return "UI";
		case MemTag::Physics:
			return "Physics";
		case MemTag::Audio:
			return "Audio";
		case MemTag::Scripting:
			return "Scripting";
		case MemTag::Game:
			return "Game";
		case MemTag::Scene:
			return "Scene";
		case MemTag::Platform:
			return "Platform";
		case MemTag::String:
			return "String";
		case MemTag::Container:
			return "Container";
		case MemTag::Asset:
			return "Asset";
		case MemTag::Debug:
			return "Debug";
		default:
			return "Invalid";
		}
	}

	void MemoryManager::LogReport() {
		auto& data = GetData();

		std::ostringstream oss;
		oss << "\n";
		oss << "================ MEMORY REPORT ================\n";

		for (size_t i = 0; i < TAG_COUNT; ++i) {
			size_t bytes = data.bytesPerTag[i].load(std::memory_order_relaxed);
			size_t count = data.countPerTag[i].load(std::memory_order_relaxed);

			if (bytes == 0 && count == 0)
				continue;

			MemTag tag = static_cast<MemTag>(i);
			oss << std::format("  {:<12}: {:>10}  ({} alloc(s))\n",
							   GetTagName(tag), FormatBytes(bytes), count);
		}

		oss << "-----------------------------------------------\n";
		oss << std::format("  {:<12}: {:>10}  ({} alloc(s))\n",
						   "TOTAL", FormatBytes(GetTotalBytes()), GetTotalAllocations());

		if (VramProviderFn provider = GetVramProvider()) {
			u64 used = 0;
			u64 budget = 0;
			if (provider(used, budget)) {
				oss << std::format("  {:<12}: {:>10}  (budget {})\n",
								   "VRAM", FormatBytes(used), FormatBytes(budget));
			}
		}

		oss << "===============================================";

		SLT_LOG_INFO("Memory", "{}", oss.str());
	}

	void* Allocate(size_t size, MemTag tag, size_t alignment) {
		SLT_CORE_ASSERT_MSG(IsPowerOfTwo(alignment), "alignment must be a power of 2 (got {})", alignment);
		SLT_CORE_ASSERT_MSG(alignment <= 256, "alignment > 256 isn't supported by the tracked heap. Use LinearAllocator");

		constexpr size_t HEADER = sizeof(AllocationHeader);

		const size_t total = size + HEADER + (alignment - 1);
		void* raw = ::operator new(total);

		uintptr_t rawInt = reinterpret_cast<uintptr_t>(raw);
		uintptr_t userInt = (rawInt + HEADER + alignment - 1) & ~(alignment - 1);

		AllocationHeader* hdr = reinterpret_cast<AllocationHeader*>(userInt - HEADER);
		hdr->size = size;
		hdr->magic = AllocationHeader::MAGIC;
		hdr->tag = tag;

		uintptr_t offsetToHeader = reinterpret_cast<uintptr_t>(hdr) - rawInt;
		hdr->offset = static_cast<u8>(offsetToHeader);
		hdr->_reserved[0] = 0;
		hdr->_reserved[1] = 0;

		MemoryManager::TrackAlloc(tag, size);

		return reinterpret_cast<void*>(userInt);
	}

	void Free(void* p) noexcept {
		if (!p)
			return;

		AllocationHeader* hdr = HeaderOf(p);

		SLT_CORE_ASSERT_MSG(hdr->magic == AllocationHeader::MAGIC,
							"Free() called on pointer not owned by Shalltear::Allocate (possible double-free or foreign pointer)");

		MemoryManager::TrackFree(hdr->tag, hdr->size);

		hdr->magic = 0;

		void* raw = reinterpret_cast<void*>(reinterpret_cast<uintptr_t>(hdr) - hdr->offset);
		::operator delete(raw);
	}

	LinearAllocator::LinearAllocator(size_t capacity, MemTag tag)
		: _buffer(nullptr), _capacity(capacity), _used(0), _tag(tag) {
		SLT_CORE_ASSERT_MSG(capacity > 0, "LinearAllocator capacity must be > 0");

		_buffer = static_cast<u8*>(Shalltear::Allocate(capacity, tag, 16));
	}

	LinearAllocator::~LinearAllocator() {
		if (_buffer) {
			Shalltear::Free(_buffer);
			_buffer = nullptr;
		}
	}

	void* LinearAllocator::Allocate(size_t size, size_t alignment) {
		SLT_CORE_ASSERT_MSG(IsPowerOfTwo(alignment), "alignment must be a power of 2 (got {})", alignment);
		SLT_CORE_ASSERT_MSG(_buffer != nullptr, "LinearAllocator used before construction or after destruction");

		u8* cursor = _buffer + _used;
		u8* aligned = static_cast<u8*>(AlignForward(cursor, alignment));

		size_t newUsed = static_cast<size_t>(aligned - _buffer) + size;
		if (newUsed > _capacity) {
			SLT_LOG_ERROR("Memory",
						  "LinearAllocator (tag={}) exhausted: requested {} bytes (aligned to {}), only {} of {} remain",
						  MemoryManager::GetTagName(_tag), size, alignment,
						  _capacity - _used, _capacity);
			SLT_CORE_ASSERT_MSG(false, "LinearAllocator exhausted. Increase its capacity");
			return nullptr;
		}

		_used = newUsed;
		return aligned;
	}

	void LinearAllocator::Reset() noexcept {
		_used = 0;
	}

	namespace ModuleInternal {

		void CaptureMemoryEngineState(EngineStateTable& table) {
			table.memoryManager = &GetData();
		}

		void BindMemoryEngineState(const EngineStateTable& table) {
			_boundData = static_cast<MemoryManagerData*>(table.memoryManager);
		}

	} // namespace ModuleInternal

} //namespace Shalltear
