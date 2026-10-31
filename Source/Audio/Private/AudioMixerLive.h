#pragma once

#include "AudioVoiceInternals.h"

#include "Shalltear/Audio/AudioEffect.h"
#include "Shalltear/Audio/AudioMixer.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Types.h"

#include "AudioBus.h"

#include <string_view>

namespace Shalltear {

	namespace AudioImpl {

		struct AudioDetachment;

		struct AudioMixerLiveRow {
			AudioMixerUnitKind kind = AudioMixerUnitKind::Attenuation;
			str target;
			str targetUnit;
			u8 targetRow = NO_BUS_ROW;
			str id;
			str effect;
			bool receives = false;
			bool bypass = false;
			bool wetMixing = false;
			IAudioEffectDefinition* definition = nullptr;
			IAudioEffect* instance = nullptr;
		};

		struct AudioMixerLiveGroup {
			str id;
			AudioBus bus;
			str parent;
			AudioMixerGroupClock clock = AudioMixerGroupClock::Wall;
			bool followTimeScale = false;
			bool mute = false;
			bool solo = false;
			bool bypassEffects = false;
			Vector<AudioMixerLiveRow> rows{GetMemoryResource(MemTag::Audio)};
		};

		struct AudioMixerLiveParameter {
			str group;
			AudioMixerParameter parameter = AudioMixerParameter::Volume;
			str target;
			str effectParameter;

			AudioBus bus;
			u32 row = 0;
			u32 index = ~0u;
			f32 minimum = 0.0f;
			f32 maximum = 0.0f;

			f32 from = 0.0f;
			f32 to = 0.0f;
			f32 snapshot = 0.0f;
			AudioMixerTransitionType curve = AudioMixerTransitionType::Lerp;
			f32 override = 0.0f;
			bool overridden = false;
			f32 hold = 0.0f;
			bool holding = false;
			f32 sent = 0.0f;
			bool everSent = false;

			f32 Effective() const { return overridden ? override : (holding ? hold : snapshot); }
		};

		struct AudioMixerKept {
			Asset* product = nullptr;
			bool* holdsMixer = nullptr;
		};

		struct AudioMixerLive {
			bool live = false;
			bool built = false;
			u32 revision = 1;

			Vector<AudioMixerLiveGroup> groups{GetMemoryResource(MemTag::Audio)};
			Vector<AudioMixerLiveParameter> parameters{GetMemoryResource(MemTag::Audio)};

			bool transitioning = false;
			f32 elapsed = 0.0f;
			f32 duration = 0.0f;

			Vector<u32> heading{GetMemoryResource(MemTag::Audio)};
			Vector<f32> headingWeights{GetMemoryResource(MemTag::Audio)};
			u32 holds = 0;

			Handle<AudioMixer> output;
			AudioBus outputBus;
			bool warnedOutput = false;

			Vector<AudioMixerKept> handedOut{GetMemoryResource(MemTag::Audio)};
		};

		struct AudioMixerAccess {
			static AudioMixerLive* GetLive(const AudioMixer& mixer) { return mixer._live; }
			static AudioMixerLive& EnsureLive(AudioMixer& mixer);
			static void DropLive(AudioMixer& mixer);

			static void InitGroup(AudioMixerGroup& group, const Guid& mixer, std::string_view id) {
				group._mixer = mixer;
				group._groupId = str(id);
			}
			static void InitSnapshot(AudioMixerSnapshot& snapshot, const Guid& mixer, std::string_view id) {
				snapshot._mixer = mixer;
				snapshot._snapshotId = str(id);
			}

			static void AddProduct(AudioMixer& mixer, std::string_view key, const Guid& guid) {
				mixer._products.push_back(AudioMixer::Product{str(key), guid});
			}
			static Guid FindProduct(const AudioMixer& mixer, std::string_view key) {
				for (const AudioMixer::Product& product : mixer._products) {
					if (product.key == key) {
						return product.guid;
					}
				}
				return {};
			}
			static size_t CountProducts(const AudioMixer& mixer) { return mixer._products.size(); }
			static const Guid& MixerOf(const AudioMixerSnapshot& snapshot) { return snapshot._mixer; }

			static bool Keeps(const AudioMixer& mixer, const Asset& product);

			static void KeptBy(AudioMixerGroup& group, AudioMixer& mixer, AudioMixerLive& live) {
				Link(group, group._audioMixer, group._holdsMixer, mixer, live);
			}
			static void KeptBy(AudioMixerSnapshot& snapshot, AudioMixer& mixer, AudioMixerLive& live) {
				Link(snapshot, snapshot._audioMixer, snapshot._holdsMixer, mixer, live);
			}
			static bool HoldsItsMixer(const AudioMixerGroup& group) { return group._holdsMixer && group._audioMixer.Get(); }
			static bool HoldsItsMixer(const AudioMixerSnapshot& snapshot) { return snapshot._holdsMixer && snapshot._audioMixer.Get(); }
		private:
			static void Link(Asset& product, Handle<AudioMixer>& held, bool& holds, AudioMixer& mixer, AudioMixerLive& live) {
				if (!held.Get()) {
					held = Handle<AudioMixer>(&mixer);
					holds = false;
				}
				live.handedOut.push_back(AudioMixerKept{&product, &holds});
			}
		};

		AudioMixer* ResolveProductMixer(const Guid& mixer, Handle<AudioMixer>& held, bool& holds, bool& refused, std::string_view kind,
										std::string_view id, std::string_view consequence);

		AudioBus ResolveGroupBus(Handle<AudioMixerGroup> group);

		bool ValidateMixer(const AudioMixer& mixer, str& error);

		i32 FindGroup(const AudioMixer& mixer, std::string_view id);

		bool ParameterExists(const AudioMixer& mixer, std::string_view group, AudioMixerParameter parameter, std::string_view target,
							 std::string_view effectParameter);

		f32 SnapshotValue(const AudioMixer& mixer, u32 index, std::string_view group, AudioMixerParameter parameter, std::string_view target,
						  std::string_view effectParameter);

		u32 StartSnapshotIndex(const AudioMixer& mixer);

		f32 ToLinear(AudioMixerParameter parameter, f32 value);

		void PushMixer(AudioMixerLive& live, u32 frames);

		void RegisterMixer(AudioMixer& mixer);
		void UnregisterMixer(AudioMixer& mixer);

		void ActivateMixers();
		void DeactivateMixers();

		void UpdateMixers();

		void ResetMixers();

		u32 CountLiveMixers();

		void OnEffectRegistered(std::string_view name);

		void DetachMixerEffects(IAudioEffectDefinition* definition, AudioDetachment& detachment);

		void RemakeMixerEffects();

		void ShutdownMixerRegistry();

		void RegisterMixerImporter(const void* owner);

	} // namespace AudioImpl

} // namespace Shalltear
