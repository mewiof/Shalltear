#include "AudioListenerMix.h"

#include "Shalltear/Audio/AudioEffect.h"
#include "Shalltear/Audio/AudioListener.h"

#include "AudioEffectRegistry.h"
#include "AudioMixerLive.h"
#include "AudioScenePass.h"
#include "AudioSystem.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Scene/SceneObject.h"

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			IAudioEffect* MakeInstance(AudioListenerMix& mix, const AudioListenerFilterRow& row) {
				AudioSystem& system = GetAudioSystem();
				const AudioRenderer& renderer = system.GetRenderer();
				if (!row.definition || !renderer.IsPrepared()) {
					return nullptr;
				}
				Vector<f32> values(row.values, row.values + row.count, GetMemoryResource(MemTag::Audio));
				ResolveParameterValues(*row.definition, values);
				IAudioEffect* effect = row.definition->CreateEffect(system.MakeBusEffectContext({values.data(), values.size()}));
				if (!effect) {
					SLT_LOG_WARN("Audio", "the effect '{}' couldn't make an instance for a listener's filter (sound passes through)", row.effect);
				}
				mix.madeRate = renderer.GetSampleRate();
				mix.madeLayout = renderer.GetLayout();
				return effect;
			}

			bool SameFilters(const AudioListenerMix& mix, const AudioFilterList& filters) {
				if (filters.count != mix.rows.size()) {
					return false;
				}
				for (u32 index = 0; index < filters.count; ++index) {
					const AudioListenerFilterRow& row = mix.rows[index];
					if (row.component != filters.components[index] || row.effect != filters.effects[index] || row.count != filters.counts[index]) {
						return false;
					}
				}
				return true;
			}

			void SendMovedValues(AudioListenerMix& mix, u32 index, AudioListenerFilterRow& row, const f32* values) {
				AudioSystem& system = GetAudioSystem();
				for (u32 parameter = 0; parameter < row.count; ++parameter) {
					if (values[parameter] == row.values[parameter]) {
						continue;
					}
					row.values[parameter] = values[parameter];
					if (row.instance && parameter < row.definition->GetParameters().size()) {
						system.SetBusEffectParameter(mix.bus, index, parameter, ClampParameterValue(*row.definition, parameter, values[parameter]));
					}
				}
			}

			void Unbuild(AudioListenerMix& mix) {
				AudioSystem& system = GetAudioSystem();
				system.DestroyBus(mix.bus);
				for (AudioListenerFilterRow& row : mix.rows) {
					system.RetireEffect(row.instance);
				}
				mix.rows.clear();
				mix.bus = {};
				mix.parent = {};
			}

			void Rebuild(AudioListenerMix& mix, const AudioFilterList& filters, AudioBus output) {
				AudioSystem& system = GetAudioSystem();
				if (!mix.bus.IsValid()) {
					AudioBusDesc desc;
					desc.parent = output;
					desc.clock = AudioBusClock::Inherit;
					mix.bus = system.CreateBus(desc);
					if (!mix.bus.IsValid()) {
						return;
					}
					mix.parent = output;
				}

				Vector<AudioListenerFilterRow> rows{GetMemoryResource(MemTag::Audio)};
				AudioBusRowDesc descs[MAX_BUS_ROWS];
				bool carried[MAX_BUS_ROWS]{};
				bool made[MAX_BUS_ROWS]{};
				i32 from[MAX_BUS_ROWS];
				const u32 count = filters.count;
				for (u32 index = 0; index < count; ++index) {
					AudioListenerFilterRow row;
					row.component = filters.components[index];
					row.effect = str(filters.effects[index]);
					row.count = filters.counts[index];
					from[index] = -1;
					for (u32 old = 0; old < mix.rows.size(); ++old) {
						const AudioListenerFilterRow& was = mix.rows[old];
						if (!carried[old] && was.instance && was.component == row.component && was.effect == row.effect) {
							row.definition = was.definition;
							row.instance = was.instance;
							carried[old] = true;
							from[index] = static_cast<i32>(old);
							break;
						}
					}
					if (from[index] >= 0) {
						const AudioListenerFilterRow& was = mix.rows[static_cast<size_t>(from[index])];
						for (u32 parameter = 0; parameter < row.count; ++parameter) {
							row.values[parameter] = parameter < was.count ? was.values[parameter] : filters.values[filters.firsts[index] + parameter];
						}
					} else {
						for (u32 parameter = 0; parameter < row.count; ++parameter) {
							row.values[parameter] = filters.values[filters.firsts[index] + parameter];
						}
						row.definition = FindEffect(row.effect);
						row.instance = MakeInstance(mix, row);
						made[index] = row.instance != nullptr;
					}
					descs[index] = AudioBusRowDesc{};
					descs[index].kind = AudioBusRowKind::Effect;
					descs[index].effect = row.instance;
					descs[index].level = 1.0f;
					rows.push_back(std::move(row));
				}
				descs[count] = AudioBusRowDesc{};

				if (!system.SetBusChain(mix.bus, descs, count + 1)) {
					SLT_LOG_ERROR("Audio", "a listener's filters couldn't be applied to its bus");
					for (u32 index = 0; index < count; ++index) {
						if (made[index]) {
							rows[index].instance->Release();
						}
					}
					return;
				}
				for (u32 old = 0; old < mix.rows.size(); ++old) {
					if (!carried[old]) {
						system.RetireEffect(mix.rows[old].instance);
					}
				}
				mix.rows = std::move(rows);
				for (u32 index = 0; index < count; ++index) {
					if (from[index] >= 0) {
						SendMovedValues(mix, index, mix.rows[index], filters.values + filters.firsts[index]);
					}
				}
			}

			constexpr std::string_view ZONE_REVERB = "SFX Reverb";

			IAudioEffect* MakeZoneReverb(AudioListenerMix& mix, const f32* values) {
				AudioSystem& system = GetAudioSystem();
				const AudioRenderer& renderer = system.GetRenderer();
				if (!mix.zoneDefinition || !renderer.IsPrepared()) {
					return nullptr;
				}
				Vector<f32> resolved(values, values + SFX_REVERB_PARAMETERS, GetMemoryResource(MemTag::Audio));
				ResolveParameterValues(*mix.zoneDefinition, resolved);
				IAudioEffect* effect = mix.zoneDefinition->CreateEffect(system.MakeBusEffectContext({resolved.data(), resolved.size()}));
				mix.madeRate = renderer.GetSampleRate();
				mix.madeLayout = renderer.GetLayout();
				return effect;
			}

			void UnbuildZones(AudioListenerMix& mix) {
				AudioSystem& system = GetAudioSystem();
				system.DestroyBus(mix.zones);
				system.RetireEffect(mix.zoneReverb);
				mix.zones = {};
				mix.zonesParent = {};
				mix.zoneDefinition = nullptr;
				mix.zoneReverb = nullptr;
				mix.inZone = false;
			}

			void BuildZones(AudioListenerMix& mix, const f32* values) {
				AudioSystem& system = GetAudioSystem();
				AudioBusDesc desc;
				desc.parent = mix.output;
				desc.clock = AudioBusClock::Inherit;
				mix.zones = system.CreateBus(desc);
				if (!mix.zones.IsValid()) {
					return;
				}
				mix.zonesParent = mix.output;
				mix.zoneDefinition = FindEffect(ZONE_REVERB);
				mix.zoneReverb = MakeZoneReverb(mix, values);
				AudioBusRowDesc rows[2];
				rows[0].kind = AudioBusRowKind::Effect;
				rows[0].effect = mix.zoneReverb;
				rows[0].level = 1.0f;
				if (!system.SetBusChain(mix.zones, rows, 2)) {
					if (mix.zoneReverb) {
						mix.zoneReverb->Release();
						mix.zoneReverb = nullptr;
					}
					SLT_LOG_ERROR("Audio", "a listener's reverb zone return couldn't be applied to its bus");
				}
				for (u32 index = 0; index < SFX_REVERB_PARAMETERS; ++index) {
					mix.zoneValues[index] = values[index];
				}
			}

			void SyncZones(AudioListenerMix& mix, const Vec3& position, std::span<const AudioReverbZoneShape> zones) {
				if (zones.empty()) {
					if (mix.zones.IsValid()) {
						UnbuildZones(mix);
					}
					return;
				}
				AudioReverbValues blend;
				mix.inZone = BlendReverbZones(zones, position, blend);
				f32 values[SFX_REVERB_PARAMETERS];
				WriteSfxReverb(blend, -10000.0f, values);
				if (!mix.zones.IsValid()) {
					BuildZones(mix, values);
					return;
				}
				AudioSystem& system = GetAudioSystem();
				if (mix.zonesParent != mix.output && system.SetBusParent(mix.zones, mix.output)) {
					mix.zonesParent = mix.output;
				}
				for (u32 index = 0; index < SFX_REVERB_PARAMETERS; ++index) {
					if (values[index] == mix.zoneValues[index]) {
						continue;
					}
					mix.zoneValues[index] = values[index];
					if (mix.zoneReverb && index < mix.zoneDefinition->GetParameters().size()) {
						system.SetBusEffectParameter(mix.zones, 0, index, ClampParameterValue(*mix.zoneDefinition, index, values[index]));
					}
				}
			}

			bool SameLayout(const ChannelLayout& left, const ChannelLayout& right) {
				if (left.count != right.count) {
					return false;
				}
				for (u32 channel = 0; channel < left.count; ++channel) {
					if (left.positions[channel] != right.positions[channel]) {
						return false;
					}
				}
				return true;
			}

		} // anonymous namespace

		void SyncListenerMix(AudioListenerMix& mix, const AudioListener& listener, std::span<const AudioReverbZoneShape> zones) {
			AudioSystem& system = GetAudioSystem();
			if (!system.IsUp()) {
				return;
			}
			const AudioBus output = ResolveGroupBus(listener.GetOutputAudioMixerGroup());
			const SceneObject* owner = listener.GetOwner().Get();

			AudioFilterList filters;
			if (GetEnabledFilterCount() > 0 || !mix.rows.empty()) {
				ReadFilters(owner, MAX_LISTENER_FILTERS, filters);
			}
			if (!SameFilters(mix, filters)) {
				if (filters.count == 0) {
					Unbuild(mix);
				} else {
					Rebuild(mix, filters, output);
				}
			} else {
				for (u32 index = 0; index < filters.count; ++index) {
					SendMovedValues(mix, index, mix.rows[index], filters.values + filters.firsts[index]);
				}
			}

			mix.output = output;
			if (mix.bus.IsValid() && mix.parent != output && system.SetBusParent(mix.bus, output)) {
				mix.parent = output;
			}

			SyncZones(mix, owner ? owner->GetWorldPosition() : Vec3{}, zones);
		}

		void ReleaseListenerMix(AudioListenerMix& mix) {
			if (GetAudioSystem().IsUp()) {
				if (mix.bus.IsValid()) {
					Unbuild(mix);
				}
				if (mix.zones.IsValid()) {
					UnbuildZones(mix);
				}
			}
			mix.output = {};
		}

		void ForgetListenerMix(AudioListenerMix& mix) {
			AudioSystem& system = GetAudioSystem();
			for (AudioListenerFilterRow& row : mix.rows) {
				system.ReleaseAtClose(row.instance);
				row.instance = nullptr;
			}
			mix.rows.clear();
			system.ReleaseAtClose(mix.zoneReverb);
			mix.zoneReverb = nullptr;
			mix.zoneDefinition = nullptr;
			mix.bus = {};
			mix.parent = {};
			mix.zones = {};
			mix.zonesParent = {};
			mix.output = {};
			mix.inZone = false;
		}

		namespace {

			void TakeOff(AudioBus bus, u32 row, IAudioEffect* instance, AudioDetachment& detachment) {
				AudioCommand command;
				command.kind = AudioCommandKind::SetBusRowEffect;
				command.bus = AudioBusAccess::GetSlot(bus);
				command.busGeneration = AudioBusAccess::GetGeneration(bus);
				command.mode = static_cast<u8>(row);
				command.effect = nullptr;
				GetAudioSystem().SendNow(command);
				detachment.effects.push_back(instance);
				detachment.posted = true;
			}

		} // anonymous namespace

		void DetachListenerMixEffects(AudioListenerMix& mix, IAudioEffectDefinition* definition, AudioDetachment& detachment) {
			for (u32 index = 0; index < mix.rows.size(); ++index) {
				AudioListenerFilterRow& row = mix.rows[index];
				if (row.definition != definition) {
					continue;
				}
				if (row.instance) {
					TakeOff(mix.bus, index, row.instance, detachment);
				}
				row.instance = nullptr;
				row.definition = nullptr;
			}
			if (mix.zoneDefinition == definition) {
				if (mix.zoneReverb) {
					TakeOff(mix.zones, 0, mix.zoneReverb, detachment);
				}
				mix.zoneReverb = nullptr;
				mix.zoneDefinition = nullptr;
			}
		}

		void ResolveListenerMixEffect(AudioListenerMix& mix, std::string_view name) {
			AudioSystem& system = GetAudioSystem();
			for (u32 index = 0; index < mix.rows.size(); ++index) {
				AudioListenerFilterRow& row = mix.rows[index];
				if (row.definition || row.effect != name) {
					continue;
				}
				row.definition = FindEffect(name);
				IAudioEffect* instance = MakeInstance(mix, row);
				if (instance && system.SetBusRowEffect(mix.bus, index, instance)) {
					row.instance = instance;
				} else if (instance) {
					instance->Release();
				}
			}
			if (mix.zones.IsValid() && !mix.zoneDefinition && name == ZONE_REVERB) {
				mix.zoneDefinition = FindEffect(name);
				IAudioEffect* instance = MakeZoneReverb(mix, mix.zoneValues);
				if (instance && system.SetBusRowEffect(mix.zones, 0, instance)) {
					mix.zoneReverb = instance;
				} else if (instance) {
					instance->Release();
				}
			}
		}

		void RemakeListenerMixEffects(AudioListenerMix& mix) {
			AudioSystem& system = GetAudioSystem();
			const AudioRenderer& renderer = system.GetRenderer();
			const bool same = mix.madeRate == renderer.GetSampleRate() && SameLayout(mix.madeLayout, renderer.GetLayout());
			for (u32 index = 0; mix.bus.IsValid() && index < mix.rows.size(); ++index) {
				AudioListenerFilterRow& row = mix.rows[index];
				if (!row.definition || (row.instance && same)) {
					continue;
				}
				IAudioEffect* fresh = MakeInstance(mix, row);
				if (!system.SetBusRowEffect(mix.bus, index, fresh)) {
					if (fresh) {
						fresh->Release();
					}
					continue;
				}
				if (row.instance) {
					row.instance->Release();
				}
				row.instance = fresh;
			}
			if (mix.zones.IsValid() && mix.zoneDefinition && !(mix.zoneReverb && same)) {
				IAudioEffect* fresh = MakeZoneReverb(mix, mix.zoneValues);
				if (!system.SetBusRowEffect(mix.zones, 0, fresh)) {
					if (fresh) {
						fresh->Release();
					}
					return;
				}
				if (mix.zoneReverb) {
					mix.zoneReverb->Release();
				}
				mix.zoneReverb = fresh;
			}
		}

	} // namespace AudioImpl

} // namespace Shalltear
