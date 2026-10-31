#include "TestsPCH.h"

#include "Shalltear/Core/Config.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Settings.h"

#include "Shalltear/Editor/BuildProfile.h"
#include "Shalltear/Editor/SettingsOps.h"

#include "Graphics/GraphicsSettings.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

	class SettingsMigrationFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		Path _legacyEngineIni;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_settings_migration_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root);

			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());
			_legacyEngineIni = Path((_root / "Engine.ini").generic_string());
		}

		void TearDown() override {
			Settings::Terminate();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		void WriteFileText(const Path& path, const char* text) { FileSystem::WriteTextFile(path, str(text)); }

		u32 MigrateEngineIni() { return SettingsOps::MigrateLegacyEngineIni(_legacyEngineIni, _paths.project); }

		void WriteProjectIni(const char* text) { WriteFileText(Path((_root / "Project.ini").generic_string()), text); }

		Config LoadProjectIni() const {
			Config project;
			project.Load(Path((_root / "Project.ini").generic_string()));
			return project;
		}

		Config LoadUserDocument() const {
			Config document;
			document.Load(_paths.user);
			return document;
		}

		Config LoadProjectDocument() const {
			Config document;
			document.Load(_paths.project);
			return document;
		}
	};

} // namespace

TEST_F(SettingsMigrationFixture, GraphicsKeysMoveFileRetired) {
	WriteFileText(_legacyEngineIni, "[Graphics]\nVSync = true\nAdapter = 1\nValidation = true\n");

	EXPECT_EQ(MigrateEngineIni(), 3u);

	Config document = LoadProjectDocument();
	EXPECT_EQ(document.GetString("Graphics", "vsync"), "On");
	EXPECT_EQ(document.GetI64("Graphics", "adapter", -99), 1);
	EXPECT_TRUE(document.GetBool("Graphics", "validation"));
	EXPECT_FALSE(FileSystem::Exists(_legacyEngineIni));
}

TEST_F(SettingsMigrationFixture, MigratedDocumentResolvesThroughRealGraphicsCategory) {
	WriteFileText(_legacyEngineIni, "[Graphics]\nVSync = true\nAdapter = 1\nValidation = true\n");
	ASSERT_EQ(MigrateEngineIni(), 3u);

	Settings::Initialize(_paths);
	const GraphicsSettings* graphics =
		Settings::DeclareCategory<GraphicsSettings>("Graphics", SettingScope::Project, true);
	ASSERT_NE(graphics, nullptr);
	EXPECT_EQ(graphics->vsync, VSyncMode::On);
	EXPECT_EQ(graphics->adapter, 1);
	EXPECT_TRUE(graphics->validation);
}

TEST_F(SettingsMigrationFixture, FalseVSyncBecomesOff) {
	WriteFileText(_legacyEngineIni, "[Graphics]\nVSync = false\n");
	ASSERT_EQ(MigrateEngineIni(), 1u);

	Settings::Initialize(_paths);
	const GraphicsSettings* graphics =
		Settings::DeclareCategory<GraphicsSettings>("Graphics", SettingScope::Project, true);
	ASSERT_NE(graphics, nullptr);
	EXPECT_EQ(graphics->vsync, VSyncMode::Off);
}

TEST_F(SettingsMigrationFixture, StoreValueBeatsLegacy) {
	WriteFileText(_paths.project, "[Graphics]\nvsync = Off\n");
	WriteFileText(_legacyEngineIni, "[Graphics]\nVSync = true\nAdapter = 3\n");

	EXPECT_EQ(MigrateEngineIni(), 1u);

	Config document = LoadProjectDocument();
	EXPECT_EQ(document.GetString("Graphics", "vsync"), "Off");
	EXPECT_EQ(document.GetI64("Graphics", "adapter", -99), 3);
}

TEST_F(SettingsMigrationFixture, EngineIniKeepsOtherKeys) {
	WriteFileText(_legacyEngineIni, "[Graphics]\nAdapter = 2\n\n[Mine]\nkeep = yes\n");

	EXPECT_EQ(MigrateEngineIni(), 1u);

	ASSERT_TRUE(FileSystem::Exists(_legacyEngineIni));
	Config legacy;
	ASSERT_TRUE(legacy.Load(_legacyEngineIni));
	EXPECT_EQ(legacy.GetString("Mine", "keep"), "yes");
	EXPECT_FALSE(legacy.HasSection("Graphics"));
}

TEST_F(SettingsMigrationFixture, NoLegacyFileMovesNothing) {
	EXPECT_EQ(MigrateEngineIni(), 0u);
	EXPECT_FALSE(FileSystem::Exists(_paths.project));
}

TEST_F(SettingsMigrationFixture, EmptyDestinationRefusesNotGuessingOne) {
	WriteFileText(_legacyEngineIni, "[Graphics]\nAdapter = 1\n");

	EXPECT_EQ(SettingsOps::MigrateLegacyEngineIni(_legacyEngineIni, Path()), 0u);
	EXPECT_TRUE(FileSystem::Exists(_legacyEngineIni));
}

TEST_F(SettingsMigrationFixture, StaticSdkRootMovesToUser) {
	const str sdk = (_root / "SomeStaticSdk").generic_string();
	WriteProjectIni(("[Project]\nname = MyGame\n\n[Build]\nstaticSdkRoot = " + sdk + "\n").c_str());
	Settings::Initialize(_paths);

	EXPECT_TRUE(BuildProfileOps::MigrateStaticSDKRootToUserScope(_root.generic_string()));

	EXPECT_EQ(LoadUserDocument().GetString("Editor/Build", "staticSDKRoot"), sdk);
	Config project = LoadProjectIni();
	EXPECT_FALSE(project.Has("Build", "staticSdkRoot"));
	EXPECT_EQ(project.GetString("Project", "name"), "MyGame");
}

TEST_F(SettingsMigrationFixture, LocalStaticSdkRootKept) {
	const str mine = (_root / "MySdk").generic_string();
	const str theirs = (_root / "TheirSdk").generic_string();
	WriteFileText(_paths.user, ("[Editor/Build]\nstaticSDKRoot = " + mine + "\n").c_str());
	WriteProjectIni(("[Build]\nstaticSdkRoot = " + theirs + "\n").c_str());
	Settings::Initialize(_paths);

	EXPECT_TRUE(BuildProfileOps::MigrateStaticSDKRootToUserScope(_root.generic_string()));

	EXPECT_EQ(LoadUserDocument().GetString("Editor/Build", "staticSDKRoot"), mine);
	EXPECT_FALSE(LoadProjectIni().Has("Build", "staticSdkRoot"));
}

TEST_F(SettingsMigrationFixture, RootlessProjectUntouched) {
	const char* original = "[Project]\nname = MyGame\n; a comment Config does not preserve\n";
	WriteProjectIni(original);
	Settings::Initialize(_paths);
	const str before = FileSystem::ReadTextFile(Path((_root / "Project.ini").generic_string()));

	EXPECT_FALSE(BuildProfileOps::MigrateStaticSDKRootToUserScope(_root.generic_string()));

	EXPECT_EQ(FileSystem::ReadTextFile(Path((_root / "Project.ini").generic_string())), before);
	EXPECT_FALSE(FileSystem::Exists(_paths.user));
}
