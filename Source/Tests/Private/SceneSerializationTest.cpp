#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Math.h"
#include "Shalltear/Core/Scene.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <thread>
#include <condition_variable>
#include <mutex>
#include <unordered_set>
#include <vector>

using namespace Shalltear;

namespace {

	class SsStatsComponent : public Component {
	public:
		f32 health = 100.0f;
		i32 damage = 10;
		bool shielded = false;
		str title = "default";
		Vec3 aim{};
		Guid loot{};

		SLT_REFLECT_COMPONENT(SsStatsComponent, Component)
		SLT_REFLECT_FIELD(health)
		SLT_REFLECT_FIELD(damage)
		SLT_REFLECT_FIELD(shielded)
		SLT_REFLECT_FIELD(title)
		SLT_REFLECT_FIELD(aim)
		SLT_REFLECT_FIELD(loot)
		SLT_END_REFLECT()

		SsStatsComponent() = default;
	};

	class SsRefComponent : public Component {
	public:
		Handle<SceneObject> target;
		Handle<SsStatsComponent> stats;
		Handle<TextAsset> asset;

		SLT_REFLECT_COMPONENT(SsRefComponent, Component)
		SLT_REFLECT_FIELD(target)
		SLT_REFLECT_FIELD(stats)
		SLT_REFLECT_FIELD(asset)
		SLT_END_REFLECT()

		SsRefComponent() = default;
	};

	class SsFragileComponent : public Component {
	public:
		i32 charges = 0;
		str sigil;
		Handle<SceneObject> anchor;

		SLT_REFLECT_COMPONENT(SsFragileComponent, Component)
		SLT_REFLECT_FIELD(charges)
		SLT_REFLECT_FIELD(sigil)
		SLT_REFLECT_FIELD(anchor)
		SLT_END_REFLECT()

		SsFragileComponent() = default;
	};

	struct ScopedFragileType {
		ScopedFragileType() {
			TypeRegistry::Register(const_cast<TypeInfo*>(SsFragileComponent::GetReflectType()));
		}
		~ScopedFragileType() { TypeRegistry::Unregister(SsFragileComponent::GetReflectType()->name); }
	};

	class SsReservedKeyComponent : public Component {
	public:
		str _opaque = "cafe";
		i32 charge = 0;

		SLT_REFLECT_COMPONENT(SsReservedKeyComponent, Component)
		SLT_REFLECT_FIELD(_opaque)
		SLT_REFLECT_FIELD(charge)
		SLT_END_REFLECT()

		SsReservedKeyComponent() = default;
	};

	SLT_REGISTER_TYPE(SsStatsComponent)
	SLT_REGISTER_TYPE(SsRefComponent)
	SLT_REGISTER_TYPE(SsReservedKeyComponent)

	class SceneSerializationFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		size_t _sceneBytesBaseline = 0;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_scene_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root);
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();

			SceneSystem::Initialize();
			SceneSystem::Terminate();
			_sceneBytesBaseline = MemoryManager::GetBytesForTag(MemTag::Scene);
			SceneSystem::Initialize();
		}

		void TearDown() override {
			SceneSystem::Terminate();
			EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Scene), _sceneBytesBaseline);
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		Handle<Scene> BuildReferenceScene(Handle<TextAsset>* outAsset = nullptr) {
			Handle<Scene> scene = SceneSystem::CreateScene("Level1");

			Handle<SceneObject> player = SceneSystem::CreateObject("Player", scene);
			player->SetLocalPosition(Vec3(1.0f, 2.0f, 3.0f));
			player->SetLocalRotation(Quat::FromEuler(0.0f, 45.0f, 0.0f));
			player->SetLocalScale(Vec3(2.0f, 2.0f, 2.0f));

			Handle<SceneObject> arm = SceneSystem::CreateObject("Arm", scene, player);
			arm->SetLocalPosition(Vec3(0.0f, 1.0f, 0.0f));
			Handle<SceneObject> shield = SceneSystem::CreateObject("Shield", scene, player);
			(void)shield;

			Handle<SceneObject> hand = SceneSystem::CreateObject("Hand", scene, arm);
			hand->SetActive(false);

			Handle<SsStatsComponent> stats = hand->AddComponent<SsStatsComponent>();
			stats->health = 42.5f;
			stats->damage = -3;
			stats->shielded = true;
			stats->title = "left hand";
			stats->aim = Vec3(0.5f, 0.25f, 0.125f);
			stats->loot = Guid{0x1111, 0x2222};

			Handle<SsStatsComponent> disabledStats = arm->AddComponent<SsStatsComponent>();
			disabledStats->title = "sleepy";
			disabledStats->SetEnabled(false);

			Handle<SsRefComponent> refs = player->AddComponent<SsRefComponent>();
			refs->target = hand;
			refs->stats = stats;
			if (outAsset) {
				FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://loot.txt")), "gold");
				TextAsset* text = static_cast<TextAsset*>(AssetDatabase::LoadAsset(Path("project://loot.txt")));
				*outAsset = Handle<TextAsset>(text);
				refs->asset = *outAsset;
			}
			return scene;
		}

		void VerifyReferenceScene(Handle<Scene> scene, const Guid& expectedAssetId) {
			ASSERT_TRUE(scene.IsValid());
			ASSERT_EQ(scene->GetRootCount(), 1u);
			EXPECT_EQ(scene->GetObjectCount(), 4u);

			Handle<SceneObject> player = scene->GetRoot(0);
			EXPECT_EQ(player->name, "Player");
			EXPECT_TRUE(Vec3(1.0f, 2.0f, 3.0f) == player->GetLocalPosition());
			EXPECT_TRUE(Quat::Approximately(player->GetLocalRotation(), Quat::FromEuler(0.0f, 45.0f, 0.0f), 1e-5f));
			EXPECT_TRUE(Vec3(2.0f, 2.0f, 2.0f) == player->GetLocalScale());

			ASSERT_EQ(player->GetChildCount(), 2u);
			Handle<SceneObject> arm = player->GetChild(0);
			EXPECT_EQ(arm->name, "Arm");
			EXPECT_EQ(player->GetChild(1)->name, "Shield");

			Handle<SceneObject> hand = arm->FindChildByName("Hand");
			ASSERT_TRUE(hand.IsValid());
			EXPECT_FALSE(hand->IsActiveSelf());
			EXPECT_FALSE(hand->IsActiveInHierarchy());

			Handle<SsStatsComponent> stats = hand->GetComponent<SsStatsComponent>();
			ASSERT_TRUE(stats.IsValid());
			EXPECT_FLOAT_EQ(stats->health, 42.5f);
			EXPECT_EQ(stats->damage, -3);
			EXPECT_TRUE(stats->shielded);
			EXPECT_EQ(stats->title, "left hand");
			EXPECT_TRUE(stats->aim == Vec3(0.5f, 0.25f, 0.125f));
			EXPECT_EQ(stats->loot, (Guid{0x1111, 0x2222}));

			Handle<SsStatsComponent> disabledStats = arm->GetComponent<SsStatsComponent>();
			ASSERT_TRUE(disabledStats.IsValid());
			EXPECT_EQ(disabledStats->title, "sleepy");
			EXPECT_FALSE(disabledStats->IsEnabledSelf());

			Handle<SsRefComponent> refs = player->GetComponent<SsRefComponent>();
			ASSERT_TRUE(refs.IsValid());
			ASSERT_TRUE(refs->target.IsValid());
			EXPECT_EQ(refs->target.GetInstanceId(), hand.GetInstanceId());
			ASSERT_TRUE(refs->stats.IsValid());
			EXPECT_EQ(refs->stats.GetInstanceId(), stats.GetInstanceId());

			if (expectedAssetId.IsValid()) {
				ASSERT_TRUE(refs->asset.IsValid());
				EXPECT_EQ(refs->asset->assetId, expectedAssetId);
			}
		}
	};

} // namespace

