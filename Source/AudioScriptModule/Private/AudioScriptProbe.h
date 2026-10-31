#pragma once

#include <atomic>
#include <cstdint>

struct AudioScriptProbe {
	struct Version {
		std::atomic<uint32_t> effectsMade{0};
		std::atomic<uint32_t> effectsReleased{0};
		std::atomic<uint64_t> effectBlocks{0};
		std::atomic<uint32_t> generatorsMade{0};
		std::atomic<uint32_t> generatorsReleased{0};
		std::atomic<uint64_t> generatorBlocks{0};
		std::atomic<uint64_t> firstFrom{~0ull};
		std::atomic<uint64_t> lastEnd{0};
		std::atomic<uint32_t> lastReleaseAt{0};
		std::atomic<uint32_t> shutdownAt{0};
	};

	Version versions[3];
	std::atomic<uint32_t> sequence{0};
};

using AudioScriptProbeFn = AudioScriptProbe* (*)();
inline constexpr const char* AUDIO_SCRIPT_PROBE_EXPORT = "ShalltearAudioScriptProbe";
