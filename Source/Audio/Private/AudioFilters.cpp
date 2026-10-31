#include "Shalltear/Audio/AudioFilters.h"

#include "AudioFilterAccess.h"
#include "AudioReverbPresets.h"
#include "AudioScenePass.h"

#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/SceneObject.h"

namespace Shalltear {

	void AudioImpl::ReadFilters(const SceneObject* owner, u32 limit, AudioFilterList& out) {
		out.count = 0;
		const u32 most = limit < MAX_SOURCE_FILTERS ? limit : MAX_SOURCE_FILTERS;
		const u32 components = owner ? owner->GetComponentCount() : 0;
		u32 used = 0;
		for (u32 index = 0; index < components && out.count < most; ++index) {
			const Component* component = owner->GetComponentAt(index).Get();
			if (!component || component->IsPendingDestroy() || !component->IsEffectivelyEnabled() ||
				!TypeRegistry::IsDerivedFrom(component->GetType(), AudioFilter::GetReflectType())) {
				continue;
			}
			u32 count = 0;
			out.effects[out.count] = AudioFilterAccess::Describe(*static_cast<const AudioFilter*>(component), out.values + used, count);
			out.distanceCurves[out.count] = AudioFilterAccess::DescribeDistance(*static_cast<const AudioFilter*>(component), out.distanceParameters[out.count]);
			out.components[out.count] = component->GetInstanceId();
			out.firsts[out.count] = used;
			out.counts[out.count] = count < MAX_FILTER_PARAMETERS ? count : MAX_FILTER_PARAMETERS;
			used += out.counts[out.count];
			++out.count;
		}
	}

	AudioFilter::~AudioFilter() {
		if (_counted) {
			AudioImpl::NoteFilterEnabled(false);
			_counted = false;
		}
	}

	void AudioFilter::OnEnable() {
		if (!_counted) {
			AudioImpl::NoteFilterEnabled(true);
			_counted = true;
		}
	}

	void AudioFilter::OnDisable() {
		if (_counted) {
			AudioImpl::NoteFilterEnabled(false);
			_counted = false;
		}
	}

	std::string_view AudioLowPassFilter::Describe(f32* values, u32& count) const {
		values[0] = _cutoffFrequency;
		values[1] = _lowpassResonanceQ;
		count = 2;
		return "LowPass";
	}

	const AnimationCurve* AudioLowPassFilter::DescribeDistance(u32& parameter) const {
		parameter = 0;
		return _cutoffCurve.keys.size() >= 2 ? &_cutoffCurve : nullptr;
	}

	AnimationCurve AudioLowPassFilter::GetCustomCutoffCurve() const {
		if (_cutoffCurve.keys.size() >= 2) {
			return _cutoffCurve;
		}
		const f32 share = (_cutoffFrequency - 10.0f) / (22000.0f - 10.0f);
		return AnimationCurve{Keyframe{.time = 0.0f, .value = share < 0.0f ? 0.0f : (share > 1.0f ? 1.0f : share)}};
	}

	void AudioLowPassFilter::SetCustomCutoffCurve(const AnimationCurve& curve) {
		if (curve.keys.size() == 1) {
			_cutoffFrequency = 10.0f + (22000.0f - 10.0f) * curve.keys[0].value;
		}
		if (curve.keys.size() < 2) {
			_cutoffCurve.keys.clear();
			return;
		}
		_cutoffCurve = curve;
	}

	std::string_view AudioHighPassFilter::Describe(f32* values, u32& count) const {
		values[0] = _cutoffFrequency;
		values[1] = _highpassResonanceQ;
		count = 2;
		return "HighPass";
	}

	std::string_view AudioEchoFilter::Describe(f32* values, u32& count) const {
		values[0] = _delay;
		values[1] = _decayRatio * 100.0f;
		values[2] = _dryMix * 100.0f;
		values[3] = _wetMix * 100.0f;
		count = 4;
		return "Echo";
	}

	std::string_view AudioDistortionFilter::Describe(f32* values, u32& count) const {
		values[0] = _distortionLevel;
		count = 1;
		return "Distortion";
	}

	std::string_view AudioReverbFilter::Describe(f32* values, u32& count) const {
		count = 14;
		if (_reverbPreset != AudioReverbPreset::User) {
			AudioImpl::WriteSfxReverb(AudioImpl::GetReverbPreset(_reverbPreset), 0.0f, values);
			return "SFX Reverb";
		}
		values[0] = _dryLevel;
		values[1] = _room;
		values[2] = _roomHF;
		values[3] = _decayTime;
		values[4] = _decayHFRatio;
		values[5] = _reflectionsLevel;
		values[6] = _reflectionsDelay;
		values[7] = _reverbLevel;
		values[8] = _reverbDelay;
		values[9] = _diffusion;
		values[10] = _density;
		values[11] = _hfReference;
		values[12] = _roomLF;
		values[13] = _lfReference;
		return "SFX Reverb";
	}

	std::string_view AudioChorusFilter::Describe(f32* values, u32& count) const {
		values[0] = _dryMix;
		values[1] = _wetMix1;
		values[2] = _wetMix2;
		values[3] = _wetMix3;
		values[4] = _delay;
		values[5] = _rate;
		values[6] = _depth;
		values[7] = _feedback;
		count = 8;
		return "Chorus";
	}

} // namespace Shalltear
