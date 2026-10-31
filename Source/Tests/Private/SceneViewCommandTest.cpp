#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Scene.h"

#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/SceneAnnotations.h"
#include "Shalltear/Editor/UndoSystem.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	class SceneViewCommandTest : public ::testing::Test {
	protected:
		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			SceneSystem::Initialize();
			EditorSceneOps::Initialize();
			EditorSelection::Initialize();
			UndoSystem::Initialize();
			SceneAnnotations::Initialize();
			PanelManager::Initialize();
			_scene = SceneSystem::CreateScene("Fixture");
		}
		void TearDown() override {
			PanelManager::Terminate();
			SceneAnnotations::Terminate();
			UndoSystem::Terminate();
			EditorSelection::Terminate();
			EditorSceneOps::Terminate();
			SceneSystem::Terminate();
		}
		Handle<Scene> _scene;
	};

	struct SceneViewRig {
		MockFontSystem fonts;
		UI::Context context;
		UI::ElementHandle root;
		Handle<EditorPanel> panel;

		SceneViewRig()
			: context(Desc(), &fonts) {
			EditorTheme::ApplyToContext(context);
			panel = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("SceneViewPanel")));
			UI::Style fill;
			fill.width = UI::Length::Percent(1.0f);
			fill.height = UI::Length::Percent(1.0f);
			root = context.CreateElement();
			context.AppendChild(context.GetRoot(), root);
			context.SetStyle(root, fill);
			panel.Get()->BuildContent(context, root);
			context.Update();
		}

		static UI::ContextDesc Desc() {
			UI::ContextDesc desc;
			desc.sizePx = {600, 400};
			return desc;
		}
	};

	void SelectObjects(std::initializer_list<Handle<SceneObject>> objects) {
		EditorSelection::Clear();
		for (Handle<SceneObject> object : objects) {
			EditorSelection::AddToSelection(Handle<Object>(static_cast<Object*>(object.Get())));
		}
	}

} // anonymous namespace

TEST_F(SceneViewCommandTest, DeleteIsOneUndoStep) {
	Handle<SceneObject> a = SceneSystem::CreateObject("Alpha", _scene);
	Handle<SceneObject> b = SceneSystem::CreateObject("Beta", _scene);
	Handle<SceneObject> c = SceneSystem::CreateObject("Gamma", _scene);
	u64 aId = a.GetInstanceId();
	u64 bId = b.GetInstanceId();
	u64 cId = c.GetInstanceId();

	SceneViewRig rig;
	SelectObjects({a, b});
	ASSERT_EQ(EditorSelection::GetSelectionCount(), 2u);

	EXPECT_TRUE(rig.panel.Get()->OnCommand(PanelCommand::Delete));
	SceneSystem::EndFrame();

	EXPECT_EQ(ObjectRegistry::Resolve(aId), nullptr);
	EXPECT_EQ(ObjectRegistry::Resolve(bId), nullptr);
	EXPECT_NE(ObjectRegistry::Resolve(cId), nullptr);
	EXPECT_EQ(EditorSelection::GetSelectionCount(), 0u);

	UndoSystem::Undo();
	SceneSystem::EndFrame();
	EXPECT_EQ(_scene.Get()->GetRootCount(), 3u);
}

TEST_F(SceneViewCommandTest, DuplicateIsOneUndoStep) {
	Handle<SceneObject> a = SceneSystem::CreateObject("Alpha", _scene);
	Handle<SceneObject> b = SceneSystem::CreateObject("Beta", _scene);
	SceneSystem::CreateObject("Gamma", _scene);

	SceneViewRig rig;
	SelectObjects({a, b});

	EXPECT_TRUE(rig.panel.Get()->OnCommand(PanelCommand::Duplicate));
	SceneSystem::EndFrame();
	EXPECT_EQ(_scene.Get()->GetRootCount(), 5u);
	EXPECT_EQ(EditorSelection::GetSelectionCount(), 2u);
	EXPECT_FALSE(EditorSelection::IsSelected(Handle<Object>(static_cast<Object*>(a.Get()))));
	EXPECT_FALSE(EditorSelection::IsSelected(Handle<Object>(static_cast<Object*>(b.Get()))));
	Handle<SceneObject> copyOfB = _scene.Get()->GetRoot(3);
	ASSERT_TRUE(copyOfB.IsValid());
	EXPECT_EQ(copyOfB.Get()->name, "Beta (1)");
	EXPECT_EQ(_scene.Get()->GetRoot(1).Get()->name, "Alpha (1)");
	EXPECT_EQ(EditorSelection::GetActive().GetInstanceId(), copyOfB.GetInstanceId());

	UndoSystem::Undo();
	SceneSystem::EndFrame();
	EXPECT_EQ(_scene.Get()->GetRootCount(), 3u);
}

TEST_F(SceneViewCommandTest, EmptySelectionChordRefused) {
	SceneSystem::CreateObject("Alpha", _scene);
	SceneViewRig rig;
	EditorSelection::Clear();

	EXPECT_FALSE(rig.panel.Get()->OnCommand(PanelCommand::Delete));
	EXPECT_FALSE(rig.panel.Get()->OnCommand(PanelCommand::Duplicate));
	EXPECT_FALSE(UndoSystem::CanUndo());
	SceneSystem::EndFrame();
	EXPECT_EQ(_scene.Get()->GetRootCount(), 1u);
}

TEST_F(SceneViewCommandTest, ViewSkipsSelectAllAndRename) {
	Handle<SceneObject> a = SceneSystem::CreateObject("Alpha", _scene);
	SceneSystem::CreateObject("Beta", _scene);
	SceneViewRig rig;
	SelectObjects({a});

	EXPECT_FALSE(rig.panel.Get()->OnCommand(PanelCommand::SelectAll));
	EXPECT_EQ(EditorSelection::GetSelectionCount(), 1u);
	EXPECT_FALSE(rig.panel.Get()->OnCommand(PanelCommand::Rename));
	EXPECT_FALSE(UndoSystem::CanUndo());
}
