#pragma once

#include "Shalltear/Audio/AudioAPI.h"
#include "Shalltear/Audio/AudioReverbZone.h"

#include "Shalltear/Core/Math/AnimationCurve.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear {

	namespace AudioImpl {

		struct AudioFilterAccess;

	} // namespace AudioImpl

	class SHALLTEAR_AUDIO_API AudioFilter : public Component {
	public:
		SLT_REFLECT_COMPONENT_ABSTRACT(AudioFilter, Component)
		SLT_END_REFLECT()

		~AudioFilter() override;
	protected:
		void OnEnable() override;
		void OnDisable() override;
	private:
		friend struct AudioImpl::AudioFilterAccess;

		virtual std::string_view Describe(f32* values, u32& count) const = 0;

		virtual const AnimationCurve* DescribeDistance(u32& parameter) const {
			(void)parameter;
			return nullptr;
		}

		bool _counted = false;
	};

	class SHALLTEAR_AUDIO_API AudioLowPassFilter : public AudioFilter {
	public:
		SLT_REFLECT_COMPONENT(AudioLowPassFilter, AudioFilter)
		SLT_REFLECT_FIELD(_cutoffFrequency, SLT_RANGE(10, 22000))
		SLT_REFLECT_SUBOBJECT(_cutoffCurve, SLT_HIDDEN)
		SLT_REFLECT_FIELD(_lowpassResonanceQ, SLT_RANGE(1, 10))
		SLT_END_REFLECT()

		f32 GetCutoffFrequency() const { return _cutoffFrequency; }
		void SetCutoffFrequency(f32 cutoffFrequency) {
			_cutoffFrequency = cutoffFrequency;
			_cutoffCurve.keys.clear();
		}
		f32 GetLowpassResonanceQ() const { return _lowpassResonanceQ; }
		void SetLowpassResonanceQ(f32 lowpassResonanceQ) { _lowpassResonanceQ = lowpassResonanceQ; }

		AnimationCurve GetCustomCutoffCurve() const;
		void SetCustomCutoffCurve(const AnimationCurve& curve);
	private:
		std::string_view Describe(f32* values, u32& count) const override;
		const AnimationCurve* DescribeDistance(u32& parameter) const override;

		f32 _cutoffFrequency = 5000.0f;
		AnimationCurve _cutoffCurve{GetMemoryResource(MemTag::Scene)};
		f32 _lowpassResonanceQ = 1.0f;
	};

	class SHALLTEAR_AUDIO_API AudioHighPassFilter : public AudioFilter {
	public:
		SLT_REFLECT_COMPONENT(AudioHighPassFilter, AudioFilter)
		SLT_REFLECT_FIELD(_cutoffFrequency, SLT_RANGE(10, 22000))
		SLT_REFLECT_FIELD(_highpassResonanceQ, SLT_RANGE(1, 10))
		SLT_END_REFLECT()

		f32 GetCutoffFrequency() const { return _cutoffFrequency; }
		void SetCutoffFrequency(f32 cutoffFrequency) { _cutoffFrequency = cutoffFrequency; }
		f32 GetHighpassResonanceQ() const { return _highpassResonanceQ; }
		void SetHighpassResonanceQ(f32 highpassResonanceQ) { _highpassResonanceQ = highpassResonanceQ; }
	private:
		std::string_view Describe(f32* values, u32& count) const override;

		f32 _cutoffFrequency = 5000.0f;
		f32 _highpassResonanceQ = 1.0f;
	};

	class SHALLTEAR_AUDIO_API AudioEchoFilter : public AudioFilter {
	public:
		SLT_REFLECT_COMPONENT(AudioEchoFilter, AudioFilter)
		SLT_REFLECT_FIELD(_delay, SLT_RANGE(10, 5000))
		SLT_REFLECT_FIELD(_decayRatio, SLT_RANGE(0, 1))
		SLT_REFLECT_FIELD(_wetMix, SLT_RANGE(0, 1))
		SLT_REFLECT_FIELD(_dryMix, SLT_RANGE(0, 1))
		SLT_END_REFLECT()

		f32 GetDelay() const { return _delay; }
		void SetDelay(f32 delay) { _delay = delay; }
		f32 GetDecayRatio() const { return _decayRatio; }
		void SetDecayRatio(f32 decayRatio) { _decayRatio = decayRatio; }
		f32 GetWetMix() const { return _wetMix; }
		void SetWetMix(f32 wetMix) { _wetMix = wetMix; }
		f32 GetDryMix() const { return _dryMix; }
		void SetDryMix(f32 dryMix) { _dryMix = dryMix; }
	private:
		std::string_view Describe(f32* values, u32& count) const override;

		f32 _delay = 500.0f;
		f32 _decayRatio = 0.5f;
		f32 _wetMix = 1.0f;
		f32 _dryMix = 1.0f;
	};

	class SHALLTEAR_AUDIO_API AudioDistortionFilter : public AudioFilter {
	public:
		SLT_REFLECT_COMPONENT(AudioDistortionFilter, AudioFilter)
		SLT_REFLECT_FIELD(_distortionLevel, SLT_RANGE(0, 1))
		SLT_END_REFLECT()

		f32 GetDistortionLevel() const { return _distortionLevel; }
		void SetDistortionLevel(f32 distortionLevel) { _distortionLevel = distortionLevel; }
	private:
		std::string_view Describe(f32* values, u32& count) const override;

		f32 _distortionLevel = 0.5f;
	};

	class SHALLTEAR_AUDIO_API AudioReverbFilter : public AudioFilter {
	public:
		SLT_REFLECT_COMPONENT(AudioReverbFilter, AudioFilter)
		SLT_REFLECT_FIELD(_reverbPreset)
		SLT_REFLECT_FIELD(_dryLevel, SLT_RANGE(-10000, 0))
		SLT_REFLECT_FIELD(_room, SLT_RANGE(-10000, 0))
		SLT_REFLECT_FIELD(_roomHF, SLT_RANGE(-10000, 0))
		SLT_REFLECT_FIELD(_roomLF, SLT_RANGE(-10000, 0))
		SLT_REFLECT_FIELD(_decayTime, SLT_RANGE(0.1, 20))
		SLT_REFLECT_FIELD(_decayHFRatio, SLT_RANGE(0.1, 2))
		SLT_REFLECT_FIELD(_reflectionsLevel, SLT_RANGE(-10000, 1000))
		SLT_REFLECT_FIELD(_reflectionsDelay, SLT_RANGE(0, 0.3))
		SLT_REFLECT_FIELD(_reverbLevel, SLT_RANGE(-10000, 2000))
		SLT_REFLECT_FIELD(_reverbDelay, SLT_RANGE(0, 0.1))
		SLT_REFLECT_FIELD(_hfReference, SLT_RANGE(1000, 20000))
		SLT_REFLECT_FIELD(_lfReference, SLT_RANGE(20, 1000))
		SLT_REFLECT_FIELD(_diffusion, SLT_RANGE(0, 100))
		SLT_REFLECT_FIELD(_density, SLT_RANGE(0, 100))
		SLT_END_REFLECT()

		AudioReverbPreset GetReverbPreset() const { return _reverbPreset; }
		void SetReverbPreset(AudioReverbPreset reverbPreset) { _reverbPreset = reverbPreset; }

		f32 GetDryLevel() const { return _dryLevel; }
		void SetDryLevel(f32 dryLevel) { _dryLevel = dryLevel; }
		f32 GetRoom() const { return _room; }
		void SetRoom(f32 room) { _room = room; }
		f32 GetRoomHF() const { return _roomHF; }
		void SetRoomHF(f32 roomHF) { _roomHF = roomHF; }
		f32 GetRoomLF() const { return _roomLF; }
		void SetRoomLF(f32 roomLF) { _roomLF = roomLF; }
		f32 GetDecayTime() const { return _decayTime; }
		void SetDecayTime(f32 decayTime) { _decayTime = decayTime; }
		f32 GetDecayHFRatio() const { return _decayHFRatio; }
		void SetDecayHFRatio(f32 decayHFRatio) { _decayHFRatio = decayHFRatio; }
		// millibels
		f32 GetReflectionsLevel() const { return _reflectionsLevel; }
		void SetReflectionsLevel(f32 reflectionsLevel) { _reflectionsLevel = reflectionsLevel; }
		f32 GetReflectionsDelay() const { return _reflectionsDelay; }
		void SetReflectionsDelay(f32 reflectionsDelay) { _reflectionsDelay = reflectionsDelay; }
		f32 GetReverbLevel() const { return _reverbLevel; }
		void SetReverbLevel(f32 reverbLevel) { _reverbLevel = reverbLevel; }
		f32 GetReverbDelay() const { return _reverbDelay; }
		void SetReverbDelay(f32 reverbDelay) { _reverbDelay = reverbDelay; }
		f32 GetHfReference() const { return _hfReference; }
		void SetHfReference(f32 hfReference) { _hfReference = hfReference; }
		f32 GetLfReference() const { return _lfReference; }
		void SetLfReference(f32 lfReference) { _lfReference = lfReference; }
		f32 GetDiffusion() const { return _diffusion; }
		void SetDiffusion(f32 diffusion) { _diffusion = diffusion; }
		f32 GetDensity() const { return _density; }
		void SetDensity(f32 density) { _density = density; }
	private:
		std::string_view Describe(f32* values, u32& count) const override;

		AudioReverbPreset _reverbPreset = AudioReverbPreset::User;
		f32 _dryLevel = 0.0f;
		f32 _room = 0.0f;
		f32 _roomHF = 0.0f;
		f32 _roomLF = 0.0f;
		f32 _decayTime = 1.0f;
		f32 _decayHFRatio = 0.5f;
		f32 _reflectionsLevel = -10000.0f;
		f32 _reflectionsDelay = 0.0f;
		f32 _reverbLevel = 0.0f;
		f32 _reverbDelay = 0.04f;
		f32 _hfReference = 5000.0f;
		f32 _lfReference = 250.0f;
		f32 _diffusion = 100.0f;
		f32 _density = 100.0f;
	};

	class SHALLTEAR_AUDIO_API AudioChorusFilter : public AudioFilter {
	public:
		SLT_REFLECT_COMPONENT(AudioChorusFilter, AudioFilter)
		SLT_REFLECT_FIELD(_dryMix, SLT_RANGE(0, 1))
		SLT_REFLECT_FIELD(_wetMix1, SLT_RANGE(0, 1))
		SLT_REFLECT_FIELD(_wetMix2, SLT_RANGE(0, 1))
		SLT_REFLECT_FIELD(_wetMix3, SLT_RANGE(0, 1))
		SLT_REFLECT_FIELD(_delay, SLT_RANGE(0.1, 100))
		SLT_REFLECT_FIELD(_rate, SLT_RANGE(0, 20))
		SLT_REFLECT_FIELD(_depth, SLT_RANGE(0, 1))
		SLT_REFLECT_FIELD(_feedback, SLT_RANGE(0, 1))
		SLT_END_REFLECT()

		f32 GetDryMix() const { return _dryMix; }
		void SetDryMix(f32 dryMix) { _dryMix = dryMix; }
		f32 GetWetMix1() const { return _wetMix1; }
		void SetWetMix1(f32 wetMix1) { _wetMix1 = wetMix1; }
		f32 GetWetMix2() const { return _wetMix2; }
		void SetWetMix2(f32 wetMix2) { _wetMix2 = wetMix2; }
		f32 GetWetMix3() const { return _wetMix3; }
		void SetWetMix3(f32 wetMix3) { _wetMix3 = wetMix3; }
		f32 GetDelay() const { return _delay; }
		void SetDelay(f32 delay) { _delay = delay; }
		f32 GetRate() const { return _rate; }
		void SetRate(f32 rate) { _rate = rate; }
		f32 GetDepth() const { return _depth; }
		void SetDepth(f32 depth) { _depth = depth; }
		f32 GetFeedback() const { return _feedback; }
		void SetFeedback(f32 feedback) { _feedback = feedback; }
	private:
		std::string_view Describe(f32* values, u32& count) const override;

		f32 _dryMix = 0.5f;
		f32 _wetMix1 = 0.5f;
		f32 _wetMix2 = 0.5f;
		f32 _wetMix3 = 0.5f;
		f32 _delay = 40.0f;
		f32 _rate = 0.8f;
		f32 _depth = 0.03f;
		f32 _feedback = 0.0f;
	};

} // namespace Shalltear
