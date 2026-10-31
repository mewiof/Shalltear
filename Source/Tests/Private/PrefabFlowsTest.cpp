#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Graphics/PrimitiveMesh.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Graphics/Camera.h"
#include "Shalltear/Core/Graphics/Light.h"
#include "Shalltear/Core/Scene.h"

#include "Shalltear/Editor/AssetBrowserOps.h"
#include "Shalltear/Editor/AuthoringDefaults.h"
#include "Shalltear/Editor/EditorCommands.h"
#include "Shalltear/Editor/EditTarget.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorViewportOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/SceneAnnotations.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

	class PrefabFlowsFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		size_t _sceneBytesBaseline = 0;
		size_t _editorBytesBaseline = 0;
		Handle<Scene> _scene;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_prefabflows_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root / "Scenes");

			TypeRegistry::InitializePrimitives();
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();

			InitializeStack();
			TerminateStack();
			_sceneBytesBaseline = MemoryManager::GetBytesForTag(MemTag::Scene);
			_editorBytesBaseline = MemoryManager::GetBytesForTag(MemTag::Editor);

			InitializeStack();
			_scene = SceneSystem::CreateScene("Fixture");
		}

		void TearDown() override {
			TerminateStack();
			AssetDatabase::CollectGarbage();
			EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Scene), _sceneBytesBaseline);
			EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Editor), _editorBytesBaseline);
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		static void InitializeStack() {
			SceneSystem::Initialize();
			EditorSceneOps::Initialize();
			EditorSelection::Initialize();
			UndoSystem::Initialize();
			SceneAnnotations::Initialize();
		}

		static void TerminateStack() {
			AuthoringDefaults::Shutdown();
			SceneAnnotations::Terminate();
			UndoSystem::Terminate();
			EditorSelection::Terminate();
			EditorSceneOps::Terminate();
			SceneSystem::Terminate();
		}

		void WriteSceneFile(const str& path, const str& objectName) {
			Handle<Scene> source = SceneSystem::CreateScene("Source");
			SceneSystem::CreateObject(objectName, source);
			ASSERT_TRUE(SceneSystem::SaveScene(source, Path(path)));
			SceneSystem::UnloadScene(source);
			SceneSystem::EndFrame();
		}

		Guid WritePrefabFile(const str& path = "project://Assets/Prefabs/Unit.prefab", const str& rootName = "Unit") {
			Handle<SceneObject> unit = SceneSystem::CreateObject(rootName, _scene);
			SceneSystem::CreateObject("Body", _scene, unit);
			Guid guid = EditorSceneOps::CreatePrefabFrom(unit, Path(path));
			EXPECT_TRUE(guid.IsValid());
			SceneSystem::Destroy(unit);
			SceneSystem::EndFrame();
			EditorSceneOps::MarkClean(_scene);
			return guid;
		}

		Guid StageAndImportModel() {
			std::error_code ec;
			std::filesystem::path probe = std::filesystem::current_path(ec);
			std::filesystem::path source;
			for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
				std::filesystem::path candidate = probe / "Source/Tests/Fixtures/Graphics/hierarchy.gltf";
				if (std::filesystem::exists(candidate, ec)) {
					source = candidate;
					break;
				}
				std::filesystem::path parent = probe.parent_path();
				if (parent == probe) {
					break;
				}
				probe = parent;
			}
			if (source.empty()) {
				return {};
			}
			std::filesystem::create_directories(_root / "Assets", ec);
			std::filesystem::copy_file(source, _root / "Assets" / "hierarchy.gltf",
									   std::filesystem::copy_options::overwrite_existing, ec);
			if (ec) {
				return {};
			}
			Asset* imported = AssetDatabase::LoadAsset(Path("project://Assets/hierarchy.gltf"));
			if (!imported) {
				return {};
			}
			Guid guid = imported->assetId;
			AssetDatabase::UnloadAsset(imported);
			return guid;
		}

		Vector<str> FilesWithExtension(const char* extension) const {
			Vector<str> found{GetMemoryResource(MemTag::Editor)};
			std::error_code ec;
			for (const auto& entry : std::filesystem::recursive_directory_iterator(_root, ec)) {
				if (entry.is_regular_file(ec) && entry.path().extension() == extension) {
					found.push_back(str(entry.path().generic_string()));
				}
			}
			return found;
		}

		static str BytesOf(const str& virtualPath) {
			return FileSystem::ReadTextFile(FileSystem::Resolve(Path(virtualPath)), MemTag::Editor);
		}

		static size_t LoadedCount() {
			return SceneSystem::GetLoadedScenes().size();
		}

		static bool IsLoaded(Handle<Scene> scene) {
			for (const Handle<Scene>& loaded : SceneSystem::GetLoadedScenes()) {
				if (loaded.GetInstanceId() == scene.GetInstanceId()) {
					return true;
				}
			}
			return false;
		}
	};

} // anonymous namespace

TEST_F(PrefabFlowsFixture, AdditiveLoadLeavesThePrimarySceneAlone) {
	WriteSceneFile("project://second.scene", "Arrival");
	u64 primaryBefore = SceneSystem::GetPrimaryScene().GetInstanceId();
	ASSERT_EQ(primaryBefore, _scene.GetInstanceId());

	Handle<Scene> added = EditorSceneOps::LoadSceneAdditive(Path("project://second.scene"));
	ASSERT_TRUE(added.IsValid());

	EXPECT_EQ(SceneSystem::GetPrimaryScene().GetInstanceId(), primaryBefore);
	EXPECT_TRUE(IsLoaded(_scene));
	EXPECT_TRUE(IsLoaded(added));
	EXPECT_TRUE(added->FindObjectByName("Arrival").IsValid());
}

TEST_F(PrefabFlowsFixture, OpenSceneStillMovesThePrimaryScene) {
	WriteSceneFile("project://second.scene", "Arrival");

	Handle<Scene> opened = EditorSceneOps::OpenScene(Path("project://second.scene"));
	ASSERT_TRUE(opened.IsValid());

	EXPECT_EQ(SceneSystem::GetPrimaryScene().GetInstanceId(), opened.GetInstanceId());
	EXPECT_NE(SceneSystem::GetPrimaryScene().GetInstanceId(), _scene.GetInstanceId());
	EXPECT_TRUE(IsLoaded(_scene));
}

TEST_F(PrefabFlowsFixture, AdditiveLoadKeepsTheSameBookkeeping) {
	WriteSceneFile("project://Scenes/second.scene", "Arrival");

	Handle<Scene> added = EditorSceneOps::LoadSceneAdditive(Path("project://Scenes/second.scene"));
	ASSERT_TRUE(added.IsValid());

	EXPECT_EQ(EditorSceneOps::GetScenePath(added).ToString(), "project://Scenes/second.scene");
	EXPECT_FALSE(EditorSceneOps::IsDirty(added));

	Vector<str> recents{GetMemoryResource(MemTag::Editor)};
	EditorSceneOps::GetRecentScenes(recents);
	ASSERT_FALSE(recents.empty());
	EXPECT_EQ(recents.front(), "project://Scenes/second.scene");
}

TEST_F(PrefabFlowsFixture, FailedAdditiveLoadLeavesTheWorldIntact) {
	size_t loadedBefore = LoadedCount();
	u64 primaryBefore = SceneSystem::GetPrimaryScene().GetInstanceId();

	EXPECT_FALSE(EditorSceneOps::LoadSceneAdditive(Path("project://missing.scene")).IsValid());

	EXPECT_EQ(LoadedCount(), loadedBefore);
	EXPECT_EQ(SceneSystem::GetPrimaryScene().GetInstanceId(), primaryBefore);
	Vector<str> recents{GetMemoryResource(MemTag::Editor)};
	EditorSceneOps::GetRecentScenes(recents);
	EXPECT_TRUE(recents.empty());
}

TEST_F(PrefabFlowsFixture, OpenPrefabMakesBoundScene) {
	Guid guid = WritePrefabFile();
	u64 primaryBefore = SceneSystem::GetPrimaryScene().GetInstanceId();

	Handle<Scene> prefabScene = EditorSceneOps::OpenPrefab(guid);
	ASSERT_TRUE(prefabScene.IsValid());

	EXPECT_EQ(prefabScene->GetRootCount(), 1u);
	EXPECT_EQ(prefabScene->GetRoot(0)->name, "Unit");
	EXPECT_TRUE(SceneSystem::IsPrefabLinked(prefabScene->GetRoot(0)));
	EXPECT_EQ(EditorSceneOps::GetPrefabTarget(prefabScene), guid);
	EXPECT_TRUE(EditorSceneOps::GetScenePath(prefabScene).ToString().empty());
	EXPECT_EQ(SceneSystem::GetPrimaryScene().GetInstanceId(), prefabScene.GetInstanceId());
	EXPECT_EQ(EditorSceneOps::GetPrefabStage().GetInstanceId(), prefabScene.GetInstanceId());
	EXPECT_TRUE(EditorSceneOps::LeavePrefabStage());
	SceneSystem::EndFrame();
	EXPECT_EQ(SceneSystem::GetPrimaryScene().GetInstanceId(), primaryBefore);
	EXPECT_FALSE(EditorSceneOps::GetPrefabStage().IsValid());
	EXPECT_FALSE(EditorSceneOps::LeavePrefabStage());
	prefabScene = EditorSceneOps::OpenPrefab(guid);
	ASSERT_TRUE(prefabScene.IsValid());
	EXPECT_FALSE(std::filesystem::exists(_root / "EditorData"));
	EXPECT_FALSE(EditorSceneOps::GetPrefabTarget(_scene).IsValid());
}

