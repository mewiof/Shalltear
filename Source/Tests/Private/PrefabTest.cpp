#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Math.h"
#include "Shalltear/Core/Scene.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace PfTestEnum {
	enum class Mode : u8 {
		Idle = 0,
		Run,
		Fly
	};
} // namespace PfTestEnum

SLT_REFLECT_ENUM(::PfTestEnum::Mode)
SLT_ENUM_VALUE(Idle)
SLT_ENUM_VALUE(Run)
SLT_ENUM_VALUE(Fly)
SLT_END_ENUM()

namespace {

	class PfModeComponent : public Component {
	public:
		PfTestEnum::Mode mode = PfTestEnum::Mode::Idle;

		SLT_REFLECT_COMPONENT(PfModeComponent, Component)
		SLT_REFLECT_FIELD(mode)
		SLT_END_REFLECT()

		PfModeComponent() = default;
	};

	SLT_REGISTER_TYPE(PfModeComponent)

	class PfStatsComponent : public Component {
	public:
		f32 power = 0.0f;
		f32 speed = 0.0f;
		str label = "";
		Handle<SceneObject> buddy;

		SLT_REFLECT_COMPONENT(PfStatsComponent, Component)
		SLT_REFLECT_FIELD(power)
		SLT_REFLECT_FIELD(speed)
		SLT_REFLECT_FIELD(label)
		SLT_REFLECT_FIELD(buddy)
		SLT_END_REFLECT()

		PfStatsComponent() = default;
	};

	SLT_REGISTER_TYPE(PfStatsComponent)

	class PfLifecycleComponent : public Component {
	public:
		static i32 attaches;
		static i32 enables;

		SLT_REFLECT_COMPONENT(PfLifecycleComponent, Component)
		SLT_END_REFLECT()

		PfLifecycleComponent() = default;

		void OnAttach() override { ++attaches; }
		void OnEnable() override { ++enables; }
	};

	i32 PfLifecycleComponent::attaches = 0;
	i32 PfLifecycleComponent::enables = 0;

	SLT_REGISTER_TYPE(PfLifecycleComponent)

	class PfAssetComponent : public Component {
	public:
		Handle<TextAsset> asset;

		SLT_REFLECT_COMPONENT(PfAssetComponent, Component)
		SLT_REFLECT_FIELD(asset)
		SLT_END_REFLECT()

		PfAssetComponent() = default;
	};

	SLT_REGISTER_TYPE(PfAssetComponent)

	str Hex(u64 value) {
		return std::format("{:016x}", value);
	}

	class PrefabFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		size_t _sceneBytesBaseline = 0;
		Handle<Scene> _scene;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_prefab_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();

			SceneSystem::Initialize();
			SceneSystem::Terminate();
			_sceneBytesBaseline = MemoryManager::GetBytesForTag(MemTag::Scene);
			SceneSystem::Initialize();
			_scene = SceneSystem::CreateScene("PrefabTest");
		}

		void TearDown() override {
			SceneSystem::Terminate();
			AssetDatabase::CollectGarbage();
			EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Scene), _sceneBytesBaseline);
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		Guid MakeBasePrefab(const str& path = "project://unit.prefab") {
			Handle<SceneObject> unit = SceneSystem::CreateObject("Unit", _scene);
			Handle<SceneObject> body = SceneSystem::CreateObject("Body", _scene, unit);
			Handle<PfStatsComponent> bodyStats = body->AddComponent<PfStatsComponent>();
			bodyStats->power = 1.0f;
			bodyStats->speed = 2.0f;
			bodyStats->label = "base";
			Handle<SceneObject> turret = SceneSystem::CreateObject("Turret", _scene, unit);
			Handle<PfStatsComponent> turretStats = turret->AddComponent<PfStatsComponent>();
			turretStats->power = 5.0f;

			Guid guid = SceneSystem::SavePrefab(unit, Path(path));
			EXPECT_TRUE(guid.IsValid());

			SceneSystem::Destroy(unit);
			SceneSystem::EndFrame();
			return guid;
		}

		Guid WritePrefabJSON(const str& virtualPath, const str& json) {
			FileSystem::WriteTextFile(FileSystem::Resolve(Path(virtualPath)), json);
			Asset* asset = AssetDatabase::LoadAsset(Path(virtualPath));
			EXPECT_NE(asset, nullptr);
			if (!asset) {
				return Guid{};
			}
			Guid guid = asset->assetId;
			AssetDatabase::UnloadAsset(asset);
			AssetDatabase::CollectGarbage();
			return guid;
		}

		Guid MakeFloatVariant(const str& virtualPath, const Guid& base, const Guid& targetFile,
							  u64 targetRowId, const char* fieldName, f32 value, const str& extra = "") {
			str json = "{\n\"_type\": \"Prefab\",\n\"formatVersion\": 1,\n\"engineHash\": \"0000000000000000\",\n"
					   "\"name\": \"Variant\",\n\"nextLocalId\": 100,\n\"prefab\": {\"base\": \"" +
					   base.ToString() + "\",\n\"overrides\": {\"properties\": [" +
					   FloatPropOp(targetFile, targetRowId, fieldName, value) + extra +
					   "]}},\n\"objects\": []\n}";
			return WritePrefabJSON(virtualPath, json);
		}

		static str FloatPropOp(const Guid& file, u64 rowId, const char* fieldName, f32 value) {
			return "{\"target\": {\"file\": \"" + file.ToString() + "\", \"id\": " + std::to_string(rowId) +
				   "}, \"path\": [\"" + Hex(StringHash(fieldName).GetValue()) + "\"], \"t\": \"" +
				   Hex(StringHash("f32").GetValue()) + "\", \"v\": " + std::format("{}", value) + "}";
		}

		static str StrPropOp(const Guid& file, u64 rowId, const char* fieldName, const str& value) {
			return "{\"target\": {\"file\": \"" + file.ToString() + "\", \"id\": " + std::to_string(rowId) +
				   "}, \"path\": [\"" + Hex(StringHash(fieldName).GetValue()) + "\"], \"t\": \"" +
				   Hex(StringHash("str").GetValue()) + "\", \"v\": \"" + value + "\"}";
		}
	};

	Handle<PfStatsComponent> BodyStatsOf(Handle<SceneObject> unit) {
		Handle<SceneObject> body = unit->FindChildByName("Body");
		return body.IsValid() ? body->GetComponent<PfStatsComponent>() : Handle<PfStatsComponent>();
	}

} // namespace

TEST_F(PrefabFixture, SaveThenInstantiateRestores) {
	Guid guid = MakeBasePrefab();

	Handle<SceneObject> instance = SceneSystem::Instantiate(guid, _scene);
	ASSERT_TRUE(instance.IsValid());
	EXPECT_EQ(instance->name, "Unit");
	ASSERT_EQ(instance->GetChildCount(), 2u);
	EXPECT_EQ(instance->GetChild(0)->name, "Body");
	EXPECT_EQ(instance->GetChild(1)->name, "Turret");

	Handle<PfStatsComponent> bodyStats = BodyStatsOf(instance);
	ASSERT_TRUE(bodyStats.IsValid());
	EXPECT_FLOAT_EQ(bodyStats->power, 1.0f);
	EXPECT_FLOAT_EQ(bodyStats->speed, 2.0f);
	EXPECT_EQ(bodyStats->label, "base");
	EXPECT_TRUE(bodyStats->IsEffectivelyEnabled());

	EXPECT_TRUE(SceneSystem::RevertInstanceOverrides(instance));

	Handle<SceneObject> plain = SceneSystem::CreateObject("Plain", _scene);
	EXPECT_FALSE(SceneSystem::RevertInstanceOverrides(plain));
}

TEST_F(PrefabFixture, InstantiatePlacementSurvivesAsOverride) {
	Guid guid = MakeBasePrefab();
	Handle<SceneObject> instance = SceneSystem::Instantiate(guid, _scene, {}, Vec3(7.0f, 0.0f, 0.0f));
	ASSERT_TRUE(instance.IsValid());
	EXPECT_TRUE(instance->GetLocalPosition() == Vec3(7.0f, 0.0f, 0.0f));

	ASSERT_TRUE(SceneSystem::SaveScene(_scene, Path("project://placed.scene")));
	EXPECT_TRUE(instance->GetLocalPosition() == Vec3(7.0f, 0.0f, 0.0f));
}

