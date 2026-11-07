#include "TestsPCH.h"
#include "Shalltear/Core/Jobs/JobSystem.h"

#include <algorithm>
#include <array>

using namespace Shalltear;

TEST(JobSystemTest, BasicExecution) {
	JobSystem::Initialize();

	std::atomic<int> counter{0};
	JobHandle job = JobSystem::Submit([&]() {
		counter++;
	});

	JobSystem::WaitFor(job);
	EXPECT_EQ(counter.load(), 1);

	JobSystem::Terminate();
}

TEST(JobSystemTest, SingleDependency) {
	JobSystem::Initialize();

	std::atomic<int> value{0};

	JobHandle jobA = JobSystem::Submit([&]() {
		std::this_thread::sleep_for(std::chrono::milliseconds(10));
		value += 1;
	});

	JobHandle jobB = JobSystem::Submit([&]() {
		value.store(value.load() * 2);
	},
									   jobA);

	JobSystem::WaitFor(jobB);

	EXPECT_EQ(value.load(), 2);

	JobSystem::Terminate();
}

TEST(JobSystemTest, MultipleDependencies) {
	JobSystem::Initialize();

	std::atomic<int> pool{0};

	JobHandle dep1 = JobSystem::Submit([&]() { pool += 5; });
	JobHandle dep2 = JobSystem::Submit([&]() { pool += 5; });

	JobHandle combine = JobSystem::Submit([&]() {
		pool.store(pool.load() * 2);
	},
										  {dep1, dep2});

	JobSystem::WaitFor(combine);

	EXPECT_EQ(pool.load(), 20);

	JobSystem::Terminate();
}

TEST(JobSystemTest, WaitOnFinishedJobDoesNotDeadlock) {
	JobSystem::Initialize();

	JobHandle job = JobSystem::Submit([]() {
	});

	std::this_thread::sleep_for(std::chrono::milliseconds(20));

	JobSystem::WaitFor(job);
	SUCCEED();

	JobSystem::Terminate();
}

TEST(JobSystemTest, ParallelExecutionScalesCorrectly) {
	JobSystem::Initialize();

	constexpr int NUM_JOBS = 10;
	std::atomic<int> completed{0};
	std::vector<JobHandle> handles;

	Stopwatch sw;
	sw.Start();

	for (int i = 0; i < NUM_JOBS; ++i) {
		handles.push_back(JobSystem::Submit([&]() {
			std::this_thread::sleep_for(std::chrono::milliseconds(10));
			completed++;
		}));
	}

	JobSystem::WaitForAll(handles);
	sw.Stop();

	EXPECT_EQ(completed.load(), NUM_JOBS);

	if (JobSystem::GetWorkerCount() >= 2) {
		EXPECT_LT(sw.GetElapsedMilliseconds(), 90.0);
	}

	JobSystem::Terminate();
}

TEST(JobSystemTest, NestedJobSubmission) {
	JobSystem::Initialize();

	std::atomic<int> counter{0};

	JobHandle outer = JobSystem::Submit([&]() {
		JobHandle inner = JobSystem::Submit([&]() {
			counter++;
		});
		JobSystem::WaitFor(inner);
		counter++;
	});

	JobSystem::WaitFor(outer);
	EXPECT_EQ(counter.load(), 2);

	JobSystem::Terminate();
}

TEST(JobSystemTest, ParallelForWithoutWorkersRunsInline) {
	EXPECT_EQ(JobSystem::LaneCount(100000, 1), 1u);
	const std::thread::id caller = std::this_thread::get_id();
	std::vector<std::array<u32, 3>> calls;
	JobSystem::ParallelFor(3, 10, [&](u32 lane, u32 first, u32 end) {
		EXPECT_EQ(std::this_thread::get_id(), caller);
		calls.push_back({lane, first, end});
	});
	const std::vector<std::array<u32, 3>> expected = {{0, 0, 3}, {1, 3, 6}, {2, 6, 10}};
	EXPECT_EQ(calls, expected);
}

