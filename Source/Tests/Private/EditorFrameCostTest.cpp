#include "TestsPCH.h"

#include "Shalltear/Editor/EditorFrameCost.h"

using namespace Shalltear;
using namespace Shalltear::EditorFrameCost;

namespace {

	constexpr u64 TICKS_PER_SECOND = 1'000'000;

	u64 Ms(f64 milliseconds) {
		return static_cast<u64>(milliseconds * 1000.0);
	}

	Sample MakeSample(f64 frameMs, bool pointerDown = false, bool pointerMoved = false) {
		Sample sample;
		sample.stageTicks[static_cast<u32>(Stage::Pump)] = Ms(frameMs * 0.1);
		sample.stageTicks[static_cast<u32>(Stage::UI)] = Ms(frameMs * 0.2);
		sample.stageTicks[static_cast<u32>(Stage::Update)] = Ms(frameMs * 0.4);
		sample.stageTicks[static_cast<u32>(Stage::Render)] = Ms(frameMs * 0.5);
		sample.stageTicks[static_cast<u32>(Stage::Frame)] = Ms(frameMs);
		sample.pointerDown = pointerDown;
		sample.pointerMoved = pointerMoved;
		return sample;
	}

	void PassWarmup(Recorder& recorder) {
		for (u64 i = 0; i < WARMUP_FRAMES; ++i) {
			recorder.Record(MakeSample(1.0));
		}
	}

	const Distribution& Frame(const Recorder& recorder, Class frameClass) {
		return recorder.Get(frameClass, Stage::Frame);
	}

} // namespace

TEST(EditorFrameCostTest, GestureThatNeverMovedIsNotDrag) {
	Recorder recorder{TICKS_PER_SECOND};
	PassWarmup(recorder);

	for (int i = 0; i < 3; ++i) {
		recorder.Record(MakeSample(2.0, true, false));
	}
	recorder.Record(MakeSample(1.0));

	EXPECT_EQ(Frame(recorder, Class::Drag).frames, 0u);
	EXPECT_EQ(Frame(recorder, Class::Steady).frames, 4u);
}

TEST(EditorFrameCostTest, GestureClassifiesItsFrames) {
	Recorder recorder{TICKS_PER_SECOND};
	PassWarmup(recorder);

	recorder.Record(MakeSample(2.0, true, false));
	recorder.Record(MakeSample(2.0, true, true));
	recorder.Record(MakeSample(2.0, true, false));
	recorder.Record(MakeSample(1.0));

	EXPECT_EQ(Frame(recorder, Class::Drag).frames, 3u);
	EXPECT_EQ(Frame(recorder, Class::Steady).frames, 1u);
}

TEST(EditorFrameCostTest, GesturesReportedAndAccumulated) {
	Recorder recorder{TICKS_PER_SECOND};
	PassWarmup(recorder);

	for (int gestureIndex = 0; gestureIndex < 2; ++gestureIndex) {
		for (int i = 0; i < 2; ++i) {
			recorder.Record(MakeSample(3.0, true, true));
		}
		recorder.Record(MakeSample(1.0));
	}

	Gesture first;
	Gesture second;
	ASSERT_TRUE(recorder.TakeGesture(first));
	ASSERT_TRUE(recorder.TakeGesture(second));
	EXPECT_FALSE(recorder.TakeGesture(first));

	EXPECT_EQ(first.index, 1u);
	EXPECT_EQ(second.index, 2u);
	EXPECT_EQ(first.frames, 2u);
	EXPECT_EQ(second.frames, 2u);
	EXPECT_TRUE(first.moved);
	EXPECT_EQ(first.movedFrames, 2u);
	EXPECT_EQ(Frame(recorder, Class::Drag).frames, 4u);
}

