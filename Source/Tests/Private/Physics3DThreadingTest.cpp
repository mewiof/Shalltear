#include "TestsPCH.h"

#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Core/Settings.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <mutex>
#include <thread>
#include <vector>

#ifndef SHALLTEAR_SHARED
#include "PhysicsBackend.h"
#include "PhysicsSettings.h"
#include "PhysicsWorld.h"
#endif

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <tlhelp32.h>

using namespace Shalltear;

#ifndef SHALLTEAR_SHARED
SLT_DECLARE_LINKED_PACKAGE(Physics3D)
#endif

namespace {

	constexpr const char* PACKAGE_TARGET = "Physics3D";

	u32 CountProcessThreads() {
		const unsigned long self = ::GetCurrentProcessId();
		void* snapshot = ::CreateToolhelp32Snapshot(TH32CS_SNAPTHREAD, 0);
		if (snapshot == INVALID_HANDLE_VALUE) {
			return 0;
		}

		THREADENTRY32 entry{};
		entry.dwSize = sizeof(entry);
		u32 count = 0;
		if (::Thread32First(snapshot, &entry)) {
			do {
				if (entry.th32OwnerProcessID == self) {
					++count;
				}
			} while (::Thread32Next(snapshot, &entry));
		}
		::CloseHandle(snapshot);
		return count;
	}

	u32 SettledThreadCount() {
		for (int attempt = 0; attempt < 60; ++attempt) {
			const u32 first = CountProcessThreads();
			std::this_thread::sleep_for(std::chrono::milliseconds(4));
			const u32 second = CountProcessThreads();
			if (first == second) {
				return second;
			}
		}
		return CountProcessThreads();
	}

} // namespace

#ifndef SHALLTEAR_SHARED

TEST(Physics3DWorkerResolveTest, ZeroLeavesOneCore) {
	EXPECT_EQ(PhysicsBackend::ResolveWorkerCount(0, 8), 7u);
	EXPECT_EQ(PhysicsBackend::ResolveWorkerCount(0, 2), 1u);
	EXPECT_EQ(PhysicsBackend::ResolveWorkerCount(0, 4), 3u);

	EXPECT_EQ(PhysicsBackend::ResolveWorkerCount(-4, 8), 7u);
}

TEST(Physics3DWorkerResolveTest, AutoStopsAtSimulationsMeasuredScalingCeiling) {
	EXPECT_EQ(PhysicsBackend::PHYSICS_AUTO_WORKER_CEILING, 8);
	EXPECT_EQ(PhysicsBackend::ResolveWorkerCount(0, 28), 8u);
	EXPECT_EQ(PhysicsBackend::ResolveWorkerCount(0, 64), 8u);

	EXPECT_EQ(PhysicsBackend::ResolveWorkerCount(24, 28), 24u);
	EXPECT_EQ(PhysicsBackend::ResolveWorkerCount(9, 28), 9u);
}

TEST(Physics3DWorkerResolveTest, UncountedMachineGetsOneWorker) {
	EXPECT_EQ(PhysicsBackend::ResolveWorkerCount(0, 0), 1u);

	EXPECT_EQ(PhysicsBackend::ResolveWorkerCount(0, 1), 1u);
}

TEST(Physics3DWorkerResolveTest, CeilingIsSimulations) {
	EXPECT_EQ(PhysicsBackend::PHYSICS_MAX_WORKERS, 32);
	EXPECT_EQ(PhysicsBackend::ResolveWorkerCount(64, 8), 32u);
	EXPECT_EQ(PhysicsBackend::ResolveWorkerCount(999, 256), 32u);

	EXPECT_EQ(PhysicsBackend::ResolveWorkerCount(1, 8), 1u);
	EXPECT_EQ(PhysicsBackend::ResolveWorkerCount(4, 8), 4u);
	EXPECT_EQ(PhysicsBackend::ResolveWorkerCount(32, 8), 32u);
}

namespace {

	class WorkerCapturingSink : public ILogSink {
	public:
		void ReceiveLog(const LogEvent& event) override {
			std::lock_guard<std::mutex> lock(_mutex);
			_messages.push_back(str(event.message));
		}
		void Flush() override {}

		u32 CountMatching(std::string_view needle) const {
			std::lock_guard<std::mutex> lock(_mutex);
			u32 count = 0;
			for (const str& message : _messages) {
				if (message.find(needle) != str::npos) {
					++count;
				}
			}
			return count;
		}
	private:
		mutable std::mutex _mutex;
		std::vector<str> _messages;
	};

	class Physics3DThreadingFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		char _token = 0;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_physics3d_threads_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();

			SLT_CALL_LINKED_PACKAGE(Physics3D, &_token);
		}

		void TearDown() override {
			GetPhysicsWorld().Shutdown();
			FrameStages::UnregisterOwnedBy(&_token);
			ModuleSystem::Terminate();
			SceneSystem::Terminate();
			Settings::Terminate();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		PhysicsSettings* MutableSettings() {
			return const_cast<PhysicsSettings*>(GetPhysicsWorld().GetSettings());
		}

		void Recreate(PhysicsSettings* settings) {
			GetPhysicsWorld().Shutdown();
			ASSERT_TRUE(GetPhysicsWorld().Initialize(*settings));
		}
	};

} // namespace

