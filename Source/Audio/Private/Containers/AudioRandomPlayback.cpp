#include "AudioContainerKinds.h"
#include "AudioContainerPlayback.h"
#include "AudioContainerState.h"

#include "Shalltear/Audio/AudioContainer.h"

#include "Shalltear/Core/Memory.h"

#include <algorithm>
#include <cmath>

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			constexpr f64 LOOKAHEAD_SECONDS = 0.1;

			constexpr u32 MAX_STARTS_PER_UPDATE = 256;

			f64 ClampTo(f64 value, f64 low, f64 high) {
				return value >= low ? (value <= high ? value : high) : low;
			}

			f64 DrawRange(f64 a, f64 b) {
				return a <= b ? DrawBetween(a, b) : DrawBetween(b, a);
			}

			bool IsPlayable(const AudioRandomContainerElement& element) {
				return element.enabled && element.resource.GetInstanceId() != 0;
			}

			u64 FramesOfSeconds(f64 seconds) {
				const f64 frames = seconds * static_cast<f64>(Audio::GetOutputSampleRate());
				return frames > 0.0 ? static_cast<u64>(frames + 0.5) : 0;
			}

			class AudioRandomPlayback final : public AudioContainerPlayback {
			public:
				AudioRandomPlayback(Handle<AudioResource> resource, const AudioPlayParams& params)
					: AudioContainerPlayback(resource, params), _containerId(resource.GetInstanceId()),
					  _ownerId(params.owner.GetInstanceId()) {}

				bool Begin(const AudioRandomContainer& container) {
					_automatic = container.triggerMode == AudioRandomContainerTriggerMode::Automatic;
					if (!_automatic) {
						return Trigger(container, _at, _startSample);
					}
					u32 playable = 0;
					for (const AudioRandomContainerElement& element : container.elements) {
						playable += IsPlayable(element) ? 1 : 0;
					}
					if (playable == 0) {
						return false;
					}
					const i64 drawn = container.loopCount + static_cast<i64>(std::lround(DrawRange(container.loopCountRandomizationRange.x,
																								   container.loopCountRandomizationRange.y)));
					const u64 count = drawn > 0 ? static_cast<u64>(drawn) : 1;
					switch (container.loopMode) {
					case AudioRandomContainerLoopMode::Infinite:
						_remaining = ~0ull;
						break;
					case AudioRandomContainerLoopMode::Clips:
						_remaining = count;
						break;
					case AudioRandomContainerLoopMode::Cycles:
						_remaining = count * playable;
						break;
					}
					const AudioTime earliest = _timebase.EarliestExactStart();
					_next = _at > earliest ? _at : earliest;
					_nextKnown = true;
					Schedule(container);
					return !_children.empty();
				}

				void Update() override {
					PruneChildren();
					if (_stopped || _paused || !HasMoreToStart()) {
						return;
					}
					const AudioRandomContainer* container = ResolveContainer<AudioRandomContainer>();
					if (!container) {
						_remaining = 0;
						return;
					}
					if (!_nextKnown) {
						if (Audio::IsPlaying(_lastVoice)) {
							return;
						}
						_next = AudioTime{_timebase.Now().frames + FramesOfSeconds(OffsetSeconds(*container))};
						_nextKnown = true;
					}
					Schedule(*container);
				}
			protected:
				bool HasMoreToStart() const override { return _automatic && !_stopped && _remaining > 0; }

				void ShiftTimeline(i64 frames) override {
					if (_automatic && _nextKnown) {
						const i64 moved = static_cast<i64>(_next.frames) + frames;
						_next = AudioTime{moved > 0 ? static_cast<u64>(moved) : 0};
					}
				}

				void ApplyLoop() override {
					if (!_automatic) {
						AudioContainerPlayback::ApplyLoop();
					}
				}
			private:
				void Schedule(const AudioRandomContainer& container) {
					const u64 horizon = _timebase.Now().frames + FramesOfSeconds(LOOKAHEAD_SECONDS);
					for (u32 starts = 0; starts < MAX_STARTS_PER_UPDATE && _remaining > 0 && _nextKnown && _next.frames <= horizon; ++starts) {
						const AudioTime at = _next;
						const bool first = _children.empty() && _lastVoice == AudioVoice{};
						if (_end.frames != 0 && at >= _end) {
							_remaining = 0;
							break;
						}
						Trigger(container, at, first ? _startSample : 0);
						if (_remaining != ~0ull) {
							--_remaining;
						}
						if (container.automaticTriggerMode == AudioRandomContainerAutomaticTriggerMode::Pulse) {
							const u64 interval = FramesOfSeconds(IntervalSeconds(container));
							_next = AudioTime{at.frames + (interval > 0 ? interval : 1)};
						} else if (_lastFrames > 0) {
							_next = AudioTime{at.frames + _lastFrames + FramesOfSeconds(OffsetSeconds(container))};
						} else {
							_nextKnown = false;
						}
					}
				}

				f64 IntervalSeconds(const AudioRandomContainer& container) const {
					return container.automaticTriggerTime + DrawRange(container.automaticTriggerTimeRandomizationRange.x,
																	  container.automaticTriggerTimeRandomizationRange.y);
				}

				f64 OffsetSeconds(const AudioRandomContainer& container) const {
					const f64 seconds = IntervalSeconds(container);
					return seconds > 0.0 ? seconds : 0.0;
				}

				bool Trigger(const AudioRandomContainer& container, AudioTime at, u64 startSample) {
					const i32 index = PickRandomElement(container, GetRandomPickState(_containerId, _ownerId));
					if (index < 0) {
						return false;
					}
					const AudioRandomContainerElement& element = container.elements[static_cast<size_t>(index)];

					const f64 decibels = DrawRange(ClampTo(container.volumeRandomizationRange.x, -80.0, 0.0),
												   ClampTo(container.volumeRandomizationRange.y, -80.0, 0.0));
					const f32 volumeScale = static_cast<f32>(ClampTo(container.volume, 0.0, 1.0) * ClampTo(element.volume, 0.0, 1.0) *
															 std::pow(10.0, decibels / 20.0));
					const f64 cents = ClampTo(container.pitch + DrawRange(container.pitchRandomizationRange.x, container.pitchRandomizationRange.y),
											  -1902.0, 1902.0);
					const f32 pitchScale = static_cast<f32>(std::pow(2.0, cents / 1200.0));

					const AudioVoice voice = StartChild(element.resource, at, startSample, !_automatic && _loop, volumeScale, pitchScale,
														static_cast<u32>(index));
					if (!voice.IsValid()) {
						return false;
					}
					_lastVoice = voice;
					_lastFrames = FramesOf(element.resource, _pitch * pitchScale);
					return true;
				}

				const u64 _containerId;
				const u64 _ownerId;
				bool _automatic = false;
				u64 _remaining = 0;
				AudioTime _next;
				bool _nextKnown = false;
				AudioVoice _lastVoice;
				u64 _lastFrames = 0;
			};

		} // anonymous namespace

		IAudioComposite* CreateRandomPlayback(Handle<AudioResource> resource, const AudioRandomContainer& container,
											  const AudioPlayParams& params) {
			auto* playback = New<AudioRandomPlayback>(MemTag::Audio, resource, params);
			if (!playback->Begin(container)) {
				Delete(playback);
				return nullptr;
			}
			return playback;
		}

	} // namespace AudioImpl

} // namespace Shalltear
