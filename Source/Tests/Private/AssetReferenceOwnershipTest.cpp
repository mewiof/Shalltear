#include "TestsPCH.h"

#include "ScopedImportCostThreshold.h"

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Asset/TextAsset.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <span>

namespace Shalltear {

	namespace {

		class OwnedRefAsset : public Asset {
		public:
			SLT_REFLECT_CLASS(OwnedRefAsset)
			SLT_REFLECT_PARENT(Asset)
			SLT_REFLECT_CREATABLE()
			SLT_REFLECT_FIELD(dependency)
			SLT_REFLECT_ARRAY(dependencies)
			SLT_END_REFLECT()

			Handle<TextAsset> dependency;
			Vector<Handle<TextAsset>> dependencies{GetMemoryResource(MemTag::Asset)};

			static bool _failImport;

			StringHash GetAssetTypeId() const override { return OwnedRefAsset::GetReflectType()->name; }

			bool Import(const Path& sourcePath) override {
				if (!TextSerializer::Deserialize(this, GetReflectType(),
												 FileSystem::ReadTextFile(sourcePath, MemTag::Asset))) {
					return false;
				}
				return !_failImport;
			}

			bool Export(const Path& destPath) const override {
				return FileSystem::WriteTextFile(destPath, TextSerializer::Serialize(this, GetReflectType()));
			}
		};

		bool OwnedRefAsset::_failImport = false;

		class OwnedRefAssetImporter : public IAssetImporter {
		public:
			StringHash GetAssetType() const override { return StringHash("OwnedRefAsset"); }

			Asset* Import(const Path& sourcePath) override {
				auto* asset = New<OwnedRefAsset>(MemTag::Asset);
				if (!asset->Import(sourcePath)) {
					Delete(asset);
					return nullptr;
				}
				return asset;
			}
		};

	} // namespace

	class AssetReferenceOwnershipFixture : public ::testing::Test {
	protected:
		OwnedRefAssetImporter _importer;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_asset_refown_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::error_code ec;
			std::filesystem::create_directories(_root / "Assets", ec);
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();
			OwnedRefAsset::GetReflectType();
			AssetImporterRegistry::Register(StringHash("OwnedRefAsset"), &_importer, {".owndata"});
			OwnedRefAsset::_failImport = false;
		}

		void TearDown() override {
			OwnedRefAsset::_failImport = false;
			AssetImporterRegistry::Unregister(&_importer);
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		Guid WriteDependency(const char* name) {
			str virtualPath = str("project://Assets/") + name;
			EXPECT_TRUE(FileSystem::WriteTextFile(Path(virtualPath), str("the dependency's content")));
			Asset* dependency = AssetDatabase::LoadAsset(Path(virtualPath));
			EXPECT_NE(dependency, nullptr);
			if (!dependency) {
				return Guid{};
			}
			Guid guid = dependency->assetId;
			AssetDatabase::UnloadAsset(dependency);
			return guid;
		}

		void WriteOwner(const char* name, const Guid& scalar, std::span<const Guid> list = {}) {
			auto* owner = New<OwnedRefAsset>(MemTag::Asset);
			Vector<Asset*> held{GetMemoryResource(MemTag::Asset)};

			if (scalar.IsValid()) {
				Asset* target = AssetDatabase::LoadAssetByGuid(scalar);
				EXPECT_NE(target, nullptr);
				owner->dependency = Handle<TextAsset>(static_cast<TextAsset*>(target));
				held.push_back(target);
			}
			for (const Guid& guid : list) {
				Asset* target = AssetDatabase::LoadAssetByGuid(guid);
				EXPECT_NE(target, nullptr);
				owner->dependencies.push_back(Handle<TextAsset>(static_cast<TextAsset*>(target)));
				held.push_back(target);
			}

			EXPECT_TRUE(owner->Export(Path(str("project://Assets/") + name)));
			Delete(owner);
			for (Asset* target : held) {
				AssetDatabase::UnloadAsset(target);
			}
		}

		std::filesystem::path _root;
	};

	TEST_F(AssetReferenceOwnershipFixture, AssetBindsUnloadedReferences) {
		Guid dependency = WriteDependency("Dependency.txt");
		ASSERT_TRUE(dependency.IsValid());
		WriteOwner("Cold.owndata", dependency);
		ASSERT_GE(AssetDatabase::CollectGarbage(), 1u);

		Asset* owner = AssetDatabase::LoadAsset(Path("project://Assets/Cold.owndata"));
		ASSERT_NE(owner, nullptr);
		EXPECT_NE(static_cast<OwnedRefAsset*>(owner)->dependency.Get(), nullptr);
	}

