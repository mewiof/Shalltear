#include "Shalltear/Editor/EditorFrameCost.h"

#include <cmath>

namespace Shalltear::EditorFrameCost {

	Recorder::Recorder(u64 ticksPerSecond, f64 epochSeconds) : _ticksPerSecond(ticksPerSecond) {
		if (ticksPerSecond != 0 && epochSeconds > 0.0) {
			_epochTicks = static_cast<u64>(static_cast<f64>(ticksPerSecond) * epochSeconds);
		}
	}

	u32 Recorder::BucketOf(u64 ticks) const {
		if (_ticksPerSecond == 0) {
			return BUCKET_COUNT;
		}
		u64 index = (ticks * 1000 * BUCKETS_PER_MS) / _ticksPerSecond;
		return index >= BUCKET_COUNT ? BUCKET_COUNT : static_cast<u32>(index);
	}

	void Recorder::Accumulate(Distribution& distribution, u64 ticks) const {
		if (distribution.frames == 0 || ticks < distribution.minTicks) {
			distribution.minTicks = ticks;
		}
		if (ticks > distribution.maxTicks) {
			distribution.maxTicks = ticks;
		}
		++distribution.frames;
		distribution.sumTicks += ticks;
		++distribution.buckets[BucketOf(ticks)];
	}

	void Recorder::Record(const Sample& sample) {
		if (_recorded < WARMUP_FRAMES) {
			++_recorded;
			++_excluded[static_cast<u32>(Exclusion::Warmup)];
			return;
		}
		++_recorded;

		if (sample.modal) {
			++_excluded[static_cast<u32>(Exclusion::Modal)];
			CloseGesture(false);
			return;
		}
		if (sample.throttled) {
			++_excluded[static_cast<u32>(Exclusion::Throttled)];
			CloseGesture(false);
			return;
		}

		AccumulateEpoch(sample);

		if (sample.pointerDown) {
			if (!_gestureOpen) {
				_open = Gesture{};
				_gestureOpen = true;
			}
			++_open.frames;
			if (sample.pointerMoved) {
				++_open.movedFrames;
				_open.moved = true;
			}
			for (u32 stage = 0; stage < STAGE_COUNT; ++stage) {
				Accumulate(_open.stages[stage], sample.stageTicks[stage]);
			}
			return;
		}

		CloseGesture(true);
		for (u32 stage = 0; stage < STAGE_COUNT; ++stage) {
			Accumulate(_classes[static_cast<u32>(Class::Steady)][stage], sample.stageTicks[stage]);
		}
	}

	void Recorder::CloseGesture(bool keep) {
		if (!_gestureOpen) {
			return;
		}
		_gestureOpen = false;
		if (!keep) {
			++_discardedGestures;
			return;
		}

		Class frameClass = _open.moved ? Class::Drag : Class::Steady;
		for (u32 stage = 0; stage < STAGE_COUNT; ++stage) {
			Distribution& target = _classes[static_cast<u32>(frameClass)][stage];
			const Distribution& source = _open.stages[stage];
			if (source.frames == 0) {
				continue;
			}
			if (target.frames == 0 || source.minTicks < target.minTicks) {
				target.minTicks = source.minTicks;
			}
			if (source.maxTicks > target.maxTicks) {
				target.maxTicks = source.maxTicks;
			}
			target.frames += source.frames;
			target.sumTicks += source.sumTicks;
			for (u32 bucket = 0; bucket <= BUCKET_COUNT; ++bucket) {
				target.buckets[bucket] += source.buckets[bucket];
			}
		}

		_open.index = ++_gestureIndex;
		if (_completedCount < MAX_COMPLETED) {
			_completed[_completedCount++] = _open;
		} else {
			++_lostGestures;
		}
	}

	void Recorder::BeginModalPeriod(u64 startTicks) {
		if (_modalOpen) {
			EndModalPeriod(startTicks);
		}
		_openModal = ModalPeriod{};
		_openModal.spanTicks = startTicks;
		_modalOpen = true;
	}

	void Recorder::NoteModalMove() {
		if (_modalOpen) {
			++_openModal.moves;
		}
	}

	void Recorder::NoteModalResize() {
		if (_modalOpen) {
			++_openModal.resizes;
		}
	}

	void Recorder::RecordModal(const Sample& sample) {
		for (u32 stage = 0; stage < STAGE_COUNT; ++stage) {
			Accumulate(_classes[static_cast<u32>(Class::Modal)][stage], sample.stageTicks[stage]);
		}
		if (!_modalOpen) {
			++_unpairedModalFrames;
			return;
		}
		++_openModal.frames;
		_openModal.insideTicks += sample.stageTicks[static_cast<u32>(Stage::Frame)];
		for (u32 stage = 0; stage < STAGE_COUNT; ++stage) {
			Accumulate(_openModal.stages[stage], sample.stageTicks[stage]);
		}
	}

	void Recorder::EndModalPeriod(u64 endTicks) {
		if (!_modalOpen) {
			return;
		}
		_modalOpen = false;
		_openModal.spanTicks = endTicks >= _openModal.spanTicks ? endTicks - _openModal.spanTicks : 0;
		_openModal.index = ++_modalIndex;
		if (_modalWaiting) {
			++_lostModalPeriods;
		}
		_endedModal = _openModal;
		_modalWaiting = true;
	}

