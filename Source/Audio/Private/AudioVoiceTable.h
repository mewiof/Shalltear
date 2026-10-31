#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Audio/Audio.h"
#include "Shalltear/Audio/AudioGenerator.h"
#include "Shalltear/Audio/AudioResource.h"
#include "Shalltear/Audio/AudioSpatializer.h"

#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include "AudioMessages.h"
#include "AudioVoiceManager.h"

#include <span>

namespace Shalltear {

	namespace AudioImpl {

		struct AudioClipData;
		struct AudioStream;
		struct AudioVoiceEffects;

		enum class VoiceClaim : u8 {
			Free = 0,
			Playing,
			Paused,
			Stopping,
		};

		struct AudioVoiceRecord {
			VoiceClaim claim = VoiceClaim::Free;
			u32 generation = 0;
			AudioClipData* clip = nullptr;
			u64 askedPosition = 0;
			u64 askedUntil = 0;

			AudioSpatialSettings spatial;
			Vec3 position;
			Vec3 velocity;
			IAudioSpatializer* spatializer = nullptr;
			void* spatializerState = nullptr;
			AudioVoicePlacement placement;
			AudioDistanceCurves* curves = nullptr;
			f32 distanceShare = 0.0f;

			i32 priority = DEFAULT_PRIORITY;
			f32 volume = 1.0f;
			u32 bus = 0;
			u32 busGeneration = 0;
			bool toListener = false;
			bool loop = false;
			bool bypassEffects = false;
			bool bypassListenerEffects = false;
			f32 reverbZoneMix = 1.0f;
			u32 send = 0;
			u32 sendGeneration = 0;
			f32 sendLevel = 0.0f;
			f32 audibility = 0.0f;
			bool real = false;
			u32 realIndex = NO_VOICE_SLOT;
			bool releasing = false;
			u64 sequence = 0;

			u64 groupAsset = 0;
			u64 groupOwner = 0;
			u32 groupNext = NO_VOICE_SLOT;
			u32 groupPrevious = NO_VOICE_SLOT;

			Handle<AudioResource> resource;

			bool isGenerator = false;
			IAudioGeneratorInstance* generatorInstance = nullptr;
			bool orphaned = false;
			const void* generatorImage = nullptr;

			AudioVoiceEffects* effects = nullptr;

			AudioStream* stream = nullptr;
		};

		class AudioVoiceTable {
		public:
			AudioVoiceTable() = default;
			~AudioVoiceTable() { Shutdown(); }
			AudioVoiceTable(const AudioVoiceTable&) = delete;
			AudioVoiceTable& operator=(const AudioVoiceTable&) = delete;

			void Initialize(u32 limit, u32 headroom);

			// only after device close + render thread join
			void Shutdown();

			void Resize(u32 limit, u32 headroom);

			bool IsInitialized() const { return _records != nullptr; }
			u32 GetCapacity() const { return _capacity; }
			u32 GetLimit() const { return _limit; }

			bool Claim(AudioClipData* clip, u32& slot, u32& generation);

			void Abandon(u32 slot);

			void OnEnded(u32 slot, u32 generation);

			AudioVoiceRecord* Find(u32 slot, u32 generation);

			u32 GetGeneration(u32 slot) const { return _records && slot < _capacity ? _records[slot].generation : 0; }

			AudioVoiceRecord& At(u32 slot) { return _records[slot]; }

			void SetClaim(u32 slot, VoiceClaim claim);

			void BeginStop(u32 slot, bool releasing);

			void SetReal(u32 slot, bool real);

			std::span<const u32> GetRealSlots() const { return {_realSlots, _realCount}; }

			void JoinGroup(u32 slot, AudioConcurrencyGroup& group, u64 now, u64 retriggerFrames);

			AudioConcurrencyGroups& GetGroups() { return _groups; }

			void SetGenerator(u32 slot, IAudioGeneratorInstance* instance, const void* image);

			void SetOrphaned(u32 slot, bool orphaned);

			u32 CountGenerators() const { return _generators; }
			u32 CountOrphans() const { return _orphans; }

			u32 CountLive() const { return _live; }
			u32 CountBudgeted() const { return _budgeted; }
			u32 CountClaimed() const;
		private:
			// writers subtract before a change, add after
			void Account(const AudioVoiceRecord& record, i32 sign);

			void LeaveGroup(u32 slot);

			void Release(u32 slot);

			void MarkReal(u32 slot, bool real);

			AudioVoiceRecord* _records = nullptr;
			u32* _realSlots = nullptr;
			u32 _realCount = 0;
			u32 _capacity = 0;
			u32 _limit = 0;
			u32 _live = 0;
			u32 _budgeted = 0;
			u32 _generators = 0;
			u32 _orphans = 0;
			u64 _sequence = 0;
			u32 _cursor = 0;
			AudioConcurrencyGroups _groups;
		};

	} // namespace AudioImpl

} // namespace Shalltear
