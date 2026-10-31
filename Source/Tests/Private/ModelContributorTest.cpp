#include "TestsPCH.h"

#include "ProductLookup.h"

#include "Asset/ImportArtifact.h"
#include "Scene/SceneDocument.h"

#include "ModelContributorsInternal.h"
#include "ModelImport.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/SkeletonAsset.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Scene/Prefab.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

	std::filesystem::path RepoDir(const char* relative) {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
			std::filesystem::path candidate = probe / relative;
			if (std::filesystem::exists(candidate, ec)) {
				return candidate;
			}
			std::filesystem::path parent = probe.parent_path();
			if (parent == probe) {
				break;
			}
			probe = parent;
		}
		return {};
	}

	struct Witness {
		u32 invocations = 0;
		size_t takes = 0;
		size_t tracks = 0;
		size_t nodes = 0;
		size_t meshes = 0;
		size_t meshVertices = 0;
		str firstMeshName;
		str modelPath;
		bool addProduct = false;
		Guid productGuid;

		size_t products = 0;
		str firstProductKey;
		Guid skeletonGuid;
		str mintedKey;
	};

	void Contribute(const ModelContribution& contribution) {
		Witness& witness = *static_cast<Witness*>(contribution.user);
		const ModelParseResult& parsed = contribution.parsed;
		++witness.invocations;
		witness.takes = parsed.takes.size();
		witness.tracks = parsed.tracks.size();
		witness.nodes = parsed.nodes.size();
		witness.meshes = parsed.meshes.size();
		witness.meshVertices = parsed.meshes.empty() ? 0 : parsed.meshes[0].vertices.size();
		witness.firstMeshName = parsed.meshes.empty() ? str() : parsed.meshes[0].name;
		witness.modelPath = contribution.modelVirtualPath.ToString();
		witness.products = contribution.products.size();
		witness.firstProductKey = contribution.products.empty() ? str() : contribution.products[0].key;
		witness.skeletonGuid = contribution.FindProduct(SkeletonAsset::GetReflectType()->name);
		if (witness.addProduct) {
			MaterialAsset* draft = New<MaterialAsset>(MemTag::Asset);
			witness.mintedKey = contribution.MakeProductKey("Clips", "Contributed");
			witness.productGuid = contribution.context.AddProduct(witness.mintedKey, draft);
		}
	}

	void ContributeNothing(const ModelContribution&) {}

	ModelContributorDesc MakeDesc(const char* id, u32 version, bool wantsTracks, const void* owner,
								  ModelContributorFn fn = &ContributeNothing, void* user = nullptr) {
		ModelContributorDesc desc;
		desc.id = StringHash(id);
		desc.version = version;
		desc.wantsTracks = wantsTracks;
		desc.contribute = fn;
		desc.user = user;
		desc.owner = owner;
		return desc;
	}

	class ModelContributorFixture : public ::testing::Test {
	protected:
		char _ownerA = 0;
		char _ownerB = 0;

		void TearDown() override {
			ModelContributors::UnregisterOwnedBy(&_ownerA);
			ModelContributors::UnregisterOwnedBy(&_ownerB);
		}
	};

} // anonymous namespace

TEST_F(ModelContributorFixture, IncompleteRegistrationRefused) {
	ModelContributorDesc noId = MakeDesc("test.placeholder", 1, false, &_ownerA);
	noId.id = StringHash{};
	EXPECT_FALSE(ModelContributors::Register(noId));

	ModelContributorDesc noFunction = MakeDesc("test.nofunction", 1, false, &_ownerA);
	noFunction.contribute = nullptr;
	EXPECT_FALSE(ModelContributors::Register(noFunction));

	EXPECT_FALSE(ModelContributors::Register(MakeDesc("test.noowner", 1, false, nullptr)));

	EXPECT_EQ(ModelContributors::Fingerprint(), 0u);
}

TEST_F(ModelContributorFixture, DuplicateIdIsRefusedNotShadowingFirst) {
	ASSERT_TRUE(ModelContributors::Register(MakeDesc("test.duplicate", 1, false, &_ownerA)));
	const u64 first = ModelContributors::Fingerprint();

	EXPECT_FALSE(ModelContributors::Register(MakeDesc("test.duplicate", 2, true, &_ownerB)));
	EXPECT_EQ(ModelContributors::Fingerprint(), first);
	EXPECT_FALSE(ModelContributors::AnyWantsTracks());
}

