#include "Shalltear/Audio/AudioSource.h"
#include "Shalltear/Audio/AudioClip.h"
#include "Shalltear/Audio/AudioFilters.h"

#include "AudioClipAccess.h"
#include "AudioFilterAccess.h"
#include "AudioMixerLive.h"
#include "AudioScenePass.h"

#include "Shalltear/Core/Asset/AssetCues.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Scene/SceneObject.h"

#include <algorithm>

namespace Shalltear {

	namespace {

		f32 Clamp(f32 value, f32 low, f32 high) {
			return value >= low ? (value <= high ? value : high) : low;
		}

		void WarnDisabled(const AudioSource& source) {
			SceneObject* owner = source.GetOwner().Get();
			SLT_LOG_WARN("Audio", "Can not play a disabled audio source ('{}')", owner ? owner->name : str("<no object>"));
		}

		const AnimationCurve* Replacing(const AnimationCurve& curve) {
			return curve.keys.size() >= 2 ? &curve : nullptr;
		}

		bool Told(const AnimationCurve* wanted, const AnimationCurve& applied) {
			return wanted ? *wanted == applied : applied.keys.empty();
		}

		void Tell(const AnimationCurve* wanted, AnimationCurve& applied) {
			if (wanted) {
				applied = *wanted;
			} else {
				applied.keys.clear();
			}
		}

		AnimationCurve SingleKey(f32 value) {
			return AnimationCurve{Keyframe{.time = 0.0f, .value = value}};
		}

	} // anonymous namespace

	AudioSource::~AudioSource() {
		Stop();
		AudioImpl::UnregisterSource(Handle<AudioSource>(this));
	}

	void AudioSource::OnEnable() {
		if (!AudioImpl::IsInWorldScene(*this)) {
			return;
		}
		_awakePending = true;
		AudioImpl::RegisterSource(Handle<AudioSource>(this));
	}

	void AudioSource::OnDisable() {
		_awakePending = false;
		Stop();
		AudioImpl::UnregisterSource(Handle<AudioSource>(this));
	}

	f32 AudioSource::EffectiveVolume() const {
		return _mute ? 0.0f : Clamp(_volume, 0.0f, 1.0f);
	}

	f32 AudioSource::EffectiveReverbZoneMix() const {
		return _bypassReverbZones ? 0.0f : Clamp(_reverbZoneMix, 0.0f, 1.1f);
	}

	i32 AudioSource::EffectivePriority() const {
		return _priority < 0 ? 0 : (_priority > 256 ? 256 : _priority);
	}

	AudioSpatialSettings AudioSource::EffectiveSpatial() const {
		AudioSpatialSettings spatial;
		spatial.spatialBlend = Clamp(_spatialBlend, 0.0f, 1.0f);
		spatial.minDistance = _minDistance > 0.0f ? _minDistance : 0.0f;
		spatial.maxDistance = _maxDistance > 0.0f ? _maxDistance : 0.0f;
		spatial.rolloffMode = _rolloffMode;
		spatial.spread = Clamp(_spread, 0.0f, 360.0f);
		spatial.dopplerLevel = Clamp(_dopplerLevel, 0.0f, 5.0f);
		spatial.occlusion = Clamp(_occlusion, 0.0f, 1.0f);
		spatial.spatialize = _spatialize;
		spatial.spatializePostEffects = _spatializePostEffects;
		return spatial;
	}

	u32 AudioSource::DescribeAppliedEffects(AudioVoiceEffect* chain) const {
		const u32 count = static_cast<u32>(_appliedFilters.size());
		for (u32 index = 0; index < count; ++index) {
			const AppliedFilter& filter = _appliedFilters[index];
			chain[index] = AudioVoiceEffect{filter.effect, {_appliedValues.data() + filter.first, filter.count}};
			if (!filter.distance.keys.empty()) {
				chain[index].distanceCurve = &filter.distance;
				chain[index].distanceParameter = filter.distanceParameter;
			}
		}
		return count;
	}

