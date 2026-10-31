#include "AudioProfilerOps.h"

#include "AudioComposite.h"
#include "AudioMixerLive.h"
#include "AudioRenderer.h"
#include "AudioScenePass.h"
#include "AudioStream.h"
#include "AudioSystem.h"

#include "Shalltear/Audio/AudioMixer.h"
#include "Shalltear/Audio/AudioSource.h"

#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Time.h"

#include <algorithm>
#include <cmath>
#include <format>

namespace Shalltear::AudioImpl {

	struct AudioProfilerAccess {
		static AudioVoice GetVoice(const AudioSource& source) { return source._voice; }
		static u32 CountOneShots(const AudioSource& source) { return static_cast<u32>(source._oneShots.size()); }
		static AudioVoice GetOneShot(const AudioSource& source, u32 index) { return source._oneShots[index].voice; }
		static bool IsMuted(const AudioSource& source) { return source._mute; }
		static bool IsPaused(const AudioSource& source) { return source._paused; }
	};

	namespace AudioProfilerOps {

		namespace {

			f64 TicksToMs(u64 ticks) { return static_cast<f64>(ticks) * 1000.0 / static_cast<f64>(Time::GetTicksPerSecond()); }

			struct Found {
				Vector<const AudioSource*> sources{GetMemoryResource(MemTag::Audio)};
				Vector<const AudioMixer*> mixers{GetMemoryResource(MemTag::Audio)};
			};

			void Find(Found& out) {
				ObjectRegistry::ForEachLiveObject(
					[](Object* object, u64, void* user) {
						Found& found = *static_cast<Found*>(user);
						const TypeInfo* type = object->GetType();
						if (TypeRegistry::IsDerivedFrom(type, AudioSource::GetReflectType())) {
							const auto* source = static_cast<const AudioSource*>(object);
							if (!source->IsPendingDestroy() && source->IsEffectivelyEnabled() && IsInWorldScene(*source)) {
								found.sources.push_back(source);
							}
						} else if (TypeRegistry::IsDerivedFrom(type, AudioMixer::GetReflectType())) {
							const auto* mixer = static_cast<const AudioMixer*>(object);
							const AudioMixerLive* live = AudioMixerAccess::GetLive(*mixer);
							if (live && live->live && live->built) {
								found.mixers.push_back(mixer);
							}
						}
					},
					&out);
			}

			str FileName(const Asset& asset) {
				const str& path = asset.virtualPath;
				const size_t slash = path.find_last_of('/');
				str name = path.substr(slash == str::npos ? 0 : slash + 1);
				const size_t dot = name.find_last_of('.');
				return dot == str::npos || dot == 0 ? name : name.substr(0, dot);
			}

			struct Played {
				u32 slot = 0;
				u32 generation = 0;
				const AudioSource* source = nullptr;
				bool oneShot = false;
			};

			void Attribute(AudioVoice voice, const AudioSource& source, bool oneShot, Vector<Played>& out, u32 depth) {
				if (depth > 8) {
					return;
				}
				if (IAudioComposite* composite = FindComposite(voice)) {
					AudioVoice children[64];
					const u32 count = std::min<u32>(composite->GetVoices(children), 64u);
					for (u32 index = 0; index < count; ++index) {
						Attribute(children[index], source, oneShot, out, depth + 1);
					}
					return;
				}
				out.push_back(Played{AudioVoiceAccess::GetSlot(voice), AudioVoiceAccess::GetGeneration(voice), &source, oneShot});
			}

			struct Named {
				u32 slot = 0;
				u32 generation = 0;
				str name;
			};

			void NameBuses(std::span<const AudioMixer* const> mixers, Vector<Named>& out) {
				for (const AudioMixer* mixer : mixers) {
					for (const AudioMixerLiveGroup& group : AudioMixerAccess::GetLive(*mixer)->groups) {
						const i32 at = FindGroup(*mixer, group.id);
						const std::string_view name = at >= 0 ? std::string_view(mixer->groups[static_cast<size_t>(at)].name) : std::string_view(group.id);
						out.push_back(Named{AudioBusAccess::GetSlot(group.bus), AudioBusAccess::GetGeneration(group.bus), std::format("{} ({})", name, FileName(*mixer))});
					}
				}
			}

		} // anonymous namespace

