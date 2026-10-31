#include "TestsPCH.h"

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Settings.h"

#include "Shalltear/Editor/PackageOps.h"

#include <algorithm>
#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

#ifndef SHALLTEAR_SHARED
SLT_DECLARE_LINKED_PACKAGE(Physics3D)
#endif

namespace {

	constexpr const char* PACKAGE_ID = "dev.shalltear.physics3d";
	constexpr const char* PACKAGE_TARGET = "Physics3D";
	constexpr const char* PACKAGE_DISPLAY_NAME = "Physics 3D";
	constexpr const char* SETTINGS_CATEGORY_PATH = "Physics";

	const PackageOps::PackageRow* FindRow(std::span<const PackageOps::PackageRow> rows, std::string_view name) {
		for (const PackageOps::PackageRow& row : rows) {
			if (row.name == name) {
				return &row;
			}
		}
		return nullptr;
	}

	bool FindCategory(std::string_view path, SettingsCategoryInfo& out) {
		Vector<SettingsCategoryInfo> categories;
		Settings::GetCategories(categories);
		for (const SettingsCategoryInfo& category : categories) {
			if (category.path == path) {
				out = category;
				return true;
			}
		}
		return false;
	}

	bool HasCategory(std::string_view path) {
		SettingsCategoryInfo ignored;
		return FindCategory(path, ignored);
	}

	const FieldInfo* FindField(const TypeInfo* type, std::string_view fieldName) {
		if (!type) {
			return nullptr;
		}
		StringHash wanted(fieldName);
		for (u32 index = 0; index < type->fieldCount; ++index) {
			if (type->fields[index].name == wanted) {
				return &type->fields[index];
			}
		}
		return nullptr;
	}

	template <typename T>
	bool ReadField(const SettingsCategoryInfo& category, std::string_view fieldName, T& out) {
		const FieldInfo* field = FindField(category.type, fieldName);
		if (!field || !category.live || !field->addrFn) {
			return false;
		}
		out = *static_cast<const T*>(field->addrFn(category.live));
		return true;
	}

	template <typename T>
	bool WriteField(const SettingsCategoryInfo& category, std::string_view fieldName, const T& value) {
		const FieldInfo* field = FindField(category.type, fieldName);
		if (!field || !category.live || !field->addrFn) {
			return false;
		}
		*static_cast<T*>(field->addrFn(category.live)) = value;
		return true;
	}

} // namespace

TEST(Physics3DManifestTest, CatalogueCarries1stEngineManifest) {
	Vector<PackageOps::PackageManifest> catalogue;
	Vector<str> diagnostics;
	PackageOps::LoadBuiltInCatalogue(catalogue, diagnostics);

	const PackageOps::PackageManifest* physics = PackageOps::FindPackage(catalogue, PACKAGE_ID);
	ASSERT_NE(physics, nullptr);

	EXPECT_EQ(physics->kind, PackageOps::PackageKind::Engine);
	EXPECT_EQ(physics->displayName, PACKAGE_DISPLAY_NAME);
	EXPECT_EQ(physics->version, "1.0.0");

	ASSERT_TRUE(physics->stage.has_value());
	EXPECT_EQ(*physics->stage, FrameStage::FixedUpdate);

	EXPECT_EQ(PackageOps::GetPackageBinaryName(*physics), PACKAGE_TARGET);
}

TEST(Physics3DManifestTest, NothingInTheManifestNamesTheBackend) {
	Vector<PackageOps::PackageManifest> catalogue;
	Vector<str> diagnostics;
	PackageOps::LoadBuiltInCatalogue(catalogue, diagnostics);

	const PackageOps::PackageManifest* physics = PackageOps::FindPackage(catalogue, PACKAGE_ID);
	ASSERT_NE(physics, nullptr);

	str document = FileSystem::ReadTextFile(Path(physics->directory.ToString() + "/package.json"));
	ASSERT_FALSE(document.empty());

	str lowered = document;
	for (char& character : lowered) {
		character = static_cast<char>(std::tolower(static_cast<unsigned char>(character)));
	}
	EXPECT_EQ(lowered.find("box3d"), str::npos);
}