TEST(EditorFrameCostTest, OpenGestureReportedAtShutdown) {
	Recorder recorder{TICKS_PER_SECOND};
	PassWarmup(recorder);

	recorder.Record(MakeSample(4.0, true, true));
	recorder.Record(MakeSample(4.0, true, true));
	EXPECT_EQ(Frame(recorder, Class::Drag).frames, 0u);

	recorder.Finish();

	EXPECT_EQ(Frame(recorder, Class::Drag).frames, 2u);
	Gesture gesture;
	ASSERT_TRUE(recorder.TakeGesture(gesture));
	EXPECT_EQ(gesture.frames, 2u);
}

TEST(EditorFrameCostTest, TheWarmupFramesAreExcludedAndCounted) {
	Recorder recorder{TICKS_PER_SECOND};
	for (u64 i = 0; i < WARMUP_FRAMES; ++i) {
		recorder.Record(MakeSample(400.0));
	}
	recorder.Record(MakeSample(1.0));

	EXPECT_EQ(recorder.GetExcluded(Exclusion::Warmup), WARMUP_FRAMES);
	EXPECT_EQ(Frame(recorder, Class::Steady).frames, 1u);
	EXPECT_LT(recorder.MaxMs(Frame(recorder, Class::Steady)), 2.0);
}

TEST(EditorFrameCostTest, ModalFrameExcludedWithGesture) {
	Recorder recorder{TICKS_PER_SECOND};
	PassWarmup(recorder);

	recorder.Record(MakeSample(4.0, true, true));
	Sample modal = MakeSample(340.0, true, true);
	modal.modal = true;
	recorder.Record(modal);
	recorder.Record(MakeSample(1.0));

	EXPECT_EQ(recorder.GetExcluded(Exclusion::Modal), 1u);
	EXPECT_EQ(recorder.GetDiscardedGestures(), 1u);
	EXPECT_EQ(Frame(recorder, Class::Drag).frames, 0u);
	Gesture gesture;
	EXPECT_FALSE(recorder.TakeGesture(gesture));
}

TEST(EditorFrameCostTest, AThrottledFrameIsExcludedAndCounted) {
	Recorder recorder{TICKS_PER_SECOND};
	PassWarmup(recorder);

	Sample throttled = MakeSample(250.0);
	throttled.throttled = true;
	recorder.Record(throttled);
	recorder.Record(MakeSample(1.0));

	EXPECT_EQ(recorder.GetExcluded(Exclusion::Throttled), 1u);
	EXPECT_EQ(Frame(recorder, Class::Steady).frames, 1u);
	EXPECT_LT(recorder.MaxMs(Frame(recorder, Class::Steady)), 2.0);
}

TEST(EditorFrameCostTest, PercentilesComeFromBucketSampleLandedIn) {
	Recorder recorder{TICKS_PER_SECOND};
	PassWarmup(recorder);

	for (int i = 0; i < 90; ++i) {
		recorder.Record(MakeSample(1.05));
	}
	for (int i = 0; i < 9; ++i) {
		recorder.Record(MakeSample(3.05));
	}
	recorder.Record(MakeSample(9.05));

	const Distribution& steady = Frame(recorder, Class::Steady);
	ASSERT_EQ(steady.frames, 100u);
	EXPECT_DOUBLE_EQ(recorder.PercentileMs(steady, 0.50), 1.1);
	EXPECT_DOUBLE_EQ(recorder.PercentileMs(steady, 0.95), 3.1);
	EXPECT_DOUBLE_EQ(recorder.PercentileMs(steady, 0.99), 3.1);
	EXPECT_DOUBLE_EQ(recorder.PercentileMs(steady, 1.00), 9.1);
	EXPECT_NEAR(recorder.MinMs(steady), 1.05, 0.001);
	EXPECT_NEAR(recorder.MaxMs(steady), 9.05, 0.001);
	EXPECT_NEAR(recorder.MeanMs(steady), (90 * 1.05 + 9 * 3.05 + 9.05) / 100.0, 0.001);
}

