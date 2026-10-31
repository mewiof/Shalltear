#include "TestsPCH.h"
#include "Shalltear/Core/Jobs/JobSystem.h"

using namespace Shalltear;

namespace {

	class CapturingSink : public ILogSink {
	public:
		void ReceiveLog(const LogEvent& event) override {
			std::lock_guard<std::mutex> lock(_mutex);
			_events.push_back(event);
			++_receiveCount;
			_cv.notify_all();
		}

		void Flush() override {
			_flushCount.fetch_add(1, std::memory_order_relaxed);
		}

		bool WaitForEvents(size_t count, std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
			std::unique_lock<std::mutex> lock(_mutex);
			return _cv.wait_for(lock, timeout, [&] {
				return _events.size() >= count;
			});
		}

		bool WaitForMessage(const str& needle, std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) {
			std::unique_lock<std::mutex> lock(_mutex);
			return _cv.wait_for(lock, timeout, [&] {
				for (const auto& e : _events) {
					if (e.message == needle)
						return true;
				}
				return false;
			});
		}

		bool SawMessage(const str& needle) const {
			std::lock_guard<std::mutex> lock(_mutex);
			for (const auto& e : _events) {
				if (e.message == needle) {
					return true;
				}
			}
			return false;
		}

		std::vector<LogEvent> CopyEvents() const {
			std::lock_guard<std::mutex> lock(_mutex);
			return _events;
		}

		size_t EventCount() const {
			std::lock_guard<std::mutex> lock(_mutex);
			return _events.size();
		}

		int FlushCount() const {
			return _flushCount.load(std::memory_order_relaxed);
		}

		void Reset() {
			std::lock_guard<std::mutex> lock(_mutex);
			_events.clear();
			_receiveCount = 0;
			_flushCount.store(0, std::memory_order_relaxed);
		}
	private:
		mutable std::mutex _mutex;
		std::condition_variable _cv;
		std::vector<LogEvent> _events;
		size_t _receiveCount = 0;
		std::atomic<int> _flushCount{0};
	};

	class LogFixture : public ::testing::Test {
	protected:
		std::shared_ptr<CapturingSink> sink;

		void SetUp() override {
			JobSystem::Initialize();

			LogManager::Initialize({false, false});
			LogManager::SetGlobalLevel(LogLevel::Trace);

			sink = std::make_shared<CapturingSink>();
			LogManager::AddSink(sink);

			SLT_LOG_INFO("Sync", "barrier");
			ASSERT_TRUE(sink->WaitForMessage("barrier"));
			sink->Reset();
		}

		void TearDown() override {
			LogManager::RemoveAllSinks();
			LogManager::Terminate();
			sink.reset();

			JobSystem::Terminate();
		}
	};
} // namespace

TEST(LogLevelTest, SeverityIsMonotonicallyIncreasing) {
	EXPECT_LT(static_cast<int>(LogLevel::Trace), static_cast<int>(LogLevel::Debug));
	EXPECT_LT(static_cast<int>(LogLevel::Debug), static_cast<int>(LogLevel::Info));
	EXPECT_LT(static_cast<int>(LogLevel::Info), static_cast<int>(LogLevel::Warn));
	EXPECT_LT(static_cast<int>(LogLevel::Warn), static_cast<int>(LogLevel::Error));
	EXPECT_LT(static_cast<int>(LogLevel::Error), static_cast<int>(LogLevel::Fatal));
	EXPECT_LT(static_cast<int>(LogLevel::Fatal), static_cast<int>(LogLevel::None));
}

TEST_F(LogFixture, GlobalLevelRoundTripsThroughSetter) {
	LogManager::SetGlobalLevel(LogLevel::Warn);
	EXPECT_EQ(LogManager::GetGlobalLevel(), LogLevel::Warn);

	LogManager::SetGlobalLevel(LogLevel::Error);
	EXPECT_EQ(LogManager::GetGlobalLevel(), LogLevel::Error);

	LogManager::SetGlobalLevel(LogLevel::Trace);
	EXPECT_EQ(LogManager::GetGlobalLevel(), LogLevel::Trace);
}

