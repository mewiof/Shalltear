#include "TestsPCH.h"

#include "TestPlatform.h"

#ifdef SHALLTEAR_SHARED

#include "TestHelperProcess.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Reflection/TypeInfo.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Scene/Behavior.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Scene/UnresolvedComponent.h"
#include "Shalltear/Core/Settings.h"

#include "Shalltear/Editor/ComponentMenu.h"
#include "Shalltear/Editor/EditorAssetOps.h"
#include "Shalltear/Editor/EditorExtensions.h"
#include "Shalltear/Editor/EditorGizmos.h"
#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorPreviews.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/PackageOps.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/PropertyDrawers.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <map>
#include <set>

using namespace Shalltear;

namespace {

	str NameOf(const TypeInfo* type) {
		return type && type->displayName ? str(type->displayName) : str("<unnamed type>");
	}

	bool IsADeliberateFloor(const TypeInfo* type) {
		return type == Component::GetReflectType() || type == Behavior::GetReflectType() ||
			   type == UnresolvedComponent::GetReflectType();
	}

	bool DerivesFromComponent(const TypeInfo* type) {
		for (const TypeInfo* walk = type; walk != nullptr; walk = walk->parent) {
			if (walk == Component::GetReflectType()) {
				return true;
			}
		}
		return false;
	}

	struct ComponentSweep {
		const void* coreImage = nullptr;
		u32 visited = 0;
		Vector<str> wearingTheCog{GetMemoryResource(MemTag::Editor)};
		Vector<str> withNoGroup{GetMemoryResource(MemTag::Editor)};
	};

	void VisitComponentType(const TypeInfo* type, void* user) {
		auto& sweep = *static_cast<ComponentSweep*>(user);
		if (!type || type->ownerModule != sweep.coreImage || !DerivesFromComponent(type) ||
			IsADeliberateFloor(type)) {
			return;
		}
		++sweep.visited;
		if (EditorIcons::ComponentIcons::For(type) == EditorIcons::Mark::Component) {
			sweep.wearingTheCog.push_back(NameOf(type));
		}
		if (ComponentMenu::CategoryFor(type).empty()) {
			sweep.withNoGroup.push_back(NameOf(type));
		}
	}

	bool DerivesFrom(const TypeInfo* type, const TypeInfo* base) {
		for (const TypeInfo* walk = type; walk != nullptr; walk = walk->parent) {
			if (walk == base) {
				return true;
			}
		}
		return false;
	}

	struct PackageSweep {
		std::set<const void*> images;
		u32 visited = 0;
		Vector<str> wearingAnother{GetMemoryResource(MemTag::Editor)};
	};

	void VisitPackageComponent(const TypeInfo* type, void* user) {
		auto& sweep = *static_cast<PackageSweep*>(user);
		if (!type || !sweep.images.contains(type->ownerModule) || !DerivesFromComponent(type) ||
			IsADeliberateFloor(type) || DerivesFrom(type, Behavior::GetReflectType()) ||
			(type->flags & TypeFlagAbstract) != 0 || !type->createFn) {
			return;
		}
		++sweep.visited;
		const std::string_view mark = EditorIcons::ComponentIcons::For(type);
		if (mark != "engine://Icons/Components/" + NameOf(type) + ".svg") {
			sweep.wearingAnother.push_back(NameOf(type) + " -> " + str(mark));
		}
	}

	class PackageBadgeCoverageFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;
		Vector<const void*> _bases{GetMemoryResource(MemTag::Editor)};
		Vector<const void*> _linked{GetMemoryResource(MemTag::Editor)};
		bool _ownsJobSystem = false;

		void SetUp() override {
			_ownsJobSystem = JobSystem::GetWorkerCount() == 0;
			if (_ownsJobSystem) {
				JobSystem::Initialize();
			}
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_package_badges_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root / "Assets");
			FileSystem::Mount("project", Path(_root.generic_string()));
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());
			FileSystem::WriteTextFile(_paths.project, str("[Audio]\ndevice = None\n"));

			TypeRegistry::InitializePrimitives();
			Settings::Initialize(_paths);
			RegisterBuiltInPanels();
			SceneSystem::Initialize();
			ModuleSystem::Initialize();
			EditorSelection::Initialize();
			PropertyDrawers::Initialize();
			EditorGizmos::Initialize();
			EditorSceneOps::Initialize();
			EditorAssetOps::Initialize();
			UndoSystem::Initialize();
			PanelManager::Initialize();
			EditorPreviews::Initialize();
			EditorPreviewStore::Initialize();
		}

		void TearDown() override {
			PanelManager::Terminate();
			for (const void* base : _bases) {
				EditorExtensions::Unload(base);
			}
			EditorPreviewStore::Terminate();
			UndoSystem::Terminate();
			EditorAssetOps::Terminate();
			EditorSceneOps::Terminate();
			EditorGizmos::Terminate();
			PropertyDrawers::Terminate();
			EditorSelection::Terminate();
			AssetDatabase::Terminate();
			SceneSystem::Terminate();
			ModuleSystem::Terminate();
			Settings::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
			if (_ownsJobSystem) {
				JobSystem::Terminate();
			}
		}

		void LoadEngineAndToolPackages() {
			Vector<PackageOps::PackageManifest> catalogue;
			Vector<str> diagnostics;
			PackageOps::LoadBuiltInCatalogue(catalogue, diagnostics);
			Vector<str> wanted{GetMemoryResource(MemTag::Editor)};
			for (const PackageOps::PackageManifest& manifest : catalogue) {
				if (manifest.kind == PackageOps::PackageKind::Engine || manifest.kind == PackageOps::PackageKind::Tool) {
					wanted.push_back(manifest.name);
				}
			}
			ASSERT_GE(wanted.size(), 5u);

			Vector<PackageOps::ResolvedPackage> resolution;
			PackageOps::ResolveEnabledSet(catalogue, wanted, resolution);
			for (const PackageOps::ResolvedPackage& resolved : resolution) {
				const PackageOps::PackageManifest* manifest = PackageOps::FindPackage(catalogue, resolved.name);
				ASSERT_NE(manifest, nullptr);
				const str binary = PackageOps::GetPackageBinaryName(*manifest) + SLT_TEST_DLL_SUFFIX;
				if (const void* linked = TestPlatform::FindLoadedLibrary(binary)) {
					_linked.push_back(linked);
					continue;
				}
				ModuleHandle module = ModuleSystem::Load(Path(binary), ModuleLoadPolicy::InPlace);
				ASSERT_TRUE(ModuleSystem::IsLoaded(module)) << binary;
				_bases.push_back(ModuleSystem::GetNativeHandle(module));
			}
			AssetDatabase::Initialize();
			for (const void* base : _bases) {
				EditorExtensions::Load(base);
			}
		}
	};

	str Join(const Vector<str>& names) {
		str out;
		for (const str& name : names) {
			if (!out.empty()) {
				out += ", ";
			}
			out += name;
		}
		return out;
	}

} // anonymous namespace

