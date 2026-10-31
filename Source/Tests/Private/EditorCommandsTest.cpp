#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Math.h"
#include "Shalltear/Core/Scene.h"

#include "Shalltear/Editor/EditorCommands.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

	i32 _cmdDisables = 0;

	class CmdTestComponent : public Component {
	public:
		SLT_REFLECT_COMPONENT(CmdTestComponent, Component)
		SLT_REFLECT_FIELD(payload)
		SLT_END_REFLECT()

		CmdTestComponent() = default;

		i32 payload = 0;
	protected:
		void OnDisable() override { ++_cmdDisables; }
	};

	class EditorCommandsTest : public ::testing::Test {
	protected:
		void SetUp() override {
			_cmdDisables = 0;
			TypeRegistry::InitializePrimitives();
			CmdTestComponent::GetReflectType();
			SceneSystem::Initialize();
			EditorSceneOps::Initialize();
			UndoSystem::Initialize();
			_scene = SceneSystem::CreateScene("CommandsTest");
		}
		void TearDown() override {
			UndoSystem::Terminate();
			EditorSceneOps::Terminate();
			SceneSystem::Terminate();
		}

		Handle<SceneObject> Make(const str& name, Handle<SceneObject> parent = {}) {
			return SceneSystem::CreateObject(name, _scene, parent);
		}

		Handle<Scene> _scene;
	};

} // anonymous namespace

TEST_F(EditorCommandsTest, CreateObjectUndoRedo) {
	auto command = MakeUnique<CreateObjectCommand>("Fresh", _scene, Handle<SceneObject>{});
	CreateObjectCommand* raw = command.get();
	UndoSystem::Push(std::move(command));

	Handle<SceneObject> created = raw->GetCreated();
	ASSERT_TRUE(created.IsValid());
	EXPECT_EQ(created.Get()->name, "Fresh");

	UndoSystem::Undo();
	SceneSystem::EndFrame();
	EXPECT_FALSE(created.IsValid());

	UndoSystem::Redo();
	Handle<SceneObject> redone = raw->GetCreated();
	ASSERT_TRUE(redone.IsValid());
	EXPECT_EQ(redone.Get()->name, "Fresh");
	EXPECT_NE(redone.GetInstanceId(), created.GetInstanceId());
}

TEST_F(EditorCommandsTest, DestroyRevivesTheSubtree) {
	Make("Before");
	Handle<SceneObject> parent = Make("Parent");
	Make("After");
	parent.Get()->SetLocalPosition(Vec3(1.0f, 2.0f, 3.0f));

	Handle<SceneObject> child = Make("Child", parent);
	Handle<CmdTestComponent> component = child.Get()->AddComponent<CmdTestComponent>();
	component.Get()->payload = 42;
	component.Get()->SetEnabled(false);

	ASSERT_EQ(parent.Get()->GetSiblingIndex(), 1u);
	UndoSystem::Push(MakeUnique<DestroyObjectCommand>(parent));
	SceneSystem::EndFrame();
	EXPECT_FALSE(parent.IsValid());
	EXPECT_FALSE(child.IsValid());

	UndoSystem::Undo();
	Handle<SceneObject> revived = _scene.Get()->FindObjectByName("Parent");
	ASSERT_TRUE(revived.IsValid());
	EXPECT_EQ(revived.Get()->GetSiblingIndex(), 1u);
	Vec3 position = revived.Get()->GetLocalPosition();
	EXPECT_FLOAT_EQ(position.x, 1.0f);
	EXPECT_FLOAT_EQ(position.z, 3.0f);

	ASSERT_EQ(revived.Get()->GetChildCount(), 1u);
	Handle<SceneObject> revivedChild = revived.Get()->GetChild(0);
	EXPECT_EQ(revivedChild.Get()->name, "Child");
	Handle<CmdTestComponent> revivedComponent = revivedChild.Get()->GetComponent<CmdTestComponent>();
	ASSERT_TRUE(revivedComponent.IsValid());
	EXPECT_EQ(revivedComponent.Get()->payload, 42);
	EXPECT_FALSE(revivedComponent.Get()->IsEnabledSelf());
	EXPECT_FALSE(revivedComponent.Get()->IsEffectivelyEnabled());
}

TEST_F(EditorCommandsTest, ReparentRoundTrip) {
	Handle<SceneObject> a = Make("A");
	Handle<SceneObject> b = Make("B");

	UndoSystem::Push(MakeUnique<ReparentCommand>(b, a, 0));
	EXPECT_EQ(b.Get()->GetParent(), a);
	EXPECT_EQ(a.Get()->GetChildCount(), 1u);

	UndoSystem::Undo();
	EXPECT_FALSE(b.Get()->GetParent().IsValid());
	EXPECT_EQ(b.Get()->GetSiblingIndex(), 1u);
}

