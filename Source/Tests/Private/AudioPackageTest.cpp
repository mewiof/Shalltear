#include "TestsPCH.h"

#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Settings.h"

#include "Shalltear/Editor/PackageOps.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <filesystem>

#ifndef SHALLTEAR_SHARED
#include "AudioPackage.h"
#endif

using namespace Shalltear;

#ifndef SHALLTEAR_SHARED
SLT_DECLARE_LINKED_PACKAGE(Audio)
#endif

namespace {

	constexpr const char* PACKAGE_ID = "dev.shalltear.audio";
	constexpr const char* PACKAGE_TARGET = "Audio";
	constexpr const char* PACKAGE_DISPLAY_NAME = "Audio";
	constexpr const char* SETTINGS_CATEGORY_PATH = "Audio";

	bool Contains(std::span<const str> names, std::string_view name) {
		return std::find(names.begin(), names.end(), str(name)) != names.end();
	}

} // namespace

TEST(AudioManifestTest, CatalogueCarries4thEngineManifest) {
	Vector<PackageOps::PackageManifest> catalogue;
	Vector<str> diagnostics;
	PackageOps::LoadBuiltInCatalogue(catalogue, diagnostics);

	const PackageOps::PackageManifest* audio = PackageOps::FindPackage(catalogue, PACKAGE_ID);
	ASSERT_NE(audio, nullptr);

	EXPECT_EQ(audio->kind, PackageOps::PackageKind::Engine);
	EXPECT_EQ(audio->displayName, PACKAGE_DISPLAY_NAME);
	EXPECT_EQ(audio->version, "1.0.0");
	EXPECT_EQ(PackageOps::GetPackageBinaryName(*audio), PACKAGE_TARGET);

	EXPECT_TRUE(audio->dependencies.empty());
}

TEST(AudioManifestTest, ItNamesStageScenesAudioRunsIn) {
	Vector<PackageOps::PackageManifest> catalogue;
	Vector<str> diagnostics;
	PackageOps::LoadBuiltInCatalogue(catalogue, diagnostics);

	const PackageOps::PackageManifest* audio = PackageOps::FindPackage(catalogue, PACKAGE_ID);
	ASSERT_NE(audio, nullptr);
	ASSERT_TRUE(audio->stage.has_value());
	EXPECT_EQ(*audio->stage, FrameStage::LateUpdate);
}

TEST(AudioManifestTest, BuiltInAndOffByDefault) {
	Vector<PackageOps::PackageManifest> catalogue;
	Vector<str> diagnostics;
	PackageOps::LoadBuiltInCatalogue(catalogue, diagnostics);
	ASSERT_NE(PackageOps::FindPackage(catalogue, PACKAGE_ID), nullptr);

	Vector<PackageOps::ResolvedPackage> resolution;
	PackageOps::ResolveEnabledSet(catalogue, {}, resolution);
	EXPECT_FALSE(PackageOps::IsEnabled(resolution, PACKAGE_ID));

	Vector<str> defaultPlayerPackages;
	PackageOps::SelectGamePackages(catalogue, resolution, defaultPlayerPackages);
	EXPECT_FALSE(Contains(defaultPlayerPackages, PACKAGE_ID));

	Vector<str> stored;
	stored.emplace_back(PACKAGE_ID);
	Vector<PackageOps::ResolvedPackage> enabledResolution;
	PackageOps::ResolveEnabledSet(catalogue, stored, enabledResolution);
	EXPECT_TRUE(PackageOps::IsEnabled(enabledResolution, PACKAGE_ID));
	Vector<str> playerPackages;
	PackageOps::SelectGamePackages(catalogue, enabledResolution, playerPackages);
	EXPECT_TRUE(Contains(playerPackages, PACKAGE_ID));
}

namespace {

	class AudioPackageFixture : public ::testing::Test {
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
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_audio_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());

