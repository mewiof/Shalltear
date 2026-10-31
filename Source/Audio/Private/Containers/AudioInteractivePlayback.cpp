#include "AudioContainerKinds.h"
#include "AudioContainerPlayback.h"

#include "Shalltear/Audio/AudioClip.h"
#include "Shalltear/Audio/AudioContainer.h"
#include "Shalltear/Audio/AudioMusicClock.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"

#include <cmath>

namespace Shalltear {

	namespace AudioImpl {

		namespace {

			constexpr f64 LOOKAHEAD_SECONDS = 0.1;

			constexpr i32 NO_CLIP = -1;

			struct ClipShape {
				u64 samples = 0;
				u32 rate = 0;
			};

			ClipShape ShapeOf(Handle<AudioResource> resource) {
				Object* object = ObjectRegistry::Resolve(resource.GetInstanceId());
				if (!object || !TypeRegistry::IsDerivedFrom(object->GetType(), AudioClip::GetReflectType())) {
					return {};
				}
				const AudioClip* clip = static_cast<const AudioClip*>(object);
				return ClipShape{clip->GetSamples(), clip->GetFrequency()};
			}

			f64 OutputRate() { return static_cast<f64>(Audio::GetOutputSampleRate()); }

			u64 OutputFramesOf(u64 samples, u32 rate, f64 speed) {
				if (rate == 0 || speed <= 0.0) {
					return 0;
				}
				return static_cast<u64>(static_cast<f64>(samples) * OutputRate() / static_cast<f64>(rate) / speed + 0.5);
			}

			i32 FindClip(const AudioInteractiveContainer& container, std::string_view name) {
				for (u32 index = 0; index < container.clips.size(); ++index) {
					if (container.clips[index].name == name) {
						return static_cast<i32>(index);
					}
				}
				return NO_CLIP;
			}

			AudioInteractiveContainerTransition FindTransition(const AudioInteractiveContainer& container, std::string_view from,
															   std::string_view to) {
				const std::pair<bool, bool> order[] = {{true, true}, {true, false}, {false, true}, {false, false}};
				for (const auto& [fromNamed, toNamed] : order) {
					for (const AudioInteractiveContainerTransition& row : container.transitions) {
						const bool fromMatches = fromNamed ? row.fromClip == from : row.fromClip.empty();
						const bool toMatches = toNamed ? row.toClip == to : row.toClip.empty();
						if (fromMatches && toMatches) {
							return row;
						}
					}
				}
				return AudioInteractiveContainerTransition{};
			}

			class AudioInteractivePlayback final : public AudioContainerPlayback {
			public:
				AudioInteractivePlayback(Handle<AudioResource> resource, const AudioPlayParams& params)
					: AudioContainerPlayback(resource, params) {}

				bool Begin(const AudioInteractiveContainer& container) {
					if (container.clips.empty()) {
						return false;
					}
					_previous.assign(container.clips.size(), 0);
					_hasPrevious.assign(container.clips.size(), 0);
					const i32 initial = container.initialClip.empty() ? 0 : FindClip(container, container.initialClip);
					if (initial == NO_CLIP) {
						return false;
					}
					const AudioTime earliest = _timebase.EarliestExactStart();
					const AudioTime at = _at > earliest ? _at : earliest;
					if (!StartClip(container, initial, at, _startSample, 1.0f).IsValid()) {
						return false;
					}
					Become(container, initial, at, _startSample);
					return true;
				}

				void Update() override {
					PruneChildren();
					if (_stopped || _paused) {
						return;
					}
					const AudioInteractiveContainer* container = ResolveContainer<AudioInteractiveContainer>();
					if (!container) {
						_planned = false;
						return;
					}
					const AudioTime now = _timebase.Now();
					if (_committed.active && now >= _committed.destAt) {
						_committed.active = false;
						Become(*container, _committed.to, _committed.destAt, _committed.destSample);
						if (_deferred != NO_CLIP) {
							const i32 deferred = _deferred;
							_deferred = NO_CLIP;
							if (deferred != _current) {
								Plan(*container, deferred, false);
							}
						}
					}
					if (_planned && !_committed.active && IsDue(_plan, now)) {
						Commit(*container);
					}
					WalkFades(now);
				}