TEST_F(PrefabFlowsFixture, StagingASecondPrefabLeavesTheFirst) {
	Guid first = WritePrefabFile("project://Assets/Prefabs/First.prefab", "First");
	Guid second = WritePrefabFile("project://Assets/Prefabs/Second.prefab", "Second");

	Handle<Scene> firstStage = EditorSceneOps::OpenPrefab(first);
	ASSERT_TRUE(firstStage.IsValid());
	EXPECT_EQ(LoadedCount(), 2u);

	Handle<Scene> secondStage = EditorSceneOps::OpenPrefab(second);
	ASSERT_TRUE(secondStage.IsValid());
	EXPECT_NE(secondStage.GetInstanceId(), firstStage.GetInstanceId());
	SceneSystem::EndFrame();
	EXPECT_FALSE(firstStage.IsValid());
	EXPECT_EQ(LoadedCount(), 2u);
	EXPECT_EQ(EditorSceneOps::GetPrefabStage().GetInstanceId(), secondStage.GetInstanceId());
	EXPECT_EQ(EditorSceneOps::GetPrefabTarget(secondStage), second);

	EXPECT_TRUE(EditorSceneOps::LeavePrefabStage());
	SceneSystem::EndFrame();
	EXPECT_EQ(SceneSystem::GetPrimaryScene().GetInstanceId(), _scene.GetInstanceId());
}

TEST_F(PrefabFlowsFixture, StagingClearsSelectionThatPointsIntoLevel) {
	Guid guid = WritePrefabFile();
	Handle<SceneObject> levelObject = SceneSystem::CreateObject("Levelish", _scene);
	EditorSelection::Select(Handle<Object>(levelObject.Get()));
	ASSERT_EQ(EditorSelection::GetSelection().size(), 1u);

	ASSERT_TRUE(EditorSceneOps::OpenPrefab(guid).IsValid());
	EXPECT_TRUE(EditorSelection::GetSelection().empty());
	EXPECT_TRUE(levelObject.IsValid());

	Handle<SceneObject> staged = EditorSceneOps::GetPrefabStage()->GetRoot(0);
	EditorSelection::Select(Handle<Object>(staged.Get()));
	ASSERT_EQ(EditorSelection::GetSelection().size(), 1u);
	EXPECT_TRUE(EditorSceneOps::LeavePrefabStage());
	SceneSystem::EndFrame();
	EXPECT_TRUE(EditorSelection::GetSelection().empty());
	EXPECT_FALSE(staged.IsValid());
}

TEST_F(PrefabFlowsFixture, PickingInsideStageCannotReachLevel) {
	MeshAsset* mesh = New<MeshAsset>(MemTag::Asset);
	BuildPrimitiveMesh(PrimitiveShape::Cube, *mesh);

	Guid guid = WritePrefabFile();
	Handle<SceneObject> levelBox = SceneSystem::CreateObject("LevelBox", _scene);
	levelBox->SetLocalPosition(Vec3(0.0f, 0.0f, -2.0f));
	levelBox->AddComponent<MeshRenderer>()->SetMesh(Handle<MeshAsset>(mesh));

	Handle<Scene> stage = EditorSceneOps::OpenPrefab(guid);
	ASSERT_TRUE(stage.IsValid());
	Handle<SceneObject> stagedRoot = stage->GetRoot(0);
	stagedRoot->AddComponent<MeshRenderer>()->SetMesh(Handle<MeshAsset>(mesh));

	Ray downZ{Vec3(0.0f, 0.0f, -10.0f), Vec3(0.0f, 0.0f, 1.0f)};
	EXPECT_EQ(EditorViewportOps::PickObject(downZ).GetInstanceId(), stagedRoot.GetInstanceId());

	EXPECT_TRUE(EditorSceneOps::LeavePrefabStage());
	SceneSystem::EndFrame();
	EXPECT_EQ(EditorViewportOps::PickObject(downZ).GetInstanceId(), levelBox.GetInstanceId());

	Delete(mesh);
}

TEST_F(PrefabFlowsFixture, CreatingObjectInsideStageLandsInPrefab) {
	Guid guid = WritePrefabFile();
	Handle<Scene> stage = EditorSceneOps::OpenPrefab(guid);
	ASSERT_TRUE(stage.IsValid());
	u32 levelRootsBefore = _scene->GetRootCount();
	u32 prefabChildrenBefore = stage->GetRoot(0)->GetChildCount();

	Handle<Scene> named{};
	Handle<Scene> target = named.IsValid() ? named : SceneSystem::GetPrimaryScene();
	UndoSystem::Push(MakeUnique<CreateObjectCommand>("New Object", target,
													 EditorSceneOps::GetDefaultParent(target)));
	EXPECT_EQ(target.GetInstanceId(), stage.GetInstanceId());
	EXPECT_EQ(stage->GetRootCount(), 1u);
	EXPECT_EQ(stage->GetRoot(0)->GetChildCount(), prefabChildrenBefore + 1);
	EXPECT_EQ(_scene->GetRootCount(), levelRootsBefore);

	UndoSystem::Undo();
	EXPECT_TRUE(EditorSceneOps::LeavePrefabStage());
	SceneSystem::EndFrame();
	Handle<Scene> after = SceneSystem::GetPrimaryScene();
	UndoSystem::Push(MakeUnique<CreateObjectCommand>("New Object", after,
													 EditorSceneOps::GetDefaultParent(after)));
	EXPECT_EQ(_scene->GetRootCount(), levelRootsBefore + 1);
}

TEST_F(PrefabFlowsFixture, OpenRefusesNonPrefab) {
	EXPECT_FALSE(EditorSceneOps::OpenPrefab(Guid{}).IsValid());
	EXPECT_FALSE(EditorSceneOps::OpenPrefab(Guid{0x1234u, 0x5678u}).IsValid());
	SceneSystem::EndFrame();
	EXPECT_EQ(LoadedCount(), 1u);
}

TEST_F(PrefabFlowsFixture, ModelPrefabStagingRefused) {
	Guid model = StageAndImportModel();
	if (!model.IsValid()) {
		GTEST_SKIP() << "hierarchy.gltf fixture not found";
	}
	Guid authored = WritePrefabFile();
	Handle<Scene> stage = EditorSceneOps::OpenPrefab(authored);
	ASSERT_TRUE(stage.IsValid());
	EXPECT_EQ(LoadedCount(), 2u);

	EXPECT_FALSE(EditorSceneOps::OpenPrefab(model).IsValid());
	SceneSystem::EndFrame();

	EXPECT_TRUE(stage.IsValid());
	EXPECT_EQ(LoadedCount(), 2u);
	EXPECT_EQ(EditorSceneOps::GetPrefabStage().GetInstanceId(), stage.GetInstanceId());
	EXPECT_EQ(EditorSceneOps::GetPrefabTarget(stage), authored);

	EXPECT_TRUE(EditorSceneOps::LeavePrefabStage());
	SceneSystem::EndFrame();
	EXPECT_TRUE(SceneSystem::Instantiate(model, _scene).IsValid());
}

TEST_F(PrefabFlowsFixture, PrefabSceneSavesPrefab) {
	Guid guid = WritePrefabFile();
	Handle<Scene> prefabScene = EditorSceneOps::OpenPrefab(guid);
	ASSERT_TRUE(prefabScene.IsValid());
	str before = BytesOf("project://Assets/Prefabs/Unit.prefab");
	ASSERT_FALSE(before.empty());

	prefabScene->GetRoot(0)->FindChildByName("Body")->SetLocalPosition({1.0f, 2.0f, 3.0f});
	EditorSceneOps::MarkDirty(prefabScene);
	ASSERT_TRUE(EditorSceneOps::SavePrefabScene(prefabScene));

	EXPECT_NE(BytesOf("project://Assets/Prefabs/Unit.prefab"), before);
	EXPECT_TRUE(FilesWithExtension(".scene").empty());
	EXPECT_FALSE(EditorSceneOps::IsDirty(prefabScene));
}