TEST_F(PrefabFixture, AllOverrideKindsSurviveSceneRoundTrip) {
	Guid guid = MakeBasePrefab();
	Handle<SceneObject> instance = SceneSystem::Instantiate(guid, _scene);
	ASSERT_TRUE(instance.IsValid());

	BodyStatsOf(instance)->power = 42.0f;
	SceneSystem::Destroy(instance->FindChildByName("Turret"));
	Handle<SceneObject> body = instance->FindChildByName("Body");
	Handle<SceneObject> antenna = SceneSystem::CreateObject("Antenna", _scene, body);
	antenna->AddComponent<PfStatsComponent>()->label = "added";
	Handle<PfStatsComponent> extra = instance->AddComponent<PfStatsComponent>();
	extra->label = "extra";
	SceneSystem::EndFrame();

	ASSERT_TRUE(SceneSystem::SaveScene(_scene, Path("project://overrides.scene")));
	SceneSystem::UnloadScene(_scene);
	SceneSystem::EndFrame();

	Handle<Scene> reloaded = SceneSystem::LoadScene(Path("project://overrides.scene"));
	ASSERT_TRUE(reloaded.IsValid());
	Handle<SceneObject> reloadedInstance = reloaded->FindObjectByName("Unit");
	ASSERT_TRUE(reloadedInstance.IsValid());

	EXPECT_FLOAT_EQ(BodyStatsOf(reloadedInstance)->power, 42.0f);
	EXPECT_FLOAT_EQ(BodyStatsOf(reloadedInstance)->speed, 2.0f);
	EXPECT_EQ(reloadedInstance->FindChildByName("Turret"), nullptr);
	Handle<SceneObject> reloadedAntenna = reloadedInstance->FindChildByName("Body")->FindChildByName("Antenna");
	ASSERT_TRUE(reloadedAntenna.IsValid());
	EXPECT_EQ(reloadedAntenna->GetComponent<PfStatsComponent>()->label, "added");
	Handle<PfStatsComponent> reloadedExtra = reloadedInstance->GetComponent<PfStatsComponent>();
	ASSERT_TRUE(reloadedExtra.IsValid());
	EXPECT_EQ(reloadedExtra->label, "extra");

	EXPECT_TRUE(SceneSystem::RevertInstanceOverrides(reloadedInstance));
	SceneSystem::EndFrame();
	EXPECT_TRUE(reloadedInstance->FindChildByName("Turret").IsValid());
	EXPECT_FLOAT_EQ(BodyStatsOf(reloadedInstance)->power, 1.0f);
}

TEST_F(PrefabFixture, VariantChainComposesLaterLayerWins) {
	Guid base = MakeBasePrefab();
	Guid v1 = MakeFloatVariant("project://v1.prefab", base, base, 3, "power", 10.0f);
	Guid v2 = MakeFloatVariant("project://v2.prefab", v1, base, 3, "power", 20.0f,
							   ", " + StrPropOp(base, 3, "label", "v2"));
	Guid v3 = MakeFloatVariant("project://v3.prefab", v2, base, 5, "power", 99.0f);
	ASSERT_TRUE(v1.IsValid() && v2.IsValid() && v3.IsValid());

	Handle<SceneObject> fromV1 = SceneSystem::Instantiate(v1, _scene);
	ASSERT_TRUE(fromV1.IsValid());
	EXPECT_FLOAT_EQ(BodyStatsOf(fromV1)->power, 10.0f);
	EXPECT_EQ(BodyStatsOf(fromV1)->label, "base");

	Handle<SceneObject> fromV3 = SceneSystem::Instantiate(v3, _scene);
	ASSERT_TRUE(fromV3.IsValid());
	EXPECT_FLOAT_EQ(BodyStatsOf(fromV3)->power, 20.0f);
	EXPECT_EQ(BodyStatsOf(fromV3)->label, "v2");
	EXPECT_FLOAT_EQ(fromV3->FindChildByName("Turret")->GetComponent<PfStatsComponent>()->power, 99.0f);
	EXPECT_FLOAT_EQ(BodyStatsOf(fromV3)->speed, 2.0f);
}

TEST_F(PrefabFixture, BaseEditPropagatesThroughChain) {
	Guid base = MakeBasePrefab();
	Guid v1 = MakeFloatVariant("project://v1.prefab", base, base, 3, "power", 10.0f);
	Guid v2 = MakeFloatVariant("project://v2.prefab", v1, base, 3, "power", 20.0f);
	Guid v3 = MakeFloatVariant("project://v3.prefab", v2, base, 5, "power", 99.0f);

	Handle<SceneObject> baseInstance = SceneSystem::Instantiate(base, _scene);
	Handle<SceneObject> v3Instance = SceneSystem::Instantiate(v3, _scene);
	ASSERT_TRUE(baseInstance.IsValid() && v3Instance.IsValid());

	BodyStatsOf(v3Instance)->label = "mine";

	Handle<SceneObject> v3Body = v3Instance->FindChildByName("Body");
	u64 bodyId = v3Body.GetInstanceId();
	u64 statsId = BodyStatsOf(v3Instance).GetInstanceId();
	Handle<PfStatsComponent> externalHandle = BodyStatsOf(v3Instance);

	BodyStatsOf(baseInstance)->speed = 77.0f;
	ASSERT_TRUE(SceneSystem::ApplyInstanceOverridesToBase(baseInstance));
	SceneSystem::EndFrame();

	EXPECT_FLOAT_EQ(BodyStatsOf(v3Instance)->speed, 77.0f);
	EXPECT_FLOAT_EQ(BodyStatsOf(v3Instance)->power, 20.0f);
	EXPECT_EQ(BodyStatsOf(v3Instance)->label, "mine");
	EXPECT_EQ(v3Instance->FindChildByName("Body").GetInstanceId(), bodyId);
	EXPECT_EQ(BodyStatsOf(v3Instance).GetInstanceId(), statsId);
	EXPECT_TRUE(externalHandle.IsValid());
	EXPECT_FLOAT_EQ(externalHandle->speed, 77.0f);

	EXPECT_FLOAT_EQ(BodyStatsOf(baseInstance)->speed, 77.0f);
}

TEST_F(PrefabFixture, BaseRemoveKeepsAdditions) {
	Guid base = MakeBasePrefab();
	Handle<SceneObject> editorInstance = SceneSystem::Instantiate(base, _scene);
	Handle<SceneObject> userInstance = SceneSystem::Instantiate(base, _scene);
	ASSERT_TRUE(editorInstance.IsValid() && userInstance.IsValid());

	Handle<SceneObject> turret = userInstance->FindChildByName("Turret");
	Handle<SceneObject> antenna = SceneSystem::CreateObject("Antenna", _scene, turret);

	SceneSystem::Destroy(editorInstance->FindChildByName("Body"));
	SceneSystem::EndFrame();
	ASSERT_TRUE(SceneSystem::ApplyInstanceOverridesToBase(editorInstance));
	SceneSystem::EndFrame();

	EXPECT_EQ(userInstance->FindChildByName("Body"), nullptr);
	ASSERT_TRUE(antenna.IsValid());
	EXPECT_EQ(userInstance->FindChildByName("Turret")->FindChildByName("Antenna"), antenna);
}

TEST_F(PrefabFixture, ApplyPropagatesToSiblingAndCollapsesLayer) {
	Guid base = MakeBasePrefab();
	Handle<SceneObject> first = SceneSystem::Instantiate(base, _scene);
	Handle<SceneObject> second = SceneSystem::Instantiate(base, _scene);

	BodyStatsOf(first)->power = 123.0f;
	ASSERT_TRUE(SceneSystem::ApplyInstanceOverridesToBase(first));
	SceneSystem::EndFrame();

	EXPECT_FLOAT_EQ(BodyStatsOf(second)->power, 123.0f);

	ASSERT_TRUE(SceneSystem::RevertInstanceOverrides(first));
	SceneSystem::EndFrame();
	EXPECT_FLOAT_EQ(BodyStatsOf(first)->power, 123.0f);

	Handle<SceneObject> third = SceneSystem::Instantiate(base, _scene);
	EXPECT_FLOAT_EQ(BodyStatsOf(third)->power, 123.0f);
}

TEST_F(PrefabFixture, RevertDropsLiveOverrides) {
	Guid base = MakeBasePrefab();
	Handle<SceneObject> instance = SceneSystem::Instantiate(base, _scene);

	BodyStatsOf(instance)->power = 55.0f;
	BodyStatsOf(instance)->label = "changed";
	ASSERT_TRUE(SceneSystem::RevertInstanceOverrides(instance));
	SceneSystem::EndFrame();

	EXPECT_FLOAT_EQ(BodyStatsOf(instance)->power, 1.0f);
	EXPECT_EQ(BodyStatsOf(instance)->label, "base");
}

TEST_F(PrefabFixture, CloneOfInstanceKeepsLinkAndOverrides) {
	Guid base = MakeBasePrefab();
	Handle<SceneObject> source = SceneSystem::Instantiate(base, _scene);
	BodyStatsOf(source)->power = 8.0f;

	Handle<SceneObject> clone = SceneSystem::Instantiate(source);
	ASSERT_TRUE(clone.IsValid());
	EXPECT_NE(clone.GetInstanceId(), source.GetInstanceId());
	EXPECT_FLOAT_EQ(BodyStatsOf(clone)->power, 8.0f);

	ASSERT_TRUE(SceneSystem::RevertInstanceOverrides(clone));
	SceneSystem::EndFrame();
	EXPECT_FLOAT_EQ(BodyStatsOf(clone)->power, 1.0f);
	EXPECT_FLOAT_EQ(BodyStatsOf(source)->power, 8.0f);
}

