#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace Shalltear;

namespace {

	constexpr const char* V1_DLL = "ShalltearHotReloadModuleV1.dll";
	constexpr const char* V2_DLL = "ShalltearHotReloadModuleV2.dll";
	constexpr const char* GONE_DLL = "ShalltearHotReloadModuleGone.dll";

	const StringHash DataAssetType{"HotReloadDataAsset"};
	const StringHash BehaviorType{"HotReloadBehavior"};
	constexpr const char* ASSET_PATH = "project://MigrationProbe.hrdata";

	class DataAssetMigrationFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		Handle<Scene> _scene;
		ModuleHandle _module;
		Guid _assetId;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_dataassetmig_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));

			AssetDatabase::Initialize();
			SceneSystem::Initialize();
			ModuleSystem::Initialize();

			_scene = SceneSystem::CreateScene("DataAssetMigrationTest");
			_module = ModuleSystem::Load(Path(V1_DLL));
			ASSERT_TRUE(ModuleSystem::IsLoaded(_module));
			ASSERT_NE(TypeRegistry::FindType(DataAssetType), nullptr);
		}

		void TearDown() override {
			SceneSystem::Terminate();
			ModuleSystem::Terminate();
			AssetDatabase::CollectGarbage();
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		Asset* CreateAndLoadProbeAsset() {
			_assetId = AssetDatabase::CreateAsset(Path(ASSET_PATH), DataAssetType);
			EXPECT_TRUE(_assetId.IsValid());
			Asset* asset = AssetDatabase::LoadAsset(Path(ASSET_PATH));
			EXPECT_NE(asset, nullptr);
			return asset;
		}

		const TypeInfo* ProbeType() { return TypeRegistry::FindType(DataAssetType); }

		void SetDamage(Asset* asset, i32 value) {
			Variant boxed{value};
			ASSERT_TRUE(SetField(asset, ProbeType(), StringHash("damage"), &boxed));
		}

		i32 GetDamage(Asset* asset) {
			Variant out;
			EXPECT_TRUE(GetField(asset, ProbeType(), StringHash("damage"), &out));
			const i32* value = out.TryGet<i32>();
			return value ? *value : -1;
		}

		i32 AssetMagic(Asset* asset) {
			Variant ret;
			EXPECT_TRUE(InvokeMethod(asset, ProbeType(), StringHash("GetAssetMagic"), nullptr, 0, &ret));
			const i32* value = ret.TryGet<i32>();
			return value ? *value : -1;
		}

		u64 ResolvedDependencyId(Asset* asset) {
			Variant ret;
			EXPECT_TRUE(InvokeMethod(asset, ProbeType(), StringHash("GetResolvedDependencyId"), nullptr, 0, &ret));
			const u64* value = ret.TryGet<u64>();
			return value ? *value : ~0ull;
		}

		void RebuildResolvedDependency(Asset* asset) {
			EXPECT_TRUE(InvokeMethod(asset, ProbeType(), StringHash("RebuildResolvedDependency"), nullptr, 0, nullptr));
		}

		bool ReloadTo(const char* dll) {
			Path target{dll};
			return ModuleSystem::Reload(_module, &target);
		}
	};

} // anonymous namespace

TEST_F(DataAssetMigrationFixture, ReloadKeepsIdentityAndType) {
	Asset* before = CreateAndLoadProbeAsset();
	ASSERT_NE(before, nullptr);
	const u64 id = before->GetInstanceId();
	const StringHash type = before->GetAssetTypeId();
	ASSERT_EQ(AssetMagic(before), 1);

	Handle<Asset> handle(before);
	ASSERT_TRUE(ReloadTo(V2_DLL));

	Asset* after = handle.Get();
	ASSERT_NE(after, nullptr);
	EXPECT_EQ(after->GetInstanceId(), id);
	EXPECT_EQ(after->GetAssetTypeId(), type);
	EXPECT_EQ(AssetMagic(after), 2);
	EXPECT_EQ(after->assetId, _assetId);
}

