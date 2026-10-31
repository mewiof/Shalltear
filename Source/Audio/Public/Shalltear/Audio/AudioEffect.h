#pragma once

#include "Shalltear/Audio/AudioAPI.h"
#include "Shalltear/Audio/AudioSpatializer.h"

#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear {

	// strings must outlive the registration
	struct AudioParameterDefinition {
		std::string_view name;
		std::string_view unit;
		f32 minimum = 0.0f;
		f32 maximum = 1.0f;
		f32 defaultValue = 0.0f;
	};

	struct AudioEffectContext {
		u32 sampleRate = 0;
		u32 blockFrames = 0;
		u32 channels = 0;
		AudioSpeaker speakers[AUDIO_MAX_CHANNELS]{};
		std::span<const f32> parameters;
	};

	class IAudioEffect {
	public:
		virtual void Process(AudioBlock& block, const AudioBlock* sidechain) [[clang::nonblocking]] = 0;

		virtual void SetParameter(u32 index, f32 value) [[clang::nonblocking]] {
			(void)index;
			(void)value;
		}

		virtual bool IsQuiet() const [[clang::nonblocking]] { return false; }

		// once after the render thread lets go
		virtual void Release() = 0;
	protected:
		~IAudioEffect() = default;
	};

	class IAudioEffectDefinition {
	public:
		virtual ~IAudioEffectDefinition() = default;

		virtual std::span<const AudioParameterDefinition> GetParameters() const { return {}; }

		virtual bool IsSidechainTarget() const { return false; }

		virtual IAudioEffect* CreateEffect(const AudioEffectContext& context) = 0;
	};

	namespace AudioEffects {

		SHALLTEAR_AUDIO_API bool Register(std::string_view name, IAudioEffectDefinition* definition, const void* owner);

		SHALLTEAR_AUDIO_API u32 UnregisterOwnedBy(const void* owner);

	} // namespace AudioEffects

} // namespace Shalltear