		void ReadCounts(const AudioRenderCounters& counters, u64 streamFailures, const AudioDeviceEvents& events, Counts& out) {
			out.ticks = Time::GetTicks();
			out.callbacks = counters.callbacks.load(std::memory_order_relaxed);
			out.frames = counters.frames.load(std::memory_order_relaxed);
			out.costTicks = counters.costTicks.load(std::memory_order_relaxed);
			out.worstCostTicks = counters.worstCostTicks.load(std::memory_order_relaxed);
			out.overruns = counters.overruns.load(std::memory_order_relaxed);
			out.streamUnderruns = counters.streamUnderruns.load(std::memory_order_relaxed);
			out.decoderShortfalls = counters.decoderShortfalls.load(std::memory_order_relaxed);
			out.lateStarts = counters.lateStarts.load(std::memory_order_relaxed);
			out.droppedEvents = counters.droppedEvents.load(std::memory_order_relaxed);
			out.streamFailures = streamFailures;
			out.restarts = events.Restarts();
			out.stops = events.stopped;
			out.reroutes = events.rerouted;
			out.interruptions = events.interrupted;
		}

		void ReadCounts(Counts& out) {
			AudioSystem& system = GetAudioSystem();
			ReadCounts(system.GetRenderer().GetCounters(), system.GetStreamer().GetFailures(), system.GetDevice().GetEvents(), out);
		}

		void Summarize(const Counts& before, const Counts& now, Summary& out) {
			out = Summary{};
			out.counts = now;
			AudioSystem& system = GetAudioSystem();
			Found found;
			Find(found);
			for (const AudioSource* source : found.sources) {
				++out.sources;
				out.playingSources += source->IsPlaying() ? 1u : 0u;
				out.pausedSources += AudioProfilerAccess::IsPaused(*source) ? 1u : 0u;
			}
			AudioVoiceTable& voices = system.GetVoices();
			out.voices = voices.CountLive();
			out.maxRealVoices = system.GetMaxRealVoices();
			for (u32 slot = 0; slot < voices.GetCapacity(); ++slot) {
				const AudioVoiceRecord& record = voices.At(slot);
				if (record.claim != VoiceClaim::Playing && record.claim != VoiceClaim::Paused) {
					continue;
				}
				out.realVoices += record.real ? 1u : 0u;
				out.streams += record.stream ? 1u : 0u;
			}
			out.virtualVoices = out.voices > out.realVoices ? out.voices - out.realVoices : 0u;

			const AudioRenderer& renderer = system.GetRenderer();
			out.sampleRate = renderer.GetSampleRate();
			out.periodFrames = system.GetDevice().GetFormat().periodFrames;
			const u64 callbacks = now.callbacks > before.callbacks ? now.callbacks - before.callbacks : 0;
			const u64 cost = now.costTicks > before.costTicks ? now.costTicks - before.costTicks : 0;
			const u64 frames = now.frames > before.frames ? now.frames - before.frames : 0;
			out.renderMeanMs = callbacks > 0 ? TicksToMs(cost) / static_cast<f64>(callbacks) : 0.0;
			const f64 made = out.sampleRate > 0 ? static_cast<f64>(frames) * 1000.0 / static_cast<f64>(out.sampleRate) : 0.0;
			out.renderShare = made > 0.0 ? TicksToMs(cost) / made : 0.0;
			out.renderWorstMs = TicksToMs(now.worstCostTicks);
			out.memoryBytes = MemoryManager::GetBytesForTag(MemTag::Audio);
			if (out.sampleRate > 0) {
				out.time = static_cast<f64>(system.GetTimeFrames()) / static_cast<f64>(out.sampleRate);
				out.deviceTime = static_cast<f64>(system.GetDeviceFrames()) / static_cast<f64>(out.sampleRate);
			}
		}

