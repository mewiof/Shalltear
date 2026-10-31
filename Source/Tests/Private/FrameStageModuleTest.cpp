#include "TestsPCH.h"

#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics/FramePass.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Scene.h"

#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>

using namespace Shalltear;

namespace {

	constexpr const char* V1_DLL = "ShalltearHotReloadModuleV1.dll";
	constexpr const char* V2_DLL = "ShalltearHotReloadModuleV2.dll";

	constexpr FrameStage PROBE_STAGE = FrameStage::Update;

	using GetRunsFn = u32 (*)();
	using GetF32Fn = f32 (*)();
	using GetF64Fn = f64 (*)();
	using GetU64Fn = u64 (*)();

	FrameTime UnboundedClock() noexcept {
		FrameTime frame;
		frame.maximumDeltaTime = 1000.0f;
		frame.fixedDeltaTime = 0.1f;
		return frame;
	}

	constexpr f64 EIGHTH_STEP_SECONDS = 0.0125;
	constexpr f64 HALF_STEP_SECONDS = 0.05;

	class FrameStageModuleFixture : public ::testing::Test {
	protected:
		ModuleHandle _module;

		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			SceneSystem::Initialize();
			ModuleSystem::Initialize();

			_module = ModuleSystem::Load(Path(V1_DLL));
			ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
		}

		void TearDown() override {
			SceneSystem::Terminate();
			ModuleSystem::Terminate();
		}

		const void* ModuleBase() const { return ModuleSystem::GetNativeHandle(_module); }

		u32 ModulesOwnRuns() const {
			auto fn = reinterpret_cast<GetRunsFn>(
				::GetProcAddress(static_cast<HMODULE>(const_cast<void*>(ModuleBase())), "HotReloadModuleGetFrameStageRuns"));
			return fn ? fn() : 0u;
		}

		f32 ModulesOwnAlpha() const {
			auto fn = reinterpret_cast<GetF32Fn>(
				::GetProcAddress(static_cast<HMODULE>(const_cast<void*>(ModuleBase())), "HotReloadModuleGetFrameStageAlpha"));
			return fn ? fn() : 0.0f;
		}

		template <typename Fn>
		auto ModulesOwnClock(const char* symbol) const {
			auto fn = reinterpret_cast<Fn>(
				::GetProcAddress(static_cast<HMODULE>(const_cast<void*>(ModuleBase())), symbol));
			return fn ? fn() : decltype(fn())();
		}

		bool ModuleSetsTimeScale(f32 scale) const {
			using SetScaleFn = void (*)(float);
			auto fn = reinterpret_cast<SetScaleFn>(
				::GetProcAddress(static_cast<HMODULE>(const_cast<void*>(ModuleBase())), "HotReloadModuleSetTimeScale"));
			if (!fn) {
				return false;
			}
			fn(scale);
			return true;
		}

		static FrameStageArgs Args() {
			return FrameStageArgs{};
		}

		Time::FrameScope _clock{UnboundedClock()};
	};

} // anonymous namespace

TEST_F(FrameStageModuleFixture, ModuleStageRunsInHostFrame) {
	ASSERT_EQ(ModulesOwnRuns(), 0u);

	Time::BeginFrame(EIGHTH_STEP_SECONDS);
	FrameStages::Run(PROBE_STAGE, Args());
	EXPECT_EQ(ModulesOwnRuns(), 1u);
	EXPECT_FLOAT_EQ(ModulesOwnAlpha(), 0.125f);

	Time::BeginFrame(EIGHTH_STEP_SECONDS);
	FrameStages::Run(PROBE_STAGE, Args());
	FrameStages::Run(PROBE_STAGE, Args());
	EXPECT_EQ(ModulesOwnRuns(), 3u);
	EXPECT_FLOAT_EQ(ModulesOwnAlpha(), 0.25f);
}

TEST_F(FrameStageModuleFixture, ModuleReadsHostClock) {
	Time::BeginFrame(0.25);
	FrameStages::Run(PROBE_STAGE, Args());
	ASSERT_EQ(ModulesOwnRuns(), 1u);

	EXPECT_FLOAT_EQ(ModulesOwnClock<GetF32Fn>("HotReloadModuleGetClockDeltaTime"), Time::GetDeltaTime());
	EXPECT_FLOAT_EQ(ModulesOwnClock<GetF32Fn>("HotReloadModuleGetClockDeltaTime"), 0.25f);
	EXPECT_FLOAT_EQ(ModulesOwnClock<GetF32Fn>("HotReloadModuleGetClockUnscaledDeltaTime"), Time::GetUnscaledDeltaTime());
	EXPECT_DOUBLE_EQ(ModulesOwnClock<GetF64Fn>("HotReloadModuleGetClockTime"), Time::GetTime());
	EXPECT_EQ(ModulesOwnClock<GetU64Fn>("HotReloadModuleGetClockFrameCount"), Time::GetFrameCount());

	Time::BeginFrame(0.5);
	FrameStages::Run(PROBE_STAGE, Args());
	EXPECT_FLOAT_EQ(ModulesOwnClock<GetF32Fn>("HotReloadModuleGetClockDeltaTime"), 0.5f);
	EXPECT_EQ(ModulesOwnClock<GetU64Fn>("HotReloadModuleGetClockFrameCount"), Time::GetFrameCount());
}

