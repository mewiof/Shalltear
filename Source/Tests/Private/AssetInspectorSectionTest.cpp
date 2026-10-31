#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/UI/UISystem.h"

#include "Shalltear/Editor/EditorAssetOps.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/PropertyDrawers.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;

namespace {

	u32 CountByExactText(UI::Context& context, UI::ElementHandle from, std::string_view text) {
		u32 count = context.GetTextString(from) == text ? 1u : 0u;
		const UI::Element* element = context.GetElement(from);
		for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(child);
			count += CountByExactText(context, handle, text);
			child = context.GetElement(handle)->nextSibling;
		}
		return count;
	}

	class AssetSectionFixture : public ::testing::Test {
	protected:
		std::filesystem::path _projectRoot;
		Handle<Scene> _scene;
		int _owner = 0;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_projectRoot = std::filesystem::temp_directory_path() /
						   ("shalltear_assetsection_" + std::to_string(stamp) + "_" +
							std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_projectRoot / "Assets");
			FileSystem::Mount("project", Path(_projectRoot.generic_string()));
			AssetDatabase::Initialize();

			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			SceneSystem::Initialize();
			EditorSceneOps::Initialize();
			EditorAssetOps::Initialize();
			EditorSelection::Initialize();
			UndoSystem::Initialize();
			PanelManager::Initialize();
			PropertyDrawers::Initialize();
			_scene = SceneSystem::CreateScene("AssetSectionFixture");
		}

		void TearDown() override {
			PropertyDrawers::UnregisterOwnedBy(&_owner);
			PanelManager::Terminate();
			UndoSystem::Terminate();
			EditorSelection::Terminate();
			EditorSceneOps::Terminate();
			EditorAssetOps::Terminate();
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_projectRoot, ec);
		}

		Guid MakePrefab(const char* name) {
			Handle<SceneObject> unit = SceneSystem::CreateObject(name, _scene);
			return EditorSceneOps::CreatePrefabFrom(unit, Path(str("project://Assets/") + name + ".prefab"));
		}

		void AddSection(std::string_view label, i32 order, bool applies, u32* drawCount) {
			AssetInspectorSectionDesc desc;
			desc.label = label;
			desc.order = order;
			desc.applies = [applies](const Guid&, Asset&) { return applies; };
			desc.draw = [drawCount](UI::Context&, UI::ElementHandle, const Guid&, Asset&) {
				if (drawCount) {
					++(*drawCount);
				}
			};
			ASSERT_TRUE(PropertyDrawers::RegisterAssetInspectorSection(
				StringHash("PrefabAsset"), std::move(desc), &_owner));
		}
	};

} // namespace

TEST_F(AssetSectionFixture, SectionsAddUpInOrder) {
	u32 drewA = 0, drewB = 0;
	AddSection("Rig", 100, true, &drewA);
	AddSection("Rig", 10, true, &drewB);

	Vector<AssetInspectorSection> sections{GetMemoryResource(MemTag::Editor)};
	PropertyDrawers::GetAssetInspectorSections(StringHash("PrefabAsset"), sections);
	ASSERT_EQ(sections.size(), 2u);
	EXPECT_EQ(sections[0].order, 10);
	EXPECT_EQ(sections[1].order, 100);
}

TEST_F(AssetSectionFixture, EverySectionOwnerSwept) {
	AddSection("Rig", 0, true, nullptr);
	AddSection("Animation", 0, true, nullptr);
	AddSection("Rig", 1, true, nullptr);

	Vector<AssetInspectorSection> sections{GetMemoryResource(MemTag::Editor)};
	PropertyDrawers::GetAssetInspectorSections(StringHash("PrefabAsset"), sections);
	ASSERT_EQ(sections.size(), 3u);

	PropertyDrawers::UnregisterOwnedBy(&_owner);
	PropertyDrawers::GetAssetInspectorSections(StringHash("PrefabAsset"), sections);
	EXPECT_TRUE(sections.empty());
}

TEST_F(AssetSectionFixture, SharedLabelOneTabBothBodies) {
	Guid guid = MakePrefab("Unit");
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);

	u32 drewFirst = 0, drewSecond = 0;
	AddSection("Rig", 10, true, &drewFirst);
	AddSection("Rig", 100, true, &drewSecond);

	Handle<EditorPanel> panel = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("InspectorPanel")));
	ASSERT_TRUE(panel.IsValid());
	UITest::MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {500, 900};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	context.Update();

	EXPECT_EQ(CountByExactText(context, root, "Rig"), 1u);
	EXPECT_EQ(drewFirst, 1u);
	EXPECT_EQ(drewSecond, 1u);
}

TEST_F(AssetSectionFixture, InapplicableSectionDrawsNoTab) {
	Guid guid = MakePrefab("Plain");
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);

	u32 drew = 0;
	AddSection("Rig", 0, false, &drew);

	Handle<EditorPanel> panel = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("InspectorPanel")));
	ASSERT_TRUE(panel.IsValid());
	UITest::MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {500, 900};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	context.Update();

	EXPECT_EQ(CountByExactText(context, root, "Rig"), 0u);
	EXPECT_EQ(drew, 0u);
}

TEST_F(AssetSectionFixture, TabClickShowsOnlyItsBody) {
	Guid guid = MakePrefab("Unit");
	ASSERT_TRUE(guid.IsValid());
	EditorSelection::SelectAsset(guid);

	u32 drewRig = 0, drewAnimation = 0;
	AddSection("Rig", 0, true, &drewRig);
	AddSection("Animation", 1, true, &drewAnimation);

	Handle<EditorPanel> panel = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("InspectorPanel")));
	ASSERT_TRUE(panel.IsValid());
	UITest::MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {500, 900};
	UI::Context context(desc, &fonts);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	panel.Get()->BuildContent(context, root);
	context.Update();

	ASSERT_EQ(drewRig, 1u);
	ASSERT_EQ(drewAnimation, 0u);

	UI::ElementHandle tab = UITest::FindByTextContaining(context, root, "Animation");
	ASSERT_TRUE(tab.IsValid());
	UITest::ClickElement(context, tab);

	panel.Get()->BuildContent(context, root);
	context.Update();

	EXPECT_EQ(drewAnimation, 1u);
	EXPECT_EQ(drewRig, 1u);
}
