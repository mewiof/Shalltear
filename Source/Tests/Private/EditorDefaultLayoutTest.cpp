#include "TestsPCH.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Core/UI/Docking.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/Window.h"

#include "Shalltear/Editor/EditorConsole.h"
#include "Shalltear/Editor/EditorFonts.h"
#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorLayoutOps.h"
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

	constexpr size_t ROOT = 0;
	constexpr size_t LEFT_OF_INSPECTOR = 1;
	constexpr size_t UPPER_ROW = 2;
	constexpr size_t HIERARCHY_LEAF = 3;
	constexpr size_t VIEWPORT_LEAF = 4;
	constexpr size_t BOTTOM_LEAF = 5;
	constexpr size_t INSPECTOR_LEAF = 6;

	class EditorDefaultLayoutTest : public ::testing::Test {
	protected:
		Handle<Window> _window;

		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			JobSystem::Initialize();
			AssetDatabase::Initialize();
			SceneSystem::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			UISystem::Initialize();
			EditorConsole::Initialize();
			EditorSceneOps::Initialize();
			EditorSelection::Initialize();
			UndoSystem::Initialize();
			PropertyDrawers::Initialize();
			PanelManager::Initialize();
			RegisterBuiltInPanels();

			WindowDesc desc;
			desc.title = "EditorDefaultLayout";
			desc.clientSize = {1600, 900};
			desc.flags = WindowFlags::Hidden;
			_window = WindowSystem::Create(desc);
			ASSERT_TRUE(_window.IsValid());
			ASSERT_NE(UISystem::CreateDockManager(_window), nullptr);
			PanelManager::SetDefaultWindow(_window);
		}

		void TearDown() override {
			PanelManager::Terminate();
			PropertyDrawers::Terminate();
			UndoSystem::Terminate();
			EditorSelection::Terminate();
			EditorSceneOps::Terminate();
			EditorConsole::Terminate();
			EditorIcons::Shutdown();
			EditorFonts::Shutdown();
			UISystem::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			SceneSystem::Terminate();
			AssetDatabase::Terminate();
			JobSystem::Terminate();
		}

		UI::DockManager& Dock() { return *UISystem::GetDockManager(_window); }

		void Frame() {
			PanelManager::TickAll(1.0f / 60.0f);
			UISystem::Update();
		}

		std::vector<UI::DockNodeDesc> Snapshot() {
			Vector<UI::DockNodeDesc> nodes{GetMemoryResource(MemTag::Editor)};
			Dock().SaveLayout(nodes);
			return {nodes.begin(), nodes.end()};
		}

		static StringHash FirstInstanceOf(const char* typeName) {
			const TypeInfo* type = TypeRegistry::FindType(StringHash(typeName));
			return type ? PanelManager::MakeDockId(type, 1) : StringHash{};
		}

		void ExpectTheDefaultArrangement(const std::vector<UI::DockNodeDesc>& tree) {
			ASSERT_EQ(tree.size(), 7u);

			EXPECT_EQ(tree[ROOT].split, UI::DockSplit::Horizontal);
			EXPECT_EQ(tree[ROOT].childA, static_cast<i32>(LEFT_OF_INSPECTOR));
			EXPECT_EQ(tree[ROOT].childB, static_cast<i32>(INSPECTOR_LEAF));
			EXPECT_EQ(tree[LEFT_OF_INSPECTOR].split, UI::DockSplit::Vertical);
			EXPECT_EQ(tree[LEFT_OF_INSPECTOR].childA, static_cast<i32>(UPPER_ROW));
			EXPECT_EQ(tree[LEFT_OF_INSPECTOR].childB, static_cast<i32>(BOTTOM_LEAF));
			EXPECT_EQ(tree[UPPER_ROW].split, UI::DockSplit::Horizontal);
			EXPECT_EQ(tree[UPPER_ROW].childA, static_cast<i32>(HIERARCHY_LEAF));
			EXPECT_EQ(tree[UPPER_ROW].childB, static_cast<i32>(VIEWPORT_LEAF));

			ExpectLeaf(tree[HIERARCHY_LEAF], {FirstInstanceOf("HierarchyPanel")}, "the Hierarchy column");
			ExpectLeaf(tree[VIEWPORT_LEAF],
					   {FirstInstanceOf("SceneViewPanel"), FirstInstanceOf("RuntimeViewPanel")},
					   "the viewport leaf");
			ExpectLeaf(tree[BOTTOM_LEAF],
					   {FirstInstanceOf("AssetBrowserPanel"), FirstInstanceOf("ConsolePanel")},
					   "the bottom leaf");
			ExpectLeaf(tree[INSPECTOR_LEAF], {FirstInstanceOf("InspectorPanel")}, "the Inspector column");
		}

		static void ExpectLeaf(const UI::DockNodeDesc& leaf, const std::vector<StringHash>& tabs,
							   const char* what) {
			EXPECT_EQ(leaf.split, UI::DockSplit::None) << what;
			ASSERT_EQ(leaf.tabs.size(), tabs.size()) << what << " holds " << tabs.size() << " tabs";
			for (size_t i = 0; i < tabs.size(); ++i) {
				EXPECT_EQ(leaf.tabs[i].GetValue(), tabs[i].GetValue()) << what << " tab " << i;
			}
			EXPECT_EQ(leaf.activeTab, 0u) << what;
		}
	};

} // anonymous namespace