				void SwitchToClip(std::string_view name) override {
					const AudioInteractiveContainer* container = ResolveContainer<AudioInteractiveContainer>();
					if (!container || _stopped || _current == NO_CLIP) {
						return;
					}
					const i32 to = FindClip(*container, name);
					if (to == NO_CLIP) {
						if (!_warnedUnknown) {
							_warnedUnknown = true;
							SLT_LOG_WARN("Audio", "SwitchToClip: the interactive container has no clip named '{}'", name);
						}
						return;
					}
					if (_committed.active) {
						_deferred = to;
						return;
					}
					if (to == _current) {
						_planned = false;
						PlanAdvance(*container);
						return;
					}
					Plan(*container, to, false);
				}

				bool GetMusicClock(AudioMusicClock& out) const override {
					out = _committed.active && _timebase.Now() >= _committed.destAt ? _committed.clock : _clock;
					return true;
				}

				u64 GetTimeSamples() override { return Audio::GetTimeSamples(_currentVoice); }

				void SetTimeSamples(u64 sample) override {
					const AudioInteractiveContainer* container = ResolveContainer<AudioInteractiveContainer>();
					if (!container || _committed.active || _current == NO_CLIP || !Audio::IsPlaying(_currentVoice)) {
						Audio::SetTimeSamples(_currentVoice, sample);
						return;
					}
					const AudioTime at = _timebase.EarliestExactStart();
					const AudioVoice left = _currentVoice;
					f32 gain = 1.0f;
					for (const AudioContainerChild& child : _children) {
						if (child.voice == left) {
							gain = child.volumeScale;
						}
					}
					Audio::SetScheduledEndTime(left, at);
					const AudioVoice entered = StartClip(*container, _current, at, sample, gain);
					for (Fade& fade : _fades) {
						if (fade.voice == left) {
							fade.voice = entered;
						}
					}
					const i32 waiting = _planned && !_plan.advance ? _plan.to : NO_CLIP;
					_planned = false;
					Become(*container, _current, at, sample);
					if (waiting != NO_CLIP) {
						Plan(*container, waiting, false);
					}
				}
			protected:
				bool HasMoreToStart() const override { return !_stopped && (_planned || _committed.active || _deferred != NO_CLIP); }
				void ApplyLoop() override {}

				void ShiftTimeline(i64 frames) override {
					auto shift = [frames](AudioTime& time) {
						const i64 moved = static_cast<i64>(time.frames) + frames;
						time = AudioTime{moved > 0 ? static_cast<u64>(moved) : 0};
					};
					shift(_clock.origin);
					shift(_currentAt);
					if (_planned) {
						shift(_plan.boundary);
						shift(_plan.destAt);
						shift(_plan.clock.origin);
					}
					if (_committed.active) {
						shift(_committed.destAt);
						shift(_committed.clock.origin);
					}
					for (Fade& fade : _fades) {
						shift(fade.from);
					}
				}
			private:
				void Become(const AudioInteractiveContainer& container, i32 clip, AudioTime at, u64 sample) {
					_current = clip;
					_currentAt = at;
					_currentSample = sample;
					_clock = ClockOf(container, clip, at, sample);
					_currentVoice = {};
					for (const AudioContainerChild& child : _children) {
						if (child.tag == static_cast<u32>(clip) && child.start == at) {
							_currentVoice = child.voice;
						}
					}
					const AudioInteractiveContainerClip& current = container.clips[static_cast<size_t>(clip)];
					_advanceTo = NO_CLIP;
					if (!current.loop && current.autoAdvance == AudioInteractiveAutoAdvanceMode::Enabled) {
						_advanceTo = FindClip(container, current.nextClip);
					} else if (!current.loop && current.autoAdvance == AudioInteractiveAutoAdvanceMode::ReturnToHold) {
						_advanceTo = _held;
						_held = NO_CLIP;
					}
					if (_advanceTo == clip) {
						_advanceTo = NO_CLIP;
					}
					PlanAdvance(container);
				}

