#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Audio/AudioConcurrency.h"

#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear {

	namespace AudioImpl {

		inline constexpr u32 NO_VOICE_SLOT = ~0u;

		inline constexpr i32 MIN_PRIORITY = 0;
		inline constexpr i32 MAX_PRIORITY = 256;
		inline constexpr i32 DEFAULT_PRIORITY = 128;

		// 1 dB hysteresis
		inline constexpr f32 REAL_VOICE_HYSTERESIS = 1.12201845f;

		struct AudioVoiceRank {
			u32 slot = 0;
			i32 priority = DEFAULT_PRIORITY;
			f32 audibility = 0.0f;
			u64 sequence = 0;
			bool real = false;
			bool wantReal = false;
		};

		bool RanksAbove(const AudioVoiceRank& a, const AudioVoiceRank& b);

		u32 SelectRealVoices(std::span<AudioVoiceRank> voices, u32 budget, std::span<u32> order);

		struct AudioConcurrencyMember {
			u32 slot = NO_VOICE_SLOT;
			i32 priority = DEFAULT_PRIORITY;
			f32 audibility = 0.0f;
			f32 distance = 0.0f;
			u64 sequence = 0;
		};

		u32 ChooseConcurrencyVictim(AudioConcurrencyRule rule, std::span<const AudioConcurrencyMember> members, const AudioConcurrencyMember& incoming);

		struct AudioConcurrencyGroup {
			u64 asset = 0;
			u64 owner = 0;
			u32 count = 0;
			u32 head = NO_VOICE_SLOT;
			u64 lastStart = 0;
			u64 retriggerFrames = 0;
			bool started = false;
		};

		class AudioConcurrencyGroups {
		public:
			AudioConcurrencyGroups() = default;
			~AudioConcurrencyGroups() { Shutdown(); }
			AudioConcurrencyGroups(const AudioConcurrencyGroups&) = delete;
			AudioConcurrencyGroups& operator=(const AudioConcurrencyGroups&) = delete;

			void Initialize(u32 voiceCapacity);
			void Shutdown();

			AudioConcurrencyGroup* Find(u64 asset, u64 owner);

			AudioConcurrencyGroup* FindOrAdd(u64 asset, u64 owner);

			void Sweep(u64 now);

			u32 CountGroups() const { return _used; }
		private:
			void Remove(u32 index);
			u32 Probe(u64 asset, u64 owner) const;

			AudioConcurrencyGroup* _entries = nullptr;
			u32 _mask = 0;
			u32 _used = 0;
		};

		void RegisterConcurrencyImporter(const void* owner);

	} // namespace AudioImpl

} // namespace Shalltear
