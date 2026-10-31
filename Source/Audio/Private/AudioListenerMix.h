#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Audio/Audio.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Types.h"

#include "AudioBus.h"
#include "AudioChannels.h"
#include "AudioFilterAccess.h"
#include "AudioReverbPresets.h"

#include <span>
#include <string_view>

namespace Shalltear {

	class AudioListener;
	class IAudioEffect;
	class IAudioEffectDefinition;

	namespace AudioImpl {

		struct AudioDetachment;

		inline constexpr u32 MAX_LISTENER_FILTERS = MAX_BUS_ROWS - 1;

		struct AudioListenerFilterRow {
			u64 component = 0;
			str effect;
			IAudioEffectDefinition* definition = nullptr;
			IAudioEffect* instance = nullptr;
			f32 values[MAX_FILTER_PARAMETERS]{};
			u32 count = 0;
		};

		struct AudioListenerMix {
			AudioBus bus;
			AudioBus output;
			AudioBus parent;
			Vector<AudioListenerFilterRow> rows{GetMemoryResource(MemTag::Audio)};

			AudioBus zones;
			AudioBus zonesParent;
			IAudioEffectDefinition* zoneDefinition = nullptr;
			IAudioEffect* zoneReverb = nullptr;
			f32 zoneValues[SFX_REVERB_PARAMETERS]{};
			bool inZone = false;

			u32 madeRate = 0;
			ChannelLayout madeLayout;
		};

		inline bool IsMixing(const AudioListenerMix& mix) { return mix.bus.IsValid() || mix.output.IsValid(); }

		void SyncListenerMix(AudioListenerMix& mix, const AudioListener& listener, std::span<const AudioReverbZoneShape> zones);

		void ReleaseListenerMix(AudioListenerMix& mix);

		void ForgetListenerMix(AudioListenerMix& mix);

		void DetachListenerMixEffects(AudioListenerMix& mix, IAudioEffectDefinition* definition, AudioDetachment& detachment);

		void ResolveListenerMixEffect(AudioListenerMix& mix, std::string_view name);

		void RemakeListenerMixEffects(AudioListenerMix& mix);

	} // namespace AudioImpl

} // namespace Shalltear
