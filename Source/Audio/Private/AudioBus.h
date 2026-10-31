#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Core/Types.h"

#include "AudioChannels.h"

namespace Shalltear {

	class IAudioEffect;

	namespace AudioImpl {

		// 1st 2 values must match AudioMixerGroupClock
		enum class AudioBusClock : u8 {
			Wall = 0,
			Gameplay = 1,
			Inherit = 2,
			Device = 3,
		};

		struct AudioRamp {
			f32 value = 1.0f;
			f32 target = 1.0f;
			u32 remaining = 0;

			f32 start = 1.0f;
			f32 end = 1.0f;
			u32 covered = 0;

			void Jump(f32 to) [[clang::nonblocking]] {
				value = target = start = end = to;
				remaining = covered = 0;
			}

			void Walk(f32 to, u32 frames) [[clang::nonblocking]] {
				target = to;
				remaining = frames > 0 ? frames : 1;
			}

			void Advance(u32 frames) [[clang::nonblocking]] {
				start = value;
				if (remaining == 0) {
					end = value;
					covered = 0;
					return;
				}
				covered = frames < remaining ? frames : remaining;
				value += (target - value) * (static_cast<f32>(covered) / static_cast<f32>(remaining));
				remaining -= covered;
				if (remaining == 0) {
					value = target;
				}
				end = value;
			}

			f32 At(u32 frame) const [[clang::nonblocking]] {
				if (covered == 0 || frame + 1 >= covered) {
					return covered == 0 ? start : end;
				}
				return start + (end - start) * (static_cast<f32>(frame + 1) / static_cast<f32>(covered));
			}

			bool IsFlat() const [[clang::nonblocking]] { return covered == 0 || start == end; }
		};

		inline constexpr u32 MAX_BUS_ROWS = 16;

		inline constexpr u32 NO_BUS_TARGET = ~0u;

		inline constexpr u8 NO_BUS_ROW = 0xFF;

		enum class AudioBusRowKind : u8 {
			Fader = 0,
			Send,
			Effect,
			Receive,
		};

		struct AudioBusRow {
			AudioBusRowKind kind = AudioBusRowKind::Fader;
			bool bypass = false;
			bool wetMixing = false;
			u8 targetRow = NO_BUS_ROW;
			u8 receive = NO_BUS_ROW;
			u32 target = NO_BUS_TARGET;
			AudioRamp level;
			IAudioEffect* effect = nullptr;
		};

		inline constexpr size_t BUS_ROWS_BYTES = ((sizeof(AudioBusRow) * MAX_BUS_ROWS + 63) / 64) * 64;

		inline AudioBusRow* BusRowsOf(void* block) [[clang::nonblocking]] { return static_cast<AudioBusRow*>(block); }

		inline f32* BusSamplesOf(void* block) [[clang::nonblocking]] {
			return reinterpret_cast<f32*>(static_cast<u8*>(block) + BUS_ROWS_BYTES);
		}

		inline constexpr u32 BUS_BUFFER_FRAMES = 256;
		inline constexpr size_t BUS_BUFFER_SAMPLES = static_cast<size_t>(MAX_AUDIO_CHANNELS) * BUS_BUFFER_FRAMES;

		struct AudioBusState {
			bool live = false;
			bool solo = false;
			bool followTimeScale = false;
			bool bypassEffects = false;
			AudioBusClock clock = AudioBusClock::Wall;
			u32 generation = 0;
			u32 parent = 0;
			f32* buffer = nullptr;
			AudioBusRow* rows = nullptr;
			u32 rowCount = 0;
			// replaced whole by main thread
			f32* receives = nullptr;
			u32 receiveCapacity = 0;

			AudioRamp fader;
			AudioRamp mute;
			AudioRamp pitch;
			AudioRamp soloGain;

			bool touched = false;
			u32 receiveTouched = 0;
			bool hold = false;
			bool follows = false;
			bool inSoloPath = false;
			f32 pitchStart = 1.0f;
			f32 pitchEnd = 1.0f;
			f32 rateStart = 1.0f;
			f32 rateEnd = 1.0f;
		};

	} // namespace AudioImpl

} // namespace Shalltear