TEST_F(PrefabFixture, CloneOfPlainSubtreeRemapsInternalHandles) {
	Handle<SceneObject> root = SceneSystem::CreateObject("CloneRoot", _scene);
	Handle<SceneObject> child = SceneSystem::CreateObject("Child", _scene, root);
	Handle<PfStatsComponent> stats = root->AddComponent<PfStatsComponent>();
	stats->buddy = child;
	stats->power = 3.0f;

	Handle<SceneObject> clone = SceneSystem::Instantiate(root);
	ASSERT_TRUE(clone.IsValid());
	Handle<PfStatsComponent> cloneStats = clone->GetComponent<PfStatsComponent>();
	ASSERT_TRUE(cloneStats.IsValid());
	EXPECT_FLOAT_EQ(cloneStats->power, 3.0f);

	Handle<SceneObject> cloneChild = clone->FindChildByName("Child");
	ASSERT_TRUE(cloneChild.IsValid());
	EXPECT_EQ(cloneStats->buddy.GetInstanceId(), cloneChild.GetInstanceId());
	EXPECT_NE(cloneStats->buddy.GetInstanceId(), child.GetInstanceId());
}

TEST_F(PrefabFixture, DetachedInstanceHasContentsAndNoScene) {
	Guid guid = MakeBasePrefab();
	size_t scenesBefore = SceneSystem::GetLoadedScenes().size();

	Handle<SceneObject> root = SceneSystem::InstantiateDetached(guid);
	ASSERT_TRUE(root.IsValid());

	EXPECT_EQ(root->name, "Unit");
	ASSERT_EQ(root->GetChildCount(), 2u);
	EXPECT_EQ(root->GetChild(0)->name, "Body");
	ASSERT_TRUE(BodyStatsOf(root).IsValid());
	EXPECT_FLOAT_EQ(BodyStatsOf(root)->power, 1.0f);
	EXPECT_EQ(BodyStatsOf(root)->label, "base");

	EXPECT_EQ(SceneSystem::GetLoadedScenes().size(), scenesBefore);
	EXPECT_FALSE(root->GetScene().IsValid());
	u32 objectsInScene = 0;
	_scene->ForEachObject([](SceneObject&, void* user) { ++*static_cast<u32*>(user); }, &objectsInScene);
	EXPECT_EQ(objectsInScene, 0u);

	SceneSystem::DestroyDetached(root);
}

TEST_F(PrefabFixture, ADetachedInstanceNeverActivates) {
	Handle<SceneObject> authoring = SceneSystem::CreateObject("Lit", _scene);
	authoring->AddComponent<PfLifecycleComponent>();
	Guid guid = SceneSystem::SavePrefab(authoring, Path("project://lit.prefab"));
	ASSERT_TRUE(guid.IsValid());
	SceneSystem::Destroy(authoring);
	SceneSystem::EndFrame();

	PfLifecycleComponent::attaches = 0;
	PfLifecycleComponent::enables = 0;
	Handle<SceneObject> detached = SceneSystem::InstantiateDetached(guid);
	ASSERT_TRUE(detached.IsValid());
	EXPECT_EQ(PfLifecycleComponent::attaches, 0);
	EXPECT_EQ(PfLifecycleComponent::enables, 0);

	Handle<SceneObject> placed = SceneSystem::Instantiate(guid, _scene);
	ASSERT_TRUE(placed.IsValid());
	EXPECT_EQ(PfLifecycleComponent::attaches, 1);
	EXPECT_EQ(PfLifecycleComponent::enables, 1);

	SceneSystem::DestroyDetached(detached);
}

TEST_F(PrefabFixture, DetachedInstancesIndependent) {
	Guid guid = MakeBasePrefab();

	Handle<SceneObject> first = SceneSystem::InstantiateDetached(guid);
	Handle<SceneObject> second = SceneSystem::InstantiateDetached(guid);
	ASSERT_TRUE(first.IsValid());
	ASSERT_TRUE(second.IsValid());
	EXPECT_NE(first.GetInstanceId(), second.GetInstanceId());

	BodyStatsOf(first)->power = 99.0f;
	EXPECT_FLOAT_EQ(BodyStatsOf(second)->power, 1.0f);

	SceneSystem::DestroyDetached(first);
	EXPECT_FALSE(first.IsValid());
	ASSERT_TRUE(second.IsValid());
	EXPECT_FLOAT_EQ(BodyStatsOf(second)->power, 1.0f);
	SceneSystem::DestroyDetached(second);
}

TEST_F(PrefabFixture, DestroyDetachedFreesGraph) {
	Guid guid = MakeBasePrefab();
	Handle<SceneObject> root = SceneSystem::InstantiateDetached(guid);
	ASSERT_TRUE(root.IsValid());
	Handle<SceneObject> child = root->FindChildByName("Body");
	Handle<PfStatsComponent> stats = BodyStatsOf(root);
	ASSERT_TRUE(child.IsValid());
	ASSERT_TRUE(stats.IsValid());

	SceneSystem::DestroyDetached(root);

	EXPECT_FALSE(root.IsValid());
	EXPECT_FALSE(child.IsValid());
	EXPECT_FALSE(stats.IsValid());
}

TEST_F(PrefabFixture, DetachedApplyReachesInstances) {
	Guid guid = MakeBasePrefab();
	Handle<SceneObject> instance = SceneSystem::Instantiate(guid, _scene);
	ASSERT_TRUE(instance.IsValid());
	BodyStatsOf(instance)->label = "mine";

	Handle<SceneObject> detached = SceneSystem::InstantiateDetached(guid);
	ASSERT_TRUE(detached.IsValid());
	BodyStatsOf(detached)->speed = 77.0f;
	ASSERT_TRUE(SceneSystem::ApplyDetachedToPrefab(detached, guid));
	SceneSystem::DestroyDetached(detached);
	SceneSystem::EndFrame();

	EXPECT_FLOAT_EQ(BodyStatsOf(instance)->speed, 77.0f);
	EXPECT_EQ(BodyStatsOf(instance)->label, "mine");

	Handle<SceneObject> fresh = SceneSystem::Instantiate(guid, _scene);
	ASSERT_TRUE(fresh.IsValid());
	EXPECT_FLOAT_EQ(BodyStatsOf(fresh)->speed, 77.0f);
}

TEST_F(PrefabFixture, ApplyingDetachedVariantKeepsBaseLink) {
	Guid base = MakeBasePrefab();
	Guid variant = MakeFloatVariant("project://variant.prefab", base, base, 3, "power", 10.0f);

	Handle<SceneObject> detached = SceneSystem::InstantiateDetached(variant);
	ASSERT_TRUE(detached.IsValid());
	EXPECT_FLOAT_EQ(BodyStatsOf(detached)->power, 10.0f);
	BodyStatsOf(detached)->label = "edited";
	ASSERT_TRUE(SceneSystem::ApplyDetachedToPrefab(detached, variant));
	SceneSystem::DestroyDetached(detached);

	Handle<SceneObject> fromVariant = SceneSystem::Instantiate(variant, _scene);
	ASSERT_TRUE(fromVariant.IsValid());
	EXPECT_EQ(BodyStatsOf(fromVariant)->label, "edited");
	EXPECT_FLOAT_EQ(BodyStatsOf(fromVariant)->power, 10.0f);

	Handle<SceneObject> baseInstance = SceneSystem::Instantiate(base, _scene);
	ASSERT_TRUE(baseInstance.IsValid());
	BodyStatsOf(baseInstance)->speed = 55.0f;
	ASSERT_TRUE(SceneSystem::ApplyInstanceOverridesToBase(baseInstance));
	SceneSystem::EndFrame();
	EXPECT_FLOAT_EQ(BodyStatsOf(fromVariant)->speed, 55.0f);
}

TEST_F(PrefabFixture, ApplyDetachedRefusesSceneObjects) {
	Guid guid = MakeBasePrefab();
	Handle<SceneObject> placed = SceneSystem::Instantiate(guid, _scene);
	ASSERT_TRUE(placed.IsValid());
	BodyStatsOf(placed)->speed = 12.0f;

	EXPECT_FALSE(SceneSystem::ApplyDetachedToPrefab(placed, guid));

	Handle<SceneObject> fresh = SceneSystem::InstantiateDetached(guid);
	ASSERT_TRUE(fresh.IsValid());
	EXPECT_FLOAT_EQ(BodyStatsOf(fresh)->speed, 2.0f);
	SceneSystem::DestroyDetached(fresh);
}

TEST_F(PrefabFixture, DestroyDetachedRefusesSceneObjects) {
	Guid guid = MakeBasePrefab();
	Handle<SceneObject> placed = SceneSystem::Instantiate(guid, _scene);
	ASSERT_TRUE(placed.IsValid());
	Handle<SceneObject> child = placed->FindChildByName("Body");
	ASSERT_TRUE(child.IsValid());

	SceneSystem::DestroyDetached(placed);
	SceneSystem::DestroyDetached(child);
	SceneSystem::EndFrame();

	EXPECT_TRUE(placed.IsValid());
	EXPECT_TRUE(child.IsValid());
	EXPECT_EQ(placed->GetChildCount(), 2u);
}