	AudioPlayParams AudioSource::MakePlayParams() const {
		AudioPlayParams params;
		params.volume = EffectiveVolume();
		params.pitch = Clamp(_pitch, -3.0f, 3.0f);
		params.stereoPan = Clamp(_stereoPan, -1.0f, 1.0f);
		params.output = ResolveOutput();
		params.spatial = EffectiveSpatial();
		params.curves = AppliedCurves();
		params.priority = EffectivePriority();
		params.concurrency = _concurrency;
		params.reverbZoneMix = EffectiveReverbZoneMix();
		params.bypassEffects = _appliedBypass;
		params.bypassListenerEffects = _appliedBypassListener;
		SceneObject* owner = GetOwner().Get();
		params.position = owner ? owner->GetWorldPosition() : Vec3{};
		params.velocity = _velocity;
		params.owner = Handle<Object>(owner);
		return params;
	}

	AudioBus AudioSource::ResolveOutput() const {
		return AudioImpl::ResolveGroupBus(_output);
	}

	void AudioSource::Start(AudioTime at, u64 from) {
		Audio::Stop(_voice);

		SyncEffects();
		SyncCurves();
		AudioVoiceEffect chain[AudioImpl::MAX_SOURCE_FILTERS];
		AudioPlayParams params = MakePlayParams();
		params.effects = {chain, DescribeAppliedEffects(chain)};
		params.at = at;
		params.startSample = from;
		params.loop = _loop;
		_voice = Audio::Play(_resource, params);

		if (_oneShots.empty()) {
			_appliedVolume = params.volume;
			_appliedPitch = params.pitch;
			_appliedStereoPan = params.stereoPan;
			_appliedPriority = params.priority;
			_appliedOutput = params.output;
			_appliedSpatial = params.spatial;
			_appliedReverbZoneMix = params.reverbZoneMix;
		}
		_appliedLoop = _loop;
	}

	void AudioSource::Play() {
		if (!IsEffectivelyEnabled()) {
			WarnDisabled(*this);
			return;
		}
		if (_paused) {
			UnPause();
			if (Audio::IsPlaying(_voice)) {
				return;
			}
		}
		const u64 from = _startSample;
		_startSample = 0;
		Start(AudioTime{}, from);
	}

	void AudioSource::PlayDelayed(f32 delay) {
		const u32 rate = Audio::GetOutputSampleRate();
		const f64 seconds = delay > 0.0f ? static_cast<f64>(delay) : 0.0;
		PlayScheduled(AudioTime{Audio::GetTime().frames + static_cast<u64>(seconds * static_cast<f64>(rate))});
	}

	void AudioSource::PlayScheduled(AudioTime time) {
		if (!IsEffectivelyEnabled()) {
			WarnDisabled(*this);
			return;
		}
		if (_paused) {
			UnPause();
		}
		const u64 from = _startSample;
		_startSample = 0;
		Start(time, from);
	}

	void AudioSource::PlayOneShot(Handle<AudioResource> resource, f32 volumeScale) {
		if (!IsEffectivelyEnabled()) {
			WarnDisabled(*this);
			return;
		}
		if (resource.GetInstanceId() == 0) {
			SLT_LOG_WARN("Audio", "PlayOneShot was called with no audio resource");
			return;
		}
		const f32 scale = volumeScale > 0.0f ? volumeScale : 0.0f;
		SyncEffects();
		SyncCurves();
		AudioVoiceEffect chain[AudioImpl::MAX_SOURCE_FILTERS];
		AudioPlayParams params = MakePlayParams();
		params.effects = {chain, DescribeAppliedEffects(chain)};
		params.volume *= scale;
		const AudioVoice voice = Audio::Play(resource, params);
		if (voice.IsValid()) {
			_oneShots.push_back(OneShot{voice, scale});
		}
	}

	void AudioSource::Stop() {
		Audio::Stop(_voice);
		_voice = {};
		for (const OneShot& shot : _oneShots) {
			Audio::Stop(shot.voice);
		}
		_oneShots.clear();
		_paused = false;
		_startSample = 0;
	}

	void AudioSource::Pause() {
		if (!IsPlaying()) {
			return;
		}
		Audio::Pause(_voice);
		for (const OneShot& shot : _oneShots) {
			Audio::Pause(shot.voice);
		}
		_paused = true;
	}

	void AudioSource::UnPause() {
		if (!_paused) {
			return;
		}
		Audio::UnPause(_voice);
		for (const OneShot& shot : _oneShots) {
			Audio::UnPause(shot.voice);
		}
		_paused = false;
	}