				void PlanAdvance(const AudioInteractiveContainer& container) {
					if (_advanceTo != NO_CLIP) {
						Plan(container, _advanceTo, true);
					}
				}

				AudioMusicClock ClockOf(const AudioInteractiveContainer& container, i32 clip, AudioTime at, u64 sample) const {
					const AudioInteractiveContainerClip& entry = container.clips[static_cast<size_t>(clip)];
					const f64 bpm = entry.beatsPerMinute > 0.0f ? entry.beatsPerMinute : container.beatsPerMinute;
					const i32 bar = entry.beatsPerBar > 0 ? entry.beatsPerBar : container.beatsPerBar;
					const ClipShape shape = ShapeOf(entry.resource);
					const f64 speed = Speed();
					const u64 into = OutputFramesOf(sample, shape.rate, speed);
					AudioMusicClock clock;
					clock.origin = AudioTime{at.frames > into ? at.frames - into : 0};
					clock.beatsPerMinute = bpm > 0.0 ? bpm * speed : 0.0;
					clock.beatsPerBar = bar > 0 ? static_cast<u32>(bar) : 1;
					clock.sampleRate = Audio::GetOutputSampleRate();
					return clock;
				}

				f64 Speed() const { return _pitch < 0.0f ? -static_cast<f64>(_pitch) : static_cast<f64>(_pitch); }

				u64 PositionAt(const AudioInteractiveContainer& container, AudioTime time) const {
					const ClipShape shape = ShapeOf(container.clips[static_cast<size_t>(_current)].resource);
					if (shape.rate == 0 || time.frames < _currentAt.frames) {
						return _currentSample;
					}
					const f64 samples = static_cast<f64>(time.frames - _currentAt.frames) * static_cast<f64>(shape.rate) / OutputRate() * Speed();
					u64 position = _currentSample + static_cast<u64>(samples + 0.5);
					if (container.clips[static_cast<size_t>(_current)].loop && shape.samples > 0) {
						position %= shape.samples;
					}
					return position;
				}

				struct TransitionPlan {
					i32 to = NO_CLIP;
					i32 filler = NO_CLIP;
					AudioInteractiveFadeMode fade = AudioInteractiveFadeMode::Disabled;
					AudioTime boundary;
					AudioTime destAt;
					u64 destSample = 0;
					u64 fadeFrames = 0;
					u64 fillerFrames = 0;
					bool letFinish = false;
					bool awaitEnd = false;
					bool advance = false;
					bool holdPrevious = false;
					AudioMusicClock clock;
				};

				struct Committed {
					bool active = false;
					i32 to = NO_CLIP;
					AudioTime destAt;
					u64 destSample = 0;
					AudioMusicClock clock;
				};