TEST_F(PrefabFixture, NestedInstanceHopTargeting) {
	Guid inner = MakeBasePrefab("project://inner.prefab");

	str outerJSON = "{\n\"_type\": \"Prefab\",\n\"formatVersion\": 1,\n\"engineHash\": \"0000000000000000\",\n"
					"\"name\": \"Outer\",\n\"nextLocalId\": 10,\n\"prefab\": {\"base\": \"" +
					Guid{}.ToString() + "\", \"overrides\": {}},\n\"objects\": [\n"
										"{\"id\": 1, \"parent\": 0, \"type\": \"" +
					Hex(SceneObject::GetReflectType()->name.GetValue()) +
					"\", \"fields\": {}, \"components\": []},\n"
					"{\"id\": 2, \"kind\": \"instance\", \"parent\": 1, \"prefab\": \"" +
					inner.ToString() + "\", \"overrides\": {}}\n]}";
	Guid outer = WritePrefabJSON("project://outer.prefab", outerJSON);
	ASSERT_TRUE(outer.IsValid());

	str variantJSON = "{\n\"_type\": \"Prefab\",\n\"formatVersion\": 1,\n\"engineHash\": \"0000000000000000\",\n"
					  "\"name\": \"OuterV\",\n\"nextLocalId\": 10,\n\"prefab\": {\"base\": \"" +
					  outer.ToString() + "\",\n\"overrides\": {\"properties\": [" +
					  "{\"target\": {\"hops\": [{\"file\": \"" + outer.ToString() + "\", \"id\": 2}], \"file\": \"" +
					  inner.ToString() + "\", \"id\": 3}, \"path\": [\"" + Hex(StringHash("power").GetValue()) +
					  "\"], \"t\": \"" + Hex(StringHash("f32").GetValue()) + "\", \"v\": 444}" +
					  "]}},\n\"objects\": []\n}";
	Guid outerVariant = WritePrefabJSON("project://outer_v.prefab", variantJSON);
	ASSERT_TRUE(outerVariant.IsValid());

	Handle<SceneObject> composed = SceneSystem::Instantiate(outerVariant, _scene);
	ASSERT_TRUE(composed.IsValid());
	Handle<SceneObject> nestedUnit = composed->FindChildByName("Unit");
	ASSERT_TRUE(nestedUnit.IsValid());
	EXPECT_FLOAT_EQ(BodyStatsOf(nestedUnit)->power, 444.0f);
	EXPECT_FLOAT_EQ(BodyStatsOf(nestedUnit)->speed, 2.0f);
}

TEST_F(PrefabFixture, VariantCycleIsDetected) {
	str stubJSON = "{\"_type\": \"Prefab\", \"formatVersion\": 1, \"engineHash\": \"0000000000000000\", "
				   "\"name\": \"A\", \"nextLocalId\": 2, \"prefab\": {\"base\": \"" +
				   Guid{}.ToString() + "\", \"overrides\": {}}, \"objects\": ["
									   "{\"id\": 1, \"parent\": 0, \"type\": \"" +
				   Hex(SceneObject::GetReflectType()->name.GetValue()) + "\", \"fields\": {}, \"components\": []}]}";
	Guid a = WritePrefabJSON("project://cycle_a.prefab", stubJSON);
	str bJSON = stubJSON;
	bJSON.replace(bJSON.find(Guid{}.ToString()), 33, a.ToString());
	Guid b = WritePrefabJSON("project://cycle_b.prefab", bJSON);
	str aJSON = stubJSON;
	aJSON.replace(aJSON.find(Guid{}.ToString()), 33, b.ToString());
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://cycle_a.prefab")), aJSON);

	u32 objectsBefore = _scene->GetObjectCount();
	Handle<SceneObject> composed = SceneSystem::Instantiate(a, _scene);
	EXPECT_FALSE(composed.IsValid());
	EXPECT_EQ(_scene->GetObjectCount(), objectsBefore);
}

TEST_F(PrefabFixture, NestedInstanceCycleIsDetected) {
	str stubJSON = "{\"_type\": \"Prefab\", \"formatVersion\": 1, \"engineHash\": \"0000000000000000\", "
				   "\"name\": \"Selfie\", \"nextLocalId\": 3, \"prefab\": {\"base\": \"" +
				   Guid{}.ToString() + "\", \"overrides\": {}}, \"objects\": ["
									   "{\"id\": 1, \"parent\": 0, \"type\": \"" +
				   Hex(SceneObject::GetReflectType()->name.GetValue()) + "\", \"fields\": {}, \"components\": []}]}";
	Guid selfie = WritePrefabJSON("project://selfie.prefab", stubJSON);

	str selfJSON = "{\"_type\": \"Prefab\", \"formatVersion\": 1, \"engineHash\": \"0000000000000000\", "
				   "\"name\": \"Selfie\", \"nextLocalId\": 3, \"prefab\": {\"base\": \"" +
				   Guid{}.ToString() + "\", \"overrides\": {}}, \"objects\": ["
									   "{\"id\": 1, \"parent\": 0, \"type\": \"" +
				   Hex(SceneObject::GetReflectType()->name.GetValue()) + "\", \"fields\": {}, \"components\": []},"
																		 "{\"id\": 2, \"kind\": \"instance\", \"parent\": 1, \"prefab\": \"" +
				   selfie.ToString() + "\", \"overrides\": {}}]}";
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://selfie.prefab")), selfJSON);

	Handle<SceneObject> composed = SceneSystem::Instantiate(selfie, _scene);
	ASSERT_TRUE(composed.IsValid());
	EXPECT_EQ(composed->GetChildCount(), 0u);
}

TEST_F(PrefabFixture, InstantiatedPrefabAssetHandleResolvesAfterRestart) {
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://loot.txt")), "gold");
	TextAsset* text = static_cast<TextAsset*>(AssetDatabase::LoadAsset(Path("project://loot.txt")));
	ASSERT_NE(text, nullptr);
	Guid assetId = text->assetId;

	Handle<SceneObject> unit = SceneSystem::CreateObject("Unit", _scene);
	unit->AddComponent<PfAssetComponent>()->asset = Handle<TextAsset>(text);
	Guid prefabGuid = SceneSystem::SavePrefab(unit, Path("project://ref.prefab"));
	ASSERT_TRUE(prefabGuid.IsValid());
	SceneSystem::Destroy(unit);
	SceneSystem::EndFrame();

	AssetDatabase::UnloadAsset(text);
	SceneSystem::Terminate();
	AssetDatabase::Terminate();
	AssetDatabase::Initialize();
	AssetDatabase::ScanProjectDirectory();
	SceneSystem::Initialize();
	Handle<Scene> scene = SceneSystem::CreateScene("AfterRestart");

	Handle<SceneObject> instance = SceneSystem::Instantiate(prefabGuid, scene);
	ASSERT_TRUE(instance.IsValid());
	Handle<PfAssetComponent> restored = instance->GetComponent<PfAssetComponent>();
	ASSERT_TRUE(restored.IsValid());
	ASSERT_TRUE(restored->asset.IsValid());
	EXPECT_EQ(restored->asset->assetId, assetId);
}

namespace {

	const StringHash PfVanished{"PfVanishedComponent"};

	StringHash PfTypeAt(const Handle<SceneObject>& object, u32 index) {
		Component* component = object->GetComponentAt(index).Get();
		return component && component->GetType() ? component->GetType()->name : StringHash{};
	}

} // namespace

TEST_F(PrefabFixture, UnresolvableComponentKeepsSlot) {
	str objectType = Hex(SceneObject::GetReflectType()->name.GetValue());
	str statsType = Hex(PfStatsComponent::GetReflectType()->name.GetValue());
	str json = "{\n\"_type\": \"Prefab\",\n\"formatVersion\": 1,\n\"engineHash\": \"0000000000000000\",\n"
			   "\"name\": \"Holed\",\n\"nextLocalId\": 10,\n\"prefab\": {\"base\": \"" +
			   Guid{}.ToString() +
			   "\",\n\"overrides\": {}},\n\"objects\": [\n"
			   "{\"id\": 1, \"parent\": 0, \"type\": \"" +
			   objectType + "\", \"fields\": {\"_type\": \"" + objectType + "\", \"" +
			   Hex(StringHash("name").GetValue()) +
			   "\": \"Holed\"}, \"components\": [\n"
			   "  {\"id\": 2, \"type\": \"" +
			   statsType + "\", \"fields\": {\"_type\": \"" + statsType + "\", \"" +
			   Hex(StringHash("power").GetValue()) +
			   "\": 3.0}},\n"
			   "  {\"id\": 3, \"type\": \"" +
			   Hex(PfVanished.GetValue()) +
			   "\", \"fields\": {\"_type\": \"0000000000000000\", \"cafef00dcafef00d\": 9}},\n"
			   "  {\"id\": 4, \"type\": \"" +
			   statsType + "\", \"fields\": {\"_type\": \"" + statsType + "\", \"" +
			   Hex(StringHash("speed").GetValue()) +
			   "\": 4.0}}\n"
			   "]}\n]\n}";
	Guid guid = WritePrefabJSON("project://holed.prefab", json);
	ASSERT_TRUE(guid.IsValid());

	Handle<SceneObject> instance = SceneSystem::Instantiate(guid, _scene);
	ASSERT_TRUE(instance.IsValid());
	ASSERT_EQ(instance->GetComponentCount(), 3u);
	EXPECT_EQ(PfTypeAt(instance, 0), StringHash("PfStatsComponent"));
	EXPECT_EQ(PfTypeAt(instance, 1), StringHash("UnresolvedComponent"));
	EXPECT_EQ(PfTypeAt(instance, 2), StringHash("PfStatsComponent"));

	auto* first = static_cast<PfStatsComponent*>(instance->GetComponentAt(0).Get());
	auto* third = static_cast<PfStatsComponent*>(instance->GetComponentAt(2).Get());
	ASSERT_NE(first, nullptr);
	ASSERT_NE(third, nullptr);
	EXPECT_FLOAT_EQ(first->power, 3.0f);
	EXPECT_FLOAT_EQ(third->speed, 4.0f);

	auto* preserved = static_cast<UnresolvedComponent*>(instance->GetComponentAt(1).Get());
	ASSERT_NE(preserved, nullptr);
	EXPECT_EQ(preserved->GetUnresolvedType(), PfVanished);
}