	bool AudioSource::IsPlaying() const {
		if (Audio::IsPlaying(_voice)) {
			return true;
		}
		for (const OneShot& shot : _oneShots) {
			if (Audio::IsPlaying(shot.voice)) {
				return true;
			}
		}
		return false;
	}

	bool AudioSource::IsVirtual() const {
		bool any = false;
		bool heard = false;
		const auto weigh = [&](AudioVoice voice) {
			if (Audio::IsVirtual(voice)) {
				any = true;
			} else if (Audio::IsPlaying(voice)) {
				any = true;
				heard = true;
			}
		};
		weigh(_voice);
		for (const OneShot& shot : _oneShots) {
			weigh(shot.voice);
		}
		return any && !heard;
	}

	void AudioSource::SetScheduledStartTime(AudioTime time) {
		Audio::SetScheduledStartTime(_voice, time);
	}

	void AudioSource::SetScheduledEndTime(AudioTime time) {
		Audio::SetScheduledEndTime(_voice, time);
	}

	u64 AudioSource::GetTimeSamples() const {
		if (_paused || Audio::IsPlaying(_voice)) {
			return Audio::GetTimeSamples(_voice);
		}
		return _startSample;
	}

	void AudioSource::SetTimeSamples(u64 sample) {
		if (_paused || Audio::IsPlaying(_voice)) {
			Audio::SetTimeSamples(_voice, sample);
			return;
		}
		_startSample = sample;
	}

	f32 AudioSource::GetTime() const {
		const AudioClip* clip = AudioImpl::AudioClipAccess::Resolve(_resource);
		if (!clip || clip->GetFrequency() == 0) {
			return 0.0f;
		}
		return static_cast<f32>(static_cast<f64>(GetTimeSamples()) / static_cast<f64>(clip->GetFrequency()));
	}

	void AudioSource::SetTime(f32 seconds) {
		const AudioClip* clip = AudioImpl::AudioClipAccess::Resolve(_resource);
		if (!clip) {
			return;
		}
		const f64 clamped = seconds > 0.0f ? static_cast<f64>(seconds) : 0.0;
		SetTimeSamples(static_cast<u64>(clamped * static_cast<f64>(clip->GetFrequency())));
	}

	AnimationCurve AudioSource::GetCustomCurve(AudioSourceCurveType type) const {
		switch (type) {
		case AudioSourceCurveType::CustomRolloff:
			return _customRolloffCurve;
		case AudioSourceCurveType::SpatialBlend:
			return Replacing(_spatialBlendCurve) ? _spatialBlendCurve : SingleKey(_spatialBlend);
		case AudioSourceCurveType::ReverbZoneMix:
			return Replacing(_reverbZoneMixCurve) ? _reverbZoneMixCurve : SingleKey(_reverbZoneMix);
		case AudioSourceCurveType::Spread:
			return Replacing(_spreadCurve) ? _spreadCurve : SingleKey(_spread / 360.0f);
		}
		return {};
	}

	void AudioSource::SetCustomCurve(AudioSourceCurveType type, const AnimationCurve& curve) {
		const auto set = [&curve](AnimationCurve& into, f32& value, f32 scale) {
			if (curve.keys.size() == 1) {
				value = curve.keys[0].value * scale;
			}
			if (curve.keys.size() < 2) {
				into.keys.clear();
				return;
			}
			into = curve;
		};
		switch (type) {
		case AudioSourceCurveType::CustomRolloff:
			_customRolloffCurve = curve;
			return;
		case AudioSourceCurveType::SpatialBlend:
			set(_spatialBlendCurve, _spatialBlend, 1.0f);
			return;
		case AudioSourceCurveType::ReverbZoneMix:
			set(_reverbZoneMixCurve, _reverbZoneMix, 1.0f);
			return;
		case AudioSourceCurveType::Spread:
			set(_spreadCurve, _spread, 360.0f);
			return;
		}
	}

	bool AudioSource::CurvesMoved() const {
		const AnimationCurve* rolloff = _rolloffMode == AudioRolloffMode::Custom ? &_customRolloffCurve : nullptr;
		const AnimationCurve* reverb = _bypassReverbZones ? nullptr : Replacing(_reverbZoneMixCurve);
		return !Told(rolloff, _appliedCurves.customRolloff) || !Told(Replacing(_spatialBlendCurve), _appliedCurves.spatialBlend) ||
			   !Told(Replacing(_spreadCurve), _appliedCurves.spread) || !Told(reverb, _appliedCurves.reverbZoneMix);
	}

