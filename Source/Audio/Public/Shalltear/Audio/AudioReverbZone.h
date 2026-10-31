#pragma once

#include "Shalltear/Audio/AudioAPI.h"

#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	enum class AudioReverbPreset : u8 {
		Off = 0,
		Generic,
		PaddedCell,
		Room,
		Bathroom,
		Livingroom,
		Stoneroom,
		Auditorium,
		Concerthall,
		Cave,
		Arena,
		Hangar,
		CarpetedHallway,
		Hallway,
		StoneCorridor,
		Alley,
		Forest,
		City,
		Mountains,
		Quarry,
		Plain,
		ParkingLot,
		SewerPipe,
		Underwater,
		Drugged,
		Dizzy,
		Psychotic,
		User,
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::AudioReverbPreset)
SLT_ENUM_VALUE(Off)
SLT_ENUM_VALUE(Generic)
SLT_ENUM_VALUE(PaddedCell)
SLT_ENUM_VALUE(Room)
SLT_ENUM_VALUE(Bathroom)
SLT_ENUM_VALUE(Livingroom)
SLT_ENUM_VALUE(Stoneroom)
SLT_ENUM_VALUE(Auditorium)
SLT_ENUM_VALUE(Concerthall)
SLT_ENUM_VALUE(Cave)
SLT_ENUM_VALUE(Arena)
SLT_ENUM_VALUE(Hangar)
SLT_ENUM_VALUE(CarpetedHallway)
SLT_ENUM_VALUE(Hallway)
SLT_ENUM_VALUE(StoneCorridor)
SLT_ENUM_VALUE(Alley)
SLT_ENUM_VALUE(Forest)
SLT_ENUM_VALUE(City)
SLT_ENUM_VALUE(Mountains)
SLT_ENUM_VALUE(Quarry)
SLT_ENUM_VALUE(Plain)
SLT_ENUM_VALUE(ParkingLot)
SLT_ENUM_VALUE(SewerPipe)
SLT_ENUM_VALUE(Underwater)
SLT_ENUM_VALUE(Drugged)
SLT_ENUM_VALUE(Dizzy)
SLT_ENUM_VALUE(Psychotic)
SLT_ENUM_VALUE(User)
SLT_END_ENUM()

namespace Shalltear {

	namespace AudioImpl {

		struct AudioReverbZoneAccess;

	} // namespace AudioImpl

	class SHALLTEAR_AUDIO_API AudioReverbZone : public Component {
	public:
		SLT_REFLECT_COMPONENT(AudioReverbZone, Component)
		SLT_REFLECT_FIELD(_minDistance)
		SLT_REFLECT_FIELD(_maxDistance)
		SLT_REFLECT_FIELD(_reverbPreset)
		SLT_REFLECT_FIELD(_room, SLT_RANGE(-10000, 0))
		SLT_REFLECT_FIELD(_roomHF, SLT_RANGE(-10000, 0))
		SLT_REFLECT_FIELD(_roomLF, SLT_RANGE(-10000, 0))
		SLT_REFLECT_FIELD(_decayTime, SLT_RANGE(0.1, 20))
		SLT_REFLECT_FIELD(_decayHFRatio, SLT_RANGE(0.1, 2))
		SLT_REFLECT_FIELD(_reflections, SLT_RANGE(-10000, 1000))
		SLT_REFLECT_FIELD(_reflectionsDelay, SLT_RANGE(0, 0.3))
		SLT_REFLECT_FIELD(_reverb, SLT_RANGE(-10000, 2000))
		SLT_REFLECT_FIELD(_reverbDelay, SLT_RANGE(0, 0.1))
		SLT_REFLECT_FIELD(_hfReference, SLT_RANGE(1000, 20000))
		SLT_REFLECT_FIELD(_lfReference, SLT_RANGE(20, 1000))
		SLT_REFLECT_FIELD(_diffusion, SLT_RANGE(0, 100))
		SLT_REFLECT_FIELD(_density, SLT_RANGE(0, 100))
		SLT_END_REFLECT()

		AudioReverbZone() = default;
		~AudioReverbZone() override;

		f32 GetMinDistance() const { return _minDistance; }
		void SetMinDistance(f32 minDistance) { _minDistance = minDistance; }
		f32 GetMaxDistance() const { return _maxDistance; }
		void SetMaxDistance(f32 maxDistance) { _maxDistance = maxDistance; }

		AudioReverbPreset GetReverbPreset() const { return _reverbPreset; }
		void SetReverbPreset(AudioReverbPreset reverbPreset) { _reverbPreset = reverbPreset; }

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
		f32 GetReflections() const { return _reflections; }
		void SetReflections(f32 reflections) { _reflections = reflections; }
		f32 GetReflectionsDelay() const { return _reflectionsDelay; }
		void SetReflectionsDelay(f32 reflectionsDelay) { _reflectionsDelay = reflectionsDelay; }
		f32 GetReverb() const { return _reverb; }
		void SetReverb(f32 reverb) { _reverb = reverb; }
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
	protected:
		void OnEnable() override;
		void OnDisable() override;
	private:
		friend struct AudioImpl::AudioReverbZoneAccess;

		f32 _minDistance = 10.0f;
		f32 _maxDistance = 15.0f;
		AudioReverbPreset _reverbPreset = AudioReverbPreset::Generic;
		f32 _room = -1000.0f;
		f32 _roomHF = -100.0f;
		f32 _roomLF = 0.0f;
		f32 _decayTime = 1.49f;
		f32 _decayHFRatio = 0.83f;
		f32 _reflections = -2602.0f;
		f32 _reflectionsDelay = 0.007f;
		f32 _reverb = 200.0f;
		f32 _reverbDelay = 0.011f;
		f32 _hfReference = 5000.0f;
		f32 _lfReference = 250.0f;
		f32 _diffusion = 100.0f;
		f32 _density = 100.0f;
	};

} // namespace Shalltear