TEST_F(EditorDefaultLayoutTest, TheDefaultLayoutIsTheDesignedArrangement) {
	EditorLayoutOps::BuildDefaultLayout(_window);

	EXPECT_EQ(PanelManager::GetOpenPanelCount(), 6u);
	ExpectTheDefaultArrangement(Snapshot());
}

TEST_F(EditorDefaultLayoutTest, DefaultLayoutDoesNotHalveEverySplit) {
	EditorLayoutOps::BuildDefaultLayout(_window);
	std::vector<UI::DockNodeDesc> tree = Snapshot();
	ASSERT_EQ(tree.size(), 7u);

	EXPECT_LT(tree[UPPER_ROW].ratio, 0.4f);
	EXPECT_GT(tree[ROOT].ratio, 0.6f);
	EXPECT_GT(tree[LEFT_OF_INSPECTOR].ratio, 0.6f);
}

TEST_F(EditorDefaultLayoutTest, ResetLayoutRebuildsDefaultOverRearrangedOne) {
	EditorLayoutOps::BuildDefaultLayout(_window);
	Frame();
	const std::vector<UI::DockNodeDesc> pristine = Snapshot();

	Handle<EditorPanel> hierarchy = PanelManager::FindPanelByDockId(FirstInstanceOf("HierarchyPanel"));
	ASSERT_TRUE(hierarchy.IsValid());
	PanelManager::ClosePanel(hierarchy);
	const TypeInfo* inspectorType = TypeRegistry::FindType(StringHash("InspectorPanel"));
	ASSERT_NE(inspectorType, nullptr);
	ASSERT_TRUE(PanelManager::OpenPanel(inspectorType, _window).IsValid());
	Dock().DockPanel(FirstInstanceOf("ConsolePanel"), Dock().GetRootNode(), UI::DockZone::Top);
	Frame();
	const std::vector<UI::DockNodeDesc> rearranged = Snapshot();
	bool differs = rearranged.size() != pristine.size();
	for (size_t i = 0; !differs && i < rearranged.size(); ++i) {
		differs = rearranged[i].split != pristine[i].split || rearranged[i].tabs != pristine[i].tabs;
	}
	ASSERT_TRUE(differs);

	EditorLayoutOps::ResetLayout(_window);
	Frame();
	Frame();

	EXPECT_EQ(PanelManager::GetOpenPanelCount(), 6u);
	ExpectTheDefaultArrangement(Snapshot());
}

TEST_F(EditorDefaultLayoutTest, ResettingDefaultChangesNothing) {
	EditorLayoutOps::BuildDefaultLayout(_window);
	Frame();
	std::vector<UI::DockNodeDesc> before = Snapshot();

	EditorLayoutOps::ResetLayout(_window);
	Frame();
	Frame();

	EXPECT_EQ(PanelManager::GetOpenPanelCount(), 6u);
	std::vector<UI::DockNodeDesc> after = Snapshot();
	ASSERT_EQ(after.size(), before.size());
	for (size_t i = 0; i < after.size(); ++i) {
		EXPECT_EQ(after[i].split, before[i].split) << "node " << i;
		EXPECT_FLOAT_EQ(after[i].ratio, before[i].ratio) << "node " << i;
		EXPECT_EQ(after[i].childA, before[i].childA) << "node " << i;
		EXPECT_EQ(after[i].childB, before[i].childB) << "node " << i;
		ASSERT_EQ(after[i].tabs.size(), before[i].tabs.size()) << "node " << i;
		for (size_t tab = 0; tab < after[i].tabs.size(); ++tab) {
			EXPECT_EQ(after[i].tabs[tab].GetValue(), before[i].tabs[tab].GetValue()) << "node " << i;
		}
	}
	ExpectTheDefaultArrangement(after);
}