TEST_F(SceneSerializationFixture, TextRoundTripPreservesIdentityAndState) {
	Handle<TextAsset> asset;
	Handle<Scene> original = BuildReferenceScene(&asset);
	Guid assetId = asset->assetId;

	ASSERT_TRUE(SceneSystem::SaveScene(original, Path("project://level1.scene")));
	EXPECT_TRUE(original->GetSourceGuid().IsValid());

	SceneSystem::UnloadScene(original);
	SceneSystem::EndFrame();

	Handle<Scene> reloaded = SceneSystem::LoadScene(Path("project://level1.scene"));
	VerifyReferenceScene(reloaded, assetId);
	EXPECT_EQ(reloaded->name, "level1");

	AssetDatabase::UnloadAsset(asset.Get());
}

TEST_F(SceneSerializationFixture, ASavedSceneSpellsItsFieldNames) {
	Handle<Scene> original = BuildReferenceScene();
	ASSERT_TRUE(SceneSystem::SaveScene(original, Path("project://readable.scene")));
	str text = FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://readable.scene")), MemTag::Scene);

	EXPECT_NE(text.find("\"_localPosition\""), str::npos) << text;
	EXPECT_NE(text.find("\"health\""), str::npos) << text;
	EXPECT_NE(text.find("\"title\""), str::npos) << text;
	EXPECT_NE(text.find("\"_type\": \"SsStatsComponent\""), str::npos) << text;

	EXPECT_EQ(text.find(std::format("{:016x}", StringHash("health").GetValue())), str::npos) << text;
	EXPECT_EQ(text.find(std::format("{:016x}", StringHash("_localPosition").GetValue())), str::npos) << text;

	SceneSystem::UnloadScene(original);
	SceneSystem::EndFrame();
	VerifyReferenceScene(SceneSystem::LoadScene(Path("project://readable.scene")), Guid{});
}

TEST_F(SceneSerializationFixture, BlobMarkerFieldSurvives) {
	Handle<Scene> scene = SceneSystem::CreateScene("Reserved");
	Handle<SceneObject> object = SceneSystem::CreateObject("Holder", scene);
	Handle<SsReservedKeyComponent> component = object->AddComponent<SsReservedKeyComponent>();
	component->_opaque = "beef";
	component->charge = 5;

	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://reserved.scene")));
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();

	Handle<Scene> reloaded = SceneSystem::LoadScene(Path("project://reserved.scene"));
	ASSERT_TRUE(reloaded.IsValid());
	Handle<SsReservedKeyComponent> back =
		reloaded->FindObjectByName("Holder")->GetComponent<SsReservedKeyComponent>();
	ASSERT_TRUE(back.IsValid());
	EXPECT_EQ(back->_opaque, "beef");
	EXPECT_EQ(back->charge, 5);
}

TEST_F(SceneSerializationFixture, HashKeyedSceneHeals) {
	auto hexOf = [](u64 value) { return std::format("{:016x}", value); };
	str objectType = hexOf(SceneObject::GetReflectType()->name.GetValue());
	str statsType = hexOf(SsStatsComponent::GetReflectType()->name.GetValue());
	str json = R"({
	"_type": "Scene",
	"formatVersion": 1,
	"engineHash": "0000000000000000",
	"name": "Legacy",
	"nextLocalId": 3,
	"objects": [
		{
			"id": 1, "parent": 0, "type": ")" +
			   objectType +
			   R"(",
			"fields": {"_type": ")" +
			   objectType + R"(", ")" + hexOf(StringHash("name").GetValue()) +
			   R"(": "Ancient"},
			"components": [
				{"id": 2, "type": ")" +
			   statsType + R"(", "fields": {"_type": ")" + statsType + R"(", ")" +
			   hexOf(StringHash("health").GetValue()) + R"(": 33.5, ")" +
			   hexOf(StringHash("title").GetValue()) + R"(": "relic"}}
			]
		}
	]
})";
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://legacy.scene")), json);

	Handle<Scene> scene = SceneSystem::LoadScene(Path("project://legacy.scene"));
	ASSERT_TRUE(scene.IsValid());
	Handle<SceneObject> ancient = scene->FindObjectByName("Ancient");
	ASSERT_TRUE(ancient.IsValid());
	Handle<SsStatsComponent> stats = ancient->GetComponent<SsStatsComponent>();
	ASSERT_TRUE(stats.IsValid());
	EXPECT_FLOAT_EQ(stats->health, 33.5f);
	EXPECT_EQ(stats->title, "relic");

	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://legacy.scene")));
	str saved = FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://legacy.scene")), MemTag::Scene);
	EXPECT_NE(saved.find("\"health\""), str::npos) << saved;
	EXPECT_EQ(saved.find(hexOf(StringHash("health").GetValue())), str::npos) << saved;

	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
	Handle<Scene> healed = SceneSystem::LoadScene(Path("project://legacy.scene"));
	ASSERT_TRUE(healed.IsValid());
	Handle<SsStatsComponent> healedStats = healed->FindObjectByName("Ancient")->GetComponent<SsStatsComponent>();
	ASSERT_TRUE(healedStats.IsValid());
	EXPECT_FLOAT_EQ(healedStats->health, 33.5f);
	EXPECT_EQ(healedStats->title, "relic");
}

TEST_F(SceneSerializationFixture, BinaryRoundTripPreservesIdentityAndState) {
	Handle<TextAsset> asset;
	Handle<Scene> original = BuildReferenceScene(&asset);
	Guid assetId = asset->assetId;

	ASSERT_TRUE(SceneSystem::SaveScene(original, Path("project://level1.sltscene.scene"), true));
	SceneSystem::UnloadScene(original);
	SceneSystem::EndFrame();

	Handle<Scene> reloaded = SceneSystem::LoadScene(Path("project://level1.sltscene.scene"));
	VerifyReferenceScene(reloaded, assetId);

	AssetDatabase::UnloadAsset(asset.Get());
}

TEST_F(SceneSerializationFixture, TextAndBinaryLoadsAreEquivalent) {
	Handle<Scene> original = BuildReferenceScene();
	ASSERT_TRUE(SceneSystem::SaveScene(original, Path("project://eq.scene")));
	ASSERT_TRUE(SceneSystem::SaveScene(original, Path("project://eq_bin.scene"), true));
	SceneSystem::UnloadScene(original);
	SceneSystem::EndFrame();

	Handle<Scene> fromText = SceneSystem::LoadScene(Path("project://eq.scene"));
	Handle<Scene> fromBinary = SceneSystem::LoadScene(Path("project://eq_bin.scene"));
	VerifyReferenceScene(fromText, Guid{});
	VerifyReferenceScene(fromBinary, Guid{});
}

TEST_F(SceneSerializationFixture, TextAssetHandleResolvesAfterARestart) {
	Handle<TextAsset> asset;
	Handle<Scene> original = BuildReferenceScene(&asset);
	Guid assetId = asset->assetId;

	ASSERT_TRUE(SceneSystem::SaveScene(original, Path("project://cold.scene")));
	SceneSystem::UnloadScene(original);
	SceneSystem::EndFrame();
	AssetDatabase::UnloadAsset(asset.Get());

	AssetDatabase::Terminate();
	AssetDatabase::Initialize();
	AssetDatabase::ScanProjectDirectory();

	Handle<Scene> reloaded = SceneSystem::LoadScene(Path("project://cold.scene"));
	ASSERT_TRUE(reloaded.IsValid());
	Handle<SsRefComponent> refs = reloaded->GetRoot(0)->GetComponent<SsRefComponent>();
	ASSERT_TRUE(refs.IsValid());
	ASSERT_TRUE(refs->asset.IsValid());
	EXPECT_EQ(refs->asset->assetId, assetId);
}

TEST_F(SceneSerializationFixture, BinaryAssetHandleResolvesAfterARestart) {
	Handle<TextAsset> asset;
	Handle<Scene> original = BuildReferenceScene(&asset);
	Guid assetId = asset->assetId;

	ASSERT_TRUE(SceneSystem::SaveScene(original, Path("project://cold_bin.scene"), true));
	SceneSystem::UnloadScene(original);
	SceneSystem::EndFrame();
	AssetDatabase::UnloadAsset(asset.Get());

	AssetDatabase::Terminate();
	AssetDatabase::Initialize();
	AssetDatabase::ScanProjectDirectory();

	Handle<Scene> reloaded = SceneSystem::LoadScene(Path("project://cold_bin.scene"));
	ASSERT_TRUE(reloaded.IsValid());
	Handle<SsRefComponent> refs = reloaded->GetRoot(0)->GetComponent<SsRefComponent>();
	ASSERT_TRUE(refs.IsValid());
	ASSERT_TRUE(refs->asset.IsValid());
	EXPECT_EQ(refs->asset->assetId, assetId);
}

