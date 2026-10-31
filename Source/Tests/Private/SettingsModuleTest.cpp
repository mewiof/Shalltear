#include "TestsPCH.h"

#include "Shalltear/Core/Config.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Settings.h"

#include <atomic>
#include <chrono>
#include <filesystem>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef CreateDirectory
#undef GetFileSize

using namespace Shalltear;

namespace {

	constexpr const char* V1_DLL = "ShalltearHotReloadModuleV1.dll";
	constexpr const char* V2_DLL = "ShalltearHotReloadModuleV2.dll";

	constexpr const char* PROBE_PATH = "HotReloadProbe";
	constexpr const char* V1_PROFILE = "probe-v1";
	constexpr const char* V2_PROFILE = "probe-v2";

	using GetProbeThresholdFn = i32 (*)();

	class SettingsModuleFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		ModuleHandle _module;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_settings_module_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root);

			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());
		}

		void TearDown() override {
			SceneSystem::Terminate();
			ModuleSystem::Terminate();
			Settings::Terminate();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		void BringUpAndLoad(const char* documentText = nullptr) {
			if (documentText) {
				FileSystem::WriteTextFile(_paths.project, str(documentText));
			}
			Settings::Initialize(_paths);
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			_module = ModuleSystem::Load(Path(V1_DLL));
			ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
		}

		const void* ModuleBase() const { return ModuleSystem::GetNativeHandle(_module); }

		const SettingsCategoryInfo* FindProbe(Vector<SettingsCategoryInfo>& out) const {
			Settings::GetCategories(out);
			for (const SettingsCategoryInfo& info : out) {
				if (info.path == PROBE_PATH) {
					return &info;
				}
			}
			return nullptr;
		}

		i32 ModulesOwnThreshold() const {
			auto fn = reinterpret_cast<GetProbeThresholdFn>(
				::GetProcAddress(static_cast<HMODULE>(const_cast<void*>(ModuleBase())), "HotReloadModuleGetProbeThreshold"));
			return fn ? fn() : -1;
		}

		str ReadProjectDocument() const { return FileSystem::ReadTextFile(_paths.project); }
	};

} // anonymous namespace

TEST_F(SettingsModuleFixture, ModuleDeclaresCategoryIntoHostsStore) {
	BringUpAndLoad();

	Vector<SettingsCategoryInfo> categories{GetMemoryResource(MemTag::Core)};
	const SettingsCategoryInfo* probe = FindProbe(categories);
	ASSERT_NE(probe, nullptr);
	EXPECT_EQ(probe->scope, SettingScope::Project);
	EXPECT_FALSE(probe->overridable);
	ASSERT_NE(probe->type, nullptr);
	EXPECT_EQ(probe->type->fieldCount, 2u);

	bool sawSomethingElse = false;
	for (const SettingsCategoryInfo& info : categories) {
		sawSomethingElse |= info.path != PROBE_PATH;
	}
	EXPECT_EQ(categories.size(), 1u);
	EXPECT_FALSE(sawSomethingElse);
}

TEST_F(SettingsModuleFixture, ModuleAndHostShareStore) {
	BringUpAndLoad("[HotReloadProbe]\nthreshold = 41\n");

	Vector<SettingsCategoryInfo> categories{GetMemoryResource(MemTag::Core)};
	const SettingsCategoryInfo* probe = FindProbe(categories);
	ASSERT_NE(probe, nullptr);
	EXPECT_EQ(ModulesOwnThreshold(), 41);

	const FieldInfo* threshold = nullptr;
	for (u32 i = 0; i < probe->type->fieldCount; ++i) {
		if (str(probe->type->fields[i].displayName) == "threshold") {
			threshold = &probe->type->fields[i];
		}
	}
	ASSERT_NE(threshold, nullptr);
	*static_cast<i32*>(threshold->addrFn(probe->live)) = 77;
	EXPECT_EQ(ModulesOwnThreshold(), 77);
}