TEST_F(LogFixture, EventReachesSinkAsynchronously) {
	SLT_LOG_INFO("TestCategory", "hello world");
	ASSERT_TRUE(sink->WaitForEvents(1));

	auto events = sink->CopyEvents();
	ASSERT_EQ(events.size(), 1u);
	EXPECT_EQ(events[0].level, LogLevel::Info);
	EXPECT_EQ(events[0].category, str("TestCategory"));
	EXPECT_EQ(events[0].message, str("hello world"));
	EXPECT_GT(events[0].line, 0u);
	EXPECT_NE(events[0].file, nullptr);
	EXPECT_NE(events[0].function, nullptr);
}

TEST_F(LogFixture, FormattingArgumentsAreApplied) {
	SLT_LOG_WARN("Fmt", "user {} had {} lives", "alice", 3);
	ASSERT_TRUE(sink->WaitForEvents(1));

	EXPECT_EQ(sink->CopyEvents()[0].message, str("user alice had 3 lives"));
}

namespace {
	struct ThreadStamp {
		int value = 0;
	};

	std::thread::id formattedOn;
} // namespace

template <>
struct std::formatter<ThreadStamp> : std::formatter<int> {
	auto format(const ThreadStamp& stamp, std::format_context& context) const {
		formattedOn = std::this_thread::get_id();
		return std::formatter<int>::format(stamp.value, context);
	}
};

TEST_F(LogFixture, ArgumentsFormattedOnCaller) {
	formattedOn = {};
	SLT_LOG_WARN("Fmt", "{} took {{{}}}", str("a temporary"), ThreadStamp{7});
	EXPECT_EQ(formattedOn, std::this_thread::get_id());

	ASSERT_TRUE(sink->WaitForEvents(1));
	EXPECT_EQ(sink->CopyEvents()[0].message, str("a temporary took {7}"));
}

TEST_F(LogFixture, MultipleEventsPreserveOrder) {
	SLT_LOG_INFO("Order", "first");
	SLT_LOG_INFO("Order", "second");
	SLT_LOG_INFO("Order", "third");

	ASSERT_TRUE(sink->WaitForEvents(3));

	auto events = sink->CopyEvents();
	ASSERT_EQ(events.size(), 3u);
	EXPECT_EQ(events[0].message, str("first"));
	EXPECT_EQ(events[1].message, str("second"));
	EXPECT_EQ(events[2].message, str("third"));
}

TEST_F(LogFixture, FlushIsCalledAfterProcessingBatch) {
	SLT_LOG_INFO("FlushTest", "trigger flush");
	ASSERT_TRUE(sink->WaitForEvents(1));

	for (int i = 0; i < 100 && sink->FlushCount() == 0; ++i)
		std::this_thread::sleep_for(std::chrono::milliseconds(10));

	EXPECT_GE(sink->FlushCount(), 1);
}

TEST_F(LogFixture, LevelFilterDropsLowerSeverityEvents) {
	LogManager::SetGlobalLevel(LogLevel::Warn);

	SLT_LOG_INFO("Filter", "dropped info");
	SLT_LOG_DEBUG("Filter", "dropped debug");
	SLT_LOG_WARN("Filter", "accepted warn");
	SLT_LOG_ERROR("Filter", "accepted error");

	ASSERT_TRUE(sink->WaitForEvents(2));

	std::this_thread::sleep_for(std::chrono::milliseconds(50));

	auto events = sink->CopyEvents();
	ASSERT_EQ(events.size(), 2u);
	EXPECT_EQ(events[0].level, LogLevel::Warn);
	EXPECT_EQ(events[1].level, LogLevel::Error);
}

TEST_F(LogFixture, NoneLevelSilencesAllLogging) {
	LogManager::SetGlobalLevel(LogLevel::None);

	SLT_LOG_FATAL("Silence", "must not arrive");
	SLT_LOG_ERROR("Silence", "must not arrive");
	SLT_LOG_WARN("Silence", "must not arrive");

	std::this_thread::sleep_for(std::chrono::milliseconds(100));
	EXPECT_EQ(sink->EventCount(), 0u);
}