				void Plan(const AudioInteractiveContainer& container, i32 to, bool autoAdvance) {
					const AudioInteractiveContainerClip& from = container.clips[static_cast<size_t>(_current)];
					const AudioInteractiveContainerClip& dest = container.clips[static_cast<size_t>(to)];
					AudioInteractiveContainerTransition row = FindTransition(container, from.name, dest.name);
					if (row.fade == AudioInteractiveFadeMode::Automatic) {
						row.fade = row.toTime == AudioInteractiveTransitionToTime::Start ? AudioInteractiveFadeMode::Out : AudioInteractiveFadeMode::Cross;
					}
					if (autoAdvance) {
						row.fromTime = AudioInteractiveTransitionFromTime::End;
						if (row.toTime == AudioInteractiveTransitionToTime::SamePosition) {
							row.toTime = AudioInteractiveTransitionToTime::Start;
						}
					}

					TransitionPlan plan;
					plan.to = to;
					plan.advance = autoAdvance;
					plan.fade = row.fade;
					plan.holdPrevious = row.holdPrevious;

					const AudioTime earliest = _timebase.EarliestExactStart();
					switch (row.fromTime) {
					case AudioInteractiveTransitionFromTime::Immediate:
						plan.boundary = earliest;
						break;
					case AudioInteractiveTransitionFromTime::NextBeat:
						plan.boundary = _clock.NextBoundary(AudioQuantization::NextBeat, earliest);
						break;
					case AudioInteractiveTransitionFromTime::NextBar:
						plan.boundary = _clock.NextBoundary(AudioQuantization::NextBar, earliest);
						break;
					case AudioInteractiveTransitionFromTime::End: {
						plan.letFinish = !from.loop;
						plan.awaitEnd = !EndOfCurrent(container, earliest, plan.boundary) && !from.loop;
						break;
					}
					}

					const f64 frames = _clock.HasTempo() ? static_cast<f64>(row.fadeBeats) * 60.0 / _clock.beatsPerMinute * _clock.sampleRate
														 : static_cast<f64>(row.fadeBeats) * OutputRate();
					plan.fadeFrames = frames > 0.0 ? static_cast<u64>(frames + 0.5) : 0;

					const ClipShape destShape = ShapeOf(dest.resource);
					if (row.toTime == AudioInteractiveTransitionToTime::PreviousPosition && _hasPrevious[static_cast<size_t>(to)]) {
						plan.destSample = _previous[static_cast<size_t>(to)];
					} else if (row.toTime == AudioInteractiveTransitionToTime::SamePosition && row.fromTime != AudioInteractiveTransitionFromTime::End) {
						const ClipShape fromShape = ShapeOf(from.resource);
						const u64 here = PositionAt(container, plan.boundary);
						const u64 there = fromShape.rate == 0 ? 0 : static_cast<u64>(static_cast<f64>(here) * destShape.rate / fromShape.rate + 0.5);
						plan.destSample = destShape.samples > 0 && there < destShape.samples ? there : 0;
					}

					const i32 filler = row.fillerClip.empty() ? NO_CLIP : FindClip(container, row.fillerClip);
					if (filler != NO_CLIP && filler != _current && filler != to) {
						const ClipShape fillerShape = ShapeOf(container.clips[static_cast<size_t>(filler)].resource);
						plan.fillerFrames = OutputFramesOf(fillerShape.samples, fillerShape.rate, Speed());
						plan.filler = plan.fillerFrames > 0 ? filler : NO_CLIP;
					}
					if (!plan.awaitEnd) {
						Place(container, plan, plan.boundary);
					}

					_plan = plan;
					_planned = true;
					if (!_committed.active && IsDue(_plan, _timebase.Now())) {
						Commit(container);
					}
				}

				void Place(const AudioInteractiveContainer& container, TransitionPlan& plan, AudioTime boundary) const {
					plan.boundary = boundary;
					plan.destAt = AudioTime{boundary.frames + plan.fillerFrames};
					plan.clock = ClockOf(container, plan.to, plan.destAt, plan.destSample);
				}

				bool IsDue(const TransitionPlan& plan, AudioTime now) const {
					if (plan.awaitEnd) {
						return !Audio::IsPlaying(_currentVoice);
					}
					return plan.boundary.frames <= now.frames + FramesOfSeconds(LOOKAHEAD_SECONDS);
				}

				bool EndOfCurrent(const AudioInteractiveContainer& container, AudioTime after, AudioTime& end) const {
					end = after;
					const AudioInteractiveContainerClip& entry = container.clips[static_cast<size_t>(_current)];
					const ClipShape shape = ShapeOf(entry.resource);
					const u64 whole = OutputFramesOf(shape.samples, shape.rate, Speed());
					if (whole == 0) {
						return false;
					}
					const u64 remaining = OutputFramesOf(shape.samples > _currentSample ? shape.samples - _currentSample : 0, shape.rate, Speed());
					u64 frames = _currentAt.frames + remaining;
					if (entry.loop) {
						while (frames < after.frames) {
							frames += whole;
						}
					}
					if (frames >= after.frames) {
						end = AudioTime{frames};
					}
					return true;
				}