TEST(EditorFrameCostTest, OverflowBucketKeepsMax) {
	Recorder recorder{TICKS_PER_SECOND};
	PassWarmup(recorder);

	recorder.Record(MakeSample(1.05));
	recorder.Record(MakeSample(64.0));

	const Distribution& steady = Frame(recorder, Class::Steady);
	EXPECT_EQ(steady.buckets[BUCKET_COUNT], 1u);
	EXPECT_DOUBLE_EQ(recorder.PercentileMs(steady, 1.00),
					 static_cast<f64>(BUCKET_COUNT) / static_cast<f64>(BUCKETS_PER_MS));
	EXPECT_NEAR(recorder.MaxMs(steady), 64.0, 0.001);
}

TEST(EditorFrameCostTest, StagesAreKeptApartNotSummed) {
	Recorder recorder{TICKS_PER_SECOND};
	PassWarmup(recorder);

	recorder.Record(MakeSample(10.0, true, true));
	recorder.Finish();

	EXPECT_NEAR(recorder.MeanMs(recorder.Get(Class::Drag, Stage::UI)), 2.0, 0.01);
	EXPECT_NEAR(recorder.MeanMs(recorder.Get(Class::Drag, Stage::Render)), 5.0, 0.01);
	EXPECT_NEAR(recorder.MeanMs(recorder.Get(Class::Drag, Stage::Frame)), 10.0, 0.01);
}

TEST(EditorFrameCostTest, EmptyDistributionReportsZeroNotDivision) {
	Recorder recorder{TICKS_PER_SECOND};

	const Distribution& drag = Frame(recorder, Class::Drag);
	EXPECT_EQ(drag.frames, 0u);
	EXPECT_DOUBLE_EQ(recorder.PercentileMs(drag, 0.50), 0.0);
	EXPECT_DOUBLE_EQ(recorder.MinMs(drag), 0.0);
	EXPECT_DOUBLE_EQ(recorder.MaxMs(drag), 0.0);
	EXPECT_DOUBLE_EQ(recorder.MeanMs(drag), 0.0);
}

TEST(EditorFrameCostTest, RecorderCostsWhatDesignNoteSays) {
	EXPECT_LE(sizeof(Recorder), 32u * 1024u);
}

TEST(EditorFrameCostTest, PeriodReportsFramesAndTraffic) {
	Recorder recorder{TICKS_PER_SECOND};
	PassWarmup(recorder);

	recorder.BeginModalPeriod(Ms(1000.0));
	for (int i = 0; i < 3; ++i) {
		recorder.NoteModalMove();
	}
	recorder.RecordModal(MakeSample(2.0));
	for (int i = 0; i < 5; ++i) {
		recorder.NoteModalMove();
	}
	recorder.NoteModalResize();
	recorder.RecordModal(MakeSample(2.0));
	recorder.EndModalPeriod(Ms(1500.0));

	ModalPeriod period;
	ASSERT_TRUE(recorder.TakeModalPeriod(period));
	EXPECT_EQ(period.index, 1u);
	EXPECT_EQ(period.frames, 2u);
	EXPECT_EQ(period.moves, 8u);
	EXPECT_EQ(period.resizes, 1u);
	EXPECT_EQ(period.spanTicks, Ms(500.0));
	EXPECT_FALSE(recorder.TakeModalPeriod(period));
}

TEST(EditorFrameCostTest, ReEntrantFrameCountsNowhere) {
	Recorder recorder{TICKS_PER_SECOND, 10.0};
	PassWarmup(recorder);

	recorder.Record(MakeSample(2.0, true, true));
	recorder.BeginModalPeriod(Ms(1000.0));
	for (int i = 0; i < 40; ++i) {
		recorder.RecordModal(MakeSample(9.0));
	}
	recorder.EndModalPeriod(Ms(1600.0));
	recorder.Record(MakeSample(1.0));

	Gesture gesture;
	ASSERT_TRUE(recorder.TakeGesture(gesture));
	EXPECT_EQ(gesture.frames, 1u);
	EXPECT_EQ(Frame(recorder, Class::Drag).frames, 1u);
	EXPECT_EQ(Frame(recorder, Class::Modal).frames, 40u);

	recorder.CloseEpoch(Counters{});
	Epoch epoch;
	ASSERT_TRUE(recorder.TakeEpoch(epoch));
	EXPECT_EQ(epoch.frames, 2u);
}

