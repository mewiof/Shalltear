#pragma once

#include "TestsPCH.h"

#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <vector>

namespace Fuzz {

	using namespace Shalltear;

	class Rng {
	public:
		explicit Rng(u64 seed) : _state(seed ? seed : 0x9E3779B97F4A7C15ull) {}

		u64 Next() {
			_state ^= _state >> 12;
			_state ^= _state << 25;
			_state ^= _state >> 27;
			return _state * 0x2545F4914F6CDD1Dull;
		}

		u32 Below(u32 bound) { return bound ? static_cast<u32>(Next() % bound) : 0; }
		u8 Byte() { return static_cast<u8>(Next() >> 33); }
		bool OneIn(u32 odds) { return Below(odds) == 0; }
	private:
		u64 _state;
	};

	class GuardedInput {
	public:
		explicit GuardedInput(size_t minimumBytes = 0) {
			SYSTEM_INFO info{};
			GetSystemInfo(&info);
			size_t pageSize = info.dwPageSize;
			size_t pages = (minimumBytes + pageSize - 1) / pageSize;
			_committed = (pages == 0 ? 1 : pages) * pageSize;

			_base = static_cast<u8*>(VirtualAlloc(nullptr, _committed + pageSize, MEM_RESERVE, PAGE_NOACCESS));
			if (_base) {
				VirtualAlloc(_base, _committed, MEM_COMMIT, PAGE_READWRITE);
			}
		}

		~GuardedInput() {
			if (_base) {
				VirtualFree(_base, 0, MEM_RELEASE);
			}
		}

		GuardedInput(const GuardedInput&) = delete;
		GuardedInput& operator=(const GuardedInput&) = delete;

		bool IsValid() const { return _base != nullptr; }
		size_t Capacity() const { return _committed; }

		const u8* Place(const void* bytes, size_t n, size_t alignment = 1) {
			SLT_APP_ASSERT_MSG(n <= _committed, "fuzz input exceeds the guarded region");
			size_t at = _committed - n;
			if (alignment > 1) {
				at -= at % alignment;
			}
			u8* placed = _base + at;
			if (n > 0) {
				std::memcpy(placed, bytes, n);
			}
			return placed;
		}

		std::string_view PlaceText(const std::vector<u8>& bytes) {
			const u8* at = Place(bytes.data(), bytes.size());
			return std::string_view(reinterpret_cast<const char*>(at), bytes.size());
		}
	private:
		u8* _base = nullptr;
		size_t _committed = 0;
	};

	class LentBuffer {
	public:
		LentBuffer(const u8* data, size_t size) {
			_buffer.data = const_cast<u8*>(data);
			_buffer.size = size;
		}

		~LentBuffer() {
			_buffer.data = nullptr;
			_buffer.size = 0;
		}

		LentBuffer(const LentBuffer&) = delete;
		LentBuffer& operator=(const LentBuffer&) = delete;

		const Buffer& Get() const { return _buffer; }
	private:
		Buffer _buffer;
	};

} // namespace Fuzz