TEST_F(ModelContributorFixture, SweepRemovesOwnersRowsAndLeavesRest) {
	ASSERT_TRUE(ModelContributors::Register(MakeDesc("test.a1", 1, false, &_ownerA)));
	ASSERT_TRUE(ModelContributors::Register(MakeDesc("test.b1", 1, true, &_ownerB)));
	ASSERT_TRUE(ModelContributors::Register(MakeDesc("test.a2", 1, false, &_ownerA)));
	const u64 all = ModelContributors::Fingerprint();

	EXPECT_EQ(ModelContributors::UnregisterOwnedBy(&_ownerA), 2u);
	EXPECT_NE(ModelContributors::Fingerprint(), all);
	EXPECT_TRUE(ModelContributors::AnyWantsTracks());

	EXPECT_EQ(ModelContributors::UnregisterOwnedBy(&_ownerA), 0u);
	EXPECT_EQ(ModelContributors::UnregisterOwnedBy(nullptr), 0u);

	EXPECT_EQ(ModelContributors::UnregisterOwnedBy(&_ownerB), 1u);
	EXPECT_EQ(ModelContributors::Fingerprint(), 0u);
}

TEST_F(ModelContributorFixture, FingerprintFoldsSetAndNotOrder) {
	ASSERT_TRUE(ModelContributors::Register(MakeDesc("test.first", 3, false, &_ownerA)));
	ASSERT_TRUE(ModelContributors::Register(MakeDesc("test.second", 7, false, &_ownerB)));
	const u64 forwards = ModelContributors::Fingerprint();

	ModelContributors::UnregisterOwnedBy(&_ownerA);
	ModelContributors::UnregisterOwnedBy(&_ownerB);
	ASSERT_TRUE(ModelContributors::Register(MakeDesc("test.second", 7, false, &_ownerB)));
	ASSERT_TRUE(ModelContributors::Register(MakeDesc("test.first", 3, false, &_ownerA)));
	EXPECT_EQ(ModelContributors::Fingerprint(), forwards);

	ModelContributors::UnregisterOwnedBy(&_ownerA);
	ASSERT_TRUE(ModelContributors::Register(MakeDesc("test.first", 4, false, &_ownerA)));
	EXPECT_NE(ModelContributors::Fingerprint(), forwards);
}

TEST_F(ModelContributorFixture, TracksOnlyWhenWanted) {
	EXPECT_FALSE(ModelContributors::AnyWantsTracks());
	ASSERT_TRUE(ModelContributors::Register(MakeDesc("test.quiet", 1, false, &_ownerA)));
	EXPECT_FALSE(ModelContributors::AnyWantsTracks());
	ASSERT_TRUE(ModelContributors::Register(MakeDesc("test.hungry", 1, true, &_ownerB)));
	EXPECT_TRUE(ModelContributors::AnyWantsTracks());
}

TEST_F(ModelContributorFixture, RegistryRefusesOverflow) {
	for (u32 i = 0; i < 8; ++i) {
		const str id = "test.slot" + std::to_string(i);
		ASSERT_TRUE(ModelContributors::Register(MakeDesc(id.c_str(), 1, false, &_ownerA))) << id;
	}
	EXPECT_FALSE(ModelContributors::Register(MakeDesc("test.ninth", 1, false, &_ownerB)));
	EXPECT_EQ(ModelContributors::UnregisterOwnedBy(&_ownerA), 8u);
}

TEST_F(ModelContributorFixture, EmptySetKeepsVersion) {
	IAssetImporter* importer = AssetImporterRegistry::FindByExtension(".glb");
	ASSERT_NE(importer, nullptr);
	EXPECT_EQ(importer->GetImporterVersion(), 4u);
}

TEST_F(ModelContributorFixture, VersionAndKeyFollowSet) {
	IAssetImporter* importer = AssetImporterRegistry::FindByExtension(".glb");
	ASSERT_NE(importer, nullptr);
	const u32 empty = importer->GetImporterVersion();

	ASSERT_TRUE(ModelContributors::Register(MakeDesc("test.versioned", 1, true, &_ownerA)));
	const u32 registered = importer->GetImporterVersion();
	EXPECT_NE(registered, empty);

	EXPECT_NE(ImportArtifactComputeKey(1, 2, empty), ImportArtifactComputeKey(1, 2, registered));

	ModelContributors::UnregisterOwnedBy(&_ownerA);
	ASSERT_TRUE(ModelContributors::Register(MakeDesc("test.versioned", 2, true, &_ownerA)));
	EXPECT_NE(importer->GetImporterVersion(), registered);

	ModelContributors::UnregisterOwnedBy(&_ownerA);
	EXPECT_EQ(importer->GetImporterVersion(), empty);
}