TEST_F(LogFixture, MultipleSinksAllReceiveEvent) {
	auto second = std::make_shared<CapturingSink>();
	LogManager::AddSink(second);

	SLT_LOG_INFO("Fanout", "broadcast");

	ASSERT_TRUE(sink->WaitForEvents(1));
	ASSERT_TRUE(second->WaitForEvents(1));

	EXPECT_EQ(sink->CopyEvents()[0].message, str("broadcast"));
	EXPECT_EQ(second->CopyEvents()[0].message, str("broadcast"));
}

TEST_F(LogFixture, RemoveSinkStopsOnlyIt) {
	auto second = std::make_shared<CapturingSink>();
	LogManager::AddSink(second);

	EXPECT_TRUE(LogManager::RemoveSink(second));
	EXPECT_FALSE(LogManager::RemoveSink(second));

	SLT_LOG_INFO("Removal", "only the fixture sink hears this");
	ASSERT_TRUE(sink->WaitForMessage("only the fixture sink hears this"));

	EXPECT_EQ(second->EventCount(), 0u);
}

TEST_F(LogFixture, RemoveAllSinksStopsDelivery) {
	LogManager::RemoveAllSinks();

	SLT_LOG_ERROR("Gone", "nobody listening");
	std::this_thread::sleep_for(std::chrono::milliseconds(100));

	EXPECT_EQ(sink->EventCount(), 0u);
}

TEST_F(LogFixture, EachMacroProducesMatchingLevel) {
	SLT_LOG_TRACE("Levels", "t");
	SLT_LOG_DEBUG("Levels", "d");
	SLT_LOG_INFO("Levels", "i");
	SLT_LOG_WARN("Levels", "w");
	SLT_LOG_ERROR("Levels", "e");
	SLT_LOG_FATAL("Levels", "f");

	ASSERT_TRUE(sink->WaitForEvents(6));

	auto events = sink->CopyEvents();
	ASSERT_EQ(events.size(), 6u);
	EXPECT_EQ(events[0].level, LogLevel::Trace);
	EXPECT_EQ(events[1].level, LogLevel::Debug);
	EXPECT_EQ(events[2].level, LogLevel::Info);
	EXPECT_EQ(events[3].level, LogLevel::Warn);
	EXPECT_EQ(events[4].level, LogLevel::Error);
	EXPECT_EQ(events[5].level, LogLevel::Fatal);
}

TEST(LogLifecycleTest, DoubleInitializeIsSafe) {
	JobSystem::Initialize();

	LogManager::Initialize({false, false});
	LogManager::Initialize({false, false});
	LogManager::Initialize({false, false});

	LogManager::Terminate();
	JobSystem::Terminate();
	SUCCEED();
}

TEST(LogLifecycleTest, TerminateWithoutInitializeIsSafe) {
	LogManager::Terminate();
	SUCCEED();
}

TEST(LogLifecycleTest, InitializeAfterTerminateStartsFreshWorker) {
	JobSystem::Initialize();

	LogManager::Initialize({false, false});
	LogManager::Terminate();

	LogManager::Initialize({false, false});
	auto sink = std::make_shared<CapturingSink>();
	LogManager::AddSink(sink);

	SLT_LOG_INFO("Sync", "barrier");
	ASSERT_TRUE(sink->WaitForMessage("barrier"));
	sink->Reset();

	SLT_LOG_INFO("Restart", "after restart");
	EXPECT_TRUE(sink->WaitForEvents(1));

	LogManager::RemoveAllSinks();
	LogManager::Terminate();

	JobSystem::Terminate();
}

