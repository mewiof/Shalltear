#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Config.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Settings.h"

#include "Shalltear/Editor/BuildPipeline.h"
#include "Shalltear/Editor/BuildProfile.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PackageOps.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/ProjectHub.h"
#include "Shalltear/Editor/ProjectOps.h"
#include "Shalltear/Editor/SceneAnnotations.h"
#include "Shalltear/Editor/SettingsOps.h"
#include "Shalltear/Editor/SettingsPresentation.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace Shalltear;
using namespace UITest;

namespace {

	namespace fs = std::filesystem;

	void CollectWithClass(UI::Context& context, UI::ElementHandle from, StringHash klass,
						  Vector<UI::ElementHandle>& out) {
		if (context.HasClass(from, klass)) {
			out.push_back(from);
		}
		const UI::Element* element = context.GetElement(from);
		for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(child);
			CollectWithClass(context, handle, klass, out);
			child = context.GetElement(handle)->nextSibling;
		}
	}

	Vector<UI::ElementHandle> RowsUnder(UI::Context& context, UI::ElementHandle from) {
		Vector<UI::ElementHandle> rows{GetMemoryResource(MemTag::Editor)};
		CollectWithClass(context, from, StringHash("slt-tree-row"), rows);
		return rows;
	}

	UI::ElementHandle ScrollViewportUnder(UI::Context& context, UI::ElementHandle from) {
		Vector<UI::ElementHandle> found{GetMemoryResource(MemTag::Editor)};
		CollectWithClass(context, from, StringHash("slt-scroll-viewport"), found);
		return found.empty() ? UI::ElementHandle{} : found.front();
	}

	str TextAnywhereIn(UI::Context& context, UI::ElementHandle from) {
		str out = context.GetTextString(from);
		const UI::Element* element = context.GetElement(from);
		for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(child);
			out += TextAnywhereIn(context, handle);
			child = context.GetElement(handle)->nextSibling;
		}
		return out;
	}

	UI::ElementHandle FirstWithText(UI::Context& context, UI::ElementHandle from, std::string_view text) {
		if (context.GetTextString(from) == text) {
			return from;
		}
		const UI::Element* element = context.GetElement(from);
		for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(child);
			if (UI::ElementHandle found = FirstWithText(context, handle, text); found.IsValid()) {
				return found;
			}
			child = context.GetElement(handle)->nextSibling;
		}
		return {};
	}

	UI::ElementHandle RowSaying(UI::Context& context, UI::ElementHandle from, std::string_view fragment) {
		for (UI::ElementHandle row : RowsUnder(context, from)) {
			if (TextAnywhereIn(context, row).find(fragment) != str::npos) {
				return row;
			}
		}
		return {};
	}

	bool IsLit(UI::Context& context, UI::ElementHandle row) {
		return context.IsAlive(row) && context.HasClass(row, StringHash("slt-selected"));
	}

	u32 LitCount(UI::Context& context, UI::ElementHandle from) {
		u32 count = 0;
		for (UI::ElementHandle row : RowsUnder(context, from)) {
			count += IsLit(context, row) ? 1u : 0u;
		}
		return count;
	}

	Vec2 CenterOf(UI::Context& context, UI::ElementHandle handle) {
		const UI::Rect& box = context.GetLayout(handle).borderBox;
		return {box.x + box.w * 0.5f, box.y + box.h * 0.5f};
	}

	f32 TopOf(UI::Context& context, UI::ElementHandle handle) {
		return context.GetLayout(handle).borderBox.y;
	}

	void InjectClick(UI::Context& context, Vec2 point, KeyMod mods = KeyMod::None,
					 MouseButton button = MouseButton::Left) {
		InputEvent event{};
		event.button = button;
		event.mousePosPx = {static_cast<i32>(point.x), static_cast<i32>(point.y)};
		event.mods = mods;
		event.type = InputEventType::MouseMove;
		context.InjectInput(event);
		event.type = InputEventType::MouseDown;
		context.InjectInput(event);
		event.type = InputEventType::MouseUp;
		context.InjectInput(event);
		context.Update();
	}

	void InjectKey(UI::Context& context, Key key, KeyMod mods = KeyMod::None) {
		InputEvent event{};
		event.type = InputEventType::KeyDown;
		event.key = key;
		event.mods = mods;
		context.InjectInput(event);
		context.Update();
	}

	class PackageListTest : public ::testing::Test {
	protected:
		fs::path _root;
		SettingsPaths _paths;
		MockFontSystem _fonts;
		std::unique_ptr<UI::Context> _context;
		UI::ElementHandle _panelRoot;
		Handle<EditorPanel> _panel;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = fs::temp_directory_path() /
					("shalltear_m5packages_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			fs::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());
			Settings::Initialize(_paths);
			PackageOps::DeclareSettings();

			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			PanelManager::Initialize();
		}

		void TearDown() override {
			_context.reset();
			PanelManager::Terminate();
			Settings::Terminate();
			std::error_code ec;
			fs::remove_all(_root, ec);
		}

		void OpenPanel() {
			_panel = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("PackageManagerPanel")));
			ASSERT_TRUE(_panel.IsValid());
			UI::ContextDesc desc;
			desc.sizePx = {900, 700};
			_context = std::make_unique<UI::Context>(desc, &_fonts);
			EditorTheme::ApplyToContext(*_context);
			_panelRoot = _context->CreateElement();
			_context->AppendChild(_context->GetRoot(), _panelRoot);
			UI::Style fill;
			fill.width = UI::Length::Percent(1.0f);
			fill.height = UI::Length::Percent(1.0f);
			_context->SetStyle(_panelRoot, fill);
			_panel.Get()->BuildContent(*_context, _panelRoot);
			_context->Update();
			Tick();
		}

		void Tick() {
			_panel.Get()->OnTick(0.016f);
			_context->Update();
		}

		Vector<UI::ElementHandle> Rows() { return RowsUnder(*_context, _panelRoot); }

		void Click(UI::ElementHandle row, KeyMod mods = KeyMod::None) {
			InjectClick(*_context, CenterOf(*_context, row), mods);
			Tick();
		}

		void Press(Key key, KeyMod mods = KeyMod::None) {
			InjectKey(*_context, key, mods);
			Tick();
		}

		str Selected() const {
			Variant boxed;
			EXPECT_TRUE(GetField(_panel.Get(), _panel.Get()->GetType(), StringHash("selectedPackage"), &boxed));
			return boxed.As<str>();
		}
	};

	struct NavProbeSettings {
		SLT_REFLECT_VALUE(NavProbeSettings)
		SLT_REFLECT_CONSTRUCTIBLE()
		SLT_REFLECT_FIELD(budget)
		SLT_END_REFLECT()

		i32 budget = 4;
	};

	class SettingsListTest : public ::testing::Test {
	protected:
		fs::path _root;
		SettingsPaths _paths;
		MockFontSystem _fonts;
		std::unique_ptr<UI::Context> _context;
		UI::ElementHandle _panelRoot;
		Handle<EditorPanel> _panel;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = fs::temp_directory_path() /
					("shalltear_m5settings_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			fs::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());

			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			SceneSystem::Initialize();
			PanelManager::Initialize();
		}

		void TearDown() override {
			_context.reset();
			PanelManager::Terminate();
			SceneSystem::Terminate();
			SettingsPresentation::Terminate();
			Settings::Terminate();
			std::error_code ec;
			fs::remove_all(_root, ec);
		}

		void DeclareProbes() {
			Settings::Initialize(_paths);
			ASSERT_NE(Settings::DeclareCategory<NavProbeSettings>("Alpha", SettingScope::Project), nullptr);
			ASSERT_NE(Settings::DeclareCategory<NavProbeSettings>("Mid/Inner", SettingScope::Project), nullptr);
		}

		void OpenPanel() {
			_panel = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("SettingsPanel")));
			ASSERT_TRUE(_panel.IsValid());
			UI::ContextDesc desc;
			desc.sizePx = {900, 700};
			_context = std::make_unique<UI::Context>(desc, &_fonts);
			EditorTheme::ApplyToContext(*_context);
			_panelRoot = _context->CreateElement();
			_context->AppendChild(_context->GetRoot(), _panelRoot);
			UI::Style fill;
			fill.width = UI::Length::Percent(1.0f);
			fill.height = UI::Length::Percent(1.0f);
			_context->SetStyle(_panelRoot, fill);
			_panel.Get()->BuildContent(*_context, _panelRoot);
			_context->Update();
			Tick();
		}

		void Tick() {
			_panel.Get()->OnTick(0.016f);
			_context->Update();
		}

		void Click(UI::ElementHandle row, KeyMod mods = KeyMod::None) {
			InjectClick(*_context, CenterOf(*_context, row), mods);
			Tick();
		}

		void Press(Key key, KeyMod mods = KeyMod::None) {
			InjectKey(*_context, key, mods);
			Tick();
		}

		str SelectedPath() const {
			Variant boxed;
			EXPECT_TRUE(GetField(_panel.Get(), _panel.Get()->GetType(), StringHash("selectedPath"), &boxed));
			return boxed.As<str>();
		}
	};

	class BuildProfileListTest : public ::testing::Test {
	protected:
		fs::path _root;
		BuildPipeline _pipeline;
		MockFontSystem _fonts;
		std::unique_ptr<UI::Context> _context;
		UI::ElementHandle _panelRoot;
		Handle<EditorPanel> _panel;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = fs::temp_directory_path() /
					("shalltear_m5profiles_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			fs::create_directories(_root / "Assets" / "Scenes");
			Config projectFile;
			projectFile.SetString("Project", "name", "NavProbe");
			projectFile.Save(Path(_root.generic_string() + "/Project.ini"));

			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			SceneSystem::Initialize();
			EditorSceneOps::Initialize();
			EditorSelection::Initialize();
			UndoSystem::Initialize();
			SceneAnnotations::Initialize();
			PanelManager::Initialize();
			ASSERT_TRUE(ProjectOps::MountProject(_root.generic_string()));
			SceneSystem::CreateScene("Fixture");
		}

		void TearDown() override {
			_context.reset();
			PanelManager::Terminate();
			SceneAnnotations::Terminate();
			UndoSystem::Terminate();
			EditorSelection::Terminate();
			EditorSceneOps::Terminate();
			SceneSystem::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			fs::remove_all(_root, ec);
		}

		str Root() const { return _root.generic_string(); }

		void WriteProfiles(std::initializer_list<const char*> names) {
			Vector<str> roster{GetMemoryResource(MemTag::Editor)};
			for (const char* name : names) {
				BuildProfile profile;
				profile.SetString("target", "windows");
				ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), name), profile));
				roster.push_back(str(name));
			}
			BuildProfileOps::WriteProfileRoster(Root(), roster);
		}

		void OpenPanel() {
			_panel = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("BuildProfilesPanel")));
			ASSERT_TRUE(_panel.IsValid());
			UI::ContextDesc desc;
			desc.sizePx = {900, 700};
			_context = std::make_unique<UI::Context>(desc, &_fonts);
			EditorTheme::ApplyToContext(*_context);
			_panelRoot = _context->CreateElement();
			_context->AppendChild(_context->GetRoot(), _panelRoot);
			UI::Style fill;
			fill.width = UI::Length::Percent(1.0f);
			fill.height = UI::Length::Percent(1.0f);
			_context->SetStyle(_panelRoot, fill);
			_panel.Get()->BuildContent(*_context, _panelRoot);
			_context->Update();
			Tick();
			Tick();
		}

		void Tick() {
			_panel.Get()->OnTick(0.016f);
			_context->Update();
		}

		UI::ElementHandle Row(std::string_view name) { return RowSaying(*_context, _panelRoot, name); }

		void Click(UI::ElementHandle element, KeyMod mods = KeyMod::None) {
			InjectClick(*_context, CenterOf(*_context, element), mods);
			Tick();
		}

		void Press(Key key, KeyMod mods = KeyMod::None) {
			InjectKey(*_context, key, mods);
			Tick();
		}

		bool Command(PanelCommand command) {
			bool answered = _panel.Get()->OnCommand(command);
			_context->Update();
			Tick();
			return answered;
		}

		str Selected() const {
			Variant boxed;
			EXPECT_TRUE(GetField(_panel.Get(), _panel.Get()->GetType(), StringHash("selectedProfile"), &boxed));
			return boxed.As<str>();
		}

		bool ProfileExists(const str& name) const {
			return fs::exists(fs::path(BuildProfileOps::GetProfilePath(Root(), name)));
		}

		void PressPopupButton(std::string_view label) {
			UI::ElementHandle popup = _context->GetOpenPopup(0);
			ASSERT_TRUE(_context->IsAlive(popup));
			UI::ElementHandle button = FirstWithText(*_context, popup, label);
			ASSERT_TRUE(_context->IsAlive(button)) << label << "'";
			InjectClick(*_context, CenterOf(*_context, button));
			Tick();
			Tick();
		}
	};

	class HubListTest : public ::testing::Test {
	protected:
		fs::path _root;
		Path _registry;
		MockFontSystem _fonts;
		std::unique_ptr<UI::Context> _context;
		std::unique_ptr<ProjectHub> _hub;
		UI::ElementHandle _hubRoot;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = fs::temp_directory_path() /
					("shalltear_m5hub_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			fs::create_directories(_root);
			_registry = Path((_root / "Projects.ini").generic_string());
		}

		void TearDown() override {
			_context.reset();
			_hub.reset();
			FileSystem::Unmount("project");
			std::error_code ec;
			fs::remove_all(_root, ec);
		}

		str MakeProject(const str& name) {
			str path;
			EXPECT_TRUE(ProjectOps::CreateProject(_root.generic_string(), name, path));
			ProjectOps::AddProject(_registry, path);
			return path;
		}

		void AddBrokenEntry(const str& name) {
			str path = (_root / name).generic_string();
			fs::create_directories(fs::path(path));
			str created;
			ASSERT_TRUE(ProjectOps::CreateProject(_root.generic_string(), name + "Tmp", created));
			ProjectOps::AddProject(_registry, created);
			std::error_code ec;
			fs::remove_all(fs::path(created), ec);
		}

		void Build() {
			_hub = std::make_unique<ProjectHub>(_registry);
			UI::ContextDesc desc;
			desc.sizePx = {940, 560};
			_context = std::make_unique<UI::Context>(desc, &_fonts);
			_hubRoot = _context->CreateElement();
			_context->AppendChild(_context->GetRoot(), _hubRoot);
			_hub->BuildContent(*_context, _hubRoot);
			_context->Update();
		}

		UI::ElementHandle Row(std::string_view name) { return RowSaying(*_context, _hubRoot, name); }

		void Click(UI::ElementHandle row, KeyMod mods = KeyMod::None) {
			InjectClick(*_context, CenterOf(*_context, row), mods);
		}

		void Press(Key key, KeyMod mods = KeyMod::None) { InjectKey(*_context, key, mods); }
	};

} // namespace