namespace {

	class ModelContributionFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		char _owner = 0;
		bool _mountedEngine = false;
		Witness _witness;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_contribution_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));

			std::filesystem::path engineAssets = RepoDir("EngineAssets");
			if (!engineAssets.empty()) {
				FileSystem::Mount("engine", Path(engineAssets.generic_string()));
				_mountedEngine = true;
			}
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			AssetDatabase::Terminate();
			ModelContributors::UnregisterOwnedBy(&_owner);
			if (_mountedEngine) {
				FileSystem::Unmount("engine");
			}
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		void Restart() {
			AssetDatabase::Terminate();
			AssetDatabase::Initialize();
		}

		bool StageFixture(const char* name) {
			std::filesystem::path fixtures = RepoDir("Source/Tests/Fixtures/Graphics");
			if (fixtures.empty()) {
				return false;
			}
			std::error_code ec;
			std::filesystem::copy_file(fixtures / name, _root / name, std::filesystem::copy_options::overwrite_existing, ec);
			return !ec;
		}

		bool RegisterWitness(bool wantsTracks, bool addProduct) {
			_witness = Witness{};
			_witness.addProduct = addProduct;
			return ModelContributors::Register(MakeDesc("test.witness", 1, wantsTracks, &_owner, &Contribute, &_witness));
		}

		bool ReadArtifactHeader(const Guid& guid, ImportArtifactHeader& out) const {
			Buffer bytes = FileSystem::ReadFile(ImportArtifactPath(guid), MemTag::Asset);
			ImportArtifactView view;
			if (!ImportArtifactRead(bytes.data, bytes.size, view)) {
				return false;
			}
			out = view.header;
			return true;
		}
	};

} // anonymous namespace

TEST_F(ModelContributionFixture, ContributorRunsOncePerImport) {
	ASSERT_TRUE(StageFixture("Fox.glb"));
	ASSERT_TRUE(RegisterWitness(true, false));

	Asset* model = AssetDatabase::LoadAsset(Path("project://Fox.glb"));
	ASSERT_NE(model, nullptr);

	EXPECT_EQ(_witness.invocations, 1u);
	EXPECT_EQ(_witness.modelPath, "project://Fox.glb");
	EXPECT_EQ(_witness.takes, 3u);
	EXPECT_EQ(_witness.tracks, 63u);
	EXPECT_EQ(_witness.nodes, 26u);

	ASSERT_EQ(_witness.meshes, 1u);
	EXPECT_EQ(_witness.firstMeshName, "fox1");
	EXPECT_EQ(_witness.meshVertices, 0u);

	EXPECT_EQ(_witness.products, 4u);
	EXPECT_EQ(_witness.firstProductKey, "Materials/fox_material");
	EXPECT_EQ(_witness.skeletonGuid, ProductLookup::Find(Path("project://Fox.glb"), "Skeletons/Fox"));

	AssetDatabase::UnloadAsset(model);
}

TEST_F(ModelContributionFixture, TracklessContributorGetsNone) {
	ASSERT_TRUE(StageFixture("Fox.glb"));
	ASSERT_TRUE(RegisterWitness(false, false));

	Asset* model = AssetDatabase::LoadAsset(Path("project://Fox.glb"));
	ASSERT_NE(model, nullptr);

	EXPECT_EQ(_witness.invocations, 1u);
	EXPECT_EQ(_witness.takes, 0u);
	EXPECT_EQ(_witness.tracks, 0u);
	EXPECT_EQ(_witness.nodes, 26u);

	AssetDatabase::UnloadAsset(model);
}

TEST_F(ModelContributionFixture, ContributorsProductJoinsModelsTable) {
	ASSERT_TRUE(StageFixture("Fox.glb"));
	ASSERT_TRUE(RegisterWitness(true, true));

	Asset* model = AssetDatabase::LoadAsset(Path("project://Fox.glb"));
	ASSERT_NE(model, nullptr);
	AssetDatabase::UnloadAsset(model);

	EXPECT_TRUE(_witness.productGuid.IsValid());
	const Guid recorded = ProductLookup::Find(Path("project://Fox.glb"), "Clips/Contributed");
	EXPECT_EQ(recorded, _witness.productGuid);

	EXPECT_TRUE(ProductLookup::Find(Path("project://Fox.glb"), "Meshes/fox1").IsValid());

	const Guid before = _witness.productGuid;
	Restart();
	Asset* again = AssetDatabase::LoadAsset(Path("project://Fox.glb"));
	ASSERT_NE(again, nullptr);
	AssetDatabase::UnloadAsset(again);
	EXPECT_EQ(ProductLookup::Find(Path("project://Fox.glb"), "Clips/Contributed"), before);
}

