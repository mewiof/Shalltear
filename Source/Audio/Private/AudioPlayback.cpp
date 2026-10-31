#include "Shalltear/Audio/Audio.h"
#include "Shalltear/Audio/AudioClip.h"
#include "Shalltear/Audio/AudioConcurrency.h"
#include "Shalltear/Audio/AudioContainer.h"

#include "AudioClipAccess.h"
#include "AudioClipData.h"
#include "AudioComposite.h"
#include "AudioGeneratorAccess.h"
#include "AudioPlacement.h"
#include "AudioPlayback.h"
#include "AudioScenePass.h"
#include "AudioSettings.h"
#include "AudioSystem.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Settings.h"

#include <algorithm>
#include <span>

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			f32 Clamp(f32 value, f32 low, f32 high) {
				return value >= low ? (value <= high ? value : high) : low;
			}

			bool HasCurves(const AudioDistanceCurves* curves) {
				return curves && (!curves->customRolloff.keys.empty() || !curves->spatialBlend.keys.empty() || !curves->spread.keys.empty() ||
								  !curves->reverbZoneMix.keys.empty());
			}

			bool FollowsDistance(std::span<const AudioVoiceEffect> effects) {
				return std::any_of(effects.begin(), effects.end(),
								   [](const AudioVoiceEffect& effect) { return effect.distanceCurve && !effect.distanceCurve->keys.empty(); });
			}

			void KeepCurves(AudioVoiceRecord& record, const AudioDistanceCurves* curves) {
				if (!HasCurves(curves)) {
					if (record.curves) {
						Delete(record.curves);
						record.curves = nullptr;
					}
					return;
				}
				if (!record.curves) {
					record.curves = New<AudioDistanceCurves>(MemTag::Audio);
				}
				*record.curves = *curves;
			}

			const AudioConcurrency* ResolveConcurrency(Handle<AudioConcurrency> handle) {
				Object* object = ObjectRegistry::Resolve(handle.GetInstanceId());
				if (!object || !TypeRegistry::IsDerivedFrom(object->GetType(), AudioConcurrency::GetReflectType())) {
					return nullptr;
				}
				return static_cast<const AudioConcurrency*>(object);
			}

			const AudioContainer* ResolveContainer(Handle<AudioResource> resource) {
				Object* object = ObjectRegistry::Resolve(resource.GetInstanceId());
				if (!object || !TypeRegistry::IsDerivedFrom(object->GetType(), AudioContainer::GetReflectType())) {
					return nullptr;
				}
				return static_cast<const AudioContainer*>(object);
			}

			AudioVoiceRecord* Find(AudioVoice voice) {
				AudioSystem& system = GetAudioSystem();
				if (!system.IsUp()) {
					return nullptr;
				}
				return system.GetVoices().Find(AudioVoiceAccess::GetSlot(voice), AudioVoiceAccess::GetGeneration(voice));
			}

			AudioCommand CommandFor(AudioCommandKind kind, AudioVoice voice) {
				AudioCommand command;
				command.kind = kind;
				command.voice = AudioVoiceAccess::GetSlot(voice);
				command.generation = AudioVoiceAccess::GetGeneration(voice);
				return command;
			}

			void SendIfLive(AudioVoice voice, AudioCommand command) {
				AudioVoiceRecord* record = Find(voice);
				if (record && (record->claim == VoiceClaim::Playing || record->claim == VoiceClaim::Paused)) {
					GetAudioSystem().Send(command);
				}
			}

			u64 AskedUntil() {
				AudioClockStamp stamp;
				GetAudioSystem().GetRenderer().ReadClock(stamp);
				return stamp.frames + 2 * AudioRenderer::BLOCK_FRAMES;
			}

		} // anonymous namespace

	} // namespace AudioImpl

	namespace Audio {

		using namespace AudioImpl;

		AudioVoice Play(Handle<AudioResource> resource, const AudioPlayParams& params) {
			AudioSystem& system = GetAudioSystem();
			if (!system.IsUp()) {
				return {};
			}
			if (const AudioContainer* container = ResolveContainer(resource)) {
				return AdoptComposite(CreateContainerPlayback(resource, *container, params));
			}
			const AudioClip* clip = AudioClipAccess::Resolve(resource);
			AudioClipData* data = clip ? AudioClipAccess::GetPlayback(*clip) : nullptr;

			AudioGenerator* generator = clip ? nullptr : AudioGeneratorAccess::Resolve(resource);
			IAudioGeneratorInstance* instance = nullptr;
			if (generator) {
				const AudioRenderer& renderer = system.GetRenderer();
				if (!renderer.IsPrepared()) {
					return {};
				}
				AudioGeneratorFormat suggested;
				suggested.sampleRate = renderer.GetSampleRate();
				suggested.channels = renderer.GetChannels();
				AudioGeneratorFormat chosen;
				instance = generator->CreateInstance(suggested, chosen);
				if (!instance) {
					return {};
				}
				if (chosen.sampleRate == 0 || chosen.channels == 0 || chosen.channels > MAX_AUDIO_CHANNELS) {
					SLT_LOG_WARN("Audio", "the generator '{}' chose a format no voice can play ({} Hz x {}). Not started", generator->virtualPath,
								 chosen.sampleRate, chosen.channels);
					instance->Release();
					return {};
				}
				data = CreateGeneratorClipData(chosen.sampleRate, MakeStandardLayout(chosen.channels), instance);
			}
			auto refuse = [&]() -> AudioVoice {
				if (instance) {
					ReleaseClipData(data);
					instance->Release();
				}
				return {};
			};
			if (!data) {
				return refuse();
			}

			system.DrainEvents();
			AudioVoiceTable& voices = system.GetVoices();

			const f32 volume = Clamp(params.volume, 0.0f, MAX_VOICE_GAIN);
			const i32 priority = params.priority < MIN_PRIORITY ? MIN_PRIORITY : (params.priority > MAX_PRIORITY ? MAX_PRIORITY : params.priority);
			const bool curved = HasCurves(params.curves) || FollowsDistance(params.effects);
			const bool spatial = params.spatial.spatialBlend > 0.0f || curved;
			const bool wantsSpatial = params.spatial.spatialBlend > 0.0f || (params.curves && !params.curves->spatialBlend.keys.empty());
			std::span<const AudioListenerPose> listeners;
			f32 attenuation = 1.0f;
			f32 blend = params.spatial.spatialBlend;
			f32 share = 0.0f;
			if (spatial) {
				listeners = system.GatherListeners();
				const AudioSettings& settings = GetSettings();
				const AudioVoicePlacement placed = ComputePlacement(params.spatial, params.position, params.velocity, listeners, settings.volumeRolloffScale,
																	settings.dopplerFactor, false, params.curves, &share);
				attenuation = placed.params.distanceAttenuation;
				blend = placed.blend;
			}
			const bool toListener = !params.output.IsValid();
			const bool bypassListener = params.bypassEffects || params.bypassListenerEffects;
			AudioBus output = params.output;
			const f32 askedZoneMix = Clamp(params.reverbZoneMix, 0.0f, 1.1f);
			const f32 reverbZoneMix = params.curves && !params.curves->reverbZoneMix.keys.empty()
										  ? Clamp(params.curves->reverbZoneMix.Evaluate(share), 0.0f, 1.1f)
										  : askedZoneMix;
			AudioBus send;
			const bool routing = toListener && IsAnyListenerMixing();
			const bool sending = IsAnyListenerInZone() && ComputeZoneSendLevel(reverbZoneMix) > 0.0f && !system.IsDeviceClocked(params.output);
			if (routing || sending) {
				if (!spatial) {
					listeners = system.GatherListeners();
				}
				const i32 hearing = FindHearingListener(listeners, wantsSpatial, params.position);
				if (hearing >= 0) {
					const AudioListenerPose& listener = listeners[static_cast<size_t>(hearing)];
					output = routing ? RouteThrough(listener, bypassListener) : output;
					send = sending ? listener.zones : AudioBus{};
				}
			}
			const f32 sendLevel = send.IsValid() ? ComputeZoneSendLevel(reverbZoneMix) : 0.0f;
			const u32 busSlot = AudioBusAccess::GetSlot(output);
			const u32 busGeneration = AudioBusAccess::GetGeneration(output);
			AudioConcurrencyMember incoming;
			incoming.priority = priority;
			incoming.audibility = system.ComputeAudibility(volume, blend, attenuation, busSlot, busGeneration);
			incoming.sequence = ~0ull;

			const AudioConcurrency* concurrency = ResolveConcurrency(params.concurrency);
			AudioConcurrencyGroup* group = nullptr;
			if (!system.AdmitVoice(concurrency, params.owner.GetInstanceId(), params.position, incoming, group)) {
				return refuse();
			}

			u32 slot = 0;
			u32 generation = 0;
			if (!voices.Claim(data, slot, generation)) {
				SLT_LOG_WARN("Audio", "all {} voice slots are held by sounds still stopping. '{}' wasn't started", voices.GetCapacity(),
							 clip ? clip->virtualPath : generator->virtualPath);
				return refuse();
			}
			if (generator) {
				ReleaseClipData(data);
				voices.SetGenerator(slot, instance, generator->GetType()->ownerModule);
			}
			AudioVoiceRecord* record = voices.Find(slot, generation);
			record->resource = resource;
			record->loop = params.loop;
			record->spatial = params.spatial;
			record->position = params.position;
			record->velocity = params.velocity;
			record->priority = priority;
			record->volume = volume;
			record->bus = busSlot;
			record->busGeneration = busGeneration;
			record->toListener = toListener;
			record->bypassEffects = params.bypassEffects;
			record->bypassListenerEffects = params.bypassListenerEffects;
			record->reverbZoneMix = askedZoneMix;
			KeepCurves(*record, params.curves);
			record->distanceShare = share;
			record->send = AudioBusAccess::GetSlot(send);
			record->sendGeneration = AudioBusAccess::GetGeneration(send);
			record->sendLevel = sendLevel;
			record->audibility = incoming.audibility;
			if (group) {
				const f32 retrigger = concurrency->retriggerTime > 0.0f ? concurrency->retriggerTime : 0.0f;
				voices.JoinGroup(slot, *group, system.GetTimeFrames(),
								 static_cast<u64>(static_cast<f64>(retrigger) * static_cast<f64>(system.GetRenderer().GetSampleRate())));
			}

			const bool real = system.DecideAtStart(slot, *record);
			voices.SetReal(slot, real);

			if (spatial) {
				if (real) {
					system.AttachSpatial(slot, *record);
				}
				system.PlaceVoice(slot, *record, listeners);
			}

			AudioCommand commands[4];
			u32 count = 0;
			AudioCommand& command = commands[count++];
			command.kind = AudioCommandKind::Play;
			command.voice = slot;
			command.generation = generation;
			command.clip = data;
			command.at = params.at.frames;
			command.frame = params.startSample;
			command.volume = volume;
			command.pitch = Clamp(params.pitch, MIN_PITCH, MAX_PITCH);
			command.stereoPan = Clamp(params.stereoPan, -1.0f, 1.0f);
			command.loop = params.loop;
			command.flag = !real;
			command.bus = busSlot;
			command.busGeneration = busGeneration;
			command.other = AudioBusAccess::GetSlot(send);
			command.otherGeneration = AudioBusAccess::GetGeneration(send);
			command.value = sendLevel;
			if (params.bypassEffects) {
				AudioCommand& bypass = commands[count++];
				bypass.kind = AudioCommandKind::SetVoiceBypassEffects;
				bypass.voice = slot;
				bypass.generation = generation;
				bypass.flag = true;
			}
			if (system.MakeStartingEffects(slot, *record, params.effects, commands[count])) {
				++count;
			}
			const bool streams = real && system.MakeVoiceStream(slot, *record, commands[count]);
			count += streams ? 1u : 0u;
			if (!system.SendWhole({commands, count})) {
				if (streams) {
					system.DiscardVoiceStream(*record);
				}
				voices.Abandon(slot);
				return {};
			}
			if (streams) {
				system.CommitVoiceStream(*record);
			}

			record->askedPosition = params.startSample < data->frames ? params.startSample : data->frames;
			record->askedUntil = AskedUntil();

			system.NoteVoiceStarted(busSlot, busGeneration);
			return AudioVoiceAccess::Make(slot, generation);
		}

		void Stop(AudioVoice voice) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				composite->Stop();
				return;
			}
			AudioVoiceRecord* record = Find(voice);
			if (!record || (record->claim != VoiceClaim::Playing && record->claim != VoiceClaim::Paused)) {
				return;
			}
			if (GetAudioSystem().Send(CommandFor(AudioCommandKind::Stop, voice))) {
				GetAudioSystem().GetVoices().BeginStop(AudioVoiceAccess::GetSlot(voice), false);
			}
		}

		void SetScheduledStartTime(AudioVoice voice, AudioTime time) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				composite->SetScheduledStartTime(time);
				return;
			}
			AudioCommand command = CommandFor(AudioCommandKind::Reschedule, voice);
			command.at = time.frames;
			SendIfLive(voice, command);
		}

		void SetScheduledEndTime(AudioVoice voice, AudioTime time) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				composite->SetScheduledEndTime(time);
				return;
			}
			AudioCommand command = CommandFor(AudioCommandKind::Stop, voice);
			command.at = time.frames;
			SendIfLive(voice, command);
		}

		void Pause(AudioVoice voice) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				composite->Pause();
				return;
			}
			AudioVoiceRecord* record = Find(voice);
			if (!record || record->claim != VoiceClaim::Playing) {
				return;
			}
			if (GetAudioSystem().Send(CommandFor(AudioCommandKind::Pause, voice))) {
				GetAudioSystem().GetVoices().SetClaim(AudioVoiceAccess::GetSlot(voice), VoiceClaim::Paused);
				record->audibility = 0.0f;
			}
		}

		void UnPause(AudioVoice voice) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				composite->UnPause();
				return;
			}
			AudioVoiceRecord* record = Find(voice);
			if (!record || record->claim != VoiceClaim::Paused) {
				return;
			}
			AudioSystem& system = GetAudioSystem();
			if (!system.Send(CommandFor(AudioCommandKind::Resume, voice))) {
				return;
			}
			const u32 slot = AudioVoiceAccess::GetSlot(voice);
			system.GetVoices().SetClaim(slot, VoiceClaim::Playing);
			if (!record->real) {
				record->audibility = system.ComputeAudibility(*record);
				if (system.DecideAtStart(slot, *record)) {
					system.SetVoiceReal(slot, *record, true, {});
				}
			}
		}

		void SetVolume(AudioVoice voice, f32 volume) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				composite->SetVolume(volume);
				return;
			}
			AudioCommand command = CommandFor(AudioCommandKind::SetVolume, voice);
			command.value = Clamp(volume, 0.0f, MAX_VOICE_GAIN);
			AudioVoiceRecord* record = Find(voice);
			if (record && (record->claim == VoiceClaim::Playing || record->claim == VoiceClaim::Paused) && GetAudioSystem().Send(command)) {
				record->volume = command.value;
			}
		}

		void SetPitch(AudioVoice voice, f32 pitch) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				composite->SetPitch(pitch);
				return;
			}
			AudioCommand command = CommandFor(AudioCommandKind::SetPitch, voice);
			command.value = Clamp(pitch, MIN_PITCH, MAX_PITCH);
			SendIfLive(voice, command);
		}

		void SetStereoPan(AudioVoice voice, f32 stereoPan) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				composite->SetStereoPan(stereoPan);
				return;
			}
			AudioCommand command = CommandFor(AudioCommandKind::SetStereoPan, voice);
			command.value = Clamp(stereoPan, -1.0f, 1.0f);
			SendIfLive(voice, command);
		}

		void SetLoop(AudioVoice voice, bool loop) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				composite->SetLoop(loop);
				return;
			}
			if (AudioVoiceRecord* record = Find(voice)) {
				record->loop = loop;
			}
			AudioCommand command = CommandFor(AudioCommandKind::SetLoop, voice);
			command.value = loop ? 1.0f : 0.0f;
			SendIfLive(voice, command);
		}

		void SetOutput(AudioVoice voice, AudioBus output) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				composite->SetOutput(output);
				return;
			}
			AudioVoiceRecord* record = Find(voice);
			if (!record || (record->claim != VoiceClaim::Playing && record->claim != VoiceClaim::Paused)) {
				return;
			}
			record->toListener = !output.IsValid();
			if (record->toListener) {
				GetAudioSystem().RouteToListener(AudioVoiceAccess::GetSlot(voice), *record);
				return;
			}
			AudioCommand command = CommandFor(AudioCommandKind::SetVoiceOutput, voice);
			command.bus = AudioBusAccess::GetSlot(output);
			command.busGeneration = AudioBusAccess::GetGeneration(output);
			if (GetAudioSystem().Send(command)) {
				record->bus = command.bus;
				record->busGeneration = command.busGeneration;
			}
		}

		void SetReverbZoneMix(AudioVoice voice, f32 reverbZoneMix) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				composite->SetReverbZoneMix(reverbZoneMix);
				return;
			}
			AudioVoiceRecord* record = Find(voice);
			if (record && (record->claim == VoiceClaim::Playing || record->claim == VoiceClaim::Paused)) {
				record->reverbZoneMix = Clamp(reverbZoneMix, 0.0f, 1.1f);
				GetAudioSystem().RouteToListener(AudioVoiceAccess::GetSlot(voice), *record);
			}
		}

		void SetGeneratorParameter(AudioVoice voice, u32 index, f32 value) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				composite->SetGeneratorParameter(index, value);
				return;
			}
			AudioVoiceRecord* record = Find(voice);
			if (record && record->isGenerator && (record->claim == VoiceClaim::Playing || record->claim == VoiceClaim::Paused)) {
				GetAudioSystem().SendGeneratorParameter(AudioVoiceAccess::GetSlot(voice), AudioVoiceAccess::GetGeneration(voice), index, value);
			}
		}

		void SetPriority(AudioVoice voice, i32 priority) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				composite->SetPriority(priority);
				return;
			}
			AudioVoiceRecord* record = Find(voice);
			if (record && (record->claim == VoiceClaim::Playing || record->claim == VoiceClaim::Paused)) {
				record->priority = priority < MIN_PRIORITY ? MIN_PRIORITY : (priority > MAX_PRIORITY ? MAX_PRIORITY : priority);
			}
		}

		bool IsVirtual(AudioVoice voice) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				return composite->IsVirtual();
			}
			GetAudioSystem().DrainEvents();
			const AudioVoiceRecord* record = Find(voice);
			return record && (record->claim == VoiceClaim::Playing || record->claim == VoiceClaim::Paused) && !record->real;
		}

		void SetSpatialSettings(AudioVoice voice, const AudioSpatialSettings& settings) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				composite->SetSpatialSettings(settings);
				return;
			}
			AudioVoiceRecord* record = Find(voice);
			if (record && (record->claim == VoiceClaim::Playing || record->claim == VoiceClaim::Paused)) {
				record->spatial = settings;
				GetAudioSystem().OnVoiceSpatialChanged(AudioVoiceAccess::GetSlot(voice), *record);
			}
		}

		void SetDistanceCurves(AudioVoice voice, const AudioDistanceCurves* curves) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				composite->SetDistanceCurves(curves);
				return;
			}
			AudioVoiceRecord* record = Find(voice);
			if (record && (record->claim == VoiceClaim::Playing || record->claim == VoiceClaim::Paused)) {
				KeepCurves(*record, curves);
			}
		}

		void SetEffects(AudioVoice voice, std::span<const AudioVoiceEffect> effects) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				composite->SetEffects(effects);
				return;
			}
			AudioVoiceRecord* record = Find(voice);
			if (record && (record->claim == VoiceClaim::Playing || record->claim == VoiceClaim::Paused)) {
				GetAudioSystem().SetVoiceEffects(AudioVoiceAccess::GetSlot(voice), *record, effects);
			}
		}

		void SetEffectParameter(AudioVoice voice, u32 index, u32 parameter, f32 value) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				composite->SetEffectParameter(index, parameter, value);
				return;
			}
			AudioVoiceRecord* record = Find(voice);
			if (record && (record->claim == VoiceClaim::Playing || record->claim == VoiceClaim::Paused)) {
				GetAudioSystem().SetVoiceEffectParameter(AudioVoiceAccess::GetSlot(voice), *record, index, parameter, value);
			}
		}

		void SetBypassEffects(AudioVoice voice, bool bypass) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				composite->SetBypassEffects(bypass);
				return;
			}
			AudioVoiceRecord* record = Find(voice);
			if (!record || (record->claim != VoiceClaim::Playing && record->claim != VoiceClaim::Paused)) {
				return;
			}
			AudioCommand command = CommandFor(AudioCommandKind::SetVoiceBypassEffects, voice);
			command.flag = bypass;
			if (!GetAudioSystem().Send(command)) {
				return;
			}
			record->bypassEffects = bypass;
			if (record->toListener) {
				GetAudioSystem().RouteToListener(AudioVoiceAccess::GetSlot(voice), *record);
			}
		}

		void SetBypassListenerEffects(AudioVoice voice, bool bypass) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				composite->SetBypassListenerEffects(bypass);
				return;
			}
			AudioVoiceRecord* record = Find(voice);
			if (record && (record->claim == VoiceClaim::Playing || record->claim == VoiceClaim::Paused)) {
				record->bypassListenerEffects = bypass;
				if (record->toListener) {
					GetAudioSystem().RouteToListener(AudioVoiceAccess::GetSlot(voice), *record);
				}
			}
		}

		void SetPosition(AudioVoice voice, const Vec3& position, const Vec3& velocity) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				composite->SetPosition(position, velocity);
				return;
			}
			AudioVoiceRecord* record = Find(voice);
			if (record && (record->claim == VoiceClaim::Playing || record->claim == VoiceClaim::Paused)) {
				record->position = position;
				record->velocity = velocity;
			}
		}

		u64 GetTimeSamples(AudioVoice voice) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				return composite->GetTimeSamples();
			}
			AudioVoiceRecord* record = Find(voice);
			if (!record || record->claim == VoiceClaim::Free) {
				return 0;
			}
			AudioClockStamp stamp;
			const AudioRenderer& renderer = GetAudioSystem().GetRenderer();
			renderer.ReadClock(stamp);
			if (stamp.frames < record->askedUntil) {
				return record->askedPosition;
			}
			return renderer.GetVoicePosition(AudioVoiceAccess::GetSlot(voice));
		}

		void SetTimeSamples(AudioVoice voice, u64 sample) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				composite->SetTimeSamples(sample);
				return;
			}
			AudioVoiceRecord* record = Find(voice);
			if (!record || (record->claim != VoiceClaim::Playing && record->claim != VoiceClaim::Paused)) {
				return;
			}
			AudioCommand command = CommandFor(AudioCommandKind::Seek, voice);
			command.frame = sample;
			if (!GetAudioSystem().Send(command)) {
				return;
			}
			const u64 frames = record->clip ? record->clip->frames : 0;
			record->askedPosition = sample < frames ? sample : frames;
			record->askedUntil = AskedUntil();
		}

		bool IsPlaying(AudioVoice voice) {
			if (IAudioComposite* composite = FindComposite(voice)) {
				return composite->IsPlaying();
			}
			GetAudioSystem().DrainEvents();
			AudioVoiceRecord* record = Find(voice);
			return record && record->claim == VoiceClaim::Playing;
		}

		AudioTime GetTime() {
			return AudioTime{GetAudioSystem().GetTimeFrames()};
		}

		u32 GetOutputSampleRate() {
			return GetAudioSystem().GetRenderer().GetSampleRate();
		}

		f32 GetGlobalVolume() {
			return GetSettings().globalVolume;
		}

		bool SetGlobalVolume(f32 volume) {
			AudioSettings* settings = GetDeclaredSettings();
			if (!settings) {
				SLT_LOG_WARN("Audio", "SetGlobalVolume: the Audio settings category isn't declared");
				return false;
			}
			const f32 clamped = Clamp(volume, 0.0f, 1.0f);
			if (settings->globalVolume == clamped) {
				return true;
			}
			Settings::SetFieldEditScope(SETTINGS_CATEGORY_PATH, "globalVolume", SettingScope::UserProject);
			settings->globalVolume = clamped;
			Settings::NotifyChanged(SETTINGS_CATEGORY_PATH, "globalVolume");
			Settings::Save();
			return true;
		}

	} // namespace Audio

	namespace AudioImpl {

		u32 StopGameVoices() {
			AudioSystem& system = GetAudioSystem();
			if (!system.IsUp()) {
				return 0;
			}
			u32 stopped = StopGameComposites();
			system.DrainEvents();
			AudioVoiceTable& voices = system.GetVoices();
			for (u32 slot = 0; slot < voices.GetCapacity(); ++slot) {
				const AudioVoice voice = AudioVoiceAccess::Make(slot, voices.GetGeneration(slot));
				const AudioVoiceRecord* record = Find(voice);
				if (record && (record->claim == VoiceClaim::Playing || record->claim == VoiceClaim::Paused) &&
					!system.IsDeviceClocked(record->bus, record->busGeneration)) {
					Audio::Stop(voice);
					++stopped;
				}
			}
			return stopped;
		}

		void SetGameMuted(bool muted) { GetAudioSystem().SetGameMuted(muted); }

	} // namespace AudioImpl

} // namespace Shalltear