SLT_REGISTER_TYPE(NavProbeSettings)

TEST_F(PackageListTest, ArrowsMoveSelectionDetailFollows) {
	OpenPanel();
	Vector<UI::ElementHandle> rows = Rows();
	ASSERT_GE(rows.size(), 2u);

	Click(rows[0]);
	str first = Selected();
	ASSERT_FALSE(first.empty());
	EXPECT_TRUE(IsLit(*_context, Rows()[0]));

	Press(Key::Down);
	str second = Selected();
	EXPECT_NE(second, first);
	EXPECT_TRUE(IsLit(*_context, Rows()[1]));
	EXPECT_FALSE(IsLit(*_context, Rows()[0]));
	EXPECT_NE(TextAnywhereIn(*_context, _panelRoot).find(second), str::npos);

	Press(Key::Up);
	EXPECT_EQ(Selected(), first);
}

TEST_F(PackageListTest, SingleModeCtrlClickSelects) {
	OpenPanel();
	Vector<UI::ElementHandle> rows = Rows();
	ASSERT_GE(rows.size(), 2u);

	Click(rows[0]);
	str first = Selected();
	Click(Rows()[1], KeyMod::Ctrl);
	EXPECT_NE(Selected(), first);
	EXPECT_EQ(LitCount(*_context, _panelRoot), 1u);

	Click(Rows()[1], KeyMod::Ctrl);
	EXPECT_EQ(LitCount(*_context, _panelRoot), 1u);
	EXPECT_FALSE(Selected().empty());
}

