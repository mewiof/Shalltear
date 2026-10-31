#include "Effects/BuiltinEffects.h"

#include "AudioEffectRegistry.h"

namespace Shalltear {

	namespace AudioImpl {

		std::span<const AudioBuiltinEffect> GetBuiltinEffects() {
			// mixers use these names, order matters
			static const AudioBuiltinEffect effects[] = {
				{"LowPass", &Effects::GetLowPass()},
				{"HighPass", &Effects::GetHighPass()},
				{"Echo", &Effects::GetEcho()},
				{"Flange", &Effects::GetFlange()},
				{"Distortion", &Effects::GetDistortion()},
				{"Normalize", &Effects::GetNormalize()},
				{"Param EQ", &Effects::GetParamEq()},
				{"Pitch Shifter", &Effects::GetPitchShifter()},
				{"Chorus", &Effects::GetChorus()},
				{"Compressor", &Effects::GetCompressor()},
				{"SFX Reverb", &Effects::GetSfxReverb()},
				{"LowPass Simple", &Effects::GetLowPassSimple()},
				{"HighPass Simple", &Effects::GetHighPassSimple()},
				{"Duck Volume", &Effects::GetDuckVolume()},
			};
			return effects;
		}

		void RegisterBuiltinEffects(const void* owner) {
			for (const AudioBuiltinEffect& effect : GetBuiltinEffects()) {
				if (FindEffect(effect.name) != effect.definition) {
					AudioEffects::Register(effect.name, effect.definition, owner);
				}
			}
		}

	} // namespace AudioImpl

} // namespace Shalltear