TEST_F(SettingsModuleFixture, ModuleDeclaredValueResolvesFromCommittedDocument) {
	BringUpAndLoad("[HotReloadProbe]\nthreshold = 12\nprofile = fromDisk\n");

	Vector<SettingsCategoryInfo> categories{GetMemoryResource(MemTag::Core)};
	const SettingsCategoryInfo* probe = FindProbe(categories);
	ASSERT_NE(probe, nullptr);

	SettingFieldState state;
	ASSERT_TRUE(Settings::GetFieldState(PROBE_PATH, "threshold", state));
	EXPECT_TRUE(state.isOverride);
	EXPECT_EQ(state.resolvedFrom, SettingScope::Project);
	EXPECT_EQ(ModulesOwnThreshold(), 12);

	ASSERT_TRUE(Settings::GetFieldState(PROBE_PATH, "profile", state));
	EXPECT_TRUE(state.isOverride);
}

TEST_F(SettingsModuleFixture, UnloadingTakesCategoryAndKeepsValues) {
	const char* document =
		"[HotReloadProbe]\n"
		"threshold = 33\n"
		"profile = mine\n";
	BringUpAndLoad(document);

	Vector<SettingsCategoryInfo> categories{GetMemoryResource(MemTag::Core)};
	ASSERT_NE(FindProbe(categories), nullptr);

	ASSERT_TRUE(ModuleSystem::Unload(_module));
	EXPECT_EQ(FindProbe(categories), nullptr);

	ASSERT_TRUE(Settings::Save());
	EXPECT_EQ(ReadProjectDocument(), document);
}

TEST_F(SettingsModuleFixture, ReloadingBringsCategoryBackWithStoredValues) {
	BringUpAndLoad("[HotReloadProbe]\nthreshold = 21\n");

	Path v1{V1_DLL};
	ASSERT_TRUE(ModuleSystem::Reload(_module, &v1));

	Vector<SettingsCategoryInfo> categories{GetMemoryResource(MemTag::Core)};
	const SettingsCategoryInfo* probe = FindProbe(categories);
	ASSERT_NE(probe, nullptr);
	EXPECT_EQ(ModulesOwnThreshold(), 21);
}

TEST_F(SettingsModuleFixture, NewSchemaReplacesCategory) {
	BringUpAndLoad();

	Vector<SettingsCategoryInfo> categories{GetMemoryResource(MemTag::Core)};
	const SettingsCategoryInfo* probe = FindProbe(categories);
	ASSERT_NE(probe, nullptr);
	ASSERT_EQ(probe->type->fieldCount, 2u);

	Path v2{V2_DLL};
	ASSERT_TRUE(ModuleSystem::Reload(_module, &v2));

	probe = FindProbe(categories);
	ASSERT_NE(probe, nullptr);
	EXPECT_EQ(probe->type->fieldCount, 3u);

	SettingFieldState state;
	ASSERT_TRUE(Settings::GetFieldState(PROBE_PATH, "verbose", state));
	EXPECT_FALSE(state.isOverride);

	Settings::Save();
	str written = ReadProjectDocument();
	EXPECT_EQ(written.find(V1_PROFILE), str::npos) << written;
	EXPECT_EQ(written.find(V2_PROFILE), str::npos) << written;
}

TEST_F(SettingsModuleFixture, ReloadCyclesLeaveNoResidue) {
	BringUpAndLoad();

	for (int cycle = 0; cycle < 3; ++cycle) {
		Path v2{V2_DLL};
		ASSERT_TRUE(ModuleSystem::Reload(_module, &v2)) << "cycle " << cycle;
		Path v1{V1_DLL};
		ASSERT_TRUE(ModuleSystem::Reload(_module, &v1)) << "cycle " << cycle;

		Vector<SettingsCategoryInfo> categories{GetMemoryResource(MemTag::Core)};
		Settings::GetCategories(categories);
		EXPECT_EQ(categories.size(), 1u) << "cycle " << cycle;
	}

	EXPECT_EQ(Settings::UnregisterOwnedBy(ModuleBase()), 1u);
}
