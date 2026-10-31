#include "AudioClipData.h"

#include "Realtime.h"

#include "Shalltear/Core/Memory.h"

#include <cstdint>
#include <cstring>
#include <new>

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			constexpr size_t PayloadOffset() {
				return (sizeof(AudioClipData) + 15) & ~static_cast<size_t>(15);
			}

			AudioClipData* AllocateBlock(u64 payloadBytes) {
				void* memory = Allocate(PayloadOffset() + static_cast<size_t>(payloadBytes), MemTag::Audio, 16);
				AudioClipData* data = new (memory) AudioClipData();
				data->payloadBytes = payloadBytes;
				data->references = 1;
				return data;
			}

			u8* PayloadOf(AudioClipData* data) {
				return reinterpret_cast<u8*>(data) + PayloadOffset();
			}

			void FillHead(AudioClipData& data) {
				const u64 frames = data.frames < CLIP_HEAD_FRAMES ? data.frames : CLIP_HEAD_FRAMES;
				if (data.encoding == AudioClipEncoding::Pcm || data.encoding == AudioClipEncoding::Streamed) {
					const f32* from = data.encoding == AudioClipEncoding::Pcm ? data.pcm : data.prime;
					std::memcpy(data.head, from, static_cast<size_t>(frames * data.channels) * sizeof(f32));
					return;
				}
				QoaCursor cursor;
				for (u64 frame = 0; frame < frames; ++frame) {
					QoaRead(data.qoa, cursor, frame, data.head + frame * data.channels);
				}
			}

			u64 PrimeFramesFor(u64 frames, u32 chunkFrames, u32 primeFrames) {
				u64 prime = (static_cast<u64>(primeFrames) + chunkFrames - 1) / chunkFrames * chunkFrames;
				prime = prime < chunkFrames ? chunkFrames : prime;
				return prime < frames ? prime : frames;
			}

			bool DecodePrime(const AudioClipData& data, f32* out) {
				const u32 channels = data.channels;
				if (data.codec == AudioClipEncoding::Pcm) {
					return ReadStreamSource(data.source, 0, reinterpret_cast<u8*>(out), data.primeFrames * channels * sizeof(f32));
				}
				if (data.codec == AudioClipEncoding::Opus) {
					return DecodeOpusHead(data.source, data.opus, channels, data.primeFrames, out);
				}
				Buffer bytes(static_cast<size_t>(QoaFrameSize(channels, QOA_FRAME_FRAMES)), MemTag::Audio);
				for (u64 first = 0, frame = 0; first < data.primeFrames; first += QOA_FRAME_FRAMES, ++frame) {
					const u32 count = static_cast<u32>(data.frames - first < QOA_FRAME_FRAMES ? data.frames - first : QOA_FRAME_FRAMES);
					const u64 size = QoaFrameSize(channels, count);
					if (!ReadStreamSource(data.source, QoaFrameOffset(channels, frame), bytes.data, size) ||
						!QoaDecodeFrame(bytes.data, size, channels, data.sampleRate, count, out + first * channels)) {
						return false;
					}
				}
				return true;
			}

		} // anonymous namespace

		bool ReadStreamSource(const AudioStreamSource& source, u64 offset, u8* out, u64 count) {
			if (offset > source.size || count > source.size - offset) {
				return false;
			}
			if (source.memory) {
				if (count > 0) {
					std::memcpy(out, source.memory + offset, static_cast<size_t>(count));
				}
				return true;
			}
			return Realtime::ReadAt(source.file, source.base + offset, std::span<u8>(out, static_cast<size_t>(count)));
		}

		AudioClipData* CreatePcmClipData(const void* interleaved, u64 frames, const ChannelLayout& layout, u32 sampleRate) {
			const u32 channels = layout.count;
			if (!interleaved || frames == 0 || channels == 0 || channels > MAX_AUDIO_CHANNELS || sampleRate == 0) {
				return nullptr;
			}
			const u64 bytes = frames * channels * sizeof(f32);
			AudioClipData* data = AllocateBlock(bytes);
			std::memcpy(PayloadOf(data), interleaved, static_cast<size_t>(bytes));
			data->encoding = AudioClipEncoding::Pcm;
			data->sampleRate = sampleRate;
			data->channels = channels;
			data->frames = frames;
			data->layout = layout;
			data->pcm = reinterpret_cast<const f32*>(PayloadOf(data));
			FillHead(*data);
			return data;
		}

		AudioClipData* CreateQoaClipData(const u8* bytes, u64 size, const ChannelLayout& layout) {
			QoaStream probe;
			if (!QoaOpen(bytes, size, probe) || probe.channels != layout.count) {
				return nullptr;
			}
			AudioClipData* data = AllocateBlock(size);
			std::memcpy(PayloadOf(data), bytes, static_cast<size_t>(size));
			QoaOpen(PayloadOf(data), size, data->qoa);
			data->encoding = AudioClipEncoding::Qoa;
			data->sampleRate = data->qoa.sampleRate;
			data->channels = data->qoa.channels;
			data->frames = data->qoa.frames;
			data->layout = layout;
			FillHead(*data);
			return data;
		}

		AudioClipData* CreateOpusClipData(const u8* bytes, u64 size, const ChannelLayout& layout, u64 frames) {
			const u32 channels = layout.count;
			if (!bytes || frames == 0 || channels == 0 || channels > MAX_AUDIO_CHANNELS) {
				return nullptr;
			}
			const u64 offsetsAt = (size + 3) & ~static_cast<u64>(3);
			AudioClipData* data = AllocateBlock(offsetsAt + OpusOffsetBytes(frames));
			u8* payload = PayloadOf(data);
			std::memcpy(payload, bytes, static_cast<size_t>(size));
			data->encoding = AudioClipEncoding::Opus;
			data->codec = AudioClipEncoding::Opus;
			data->sampleRate = OPUS_RATE;
			data->channels = channels;
			data->frames = frames;
			data->layout = layout;
			data->source.memory = payload;
			data->source.size = size;
			if (!ReadOpusIndex(data->source, channels, frames, reinterpret_cast<u32*>(payload + offsetsAt), data->opus)) {
				ReleaseClipData(data);
				return nullptr;
			}
			return data;
		}

		AudioClipData* CreateGeneratorClipData(u32 sampleRate, const ChannelLayout& layout, IAudioGeneratorInstance* instance) {
			const u32 channels = layout.count;
			if (channels == 0 || channels > MAX_AUDIO_CHANNELS || sampleRate == 0) {
				return nullptr;
			}
			const size_t streamBytes = (sizeof(AudioGeneratorStream) + 63) & ~static_cast<size_t>(63);
			const size_t windowBytes = static_cast<size_t>(channels) * GENERATOR_WINDOW_FRAMES * sizeof(f32);
			const size_t payloadStart = PayloadOffset();
			const size_t pad = (64 - (payloadStart % 64)) % 64;
			AudioClipData* data = AllocateBlock(pad + streamBytes + windowBytes);
			u8* payload = PayloadOf(data) + pad;
			data->stream = new (payload) AudioGeneratorStream();
			data->stream->instance = instance;
			data->stream->window = reinterpret_cast<f32*>(payload + streamBytes);
			std::memset(data->stream->window, 0, windowBytes);
			data->encoding = AudioClipEncoding::Generator;
			data->sampleRate = sampleRate;
			data->channels = channels;
			data->frames = static_cast<u64>(INT64_MAX);
			data->layout = layout;
			return data;
		}

		AudioClipData* CreateStreamedClipData(AudioClipEncoding codec, const u8* bytes, u64 size, const ChannelLayout& layout, u32 sampleRate,
											  u64 frames, u32 primeFrames) {
			const u32 channels = layout.count;
			if (!bytes || frames == 0 || channels == 0 || channels > MAX_AUDIO_CHANNELS || sampleRate == 0) {
				return nullptr;
			}
			u32 chunkFrames = 0;
			if (codec == AudioClipEncoding::Pcm) {
				if (size / (static_cast<u64>(channels) * sizeof(f32)) != frames || size % (static_cast<u64>(channels) * sizeof(f32)) != 0) {
					return nullptr;
				}
				chunkFrames = PCM_STREAM_CHUNK_FRAMES;
			} else if (codec == AudioClipEncoding::Opus) {
				if (sampleRate != OPUS_RATE) {
					return nullptr;
				}
				chunkFrames = OPUS_CHUNK_FRAMES;
			} else if (codec == AudioClipEncoding::Qoa) {
				QoaStream probe;
				if (!QoaOpen(bytes, size, probe) || probe.channels != channels || probe.sampleRate != sampleRate || probe.frames != frames) {
					return nullptr;
				}
				chunkFrames = QOA_FRAME_FRAMES;
			} else {
				return nullptr;
			}

			const u64 prime = PrimeFramesFor(frames, chunkFrames, primeFrames);
			const u64 primeOffset = (size + 15) & ~static_cast<u64>(15);
			const u64 offsetsAt = primeOffset + prime * channels * sizeof(f32);
			AudioClipData* data = AllocateBlock(offsetsAt + (codec == AudioClipEncoding::Opus ? OpusOffsetBytes(frames) : 0));
			u8* payload = PayloadOf(data);
			std::memcpy(payload, bytes, static_cast<size_t>(size));
			data->encoding = AudioClipEncoding::Streamed;
			data->codec = codec;
			data->sampleRate = sampleRate;
			data->channels = channels;
			data->frames = frames;
			data->layout = layout;
			data->chunkFrames = chunkFrames;
			data->primeFrames = prime;
			data->source.memory = payload;
			data->source.size = size;
			if (codec == AudioClipEncoding::Opus && !ReadOpusIndex(data->source, channels, frames, reinterpret_cast<u32*>(payload + offsetsAt), data->opus)) {
				ReleaseClipData(data);
				return nullptr;
			}
			f32* primeSamples = reinterpret_cast<f32*>(payload + primeOffset);
			if (!DecodePrime(*data, primeSamples)) {
				ReleaseClipData(data);
				return nullptr;
			}
			data->prime = primeSamples;
			FillHead(*data);
			return data;
		}

		AudioClipData* CreateLocatedClipData(AudioClipEncoding codec, const PakPayloadLocator& locator, const ChannelLayout& layout, u32 sampleRate,
											 u64 frames, u32 primeFrames) {
			const u32 channels = layout.count;
			if (frames == 0 || channels == 0 || channels > MAX_AUDIO_CHANNELS || sampleRate == 0) {
				return nullptr;
			}
			u64 expected = 0;
			u32 chunkFrames = 0;
			if (codec == AudioClipEncoding::Pcm) {
				expected = frames * channels * sizeof(f32);
				chunkFrames = PCM_STREAM_CHUNK_FRAMES;
			} else if (codec == AudioClipEncoding::Qoa && frames <= 0xFFFFFFFFull) {
				expected = QoaFileSize(channels, frames);
				chunkFrames = QOA_FRAME_FRAMES;
			} else if (codec == AudioClipEncoding::Opus && sampleRate == OPUS_RATE) {
				expected = locator.size;
				chunkFrames = OPUS_CHUNK_FRAMES;
			} else {
				return nullptr;
			}
			if (locator.size != expected) {
				return nullptr;
			}
			FileReader file;
			if (!file.Open(Path(locator.physicalPath)) || locator.offset > file.GetSize() || locator.size > file.GetSize() - locator.offset) {
				return nullptr;
			}
			if (codec == AudioClipEncoding::Qoa) {
				u8 header[8];
				if (!file.ReadAt(locator.offset, std::span<u8>(header, sizeof(header)))) {
					return nullptr;
				}
				const u32 magic = (static_cast<u32>(header[0]) << 24) | (static_cast<u32>(header[1]) << 16) | (static_cast<u32>(header[2]) << 8) | header[3];
				const u32 samples = (static_cast<u32>(header[4]) << 24) | (static_cast<u32>(header[5]) << 16) | (static_cast<u32>(header[6]) << 8) | header[7];
				if (magic != 0x716f6166u || samples != frames) {
					return nullptr;
				}
			}

			const u64 prime = PrimeFramesFor(frames, chunkFrames, primeFrames);
			const u64 offsetsAt = prime * channels * sizeof(f32);
			AudioClipData* data = AllocateBlock(offsetsAt + (codec == AudioClipEncoding::Opus ? OpusOffsetBytes(frames) : 0));
			data->encoding = AudioClipEncoding::Streamed;
			data->codec = codec;
			data->sampleRate = sampleRate;
			data->channels = channels;
			data->frames = frames;
			data->layout = layout;
			data->chunkFrames = chunkFrames;
			data->primeFrames = prime;
			data->source.file = std::move(file);
			data->source.base = locator.offset;
			data->source.size = locator.size;
			if (codec == AudioClipEncoding::Opus &&
				!ReadOpusIndex(data->source, channels, frames, reinterpret_cast<u32*>(PayloadOf(data) + offsetsAt), data->opus)) {
				ReleaseClipData(data);
				return nullptr;
			}
			f32* primeSamples = reinterpret_cast<f32*>(PayloadOf(data));
			if (!DecodePrime(*data, primeSamples)) {
				ReleaseClipData(data);
				return nullptr;
			}
			data->prime = primeSamples;
			FillHead(*data);
			return data;
		}

		void GetPayload(const AudioClipData& data, const u8*& bytes, u64& size) {
			if (data.encoding == AudioClipEncoding::Pcm) {
				bytes = reinterpret_cast<const u8*>(data.pcm);
				size = data.frames * data.channels * sizeof(f32);
				return;
			}
			if (data.encoding == AudioClipEncoding::Streamed || data.encoding == AudioClipEncoding::Opus) {
				bytes = data.source.memory;
				size = data.source.memory ? data.source.size : 0;
				return;
			}
			if (data.encoding == AudioClipEncoding::Generator) {
				bytes = nullptr;
				size = 0;
				return;
			}
			bytes = data.qoa.bytes;
			size = data.qoa.size;
		}

		AudioClipData* DecodeToPcm(const AudioClipData& source) {
			if (source.encoding == AudioClipEncoding::Pcm) {
				return CreatePcmClipData(source.pcm, source.frames, source.layout, source.sampleRate);
			}
			const u64 bytes = source.frames * source.channels * sizeof(f32);
			AudioClipData* data = AllocateBlock(bytes);
			f32* samples = reinterpret_cast<f32*>(PayloadOf(data));
			if (source.encoding == AudioClipEncoding::Opus) {
				if (!DecodeOpusHead(source.source, source.opus, source.channels, source.frames, samples)) {
					ReleaseClipData(data);
					return nullptr;
				}
			} else {
				QoaDecodeAll(source.qoa, samples);
			}
			data->encoding = AudioClipEncoding::Pcm;
			data->sampleRate = source.sampleRate;
			data->channels = source.channels;
			data->frames = source.frames;
			data->layout = source.layout;
			data->pcm = samples;
			FillHead(*data);
			return data;
		}

		void RetainClipData(AudioClipData* data) {
			if (data) {
				++data->references;
			}
		}

		void ReleaseClipData(AudioClipData* data) {
			if (!data || data->references == 0) {
				return;
			}
			if (--data->references == 0) {
				data->~AudioClipData();
				Free(data);
			}
		}

	} // namespace AudioImpl

} // namespace Shalltear