TEST(Physics3DManifestTest, BuiltInAndOffByDefault) {
	Vector<PackageOps::PackageManifest> catalogue;
	Vector<str> diagnostics;
	PackageOps::LoadBuiltInCatalogue(catalogue, diagnostics);
	ASSERT_NE(PackageOps::FindPackage(catalogue, PACKAGE_ID), nullptr);

	Vector<PackageOps::ResolvedPackage> resolution;
	PackageOps::ResolveEnabledSet(catalogue, {}, resolution);
	EXPECT_FALSE(PackageOps::IsEnabled(resolution, PACKAGE_ID));

	Vector<PackageOps::PackageRow> rows;
	PackageOps::BuildPackageRows(catalogue, resolution, rows);
	const PackageOps::PackageRow* row = FindRow(rows, PACKAGE_ID);
	ASSERT_NE(row, nullptr);
	EXPECT_EQ(row->kind, PackageOps::PackageKind::Engine);
	EXPECT_EQ(row->displayName, PACKAGE_DISPLAY_NAME);
	EXPECT_FALSE(row->enabled);
	EXPECT_TRUE(row->canToggle);
	EXPECT_TRUE(row->blockedBy.empty());

	const PackageOps::PackageRow* sample = FindRow(rows, "dev.shalltear.samplepackage");
	if (sample) {
		EXPECT_LT(row - rows.data(), sample - rows.data());
	}

	Vector<str> stored;
	stored.emplace_back(PACKAGE_ID);
	Vector<PackageOps::ResolvedPackage> enabledResolution;
	PackageOps::ResolveEnabledSet(catalogue, stored, enabledResolution);
	EXPECT_TRUE(PackageOps::IsEnabled(enabledResolution, PACKAGE_ID));

	Vector<PackageOps::PackageRow> enabledRows;
	PackageOps::BuildPackageRows(catalogue, enabledResolution, enabledRows);
	const PackageOps::PackageRow* enabledRow = FindRow(enabledRows, PACKAGE_ID);
	ASSERT_NE(enabledRow, nullptr);
	EXPECT_TRUE(enabledRow->enabled);
	EXPECT_TRUE(enabledRow->requested);
	EXPECT_TRUE(enabledRow->canToggle);

	Vector<str> playerPackages;
	PackageOps::SelectGamePackages(catalogue, resolution, playerPackages);
	EXPECT_EQ(std::find(playerPackages.begin(), playerPackages.end(), str(PACKAGE_ID)), playerPackages.end());

	PackageOps::SelectGamePackages(catalogue, enabledResolution, playerPackages);
	EXPECT_NE(std::find(playerPackages.begin(), playerPackages.end(), str(PACKAGE_ID)), playerPackages.end());
}

namespace {

	class Physics3DPackageFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
#ifdef SHALLTEAR_SHARED
		ModuleHandle _module;
#else
		char _token = 0;
#endif

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_physics3d_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			ModuleSystem::Initialize();
		}

		void TearDown() override {
#ifndef SHALLTEAR_SHARED
			FrameStages::UnregisterOwnedBy(&_token);
#endif
			ModuleSystem::Terminate();
			Settings::Terminate();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		bool StandUp() {
#ifdef SHALLTEAR_SHARED
			_module = ModuleSystem::Load(Path(str(PACKAGE_TARGET) + ".dll"));
			return ModuleSystem::IsLoaded(_module);
#else
			SLT_CALL_LINKED_PACKAGE(Physics3D, &_token);
			return true;
#endif
		}
	};

} // namespace

TEST_F(Physics3DPackageFixture, RegisteringDeclaresThePhysicsSettingsCategory) {
	EXPECT_FALSE(HasCategory(SETTINGS_CATEGORY_PATH));

	ASSERT_TRUE(StandUp());

	SettingsCategoryInfo physics;
	ASSERT_TRUE(FindCategory(SETTINGS_CATEGORY_PATH, physics));
	ASSERT_NE(physics.live, nullptr);
	ASSERT_NE(physics.type, nullptr);
	EXPECT_GT(physics.type->fieldCount, 0u);

	EXPECT_EQ(physics.scope, SettingScope::Project);
	EXPECT_TRUE(physics.overridable);

	f32 gravityY = 0.0f;
	ASSERT_TRUE(ReadField(physics, "gravityY", gravityY));
	EXPECT_FLOAT_EQ(gravityY, -9.81f);

	i32 workerCount = -1;
	ASSERT_TRUE(ReadField(physics, "workerCount", workerCount));
	EXPECT_EQ(workerCount, 0);
}

#ifndef SHALLTEAR_SHARED
TEST_F(Physics3DPackageFixture, ArchiveLeaves2StageRegistrationsItDeclares) {
	ASSERT_TRUE(StandUp());

	EXPECT_EQ(FrameStages::UnregisterOwnedBy(&_token), 2u);
}
#endif

#ifdef SHALLTEAR_SHARED
TEST_F(Physics3DPackageFixture, DisableSweepsPageKeepsValue) {
	ASSERT_TRUE(StandUp());

	SettingsCategoryInfo physics;
	ASSERT_TRUE(FindCategory(SETTINGS_CATEGORY_PATH, physics));
	ASSERT_TRUE(WriteField(physics, "gravityY", -3.25f));
	ASSERT_TRUE(Settings::Save());

	str withPackage = FileSystem::ReadTextFile(_paths.project);
	EXPECT_NE(withPackage.find("[Physics]"), str::npos);
	EXPECT_NE(withPackage.find("gravityY"), str::npos);

	ModuleSystem::Unload(_module);
	EXPECT_FALSE(HasCategory(SETTINGS_CATEGORY_PATH));

	ASSERT_TRUE(Settings::Save());
	EXPECT_EQ(FileSystem::ReadTextFile(_paths.project), withPackage);

	ASSERT_TRUE(StandUp());
	SettingsCategoryInfo returned;
	ASSERT_TRUE(FindCategory(SETTINGS_CATEGORY_PATH, returned));
	f32 gravityY = 0.0f;
	ASSERT_TRUE(ReadField(returned, "gravityY", gravityY));
	EXPECT_FLOAT_EQ(gravityY, -3.25f);
}
#endif