TEST(EditorFrameCostTest, PeriodInsideIsGrantedCost) {
	Recorder recorder{TICKS_PER_SECOND};
	PassWarmup(recorder);

	recorder.BeginModalPeriod(0);
	recorder.RecordModal(MakeSample(2.0));
	recorder.RecordModal(MakeSample(3.0));
	recorder.RecordModal(MakeSample(5.0));
	recorder.EndModalPeriod(Ms(100.0));

	ModalPeriod period;
	ASSERT_TRUE(recorder.TakeModalPeriod(period));
	EXPECT_EQ(period.insideTicks, Ms(10.0));
	EXPECT_EQ(period.spanTicks, Ms(100.0));
	EXPECT_EQ(period.stages[static_cast<u32>(Stage::Frame)].frames, 3u);
	EXPECT_EQ(period.stages[static_cast<u32>(Stage::Frame)].maxTicks, Ms(5.0));
}

TEST(EditorFrameCostTest, ReEntrantFrameStillCounted) {
	Recorder recorder{TICKS_PER_SECOND};
	PassWarmup(recorder);

	recorder.RecordModal(MakeSample(4.0));
	recorder.RecordModal(MakeSample(4.0));

	EXPECT_EQ(recorder.GetUnpairedModalFrames(), 2u);
	EXPECT_EQ(Frame(recorder, Class::Modal).frames, 2u);
}

TEST(EditorFrameCostTest, PeriodNobodyTookIsCountedNotDropped) {
	Recorder recorder{TICKS_PER_SECOND};
	PassWarmup(recorder);

	recorder.BeginModalPeriod(0);
	recorder.RecordModal(MakeSample(2.0));
	recorder.EndModalPeriod(Ms(50.0));
	recorder.BeginModalPeriod(Ms(60.0));
	recorder.RecordModal(MakeSample(2.0));
	recorder.EndModalPeriod(Ms(90.0));

	EXPECT_EQ(recorder.GetLostModalPeriods(), 1u);
	ModalPeriod period;
	ASSERT_TRUE(recorder.TakeModalPeriod(period));
	EXPECT_EQ(period.index, 2u);
}

TEST(EditorFrameCostTest, EndingPeriodNobodyOpenedIsNoOp) {
	Recorder recorder{TICKS_PER_SECOND};
	PassWarmup(recorder);

	recorder.EndModalPeriod(Ms(10.0));
	ModalPeriod period;
	EXPECT_FALSE(recorder.TakeModalPeriod(period));
	EXPECT_EQ(recorder.GetLostModalPeriods(), 0u);
}

TEST(EditorFrameCostTest, SecondBeginPublishesFirstNotLosingIt) {
	Recorder recorder{TICKS_PER_SECOND};
	PassWarmup(recorder);

	recorder.BeginModalPeriod(0);
	recorder.NoteModalMove();
	recorder.RecordModal(MakeSample(2.0));
	recorder.BeginModalPeriod(Ms(40.0));
	recorder.NoteModalMove();
	recorder.NoteModalMove();
	recorder.RecordModal(MakeSample(2.0));
	recorder.EndModalPeriod(Ms(80.0));

	ModalPeriod live;
	ASSERT_TRUE(recorder.TakeModalPeriod(live));
	EXPECT_EQ(live.index, 2u);
	EXPECT_EQ(live.moves, 2u);
	EXPECT_EQ(live.frames, 1u);
	EXPECT_EQ(recorder.GetLostModalPeriods(), 1u);
}

TEST(EditorFrameCostTest, ReEntrantFrameSkipsWarmup) {
	Recorder recorder{TICKS_PER_SECOND};

	recorder.BeginModalPeriod(0);
	recorder.RecordModal(MakeSample(2.0));
	recorder.EndModalPeriod(Ms(20.0));

	ModalPeriod period;
	ASSERT_TRUE(recorder.TakeModalPeriod(period));
	EXPECT_EQ(period.frames, 1u);
	EXPECT_EQ(recorder.GetExcluded(Exclusion::Warmup), 0u);
}

