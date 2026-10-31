#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Settings.h"

#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PackageOps.h"
#include "Shalltear/Editor/PanelManager.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;
using namespace UITest;

namespace {

	constexpr const char* TOOL_ID = "dev.shalltear.importpipeline";
	constexpr const char* SAMPLE_ID = "dev.shalltear.samplepackage";
	constexpr const char* DEPENDENT_ID = "dev.shalltear.sampledependent";

	class PackageManagerPanelFixture : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		SettingsPaths _paths;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_packagepanel_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
			std::filesystem::create_directories(_root);
			_paths.project = Path((_root / "ProjectSettings.ini").generic_string());
			_paths.user = Path((_root / "UserSettings.ini").generic_string());
			_paths.userProject = Path((_root / "UserProjectSettings.ini").generic_string());
			Settings::Initialize(_paths);
			PackageOps::DeclareSettings();
			PackageOps::WriteEnabledSet({});

			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			PanelManager::Initialize();
		}

		void TearDown() override {
			PanelManager::Terminate();
			Settings::Terminate();
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		static Handle<EditorPanel> OpenPanel() {
			return PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("PackageManagerPanel")));
		}

		static bool HasTestPackages() {
			Vector<PackageOps::PackageManifest> catalogue{GetMemoryResource(MemTag::Editor)};
			Vector<str> diagnostics{GetMemoryResource(MemTag::Editor)};
			PackageOps::LoadBuiltInCatalogue(catalogue, diagnostics);
			return PackageOps::FindPackage(catalogue, SAMPLE_ID) && PackageOps::FindPackage(catalogue, DEPENDENT_ID);
		}

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

		static void CollectWithClass(UI::Context& context, UI::ElementHandle from, StringHash klass,
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

		static UI::ElementHandle RowNamed(UI::Context& context, UI::ElementHandle root, std::string_view display) {
			Vector<UI::ElementHandle> rows{GetMemoryResource(MemTag::Editor)};
			CollectWithClass(context, root, StringHash("slt-tree-row"), rows);
			for (UI::ElementHandle row : rows) {
				if (SaysSomethingContaining(context, row, display)) {
					return row;
				}
			}
			return {};
		}

		static UI::ElementHandle ToggleIn(UI::Context& context, UI::ElementHandle row) {
			Vector<UI::ElementHandle> boxes{GetMemoryResource(MemTag::Editor)};
			CollectWithClass(context, row, StringHash("slt-checkbox"), boxes);
			return boxes.empty() ? UI::ElementHandle{} : boxes.front();
		}

		static void ClickAndTick(UI::Context& context, EditorPanel& panel, UI::ElementHandle element) {
			ClickElement(context, element);
			context.Update();
			panel.OnTick(0.016f);
			context.Update();
		}

		static str StoredSet() {
			Vector<str> stored{GetMemoryResource(MemTag::Editor)};
			PackageOps::ReadEnabledSet(stored);
			str out;
			for (const str& name : stored) {
				if (!out.empty()) {
					out += ",";
				}
				out += name;
			}
			return out;
		}
	};

} // anonymous namespace

TEST_F(PackageManagerPanelFixture, CarriedToolHasRow) {
	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness{panel};

	Vector<UI::ElementHandle> rows{GetMemoryResource(MemTag::Editor)};
	CollectWithClass(harness.context, harness.root, StringHash("slt-tree-row"), rows);
	EXPECT_FALSE(rows.empty());

	EXPECT_TRUE(RowNamed(harness.context, harness.root, "Import Pipeline").IsValid());
	EXPECT_TRUE(SaysSomethingContaining(harness.context, harness.root, "Built-in"));
}