			FileSystem::WriteTextFile(_paths.project, str("[Audio]\ndevice = None\n"));

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			ModuleSystem::Initialize();
		}

		void TearDown() override {
#ifndef SHALLTEAR_SHARED
			AudioImpl::ShutdownPackage();
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
			SLT_CALL_LINKED_PACKAGE(Audio, &_token);
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

		static bool FindLive(void*& live, const TypeInfo*& type) {
			Vector<SettingsCategoryInfo> categories{GetMemoryResource(MemTag::Core)};
			Settings::GetCategories(categories);
			for (const SettingsCategoryInfo& info : categories) {
				if (info.path == SETTINGS_CATEGORY_PATH) {
					live = info.live;
					type = info.type;
					return live && type;
				}
			}
			return false;
		}

		static str ReadReport() {
			void* live = nullptr;
			const TypeInfo* type = nullptr;
			if (!FindLive(live, type)) {
				return "(no category)";
			}
			const FieldInfo* field = FindField(type, StringHash("obtainedPeriod"));
			if (!field || !field->addrFn) {
				return "(no report field)";
			}
			return *static_cast<const str*>(field->addrFn(live));
		}

		static bool WritePeriod(i32 milliseconds) {
			void* live = nullptr;
			const TypeInfo* type = nullptr;
			if (!FindLive(live, type)) {
				return false;
			}
			const FieldInfo* field = FindField(type, StringHash("periodMilliseconds"));
			if (!field || !field->addrFn) {
				return false;
			}
			*static_cast<i32*>(field->addrFn(live)) = milliseconds;
			Settings::NotifyChanged(SETTINGS_CATEGORY_PATH, "periodMilliseconds");
			return true;
		}
	};

} // namespace

TEST_F(AudioPackageFixture, PackageOpensNamedDevice) {
	ASSERT_TRUE(StandUp());
	const void* owner = OwnerToken();
	ASSERT_NE(owner, nullptr);

	void* live = nullptr;
	const TypeInfo* type = nullptr;
	ASSERT_TRUE(FindLive(live, type));

	const str report = ReadReport();
	EXPECT_NE(report.find("the null device"), str::npos) << report;
	EXPECT_NE(report.find("frame(s) at"), str::npos) << report;

	EXPECT_EQ(FrameStages::UnregisterOwnedBy(owner), 4u);

#ifdef SHALLTEAR_SHARED
	EXPECT_EQ(AssetImporterRegistry::UnregisterOwnedBy(owner), 6u);
#else
	EXPECT_EQ(AssetImporterRegistry::UnregisterOwnedBy(owner), 5u);
#endif
}

TEST_F(AudioPackageFixture, ChangedPeriodReopensDeviceAtPeriodAsked) {
	ASSERT_TRUE(StandUp());
	const str before = ReadReport();
	ASSERT_NE(before.find("480 frame(s) at 48000 Hz"), str::npos) << before;

	ASSERT_TRUE(WritePeriod(20));
	const str after = ReadReport();
	EXPECT_NE(after.find("960 frame(s) at 48000 Hz"), str::npos) << after;
	EXPECT_NE(after.find("the null device"), str::npos) << after;

	ASSERT_TRUE(WritePeriod(10));
	EXPECT_NE(ReadReport().find("480 frame(s) at 48000 Hz"), str::npos) << ReadReport();
}

TEST_F(AudioPackageFixture, MemTagAudioReturnsOnUnload) {
	const size_t baseline = MemoryManager::GetBytesForTag(MemTag::Audio);
	ASSERT_TRUE(StandUp());
	EXPECT_GT(MemoryManager::GetBytesForTag(MemTag::Audio), baseline);

#ifdef SHALLTEAR_SHARED
	ModuleSystem::Unload(_module);
	EXPECT_FALSE(ModuleSystem::IsLoaded(_module));
#else
	AudioImpl::ShutdownPackage();
#endif
	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Audio), baseline);
}

#ifdef SHALLTEAR_SHARED
TEST_F(AudioPackageFixture, CategoryAndStageFollowImage) {
	ASSERT_TRUE(StandUp());
	const void* firstOwner = OwnerToken();
	void* live = nullptr;
	const TypeInfo* type = nullptr;
	ASSERT_TRUE(FindLive(live, type));

	ModuleSystem::Unload(_module);
	EXPECT_FALSE(ModuleSystem::IsLoaded(_module));
	EXPECT_FALSE(FindLive(live, type));
	EXPECT_EQ(FrameStages::UnregisterOwnedBy(firstOwner), 0u);

	ASSERT_TRUE(StandUp());
	EXPECT_TRUE(FindLive(live, type));
	EXPECT_NE(ReadReport().find("the null device"), str::npos) << ReadReport();
}
#endif