	TEST_F(AssetReferenceOwnershipFixture, AnAssetsBindingSurvivesTheCollector) {
		Guid dependency = WriteDependency("Dependency.txt");
		ASSERT_TRUE(dependency.IsValid());
		WriteOwner("Owner.owndata", dependency);

		Asset* owner = AssetDatabase::LoadAsset(Path("project://Assets/Owner.owndata"));
		ASSERT_NE(owner, nullptr);
		ASSERT_NE(static_cast<OwnedRefAsset*>(owner)->dependency.Get(), nullptr);

		AssetDatabase::CollectGarbage();

		EXPECT_NE(static_cast<OwnedRefAsset*>(owner)->dependency.Get(), nullptr);
	}

	TEST_F(AssetReferenceOwnershipFixture, ContainerOfHandlesIsOwnedSameWay) {
		Vector<Guid> dependencies{GetMemoryResource(MemTag::Asset)};
		for (const char* name : {"A.txt", "B.txt", "C.txt", "D.txt", "E.txt"}) {
			Guid guid = WriteDependency(name);
			ASSERT_TRUE(guid.IsValid());
			dependencies.push_back(guid);
		}
		WriteOwner("Pack.owndata", Guid{}, std::span<const Guid>(dependencies.data(), dependencies.size()));
		ASSERT_GE(AssetDatabase::CollectGarbage(), 5u);

		Asset* owner = AssetDatabase::LoadAsset(Path("project://Assets/Pack.owndata"));
		ASSERT_NE(owner, nullptr);
		auto* pack = static_cast<OwnedRefAsset*>(owner);
		ASSERT_EQ(pack->dependencies.size(), dependencies.size());

		AssetDatabase::CollectGarbage();

		for (size_t i = 0; i < pack->dependencies.size(); ++i) {
			EXPECT_NE(pack->dependencies[i].Get(), nullptr);
		}
	}

	TEST_F(AssetReferenceOwnershipFixture, ArtifactRestoreOwnsReferences) {
		ScopedImportCostThreshold everything(ScopedImportCostThreshold::CacheEverything);

		Guid dependency = WriteDependency("Dependency.txt");
		ASSERT_TRUE(dependency.IsValid());
		WriteOwner("Cached.owndata", dependency);

		Asset* first = AssetDatabase::LoadAsset(Path("project://Assets/Cached.owndata"));
		ASSERT_NE(first, nullptr);
		AssetDatabase::UnloadAsset(first);
		AssetDatabase::CollectGarbage();

		OwnedRefAsset::_failImport = true;
		Asset* warm = AssetDatabase::LoadAsset(Path("project://Assets/Cached.owndata"));
		ASSERT_NE(warm, nullptr);
		ASSERT_NE(static_cast<OwnedRefAsset*>(warm)->dependency.Get(), nullptr);

		AssetDatabase::CollectGarbage();
		EXPECT_NE(static_cast<OwnedRefAsset*>(warm)->dependency.Get(), nullptr);
	}

	TEST_F(AssetReferenceOwnershipFixture, UnloadingAnAssetReleasesWhatItBound) {
		Guid dependency = WriteDependency("Dependency.txt");
		ASSERT_TRUE(dependency.IsValid());
		WriteOwner("Owner.owndata", dependency);

		Asset* owner = AssetDatabase::LoadAsset(Path("project://Assets/Owner.owndata"));
		ASSERT_NE(owner, nullptr);
		AssetDatabase::CollectGarbage();
		ASSERT_NE(AssetDatabase::FindLoadedByGuid(dependency), nullptr);

		AssetDatabase::UnloadAsset(owner);
		AssetDatabase::CollectGarbage();

		EXPECT_EQ(AssetDatabase::FindLoadedByGuid(dependency), nullptr);
	}

	TEST_F(AssetReferenceOwnershipFixture, FailedImportReleasesBindings) {
		Guid dependency = WriteDependency("Dependency.txt");
		ASSERT_TRUE(dependency.IsValid());
		WriteOwner("Broken.owndata", dependency);
		ASSERT_GE(AssetDatabase::CollectGarbage(), 1u);

		OwnedRefAsset::_failImport = true;
		Asset* owner = AssetDatabase::LoadAsset(Path("project://Assets/Broken.owndata"));
		ASSERT_EQ(owner, nullptr);

		AssetDatabase::CollectGarbage();
		EXPECT_EQ(AssetDatabase::FindLoadedByGuid(dependency), nullptr);
	}

	TEST_F(AssetReferenceOwnershipFixture, DuplicateDependencyRetainedOnce) {
		Guid dependency = WriteDependency("Dependency.txt");
		ASSERT_TRUE(dependency.IsValid());
		const Guid repeated[] = {dependency};
		WriteOwner("Twice.owndata", dependency, std::span<const Guid>(repeated, 1));

		Asset* owner = AssetDatabase::LoadAsset(Path("project://Assets/Twice.owndata"));
		ASSERT_NE(owner, nullptr);
		ASSERT_NE(static_cast<OwnedRefAsset*>(owner)->dependency.Get(), nullptr);

		AssetDatabase::UnloadAsset(owner);
		AssetDatabase::CollectGarbage();

		EXPECT_EQ(AssetDatabase::FindLoadedByGuid(dependency), nullptr);
	}

} // namespace Shalltear
