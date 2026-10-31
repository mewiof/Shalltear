#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Audio/AudioEffect.h"

#include "Shalltear/Core/Containers.h"

#include <string_view>

namespace Shalltear {

	namespace AudioImpl {

		struct AudioEffectEntry {
			IAudioEffectDefinition* definition = nullptr;
			const void* owner = nullptr;
		};

		IAudioEffectDefinition* FindEffect(std::string_view name);

		const void* FindEffectOwner(const IAudioEffectDefinition* definition);

		inline constexpr u32 NO_EFFECT_PARAMETER = ~0u;
		u32 FindEffectParameter(const IAudioEffectDefinition& definition, std::string_view name);

		void ResolveParameterValues(const IAudioEffectDefinition& definition, Vector<f32>& values);

		f32 ClampParameterValue(const IAudioEffectDefinition& definition, u32 index, f32 value);

		u32 GetRegisteredEffectCount();
		std::string_view GetRegisteredEffectName(u32 index);

		void TakeEffectsOwnedBy(const void* owner, Vector<IAudioEffectDefinition*>& out);

		void ShutdownEffectRegistry();

	} // namespace AudioImpl

} // namespace Shalltear
