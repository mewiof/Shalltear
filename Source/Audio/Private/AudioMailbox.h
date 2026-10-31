#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Core/Types.h"

#include <atomic>
#include <type_traits>

namespace Shalltear {

	namespace AudioImpl {

		template <typename T>
		class AudioMailbox {
			static_assert(std::is_trivially_copyable_v<T> && std::is_trivially_destructible_v<T>,
						  "value crosses threads as bytes. It may own nothing, free nothing, run no destructor");
		public:
			void Post(const T& value) [[clang::nonblocking]] {
				_slots[_back] = value;
				const u32 previous = _middle.exchange(_back | FRESH, std::memory_order_acq_rel);
				_back = previous & INDEX;
			}

			bool Take(T& out) [[clang::nonblocking]] {
				if ((_middle.load(std::memory_order_acquire) & FRESH) == 0) {
					return false;
				}
				const u32 previous = _middle.exchange(_front, std::memory_order_acq_rel);
				_front = previous & INDEX;
				out = _slots[_front];
				return true;
			}
		private:
			static constexpr u32 INDEX = 3u;
			static constexpr u32 FRESH = 4u;

			T _slots[3]{};
			// each touched by one thread only
			u32 _back = 0;
			u32 _front = 1;
			std::atomic<u32> _middle{2};
		};

	} // namespace AudioImpl

} // namespace Shalltear