TEST_F(PrefabFlowsFixture, SaveRefusesSceneOrSecondRoot) {
	Guid guid = WritePrefabFile();
	EXPECT_FALSE(EditorSceneOps::SavePrefabScene(_scene));

	Handle<Scene> prefabScene = EditorSceneOps::OpenPrefab(guid);
	ASSERT_TRUE(prefabScene.IsValid());
	SceneSystem::CreateObject("Stray", prefabScene);
	EXPECT_FALSE(EditorSceneOps::SavePrefabScene(prefabScene));

	SceneSystem::Destroy(prefabScene->FindObjectByName("Stray"));
	SceneSystem::EndFrame();
	EXPECT_TRUE(EditorSceneOps::SavePrefabScene(prefabScene));
}

TEST_F(PrefabFlowsFixture, StageEditReachesInstances) {
	Guid guid = WritePrefabFile();
	Handle<SceneObject> instance = SceneSystem::Instantiate(guid, _scene);
	ASSERT_TRUE(instance.IsValid());
	instance->name = "Mine";
	Handle<SceneObject> instanceBody = instance->FindChildByName("Body");
	ASSERT_TRUE(instanceBody.IsValid());
	u64 bodyId = instanceBody.GetInstanceId();

	Handle<Scene> prefabScene = EditorSceneOps::OpenPrefab(guid);
	ASSERT_TRUE(prefabScene.IsValid());
	prefabScene->GetRoot(0)->FindChildByName("Body")->SetLocalPosition({4.0f, 0.0f, 0.0f});
	ASSERT_TRUE(EditorSceneOps::SavePrefabScene(prefabScene));
	SceneSystem::EndFrame();

	EXPECT_FLOAT_EQ(instance->FindChildByName("Body")->GetLocalPosition().x, 4.0f);
	EXPECT_EQ(instance->name, "Mine");
	EXPECT_EQ(instance->FindChildByName("Body").GetInstanceId(), bodyId);
}

TEST_F(PrefabFlowsFixture, OpeningAlreadyOpenPrefabReturnsSameScene) {
	Guid guid = WritePrefabFile();
	Handle<Scene> first = EditorSceneOps::OpenPrefab(guid);
	ASSERT_TRUE(first.IsValid());
	size_t loadedAfterFirst = LoadedCount();

	first->GetRoot(0)->FindChildByName("Body")->SetLocalPosition({0.0f, 5.0f, 0.0f});

	Handle<Scene> second = EditorSceneOps::OpenPrefab(guid);
	EXPECT_EQ(second.GetInstanceId(), first.GetInstanceId());
	EXPECT_EQ(LoadedCount(), loadedAfterFirst);
	EXPECT_FLOAT_EQ(second->GetRoot(0)->FindChildByName("Body")->GetLocalPosition().y, 5.0f);

	Guid other = WritePrefabFile("project://Assets/Prefabs/Other.prefab", "Other");
	Handle<Scene> otherScene = EditorSceneOps::OpenPrefab(other);
	ASSERT_TRUE(otherScene.IsValid());
	EXPECT_NE(otherScene.GetInstanceId(), first.GetInstanceId());
	EXPECT_EQ(LoadedCount(), loadedAfterFirst + 1);

	SceneSystem::UnloadScene(first);
	SceneSystem::EndFrame();
	Handle<Scene> reopened = EditorSceneOps::OpenPrefab(guid);
	ASSERT_TRUE(reopened.IsValid());
	EXPECT_NE(reopened.GetInstanceId(), first.GetInstanceId());
	EXPECT_EQ(EditorSceneOps::GetPrefabTarget(reopened), guid);
}

TEST_F(PrefabFlowsFixture, ClosingStageWithSelection) {
	Guid guid = WritePrefabFile();
	Handle<Scene> prefabScene = EditorSceneOps::OpenPrefab(guid);
	ASSERT_TRUE(prefabScene.IsValid());
	Handle<SceneObject> root = prefabScene->GetRoot(0);

	EditorSelection::Select(Handle<Object>(root.Get()));
	UndoSystem::Push(MakeUnique<PropertySetCommand>(Handle<Object>(root.Get()), "name"_hash,
													Variant(root->name), Variant(str("Renamed")), "Rename"));
	ASSERT_TRUE(UndoSystem::CanUndo());

	EditorSelection::Clear();
	UndoSystem::Clear();
	SceneSystem::UnloadScene(prefabScene);
	SceneSystem::EndFrame();

	EXPECT_EQ(LoadedCount(), 1u);
	EXPECT_FALSE(EditorSceneOps::AnyDirty());
	EXPECT_FALSE(EditorSceneOps::GetPrefabTarget(prefabScene).IsValid());
	EXPECT_EQ(EditorSelection::GetSelectionCount(), 0u);
}

TEST_F(PrefabFlowsFixture, NestedInstanceDroppedOnApply) {
	Guid outer = WritePrefabFile("project://Assets/Prefabs/Outer.prefab", "Outer");
	Guid inner = WritePrefabFile("project://Assets/Prefabs/Inner.prefab", "Inner");

	Handle<Scene> prefabScene = EditorSceneOps::OpenPrefab(outer);
	ASSERT_TRUE(prefabScene.IsValid());
	ASSERT_TRUE(SceneSystem::Instantiate(inner, prefabScene, prefabScene->GetRoot(0)).IsValid());
	ASSERT_EQ(prefabScene->GetRoot(0)->GetChildCount(), 2u);
	ASSERT_TRUE(EditorSceneOps::SavePrefabScene(prefabScene));

	EXPECT_EQ(BytesOf("project://Assets/Prefabs/Outer.prefab").find("Inner"), str::npos);
	Handle<SceneObject> instance = SceneSystem::Instantiate(outer, _scene);
	ASSERT_TRUE(instance.IsValid());
	EXPECT_EQ(instance->GetChildCount(), 1u);
}

TEST_F(PrefabFlowsFixture, CreatePrefabWritesAssetAndLinksSubtree) {
	Handle<SceneObject> unit = SceneSystem::CreateObject("Unit", _scene);
	SceneSystem::CreateObject("Body", _scene, unit);
	ASSERT_FALSE(SceneSystem::IsPrefabLinked(unit));

	Guid guid = EditorSceneOps::CreatePrefabFrom(unit, Path("project://Assets/Prefabs/Unit.prefab"));
	ASSERT_TRUE(guid.IsValid());

	EXPECT_TRUE(std::filesystem::exists(_root / "Assets" / "Prefabs" / "Unit.prefab"));
	EXPECT_EQ(AssetDatabase::LookupGuid(Path("project://Assets/Prefabs/Unit.prefab")), guid);

	AssetDatabase::RebuildDatabase();
	EXPECT_EQ(AssetDatabase::LookupGuid(Path("project://Assets/Prefabs/Unit.prefab")), guid);

	EXPECT_TRUE(SceneSystem::IsPrefabLinked(unit));
	EXPECT_TRUE(EditorSceneOps::IsDirty(_scene));
}

TEST_F(PrefabFlowsFixture, CreatePrefabIsNotUndoable) {
	Handle<SceneObject> unit = SceneSystem::CreateObject("Unit", _scene);
	ASSERT_TRUE(EditorSceneOps::CreatePrefabFrom(unit, Path("project://Assets/Prefabs/Unit.prefab")).IsValid());

	EXPECT_EQ(UndoSystem::GetUndoDepth(), 0u);
	EXPECT_FALSE(UndoSystem::CanUndo());
	EXPECT_FALSE(UndoSystem::Undo());
	SceneSystem::EndFrame();
	EXPECT_TRUE(SceneSystem::IsPrefabLinked(unit));
	EXPECT_TRUE(std::filesystem::exists(_root / "Assets" / "Prefabs" / "Unit.prefab"));
}

TEST_F(PrefabFlowsFixture, TheDisabledPredicateAgreesWithTheRefusal) {
	Handle<SceneObject> unit = SceneSystem::CreateObject("Unit", _scene);
	Handle<SceneObject> body = SceneSystem::CreateObject("Body", _scene, unit);
	ASSERT_TRUE(EditorSceneOps::CreatePrefabFrom(unit, Path("project://Assets/Prefabs/Unit.prefab")).IsValid());

	EXPECT_TRUE(SceneSystem::IsPrefabLinked(unit));
	EXPECT_FALSE(EditorSceneOps::CreatePrefabFrom(unit, Path("project://Assets/Prefabs/Again.prefab")).IsValid());
	EXPECT_FALSE(std::filesystem::exists(_root / "Assets" / "Prefabs" / "Again.prefab"));

	EXPECT_TRUE(SceneSystem::IsPrefabLinked(body));
	EXPECT_FALSE(EditorSceneOps::CreatePrefabFrom(body, Path("project://Assets/Prefabs/Body.prefab")).IsValid());
	EXPECT_FALSE(std::filesystem::exists(_root / "Assets" / "Prefabs" / "Body.prefab"));
}