TEST_F(EditorCommandsTest, SiblingIndexRoundTrip) {
	Handle<SceneObject> first = Make("First");
	Make("Second");
	Make("Third");

	UndoSystem::Push(MakeUnique<SetSiblingIndexCommand>(first, 2));
	EXPECT_EQ(first.Get()->GetSiblingIndex(), 2u);
	UndoSystem::Undo();
	EXPECT_EQ(first.Get()->GetSiblingIndex(), 0u);
}

namespace {

	Handle<SceneObject> PushDuplicate(Handle<SceneObject> source) {
		auto command = MakeUnique<DuplicateObjectCommand>(source);
		DuplicateObjectCommand* raw = command.get();
		UndoSystem::Push(std::move(command));
		return raw->GetCreated();
	}

} // anonymous namespace

TEST_F(EditorCommandsTest, DuplicateTakesNextFreeNumberAmongSiblings) {
	Handle<SceneObject> cube = Make("Cube");
	Handle<SceneObject> first = PushDuplicate(cube);
	ASSERT_TRUE(first.IsValid());
	EXPECT_EQ(first.Get()->name, "Cube (1)");
	EXPECT_EQ(PushDuplicate(cube).Get()->name, "Cube (2)");
	EXPECT_EQ(PushDuplicate(first).Get()->name, "Cube (3)");

	Make("Leg (1)");
	Handle<SceneObject> body = Make("Body");
	Handle<SceneObject> leg = Make("Leg", body);
	EXPECT_EQ(PushDuplicate(leg).Get()->name, "Leg (1)");
}

TEST_F(EditorCommandsTest, ADuplicateLandsDirectlyBelowItsOriginal) {
	Handle<SceneObject> a = Make("A");
	Make("B");
	Make("C");
	auto command = MakeUnique<DuplicateObjectCommand>(a);
	DuplicateObjectCommand* raw = command.get();
	UndoSystem::Push(std::move(command));
	ASSERT_TRUE(raw->GetCreated().IsValid());
	EXPECT_EQ(raw->GetCreated().Get()->GetSiblingIndex(), 1u);
	EXPECT_EQ(_scene.Get()->GetRoot(2).Get()->name, "B");

	UndoSystem::Undo();
	SceneSystem::EndFrame();
	ASSERT_EQ(_scene.Get()->GetRootCount(), 3u);
	UndoSystem::Redo();
	ASSERT_TRUE(raw->GetCreated().IsValid());
	EXPECT_EQ(raw->GetCreated().Get()->GetSiblingIndex(), 1u);
	EXPECT_EQ(raw->GetCreated().Get()->name, "A (1)");

	Handle<SceneObject> parent = Make("Parent");
	Handle<SceneObject> x = Make("X", parent);
	Make("Y", parent);
	Handle<SceneObject> copyOfX = PushDuplicate(x);
	ASSERT_TRUE(copyOfX.IsValid());
	EXPECT_EQ(copyOfX.Get()->GetSiblingIndex(), 1u);
	EXPECT_EQ(parent.Get()->GetChild(2).Get()->name, "Y");
}

TEST_F(EditorCommandsTest, SetActiveCascadesAndReverts) {
	Handle<SceneObject> parent = Make("Parent");
	Handle<SceneObject> child = Make("Child", parent);
	ASSERT_TRUE(child.Get()->IsActiveInHierarchy());

	UndoSystem::Push(MakeUnique<SetActiveCommand>(parent, false));
	EXPECT_FALSE(parent.Get()->IsActiveSelf());
	EXPECT_FALSE(child.Get()->IsActiveInHierarchy());

	UndoSystem::Undo();
	EXPECT_TRUE(parent.Get()->IsActiveSelf());
	EXPECT_TRUE(child.Get()->IsActiveInHierarchy());
}

TEST_F(EditorCommandsTest, AddComponentRoundTrip) {
	Handle<SceneObject> object = Make("Host");

	auto command = MakeUnique<AddComponentCommand>(object, StringHash("CmdTestComponent"));
	AddComponentCommand* raw = command.get();
	UndoSystem::Push(std::move(command));
	EXPECT_TRUE(raw->GetCreated().IsValid());
	EXPECT_TRUE(object.Get()->GetComponent<CmdTestComponent>().IsValid());

	UndoSystem::Undo();
	SceneSystem::EndFrame();
	EXPECT_FALSE(object.Get()->GetComponent<CmdTestComponent>().IsValid());

	UndoSystem::Redo();
	EXPECT_TRUE(object.Get()->GetComponent<CmdTestComponent>().IsValid());
}