TEST_F(PrefabFixture, ApplyKeepsPreservedRows) {
	str objectType = Hex(SceneObject::GetReflectType()->name.GetValue());
	str statsType = Hex(PfStatsComponent::GetReflectType()->name.GetValue());
	str json = "{\n\"_type\": \"Prefab\",\n\"formatVersion\": 1,\n\"engineHash\": \"0000000000000000\",\n"
			   "\"name\": \"Holed\",\n\"nextLocalId\": 10,\n\"prefab\": {\"base\": \"" +
			   Guid{}.ToString() +
			   "\",\n\"overrides\": {}},\n\"objects\": [\n"
			   "{\"id\": 1, \"parent\": 0, \"type\": \"" +
			   objectType + "\", \"fields\": {\"_type\": \"" + objectType + "\", \"" +
			   Hex(StringHash("name").GetValue()) +
			   "\": \"Holed\"}, \"components\": [\n"
			   "  {\"id\": 2, \"type\": \"" +
			   statsType + "\", \"fields\": {\"_type\": \"" + statsType + "\", \"" +
			   Hex(StringHash("power").GetValue()) +
			   "\": 3.0}},\n"
			   "  {\"id\": 3, \"type\": \"" +
			   Hex(PfVanished.GetValue()) +
			   "\", \"fields\": {\"_type\": \"0000000000000000\", \"cafef00dcafef00d\": 9}}\n"
			   "]}\n]\n}";
	Guid guid = WritePrefabJSON("project://holed2.prefab", json);
	ASSERT_TRUE(guid.IsValid());

	Handle<SceneObject> instance = SceneSystem::Instantiate(guid, _scene);
	ASSERT_TRUE(instance.IsValid());
	ASSERT_EQ(instance->GetComponentCount(), 2u);

	auto* stats = static_cast<PfStatsComponent*>(instance->GetComponentAt(0).Get());
	ASSERT_NE(stats, nullptr);
	stats->power = 42.0f;
	ASSERT_TRUE(SceneSystem::ApplyInstanceOverridesToBase(instance));

	str written = FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://holed2.prefab")), MemTag::Scene);
	EXPECT_NE(written.find(Hex(PfVanished.GetValue())), str::npos);
	EXPECT_EQ(written.find("\"removedComponents\": [{"), str::npos);

	Handle<SceneObject> second = SceneSystem::Instantiate(guid, _scene);
	ASSERT_TRUE(second.IsValid());
	ASSERT_EQ(second->GetComponentCount(), 2u);
	EXPECT_EQ(PfTypeAt(second, 1), StringHash("UnresolvedComponent"));
	EXPECT_FLOAT_EQ(static_cast<PfStatsComponent*>(second->GetComponentAt(0).Get())->power, 42.0f);
}

TEST_F(PrefabFixture, SavePrefabRoundTripsAPreservedRow) {
	str objectType = Hex(SceneObject::GetReflectType()->name.GetValue());
	str sceneJSON = "{\n\"_type\": \"Scene\",\n\"formatVersion\": 1,\n\"engineHash\": \"0000000000000000\",\n"
					"\"name\": \"Holder\",\n\"nextLocalId\": 10,\n\"objects\": [\n"
					"{\"id\": 1, \"parent\": 0, \"type\": \"" +
					objectType + "\", \"fields\": {\"_type\": \"" + objectType + "\", \"" +
					Hex(StringHash("name").GetValue()) +
					"\": \"Subject\"}, \"components\": [\n"
					"  {\"id\": 2, \"type\": \"" +
					Hex(PfVanished.GetValue()) +
					"\", \"fields\": {\"_type\": \"0000000000000000\", \"cafef00dcafef00d\": 5}}\n"
					"]}\n]\n}";
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://holder.scene")), sceneJSON);

	Handle<Scene> loaded = SceneSystem::LoadScene(Path("project://holder.scene"));
	ASSERT_TRUE(loaded.IsValid());
	Handle<SceneObject> subject = loaded->FindObjectByName("Subject");
	ASSERT_TRUE(subject.IsValid());
	ASSERT_EQ(subject->GetComponentCount(), 1u);

	Guid guid = SceneSystem::SavePrefab(subject, Path("project://fromlive.prefab"));
	ASSERT_TRUE(guid.IsValid());
	str written = FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://fromlive.prefab")), MemTag::Scene);
	EXPECT_NE(written.find(Hex(PfVanished.GetValue())), str::npos);

	ASSERT_EQ(subject->GetComponentCount(), 1u);
	EXPECT_EQ(PfTypeAt(subject, 0), StringHash("UnresolvedComponent"));

	Handle<SceneObject> instance = SceneSystem::Instantiate(guid, _scene);
	ASSERT_TRUE(instance.IsValid());
	ASSERT_EQ(instance->GetComponentCount(), 1u);
	EXPECT_EQ(static_cast<UnresolvedComponent*>(instance->GetComponentAt(0).Get())->GetUnresolvedType(), PfVanished);
}

TEST_F(PrefabFixture, PropagationKeepsPreservedRowsOnLiveInstances) {
	str objectType = Hex(SceneObject::GetReflectType()->name.GetValue());
	str statsType = Hex(PfStatsComponent::GetReflectType()->name.GetValue());
	str json = "{\n\"_type\": \"Prefab\",\n\"formatVersion\": 1,\n\"engineHash\": \"0000000000000000\",\n"
			   "\"name\": \"Holed\",\n\"nextLocalId\": 10,\n\"prefab\": {\"base\": \"" +
			   Guid{}.ToString() +
			   "\",\n\"overrides\": {}},\n\"objects\": [\n"
			   "{\"id\": 1, \"parent\": 0, \"type\": \"" +
			   objectType + "\", \"fields\": {\"_type\": \"" + objectType + "\", \"" +
			   Hex(StringHash("name").GetValue()) +
			   "\": \"Holed\"}, \"components\": [\n"
			   "  {\"id\": 2, \"type\": \"" +
			   statsType + "\", \"fields\": {\"_type\": \"" + statsType + "\"}},\n"
																		  "  {\"id\": 3, \"type\": \"" +
			   Hex(PfVanished.GetValue()) +
			   "\", \"fields\": {\"_type\": \"0000000000000000\", \"cafef00dcafef00d\": 9}}\n"
			   "]}\n]\n}";
	Guid guid = WritePrefabJSON("project://holed3.prefab", json);
	ASSERT_TRUE(guid.IsValid());

	Handle<SceneObject> a = SceneSystem::Instantiate(guid, _scene);
	Handle<SceneObject> b = SceneSystem::Instantiate(guid, _scene);
	ASSERT_TRUE(a.IsValid());
	ASSERT_TRUE(b.IsValid());
	ASSERT_EQ(a->GetComponentCount(), 2u);
	ASSERT_EQ(b->GetComponentCount(), 2u);

	SceneSystem::NotifyPrefabChanged(guid);
	SceneSystem::EndFrame();

	ASSERT_TRUE(a.IsValid());
	ASSERT_TRUE(b.IsValid());
	ASSERT_EQ(a->GetComponentCount(), 2u);
	ASSERT_EQ(b->GetComponentCount(), 2u);
	EXPECT_EQ(PfTypeAt(a, 1), StringHash("UnresolvedComponent"));
	EXPECT_EQ(PfTypeAt(b, 1), StringHash("UnresolvedComponent"));
	EXPECT_EQ(static_cast<UnresolvedComponent*>(a->GetComponentAt(1).Get())->GetUnresolvedType(), PfVanished);
}

