#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Serialization/BinarySerializer.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <format>

using namespace Shalltear;

namespace {

	class AssetRefComponent : public Object {
	public:
		Handle<Object> target;
		i32 marker = 0;

		SLT_REFLECT_CLASS(AssetRefComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(target)
		SLT_REFLECT_FIELD(marker)
		SLT_END_REFLECT()
	};

	class SelfLookupImporter : public IAssetImporter {
	public:
		Guid seenGuid{};
		Path seenPath;

		StringHash GetAssetType() const override { return StringHash("SelfProbeAsset"); }
		Asset* Import(const Path& sourcePath) override {
			TextAsset* asset = New<TextAsset>(MemTag::Asset);
			asset->Import(sourcePath);
			return asset;
		}
		Asset* ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue&) override {
			seenGuid = AssetDatabase::LookupGuid(virtualPath);
			seenPath = AssetDatabase::LookupPath(seenGuid);
			return Import(sourcePath);
		}
	};

	class SettingsShapeImporter : public IAssetImporter {
	public:
		std::vector<str> seen;

		StringHash GetAssetType() const override { return StringHash("SettingsShapeAsset"); }
		Asset* Import(const Path& sourcePath) override {
			TextAsset* asset = New<TextAsset>(MemTag::Asset);
			asset->Import(sourcePath);
			return asset;
		}
		Asset* ImportWithSettings(const Path& sourcePath, const Path&, const JSONValue& settings) override {
			seen.push_back(JSONSerialize(settings, false));
			return Import(sourcePath);
		}
	};

	class CountingImporter : public IAssetImporter {
	public:
		int imports = 0;

		StringHash GetAssetType() const override { return StringHash("CountedAsset"); }
		Asset* Import(const Path& sourcePath) override {
			++imports;
			TextAsset* asset = New<TextAsset>(MemTag::Asset);
			asset->Import(sourcePath);
			return asset;
		}
	};

	class OwnerAsset : public TextAsset {
	public:
		Asset* held = nullptr;

		void OnUnload() override {
			if (held) {
				AssetDatabase::UnloadAsset(held);
				held = nullptr;
			}
		}
	};

	class OwnerImporter : public IAssetImporter {
	public:
		Path dependency;

		StringHash GetAssetType() const override { return StringHash("OwnerAsset"); }
		Asset* Import(const Path& sourcePath) override {
			OwnerAsset* asset = New<OwnerAsset>(MemTag::Asset);
			asset->Import(sourcePath);
			asset->held = AssetDatabase::LoadAsset(dependency);
			return asset;
		}
	};

	class AssetFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_assets_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root / "Assets");

			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();
		}

		void TearDown() override {
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		void WriteSource(const str& virtualPath, const str& content) {
			Path resolved = FileSystem::Resolve(Path(virtualPath));
			std::filesystem::path fsPath(resolved.ToString());
			std::error_code ec;
			std::filesystem::create_directories(fsPath.parent_path(), ec);
			FileSystem::WriteTextFile(resolved, content);
		}
	};

} // namespace

TEST_F(AssetFixture, LoadImportsContentAndMintsMeta) {
	WriteSource("project://Assets/readme.txt", "hello shalltear");

	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/readme.txt"));
	ASSERT_NE(asset, nullptr);

	TextAsset* text = static_cast<TextAsset*>(asset);
	EXPECT_EQ(text->content, "hello shalltear");
	EXPECT_TRUE(asset->assetId.IsValid());
	EXPECT_EQ(asset->GetLoadState(), AssetLoadState::Loaded);

	EXPECT_TRUE(FileSystem::Exists(Path("project://Assets/readme.txt.meta")));

	AssetDatabase::UnloadAsset(asset);
}

TEST_F(AssetFixture, LoadByPathCachesInstance) {
	WriteSource("project://Assets/a.txt", "cached");

	Asset* first = AssetDatabase::LoadAsset(Path("project://Assets/a.txt"));
	Asset* second = AssetDatabase::LoadAsset(Path("project://Assets/a.txt"));
	ASSERT_NE(first, nullptr);

	EXPECT_EQ(first, second);
	EXPECT_EQ(first->GetRefCount(), 2);

	AssetDatabase::UnloadAsset(first);
	AssetDatabase::UnloadAsset(second);
}