TEST_F(SceneSerializationFixture, OneReferencePerAsset) {
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://shared.txt")), "gold");
	TextAsset* text = static_cast<TextAsset*>(AssetDatabase::LoadAsset(Path("project://shared.txt")));
	ASSERT_NE(text, nullptr);

	Handle<Scene> authored = SceneSystem::CreateScene("Retention");
	Handle<SceneObject> first = SceneSystem::CreateObject("First", authored);
	Handle<SceneObject> second = SceneSystem::CreateObject("Second", authored);
	first->AddComponent<SsRefComponent>()->asset = Handle<TextAsset>(text);
	second->AddComponent<SsRefComponent>()->asset = Handle<TextAsset>(text);
	ASSERT_TRUE(SceneSystem::SaveScene(authored, Path("project://retention.scene")));
	SceneSystem::UnloadScene(authored);
	SceneSystem::EndFrame();

	int held = text->GetRefCount();
	ASSERT_EQ(held, 1);

	Handle<Scene> reloaded = SceneSystem::LoadScene(Path("project://retention.scene"));
	ASSERT_TRUE(reloaded.IsValid());
	EXPECT_EQ(text->GetRefCount(), held + 1);

	SceneSystem::UnloadScene(reloaded);
	SceneSystem::EndFrame();
	EXPECT_EQ(text->GetRefCount(), held);
	AssetDatabase::UnloadAsset(text);
}

TEST_F(SceneSerializationFixture, SecondSaveIsByteIdentical) {
	Handle<Scene> scene = BuildReferenceScene();
	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://stable.scene")));
	str first = FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://stable.scene")), MemTag::Scene);

	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://stable.scene")));
	str second = FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://stable.scene")), MemTag::Scene);

	ASSERT_FALSE(first.empty());
	EXPECT_EQ(first, second);
}

TEST_F(SceneSerializationFixture, ReloadedSceneResavesByteIdentical) {
	Handle<Scene> scene = BuildReferenceScene();
	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://stable2.scene")));
	str first = FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://stable2.scene")), MemTag::Scene);
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();

	Handle<Scene> reloaded = SceneSystem::LoadScene(Path("project://stable2.scene"));
	ASSERT_TRUE(SceneSystem::SaveScene(reloaded, Path("project://stable2.scene")));
	str second = FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://stable2.scene")), MemTag::Scene);

	EXPECT_EQ(first, second);
}

TEST_F(SceneSerializationFixture, ExistingLocalIdsNeverChange) {
	Handle<Scene> scene = SceneSystem::CreateScene("Ids");
	Handle<SceneObject> first = SceneSystem::CreateObject("First", scene);
	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://ids.scene")));
	u64 firstId = first->GetLocalId();
	EXPECT_NE(firstId, 0u);

	Handle<SceneObject> second = SceneSystem::CreateObject("Second", scene);
	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://ids.scene")));
	EXPECT_EQ(first->GetLocalId(), firstId);
	EXPECT_GT(second->GetLocalId(), firstId);
}

TEST_F(SceneSerializationFixture, DuplicatingAnObjectKeepsLocalIdsUnique) {
	Handle<Scene> scene = BuildReferenceScene();
	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://dup.scene")));

	Handle<SceneObject> player = scene->GetRoot(0);
	Handle<SceneObject> shield = player->GetChild(1);
	ASSERT_TRUE(SceneSystem::Instantiate(shield, player).IsValid());
	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://dup.scene")));

	struct IdCollector {
		std::vector<u64> ids;
	} collected;
	scene->ForEachObject(
		+[](SceneObject& object, void* user) {
			auto& out = static_cast<IdCollector*>(user)->ids;
			out.push_back(object.GetLocalId());
			for (u32 i = 0; i < object.GetComponentCount(); ++i) {
				out.push_back(object.GetComponentAt(i)->GetLocalId());
			}
		},
		&collected);

	std::unordered_set<u64> seen;
	for (u64 id : collected.ids) {
		EXPECT_NE(id, 0u);
		EXPECT_TRUE(seen.insert(id).second) << "localId " << id;
	}
}

TEST_F(SceneSerializationFixture, DuplicateRowIdsHeal) {
	str objectType = std::format("{:016x}", SceneObject::GetReflectType()->name.GetValue());
	str refType = std::format("{:016x}", SsRefComponent::GetReflectType()->name.GetValue());
	str targetField = std::format("{:016x}", StringHash("target").GetValue());
	str json = R"({
	"_type": "Scene",
	"formatVersion": 1,
	"engineHash": "0000000000000000",
	"name": "Collided",
	"nextLocalId": 4,
	"objects": [
		{
			"id": 1, "parent": 0, "type": ")" +
			   objectType +
			   R"(",
			"fields": {"_type": ")" +
			   objectType +
			   R"("},
			"components": [
				{"id": 3, "type": ")" +
			   refType +
			   R"(", "fields": {"_type": ")" + refType + R"(", ")" + targetField +
			   R"(": {"_handle": true, "pointeeType": ")" + objectType +
			   R"(", "assetId": "0000000000000002-534c544c4f43414c"}}}
			]
		},
		{
			"id": 2, "parent": 0, "type": ")" +
			   objectType +
			   R"(",
			"fields": {"_type": ")" +
			   objectType + R"(", ")" +
			   std::format("{:016x}", StringHash("name").GetValue()) + R"(": "Original"}
		},
		{
			"id": 2, "parent": 0, "type": ")" +
			   objectType +
			   R"(",
			"fields": {"_type": ")" +
			   objectType + R"(", ")" +
			   std::format("{:016x}", StringHash("name").GetValue()) + R"(": "Collider"}
		}
	]
})";
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://collided.scene")), json);

	Handle<Scene> scene = SceneSystem::LoadScene(Path("project://collided.scene"));
	ASSERT_TRUE(scene.IsValid());
	ASSERT_EQ(scene->GetRootCount(), 3u);

	Handle<SsRefComponent> refs = scene->GetRoot(0)->GetComponent<SsRefComponent>();
	ASSERT_TRUE(refs.IsValid());
	ASSERT_TRUE(refs->target.IsValid());
	EXPECT_EQ(refs->target->name, "Original");

	Handle<SceneObject> collider = scene->FindObjectByName("Collider");
	ASSERT_TRUE(collider.IsValid());
	EXPECT_EQ(collider->GetLocalId(), 0u);

	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://collided.scene")));
	EXPECT_NE(collider->GetLocalId(), 0u);
	EXPECT_NE(collider->GetLocalId(), scene->FindObjectByName("Original")->GetLocalId());

	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
	Handle<Scene> reloaded = SceneSystem::LoadScene(Path("project://collided.scene"));
	ASSERT_TRUE(reloaded.IsValid());
	Handle<SsRefComponent> healed = reloaded->GetRoot(0)->GetComponent<SsRefComponent>();
	ASSERT_TRUE(healed.IsValid());
	ASSERT_TRUE(healed->target.IsValid());
	EXPECT_EQ(healed->target->name, "Original");
}

TEST_F(SceneSerializationFixture, CrossSceneHandleSerializesNull) {
	Handle<Scene> sceneA = SceneSystem::CreateScene("A");
	Handle<Scene> sceneB = SceneSystem::CreateScene("B");
	Handle<SceneObject> localObject = SceneSystem::CreateObject("Local", sceneA);
	Handle<SceneObject> foreignObject = SceneSystem::CreateObject("Foreign", sceneB);

	Handle<SsRefComponent> refs = localObject->AddComponent<SsRefComponent>();
	refs->target = foreignObject;

	ASSERT_TRUE(SceneSystem::SaveScene(sceneA, Path("project://cross.scene")));
	SceneSystem::UnloadScene(sceneA);
	SceneSystem::EndFrame();

	Handle<Scene> reloaded = SceneSystem::LoadScene(Path("project://cross.scene"));
	Handle<SsRefComponent> reloadedRefs = reloaded->FindObjectByName("Local")->GetComponent<SsRefComponent>();
	ASSERT_TRUE(reloadedRefs.IsValid());
	EXPECT_FALSE(reloadedRefs->target.IsValid());
}

