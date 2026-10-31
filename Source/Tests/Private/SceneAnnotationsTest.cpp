#include "TestsPCH.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Scene.h"

#include "Shalltear/Editor/AssetBrowserOps.h"
#include "Shalltear/Editor/EditorEvents.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/SceneAnnotations.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

	class SceneAnnotationsFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		Handle<Scene> _scene;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_annot_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root / "Assets");

			TypeRegistry::InitializePrimitives();
			FileSystem::Mount("project", Path(_root.generic_string()));
			AssetDatabase::Initialize();
			SceneSystem::Initialize();
			EditorSceneOps::Initialize();
			EditorSelection::Initialize();
			UndoSystem::Initialize();
			SceneAnnotations::Initialize();
			_scene = SceneSystem::CreateScene("Fixture");
		}

		void TearDown() override {
			SceneAnnotations::Terminate();
			UndoSystem::Terminate();
			EditorSelection::Terminate();
			EditorSceneOps::Terminate();
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		Handle<SceneObject> Make(std::string_view name, Handle<SceneObject> parent = {}) {
			return SceneSystem::CreateObject(name, _scene, parent);
		}

		static Handle<SceneObject> Find(Handle<Scene> scene, std::string_view name) {
			struct Search {
				std::string_view want;
				Handle<SceneObject> found;
				static void Visit(SceneObject& node, void* user) {
					Search& search = *static_cast<Search*>(user);
					if (std::string_view(node.name) == search.want) {
						search.found = Handle<SceneObject>(&node);
					}
				}
			} search{name, {}};
			if (Scene* resolved = scene.Get()) {
				resolved->ForEachObject(&Search::Visit, &search);
			}
			return search.found;
		}

		static Vector<Handle<SceneObject>> List(std::initializer_list<Handle<SceneObject>> objects) {
			Vector<Handle<SceneObject>> out{GetMemoryResource(MemTag::Editor)};
			for (Handle<SceneObject> object : objects) {
				out.push_back(object);
			}
			return out;
		}
	};

	TEST_F(SceneAnnotationsFixture, LabelIsOneUndoStep) {
		Handle<SceneObject> a = Make("A");
		Handle<SceneObject> b = Make("B");
		Vector<Handle<SceneObject>> both = List({a, b});

		u32 depthBefore = UndoSystem::GetUndoDepth();
		SceneAnnotations::SetObjectLabels({both.data(), both.size()}, LabelColor::Green);

		EXPECT_EQ(SceneAnnotations::GetObjectLabel(a), LabelColor::Green);
		EXPECT_EQ(SceneAnnotations::GetObjectLabel(b), LabelColor::Green);
		EXPECT_EQ(UndoSystem::GetUndoDepth(), depthBefore + 1);

		ASSERT_TRUE(UndoSystem::Undo());
		EXPECT_EQ(SceneAnnotations::GetObjectLabel(a), LabelColor::None);
		EXPECT_EQ(SceneAnnotations::GetObjectLabel(b), LabelColor::None);

		ASSERT_TRUE(UndoSystem::Redo());
		EXPECT_EQ(SceneAnnotations::GetObjectLabel(a), LabelColor::Green);
	}

	TEST_F(SceneAnnotationsFixture, ClearingLabelRemovesItNotStoringNone) {
		Handle<SceneObject> a = Make("A");
		Vector<Handle<SceneObject>> one = List({a});
		SceneAnnotations::SetObjectLabels({one.data(), one.size()}, LabelColor::Red);
		SceneAnnotations::SetObjectLabels({one.data(), one.size()}, LabelColor::None);
		EXPECT_EQ(SceneAnnotations::GetObjectLabel(a), LabelColor::None);

		ASSERT_TRUE(UndoSystem::Undo());
		EXPECT_EQ(SceneAnnotations::GetObjectLabel(a), LabelColor::Red);
	}

	TEST_F(SceneAnnotationsFixture, AnEditThatChangesNothingRecordsNothing) {
		Handle<SceneObject> a = Make("A");
		Vector<Handle<SceneObject>> one = List({a});
		SceneAnnotations::SetObjectLabels({one.data(), one.size()}, LabelColor::Blue);
		u32 depth = UndoSystem::GetUndoDepth();
		SceneAnnotations::SetObjectLabels({one.data(), one.size()}, LabelColor::Blue);
		EXPECT_EQ(UndoSystem::GetUndoDepth(), depth);
	}

	TEST_F(SceneAnnotationsFixture, FolderNeedsSiblings) {
		Handle<SceneObject> parent = Make("Parent");
		Handle<SceneObject> child = Make("Child", parent);
		Handle<SceneObject> root = Make("Root");
		Vector<Handle<SceneObject>> mixed = List({child, root});

		Handle<Scene> outScene;
		FolderParent outParent;
		EXPECT_FALSE(SceneAnnotations::CanCreateFolderFrom({mixed.data(), mixed.size()}, outScene, outParent));
		EXPECT_EQ(SceneAnnotations::CreateFolder(_scene, "Mixed", FolderParent::Roots(),
												 {mixed.data(), mixed.size()}),
				  0u);
	}

	TEST_F(SceneAnnotationsFixture, CanCreateFolderReturnsGroup) {
		Handle<SceneObject> parent = Make("Parent");
		Handle<SceneObject> a = Make("A", parent);
		Handle<SceneObject> b = Make("B", parent);
		Vector<Handle<SceneObject>> siblings = List({a, b});

		Handle<Scene> outScene;
		FolderParent outParent;
		ASSERT_TRUE(SceneAnnotations::CanCreateFolderFrom({siblings.data(), siblings.size()}, outScene, outParent));
		EXPECT_EQ(outScene, _scene);
		EXPECT_EQ(outParent.kind, FolderParentKind::Object);
		EXPECT_EQ(outParent.id, parent.GetInstanceId());
	}

	TEST_F(SceneAnnotationsFixture, ReparentUnfiles) {
		Handle<SceneObject> a = Make("A");
		Handle<SceneObject> elsewhere = Make("Elsewhere");
		Vector<Handle<SceneObject>> one = List({a});

		u64 folder = SceneAnnotations::CreateFolder(_scene, "Bin", FolderParent::Roots(), {one.data(), one.size()});
		ASSERT_NE(folder, 0u);
		EXPECT_EQ(SceneAnnotations::GetFolderOf(a), folder);

		a.Get()->SetParent(elsewhere);
		EXPECT_EQ(SceneAnnotations::GetFolderOf(a), 0u);

		a.Get()->SetParent({});
		EXPECT_EQ(SceneAnnotations::GetFolderOf(a), folder);
	}

	TEST_F(SceneAnnotationsFixture, AFolderNestsInsideASceneObject) {
		Handle<SceneObject> host = Make("Host");
		Handle<SceneObject> child = Make("Child", host);
		Vector<Handle<SceneObject>> one = List({child});

		u64 folder = SceneAnnotations::CreateFolder(_scene, "Inner", FolderParent::Under(host.GetInstanceId()),
													{one.data(), one.size()});
		ASSERT_NE(folder, 0u);
		EXPECT_EQ(SceneAnnotations::GetFolderAnchor(_scene, folder), host);
		EXPECT_EQ(SceneAnnotations::GetFolderOf(child), folder);

		Vector<FolderInfo> under{GetMemoryResource(MemTag::Editor)};
		SceneAnnotations::GetChildFolders(_scene, FolderParent::Under(host.GetInstanceId()), under);
		ASSERT_EQ(under.size(), 1u);
		EXPECT_EQ(under[0].id, folder);
		Vector<FolderInfo> roots{GetMemoryResource(MemTag::Editor)};
		SceneAnnotations::GetChildFolders(_scene, FolderParent::Roots(), roots);
		EXPECT_TRUE(roots.empty());
	}

	TEST_F(SceneAnnotationsFixture, ANestedFolderInheritsItsParentsAnchor) {
		Handle<SceneObject> host = Make("Host");
		u64 outer = SceneAnnotations::CreateFolder(_scene, "Outer", FolderParent::Under(host.GetInstanceId()), {});
		u64 inner = SceneAnnotations::CreateFolder(_scene, "Inner", FolderParent::In(outer), {});
		ASSERT_NE(inner, 0u);
		EXPECT_EQ(SceneAnnotations::GetFolderAnchor(_scene, inner), host);
	}

	TEST_F(SceneAnnotationsFixture, DeletingFolderUnfilesMembersAndKeepsSubfolders) {
		Handle<SceneObject> a = Make("A");
		Vector<Handle<SceneObject>> one = List({a});
		u64 outer = SceneAnnotations::CreateFolder(_scene, "Outer", FolderParent::Roots(), {one.data(), one.size()});
		u64 inner = SceneAnnotations::CreateFolder(_scene, "Inner", FolderParent::In(outer), {});

		ASSERT_TRUE(SceneAnnotations::DeleteFolder(_scene, outer));

		EXPECT_TRUE(a.IsValid());
		EXPECT_EQ(SceneAnnotations::GetFolderOf(a), 0u);
		FolderInfo info;
		ASSERT_TRUE(SceneAnnotations::GetFolder(_scene, inner, info));
		EXPECT_EQ(info.parent.kind, FolderParentKind::SceneRoot);
	}

	TEST_F(SceneAnnotationsFixture, RenamingAFolderIsUndoable) {
		u64 folder = SceneAnnotations::CreateFolder(_scene, "Before", FolderParent::Roots(), {});
		ASSERT_TRUE(SceneAnnotations::RenameFolder(_scene, folder, "After"));
		FolderInfo info;
		ASSERT_TRUE(SceneAnnotations::GetFolder(_scene, folder, info));
		EXPECT_EQ(info.name, "After");

		ASSERT_TRUE(UndoSystem::Undo());
		ASSERT_TRUE(SceneAnnotations::GetFolder(_scene, folder, info));
		EXPECT_EQ(info.name, "Before");
	}

	TEST_F(SceneAnnotationsFixture, MoveFolderInsertsIntoMiddleOfSiblings) {
		u64 first = SceneAnnotations::CreateFolder(_scene, "First", FolderParent::Roots(), {});
		u64 second = SceneAnnotations::CreateFolder(_scene, "Second", FolderParent::Roots(), {});
		u64 third = SceneAnnotations::CreateFolder(_scene, "Third", FolderParent::Roots(), {});

		auto names = [this]() {
			Vector<FolderInfo> folders{GetMemoryResource(MemTag::Editor)};
			SceneAnnotations::GetChildFolders(_scene, FolderParent::Roots(), folders);
			str out;
			for (const FolderInfo& folder : folders) {
				out += folder.name + " ";
			}
			return out;
		};
		EXPECT_EQ(names(), "First Second Third ");

		ASSERT_TRUE(SceneAnnotations::MoveFolder(_scene, third, FolderParent::Roots(), 0, 1));
		EXPECT_EQ(names(), "First Third Second ");

		ASSERT_TRUE(SceneAnnotations::MoveFolder(_scene, first, FolderParent::Roots(), 0, 2));
		EXPECT_EQ(names(), "Third Second First ");
		(void)second;
	}

	TEST_F(SceneAnnotationsFixture, FolderMoveRecorded) {
		Make("First");
		Make("Second");
		u64 folder = SceneAnnotations::CreateFolder(_scene, "Bin", FolderParent::Roots(), {}, 0);
		ASSERT_NE(folder, 0u);
		u64 version = SceneAnnotations::GetVersion(_scene);
		u32 depth = UndoSystem::GetUndoDepth();

		ASSERT_TRUE(SceneAnnotations::MoveFolder(_scene, folder, FolderParent::Roots(), 1, 0));

		FolderInfo info;
		ASSERT_TRUE(SceneAnnotations::GetFolder(_scene, folder, info));
		EXPECT_EQ(info.slot, 1u);
		EXPECT_NE(SceneAnnotations::GetVersion(_scene), version);
		ASSERT_EQ(UndoSystem::GetUndoDepth(), depth + 1);

		ASSERT_TRUE(UndoSystem::Undo());
		ASSERT_TRUE(SceneAnnotations::GetFolder(_scene, folder, info));
		EXPECT_EQ(info.slot, 0u);
	}

	TEST_F(SceneAnnotationsFixture, FolderCannotBeMovedIntoSubtree) {
		u64 outer = SceneAnnotations::CreateFolder(_scene, "Outer", FolderParent::Roots(), {});
		u64 inner = SceneAnnotations::CreateFolder(_scene, "Inner", FolderParent::In(outer), {});
		EXPECT_FALSE(SceneAnnotations::MoveFolder(_scene, outer, FolderParent::In(inner), 0, 0));
		FolderInfo info;
		ASSERT_TRUE(SceneAnnotations::GetFolder(_scene, outer, info));
		EXPECT_EQ(info.parent.kind, FolderParentKind::SceneRoot);
	}

	TEST_F(SceneAnnotationsFixture, FolderAnchorMoveIsOneStep) {
		Handle<SceneObject> host = Make("Host");
		Handle<SceneObject> a = Make("A");
		Handle<SceneObject> b = Make("B");
		Vector<Handle<SceneObject>> members = List({a, b});
		u64 folder = SceneAnnotations::CreateFolder(_scene, "Bin", FolderParent::Roots(),
													{members.data(), members.size()});
		ASSERT_NE(folder, 0u);

		u32 depth = UndoSystem::GetUndoDepth();
		ASSERT_TRUE(SceneAnnotations::MoveFolder(_scene, folder, FolderParent::Under(host.GetInstanceId()), 0, 0));

		EXPECT_EQ(a.Get()->GetParent(), host);
		EXPECT_EQ(b.Get()->GetParent(), host);
		EXPECT_EQ(SceneAnnotations::GetFolderOf(a), folder);
		EXPECT_EQ(UndoSystem::GetUndoDepth(), depth + 1);

		ASSERT_TRUE(UndoSystem::Undo());
		EXPECT_FALSE(a.Get()->GetParent().IsValid());
		FolderInfo info;
		ASSERT_TRUE(SceneAnnotations::GetFolder(_scene, folder, info));
		EXPECT_EQ(info.parent.kind, FolderParentKind::SceneRoot);
		EXPECT_EQ(SceneAnnotations::GetFolderOf(a), folder);
	}

	TEST_F(SceneAnnotationsFixture, APureReorderMutatesNoScene) {
		Handle<SceneObject> a = Make("A");
		Vector<Handle<SceneObject>> one = List({a});
		u64 first = SceneAnnotations::CreateFolder(_scene, "First", FolderParent::Roots(), {one.data(), one.size()});
		SceneAnnotations::CreateFolder(_scene, "Second", FolderParent::Roots(), {});

		ASSERT_TRUE(SceneAnnotations::MoveFolder(_scene, first, FolderParent::Roots(), 0, 1));
		EXPECT_FALSE(a.Get()->GetParent().IsValid());
		EXPECT_FALSE(EditorSceneOps::IsDirty(_scene));
	}

	TEST_F(SceneAnnotationsFixture, FilingReparentsOntoAnchor) {
		Handle<SceneObject> host = Make("Host");
		Handle<SceneObject> stray = Make("Stray");
		u64 folder = SceneAnnotations::CreateFolder(_scene, "Inner", FolderParent::Under(host.GetInstanceId()), {});
		ASSERT_NE(folder, 0u);

		Vector<Handle<SceneObject>> one = List({stray});
		u32 depth = UndoSystem::GetUndoDepth();
		SceneAnnotations::FileObjects(_scene, {one.data(), one.size()}, folder);

		EXPECT_EQ(stray.Get()->GetParent(), host);
		EXPECT_EQ(SceneAnnotations::GetFolderOf(stray), folder);
		EXPECT_EQ(UndoSystem::GetUndoDepth(), depth + 1);
	}

	TEST_F(SceneAnnotationsFixture, FilingIntoFolderZeroUnfiles) {
		Handle<SceneObject> a = Make("A");
		Vector<Handle<SceneObject>> one = List({a});
		u64 folder = SceneAnnotations::CreateFolder(_scene, "Bin", FolderParent::Roots(), {one.data(), one.size()});
		ASSERT_NE(folder, 0u);
		SceneAnnotations::FileObjects(_scene, {one.data(), one.size()}, 0);
		EXPECT_EQ(SceneAnnotations::GetFolderOf(a), 0u);
	}

	TEST_F(SceneAnnotationsFixture, ObjectIsNotFiledUnderDescendant) {
		Handle<SceneObject> parent = Make("Parent");
		Handle<SceneObject> child = Make("Child", parent);
		u64 folder = SceneAnnotations::CreateFolder(_scene, "Inner", FolderParent::Under(child.GetInstanceId()), {});
		ASSERT_NE(folder, 0u);

		Vector<Handle<SceneObject>> one = List({parent});
		SceneAnnotations::FileObjects(_scene, {one.data(), one.size()}, folder);
		EXPECT_EQ(SceneAnnotations::GetFolderOf(parent), 0u);
		EXPECT_FALSE(parent.Get()->GetParent().IsValid());
	}

	TEST_F(SceneAnnotationsFixture, AnnotationsRoundTripThroughTheSidecar) {
		Handle<SceneObject> host = Make("Host");
		Handle<SceneObject> child = Make("Child", host);
		Handle<SceneObject> loose = Make("Loose");
		Vector<Handle<SceneObject>> one = List({child});

		u64 folder = SceneAnnotations::CreateFolder(_scene, "Inner", FolderParent::Under(host.GetInstanceId()),
													{one.data(), one.size()});
		ASSERT_NE(folder, 0u);
		SceneAnnotations::SetFolderLabel(_scene, folder, LabelColor::Purple);
		Vector<Handle<SceneObject>> tinted = List({loose});
		SceneAnnotations::SetObjectLabels({tinted.data(), tinted.size()}, LabelColor::Teal);

		Path scenePath("project://Assets/Round.scene");
		ASSERT_TRUE(EditorSceneOps::SaveScene(_scene, scenePath));
		Path sidecar = SceneAnnotations::GetSidecarPath(_scene);
		EXPECT_EQ(sidecar.ToString(),
				  "project://EditorData/Scenes/" + _scene.Get()->GetSourceGuid().ToString() + ".json");
		ASSERT_TRUE(FileSystem::Exists(sidecar));
		EXPECT_NE(FileSystem::ReadTextFile(sidecar, MemTag::Editor).find("\"scene\": \"" + scenePath.ToString() + "\""),
				  str::npos);

		SceneSystem::UnloadScene(_scene);
		SceneSystem::EndFrame();

		Handle<Scene> reopened = EditorSceneOps::OpenScene(scenePath);
		ASSERT_TRUE(reopened.IsValid());

		Handle<SceneObject> reloadedLoose = Find(reopened, "Loose");
		Handle<SceneObject> reloadedChild = Find(reopened, "Child");
		ASSERT_TRUE(reloadedLoose.IsValid());
		ASSERT_TRUE(reloadedChild.IsValid());

		EXPECT_EQ(SceneAnnotations::GetObjectLabel(reloadedLoose), LabelColor::Teal);
		u64 reloadedFolder = SceneAnnotations::GetFolderOf(reloadedChild);
		ASSERT_NE(reloadedFolder, 0u);
		FolderInfo info;
		ASSERT_TRUE(SceneAnnotations::GetFolder(reopened, reloadedFolder, info));
		EXPECT_EQ(info.name, "Inner");
		EXPECT_EQ(info.label, LabelColor::Purple);
		EXPECT_EQ(info.parent.kind, FolderParentKind::Object);
		EXPECT_EQ(SceneAnnotations::GetFolderAnchor(reopened, reloadedFolder).Get()->name, "Host");
	}

	TEST_F(SceneAnnotationsFixture, UnsavedSceneCannotWrite) {
		EXPECT_TRUE(SceneAnnotations::GetSidecarPath(_scene).ToString().empty());
		EXPECT_FALSE(SceneAnnotations::SaveFor(_scene));
	}

	TEST_F(SceneAnnotationsFixture, RenamingASceneKeepsItsFolders) {
		Handle<SceneObject> host = Make("Host");
		Handle<SceneObject> child = Make("Child", host);
		Handle<SceneObject> loose = Make("Loose");
		Vector<Handle<SceneObject>> members = List({child});
		u64 folder = SceneAnnotations::CreateFolder(_scene, "Inner", FolderParent::Under(host.GetInstanceId()),
													{members.data(), members.size()});
		ASSERT_NE(folder, 0u);
		Vector<Handle<SceneObject>> tinted = List({loose});
		SceneAnnotations::SetObjectLabels({tinted.data(), tinted.size()}, LabelColor::Orange);

		Path before("project://Assets/Before.scene");
		ASSERT_TRUE(EditorSceneOps::SaveScene(_scene, before));
		Path sidecar = SceneAnnotations::GetSidecarPath(_scene);
		ASSERT_TRUE(FileSystem::Exists(sidecar));

		ASSERT_TRUE(AssetBrowserOps::RenameEntry(before, "After.scene"));
		EXPECT_TRUE(FileSystem::Exists(sidecar));

		SceneSystem::UnloadScene(_scene);
		SceneSystem::EndFrame();

		Path after("project://Assets/After.scene");
		Handle<Scene> reopened = EditorSceneOps::OpenScene(after);
		ASSERT_TRUE(reopened.IsValid());
		EXPECT_EQ(SceneAnnotations::GetSidecarPath(reopened).ToString(), sidecar.ToString());

		Handle<SceneObject> reloadedChild = Find(reopened, "Child");
		Handle<SceneObject> reloadedLoose = Find(reopened, "Loose");
		ASSERT_TRUE(reloadedChild.IsValid());
		ASSERT_TRUE(reloadedLoose.IsValid());
		EXPECT_EQ(SceneAnnotations::GetObjectLabel(reloadedLoose), LabelColor::Orange);
		u64 reloadedFolder = SceneAnnotations::GetFolderOf(reloadedChild);
		ASSERT_NE(reloadedFolder, 0u);
		FolderInfo info;
		ASSERT_TRUE(SceneAnnotations::GetFolder(reopened, reloadedFolder, info));
		EXPECT_EQ(info.name, "Inner");
	}

	TEST_F(SceneAnnotationsFixture, OldSidecarAdoptedAndRemoved) {
		Handle<SceneObject> host = Make("Host");
		Vector<Handle<SceneObject>> one = List({host});
		u64 folder = SceneAnnotations::CreateFolder(_scene, "Legacy", FolderParent::Roots(), {one.data(), one.size()});
		ASSERT_NE(folder, 0u);

		Path scenePath("project://Assets/Legacy.scene");
		ASSERT_TRUE(EditorSceneOps::SaveScene(_scene, scenePath));
		Path guidNamed = SceneAnnotations::GetSidecarPath(_scene);
		ASSERT_TRUE(FileSystem::Exists(guidNamed));

		Path legacy("project://EditorData/Scenes/Assets/Legacy.scene.json");
		str contents = FileSystem::ReadTextFile(guidNamed, MemTag::Editor);
		ASSERT_FALSE(contents.empty());
		FileSystem::CreateDirectory(Path(legacy.GetDirectory()));
		ASSERT_TRUE(FileSystem::WriteTextFile(legacy, contents));
		std::error_code ec;
		std::filesystem::remove(std::filesystem::path(FileSystem::Resolve(guidNamed).ToString()), ec);
		ASSERT_FALSE(FileSystem::Exists(guidNamed));

		SceneSystem::UnloadScene(_scene);
		SceneSystem::EndFrame();

		Handle<Scene> reopened = EditorSceneOps::OpenScene(scenePath);
		ASSERT_TRUE(reopened.IsValid());
		Handle<SceneObject> reloaded = Find(reopened, "Host");
		ASSERT_TRUE(reloaded.IsValid());
		EXPECT_NE(SceneAnnotations::GetFolderOf(reloaded), 0u);

		EXPECT_TRUE(FileSystem::Exists(SceneAnnotations::GetSidecarPath(reopened)));
		EXPECT_FALSE(FileSystem::Exists(legacy));
	}

	TEST_F(SceneAnnotationsFixture, AbsolutePathKeepsFolders) {
		Handle<SceneObject> host = Make("Host");
		Vector<Handle<SceneObject>> one = List({host});
		u64 folder = SceneAnnotations::CreateFolder(_scene, "Inner", FolderParent::Roots(), {one.data(), one.size()});
		ASSERT_NE(folder, 0u);

		Path virtualPath("project://Assets/Absolute.scene");
		ASSERT_TRUE(EditorSceneOps::SaveScene(_scene, virtualPath));
		Guid assetId = AssetDatabase::LookupGuid(virtualPath);
		ASSERT_TRUE(assetId.IsValid());

		SceneSystem::UnloadScene(_scene);
		SceneSystem::EndFrame();

		Path absolute((_root / "Assets" / "Absolute.scene").generic_string());
		Handle<Scene> reopened = EditorSceneOps::OpenScene(absolute);
		ASSERT_TRUE(reopened.IsValid());

		EXPECT_EQ(reopened.Get()->GetSourceGuid(), assetId);
		EXPECT_EQ(EditorSceneOps::GetScenePath(reopened).ToString(), virtualPath.ToString());

		Handle<SceneObject> reloaded = Find(reopened, "Host");
		ASSERT_TRUE(reloaded.IsValid());
		EXPECT_NE(SceneAnnotations::GetFolderOf(reloaded), 0u);

		ASSERT_TRUE(EditorSceneOps::SaveScene(reopened, absolute));
		EXPECT_EQ(reopened.Get()->GetSourceGuid(), assetId);
		EXPECT_EQ(AssetDatabase::LookupGuid(virtualPath), assetId);
	}

	TEST_F(SceneAnnotationsFixture, RemapCarriesAnnotations) {
		Handle<SceneObject> host = Make("Host");
		Handle<SceneObject> child = Make("Child", host);
		Vector<Handle<SceneObject>> one = List({child});
		u64 folder = SceneAnnotations::CreateFolder(_scene, "Inner", FolderParent::Under(host.GetInstanceId()),
													{one.data(), one.size()});
		Vector<Handle<SceneObject>> tinted = List({child});
		SceneAnnotations::SetObjectLabels({tinted.data(), tinted.size()}, LabelColor::Pink);

		Handle<Scene> after = SceneSystem::CreateScene("Restored");
		Handle<SceneObject> newHost = SceneSystem::CreateObject("Host", after);
		Handle<SceneObject> newChild = SceneSystem::CreateObject("Child", after, newHost);

		EditorIdRemap table[] = {
			{_scene.GetInstanceId(), after.GetInstanceId()},
			{host.GetInstanceId(), newHost.GetInstanceId()},
			{child.GetInstanceId(), newChild.GetInstanceId()},
		};
		SceneAnnotations::RemapIds({table, 3});

		EXPECT_EQ(SceneAnnotations::GetObjectLabel(newChild), LabelColor::Pink);
		EXPECT_EQ(SceneAnnotations::GetFolderOf(newChild), folder);
		EXPECT_EQ(SceneAnnotations::GetFolderAnchor(after, folder), newHost);
		EXPECT_EQ(SceneAnnotations::GetVersion(_scene), 0u);
	}

	TEST_F(SceneAnnotationsFixture, ObjectAbsentFromRemapIsDropped) {
		Handle<SceneObject> a = Make("A");
		Vector<Handle<SceneObject>> one = List({a});
		SceneAnnotations::SetObjectLabels({one.data(), one.size()}, LabelColor::Red);

		Handle<Scene> after = SceneSystem::CreateScene("Restored");
		EditorIdRemap table[] = {{_scene.GetInstanceId(), after.GetInstanceId()}};
		SceneAnnotations::RemapIds({table, 1});

		Handle<SceneObject> impostor = SceneSystem::CreateObject("A", after);
		EXPECT_EQ(SceneAnnotations::GetObjectLabel(impostor), LabelColor::None);
	}

} // namespace
