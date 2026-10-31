#include "AudioStream.h"

#include "AudioClipData.h"
#include "AudioQoa.h"

#include "Shalltear/Core/Memory.h"

#include <cstring>
#include <new>

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			u64 ChunkFirst(const AudioClipData& clip, i64 chunk) [[clang::nonblocking]] {
				return static_cast<u64>(chunk) * clip.chunkFrames;
			}

			u32 ChunkCount(const AudioClipData& clip, i64 chunk) [[clang::nonblocking]] {
				const u64 left = clip.frames - ChunkFirst(clip, chunk);
				return left < clip.chunkFrames ? static_cast<u32>(left) : clip.chunkFrames;
			}

			i64 LastChunk(const AudioClipData& clip) [[clang::nonblocking]] {
				return static_cast<i64>((clip.frames + clip.chunkFrames - 1) / clip.chunkFrames) - 1;
			}

			i64 FirstStreamedChunk(const AudioClipData& clip) [[clang::nonblocking]] {
				return static_cast<i64>(clip.primeFrames / clip.chunkFrames);
			}

			bool DecodeOpusChunk(AudioStream& stream, u64 first, u32 count, AudioStreamChunk& into) [[clang::nonallocating]] {
				const AudioClipData& clip = *stream.clip;
				const u64 from = 1 + first / OPUS_PACKET_FRAMES;
				const u64 to = from + (count + OPUS_PACKET_FRAMES - 1) / OPUS_PACKET_FRAMES;
				bool ok = true;
				if (stream.opusNext != from) {
					const u64 exactFrom = 1 + clip.primeFrames / OPUS_PACKET_FRAMES;
					const u64 start = from <= exactFrom || from <= OPUS_PREROLL_PACKETS ? 0 : from - OPUS_PREROLL_PACKETS;
					ok = InitOpusDecoder(stream.opus, clip.channels, clip.opus.layout);
					if (ok) {
						DecodeOpusPackets(stream.opus, clip.source, clip.opus, clip.channels, start, from, into.samples, false, stream.bytes,
										  stream.byteCapacity);
					}
				}
				ok = ok && DecodeOpusPackets(stream.opus, clip.source, clip.opus, clip.channels, from, to, into.samples, true, stream.bytes,
											 stream.byteCapacity);
				stream.opusNext = ok ? to : ~0ull;
				return ok;
			}

			bool DecodeChunk(AudioStream& stream, i64 chunk, AudioStreamChunk& into) [[clang::nonallocating]] {
				const AudioClipData& clip = *stream.clip;
				const u32 channels = clip.channels;
				const u64 first = ChunkFirst(clip, chunk);
				const u32 count = ChunkCount(clip, chunk);
				if (clip.codec == AudioClipEncoding::Pcm) {
					const u64 frameBytes = static_cast<u64>(channels) * sizeof(f32);
					return ReadStreamSource(clip.source, first * frameBytes, reinterpret_cast<u8*>(into.samples), count * frameBytes);
				}
				if (clip.codec == AudioClipEncoding::Opus) {
					return DecodeOpusChunk(stream, first, count, into);
				}
				const u64 offset = QoaFrameOffset(channels, static_cast<u64>(chunk));
				const u64 size = QoaFrameSize(channels, count);
				if (size > stream.byteCapacity || !ReadStreamSource(clip.source, offset, stream.bytes, size)) {
					return false;
				}
				return QoaDecodeFrame(stream.bytes, size, channels, clip.sampleRate, count, into.samples);
			}

			void Hold(AudioStream& stream, u16 index) [[clang::nonblocking]] {
				if (stream.heldCount < 2) {
					stream.held[stream.heldCount++] = index;
					return;
				}
				const u64 first = stream.chunks[index].first;
				const u64 a = stream.chunks[stream.held[0]].first;
				const u64 b = stream.chunks[stream.held[1]].first;
				const u64 toA = a > first ? a - first : first - a;
				const u64 toB = b > first ? b - first : first - b;
				const u32 evict = toA >= toB ? 0 : 1;
				stream.free.TryPush(stream.held[evict]);
				stream.held[evict] = index;
			}

			void CopyFrame(const AudioStreamChunk& chunk, u64 index, u32 channels, f32* out) [[clang::nonblocking]] {
				const f32* from = chunk.samples + static_cast<size_t>(index - chunk.first) * channels;
				for (u32 channel = 0; channel < channels; ++channel) {
					out[channel] = from[channel];
				}
			}

		} // anonymous namespace

		AudioStream* CreateStream(AudioClipData* clip, u32 aheadFrames) {
			if (!clip || clip->encoding != AudioClipEncoding::Streamed || clip->chunkFrames == 0) {
				return nullptr;
			}
			u32 count = (aheadFrames + clip->chunkFrames - 1) / clip->chunkFrames + 2;
			count = count < 3 ? 3 : (count > 1024 ? 1024 : count);

			AudioStream* stream = New<AudioStream>(MemTag::Audio);
			stream->clip = clip;
			RetainClipData(clip);
			stream->chunkCount = count;
			stream->chunks = static_cast<AudioStreamChunk*>(Allocate(sizeof(AudioStreamChunk) * count, MemTag::Audio, 64));
			const size_t samples = (static_cast<size_t>(clip->chunkFrames) * clip->channels * sizeof(f32) + 63) & ~static_cast<size_t>(63);
			u8* storage = static_cast<u8*>(Allocate(samples * count, MemTag::Audio, 64));
			for (u32 index = 0; index < count; ++index) {
				new (&stream->chunks[index]) AudioStreamChunk();
				stream->chunks[index].samples = reinterpret_cast<f32*>(storage + samples * index);
			}
			if (clip->codec == AudioClipEncoding::Qoa) {
				stream->byteCapacity = QoaFrameSize(clip->channels, QOA_FRAME_FRAMES);
			} else if (clip->codec == AudioClipEncoding::Opus && !clip->source.memory) {
				stream->byteCapacity = clip->opus.largestRun;
			}
			if (stream->byteCapacity > 0) {
				stream->bytes = static_cast<u8*>(Allocate(static_cast<size_t>(stream->byteCapacity), MemTag::Audio, 16));
			}
			if (clip->codec == AudioClipEncoding::Opus) {
				stream->opus = static_cast<OpusMSDecoder*>(Allocate(static_cast<size_t>(OpusDecoderBytes(clip->opus.layout)), MemTag::Audio, 16));
			}
			stream->filled.Reserve(count);
			stream->free.Reserve(count);
			for (u32 index = 0; index < count; ++index) {
				stream->free.TryPush(static_cast<u16>(index));
			}
			return stream;
		}

		void DestroyStream(AudioStream* stream) {
			if (!stream) {
				return;
			}
			if (stream->chunkCount > 0) {
				Free(stream->chunks[0].samples);
			}
			Free(stream->chunks);
			if (stream->bytes) {
				Free(stream->bytes);
			}
			if (stream->opus) {
				Free(stream->opus);
			}
			ReleaseClipData(stream->clip);
			Delete(stream);
		}

		void RestartStream(AudioStream& stream, u64 frame, i32 direction, bool loop) {
			const AudioClipData& clip = *stream.clip;
			++stream.generation;
			stream.direction = direction < 0 ? -1 : 1;
			stream.loop = loop;
			stream.awaiting = true;
			u16 index = 0;
			while (stream.filled.TryPop(index)) {
				stream.free.TryPush(index);
			}

			AudioStreamRequest request;
			request.generation = stream.generation;
			request.direction = stream.direction;
			request.loop = loop;
			const i64 frames = static_cast<i64>(clip.frames);
			const i64 prime = static_cast<i64>(clip.primeFrames);
			i64 at = 0;
			if (stream.direction > 0) {
				at = static_cast<i64>(frame) > prime ? static_cast<i64>(frame) : prime;
				if (at >= frames) {
					at = prime;
					request.idle = !loop;
				}
			} else {
				at = static_cast<i64>(frame) < frames ? static_cast<i64>(frame) : frames - 1;
				if (at < prime) {
					at = frames - 1;
					request.idle = !loop;
				}
			}
			i64 chunk = at / static_cast<i64>(clip.chunkFrames);
			for (u32 step = 0; step < 2 && !request.idle; ++step) {
				bool isHeld = false;
				for (u32 slot = 0; slot < stream.heldCount; ++slot) {
					isHeld = isHeld || stream.chunks[stream.held[slot]].first == static_cast<u64>(chunk) * clip.chunkFrames;
				}
				if (!isHeld) {
					break;
				}
				chunk += stream.direction;
				if (chunk > LastChunk(clip)) {
					chunk = FirstStreamedChunk(clip);
					request.idle = !loop;
				} else if (chunk < FirstStreamedChunk(clip)) {
					chunk = LastChunk(clip);
					request.idle = !loop;
				}
			}
			request.chunk = static_cast<u64>(chunk);
			stream.requests.Post(request);
		}

		void ReadStreamFrame(AudioStream& stream, u64 index, f32* out) {
			const u32 channels = stream.clip->channels;
			for (u32 slot = 0; slot < stream.heldCount; ++slot) {
				const AudioStreamChunk& chunk = stream.chunks[stream.held[slot]];
				if (index >= chunk.first && index < chunk.first + chunk.count) {
					CopyFrame(chunk, index, channels, out);
					return;
				}
			}

			u16 popped = 0;
			bool passed = false;
			bool restarted = false;
			while (stream.filled.TryPop(popped)) {
				const AudioStreamChunk& chunk = stream.chunks[popped];
				if (chunk.generation != stream.generation) {
					stream.free.TryPush(popped);
					continue;
				}
				stream.awaiting = false;
				if (index >= chunk.first && index < chunk.first + chunk.count) {
					Hold(stream, popped);
					CopyFrame(chunk, index, channels, out);
					return;
				}
				const u64 first = chunk.first;
				const u64 end = chunk.first + chunk.count;
				stream.free.TryPush(popped);
				const bool behind = stream.direction > 0 ? end <= index : first > index;
				if (!behind) {
					RestartStream(stream, index, stream.direction, stream.loop);
					restarted = true;
					break;
				}
				const u64 chunkFrames = stream.clip->chunkFrames;
				passed = passed || (stream.direction > 0 ? end + chunkFrames <= index : first >= index + chunkFrames);
			}

			for (u32 channel = 0; channel < channels; ++channel) {
				out[channel] = 0.0f;
			}
			stream.starved = true;
			stream.underrun = stream.underrun || !stream.awaiting;
			if (passed && !restarted) {
				RestartStream(stream, index, stream.direction, stream.loop);
			}
		}

		bool ProduceChunk(AudioStream& stream, std::atomic<u64>& failures) {
			AudioStreamRequest request;
			if (stream.requests.Take(request)) {
				stream.producing = request.generation;
				stream.next = static_cast<i64>(request.chunk);
				stream.producingDirection = request.direction < 0 ? -1 : 1;
				stream.producingLoop = request.loop;
				stream.done = request.idle;
			}
			if (stream.done) {
				return false;
			}
			u16 index = 0;
			if (!stream.free.TryPop(index)) {
				return false;
			}

			const AudioClipData& clip = *stream.clip;
			AudioStreamChunk& chunk = stream.chunks[index];
			chunk.generation = stream.producing;
			chunk.first = ChunkFirst(clip, stream.next);
			chunk.count = ChunkCount(clip, stream.next);
			if (!DecodeChunk(stream, stream.next, chunk)) {
				std::memset(chunk.samples, 0, static_cast<size_t>(chunk.count) * clip.channels * sizeof(f32));
				failures.fetch_add(1, std::memory_order_relaxed);
			}
			stream.filled.TryPush(index);

			stream.next += stream.producingDirection;
			if (stream.next > LastChunk(clip)) {
				stream.next = FirstStreamedChunk(clip);
				stream.done = !stream.producingLoop;
			} else if (stream.next < FirstStreamedChunk(clip)) {
				stream.next = LastChunk(clip);
				stream.done = !stream.producingLoop;
			}
			return true;
		}

	} // namespace AudioImpl

} // namespace Shalltear
