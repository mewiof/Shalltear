#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear::RHI {

	class SwapChain;

	enum class PresentPolicy : u8 {
		Fifo,
		Mailbox,

		Count,
	};

	static_assert(static_cast<u8>(PresentPolicy::Count) == 2);

	inline constexpr PresentPolicy ChoosePresentPolicy(bool vsync, bool mailboxOffered) noexcept {
		return (!vsync && mailboxOffered) ? PresentPolicy::Mailbox : PresentPolicy::Fifo;
	}

	inline constexpr bool WaitsForDisplay(PresentPolicy policy, bool presentWaitSupported) noexcept {
		return policy == PresentPolicy::Fifo && presentWaitSupported;
	}

	inline constexpr f64 PRESENT_WAIT_TIMEOUT_REFRESHES = 4.0;
	inline constexpr f64 PRESENT_WAIT_TIMEOUT_MIN_SECONDS = 0.025;
	inline constexpr f64 PRESENT_WAIT_TIMEOUT_MAX_SECONDS = 0.1;
	inline constexpr f64 PRESENT_WAIT_RETRY_SECONDS = 1.0;
	inline constexpr f64 PRESENT_WAIT_RETRY_MAX_SECONDS = 60.0;

	inline constexpr f64 PresentWaitTimeout(f64 refreshSeconds) noexcept {
		const f64 timeout = refreshSeconds * PRESENT_WAIT_TIMEOUT_REFRESHES;
		return timeout < PRESENT_WAIT_TIMEOUT_MIN_SECONDS ? PRESENT_WAIT_TIMEOUT_MIN_SECONDS
														  : (timeout > PRESENT_WAIT_TIMEOUT_MAX_SECONDS ? PRESENT_WAIT_TIMEOUT_MAX_SECONDS : timeout);
	}

	inline constexpr f64 NextPresentWaitRetry(f64 retrySeconds) noexcept {
		const f64 doubled = retrySeconds * 2.0;
		return doubled > PRESENT_WAIT_RETRY_MAX_SECONDS ? PRESENT_WAIT_RETRY_MAX_SECONDS : doubled;
	}

	struct PresentQueue {
		u32 depth = 1;
		u32 late = 0;
		u32 early = 0;
	};

	inline constexpr f64 PRESENT_LATE_SLACK_SECONDS = 0.0035;
	inline constexpr f64 PRESENT_LATE_LATENCY = 1.5;
	inline constexpr u32 PRESENT_LATE_LIMIT = 6;
	inline constexpr f64 PRESENT_EARLY_SLACK_SECONDS = 0.0045;
	inline constexpr u32 PRESENT_EARLY_RUN = 120;
	inline constexpr f64 PRESENT_WORK_UNKNOWN = -1.0;

	inline constexpr PresentQueue AdvancePresentQueue(PresentQueue queue, f64 workSeconds, f64 latencySeconds, f64 refreshSeconds) noexcept {
		if (queue.depth <= 1) {
			const bool late = workSeconds > refreshSeconds - PRESENT_LATE_SLACK_SECONDS || latencySeconds > refreshSeconds * PRESENT_LATE_LATENCY;
			queue.late = late ? queue.late + 2 : (queue.late > 0 ? queue.late - 1 : 0);
			if (queue.late >= PRESENT_LATE_LIMIT) {
				queue = PresentQueue{2, 0, 0};
			}
			return queue;
		}
		const bool early = workSeconds >= 0.0 && workSeconds < refreshSeconds - PRESENT_EARLY_SLACK_SECONDS;
		queue.early = early ? queue.early + 1 : 0;
		if (queue.early >= PRESENT_EARLY_RUN) {
			queue = PresentQueue{1, 0, 0};
		}
		return queue;
	}

	struct FrameSync {
		u32 waitable = 0;
		u32 synced = 0;
	};

	FrameSync SyncFrame(std::span<SwapChain* const> presented, f64 refreshSeconds, u64 frameStartTicks);

} // namespace Shalltear::RHI

namespace Shalltear::GraphicsInternal {

	struct PresentCounts {
		u32 windows = 0;
		u32 vsyncWindows = 0;
		u32 displayWaitable = 0;
		u32 displaySynced = 0;
	};

	SHALLTEAR_CORE_API PresentCounts GetLastFramePresents();

} // namespace Shalltear::GraphicsInternal