TEST_F(SceneSerializationFixture, UnknownFieldsAndTypesAreTolerated) {
	str json = R"({
	"_type": "Scene",
	"formatVersion": 1,
	"engineHash": "0000000000000000",
	"name": "Tolerant",
	"nextLocalId": 10,
	"futureTopLevelKey": {"nested": true},
	"objects": [
		{
			"id": 1, "parent": 0, "type": ")" +
			   std::format("{:016x}", SceneObject::GetReflectType()->name.GetValue()) + R"(",
			"futureRowKey": 3,
			"fields": {"_type": "0000000000000000", "ffffffffffffffff": 12},
			"components": [
				{"id": 2, "type": "ffffffffffffffff", "fields": {}}
			]
		}
	]
})";
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://tolerant.scene")), json);

	Handle<Scene> scene = SceneSystem::LoadScene(Path("project://tolerant.scene"));
	ASSERT_TRUE(scene.IsValid());
	EXPECT_EQ(scene->name, "tolerant");
	EXPECT_EQ(scene->GetObjectCount(), 1u);
	ASSERT_EQ(scene->GetRoot(0)->GetComponentCount(), 1u);
	Component* preserved = scene->GetRoot(0)->GetComponentAt(0).Get();
	ASSERT_NE(preserved, nullptr);
	EXPECT_EQ(preserved->GetType(), UnresolvedComponent::GetReflectType());
	EXPECT_EQ(static_cast<UnresolvedComponent*>(preserved)->GetUnresolvedType(), StringHash(0xffffffffffffffffULL));
}

TEST_F(SceneSerializationFixture, CorruptedBinaryFailsCleanly) {
	{
		Buffer bytes(6, MemTag::Scene);
		bytes.data[0] = 'S';
		bytes.data[1] = 'L';
		FileSystem::WriteFile(FileSystem::Resolve(Path("project://corrupt1.scene")), bytes);
		EXPECT_FALSE(SceneSystem::LoadScene(Path("project://corrupt1.scene")).IsValid());
	}
	{
		Handle<Scene> scene = SceneSystem::CreateScene("ToCorrupt");
		SceneSystem::CreateObject("X", scene);
		ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://corrupt2.scene"), true));
		Path resolved = FileSystem::Resolve(Path("project://corrupt2.scene"));
		Buffer bytes = FileSystem::ReadFile(resolved, MemTag::Scene);
		ASSERT_TRUE(bytes && bytes.size > 16);
		Buffer half(bytes.size / 2, MemTag::Scene);
		std::memcpy(half.data, bytes.data, half.size);
		FileSystem::WriteFile(resolved, half);
		EXPECT_FALSE(SceneSystem::LoadScene(Path("project://corrupt2.scene")).IsValid());
	}
}

TEST_F(SceneSerializationFixture, LifecycleAfterIslandBuilds) {
	Handle<Scene> scene = SceneSystem::CreateScene("Lifecycle");
	Handle<SceneObject> node = SceneSystem::CreateObject("Node", scene);
	node->AddComponent<SsStatsComponent>();
	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://lifecycle.scene")));
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();

	Handle<Scene> reloaded = SceneSystem::LoadScene(Path("project://lifecycle.scene"));
	Handle<SsStatsComponent> stats = reloaded->FindObjectByName("Node")->GetComponent<SsStatsComponent>();
	ASSERT_TRUE(stats.IsValid());
	EXPECT_TRUE(stats->IsEffectivelyEnabled());
}

TEST_F(SceneSerializationFixture, SavingUnderNewNameRenamesOpenScene) {
	Handle<Scene> scene = SceneSystem::CreateScene("Untitled");
	SceneSystem::CreateObject("Node", scene);

	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://Corridor.scene")));
	EXPECT_EQ(scene->name, "Corridor");

	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://Atrium.scene")));
	EXPECT_EQ(scene->name, "Atrium");
}

TEST_F(SceneSerializationFixture, NamelessSceneUsesFileName) {
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://Nameless.scene")),
							  R"({"_type": "Scene", "formatVersion": 1, "nextLocalId": 1, "objects": []})");

	Handle<Scene> scene = SceneSystem::LoadScene(Path("project://Nameless.scene"));
	ASSERT_TRUE(scene.IsValid());
	EXPECT_EQ(scene->name, "Nameless");
}

TEST_F(SceneSerializationFixture, RenamingFileRenamesSceneOnNextOpen) {
	Handle<Scene> scene = SceneSystem::CreateScene("Untitled");
	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://Before.scene")));
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();

	std::error_code ec;
	std::filesystem::rename(_root / "Before.scene", _root / "After.scene", ec);
	ASSERT_FALSE(ec);

	Handle<Scene> reopened = SceneSystem::LoadScene(Path("project://After.scene"));
	ASSERT_TRUE(reopened.IsValid());
	EXPECT_EQ(reopened->name, "After");
}

TEST_F(SceneSerializationFixture, DotsDontTruncateName) {
	std::filesystem::create_directories(_root / "v1.2");
	Handle<Scene> scene = SceneSystem::CreateScene("Untitled");
	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://v1.2/Level.01.scene")));
	EXPECT_EQ(scene->name, "Level.01");

	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
	Handle<Scene> reopened = SceneSystem::LoadScene(Path("project://v1.2/Level.01.scene"));
	ASSERT_TRUE(reopened.IsValid());
	EXPECT_EQ(reopened->name, "Level.01");
}

TEST_F(SceneSerializationFixture, SnapshotCarriesName) {
	Handle<Scene> scene = SceneSystem::CreateScene("Untitled");
	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://Arena.scene")));
	ASSERT_EQ(scene->name, "Arena");

	Buffer snapshot;
	ASSERT_TRUE(SceneSystem::SaveSceneToMemory(scene, snapshot));
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();

	Handle<Scene> restored = SceneSystem::LoadSceneFromMemory(snapshot);
	ASSERT_TRUE(restored.IsValid());
	EXPECT_EQ(restored->name, "Arena");
}

TEST_F(SceneSerializationFixture, AsyncNamesLikeSync) {
	JobSystem::Initialize();
	{
		Handle<Scene> original = SceneSystem::CreateScene("Untitled");
		ASSERT_TRUE(SceneSystem::SaveScene(original, Path("project://Deferred.scene")));
		SceneSystem::UnloadScene(original);
		SceneSystem::EndFrame();

		Handle<SceneLoadOperation> operation = SceneSystem::LoadSceneAsync(Path("project://Deferred.scene"));
		ASSERT_TRUE(operation.IsValid());
		operation->WaitForCompletion();
		ASSERT_TRUE(operation->GetScene().IsValid());
		EXPECT_EQ(operation->GetScene()->name, "Deferred");
	}
	SceneSystem::Terminate();
	SceneSystem::Initialize();
	JobSystem::Terminate();
}

TEST_F(SceneSerializationFixture, AsyncLoadMatchesSyncResult) {
	JobSystem::Initialize();
	{
		Handle<Scene> original = BuildReferenceScene();
		ASSERT_TRUE(SceneSystem::SaveScene(original, Path("project://async.scene")));
		SceneSystem::UnloadScene(original);
		SceneSystem::EndFrame();

		Handle<SceneLoadOperation> operation = SceneSystem::LoadSceneAsync(Path("project://async.scene"));
		ASSERT_TRUE(operation.IsValid());
		operation->WaitForCompletion();

		EXPECT_TRUE(operation->IsDone());
		EXPECT_EQ(operation->GetState(), SceneLoadState::Done);
		EXPECT_FLOAT_EQ(operation->GetProgress(), 1.0f);
		VerifyReferenceScene(operation->GetScene(), Guid{});
	}
	SceneSystem::Terminate();
	SceneSystem::Initialize();
	JobSystem::Terminate();
}

