#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Core/Types.h"

#include "AudioChannels.h"
#include "AudioClipData.h"
#include "AudioMessages.h"
#include "AudioQoa.h"

namespace Shalltear {

	class IAudioEffectDefinition;

	namespace AudioImpl {

		struct AudioStream;

		enum class VoicePhase : u8 {
			Free = 0,
			Scheduled,
			Playing,
			Stopping,
		};

		inline constexpr f32 MIN_PITCH = -3.0f;
		inline constexpr f32 MAX_PITCH = 3.0f;

		inline constexpr f32 MAX_VOICE_GAIN = 16.0f;

		inline constexpr u32 NO_DECODER = ~0u;

		struct AudioVoiceChain {
			IAudioEffect** effects = nullptr;
			IAudioEffectDefinition** definitions = nullptr;
			u32 count = 0;
			bool onOutput = false;
			bool keptWhileVirtual = false;
		};

		struct AudioVoiceDecoder {
			bool gainsValid = false;
			f32 gains[MAX_AUDIO_CHANNELS * MAX_AUDIO_CHANNELS]{};
			QoaCursor qoa;
		};

		struct AudioVoiceState {
			VoicePhase phase = VoicePhase::Free;
			bool loop = false;
			bool paused = false;
			bool pausing = false;
			bool held = false;
			u32 generation = 0;
			u32 bus = 0;
			const AudioClipData* clip = nullptr;

			u64 startAt = 0;
			u64 stopAt = 0;

			// clip frames
			f64 position = 0.0;
			f64 rateRatio = 1.0;

			f32 volume = 1.0f;
			f32 targetVolume = 1.0f;
			f32 pitch = 1.0f;
			f32 targetPitch = 1.0f;
			f32 stereoPan = 0.0f;
			f32 targetStereoPan = 0.0f;

			bool wantsReal = true;
			u32 decoder = NO_DECODER;
			u32 stopFade = 0;

			IAudioSpatializer* spatializer = nullptr;
			void* spatializerState = nullptr;
			AudioSpatialParams spatial;
			f32 blend = 0.0f;
			f32 targetBlend = 0.0f;
			f32 gain3D = 1.0f;
			f32 targetGain3D = 1.0f;
			f32 doppler = 1.0f;
			f32 targetDoppler = 1.0f;

			AudioVoiceChain* chain = nullptr;
			bool bypassEffects = false;

			u32 send = 0;
			u32 sendGeneration = 0;
			f32 sendLevel = 0.0f;
			f32 targetSendLevel = 0.0f;

			AudioStream* stream = nullptr;
		};

		struct AudioMixScratch {
			f32* dry = nullptr;
			f32* wet = nullptr;
			u32 stride = 0;
			f32* post = nullptr;
		};

		struct AudioVoiceClock {
			bool hold = false;
			f32 rateStart = 1.0f;
			f32 rateEnd = 1.0f;
		};

		void StartVoice(AudioVoiceState& voice, const AudioClipData* clip, u32 generation, u32 deviceRate, u64 startAt, u64 startFrame,
						f32 volume, f32 pitch, f32 stereoPan, bool loop, u32 bus) [[clang::nonblocking]];

		void SeekVoice(AudioVoiceState& voice, u64 frame) [[clang::nonblocking]];

		void PlaceVoice(AudioVoiceState& voice, const AudioVoicePlacement& placement, bool start) [[clang::nonblocking]];

		bool MixVoice(AudioVoiceState& voice, AudioVoiceDecoder* decoder, bool fadeOut, const ChannelLayout& output, u64 blockStart, u32 frames,
					  f32* bus, u32 stride, const AudioVoiceClock& clock, const AudioMixScratch& scratch) [[clang::nonblocking]];

	} // namespace AudioImpl

} // namespace Shalltear