TEST_F(DataAssetMigrationFixture, EditNeverWrittenToDiskSurvivesReload) {
	Asset* before = CreateAndLoadProbeAsset();
	ASSERT_NE(before, nullptr);
	SetDamage(before, 4242);

	str onDisk = FileSystem::ReadTextFile(FileSystem::Resolve(Path(ASSET_PATH)), MemTag::Core);
	EXPECT_EQ(onDisk.find("4242"), str::npos);

	Handle<Asset> handle(before);
	ASSERT_TRUE(ReloadTo(V2_DLL));
	Asset* after = handle.Get();
	ASSERT_NE(after, nullptr);
	EXPECT_EQ(GetDamage(after), 4242);
}

TEST_F(DataAssetMigrationFixture, ComponentHandleResolvesAfterReload) {
	Asset* asset = CreateAndLoadProbeAsset();
	ASSERT_NE(asset, nullptr);
	const u64 assetId = asset->GetInstanceId();

	Handle<SceneObject> owner = SceneSystem::CreateObject("Holder", _scene);
	Handle<Component> behavior = owner.Get()->AddComponent(TypeRegistry::FindType(BehaviorType));
	ASSERT_TRUE(behavior.IsValid());
	{
		Variant boxed{Handle<Asset>(asset)};
		ASSERT_TRUE(SetField(behavior.Get(), TypeRegistry::FindType(BehaviorType), StringHash("data"), &boxed));
	}

	ASSERT_TRUE(ReloadTo(V2_DLL));

	Variant out;
	ASSERT_TRUE(GetField(behavior.Get(), TypeRegistry::FindType(BehaviorType), StringHash("data"), &out));
	const Handle<Asset>* restored = out.TryGet<Handle<Asset>>();
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->GetInstanceId(), assetId);
	EXPECT_NE(restored->Get(), nullptr);
}

TEST_F(DataAssetMigrationFixture, AssetHandleResolvesAfterReload) {
	Asset* asset = CreateAndLoadProbeAsset();
	ASSERT_NE(asset, nullptr);

	Handle<SceneObject> owner = SceneSystem::CreateObject("Holder", _scene);
	Handle<Component> behavior = owner.Get()->AddComponent(TypeRegistry::FindType(BehaviorType));
	ASSERT_TRUE(behavior.IsValid());
	const u64 behaviorId = behavior.GetInstanceId();
	{
		Variant boxed{behavior};
		ASSERT_TRUE(SetField(asset, ProbeType(), StringHash("holder"), &boxed));
	}

	Handle<Asset> handle(asset);
	ASSERT_TRUE(ReloadTo(V2_DLL));
	Asset* after = handle.Get();
	ASSERT_NE(after, nullptr);

	Variant out;
	ASSERT_TRUE(GetField(after, ProbeType(), StringHash("holder"), &out));
	const Handle<Component>* restored = out.TryGet<Handle<Component>>();
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->GetInstanceId(), behaviorId);
	EXPECT_NE(restored->Get(), nullptr);
}

TEST_F(DataAssetMigrationFixture, HandlePersistsAsGuidAfterReload) {
	Asset* asset = CreateAndLoadProbeAsset();
	ASSERT_NE(asset, nullptr);

	Handle<SceneObject> owner = SceneSystem::CreateObject("Holder", _scene);
	Handle<Component> behavior = owner.Get()->AddComponent(TypeRegistry::FindType(BehaviorType));
	ASSERT_TRUE(behavior.IsValid());
	Variant boxed{Handle<Asset>(asset)};
	ASSERT_TRUE(SetField(behavior.Get(), TypeRegistry::FindType(BehaviorType), StringHash("data"), &boxed));

	const str guidText = _assetId.ToString();
	str beforeText = TextSerializer::Serialize(behavior.Get(), TypeRegistry::FindType(BehaviorType));
	ASSERT_NE(beforeText.find(guidText), str::npos);

	ASSERT_TRUE(ReloadTo(V2_DLL));

	str afterText = TextSerializer::Serialize(behavior.Get(), TypeRegistry::FindType(BehaviorType));
	EXPECT_NE(afterText.find(guidText), str::npos);
}