TEST_F(AssetFixture, LoadByGuidReturnsSameInstance) {
	WriteSource("project://Assets/b.txt", "by guid");

	Asset* byPath = AssetDatabase::LoadAsset(Path("project://Assets/b.txt"));
	ASSERT_NE(byPath, nullptr);
	Guid guid = byPath->assetId;

	Asset* byGuid = AssetDatabase::LoadAssetByGuid(guid);
	EXPECT_EQ(byGuid, byPath);

	AssetDatabase::UnloadAsset(byPath);
	AssetDatabase::UnloadAsset(byGuid);
}

TEST_F(AssetFixture, LoadMissingSourceReturnsNull) {
	EXPECT_EQ(AssetDatabase::LoadAsset(Path("project://Assets/does_not_exist.txt")), nullptr);
}

TEST_F(AssetFixture, ImporterCanResolveAssetWhileItRuns) {
	SelfLookupImporter importer;
	AssetImporterRegistry::Register(StringHash("SelfProbeAsset"), &importer, {".selfprobe"});

	WriteSource("project://Assets/probe.selfprobe", "probe");
	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/probe.selfprobe"));
	ASSERT_NE(asset, nullptr);

	EXPECT_TRUE(importer.seenGuid.IsValid());
	EXPECT_EQ(importer.seenGuid, asset->assetId);
	EXPECT_EQ(importer.seenPath.ToString(), "project://Assets/probe.selfprobe");

	AssetDatabase::UnloadAsset(asset);
	AssetImporterRegistry::Unregister(&importer);
}

TEST_F(AssetFixture, MintingImportSeesSettings) {
	SettingsShapeImporter importer;
	AssetImporterRegistry::Register(StringHash("SettingsShapeAsset"), &importer, {".shape"});

	WriteSource("project://Assets/knobs.shape", "content");

	Asset* first = AssetDatabase::LoadAsset(Path("project://Assets/knobs.shape"));
	ASSERT_NE(first, nullptr);
	AssetDatabase::UnloadAsset(first);
	AssetDatabase::CollectGarbage();

	WriteSource("project://Assets/knobs.shape", "content, revised");

	Asset* second = AssetDatabase::LoadAsset(Path("project://Assets/knobs.shape"));
	ASSERT_NE(second, nullptr);
	AssetDatabase::UnloadAsset(second);

	ASSERT_EQ(importer.seen.size(), 2u);
	EXPECT_EQ(importer.seen[0], importer.seen[1]);

	AssetImporterRegistry::Unregister(&importer);
}

TEST_F(AssetFixture, ProducerTeardownLeavesOthers) {
	SelfLookupImporter owner;
	SelfLookupImporter producer;
	AssetImporterRegistry::Register(StringHash("SelfProbeAsset"), &owner, {".selfprobe"});
	AssetImporterRegistry::RegisterExtension(".selfprobe2", &producer);

	EXPECT_EQ(AssetImporterRegistry::Find(StringHash("SelfProbeAsset")), &owner);
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(".selfprobe"), &owner);
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(".selfprobe2"), &producer);

	AssetImporterRegistry::Unregister(&producer);
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(".selfprobe2"), nullptr);
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(".selfprobe"), &owner);
	EXPECT_EQ(AssetImporterRegistry::Find(StringHash("SelfProbeAsset")), &owner);

	AssetImporterRegistry::Unregister(&owner);
	EXPECT_EQ(AssetImporterRegistry::Find(StringHash("SelfProbeAsset")), nullptr);
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(".selfprobe"), nullptr);
}