	void AudioSource::SyncCurves() {
		if (!CurvesMoved()) {
			return;
		}
		Tell(_rolloffMode == AudioRolloffMode::Custom ? &_customRolloffCurve : nullptr, _appliedCurves.customRolloff);
		Tell(Replacing(_spatialBlendCurve), _appliedCurves.spatialBlend);
		Tell(Replacing(_spreadCurve), _appliedCurves.spread);
		Tell(_bypassReverbZones ? nullptr : Replacing(_reverbZoneMixCurve), _appliedCurves.reverbZoneMix);
		const AudioDistanceCurves* curves = AppliedCurves();
		Audio::SetDistanceCurves(_voice, curves);
		for (const OneShot& shot : _oneShots) {
			Audio::SetDistanceCurves(shot.voice, curves);
		}
	}

	const AudioDistanceCurves* AudioSource::AppliedCurves() const {
		const bool any = !_appliedCurves.customRolloff.keys.empty() || !_appliedCurves.spatialBlend.keys.empty() ||
						 !_appliedCurves.spread.keys.empty() || !_appliedCurves.reverbZoneMix.keys.empty();
		return any ? &_appliedCurves : nullptr;
	}

	void AudioSource::TickFilters() {
		if (AudioImpl::GetEnabledFilterCount() == 0 && _appliedFilters.empty()) {
			return;
		}
		AudioImpl::AudioFilterList filters;
		AudioImpl::ReadFilters(GetOwner().Get(), AudioImpl::MAX_SOURCE_FILTERS, filters);
		const u32 found = filters.count;

		bool same = found == _appliedFilters.size();
		for (u32 index = 0; same && index < found; ++index) {
			const AppliedFilter& applied = _appliedFilters[index];
			const AnimationCurve* distance = filters.distanceCurves[index];
			same = applied.effect == filters.effects[index] && applied.count == filters.counts[index] && Told(distance, applied.distance) &&
				   (!distance || applied.distanceParameter == filters.distanceParameters[index]);
		}
		if (same) {
			for (u32 index = 0; index < found; ++index) {
				const AppliedFilter& filter = _appliedFilters[index];
				for (u32 parameter = 0; parameter < filter.count; ++parameter) {
					const f32 value = filters.values[filters.firsts[index] + parameter];
					f32& applied = _appliedValues[filter.first + parameter];
					if (value == applied) {
						continue;
					}
					Audio::SetEffectParameter(_voice, index, parameter, value);
					for (const OneShot& shot : _oneShots) {
						Audio::SetEffectParameter(shot.voice, index, parameter, value);
					}
					applied = value;
				}
			}
			return;
		}

		_appliedFilters.clear();
		const u32 used = found > 0 ? filters.firsts[found - 1] + filters.counts[found - 1] : 0;
		_appliedValues.assign(filters.values, filters.values + used);
		for (u32 index = 0; index < found; ++index) {
			AppliedFilter& applied = _appliedFilters.emplace_back();
			applied.effect = str(filters.effects[index]);
			applied.first = filters.firsts[index];
			applied.count = filters.counts[index];
			Tell(filters.distanceCurves[index], applied.distance);
			applied.distanceParameter = filters.distanceParameters[index];
		}
		AudioVoiceEffect chain[AudioImpl::MAX_SOURCE_FILTERS];
		const u32 count = DescribeAppliedEffects(chain);
		Audio::SetEffects(_voice, {chain, count});
		for (const OneShot& shot : _oneShots) {
			Audio::SetEffects(shot.voice, {chain, count});
		}
	}

	void AudioSource::SyncEffects() {
		TickFilters();
		if (_bypassEffects != _appliedBypass) {
			Audio::SetBypassEffects(_voice, _bypassEffects);
			for (const OneShot& shot : _oneShots) {
				Audio::SetBypassEffects(shot.voice, _bypassEffects);
			}
			_appliedBypass = _bypassEffects;
		}
		if (_bypassListenerEffects != _appliedBypassListener) {
			Audio::SetBypassListenerEffects(_voice, _bypassListenerEffects);
			for (const OneShot& shot : _oneShots) {
				Audio::SetBypassListenerEffects(shot.voice, _bypassListenerEffects);
			}
			_appliedBypassListener = _bypassListenerEffects;
		}
	}

