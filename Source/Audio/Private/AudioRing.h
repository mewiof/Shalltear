#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Types.h"

#include <atomic>
#include <cstring>
#include <type_traits>

namespace Shalltear {

	namespace AudioImpl {

		template <typename T>
		class AudioRing {
			static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>,
						  "message crosses threads as bytes. It may own nothing, free nothing, run no destructor");
		public:
			AudioRing() = default;
			~AudioRing() { Release(); }
			AudioRing(const AudioRing&) = delete;
			AudioRing& operator=(const AudioRing&) = delete;

			// main thread, neither end in use

			void Reserve(u32 capacity) {
				Release();
				u32 size = 1;
				while (size < capacity) {
					size <<= 1;
				}
				_slots = static_cast<T*>(Allocate(sizeof(T) * size, MemTag::Audio, alignof(T) < 16 ? 16 : alignof(T)));
				std::memset(static_cast<void*>(_slots), 0, sizeof(T) * size);
				_mask = size - 1;
				_head.store(0, std::memory_order_relaxed);
				_tail.store(0, std::memory_order_relaxed);
			}

			void Release() {
				if (_slots) {
					Free(_slots);
					_slots = nullptr;
				}
				_mask = 0;
				_head.store(0, std::memory_order_relaxed);
				_tail.store(0, std::memory_order_relaxed);
			}

			u32 GetCapacity() const { return _slots ? _mask + 1 : 0; }

			u32 CountApproximate() const {
				return _tail.load(std::memory_order_relaxed) - _head.load(std::memory_order_relaxed);
			}

			bool TryPush(const T& message) [[clang::nonblocking]] {
				if (!_slots) {
					return false;
				}
				const u32 tail = _tail.load(std::memory_order_relaxed);
				if (tail - _head.load(std::memory_order_acquire) > _mask) {
					return false;
				}
				_slots[tail & _mask] = message;
				_tail.store(tail + 1, std::memory_order_release);
				return true;
			}

			bool TryPushAll(const T* messages, u32 count) [[clang::nonblocking]] {
				if (!_slots || count > _mask + 1) {
					return false;
				}
				const u32 tail = _tail.load(std::memory_order_relaxed);
				if (tail + count - _head.load(std::memory_order_acquire) > _mask + 1) {
					return false;
				}
				for (u32 index = 0; index < count; ++index) {
					_slots[(tail + index) & _mask] = messages[index];
				}
				_tail.store(tail + count, std::memory_order_release);
				return true;
			}

			bool TryPop(T& out) [[clang::nonblocking]] {
				if (!_slots) {
					return false;
				}
				const u32 head = _head.load(std::memory_order_relaxed);
				if (head == _tail.load(std::memory_order_acquire)) {
					return false;
				}
				out = _slots[head & _mask];
				_head.store(head + 1, std::memory_order_release);
				return true;
			}
		private:
			T* _slots = nullptr;
			u32 _mask = 0;
			alignas(64) std::atomic<u32> _head{0};
			alignas(64) std::atomic<u32> _tail{0};
		};

	} // namespace AudioImpl

} // namespace Shalltear