		void CollectVoices(Vector<Voice>& out) {
			out.clear();
			AudioSystem& system = GetAudioSystem();
			system.DrainEvents();
			Found found;
			Find(found);
			Vector<Played> played{GetMemoryResource(MemTag::Audio)};
			for (const AudioSource* source : found.sources) {
				Attribute(AudioProfilerAccess::GetVoice(*source), *source, false, played, 0);
				for (u32 shot = 0; shot < AudioProfilerAccess::CountOneShots(*source); ++shot) {
					Attribute(AudioProfilerAccess::GetOneShot(*source, shot), *source, true, played, 0);
				}
			}
			Vector<Named> buses{GetMemoryResource(MemTag::Audio)};
			NameBuses(found.mixers, buses);

			AudioVoiceTable& voices = system.GetVoices();
			const AudioRenderer& renderer = system.GetRenderer();
			for (u32 slot = 0; slot < voices.GetCapacity(); ++slot) {
				const AudioVoiceRecord& record = voices.At(slot);
				if (record.claim != VoiceClaim::Playing && record.claim != VoiceClaim::Paused) {
					continue;
				}
				Voice row;
				row.sequence = record.sequence;
				for (const Played& entry : played) {
					if (entry.slot == slot && entry.generation == record.generation) {
						const SceneObject* object = entry.source->GetOwner().Get();
						row.object = object ? object->name : str();
						row.oneShot = entry.oneShot;
						row.muted = AudioProfilerAccess::IsMuted(*entry.source);
						break;
					}
				}
				if (const AudioResource* resource = record.resource.Get()) {
					row.asset = FileName(*resource);
					row.assetId = record.resource.GetInstanceId();
				}
				const f32 blend = std::clamp(record.curves ? record.placement.blend : record.spatial.spatialBlend, 0.0f, 1.0f);
				const f32 attenuation = std::clamp(record.placement.params.distanceAttenuation, 0.0f, 1.0f);
				row.volume = record.volume * ((1.0f - blend) + blend * attenuation);
				row.audibility = record.audibility;
				for (const Named& bus : buses) {
					if (bus.slot == record.bus && bus.generation == record.busGeneration) {
						row.group = bus.name;
						break;
					}
				}
				row.priority = record.priority;
				row.is3D = AudioSystem::WantsSpatial(record);
				row.paused = record.claim == VoiceClaim::Paused;
				row.isVirtual = !record.real;
				row.looped = record.loop;
				if (AudioSystem::IsPlaced(record)) {
					const Vec3& at = record.placement.params.position;
					row.distance = std::sqrt(at.x * at.x + at.y * at.y + at.z * at.z);
				}
				if (row.is3D) {
					row.minDistance = record.spatial.minDistance;
					row.maxDistance = record.spatial.maxDistance;
				}
				if (record.clip && record.clip->sampleRate > 0) {
					const f64 rate = static_cast<f64>(record.clip->sampleRate);
					row.time = static_cast<f64>(renderer.GetVoicePosition(slot)) / rate;
					row.duration = record.isGenerator ? 0.0 : static_cast<f64>(record.clip->frames) / rate;
				}
				if (record.stream) {
					row.streamed = true;
					row.streamedAhead = static_cast<u64>(record.stream->filled.CountApproximate()) * record.clip->chunkFrames;
				}
				out.push_back(std::move(row));
			}
			std::sort(out.begin(), out.end(), [](const Voice& a, const Voice& b) {
				return a.audibility != b.audibility ? a.audibility > b.audibility : a.sequence < b.sequence;
			});
		}

		u64 CountPlays(u64 after, const std::function<void(u64 assetId)>& count) {
			AudioVoiceTable& voices = GetAudioSystem().GetVoices();
			u64 latest = after;
			for (u32 slot = 0; slot < voices.GetCapacity(); ++slot) {
				const AudioVoiceRecord& record = voices.At(slot);
				if (record.claim == VoiceClaim::Free || record.sequence <= after) {
					continue;
				}
				latest = std::max(latest, record.sequence);
				if (const u64 id = record.resource.GetInstanceId(); id != 0) {
					count(id);
				}
			}
			return latest;
		}

	} // namespace AudioProfilerOps

} // namespace Shalltear::AudioImpl