TEST_F(PrefabFlowsFixture, InstanceRowDragRefused) {
	Handle<SceneObject> unit = SceneSystem::CreateObject("Unit", _scene);
	Handle<SceneObject> body = SceneSystem::CreateObject("Body", _scene, unit);
	ASSERT_TRUE(EditorSceneOps::CreatePrefabFrom(unit, Path("project://Assets/Prefabs/Unit.prefab")).IsValid());

	body->SetParent({});
	ASSERT_FALSE(body->GetParent().IsValid());

	EXPECT_TRUE(SceneSystem::IsPrefabLinked(body));
	EXPECT_FALSE(EditorSceneOps::CreatePrefabFrom(body, Path("project://Assets/Prefabs/Body.prefab")).IsValid());
}

TEST_F(PrefabFlowsFixture, TwoCreationsMakeTwoAssets) {
	Handle<SceneObject> first = SceneSystem::CreateObject("Unit", _scene);
	Handle<SceneObject> second = SceneSystem::CreateObject("Unit", _scene);

	Guid a = EditorSceneOps::CreatePrefabFrom(first, Path("project://Assets/Prefabs/Unit.prefab"));
	Guid b = EditorSceneOps::CreatePrefabFrom(second, Path("project://Assets/Prefabs/Unit 1.prefab"));
	ASSERT_TRUE(a.IsValid());
	ASSERT_TRUE(b.IsValid());
	EXPECT_NE(a, b);
	EXPECT_TRUE(SceneSystem::IsPrefabLinked(first));
	EXPECT_TRUE(SceneSystem::IsPrefabLinked(second));

	Handle<SceneObject> instance = SceneSystem::Instantiate(b, _scene);
	ASSERT_TRUE(instance.IsValid());
	EXPECT_TRUE(SceneSystem::IsPrefabLinked(instance));
}

TEST_F(PrefabFlowsFixture, CreatePrefabRefusesPathOutsideContentRoot) {
	Handle<SceneObject> unit = SceneSystem::CreateObject("Unit", _scene);

	EXPECT_FALSE(EditorSceneOps::CreatePrefabFrom(unit, Path("project://Prefabs/Unit.prefab")).IsValid());
	EXPECT_FALSE(EditorSceneOps::CreatePrefabFrom(unit, Path("project://Unit.prefab")).IsValid());
	EXPECT_FALSE(EditorSceneOps::CreatePrefabFrom(unit, Path("project://AssetsExtra/Unit.prefab")).IsValid());
	EXPECT_FALSE(std::filesystem::exists(_root / "Prefabs"));
	EXPECT_FALSE(std::filesystem::exists(_root / "AssetsExtra"));
	EXPECT_FALSE(SceneSystem::IsPrefabLinked(unit));

	EXPECT_TRUE(EditorSceneOps::CreatePrefabFrom(unit, Path("project://Assets/Unit.prefab")).IsValid());

	Handle<SceneObject> stray = SceneSystem::CreateObject("Stray", _scene);
	std::filesystem::create_directories(_root / "Prefabs");
	Guid strayGuid = SceneSystem::SavePrefab(stray, Path("project://Prefabs/Stray.prefab"));
	ASSERT_TRUE(strayGuid.IsValid());
	ASSERT_EQ(AssetDatabase::LookupGuid(Path("project://Prefabs/Stray.prefab")), strayGuid);
	AssetDatabase::RebuildDatabase();
	EXPECT_FALSE(AssetDatabase::LookupGuid(Path("project://Prefabs/Stray.prefab")).IsValid());
}

TEST_F(PrefabFlowsFixture, PreviewGraphIsSceneless) {
	Guid guid = WritePrefabFile();
	size_t scenesBefore = LoadedCount();

	Handle<SceneObject> preview = EditorSceneOps::GetPrefabPreviewRoot(guid);
	ASSERT_TRUE(preview.IsValid());
	EXPECT_EQ(preview->name, "Unit");
	EXPECT_FALSE(preview->GetScene().IsValid());
	EXPECT_EQ(LoadedCount(), scenesBefore);
	EXPECT_NE(preview->GetLocalId(), 0u);

	EXPECT_EQ(EditorSceneOps::GetPrefabPreviewRoot(guid).GetInstanceId(), preview.GetInstanceId());
}

TEST_F(PrefabFlowsFixture, InspectorRedirectsToStage) {
	Guid guid = WritePrefabFile();

	Handle<Scene> stage = EditorSceneOps::OpenPrefab(guid);
	ASSERT_TRUE(stage.IsValid());
	ASSERT_EQ(stage->GetRootCount(), 1u);
	u64 stageRoot = stage->GetRoot(0).GetInstanceId();

	Handle<SceneObject> preview = EditorSceneOps::GetPrefabPreviewRoot(guid);
	ASSERT_TRUE(preview.IsValid());
	EXPECT_EQ(preview.GetInstanceId(), stageRoot);
	EXPECT_TRUE(preview->GetScene().IsValid());

	Guid other = WritePrefabFile("project://Assets/Prefabs/Other.prefab", "Other");
	Handle<SceneObject> otherPreview = EditorSceneOps::GetPrefabPreviewRoot(other);
	ASSERT_TRUE(otherPreview.IsValid());
	EXPECT_NE(otherPreview.GetInstanceId(), stageRoot);
	EXPECT_FALSE(otherPreview->GetScene().IsValid());
}

TEST_F(PrefabFlowsFixture, PrefabRowsResolveByTheirCorrespondenceKey) {
	Handle<SceneObject> unit = SceneSystem::CreateObject("Unit", _scene);
	unit->AddComponent<MeshRenderer>();
	Guid guid = EditorSceneOps::CreatePrefabFrom(unit, Path("project://Assets/Prefabs/Unit.prefab"));
	ASSERT_TRUE(guid.IsValid());
	SceneSystem::Destroy(unit);
	SceneSystem::EndFrame();

	Handle<SceneObject> preview = EditorSceneOps::GetPrefabPreviewRoot(guid);
	ASSERT_TRUE(preview.IsValid());
	ASSERT_EQ(preview->GetComponentCount(), 1u);
	Handle<Component> renderer = preview->GetComponentAt(0);
	ASSERT_TRUE(renderer.IsValid());
	ASSERT_NE(renderer->GetPrefabKey(), 0u);

	EXPECT_EQ(EditorSceneOps::ResolvePrefabRow(guid, preview->GetPrefabKey()), preview.Get());
	EXPECT_EQ(EditorSceneOps::ResolvePrefabRow(guid, renderer->GetPrefabKey()), renderer.Get());
	EXPECT_EQ(EditorSceneOps::ResolvePrefabRow(guid, 0), nullptr);
	EXPECT_EQ(EditorSceneOps::ResolvePrefabRow(guid, 999999), nullptr);
}

TEST_F(PrefabFlowsFixture, AssetInspectorEditWritesPrefabImmediately) {
	Guid guid = WritePrefabFile();
	Handle<SceneObject> preview = EditorSceneOps::GetPrefabPreviewRoot(guid);
	ASSERT_TRUE(preview.IsValid());
	u64 rootRow = preview->GetPrefabKey();

	UndoSystem::Push(MakeUnique<PropertySetCommand>(
		EditTarget::PrefabRow(guid, rootRow), "name"_hash, Variant(str("Unit")), Variant(str("Renamed")), "Rename"));
	EXPECT_EQ(preview->name, "Renamed");

	EditorSceneOps::FlushPrefabPreview();

	Handle<SceneObject> fresh = SceneSystem::Instantiate(guid, _scene);
	ASSERT_TRUE(fresh.IsValid());
	EXPECT_EQ(fresh->name, "Renamed");
	EXPECT_NE(BytesOf("project://Assets/Prefabs/Unit.prefab").find("Renamed"), str::npos);
}

TEST_F(PrefabFlowsFixture, InspectorEditOneWritePerGesture) {
	Guid guid = WritePrefabFile();
	Handle<SceneObject> preview = EditorSceneOps::GetPrefabPreviewRoot(guid);
	ASSERT_TRUE(preview.IsValid());
	u64 rootRow = preview->GetPrefabKey();
	str before = BytesOf("project://Assets/Prefabs/Unit.prefab");

	UndoSystem::BeginGroup("Edit Transform");
	for (u32 frame = 1; frame <= 3; ++frame) {
		Vec3 was = preview->GetLocalPosition();
		Vec3 now{static_cast<f32>(frame), 0.0f, 0.0f};
		UndoSystem::Push(MakeUnique<PropertySetCommand>(
			EditTarget::PrefabRow(guid, rootRow), "_localPosition"_hash, Variant(was), Variant(now), "Edit Transform"));
		EditorSceneOps::FlushPrefabPreview();
		EXPECT_EQ(BytesOf("project://Assets/Prefabs/Unit.prefab"), before) << frame;
	}
	UndoSystem::EndGroup();

	EditorSceneOps::FlushPrefabPreview();
	EXPECT_NE(BytesOf("project://Assets/Prefabs/Unit.prefab"), before);

	Handle<SceneObject> fresh = SceneSystem::Instantiate(guid, _scene);
	ASSERT_TRUE(fresh.IsValid());
	EXPECT_FLOAT_EQ(fresh->GetLocalPosition().x, 3.0f);
}