TEST_F(SceneSerializationFixture, TinyBudgetSpansFrames) {
	JobSystem::Initialize();
	{
		Handle<Scene> original = BuildReferenceScene();
		ASSERT_TRUE(SceneSystem::SaveScene(original, Path("project://sliced.scene")));
		SceneSystem::UnloadScene(original);
		SceneSystem::EndFrame();

		SceneSystem::SetAsyncLoadBudget(0.0);
		Handle<SceneLoadOperation> operation = SceneSystem::LoadSceneAsync(Path("project://sliced.scene"));
		ASSERT_TRUE(operation.IsValid());

		f32 lastProgress = 0.0f;
		auto handoffStart = std::chrono::steady_clock::now();
		while (!operation->IsDone() && operation->GetState() != SceneLoadState::Integrating &&
			   std::chrono::steady_clock::now() - handoffStart < std::chrono::seconds(30)) {
			SceneSystem::BeginFrame();
			f32 progress = operation->GetProgress();
			EXPECT_GE(progress, lastProgress);
			lastProgress = progress;
			std::this_thread::sleep_for(std::chrono::milliseconds(1));
		}
		ASSERT_EQ(operation->GetState(), SceneLoadState::Integrating);

		u32 integrationPumps = 0;
		for (u32 guard = 0; guard < 100000 && !operation->IsDone(); ++guard) {
			SceneLoadState before = operation->GetState();
			SceneSystem::BeginFrame();
			f32 progress = operation->GetProgress();
			EXPECT_GE(progress, lastProgress);
			lastProgress = progress;
			if (before == SceneLoadState::Integrating) {
				++integrationPumps;
			}
		}
		EXPECT_TRUE(operation->IsDone());
		EXPECT_GT(integrationPumps, 3u);
		VerifyReferenceScene(operation->GetScene(), Guid{});
		SceneSystem::SetAsyncLoadBudget(0.002);
	}
	SceneSystem::Terminate();
	SceneSystem::Initialize();
	JobSystem::Terminate();
}

TEST_F(SceneSerializationFixture, ActivationGateHoldsSceneInvisible) {
	JobSystem::Initialize();
	{
		Handle<Scene> original = BuildReferenceScene();
		ASSERT_TRUE(SceneSystem::SaveScene(original, Path("project://gated.scene")));
		SceneSystem::UnloadScene(original);
		SceneSystem::EndFrame();

		Handle<SceneLoadOperation> operation = SceneSystem::LoadSceneAsync(Path("project://gated.scene"));
		ASSERT_TRUE(operation.IsValid());
		operation->SetActivationAllowed(false);
		operation->WaitForCompletion();

		EXPECT_EQ(operation->GetState(), SceneLoadState::AwaitingActivation);
		Handle<Scene> gated = operation->GetScene();
		ASSERT_TRUE(gated.IsValid());
		bool listed = false;
		for (const Handle<Scene>& handle : SceneSystem::GetLoadedScenes()) {
			listed = listed || handle.GetInstanceId() == gated.GetInstanceId();
		}
		EXPECT_FALSE(listed);
		Handle<SsRefComponent> playerRefs = gated->FindObjectByName("Player")->GetComponent<SsRefComponent>();
		ASSERT_TRUE(playerRefs.IsValid());
		EXPECT_FALSE(playerRefs->IsEffectivelyEnabled());

		operation->SetActivationAllowed(true);
		SceneSystem::BeginFrame();
		EXPECT_EQ(operation->GetState(), SceneLoadState::Done);
		EXPECT_TRUE(playerRefs->IsEffectivelyEnabled());
		listed = false;
		for (const Handle<Scene>& handle : SceneSystem::GetLoadedScenes()) {
			listed = listed || handle.GetInstanceId() == gated.GetInstanceId();
		}
		EXPECT_TRUE(listed);
	}
	SceneSystem::Terminate();
	SceneSystem::Initialize();
	JobSystem::Terminate();
}

TEST_F(SceneSerializationFixture, ConcurrentIndependentLoads) {
	JobSystem::Initialize();
	{
		Handle<Scene> first = SceneSystem::CreateScene("First");
		SceneSystem::CreateObject("Alpha", first);
		ASSERT_TRUE(SceneSystem::SaveScene(first, Path("project://first.scene")));
		Handle<Scene> second = SceneSystem::CreateScene("Second");
		SceneSystem::CreateObject("Beta", second);
		SceneSystem::CreateObject("Gamma", second);
		ASSERT_TRUE(SceneSystem::SaveScene(second, Path("project://second.scene")));
		SceneSystem::UnloadAllScenes();
		SceneSystem::EndFrame();

		Handle<SceneLoadOperation> opFirst = SceneSystem::LoadSceneAsync(Path("project://first.scene"));
		Handle<SceneLoadOperation> opSecond = SceneSystem::LoadSceneAsync(Path("project://second.scene"));
		ASSERT_TRUE(opFirst.IsValid() && opSecond.IsValid());
		opFirst->WaitForCompletion();
		opSecond->WaitForCompletion();

		ASSERT_EQ(opFirst->GetState(), SceneLoadState::Done);
		ASSERT_EQ(opSecond->GetState(), SceneLoadState::Done);
		EXPECT_EQ(opFirst->GetScene()->GetObjectCount(), 1u);
		EXPECT_EQ(opSecond->GetScene()->GetObjectCount(), 2u);
		EXPECT_TRUE(opFirst->GetScene()->FindObjectByName("Alpha").IsValid());
		EXPECT_TRUE(opSecond->GetScene()->FindObjectByName("Gamma").IsValid());
		EXPECT_EQ(SceneSystem::GetLoadedScenes().size(), 2u);
	}
	SceneSystem::Terminate();
	SceneSystem::Initialize();
	JobSystem::Terminate();
}

TEST_F(SceneSerializationFixture, UnloadWhileLoadingCancelsCleanly) {
	JobSystem::Initialize();
	{
		Handle<Scene> original = BuildReferenceScene();
		ASSERT_TRUE(SceneSystem::SaveScene(original, Path("project://cancelled.scene")));
		SceneSystem::UnloadScene(original);
		SceneSystem::EndFrame();

		Handle<SceneLoadOperation> operation = SceneSystem::LoadSceneAsync(Path("project://cancelled.scene"));
		ASSERT_TRUE(operation.IsValid());
		operation->SetActivationAllowed(false);
		operation->WaitForCompletion();
		ASSERT_EQ(operation->GetState(), SceneLoadState::AwaitingActivation);

		Handle<Scene> gated = operation->GetScene();
		SceneSystem::UnloadScene(gated);
		SceneSystem::EndFrame();
		EXPECT_FALSE(gated.IsValid());

		operation->SetActivationAllowed(true);
		SceneSystem::BeginFrame();
		EXPECT_EQ(operation->GetState(), SceneLoadState::Failed);
		EXPECT_FALSE(operation->GetScene().IsValid());
		EXPECT_FALSE(operation->GetError().empty());
	}
	SceneSystem::Terminate();
	SceneSystem::Initialize();
	JobSystem::Terminate();
}

TEST_F(SceneSerializationFixture, AsyncLoadOfMissingFileFails) {
	JobSystem::Initialize();
	{
		Handle<SceneLoadOperation> operation = SceneSystem::LoadSceneAsync(Path("project://does_not_exist.scene"));
		ASSERT_TRUE(operation.IsValid());
		operation->WaitForCompletion();
		EXPECT_EQ(operation->GetState(), SceneLoadState::Failed);
		EXPECT_FALSE(operation->GetError().empty());
	}
	SceneSystem::Terminate();
	SceneSystem::Initialize();
	JobSystem::Terminate();
}

TEST_F(SceneSerializationFixture, ComponentOrderSurvivesARoundTrip) {
	Handle<Scene> scene = SceneSystem::CreateScene("Ordering");
	Handle<SceneObject> object = SceneSystem::CreateObject("Subject", scene);
	Handle<SsStatsComponent> first = object->AddComponent<SsStatsComponent>();
	object->AddComponent<SsRefComponent>();
	Handle<SsStatsComponent> last = object->AddComponent<SsStatsComponent>();
	first->health = 1;
	last->health = 3;

	object->SetComponentIndex(Handle<Component>(last.Get()), 0);
	ASSERT_EQ(object->GetComponentAt(0), Handle<Component>(last.Get()));

	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://order.scene")));
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();

	Handle<Scene> reloaded = SceneSystem::LoadScene(Path("project://order.scene"));
	ASSERT_TRUE(reloaded.IsValid());
	Handle<SceneObject> restored = reloaded->FindObjectByName("Subject");
	ASSERT_TRUE(restored.IsValid());
	ASSERT_EQ(restored->GetComponentCount(), 3u);

	auto typeAt = [&](u32 i) {
		Component* component = restored->GetComponentAt(i).Get();
		return component && component->GetType() ? component->GetType()->name : StringHash{};
	};
	EXPECT_EQ(typeAt(0), StringHash("SsStatsComponent"));
	EXPECT_EQ(typeAt(1), StringHash("SsStatsComponent"));
	EXPECT_EQ(typeAt(2), StringHash("SsRefComponent"));

	SsStatsComponent* slot0 = static_cast<SsStatsComponent*>(restored->GetComponentAt(0).Get());
	SsStatsComponent* slot1 = static_cast<SsStatsComponent*>(restored->GetComponentAt(1).Get());
	ASSERT_NE(slot0, nullptr);
	ASSERT_NE(slot1, nullptr);
	EXPECT_EQ(slot0->health, 3);
	EXPECT_EQ(slot1->health, 1);
}

