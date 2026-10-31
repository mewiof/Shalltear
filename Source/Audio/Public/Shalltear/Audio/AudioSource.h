#pragma once

#include "Shalltear/Audio/Audio.h"
#include "Shalltear/Audio/AudioAPI.h"
#include "Shalltear/Audio/AudioConcurrency.h"
#include "Shalltear/Audio/AudioMixer.h"
#include "Shalltear/Audio/AudioResource.h"
#include "Shalltear/Audio/AudioTime.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	namespace AudioImpl {

		struct AudioSourceAccess;
		struct AudioProfilerAccess;

	} // namespace AudioImpl

	enum class AudioSourceCurveType : u8 {
		CustomRolloff = 0,
		SpatialBlend = 1,
		ReverbZoneMix = 2,
		Spread = 3,
	};

	class SHALLTEAR_AUDIO_API AudioSource : public Component {
	public:
		SLT_REFLECT_COMPONENT(AudioSource, Component)
		SLT_REFLECT_FIELD(_resource)
		SLT_REFLECT_FIELD(_output)
		SLT_REFLECT_FIELD(_mute)
		SLT_REFLECT_FIELD(_spatialize)
		SLT_REFLECT_FIELD(_spatializePostEffects)
		SLT_REFLECT_FIELD(_bypassEffects)
		SLT_REFLECT_FIELD(_bypassListenerEffects)
		SLT_REFLECT_FIELD(_bypassReverbZones)
		SLT_REFLECT_FIELD(_playOnAwake)
		SLT_REFLECT_FIELD(_loop)
		SLT_REFLECT_FIELD(_priority, SLT_RANGE(0, 256))
		SLT_REFLECT_FIELD(_concurrency)
		SLT_REFLECT_FIELD(_volume, SLT_RANGE(0, 1))
		SLT_REFLECT_FIELD(_pitch, SLT_RANGE(-3, 3))
		SLT_REFLECT_FIELD(_stereoPan, SLT_RANGE(-1, 1))
		SLT_REFLECT_FIELD(_spatialBlend, SLT_RANGE(0, 1))
		SLT_REFLECT_FIELD(_reverbZoneMix, SLT_RANGE(0, 1.1))
		SLT_REFLECT_FIELD(_dopplerLevel, SLT_RANGE(0, 5), SLT_HEADER("3D Sound Settings"))
		SLT_REFLECT_FIELD(_spread, SLT_RANGE(0, 360))
		SLT_REFLECT_FIELD(_rolloffMode)
		SLT_REFLECT_FIELD(_minDistance)
		SLT_REFLECT_FIELD(_maxDistance)
		SLT_REFLECT_SUBOBJECT(_customRolloffCurve)
		SLT_REFLECT_SUBOBJECT(_spatialBlendCurve, SLT_HIDDEN)
		SLT_REFLECT_SUBOBJECT(_spreadCurve, SLT_HIDDEN)
		SLT_REFLECT_SUBOBJECT(_reverbZoneMixCurve, SLT_HIDDEN)
		SLT_REFLECT_FIELD(_occlusion, SLT_RANGE(0, 1))
		SLT_END_REFLECT()

		AudioSource() = default;
		~AudioSource() override;

		Handle<AudioResource> GetResource() const { return _resource; }
		void SetResource(Handle<AudioResource> resource) { _resource = resource; }

		Handle<AudioMixerGroup> GetOutputAudioMixerGroup() const { return _output; }
		void SetOutputAudioMixerGroup(Handle<AudioMixerGroup> output) { _output = output; }

		bool GetMute() const { return _mute; }
		void SetMute(bool mute) { _mute = mute; }

		bool GetPlayOnAwake() const { return _playOnAwake; }
		void SetPlayOnAwake(bool playOnAwake) { _playOnAwake = playOnAwake; }

		bool GetLoop() const { return _loop; }
		void SetLoop(bool loop) { _loop = loop; }

		// 0 most important, 256 least
		i32 GetPriority() const { return _priority; }
		void SetPriority(i32 priority) { _priority = priority; }

		Handle<AudioConcurrency> GetConcurrency() const { return _concurrency; }
		void SetConcurrency(Handle<AudioConcurrency> concurrency) { _concurrency = concurrency; }

		f32 GetVolume() const { return _volume; }
		void SetVolume(f32 volume) { _volume = volume; }

		f32 GetPitch() const { return _pitch; }
		void SetPitch(f32 pitch) { _pitch = pitch; }

		f32 GetStereoPan() const { return _stereoPan; }
		void SetStereoPan(f32 stereoPan) { _stereoPan = stereoPan; }

		f32 GetSpatialBlend() const { return _spatialBlend; }
		void SetSpatialBlend(f32 spatialBlend) {
			_spatialBlend = spatialBlend;
			_spatialBlendCurve.keys.clear();
		}

		f32 GetDopplerLevel() const { return _dopplerLevel; }
		void SetDopplerLevel(f32 dopplerLevel) { _dopplerLevel = dopplerLevel; }

		// degrees in [0, 360], setting it removes the curve
		f32 GetSpread() const { return _spread; }
		void SetSpread(f32 spread) {
			_spread = spread;
			_spreadCurve.keys.clear();
		}

		f32 GetMinDistance() const { return _minDistance; }
		void SetMinDistance(f32 minDistance) { _minDistance = minDistance; }
		f32 GetMaxDistance() const { return _maxDistance; }
		void SetMaxDistance(f32 maxDistance) { _maxDistance = maxDistance; }

		AudioRolloffMode GetRolloffMode() const { return _rolloffMode; }
		void SetRolloffMode(AudioRolloffMode rolloffMode) { _rolloffMode = rolloffMode; }

		AnimationCurve GetCustomCurve(AudioSourceCurveType type) const;
		void SetCustomCurve(AudioSourceCurveType type, const AnimationCurve& curve);

		f32 GetOcclusion() const { return _occlusion; }
		void SetOcclusion(f32 occlusion) { _occlusion = occlusion; }

		bool GetSpatialize() const { return _spatialize; }
		void SetSpatialize(bool spatialize) { _spatialize = spatialize; }

		bool GetSpatializePostEffects() const { return _spatializePostEffects; }
		void SetSpatializePostEffects(bool spatializePostEffects) { _spatializePostEffects = spatializePostEffects; }

		bool GetBypassEffects() const { return _bypassEffects; }
		void SetBypassEffects(bool bypassEffects) { _bypassEffects = bypassEffects; }

		bool GetBypassListenerEffects() const { return _bypassListenerEffects; }
		void SetBypassListenerEffects(bool bypassListenerEffects) { _bypassListenerEffects = bypassListenerEffects; }

		bool GetBypassReverbZones() const { return _bypassReverbZones; }
		void SetBypassReverbZones(bool bypassReverbZones) { _bypassReverbZones = bypassReverbZones; }

		f32 GetReverbZoneMix() const { return _reverbZoneMix; }
		void SetReverbZoneMix(f32 reverbZoneMix) {
			_reverbZoneMix = reverbZoneMix;
			_reverbZoneMixCurve.keys.clear();
		}

		AudioVelocityUpdateMode GetVelocityUpdateMode() const { return _velocityUpdateMode; }
		void SetVelocityUpdateMode(AudioVelocityUpdateMode mode) { _velocityUpdateMode = mode; }

		void Play();

		// from now, on the DSP clock
		void PlayDelayed(f32 delay);

		void PlayScheduled(AudioTime time);

		void PlayOneShot(Handle<AudioResource> resource, f32 volumeScale = 1.0f);

		void Stop();

		void Pause();

		void UnPause();

		bool IsPlaying() const;

		bool IsVirtual() const;

		AudioVoice GetVoice() const { return _voice; }

		void SetScheduledStartTime(AudioTime time);
		void SetScheduledEndTime(AudioTime time);

		u64 GetTimeSamples() const;
		void SetTimeSamples(u64 sample);

		f32 GetTime() const;
		void SetTime(f32 seconds);
	protected:
		void OnEnable() override;
		void OnDisable() override;
	private:
		friend struct AudioImpl::AudioSourceAccess;
		friend struct AudioImpl::AudioProfilerAccess;

		struct OneShot {
			AudioVoice voice;
			f32 volumeScale = 1.0f;
		};

		f32 EffectiveVolume() const;
		f32 EffectiveReverbZoneMix() const;
		i32 EffectivePriority() const;
		AudioSpatialSettings EffectiveSpatial() const;

		AudioBus ResolveOutput() const;

		bool CurvesMoved() const;
		void SyncCurves();
		const AudioDistanceCurves* AppliedCurves() const;

		void Start(AudioTime at, u64 from);

		AudioPlayParams MakePlayParams() const;

		u32 DescribeAppliedEffects(AudioVoiceEffect* chain) const;

		void TickFilters();

		void SyncEffects();

		void Tick(const Vec3& position, const Vec3& velocity);

		Handle<AudioResource> _resource;
		Handle<AudioMixerGroup> _output;
		bool _mute = false;
		bool _spatialize = false;
		bool _spatializePostEffects = false;
		bool _bypassEffects = false;
		bool _bypassListenerEffects = false;
		bool _bypassReverbZones = false;
		bool _playOnAwake = true;
		bool _loop = false;
		i32 _priority = 128;
		Handle<AudioConcurrency> _concurrency;
		f32 _volume = 1.0f;
		f32 _pitch = 1.0f;
		f32 _stereoPan = 0.0f;
		f32 _spatialBlend = 0.0f;
		f32 _reverbZoneMix = 1.0f;
		f32 _dopplerLevel = 1.0f;
		f32 _spread = 0.0f;
		f32 _minDistance = 1.0f;
		f32 _maxDistance = 500.0f;
		AudioRolloffMode _rolloffMode = AudioRolloffMode::Logarithmic;
		AnimationCurve _customRolloffCurve{{Keyframe{.time = 0.0f, .value = 1.0f}, Keyframe{.time = 1.0f, .value = 0.0f}},
										   GetMemoryResource(MemTag::Scene)};
		AnimationCurve _spatialBlendCurve{GetMemoryResource(MemTag::Scene)};
		AnimationCurve _spreadCurve{GetMemoryResource(MemTag::Scene)};
		AnimationCurve _reverbZoneMixCurve{GetMemoryResource(MemTag::Scene)};
		f32 _occlusion = 0.0f;

		AudioVelocityUpdateMode _velocityUpdateMode = AudioVelocityUpdateMode::Auto;

		AudioVoice _voice;
		Vector<OneShot> _oneShots{GetMemoryResource(MemTag::Scene)};

		f32 _appliedVolume = 1.0f;
		f32 _appliedPitch = 1.0f;
		f32 _appliedStereoPan = 0.0f;
		i32 _appliedPriority = 128;
		bool _appliedLoop = false;
		AudioBus _appliedOutput;
		AudioSpatialSettings _appliedSpatial;
		bool _appliedBypass = false;
		bool _appliedBypassListener = false;
		f32 _appliedReverbZoneMix = 1.0f;
		AudioDistanceCurves _appliedCurves;
		struct AppliedFilter {
			str effect;
			u32 first = 0;
			u32 count = 0;
			AnimationCurve distance{GetMemoryResource(MemTag::Scene)};
			u32 distanceParameter = 0;
		};
		Vector<AppliedFilter> _appliedFilters{GetMemoryResource(MemTag::Scene)};
		Vector<f32> _appliedValues{GetMemoryResource(MemTag::Scene)};

		Vec3 _velocity;

		u64 _startSample = 0;
		bool _paused = false;
		bool _awakePending = false;
	};

} // namespace Shalltear