TEST_F(EditorCommandsTest, RemoveComponentRevivesState) {
	Handle<SceneObject> object = Make("Host");
	Handle<CmdTestComponent> component = object.Get()->AddComponent<CmdTestComponent>();
	component.Get()->payload = 9;
	component.Get()->SetEnabled(false);
	i32 disablesBefore = _cmdDisables;

	UndoSystem::Push(MakeUnique<RemoveComponentCommand>(Handle<Component>(component.Get())));
	SceneSystem::EndFrame();
	EXPECT_FALSE(object.Get()->GetComponent<CmdTestComponent>().IsValid());

	UndoSystem::Undo();
	Handle<CmdTestComponent> revived = object.Get()->GetComponent<CmdTestComponent>();
	ASSERT_TRUE(revived.IsValid());
	EXPECT_EQ(revived.Get()->payload, 9);
	EXPECT_FALSE(revived.Get()->IsEnabledSelf());
	EXPECT_GT(_cmdDisables, disablesBefore);
}

TEST_F(EditorCommandsTest, CommandsMarkTheirSceneDirty) {
	Handle<SceneObject> object = Make("Dirtier");
	EditorSceneOps::MarkClean(_scene);
	ASSERT_FALSE(EditorSceneOps::IsDirty(_scene));

	UndoSystem::Push(MakeUnique<SetActiveCommand>(object, false));
	EXPECT_TRUE(EditorSceneOps::IsDirty(_scene));

	EditorSceneOps::MarkClean(_scene);
	UndoSystem::Undo();
	EXPECT_TRUE(EditorSceneOps::IsDirty(_scene));
	EXPECT_TRUE(EditorSceneOps::AnyDirty());
}

TEST_F(EditorCommandsTest, SceneOpsSaveOpenRoundTrip) {
	Handle<SceneObject> object = Make("Persisted");
	object.Get()->SetLocalPosition(Vec3(5.0f, 0.0f, 0.0f));
	EditorSceneOps::MarkDirty(_scene);

	FileSystem::CreateDirectory(Path("Library/EditorTest"));
	Path path("Library/EditorTest/roundtrip.scene");
	ASSERT_TRUE(EditorSceneOps::SaveScene(_scene, path));
	EXPECT_FALSE(EditorSceneOps::IsDirty(_scene));
	EXPECT_EQ(EditorSceneOps::GetScenePath(_scene).ToString(), path.ToString());

	Vector<str> recents{GetMemoryResource(MemTag::Editor)};
	EditorSceneOps::GetRecentScenes(recents);
	ASSERT_FALSE(recents.empty());
	EXPECT_EQ(recents[0], path.ToString());

	Handle<Scene> reopened = EditorSceneOps::OpenScene(path);
	ASSERT_TRUE(reopened.IsValid());
	Handle<SceneObject> loaded = reopened.Get()->FindObjectByName("Persisted");
	ASSERT_TRUE(loaded.IsValid());
	EXPECT_FLOAT_EQ(loaded.Get()->GetLocalPosition().x, 5.0f);
	EXPECT_FALSE(EditorSceneOps::IsDirty(reopened));
	EXPECT_EQ(SceneSystem::GetPrimaryScene(), reopened);
}

TEST_F(EditorCommandsTest, ReorderComponentRoundTrip) {
	Handle<SceneObject> object = Make("Host");
	Handle<CmdTestComponent> a = object.Get()->AddComponent<CmdTestComponent>();
	Handle<CmdTestComponent> b = object.Get()->AddComponent<CmdTestComponent>();
	Handle<CmdTestComponent> c = object.Get()->AddComponent<CmdTestComponent>();
	a.Get()->payload = 1;
	b.Get()->payload = 2;
	c.Get()->payload = 3;

	auto order = [&]() {
		str out;
		for (u32 i = 0; i < object.Get()->GetComponentCount(); ++i) {
			out += std::to_string(static_cast<CmdTestComponent*>(object.Get()->GetComponentAt(i).Get())->payload);
		}
		return out;
	};
	ASSERT_EQ(order(), "123");

	UndoSystem::Push(MakeUnique<SetComponentIndexCommand>(Handle<Component>(c.Get()), 0));
	EXPECT_EQ(order(), "312");

	UndoSystem::Undo();
	EXPECT_EQ(order(), "123");

	UndoSystem::Redo();
	EXPECT_EQ(order(), "312");
}

namespace {

