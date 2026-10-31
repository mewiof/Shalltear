#include "TestsPCH.h"

#include "Shalltear/Editor/EditorIcons.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Config.h"
#include "Shalltear/Core/Scene.h"

#include "Shalltear/Editor/BuildPipeline.h"
#include "Shalltear/Editor/BuildProfile.h"
#include "Shalltear/Editor/BuildTarget.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/ProjectOps.h"
#include "Shalltear/Editor/SceneAnnotations.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <fstream>

using namespace Shalltear;
using namespace UITest;

namespace {

	namespace fs = std::filesystem;

	class BuildProfilesPanelFixture : public ::testing::Test {
	protected:
		fs::path _root;
		Handle<Scene> _scene;
		BuildPipeline _pipeline;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = fs::temp_directory_path() /
					("shalltear_buildpanel_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			fs::create_directories(_root / "Assets" / "Scenes");
			Config projectFile;
			projectFile.SetString("Project", "name", "PanelProbe");
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
			_scene = SceneSystem::CreateScene("Fixture");
		}

		void TearDown() override {
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

		void WriteScene(const str& relative) const {
			fs::path full = _root / relative;
			fs::create_directories(full.parent_path());
			std::ofstream file(full, std::ios::binary);
			file << "{}\n";
		}

		void SaveProfile(const str& name, const BuildProfile& profile) const {
			ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), name), profile));
		}

		BuildProfile Load(const str& name) const {
			BuildProfile profile;
			BuildProfileOps::LoadProfile(BuildProfileOps::GetProfilePath(Root(), name), profile);
			return profile;
		}

		Handle<EditorPanel> OpenPanel() {
			return PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("BuildProfilesPanel")));
		}

		static Vec2 CenterOf(UI::Context& context, UI::ElementHandle handle) {
			const UI::Rect& box = context.GetLayout(handle).borderBox;
			return {box.x + box.w * 0.5f, box.y + box.h * 0.5f};
		}

		static InputEvent Mouse(InputEventType type, Vec2 point) {
			InputEvent event{};
			event.type = type;
			event.button = MouseButton::Left;
			event.mousePosPx = {static_cast<i32>(point.x), static_cast<i32>(point.y)};
			return event;
		}

		static void ClickAndTick(UI::Context& context, EditorPanel& panel, Vec2 point) {
			context.InjectInput(Mouse(InputEventType::MouseMove, point));
			context.InjectInput(Mouse(InputEventType::MouseDown, point));
			context.InjectInput(Mouse(InputEventType::MouseUp, point));
			context.Update();
			panel.OnTick(0.016f);
			context.Update();
		}

		static void CollectRows(UI::Context& context, UI::ElementHandle from, Vector<UI::ElementHandle>& out) {
			if (context.HasClass(from, StringHash("slt-tree-row"))) {
				out.push_back(from);
			}
			const UI::Element* element = context.GetElement(from);
			for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = context.MakeHandle(child);
				CollectRows(context, handle, out);
				child = context.GetElement(handle)->nextSibling;
			}
		}

		static bool SubtreeSays(UI::Context& context, UI::ElementHandle from, std::string_view text) {
			if (context.GetTextString(from) == text) {
				return true;
			}
			const UI::Element* element = context.GetElement(from);
			for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = context.MakeHandle(child);
				if (SubtreeSays(context, handle, text)) {
					return true;
				}
				child = context.GetElement(handle)->nextSibling;
			}
			return false;
		}

		static UI::ElementHandle RowNamed(UI::Context& context, UI::ElementHandle root, std::string_view name) {
			Vector<UI::ElementHandle> rows{GetMemoryResource(MemTag::Editor)};
			CollectRows(context, root, rows);
			for (UI::ElementHandle row : rows) {
				if (SubtreeSays(context, row, name)) {
					return row;
				}
			}
			return {};
		}

		static void CollectTexts(UI::Context& context, UI::ElementHandle from, Vector<str>& out) {
			str text = context.GetTextString(from);
			if (!text.empty()) {
				out.push_back(std::move(text));
			}
			const UI::Element* element = context.GetElement(from);
			for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = context.MakeHandle(child);
				CollectTexts(context, handle, out);
				child = context.GetElement(handle)->nextSibling;
			}
		}

		static bool SaysSomethingContaining(UI::Context& context, UI::ElementHandle from, std::string_view fragment) {
			Vector<str> texts{GetMemoryResource(MemTag::Editor)};
			CollectTexts(context, from, texts);
			for (const str& text : texts) {
				if (text.find(fragment) != str::npos) {
					return true;
				}
			}
			return false;
		}

		static UI::ElementHandle ButtonSaying(UI::Context& context, UI::ElementHandle from, std::string_view label) {
			if (context.HasClass(from, StringHash("slt-button")) && SubtreeSays(context, from, label)) {
				return from;
			}
			const UI::Element* element = context.GetElement(from);
			for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = context.MakeHandle(child);
				UI::ElementHandle found = ButtonSaying(context, handle, label);
				if (found.IsValid()) {
					return found;
				}
				child = context.GetElement(handle)->nextSibling;
			}
			return {};
		}
	};

	struct Harness {
		MockFontSystem fonts;
		UI::Context context;
		UI::ElementHandle root;
		Handle<EditorPanel> panel;

		explicit Harness(Handle<EditorPanel> opened)
			: context(MakeDesc(), &fonts), panel(opened) {
			EditorTheme::ApplyToContext(context);
			root = context.CreateElement();
			context.AppendChild(context.GetRoot(), root);
			panel.Get()->BuildContent(context, root);
			context.Update();
		}

		static UI::ContextDesc MakeDesc() {
			UI::ContextDesc desc;
			desc.sizePx = {900, 700};
			return desc;
		}

		EditorPanel& Panel() { return *panel.Get(); }
	};

} // anonymous namespace