TEST_F(PrefabFlowsFixture, UndoSurvivesFreedPreview) {
	Guid guid = WritePrefabFile();
	Handle<SceneObject> preview = EditorSceneOps::GetPrefabPreviewRoot(guid);
	ASSERT_TRUE(preview.IsValid());
	u64 rootRow = preview->GetPrefabKey();
	u64 firstGraph = preview.GetInstanceId();

	UndoSystem::Push(MakeUnique<PropertySetCommand>(
		EditTarget::PrefabRow(guid, rootRow), "name"_hash, Variant(str("Unit")), Variant(str("Renamed")), "Rename"));
	EditorSceneOps::FlushPrefabPreview();

	EditorSceneOps::ReleasePrefabPreview();
	SceneSystem::EndFrame();
	EXPECT_EQ(ObjectRegistry::Resolve(firstGraph), nullptr);

	ASSERT_TRUE(UndoSystem::Undo());
	EditorSceneOps::FlushPrefabPreview();

	Handle<SceneObject> rebuilt = EditorSceneOps::GetPrefabPreviewRoot(guid);
	ASSERT_TRUE(rebuilt.IsValid());
	EXPECT_NE(rebuilt.GetInstanceId(), firstGraph);
	EXPECT_EQ(rebuilt->name, "Unit");
	Handle<SceneObject> fresh = SceneSystem::Instantiate(guid, _scene);
	ASSERT_TRUE(fresh.IsValid());
	EXPECT_EQ(fresh->name, "Unit");
}

TEST_F(PrefabFlowsFixture, StagedAssetEditWaitsForStagesSave) {
	Guid guid = WritePrefabFile();
	Handle<Scene> stage = EditorSceneOps::OpenPrefab(guid);
	ASSERT_TRUE(stage.IsValid());
	Handle<SceneObject> root = stage->GetRoot(0);
	ASSERT_TRUE(root.IsValid());
	EditorSceneOps::MarkClean(stage);
	str before = BytesOf("project://Assets/Prefabs/Unit.prefab");

	UndoSystem::Push(MakeUnique<PropertySetCommand>(
		EditTarget::PrefabRow(guid, root->GetPrefabKey()), "name"_hash,
		Variant(str("Unit")), Variant(str("Renamed")), "Rename"));
	EditorSceneOps::FlushPrefabPreview();

	EXPECT_EQ(root->name, "Renamed");
	EXPECT_EQ(BytesOf("project://Assets/Prefabs/Unit.prefab"), before);
	EXPECT_TRUE(EditorSceneOps::IsDirty(stage));

	ASSERT_TRUE(EditorSceneOps::SavePrefabScene(stage));
	EXPECT_NE(BytesOf("project://Assets/Prefabs/Unit.prefab"), before);
}

TEST_F(PrefabFlowsFixture, AssetInspectorEditReachesComponentRow) {
	Handle<SceneObject> unit = SceneSystem::CreateObject("Unit", _scene);
	unit->AddComponent<MeshRenderer>();
	Guid guid = EditorSceneOps::CreatePrefabFrom(unit, Path("project://Assets/Prefabs/Unit.prefab"));
	ASSERT_TRUE(guid.IsValid());
	SceneSystem::Destroy(unit);
	SceneSystem::EndFrame();

	Handle<SceneObject> preview = EditorSceneOps::GetPrefabPreviewRoot(guid);
	ASSERT_TRUE(preview.IsValid());
	Handle<Component> renderer = preview->GetComponentAt(0);
	ASSERT_TRUE(renderer.IsValid());
	ASSERT_TRUE(renderer->IsEnabledSelf());

	UndoSystem::Push(MakeUnique<SetEnabledCommand>(EditTarget::PrefabRow(guid, renderer->GetPrefabKey()), false));
	EditorSceneOps::FlushPrefabPreview();

	Handle<SceneObject> fresh = SceneSystem::Instantiate(guid, _scene);
	ASSERT_TRUE(fresh.IsValid());
	ASSERT_EQ(fresh->GetComponentCount(), 1u);
	EXPECT_FALSE(fresh->GetComponentAt(0)->IsEnabledSelf());
}

TEST_F(PrefabFlowsFixture, ReleasingPreviewWritesWhatItStillOwes) {
	Guid guid = WritePrefabFile();
	Handle<SceneObject> preview = EditorSceneOps::GetPrefabPreviewRoot(guid);
	ASSERT_TRUE(preview.IsValid());

	UndoSystem::BeginGroup("Edit Transform");
	UndoSystem::Push(MakeUnique<PropertySetCommand>(
		EditTarget::PrefabRow(guid, preview->GetPrefabKey()), "_localScale"_hash,
		Variant(Vec3{1.0f, 1.0f, 1.0f}), Variant(Vec3{2.0f, 2.0f, 2.0f}), "Edit Transform"));
	EditorSceneOps::ReleasePrefabPreview();
	UndoSystem::EndGroup();
	SceneSystem::EndFrame();

	Handle<SceneObject> fresh = SceneSystem::Instantiate(guid, _scene);
	ASSERT_TRUE(fresh.IsValid());
	EXPECT_FLOAT_EQ(fresh->GetLocalScale().x, 2.0f);
}

TEST_F(PrefabFlowsFixture, CreatePrefabRefusesBadInput) {
	Handle<SceneObject> unit = SceneSystem::CreateObject("Unit", _scene);
	EXPECT_FALSE(EditorSceneOps::CreatePrefabFrom(unit, Path("")).IsValid());
	EXPECT_FALSE(SceneSystem::IsPrefabLinked(unit));

	SceneSystem::Destroy(unit);
	SceneSystem::EndFrame();
	EXPECT_FALSE(EditorSceneOps::CreatePrefabFrom(unit, Path("project://Assets/Prefabs/Gone.prefab")).IsValid());
	EXPECT_FALSE(std::filesystem::exists(_root / "Assets" / "Prefabs" / "Gone.prefab"));
}

TEST_F(PrefabFlowsFixture, TwoAdditiveLoadsCoexist) {
	WriteSceneFile("project://a.scene", "FromA");
	WriteSceneFile("project://b.scene", "FromB");

	Handle<Scene> a = EditorSceneOps::LoadSceneAdditive(Path("project://a.scene"));
	Handle<Scene> b = EditorSceneOps::LoadSceneAdditive(Path("project://b.scene"));
	ASSERT_TRUE(a.IsValid());
	ASSERT_TRUE(b.IsValid());
	EXPECT_NE(a.GetInstanceId(), b.GetInstanceId());

	EXPECT_EQ(LoadedCount(), 3u);
	EXPECT_EQ(SceneSystem::GetPrimaryScene().GetInstanceId(), _scene.GetInstanceId());
	EXPECT_TRUE(a->FindObjectByName("FromA").IsValid());
	EXPECT_TRUE(b->FindObjectByName("FromB").IsValid());
	EXPECT_EQ(EditorSceneOps::GetScenePath(a).ToString(), "project://a.scene");
	EXPECT_EQ(EditorSceneOps::GetScenePath(b).ToString(), "project://b.scene");
}

TEST_F(PrefabFlowsFixture, SavingIntoAMissingDirectoryCreatesIt) {
	Handle<Scene> scene = SceneSystem::CreateScene("Fresh");
	SceneSystem::CreateObject("Thing", scene);
	ASSERT_FALSE(std::filesystem::exists(_root / "Assets" / "Scenes"));

	ASSERT_TRUE(EditorSceneOps::SaveScene(scene, Path("project://Assets/Scenes/Fresh.scene")));
	EXPECT_TRUE(std::filesystem::exists(_root / "Assets" / "Scenes" / "Fresh.scene"));
	ASSERT_TRUE(EditorSceneOps::SaveScene(scene, Path("project://Assets/Levels/Act1/Fresh.scene")));
	EXPECT_TRUE(std::filesystem::exists(_root / "Assets" / "Levels" / "Act1" / "Fresh.scene"));

	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
}

TEST_F(PrefabFlowsFixture, StageCreateParentsUnderRoot) {
	Guid guid = WritePrefabFile();
	Handle<Scene> stage = EditorSceneOps::OpenPrefab(guid);
	ASSERT_TRUE(stage.IsValid());
	Handle<SceneObject> prefabRoot = stage->GetRoot(0);

	EXPECT_EQ(EditorSceneOps::GetDefaultParent(stage).GetInstanceId(), prefabRoot.GetInstanceId());
	EXPECT_FALSE(EditorSceneOps::GetDefaultParent(_scene).IsValid());

	u32 childrenBefore = prefabRoot->GetChildCount();
	UndoSystem::Push(MakeUnique<CreateObjectCommand>("New Object", stage,
													 EditorSceneOps::GetDefaultParent(stage)));
	EXPECT_EQ(stage->GetRootCount(), 1u);
	EXPECT_EQ(prefabRoot->GetChildCount(), childrenBefore + 1);

	EXPECT_TRUE(EditorSceneOps::SavePrefabScene(stage));

	if (EditorSceneOps::CreatePrimitiveObject(PrimitiveShape::Cube, stage).IsValid()) {
		EXPECT_EQ(stage->GetRootCount(), 1u);
	}
}