	class PrefabCommandTest : public EditorCommandsTest {
	protected:
		std::filesystem::path _projectRoot;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_projectRoot = std::filesystem::temp_directory_path() /
						   ("shalltear_prefabcmd_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_projectRoot);
			FileSystem::Mount("project", Path(_projectRoot.generic_string()));
			AssetDatabase::Initialize();
			EditorCommandsTest::SetUp();
		}

		void TearDown() override {
			EditorCommandsTest::TearDown();
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_projectRoot, ec);
		}

		Guid MakePrefab(const str& path = "project://unit.prefab") {
			Handle<SceneObject> unit = Make("Unit");
			Handle<SceneObject> body = Make("Body", unit);
			body.Get()->AddComponent<CmdTestComponent>().Get()->payload = 7;
			Guid guid = SceneSystem::SavePrefab(unit, Path(path));
			EXPECT_TRUE(guid.IsValid());
			SceneSystem::Destroy(unit);
			SceneSystem::EndFrame();
			return guid;
		}

		static bool IsLinkedInstance(Handle<SceneObject> root) {
			return SceneSystem::RevertInstanceOverrides(root);
		}
	};

} // anonymous namespace

TEST_F(PrefabCommandTest, InstantiateAddsALinkedInstance) {
	Guid prefab = MakePrefab();
	ASSERT_TRUE(prefab.IsValid());

	UndoSystem::Push(MakeUnique<InstantiatePrefabCommand>(prefab, _scene, Handle<SceneObject>{}));

	Handle<SceneObject> instance = _scene.Get()->FindObjectByName("Unit");
	ASSERT_TRUE(instance.IsValid());
	EXPECT_EQ(instance.Get()->GetChildCount(), 1u);
	EXPECT_TRUE(IsLinkedInstance(instance));
	EXPECT_FALSE(IsLinkedInstance(Make("Plain")));
	EXPECT_TRUE(EditorSceneOps::IsDirty(_scene));
}

TEST_F(PrefabCommandTest, InstantiateUnderParentLandsAsChild) {
	Guid prefab = MakePrefab();
	Handle<SceneObject> host = Make("Host");

	UndoSystem::Push(MakeUnique<InstantiatePrefabCommand>(prefab, _scene, host));

	ASSERT_EQ(host.Get()->GetChildCount(), 1u);
	EXPECT_EQ(host.Get()->GetChild(0).Get()->name, "Unit");
	EXPECT_TRUE(IsLinkedInstance(host.Get()->GetChild(0)));
}

TEST_F(PrefabCommandTest, RedoBringsBackRealInstanceNotCopy) {
	Guid prefab = MakePrefab();
	UndoSystem::Push(MakeUnique<InstantiatePrefabCommand>(prefab, _scene, Handle<SceneObject>{}));
	Handle<SceneObject> first = _scene.Get()->FindObjectByName("Unit");
	ASSERT_TRUE(first.IsValid());

	UndoSystem::Undo();
	SceneSystem::EndFrame();
	EXPECT_FALSE(first.IsValid());
	EXPECT_FALSE(_scene.Get()->FindObjectByName("Unit").IsValid());

	UndoSystem::Redo();
	Handle<SceneObject> again = _scene.Get()->FindObjectByName("Unit");
	ASSERT_TRUE(again.IsValid());
	EXPECT_NE(again.GetInstanceId(), first.GetInstanceId());
	EXPECT_TRUE(IsLinkedInstance(again));
	EXPECT_EQ(again.Get()->GetChildCount(), 1u);
	Handle<CmdTestComponent> payload = again.Get()->GetChild(0).Get()->GetComponent<CmdTestComponent>();
	ASSERT_TRUE(payload.IsValid());
	EXPECT_EQ(payload.Get()->payload, 7);
}

TEST_F(PrefabCommandTest, InstantiateSurvivesPrefabThatIsNotThere) {
	auto command = MakeUnique<InstantiatePrefabCommand>(Guid{0x99u, 0x99u}, _scene, Handle<SceneObject>{});
	EXPECT_EQ(command->GetLabel(), "Instantiate Prefab");
	UndoSystem::Push(std::move(command));

	EXPECT_EQ(_scene.Get()->GetRootCount(), 0u);
	UndoSystem::Undo();
	UndoSystem::Redo();
	EXPECT_EQ(_scene.Get()->GetRootCount(), 0u);
}

TEST_F(PrefabCommandTest, TheLabelNamesTheAsset) {
	Guid prefab = MakePrefab("project://turret.prefab");
	InstantiatePrefabCommand command(prefab, _scene, Handle<SceneObject>{});
	EXPECT_EQ(command.GetLabel(), "Instantiate turret.prefab");
}
