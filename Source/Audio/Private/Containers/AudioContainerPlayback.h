#pragma once

#include "AudioComposite.h"

#include "Shalltear/Audio/Audio.h"
#include "Shalltear/Audio/AudioConcurrency.h"
#include "Shalltear/Audio/AudioResource.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear {

	class Asset;
	class AudioClip;

	namespace AudioImpl {

		struct AudioContainerChild {
			AudioVoice voice;
			f32 volumeScale = 1.0f;
			f32 pitchScale = 1.0f;
			AudioTime start;
			u64 startSample = 0;
			u32 tag = 0;
		};

		class AudioContainerPlayback : public IAudioComposite {
		public:
			AudioContainerPlayback(Handle<AudioResource> resource, const AudioPlayParams& params);
			~AudioContainerPlayback() override;

			AudioContainerPlayback(const AudioContainerPlayback&) = delete;
			AudioContainerPlayback& operator=(const AudioContainerPlayback&) = delete;

			bool IsFinished() const override;
			void Stop() override;
			void SetScheduledStartTime(AudioTime time) override;
			void SetScheduledEndTime(AudioTime time) override;
			void Pause() override;
			void UnPause() override;
			void SetVolume(f32 volume) override;
			void SetPitch(f32 pitch) override;
			void SetStereoPan(f32 stereoPan) override;
			void SetLoop(bool loop) override;
			void SetOutput(AudioBus output) override;
			void SetReverbZoneMix(f32 reverbZoneMix) override;
			void SetGeneratorParameter(u32 index, f32 value) override;
			void SetSpatialSettings(const AudioSpatialSettings& settings) override;
			void SetDistanceCurves(const AudioDistanceCurves* curves) override;
			void SetPosition(const Vec3& position, const Vec3& velocity) override;
			void SetEffects(std::span<const AudioVoiceEffect> effects) override;
			void SetEffectParameter(u32 index, u32 parameter, f32 value) override;
			void SetBypassEffects(bool bypass) override;
			void SetBypassListenerEffects(bool bypass) override;
			bool IsPlaying() override;
			void SetPriority(i32 priority) override;
			bool IsVirtual() override;
			u32 GetVoices(std::span<AudioVoice> out) const override;
			bool KeepsDeviceTime() const override { return _timebase.device; }
			u64 GetTimeSamples() override;
			void SetTimeSamples(u64 sample) override;

			std::span<const AudioContainerChild> GetChildren() const { return {_children.data(), _children.size()}; }
			u32 GetStartedCount() const { return _started; }
		protected:
			virtual bool HasMoreToStart() const = 0;

			virtual void ShiftTimeline(i64 frames) { (void)frames; }

			virtual void ApplyLoop();

			AudioVoice StartChild(Handle<AudioResource> resource, AudioTime at, u64 startSample, bool loop, f32 volumeScale,
								  f32 pitchScale, u32 tag);

			void PruneChildren();

			template <typename T>
			const T* ResolveContainer() const {
				return static_cast<const T*>(ResolveResource());
			}

			static u64 FramesOf(Handle<AudioResource> resource, f32 pitch);

			Vector<AudioContainerChild> _children{GetMemoryResource(MemTag::Audio)};

			AudioPlaybackClock _timebase;

			f32 _volume = 1.0f;
			f32 _pitch = 1.0f;
			bool _loop = false;
			u64 _startSample = 0;
			AudioTime _at;

			bool _stopped = false;
			bool _paused = false;
			AudioTime _pausedAt;
			AudioTime _end;
		private:
			const Object* ResolveResource() const;

			Handle<AudioResource> _resource;
			u32 _started = 0;
			Handle<Asset> _held;

			f32 _stereoPan = 0.0f;
			AudioBus _output;
			f32 _reverbZoneMix = 1.0f;
			AudioSpatialSettings _spatial;
			AudioDistanceCurves _curves;
			bool _curved = false;
			Vec3 _position;
			Vec3 _velocity;
			i32 _priority = 128;
			Handle<AudioConcurrency> _concurrency;
			Handle<Object> _owner;
			bool _bypassEffects = false;
			bool _bypassListenerEffects = false;

			struct OwnedEffect {
				str effect;
				Vector<f32> parameters{GetMemoryResource(MemTag::Audio)};
				AnimationCurve distance{GetMemoryResource(MemTag::Audio)};
				u32 distanceParameter = 0;
			};
			Vector<OwnedEffect> _effects{GetMemoryResource(MemTag::Audio)};
		};

	} // namespace AudioImpl

} // namespace Shalltear