TEST(JobSystemTest, ParallelForCoversEachIndexOnce) {
	JobSystem::Initialize();
	const u32 threads = JobSystem::GetWorkerCount() + 1;
	EXPECT_EQ(JobSystem::LaneCount(10, 100), 1u);
	EXPECT_EQ(JobSystem::LaneCount(1000, 100), std::min(10u, threads * 4));
	EXPECT_LE(JobSystem::LaneCount(1000000, 1), JobSystem::MAX_LANES);

	constexpr u32 COUNT = 100000;
	const u32 lanes = JobSystem::LaneCount(COUNT, 1000);
	ASSERT_GE(lanes, 2u);
	std::vector<std::atomic<u32>> visits(COUNT);
	std::vector<std::array<u32, 2>> ranges(lanes, {~0u, ~0u});
	std::vector<std::thread::id> runners(lanes);
	JobSystem::ParallelFor(lanes, COUNT, [&](u32 lane, u32 first, u32 end) {
		ranges[lane] = {first, end};
		runners[lane] = std::this_thread::get_id();
		for (u32 i = first; i < end; ++i) {
			visits[i].fetch_add(1, std::memory_order_relaxed);
		}
	});

	u32 wrong = 0;
	for (const std::atomic<u32>& visit : visits) {
		wrong += visit.load() != 1;
	}
	EXPECT_EQ(wrong, 0u);
	EXPECT_EQ(ranges.front()[0], 0u);
	EXPECT_EQ(ranges.back()[1], COUNT);
	for (u32 lane = 1; lane < lanes; ++lane) {
		EXPECT_EQ(ranges[lane][0], ranges[lane - 1][1]) << lane;
	}
	EXPECT_EQ(runners[0], std::this_thread::get_id());

	JobSystem::Terminate();
}

TEST(JobSystemTest, ParallelForWaitsOnlyForOwnLanes) {
	JobSystem::Initialize();
	const u32 workers = JobSystem::GetWorkerCount();
	const size_t coreAllocations = MemoryManager::GetAllocationCountForTag(MemTag::Core);
	std::atomic<bool> release{false};
	std::atomic<u32> blocked{0};
	std::atomic<u32> unblocked{0};
	u32 blockers = 0;
	auto block = [&]() {
		++blockers;
		JobSystem::Submit([&]() {
			blocked.fetch_add(1);
			const auto start = std::chrono::steady_clock::now();
			while (!release.load() && std::chrono::steady_clock::now() - start < std::chrono::seconds(20)) {
				std::this_thread::yield();
			}
			unblocked.fetch_add(1);
		});
	};
	for (u32 i = 0; i < workers; ++i) {
		block();
	}
	for (int spin = 0; spin < 5000 && blocked.load() < workers; ++spin) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
		if (spin % 20 == 19 && blockers < workers * 4) {
			block();
		}
	}
	ASSERT_EQ(blocked.load(), workers);
	std::atomic<bool> victimRan{false};
	std::thread::id victimThread;
	JobSystem::Submit([&]() {
		victimThread = std::this_thread::get_id();
		victimRan.store(true);
	});

	const std::thread::id caller = std::this_thread::get_id();
	std::atomic<u32> onCaller{0};
	Stopwatch watch;
	watch.Start();
	JobSystem::ParallelFor(8, 8, [&](u32, u32, u32) {
		onCaller.fetch_add(std::this_thread::get_id() == caller);
	});
	watch.Stop();
	EXPECT_LT(watch.GetElapsedMilliseconds(), 1000.0);
	EXPECT_EQ(onCaller.load(), 8u);
	EXPECT_FALSE(victimRan.load());

	release.store(true);
	for (int spin = 0; spin < 5000 && (unblocked.load() < blockers || !victimRan.load()); ++spin) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	EXPECT_EQ(unblocked.load(), blockers);
	EXPECT_TRUE(victimRan.load());
	EXPECT_NE(victimThread, caller);
	for (int spin = 0; spin < 2000 && MemoryManager::GetAllocationCountForTag(MemTag::Core) != coreAllocations; ++spin) {
		std::this_thread::sleep_for(std::chrono::milliseconds(1));
	}
	EXPECT_EQ(MemoryManager::GetAllocationCountForTag(MemTag::Core), coreAllocations);

	JobSystem::Terminate();
}