TEST(EditorFrameCostTest, BackwardPeriodReportsNoSpan) {
	Recorder recorder{TICKS_PER_SECOND};
	PassWarmup(recorder);

	recorder.BeginModalPeriod(Ms(500.0));
	recorder.RecordModal(MakeSample(2.0));
	recorder.EndModalPeriod(Ms(100.0));

	ModalPeriod period;
	ASSERT_TRUE(recorder.TakeModalPeriod(period));
	EXPECT_EQ(period.spanTicks, 0u);
}

namespace {

	constexpr f64 EPOCH_SECONDS = 1.0;

	Sample MakeSampleAt(f64 startMs, f64 frameMs, bool pointerDown = false) {
		Sample sample = MakeSample(frameMs, pointerDown, pointerDown);
		sample.startTicks = Ms(startMs);
		return sample;
	}

	Counters MakeCounters(u64 measureCalls, u64 liveTextures) {
		Counters counters;
		counters.values[static_cast<u32>(Counter::MeasureCalls)] = measureCalls;
		counters.values[static_cast<u32>(Counter::LiveTextures)] = liveTextures;
		return counters;
	}

	std::vector<Epoch> DriveIdle(Recorder& recorder, f64 startMs, f64 stepMs, u32 frames,
								 const std::function<f64(u32)>& frameMsOf) {
		std::vector<Epoch> closed;
		for (u32 i = 0; i < frames; ++i) {
			recorder.Record(MakeSampleAt(startMs + stepMs * i, frameMsOf(i)));
			if (recorder.EpochElapsed()) {
				recorder.CloseEpoch(Counters{});
				Epoch epoch;
				if (recorder.TakeEpoch(epoch)) {
					closed.push_back(epoch);
				}
			}
		}
		return closed;
	}

	f64 EpochMinMs(const Recorder& recorder, const Epoch& epoch) {
		return recorder.MinMs(epoch.stages[static_cast<u32>(Stage::Frame)]);
	}

} // namespace

TEST(EditorFrameCostTest, EpochClosesOnLengthAndNotBefore) {
	Recorder recorder{TICKS_PER_SECOND, EPOCH_SECONDS};
	PassWarmup(recorder);

	for (u32 i = 0; i < 10; ++i) {
		recorder.Record(MakeSampleAt(100.0 * i, 1.0));
		EXPECT_FALSE(recorder.EpochElapsed()) << "closed at " << (100.0 * i) << " ms";
	}
	recorder.Record(MakeSampleAt(1000.0, 1.0));
	EXPECT_TRUE(recorder.EpochElapsed());

	EXPECT_TRUE(recorder.CloseEpoch(Counters{}));
	Epoch epoch;
	ASSERT_TRUE(recorder.TakeEpoch(epoch));
	EXPECT_EQ(epoch.index, 1u);
	EXPECT_EQ(epoch.frames, 11u);
	EXPECT_EQ(epoch.spanTicks, Ms(1000.0));
	EXPECT_FALSE(recorder.TakeEpoch(epoch));
}

TEST(EditorFrameCostTest, EpochExcludesWhatRunExcludes) {
	Recorder recorder{TICKS_PER_SECOND, EPOCH_SECONDS};
	PassWarmup(recorder);

	for (u32 i = 0; i < 3; ++i) {
		recorder.Record(MakeSampleAt(100.0 * i, 1.0));
	}
	Sample modal = MakeSampleAt(400.0, 250.0);
	modal.modal = true;
	recorder.Record(modal);
	Sample throttled = MakeSampleAt(500.0, 250.0);
	throttled.throttled = true;
	recorder.Record(throttled);
	recorder.Record(MakeSampleAt(1000.0, 1.0));

	ASSERT_TRUE(recorder.EpochElapsed());
	recorder.CloseEpoch(Counters{});
	Epoch epoch;
	ASSERT_TRUE(recorder.TakeEpoch(epoch));
	EXPECT_EQ(epoch.frames, 4u);
	EXPECT_DOUBLE_EQ(recorder.MaxMs(epoch.stages[static_cast<u32>(Stage::Frame)]), 1.0);
}

