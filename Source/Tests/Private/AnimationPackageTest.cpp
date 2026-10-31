#include "TestsPCH.h"

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Settings.h"

#include "Shalltear/Editor/PackageOps.h"

#include "ModelContributorsInternal.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

#ifndef SHALLTEAR_SHARED
SLT_DECLARE_LINKED_PACKAGE(Animation)
#endif

namespace {

	constexpr const char* PACKAGE_ID = "dev.shalltear.animation";
	constexpr const char* PACKAGE_TARGET = "Animation";
	constexpr const char* PACKAGE_DISPLAY_NAME = "Animation";

	constexpr const char* IMPORT_PIPELINE_ID = "dev.shalltear.importpipeline";

	const PackageOps::PackageRow* FindRow(std::span<const PackageOps::PackageRow> rows, std::string_view name) {
		for (const PackageOps::PackageRow& row : rows) {
			if (row.name == name) {
				return &row;
			}
		}
		return nullptr;
	}

	bool Contains(std::span<const str> names, std::string_view name) {
		return std::find(names.begin(), names.end(), str(name)) != names.end();
	}

	i32 ResolvedIndex(std::span<const PackageOps::ResolvedPackage> resolution, std::string_view name) {
		for (size_t index = 0; index < resolution.size(); ++index) {
			if (resolution[index].name == name) {
				return static_cast<i32>(index);
			}
		}
		return -1;
	}

	u32 CountSettingsCategories() {
		Vector<SettingsCategoryInfo> categories;
		Settings::GetCategories(categories);
		return static_cast<u32>(categories.size());
	}

} // namespace

TEST(AnimationManifestTest, CatalogueCarries2ndEngineManifest) {
	Vector<PackageOps::PackageManifest> catalogue;
	Vector<str> diagnostics;
	PackageOps::LoadBuiltInCatalogue(catalogue, diagnostics);

	const PackageOps::PackageManifest* animation = PackageOps::FindPackage(catalogue, PACKAGE_ID);
	ASSERT_NE(animation, nullptr);

	EXPECT_EQ(animation->kind, PackageOps::PackageKind::Engine);
	EXPECT_EQ(animation->displayName, PACKAGE_DISPLAY_NAME);
	EXPECT_EQ(animation->version, "1.0.0");

	EXPECT_EQ(PackageOps::GetPackageBinaryName(*animation), PACKAGE_TARGET);

	ASSERT_TRUE(animation->stage.has_value());
	EXPECT_EQ(*animation->stage, FrameStage::Update);
}

TEST(AnimationManifestTest, DeclaresFirstNonFixtureDependency) {
	Vector<PackageOps::PackageManifest> catalogue;
	Vector<str> diagnostics;
	PackageOps::LoadBuiltInCatalogue(catalogue, diagnostics);

	const PackageOps::PackageManifest* animation = PackageOps::FindPackage(catalogue, PACKAGE_ID);
	ASSERT_NE(animation, nullptr);

	ASSERT_EQ(animation->dependencies.size(), 1u);
	EXPECT_EQ(animation->dependencies[0].name, IMPORT_PIPELINE_ID);

	const PackageOps::PackageManifest* pipeline = PackageOps::FindPackage(catalogue, IMPORT_PIPELINE_ID);
	ASSERT_NE(pipeline, nullptr);
	EXPECT_EQ(pipeline->kind, PackageOps::PackageKind::Tool);
}

TEST(AnimationManifestTest, BuiltInAndOffByDefault) {
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

	Vector<str> stored;
	stored.emplace_back(PACKAGE_ID);
	Vector<PackageOps::ResolvedPackage> enabledResolution;
	PackageOps::ResolveEnabledSet(catalogue, stored, enabledResolution);
	EXPECT_TRUE(PackageOps::IsEnabled(enabledResolution, PACKAGE_ID));

	Vector<str> blockers;
	EXPECT_TRUE(PackageOps::CanDisable(enabledResolution, PACKAGE_ID, blockers));
	EXPECT_TRUE(blockers.empty());
}

TEST(AnimationManifestTest, EnablingAddsNoToolToPlayerLink) {
	Vector<PackageOps::PackageManifest> catalogue;
	Vector<str> diagnostics;
	PackageOps::LoadBuiltInCatalogue(catalogue, diagnostics);
	ASSERT_NE(PackageOps::FindPackage(catalogue, PACKAGE_ID), nullptr);
	ASSERT_NE(PackageOps::FindPackage(catalogue, IMPORT_PIPELINE_ID), nullptr);

	Vector<str> stored;
	stored.emplace_back(PACKAGE_ID);
	Vector<PackageOps::ResolvedPackage> resolution;
	PackageOps::ResolveEnabledSet(catalogue, stored, resolution);

	ASSERT_TRUE(PackageOps::IsEnabled(resolution, IMPORT_PIPELINE_ID));

	Vector<str> playerPackages;
	PackageOps::SelectGamePackages(catalogue, resolution, playerPackages);
	EXPECT_TRUE(Contains(playerPackages, PACKAGE_ID));
	EXPECT_FALSE(Contains(playerPackages, IMPORT_PIPELINE_ID));

	Vector<PackageOps::PackageRow> rows;
	PackageOps::BuildPackageRows(catalogue, resolution, rows);
	const PackageOps::PackageRow* pipelineRow = FindRow(rows, IMPORT_PIPELINE_ID);
	ASSERT_NE(pipelineRow, nullptr);
	EXPECT_FALSE(pipelineRow->canToggle);
	EXPECT_TRUE(pipelineRow->blockedBy.empty());
}

