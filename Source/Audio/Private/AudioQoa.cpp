#include "AudioQoa.h"

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			constexpr i32 DEQUANT[16][8] = {
				{1, -1, 3, -3, 5, -5, 7, -7},
				{5, -5, 18, -18, 32, -32, 49, -49},
				{16, -16, 53, -53, 95, -95, 147, -147},
				{34, -34, 113, -113, 203, -203, 315, -315},
				{63, -63, 210, -210, 378, -378, 588, -588},
				{104, -104, 345, -345, 621, -621, 966, -966},
				{158, -158, 528, -528, 950, -950, 1477, -1477},
				{228, -228, 760, -760, 1368, -1368, 2128, -2128},
				{316, -316, 1053, -1053, 1895, -1895, 2947, -2947},
				{422, -422, 1405, -1405, 2529, -2529, 3934, -3934},
				{548, -548, 1828, -1828, 3290, -3290, 5117, -5117},
				{696, -696, 2320, -2320, 4176, -4176, 6496, -6496},
				{868, -868, 2893, -2893, 5207, -5207, 8099, -8099},
				{1064, -1064, 3548, -3548, 6386, -6386, 9933, -9933},
				{1286, -1286, 4288, -4288, 7718, -7718, 12005, -12005},
				{1536, -1536, 5120, -5120, 9216, -9216, 14336, -14336},
			};

			constexpr u32 QOA_MAGIC = 0x716f6166;

			u64 ReadU64(const u8* bytes) [[clang::nonblocking]] {
				return (static_cast<u64>(bytes[0]) << 56) | (static_cast<u64>(bytes[1]) << 48) | (static_cast<u64>(bytes[2]) << 40) |
					   (static_cast<u64>(bytes[3]) << 32) | (static_cast<u64>(bytes[4]) << 24) | (static_cast<u64>(bytes[5]) << 16) |
					   (static_cast<u64>(bytes[6]) << 8) | static_cast<u64>(bytes[7]);
			}

			u64 FrameBytes(u32 channels, u32 slices) [[clang::nonblocking]] {
				return 8 + 16 * static_cast<u64>(channels) + 8 * static_cast<u64>(slices) * channels;
			}

			u64 FrameOffset(const QoaStream& stream, u32 frame) [[clang::nonblocking]] {
				return 8 + static_cast<u64>(frame) * FrameBytes(stream.channels, QOA_SLICES_PER_FRAME);
			}

			u32 FrameCount(const QoaStream& stream) [[clang::nonblocking]] {
				return static_cast<u32>((stream.frames + QOA_FRAME_FRAMES - 1) / QOA_FRAME_FRAMES);
			}

			u32 SamplesInFrame(const QoaStream& stream, u32 frame) [[clang::nonblocking]] {
				const u64 start = static_cast<u64>(frame) * QOA_FRAME_FRAMES;
				const u64 left = stream.frames - start;
				return left < QOA_FRAME_FRAMES ? static_cast<u32>(left) : QOA_FRAME_FRAMES;
			}

			i32 ClampS16(i32 value) [[clang::nonblocking]] {
				return value < -32768 ? -32768 : (value > 32767 ? 32767 : value);
			}

			void ReadFrameState(const u8* state, u32 channels, i32 (*history)[4], i32 (*weights)[4]) [[clang::nonblocking]] {
				for (u32 channel = 0; channel < channels; ++channel) {
					u64 historyBits = ReadU64(state);
					u64 weightBits = ReadU64(state + 8);
					state += 16;
					for (u32 tap = 0; tap < 4; ++tap) {
						history[channel][tap] = static_cast<i16>(historyBits >> 48);
						weights[channel][tap] = static_cast<i16>(weightBits >> 48);
						historyBits <<= 16;
						weightBits <<= 16;
					}
				}
			}

			void LoadFrameState(const QoaStream& stream, QoaCursor& cursor, u32 frame) [[clang::nonblocking]] {
				ReadFrameState(stream.bytes + FrameOffset(stream, frame) + 8, stream.channels, cursor.history, cursor.weights);
				cursor.frame = frame;
				cursor.slice = 0;
			}

			void DecodeSliceInto(const u8* slices, u32 channels, u32 length, i32 (*histories)[4], i32 (*weightSets)[4], f32* out) [[clang::nonblocking]] {
				for (u32 channel = 0; channel < channels; ++channel) {
					u64 slice = ReadU64(slices + 8 * static_cast<u64>(channel));
					const i32 scalefactor = static_cast<i32>((slice >> 60) & 0xf);
					slice <<= 4;
					i32* history = histories[channel];
					i32* weights = weightSets[channel];
					for (u32 index = 0; index < length; ++index) {
						const i32 predicted = (weights[0] * history[0] + weights[1] * history[1] + weights[2] * history[2] + weights[3] * history[3]) >> 13;
						const i32 dequantized = DEQUANT[scalefactor][(slice >> 61) & 0x7];
						const i32 reconstructed = ClampS16(predicted + dequantized);
						out[index * channels + channel] = static_cast<f32>(reconstructed) * (1.0f / 32768.0f);
						slice <<= 3;

						const i32 delta = dequantized >> 4;
						for (u32 tap = 0; tap < 4; ++tap) {
							weights[tap] += history[tap] < 0 ? -delta : delta;
						}
						history[0] = history[1];
						history[1] = history[2];
						history[2] = history[3];
						history[3] = reconstructed;
					}
				}
			}

			void DecodeSlice(const QoaStream& stream, QoaCursor& cursor, f32* out) [[clang::nonblocking]] {
				const u32 channels = stream.channels;
				const u32 frameSamples = SamplesInFrame(stream, cursor.frame);
				const u32 first = cursor.slice * QOA_SLICE_FRAMES;
				const u32 length = frameSamples - first < QOA_SLICE_FRAMES ? frameSamples - first : QOA_SLICE_FRAMES;
				const u8* slices = stream.bytes + FrameOffset(stream, cursor.frame) + 8 + 16 * static_cast<u64>(channels) +
								   8 * static_cast<u64>(cursor.slice) * channels;
				DecodeSliceInto(slices, channels, length, cursor.history, cursor.weights, out);

				if (++cursor.slice * QOA_SLICE_FRAMES >= frameSamples && cursor.frame + 1 < FrameCount(stream)) {
					LoadFrameState(stream, cursor, cursor.frame + 1);
				}
			}

			u64 NextSlice(const QoaCursor& cursor) [[clang::nonblocking]] {
				return static_cast<u64>(cursor.frame) * QOA_SLICES_PER_FRAME + cursor.slice;
			}

			constexpr u64 SKIP_THROUGH = 16;

		} // anonymous namespace

		bool QoaOpen(const u8* bytes, u64 size, QoaStream& out) {
			out = {};
			if (!bytes || size < 8 + 8) {
				return false;
			}
			const u64 fileHeader = ReadU64(bytes);
			if (static_cast<u32>(fileHeader >> 32) != QOA_MAGIC) {
				return false;
			}
			QoaStream stream;
			stream.bytes = bytes;
			stream.size = size;
			stream.frames = static_cast<u32>(fileHeader & 0xffffffffu);
			if (stream.frames == 0) {
				return false;
			}
			const u64 firstFrame = ReadU64(bytes + 8);
			stream.channels = static_cast<u32>(firstFrame >> 56);
			stream.sampleRate = static_cast<u32>((firstFrame >> 32) & 0xffffff);
			if (stream.channels == 0 || stream.channels > MAX_AUDIO_CHANNELS || stream.sampleRate == 0) {
				return false;
			}

			u64 offset = 8;
			const u32 frames = FrameCount(stream);
			for (u32 frame = 0; frame < frames; ++frame) {
				if (offset + 8 > size) {
					return false;
				}
				const u64 header = ReadU64(bytes + offset);
				const u32 channels = static_cast<u32>(header >> 56);
				const u32 rate = static_cast<u32>((header >> 32) & 0xffffff);
				const u32 samples = static_cast<u32>((header >> 16) & 0xffff);
				const u32 frameSize = static_cast<u32>(header & 0xffff);
				const u32 slices = (samples + QOA_SLICE_FRAMES - 1) / QOA_SLICE_FRAMES;
				if (channels != stream.channels || rate != stream.sampleRate || samples != SamplesInFrame(stream, frame) ||
					frameSize != FrameBytes(channels, slices) || offset + frameSize > size) {
					return false;
				}
				offset += frameSize;
			}
			if (offset != size) {
				return false;
			}
			out = stream;
			return true;
		}

		bool QoaRead(const QoaStream& stream, QoaCursor& cursor, u64 index, f32* out) {
			const u32 channels = stream.channels;
			if (index >= stream.frames) {
				for (u32 channel = 0; channel < channels; ++channel) {
					out[channel] = 0.0f;
				}
				return false;
			}

			const u64 slice = index / QOA_SLICE_FRAMES;
			const bool held = cursor.held > 0 && slice >= cursor.anchor && slice < cursor.anchor + cursor.held;
			if (!held) {
				const u64 next = NextSlice(cursor);
				if (cursor.held > 0 && slice >= next && slice - next < SKIP_THROUGH) {
					while (NextSlice(cursor) <= slice) {
						const u64 decoding = NextSlice(cursor);
						DecodeSlice(stream, cursor, cursor.decoded[decoding & 1]);
						if (cursor.held == 2) {
							cursor.anchor += 1;
						} else {
							cursor.held = 2;
						}
					}
				} else {
					const u32 frame = static_cast<u32>(slice / QOA_SLICES_PER_FRAME);
					const u32 local = static_cast<u32>(slice % QOA_SLICES_PER_FRAME);
					LoadFrameState(stream, cursor, frame);
					f32 discard[QOA_SLICE_FRAMES * MAX_AUDIO_CHANNELS];
					for (u32 skipped = 0; skipped < local; ++skipped) {
						DecodeSlice(stream, cursor, skipped + 1 == local ? cursor.decoded[(slice - 1) & 1] : discard);
					}
					DecodeSlice(stream, cursor, cursor.decoded[slice & 1]);
					cursor.anchor = local > 0 ? slice - 1 : slice;
					cursor.held = local > 0 ? 2 : 1;
				}
			}

			const f32* source = cursor.decoded[slice & 1] + (index % QOA_SLICE_FRAMES) * channels;
			for (u32 channel = 0; channel < channels; ++channel) {
				out[channel] = source[channel];
			}
			return true;
		}

		void QoaDecodeAll(const QoaStream& stream, f32* out) {
			QoaCursor cursor;
			LoadFrameState(stream, cursor, 0);
			const u64 slices = (stream.frames + QOA_SLICE_FRAMES - 1) / QOA_SLICE_FRAMES;
			for (u64 slice = 0; slice < slices; ++slice) {
				DecodeSlice(stream, cursor, out + slice * QOA_SLICE_FRAMES * stream.channels);
			}
		}

		u64 QoaFrameOffset(u32 channels, u64 frame) {
			return 8 + frame * FrameBytes(channels, QOA_SLICES_PER_FRAME);
		}

		u64 QoaFrameSize(u32 channels, u32 samples) {
			return FrameBytes(channels, (samples + QOA_SLICE_FRAMES - 1) / QOA_SLICE_FRAMES);
		}

		u64 QoaFileSize(u32 channels, u64 frames) {
			const u64 full = frames / QOA_FRAME_FRAMES;
			const u32 rest = static_cast<u32>(frames % QOA_FRAME_FRAMES);
			return 8 + full * FrameBytes(channels, QOA_SLICES_PER_FRAME) + (rest > 0 ? QoaFrameSize(channels, rest) : 0);
		}

		bool QoaDecodeFrame(const u8* bytes, u64 size, u32 channels, u32 sampleRate, u32 samples, f32* out) {
			if (!bytes || channels == 0 || channels > MAX_AUDIO_CHANNELS || samples == 0 || samples > QOA_FRAME_FRAMES) {
				return false;
			}
			const u32 slices = (samples + QOA_SLICE_FRAMES - 1) / QOA_SLICE_FRAMES;
			const u64 expected = FrameBytes(channels, slices);
			if (size != expected) {
				return false;
			}
			const u64 header = ReadU64(bytes);
			if (static_cast<u32>(header >> 56) != channels || static_cast<u32>((header >> 32) & 0xffffff) != sampleRate ||
				static_cast<u32>((header >> 16) & 0xffff) != samples || (header & 0xffff) != expected) {
				return false;
			}
			i32 history[MAX_AUDIO_CHANNELS][4];
			i32 weights[MAX_AUDIO_CHANNELS][4];
			ReadFrameState(bytes + 8, channels, history, weights);
			const u8* slice = bytes + 8 + 16 * static_cast<u64>(channels);
			for (u32 index = 0; index < slices; ++index) {
				const u32 first = index * QOA_SLICE_FRAMES;
				const u32 length = samples - first < QOA_SLICE_FRAMES ? samples - first : QOA_SLICE_FRAMES;
				DecodeSliceInto(slice, channels, length, history, weights, out + static_cast<size_t>(first) * channels);
				slice += 8 * static_cast<u64>(channels);
			}
			return true;
		}

	} // namespace AudioImpl

} // namespace Shalltear