TEST_F(DataAssetMigrationFixture, ParkedAssetStaysParked) {
	Asset* asset = CreateAndLoadProbeAsset();
	ASSERT_NE(asset, nullptr);

	AssetDatabase::UnloadAsset(asset);
	ASSERT_TRUE(ReloadTo(V2_DLL));

	EXPECT_EQ(AssetDatabase::CollectGarbage(), 1u);
	EXPECT_EQ(AssetDatabase::CollectGarbage(), 0u);
}

TEST_F(DataAssetMigrationFixture, RevivedAssetIsReplacement) {
	Asset* asset = CreateAndLoadProbeAsset();
	ASSERT_NE(asset, nullptr);
	SetDamage(asset, 99);
	AssetDatabase::UnloadAsset(asset);

	ASSERT_TRUE(ReloadTo(V2_DLL));

	Asset* revived = AssetDatabase::LoadAssetByGuid(_assetId);
	ASSERT_NE(revived, nullptr);
	EXPECT_EQ(GetDamage(revived), 99);
	EXPECT_EQ(AssetMagic(revived), 2);

	EXPECT_EQ(AssetDatabase::CollectGarbage(), 0u);
	AssetDatabase::UnloadAsset(revived);
	EXPECT_EQ(AssetDatabase::CollectGarbage(), 1u);
}

TEST_F(DataAssetMigrationFixture, TheReferenceCountRidesTheReload) {
	Asset* asset = CreateAndLoadProbeAsset();
	ASSERT_NE(asset, nullptr);
	ASSERT_NE(AssetDatabase::LoadAssetByGuid(_assetId), nullptr);
	ASSERT_EQ(asset->GetRefCount(), 2);

	Handle<Asset> handle(asset);
	ASSERT_TRUE(ReloadTo(V2_DLL));
	Asset* after = handle.Get();
	ASSERT_NE(after, nullptr);
	EXPECT_EQ(after->GetRefCount(), 2);
	EXPECT_EQ(after->GetLoadState(), AssetLoadState::Loaded);

	AssetDatabase::UnloadAsset(after);
	EXPECT_EQ(AssetDatabase::CollectGarbage(), 0u);
	AssetDatabase::UnloadAsset(after);
	EXPECT_EQ(AssetDatabase::CollectGarbage(), 1u);
}

TEST_F(DataAssetMigrationFixture, MissingTypeParksState) {
	Asset* asset = CreateAndLoadProbeAsset();
	ASSERT_NE(asset, nullptr);
	SetDamage(asset, 1234);
	Handle<Asset> handle(asset);
	const u64 idBefore = handle.GetInstanceId();

	ASSERT_TRUE(ReloadTo(GONE_DLL));
	EXPECT_EQ(TypeRegistry::FindType(DataAssetType), nullptr);
	EXPECT_EQ(handle.Get(), nullptr);
	EXPECT_EQ(AssetDatabase::LoadAssetByGuid(_assetId), nullptr);

	ASSERT_TRUE(ReloadTo(V1_DLL));
	ASSERT_NE(TypeRegistry::FindType(DataAssetType), nullptr);
	Asset* reclaimed = AssetDatabase::LoadAssetByGuid(_assetId);
	ASSERT_NE(reclaimed, nullptr);
	EXPECT_EQ(GetDamage(reclaimed), 1234);
	EXPECT_EQ(reclaimed->assetId, _assetId);

	EXPECT_NE(reclaimed->GetInstanceId(), idBefore);

	AssetDatabase::UnloadAsset(reclaimed);
}

