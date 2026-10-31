#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear::Profiler {

	struct ZoneResult {
		const char* name;
		f64 milliseconds;
		f64 maxMilliseconds;
		u32 calls;
		u32 depth;
	};

	struct FrameResults {
		const ZoneResult* zones;
		u32 count;
		u32 droppedSites;
		u32 droppedDepth;
	};

	// copies name (never points into an image), same name -> same site
	SHALLTEAR_CORE_API u32 RegisterSite(const char* name) noexcept;

	SHALLTEAR_CORE_API void Enter(u32 site) noexcept;
	SHALLTEAR_CORE_API void Leave() noexcept;

	SHALLTEAR_CORE_API void NextFrame() noexcept;

	SHALLTEAR_CORE_API void SetEnabled(bool enabled) noexcept;

	SHALLTEAR_CORE_API bool IsEnabled() noexcept;

	SHALLTEAR_CORE_API FrameResults GetFrameResults() noexcept;

	class ScopedZone {
	public:
		explicit ScopedZone(u32 site) noexcept { Enter(site); }
		~ScopedZone() noexcept { Leave(); }

		ScopedZone(const ScopedZone&) = delete;
		ScopedZone& operator=(const ScopedZone&) = delete;
		ScopedZone(ScopedZone&&) = delete;
		ScopedZone& operator=(ScopedZone&&) = delete;
	};

} // namespace Shalltear::Profiler

#if defined(SHALLTEAR_CONFIG_RELEASE)
#define SLT_PROFILE_ZONE(name)
#else
#define _SLT_ZONE_CAT_IMPL(a, b) a##b
#define _SLT_ZONE_CAT(a, b) _SLT_ZONE_CAT_IMPL(a, b)
#define SLT_PROFILE_ZONE(name)                                                                                       \
	static const ::Shalltear::u32 _SLT_ZONE_CAT(sltZoneSite_, __LINE__) = ::Shalltear::Profiler::RegisterSite(name); \
	::Shalltear::Profiler::ScopedZone _SLT_ZONE_CAT(sltZone_, __LINE__)(_SLT_ZONE_CAT(sltZoneSite_, __LINE__))
#endif
