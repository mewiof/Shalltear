#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Types.h"

#include "AudioChannels.h"
#include "AudioOpus.h"
#include "AudioQoa.h"

namespace Shalltear {

	class IAudioGeneratorInstance;

	namespace AudioImpl {

		enum class AudioClipEncoding : u8 {
			Pcm = 0,
			Qoa,
			Generator,
			Streamed,
			Opus,
		};

		inline constexpr u32 PCM_STREAM_CHUNK_FRAMES = 4096;

		struct AudioStreamSource {
			const u8* memory = nullptr;
			FileReader file;
			u64 base = 0;
			u64 size = 0;
		};

		bool ReadStreamSource(const AudioStreamSource& source, u64 offset, u8* out, u64 count) [[clang::nonallocating]];

		inline constexpr u32 GENERATOR_HISTORY_FRAMES = 4;

		inline constexpr u32 GENERATOR_WINDOW_FRAMES = 1024;

		struct AudioGeneratorStream {
			IAudioGeneratorInstance* instance = nullptr;
			f32* window = nullptr;
			i64 start = 0;
			u32 count = 0;
			u64 end = ~0ull;
		};

		inline constexpr u32 CLIP_HEAD_FRAMES = 4;

		struct AudioClipData {
			AudioClipEncoding encoding = AudioClipEncoding::Pcm;
			u32 sampleRate = 0;
			u32 channels = 0;
			u64 frames = 0;
			ChannelLayout layout;

			const f32* pcm = nullptr;
			QoaStream qoa;
			f32 head[CLIP_HEAD_FRAMES * MAX_AUDIO_CHANNELS]{};
			AudioGeneratorStream* stream = nullptr;

			AudioClipEncoding codec = AudioClipEncoding::Pcm;
			const f32* prime = nullptr;
			u64 primeFrames = 0;
			u32 chunkFrames = 0;
			AudioStreamSource source;
			AudioOpusIndex opus;

			u32 references = 0;
			u64 payloadBytes = 0;
		};

		AudioClipData* CreatePcmClipData(const void* interleaved, u64 frames, const ChannelLayout& layout, u32 sampleRate);

		AudioClipData* CreateQoaClipData(const u8* bytes, u64 size, const ChannelLayout& layout);

		AudioClipData* CreateOpusClipData(const u8* bytes, u64 size, const ChannelLayout& layout, u64 frames);

		AudioClipData* DecodeToPcm(const AudioClipData& source);

		AudioClipData* CreateStreamedClipData(AudioClipEncoding codec, const u8* bytes, u64 size, const ChannelLayout& layout, u32 sampleRate,
											  u64 frames, u32 primeFrames);

		AudioClipData* CreateLocatedClipData(AudioClipEncoding codec, const PakPayloadLocator& locator, const ChannelLayout& layout, u32 sampleRate,
											 u64 frames, u32 primeFrames);

		AudioClipData* CreateGeneratorClipData(u32 sampleRate, const ChannelLayout& layout, IAudioGeneratorInstance* instance);

		void GetPayload(const AudioClipData& data, const u8*& bytes, u64& size);

		void RetainClipData(AudioClipData* data);

		void ReleaseClipData(AudioClipData* data);

	} // namespace AudioImpl

} // namespace Shalltear