namespace {

	const StringHash VanishedType{"SsVanishedComponent"};

	constexpr const char* PAYLOAD_MARKER = "cafef00dcafef00d";

	str HexOf(u64 value) {
		return std::format("{:016x}", value);
	}

	str SceneWithAnUnresolvableComponent() {
		str objectType = HexOf(SceneObject::GetReflectType()->name.GetValue());
		str statsType = HexOf(SsStatsComponent::GetReflectType()->name.GetValue());
		str refType = HexOf(SsRefComponent::GetReflectType()->name.GetValue());
		return R"({
	"_type": "Scene",
	"formatVersion": 1,
	"engineHash": "0000000000000000",
	"name": "Vanished",
	"nextLocalId": 5,
	"objects": [
		{
			"id": 1, "parent": 0, "type": ")" +
			   objectType +
			   R"(",
			"fields": {"_type": ")" +
			   objectType + R"(", ")" + HexOf(StringHash("name").GetValue()) +
			   R"(": "Subject"},
			"components": [
				{"id": 2, "type": ")" +
			   statsType + R"(", "fields": {"_type": ")" + statsType + R"(", ")" +
			   HexOf(StringHash("health").GetValue()) + R"(": 11}},
				{"id": 3, "type": ")" +
			   HexOf(VanishedType.GetValue()) +
			   R"(", "fields": {"_type": "0000000000000000", ")" + PAYLOAD_MARKER + R"(": 7}},
				{"id": 4, "type": ")" +
			   refType + R"(", "fields": {"_type": ")" + refType + R"("}}
			]
		}
	]
})";
	}

	StringHash TypeAt(const Handle<SceneObject>& object, u32 index) {
		Component* component = object->GetComponentAt(index).Get();
		return component && component->GetType() ? component->GetType()->name : StringHash{};
	}

	Handle<Scene> AuthorThenLoseTheType(const Path& file, bool binary) {
		Handle<Scene> scene = SceneSystem::CreateScene("Fragile");
		Handle<SceneObject> anchor = SceneSystem::CreateObject("Anchor", scene);
		Handle<SceneObject> subject = SceneSystem::CreateObject("Subject", scene);
		{
			ScopedFragileType present;
			subject->AddComponent<SsStatsComponent>()->health = 55.0f;
			Handle<SsFragileComponent> fragile = subject->AddComponent<SsFragileComponent>();
			fragile->charges = 3;
			fragile->sigil = "ward";
			fragile->anchor = anchor;
			EXPECT_TRUE(SceneSystem::SaveScene(scene, file, binary));
		}
		SceneSystem::UnloadScene(scene);
		SceneSystem::EndFrame();

		return SceneSystem::LoadScene(file);
	}

	str DecodePreservedPayload(const str& sceneText) {
		size_t key = sceneText.find("\"_opaque\"");
		if (key == str::npos) {
			return {};
		}
		size_t open = sceneText.find('"', sceneText.find(':', key));
		size_t close = sceneText.find('"', open + 1);
		if (open == str::npos || close == str::npos) {
			return {};
		}
		str hex = sceneText.substr(open + 1, close - open - 1);
		str out;
		for (size_t i = 0; i + 1 < hex.size(); i += 2) {
			out += static_cast<char>(std::stoul(hex.substr(i, 2), nullptr, 16));
		}
		return out;
	}

} // namespace

TEST_F(SceneSerializationFixture, UnresolvableComponentKeepsItsSlotAndOrder) {
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://vanished.scene")), SceneWithAnUnresolvableComponent());

	Handle<Scene> scene = SceneSystem::LoadScene(Path("project://vanished.scene"));
	ASSERT_TRUE(scene.IsValid());
	Handle<SceneObject> subject = scene->FindObjectByName("Subject");
	ASSERT_TRUE(subject.IsValid());

	ASSERT_EQ(subject->GetComponentCount(), 3u);
	EXPECT_EQ(TypeAt(subject, 0), StringHash("SsStatsComponent"));
	EXPECT_EQ(TypeAt(subject, 1), StringHash("UnresolvedComponent"));
	EXPECT_EQ(TypeAt(subject, 2), StringHash("SsRefComponent"));

	auto* stats = static_cast<SsStatsComponent*>(subject->GetComponentAt(0).Get());
	ASSERT_NE(stats, nullptr);
	EXPECT_FLOAT_EQ(stats->health, 11.0f);

	auto* preserved = static_cast<UnresolvedComponent*>(subject->GetComponentAt(1).Get());
	ASSERT_NE(preserved, nullptr);
	EXPECT_EQ(preserved->GetUnresolvedType(), VanishedType);
}

TEST_F(SceneSerializationFixture, UnresolvableComponentSurvivesSaveAndReload) {
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://vanished.scene")), SceneWithAnUnresolvableComponent());
	Handle<Scene> scene = SceneSystem::LoadScene(Path("project://vanished.scene"));
	ASSERT_TRUE(scene.IsValid());

	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://resaved.scene")));
	str first = FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://resaved.scene")), MemTag::Scene);

	EXPECT_NE(first.find(HexOf(VanishedType.GetValue())), str::npos);
	EXPECT_EQ(first.find(HexOf(StringHash("UnresolvedComponent").GetValue())), str::npos);
	EXPECT_NE(DecodePreservedPayload(first).find(PAYLOAD_MARKER), str::npos);

	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();

	Handle<Scene> reloaded = SceneSystem::LoadScene(Path("project://resaved.scene"));
	ASSERT_TRUE(reloaded.IsValid());
	Handle<SceneObject> subject = reloaded->FindObjectByName("Subject");
	ASSERT_TRUE(subject.IsValid());
	ASSERT_EQ(subject->GetComponentCount(), 3u);
	EXPECT_EQ(TypeAt(subject, 1), StringHash("UnresolvedComponent"));

	ASSERT_TRUE(SceneSystem::SaveScene(reloaded, Path("project://resaved.scene")));
	str second = FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://resaved.scene")), MemTag::Scene);
	EXPECT_EQ(first, second);
}

TEST_F(SceneSerializationFixture, UnresolvableComponentSurvivesBinarySnapshotRoundTrip) {
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://vanished.scene")), SceneWithAnUnresolvableComponent());
	Handle<Scene> scene = SceneSystem::LoadScene(Path("project://vanished.scene"));
	ASSERT_TRUE(scene.IsValid());

	Buffer snapshot;
	ASSERT_TRUE(SceneSystem::SaveSceneToMemory(scene, snapshot));
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();

	Handle<Scene> restored = SceneSystem::LoadSceneFromMemory(snapshot);
	ASSERT_TRUE(restored.IsValid());
	Handle<SceneObject> subject = restored->FindObjectByName("Subject");
	ASSERT_TRUE(subject.IsValid());
	ASSERT_EQ(subject->GetComponentCount(), 3u);
	EXPECT_EQ(TypeAt(subject, 1), StringHash("UnresolvedComponent"));

	ASSERT_TRUE(SceneSystem::SaveScene(restored, Path("project://afterplay.scene")));
	str text = FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://afterplay.scene")), MemTag::Scene);
	EXPECT_NE(text.find(HexOf(VanishedType.GetValue())), str::npos);
	EXPECT_NE(DecodePreservedPayload(text).find(PAYLOAD_MARKER), str::npos);
}