TEST_F(FrameStageModuleFixture, ModuleSetsHostTimeScale) {
	ASSERT_TRUE(ModuleSetsTimeScale(0.5f));
	EXPECT_FLOAT_EQ(Time::GetTimeScale(), 0.5f);

	Time::BeginFrame(0.25);
	FrameStages::Run(PROBE_STAGE, Args());
	ASSERT_EQ(ModulesOwnRuns(), 1u);

	EXPECT_FLOAT_EQ(Time::GetDeltaTime(), 0.125f);
	EXPECT_FLOAT_EQ(ModulesOwnClock<GetF32Fn>("HotReloadModuleGetClockDeltaTime"), 0.125f);
	EXPECT_FLOAT_EQ(ModulesOwnClock<GetF32Fn>("HotReloadModuleGetClockUnscaledDeltaTime"), 0.25f);
}

TEST_F(FrameStageModuleFixture, ModuleRegistersInOneStage) {
	for (FrameStage stage : FRAME_STAGES) {
		if (stage != PROBE_STAGE) {
			FrameStages::Run(stage, Args());
		}
	}
	EXPECT_EQ(ModulesOwnRuns(), 0u);
}

TEST_F(FrameStageModuleFixture, UnloadingModuleTakesStageRegistrationWithIt) {
	FrameStages::Run(PROBE_STAGE, Args());
	ASSERT_EQ(ModulesOwnRuns(), 1u);

	const void* base = ModuleBase();
	ModuleSystem::Unload(_module);
	_module = {};
	ASSERT_FALSE(ModuleSystem::IsLoaded(_module));

	EXPECT_EQ(FrameStages::UnregisterOwnedBy(base), 0u);

	for (u32 i = 0; i < 8; ++i) {
		FrameStages::Run(PROBE_STAGE, Args());
	}
}

TEST_F(FrameStageModuleFixture, ModulesFramePassIsRegisteredUnderImage) {
	EXPECT_EQ(FramePasses::UnregisterOwnedBy(ModuleBase()), 1u);
}

TEST_F(FrameStageModuleFixture, UnloadingModuleTakesFramePassWithIt) {
	const void* base = ModuleBase();
	ModuleSystem::Unload(_module);
	_module = {};
	EXPECT_EQ(FramePasses::UnregisterOwnedBy(base), 0u);
}

TEST_F(FrameStageModuleFixture, SweptRegistrationLeavesStagesOtherEntriesRunning) {
	static u32 hostRuns = 0;
	hostRuns = 0;
	static const u8 hostOwner = 0;
	struct Host {
		static void Run(const FrameStageArgs&, void*) { ++hostRuns; }
	};
	ASSERT_TRUE(FrameStages::Register(PROBE_STAGE, FrameStagePosition::Normal, &Host::Run, nullptr, &hostOwner));

	FrameStages::Run(PROBE_STAGE, Args());
	EXPECT_EQ(hostRuns, 1u);
	EXPECT_EQ(ModulesOwnRuns(), 1u);

	ModuleSystem::Unload(_module);
	_module = {};

	FrameStages::Run(PROBE_STAGE, Args());
	EXPECT_EQ(hostRuns, 2u);

	EXPECT_EQ(FrameStages::UnregisterOwnedBy(&hostOwner), 1u);
}

TEST_F(FrameStageModuleFixture, ReloadReplacesRegistrationNotAddingToIt) {
	FrameStages::Run(PROBE_STAGE, Args());
	FrameStages::Run(PROBE_STAGE, Args());
	ASSERT_EQ(ModulesOwnRuns(), 2u);

	Path v2(V2_DLL);
	ASSERT_TRUE(ModuleSystem::Reload(_module, &v2));
	ASSERT_TRUE(ModuleSystem::IsLoaded(_module));

	Time::ResetFrameClock();
	Time::BeginFrame(HALF_STEP_SECONDS);
	FrameStages::Run(PROBE_STAGE, Args());
	EXPECT_EQ(ModulesOwnRuns(), 1u);
	EXPECT_FLOAT_EQ(ModulesOwnAlpha(), 0.5f);
}
