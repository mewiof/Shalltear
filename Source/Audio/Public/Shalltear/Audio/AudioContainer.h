#pragma once

#include "Shalltear/Audio/Audio.h"
#include "Shalltear/Audio/AudioAPI.h"
#include "Shalltear/Audio/AudioResource.h"

#include "Shalltear/Core/Asset/DataAsset.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/MathReflect.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear {

	enum class AudioRandomContainerTriggerMode : u8 {
		Manual = 0,
		Automatic,
	};

	enum class AudioRandomContainerPlaybackMode : u8 {
		Sequential = 0,
		Shuffle,
		Random,
	};

	enum class AudioRandomContainerAutomaticTriggerMode : u8 {
		Pulse = 0,
		Offset,
	};

	enum class AudioRandomContainerLoopMode : u8 {
		Infinite = 0,
		Clips,
		Cycles,
	};

	enum class AudioInteractiveTransitionFromTime : u8 {
		Immediate = 0,
		NextBeat,
		NextBar,
		End,
	};

	enum class AudioInteractiveTransitionToTime : u8 {
		SamePosition = 0,
		Start,
		PreviousPosition,
	};

	enum class AudioInteractiveFadeMode : u8 {
		Disabled = 0,
		In,
		Out,
		Cross,
		Automatic,
	};

	enum class AudioInteractiveAutoAdvanceMode : u8 {
		Disabled = 0,
		Enabled,
		ReturnToHold,
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::AudioRandomContainerTriggerMode)
SLT_ENUM_VALUE(Manual)
SLT_ENUM_VALUE(Automatic)
SLT_END_ENUM()

SLT_REFLECT_ENUM(::Shalltear::AudioRandomContainerPlaybackMode)
SLT_ENUM_VALUE(Sequential)
SLT_ENUM_VALUE(Shuffle)
SLT_ENUM_VALUE(Random)
SLT_END_ENUM()

SLT_REFLECT_ENUM(::Shalltear::AudioRandomContainerAutomaticTriggerMode)
SLT_ENUM_VALUE(Pulse)
SLT_ENUM_VALUE(Offset)
SLT_END_ENUM()

SLT_REFLECT_ENUM(::Shalltear::AudioRandomContainerLoopMode)
SLT_ENUM_VALUE(Infinite)
SLT_ENUM_VALUE(Clips)
SLT_ENUM_VALUE(Cycles)
SLT_END_ENUM()

SLT_REFLECT_ENUM(::Shalltear::AudioInteractiveTransitionFromTime)
SLT_ENUM_VALUE(Immediate)
SLT_ENUM_VALUE(NextBeat)
SLT_ENUM_VALUE(NextBar)
SLT_ENUM_VALUE(End)
SLT_END_ENUM()

SLT_REFLECT_ENUM(::Shalltear::AudioInteractiveTransitionToTime)
SLT_ENUM_VALUE(SamePosition)
SLT_ENUM_VALUE(Start)
SLT_ENUM_VALUE(PreviousPosition)
SLT_END_ENUM()

SLT_REFLECT_ENUM(::Shalltear::AudioInteractiveFadeMode)
SLT_ENUM_VALUE(Disabled)
SLT_ENUM_VALUE(In)
SLT_ENUM_VALUE(Out)
SLT_ENUM_VALUE(Cross)
SLT_ENUM_VALUE(Automatic)
SLT_END_ENUM()

SLT_REFLECT_ENUM(::Shalltear::AudioInteractiveAutoAdvanceMode)
SLT_ENUM_VALUE(Disabled)
SLT_ENUM_VALUE(Enabled)
SLT_ENUM_VALUE(ReturnToHold)
SLT_END_ENUM()

namespace Shalltear {

	class SHALLTEAR_AUDIO_API AudioContainer : public AudioResource {
	public:
		SLT_REFLECT_CLASS(AudioContainer)
		SLT_REFLECT_PARENT(AudioResource)
		builder.SetFlags(::Shalltear::TypeFlagAbstract);
		SLT_END_REFLECT()

		~AudioContainer() override = default;

		StringHash GetAssetTypeId() const override { return GetType()->name; }

		bool Import(const Path& sourcePath) override;
		bool Export(const Path& destPath) const override;
	protected:
		AudioContainer() = default;
	};

	struct AudioRandomContainerElement {
		Handle<AudioResource> resource;
		bool enabled = true;
		f32 volume = 1.0f;

		SLT_REFLECT_VALUE(AudioRandomContainerElement)
		SLT_REFLECT_FIELD(resource)
		SLT_REFLECT_FIELD(enabled)
		SLT_REFLECT_FIELD(volume, SLT_RANGE(0, 1))
		SLT_END_REFLECT()
	};

	class SHALLTEAR_AUDIO_API AudioRandomContainer : public AudioContainer {
	public:
		SLT_REFLECT_CLASS(AudioRandomContainer)
		SLT_REFLECT_PARENT(AudioContainer)
		SLT_CREATE_ASSET_MENU("Audio Random Container", ".randomcontainer")
		SLT_REFLECT_FIELD(volume, SLT_RANGE(0, 1))
		SLT_REFLECT_FIELD(volumeRandomizationRange)
		SLT_REFLECT_FIELD(pitch, SLT_RANGE(-1902, 1902))
		SLT_REFLECT_FIELD(pitchRandomizationRange)
		SLT_REFLECT_ARRAY(elements)
		SLT_REFLECT_FIELD(triggerMode)
		SLT_REFLECT_FIELD(playbackMode)
		SLT_REFLECT_FIELD(avoidRepeatingLast)
		SLT_REFLECT_FIELD(automaticTriggerMode)
		SLT_REFLECT_FIELD(automaticTriggerTime)
		SLT_REFLECT_FIELD(automaticTriggerTimeRandomizationRange)
		SLT_REFLECT_FIELD(loopMode)
		SLT_REFLECT_FIELD(loopCount)
		SLT_REFLECT_FIELD(loopCountRandomizationRange)
		SLT_END_REFLECT()

