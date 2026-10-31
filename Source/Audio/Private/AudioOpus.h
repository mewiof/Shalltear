#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Core/Types.h"

#include "AudioChannels.h"

struct OpusMSDecoder;

namespace Shalltear {

	namespace AudioImpl {

		struct AudioStreamSource;

		inline constexpr u32 OPUS_RATE = 48000;
		inline constexpr u32 OPUS_PACKET_FRAMES = 960;
		inline constexpr u32 OPUS_CHUNK_PACKETS = 5;
		inline constexpr u32 OPUS_CHUNK_FRAMES = OPUS_CHUNK_PACKETS * OPUS_PACKET_FRAMES;
		inline constexpr u32 OPUS_PREROLL_PACKETS = 8;
		inline constexpr u32 OPUS_HEADER_BYTES = 16;

		inline constexpr u32 OpusMaxPacketBytes(u32 streams) {
			return 1275u * streams + 2u * (streams - 1u);
		}

		struct OpusStreamLayout {
			u8 streams = 0;
			u8 coupled = 0;
			u8 mapping[MAX_AUDIO_CHANNELS]{};
		};

		OpusStreamLayout MakeOpusStreamLayout(const ChannelLayout& layout);

		inline constexpr u64 OpusPacketCount(u64 frames) {
			return 1 + (frames + OPUS_PACKET_FRAMES - 1) / OPUS_PACKET_FRAMES;
		}

		struct AudioOpusIndex {
			OpusStreamLayout layout;
			u64 packets = 0;
			const u32* offsets = nullptr;
			u32 largestRun = 0;
		};

		inline constexpr u64 OpusOffsetBytes(u64 frames) {
			return (OpusPacketCount(frames) + 1) * sizeof(u32);
		}

		bool ReadOpusIndex(const AudioStreamSource& source, u32 channels, u64 frames, u32* offsets, AudioOpusIndex& index);

		u64 OpusDecoderBytes(const OpusStreamLayout& layout);
		bool InitOpusDecoder(OpusMSDecoder* decoder, u32 channels, const OpusStreamLayout& layout) [[clang::nonallocating]];

		bool DecodeOpusPackets(OpusMSDecoder* decoder, const AudioStreamSource& source, const AudioOpusIndex& index, u32 channels, u64 from, u64 to,
							   f32* out, bool keep, u8* scratch, u64 scratchBytes) [[clang::nonallocating]];

		bool DecodeOpusHead(const AudioStreamSource& source, const AudioOpusIndex& index, u32 channels, u64 frames, f32* out);

	} // namespace AudioImpl

} // namespace Shalltear