TEST(LogFileSinkTest, UnwritablePathDoesNotThrow) {
	std::filesystem::path base = std::filesystem::temp_directory_path() / "shalltear_log_sink_probe";
	std::error_code ec;
	std::filesystem::remove_all(base, ec);
	{
		std::ofstream blocker(base);
		ASSERT_TRUE(blocker.is_open());
	}

	str impossible = (base / "Logs" / "Engine.log").generic_string();
	EXPECT_NO_THROW({
		FileSink sink(impossible);
		LogEvent event{};
		event.category = "Probe";
		event.message = "into the void";
		event.file = __FILE__;
		event.function = "UnwritablePathDoesNotThrow";
		event.line = __LINE__;
		sink.ReceiveLog(event);
		sink.Flush();
	});

	std::filesystem::remove_all(base, ec);
}

TEST(LogLifecycleTest, DrainDiscardsUndeliverable) {
	JobSystem::Initialize();
	SLT_LOG_WARN("Doomed", "written by an image that is about to go away");
	SLT_LOG_WARN("Doomed", "and another");

	LogManager::DrainBeforeImageUnload();

	LogManager::Initialize({false, false});
	auto sink = std::make_shared<CapturingSink>();
	LogManager::AddSink(sink);
	SLT_LOG_INFO("Sync", "barrier");
	ASSERT_TRUE(sink->WaitForMessage("barrier"));

	EXPECT_FALSE(sink->SawMessage("written by an image that is about to go away"));
	EXPECT_FALSE(sink->SawMessage("and another"));

	LogManager::RemoveAllSinks();
	LogManager::Terminate();
	JobSystem::Terminate();
}

TEST(LogLifecycleTest, DrainWaitsForDelivery) {
	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	auto sink = std::make_shared<CapturingSink>();
	LogManager::AddSink(sink);
	SLT_LOG_INFO("Sync", "barrier");
	ASSERT_TRUE(sink->WaitForMessage("barrier"));
	sink->Reset();

	for (int i = 0; i < 32; ++i) {
		SLT_LOG_INFO("Live", "line {}", i);
	}
	LogManager::DrainBeforeImageUnload();

	EXPECT_TRUE(sink->SawMessage("line 0"));
	EXPECT_TRUE(sink->SawMessage("line 31"));

	LogManager::RemoveAllSinks();
	LogManager::Terminate();
	JobSystem::Terminate();
}

TEST(LogFileSinkTest, LaunchBannerKeepsPreviousRun) {
	std::filesystem::path base = std::filesystem::temp_directory_path() / "shalltear_log_banner_probe";
	std::error_code ec;
	std::filesystem::remove_all(base, ec);
	std::filesystem::create_directories(base, ec);
	str path = (base / "Engine.log").generic_string();

	auto runOnce = [&path](const char* message) {
		FileSink sink(path);
		LogEvent event{};
		event.category = "Probe";
		event.message = message;
		event.file = __FILE__;
		event.function = "EachLaunchOpensWithADatedBanner";
		event.line = __LINE__;
		sink.ReceiveLog(event);
		sink.Flush();
	};

	runOnce("first launch");
	runOnce("second launch");

	std::ifstream read(path);
	ASSERT_TRUE(read.is_open());
	str contents((std::istreambuf_iterator<char>(read)), std::istreambuf_iterator<char>());

	size_t first = contents.find("SESSION ");
	size_t second = contents.find("SESSION ", first + 1);
	ASSERT_NE(first, str::npos) << contents;
	ASSERT_NE(second, str::npos) << contents;

	str stamp = contents.substr(first + 8, 10);
	ASSERT_EQ(stamp.size(), 10u) << contents;
	for (size_t i = 0; i < stamp.size(); ++i) {
		bool separator = (i == 4 || i == 7);
		EXPECT_EQ(separator, stamp[i] == '-') << stamp << "'";
		if (!separator) {
			EXPECT_TRUE(stamp[i] >= '0' && stamp[i] <= '9');
		}
	}

	size_t firstMessage = contents.find("first launch");
	size_t secondMessage = contents.find("second launch");
	ASSERT_NE(firstMessage, str::npos);
	ASSERT_NE(secondMessage, str::npos);
	EXPECT_LT(firstMessage, second);
	EXPECT_LT(second, secondMessage);

	std::filesystem::remove_all(base, ec);
}
