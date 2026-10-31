#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Config.h"
#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Core/FileSystem.h"

#include "Shalltear/Editor/BuildPipeline.h"
#include "Shalltear/Editor/BuildProfile.h"
#include "Shalltear/Editor/CookOps.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/ProjectOps.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;
using namespace UITest;

namespace {

	namespace fs = std::filesystem;

	class BuildReportPanelFixture : public ::testing::Test {
	protected:
		fs::path _root;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = fs::temp_directory_path() /
					("shalltear_reportpanel_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			fs::create_directories(_root);
			Config projectFile;
			projectFile.SetString("Project", "name", "ReportProbe");
			projectFile.Save(Path(_root.generic_string() + "/Project.ini"));

			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			SceneSystem::Initialize();
			PanelManager::Initialize();
			ASSERT_TRUE(ProjectOps::MountProject(_root.generic_string()));
		}

		void TearDown() override {
			PanelManager::Terminate();
			SceneSystem::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			fs::remove_all(_root, ec);
		}

		str Root() const { return _root.generic_string(); }

		void SaveProfile(const str& name) const {
			BuildProfile profile;
			profile.name = name;
			ASSERT_TRUE(BuildProfileOps::SaveProfile(BuildProfileOps::GetProfilePath(Root(), name), profile));
		}

		static CookOps::CookReport MakeReport() {
			CookOps::CookReport report;
			report.profileName = "Windows";
			report.packagePath = "Builds/Windows/Data/Content.slpak";
			report.fileBytes = 413696;
			report.tocBytes = 3081;
			report.payloadBytes = 405504;
			report.regionCount = 2;
			report.pageCount = 3;
			report.candidateCount = 5;
			report.compression = static_cast<u8>(CookOps::CookCompression::None);
			report.debugNamesShipped = true;

			auto add = [&report](u64 low, const char* name, const char* type, u64 stored) {
				CookOps::CookReportAsset line;
				line.guid = Guid(1, low);
				line.name = name;
				line.typeName = type;
				line.descriptorBytes = 64;
				line.payloadRaw = stored;
				line.payloadStored = stored;
				report.assets.push_back(std::move(line));
			};
			add(1, "project://Assets/Boot.scene", "SceneAsset", 0);
			add(2, "project://Assets/Rock.mat", "MaterialAsset", 1024);
			add(3, "project://Assets/Rock.png", "TextureAsset", 300000);

			report.assets[0].isRoot = true;
			report.assets[0].dependsOn.push_back(1);
			report.assets[1].dependsOn.push_back(2);
			report.assets[1].pulledInBy.push_back(0);
			report.assets[2].pulledInBy.push_back(1);
			report.assets[1].rootPath.push_back(0);
			report.assets[2].rootPath.push_back(0);
			report.assets[2].rootPath.push_back(1);
			return report;
		}

		void WriteReport(const str& profile, const CookOps::CookReport& report) const {
			str path = BuildPipeline::GetCookReportPath(Root(), profile);
			std::error_code ec;
			fs::create_directories(fs::path{path}.parent_path(), ec);
			ASSERT_TRUE(FileSystem::WriteTextFile(Path(path), CookOps::WriteReportJSON(report)));
		}

		static void SelectProfile(EditorPanel& panel, const char* profile) {
			const FieldInfo* field = FindField(panel.GetType(), StringHash("selectedProfile"));
			ASSERT_NE(field, nullptr);
			Variant value{str(profile)};
			field->setFn(&panel, &value);
			panel.OnOpen();
		}

		Handle<EditorPanel> OpenPanel() {
			return PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("BuildReportPanel")));
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

		static bool Says(UI::Context& context, UI::ElementHandle from, std::string_view fragment) {
			Vector<str> texts{GetMemoryResource(MemTag::Editor)};
			CollectTexts(context, from, texts);
			for (const str& text : texts) {
				if (text.find(fragment) != str::npos) {
					return true;
				}
			}
			return false;
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
	};

	struct Harness {
		MockFontSystem fonts;
		UI::Context context;
		UI::ElementHandle root;
		Handle<EditorPanel> panel;

		explicit Harness(Handle<EditorPanel> opened) : context(MakeDesc(), &fonts), panel(opened) {
			EditorTheme::ApplyToContext(context);
			root = context.CreateElement();
			context.AppendChild(context.GetRoot(), root);
			panel.Get()->BuildContent(context, root);
			context.Update();
		}

		static UI::ContextDesc MakeDesc() {
			UI::ContextDesc desc;
			desc.sizePx = {1000, 800};
			return desc;
		}

		EditorPanel& Panel() { return *panel.Get(); }
	};

} // anonymous namespace

TEST_F(BuildReportPanelFixture, UnbuiltProfileSaysSo) {
	SaveProfile("Windows");
	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness(panel);

	EXPECT_TRUE(Says(harness.context, harness.root, "hasn't been built"));
	EXPECT_FALSE(Says(harness.context, harness.root, "format"));
}