TEST_F(AssetFixture, GoneImporterLoadsNothing) {
	CountingImporter owner;
	CountingImporter producer;
	AssetImporterRegistry::Register(StringHash("CountedAsset"), &owner, {".counted"});
	AssetImporterRegistry::RegisterExtension(".counted2", &producer);
	WriteSource("project://Assets/made.counted2", "made by the producer");
	const Path path("project://Assets/made.counted2");

	Asset* first = AssetDatabase::LoadAsset(path);
	ASSERT_NE(first, nullptr);
	const Guid guid = first->assetId;
	AssetDatabase::UnloadAsset(first);
	AssetDatabase::CollectGarbage();
	ASSERT_EQ(producer.imports, 1);
	const str sidecar = FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://Assets/made.counted2.meta")), MemTag::Asset);
	ASSERT_NE(sidecar.find(std::format("{:016x}", StringHash("CountedAsset").GetValue())), str::npos);

	AssetImporterRegistry::Unregister(&producer);
	ASSERT_EQ(AssetImporterRegistry::FindByExtension(".counted2"), nullptr);
	EXPECT_EQ(AssetDatabase::LoadAsset(path), nullptr);
	EXPECT_EQ(owner.imports, 0);
	EXPECT_EQ(FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://Assets/made.counted2.meta")), MemTag::Asset), sidecar);
	EXPECT_EQ(AssetDatabase::LookupGuid(path), guid);

	AssetImporterRegistry::RegisterExtension(".counted2", &producer);
	Asset* again = AssetDatabase::LoadAsset(path);
	ASSERT_NE(again, nullptr);
	EXPECT_EQ(again->assetId, guid);
	AssetDatabase::UnloadAsset(again);

	AssetImporterRegistry::Unregister(&producer);
	AssetImporterRegistry::Unregister(&owner);
}

TEST_F(AssetFixture, OwnerSweepLeavesOthers) {
	SelfLookupImporter first;
	SelfLookupImporter second;
	SelfLookupImporter unowned;
	const int firstImage = 1;
	const int secondImage = 2;

	AssetImporterRegistry::Register(StringHash("FirstOwnedAsset"), &first, {".ownedone"}, &firstImage);
	AssetImporterRegistry::RegisterExtension(".ownedone2", &first, &firstImage);
	AssetImporterRegistry::Register(StringHash("SecondOwnedAsset"), &second, {".ownedtwo"}, &secondImage);
	AssetImporterRegistry::Register(StringHash("UnownedAsset"), &unowned, {".unowned"});

	EXPECT_EQ(AssetImporterRegistry::UnregisterOwnedBy(nullptr), 0u);
	EXPECT_EQ(AssetImporterRegistry::Find(StringHash("UnownedAsset")), &unowned);

	EXPECT_EQ(AssetImporterRegistry::UnregisterOwnedBy(&firstImage), 1u);
	EXPECT_EQ(AssetImporterRegistry::Find(StringHash("FirstOwnedAsset")), nullptr);
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(".ownedone"), nullptr);
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(".ownedone2"), nullptr);

	EXPECT_EQ(AssetImporterRegistry::Find(StringHash("SecondOwnedAsset")), &second);
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(".ownedtwo"), &second);
	EXPECT_EQ(AssetImporterRegistry::Find(StringHash("UnownedAsset")), &unowned);
	EXPECT_EQ(AssetImporterRegistry::FindByExtension(".unowned"), &unowned);

	EXPECT_EQ(AssetImporterRegistry::UnregisterOwnedBy(&firstImage), 0u);

	AssetImporterRegistry::Unregister(&second);
	AssetImporterRegistry::Unregister(&unowned);
}

TEST_F(AssetFixture, UnownedReRegisterEscapesSweep) {
	SelfLookupImporter importer;
	const int image = 0;

	AssetImporterRegistry::Register(StringHash("RestampedAsset"), &importer, {".restamped"}, &image);
	AssetImporterRegistry::Unregister(&importer);
	AssetImporterRegistry::Register(StringHash("RestampedAsset"), &importer, {".restamped"});

	EXPECT_EQ(AssetImporterRegistry::UnregisterOwnedBy(&image), 0u);
	EXPECT_EQ(AssetImporterRegistry::Find(StringHash("RestampedAsset")), &importer);

	AssetImporterRegistry::Unregister(&importer);
}

TEST_F(AssetFixture, SaveWritesSourceAndMetaWithGuid) {
	TextAsset* asset = New<TextAsset>(MemTag::Asset);
	asset->virtualPath = "project://Assets/saved.txt";
	asset->content = "persisted content";

	AssetDatabase::SaveAsset(asset);
	Guid guid = asset->assetId;
	EXPECT_TRUE(guid.IsValid());

	EXPECT_TRUE(FileSystem::Exists(Path("project://Assets/saved.txt")));
	EXPECT_TRUE(FileSystem::Exists(Path("project://Assets/saved.txt.meta")));

	Delete(asset);

	Asset* reloaded = AssetDatabase::LoadAsset(Path("project://Assets/saved.txt"));
	ASSERT_NE(reloaded, nullptr);
	EXPECT_EQ(static_cast<TextAsset*>(reloaded)->content, "persisted content");
	EXPECT_EQ(reloaded->assetId, guid);

	AssetDatabase::UnloadAsset(reloaded);
}