TEST_F(PackageListTest, CtrlMeansNothingInSingleSelectList) {
	OpenPanel();
	Vector<UI::ElementHandle> rows = Rows();
	ASSERT_GE(rows.size(), 2u);
	Click(rows[0]);

	EXPECT_FALSE(_panel.Get()->OnCommand(PanelCommand::SelectAll));
	Tick();
	EXPECT_EQ(LitCount(*_context, _panelRoot), 1u);
}

TEST_F(PackageListTest, ChoosingRowRestylesListNotRebuildingIt) {
	OpenPanel();
	Vector<UI::ElementHandle> rows = Rows();
	ASSERT_GE(rows.size(), 2u);
	UI::ElementHandle first = rows[0];

	Click(first);
	Click(Rows()[1]);

	EXPECT_TRUE(_context->IsAlive(first));
	EXPECT_TRUE(IsLit(*_context, Rows()[1]));
	EXPECT_FALSE(IsLit(*_context, Rows()[0]));
}

TEST_F(PackageListTest, HeadingsAboveFirstRow) {
	OpenPanel();
	UI::ElementHandle heading = FirstWithText(*_context, _panelRoot, "Built-in");
	ASSERT_TRUE(_context->IsAlive(heading));

	Vector<UI::ElementHandle> rows = Rows();
	ASSERT_FALSE(rows.empty());
	EXPECT_LT(TopOf(*_context, heading), TopOf(*_context, rows.front()));
	EXPECT_FALSE(_context->HasClass(heading, StringHash("slt-tree-row")));
}