	bool Recorder::TakeModalPeriod(ModalPeriod& out) {
		if (!_modalWaiting) {
			return false;
		}
		out = _endedModal;
		_modalWaiting = false;
		return true;
	}

	void Recorder::AccumulateEpoch(const Sample& sample) {
		if (_epochTicks == 0) {
			return;
		}
		if (!_epochOpen) {
			_openEpoch = Epoch{};
			_openEpoch.spanTicks = sample.startTicks;
			_epochOpen = true;
		}
		++_openEpoch.frames;
		if (sample.pointerDown) {
			++_openEpoch.pointerFrames;
		}
		for (u32 stage = 0; stage < STAGE_COUNT; ++stage) {
			Accumulate(_openEpoch.stages[stage], sample.stageTicks[stage]);
		}
		_epochLastTicks = sample.startTicks;
	}

	bool Recorder::EpochElapsed() const {
		if (!_epochOpen || _epochTicks == 0) {
			return false;
		}
		u64 span = _epochLastTicks >= _openEpoch.spanTicks ? _epochLastTicks - _openEpoch.spanTicks : 0;
		return span >= _epochTicks;
	}

	bool Recorder::CloseEpoch(const Counters& counters, bool atShutdown) {
		if (!_epochOpen) {
			return false;
		}
		_epochOpen = false;
		_openEpoch.closedAtShutdown = atShutdown;
		_openEpoch.spanTicks = _epochLastTicks >= _openEpoch.spanTicks ? _epochLastTicks - _openEpoch.spanTicks : 0;
		_openEpoch.index = ++_epochIndex;
		_openEpoch.counters = counters;
		if (_hadCounters) {
			for (u32 i = 0; i < COUNTER_COUNT; ++i) {
				u64 previous = _previousCounters.values[i];
				u64 current = counters.values[i];
				_openEpoch.delta.values[i] = current >= previous ? current - previous : 0;
			}
		}
		_previousCounters = counters;
		_hadCounters = true;

		if (_epochWaiting) {
			++_lostEpochs;
		}
		_closedEpoch = _openEpoch;
		_epochWaiting = true;
		return true;
	}

	bool Recorder::TakeEpoch(Epoch& out) {
		if (!_epochWaiting) {
			return false;
		}
		out = _closedEpoch;
		_epochWaiting = false;
		return true;
	}

	void Recorder::Finish() {
		CloseGesture(true);
	}

	bool Recorder::TakeGesture(Gesture& out) {
		if (_completedCount == 0) {
			return false;
		}
		out = _completed[0];
		for (u32 i = 1; i < _completedCount; ++i) {
			_completed[i - 1] = _completed[i];
		}
		--_completedCount;
		return true;
	}

	const Distribution& Recorder::Get(Class frameClass, Stage stage) const {
		return _classes[static_cast<u32>(frameClass)][static_cast<u32>(stage)];
	}

	u64 Recorder::GetExcluded(Exclusion reason) const {
		return _excluded[static_cast<u32>(reason)];
	}

	namespace {

		f64 TicksToMs(u64 ticks, u64 ticksPerSecond) {
			if (ticksPerSecond == 0) {
				return 0.0;
			}
			return (static_cast<f64>(ticks) * 1000.0) / static_cast<f64>(ticksPerSecond);
		}

	} // namespace

	f64 Recorder::PercentileMs(const Distribution& distribution, f64 quantile) const {
		if (distribution.frames == 0) {
			return 0.0;
		}
		f64 rankReal = quantile * static_cast<f64>(distribution.frames);
		u64 rank = static_cast<u64>(std::ceil(rankReal - rankReal * 1e-9));
		if (rank == 0) {
			rank = 1;
		}
		if (rank > distribution.frames) {
			rank = distribution.frames;
		}
		u64 seen = 0;
		for (u32 bucket = 0; bucket <= BUCKET_COUNT; ++bucket) {
			seen += distribution.buckets[bucket];
			if (seen >= rank) {
				if (bucket == BUCKET_COUNT) {
					break;
				}
				return static_cast<f64>(bucket + 1) / static_cast<f64>(BUCKETS_PER_MS);
			}
		}
		return static_cast<f64>(BUCKET_COUNT) / static_cast<f64>(BUCKETS_PER_MS);
	}

	f64 Recorder::MinMs(const Distribution& distribution) const {
		return distribution.frames == 0 ? 0.0 : TicksToMs(distribution.minTicks, _ticksPerSecond);
	}

	f64 Recorder::MaxMs(const Distribution& distribution) const {
		return distribution.frames == 0 ? 0.0 : TicksToMs(distribution.maxTicks, _ticksPerSecond);
	}

	f64 Recorder::MeanMs(const Distribution& distribution) const {
		if (distribution.frames == 0) {
			return 0.0;
		}
		return TicksToMs(distribution.sumTicks, _ticksPerSecond) / static_cast<f64>(distribution.frames);
	}

	f64 Recorder::BucketFloorMs(u32 bucket) const {
		return static_cast<f64>(bucket) / static_cast<f64>(BUCKETS_PER_MS);
	}

} // namespace Shalltear::EditorFrameCost