TEST_F(AssetFixture, UnloadParksCollectDestroys) {
	WriteSource("project://Assets/temp.txt", "ephemeral");

	Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/temp.txt"));
	ASSERT_NE(asset, nullptr);

	Handle<Object> weak(asset);
	EXPECT_TRUE(weak.IsValid());

	AssetDatabase::UnloadAsset(asset);
	EXPECT_TRUE(weak.IsValid());

	EXPECT_EQ(AssetDatabase::CollectGarbage(), 1u);
	EXPECT_FALSE(weak.IsValid());
	EXPECT_EQ(weak.Get(), nullptr);

	EXPECT_EQ(AssetDatabase::CollectGarbage(), 0u);
}

TEST_F(AssetFixture, UnloadKeepsInstanceWhileReferenced) {
	WriteSource("project://Assets/shared.txt", "shared");

	Asset* a = AssetDatabase::LoadAsset(Path("project://Assets/shared.txt"));
	Asset* b = AssetDatabase::LoadAsset(Path("project://Assets/shared.txt"));
	Handle<Object> weak(a);

	AssetDatabase::UnloadAsset(a);
	EXPECT_TRUE(weak.IsValid());
	EXPECT_EQ(AssetDatabase::CollectGarbage(), 0u);
	EXPECT_TRUE(weak.IsValid());

	AssetDatabase::UnloadAsset(b);
	EXPECT_EQ(AssetDatabase::CollectGarbage(), 1u);
	EXPECT_FALSE(weak.IsValid());
}

TEST_F(AssetFixture, EnsureIdentityMintsWithoutImport) {
	CountingImporter importer;
	AssetImporterRegistry::Register(StringHash("CountedAsset"), &importer, {".counted"});
	WriteSource("project://Assets/art.counted", "payload");

	Guid guid = AssetDatabase::EnsureAssetIdentity(Path("project://Assets/art.counted"));
	EXPECT_TRUE(guid.IsValid());
	EXPECT_EQ(importer.imports, 0);

	EXPECT_TRUE(FileSystem::Exists(FileSystem::Resolve(AssetDatabase::GetMetaPath(Path("project://Assets/art.counted")))));
	EXPECT_EQ(AssetDatabase::LookupGuid(Path("project://Assets/art.counted")), guid);
	EXPECT_EQ(AssetDatabase::LookupPath(guid).ToString(), "project://Assets/art.counted");

	EXPECT_EQ(AssetDatabase::EnsureAssetIdentity(Path("project://Assets/art.counted")), guid);
	EXPECT_EQ(importer.imports, 0);

	Asset* loaded = AssetDatabase::LoadAssetByGuid(guid);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(loaded->assetId, guid);
	EXPECT_EQ(importer.imports, 1);
	AssetDatabase::UnloadAsset(loaded);

	AssetDatabase::CollectGarbage();
	AssetImporterRegistry::Unregister(&importer);
}

TEST_F(AssetFixture, EnsureIdentityRefusesInvalid) {
	EXPECT_FALSE(AssetDatabase::EnsureAssetIdentity(Path("project://Assets/absent.txt")).IsValid());
	WriteSource("project://Assets/notes.unclaimed", "x");
	EXPECT_FALSE(AssetDatabase::EnsureAssetIdentity(Path("project://Assets/notes.unclaimed")).IsValid());
	EXPECT_FALSE(FileSystem::Exists(FileSystem::Resolve(AssetDatabase::GetMetaPath(Path("project://Assets/notes.unclaimed")))));
}

TEST_F(AssetFixture, SequentialConsumersImportOnce) {
	CountingImporter importer;
	AssetImporterRegistry::Register(StringHash("CountedAsset"), &importer, {".counted"});
	WriteSource("project://Assets/shared.counted", "body");

	u64 residentInstance = 0;
	for (int i = 0; i < 4; ++i) {
		Asset* asset = AssetDatabase::LoadAsset(Path("project://Assets/shared.counted"));
		ASSERT_NE(asset, nullptr) << "iteration " << i;
		residentInstance = asset->GetInstanceId();
		AssetDatabase::UnloadAsset(asset);
	}
	EXPECT_EQ(importer.imports, 1);

	EXPECT_EQ(AssetDatabase::CollectGarbage(), 1u);
	Asset* again = AssetDatabase::LoadAsset(Path("project://Assets/shared.counted"));
	ASSERT_NE(again, nullptr);
	EXPECT_NE(again->GetInstanceId(), residentInstance);
	AssetDatabase::UnloadAsset(again);

	AssetDatabase::CollectGarbage();
	AssetImporterRegistry::Unregister(&importer);
}