TEST_F(DataAssetMigrationFixture, VanishedTypeSaveRefused) {
	Asset* asset = CreateAndLoadProbeAsset();
	ASSERT_NE(asset, nullptr);
	const Path physical = FileSystem::Resolve(Path(ASSET_PATH));
	const str original = FileSystem::ReadTextFile(physical, MemTag::Core);
	ASSERT_FALSE(original.empty());

	ASSERT_TRUE(ReloadTo(GONE_DLL));

	auto* impostor = New<TextAsset>(MemTag::Asset);
	impostor->assetId = _assetId;
	impostor->virtualPath = Path(ASSET_PATH).ToString();
	AssetDatabase::SaveAsset(impostor);
	Delete(impostor);

	EXPECT_EQ(FileSystem::ReadTextFile(physical, MemTag::Core), original);
}

TEST_F(DataAssetMigrationFixture, ReloadDropsLiveHalfKeepsReference) {
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Dependency.txt")), "payload");
	const Guid dependencyId = AssetDatabase::EnsureAssetIdentity(Path("project://Dependency.txt"));
	ASSERT_TRUE(dependencyId.IsValid());

	Asset* probe = CreateAndLoadProbeAsset();
	ASSERT_NE(probe, nullptr);
	{
		Variant boxed{dependencyId};
		ASSERT_TRUE(SetField(probe, ProbeType(), StringHash("dependency"), &boxed));
	}
	AssetDatabase::SaveAsset(probe);
	AssetDatabase::UnloadAsset(probe);
	ASSERT_EQ(AssetDatabase::CollectGarbage(), 1u);

	probe = AssetDatabase::LoadAsset(Path(ASSET_PATH));
	ASSERT_NE(probe, nullptr);

	Handle<Asset> dependency(AssetDatabase::LoadAssetByGuid(dependencyId));
	ASSERT_TRUE(dependency.IsValid());
	ASSERT_EQ(ResolvedDependencyId(probe), dependency.GetInstanceId());
	ASSERT_EQ(dependency.Get()->GetRefCount(), 2);

	Handle<Asset> handle(probe);
	ASSERT_TRUE(ReloadTo(V2_DLL));
	Asset* after = handle.Get();
	ASSERT_NE(after, nullptr);

	Variant persisted;
	ASSERT_TRUE(GetField(after, ProbeType(), StringHash("dependency"), &persisted));
	ASSERT_NE(persisted.TryGet<Guid>(), nullptr);
	EXPECT_EQ(*persisted.TryGet<Guid>(), dependencyId);

	EXPECT_EQ(ResolvedDependencyId(after), 0ull);
	EXPECT_EQ(dependency.Get()->GetRefCount(), 2);

	RebuildResolvedDependency(after);
	EXPECT_EQ(ResolvedDependencyId(after), dependency.GetInstanceId());
	EXPECT_EQ(dependency.Get()->GetRefCount(), 2);

	AssetDatabase::UnloadAsset(dependency.Get());
}
TEST_F(DataAssetMigrationFixture, FindLoadedHasNoSideEffects) {
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Standalone.txt")), "payload");
	const Guid id = AssetDatabase::EnsureAssetIdentity(Path("project://Standalone.txt"));
	ASSERT_TRUE(id.IsValid());

	EXPECT_EQ(AssetDatabase::FindLoadedByGuid(id), nullptr);

	Asset* loaded = AssetDatabase::LoadAssetByGuid(id);
	ASSERT_NE(loaded, nullptr);
	EXPECT_EQ(AssetDatabase::FindLoadedByGuid(id), loaded);
	EXPECT_EQ(loaded->GetRefCount(), 1);

	AssetDatabase::UnloadAsset(loaded);
	EXPECT_EQ(AssetDatabase::FindLoadedByGuid(id), loaded);
	EXPECT_EQ(loaded->GetRefCount(), 0);
	EXPECT_EQ(AssetDatabase::CollectGarbage(), 1u);
	EXPECT_EQ(AssetDatabase::FindLoadedByGuid(id), nullptr);
}