TEST_F(BuildProfilesPanelFixture, RowsMirrorRosterAndRebuildingIsIdempotent) {
	SaveProfile("Default", BuildProfile{});
	BuildProfile demo;
	demo.inherits = "Default";
	SaveProfile("Demo", demo);

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness(panel);

	EXPECT_TRUE(RowNamed(harness.context, harness.root, "Default").IsValid());
	EXPECT_TRUE(RowNamed(harness.context, harness.root, "Demo").IsValid());

	UI::ElementHandle second = harness.context.CreateElement();
	harness.context.AppendChild(harness.context.GetRoot(), second);
	panel.Get()->BuildContent(harness.context, second);
	harness.context.Update();
	Vector<UI::ElementHandle> rows{GetMemoryResource(MemTag::Editor)};
	CollectRows(harness.context, second, rows);
	EXPECT_EQ(rows.size(), 2u);
}

TEST_F(BuildProfilesPanelFixture, RowClickSelectsAndShowsSettings) {
	BuildProfile base;
	base.SetString("executableName", "BaseGame");
	SaveProfile("Default", base);
	BuildProfile demo;
	demo.inherits = "Default";
	demo.SetString("executableName", "DemoGame");
	SaveProfile("Demo", demo);

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness(panel);

	EXPECT_TRUE(SaysSomethingContaining(harness.context, harness.root, "BaseGame"));

	UI::ElementHandle demoRow = RowNamed(harness.context, harness.root, "Demo");
	ASSERT_TRUE(demoRow.IsValid());
	ClickAndTick(harness.context, harness.Panel(), CenterOf(harness.context, demoRow));

	EXPECT_TRUE(SaysSomethingContaining(harness.context, harness.root, "DemoGame"));
	UI::ElementHandle selected = RowNamed(harness.context, harness.root, "Demo");
	ASSERT_TRUE(selected.IsValid());
	EXPECT_TRUE(harness.context.HasClass(selected, StringHash("slt-selected")));
}

TEST_F(BuildProfilesPanelFixture, EyeExcludesProfileFromBuildAllPlan) {
	SaveProfile("Default", BuildProfile{});
	SaveProfile("Demo", BuildProfile{});

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness(panel);

	Vector<str> before{GetMemoryResource(MemTag::Editor)};
	BuildProfileOps::ListBuildAllProfiles(Root(), before);
	ASSERT_EQ(before.size(), 2u);

	UI::ElementHandle demoRow = RowNamed(harness.context, harness.root, "Demo");
	ASSERT_TRUE(demoRow.IsValid());
	UI::ElementHandle eye = NthChildOf(harness.context, demoRow, 0);
	ASSERT_TRUE(eye.IsValid());
	ClickAndTick(harness.context, harness.Panel(), CenterOf(harness.context, eye));

	EXPECT_EQ(Load("Demo").GetString("includeInBuildAll"), "false");
	Vector<str> after{GetMemoryResource(MemTag::Editor)};
	BuildProfileOps::ListBuildAllProfiles(Root(), after);
	ASSERT_EQ(after.size(), 1u);
	EXPECT_EQ(after[0], "Default");

	UI::ElementHandle again = RowNamed(harness.context, harness.root, "Demo");
	ASSERT_TRUE(again.IsValid());
	ClickAndTick(harness.context, harness.Panel(), CenterOf(harness.context, NthChildOf(harness.context, again, 0)));
	EXPECT_EQ(Load("Demo").GetString("includeInBuildAll"), "true");
}

