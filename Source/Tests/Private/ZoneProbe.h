#pragma once

#include "Shalltear/Core/Profiler.h"

#include <string_view>

namespace ZoneProbe {

	using namespace Shalltear;

	class ZoneScope {
	public:
		ZoneScope() {
			Profiler::SetEnabled(false);
			Profiler::NextFrame();
			Profiler::SetEnabled(true);
		}
		~ZoneScope() {
			Profiler::SetEnabled(false);
			Profiler::NextFrame();
		}

		ZoneScope(const ZoneScope&) = delete;
		ZoneScope& operator=(const ZoneScope&) = delete;
	};

	inline u32 ZoneCalls(std::string_view name) {
		Profiler::FrameResults frame = Profiler::GetFrameResults();
		for (u32 i = 0; i < frame.count; ++i) {
			if (frame.zones[i].name == name) {
				return frame.zones[i].calls;
			}
		}
		return 0;
	}

} // namespace ZoneProbe