TEST_F(DataAssetMigrationFixture, ModuleAssetOwnsReferences) {
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Bound.txt")), "payload");
	const Guid targetId = AssetDatabase::EnsureAssetIdentity(Path("project://Bound.txt"));
	ASSERT_TRUE(targetId.IsValid());

	Asset* probe = CreateAndLoadProbeAsset();
	ASSERT_NE(probe, nullptr);
	{
		Asset* target = AssetDatabase::LoadAssetByGuid(targetId);
		ASSERT_NE(target, nullptr);
		Variant boxed{Handle<TextAsset>(static_cast<TextAsset*>(target))};
		ASSERT_TRUE(SetField(probe, ProbeType(), StringHash("reference"), &boxed));
		AssetDatabase::SaveAsset(probe);
		AssetDatabase::UnloadAsset(target);
	}
	AssetDatabase::UnloadAsset(probe);
	ASSERT_EQ(AssetDatabase::CollectGarbage(), 2u);

	probe = AssetDatabase::LoadAsset(Path(ASSET_PATH));
	ASSERT_NE(probe, nullptr);

	Variant out;
	ASSERT_TRUE(GetField(probe, ProbeType(), StringHash("reference"), &out));
	const Handle<TextAsset>* bound = out.TryGet<Handle<TextAsset>>();
	ASSERT_NE(bound, nullptr);
	EXPECT_NE(bound->GetInstanceId(), 0ull);

	Handle<Asset> target(AssetDatabase::LoadAssetByGuid(targetId));
	ASSERT_TRUE(target.IsValid());
	EXPECT_EQ(target.Get()->GetRefCount(), 2);

	AssetDatabase::UnloadAsset(probe);
	EXPECT_EQ(AssetDatabase::CollectGarbage(), 1u);
	EXPECT_EQ(target.Get()->GetRefCount(), 1);
	AssetDatabase::UnloadAsset(target.Get());
	EXPECT_EQ(AssetDatabase::CollectGarbage(), 1u);
}

TEST_F(DataAssetMigrationFixture, BoundReferenceOwnedAfterReload) {
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Referenced.txt")), "payload");
	const Guid referencedId = AssetDatabase::EnsureAssetIdentity(Path("project://Referenced.txt"));
	ASSERT_TRUE(referencedId.IsValid());

	Asset* probe = CreateAndLoadProbeAsset();
	ASSERT_NE(probe, nullptr);
	{
		Asset* target = AssetDatabase::LoadAssetByGuid(referencedId);
		ASSERT_NE(target, nullptr);
		Variant boxed{Handle<TextAsset>(static_cast<TextAsset*>(target))};
		ASSERT_TRUE(SetField(probe, ProbeType(), StringHash("reference"), &boxed));
		AssetDatabase::SaveAsset(probe);
		AssetDatabase::UnloadAsset(target);
	}
	AssetDatabase::UnloadAsset(probe);
	ASSERT_EQ(AssetDatabase::CollectGarbage(), 2u);

	probe = AssetDatabase::LoadAsset(Path(ASSET_PATH));
	ASSERT_NE(probe, nullptr);

	{
		Variant cold;
		ASSERT_TRUE(GetField(probe, ProbeType(), StringHash("reference"), &cold));
		const Handle<TextAsset>* bound = cold.TryGet<Handle<TextAsset>>();
		ASSERT_NE(bound, nullptr);
		ASSERT_NE(bound->GetInstanceId(), 0ull);
	}

	Handle<Asset> referenced(AssetDatabase::LoadAssetByGuid(referencedId));
	ASSERT_TRUE(referenced.IsValid());
	ASSERT_EQ(referenced.Get()->GetRefCount(), 2);

	Handle<Asset> handle(probe);
	ASSERT_TRUE(ReloadTo(V2_DLL));
	Asset* after = handle.Get();
	ASSERT_NE(after, nullptr);

	Variant out;
	ASSERT_TRUE(GetField(after, ProbeType(), StringHash("reference"), &out));
	const Handle<TextAsset>* restored = out.TryGet<Handle<TextAsset>>();
	ASSERT_NE(restored, nullptr);
	EXPECT_EQ(restored->GetInstanceId(), referenced.GetInstanceId());
	EXPECT_EQ(referenced.Get()->GetRefCount(), 2);

	AssetDatabase::UnloadAsset(after);
	EXPECT_EQ(AssetDatabase::CollectGarbage(), 1u);
	EXPECT_EQ(referenced.Get()->GetRefCount(), 1);

	AssetDatabase::UnloadAsset(referenced.Get());
	EXPECT_EQ(AssetDatabase::CollectGarbage(), 1u);
}