				void Commit(const AudioInteractiveContainer& container) {
					_planned = false;
					if (_plan.awaitEnd) {
						_plan.awaitEnd = false;
						Place(container, _plan, _timebase.EarliestExactStart());
					}
					const TransitionPlan plan = _plan;
					const bool fadeIn = plan.fade == AudioInteractiveFadeMode::In || plan.fade == AudioInteractiveFadeMode::Cross;
					const bool fadeOut = plan.fade == AudioInteractiveFadeMode::Out || plan.fade == AudioInteractiveFadeMode::Cross;

					_previous[static_cast<size_t>(_current)] = PositionAt(container, plan.boundary);
					_hasPrevious[static_cast<size_t>(_current)] = 1;
					if (fadeOut && plan.fadeFrames > 0) {
						Audio::SetScheduledEndTime(_currentVoice, AudioTime{plan.boundary.frames + plan.fadeFrames});
						_fades.push_back(Fade{_currentVoice, plan.boundary, plan.fadeFrames, 1.0f, 0.0f});
					} else if (!plan.letFinish) {
						Audio::SetScheduledEndTime(_currentVoice, plan.boundary);
					}
					if (plan.holdPrevious) {
						_held = _current;
					}

					if (plan.filler != NO_CLIP) {
						StartChild(container.clips[static_cast<size_t>(plan.filler)].resource, plan.boundary, 0, false, 1.0f, 1.0f,
								   static_cast<u32>(plan.filler));
					}
					const f32 gain = fadeIn && plan.fadeFrames > 0 ? 0.0f : 1.0f;
					const AudioVoice dest = StartClip(container, plan.to, plan.destAt, plan.destSample, gain);
					if (dest.IsValid() && gain == 0.0f) {
						_fades.push_back(Fade{dest, plan.destAt, plan.fadeFrames, 0.0f, 1.0f});
					}
					_committed = Committed{true, plan.to, plan.destAt, plan.destSample, plan.clock};
				}

				AudioVoice StartClip(const AudioInteractiveContainer& container, i32 clip, AudioTime at, u64 sample, f32 gain) {
					const AudioInteractiveContainerClip& entry = container.clips[static_cast<size_t>(clip)];
					return StartChild(entry.resource, at, sample, entry.loop, gain, 1.0f, static_cast<u32>(clip));
				}

				struct Fade {
					AudioVoice voice;
					AudioTime from;
					u64 frames = 0;
					f32 start = 0.0f;
					f32 end = 0.0f;
				};

				void WalkFades(AudioTime now) {
					for (size_t index = 0; index < _fades.size();) {
						Fade& fade = _fades[index];
						const f64 progress = now.frames <= fade.from.frames
												 ? 0.0
												 : static_cast<f64>(now.frames - fade.from.frames) / static_cast<f64>(fade.frames ? fade.frames : 1);
						const f32 gain = progress >= 1.0 ? fade.end : static_cast<f32>(fade.start + (fade.end - fade.start) * progress);
						for (AudioContainerChild& child : _children) {
							if (child.voice == fade.voice) {
								child.volumeScale = gain;
								Audio::SetVolume(child.voice, _volume * gain);
							}
						}
						if (progress >= 1.0) {
							_fades.erase(_fades.begin() + static_cast<std::ptrdiff_t>(index));
						} else {
							++index;
						}
					}
				}

				static u64 FramesOfSeconds(f64 seconds) { return static_cast<u64>(seconds * OutputRate() + 0.5); }

				i32 _current = NO_CLIP;
				AudioVoice _currentVoice;
				AudioTime _currentAt;
				u64 _currentSample = 0;
				AudioMusicClock _clock;
				i32 _held = NO_CLIP;
				i32 _advanceTo = NO_CLIP;
				Vector<u64> _previous{GetMemoryResource(MemTag::Audio)};
				Vector<u8> _hasPrevious{GetMemoryResource(MemTag::Audio)};

				bool _planned = false;
				TransitionPlan _plan;
				Committed _committed;
				Vector<Fade> _fades{GetMemoryResource(MemTag::Audio)};
				i32 _deferred = NO_CLIP;
				bool _warnedUnknown = false;
			};

		} // anonymous namespace

		IAudioComposite* CreateInteractivePlayback(Handle<AudioResource> resource, const AudioInteractiveContainer& container,
												   const AudioPlayParams& params) {
			auto* playback = New<AudioInteractivePlayback>(MemTag::Audio, resource, params);
			if (!playback->Begin(container)) {
				Delete(playback);
				return nullptr;
			}
			return playback;
		}

	} // namespace AudioImpl

} // namespace Shalltear