TEST_F(PrefabFixture, VariantInheritsBase) {
	Guid base = MakeBasePrefab();
	Guid variant = SceneSystem::CreatePrefabVariant(base, Path("project://unit_variant.prefab"));
	ASSERT_TRUE(variant.IsValid());
	EXPECT_NE(variant, base);
	EXPECT_EQ(AssetDatabase::LookupGuid(Path("project://unit_variant.prefab")), variant);

	PrefabAsset* asset = static_cast<PrefabAsset*>(AssetDatabase::LoadAssetByGuid(variant));
	ASSERT_NE(asset, nullptr);
	EXPECT_EQ(asset->GetBasePrefab(), base);
	EXPECT_TRUE(asset->GetOverrides().IsEmpty());
	EXPECT_EQ(asset->GetKind(), PrefabKind::Regular);
	AssetDatabase::UnloadAsset(asset);

	Handle<SceneObject> fromVariant = SceneSystem::Instantiate(variant, _scene);
	ASSERT_TRUE(fromVariant.IsValid());
	EXPECT_EQ(fromVariant->name, "Unit");
	EXPECT_FLOAT_EQ(BodyStatsOf(fromVariant)->power, 1.0f);

	Handle<SceneObject> fromBase = SceneSystem::Instantiate(base, _scene);
	BodyStatsOf(fromBase)->power = 77.0f;
	ASSERT_TRUE(SceneSystem::ApplyInstanceOverridesToBase(fromBase));
	SceneSystem::EndFrame();
	EXPECT_FLOAT_EQ(BodyStatsOf(fromVariant)->power, 77.0f);
}

TEST_F(PrefabFixture, VariantChainsOrRefuses) {
	Guid base = MakeBasePrefab();
	Guid v1 = SceneSystem::CreatePrefabVariant(base, Path("project://v1.prefab"));
	Guid v2 = SceneSystem::CreatePrefabVariant(v1, Path("project://v2.prefab"));
	ASSERT_TRUE(v1.IsValid() && v2.IsValid());
	Handle<SceneObject> fromV2 = SceneSystem::Instantiate(v2, _scene);
	ASSERT_TRUE(fromV2.IsValid());
	EXPECT_EQ(BodyStatsOf(fromV2)->label, "base");

	ASSERT_TRUE(SceneSystem::SaveScene(_scene, Path("project://world.scene")));
	Guid sceneGuid = AssetDatabase::LookupGuid(Path("project://world.scene"));
	ASSERT_TRUE(sceneGuid.IsValid());
	EXPECT_FALSE(SceneSystem::CreatePrefabVariant(sceneGuid, Path("project://bad.prefab")).IsValid());
	EXPECT_FALSE(SceneSystem::CreatePrefabVariant(Guid{}, Path("project://bad.prefab")).IsValid());
	EXPECT_FALSE(std::filesystem::exists(_root / "bad.prefab"));

	EXPECT_FALSE(SceneSystem::CreatePrefabVariant(v2, Path("project://unit.prefab")).IsValid());
	Handle<SceneObject> again = SceneSystem::Instantiate(v2, _scene);
	ASSERT_TRUE(again.IsValid());
	EXPECT_FLOAT_EQ(BodyStatsOf(again)->power, 1.0f);
}

TEST_F(PrefabFixture, SaveVariantPromotesLayer) {
	Guid base = MakeBasePrefab();
	Handle<SceneObject> instance = SceneSystem::Instantiate(base, _scene);
	ASSERT_TRUE(instance.IsValid());
	EXPECT_TRUE(SceneSystem::IsPrefabInstanceRoot(instance));

	BodyStatsOf(instance)->power = 42.0f;
	SceneSystem::Destroy(instance->FindChildByName("Turret"));
	Handle<SceneObject> body = instance->FindChildByName("Body");
	Handle<SceneObject> antenna = SceneSystem::CreateObject("Antenna", _scene, body);
	antenna->AddComponent<PfStatsComponent>()->label = "added";
	Handle<PfStatsComponent> extra = instance->AddComponent<PfStatsComponent>();
	extra->label = "extra";
	SceneSystem::EndFrame();
	u64 bodyId = body.GetInstanceId();
	u64 antennaId = antenna.GetInstanceId();

	Guid variant = SceneSystem::SavePrefabVariant(instance, Path("project://unit_variant.prefab"));
	ASSERT_TRUE(variant.IsValid());
	EXPECT_NE(variant, base);
	SceneSystem::EndFrame();

	PrefabAsset* asset = static_cast<PrefabAsset*>(AssetDatabase::LoadAssetByGuid(variant));
	ASSERT_NE(asset, nullptr);
	EXPECT_EQ(asset->GetBasePrefab(), base);
	EXPECT_EQ(asset->GetOverrides().properties.size(), 1u);
	EXPECT_EQ(asset->GetOverrides().removedObjects.size(), 1u);
	EXPECT_EQ(asset->GetOverrides().addedObjects.size(), 1u);
	EXPECT_EQ(asset->GetOverrides().addedComponents.size(), 1u);
	AssetDatabase::UnloadAsset(asset);

	EXPECT_TRUE(SceneSystem::IsPrefabInstanceRoot(instance));
	EXPECT_EQ(body.GetInstanceId(), bodyId);
	ASSERT_TRUE(antenna.IsValid());
	EXPECT_EQ(antenna.GetInstanceId(), antennaId);
	ASSERT_TRUE(SceneSystem::RevertInstanceOverrides(instance));
	SceneSystem::EndFrame();
	EXPECT_FLOAT_EQ(BodyStatsOf(instance)->power, 42.0f);
	EXPECT_FALSE(instance->FindChildByName("Turret").IsValid());
	ASSERT_TRUE(instance->FindChildByName("Body")->FindChildByName("Antenna").IsValid());
	EXPECT_EQ(instance->FindChildByName("Body")->FindChildByName("Antenna")->GetComponent<PfStatsComponent>()->label, "added");
	ASSERT_TRUE(instance->GetComponent<PfStatsComponent>().IsValid());
	EXPECT_EQ(instance->GetComponent<PfStatsComponent>()->label, "extra");

	Handle<SceneObject> fresh = SceneSystem::Instantiate(variant, _scene);
	ASSERT_TRUE(fresh.IsValid());
	EXPECT_FLOAT_EQ(BodyStatsOf(fresh)->power, 42.0f);
	EXPECT_FLOAT_EQ(BodyStatsOf(fresh)->speed, 2.0f);
	EXPECT_FALSE(fresh->FindChildByName("Turret").IsValid());
	ASSERT_TRUE(fresh->FindChildByName("Body")->FindChildByName("Antenna").IsValid());
	ASSERT_TRUE(fresh->GetComponent<PfStatsComponent>().IsValid());
	EXPECT_EQ(fresh->GetComponent<PfStatsComponent>()->label, "extra");

	Handle<SceneObject> fromBase = SceneSystem::Instantiate(base, _scene);
	BodyStatsOf(fromBase)->speed = 9.0f;
	BodyStatsOf(fromBase)->power = 5.0f;
	ASSERT_TRUE(SceneSystem::ApplyInstanceOverridesToBase(fromBase));
	SceneSystem::EndFrame();
	EXPECT_FLOAT_EQ(BodyStatsOf(instance)->speed, 9.0f);
	EXPECT_FLOAT_EQ(BodyStatsOf(fresh)->speed, 9.0f);
	EXPECT_FLOAT_EQ(BodyStatsOf(instance)->power, 42.0f);
	EXPECT_FLOAT_EQ(BodyStatsOf(fresh)->power, 42.0f);
}

TEST_F(PrefabFixture, RelinkedInstanceReloadsAsInstanceOfVariant) {
	Guid base = MakeBasePrefab();
	Handle<SceneObject> instance = SceneSystem::Instantiate(base, _scene);
	BodyStatsOf(instance)->power = 42.0f;
	Guid variant = SceneSystem::SavePrefabVariant(instance, Path("project://unit_variant.prefab"));
	ASSERT_TRUE(variant.IsValid());
	SceneSystem::EndFrame();

	ASSERT_TRUE(SceneSystem::SaveScene(_scene, Path("project://relinked.scene")));
	SceneSystem::UnloadScene(_scene);
	SceneSystem::EndFrame();
	Handle<Scene> reloaded = SceneSystem::LoadScene(Path("project://relinked.scene"));
	ASSERT_TRUE(reloaded.IsValid());
	Handle<SceneObject> unit = reloaded->FindObjectByName("Unit");
	ASSERT_TRUE(unit.IsValid());
	EXPECT_FLOAT_EQ(BodyStatsOf(unit)->power, 42.0f);

	Handle<SceneObject> detached = SceneSystem::InstantiateDetached(variant);
	ASSERT_TRUE(detached.IsValid());
	BodyStatsOf(detached)->label = "variant";
	ASSERT_TRUE(SceneSystem::ApplyDetachedToPrefab(detached, variant));
	SceneSystem::DestroyDetached(detached);
	SceneSystem::EndFrame();
	EXPECT_EQ(BodyStatsOf(unit)->label, "variant");
	ASSERT_TRUE(SceneSystem::RevertInstanceOverrides(unit));
	SceneSystem::EndFrame();
	EXPECT_FLOAT_EQ(BodyStatsOf(unit)->power, 42.0f);
	EXPECT_EQ(BodyStatsOf(unit)->label, "variant");
}