TEST_F(SceneSerializationFixture, UnresolvableComponentSurvivesBinaryFileRoundTrip) {
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://vanished.scene")), SceneWithAnUnresolvableComponent());
	Handle<Scene> scene = SceneSystem::LoadScene(Path("project://vanished.scene"));
	ASSERT_TRUE(scene.IsValid());
	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://vanished.bscene"), true));
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();

	Handle<Scene> fromBinary = SceneSystem::LoadScene(Path("project://vanished.bscene"));
	ASSERT_TRUE(fromBinary.IsValid());
	Handle<SceneObject> subject = fromBinary->FindObjectByName("Subject");
	ASSERT_TRUE(subject.IsValid());
	ASSERT_EQ(subject->GetComponentCount(), 3u);
	EXPECT_EQ(TypeAt(subject, 1), StringHash("UnresolvedComponent"));
	ASSERT_TRUE(SceneSystem::SaveScene(fromBinary, Path("project://again.bscene"), true));

	SceneSystem::UnloadScene(fromBinary);
	SceneSystem::EndFrame();
	Handle<Scene> again = SceneSystem::LoadScene(Path("project://again.bscene"));
	ASSERT_TRUE(again.IsValid());

	ASSERT_TRUE(SceneSystem::SaveScene(again, Path("project://backtotext.scene")));
	str text = FileSystem::ReadTextFile(FileSystem::Resolve(Path("project://backtotext.scene")), MemTag::Scene);
	EXPECT_NE(text.find(HexOf(VanishedType.GetValue())), str::npos);
	EXPECT_NE(DecodePreservedPayload(text).find(PAYLOAD_MARKER), str::npos);
}

TEST_F(SceneSerializationFixture, UnresolvableComponentHoldsSlotThroughReorder) {
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://vanished.scene")), SceneWithAnUnresolvableComponent());
	Handle<Scene> scene = SceneSystem::LoadScene(Path("project://vanished.scene"));
	ASSERT_TRUE(scene.IsValid());
	Handle<SceneObject> subject = scene->FindObjectByName("Subject");
	ASSERT_TRUE(subject.IsValid());

	subject->SetComponentIndex(subject->GetComponentAt(1), 0);
	ASSERT_EQ(TypeAt(subject, 0), StringHash("UnresolvedComponent"));

	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://reordered.scene")));
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();

	Handle<Scene> reloaded = SceneSystem::LoadScene(Path("project://reordered.scene"));
	ASSERT_TRUE(reloaded.IsValid());
	Handle<SceneObject> restored = reloaded->FindObjectByName("Subject");
	ASSERT_TRUE(restored.IsValid());
	ASSERT_EQ(restored->GetComponentCount(), 3u);
	EXPECT_EQ(TypeAt(restored, 0), StringHash("UnresolvedComponent"));
	EXPECT_EQ(TypeAt(restored, 1), StringHash("SsStatsComponent"));
	EXPECT_EQ(TypeAt(restored, 2), StringHash("SsRefComponent"));
}

TEST_F(SceneSerializationFixture, PreservedRowsDieWithOwner) {
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://vanished.scene")), SceneWithAnUnresolvableComponent());
	Handle<Scene> scene = SceneSystem::LoadScene(Path("project://vanished.scene"));
	ASSERT_TRUE(scene.IsValid());
	Handle<SceneObject> first = scene->FindObjectByName("Subject");
	ASSERT_TRUE(first.IsValid());

	Handle<SceneObject> second = SceneSystem::Instantiate(first, {});
	ASSERT_TRUE(second.IsValid());
	ASSERT_EQ(second->GetComponentCount(), 3u);
	EXPECT_EQ(TypeAt(second, 1), StringHash("UnresolvedComponent"));
	EXPECT_EQ(static_cast<UnresolvedComponent*>(second->GetComponentAt(1).Get())->GetUnresolvedType(), VanishedType);

	Handle<SceneObject> doomed = SceneSystem::Instantiate(first, {});
	ASSERT_TRUE(doomed.IsValid());
	SceneSystem::Destroy(doomed);
	SceneSystem::EndFrame();
	EXPECT_FALSE(doomed.IsValid());

	ASSERT_TRUE(SceneSystem::SaveScene(scene, Path("project://pairs.scene")));
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();

	Handle<Scene> reloaded = SceneSystem::LoadScene(Path("project://pairs.scene"));
	ASSERT_TRUE(reloaded.IsValid());
	EXPECT_EQ(reloaded->GetRootCount(), 2u);
	for (u32 i = 0; i < reloaded->GetRootCount(); ++i) {
		Handle<SceneObject> root = reloaded->GetRoot(i);
		ASSERT_EQ(root->GetComponentCount(), 3u);
		EXPECT_EQ(TypeAt(root, 1), StringHash("UnresolvedComponent"));
	}
}

TEST_F(SceneSerializationFixture, PreservedRowComesBackWhenTypeDoes) {
	Handle<Scene> absent = AuthorThenLoseTheType(Path("project://fragile.scene"), false);
	ASSERT_TRUE(absent.IsValid());
	Handle<SceneObject> preserved = absent->FindObjectByName("Subject");
	ASSERT_TRUE(preserved.IsValid());
	ASSERT_EQ(preserved->GetComponentCount(), 2u);
	ASSERT_EQ(TypeAt(preserved, 1), StringHash("UnresolvedComponent"));

	ASSERT_TRUE(SceneSystem::SaveScene(absent, Path("project://carried.scene")));
	SceneSystem::UnloadScene(absent);
	SceneSystem::EndFrame();

	ScopedFragileType rebuilt;
	Handle<Scene> back = SceneSystem::LoadScene(Path("project://carried.scene"));
	ASSERT_TRUE(back.IsValid());
	Handle<SceneObject> subject = back->FindObjectByName("Subject");
	ASSERT_TRUE(subject.IsValid());
	ASSERT_EQ(subject->GetComponentCount(), 2u);
	ASSERT_EQ(TypeAt(subject, 1), StringHash("SsFragileComponent"));

	auto* live = static_cast<SsFragileComponent*>(subject->GetComponentAt(1).Get());
	ASSERT_NE(live, nullptr);
	EXPECT_EQ(live->charges, 3);
	EXPECT_EQ(live->sigil, "ward");
	EXPECT_EQ(live->anchor.Get(), back->FindObjectByName("Anchor").Get());
	EXPECT_FLOAT_EQ(static_cast<SsStatsComponent*>(subject->GetComponentAt(0).Get())->health, 55.0f);
}

TEST_F(SceneSerializationFixture, PreservedRowReturnsFromBlob) {
	Handle<Scene> absent = AuthorThenLoseTheType(Path("project://fragile.bscene"), true);
	ASSERT_TRUE(absent.IsValid());

	ASSERT_TRUE(SceneSystem::SaveScene(absent, Path("project://carried.scene")));
	SceneSystem::UnloadScene(absent);
	SceneSystem::EndFrame();

	ScopedFragileType rebuilt;
	Handle<Scene> back = SceneSystem::LoadScene(Path("project://carried.scene"));
	ASSERT_TRUE(back.IsValid());
	Handle<SceneObject> subject = back->FindObjectByName("Subject");
	ASSERT_TRUE(subject.IsValid());
	ASSERT_EQ(subject->GetComponentCount(), 2u);
	ASSERT_EQ(TypeAt(subject, 1), StringHash("SsFragileComponent"));

	auto* live = static_cast<SsFragileComponent*>(subject->GetComponentAt(1).Get());
	ASSERT_NE(live, nullptr);
	EXPECT_EQ(live->charges, 3);
	EXPECT_EQ(live->sigil, "ward");
	EXPECT_EQ(live->anchor.Get(), back->FindObjectByName("Anchor").Get());
}

namespace {

	class SsCapturingSink : public ILogSink {
	public:
		void ReceiveLog(const LogEvent& event) override {
			{
				std::lock_guard<std::mutex> lock(_mutex);
				_messages.push_back(str(event.message));
			}
			_cv.notify_all();
		}
		void Flush() override {}

		u32 CountMatching(std::string_view needle) const {
			std::lock_guard<std::mutex> lock(_mutex);
			u32 count = 0;
			for (const str& message : _messages) {
				if (message.find(needle) != str::npos) {
					++count;
				}
			}
			return count;
		}