TEST_F(PackageManagerPanelFixture, ClickingRowsToggleWritesProjectsEnabledSet) {
	if (!HasTestPackages()) {
		GTEST_FAIL();
	}
	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness{panel};
	ASSERT_EQ(StoredSet(), "");

	UI::ElementHandle row = RowNamed(harness.context, harness.root, "Sample Package");
	ASSERT_TRUE(row.IsValid());
	UI::ElementHandle toggle = ToggleIn(harness.context, row);
	ASSERT_TRUE(harness.context.IsAlive(toggle));
	const UI::Rect box = harness.context.GetLayout(toggle).borderBox;
	ASSERT_GT(box.w, 0.0f);
	ASSERT_GT(box.h, 0.0f);

	ClickAndTick(harness.context, harness.Panel(), toggle);
	EXPECT_EQ(StoredSet(), SAMPLE_ID);

	UI::ElementHandle again = ToggleIn(harness.context, RowNamed(harness.context, harness.root, "Sample Package"));
	ASSERT_TRUE(harness.context.IsAlive(again));
	ClickAndTick(harness.context, harness.Panel(), again);
	EXPECT_EQ(StoredSet(), "");
}

TEST_F(PackageManagerPanelFixture, WhatToggleWroteIsInCommittedDocument) {
	if (!HasTestPackages()) {
		GTEST_FAIL() << "this tree staged no test packages";
	}
	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness{panel};

	UI::ElementHandle toggle = ToggleIn(harness.context, RowNamed(harness.context, harness.root, "Sample Package"));
	ASSERT_TRUE(harness.context.IsAlive(toggle));
	ClickAndTick(harness.context, harness.Panel(), toggle);

	str project = FileSystem::ReadTextFile(_paths.project);
	EXPECT_NE(project.find(SAMPLE_ID), str::npos) << project;
	EXPECT_EQ(FileSystem::ReadTextFile(_paths.user).find(SAMPLE_ID), str::npos);
}

TEST_F(PackageManagerPanelFixture, EnablingDependentDrawsDependencyAsEnabledToo) {
	if (!HasTestPackages()) {
		GTEST_FAIL() << "this tree staged no test packages";
	}
	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness{panel};

	UI::ElementHandle toggle = ToggleIn(harness.context, RowNamed(harness.context, harness.root, "Sample Dependent"));
	ASSERT_TRUE(harness.context.IsAlive(toggle));
	ClickAndTick(harness.context, harness.Panel(), toggle);

	EXPECT_EQ(StoredSet(), DEPENDENT_ID);

	UI::ElementHandle dependency = RowNamed(harness.context, harness.root, "Sample Package");
	ASSERT_TRUE(dependency.IsValid());
	Vector<PackageOps::PackageManifest> catalogue{GetMemoryResource(MemTag::Editor)};
	Vector<str> diagnostics{GetMemoryResource(MemTag::Editor)};
	PackageOps::LoadBuiltInCatalogue(catalogue, diagnostics);
	Vector<str> stored{GetMemoryResource(MemTag::Editor)};
	PackageOps::ReadEnabledSet(stored);
	Vector<PackageOps::ResolvedPackage> resolution{GetMemoryResource(MemTag::Editor)};
	PackageOps::ResolveEnabledSet(catalogue, stored, resolution);
	EXPECT_TRUE(PackageOps::IsEnabled(resolution, SAMPLE_ID));
}

TEST_F(PackageManagerPanelFixture, DependencyToggleIsDead) {
	if (!HasTestPackages()) {
		GTEST_FAIL() << "this tree staged no test packages";
	}
	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness{panel};

	UI::ElementHandle dependent = ToggleIn(harness.context, RowNamed(harness.context, harness.root, "Sample Dependent"));
	ASSERT_TRUE(harness.context.IsAlive(dependent));
	ClickAndTick(harness.context, harness.Panel(), dependent);
	ASSERT_EQ(StoredSet(), DEPENDENT_ID);

	UI::ElementHandle held = ToggleIn(harness.context, RowNamed(harness.context, harness.root, "Sample Package"));
	ASSERT_TRUE(harness.context.IsAlive(held));
	EXPECT_TRUE(harness.context.HasClass(held, EditorTheme::Class::Disabled));

	ClickAndTick(harness.context, harness.Panel(), held);
	EXPECT_EQ(StoredSet(), DEPENDENT_ID);

	UI::ElementHandle release = ToggleIn(harness.context, RowNamed(harness.context, harness.root, "Sample Dependent"));
	ASSERT_TRUE(harness.context.IsAlive(release));
	ClickAndTick(harness.context, harness.Panel(), release);
	ASSERT_EQ(StoredSet(), "");
	UI::ElementHandle freed = ToggleIn(harness.context, RowNamed(harness.context, harness.root, "Sample Package"));
	ASSERT_TRUE(harness.context.IsAlive(freed));
	EXPECT_FALSE(harness.context.HasClass(freed, EditorTheme::Class::Disabled));
	ClickAndTick(harness.context, harness.Panel(), freed);
	EXPECT_EQ(StoredSet(), SAMPLE_ID);
}

