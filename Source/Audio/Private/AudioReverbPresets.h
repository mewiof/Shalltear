#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Audio/AudioReverbZone.h"

#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear {

	namespace AudioImpl {

		struct AudioReverbValues {
			f32 room = -10000.0f;
			f32 roomHF = 0.0f;
			f32 roomLF = 0.0f;
			f32 decayTime = 1.0f;
			f32 decayHFRatio = 0.5f;
			f32 reflections = -10000.0f;
			f32 reflectionsDelay = 0.02f;
			f32 reverb = 0.0f;
			f32 reverbDelay = 0.04f;
			f32 hfReference = 5000.0f;
			f32 lfReference = 250.0f;
			f32 diffusion = 100.0f;
			f32 density = 100.0f;
		};

		inline constexpr u32 SFX_REVERB_PARAMETERS = 14;

		const AudioReverbValues& GetReverbPreset(AudioReverbPreset preset);

		void WriteSfxReverb(const AudioReverbValues& values, f32 dryLevel, f32* out);

		struct AudioReverbZoneShape {
			Vec3 position;
			f32 minDistance = 10.0f;
			f32 maxDistance = 15.0f;
			AudioReverbValues values;
		};

		f32 ComputeZoneWeight(f32 distance, f32 minDistance, f32 maxDistance);

		bool BlendReverbZones(std::span<const AudioReverbZoneShape> zones, const Vec3& position, AudioReverbValues& out);

	} // namespace AudioImpl

} // namespace Shalltear