TEST_F(AssetFixture, DoubleParkDestroysOnce) {
	CountingImporter importer;
	AssetImporterRegistry::Register(StringHash("CountedAsset"), &importer, {".counted"});
	WriteSource("project://Assets/cycle.counted", "body");

	Asset* first = AssetDatabase::LoadAsset(Path("project://Assets/cycle.counted"));
	ASSERT_NE(first, nullptr);
	Handle<Object> weak(first);
	AssetDatabase::UnloadAsset(first);
	Asset* revived = AssetDatabase::LoadAsset(Path("project://Assets/cycle.counted"));
	EXPECT_EQ(revived, first);
	AssetDatabase::UnloadAsset(revived);

	EXPECT_EQ(AssetDatabase::CollectGarbage(), 1u);
	EXPECT_FALSE(weak.IsValid());
	EXPECT_EQ(importer.imports, 1);

	AssetImporterRegistry::Unregister(&importer);
}

TEST_F(AssetFixture, DrainCollectsReleasedDependency) {
	CountingImporter leafImporter;
	AssetImporterRegistry::Register(StringHash("CountedAsset"), &leafImporter, {".counted"});
	WriteSource("project://Assets/leaf.counted", "leaf");

	OwnerImporter ownerImporter;
	ownerImporter.dependency = Path("project://Assets/leaf.counted");
	AssetImporterRegistry::Register(StringHash("OwnerAsset"), &ownerImporter, {".owner"});
	WriteSource("project://Assets/root.owner", "root");

	Asset* owner = AssetDatabase::LoadAsset(Path("project://Assets/root.owner"));
	ASSERT_NE(owner, nullptr);
	Asset* leaf = AssetDatabase::LoadAsset(Path("project://Assets/leaf.counted"));
	ASSERT_NE(leaf, nullptr);
	Handle<Object> weakLeaf(leaf);
	AssetDatabase::UnloadAsset(leaf);

	AssetDatabase::UnloadAsset(owner);
	EXPECT_EQ(AssetDatabase::CollectGarbage(), 2u);
	EXPECT_FALSE(weakLeaf.IsValid());

	AssetImporterRegistry::Unregister(&ownerImporter);
	AssetImporterRegistry::Unregister(&leafImporter);
}

TEST_F(AssetFixture, GenerateGuidIsNonZeroAndUnique) {
	Guid a = AssetDatabase::GenerateGuid();
	Guid b = AssetDatabase::GenerateGuid();
	EXPECT_TRUE(a.IsValid());
	EXPECT_TRUE(b.IsValid());
	EXPECT_NE(a, b);
}

TEST_F(AssetFixture, GetMetaPathAppendsSuffix) {
	Path meta = AssetDatabase::GetMetaPath(Path("project://Assets/Assets/Mesh.fbx"));
	EXPECT_EQ(meta.ToString(), "project://Assets/Assets/Mesh.fbx.meta");
}

TEST_F(AssetFixture, IsAssetPathMatchesRegisteredExtension) {
	EXPECT_TRUE(AssetDatabase::IsAssetPath(Path("project://Assets/thing.txt")));
	EXPECT_FALSE(AssetDatabase::IsAssetPath(Path("project://Assets/thing.unknownext")));
}

TEST_F(AssetFixture, AssetHandleRoundTripsThroughDatabase) {
	WriteSource("project://Assets/target.txt", "i am referenced");
	Asset* target = AssetDatabase::LoadAsset(Path("project://Assets/target.txt"));
	ASSERT_NE(target, nullptr);

	AssetRefComponent src;
	src.target = Handle<Object>(target);
	src.marker = 99;

	Buffer buffer = BinarySerializer::Serialize(&src, AssetRefComponent::GetReflectType());
	ASSERT_TRUE(buffer);

	AssetRefComponent dst;
	dst.marker = 0;
	ASSERT_TRUE(BinarySerializer::Deserialize(&dst, AssetRefComponent::GetReflectType(), buffer));

	EXPECT_EQ(dst.marker, 99);
	EXPECT_EQ(dst.target.GetInstanceId(), target->GetInstanceId());
	EXPECT_EQ(dst.target.Get(), target);

	AssetDatabase::UnloadAsset(target);
}

