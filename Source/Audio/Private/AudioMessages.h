#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Audio/AudioSpatializer.h"

#include "Shalltear/Core/Types.h"

namespace Shalltear {

	class IAudioEffect;
	class IAudioGeneratorInstance;

	namespace AudioImpl {

		struct AudioClipData;
		struct AudioStream;
		struct AudioVoiceChain;

		inline constexpr f32 MAX_DOPPLER_RATIO = 4.0f;

		struct AudioVoicePlacement {
			u32 generation = 0;
			IAudioSpatializer* spatializer = nullptr;
			void* state = nullptr;
			AudioSpatialParams params;
			f32 blend = 0.0f;
			f32 gain = 1.0f;
			f32 doppler = 1.0f;
		};

		enum class AudioCommandKind : u8 {
			Play = 0,
			Stop,
			Pause,
			Resume,
			SetVolume,
			SetPitch,
			SetStereoPan,
			SetLoop,
			Seek,
			Reschedule,
			SetListenerPresent,

			CreateBus,
			DestroyBus,
			SetBusParent,
			SetBusFader,
			SetBusPitch,
			SetBusMute,
			SetBusSolo,
			SetBusClock,
			SetVoiceOutput,
			SetGameClock,
			SetGlobalVolume,

			SetVoiceReal,

			SetBusRow,
			SetBusRowCount,
			SetBusRowLevel,
			SetBusRowBypass,
			SetBusRowEffect,
			SetBusBypassEffects,
			SetBusEffectParameter,
			SetVoiceGenerator,
			SetGeneratorParameter,

			SetBusReceives,

			// main thread frees it 2 blocks after replacing it
			SetVoiceEffects,
			SetVoiceEffectParameter,
			SetVoiceBypassEffects,

			SetVoiceSend,

			// main thread frees it 2 blocks after the voice lets go
			SetVoiceStream,

			SetMeters,
		};

		struct AudioCommand {
			AudioCommandKind kind = AudioCommandKind::Play;
			bool loop = false;
			bool flag = false;
			u8 mode = 0;
			u32 voice = 0;
			u32 generation = 0;
			u32 bus = 0;
			u32 busGeneration = 0;
			u32 other = 0;
			u32 otherGeneration = 0;
			u32 frames = 0;
			union {
				const AudioClipData* clip = nullptr;
				void* block;
				IAudioEffect* effect;
				IAudioGeneratorInstance* generator;
				AudioVoiceChain* chain;
				AudioStream* stream;
			};
			u64 at = 0;
			u64 frame = 0;
			f32 volume = 1.0f;
			f32 pitch = 1.0f;
			f32 stereoPan = 0.0f;
			f32 value = 0.0f;
		};

		enum class AudioEventKind : u8 {
			VoiceEnded = 0,
			BusReleased,
		};

		struct AudioEvent {
			AudioEventKind kind = AudioEventKind::VoiceEnded;
			u32 slot = 0;
			u32 generation = 0;
		};

	} // namespace AudioImpl

} // namespace Shalltear