TEST(EditorFrameCostTest, EpochCountsFramesHandWasIn) {
	Recorder recorder{TICKS_PER_SECOND, EPOCH_SECONDS};
	PassWarmup(recorder);

	recorder.Record(MakeSampleAt(0.0, 1.0));
	recorder.Record(MakeSampleAt(200.0, 4.0, true));
	recorder.Record(MakeSampleAt(400.0, 4.0, true));
	recorder.Record(MakeSampleAt(1000.0, 1.0));

	ASSERT_TRUE(recorder.EpochElapsed());
	recorder.CloseEpoch(Counters{});
	Epoch epoch;
	ASSERT_TRUE(recorder.TakeEpoch(epoch));
	EXPECT_EQ(epoch.frames, 4u);
	EXPECT_EQ(epoch.pointerFrames, 2u);
}

TEST(EditorFrameCostTest, FirstEpochHasNoRate) {
	Recorder recorder{TICKS_PER_SECOND, EPOCH_SECONDS};
	PassWarmup(recorder);

	recorder.Record(MakeSampleAt(0.0, 1.0));
	recorder.Record(MakeSampleAt(1000.0, 1.0));
	ASSERT_TRUE(recorder.EpochElapsed());
	recorder.CloseEpoch(MakeCounters(500, 12));
	Epoch first;
	ASSERT_TRUE(recorder.TakeEpoch(first));
	EXPECT_EQ(first.counters.values[static_cast<u32>(Counter::MeasureCalls)], 500u);
	EXPECT_EQ(first.delta.values[static_cast<u32>(Counter::MeasureCalls)], 0u);

	recorder.Record(MakeSampleAt(1100.0, 1.0));
	recorder.Record(MakeSampleAt(2100.0, 1.0));
	ASSERT_TRUE(recorder.EpochElapsed());
	recorder.CloseEpoch(MakeCounters(800, 14));
	Epoch second;
	ASSERT_TRUE(recorder.TakeEpoch(second));
	EXPECT_EQ(second.delta.values[static_cast<u32>(Counter::MeasureCalls)], 300u);
	EXPECT_EQ(second.counters.values[static_cast<u32>(Counter::LiveTextures)], 14u);
}

TEST(EditorFrameCostTest, BackwardCounterReads0) {
	Recorder recorder{TICKS_PER_SECOND, EPOCH_SECONDS};
	PassWarmup(recorder);

	recorder.Record(MakeSampleAt(0.0, 1.0));
	recorder.Record(MakeSampleAt(1000.0, 1.0));
	recorder.CloseEpoch(MakeCounters(500, 12));
	Epoch epoch;
	recorder.TakeEpoch(epoch);

	recorder.Record(MakeSampleAt(1100.0, 1.0));
	recorder.Record(MakeSampleAt(2100.0, 1.0));
	recorder.CloseEpoch(MakeCounters(0, 12));
	ASSERT_TRUE(recorder.TakeEpoch(epoch));
	EXPECT_EQ(epoch.delta.values[static_cast<u32>(Counter::MeasureCalls)], 0u);
}

TEST(EditorFrameCostTest, EpochNobodyTookIsCountedNotDropped) {
	Recorder recorder{TICKS_PER_SECOND, EPOCH_SECONDS};
	PassWarmup(recorder);

	recorder.Record(MakeSampleAt(0.0, 1.0));
	recorder.Record(MakeSampleAt(1000.0, 1.0));
	recorder.CloseEpoch(Counters{});
	EXPECT_EQ(recorder.GetLostEpochs(), 0u);

	recorder.Record(MakeSampleAt(1100.0, 1.0));
	recorder.Record(MakeSampleAt(2100.0, 1.0));
	recorder.CloseEpoch(Counters{});
	EXPECT_EQ(recorder.GetLostEpochs(), 1u);
	Epoch epoch;
	ASSERT_TRUE(recorder.TakeEpoch(epoch));
	EXPECT_EQ(epoch.index, 2u);
}