TEST_F(EditorDefaultLayoutTest, PlayBorrowsViewportLeaf) {
	EditorLayoutOps::BuildDefaultLayout(_window);
	Frame();
	StringHash sceneView = FirstInstanceOf("SceneViewPanel");
	StringHash runtimeView = FirstInstanceOf("RuntimeViewPanel");
	u32 leaf = Dock().FindNodeOfPanel(runtimeView);
	ASSERT_NE(leaf, UI::INVALID_DOCK_NODE);
	ASSERT_EQ(Dock().GetActivePanel(leaf).GetValue(), sceneView.GetValue());

	Vector<StringHash> displaced{GetMemoryResource(MemTag::Editor)};
	EditorLayoutOps::BringPanelsForward(TypeRegistry::FindType(StringHash("RuntimeViewPanel")), displaced);

	EXPECT_EQ(Dock().GetActivePanel(leaf).GetValue(), runtimeView.GetValue());
	ASSERT_EQ(displaced.size(), 1u);
	EXPECT_EQ(displaced[0].GetValue(), sceneView.GetValue());

	EditorLayoutOps::RestoreForwardPanels({displaced.data(), displaced.size()});
	EXPECT_EQ(Dock().GetActivePanel(leaf).GetValue(), sceneView.GetValue());
}

TEST_F(EditorDefaultLayoutTest, ViewAlreadyForwardRecordsNoWayBack) {
	EditorLayoutOps::BuildDefaultLayout(_window);
	Frame();
	StringHash runtimeView = FirstInstanceOf("RuntimeViewPanel");
	ASSERT_TRUE(Dock().ActivatePanel(runtimeView));

	Vector<StringHash> displaced{GetMemoryResource(MemTag::Editor)};
	EditorLayoutOps::BringPanelsForward(TypeRegistry::FindType(StringHash("RuntimeViewPanel")), displaced);
	EXPECT_TRUE(displaced.empty());
	EXPECT_EQ(Dock().GetActivePanel(Dock().FindNodeOfPanel(runtimeView)).GetValue(), runtimeView.GetValue());
}

TEST_F(EditorDefaultLayoutTest, RestoreSkipsVanishedTab) {
	EditorLayoutOps::BuildDefaultLayout(_window);
	Frame();
	StringHash runtimeView = FirstInstanceOf("RuntimeViewPanel");
	Vector<StringHash> displaced{GetMemoryResource(MemTag::Editor)};
	EditorLayoutOps::BringPanelsForward(TypeRegistry::FindType(StringHash("RuntimeViewPanel")), displaced);
	ASSERT_EQ(displaced.size(), 1u);

	Vector<Handle<EditorPanel>> open{GetMemoryResource(MemTag::Editor)};
	PanelManager::GetOpenPanels(open);
	for (Handle<EditorPanel> panel : open) {
		if (panel.Get() && panel.Get()->GetDockId() == displaced[0]) {
			PanelManager::ClosePanel(panel);
		}
	}
	Frame();
	Frame();

	EditorLayoutOps::RestoreForwardPanels({displaced.data(), displaced.size()});
	u32 leaf = Dock().FindNodeOfPanel(runtimeView);
	ASSERT_NE(leaf, UI::INVALID_DOCK_NODE);
	EXPECT_EQ(Dock().GetActivePanel(leaf).GetValue(), runtimeView.GetValue());
}

TEST_F(EditorDefaultLayoutTest, ActivePanelNullForNonLeaf) {
	EditorLayoutOps::BuildDefaultLayout(_window);
	Frame();
	EXPECT_EQ(Dock().GetActivePanel(Dock().GetRootNode()).GetValue(), 0u);
	EXPECT_EQ(Dock().GetActivePanel(UI::INVALID_DOCK_NODE).GetValue(), 0u);
	EXPECT_EQ(Dock().GetActivePanel(0xDEADu).GetValue(), 0u);
}

