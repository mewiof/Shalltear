#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Audio/AudioEffect.h"

#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear {

	namespace AudioImpl {

		struct AudioBuiltinEffect {
			std::string_view name;
			IAudioEffectDefinition* definition = nullptr;
		};

		std::span<const AudioBuiltinEffect> GetBuiltinEffects();

		void RegisterBuiltinEffects(const void* owner);

		namespace Effects {

			IAudioEffectDefinition& GetLowPass();
			IAudioEffectDefinition& GetHighPass();
			IAudioEffectDefinition& GetLowPassSimple();
			IAudioEffectDefinition& GetHighPassSimple();
			IAudioEffectDefinition& GetParamEq();
			IAudioEffectDefinition& GetEcho();
			IAudioEffectDefinition& GetFlange();
			IAudioEffectDefinition& GetChorus();
			IAudioEffectDefinition& GetDistortion();
			IAudioEffectDefinition& GetNormalize();
			IAudioEffectDefinition& GetCompressor();
			IAudioEffectDefinition& GetDuckVolume();
			IAudioEffectDefinition& GetPitchShifter();
			IAudioEffectDefinition& GetSfxReverb();

		} // namespace Effects

	} // namespace AudioImpl

} // namespace Shalltear