TEST_F(AssetFixture, NullAssetHandleRoundTripsAsNull) {
	AssetRefComponent src;
	src.marker = 3;

	Buffer buffer = BinarySerializer::Serialize(&src, AssetRefComponent::GetReflectType());
	ASSERT_TRUE(buffer);

	AssetRefComponent dst;
	ASSERT_TRUE(BinarySerializer::Deserialize(&dst, AssetRefComponent::GetReflectType(), buffer));

	EXPECT_EQ(dst.marker, 3);
	EXPECT_EQ(dst.target.GetInstanceId(), 0u);
}

TEST_F(AssetFixture, ScanGeneratesMetaAndIndexesAssets) {
	WriteSource("project://Assets/one.txt", "1");
	WriteSource("project://Assets/sub/two.txt", "2");

	AssetDatabase::ScanProjectDirectory();

	Guid g1 = AssetDatabase::LookupGuid(Path("project://Assets/one.txt"));
	Guid g2 = AssetDatabase::LookupGuid(Path("project://Assets/sub/two.txt"));
	EXPECT_TRUE(g1.IsValid());
	EXPECT_TRUE(g2.IsValid());
	EXPECT_NE(g1, g2);

	EXPECT_TRUE(FileSystem::Exists(Path("project://.assets.db")));
	EXPECT_TRUE(FileSystem::Exists(Path("project://Assets/one.txt.meta")));
	EXPECT_TRUE(FileSystem::Exists(Path("project://Assets/sub/two.txt.meta")));
}

TEST_F(AssetFixture, ScanIndexesOnlyTheContentRoot) {
	WriteSource("project://Assets/keep.txt", "keep");
	WriteSource("project://Project.ini", "[Project]\nname=X\n");
	WriteSource("project://CMakeLists.txt", "cmake_minimum_required(VERSION 3.25)");
	WriteSource("project://.gitignore", "Library/");
	WriteSource("project://.vscode/settings.json", "{}");
	WriteSource("project://.git/objects/ab/cdef", "binary");
	WriteSource("project://Library/ShaderCache/blob.bin", "x");
	WriteSource("project://Source/Player.cpp", "int main(){}");

	AssetDatabase::ScanProjectDirectory();

	EXPECT_TRUE(AssetDatabase::LookupGuid(Path("project://Assets/keep.txt")).IsValid());
	for (const char* outside : {"project://Project.ini", "project://CMakeLists.txt",
								"project://.gitignore", "project://.vscode/settings.json",
								"project://.git/objects/ab/cdef",
								"project://Library/ShaderCache/blob.bin",
								"project://Source/Player.cpp"}) {
		EXPECT_FALSE(AssetDatabase::LookupGuid(Path(outside)).IsValid()) << outside;
		EXPECT_FALSE(FileSystem::Exists(Path(str(outside) + ".meta"))) << outside;
	}
}

TEST_F(AssetFixture, EngineScopeStillIndexesFromMountRoot) {
	std::filesystem::path engineRoot = _root / "EngineScope";
	std::filesystem::create_directories(engineRoot / "Shaders");
	FileSystem::WriteTextFile(Path((engineRoot / "Shaders" / "builtin.txt").generic_string()), "shader");
	FileSystem::WriteTextFile(Path((engineRoot / "Shaders" / "builtin.txt.meta").generic_string()),
							  "{\n\t\"assetId\": \"376e2ff23e456f74-32aa3655589414a0\",\n\t\"type\": \"0000000000000000\"\n}\n");

	FileSystem::Mount("engine", Path(engineRoot.generic_string()));
	AssetDatabase::RebuildDatabase();

	EXPECT_TRUE(AssetDatabase::LookupGuid(Path("engine://Shaders/builtin.txt")).IsValid());
	FileSystem::Unmount("engine");
}

