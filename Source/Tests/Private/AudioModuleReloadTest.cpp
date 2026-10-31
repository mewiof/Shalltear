#include "TestsPCH.h"

#ifdef SHALLTEAR_SHARED

#include "AudioScriptProbe.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Time.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <atomic>
#include <chrono>
#include <filesystem>
#include <memory>
#include <thread>

using namespace Shalltear;

extern "C" __declspec(dllexport) AudioScriptProbe* ShalltearAudioScriptProbe() {
	static AudioScriptProbe probe;
	return &probe;
}

namespace {

	constexpr const char* PACKAGE_DLL = "Audio.dll";
	constexpr const char* V1_DLL = "ShalltearAudioScriptModuleV1.dll";
	constexpr const char* V2_DLL = "ShalltearAudioScriptModuleV2.dll";
	constexpr const char* TONE_PATH = "project://Probe.sltone";

	const StringHash TONE_TYPE{"AudioScriptTone"};

	class AudioModuleReloadFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		ModuleHandle _package;
		ModuleHandle _module;
		void* _mixer = nullptr;
		AudioScriptProbe& _probe = *ShalltearAudioScriptProbe();

		void SetUp() override {
			std::destroy_at(&_probe);
			std::construct_at(&_probe);

			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_audio_reload_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());
			FileSystem::WriteTextFile(_paths.project, str("[Audio]\ndevice = None\n"));

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();
			SceneSystem::Initialize();
			ModuleSystem::Initialize();

			_package = ModuleSystem::Load(Path(PACKAGE_DLL), ModuleLoadPolicy::InPlace);
			ASSERT_TRUE(ModuleSystem::IsLoaded(_package));
			_module = ModuleSystem::Load(Path(V1_DLL));
			ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
		}

		void TearDown() override {
			if (_mixer) {
				if (auto destroy = Resolve<void (*)(void*)>("AudioScriptModuleDestroyMixer")) {
					destroy(_mixer);
				}
				_mixer = nullptr;
			}
			SceneSystem::Terminate();
			ModuleSystem::Terminate();
			AssetDatabase::CollectGarbage();
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			Settings::Terminate();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		template <typename Fn>
		Fn Resolve(const char* name) {
			HMODULE image = static_cast<HMODULE>(ModuleSystem::GetNativeHandle(_module));
			return image ? reinterpret_cast<Fn>(::GetProcAddress(image, name)) : nullptr;
		}

		bool ReloadTo(const char* dll) {
			Path next{dll};
			return ModuleSystem::Reload(_module, &next);
		}

		static void Pump() {
			FrameTime clockState;
			clockState.maximumDeltaTime = 1000.0f;
			Time::FrameScope clock{clockState};
			Time::BeginFrame(1.0f / 60.0f);
			FrameStageArgs args;
			FrameStages::Run(FrameStage::Update, args);
			FrameStages::Run(FrameStage::LateUpdate, args);
			FrameStages::Run(FrameStage::EndFrame, args);
		}

		template <typename Condition>
		static bool WaitFor(Condition&& condition) {
			const auto giveUp = std::chrono::steady_clock::now() + std::chrono::milliseconds(3000);
			while (!condition()) {
				if (std::chrono::steady_clock::now() > giveUp) {
					return condition();
				}
				std::this_thread::sleep_for(std::chrono::milliseconds(1));
			}
			return true;
		}

		void StartEverything() {
			ASSERT_TRUE(AssetDatabase::CreateAsset(Path(TONE_PATH), TONE_TYPE).IsValid());
			const auto makeMixer = Resolve<void* (*)()>("AudioScriptModuleMakeMixer");
			const auto play = Resolve<bool (*)(const char*)>("AudioScriptModulePlay");
			ASSERT_NE(makeMixer, nullptr);
			ASSERT_NE(play, nullptr);
			_mixer = makeMixer();
			ASSERT_NE(_mixer, nullptr);
			ASSERT_TRUE(play(TONE_PATH));
		}

		void ReloadAndExpectHandedOver(const char* dll, u32 from, u32 to) {
			AudioScriptProbe::Version& gone = _probe.versions[from];
			AudioScriptProbe::Version& next = _probe.versions[to];
			const u32 effectsBefore = next.effectsMade.load();
			const u32 generatorsBefore = next.generatorsMade.load();
			ASSERT_TRUE(ReloadTo(dll));

			EXPECT_GT(gone.effectsMade.load(), 0u);
			EXPECT_EQ(gone.effectsReleased.load(), gone.effectsMade.load());
			EXPECT_EQ(gone.generatorsReleased.load(), gone.generatorsMade.load());
			ASSERT_NE(gone.shutdownAt.load(), 0u);
			EXPECT_LT(gone.lastReleaseAt.load(), gone.shutdownAt.load());
			const u64 effectBlocks = gone.effectBlocks.load();
			const u64 generatorBlocks = gone.generatorBlocks.load();

			EXPECT_EQ(next.effectsMade.load(), effectsBefore + 1);
			const u64 stopped = gone.lastEnd.load();
			std::this_thread::sleep_for(std::chrono::milliseconds(100));
			next.firstFrom.store(~0ull);
			const u64 nextEffectBlocks = next.effectBlocks.load();
			const u64 nextGeneratorBlocks = next.generatorBlocks.load();
			Pump();
			EXPECT_EQ(next.generatorsMade.load(), generatorsBefore + 1);
			ASSERT_TRUE(WaitFor([&]() { return next.effectBlocks.load() > nextEffectBlocks + 16 && next.generatorBlocks.load() > nextGeneratorBlocks + 16; }));
			EXPECT_EQ(gone.effectBlocks.load(), effectBlocks);
			EXPECT_EQ(gone.generatorBlocks.load(), generatorBlocks);
			EXPECT_GT(next.firstFrom.load(), stopped);
		}
	};

} // namespace

TEST_F(AudioModuleReloadFixture, EffectAndGeneratorSurviveReload) {
	StartEverything();
	AudioScriptProbe::Version& v1 = _probe.versions[1];
	ASSERT_EQ(v1.effectsMade.load(), 1u);
	ASSERT_EQ(v1.generatorsMade.load(), 1u);
	ASSERT_TRUE(WaitFor([&]() { return v1.effectBlocks.load() > 16 && v1.generatorBlocks.load() > 16; }));

	ReloadAndExpectHandedOver(V2_DLL, 1, 2);
	ReloadAndExpectHandedOver(V1_DLL, 2, 1);
}

#endif
