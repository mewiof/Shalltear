#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Types.h"

#include "AudioPlacement.h"

#include <string_view>

namespace Shalltear {

	class AudioListener;
	class AudioReverbZone;
	class AudioSource;
	class Component;
	class IAudioEffectDefinition;

	namespace AudioImpl {

		struct AudioDetachment;

		bool IsInWorldScene(const Component& component);

		void RegisterSource(Handle<AudioSource> source);
		void UnregisterSource(Handle<AudioSource> source);

		void RegisterListener(Handle<AudioListener> listener);
		void UnregisterListener(Handle<AudioListener> listener);

		void RegisterZone(Handle<AudioReverbZone> zone);
		void UnregisterZone(Handle<AudioReverbZone> zone);

		bool IsAnyListenerEnabled();

		void GatherListenerPoses(Vector<AudioListenerPose>& out);

		bool IsAnyListenerMixing();

		bool IsAnyListenerInZone();

		void DetachListenerEffects(IAudioEffectDefinition* definition, AudioDetachment& detachment);
		void OnListenerEffectRegistered(std::string_view name);
		void RemakeListenerEffects();

		void RegisterScenePass(const void* owner);

		void RegisterSourceCues(const void* owner);

		bool FirstSilentCue(u64 objectId);

		void SampleFixedStep();
		void SampleFrameStart();

		void RunScenePass();

		void ShutdownScenePass();

		u32 GetRegisteredSourceCount();
		u32 GetRegisteredListenerCount();
		u32 GetRegisteredZoneCount();

		void NoteFilterEnabled(bool enabled);
		u32 GetEnabledFilterCount();

	} // namespace AudioImpl

} // namespace Shalltear
