#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Core/Types.h"

#include "AudioMailbox.h"
#include "AudioRing.h"

struct OpusMSDecoder;

#include <atomic>

namespace Shalltear {

	namespace AudioImpl {

		struct AudioClipData;

		struct AudioStreamChunk {
			u32 generation = 0;
			u32 count = 0;
			u64 first = 0;
			f32* samples = nullptr;
		};

		struct AudioStreamRequest {
			u32 generation = 0;
			i32 direction = 1;
			u64 chunk = 0;
			bool loop = false;
			bool idle = false;
		};

		struct AudioStream {
			AudioClipData* clip = nullptr;
			u32 chunkCount = 0;
			AudioStreamChunk* chunks = nullptr;
			u8* bytes = nullptr;
			u64 byteCapacity = 0;
			OpusMSDecoder* opus = nullptr;

			AudioRing<u16> filled;
			AudioRing<u16> free;
			AudioMailbox<AudioStreamRequest> requests;

			u32 generation = 0;
			i32 direction = 1;
			bool loop = false;
			bool awaiting = false;
			u16 held[2]{};
			u32 heldCount = 0;
			bool starved = false;
			bool underrun = false;

			u32 producing = 0;
			i64 next = 0;
			i32 producingDirection = 1;
			bool producingLoop = false;
			bool done = true;
			u64 opusNext = ~0ull;

			std::atomic<bool> retiring{false};
			std::atomic<bool> released{false};
		};

		AudioStream* CreateStream(AudioClipData* clip, u32 aheadFrames);

		void DestroyStream(AudioStream* stream);

		void RestartStream(AudioStream& stream, u64 frame, i32 direction, bool loop) [[clang::nonblocking]];

		void ReadStreamFrame(AudioStream& stream, u64 index, f32* out) [[clang::nonblocking]];

		bool ProduceChunk(AudioStream& stream, std::atomic<u64>& failures) [[clang::nonallocating]];

	} // namespace AudioImpl

} // namespace Shalltear