TEST_F(BuildReportPanelFixture, HeadlineShowsExactBytes) {
	SaveProfile("Windows");
	WriteReport("Windows", MakeReport());

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness(panel);

	EXPECT_TRUE(Says(harness.context, harness.root, "413696 B on disk"));
	EXPECT_TRUE(Says(harness.context, harness.root, "3081 B resident"));
	EXPECT_TRUE(Says(harness.context, harness.root, "405504 B"));
	EXPECT_TRUE(Says(harness.context, harness.root, "3 asset(s) cooked"));

	EXPECT_TRUE(Says(harness.context, harness.root, "2 not reached"));
	EXPECT_TRUE(Says(harness.context, harness.root, "compression: none"));
}

TEST_F(BuildReportPanelFixture, RowsLargestFirstWithTypeAndCost) {
	SaveProfile("Windows");
	WriteReport("Windows", MakeReport());

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness(panel);

	Vector<UI::ElementHandle> rows{GetMemoryResource(MemTag::Editor)};
	CollectRows(harness.context, harness.root, rows);
	ASSERT_EQ(rows.size(), 3u);

	EXPECT_TRUE(SubtreeSays(harness.context, rows[0], "project://Assets/Rock.png"));
	EXPECT_TRUE(SubtreeSays(harness.context, rows[0], "TextureAsset"));
	EXPECT_TRUE(SubtreeSays(harness.context, rows[0], "300064 B"));
	EXPECT_TRUE(SubtreeSays(harness.context, rows[2], "project://Assets/Boot.scene"));
}

TEST_F(BuildReportPanelFixture, RowClickExplainsInclusion) {
	SaveProfile("Windows");
	WriteReport("Windows", MakeReport());

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness(panel);

	UI::ElementHandle row = RowNamed(harness.context, harness.root, "project://Assets/Rock.png");
	ASSERT_TRUE(row.IsValid());
	EXPECT_FALSE(Says(harness.context, harness.root, "->"));

	ClickAndTick(harness.context, harness.Panel(), CenterOf(harness.context, row));

	EXPECT_TRUE(Says(harness.context, harness.root,
					 "project://Assets/Boot.scene  ->  project://Assets/Rock.mat  ->  project://Assets/Rock.png"));
	EXPECT_TRUE(Says(harness.context, harness.root, "referenced by 1"));

	UI::ElementHandle again = RowNamed(harness.context, harness.root, "project://Assets/Rock.png");
	ASSERT_TRUE(again.IsValid());
	ClickAndTick(harness.context, harness.Panel(), CenterOf(harness.context, again));
	EXPECT_FALSE(Says(harness.context, harness.root, "referenced by 1"));
}

TEST_F(BuildReportPanelFixture, RootSaysItsRoot) {
	SaveProfile("Windows");
	WriteReport("Windows", MakeReport());

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness(panel);

	UI::ElementHandle row = RowNamed(harness.context, harness.root, "project://Assets/Boot.scene");
	ASSERT_TRUE(row.IsValid());
	ClickAndTick(harness.context, harness.Panel(), CenterOf(harness.context, row));

	EXPECT_TRUE(Says(harness.context, harness.root, "a root"));
}

TEST_F(BuildReportPanelFixture, PanelsKeepSeparateSelections) {
	SaveProfile("Windows");
	SaveProfile("Demo");
	WriteReport("Windows", MakeReport());
	CookOps::CookReport demo = MakeReport();
	demo.fileBytes = 777;
	demo.assets.clear();
	WriteReport("Demo", demo);

	Handle<EditorPanel> first = OpenPanel();
	Handle<EditorPanel> second = OpenPanel();
	ASSERT_TRUE(first.IsValid());
	ASSERT_TRUE(second.IsValid());
	ASSERT_NE(first.Get(), second.Get());

	SelectProfile(*second.Get(), "Windows");
	SelectProfile(*first.Get(), "Demo");

	Harness one(first);
	Harness two(second);
	EXPECT_TRUE(Says(one.context, one.root, "777 B on disk"));
	EXPECT_TRUE(Says(two.context, two.root, "413696 B on disk"));
}

TEST_F(BuildReportPanelFixture, MalformedReportIsReportedNotRendered) {
	SaveProfile("Windows");
	str path = BuildPipeline::GetCookReportPath(Root(), "Windows");
	std::error_code ec;
	fs::create_directories(fs::path{path}.parent_path(), ec);
	ASSERT_TRUE(FileSystem::WriteTextFile(Path(path), str(R"({"formatVersion": 42, "assets": []})")));

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness(panel);

	EXPECT_TRUE(Says(harness.context, harness.root, "format 42"));
	Vector<UI::ElementHandle> rows{GetMemoryResource(MemTag::Editor)};
	CollectRows(harness.context, harness.root, rows);
	EXPECT_TRUE(rows.empty());
}