TEST_F(SettingsListTest, ArrowingCategoriesOpensEachPageInTurn) {
	DeclareProbes();
	OpenPanel();

	UI::ElementHandle alpha = RowSaying(*_context, _panelRoot, "Alpha");
	ASSERT_TRUE(_context->IsAlive(alpha));
	Click(alpha);
	ASSERT_EQ(SelectedPath(), "Alpha");

	Press(Key::Down);
	EXPECT_EQ(SelectedPath(), "Mid/Inner");
	EXPECT_NE(TextAnywhereIn(*_context, _panelRoot).find("budget"), str::npos);
}

TEST_F(SettingsListTest, ArrowSkipsPagelessGroup) {
	DeclareProbes();
	OpenPanel();

	UI::ElementHandle alpha = RowSaying(*_context, _panelRoot, "Alpha");
	UI::ElementHandle group = RowSaying(*_context, _panelRoot, "Mid");
	UI::ElementHandle inner = RowSaying(*_context, _panelRoot, "Inner");
	ASSERT_TRUE(_context->IsAlive(alpha));
	ASSERT_TRUE(_context->IsAlive(group));
	ASSERT_TRUE(_context->IsAlive(inner));
	ASSERT_LT(TopOf(*_context, alpha), TopOf(*_context, group));
	ASSERT_LT(TopOf(*_context, group), TopOf(*_context, inner));

	Click(alpha);
	ASSERT_EQ(SelectedPath(), "Alpha");
	Press(Key::Down);

	EXPECT_EQ(SelectedPath(), "Mid/Inner");
	EXPECT_FALSE(IsLit(*_context, RowSaying(*_context, _panelRoot, "Mid")));
}