namespace {

	std::filesystem::path MakeLayoutDir() {
		static std::atomic<u64> counter{0};
		const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
		std::filesystem::path root = std::filesystem::temp_directory_path() /
									 ("shalltear_layouts_" + std::to_string(stamp) + "_" +
									  std::to_string(counter.fetch_add(1)));
		std::filesystem::create_directories(root);
		return root;
	}

} // namespace

TEST_F(EditorDefaultLayoutTest, CapturedLayoutRebuilds) {
	EditorLayoutOps::BuildDefaultLayout(_window);
	Frame();
	const str document = EditorLayoutOps::CaptureLayout(_window);
	ASSERT_FALSE(document.empty());

	Vector<Handle<EditorPanel>> open{GetMemoryResource(MemTag::Editor)};
	PanelManager::GetOpenPanels(open);
	for (Handle<EditorPanel> panel : open) {
		PanelManager::ClosePanel(panel);
	}
	Frame();
	ASSERT_EQ(PanelManager::GetOpenPanelCount(), 0u);

	ASSERT_TRUE(EditorLayoutOps::ApplyLayout(_window, document));
	Frame();
	ExpectTheDefaultArrangement(Snapshot());
}

TEST_F(EditorDefaultLayoutTest, AnUnreadableLayoutClosesNothing) {
	EditorLayoutOps::BuildDefaultLayout(_window);
	Frame();
	const u32 before = PanelManager::GetOpenPanelCount();
	ASSERT_GT(before, 0u);

	EXPECT_FALSE(EditorLayoutOps::ApplyLayout(_window, "[Editor]\nversion = 9999\n"));
	EXPECT_FALSE(EditorLayoutOps::ApplyLayout(_window, "not an ini at all"));
	Frame();
	EXPECT_EQ(PanelManager::GetOpenPanelCount(), before);
}

TEST_F(EditorDefaultLayoutTest, SwitchSavesAndRestoresLayouts) {
	const std::filesystem::path directory = MakeLayoutDir();
	EditorLayoutOps::InitializeLayoutStore(Path(directory.generic_string()));

	EditorLayoutOps::BuildDefaultLayout(_window);
	Frame();
	ASSERT_TRUE(EditorLayoutOps::SaveLayoutAs(_window, "Work"));
	EXPECT_EQ(EditorLayoutOps::GetActiveLayout(), "Work");

	const StringHash runtimeView = FirstInstanceOf("RuntimeViewPanel");
	ASSERT_TRUE(Dock().ActivatePanel(runtimeView));
	Frame();
	const u32 leafA = Dock().FindNodeOfPanel(runtimeView);
	ASSERT_NE(leafA, UI::INVALID_DOCK_NODE);
	ASSERT_EQ(Dock().GetActivePanel(leafA).GetValue(), runtimeView.GetValue());

	ASSERT_TRUE(EditorLayoutOps::SwitchLayout(_window, EditorLayoutOps::DefaultLayoutName));
	Frame();
	EXPECT_EQ(EditorLayoutOps::GetActiveLayout(), EditorLayoutOps::DefaultLayoutName);
	const StringHash sceneView = FirstInstanceOf("SceneViewPanel");
	const u32 leafB = Dock().FindNodeOfPanel(sceneView);
	ASSERT_NE(leafB, UI::INVALID_DOCK_NODE);
	EXPECT_EQ(Dock().GetActivePanel(leafB).GetValue(), sceneView.GetValue());

	ASSERT_TRUE(EditorLayoutOps::SwitchLayout(_window, "Work"));
	Frame();
	EXPECT_EQ(EditorLayoutOps::GetActiveLayout(), "Work");
	const u32 leaf = Dock().FindNodeOfPanel(runtimeView);
	ASSERT_NE(leaf, UI::INVALID_DOCK_NODE);
	EXPECT_EQ(Dock().GetActivePanel(leaf).GetValue(), runtimeView.GetValue());

	EditorLayoutOps::InitializeLayoutStore(Path());
	std::error_code ec;
	std::filesystem::remove_all(directory, ec);
}

