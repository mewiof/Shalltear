#pragma once

#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

namespace Shalltear {

	namespace EditorFrameCost {

		enum class Stage : u8 {
			Pump = 0,
			UI,
			Update,
			Render,
			Frame,
			Count
		};

		inline constexpr u32 STAGE_COUNT = static_cast<u32>(Stage::Count);

		enum class Class : u8 {
			Steady = 0,
			Drag,
			Modal,
			Count
		};

		inline constexpr u32 CLASS_COUNT = static_cast<u32>(Class::Count);

		enum class Exclusion : u8 {
			Warmup = 0,
			Modal,
			Throttled,
			Count
		};

		inline constexpr u32 EXCLUSION_COUNT = static_cast<u32>(Exclusion::Count);

		enum class Counter : u8 {
			MeasureCalls = 0,
			MeasureWork,
			ArrangeCalls,
			ArrangeWork,
			LayoutPasses,
			ElementsVisited,
			ElementsPainted,
			ScopesWalked,
			ScopesSorted,
			LinkOps,
			LinkWork,
			TextWalks,
			GlyphsResolved,
			RunsBuilt,
			UIDrawCalls,
			UIQuads,
			UIContexts,
			VramBytes,
			LiveBuffers,
			LiveTextures,
			LiveSamplers,
			LivePipelines,
			Count
		};

		inline constexpr u32 COUNTER_COUNT = static_cast<u32>(Counter::Count);

		inline constexpr bool COUNTER_IS_LEVEL[COUNTER_COUNT] = {
			false, false, false, false, false,
			false, false, false, false,
			false, false,
			false, false, false,
			true, true, true,
			true, true, true, true, true};

		struct Counters {
			u64 values[COUNTER_COUNT] = {};
		};

		inline constexpr u64 WARMUP_FRAMES = 60;

		inline constexpr f64 DEFAULT_EPOCH_SECONDS = 20.0;

		inline constexpr u32 BUCKET_COUNT = 128;
		inline constexpr u64 BUCKETS_PER_MS = 10;

		struct Sample {
			u64 stageTicks[STAGE_COUNT] = {};
			u64 startTicks = 0;
			bool pointerDown = false;
			bool pointerMoved = false;
			bool modal = false;
			bool throttled = false;
		};

		struct Distribution {
			u64 frames = 0;
			u64 sumTicks = 0;
			u64 minTicks = 0;
			u64 maxTicks = 0;
			u32 buckets[BUCKET_COUNT + 1] = {};
		};

		struct Gesture {
			u32 index = 0;
			u64 frames = 0;
			u64 movedFrames = 0;
			bool moved = false;
			Distribution stages[STAGE_COUNT];
		};

		struct ModalPeriod {
			u32 index = 0;
			u64 spanTicks = 0;
			u64 frames = 0;
			u64 insideTicks = 0;
			u64 moves = 0;
			u64 resizes = 0;
			Distribution stages[STAGE_COUNT];
		};

		struct Epoch {
			u32 index = 0;
			u64 frames = 0;
			u64 pointerFrames = 0;
			u64 spanTicks = 0;
			Distribution stages[STAGE_COUNT];
			Counters counters;
			Counters delta;
			bool closedAtShutdown = false;
		};

		class SHALLTEAR_EDITOR_API Recorder {
		public:
			explicit Recorder(u64 ticksPerSecond, f64 epochSeconds = DEFAULT_EPOCH_SECONDS);

			void Record(const Sample& sample);

			void Finish();

			bool TakeGesture(Gesture& out);

			void BeginModalPeriod(u64 startTicks);
			void EndModalPeriod(u64 endTicks);

			void NoteModalMove();
			void NoteModalResize();

			void RecordModal(const Sample& sample);

			bool TakeModalPeriod(ModalPeriod& out);

			bool EpochElapsed() const;

			bool CloseEpoch(const Counters& counters, bool atShutdown = false);

			bool TakeEpoch(Epoch& out);

			u64 GetLostEpochs() const { return _lostEpochs; }

			const Distribution& Get(Class frameClass, Stage stage) const;
			u64 GetExcluded(Exclusion reason) const;

			u64 GetDiscardedGestures() const { return _discardedGestures; }
			u64 GetLostGestures() const { return _lostGestures; }

			u64 GetLostModalPeriods() const { return _lostModalPeriods; }
			u64 GetUnpairedModalFrames() const { return _unpairedModalFrames; }

			f64 PercentileMs(const Distribution& distribution, f64 quantile) const;
			f64 MinMs(const Distribution& distribution) const;
			f64 MaxMs(const Distribution& distribution) const;
			f64 MeanMs(const Distribution& distribution) const;

			f64 BucketFloorMs(u32 bucket) const;
		private:
			void Accumulate(Distribution& distribution, u64 ticks) const;
			u32 BucketOf(u64 ticks) const;
			void CloseGesture(bool keep);
			void AccumulateEpoch(const Sample& sample);

			u64 _ticksPerSecond = 0;
			static constexpr u32 MAX_COMPLETED = 2;

			Distribution _classes[CLASS_COUNT][STAGE_COUNT];
			Gesture _open;
			Gesture _completed[MAX_COMPLETED];
			u32 _completedCount = 0;
			u32 _gestureIndex = 0;
			u64 _excluded[EXCLUSION_COUNT] = {};
			u64 _recorded = 0;
			u64 _discardedGestures = 0;
			u64 _lostGestures = 0;
			bool _gestureOpen = false;

			ModalPeriod _openModal;
			ModalPeriod _endedModal;
			u32 _modalIndex = 0;
			u64 _lostModalPeriods = 0;
			u64 _unpairedModalFrames = 0;
			bool _modalOpen = false;
			bool _modalWaiting = false;

			u64 _epochTicks = 0;
			u64 _epochLastTicks = 0;
			Epoch _openEpoch;
			Epoch _closedEpoch;
			Counters _previousCounters;
			u32 _epochIndex = 0;
			u64 _lostEpochs = 0;
			bool _epochOpen = false;
			bool _epochWaiting = false;
			bool _hadCounters = false;
		};

	} // namespace EditorFrameCost

} // namespace Shalltear