TEST_F(SettingsListTest, ChoosingCategoryRestylesTreeNotRebuildingIt) {
	DeclareProbes();
	OpenPanel();

	UI::ElementHandle alpha = RowSaying(*_context, _panelRoot, "Alpha");
	ASSERT_TRUE(_context->IsAlive(alpha));

	Click(alpha);
	Click(RowSaying(*_context, _panelRoot, "Inner"));

	EXPECT_TRUE(_context->IsAlive(alpha));
	EXPECT_TRUE(IsLit(*_context, RowSaying(*_context, _panelRoot, "Inner")));
	EXPECT_FALSE(IsLit(*_context, RowSaying(*_context, _panelRoot, "Alpha")));
}

TEST_F(BuildProfileListTest, ArrowingRosterMovesSelectionAndPaneFollows) {
	WriteProfiles({"Alpha", "Beta"});
	OpenPanel();

	Click(Row("Alpha"));
	ASSERT_EQ(Selected(), "Alpha");
	Press(Key::Down);
	EXPECT_EQ(Selected(), "Beta");
	EXPECT_TRUE(IsLit(*_context, Row("Beta")));
	EXPECT_FALSE(IsLit(*_context, Row("Alpha")));
}

TEST_F(BuildProfileListTest, ChoosingProfileRestylesRosterNotRebuildingIt) {
	WriteProfiles({"Alpha", "Beta"});
	OpenPanel();

	UI::ElementHandle alpha = Row("Alpha");
	ASSERT_TRUE(_context->IsAlive(alpha));

	Click(alpha);
	Click(Row("Beta"));

	EXPECT_TRUE(_context->IsAlive(alpha));
	EXPECT_TRUE(IsLit(*_context, Row("Beta")));
	EXPECT_FALSE(IsLit(*_context, Row("Alpha")));
}