TEST_F(PackageManagerPanelFixture, ToolToggleDeadWithReason) {
	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness{panel};

	UI::ElementHandle row = RowNamed(harness.context, harness.root, "Import Pipeline");
	ASSERT_TRUE(row.IsValid());
	UI::ElementHandle toggle = ToggleIn(harness.context, row);
	ASSERT_TRUE(harness.context.IsAlive(toggle));
	EXPECT_TRUE(harness.context.HasClass(toggle, EditorTheme::Class::Disabled));

	ClickAndTick(harness.context, harness.Panel(), toggle);
	EXPECT_EQ(StoredSet(), "");

	EXPECT_TRUE(SaysSomethingContaining(harness.context, harness.root, "Always enabled in the editor"));
}

TEST_F(PackageManagerPanelFixture, RowClickShowsDetail) {
	if (!HasTestPackages()) {
		GTEST_FAIL() << "this tree staged no test packages";
	}
	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness{panel};

	Vector<PackageOps::PackageManifest> catalogue;
	Vector<str> diagnostics;
	PackageOps::LoadBuiltInCatalogue(catalogue, diagnostics);
	Vector<str> stored;
	PackageOps::ReadEnabledSet(stored);
	Vector<PackageOps::ResolvedPackage> resolution;
	PackageOps::ResolveEnabledSet(catalogue, stored, resolution);
	Vector<PackageOps::PackageRow> rows;
	PackageOps::BuildPackageRows(catalogue, resolution, rows);
	ASSERT_FALSE(rows.empty());
	EXPECT_TRUE(SaysSomethingContaining(harness.context, harness.root, rows.front().name));

	UI::ElementHandle row = RowNamed(harness.context, harness.root, "Sample Dependent");
	ASSERT_TRUE(row.IsValid());
	ClickAndTick(harness.context, harness.Panel(), row);

	EXPECT_TRUE(SaysSomethingContaining(harness.context, harness.root, DEPENDENT_ID));
	EXPECT_TRUE(SaysSomethingContaining(harness.context, harness.root, "Dependencies"));
}

TEST_F(PackageManagerPanelFixture, PanelsShareEnabledSet) {
	if (!HasTestPackages()) {
		GTEST_FAIL() << "this tree staged no test packages";
	}
	Handle<EditorPanel> first = OpenPanel();
	Handle<EditorPanel> second = OpenPanel();
	ASSERT_TRUE(first.IsValid());
	ASSERT_TRUE(second.IsValid());
	ASSERT_NE(first.Get(), second.Get());

	Harness one{first};
	Harness two{second};

	UI::ElementHandle row = RowNamed(one.context, one.root, "Sample Dependent");
	ASSERT_TRUE(row.IsValid());
	ClickAndTick(one.context, one.Panel(), row);
	EXPECT_TRUE(SaysSomethingContaining(one.context, one.root, DEPENDENT_ID));

	UI::ElementHandle toggle = ToggleIn(one.context, RowNamed(one.context, one.root, "Sample Package"));
	ASSERT_TRUE(one.context.IsAlive(toggle));
	ClickAndTick(one.context, one.Panel(), toggle);
	ASSERT_EQ(StoredSet(), SAMPLE_ID);

	two.Panel().OnTick(0.016f);
	two.context.Update();
	UI::ElementHandle mirrored = ToggleIn(two.context, RowNamed(two.context, two.root, "Sample Package"));
	ASSERT_TRUE(two.context.IsAlive(mirrored));
	ClickAndTick(two.context, two.Panel(), mirrored);
	EXPECT_EQ(StoredSet(), "");
}
