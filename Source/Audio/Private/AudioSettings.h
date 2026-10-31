#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	enum class AudioOutputDevice : u8 {
		Default = 0,
		None,
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::AudioOutputDevice)
SLT_ENUM_VALUE(Default)
SLT_ENUM_VALUE(None)
SLT_END_ENUM()

namespace Shalltear {

	struct AudioSettings {
		SLT_REFLECT_VALUE(AudioSettings)
		SLT_REFLECT_CONSTRUCTIBLE()
		SLT_REFLECT_FIELD(globalVolume, SLT_RANGE(0, 1), SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))
		SLT_REFLECT_FIELD(volumeRolloffScale, SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))
		SLT_REFLECT_FIELD(dopplerFactor, SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))
		SLT_REFLECT_FIELD(periodMilliseconds, SLT_RANGE(1, 100), SLT_MUTABILITY(::Shalltear::MutabilityClass::DeviceIdle))
		SLT_REFLECT_FIELD(obtainedPeriod, SLT_READONLY, SLT_TRANSIENT)
		SLT_REFLECT_FIELD(maxVirtualVoices, SLT_RANGE(1, 4096), SLT_MUTABILITY(::Shalltear::MutabilityClass::DeviceIdle))
		SLT_REFLECT_FIELD(maxRealVoices, SLT_RANGE(1, 256), SLT_MUTABILITY(::Shalltear::MutabilityClass::DeviceIdle))
		SLT_REFLECT_FIELD(spatializer, SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))
		SLT_REFLECT_FIELD(virtualizeEffects, SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))
		SLT_REFLECT_FIELD(device, SLT_MUTABILITY(::Shalltear::MutabilityClass::DeviceIdle))
		SLT_REFLECT_FIELD(streamAheadBlocks, SLT_RANGE(4, 1024), SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))
		SLT_REFLECT_FIELD(primeFrames, SLT_RANGE(1024, 262144), SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))
		SLT_END_REFLECT()

		AudioOutputDevice device = AudioOutputDevice::Default;

		i32 periodMilliseconds = 10;

		str obtainedPeriod;

		f32 globalVolume = 1.0f;

		f32 volumeRolloffScale = 1.0f;

		f32 dopplerFactor = 1.0f;

		i32 maxVirtualVoices = 512;
		i32 maxRealVoices = 32;

		str spatializer;

		bool virtualizeEffects = true;

		i32 streamAheadBlocks = 32;

		i32 primeFrames = 8192;
	};

	namespace AudioImpl {

		AudioSettings& GetSettings();

		AudioSettings* GetDeclaredSettings();

		void SetDeclaredSettings(AudioSettings* declared);

		inline constexpr const char* SETTINGS_CATEGORY_PATH = "Audio";

	} // namespace AudioImpl

} // namespace Shalltear