TEST_F(BuildProfileListTest, DeleteConfirmsAndRemovesProfile) {
	WriteProfiles({"Alpha", "Beta"});
	OpenPanel();

	Click(Row("Beta"));
	ASSERT_EQ(Selected(), "Beta");
	ASSERT_TRUE(ProfileExists("Beta"));

	ASSERT_TRUE(Command(PanelCommand::Delete));
	PressPopupButton("Delete");

	EXPECT_FALSE(ProfileExists("Beta"));
	EXPECT_TRUE(ProfileExists("Alpha"));
}

TEST_F(BuildProfileListTest, DuplicateThroughCommandRoadCopiesSelectedProfile) {
	WriteProfiles({"Alpha", "Beta"});
	OpenPanel();

	Click(Row("Alpha"));
	ASSERT_TRUE(Command(PanelCommand::Duplicate));
	PressPopupButton("Duplicate");

	EXPECT_TRUE(ProfileExists("Alpha Copy"));
	EXPECT_TRUE(ProfileExists("Alpha"));
}

TEST_F(BuildProfileListTest, RenameThroughCommandRoadNamesSelectedProfile) {
	WriteProfiles({"Alpha", "Beta"});
	OpenPanel();

	Click(Row("Beta"));
	ASSERT_TRUE(Command(PanelCommand::Rename));
	UI::ElementHandle popup = _context->GetOpenPopup(0);
	ASSERT_TRUE(_context->IsAlive(popup));
	EXPECT_NE(TextAnywhereIn(*_context, popup).find("Beta"), str::npos);
}

TEST_F(BuildProfileListTest, EyeTogglesWithoutSelecting) {
	WriteProfiles({"Alpha", "Beta"});
	OpenPanel();

	UI::ElementHandle row = Row("Alpha");
	ASSERT_TRUE(_context->IsAlive(row));
	Vector<UI::ElementHandle> gutters{GetMemoryResource(MemTag::Editor)};
	CollectWithClass(*_context, row, StringHash("slt-tree-arrow"), gutters);
	ASSERT_EQ(gutters.size(), 1u);

	BuildProfile before = BuildProfile();
	BuildProfileOps::LoadProfile(BuildProfileOps::GetProfilePath(Root(), "Alpha"), before);
	Click(gutters.front());

	BuildProfile after = BuildProfile();
	BuildProfileOps::LoadProfile(BuildProfileOps::GetProfilePath(Root(), "Alpha"), after);
	EXPECT_NE(after.GetString("includeInBuildAll", ""), before.GetString("includeInBuildAll", ""));
}

TEST_F(HubListTest, EnterOpensTheSelectedProject) {
	str alpha = MakeProject("Alpha");
	str beta = MakeProject("Beta");
	Build();

	UI::ElementHandle row = Row("Alpha");
	ASSERT_TRUE(_context->IsAlive(row));
	Click(row);
	ASSERT_EQ(_hub->GetSelectedPath(), alpha);
	EXPECT_TRUE(_hub->GetChosenPath().empty());

	Press(Key::Enter);
	EXPECT_EQ(_hub->GetChosenPath(), alpha);
	EXPECT_NE(_hub->GetChosenPath(), beta);
}

TEST_F(HubListTest, ArrowingTheRecentsMovesTheSelection) {
	MakeProject("Alpha");
	MakeProject("Beta");
	Build();

	Vector<UI::ElementHandle> rows = RowsUnder(*_context, _hubRoot);
	ASSERT_EQ(rows.size(), 2u);

	Click(rows[0]);
	str first = _hub->GetSelectedPath();
	ASSERT_FALSE(first.empty());
	ASSERT_TRUE(IsLit(*_context, rows[0]));

	Press(Key::Down);
	EXPECT_TRUE(IsLit(*_context, rows[1]));
	EXPECT_FALSE(IsLit(*_context, rows[0]));
	EXPECT_NE(_hub->GetSelectedPath(), first);

	Press(Key::Up);
	EXPECT_EQ(_hub->GetSelectedPath(), first);
	EXPECT_TRUE(IsLit(*_context, rows[0]));
}

