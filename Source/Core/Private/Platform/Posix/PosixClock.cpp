#include "Platform/Clock.h"

#include <time.h>

namespace Shalltear::Platform {

	u64 GetClockTicks() noexcept {
		timespec now{};
		::clock_gettime(CLOCK_MONOTONIC, &now);
		return static_cast<u64>(now.tv_sec) * 1000000000ull + static_cast<u64>(now.tv_nsec);
	}

	u64 GetClockTicksPerSecond() noexcept {
		return 1000000000ull;
	}

} // namespace Shalltear::Platform