TEST_F(PrefabFlowsFixture, DefaultParentFixesTwoRoots) {
	Guid guid = WritePrefabFile();
	Handle<Scene> stage = EditorSceneOps::OpenPrefab(guid);
	ASSERT_TRUE(stage.IsValid());
	Handle<SceneObject> prefabRoot = stage->GetRoot(0);
	SceneSystem::CreateObject("Stray", stage);
	ASSERT_EQ(stage->GetRootCount(), 2u);
	EXPECT_FALSE(EditorSceneOps::SavePrefabScene(stage));

	EXPECT_EQ(EditorSceneOps::GetDefaultParent(stage).GetInstanceId(), prefabRoot.GetInstanceId());
	UndoSystem::Push(MakeUnique<CreateObjectCommand>("New Object", stage,
													 EditorSceneOps::GetDefaultParent(stage)));
	EXPECT_EQ(stage->GetRootCount(), 2u);
}

TEST_F(PrefabFlowsFixture, DroppingObjectNamesPrefabAfterIt) {
	Handle<SceneObject> unit = SceneSystem::CreateObject("Turret", _scene);
	SceneSystem::CreateObject("Barrel", _scene, unit);

	Guid guid = AssetBrowserOps::CreatePrefabFromObject(unit, Path("project://Assets"));
	ASSERT_TRUE(guid.IsValid());

	EXPECT_TRUE(std::filesystem::exists(_root / "Assets" / "Turret.prefab"));
	EXPECT_EQ(AssetDatabase::LookupGuid(Path("project://Assets/Turret.prefab")), guid);
	EXPECT_TRUE(SceneSystem::IsPrefabLinked(unit));
	EXPECT_TRUE(EditorSceneOps::IsDirty(_scene));
}

TEST_F(PrefabFlowsFixture, SecondDropKeepsFirst) {
	Handle<SceneObject> first = SceneSystem::CreateObject("Crate", _scene);
	Handle<SceneObject> second = SceneSystem::CreateObject("Crate", _scene);

	Guid one = AssetBrowserOps::CreatePrefabFromObject(first, Path("project://Assets"));
	Guid two = AssetBrowserOps::CreatePrefabFromObject(second, Path("project://Assets"));
	ASSERT_TRUE(one.IsValid());
	ASSERT_TRUE(two.IsValid());

	EXPECT_NE(one, two);
	EXPECT_TRUE(std::filesystem::exists(_root / "Assets" / "Crate.prefab"));
	EXPECT_TRUE(std::filesystem::exists(_root / "Assets" / "Crate 1.prefab"));
}

TEST_F(PrefabFlowsFixture, DroppingAnInstanceRowIsRefused) {
	Handle<SceneObject> unit = SceneSystem::CreateObject("Unit", _scene);
	Handle<SceneObject> body = SceneSystem::CreateObject("Body", _scene, unit);
	ASSERT_TRUE(AssetBrowserOps::CreatePrefabFromObject(unit, Path("project://Assets")).IsValid());

	EXPECT_FALSE(AssetBrowserOps::CreatePrefabFromObject(unit, Path("project://Assets")).IsValid());
	EXPECT_FALSE(AssetBrowserOps::CreatePrefabFromObject(body, Path("project://Assets")).IsValid());
	EXPECT_FALSE(std::filesystem::exists(_root / "Assets" / "Unit 1.prefab"));
	EXPECT_FALSE(std::filesystem::exists(_root / "Assets" / "Body.prefab"));
}

TEST_F(PrefabFlowsFixture, DropOutsideContentRootRefused) {
	Handle<SceneObject> unit = SceneSystem::CreateObject("Unit", _scene);

	EXPECT_FALSE(AssetBrowserOps::CreatePrefabFromObject(unit, Path("project://Loose")).IsValid());
	EXPECT_FALSE(std::filesystem::exists(_root / "Loose" / "Unit.prefab"));
	EXPECT_FALSE(SceneSystem::IsPrefabLinked(unit));
}

TEST_F(PrefabFlowsFixture, VariantFromRelinksInstance) {
	Guid base = WritePrefabFile();
	Handle<SceneObject> instance = SceneSystem::Instantiate(base, _scene);
	ASSERT_TRUE(instance.IsValid());
	EditorSceneOps::MarkClean(_scene);
	ASSERT_TRUE(EditorSceneOps::CanCreatePrefabVariantFrom(instance));

	Guid variant = EditorSceneOps::CreatePrefabVariantFrom(instance, Path("project://Assets/Prefabs/Unit Variant.prefab"));
	ASSERT_TRUE(variant.IsValid());
	EXPECT_NE(variant, base);
	EXPECT_TRUE(std::filesystem::exists(_root / "Assets" / "Prefabs" / "Unit Variant.prefab"));
	EXPECT_EQ(AssetDatabase::LookupGuid(Path("project://Assets/Prefabs/Unit Variant.prefab")), variant);

	EXPECT_TRUE(SceneSystem::IsPrefabInstanceRoot(instance));
	EXPECT_TRUE(EditorSceneOps::IsDirty(_scene));
	EXPECT_EQ(UndoSystem::GetUndoDepth(), 0u);
	AssetDatabase::RebuildDatabase();
	EXPECT_EQ(AssetDatabase::LookupGuid(Path("project://Assets/Prefabs/Unit Variant.prefab")), variant);
}

TEST_F(PrefabFlowsFixture, VariantRefusesOutsidePath) {
	Guid base = WritePrefabFile();
	Handle<SceneObject> instance = SceneSystem::Instantiate(base, _scene);
	EditorSceneOps::MarkClean(_scene);
	EXPECT_FALSE(EditorSceneOps::CreatePrefabVariantFrom(instance, Path("project://Loose/Unit.prefab")).IsValid());
	EXPECT_FALSE(std::filesystem::exists(_root / "Loose"));
	EXPECT_FALSE(EditorSceneOps::IsDirty(_scene));
	EXPECT_FALSE(EditorSceneOps::CreatePrefabVariantOf(base, Path("project://Loose/Unit Variant.prefab")).IsValid());
	EXPECT_FALSE(std::filesystem::exists(_root / "Loose"));
}

TEST_F(PrefabFlowsFixture, AStageRootCannotBecomeAVariant) {
	Guid base = WritePrefabFile();
	Handle<SceneObject> plain = SceneSystem::CreateObject("Plain", _scene);
	EXPECT_FALSE(EditorSceneOps::CanCreatePrefabVariantFrom(plain));
	Handle<SceneObject> instance = SceneSystem::Instantiate(base, _scene);
	EXPECT_TRUE(EditorSceneOps::CanCreatePrefabVariantFrom(instance));
	EXPECT_FALSE(EditorSceneOps::CanCreatePrefabVariantFrom(instance->FindChildByName("Body")));

	Handle<Scene> stage = EditorSceneOps::OpenPrefab(base);
	ASSERT_TRUE(stage.IsValid());
	Handle<SceneObject> root = stage->GetRoot(0);
	ASSERT_TRUE(SceneSystem::IsPrefabInstanceRoot(root));
	EXPECT_FALSE(EditorSceneOps::CanCreatePrefabVariantFrom(root));
	EXPECT_FALSE(EditorSceneOps::CreatePrefabVariantFrom(root, Path("project://Assets/Prefabs/Staged.prefab")).IsValid());
	EXPECT_FALSE(std::filesystem::exists(_root / "Assets" / "Prefabs" / "Staged.prefab"));
	EXPECT_EQ(EditorSceneOps::GetPrefabTarget(stage), base);
	EXPECT_TRUE(EditorSceneOps::LeavePrefabStage());
	SceneSystem::EndFrame();
}

TEST_F(PrefabFlowsFixture, ModelPrefabVariantStageable) {
	Guid model = StageAndImportModel();
	if (!model.IsValid()) {
		GTEST_SKIP() << "hierarchy.gltf fixture not found";
	}
	ASSERT_TRUE(AssetBrowserOps::CanCreatePrefabVariant(Path("project://Assets/hierarchy.gltf")));
	str relPath;
	Guid variant = AssetBrowserOps::CreatePrefabVariant(Path("project://Assets/hierarchy.gltf"), &relPath);
	ASSERT_TRUE(variant.IsValid());
	EXPECT_EQ(relPath, "Assets/hierarchy Variant.prefab");
	EXPECT_TRUE(std::filesystem::exists(_root / "Assets" / "hierarchy Variant.prefab"));

	EXPECT_FALSE(EditorSceneOps::OpenPrefab(model).IsValid());
	Handle<Scene> stage = EditorSceneOps::OpenPrefab(variant);
	ASSERT_TRUE(stage.IsValid());
	ASSERT_EQ(stage->GetRootCount(), 1u);
	stage->GetRoot(0)->name = "Renamed";
	EXPECT_TRUE(EditorSceneOps::SavePrefabScene(stage));
	EXPECT_TRUE(EditorSceneOps::LeavePrefabStage());
	SceneSystem::EndFrame();

	Handle<SceneObject> fromVariant = SceneSystem::Instantiate(variant, _scene);
	ASSERT_TRUE(fromVariant.IsValid());
	EXPECT_EQ(fromVariant->name, "Renamed");
	Handle<SceneObject> fromModel = SceneSystem::Instantiate(model, _scene);
	ASSERT_TRUE(fromModel.IsValid());
	EXPECT_NE(fromModel->name, "Renamed");
}