TEST_F(HubListTest, CtrlADelForgetsRecents) {
	str alpha = MakeProject("Alpha");
	str beta = MakeProject("Beta");
	AddBrokenEntry("Broken");
	Build();
	ASSERT_EQ(RowsUnder(*_context, _hubRoot).size(), 3u);

	EXPECT_FALSE(_hub->OnCommand(PanelCommand::SelectAll));
	EXPECT_FALSE(IsLit(*_context, Row("Alpha")));

	Click(Row("Alpha"));
	EXPECT_TRUE(_hub->OnCommand(PanelCommand::SelectAll));
	_context->Update();
	EXPECT_TRUE(IsLit(*_context, Row("Alpha")));
	EXPECT_TRUE(IsLit(*_context, Row("Beta")));
	EXPECT_FALSE(IsLit(*_context, Row("BrokenTmp")));
	EXPECT_EQ(_hub->GetSelectedPath(), alpha);

	EXPECT_TRUE(_hub->OnCommand(PanelCommand::Delete));
	ASSERT_EQ(_hub->GetEntries().size(), 1u);
	EXPECT_FALSE(_hub->GetEntries()[0].IsOpenable());
	EXPECT_TRUE(_hub->GetSelectedPath().empty());
	EXPECT_TRUE(fs::exists(fs::path(alpha) / "Project.ini"));
	EXPECT_TRUE(fs::exists(fs::path(beta) / "Project.ini"));
}

TEST_F(HubListTest, DelForgetsSelectedRecent) {
	str alpha = MakeProject("Alpha");
	str beta = MakeProject("Beta");
	Build();

	Click(Row("Beta"));
	EXPECT_FALSE(_hub->OnCommand(PanelCommand::Duplicate));
	EXPECT_FALSE(_hub->OnCommand(PanelCommand::Rename));
	ASSERT_EQ(_hub->GetEntries().size(), 2u);

	EXPECT_TRUE(_hub->OnCommand(PanelCommand::Delete));
	ASSERT_EQ(_hub->GetEntries().size(), 1u);
	EXPECT_TRUE(ProjectOps::PathsEqual(_hub->GetEntries()[0].path, alpha));
	EXPECT_TRUE(fs::exists(fs::path(beta) / "Project.ini"));
}

TEST_F(HubListTest, CtrlClickGrowsEnterOpens) {
	str alpha = MakeProject("Alpha");
	str beta = MakeProject("Beta");
	Build();

	Click(Row("Alpha"));
	Click(Row("Beta"), KeyMod::Ctrl);
	EXPECT_TRUE(IsLit(*_context, Row("Alpha")));
	EXPECT_TRUE(IsLit(*_context, Row("Beta")));
	EXPECT_EQ(_hub->GetSelectedPath(), beta);

	Press(Key::Enter);
	EXPECT_EQ(_hub->GetChosenPath(), beta);
}

TEST_F(HubListTest, LongListTakesLeftover) {
	MakeProject("Alpha");
	MakeProject("Beta");
	Build();
	UI::ElementHandle shortHeader = FirstWithText(*_context, _hubRoot, "NAME");
	ASSERT_TRUE(_context->IsAlive(shortHeader));
	f32 unsqueezed = _context->GetLayout(shortHeader).borderBox.h;
	ASSERT_GT(unsqueezed, 0.0f);

	for (u32 index = 0; index < 40; ++index) {
		MakeProject("Bulk" + std::to_string(index));
	}
	Build();

	UI::ElementHandle viewport = ScrollViewportUnder(*_context, _hubRoot);
	ASSERT_TRUE(_context->IsAlive(viewport));
	const UI::Element* scroller = _context->GetElement(viewport);
	ASSERT_NE(scroller, nullptr);
	ASSERT_GT(scroller->maxScroll.y, 0.0f);

	UI::ElementHandle longHeader = FirstWithText(*_context, _hubRoot, "NAME");
	ASSERT_TRUE(_context->IsAlive(longHeader));
	EXPECT_FLOAT_EQ(_context->GetLayout(longHeader).borderBox.h, unsqueezed);
}

TEST_F(HubListTest, BrokenEntryUnselectable) {
	MakeProject("Alpha");
	AddBrokenEntry("Broken");
	MakeProject("Zulu");
	Build();

	UI::ElementHandle broken = Row("BrokenTmp");
	ASSERT_TRUE(_context->IsAlive(broken));

	Click(broken);
	EXPECT_TRUE(_hub->GetSelectedPath().empty());

	Press(Key::Home);
	for (u32 step = 0; step < 6; ++step) {
		EXPECT_FALSE(IsLit(*_context, Row("BrokenTmp")));
		Press(Key::Down);
	}
}
