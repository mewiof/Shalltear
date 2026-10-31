#include "Shalltear/Core/Profiler.h"

#include "Shalltear/Core/Assert.h"
#include "Shalltear/Core/Time.h"

#include <cstring>
#include <thread>

namespace Shalltear::Profiler {

	namespace {

		constexpr u32 MAX_SITES = 128;
		constexpr u32 MAX_DEPTH = 32;
		constexpr u32 NAME_CAPACITY = 32;

		constexpr u32 INVALID_SITE = 0xFFFFFFFFu;

		struct Recorder {
			char names[MAX_SITES][NAME_CAPACITY] = {};

			f64 milliseconds[MAX_SITES] = {};
			f64 maxMilliseconds[MAX_SITES] = {};
			u32 calls[MAX_SITES] = {};
			u32 depths[MAX_SITES] = {};

			ZoneResult published[MAX_SITES] = {};
			u32 publishedCount = 0;
			u32 publishedDroppedDepth = 0;

			u32 stackSite[MAX_DEPTH] = {};
			u64 stackStart[MAX_DEPTH] = {};
			u32 stackDepth = 0;
			u32 overflowDepth = 0;

			u32 siteCount = 0;
			u32 droppedSites = 0;
			u32 droppedDepth = 0;
			bool enabled = false;

			std::thread::id owner;
			bool ownerKnown = false;
		};

		static_assert(sizeof(Recorder) == 11688, "Recorder's .bss size changed");

		Recorder& GetRecorder() {
			static Recorder recorder;
			return recorder;
		}

		bool IsOwnerThread(const Recorder& recorder) {
			return !recorder.ownerKnown || recorder.owner == std::this_thread::get_id();
		}

#define SLT_PROFILER_ASSERT_OWNER(recorder) \
	SLT_CORE_ASSERT_MSG(IsOwnerThread(recorder), "Profiler zones are main-thread-only")

		f64 MillisecondsBetween(u64 startTicks, u64 endTicks) {
			const u64 frequency = Time::GetTicksPerSecond();
			if (frequency == 0 || endTicks <= startTicks) {
				return 0.0;
			}
			return static_cast<f64>(endTicks - startTicks) * 1000.0 / static_cast<f64>(frequency);
		}

	} // anonymous namespace

	u32 RegisterSite(const char* name) noexcept {
		Recorder& recorder = GetRecorder();
		SLT_PROFILER_ASSERT_OWNER(recorder);

		const char* source = name ? name : "";
		std::size_t length = std::strlen(source);
		if (length > NAME_CAPACITY - 1) {
			length = NAME_CAPACITY - 1;
		}
		for (u32 existing = 0; existing < recorder.siteCount; ++existing) {
			if (std::strncmp(recorder.names[existing], source, length) == 0 && recorder.names[existing][length] == '\0') {
				return existing;
			}
		}

		if (recorder.siteCount >= MAX_SITES) {
			++recorder.droppedSites;
			return INVALID_SITE;
		}

		const u32 site = recorder.siteCount++;
		std::memcpy(recorder.names[site], source, length);
		recorder.names[site][length] = '\0';
		return site;
	}

	void Enter(u32 site) noexcept {
		Recorder& recorder = GetRecorder();
		if (!recorder.enabled) {
			return;
		}
		SLT_PROFILER_ASSERT_OWNER(recorder);

		if (recorder.stackDepth >= MAX_DEPTH) {
			++recorder.overflowDepth;
			++recorder.droppedDepth;
			return;
		}
		recorder.stackSite[recorder.stackDepth] = site;
		recorder.stackStart[recorder.stackDepth] = Time::GetTicks();
		++recorder.stackDepth;
	}

	void Leave() noexcept {
		Recorder& recorder = GetRecorder();
		if (!recorder.enabled) {
			return;
		}
		SLT_PROFILER_ASSERT_OWNER(recorder);

		if (recorder.overflowDepth > 0) {
			--recorder.overflowDepth;
			return;
		}
		if (recorder.stackDepth == 0) {
			return;
		}

		--recorder.stackDepth;
		const u32 site = recorder.stackSite[recorder.stackDepth];
		if (site >= MAX_SITES) {
			return;
		}

		const f64 elapsed = MillisecondsBetween(recorder.stackStart[recorder.stackDepth], Time::GetTicks());
		if (recorder.calls[site] == 0) {
			recorder.depths[site] = recorder.stackDepth;
		}
		++recorder.calls[site];
		recorder.milliseconds[site] += elapsed;
		if (elapsed > recorder.maxMilliseconds[site]) {
			recorder.maxMilliseconds[site] = elapsed;
		}
	}

	void NextFrame() noexcept {
		Recorder& recorder = GetRecorder();
		recorder.owner = std::this_thread::get_id();
		recorder.ownerKnown = true;

		for (u32 site = 0; site < recorder.siteCount; ++site) {
			recorder.published[site].name = recorder.names[site];
			recorder.published[site].milliseconds = recorder.milliseconds[site];
			recorder.published[site].maxMilliseconds = recorder.maxMilliseconds[site];
			recorder.published[site].calls = recorder.calls[site];
			recorder.published[site].depth = recorder.depths[site];

			recorder.milliseconds[site] = 0.0;
			recorder.maxMilliseconds[site] = 0.0;
			recorder.calls[site] = 0;
		}
		recorder.publishedCount = recorder.siteCount;
		recorder.publishedDroppedDepth = recorder.droppedDepth;

		recorder.stackDepth = 0;
		recorder.overflowDepth = 0;
		recorder.droppedDepth = 0;
	}

	void SetEnabled(bool enabled) noexcept {
		Recorder& recorder = GetRecorder();
		if (recorder.enabled == enabled) {
			return;
		}
		recorder.enabled = enabled;
		recorder.stackDepth = 0;
		recorder.overflowDepth = 0;
	}

	bool IsEnabled() noexcept {
		return GetRecorder().enabled;
	}

	FrameResults GetFrameResults() noexcept {
		const Recorder& recorder = GetRecorder();
		FrameResults results;
		results.zones = recorder.publishedCount > 0 ? recorder.published : nullptr;
		results.count = recorder.publishedCount;
		results.droppedSites = recorder.droppedSites;
		results.droppedDepth = recorder.publishedDroppedDepth;
		return results;
	}

} // namespace Shalltear::Profiler
