#pragma once

#include "Shalltear/Core/Types.h"

#include <bit>

namespace Shalltear::RHI {

	inline constexpr u64 LOSS_WINDOW_MS = 60'000;
	inline constexpr u64 REBUILD_RETRY_MS = 1'000;
	inline constexpr u32 LOSS_HISTORY = 3;

	// 5 driver resets in 60 s = Windows bugcheck
	inline constexpr u64 RebuildDelayMs(u32 lossesInWindow) noexcept {
		if (lossesInWindow <= 1) {
			return 0;
		}
		return lossesInWindow == 2 ? 5'000 : LOSS_WINDOW_MS;
	}

	inline constexpr bool LogsFailedRebuild(u32 failedRebuilds) noexcept {
		return std::has_single_bit(failedRebuilds);
	}

	struct LossHistory {
		u64 atMs[LOSS_HISTORY] = {};
		u32 count = 0;

		constexpr u32 Note(u64 nowMs) noexcept {
			u32 inWindow = 1;
			for (u32 i = 0; i < count; ++i) {
				inWindow += nowMs - atMs[i] < LOSS_WINDOW_MS ? 1u : 0u;
			}
			for (u32 i = LOSS_HISTORY - 1; i > 0; --i) {
				atMs[i] = atMs[i - 1];
			}
			atMs[0] = nowMs;
			count = count < LOSS_HISTORY ? count + 1 : LOSS_HISTORY;
			return inWindow;
		}
	};

} // namespace Shalltear::RHI