TEST_F(ModelContributionFixture, IdleContributorChangesNothing) {
	ASSERT_TRUE(StageFixture("Fox.glb"));

	Asset* plain = AssetDatabase::LoadAsset(Path("project://Fox.glb"));
	ASSERT_NE(plain, nullptr);
	const SceneDocument* fragmentBefore = static_cast<PrefabAsset*>(plain)->GetFragment();
	ASSERT_NE(fragmentBefore, nullptr);
	const size_t objectsBefore = fragmentBefore->rows.size();
	AssetDatabase::UnloadAsset(plain);
	const std::vector<str> keysBefore = ProductLookup::Keys(Path("project://Fox.glb"));
	const Guid meshBefore = ProductLookup::Find(Path("project://Fox.glb"), "Meshes/fox1");
	const str metaBefore = FileSystem::ReadTextFile(FileSystem::Resolve(AssetDatabase::GetMetaPath(Path("project://Fox.glb"))), MemTag::Asset);
	ASSERT_FALSE(metaBefore.empty());
	ASSERT_FALSE(keysBefore.empty());

	ASSERT_TRUE(RegisterWitness(true, false));
	Restart();

	Asset* contributed = AssetDatabase::LoadAsset(Path("project://Fox.glb"));
	ASSERT_NE(contributed, nullptr);
	const SceneDocument* fragmentAfter = static_cast<PrefabAsset*>(contributed)->GetFragment();
	ASSERT_NE(fragmentAfter, nullptr);
	EXPECT_EQ(fragmentAfter->rows.size(), objectsBefore);
	AssetDatabase::UnloadAsset(contributed);

	EXPECT_EQ(_witness.invocations, 1u);
	EXPECT_EQ(ProductLookup::Keys(Path("project://Fox.glb")), keysBefore);
	EXPECT_EQ(ProductLookup::Find(Path("project://Fox.glb"), "Meshes/fox1"), meshBefore);
	EXPECT_EQ(FileSystem::ReadTextFile(FileSystem::Resolve(AssetDatabase::GetMetaPath(Path("project://Fox.glb"))), MemTag::Asset), metaBefore);
}

TEST_F(ModelContributionFixture, SetToggleMovesArtifactKey) {
	ASSERT_TRUE(StageFixture("Fox.glb"));

	Asset* first = AssetDatabase::LoadAsset(Path("project://Fox.glb"));
	ASSERT_NE(first, nullptr);
	const Guid guid = first->assetId;
	AssetDatabase::UnloadAsset(first);

	ImportArtifactHeader plain{};
	ASSERT_TRUE(ReadArtifactHeader(guid, plain));
	EXPECT_EQ(plain.importerVersion, 4u);

	Restart();
	Asset* again = AssetDatabase::LoadAsset(Path("project://Fox.glb"));
	ASSERT_NE(again, nullptr);
	AssetDatabase::UnloadAsset(again);
	ImportArtifactHeader unchanged{};
	ASSERT_TRUE(ReadArtifactHeader(guid, unchanged));
	EXPECT_EQ(unchanged.key, plain.key);

	ASSERT_TRUE(RegisterWitness(true, false));
	Restart();
	Asset* contributed = AssetDatabase::LoadAsset(Path("project://Fox.glb"));
	ASSERT_NE(contributed, nullptr);
	AssetDatabase::UnloadAsset(contributed);

	ImportArtifactHeader toggled{};
	ASSERT_TRUE(ReadArtifactHeader(guid, toggled));
	EXPECT_NE(toggled.key, plain.key);
	EXPECT_NE(toggled.importerVersion, plain.importerVersion);
	EXPECT_EQ(_witness.invocations, 1u);

	ModelContributors::UnregisterOwnedBy(&_owner);
	Restart();
	Asset* disabled = AssetDatabase::LoadAsset(Path("project://Fox.glb"));
	ASSERT_NE(disabled, nullptr);
	AssetDatabase::UnloadAsset(disabled);
	ImportArtifactHeader restored{};
	ASSERT_TRUE(ReadArtifactHeader(guid, restored));
	EXPECT_EQ(restored.key, plain.key);
}