namespace {

	bool HasProperty(const PrefabInstanceDiff& diff, u64 targetId, const char* field) {
		for (const PrefabInstanceDiff::Property& property : diff.properties) {
			if (property.target.GetInstanceId() == targetId && property.fieldPath.size() == 1 &&
				property.fieldPath[0] == StringHash(field)) {
				return true;
			}
		}
		return false;
	}

} // namespace

TEST_F(PrefabFlowsFixture, OverridesReadFromAnyRow) {
	Guid guid = WritePrefabFile();
	Handle<SceneObject> instance = SceneSystem::Instantiate(guid, _scene, {}, Vec3{5.0f, 0.0f, 0.0f});
	ASSERT_TRUE(instance.IsValid());
	Handle<SceneObject> body = instance->FindChildByName("Body");
	ASSERT_TRUE(body.IsValid());
	body->SetLocalPosition(Vec3{0.0f, 1.0f, 0.0f});
	body->SetLocalScale(Vec3{2.0f, 2.0f, 2.0f});
	instance->SetLocalScale(Vec3{3.0f, 3.0f, 3.0f});

	PrefabInstanceDiff diff;
	Handle<SceneObject> root;
	ASSERT_TRUE(EditorSceneOps::PrefabOverridesFor(body, diff, &root));
	EXPECT_EQ(root.GetInstanceId(), instance.GetInstanceId());
	EXPECT_TRUE(HasProperty(diff, body.GetInstanceId(), "_localPosition"));
	EXPECT_TRUE(HasProperty(diff, body.GetInstanceId(), "_localScale"));
	EXPECT_TRUE(HasProperty(diff, instance.GetInstanceId(), "_localScale"));
	EXPECT_FALSE(HasProperty(diff, instance.GetInstanceId(), "_localPosition"));
	EXPECT_FALSE(HasProperty(diff, instance.GetInstanceId(), "_localRotation"));

	PrefabInstanceDiff fromRoot;
	ASSERT_TRUE(EditorSceneOps::PrefabOverridesFor(instance, fromRoot));
	EXPECT_EQ(fromRoot.properties.size(), diff.properties.size());

	Handle<SceneObject> plain = SceneSystem::CreateObject("Plain", _scene);
	EXPECT_FALSE(EditorSceneOps::PrefabOverridesFor(plain, diff, &root));
	EXPECT_TRUE(diff.IsEmpty());
	EXPECT_FALSE(root.IsValid());
}

TEST_F(PrefabFlowsFixture, VariantComparedToBase) {
	Guid base = WritePrefabFile();
	Guid variant = EditorSceneOps::CreatePrefabVariantOf(base, Path("project://Assets/Prefabs/Unit Variant.prefab"));
	ASSERT_TRUE(variant.IsValid());

	Handle<Scene> stage = EditorSceneOps::OpenPrefab(variant);
	ASSERT_TRUE(stage.IsValid());
	Handle<SceneObject> root = stage->GetRoot(0);
	PrefabInstanceDiff diff;
	ASSERT_TRUE(EditorSceneOps::PrefabOverridesFor(root, diff));
	EXPECT_TRUE(diff.IsEmpty());
	root->name = "Boss";
	ASSERT_TRUE(EditorSceneOps::PrefabOverridesFor(root, diff));
	EXPECT_TRUE(HasProperty(diff, root.GetInstanceId(), "name"));
	ASSERT_TRUE(EditorSceneOps::SavePrefabScene(stage));
	SceneSystem::EndFrame();
	ASSERT_TRUE(EditorSceneOps::PrefabOverridesFor(root, diff));
	EXPECT_TRUE(HasProperty(diff, root.GetInstanceId(), "name"));
	EXPECT_TRUE(EditorSceneOps::LeavePrefabStage());
	SceneSystem::EndFrame();

	Handle<SceneObject> preview = EditorSceneOps::GetPrefabPreviewRoot(variant);
	ASSERT_TRUE(preview.IsValid());
	ASSERT_TRUE(EditorSceneOps::PrefabOverridesFor(preview, diff));
	EXPECT_TRUE(HasProperty(diff, preview.GetInstanceId(), "name"));
	EditorSceneOps::ReleasePrefabPreview();

	Handle<SceneObject> basePreview = EditorSceneOps::GetPrefabPreviewRoot(base);
	ASSERT_TRUE(basePreview.IsValid());
	ASSERT_TRUE(EditorSceneOps::PrefabOverridesFor(basePreview, diff));
	EXPECT_TRUE(diff.IsEmpty());
	EditorSceneOps::ReleasePrefabPreview();
	Handle<Scene> baseStage = EditorSceneOps::OpenPrefab(base);
	ASSERT_TRUE(baseStage.IsValid());
	baseStage->GetRoot(0)->name = "Edited";
	ASSERT_TRUE(EditorSceneOps::PrefabOverridesFor(baseStage->GetRoot(0), diff));
	EXPECT_TRUE(diff.IsEmpty());
	EXPECT_TRUE(EditorSceneOps::LeavePrefabStage());
	SceneSystem::EndFrame();

	Handle<SceneObject> instance = SceneSystem::Instantiate(variant, _scene);
	ASSERT_TRUE(instance.IsValid());
	EXPECT_EQ(instance->name, "Boss");
	ASSERT_TRUE(EditorSceneOps::PrefabOverridesFor(instance, diff));
	EXPECT_FALSE(HasProperty(diff, instance.GetInstanceId(), "name"));
}

TEST_F(PrefabFlowsFixture, VariantRowsAddressedByKey) {
	Guid base = WritePrefabFile();
	{
		Handle<Scene> stage = EditorSceneOps::OpenPrefab(base);
		ASSERT_TRUE(stage.IsValid());
		stage->GetRoot(0)->AddComponent<MeshRenderer>();
		ASSERT_TRUE(EditorSceneOps::SavePrefabScene(stage));
		SceneSystem::EndFrame();
		ASSERT_TRUE(EditorSceneOps::LeavePrefabStage());
		SceneSystem::EndFrame();
	}
	Guid variant = EditorSceneOps::CreatePrefabVariantOf(base, Path("project://Assets/Prefabs/Unit Variant.prefab"));
	ASSERT_TRUE(variant.IsValid());
	{
		Handle<Scene> stage = EditorSceneOps::OpenPrefab(variant);
		ASSERT_TRUE(stage.IsValid());
		stage->GetRoot(0)->AddComponent<Camera>();
		ASSERT_TRUE(EditorSceneOps::SavePrefabScene(stage));
		SceneSystem::EndFrame();
		ASSERT_TRUE(EditorSceneOps::LeavePrefabStage());
		SceneSystem::EndFrame();
	}

	Handle<SceneObject> preview = EditorSceneOps::GetPrefabPreviewRoot(variant);
	ASSERT_TRUE(preview.IsValid());
	Handle<Camera> camera = preview->GetComponent<Camera>();
	Handle<MeshRenderer> inherited = preview->GetComponent<MeshRenderer>();
	ASSERT_TRUE(camera.IsValid());
	ASSERT_TRUE(inherited.IsValid());

	EXPECT_EQ(camera->GetLocalId(), preview->GetLocalId());
	EXPECT_NE(camera->GetPrefabKey(), preview->GetPrefabKey());

	EXPECT_EQ(EditorSceneOps::ResolvePrefabRow(variant, preview->GetPrefabKey()), preview.Get());
	EXPECT_EQ(EditorSceneOps::ResolvePrefabRow(variant, inherited->GetPrefabKey()), inherited.Get());
	EXPECT_EQ(EditorSceneOps::ResolvePrefabRow(variant, camera->GetPrefabKey()), camera.Get());

	UndoSystem::Push(MakeUnique<SetEnabledCommand>(EditTarget::PrefabRow(variant, camera->GetPrefabKey()), false));
	EditorSceneOps::FlushPrefabPreview();
	EXPECT_FALSE(camera->IsEnabledSelf());
	EXPECT_TRUE(preview->IsActiveSelf());
}