TEST_F(BuildProfilesPanelFixture, InheritedFieldsNameSource) {
	BuildProfile base;
	base.SetString("executableName", "BaseGame");
	base.SetString("version", "1.0");
	SaveProfile("Default", base);
	BuildProfile demo;
	demo.inherits = "Default";
	demo.SetString("executableName", "DemoGame");
	SaveProfile("Demo", demo);

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness(panel);
	UI::ElementHandle demoRow = RowNamed(harness.context, harness.root, "Demo");
	ASSERT_TRUE(demoRow.IsValid());
	ClickAndTick(harness.context, harness.Panel(), CenterOf(harness.context, demoRow));

	EXPECT_TRUE(SaysSomethingContaining(harness.context, harness.root, "from Default"));
	UI::ElementHandle revert = ButtonSaying(harness.context, harness.root, "Revert");
	ASSERT_TRUE(revert.IsValid());

	ASSERT_TRUE(Load("Demo").Has("executableName"));
	ClickAndTick(harness.context, harness.Panel(), CenterOf(harness.context, revert));
	EXPECT_FALSE(Load("Demo").Has("executableName"));
	EXPECT_TRUE(SaysSomethingContaining(harness.context, harness.root, "BaseGame"));
}

TEST_F(BuildProfilesPanelFixture, UnionListSeparatesInherited) {
	BuildProfile base;
	base.SetList("defines", Vector<str>{"TELEMETRY"});
	SaveProfile("Default", base);
	BuildProfile demo;
	demo.inherits = "Default";
	demo.SetList("defines", Vector<str>{"DEMO"});
	SaveProfile("Demo", demo);

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness(panel);
	ClickAndTick(harness.context, harness.Panel(),
				 CenterOf(harness.context, RowNamed(harness.context, harness.root, "Demo")));

	EXPECT_TRUE(SaysSomethingContaining(harness.context, harness.root, "TELEMETRY (Default)"));
	Vector<str> texts{GetMemoryResource(MemTag::Editor)};
	CollectTexts(harness.context, harness.root, texts);
	bool editableHoldsOnlyOwn = false;
	for (const str& text : texts) {
		editableHoldsOnlyOwn = editableHoldsOnlyOwn || text == "DEMO";
	}
	EXPECT_TRUE(editableHoldsOnlyOwn);
	for (const str& text : texts) {
		EXPECT_NE(text, "TELEMETRY;DEMO");
		EXPECT_NE(text, "DEMO;TELEMETRY");
	}
}

TEST_F(BuildProfilesPanelFixture, PreviewShowsResolvedVariable) {
	BuildProfile base;
	base.SetString("version", "1.0.${ENV:SHALLTEAR_PANEL_PROBE_UNSET}");
	SaveProfile("Default", base);

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness(panel);

	EXPECT_TRUE(SaysSomethingContaining(harness.context, harness.root, "${ENV:SHALLTEAR_PANEL_PROBE_UNSET}"));
	EXPECT_TRUE(SaysSomethingContaining(harness.context, harness.root, "-> not resolved"));
	EXPECT_TRUE(SaysSomethingContaining(harness.context, harness.root, "is not set"));
}

TEST_F(BuildProfilesPanelFixture, ExpandedValuePreviewShowsResolvedVariable) {
#if defined(_WIN32)
	::_putenv_s("SHALLTEAR_PANEL_PROBE_SET", "77");
#endif
	BuildProfile base;
	base.SetString("version", "1.0.${ENV:SHALLTEAR_PANEL_PROBE_SET}");
	SaveProfile("Default", base);

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness(panel);

	EXPECT_TRUE(SaysSomethingContaining(harness.context, harness.root, "1.0.${ENV:SHALLTEAR_PANEL_PROBE_SET}"));
	EXPECT_TRUE(SaysSomethingContaining(harness.context, harness.root, "-> 1.0.77"));
#if defined(_WIN32)
	::_putenv_s("SHALLTEAR_PANEL_PROBE_SET", "");
#endif
}

TEST_F(BuildProfilesPanelFixture, ValidationShownBeforeBuild) {
	BuildProfile broken;
	broken.SetList("scenes", Vector<str>{"Assets/Scenes/Gone.scene"});
	SaveProfile("Default", broken);

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness(panel);

	EXPECT_TRUE(SaysSomethingContaining(harness.context, harness.root, "is not a file in this project"));
	UI::ElementHandle row = RowNamed(harness.context, harness.root, "Default");
	ASSERT_TRUE(row.IsValid());
	EXPECT_TRUE(SaysSomethingContaining(harness.context, row, "err"));
}

