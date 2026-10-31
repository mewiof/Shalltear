#include "AudioContainerKinds.h"
#include "AudioContainerState.h"

#include "Shalltear/Audio/AudioContainer.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"

#include <algorithm>
#include <chrono>

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			struct PickEntry {
				u64 container = 0;
				u64 owner = 0;
				AudioRandomPickState state;
			};

			struct ContainerState {
				HashMap<u64, PickEntry> picks{GetMemoryResource(MemTag::Audio)};
				u64 random = 0;
				HashSet<u64> warnedCycles{GetMemoryResource(MemTag::Audio)};
			};

			ContainerState* _state = nullptr;

			ContainerState& State() {
				if (!_state) {
					_state = New<ContainerState>(MemTag::Audio);
					_state->random = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count()) | 1;
				}
				return *_state;
			}

			u64 NextRandom() {
				u64 z = (State().random += 0x9E3779B97F4A7C15ull);
				z = (z ^ (z >> 30)) * 0xBF58476D1CE4E5B9ull;
				z = (z ^ (z >> 27)) * 0x94D049BB133111EBull;
				return z ^ (z >> 31);
			}

			const AudioContainer* ResolveContainer(Handle<AudioResource> resource) {
				Object* object = ObjectRegistry::Resolve(resource.GetInstanceId());
				return object && TypeRegistry::IsDerivedFrom(object->GetType(), AudioContainer::GetReflectType())
						   ? static_cast<const AudioContainer*>(object)
						   : nullptr;
			}

			constexpr size_t RECENT_KEPT = 64;

			constexpr u32 MAX_PICKABLE = 256;

			bool IsPlayable(const AudioRandomContainerElement& element) {
				return element.enabled && element.resource.GetInstanceId() != 0;
			}

			constexpr u32 MAX_NESTING = 64;

			bool LeadsBackTo(const AudioContainer& root, const AudioContainer& at, u32 depth) {
				if (depth >= MAX_NESTING) {
					return true;
				}
				auto visit = [&](Handle<AudioResource> element) {
					const AudioContainer* nested = ResolveContainer(element);
					return nested && (nested == &root || LeadsBackTo(root, *nested, depth + 1));
				};
				if (TypeRegistry::IsDerivedFrom(at.GetType(), AudioRandomContainer::GetReflectType())) {
					for (const AudioRandomContainerElement& element : static_cast<const AudioRandomContainer&>(at).elements) {
						if (visit(element.resource)) {
							return true;
						}
					}
				} else if (TypeRegistry::IsDerivedFrom(at.GetType(), AudioLayeredContainer::GetReflectType())) {
					for (const AudioLayeredContainerLayer& layer : static_cast<const AudioLayeredContainer&>(at).layers) {
						if (visit(layer.resource)) {
							return true;
						}
					}
				} else if (TypeRegistry::IsDerivedFrom(at.GetType(), AudioInteractiveContainer::GetReflectType())) {
					for (const AudioInteractiveContainerClip& clip : static_cast<const AudioInteractiveContainer&>(at).clips) {
						if (visit(clip.resource)) {
							return true;
						}
					}
				}
				return false;
			}

		} // anonymous namespace

		AudioRandomPickState& GetRandomPickState(u64 container, u64 owner) {
			ContainerState& state = State();
			const u64 key = container * 0x9E3779B97F4A7C15ull ^ (owner + 0x632BE59BD9B4E019ull);
			auto found = state.picks.find(key);
			if (found != state.picks.end() && found->second.container == container && found->second.owner == owner) {
				return found->second.state;
			}
			for (auto it = state.picks.begin(); it != state.picks.end();) {
				const bool gone = !ObjectRegistry::Resolve(it->second.container) ||
								  (it->second.owner != 0 && !ObjectRegistry::Resolve(it->second.owner));
				it = gone ? state.picks.erase(it) : std::next(it);
			}
			PickEntry& entry = state.picks[key];
			entry = PickEntry{};
			entry.container = container;
			entry.owner = owner;
			return entry.state;
		}

		i32 PickRandomElement(const AudioRandomContainer& container, AudioRandomPickState& state) {
			const u32 count = static_cast<u32>(container.elements.size());
			u32 enabled[MAX_PICKABLE];
			u32 enabledCount = 0;
			for (u32 index = 0; index < count && enabledCount < MAX_PICKABLE; ++index) {
				if (IsPlayable(container.elements[index])) {
					enabled[enabledCount++] = index;
				}
			}
			if (enabledCount == 0) {
				return -1;
			}
			if (state.elementCount != count) {
				state = AudioRandomPickState{};
				state.elementCount = count;
			}

			const i32 avoidAuthored = container.avoidRepeatingLast;
			const u32 avoid = avoidAuthored <= 0 ? 0 : std::min<u32>(static_cast<u32>(avoidAuthored), enabledCount - 1);
			auto recentlyPlayed = [&](u32 index) {
				const size_t from = state.recent.size() > avoid ? state.recent.size() - avoid : 0;
				return std::find(state.recent.begin() + static_cast<std::ptrdiff_t>(from), state.recent.end(), index) != state.recent.end();
			};

			u32 picked = enabled[0];
			switch (container.playbackMode) {
			case AudioRandomContainerPlaybackMode::Sequential: {
				picked = enabled[0];
				for (u32 step = 0; step < count; ++step) {
					const u32 index = (state.sequentialNext + step) % count;
					if (IsPlayable(container.elements[index])) {
						picked = index;
						break;
					}
				}
				state.sequentialNext = (picked + 1) % count;
				break;
			}
			case AudioRandomContainerPlaybackMode::Random: {
				u32 candidates[MAX_PICKABLE];
				u32 candidateCount = 0;
				for (u32 i = 0; i < enabledCount; ++i) {
					if (!recentlyPlayed(enabled[i])) {
						candidates[candidateCount++] = enabled[i];
					}
				}
				picked = candidateCount > 0 ? candidates[DrawIndex(candidateCount)] : enabled[DrawIndex(enabledCount)];
				break;
			}
			case AudioRandomContainerPlaybackMode::Shuffle: {
				std::erase_if(state.pool, [&](u32 index) { return index >= count || !IsPlayable(container.elements[index]); });
				const bool fresh = state.pool.empty();
				if (fresh) {
					state.pool.assign(enabled, enabled + enabledCount);
				}
				u32 candidates[MAX_PICKABLE];
				u32 candidateCount = 0;
				for (u32 index : state.pool) {
					if (!fresh || !recentlyPlayed(index)) {
						candidates[candidateCount++] = index;
					}
				}
				picked = candidateCount > 0 ? candidates[DrawIndex(candidateCount)] : state.pool[DrawIndex(static_cast<u32>(state.pool.size()))];
				std::erase(state.pool, picked);
				break;
			}
			}
			state.recent.push_back(picked);
			if (state.recent.size() > RECENT_KEPT) {
				state.recent.erase(state.recent.begin());
			}
			return static_cast<i32>(picked);
		}

		f64 DrawUniform() {
			return static_cast<f64>(NextRandom() >> 11) * (1.0 / 9007199254740992.0);
		}

		f64 DrawBetween(f64 low, f64 high) {
			return high > low ? low + (high - low) * DrawUniform() : low;
		}

		u32 DrawIndex(u32 count) {
			return count > 1 ? static_cast<u32>(DrawUniform() * count) % count : 0;
		}

		IAudioComposite* CreateContainerPlayback(Handle<AudioResource> resource, const AudioContainer& container,
												 const AudioPlayParams& params) {
			if (LeadsBackTo(container, container, 0)) {
				if (State().warnedCycles.insert(resource.GetInstanceId()).second) {
					SLT_LOG_WARN("Audio", "the audio container '{}' contains itself (not played)",
								 container.virtualPath);
				}
				return nullptr;
			}
			if (TypeRegistry::IsDerivedFrom(container.GetType(), AudioRandomContainer::GetReflectType())) {
				return CreateRandomPlayback(resource, static_cast<const AudioRandomContainer&>(container), params);
			}
			if (TypeRegistry::IsDerivedFrom(container.GetType(), AudioLayeredContainer::GetReflectType())) {
				return CreateLayeredPlayback(resource, static_cast<const AudioLayeredContainer&>(container), params);
			}
			if (TypeRegistry::IsDerivedFrom(container.GetType(), AudioInteractiveContainer::GetReflectType())) {
				return CreateInteractivePlayback(resource, static_cast<const AudioInteractiveContainer&>(container), params);
			}
			return nullptr;
		}

		void ReleaseContainerState() {
			Delete(_state);
			_state = nullptr;
		}

	} // namespace AudioImpl

} // namespace Shalltear