TEST_F(PrefabFlowsFixture, VariantChainRowIdsResolve) {
	Guid base = WritePrefabFile();
	Guid v1 = EditorSceneOps::CreatePrefabVariantOf(base, Path("project://Assets/Prefabs/V1.prefab"));
	ASSERT_TRUE(v1.IsValid());
	{
		Handle<Scene> stage = EditorSceneOps::OpenPrefab(v1);
		stage->GetRoot(0)->AddComponent<MeshRenderer>();
		ASSERT_TRUE(EditorSceneOps::SavePrefabScene(stage));
		SceneSystem::EndFrame();
		ASSERT_TRUE(EditorSceneOps::LeavePrefabStage());
		SceneSystem::EndFrame();
	}
	Guid v2 = EditorSceneOps::CreatePrefabVariantOf(v1, Path("project://Assets/Prefabs/V2.prefab"));
	ASSERT_TRUE(v2.IsValid());
	{
		Handle<Scene> stage = EditorSceneOps::OpenPrefab(v2);
		stage->GetRoot(0)->AddComponent<Camera>();
		ASSERT_TRUE(EditorSceneOps::SavePrefabScene(stage));
		SceneSystem::EndFrame();
		ASSERT_TRUE(EditorSceneOps::LeavePrefabStage());
		SceneSystem::EndFrame();
	}

	Handle<SceneObject> preview = EditorSceneOps::GetPrefabPreviewRoot(v2);
	ASSERT_TRUE(preview.IsValid());
	ASSERT_EQ(preview->GetComponentCount(), 2u);
	for (u32 i = 0; i < preview->GetComponentCount(); ++i) {
		Handle<Component> component = preview->GetComponentAt(i);
		EXPECT_EQ(EditorSceneOps::ResolvePrefabRow(v2, component->GetPrefabKey()), component.Get()) << "component " << i;
	}
	EXPECT_EQ(EditorSceneOps::ResolvePrefabRow(v2, preview->GetPrefabKey()), preview.Get());
}

TEST_F(PrefabFlowsFixture, VariantAdditionIsOverride) {
	Guid base = WritePrefabFile();
	Guid variant = EditorSceneOps::CreatePrefabVariantOf(base, Path("project://Assets/Prefabs/Unit Variant.prefab"));
	ASSERT_TRUE(variant.IsValid());

	Handle<Scene> stage = EditorSceneOps::OpenPrefab(variant);
	ASSERT_TRUE(stage.IsValid());
	Handle<SceneObject> root = stage->GetRoot(0);
	Handle<Camera> added = root->AddComponent<Camera>();
	ASSERT_TRUE(added.IsValid());

	PrefabInstanceDiff diff;
	ASSERT_TRUE(EditorSceneOps::PrefabOverridesFor(root, diff));
	ASSERT_EQ(diff.addedComponents.size(), 1u);
	EXPECT_EQ(diff.addedComponents[0].GetInstanceId(), added.GetInstanceId());

	ASSERT_TRUE(EditorSceneOps::SavePrefabScene(stage));
	SceneSystem::EndFrame();
	ASSERT_TRUE(EditorSceneOps::PrefabOverridesFor(root, diff));
	ASSERT_EQ(diff.addedComponents.size(), 1u);
	EXPECT_EQ(diff.addedComponents[0].GetInstanceId(), added.GetInstanceId());
	ASSERT_TRUE(EditorSceneOps::LeavePrefabStage());
	SceneSystem::EndFrame();

	Handle<SceneObject> preview = EditorSceneOps::GetPrefabPreviewRoot(variant);
	ASSERT_TRUE(preview.IsValid());
	ASSERT_TRUE(EditorSceneOps::PrefabOverridesFor(preview, diff));
	ASSERT_EQ(diff.addedComponents.size(), 1u);
	EXPECT_EQ(diff.addedComponents[0].GetInstanceId(), preview->GetComponent<Camera>().GetInstanceId());

	Handle<SceneObject> instance = SceneSystem::Instantiate(variant, _scene);
	ASSERT_TRUE(instance.IsValid());
	ASSERT_TRUE(EditorSceneOps::PrefabOverridesFor(instance, diff));
	EXPECT_TRUE(diff.addedComponents.empty());
}

TEST_F(PrefabFlowsFixture, InheritedAdditionNotRewritten) {
	Guid base = WritePrefabFile();
	Guid v1 = EditorSceneOps::CreatePrefabVariantOf(base, Path("project://Assets/Prefabs/V1.prefab"));
	{
		Handle<Scene> stage = EditorSceneOps::OpenPrefab(v1);
		stage->GetRoot(0)->AddComponent<Camera>();
		ASSERT_TRUE(EditorSceneOps::SavePrefabScene(stage));
		SceneSystem::EndFrame();
		ASSERT_TRUE(EditorSceneOps::LeavePrefabStage());
		SceneSystem::EndFrame();
	}
	Guid v2 = EditorSceneOps::CreatePrefabVariantOf(v1, Path("project://Assets/Prefabs/V2.prefab"));
	ASSERT_TRUE(v2.IsValid());
	{
		Handle<Scene> stage = EditorSceneOps::OpenPrefab(v2);
		stage->GetRoot(0)->name = "Renamed";
		ASSERT_TRUE(EditorSceneOps::SavePrefabScene(stage));
		SceneSystem::EndFrame();
		ASSERT_TRUE(EditorSceneOps::LeavePrefabStage());
		SceneSystem::EndFrame();
	}
	str v2Bytes = BytesOf("project://Assets/Prefabs/V2.prefab");
	EXPECT_EQ(v2Bytes.find("addedComponents"), str::npos) << v2Bytes;

	Handle<SceneObject> instance = SceneSystem::Instantiate(v2, _scene);
	ASSERT_TRUE(instance.IsValid());
	EXPECT_EQ(instance->name, "Renamed");
	u32 cameras = 0;
	for (u32 i = 0; i < instance->GetComponentCount(); ++i) {
		cameras += instance->GetComponentAt(i)->GetType() == Camera::GetReflectType() ? 1u : 0u;
	}
	EXPECT_EQ(cameras, 1u);
}

TEST_F(PrefabFlowsFixture, PrefabPreviewRecomposesAfterFileChanged) {
	Guid base = WritePrefabFile();
	Guid variant = EditorSceneOps::CreatePrefabVariantOf(base, Path("project://Assets/Prefabs/Unit Variant.prefab"));
	ASSERT_TRUE(variant.IsValid());
	Handle<SceneObject> stale = EditorSceneOps::GetPrefabPreviewRoot(variant);
	ASSERT_TRUE(stale.IsValid());
	ASSERT_EQ(stale->GetComponentCount(), 0u);

	{
		Handle<Scene> stage = EditorSceneOps::OpenPrefab(variant);
		stage->GetRoot(0)->AddComponent<Camera>();
		ASSERT_TRUE(EditorSceneOps::SavePrefabScene(stage));
		SceneSystem::EndFrame();
		ASSERT_TRUE(EditorSceneOps::LeavePrefabStage());
		SceneSystem::EndFrame();
	}
	Handle<SceneObject> afterOwnEdit = EditorSceneOps::GetPrefabPreviewRoot(variant);
	ASSERT_TRUE(afterOwnEdit.IsValid());
	EXPECT_EQ(afterOwnEdit->GetComponentCount(), 1u);

	{
		Handle<Scene> stage = EditorSceneOps::OpenPrefab(base);
		stage->GetRoot(0)->AddComponent<MeshRenderer>();
		ASSERT_TRUE(EditorSceneOps::SavePrefabScene(stage));
		SceneSystem::EndFrame();
		ASSERT_TRUE(EditorSceneOps::LeavePrefabStage());
		SceneSystem::EndFrame();
	}
	Handle<SceneObject> afterBaseEdit = EditorSceneOps::GetPrefabPreviewRoot(variant);
	ASSERT_TRUE(afterBaseEdit.IsValid());
	EXPECT_EQ(afterBaseEdit->GetComponentCount(), 2u);
	EditorSceneOps::ReleasePrefabPreview();
}

TEST_F(PrefabFlowsFixture, InspectorEditSkipsRecompose) {
	Guid guid = WritePrefabFile();
	Handle<SceneObject> preview = EditorSceneOps::GetPrefabPreviewRoot(guid);
	ASSERT_TRUE(preview.IsValid());
	u64 graph = preview.GetInstanceId();
	u32 before = SceneSystem::GetPrefabEpoch();

	UndoSystem::Push(MakeUnique<PropertySetCommand>(
		EditTarget::PrefabRow(guid, preview->GetPrefabKey()), "name"_hash,
		Variant(str("Unit")), Variant(str("Renamed")), "Rename"));
	EditorSceneOps::FlushPrefabPreview();
	EXPECT_GT(SceneSystem::GetPrefabEpoch(), before);
	EXPECT_EQ(EditorSceneOps::GetPrefabPreviewRoot(guid).GetInstanceId(), graph);
	EXPECT_EQ(preview->name, "Renamed");
	EditorSceneOps::ReleasePrefabPreview();
}
