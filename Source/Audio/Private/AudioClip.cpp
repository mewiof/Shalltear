#include "Shalltear/Audio/AudioClip.h"

#include "AudioClipAccess.h"
#include "AudioClipData.h"
#include "AudioSettings.h"

#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			u32 PrimeFramesSetting() {
				const i32 frames = GetSettings().primeFrames;
				return frames < 1 ? 1u : static_cast<u32>(frames);
			}

			bool StreamsFrom(AudioClipEncoding codec, AudioClipLoadType loadType) {
				return loadType == AudioClipLoadType::Streaming || (codec == AudioClipEncoding::Opus && loadType == AudioClipLoadType::CompressedInMemory);
			}

			AudioClipEncoding EncodingOf(u8 format) {
				return format == static_cast<u8>(AudioCompressionFormat::PCM)	? AudioClipEncoding::Pcm
					   : format == static_cast<u8>(AudioCompressionFormat::QOA) ? AudioClipEncoding::Qoa
																				: AudioClipEncoding::Opus;
			}

		} // anonymous namespace

		AudioClip* AudioClipAccess::Resolve(Handle<AudioResource> resource) {
			Object* object = ObjectRegistry::Resolve(resource.GetInstanceId());
			if (!object || !TypeRegistry::IsDerivedFrom(object->GetType(), AudioClip::GetReflectType())) {
				return nullptr;
			}
			return static_cast<AudioClip*>(object);
		}

		bool AudioClipAccess::Adopt(AudioClip& clip, AudioClipData* cooked, AudioClipLoadType loadType) {
			Clear(clip);
			if (!cooked) {
				return false;
			}
			const AudioClipEncoding cookedCodec = cooked->encoding == AudioClipEncoding::Streamed ? cooked->codec : cooked->encoding;
			if (StreamsFrom(cookedCodec, loadType) && cooked->encoding != AudioClipEncoding::Streamed) {
				const u8* bytes = nullptr;
				u64 size = 0;
				GetPayload(*cooked, bytes, size);
				AudioClipData* streamed =
					CreateStreamedClipData(cooked->encoding, bytes, size, cooked->layout, cooked->sampleRate, cooked->frames, PrimeFramesSetting());
				ReleaseClipData(cooked);
				if (!streamed) {
					return false;
				}
				cooked = streamed;
			}
			AudioClipData* playback = cooked;
			if ((cooked->encoding == AudioClipEncoding::Qoa || cooked->encoding == AudioClipEncoding::Opus) &&
				loadType == AudioClipLoadType::DecompressOnLoad) {
				playback = DecodeToPcm(*cooked);
				if (!playback) {
					ReleaseClipData(cooked);
					return false;
				}
			} else {
				RetainClipData(cooked);
			}
			clip._cooked = cooked;
			clip._playback = playback;
			clip._frequency = cooked->sampleRate;
			clip._channels = cooked->channels;
			clip._samples = cooked->frames;
			clip._loadType = loadType;
			clip._compressionFormat = cookedCodec == AudioClipEncoding::Qoa	   ? AudioCompressionFormat::QOA
									  : cookedCodec == AudioClipEncoding::Opus ? AudioCompressionFormat::Opus
																			   : AudioCompressionFormat::PCM;
			return true;
		}

		void AudioClipAccess::Clear(AudioClip& clip) {
			ReleaseClipData(clip._playback);
			ReleaseClipData(clip._cooked);
			clip._playback = nullptr;
			clip._cooked = nullptr;
			clip._frequency = 0;
			clip._channels = 0;
			clip._samples = 0;
		}

	} // namespace AudioImpl

	AudioClip::~AudioClip() {
		AudioImpl::AudioClipAccess::Clear(*this);
	}

	bool AudioClip::Cook(CookWriter& descriptor, CookWriter& payload) const {
		const AudioImpl::AudioClipData* cooked = _cooked;
		if (!cooked) {
			return false;
		}
		descriptor.Write(static_cast<u8>(_compressionFormat));
		descriptor.Write(static_cast<u8>(_loadType));
		descriptor.Write(cooked->sampleRate);
		descriptor.Write(static_cast<u8>(cooked->channels));
		for (u32 channel = 0; channel < cooked->channels; ++channel) {
			descriptor.Write(static_cast<u8>(cooked->layout.positions[channel]));
		}
		descriptor.Write(cooked->frames);

		const u8* bytes = nullptr;
		u64 size = 0;
		AudioImpl::GetPayload(*cooked, bytes, size);
		Buffer read;
		if (!bytes && cooked->encoding == AudioImpl::AudioClipEncoding::Streamed) {
			read.Allocate(static_cast<size_t>(cooked->source.size), MemTag::Audio);
			if (!read || !AudioImpl::ReadStreamSource(cooked->source, 0, read.data, read.size)) {
				return false;
			}
			bytes = read.data;
			size = read.size;
		}
		descriptor.Write(size);
		payload.WriteBytes(bytes, static_cast<size_t>(size));
		if (_loadType == AudioClipLoadType::Streaming) {
			payload.SetResidency(PakResidency::Streamed);
		}
		return true;
	}

	bool AudioClip::LoadCooked(CookReader& descriptor, CookReader& payload) {
		using namespace AudioImpl;

		u8 format = 0;
		u8 loadType = 0;
		u32 frequency = 0;
		u8 channels = 0;
		descriptor.Read(format);
		descriptor.Read(loadType);
		descriptor.Read(frequency);
		descriptor.Read(channels);
		if (descriptor.Failed() || channels == 0 || channels > MAX_AUDIO_CHANNELS) {
			return false;
		}
		ChannelLayout layout;
		layout.count = channels;
		for (u32 channel = 0; channel < channels; ++channel) {
			u8 position = 0;
			descriptor.Read(position);
			if (position > static_cast<u8>(SpeakerPosition::SideRight)) {
				return false;
			}
			layout.positions[channel] = static_cast<SpeakerPosition>(position);
		}
		u64 frames = 0;
		u64 size = 0;
		descriptor.Read(frames);
		descriptor.Read(size);
		if (descriptor.Failed() || frames == 0 || frequency == 0 || format > static_cast<u8>(AudioCompressionFormat::Opus) ||
			loadType > static_cast<u8>(AudioClipLoadType::Streaming)) {
			return false;
		}

		const PakPayloadLocator* locator = payload.GetLocator();
		const AudioClipEncoding codec = EncodingOf(format);
		const bool streams = StreamsFrom(codec, static_cast<AudioClipLoadType>(loadType));
		if (loadType == static_cast<u8>(AudioClipLoadType::Streaming) && locator) {
			if (locator->size != size) {
				return false;
			}
			AudioClipData* located = CreateLocatedClipData(codec, *locator, layout, frequency, frames, PrimeFramesSetting());
			return located && AudioClipAccess::Adopt(*this, located, AudioClipLoadType::Streaming);
		}

		if (size > payload.Remaining()) {
			return false;
		}
		const u8* bytes = payload.ReadSpan(static_cast<size_t>(size));
		if (payload.Failed() || !bytes) {
			return false;
		}

		AudioClipData* cooked = nullptr;
		if (streams) {
			cooked = CreateStreamedClipData(codec, bytes, size, layout, frequency, frames, PrimeFramesSetting());
		} else if (format == static_cast<u8>(AudioCompressionFormat::PCM)) {
			const u64 frameBytes = static_cast<u64>(channels) * sizeof(f32);
			if (size % frameBytes != 0 || size / frameBytes != frames) {
				return false;
			}
			cooked = CreatePcmClipData(bytes, frames, layout, frequency);
		} else if (codec == AudioClipEncoding::Opus) {
			cooked = frequency == OPUS_RATE ? CreateOpusClipData(bytes, size, layout, frames) : nullptr;
		} else {
			cooked = CreateQoaClipData(bytes, size, layout);
			if (cooked && (cooked->sampleRate != frequency || cooked->frames != frames)) {
				ReleaseClipData(cooked);
				cooked = nullptr;
			}
		}
		if (!cooked) {
			return false;
		}
		return AudioClipAccess::Adopt(*this, cooked, static_cast<AudioClipLoadType>(loadType));
	}

} // namespace Shalltear