		AudioRandomContainer() = default;
		~AudioRandomContainer() override = default;

		f32 volume = 1.0f;
		// dB
		Vec2 volumeRandomizationRange{};
		f32 pitch = 0.0f;
		Vec2 pitchRandomizationRange{};

		Vector<AudioRandomContainerElement> elements{GetMemoryResource(MemTag::Asset)};

		AudioRandomContainerTriggerMode triggerMode = AudioRandomContainerTriggerMode::Manual;
		AudioRandomContainerPlaybackMode playbackMode = AudioRandomContainerPlaybackMode::Shuffle;
		i32 avoidRepeatingLast = 1;

		AudioRandomContainerAutomaticTriggerMode automaticTriggerMode = AudioRandomContainerAutomaticTriggerMode::Pulse;
		f32 automaticTriggerTime = 1.0f;
		Vec2 automaticTriggerTimeRandomizationRange{};
		AudioRandomContainerLoopMode loopMode = AudioRandomContainerLoopMode::Infinite;
		i32 loopCount = 1;
		Vec2I loopCountRandomizationRange{};
	};

	struct AudioLayeredContainerLayer {
		Handle<AudioResource> resource;
		f32 volume = 1.0f;

		SLT_REFLECT_VALUE(AudioLayeredContainerLayer)
		SLT_REFLECT_FIELD(resource)
		SLT_REFLECT_FIELD(volume, SLT_RANGE(0, 1))
		SLT_END_REFLECT()
	};

	class SHALLTEAR_AUDIO_API AudioLayeredContainer : public AudioContainer {
	public:
		SLT_REFLECT_CLASS(AudioLayeredContainer)
		SLT_REFLECT_PARENT(AudioContainer)
		SLT_CREATE_ASSET_MENU("Audio Layered Container", ".layeredcontainer")
		SLT_REFLECT_ARRAY(layers)
		SLT_END_REFLECT()

		AudioLayeredContainer() = default;
		~AudioLayeredContainer() override = default;

		Vector<AudioLayeredContainerLayer> layers{GetMemoryResource(MemTag::Asset)};
	};

	struct AudioInteractiveContainerClip {
		str name;
		Handle<AudioResource> resource;
		bool loop = false;
		f32 beatsPerMinute = 0.0f;
		i32 beatsPerBar = 0;
		AudioInteractiveAutoAdvanceMode autoAdvance = AudioInteractiveAutoAdvanceMode::Disabled;
		str nextClip;

		SLT_REFLECT_VALUE(AudioInteractiveContainerClip)
		SLT_REFLECT_FIELD(name)
		SLT_REFLECT_FIELD(resource)
		SLT_REFLECT_FIELD(loop)
		SLT_REFLECT_FIELD(beatsPerMinute)
		SLT_REFLECT_FIELD(beatsPerBar)
		SLT_REFLECT_FIELD(autoAdvance)
		SLT_REFLECT_FIELD(nextClip)
		SLT_END_REFLECT()
	};

	struct AudioInteractiveContainerTransition {
		str fromClip;
		str toClip;
		AudioInteractiveTransitionFromTime fromTime = AudioInteractiveTransitionFromTime::NextBeat;
		AudioInteractiveTransitionToTime toTime = AudioInteractiveTransitionToTime::Start;
		AudioInteractiveFadeMode fade = AudioInteractiveFadeMode::Automatic;
		// seconds with no tempo
		f32 fadeBeats = 1.0f;
		str fillerClip;
		bool holdPrevious = false;

		SLT_REFLECT_VALUE(AudioInteractiveContainerTransition)
		SLT_REFLECT_FIELD(fromClip)
		SLT_REFLECT_FIELD(toClip)
		SLT_REFLECT_FIELD(fromTime)
		SLT_REFLECT_FIELD(toTime)
		SLT_REFLECT_FIELD(fade)
		SLT_REFLECT_FIELD(fadeBeats)
		SLT_REFLECT_FIELD(fillerClip)
		SLT_REFLECT_FIELD(holdPrevious)
		SLT_END_REFLECT()
	};

	class SHALLTEAR_AUDIO_API AudioInteractiveContainer : public AudioContainer {
	public:
		SLT_REFLECT_CLASS(AudioInteractiveContainer)
		SLT_REFLECT_PARENT(AudioContainer)
		SLT_CREATE_ASSET_MENU("Audio Interactive Container", ".interactivecontainer")
		SLT_REFLECT_FIELD(beatsPerMinute)
		SLT_REFLECT_FIELD(beatsPerBar)
		SLT_REFLECT_FIELD(initialClip)
		SLT_REFLECT_ARRAY(clips)
		SLT_REFLECT_ARRAY(transitions)
		SLT_END_REFLECT()

		AudioInteractiveContainer() = default;
		~AudioInteractiveContainer() override = default;

		f32 beatsPerMinute = 120.0f;
		i32 beatsPerBar = 4;
		str initialClip;
		Vector<AudioInteractiveContainerClip> clips{GetMemoryResource(MemTag::Asset)};
		Vector<AudioInteractiveContainerTransition> transitions{GetMemoryResource(MemTag::Asset)};
	};

	namespace Audio {

		SHALLTEAR_AUDIO_API void SwitchToClip(AudioVoice voice, std::string_view clip);

		SHALLTEAR_AUDIO_API void SetLayerVolume(AudioVoice voice, u32 layer, f32 volume);

	} // namespace Audio

} // namespace Shalltear
