#include "AudioContainerPlayback.h"

#include "Shalltear/Audio/AudioClip.h"
#include "Shalltear/Audio/AudioGenerator.h"

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"

#include <algorithm>

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			f32 Clamp(f32 value, f32 low, f32 high) {
				return value >= low ? (value <= high ? value : high) : low;
			}

			constexpr f32 MAX_VOLUME = 16.0f;
			constexpr f32 MAX_PITCH = 3.0f;

		} // anonymous namespace

		AudioContainerPlayback::AudioContainerPlayback(Handle<AudioResource> resource, const AudioPlayParams& params)
			: _resource(resource) {
			_volume = Clamp(params.volume, 0.0f, MAX_VOLUME);
			_pitch = Clamp(params.pitch, -MAX_PITCH, MAX_PITCH);
			_loop = params.loop;
			_startSample = params.startSample;
			_at = params.at;
			_stereoPan = params.stereoPan;
			_output = params.output;
			_timebase = GetPlaybackClock(params.output);
			_reverbZoneMix = params.reverbZoneMix;
			_spatial = params.spatial;
			SetDistanceCurves(params.curves);
			_position = params.position;
			_velocity = params.velocity;
			_priority = params.priority;
			_concurrency = params.concurrency;
			_owner = params.owner;
			_bypassEffects = params.bypassEffects;
			_bypassListenerEffects = params.bypassListenerEffects;
			SetEffects(params.effects);

			if (const Object* object = ResolveResource()) {
				const Asset* asset = static_cast<const Asset*>(object);
				if (asset->assetId.IsValid()) {
					if (Asset* held = AssetDatabase::LoadAssetByGuid(asset->assetId)) {
						_held = Handle<Asset>(held);
					}
				}
			}
		}

		AudioContainerPlayback::~AudioContainerPlayback() {
			if (Asset* held = _held.Get()) {
				AssetDatabase::UnloadAsset(held);
			}
		}

		const Object* AudioContainerPlayback::ResolveResource() const {
			Object* object = ObjectRegistry::Resolve(_resource.GetInstanceId());
			return object && TypeRegistry::IsDerivedFrom(object->GetType(), AudioResource::GetReflectType()) ? object : nullptr;
		}

		AudioVoice AudioContainerPlayback::StartChild(Handle<AudioResource> resource, AudioTime at, u64 startSample, bool loop,
													  f32 volumeScale, f32 pitchScale, u32 tag) {
			if (_stopped || (_end.frames != 0 && at >= _end)) {
				return {};
			}
			AudioVoiceEffect effects[16];
			const u32 effectCount = static_cast<u32>(std::min<size_t>(_effects.size(), 16));
			for (u32 index = 0; index < effectCount; ++index) {
				const OwnedEffect& owned = _effects[index];
				effects[index] = AudioVoiceEffect{owned.effect, {owned.parameters.data(), owned.parameters.size()}};
				if (!owned.distance.keys.empty()) {
					effects[index].distanceCurve = &owned.distance;
					effects[index].distanceParameter = owned.distanceParameter;
				}
			}

			AudioPlayParams params;
			params.at = at;
			params.startSample = startSample;
			params.volume = Clamp(_volume * volumeScale, 0.0f, MAX_VOLUME);
			params.pitch = Clamp(_pitch * pitchScale, -MAX_PITCH, MAX_PITCH);
			params.stereoPan = _stereoPan;
			params.loop = loop;
			params.output = _output;
			params.reverbZoneMix = _reverbZoneMix;
			params.effects = {effects, effectCount};
			params.bypassEffects = _bypassEffects;
			params.bypassListenerEffects = _bypassListenerEffects;
			params.priority = _priority;
			params.concurrency = _concurrency;
			params.owner = _owner;
			params.spatial = _spatial;
			params.curves = _curved ? &_curves : nullptr;
			params.position = _position;
			params.velocity = _velocity;
			const AudioVoice voice = Audio::Play(resource, params);
			if (!voice.IsValid()) {
				return {};
			}
			if (_end.frames != 0) {
				Audio::SetScheduledEndTime(voice, _end);
			}
			if (_paused) {
				Audio::Pause(voice);
			}
			_children.push_back(AudioContainerChild{voice, volumeScale, pitchScale, at, startSample, tag});
			++_started;
			return voice;
		}

		void AudioContainerPlayback::PruneChildren() {
			if (_paused) {
				return;
			}
			_children.erase(std::remove_if(_children.begin(), _children.end(),
										   [](const AudioContainerChild& child) { return !Audio::IsPlaying(child.voice); }),
							_children.end());
		}

		u64 AudioContainerPlayback::FramesOf(Handle<AudioResource> resource, f32 pitch) {
			Object* object = ObjectRegistry::Resolve(resource.GetInstanceId());
			if (!object || !TypeRegistry::IsDerivedFrom(object->GetType(), AudioClip::GetReflectType())) {
				return 0;
			}
			const AudioClip* clip = static_cast<const AudioClip*>(object);
			const u32 rate = Audio::GetOutputSampleRate();
			const f64 speed = pitch < 0.0f ? -static_cast<f64>(pitch) : static_cast<f64>(pitch);
			if (clip->GetFrequency() == 0 || rate == 0 || speed <= 0.0) {
				return 0;
			}
			const f64 frames = static_cast<f64>(clip->GetSamples()) * static_cast<f64>(rate) / static_cast<f64>(clip->GetFrequency()) / speed;
			return static_cast<u64>(frames + 0.5);
		}

		bool AudioContainerPlayback::IsFinished() const {
			if (_paused && !_stopped) {
				return false;
			}
			if (!_stopped && HasMoreToStart()) {
				return false;
			}
			for (const AudioContainerChild& child : _children) {
				if (Audio::IsPlaying(child.voice)) {
					return false;
				}
			}
			return true;
		}

		void AudioContainerPlayback::Stop() {
			_stopped = true;
			for (const AudioContainerChild& child : _children) {
				Audio::Stop(child.voice);
			}
			_children.clear();
		}

		void AudioContainerPlayback::SetScheduledStartTime(AudioTime time) {
			const AudioTime now = _timebase.Now();
			for (const AudioContainerChild& child : _children) {
				if (child.start <= now) {
					return;
				}
			}
			const i64 delta = static_cast<i64>(time.frames) - static_cast<i64>(_at.frames > now.frames ? _at.frames : now.frames);
			for (AudioContainerChild& child : _children) {
				const i64 moved = static_cast<i64>(child.start.frames) + delta;
				child.start = AudioTime{moved > 0 ? static_cast<u64>(moved) : 0};
				Audio::SetScheduledStartTime(child.voice, child.start);
			}
			_at = time;
			ShiftTimeline(delta);
		}

		void AudioContainerPlayback::SetScheduledEndTime(AudioTime time) {
			_end = time;
			for (const AudioContainerChild& child : _children) {
				Audio::SetScheduledEndTime(child.voice, time);
			}
		}

		void AudioContainerPlayback::Pause() {
			if (_paused || _stopped) {
				return;
			}
			_paused = true;
			_pausedAt = _timebase.Now();
			for (const AudioContainerChild& child : _children) {
				Audio::Pause(child.voice);
			}
		}

		void AudioContainerPlayback::UnPause() {
			if (!_paused) {
				return;
			}
			_paused = false;
			const AudioTime now = _timebase.Now();
			const u64 held = now.frames > _pausedAt.frames ? now.frames - _pausedAt.frames : 0;
			for (AudioContainerChild& child : _children) {
				if (child.start > _pausedAt && held > 0) {
					child.start = AudioTime{child.start.frames + held};
					Audio::SetScheduledStartTime(child.voice, child.start);
				}
				Audio::UnPause(child.voice);
			}
			if (held > 0) {
				ShiftTimeline(static_cast<i64>(held));
			}
		}

		void AudioContainerPlayback::SetVolume(f32 volume) {
			_volume = Clamp(volume, 0.0f, MAX_VOLUME);
			for (const AudioContainerChild& child : _children) {
				Audio::SetVolume(child.voice, Clamp(_volume * child.volumeScale, 0.0f, MAX_VOLUME));
			}
		}

		void AudioContainerPlayback::SetPitch(f32 pitch) {
			_pitch = Clamp(pitch, -MAX_PITCH, MAX_PITCH);
			for (const AudioContainerChild& child : _children) {
				Audio::SetPitch(child.voice, Clamp(_pitch * child.pitchScale, -MAX_PITCH, MAX_PITCH));
			}
		}

		void AudioContainerPlayback::SetStereoPan(f32 stereoPan) {
			_stereoPan = stereoPan;
			for (const AudioContainerChild& child : _children) {
				Audio::SetStereoPan(child.voice, stereoPan);
			}
		}

		void AudioContainerPlayback::SetLoop(bool loop) {
			_loop = loop;
			ApplyLoop();
		}

		void AudioContainerPlayback::ApplyLoop() {
			for (const AudioContainerChild& child : _children) {
				Audio::SetLoop(child.voice, _loop);
			}
		}

		void AudioContainerPlayback::SetOutput(AudioBus output) {
			_output = output;
			for (const AudioContainerChild& child : _children) {
				Audio::SetOutput(child.voice, output);
			}
		}

		void AudioContainerPlayback::SetReverbZoneMix(f32 reverbZoneMix) {
			_reverbZoneMix = reverbZoneMix;
			for (const AudioContainerChild& child : _children) {
				Audio::SetReverbZoneMix(child.voice, reverbZoneMix);
			}
		}

		void AudioContainerPlayback::SetGeneratorParameter(u32 index, f32 value) {
			for (const AudioContainerChild& child : _children) {
				Audio::SetGeneratorParameter(child.voice, index, value);
			}
		}

		void AudioContainerPlayback::SetSpatialSettings(const AudioSpatialSettings& settings) {
			_spatial = settings;
			for (const AudioContainerChild& child : _children) {
				Audio::SetSpatialSettings(child.voice, settings);
			}
		}

		void AudioContainerPlayback::SetDistanceCurves(const AudioDistanceCurves* curves) {
			_curved = curves != nullptr;
			if (curves) {
				_curves = *curves;
			}
			for (const AudioContainerChild& child : _children) {
				Audio::SetDistanceCurves(child.voice, curves);
			}
		}

		void AudioContainerPlayback::SetPosition(const Vec3& position, const Vec3& velocity) {
			_position = position;
			_velocity = velocity;
			for (const AudioContainerChild& child : _children) {
				Audio::SetPosition(child.voice, position, velocity);
			}
		}

		void AudioContainerPlayback::SetEffects(std::span<const AudioVoiceEffect> effects) {
			_effects.clear();
			for (const AudioVoiceEffect& effect : effects) {
				OwnedEffect& owned = _effects.emplace_back();
				owned.effect = str(effect.effect);
				owned.parameters.assign(effect.parameters.begin(), effect.parameters.end());
				if (effect.distanceCurve) {
					owned.distance = *effect.distanceCurve;
					owned.distanceParameter = effect.distanceParameter;
				}
			}
			for (const AudioContainerChild& child : _children) {
				Audio::SetEffects(child.voice, effects);
			}
		}

		void AudioContainerPlayback::SetEffectParameter(u32 index, u32 parameter, f32 value) {
			if (index < _effects.size()) {
				Vector<f32>& parameters = _effects[index].parameters;
				if (parameter >= parameters.size()) {
					parameters.resize(parameter + 1, 0.0f);
				}
				parameters[parameter] = value;
			}
			for (const AudioContainerChild& child : _children) {
				Audio::SetEffectParameter(child.voice, index, parameter, value);
			}
		}

		void AudioContainerPlayback::SetBypassEffects(bool bypass) {
			_bypassEffects = bypass;
			for (const AudioContainerChild& child : _children) {
				Audio::SetBypassEffects(child.voice, bypass);
			}
		}

		void AudioContainerPlayback::SetBypassListenerEffects(bool bypass) {
			_bypassListenerEffects = bypass;
			for (const AudioContainerChild& child : _children) {
				Audio::SetBypassListenerEffects(child.voice, bypass);
			}
		}

		bool AudioContainerPlayback::IsPlaying() {
			if (_stopped || _paused) {
				return false;
			}
			if (HasMoreToStart()) {
				return true;
			}
			for (const AudioContainerChild& child : _children) {
				if (Audio::IsPlaying(child.voice)) {
					return true;
				}
			}
			return false;
		}

		void AudioContainerPlayback::SetPriority(i32 priority) {
			_priority = priority;
			for (const AudioContainerChild& child : _children) {
				Audio::SetPriority(child.voice, priority);
			}
		}

		bool AudioContainerPlayback::IsVirtual() {
			bool any = false;
			for (const AudioContainerChild& child : _children) {
				if (Audio::IsVirtual(child.voice)) {
					any = true;
				} else if (Audio::IsPlaying(child.voice)) {
					return false;
				}
			}
			return any;
		}

		u32 AudioContainerPlayback::GetVoices(std::span<AudioVoice> out) const {
			for (size_t index = 0; index < _children.size() && index < out.size(); ++index) {
				out[index] = _children[index].voice;
			}
			return static_cast<u32>(_children.size());
		}

		u64 AudioContainerPlayback::GetTimeSamples() {
			return _children.empty() ? _startSample : Audio::GetTimeSamples(_children.back().voice);
		}

		void AudioContainerPlayback::SetTimeSamples(u64 sample) {
			if (_children.empty()) {
				_startSample = sample;
				return;
			}
			Audio::SetTimeSamples(_children.back().voice, sample);
		}

	} // namespace AudioImpl

} // namespace Shalltear
