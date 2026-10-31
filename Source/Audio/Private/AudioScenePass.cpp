#include "AudioScenePass.h"

#include "AudioListenerMix.h"
#include "AudioSystem.h"

#include "Shalltear/Audio/AudioListener.h"
#include "Shalltear/Audio/AudioReverbZone.h"
#include "Shalltear/Audio/AudioSource.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Time.h"

#include <algorithm>

namespace Shalltear {

	namespace AudioImpl {

		struct AudioSourceAccess {
			static void Tick(AudioSource& source, const Vec3& position, const Vec3& velocity) { source.Tick(position, velocity); }
		};

		struct AudioReverbZoneAccess {
			static AudioReverbZoneShape Shape(const AudioReverbZone& zone, const Vec3& position) {
				AudioReverbZoneShape shape;
				shape.position = position;
				shape.minDistance = zone._minDistance;
				shape.maxDistance = zone._maxDistance;
				if (zone._reverbPreset != AudioReverbPreset::User) {
					shape.values = GetReverbPreset(zone._reverbPreset);
					return shape;
				}
				shape.values.room = zone._room;
				shape.values.roomHF = zone._roomHF;
				shape.values.roomLF = zone._roomLF;
				shape.values.decayTime = zone._decayTime;
				shape.values.decayHFRatio = zone._decayHFRatio;
				shape.values.reflections = zone._reflections;
				shape.values.reflectionsDelay = zone._reflectionsDelay;
				shape.values.reverb = zone._reverb;
				shape.values.reverbDelay = zone._reverbDelay;
				shape.values.hfReference = zone._hfReference;
				shape.values.lfReference = zone._lfReference;
				shape.values.diffusion = zone._diffusion;
				shape.values.density = zone._density;
				return shape;
			}
		};

		namespace {

			struct VelocityTracker {
				Vec3 framePosition;
				Vec3 frameLoopStart;
				Vec3 fixedStart;
				Vec3 frameVelocity;
				Vec3 fixedVelocity;
				bool primed = false;
				bool frameLoopStarted = false;
				bool fixedPending = false;
				bool movedInFixed = false;
				bool fixedDriven = false;
			};

			void CloseFixedStep(VelocityTracker& tracker, const Vec3& position, f32 fixedDelta) {
				if (!tracker.fixedPending) {
					return;
				}
				tracker.fixedPending = false;
				const Vec3 moved = position - tracker.fixedStart;
				if (moved != Vec3{}) {
					tracker.movedInFixed = true;
				}
				tracker.fixedVelocity = fixedDelta > 0.0f ? moved * (1.0f / fixedDelta) : Vec3{};
			}

			Vec3 Measure(VelocityTracker& tracker, const Vec3& position, f32 frameDelta, AudioVelocityUpdateMode mode) {
				const bool movedInFrame = tracker.frameLoopStarted && position != tracker.frameLoopStart;
				tracker.frameVelocity = tracker.primed && frameDelta > 0.0f ? (position - tracker.framePosition) * (1.0f / frameDelta) : Vec3{};
				tracker.framePosition = position;
				tracker.primed = true;
				if (movedInFrame) {
					tracker.fixedDriven = false;
				} else if (tracker.movedInFixed) {
					tracker.fixedDriven = true;
				}
				tracker.movedInFixed = false;
				tracker.frameLoopStarted = false;
				switch (mode) {
				case AudioVelocityUpdateMode::Fixed:
					return tracker.fixedVelocity;
				case AudioVelocityUpdateMode::Dynamic:
					return tracker.frameVelocity;
				case AudioVelocityUpdateMode::Auto:
				default:
					return tracker.fixedDriven ? tracker.fixedVelocity : tracker.frameVelocity;
				}
			}

			struct SourceRow {
				Handle<AudioSource> handle;
				VelocityTracker tracker;
			};

			struct ListenerRow {
				Handle<AudioListener> handle;
				VelocityTracker tracker;
				Vec3 velocity;
				AudioListenerMix mix;
			};

			struct PassState {
				Vector<SourceRow> sources{GetMemoryResource(MemTag::Audio)};
				Vector<ListenerRow> listeners{GetMemoryResource(MemTag::Audio)};
				Vector<Handle<AudioReverbZone>> zones{GetMemoryResource(MemTag::Audio)};
				Vector<AudioReverbZoneShape> shapes{GetMemoryResource(MemTag::Audio)};
				u64 silentCues[8] = {};
				u32 silentNext = 0;
			};

			PassState* _state = nullptr;

			PassState& State() {
				if (!_state) {
					_state = New<PassState>(MemTag::Audio);
				}
				return *_state;
			}

			template <typename Row, typename T>
			void Add(Vector<Row>& rows, Handle<T> handle) {
				const u64 id = handle.GetInstanceId();
				Row* reuse = nullptr;
				for (Row& existing : rows) {
					if (existing.handle.GetInstanceId() == id) {
						return;
					}
					if (!reuse && existing.handle.GetInstanceId() == 0) {
						reuse = &existing;
					}
				}
				if (reuse) {
					*reuse = Row{};
					reuse->handle = handle;
					return;
				}
				Row row;
				row.handle = handle;
				rows.push_back(row);
			}

