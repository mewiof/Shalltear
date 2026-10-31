#pragma once

#include "Shalltear/Audio/AudioAPI.h"
#include "Shalltear/Audio/AudioTime.h"

#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/AnimationCurve.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear {

	enum class AudioRolloffMode : u8 {
		Logarithmic = 0,
		Linear = 1,
		Custom = 2,
	};

	enum class AudioVelocityUpdateMode : u8 {
		Auto = 0,
		Fixed,
		Dynamic,
	};

	struct AudioSpatialSettings {
		f32 spatialBlend = 0.0f;
		f32 minDistance = 1.0f;
		f32 maxDistance = 500.0f;
		AudioRolloffMode rolloffMode = AudioRolloffMode::Logarithmic;
		// degrees
		f32 spread = 0.0f;
		f32 dopplerLevel = 1.0f;
		f32 occlusion = 0.0f;
		bool spatialize = false;
		bool spatializePostEffects = false;

		friend bool operator==(const AudioSpatialSettings&, const AudioSpatialSettings&) = default;
	};

	struct AudioDistanceCurves {
		AnimationCurve customRolloff{GetMemoryResource(MemTag::Audio)};
		// spatialBlend in [0, 1], spread as a share of 360 degrees
		AnimationCurve spatialBlend{GetMemoryResource(MemTag::Audio)};
		AnimationCurve spread{GetMemoryResource(MemTag::Audio)};
		AnimationCurve reverbZoneMix{GetMemoryResource(MemTag::Audio)};

		friend bool operator==(const AudioDistanceCurves&, const AudioDistanceCurves&) = default;
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::AudioRolloffMode)
SLT_ENUM_VALUE(Logarithmic)
SLT_ENUM_VALUE(Linear)
SLT_ENUM_VALUE(Custom)
SLT_END_ENUM()

namespace Shalltear {

	class AudioConcurrency;
	class AudioResource;
	class Object;

	namespace AudioImpl {

		struct AudioVoiceAccess;
		struct AudioBusAccess;

	} // namespace AudioImpl

	class AudioVoice {
	public:
		AudioVoice() = default;

		bool IsValid() const { return _generation != 0; }

		friend bool operator==(const AudioVoice&, const AudioVoice&) = default;
	private:
		friend struct AudioImpl::AudioVoiceAccess;

		u32 _slot = 0;
		u32 _generation = 0;
	};

	class AudioBus {
	public:
		AudioBus() = default;

		// says nothing about whether the bus exists
		bool IsValid() const { return _generation != 0; }

		friend bool operator==(const AudioBus&, const AudioBus&) = default;
	private:
		friend struct AudioImpl::AudioBusAccess;

		u32 _slot = 0;
		u32 _generation = 0;
	};

	struct AudioVoiceEffect {
		std::string_view effect;
		std::span<const f32> parameters;
		const AnimationCurve* distanceCurve = nullptr;
		u32 distanceParameter = 0;
	};

	struct AudioPlayParams {
		AudioTime at;
		u64 startSample = 0;
		f32 volume = 1.0f;
		f32 pitch = 1.0f;
		f32 stereoPan = 0.0f;
		bool loop = false;
		AudioBus output;
		f32 reverbZoneMix = 1.0f;

		std::span<const AudioVoiceEffect> effects;
		bool bypassEffects = false;
		bool bypassListenerEffects = false;

		// 0 highest, 256 lowest, 0 never gives way
		i32 priority = 128;
		Handle<AudioConcurrency> concurrency;
		Handle<Object> owner;

		AudioSpatialSettings spatial;
		const AudioDistanceCurves* curves = nullptr;
		Vec3 position;
		Vec3 velocity;
	};

	namespace Audio {

		SHALLTEAR_AUDIO_API AudioVoice Play(Handle<AudioResource> resource, const AudioPlayParams& params = {});

		SHALLTEAR_AUDIO_API void Stop(AudioVoice voice);

		// DSP clock frames
		SHALLTEAR_AUDIO_API void SetScheduledStartTime(AudioVoice voice, AudioTime time);
		SHALLTEAR_AUDIO_API void SetScheduledEndTime(AudioVoice voice, AudioTime time);

		SHALLTEAR_AUDIO_API void Pause(AudioVoice voice);
		SHALLTEAR_AUDIO_API void UnPause(AudioVoice voice);

		SHALLTEAR_AUDIO_API void SetVolume(AudioVoice voice, f32 volume);
		SHALLTEAR_AUDIO_API void SetPitch(AudioVoice voice, f32 pitch);
		SHALLTEAR_AUDIO_API void SetStereoPan(AudioVoice voice, f32 stereoPan);

		SHALLTEAR_AUDIO_API void SetLoop(AudioVoice voice, bool loop);

		SHALLTEAR_AUDIO_API void SetOutput(AudioVoice voice, AudioBus output);

		SHALLTEAR_AUDIO_API void SetReverbZoneMix(AudioVoice voice, f32 reverbZoneMix);

		SHALLTEAR_AUDIO_API void SetSpatialSettings(AudioVoice voice, const AudioSpatialSettings& settings);

		SHALLTEAR_AUDIO_API void SetDistanceCurves(AudioVoice voice, const AudioDistanceCurves* curves);

		SHALLTEAR_AUDIO_API void SetPosition(AudioVoice voice, const Vec3& position, const Vec3& velocity = {});

		SHALLTEAR_AUDIO_API void SetEffects(AudioVoice voice, std::span<const AudioVoiceEffect> effects);

		SHALLTEAR_AUDIO_API void SetEffectParameter(AudioVoice voice, u32 index, u32 parameter, f32 value);

		SHALLTEAR_AUDIO_API void SetBypassEffects(AudioVoice voice, bool bypass);

		SHALLTEAR_AUDIO_API void SetBypassListenerEffects(AudioVoice voice, bool bypass);

		SHALLTEAR_AUDIO_API u64 GetTimeSamples(AudioVoice voice);
		SHALLTEAR_AUDIO_API void SetTimeSamples(AudioVoice voice, u64 sample);

		SHALLTEAR_AUDIO_API bool IsPlaying(AudioVoice voice);

		SHALLTEAR_AUDIO_API void SetPriority(AudioVoice voice, i32 priority);

		SHALLTEAR_AUDIO_API bool IsVirtual(AudioVoice voice);

		SHALLTEAR_AUDIO_API AudioTime GetTime();

		SHALLTEAR_AUDIO_API u32 GetOutputSampleRate();

		SHALLTEAR_AUDIO_API f32 GetGlobalVolume();
		SHALLTEAR_AUDIO_API bool SetGlobalVolume(f32 volume);

	} // namespace Audio

} // namespace Shalltear