TEST_F(Physics3DThreadingFixture, ExplicitWorkerCountUsed) {
	PhysicsSettings* settings = MutableSettings();
	ASSERT_NE(settings, nullptr);

	settings->workerCount = 3;
	Recreate(settings);
	EXPECT_EQ(GetPhysicsWorld().GetWorkerCount(), 3u);

	settings->workerCount = 1;
	Recreate(settings);
	EXPECT_EQ(GetPhysicsWorld().GetWorkerCount(), 1u);
}

TEST_F(Physics3DThreadingFixture, DefaultAsksMachineNotTakingZeroLiterally) {
	PhysicsSettings* settings = MutableSettings();
	ASSERT_NE(settings, nullptr);

	settings->workerCount = 0;
	Recreate(settings);

	const u32 hardware = std::thread::hardware_concurrency();
	EXPECT_EQ(GetPhysicsWorld().GetWorkerCount(), PhysicsBackend::ResolveWorkerCount(0, hardware));
	EXPECT_GE(GetPhysicsWorld().GetWorkerCount(), 1u);
}

TEST_F(Physics3DThreadingFixture, WorkersMatchThreads) {
	PhysicsSettings* settings = MutableSettings();
	ASSERT_NE(settings, nullptr);

	settings->workerCount = 1;
	Recreate(settings);
	const u32 serial = SettledThreadCount();

	settings->workerCount = 4;
	Recreate(settings);
	const u32 threaded = SettledThreadCount();
	EXPECT_EQ(threaded, serial + 3);

	GetPhysicsWorld().Shutdown();
	EXPECT_EQ(SettledThreadCount(), serial);
}

TEST_F(Physics3DThreadingFixture, ShutdownIsIdempotent) {
	PhysicsSettings* settings = MutableSettings();
	ASSERT_NE(settings, nullptr);

	settings->workerCount = 4;
	Recreate(settings);
	ASSERT_EQ(GetPhysicsWorld().GetWorkerCount(), 4u);

	const u32 before = SettledThreadCount();
	GetPhysicsWorld().Shutdown();
	const u32 after = SettledThreadCount();
	EXPECT_LT(after, before);

	GetPhysicsWorld().Shutdown();
	GetPhysicsWorld().Shutdown();
	EXPECT_EQ(SettledThreadCount(), after);
}

TEST_F(Physics3DThreadingFixture, LiveCountEditReported) {
	PhysicsSettings* settings = MutableSettings();
	ASSERT_NE(settings, nullptr);
	settings->workerCount = 1;
	Recreate(settings);

	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	LogManager::SetGlobalLevel(LogLevel::Trace);
	auto sink = std::make_shared<WorkerCapturingSink>();
	LogManager::AddSink(sink);

	settings->workerCount = 8;
	GetPhysicsWorld().Step(1.0f / 60.0f);
	const u32 workerAfterEdit = GetPhysicsWorld().GetWorkerCount();

	GetPhysicsWorld().Step(1.0f / 60.0f);
	GetPhysicsWorld().Step(1.0f / 60.0f);

	settings->workerCount = 1;
	GetPhysicsWorld().Step(1.0f / 60.0f);

	LogManager::Terminate();

	ASSERT_GT(sink->CountMatching(""), 0u);
	EXPECT_EQ(workerAfterEdit, 1u);
	EXPECT_EQ(sink->CountMatching("workerCount"), 1u);

	sink.reset();
	JobSystem::Terminate();
}

#endif

#ifdef SHALLTEAR_SHARED

namespace {

	class Physics3DShutdownHookFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		ModuleHandle _module;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_physics3d_hook_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
		}

		void TearDown() override {
			ModuleSystem::Terminate();
			SceneSystem::Terminate();
			Settings::Terminate();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		ShalltearModuleAPI* LoadAndGetAPI() {
			_module = ModuleSystem::Load(Path(str(PACKAGE_TARGET) + ".dll"));
			if (!ModuleSystem::IsLoaded(_module)) {
				return nullptr;
			}
			void* native = ModuleSystem::GetNativeHandle(_module);
			if (!native) {
				return nullptr;
			}
			auto getAPI = reinterpret_cast<ShalltearModuleGetAPIFn>(
				::GetProcAddress(static_cast<HMODULE>(native), SLT_MODULE_GET_API_SYMBOL));
			if (!getAPI) {
				return nullptr;
			}
			return getAPI(SLT_ENGINE_CONTEXT_VERSION);
		}
	};

} // namespace

TEST_F(Physics3DShutdownHookFixture, ShutdownJoinsBeforeDetach) {
	const u32 beforeLoad = SettledThreadCount();

	ShalltearModuleAPI* api = LoadAndGetAPI();
	ASSERT_NE(api, nullptr);
	ASSERT_NE(api->shutdown, nullptr);

	const u32 afterLoad = SettledThreadCount();
	if (afterLoad <= beforeLoad) {
		GTEST_SKIP();
	}

	api->shutdown();
	EXPECT_EQ(SettledThreadCount(), beforeLoad);

	api->shutdown();
	EXPECT_EQ(SettledThreadCount(), beforeLoad);
}

#endif
