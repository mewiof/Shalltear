#include "AudioOpus.h"

#include "AudioClipData.h"
#include "Realtime.h"

#include "Shalltear/Core/Memory.h"

#include <opus_multistream.h>

#include <cstring>

namespace Shalltear {

	namespace AudioImpl {

		i32 Realtime::OpusDecode(OpusMSDecoder* decoder, const u8* packet, u32 size, f32* out, u32 frames) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfunction-effects"
			return opus_multistream_decode_float(decoder, packet, static_cast<opus_int32>(size), out, static_cast<int>(frames), 0);
#pragma clang diagnostic pop
		}

		bool Realtime::OpusInit(OpusMSDecoder* decoder, u32 channels, u8 streams, u8 coupled, const u8* mapping) {
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wfunction-effects"
			return opus_multistream_decoder_init(decoder, OPUS_RATE, static_cast<int>(channels), streams, coupled, mapping) == OPUS_OK;
#pragma clang diagnostic pop
		}

		namespace {

			using P = SpeakerPosition;

			P RightOf(P position) {
				switch (position) {
				case P::FrontLeft:
					return P::FrontRight;
				case P::BackLeft:
					return P::BackRight;
				case P::SideLeft:
					return P::SideRight;
				default:
					return P::None;
				}
			}

			u32 ReadU16(const u8* at) {
				return static_cast<u32>(at[0]) | (static_cast<u32>(at[1]) << 8);
			}

		} // anonymous namespace

		OpusStreamLayout MakeOpusStreamLayout(const ChannelLayout& layout) {
			OpusStreamLayout out;
			const u32 channels = layout.count < MAX_AUDIO_CHANNELS ? layout.count : MAX_AUDIO_CHANNELS;
			bool placed[MAX_AUDIO_CHANNELS]{};
			u32 pairs[MAX_AUDIO_CHANNELS][2]{};
			u32 coupled = 0;
			for (u32 left = 0; left < channels; ++left) {
				const P right = RightOf(layout.positions[left]);
				if (placed[left] || right == P::None) {
					continue;
				}
				for (u32 other = 0; other < channels; ++other) {
					if (!placed[other] && other != left && layout.positions[other] == right) {
						placed[left] = placed[other] = true;
						pairs[coupled][0] = left;
						pairs[coupled][1] = other;
						++coupled;
						break;
					}
				}
			}
			for (u32 pair = 0; pair < coupled; ++pair) {
				out.mapping[pairs[pair][0]] = static_cast<u8>(2 * pair);
				out.mapping[pairs[pair][1]] = static_cast<u8>(2 * pair + 1);
			}
			u32 mono = 0;
			for (u32 channel = 0; channel < channels; ++channel) {
				if (!placed[channel]) {
					out.mapping[channel] = static_cast<u8>(2 * coupled + mono++);
				}
			}
			out.coupled = static_cast<u8>(coupled);
			out.streams = static_cast<u8>(coupled + mono);
			return out;
		}

		bool ReadOpusIndex(const AudioStreamSource& source, u32 channels, u64 frames, u32* offsets, AudioOpusIndex& index) {
			if (frames == 0 || channels == 0 || channels > MAX_AUDIO_CHANNELS || source.size > 0xFFFFFFFFull) {
				return false;
			}
			const u64 packets = OpusPacketCount(frames);
			const u64 tableEnd = OPUS_HEADER_BYTES + packets * 2;
			if (source.size < tableEnd) {
				return false;
			}
			u8 header[OPUS_HEADER_BYTES];
			if (!ReadStreamSource(source, 0, header, OPUS_HEADER_BYTES)) {
				return false;
			}
			OpusStreamLayout layout;
			layout.streams = header[0];
			layout.coupled = header[1];
			if (layout.streams == 0 || layout.coupled > layout.streams || static_cast<u32>(layout.streams) + layout.coupled != channels) {
				return false;
			}
			bool taken[MAX_AUDIO_CHANNELS]{};
			for (u32 channel = 0; channel < channels; ++channel) {
				const u8 place = header[2 + channel];
				if (place >= channels || taken[place]) {
					return false;
				}
				taken[place] = true;
				layout.mapping[channel] = place;
			}
			for (u32 spare = 2 + channels; spare < OPUS_HEADER_BYTES; ++spare) {
				if (header[spare] != 0) {
					return false;
				}
			}

			Buffer table(static_cast<size_t>(packets * 2), MemTag::Audio);
			if (!table || !ReadStreamSource(source, OPUS_HEADER_BYTES, table.data, table.size)) {
				return false;
			}
			const u32 largest = OpusMaxPacketBytes(layout.streams);
			u64 at = tableEnd;
			u64 run = 0;
			u64 largestRun = 0;
			for (u64 packet = 0; packet < packets; ++packet) {
				const u32 size = ReadU16(table.data + packet * 2);
				if (size == 0 || size > largest) {
					return false;
				}
				offsets[packet] = static_cast<u32>(at);
				at += size;
				if (at > source.size) {
					return false;
				}
				run += size;
				if (packet >= OPUS_CHUNK_PACKETS) {
					run -= ReadU16(table.data + (packet - OPUS_CHUNK_PACKETS) * 2);
				}
				largestRun = run > largestRun ? run : largestRun;
			}
			if (at != source.size) {
				return false;
			}
			offsets[packets] = static_cast<u32>(at);
			index.layout = layout;
			index.packets = packets;
			index.offsets = offsets;
			index.largestRun = static_cast<u32>(largestRun);
			return true;
		}

