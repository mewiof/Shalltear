#include "TestsPCH.h"
#include "Shalltear/Core/Jobs/JobSystem.h"

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