TEST(EditorFrameCostTest, EpochRunEndedInsideSaysSo) {
	Recorder recorder{TICKS_PER_SECOND, EPOCH_SECONDS};
	PassWarmup(recorder);

	recorder.Record(MakeSampleAt(0.0, 1.0));
	recorder.Record(MakeSampleAt(1000.0, 1.0));
	recorder.CloseEpoch(Counters{});
	Epoch epoch;
	ASSERT_TRUE(recorder.TakeEpoch(epoch));
	EXPECT_FALSE(epoch.closedAtShutdown);

	recorder.Record(MakeSampleAt(1100.0, 1.0));
	recorder.CloseEpoch(Counters{}, true);
	ASSERT_TRUE(recorder.TakeEpoch(epoch));
	EXPECT_TRUE(epoch.closedAtShutdown);
	EXPECT_EQ(epoch.frames, 1u);
}

TEST(EditorFrameCostTest, WithoutTickRateThereAreNoEpochs) {
	Recorder recorder{0};
	PassWarmup(recorder);

	recorder.Record(MakeSampleAt(0.0, 1.0));
	recorder.Record(MakeSampleAt(10'000.0, 1.0));
	EXPECT_FALSE(recorder.EpochElapsed());
	EXPECT_FALSE(recorder.CloseEpoch(Counters{}));
	Epoch epoch;
	EXPECT_FALSE(recorder.TakeEpoch(epoch));
}

TEST(EditorFrameCostTest, EpochsPartitionRunSummaryReports) {
	Recorder recorder{TICKS_PER_SECOND, EPOCH_SECONDS};
	PassWarmup(recorder);

	constexpr u32 FRAMES = 300;
	std::vector<Epoch> closed = DriveIdle(recorder, 0.0, 100.0, FRAMES, [](u32) { return 1.0; });
	ASSERT_GE(closed.size(), 25u);

	u64 inEpochs = 0;
	for (const Epoch& epoch : closed) {
		inEpochs += epoch.frames;
	}
	const Distribution& steady = Frame(recorder, Class::Steady);
	EXPECT_EQ(steady.frames, static_cast<u64>(FRAMES));
	EXPECT_LE(inEpochs, steady.frames);
	EXPECT_GE(inEpochs, steady.frames - 20u);
}

TEST(EditorFrameCostTest, AFlatRunReportsAFlatFloor) {
	Recorder recorder{TICKS_PER_SECOND, EPOCH_SECONDS};
	PassWarmup(recorder);

	std::vector<Epoch> closed = DriveIdle(recorder, 0.0, 100.0, 300, [](u32) { return 1.2; });
	ASSERT_GE(closed.size(), 25u);

	for (const Epoch& epoch : closed) {
		EXPECT_DOUBLE_EQ(EpochMinMs(recorder, epoch), 1.2) << "epoch " << epoch.index;
	}
}

TEST(EditorFrameCostTest, RunWhoseFloorClimbsReportsItClimbing) {
	Recorder recorder{TICKS_PER_SECOND, EPOCH_SECONDS};
	PassWarmup(recorder);

	constexpr u32 FRAMES = 300;
	std::vector<Epoch> closed =
		DriveIdle(recorder, 0.0, 100.0, FRAMES, [](u32 i) { return 1.12 + 0.56 * (static_cast<f64>(i) / FRAMES); });
	ASSERT_GE(closed.size(), 25u);

	f64 firstFloor = EpochMinMs(recorder, closed.front());
	f64 lastFloor = EpochMinMs(recorder, closed.back());
	EXPECT_NEAR(firstFloor, 1.12, 0.02);
	EXPECT_NEAR(lastFloor, 1.66, 0.03);
	EXPECT_GT(lastFloor, firstFloor * 1.4);
	EXPECT_NEAR(recorder.MinMs(Frame(recorder, Class::Steady)), 1.12, 0.02);
}
