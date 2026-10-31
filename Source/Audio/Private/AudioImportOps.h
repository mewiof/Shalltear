#pragma once

#include "AudioVoiceInternals.h"

#include "AudioChannels.h"

#include "Shalltear/Audio/AudioClip.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear {

	class JSONValue;

	namespace AudioImpl {

		struct AudioClipData;

		enum class AudioSampleRateSetting : u8 {
			PreserveSampleRate = 0,
			OptimizeSampleRate,
			OverrideSampleRate,
		};

		struct AudioImportSettings {
			bool forceToMono = false;
			bool normalize = true;
			AudioClipLoadType loadType = AudioClipLoadType::DecompressOnLoad;
			AudioCompressionFormat compressionFormat = AudioCompressionFormat::Opus;
			AudioSampleRateSetting sampleRateSetting = AudioSampleRateSetting::PreserveSampleRate;
			u32 sampleRateOverride = 44100;
			f32 quality = 0.7f;
		};

		u32 OpusBitrate(f32 quality, u32 channels);

		AudioClipLoadType DefaultLoadType(f64 seconds);

		void ReadImportSettings(const JSONValue& settings, AudioImportSettings& out);

		void WriteImportSettings(const AudioImportSettings& settings, JSONValue& out);

		void MergeImportSettings(const JSONValue& previous, const AudioImportSettings& settings, JSONValue& out);

		struct DecodedAudio {
			Vector<f32> samples{GetMemoryResource(MemTag::Audio)};
			ChannelLayout layout;
			u32 sampleRate = 0;

			u64 GetFrames() const { return layout.count ? samples.size() / layout.count : 0; }
		};

		void DownmixToMono(DecodedAudio& audio);

		void NormalizePeak(DecodedAudio& audio);

		bool Resample(DecodedAudio& audio, u32 targetRate);

		u32 OptimalSampleRate(const DecodedAudio& audio);

		std::span<const u32> GetStandardSampleRates();

		AudioClipData* Encode(const DecodedAudio& audio, AudioCompressionFormat format, f32 quality, str& error);

		bool BuildClip(DecodedAudio& audio, const AudioImportSettings& settings, AudioClip& clip, str& error);

		bool ReadWaveChannelLayout(const u8* bytes, size_t size, u32 channels, ChannelLayout& out);

		ChannelLayout MakeVorbisLayout(u32 channels);

		ChannelLayout MakeFlacLayout(u32 channels);

	} // namespace AudioImpl

} // namespace Shalltear
