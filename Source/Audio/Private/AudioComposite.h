#pragma once

#include "Shalltear/Audio/Audio.h"
#include "Shalltear/Audio/AudioTime.h"

#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear {

	class AudioContainer;
	struct AudioMusicClock;

	namespace AudioImpl {

		class IAudioComposite {
		public:
			virtual ~IAudioComposite() = default;

			virtual void Update() = 0;
			virtual bool IsFinished() const = 0;

			virtual void Stop() = 0;
			virtual void SetScheduledStartTime(AudioTime time) = 0;
			virtual void SetScheduledEndTime(AudioTime time) = 0;
			virtual void Pause() = 0;
			virtual void UnPause() = 0;
			virtual void SetVolume(f32 volume) = 0;
			virtual void SetPitch(f32 pitch) = 0;
			virtual void SetStereoPan(f32 stereoPan) = 0;
			virtual void SetLoop(bool loop) = 0;
			virtual void SetOutput(AudioBus output) = 0;
			virtual void SetReverbZoneMix(f32 reverbZoneMix) = 0;
			virtual void SetGeneratorParameter(u32 index, f32 value) = 0;
			virtual void SetSpatialSettings(const AudioSpatialSettings& settings) = 0;
			virtual void SetDistanceCurves(const AudioDistanceCurves* curves) = 0;
			virtual void SetPosition(const Vec3& position, const Vec3& velocity) = 0;
			virtual void SetEffects(std::span<const AudioVoiceEffect> effects) = 0;
			virtual void SetEffectParameter(u32 index, u32 parameter, f32 value) = 0;
			virtual void SetBypassEffects(bool bypass) = 0;
			virtual void SetBypassListenerEffects(bool bypass) = 0;
			virtual u64 GetTimeSamples() = 0;
			virtual void SetTimeSamples(u64 sample) = 0;
			virtual bool IsPlaying() = 0;
			virtual void SetPriority(i32 priority) = 0;
			virtual bool IsVirtual() = 0;

			virtual u32 GetVoices(std::span<AudioVoice> out) const {
				(void)out;
				return 0;
			}

			virtual void SwitchToClip(std::string_view name) { (void)name; }
			virtual void SetLayerVolume(u32 layer, f32 volume) {
				(void)layer;
				(void)volume;
			}
			virtual bool GetMusicClock(AudioMusicClock& out) const {
				(void)out;
				return false;
			}

			virtual bool KeepsDeviceTime() const { return false; }
		};

		AudioTime GetEarliestExactStart();

		struct AudioPlaybackClock {
			bool device = false;

			AudioTime Now() const;
			AudioTime EarliestExactStart() const;
		};

		AudioPlaybackClock GetPlaybackClock(AudioBus output);

		IAudioComposite* FindComposite(AudioVoice voice);

		AudioVoice AdoptComposite(IAudioComposite* composite);

		void UpdateComposites();

		void ReleaseComposites();

		u32 StopGameComposites();

		IAudioComposite* CreateContainerPlayback(Handle<AudioResource> resource, const AudioContainer& container,
												 const AudioPlayParams& params);

		void ReleaseContainerState();

		void RegisterContainerImporters(const void* owner);

	} // namespace AudioImpl

} // namespace Shalltear