TEST_F(BuildProfilesPanelFixture, LastResultReadFromLibrary) {
	SaveProfile("Default", BuildProfile{});
	fs::create_directories(_root / "Library");
	Config results;
	results.SetString("Default", "result", "Succeeded");
	results.SetString("Default", "outputPath", Root() + "/Builds/Default");
	results.SetI64("Default", "buildNumber", 48);
	results.SetF64("Default", "durationSeconds", 2.5);
	results.SetI64("Default", "artifactBytes", 22723451);
	ASSERT_TRUE(results.Save(Path(Root() + "/Library/BuildResults.ini")));

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness(panel);

	EXPECT_TRUE(SaysSomethingContaining(harness.context, harness.root, "Succeeded"));
	EXPECT_TRUE(SaysSomethingContaining(harness.context, harness.root, "build 48"));
	EXPECT_TRUE(SaysSomethingContaining(harness.context, harness.root, "MiB"));
}

TEST_F(BuildProfilesPanelFixture, BuildNamesModifiedSceneFirst) {
	WriteScene("Assets/Scenes/Main.scene");
	SaveProfile("Default", BuildProfile{});

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness(panel);

	EditorSceneOps::SaveScene(_scene, Path("project://Assets/Scenes/Main.scene"));
	EditorSceneOps::MarkDirty(_scene);
	ASSERT_TRUE(EditorSceneOps::AnyDirty());

	UI::ElementHandle build = ButtonSaying(harness.context, harness.root, "Build");
	ASSERT_TRUE(build.IsValid());
	ClickAndTick(harness.context, harness.Panel(), CenterOf(harness.context, build));

	UI::ElementHandle all = harness.context.GetRoot();
	EXPECT_TRUE(SaysSomethingContaining(harness.context, all, "scene 'Main'"));
	EXPECT_TRUE(SaysSomethingContaining(harness.context, all, "Main.scene"));
	EXPECT_TRUE(ButtonSaying(harness.context, all, "Save All and Build").IsValid());
	EXPECT_TRUE(ButtonSaying(harness.context, all, "Build Anyway").IsValid());
	EXPECT_TRUE(ButtonSaying(harness.context, all, "Cancel").IsValid());

	EXPECT_FALSE(_pipeline.IsBusy());
	EXPECT_EQ(_pipeline.GetState(), BuildState::Idle);
}

TEST_F(BuildProfilesPanelFixture, PrefabAndUnsavedSceneNamed) {
	SaveProfile("Default", BuildProfile{});
	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness(panel);

	EditorSceneOps::MarkDirty(_scene);
	UI::ElementHandle build = ButtonSaying(harness.context, harness.root, "Build");
	ASSERT_TRUE(build.IsValid());
	ClickAndTick(harness.context, harness.Panel(), CenterOf(harness.context, build));
	EXPECT_TRUE(SaysSomethingContaining(harness.context, harness.context.GetRoot(), "never saved"));
}

TEST_F(BuildProfilesPanelFixture, ProfilelessProjectOffersOne) {
	WriteScene("Assets/Scenes/Main.scene");

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness(panel);

	EXPECT_FALSE(fs::exists(_root / "BuildProfiles" / "Default.buildprofile"));
	UI::ElementHandle create = ButtonSaying(harness.context, harness.root, "Create Default Profile");
	ASSERT_TRUE(create.IsValid());

	ClickAndTick(harness.context, harness.Panel(), CenterOf(harness.context, create));

	EXPECT_TRUE(fs::exists(_root / "BuildProfiles" / "Default.buildprofile"));
	EXPECT_TRUE(RowNamed(harness.context, harness.root, "Default").IsValid());
	Vector<str> scenes;
	ASSERT_TRUE(Load("Default").GetList("scenes", scenes));
	ASSERT_EQ(scenes.size(), 1u);
	EXPECT_EQ(scenes[0], "Assets/Scenes/Main.scene");
}

TEST_F(BuildProfilesPanelFixture, MovingAProfileRewritesTheRosterOrder) {
	SaveProfile("Alpha", BuildProfile{});
	SaveProfile("Beta", BuildProfile{});
	ASSERT_TRUE(BuildProfileOps::WriteProfileRoster(Root(), Vector<str>{"Alpha", "Beta"}));

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness(panel);

	ClickAndTick(harness.context, harness.Panel(),
				 CenterOf(harness.context, RowNamed(harness.context, harness.root, "Beta")));
	UI::ElementHandle up = UITest::ControlWearing(
		harness.context, harness.root, EditorIcons::GetGlyph(EditorIcons::Glyph::MoveUp),
		StringHash("slt-button"));
	ASSERT_TRUE(up.IsValid());
	ClickAndTick(harness.context, harness.Panel(), CenterOf(harness.context, up));

	Vector<str> roster{GetMemoryResource(MemTag::Editor)};
	BuildProfileOps::ListProfiles(Root(), roster);
	ASSERT_EQ(roster.size(), 2u);
	EXPECT_EQ(roster[0], "Beta");
	EXPECT_EQ(roster[1], "Alpha");
}