		bool Saw(std::string_view needle, std::chrono::milliseconds timeout = std::chrono::milliseconds(2000)) const {
			std::unique_lock<std::mutex> lock(_mutex);
			return _cv.wait_for(lock, timeout, [&] {
				for (const str& message : _messages) {
					if (message.find(needle) != str::npos) {
						return true;
					}
				}
				return false;
			});
		}
	private:
		mutable std::mutex _mutex;
		mutable std::condition_variable _cv;
		std::vector<str> _messages;
	};

} // namespace

TEST_F(SceneSerializationFixture, SavingReportsRowsItCarriedNotBlocking) {
	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	LogManager::SetGlobalLevel(LogLevel::Trace);
	auto sink = std::make_shared<SsCapturingSink>();
	LogManager::AddSink(sink);

	Handle<Scene> absent = AuthorThenLoseTheType(Path("project://fragile.scene"), false);
	ASSERT_TRUE(absent.IsValid());
	EXPECT_TRUE(SceneSystem::SaveScene(absent, Path("project://carried.scene")));

	EXPECT_TRUE(sink->Saw("saved with 1 component row(s) of an unloaded type"));

	LogManager::RemoveAllSinks();
	LogManager::Terminate();
	sink.reset();
	JobSystem::Terminate();
}

namespace {

	Handle<Scene> AuthorThreeThenLoseTheType(const Path& file) {
		Handle<Scene> scene = SceneSystem::CreateScene("Brittle");
		{
			ScopedFragileType present;
			Handle<SceneObject> parent;
			for (i32 i = 0; i < 3; ++i) {
				Handle<SceneObject> subject = SceneSystem::CreateObject("Subject", scene);
				subject->AddComponent<SsFragileComponent>()->charges = i;
				if (parent.IsValid()) {
					subject->SetParent(parent);
				}
				parent = subject;
			}
			EXPECT_TRUE(SceneSystem::SaveScene(scene, file, false));
		}
		SceneSystem::UnloadScene(scene);
		SceneSystem::EndFrame();

		return SceneSystem::LoadScene(file);
	}

} // namespace

TEST_F(SceneSerializationFixture, UnresolvedRowsReportedOnce) {
	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	LogManager::SetGlobalLevel(LogLevel::Trace);
	auto sink = std::make_shared<SsCapturingSink>();
	LogManager::AddSink(sink);

	Handle<Scene> brittle = AuthorThreeThenLoseTheType(Path("project://brittle.scene"));
	ASSERT_TRUE(brittle.IsValid());
	ASSERT_TRUE(sink->Saw("loaded with 3 component row(s) of 1 unloaded type(s)"));

	LogManager::Terminate();

	EXPECT_EQ(sink->CountMatching("component row(s) of"), 1u);

	LogManager::RemoveAllSinks();
	sink.reset();
	JobSystem::Terminate();
}

namespace {

	class OutageCapturingSink : public ILogSink {
	public:
		static constexpr const char* SENTINEL = "scene-outage-sink-sentinel";

		void ReceiveLog(const LogEvent& event) override {
			std::lock_guard<std::mutex> lock(_mutex);
			if (event.message.find(SENTINEL) != str::npos) {
				_armed = true;
				return;
			}
			if (!_armed) {
				return;
			}
			_messages.push_back(str(event.message));
		}
		void Flush() override {}

		bool Armed() const {
			std::lock_guard<std::mutex> lock(_mutex);
			return _armed;
		}

		u32 CountMatching(std::string_view needle) const {
			std::lock_guard<std::mutex> lock(_mutex);
			u32 count = 0;
			for (const str& message : _messages) {
				if (message.find(needle) != str::npos) {
					++count;
				}
			}
			return count;
		}

		str Joined() const {
			std::lock_guard<std::mutex> lock(_mutex);
			str all;
			for (const str& message : _messages) {
				all += message + "\n";
			}
			return all;
		}
	private:
		mutable std::mutex _mutex;
		std::vector<str> _messages;
		bool _armed = false;
	};

	bool ArmOutageSink(const std::shared_ptr<OutageCapturingSink>& sink) {
		SLT_LOG_WARN("Scene", "{}", OutageCapturingSink::SENTINEL);
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (std::chrono::steady_clock::now() < deadline) {
			if (sink->Armed()) {
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		return false;
	}

} // namespace

TEST_F(SceneSerializationFixture, OutageReportedPerLoad) {
	str json = R"({
	"_type": "Scene",
	"formatVersion": 1,
	"name": "outage",
	"nextLocalId": 40,
	"objects": [
		{
			"id": 1, "parent": 0,
			"type": ")" +
			   std::format("{:016x}", SceneObject::GetReflectType()->name.GetValue()) + R"(",
			"fields": {},
			"components": [
				{"id": 2, "type": "aaaaaaaaaaaaaaa1", "fields": {}},
				{"id": 3, "type": "aaaaaaaaaaaaaaa1", "fields": {}},
				{"id": 4, "type": "bbbbbbbbbbbbbbb2", "fields": {}}
			]
		},
		{
			"id": 10, "parent": 0,
			"type": ")" +
			   std::format("{:016x}", SceneObject::GetReflectType()->name.GetValue()) + R"(",
			"fields": {},
			"components": [
				{"id": 11, "type": "aaaaaaaaaaaaaaa1", "fields": {}},
				{"id": 12, "type": "bbbbbbbbbbbbbbb2", "fields": {}},
				{"id": 13, "type": "bbbbbbbbbbbbbbb2", "fields": {}}
			]
		}
	]
})";
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://outage.scene")), json);

	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	LogManager::SetGlobalLevel(LogLevel::Trace);
	auto sink = std::make_shared<OutageCapturingSink>();
	LogManager::AddSink(sink);
	ASSERT_TRUE(ArmOutageSink(sink));

	Handle<Scene> scene = SceneSystem::LoadScene(Path("project://outage.scene"));

	LogManager::Terminate();
	ASSERT_GT(sink->CountMatching(""), 0u);

	ASSERT_TRUE(scene.IsValid());
	ASSERT_EQ(scene->GetObjectCount(), 2u);
	EXPECT_EQ(scene->GetRoot(0)->GetComponentCount(), 3u);
	EXPECT_EQ(scene->GetRoot(1)->GetComponentCount(), 3u);

	EXPECT_EQ(sink->CountMatching("AddComponent: null type"), 0u) << sink->Joined();
	EXPECT_EQ(sink->CountMatching("unloaded type(s)"), 1u) << sink->Joined();

	sink.reset();
	JobSystem::Terminate();
}

TEST_F(SceneSerializationFixture, UnloadedTypeNamed) {
	const StringHash absent{"BallGun"};
	str json = R"({
	"_type": "Scene",
	"formatVersion": 1,
	"name": "named-outage",
	"nextLocalId": 40,
	"objects": [
		{
			"id": 1, "parent": 0,
			"type": ")" +
			   std::format("{:016x}", SceneObject::GetReflectType()->name.GetValue()) + R"(",
			"fields": {},
			"components": [
				{"id": 2, "type": ")" +
			   std::format("{:016x}", absent.GetValue()) + R"(",
				 "fields": {"_type": "BallGun", "_enabledSelf": true, "_muzzleSpeed": 40}}
			]
		}
	]
})";
	FileSystem::WriteTextFile(FileSystem::Resolve(Path("project://named.scene")), json);

	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	LogManager::SetGlobalLevel(LogLevel::Trace);
	auto sink = std::make_shared<OutageCapturingSink>();
	LogManager::AddSink(sink);
	ASSERT_TRUE(ArmOutageSink(sink));

	ASSERT_EQ(TypeRegistry::FindType(absent), nullptr);
	Handle<Scene> scene = SceneSystem::LoadScene(Path("project://named.scene"));

	LogManager::Terminate();
	ASSERT_GT(sink->CountMatching(""), 0u);
	ASSERT_TRUE(scene.IsValid());
	ASSERT_EQ(scene->GetRoot(0)->GetComponentCount(), 1u);

	EXPECT_EQ(sink->CountMatching("BallGun"), 1u) << sink->Joined();

	EXPECT_EQ(sink->CountMatching(std::format("{:#018x}", absent.GetValue())), 1u) << sink->Joined();

	sink.reset();
	JobSystem::Terminate();
}
