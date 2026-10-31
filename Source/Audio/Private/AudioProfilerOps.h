#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

#include <functional>

namespace Shalltear::AudioImpl {
	struct AudioDeviceEvents;
	struct AudioRenderCounters;
} // namespace Shalltear::AudioImpl

namespace Shalltear::AudioImpl::AudioProfilerOps {

	struct Counts {
		u64 ticks = 0;
		u64 callbacks = 0;
		u64 frames = 0;
		u64 costTicks = 0;
		u64 worstCostTicks = 0;
		u64 overruns = 0;
		u64 streamUnderruns = 0;
		u64 streamFailures = 0;
		u64 decoderShortfalls = 0;
		u64 lateStarts = 0;
		u64 droppedEvents = 0;
		u32 restarts = 0;
		u32 stops = 0;
		u32 reroutes = 0;
		u32 interruptions = 0;
	};

	void ReadCounts(const AudioRenderCounters& counters, u64 streamFailures, const AudioDeviceEvents& events, Counts& out);
	void ReadCounts(Counts& out);

	struct Summary {
		u32 sources = 0;
		u32 playingSources = 0;
		u32 pausedSources = 0;
		u32 voices = 0;
		u32 realVoices = 0;
		u32 virtualVoices = 0;
		u32 maxRealVoices = 0;
		u32 streams = 0;
		f64 renderMeanMs = 0.0;
		f64 renderShare = 0.0;
		f64 renderWorstMs = 0.0;
		u64 memoryBytes = 0;
		f64 time = 0.0;
		f64 deviceTime = 0.0;
		u32 sampleRate = 0;
		u32 periodFrames = 0;
		Counts counts;
	};

	void Summarize(const Counts& before, const Counts& now, Summary& out);

	struct Voice {
		u64 sequence = 0;
		str object;
		str asset;
		u64 assetId = 0;
		f32 volume = 0.0f;
		f32 audibility = 0.0f;
		str group;
		i32 priority = 0;
		bool is3D = false;
		bool paused = false;
		bool muted = false;
		bool isVirtual = false;
		bool oneShot = false;
		bool looped = false;
		f32 distance = 0.0f;
		f32 minDistance = 0.0f;
		f32 maxDistance = 0.0f;
		f64 time = 0.0;
		f64 duration = 0.0;
		bool streamed = false;
		u64 streamedAhead = 0;
	};

	void CollectVoices(Vector<Voice>& out);

	u64 CountPlays(u64 after, const std::function<void(u64 assetId)>& count);

} // namespace Shalltear::AudioImpl::AudioProfilerOps