TEST_F(EditorDefaultLayoutTest, LayoutListAlwaysLeadsWithDefault) {
	const std::filesystem::path directory = MakeLayoutDir();
	EditorLayoutOps::InitializeLayoutStore(Path(directory.generic_string()));
	EditorLayoutOps::BuildDefaultLayout(_window);
	Frame();

	Vector<str> names{GetMemoryResource(MemTag::Editor)};
	EditorLayoutOps::GetLayoutNames(names);
	ASSERT_EQ(names.size(), 1u);
	EXPECT_EQ(names[0], EditorLayoutOps::DefaultLayoutName);

	ASSERT_TRUE(EditorLayoutOps::SaveLayoutAs(_window, "Zebra"));
	ASSERT_TRUE(EditorLayoutOps::SaveLayoutAs(_window, "apples"));
	names.clear();
	EditorLayoutOps::GetLayoutNames(names);
	ASSERT_EQ(names.size(), 3u);
	EXPECT_EQ(names[0], EditorLayoutOps::DefaultLayoutName);
	EXPECT_EQ(names[1], "apples");
	EXPECT_EQ(names[2], "Zebra");

	EditorLayoutOps::InitializeLayoutStore(Path());
	std::error_code ec;
	std::filesystem::remove_all(directory, ec);
}

TEST_F(EditorDefaultLayoutTest, InvalidNameRefusedDefaultProtected) {
	const std::filesystem::path directory = MakeLayoutDir();
	EditorLayoutOps::InitializeLayoutStore(Path(directory.generic_string()));
	EditorLayoutOps::BuildDefaultLayout(_window);
	Frame();

	EXPECT_FALSE(EditorLayoutOps::SaveLayoutAs(_window, ""));
	EXPECT_FALSE(EditorLayoutOps::SaveLayoutAs(_window, "   "));
	EXPECT_FALSE(EditorLayoutOps::SaveLayoutAs(_window, "../../etc"));
	EXPECT_FALSE(EditorLayoutOps::SaveLayoutAs(_window, "C:/Windows/system32"));

	EXPECT_FALSE(EditorLayoutOps::DeleteLayout(EditorLayoutOps::DefaultLayoutName));
	EXPECT_FALSE(EditorLayoutOps::RenameLayout(EditorLayoutOps::DefaultLayoutName, "Something"));

	ASSERT_TRUE(EditorLayoutOps::SaveLayoutAs(_window, "Before"));
	ASSERT_TRUE(EditorLayoutOps::RenameLayout("Before", "After"));
	EXPECT_EQ(EditorLayoutOps::GetActiveLayout(), "After");
	EXPECT_FALSE(std::filesystem::exists(directory / "Before.ini"));
	ASSERT_TRUE(std::filesystem::exists(directory / "After.ini"));

	Vector<str> names{GetMemoryResource(MemTag::Editor)};
	EditorLayoutOps::GetLayoutNames(names);
	ASSERT_EQ(names.size(), 2u);
	EXPECT_EQ(names[1], "After");

	const u32 panels = PanelManager::GetOpenPanelCount();
	ASSERT_TRUE(EditorLayoutOps::DeleteLayout("After"));
	Frame();
	EXPECT_EQ(EditorLayoutOps::GetActiveLayout(), EditorLayoutOps::DefaultLayoutName);
	EXPECT_EQ(PanelManager::GetOpenPanelCount(), panels);

	EditorLayoutOps::InitializeLayoutStore(Path());
	std::error_code ec;
	std::filesystem::remove_all(directory, ec);
}

TEST_F(EditorDefaultLayoutTest, DirectorylessStoreWritesNothing) {
	EditorLayoutOps::InitializeLayoutStore(Path());
	EditorLayoutOps::BuildDefaultLayout(_window);
	Frame();

	EXPECT_TRUE(EditorLayoutOps::SaveLayoutAs(_window, "Ephemeral"));
	EXPECT_EQ(EditorLayoutOps::GetActiveLayout(), "Ephemeral");
	Vector<str> names{GetMemoryResource(MemTag::Editor)};
	EditorLayoutOps::GetLayoutNames(names);
	EXPECT_EQ(names.size(), 1u);
	EditorLayoutOps::SaveActiveLayout(_window);
}
