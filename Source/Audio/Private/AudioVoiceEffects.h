#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Audio/Audio.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear {

	class IAudioEffectDefinition;

	namespace AudioImpl {

		struct AudioVoiceChain;
		struct ChannelLayout;

		struct AudioVoiceEffectRow {
			str name;
			IAudioEffectDefinition* definition = nullptr;
			Vector<f32> values{GetMemoryResource(MemTag::Audio)};
			AnimationCurve distance{GetMemoryResource(MemTag::Audio)};
			u32 distanceParameter = 0;
		};

		struct AudioVoiceEffects {
			Vector<AudioVoiceEffectRow> rows{GetMemoryResource(MemTag::Audio)};
			bool followsDistance = false;
			AudioVoiceChain* chain = nullptr;
		};

		void DescribeEffects(AudioVoiceEffects& into, std::span<const AudioVoiceEffect> effects);

		void ReadDistance(AudioVoiceEffects& description, f32 share);

		bool ResolveWaiting(AudioVoiceEffects& description, std::string_view name);

		AudioVoiceChain* MakeVoiceChain(const AudioVoiceEffects& description, u32 sampleRate, const ChannelLayout& layout, bool onOutput,
										bool keptWhileVirtual, const AudioVoiceChain* carry);

		// only once the render thread can't read chain
		void ReleaseVoiceChain(AudioVoiceChain* chain, const AudioVoiceChain* kept = nullptr);

		bool ChainRuns(const AudioVoiceChain& chain, const IAudioEffectDefinition* definition);

	} // namespace AudioImpl

} // namespace Shalltear