TEST_F(AssetFixture, ScanIsStableAcrossRuns) {
	WriteSource("project://Assets/stable.txt", "x");

	AssetDatabase::ScanProjectDirectory();
	Guid first = AssetDatabase::LookupGuid(Path("project://Assets/stable.txt"));

	AssetDatabase::ScanProjectDirectory();
	Guid second = AssetDatabase::LookupGuid(Path("project://Assets/stable.txt"));

	EXPECT_EQ(first, second);
}

TEST_F(AssetFixture, RebuildPreservesGuids) {
	WriteSource("project://Assets/keep.txt", "y");
	AssetDatabase::ScanProjectDirectory();
	Guid before = AssetDatabase::LookupGuid(Path("project://Assets/keep.txt"));

	AssetDatabase::RebuildDatabase();
	Guid after = AssetDatabase::LookupGuid(Path("project://Assets/keep.txt"));

	EXPECT_TRUE(before.IsValid());
	EXPECT_EQ(before, after);
}

TEST_F(AssetFixture, MovedAssetResolvesByGuidAfterRebuild) {
	WriteSource("project://Assets/old/name.txt", "movable");
	AssetDatabase::ScanProjectDirectory();
	Guid guid = AssetDatabase::LookupGuid(Path("project://Assets/old/name.txt"));
	ASSERT_TRUE(guid.IsValid());

	Path newDir = FileSystem::Resolve(Path("project://Assets/moved"));
	std::filesystem::create_directories(newDir.ToString());
	std::filesystem::rename(FileSystem::Resolve(Path("project://Assets/old/name.txt")).ToString(),
							FileSystem::Resolve(Path("project://Assets/moved/name.txt")).ToString());
	std::filesystem::rename(FileSystem::Resolve(Path("project://Assets/old/name.txt.meta")).ToString(),
							FileSystem::Resolve(Path("project://Assets/moved/name.txt.meta")).ToString());

	AssetDatabase::RebuildDatabase();

	EXPECT_EQ(AssetDatabase::LookupPath(guid).ToString(), "project://Assets/moved/name.txt");
	EXPECT_FALSE(AssetDatabase::LookupGuid(Path("project://Assets/old/name.txt")).IsValid());
}

TEST_F(AssetFixture, AssetsDbPersistsAcrossReinitialize) {
	WriteSource("project://Assets/persist.txt", "z");
	AssetDatabase::ScanProjectDirectory();
	Guid guid = AssetDatabase::LookupGuid(Path("project://Assets/persist.txt"));
	ASSERT_TRUE(guid.IsValid());

	AssetDatabase::Terminate();
	AssetDatabase::Initialize();

	EXPECT_EQ(AssetDatabase::LookupPath(guid).ToString(), "project://Assets/persist.txt");
	EXPECT_EQ(AssetDatabase::LookupGuid(Path("project://Assets/persist.txt")), guid);
}

TEST_F(AssetFixture, AssetsSurvivePartialMetaDeletion) {
	constexpr int COUNT = 64;
	std::vector<Guid> guids(COUNT);
	std::vector<str> paths(COUNT);

	for (int i = 0; i < COUNT; ++i) {
		paths[i] = "project://Assets/bulk/asset_" + std::to_string(i) + ".txt";
		WriteSource(paths[i], "payload " + std::to_string(i));
	}

	AssetDatabase::ScanProjectDirectory();
	for (int i = 0; i < COUNT; ++i) {
		guids[i] = AssetDatabase::LookupGuid(Path(paths[i]));
		EXPECT_TRUE(guids[i].IsValid()) << "i=" << i;
	}

	for (int i = 0; i < COUNT; i += 10) {
		std::error_code ec;
		std::filesystem::remove(FileSystem::Resolve(AssetDatabase::GetMetaPath(Path(paths[i]))).ToString(), ec);
	}

	AssetDatabase::RebuildDatabase();

	for (int i = 0; i < COUNT; ++i) {
		Guid resolved = AssetDatabase::LookupGuid(Path(paths[i]));
		ASSERT_TRUE(resolved.IsValid()) << "i=" << i;
		if (i % 10 == 0) {
			EXPECT_NE(resolved, guids[i]) << i;
			EXPECT_FALSE(AssetDatabase::LookupPath(guids[i]).ToString() == paths[i]);
		} else {
			EXPECT_EQ(resolved, guids[i]) << i;
		}
	}
}