		u64 OpusDecoderBytes(const OpusStreamLayout& layout) {
			const opus_int32 bytes = opus_multistream_decoder_get_size(layout.streams, layout.coupled);
			return bytes > 0 ? static_cast<u64>(bytes) : 0;
		}

		bool InitOpusDecoder(OpusMSDecoder* decoder, u32 channels, const OpusStreamLayout& layout) {
			return Realtime::OpusInit(decoder, channels, layout.streams, layout.coupled, layout.mapping);
		}

		bool DecodeOpusPackets(OpusMSDecoder* decoder, const AudioStreamSource& source, const AudioOpusIndex& index, u32 channels, u64 from, u64 to,
							   f32* out, bool keep, u8* scratch, u64 scratchBytes) {
			if (to > index.packets) {
				return false;
			}
			const u32* offsets = index.offsets;
			bool decoded = true;
			u64 packet = from;
			while (packet < to) {
				u64 end = packet + 1;
				while (end < to && offsets[end + 1] - offsets[packet] <= scratchBytes) {
					++end;
				}
				const u64 base = offsets[packet];
				const u64 bytes = offsets[end] - base;
				const u8* read = nullptr;
				if (source.memory) {
					read = source.memory + base;
				} else {
					if (bytes > scratchBytes || !ReadStreamSource(source, base, scratch, bytes)) {
						return false;
					}
					read = scratch;
				}
				for (; packet < end; ++packet) {
					f32* into = keep ? out + (packet - from) * OPUS_PACKET_FRAMES * channels : out;
					const u8* at = read + (offsets[packet] - base);
					if (Realtime::OpusDecode(decoder, at, offsets[packet + 1] - offsets[packet], into, OPUS_PACKET_FRAMES) !=
						static_cast<i32>(OPUS_PACKET_FRAMES)) {
						if (keep) {
							return false;
						}
						decoded = false;
					}
				}
			}
			return decoded;
		}

		bool DecodeOpusHead(const AudioStreamSource& source, const AudioOpusIndex& index, u32 channels, u64 frames, f32* out) {
			if (frames == 0 || OpusPacketCount(frames) > index.packets) {
				return false;
			}
			const u64 decoderBytes = OpusDecoderBytes(index.layout);
			if (decoderBytes == 0) {
				return false;
			}
			OpusMSDecoder* decoder = static_cast<OpusMSDecoder*>(Allocate(static_cast<size_t>(decoderBytes), MemTag::Audio, 16));
			Buffer scratch(index.largestRun, MemTag::Audio);
			Buffer spare(static_cast<size_t>(OPUS_PACKET_FRAMES) * channels * sizeof(f32), MemTag::Audio);
			f32* spareFrames = reinterpret_cast<f32*>(spare.data);
			const u64 whole = frames / OPUS_PACKET_FRAMES;
			const u64 part = frames % OPUS_PACKET_FRAMES;
			bool ok = InitOpusDecoder(decoder, channels, index.layout) &&
					  DecodeOpusPackets(decoder, source, index, channels, 0, 1, spareFrames, false, scratch.data, scratch.size) &&
					  DecodeOpusPackets(decoder, source, index, channels, 1, 1 + whole, out, true, scratch.data, scratch.size);
			if (ok && part > 0) {
				ok = DecodeOpusPackets(decoder, source, index, channels, 1 + whole, 2 + whole, spareFrames, false, scratch.data, scratch.size);
				if (ok) {
					std::memcpy(out + whole * OPUS_PACKET_FRAMES * channels, spareFrames, static_cast<size_t>(part * channels) * sizeof(f32));
				}
			}
			Free(decoder);
			return ok;
		}

	} // namespace AudioImpl

} // namespace Shalltear