			template <typename Row>
			void Clear(Vector<Row>& rows, u64 id) {
				for (Row& existing : rows) {
					if (existing.handle.GetInstanceId() == id) {
						existing.handle = {};
						return;
					}
				}
			}

			bool PositionOf(const Component& component, Vec3& position) {
				SceneObject* owner = component.GetOwner().Get();
				if (!owner) {
					return false;
				}
				position = owner->GetWorldPosition();
				return true;
			}

			template <typename Sample>
			void SampleAll(Sample&& sample) {
				if (!_state) {
					return;
				}
				Vec3 position;
				for (SourceRow& row : _state->sources) {
					if (const AudioSource* source = row.handle.Get(); source && PositionOf(*source, position)) {
						sample(row.tracker, position);
					}
				}
				for (ListenerRow& row : _state->listeners) {
					if (const AudioListener* listener = row.handle.Get(); listener && PositionOf(*listener, position)) {
						sample(row.tracker, position);
					}
				}
			}

			void PublishListenerGate() {
				GetAudioSystem().SetListenerPresent(IsAnyListenerEnabled());
			}

			void LateUpdateStage(const FrameStageArgs&, void*) {
				GetAudioSystem().NoteGameplay();
				RunScenePass();
			}

		} // anonymous namespace

		bool IsInWorldScene(const Component& component) {
			Scene* scene = component.GetScene().Get();
			return scene && scene->GetKind() == SceneKind::World;
		}

		void RegisterSource(Handle<AudioSource> source) {
			if (source.GetInstanceId() != 0) {
				Add(State().sources, source);
			}
		}

		void UnregisterSource(Handle<AudioSource> source) {
			if (_state) {
				Clear(_state->sources, source.GetInstanceId());
			}
		}

		void RegisterListener(Handle<AudioListener> listener) {
			const u64 id = listener.GetInstanceId();
			if (id == 0) {
				return;
			}
			Vector<ListenerRow>& rows = State().listeners;
			for (const ListenerRow& row : rows) {
				if (row.handle.GetInstanceId() == id) {
					return;
				}
			}
			ListenerRow row;
			row.handle = listener;
			rows.push_back(std::move(row));
			PublishListenerGate();
		}

		void UnregisterListener(Handle<AudioListener> listener) {
			if (!_state) {
				return;
			}
			const u64 id = listener.GetInstanceId();
			Vector<ListenerRow>& rows = _state->listeners;
			for (size_t index = 0; index < rows.size(); ++index) {
				if (rows[index].handle.GetInstanceId() == id) {
					ReleaseListenerMix(rows[index].mix);
					rows.erase(rows.begin() + static_cast<std::ptrdiff_t>(index));
					break;
				}
			}
			PublishListenerGate();
		}

		void RegisterZone(Handle<AudioReverbZone> zone) {
			const u64 id = zone.GetInstanceId();
			if (id == 0) {
				return;
			}
			Vector<Handle<AudioReverbZone>>& zones = State().zones;
			for (const Handle<AudioReverbZone>& existing : zones) {
				if (existing.GetInstanceId() == id) {
					return;
				}
			}
			zones.push_back(zone);
		}

		void UnregisterZone(Handle<AudioReverbZone> zone) {
			if (!_state) {
				return;
			}
			const u64 id = zone.GetInstanceId();
			std::erase_if(_state->zones, [id](const Handle<AudioReverbZone>& existing) { return existing.GetInstanceId() == id; });
		}

		bool IsAnyListenerEnabled() {
			if (!_state) {
				return false;
			}
			for (const ListenerRow& row : _state->listeners) {
				if (row.handle.IsValid()) {
					return true;
				}
			}
			return false;
		}

		void GatherListenerPoses(Vector<AudioListenerPose>& out) {
			out.clear();
			if (!_state) {
				return;
			}
			for (const ListenerRow& row : _state->listeners) {
				const AudioListener* listener = row.handle.Get();
				SceneObject* owner = listener ? listener->GetOwner().Get() : nullptr;
				if (!owner) {
					continue;
				}
				AudioListenerPose pose;
				pose.position = owner->GetWorldPosition();
				pose.rotation = owner->GetWorldRotation();
				pose.velocity = row.velocity;
				pose.filters = row.mix.bus;
				pose.output = row.mix.output;
				pose.zones = row.mix.inZone && row.mix.zoneReverb ? row.mix.zones : AudioBus{};
				out.push_back(pose);
			}
		}

		bool IsAnyListenerMixing() {
			if (!_state) {
				return false;
			}
			for (const ListenerRow& row : _state->listeners) {
				if (IsMixing(row.mix)) {
					return true;
				}
			}
			return false;
		}

		bool IsAnyListenerInZone() {
			if (!_state) {
				return false;
			}
			for (const ListenerRow& row : _state->listeners) {
				if (row.mix.inZone) {
					return true;
				}
			}
			return false;
		}

		void DetachListenerEffects(IAudioEffectDefinition* definition, AudioDetachment& detachment) {
			if (_state) {
				for (ListenerRow& row : _state->listeners) {
					DetachListenerMixEffects(row.mix, definition, detachment);
				}
			}
		}