TEST_F(DataAssetMigrationFixture, RestoredListIsWhatWasBound) {
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Bound.txt")), "bound");
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Renamed.txt")), "renamed");
	const Guid boundId = AssetDatabase::EnsureAssetIdentity(Path("project://Bound.txt"));
	const Guid renamedId = AssetDatabase::EnsureAssetIdentity(Path("project://Renamed.txt"));
	ASSERT_TRUE(boundId.IsValid() && renamedId.IsValid());

	Asset* probe = CreateAndLoadProbeAsset();
	ASSERT_NE(probe, nullptr);
	{
		Asset* target = AssetDatabase::LoadAssetByGuid(boundId);
		ASSERT_NE(target, nullptr);
		Variant boxed{Handle<TextAsset>(static_cast<TextAsset*>(target))};
		ASSERT_TRUE(SetField(probe, ProbeType(), StringHash("reference"), &boxed));
		AssetDatabase::SaveAsset(probe);
		AssetDatabase::UnloadAsset(target);
	}
	AssetDatabase::UnloadAsset(probe);
	ASSERT_EQ(AssetDatabase::CollectGarbage(), 2u);

	probe = AssetDatabase::LoadAsset(Path(ASSET_PATH));
	ASSERT_NE(probe, nullptr);

	Handle<Asset> bound(AssetDatabase::LoadAssetByGuid(boundId));
	Handle<Asset> renamed(AssetDatabase::LoadAssetByGuid(renamedId));
	ASSERT_TRUE(bound.IsValid() && renamed.IsValid());
	ASSERT_EQ(bound.Get()->GetRefCount(), 2);
	ASSERT_EQ(renamed.Get()->GetRefCount(), 1);

	{
		Variant boxed{Handle<TextAsset>(static_cast<TextAsset*>(renamed.Get()))};
		ASSERT_TRUE(SetField(probe, ProbeType(), StringHash("reference"), &boxed));
	}

	Handle<Asset> handle(probe);
	ASSERT_TRUE(ReloadTo(V2_DLL));
	Asset* after = handle.Get();
	ASSERT_NE(after, nullptr);

	Variant named;
	ASSERT_TRUE(GetField(after, ProbeType(), StringHash("reference"), &named));
	const Handle<TextAsset>* namedNow = named.TryGet<Handle<TextAsset>>();
	ASSERT_NE(namedNow, nullptr);
	ASSERT_EQ(namedNow->GetInstanceId(), renamed.GetInstanceId());

	AssetDatabase::UnloadAsset(after);
	EXPECT_EQ(AssetDatabase::CollectGarbage(), 1u);

	EXPECT_EQ(bound.Get()->GetRefCount(), 1);
	EXPECT_EQ(renamed.Get()->GetRefCount(), 1);

	AssetDatabase::UnloadAsset(bound.Get());
	AssetDatabase::UnloadAsset(renamed.Get());
	EXPECT_EQ(AssetDatabase::CollectGarbage(), 2u);
}