TEST_F(PrefabFixture, SaveVariantNeedsInstanceRoot) {
	Guid base = MakeBasePrefab();
	Handle<SceneObject> plain = SceneSystem::CreateObject("Plain", _scene);
	EXPECT_FALSE(SceneSystem::IsPrefabInstanceRoot(plain));
	EXPECT_FALSE(SceneSystem::SavePrefabVariant(plain, Path("project://plain.prefab")).IsValid());

	Handle<SceneObject> instance = SceneSystem::Instantiate(base, _scene);
	Handle<SceneObject> body = instance->FindChildByName("Body");
	EXPECT_TRUE(SceneSystem::IsPrefabLinked(body));
	EXPECT_FALSE(SceneSystem::IsPrefabInstanceRoot(body));
	EXPECT_FALSE(SceneSystem::SavePrefabVariant(body, Path("project://body.prefab")).IsValid());
	body->SetParent({});
	EXPECT_TRUE(SceneSystem::IsPrefabLinked(body));
	EXPECT_FALSE(SceneSystem::IsPrefabInstanceRoot(body));
	EXPECT_FALSE(SceneSystem::SavePrefabVariant(body, Path("project://body.prefab")).IsValid());
	Handle<SceneObject> detached = SceneSystem::InstantiateDetached(base);
	ASSERT_TRUE(detached.IsValid());
	EXPECT_FALSE(SceneSystem::IsPrefabInstanceRoot(detached));
	EXPECT_FALSE(SceneSystem::SavePrefabVariant(detached, Path("project://detached.prefab")).IsValid());
	SceneSystem::DestroyDetached(detached);

	EXPECT_TRUE(SceneSystem::IsPrefabInstanceRoot(instance));
	EXPECT_FALSE(SceneSystem::SavePrefabVariant(instance, Path("project://unit.prefab")).IsValid());
	EXPECT_TRUE(SceneSystem::IsPrefabInstanceRoot(instance));
	Handle<SceneObject> again = SceneSystem::Instantiate(base, _scene);
	ASSERT_TRUE(again.IsValid());
	EXPECT_FLOAT_EQ(BodyStatsOf(again)->power, 1.0f);

	EXPECT_FALSE(std::filesystem::exists(_root / "plain.prefab"));
	EXPECT_FALSE(std::filesystem::exists(_root / "body.prefab"));
	EXPECT_FALSE(std::filesystem::exists(_root / "detached.prefab"));
}

namespace {

	const PrefabInstanceDiff::Property* PropertyFor(const PrefabInstanceDiff& diff, u64 targetId, const char* field) {
		for (const PrefabInstanceDiff::Property& property : diff.properties) {
			if (property.target.GetInstanceId() == targetId && property.fieldPath.size() == 1 &&
				property.fieldPath[0] == StringHash(field)) {
				return &property;
			}
		}
		return nullptr;
	}

} // namespace

TEST_F(PrefabFixture, DiffReportsEveryOverrideKind) {
	Guid base = MakeBasePrefab();
	Handle<SceneObject> instance = SceneSystem::Instantiate(base, _scene, {}, Vec3{1.0f, 2.0f, 3.0f});
	ASSERT_TRUE(instance.IsValid());

	PrefabInstanceDiff diff;
	ASSERT_TRUE(SceneSystem::DiffPrefabInstance(instance, diff));
	ASSERT_EQ(diff.properties.size(), 1u);
	const PrefabInstanceDiff::Property* position = PropertyFor(diff, instance.GetInstanceId(), "_localPosition");
	ASSERT_NE(position, nullptr);
	EXPECT_FALSE(position->isHandle);
	ASSERT_TRUE(position->base.Is<Vec3>());
	EXPECT_FLOAT_EQ(position->base.As<Vec3>().x, 0.0f);
	EXPECT_EQ(position->row.leaf.file, base);
	EXPECT_EQ(position->row.leaf.localId, 1u);
	EXPECT_TRUE(diff.addedComponents.empty() && diff.removedComponents.empty() &&
				diff.addedObjects.empty() && diff.removedObjects.empty());

	Handle<SceneObject> body = instance->FindChildByName("Body");
	Handle<SceneObject> turret = instance->FindChildByName("Turret");
	Handle<PfStatsComponent> bodyStats = BodyStatsOf(instance);
	bodyStats->power = 9.0f;
	bodyStats->buddy = turret;
	Handle<PfLifecycleComponent> added = body->AddComponent<PfLifecycleComponent>();
	Handle<SceneObject> extra = SceneSystem::CreateObject("Extra", _scene, turret);
	Handle<PfStatsComponent> turretStats = turret->GetComponent<PfStatsComponent>();
	ASSERT_TRUE(turretStats.IsValid());
	SceneSystem::Destroy(Handle<Component>(turretStats.Get()));
	SceneSystem::EndFrame();

	ASSERT_TRUE(SceneSystem::DiffPrefabInstance(instance, diff));
	const PrefabInstanceDiff::Property* power = PropertyFor(diff, bodyStats.GetInstanceId(), "power");
	ASSERT_NE(power, nullptr);
	ASSERT_TRUE(power->base.Is<f32>());
	EXPECT_FLOAT_EQ(power->base.As<f32>(), 1.0f);
	EXPECT_EQ(power->row.leaf.localId, 3u);
	const PrefabInstanceDiff::Property* buddy = PropertyFor(diff, bodyStats.GetInstanceId(), "buddy");
	ASSERT_NE(buddy, nullptr);
	EXPECT_TRUE(buddy->isHandle);
	EXPECT_EQ(buddy->baseHandleId, 0u);
	EXPECT_TRUE(buddy->base.IsEmpty());

	ASSERT_EQ(diff.addedComponents.size(), 1u);
	EXPECT_EQ(diff.addedComponents[0].GetInstanceId(), added.GetInstanceId());
	ASSERT_EQ(diff.addedObjects.size(), 1u);
	EXPECT_EQ(diff.addedObjects[0].GetInstanceId(), extra.GetInstanceId());
	ASSERT_EQ(diff.removedComponents.size(), 1u);
	EXPECT_EQ(diff.removedComponents[0].owner.GetInstanceId(), turret.GetInstanceId());
	EXPECT_EQ(diff.removedComponents[0].type, PfStatsComponent::GetReflectType()->name);
	EXPECT_EQ(diff.removedComponents[0].index, 0u);
	EXPECT_EQ(diff.removedComponents[0].row.leaf.localId, 5u);
	EXPECT_TRUE(diff.removedObjects.empty());

	SceneSystem::Destroy(body);
	SceneSystem::EndFrame();
	ASSERT_TRUE(SceneSystem::DiffPrefabInstance(instance, diff));
	ASSERT_EQ(diff.removedObjects.size(), 1u);
	EXPECT_EQ(diff.removedObjects[0].parent.GetInstanceId(), instance.GetInstanceId());
	EXPECT_EQ(diff.removedObjects[0].name, "Body");
	EXPECT_EQ(diff.removedObjects[0].row.leaf.localId, 2u);
	EXPECT_EQ(diff.removedComponents.size(), 1u);
	EXPECT_EQ(PropertyFor(diff, bodyStats.GetInstanceId(), "power"), nullptr);

	EXPECT_FALSE(SceneSystem::DiffPrefabInstance(turret, diff));
	EXPECT_FALSE(SceneSystem::DiffPrefabInstance(SceneSystem::CreateObject("Plain", _scene), diff));
	EXPECT_FALSE(SceneSystem::DiffPrefabInstance(Handle<SceneObject>{}, diff));
	EXPECT_TRUE(diff.IsEmpty());
}

TEST_F(PrefabFixture, DiffReadsIslandHandle) {
	Handle<SceneObject> unit = SceneSystem::CreateObject("Unit", _scene);
	Handle<SceneObject> body = SceneSystem::CreateObject("Body", _scene, unit);
	Handle<SceneObject> turret = SceneSystem::CreateObject("Turret", _scene, unit);
	Handle<PfStatsComponent> stats = body->AddComponent<PfStatsComponent>();
	stats->buddy = turret;
	Guid guid = SceneSystem::SavePrefab(unit, Path("project://linked.prefab"));
	ASSERT_TRUE(guid.IsValid());
	SceneSystem::Destroy(unit);
	SceneSystem::EndFrame();

	Handle<SceneObject> instance = SceneSystem::Instantiate(guid, _scene);
	Handle<SceneObject> liveTurret = instance->FindChildByName("Turret");
	Handle<PfStatsComponent> liveStats = BodyStatsOf(instance);
	ASSERT_TRUE(liveStats.IsValid());
	ASSERT_EQ(liveStats->buddy.GetInstanceId(), liveTurret.GetInstanceId());
	liveStats->buddy = {};

	PrefabInstanceDiff diff;
	ASSERT_TRUE(SceneSystem::DiffPrefabInstance(instance, diff));
	const PrefabInstanceDiff::Property* buddy = PropertyFor(diff, liveStats.GetInstanceId(), "buddy");
	ASSERT_NE(buddy, nullptr);
	EXPECT_TRUE(buddy->isHandle);
	EXPECT_EQ(buddy->baseHandleId, liveTurret.GetInstanceId());
}