		void OnListenerEffectRegistered(std::string_view name) {
			if (_state) {
				for (ListenerRow& row : _state->listeners) {
					ResolveListenerMixEffect(row.mix, name);
				}
			}
		}

		void RemakeListenerEffects() {
			if (_state) {
				for (ListenerRow& row : _state->listeners) {
					RemakeListenerMixEffects(row.mix);
				}
			}
		}

		bool FirstSilentCue(u64 objectId) {
			PassState& state = State();
			for (const u64 told : state.silentCues) {
				if (told == objectId) {
					return false;
				}
			}
			state.silentCues[state.silentNext++ % std::size(state.silentCues)] = objectId;
			return true;
		}

		void RegisterScenePass(const void* owner) {
			FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Late, &LateUpdateStage, nullptr, owner);
		}

		void SampleFixedStep() {
			const f32 fixedDelta = Time::GetFixedDeltaTime();
			SampleAll([fixedDelta](VelocityTracker& tracker, const Vec3& position) {
				CloseFixedStep(tracker, position, fixedDelta);
				tracker.fixedStart = position;
				tracker.fixedPending = true;
			});
		}

		void SampleFrameStart() {
			const f32 fixedDelta = Time::GetFixedDeltaTime();
			SampleAll([fixedDelta](VelocityTracker& tracker, const Vec3& position) {
				CloseFixedStep(tracker, position, fixedDelta);
				tracker.frameLoopStart = position;
				tracker.frameLoopStarted = true;
			});
		}

		void RunScenePass() {
			if (!_state) {
				return;
			}
			const f32 frameDelta = Time::GetDeltaTime();

			Vector<ListenerRow>& listeners = _state->listeners;
			const size_t before = listeners.size();
			for (ListenerRow& row : listeners) {
				if (!row.handle.IsValid()) {
					ReleaseListenerMix(row.mix);
				}
			}
			listeners.erase(std::remove_if(listeners.begin(), listeners.end(), [](const ListenerRow& row) { return !row.handle.IsValid(); }),
							listeners.end());
			if (listeners.size() != before) {
				PublishListenerGate();
			}
			Vector<AudioReverbZoneShape>& shapes = _state->shapes;
			shapes.clear();
			for (const Handle<AudioReverbZone>& handle : _state->zones) {
				const AudioReverbZone* zone = handle.Get();
				const SceneObject* owner = zone ? zone->GetOwner().Get() : nullptr;
				if (owner) {
					shapes.push_back(AudioReverbZoneAccess::Shape(*zone, owner->GetWorldPosition()));
				}
			}
			for (ListenerRow& row : listeners) {
				if (const AudioListener* listener = row.handle.Get()) {
					SyncListenerMix(row.mix, *listener, {shapes.data(), shapes.size()});
				}
			}

			Vector<SourceRow>& sources = _state->sources;
			for (size_t index = 0; index < sources.size(); ++index) {
				AudioSource* source = sources[index].handle.Get();
				if (!source) {
					continue;
				}
				Vec3 position;
				if (!PositionOf(*source, position)) {
					continue;
				}
				const Vec3 velocity = Measure(sources[index].tracker, position, frameDelta, source->GetVelocityUpdateMode());
				AudioSourceAccess::Tick(*source, position, velocity);
			}
			sources.erase(std::remove_if(sources.begin(), sources.end(), [](const SourceRow& row) { return !row.handle.IsValid(); }), sources.end());

			for (ListenerRow& row : listeners) {
				const AudioListener* listener = row.handle.Get();
				Vec3 position;
				if (listener && PositionOf(*listener, position)) {
					row.velocity = Measure(row.tracker, position, frameDelta, listener->GetVelocityUpdateMode());
				}
			}

			GetAudioSystem().PlaceVoices();
		}

		void ShutdownScenePass() {
			if (_state) {
				for (ListenerRow& row : _state->listeners) {
					ForgetListenerMix(row.mix);
				}
				Delete(_state);
				_state = nullptr;
			}
		}

		u32 GetRegisteredSourceCount() {
			if (!_state) {
				return 0;
			}
			u32 count = 0;
			for (const SourceRow& row : _state->sources) {
				count += row.handle.IsValid() ? 1u : 0u;
			}
			return count;
		}

		u32 GetRegisteredZoneCount() {
			return _state ? static_cast<u32>(_state->zones.size()) : 0u;
		}

		u32 GetRegisteredListenerCount() {
			if (!_state) {
				return 0;
			}
			u32 count = 0;
			for (const ListenerRow& row : _state->listeners) {
				count += row.handle.IsValid() ? 1u : 0u;
			}
			return count;
		}

		namespace {

			u32 _enabledFilters = 0;

		} // anonymous namespace

		void NoteFilterEnabled(bool enabled) {
			if (enabled) {
				++_enabledFilters;
			} else if (_enabledFilters > 0) {
				--_enabledFilters;
			}
		}

		u32 GetEnabledFilterCount() { return _enabledFilters; }

	} // namespace AudioImpl

} // namespace Shalltear
