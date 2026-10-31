#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Core/Types.h"

#include "AudioChannels.h"

namespace Shalltear {

	namespace AudioImpl {

		inline constexpr u32 QOA_SLICE_FRAMES = 20;
		inline constexpr u32 QOA_SLICES_PER_FRAME = 256;
		inline constexpr u32 QOA_FRAME_FRAMES = QOA_SLICE_FRAMES * QOA_SLICES_PER_FRAME;

		struct QoaStream {
			const u8* bytes = nullptr;
			u64 size = 0;
			u32 channels = 0;
			u32 sampleRate = 0;
			u64 frames = 0;
		};

		struct QoaCursor {
			u64 anchor = ~0ull;
			u32 held = 0;
			u32 frame = 0;
			u32 slice = 0;
			i32 history[MAX_AUDIO_CHANNELS][4]{};
			i32 weights[MAX_AUDIO_CHANNELS][4]{};
			f32 decoded[2][QOA_SLICE_FRAMES * MAX_AUDIO_CHANNELS]{};
		};

		bool QoaOpen(const u8* bytes, u64 size, QoaStream& out);

		bool QoaRead(const QoaStream& stream, QoaCursor& cursor, u64 index, f32* out) [[clang::nonblocking]];

		// out must fit frames * channels floats
		void QoaDecodeAll(const QoaStream& stream, f32* out);

		u64 QoaFrameOffset(u32 channels, u64 frame) [[clang::nonblocking]];
		u64 QoaFrameSize(u32 channels, u32 samples) [[clang::nonblocking]];
		u64 QoaFileSize(u32 channels, u64 frames) [[clang::nonblocking]];

		bool QoaDecodeFrame(const u8* bytes, u64 size, u32 channels, u32 sampleRate, u32 samples, f32* out) [[clang::nonblocking]];

	} // namespace AudioImpl

} // namespace Shalltear