TEST(EditorIconCoverageTest, CoreComponentsHaveBadgeAndGroup) {
	EditorIcons::ComponentIcons::RegisterBuiltIns();
	ComponentMenu::RegisterBuiltIns();

	ComponentSweep sweep;
	sweep.coreImage = Component::GetReflectType()->ownerModule;
	ASSERT_NE(sweep.coreImage, nullptr);

	TypeRegistry::ForEachType(&VisitComponentType, &sweep);

	ASSERT_GT(sweep.visited, 5u);

	EXPECT_TRUE(sweep.wearingTheCog.empty());
	EXPECT_TRUE(sweep.withNoGroup.empty());
}

TEST(EditorIconCoverageTest, EveryImportableAssetTypeCarriesBadge) {
	static std::atomic<u64> counter{0};
	const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
	std::filesystem::path root = std::filesystem::temp_directory_path() /
								 ("shalltear_icon_coverage_" + std::to_string(stamp) + "_" +
								  std::to_string(counter.fetch_add(1)));
	std::filesystem::create_directories(root / "Assets");
	FileSystem::Mount("project", Path(root.generic_string()));

	TypeRegistry::InitializePrimitives();
	AssetDatabase::Initialize();
	EditorPreviews::AssetBadges::RegisterBuiltIns();
	TestHelpers::RestoreNodeShader();

	Vector<str> generic{GetMemoryResource(MemTag::Editor)};
	u32 visited = 0;
	AssetImporterRegistry::ForEach(
		[&](StringHash typeId, IAssetImporter*, std::string_view primaryExtension) {
			++visited;
			if (EditorPreviews::BadgeForAssetType(typeId).icon.glyph ==
				EditorIcons::Glyph::AssetGeneric.glyph) {
				generic.push_back(str(primaryExtension));
			}
		});

	EXPECT_GT(visited, 10u);
	str names;
	for (const str& name : generic) {
		names += " " + name;
	}
	EXPECT_TRUE(generic.empty()) << names;

	EXPECT_NE(EditorPreviews::BadgeForAssetType(StringHash("SkeletonAsset")).icon.glyph,
			  EditorIcons::Glyph::AssetGeneric.glyph);
	EXPECT_NE(EditorPreviews::BadgeForAssetType(StringHash("SkinAsset")).icon.glyph,
			  EditorIcons::Glyph::AssetGeneric.glyph);

	AssetDatabase::Terminate();
	std::error_code ec;
	std::filesystem::remove_all(root, ec);
}

TEST_F(PackageBadgeCoverageFixture, PackageTypesWearDistinctPictures) {
	ASSERT_NO_FATAL_FAILURE(LoadEngineAndToolPackages());
	EditorPreviews::AssetBadges::RegisterBuiltIns();
	TestHelpers::RestoreNodeShader();

	PackageSweep sweep;
	sweep.images.insert(Component::GetReflectType()->ownerModule);
	for (const void* base : _bases) {
		sweep.images.insert(base);
	}
	for (const void* image : _linked) {
		sweep.images.insert(image);
	}
	TypeRegistry::ForEachType(&VisitPackageComponent, &sweep);

	EXPECT_GT(sweep.visited, 30u);
	EXPECT_TRUE(sweep.wearingAnother.empty()) << Join(sweep.wearingAnother);

	std::set<u64> seen;
	std::map<str, str> glyphOwners;
	Vector<str> generic{GetMemoryResource(MemTag::Editor)};
	Vector<str> sharing{GetMemoryResource(MemTag::Editor)};
	AssetImporterRegistry::ForEach([&](StringHash typeId, IAssetImporter*, std::string_view primaryExtension) {
		if (!seen.insert(typeId.GetValue()).second) {
			return;
		}
		const EditorIcons::Icon icon = EditorPreviews::BadgeForAssetType(typeId).icon;
		if (icon.glyph == EditorIcons::Glyph::AssetGeneric.glyph) {
			generic.push_back(str(primaryExtension));
			return;
		}
		const str key = str(icon.glyph) + (icon.face == EditorIcons::Face::Regular ? "/regular" : "/solid");
		auto [it, inserted] = glyphOwners.emplace(key, str(primaryExtension));
		if (!inserted) {
			sharing.push_back(str(primaryExtension) + " with " + it->second);
		}
	});

	EXPECT_GT(seen.size(), 20u);
	EXPECT_TRUE(generic.empty()) << Join(generic);
	EXPECT_TRUE(sharing.empty()) << Join(sharing);
}

#endif
