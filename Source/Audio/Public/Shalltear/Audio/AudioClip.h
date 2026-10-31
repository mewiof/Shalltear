#pragma once

#include "Shalltear/Audio/AudioAPI.h"
#include "Shalltear/Audio/AudioResource.h"

#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	enum class AudioClipLoadType : u8 {
		DecompressOnLoad = 0,
		CompressedInMemory = 1,
		Streaming = 2,
	};

	enum class AudioCompressionFormat : u8 {
		PCM = 0,
		QOA = 1,
		Opus = 2,
	};

	namespace AudioImpl {

		struct AudioClipData;
		struct AudioClipAccess;

	} // namespace AudioImpl

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::AudioClipLoadType)
SLT_ENUM_VALUE(DecompressOnLoad)
SLT_ENUM_VALUE(CompressedInMemory)
SLT_ENUM_VALUE(Streaming)
SLT_END_ENUM()

SLT_REFLECT_ENUM(::Shalltear::AudioCompressionFormat)
SLT_ENUM_VALUE(PCM)
SLT_ENUM_VALUE(QOA)
SLT_ENUM_VALUE(Opus)
SLT_END_ENUM()

namespace Shalltear {

	class SHALLTEAR_AUDIO_API AudioClip : public AudioResource {
	public:
		SLT_REFLECT_CLASS(AudioClip)
		SLT_REFLECT_PARENT(AudioResource)
		SLT_REFLECT_CREATABLE()
		SLT_REFLECT_FIELD(_frequency, SLT_READONLY)
		SLT_REFLECT_FIELD(_channels, SLT_READONLY)
		SLT_REFLECT_FIELD(_samples, SLT_READONLY)
		SLT_REFLECT_FIELD(_loadType, SLT_READONLY)
		SLT_REFLECT_FIELD(_compressionFormat, SLT_READONLY)
		SLT_END_REFLECT()

		AudioClip() = default;
		~AudioClip() override;

		StringHash GetAssetTypeId() const override { return GetReflectType()->name; }

		bool Cook(CookWriter& descriptor, CookWriter& payload) const override;
		bool LoadCooked(CookReader& descriptor, CookReader& payload) override;

		u32 GetFrequency() const { return _frequency; }
		u32 GetChannels() const { return _channels; }
		u64 GetSamples() const { return _samples; }
		f32 GetLength() const { return _frequency ? static_cast<f32>(static_cast<f64>(_samples) / static_cast<f64>(_frequency)) : 0.0f; }
		AudioClipLoadType GetLoadType() const { return _loadType; }
		AudioCompressionFormat GetCompressionFormat() const { return _compressionFormat; }
	private:
		friend struct AudioImpl::AudioClipAccess;

		u32 _frequency = 0;
		u32 _channels = 0;
		u64 _samples = 0;
		AudioClipLoadType _loadType = AudioClipLoadType::DecompressOnLoad;
		AudioCompressionFormat _compressionFormat = AudioCompressionFormat::PCM;

		AudioImpl::AudioClipData* _cooked = nullptr;
		AudioImpl::AudioClipData* _playback = nullptr;
	};

} // namespace Shalltear