TEST_F(PrefabFixture, DiffAgainstBaseReportsVariantsLayer) {
	Guid base = MakeBasePrefab();
	Guid variant = MakeFloatVariant("project://variant.prefab", base, base, 3, "power", 7.0f);
	ASSERT_TRUE(variant.IsValid());
	Guid unrelated = MakeBasePrefab("project://other.prefab");

	Handle<SceneObject> detached = SceneSystem::InstantiateDetached(variant);
	ASSERT_TRUE(detached.IsValid());
	PrefabInstanceDiff diff;
	EXPECT_FALSE(SceneSystem::DiffPrefabInstance(detached, diff));
	ASSERT_TRUE(SceneSystem::DiffPrefabInstance(detached, diff, variant));
	EXPECT_TRUE(diff.IsEmpty());
	ASSERT_TRUE(SceneSystem::DiffPrefabInstance(detached, diff, base));
	ASSERT_EQ(diff.properties.size(), 1u);
	const PrefabInstanceDiff::Property* power = PropertyFor(diff, BodyStatsOf(detached).GetInstanceId(), "power");
	ASSERT_NE(power, nullptr);
	EXPECT_FLOAT_EQ(power->base.As<f32>(), 1.0f);
	BodyStatsOf(detached)->label = "edited";
	ASSERT_TRUE(SceneSystem::DiffPrefabInstance(detached, diff, base));
	EXPECT_EQ(diff.properties.size(), 2u);
	ASSERT_TRUE(SceneSystem::DiffPrefabInstance(detached, diff, variant));
	EXPECT_EQ(diff.properties.size(), 1u);
	EXPECT_FALSE(SceneSystem::DiffPrefabInstance(detached, diff, unrelated));
	SceneSystem::DestroyDetached(detached);

	Handle<SceneObject> instance = SceneSystem::Instantiate(variant, _scene);
	ASSERT_TRUE(SceneSystem::DiffPrefabInstance(instance, diff));
	EXPECT_TRUE(diff.IsEmpty());
	ASSERT_TRUE(SceneSystem::DiffPrefabInstance(instance, diff, base));
	EXPECT_NE(PropertyFor(diff, BodyStatsOf(instance).GetInstanceId(), "power"), nullptr);
	EXPECT_FALSE(SceneSystem::DiffPrefabInstance(instance, diff, unrelated));
}

TEST_F(PrefabFixture, RevertRestoresRemovedComponent) {
	Guid base = MakeBasePrefab();
	Handle<SceneObject> instance = SceneSystem::Instantiate(base, _scene);
	Handle<SceneObject> turret = instance->FindChildByName("Turret");
	BodyStatsOf(instance)->power = 9.0f;
	SceneSystem::Destroy(Handle<Component>(turret->GetComponent<PfStatsComponent>().Get()));
	SceneSystem::EndFrame();
	ASSERT_FALSE(turret->GetComponent<PfStatsComponent>().IsValid());

	PrefabInstanceDiff diff;
	ASSERT_TRUE(SceneSystem::DiffPrefabInstance(instance, diff));
	ASSERT_EQ(diff.removedComponents.size(), 1u);
	const PrefabTargetPath rows[] = {diff.removedComponents[0].row};
	ASSERT_TRUE(SceneSystem::RevertInstanceOverrides(instance, rows));
	SceneSystem::EndFrame();

	Handle<PfStatsComponent> restored = turret->GetComponent<PfStatsComponent>();
	ASSERT_TRUE(restored.IsValid());
	EXPECT_FLOAT_EQ(restored->power, 5.0f);
	EXPECT_TRUE(restored->IsEffectivelyEnabled());
	EXPECT_FLOAT_EQ(BodyStatsOf(instance)->power, 9.0f);
	EXPECT_EQ(turret.Get()->GetComponentCount(), 1u);

	ASSERT_TRUE(SceneSystem::DiffPrefabInstance(instance, diff));
	EXPECT_TRUE(diff.removedComponents.empty());
	ASSERT_EQ(diff.properties.size(), 1u);
	const PrefabTargetPath bodyStatsRow[] = {diff.properties[0].row};
	ASSERT_TRUE(SceneSystem::RevertInstanceOverrides(instance, bodyStatsRow));
	SceneSystem::EndFrame();
	EXPECT_FLOAT_EQ(BodyStatsOf(instance)->power, 1.0f);
	ASSERT_TRUE(SceneSystem::DiffPrefabInstance(instance, diff));
	EXPECT_TRUE(diff.IsEmpty());

	Handle<PfLifecycleComponent> added = turret->AddComponent<PfLifecycleComponent>();
	ASSERT_TRUE(SceneSystem::DiffPrefabInstance(instance, diff));
	ASSERT_EQ(diff.addedComponents.size(), 1u);
	PrefabTargetPath none;
	const PrefabTargetPath nothing[] = {none};
	ASSERT_TRUE(SceneSystem::RevertInstanceOverrides(instance, nothing));
	SceneSystem::EndFrame();
	EXPECT_TRUE(added.IsValid());

	EXPECT_FALSE(SceneSystem::RevertInstanceOverrides(turret, rows));
	EXPECT_FALSE(SceneSystem::RevertInstanceOverrides(SceneSystem::CreateObject("Plain", _scene), rows));
}

TEST_F(PrefabFixture, EnumOverrideSurvivesFile) {
	Handle<SceneObject> unit = SceneSystem::CreateObject("Unit", _scene);
	unit->AddComponent<PfModeComponent>();
	Guid guid = SceneSystem::SavePrefab(unit, Path("project://mode.prefab"));
	ASSERT_TRUE(guid.IsValid());
	SceneSystem::Destroy(unit);
	SceneSystem::EndFrame();

	Handle<Scene> scene = SceneSystem::CreateScene("Enums");
	Handle<SceneObject> instance = SceneSystem::Instantiate(guid, scene);
	ASSERT_TRUE(instance.IsValid());
	Handle<PfModeComponent> mode = instance->GetComponent<PfModeComponent>();
	ASSERT_TRUE(mode.IsValid());
	mode->mode = PfTestEnum::Mode::Fly;

	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://enums.scene")));
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
	Handle<Scene> reloaded = SceneSystem::LoadScene(Path("project://enums.scene"));
	ASSERT_TRUE(reloaded.IsValid());
	Handle<SceneObject> back = reloaded->FindObjectByName("Unit");
	ASSERT_TRUE(back.IsValid());
	Handle<PfModeComponent> backMode = back->GetComponent<PfModeComponent>();
	ASSERT_TRUE(backMode.IsValid());
	EXPECT_EQ(backMode->mode, PfTestEnum::Mode::Fly);

	ASSERT_TRUE(SceneSystem::RevertInstanceOverrides(back));
	SceneSystem::EndFrame();
	EXPECT_EQ(backMode->mode, PfTestEnum::Mode::Idle);
}

TEST_F(PrefabFixture, OverrideNamesSceneRow) {
	Guid base = MakeBasePrefab();
	Handle<SceneObject> target = SceneSystem::CreateObject("Camera", _scene);
	Handle<SceneObject> instance = SceneSystem::Instantiate(base, _scene);
	ASSERT_TRUE(instance.IsValid());

	Handle<PfStatsComponent> stats = BodyStatsOf(instance);
	ASSERT_TRUE(stats.IsValid());
	stats->buddy = target;

	Handle<SceneObject> clone = SceneSystem::Instantiate(instance, Handle<SceneObject>());
	ASSERT_TRUE(clone.IsValid());
	Handle<PfStatsComponent> clonedStats = BodyStatsOf(clone);
	ASSERT_TRUE(clonedStats.IsValid());
	EXPECT_EQ(clonedStats->buddy.GetInstanceId(), target.GetInstanceId());
	SceneSystem::Destroy(clone);
	SceneSystem::EndFrame();

	ASSERT_TRUE(SceneSystem::SaveScene(_scene, Path("project://ref.scene")));
	SceneSystem::UnloadScene(_scene);
	SceneSystem::EndFrame();

	_scene = SceneSystem::LoadScene(Path("project://ref.scene"));
	ASSERT_TRUE(_scene.IsValid());
	Handle<SceneObject> restoredUnit = _scene->FindObjectByName("Unit");
	ASSERT_TRUE(restoredUnit.IsValid());
	Handle<PfStatsComponent> restoredStats = BodyStatsOf(restoredUnit);
	ASSERT_TRUE(restoredStats.IsValid());
	ASSERT_TRUE(restoredStats->buddy.IsValid());
	EXPECT_EQ(restoredStats->buddy->name, "Camera");
	EXPECT_EQ(restoredStats->buddy.GetInstanceId(), _scene->FindObjectByName("Camera").GetInstanceId());
}

TEST_F(PrefabFixture, ApplyKeepsSceneReferenceOnInstance) {
	Guid base = MakeBasePrefab();
	Handle<SceneObject> target = SceneSystem::CreateObject("Camera", _scene);
	Handle<SceneObject> instance = SceneSystem::Instantiate(base, _scene);
	ASSERT_TRUE(instance.IsValid());
	Handle<PfStatsComponent> stats = BodyStatsOf(instance);
	ASSERT_TRUE(stats.IsValid());
	stats->buddy = target;
	stats->power = 42.0f;

	ASSERT_TRUE(SceneSystem::ApplyInstanceOverridesToBase(instance));

	Handle<SceneObject> second = SceneSystem::Instantiate(base, _scene);
	ASSERT_TRUE(second.IsValid());
	Handle<PfStatsComponent> secondStats = BodyStatsOf(second);
	ASSERT_TRUE(secondStats.IsValid());
	EXPECT_EQ(secondStats->power, 42.0f);
	EXPECT_FALSE(secondStats->buddy.IsValid());
}