	void AudioSource::Tick(const Vec3& position, const Vec3& velocity) {
		_velocity = velocity;

		SyncEffects();

		if (_awakePending) {
			_awakePending = false;
			if (_playOnAwake && _resource.GetInstanceId() != 0) {
				Play();
			}
		}

		if (!_paused) {
			_oneShots.erase(std::remove_if(_oneShots.begin(), _oneShots.end(), [](const OneShot& shot) { return !Audio::IsPlaying(shot.voice); }),
							_oneShots.end());
		}

		const f32 volume = EffectiveVolume();
		if (volume != _appliedVolume) {
			Audio::SetVolume(_voice, volume);
			for (const OneShot& shot : _oneShots) {
				Audio::SetVolume(shot.voice, volume * shot.volumeScale);
			}
			_appliedVolume = volume;
		}
		const f32 pitch = Clamp(_pitch, -3.0f, 3.0f);
		if (pitch != _appliedPitch) {
			Audio::SetPitch(_voice, pitch);
			for (const OneShot& shot : _oneShots) {
				Audio::SetPitch(shot.voice, pitch);
			}
			_appliedPitch = pitch;
		}
		const f32 stereoPan = Clamp(_stereoPan, -1.0f, 1.0f);
		if (stereoPan != _appliedStereoPan) {
			Audio::SetStereoPan(_voice, stereoPan);
			for (const OneShot& shot : _oneShots) {
				Audio::SetStereoPan(shot.voice, stereoPan);
			}
			_appliedStereoPan = stereoPan;
		}
		const i32 priority = EffectivePriority();
		if (priority != _appliedPriority) {
			Audio::SetPriority(_voice, priority);
			for (const OneShot& shot : _oneShots) {
				Audio::SetPriority(shot.voice, priority);
			}
			_appliedPriority = priority;
		}
		if (_loop != _appliedLoop) {
			Audio::SetLoop(_voice, _loop);
			_appliedLoop = _loop;
		}
		const AudioBus output = ResolveOutput();
		if (output != _appliedOutput) {
			Audio::SetOutput(_voice, output);
			for (const OneShot& shot : _oneShots) {
				Audio::SetOutput(shot.voice, output);
			}
			_appliedOutput = output;
		}

		const f32 reverbZoneMix = EffectiveReverbZoneMix();
		if (reverbZoneMix != _appliedReverbZoneMix) {
			Audio::SetReverbZoneMix(_voice, reverbZoneMix);
			for (const OneShot& shot : _oneShots) {
				Audio::SetReverbZoneMix(shot.voice, reverbZoneMix);
			}
			_appliedReverbZoneMix = reverbZoneMix;
		}

		const AudioSpatialSettings spatial = EffectiveSpatial();
		if (!(spatial == _appliedSpatial)) {
			Audio::SetSpatialSettings(_voice, spatial);
			for (const OneShot& shot : _oneShots) {
				Audio::SetSpatialSettings(shot.voice, spatial);
			}
			_appliedSpatial = spatial;
		}
		SyncCurves();

		Audio::SetPosition(_voice, position, velocity);
		for (const OneShot& shot : _oneShots) {
			Audio::SetPosition(shot.voice, position, velocity);
		}
	}

	namespace AudioImpl {

		namespace {

			bool CueOnSource(const AssetCue& cue) {
				Asset* asset = cue.asset.Get();
				SceneObject* object = cue.object.Get();
				if (!asset || !object) {
					return false;
				}
				AudioSource* source = object->GetComponent<AudioSource>().Get();
				if (!source) {
					if (FirstSilentCue(cue.object.GetInstanceId())) {
						SLT_LOG_WARN("Audio", "'{}' was cued to play '{}' and has no AudioSource. Add one", object->name,
									 asset->virtualPath);
					}
					return false;
				}
				source->PlayOneShot(Handle<AudioResource>(static_cast<AudioResource*>(asset)), cue.volume);
				return true;
			}

		} // anonymous namespace

		void RegisterSourceCues(const void* owner) {
			if (!AssetCues::Register(AudioResource::GetReflectType(), &CueOnSource, owner)) {
				SLT_LOG_ERROR("Audio", "couldn't register the audio cue handler");
			}
		}

	} // namespace AudioImpl

} // namespace Shalltear