TEST(AnimationManifestTest, ItResolvesBeforeRegistryContributorWillNeed) {
	Vector<PackageOps::PackageManifest> catalogue;
	Vector<str> diagnostics;
	PackageOps::LoadBuiltInCatalogue(catalogue, diagnostics);

	Vector<str> stored;
	stored.emplace_back(PACKAGE_ID);
	Vector<PackageOps::ResolvedPackage> resolution;
	PackageOps::ResolveEnabledSet(catalogue, stored, resolution);

	const i32 animationIndex = ResolvedIndex(resolution, PACKAGE_ID);
	const i32 pipelineIndex = ResolvedIndex(resolution, IMPORT_PIPELINE_ID);
	ASSERT_GE(animationIndex, 0);
	ASSERT_GE(pipelineIndex, 0);
	EXPECT_LT(animationIndex, pipelineIndex);
}

namespace {

	class AnimationDisableCycleFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		u32 _categoriesBefore = 0;
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
					("shalltear_animation_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());

			FileSystem::WriteTextFile(_paths.project, "[SomethingTheStoreDoesNotOwn]\nkeptVerbatim=7\n");

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			ModuleSystem::Initialize();
			_categoriesBefore = CountSettingsCategories();
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
			SLT_CALL_LINKED_PACKAGE(Animation, &_token);
			return true;
#endif
		}

		const void* OwnerToken() const {
#ifdef SHALLTEAR_SHARED
			return ModuleSystem::GetNativeHandle(_module);
#else
			return &_token;
#endif
		}
	};

} // namespace

TEST_F(AnimationDisableCycleFixture, RegistersTwoStagesOneCategory) {
	ASSERT_TRUE(StandUp());

	const void* owner = OwnerToken();
	ASSERT_NE(owner, nullptr);

	EXPECT_EQ(FrameStages::UnregisterOwnedBy(owner), 2u);

	EXPECT_EQ(CountSettingsCategories(), _categoriesBefore + 1);
}

#ifdef SHALLTEAR_SHARED
TEST_F(AnimationDisableCycleFixture, EnableDisableReEnableCycleLosesNothing) {
	ASSERT_TRUE(Settings::Save());
	const str before = FileSystem::ReadTextFile(_paths.project);
	ASSERT_NE(before.find("keptVerbatim"), str::npos);

	ASSERT_TRUE(StandUp());
	ASSERT_TRUE(Settings::Save());
	EXPECT_EQ(FileSystem::ReadTextFile(_paths.project), before);

	EXPECT_EQ(CountSettingsCategories(), _categoriesBefore + 1);

	ModuleSystem::Unload(_module);
	EXPECT_FALSE(ModuleSystem::IsLoaded(_module));
	ASSERT_TRUE(Settings::Save());
	EXPECT_EQ(FileSystem::ReadTextFile(_paths.project), before);
	EXPECT_EQ(CountSettingsCategories(), _categoriesBefore);

	ASSERT_TRUE(StandUp());
	ASSERT_TRUE(Settings::Save());
	EXPECT_EQ(FileSystem::ReadTextFile(_paths.project), before);
	EXPECT_EQ(CountSettingsCategories(), _categoriesBefore + 1);
}

TEST_F(AnimationDisableCycleFixture, ModelContributionSweptOnUnload) {
	ASSERT_EQ(ModelContributors::Fingerprint(), 0u);
	ASSERT_FALSE(ModelContributors::AnyWantsTracks());

	ASSERT_TRUE(StandUp());

	const u64 enabled = ModelContributors::Fingerprint();
	EXPECT_NE(enabled, 0u);

	EXPECT_EQ(enabled, 4450245920099517689ull);
	EXPECT_TRUE(ModelContributors::AnyWantsTracks());

	ModuleSystem::Unload(_module);
	ASSERT_FALSE(ModuleSystem::IsLoaded(_module));
	EXPECT_EQ(ModelContributors::Fingerprint(), 0u);
	EXPECT_FALSE(ModelContributors::AnyWantsTracks());

	ASSERT_TRUE(StandUp());
	EXPECT_EQ(ModelContributors::Fingerprint(), enabled);
	EXPECT_TRUE(ModelContributors::AnyWantsTracks());
}

TEST_F(AnimationDisableCycleFixture, ImporterVersionFollowsToggle) {
	IAssetImporter* importer = AssetImporterRegistry::FindByExtension(".glb");
	ASSERT_NE(importer, nullptr);
	ASSERT_EQ(ModelContributors::Fingerprint(), 0u);

	const u32 disabled = importer->GetImporterVersion();
	ASSERT_TRUE(StandUp());
	const u32 enabled = importer->GetImporterVersion();
	EXPECT_NE(enabled, disabled);

	ModuleSystem::Unload(_module);
	ASSERT_FALSE(ModuleSystem::IsLoaded(_module));
	EXPECT_EQ(importer->GetImporterVersion(), disabled);
}
#endif
