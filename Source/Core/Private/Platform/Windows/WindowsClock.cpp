#include "Platform/Clock.h"

#include "Platform/Windows/WindowsHeaders.h"

namespace Shalltear::Platform {

	u64 GetClockTicks() noexcept {
		LARGE_INTEGER ticks;
		::QueryPerformanceCounter(&ticks);
		return static_cast<u64>(ticks.QuadPart);
	}

	u64 GetClockTicksPerSecond() noexcept {
		LARGE_INTEGER frequency;
		::QueryPerformanceFrequency(&frequency);
		return static_cast<u64>(frequency.QuadPart);
	}

} // namespace Shalltear::Platform
