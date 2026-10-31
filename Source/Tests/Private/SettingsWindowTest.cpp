#include "TestsPCH.h"

#include "UITestUtil.h"

#include "EditorConfig.h"

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Reflection/Walk.h"
#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Core/Settings.h"
#include "ScreenSettings.h"

#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/PropertyRows.h"
#include "Shalltear/Editor/SettingsOps.h"
#include "Shalltear/Editor/SettingsPresentation.h"

#include <atomic>
#include <cctype>
#include <chrono>
#include <filesystem>

#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#undef CreateDirectory
#undef GetFileSize

using namespace Shalltear;
using namespace UITest;

namespace {

	namespace fs = std::filesystem;

	enum class Quality : u8 {
		Low = 0,
		High
	};

	void* ThisImageBase() {
		HMODULE image = nullptr;
		if (!::GetModuleHandleExW(
				GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
				reinterpret_cast<LPCWSTR>(&ThisImageBase),
				&image)) {
			return nullptr;
		}
		return image;
	}

} // namespace

SLT_REFLECT_ENUM(::Quality)
SLT_ENUM_VALUE(Low)
SLT_ENUM_VALUE(High)
SLT_END_ENUM()

namespace {

	struct WindowProbeSettings {
		SLT_REFLECT_VALUE(WindowProbeSettings)
		SLT_REFLECT_CONSTRUCTIBLE()
		SLT_REFLECT_FIELD(quality)
		SLT_REFLECT_FIELD(budget)
		SLT_REFLECT_FIELD(nickname)
		SLT_END_REFLECT()

		Quality quality = Quality::Low;
		i32 budget = 4;
		str nickname = "none";
	};

	struct WindowLockedSettings {
		SLT_REFLECT_VALUE(WindowLockedSettings)
		SLT_REFLECT_CONSTRUCTIBLE()
		SLT_REFLECT_FIELD(enabled)
		SLT_END_REFLECT()

		bool enabled = true;
	};

	struct WindowClassifiedSettings {
		SLT_REFLECT_VALUE(WindowClassifiedSettings)
		SLT_REFLECT_CONSTRUCTIBLE()
		SLT_REFLECT_FIELD(hot, SLT_MUTABILITY(::Shalltear::MutabilityClass::Hot))
		SLT_REFLECT_FIELD(nextFrame, SLT_MUTABILITY(::Shalltear::MutabilityClass::FrameBoundary))
		SLT_REFLECT_FIELD(gpuPause, SLT_MUTABILITY(::Shalltear::MutabilityClass::DeviceIdle))
		SLT_REFLECT_FIELD(onRestart, SLT_MUTABILITY(::Shalltear::MutabilityClass::Restart))
		SLT_END_REFLECT()

		i32 hot = 0;
		i32 nextFrame = 0;
		i32 gpuPause = 0;
		i32 onRestart = 0;
	};

	struct WindowReportingSettings {
		SLT_REFLECT_VALUE(WindowReportingSettings)
		SLT_REFLECT_CONSTRUCTIBLE()
		SLT_REFLECT_FIELD(requested)
		SLT_REFLECT_FIELD(obtained, SLT_READONLY, SLT_TRANSIENT)
		SLT_END_REFLECT()

		i32 requested = 10;
		str obtained;
	};

	class SettingsWindowFixture : public ::testing::Test {
	protected:
		fs::path _root;
		SettingsPaths _paths;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = fs::temp_directory_path() /
					("shalltear_settingswindow_" + std::to_string(stamp) + "_" + std::to_string(counter.fetch_add(1)));
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
			PanelManager::Terminate();
			SceneSystem::Terminate();
			SettingsPresentation::Terminate();
			Settings::Terminate();
			std::error_code ec;
			fs::remove_all(_root, ec);
		}

		void WriteDocument(const Path& path, const char* text) {
			FileSystem::WriteTextFile(path, str(text));
		}

		str ReadDocument(const Path& path) { return FileSystem::ReadTextFile(path); }

		void DeclareProbes(bool overridable = true) {
			ASSERT_NE(Settings::DeclareCategory<WindowProbeSettings>("Probe/Rendering", SettingScope::Project, overridable), nullptr);
			ASSERT_NE(Settings::DeclareCategory<WindowLockedSettings>("Probe/Modules", SettingScope::Project), nullptr);
		}

		void RegisterProbePresentation() {
			SettingsCategoryPresentation rendering;
			rendering.label = "Rendering";
			rendering.keywords = "pixels shading";
			rendering.order = 10;
			SettingsPresentation::RegisterCategory("Probe/Rendering", std::move(rendering));

			SettingsFieldPresentation quality;
			quality.label = "Detail Level";
			quality.keywords = "fidelity";
			SettingsPresentation::RegisterField("Probe/Rendering", "quality", std::move(quality));

			SettingsCategoryPresentation modules;
			modules.label = "Modules";
			modules.order = 20;
			SettingsPresentation::RegisterCategory("Probe/Modules", std::move(modules));
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

		static UI::ElementHandle TreeRowNamed(UI::Context& context, UI::ElementHandle root, std::string_view name) {
			Vector<UI::ElementHandle> rows{GetMemoryResource(MemTag::Editor)};
			CollectRows(context, root, rows);
			for (UI::ElementHandle row : rows) {
				if (Says(context, row, name)) {
					return row;
				}
			}
			return {};
		}

		static UI::ElementHandle FieldRowNamed(UI::Context& context, UI::ElementHandle from, std::string_view label) {
			const UI::Element* element = context.GetElement(from);
			for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = context.MakeHandle(child);
				if (context.GetTextString(handle) == label) {
					return from;
				}
				UI::ElementHandle found = FieldRowNamed(context, handle, label);
				if (found.IsValid()) {
					return found;
				}
				child = context.GetElement(handle)->nextSibling;
			}
			return {};
		}

		Handle<EditorPanel> OpenPanel() {
			return PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("SettingsPanel")));
		}

		static void Select(EditorPanel& panel, const char* path) {
			const FieldInfo* field = FindField(panel.GetType(), StringHash("selectedPath"));
			ASSERT_NE(field, nullptr);
			Variant value{str(path)};
			field->setFn(&panel, &value);
		}
	};

	struct Harness {
		MockFontSystem fonts;
		UI::Context context;
		UI::ElementHandle root;
		Handle<EditorPanel> panel;

		explicit Harness(Handle<EditorPanel> opened) : Harness(opened, MakeDesc()) {}

		Harness(Handle<EditorPanel> opened, const UI::ContextDesc& desc) : context(desc, &fonts), panel(opened) {
			EditorTheme::ApplyToContext(context);
			root = context.CreateElement();
			context.AppendChild(context.GetRoot(), root);
			panel.Get()->BuildContent(context, root);
			context.Update();
		}

		static UI::ContextDesc MakeDesc() {
			UI::ContextDesc desc;
			desc.sizePx = {1200, 800};
			return desc;
		}

		EditorPanel& Panel() { return *panel.Get(); }

		void Tick() {
			Panel().OnTick(0.016f);
			context.Update();
		}
	};

} // namespace

TEST_F(SettingsWindowFixture, EveryDeclaredCategoryIsReachableFromTree) {
	Settings::Initialize(_paths);
	DeclareProbes();
	RegisterProbePresentation();

	Vector<SettingsCategoryInfo> categories{GetMemoryResource(MemTag::Editor)};
	Settings::GetCategories(categories);
	ASSERT_FALSE(categories.empty());

	Vector<SettingsOps::TreeNode> nodes{GetMemoryResource(MemTag::Editor)};
	SettingsOps::BuildTree("", nodes);

	for (const SettingsCategoryInfo& info : categories) {
		bool present = false;
		for (const SettingsOps::TreeNode& node : nodes) {
			present |= node.path == info.path && node.declared;
		}
		EXPECT_TRUE(present) << info.path;
	}
}

TEST_F(SettingsWindowFixture, PathSegmentNobodyDeclaredBecomesGroupingRow) {
	Settings::Initialize(_paths);
	DeclareProbes();

	Vector<SettingsOps::TreeNode> nodes{GetMemoryResource(MemTag::Editor)};
	SettingsOps::BuildTree("", nodes);

	bool sawGroup = false;
	for (const SettingsOps::TreeNode& node : nodes) {
		if (node.path == "Probe") {
			sawGroup = true;
			EXPECT_FALSE(node.declared);
			EXPECT_EQ(node.depth, 0u);
		}
		if (node.path == "Probe/Rendering") {
			EXPECT_TRUE(node.declared);
			EXPECT_EQ(node.depth, 1u);
		}
	}
	EXPECT_TRUE(sawGroup);
}

TEST_F(SettingsWindowFixture, ChildSortsUnderParent) {
	Settings::Initialize(_paths);
	DeclareProbes();
	RegisterProbePresentation();
	ASSERT_NE(Settings::DeclareCategory<WindowLockedSettings>("Zebra", SettingScope::Project), nullptr);
	SettingsCategoryPresentation zebra;
	zebra.label = "Zebra";
	zebra.order = 15;
	SettingsPresentation::RegisterCategory("Zebra", std::move(zebra));

	Vector<SettingsOps::TreeNode> nodes{GetMemoryResource(MemTag::Editor)};
	SettingsOps::BuildTree("", nodes);

	auto indexOf = [&nodes](std::string_view path) -> i32 {
		for (size_t index = 0; index < nodes.size(); ++index) {
			if (nodes[index].path == path) {
				return static_cast<i32>(index);
			}
		}
		return -1;
	};
	const i32 group = indexOf("Probe");
	const i32 rendering = indexOf("Probe/Rendering");
	const i32 modules = indexOf("Probe/Modules");
	const i32 zebraIndex = indexOf("Zebra");
	ASSERT_GE(group, 0);
	ASSERT_GE(rendering, 0);
	ASSERT_GE(modules, 0);
	ASSERT_GE(zebraIndex, 0);

	EXPECT_LT(group, rendering);
	EXPECT_LT(rendering, modules);
	EXPECT_GT(zebraIndex, modules);
}

TEST_F(SettingsWindowFixture, SearchKeepsAncestorsNeededToReachMatch) {
	Settings::Initialize(_paths);
	DeclareProbes();
	RegisterProbePresentation();

	Vector<SettingsOps::TreeNode> nodes{GetMemoryResource(MemTag::Editor)};
	SettingsOps::BuildTree("Modules", nodes);

	bool sawGroup = false;
	bool sawModules = false;
	bool sawRendering = false;
	for (const SettingsOps::TreeNode& node : nodes) {
		sawGroup |= node.path == "Probe";
		sawModules |= node.path == "Probe/Modules";
		sawRendering |= node.path == "Probe/Rendering";
	}
	EXPECT_TRUE(sawModules);
	EXPECT_TRUE(sawGroup);
	EXPECT_FALSE(sawRendering);
}

TEST_F(SettingsWindowFixture, SearchMatchesFieldsAndKeywords) {
	Settings::Initialize(_paths);
	DeclareProbes();
	RegisterProbePresentation();

	EXPECT_TRUE(SettingsOps::CategoryMatches("detail", "Probe/Rendering"));
	EXPECT_TRUE(SettingsOps::CategoryMatches("fidelity", "Probe/Rendering"));
	EXPECT_TRUE(SettingsOps::CategoryMatches("nickname", "Probe/Rendering"));
	EXPECT_TRUE(SettingsOps::CategoryMatches("PIXELS", "Probe/Rendering"));
	EXPECT_FALSE(SettingsOps::CategoryMatches("nothinglikethis", "Probe/Rendering"));
}

TEST_F(SettingsWindowFixture, BadgeShowsDefault) {
	WriteDocument(_paths.user, "[Probe/Rendering]\nbudget = 9\n");
	Settings::Initialize(_paths);
	DeclareProbes();

	SettingFieldState stored;
	ASSERT_TRUE(Settings::GetFieldState("Probe/Rendering", "budget", stored));
	EXPECT_EQ(SettingsOps::SourceBadge(stored), SettingsOps::ScopeName(SettingScope::User));

	SettingFieldState untouched;
	ASSERT_TRUE(Settings::GetFieldState("Probe/Rendering", "nickname", untouched));
	for (SettingScope scope : {SettingScope::UserProject, SettingScope::User, SettingScope::Project}) {
		EXPECT_NE(SettingsOps::SourceBadge(untouched), SettingsOps::ScopeName(scope));
	}
	EXPECT_FALSE(SettingsOps::SourceBadge(untouched).empty());
}

TEST_F(SettingsWindowFixture, ScalarBuilderAcceptsWhatStoreCanPersist) {
	const TypeInfo* type = WindowProbeSettings::GetReflectType();
	ASSERT_NE(type, nullptr);
	for (u32 index = 0; index < type->fieldCount; ++index) {
		EXPECT_TRUE(PropertyRows::IsScalarField(type->fields[index])) << type->fields[index].displayName;
	}
}

TEST_F(SettingsWindowFixture, NumericWidthsRoundTrip) {
	struct Probe {
		SLT_REFLECT_VALUE(Probe)
		SLT_REFLECT_CONSTRUCTIBLE()
		SLT_REFLECT_FIELD(a)
		SLT_REFLECT_FIELD(b)
		SLT_REFLECT_FIELD(c)
		SLT_REFLECT_FIELD(d)
		SLT_REFLECT_FIELD(e)
		SLT_REFLECT_FIELD(f)
		SLT_REFLECT_FIELD(g)
		SLT_REFLECT_FIELD(h)
		SLT_REFLECT_FIELD(i)
		SLT_REFLECT_FIELD(j)
		SLT_END_REFLECT()

		i8 a = 0;
		i16 b = 0;
		i32 c = 0;
		i64 d = 0;
		u8 e = 0;
		u16 f = 0;
		u32 g = 0;
		u64 h = 0;
		f32 i = 0.0f;
		f64 j = 0.0;
	};

	Probe probe;
	const TypeInfo* type = Probe::GetReflectType();
	ASSERT_NE(type, nullptr);
	ASSERT_EQ(type->fieldCount, 10u);
	for (u32 index = 0; index < type->fieldCount; ++index) {
		const FieldInfo& field = type->fields[index];
		void* address = field.addrFn(&probe);
		ASSERT_TRUE(PropertyRows::WriteNumber(field.fieldType, address, 7.0)) << field.displayName;
		f64 read = 0.0;
		ASSERT_TRUE(PropertyRows::ReadNumber(field.fieldType, address, read)) << field.displayName;
		EXPECT_DOUBLE_EQ(read, 7.0) << field.displayName;
	}

	const FieldInfo* unsignedField = FindField(type, StringHash("g"));
	ASSERT_NE(unsignedField, nullptr);
	ASSERT_TRUE(PropertyRows::WriteNumber(unsignedField->fieldType, unsignedField->addrFn(&probe), -3.0));
	EXPECT_EQ(probe.g, 0u);
}

TEST_F(SettingsWindowFixture, SearchRowKeepsHeight) {
	Settings::Initialize(_paths);
	DeclareProbes();
	RegisterProbePresentation();

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Select(*panel.Get(), "Probe/Rendering");

	auto searchRowHeight = [&panel](i32 panelHeightPx) {
		MockFontSystem fonts;
		UI::ContextDesc desc;
		desc.sizePx = {600, panelHeightPx};
		UI::Context context(desc, &fonts);
		EditorTheme::ApplyToContext(context);
		UI::ElementHandle root = context.CreateElement();
		context.AppendChild(context.GetRoot(), root);
		panel.Get()->BuildContent(context, root);
		context.Update();
		return context.GetLayout(NthChildOf(context, root, 0)).borderBox.h;
	};

	f32 resting = searchRowHeight(800);
	ASSERT_GT(resting, 0.0f);
	EXPECT_FLOAT_EQ(searchRowHeight(90), resting);
}

TEST_F(SettingsWindowFixture, RowsShowSourceAndTarget) {
	WriteDocument(_paths.user, "[Probe/Rendering]\nbudget = 9\n");
	Settings::Initialize(_paths);
	DeclareProbes();
	RegisterProbePresentation();

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Select(*panel.Get(), "Probe/Rendering");
	Harness harness(panel);
	harness.Tick();

	SettingFieldState stored;
	ASSERT_TRUE(Settings::GetFieldState("Probe/Rendering", "budget", stored));
	EXPECT_TRUE(Says(harness.context, harness.root, SettingsOps::SourceBadge(stored)));

	SettingFieldState untouched;
	ASSERT_TRUE(Settings::GetFieldState("Probe/Rendering", "nickname", untouched));
	EXPECT_TRUE(Says(harness.context, harness.root, SettingsOps::SourceBadge(untouched)));

	EXPECT_NE(SettingsOps::SourceBadge(stored), SettingsOps::SourceBadge(untouched));

	EXPECT_TRUE(Says(harness.context, harness.root, "Detail Level"));
}

TEST_F(SettingsWindowFixture, RowSaysFieldsDocumentationOrNothing) {
	Settings::Initialize(_paths);
	DeclareProbes();
	RegisterProbePresentation();
	SettingsFieldPresentation budget;
	budget.label = "Budget Cap";
	budget.tooltip = "How many probes may run at once.";
	SettingsPresentation::RegisterField("Probe/Rendering", "budget", std::move(budget));
	SettingsFieldPresentation nickname;
	nickname.label = "Nickname";
	SettingsPresentation::RegisterField("Probe/Rendering", "nickname", std::move(nickname));

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Select(*panel.Get(), "Probe/Rendering");
	FontAsset* font = New<FontAsset>(MemTag::Asset);
	UI::ContextDesc desc = Harness::MakeDesc();
	desc.tooltipFont = Handle<FontAsset>(font);
	desc.tooltipDelaySeconds = 0.0f;
	Harness harness(panel, desc);
	harness.Tick();

	UI::ElementHandle documented = UITest::FindByText(harness.context, harness.root, "Budget Cap");
	UI::ElementHandle undocumented = UITest::FindByText(harness.context, harness.root, "Nickname");
	ASSERT_TRUE(harness.context.IsAlive(documented) && harness.context.IsAlive(undocumented));
	EXPECT_EQ(UITest::TooltipAt(harness.context, CenterOf(harness.context, documented)), "How many probes may run at once.");
	EXPECT_EQ(UITest::TooltipAt(harness.context, CenterOf(harness.context, undocumented)), "");
	Delete(font);
}

TEST_F(SettingsWindowFixture, FixedRowShowsOneScope) {
	Settings::Initialize(_paths);
	DeclareProbes();
	RegisterProbePresentation();

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Select(*panel.Get(), "Probe/Modules");
	Harness harness(panel);
	harness.Tick();

	EXPECT_TRUE(Says(harness.context, harness.root, SettingsOps::ScopeDescription(SettingScope::Project)));
	EXPECT_FALSE(Says(harness.context, harness.root, SettingsOps::ScopeDescription(SettingScope::UserProject)));
	EXPECT_FALSE(Says(harness.context, harness.root, SettingsOps::ScopePrecedenceHint()));
}

TEST_F(SettingsWindowFixture, RevertClickRestoresDefault) {
	WriteDocument(_paths.user, "[Probe/Rendering]\nnickname = mine\n");
	Settings::Initialize(_paths);
	DeclareProbes();
	RegisterProbePresentation();

	Vector<SettingsCategoryInfo> categories{GetMemoryResource(MemTag::Editor)};
	Settings::GetCategories(categories);
	ASSERT_FALSE(categories.empty());
	const WindowProbeSettings* live = nullptr;
	for (const SettingsCategoryInfo& info : categories) {
		if (info.path == "Probe/Rendering") {
			live = static_cast<const WindowProbeSettings*>(info.live);
		}
	}
	ASSERT_NE(live, nullptr);
	ASSERT_EQ(live->nickname, "mine");

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Select(*panel.Get(), "Probe/Rendering");
	Harness harness(panel);
	harness.Tick();

	UI::ElementHandle row = FieldRowNamed(harness.context, harness.root, "nickname");
	ASSERT_TRUE(row.IsValid());
	const UI::Element* element = harness.context.GetElement(row);
	ASSERT_NE(element, nullptr);
	UI::ElementHandle revert;
	for (u32 child = element->firstChild; child != UI::INVALID_UI_INDEX;) {
		revert = harness.context.MakeHandle(child);
		child = harness.context.GetElement(revert)->nextSibling;
	}
	ASSERT_TRUE(revert.IsValid());

	ClickAndTick(harness.context, harness.Panel(), CenterOf(harness.context, revert));

	EXPECT_EQ(live->nickname, "none");
	EXPECT_EQ(ReadDocument(_paths.user).find("nickname"), str::npos);
}

TEST_F(SettingsWindowFixture, EditWritesFileMovesBadge) {
	Settings::Initialize(_paths);
	DeclareProbes();
	RegisterProbePresentation();

	Vector<SettingsCategoryInfo> categories{GetMemoryResource(MemTag::Editor)};
	Settings::GetCategories(categories);
	WindowProbeSettings* live = nullptr;
	for (const SettingsCategoryInfo& info : categories) {
		if (info.path == "Probe/Rendering") {
			live = static_cast<WindowProbeSettings*>(info.live);
		}
	}
	ASSERT_NE(live, nullptr);

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Select(*panel.Get(), "Probe/Rendering");
	Harness harness(panel);
	harness.Tick();
	EXPECT_TRUE(Says(harness.context, harness.root, "Default"));

	live->budget = 11;
	ASSERT_TRUE(Settings::Save());
	harness.Tick();

	EXPECT_NE(ReadDocument(_paths.project).find("budget = 11"), str::npos);
	EXPECT_TRUE(Says(harness.context, harness.root, "Project"));
}

TEST_F(SettingsWindowFixture, ReportFieldNeverStored) {
	Settings::Initialize(_paths);
	WindowReportingSettings* live = Settings::DeclareCategory<WindowReportingSettings>("Probe/Reporting", SettingScope::Project);
	ASSERT_NE(live, nullptr);
	live->obtained = "9.8 ms from the probe";

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Select(*panel.Get(), "Probe/Reporting");
	Harness harness(panel);
	harness.Tick();

	UI::ElementHandle report = FieldRowNamed(harness.context, harness.root, "obtained");
	ASSERT_TRUE(report.IsValid());
	EXPECT_TRUE(Says(harness.context, report, "9.8 ms from the probe"));

	UI::ElementHandle request = FieldRowNamed(harness.context, harness.root, "requested");
	ASSERT_TRUE(request.IsValid());
	SettingFieldState untouched;
	ASSERT_TRUE(Settings::GetFieldState("Probe/Reporting", "requested", untouched));
	EXPECT_TRUE(Says(harness.context, request, SettingsOps::SourceBadge(untouched)));
	EXPECT_FALSE(Says(harness.context, report, SettingsOps::SourceBadge(untouched)));
	const UI::Element* reportElement = harness.context.GetElement(report);
	ASSERT_NE(reportElement, nullptr);
	u32 children = 0;
	for (u32 child = reportElement->firstChild; child != UI::INVALID_UI_INDEX; child = harness.context.GetElement(harness.context.MakeHandle(child))->nextSibling) {
		++children;
	}
	EXPECT_EQ(children, 2u);

	live->obtained = "21.3 ms from the probe";
	harness.Tick();
	EXPECT_TRUE(Says(harness.context, report, "21.3 ms from the probe"));
	EXPECT_FALSE(Says(harness.context, report, "9.8 ms"));

	live->requested = 12;
	ASSERT_TRUE(Settings::Save());
	const str stored = ReadDocument(_paths.project);
	EXPECT_NE(stored.find("requested = 12"), str::npos);
	EXPECT_EQ(stored.find("obtained"), str::npos);
}

TEST_F(SettingsWindowFixture, SelectingTreeRowOpensThatCategorysPage) {
	Settings::Initialize(_paths);
	DeclareProbes();
	RegisterProbePresentation();

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Harness harness(panel);
	harness.Tick();

	UI::ElementHandle row = TreeRowNamed(harness.context, harness.root, "Rendering");
	ASSERT_TRUE(row.IsValid());
	ClickAndTick(harness.context, harness.Panel(), CenterOf(harness.context, row));

	EXPECT_TRUE(Says(harness.context, harness.root, "Detail Level"));
	EXPECT_TRUE(Says(harness.context, harness.root, "nickname"));
}

TEST_F(SettingsWindowFixture, UndeclaredCategorySaysSo) {
	Settings::Initialize(_paths);
	DeclareProbes();

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Select(*panel.Get(), "Probe/Gone");
	Harness harness(panel);
	harness.Tick();

	EXPECT_TRUE(Says(harness.context, harness.root, "no longer available"));
}

TEST_F(SettingsWindowFixture, SweptPageDisappears) {
	Settings::Initialize(_paths);
	DeclareProbes();
	RegisterProbePresentation();

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Select(*panel.Get(), "Probe/Rendering");
	Harness harness(panel);
	harness.Tick();
	ASSERT_TRUE(Says(harness.context, harness.root, "Detail Level"));

	ASSERT_EQ(Settings::UnregisterOwnedBy(ThisImageBase()), 2u);
	harness.Tick();

	EXPECT_FALSE(Says(harness.context, harness.root, "Detail Level"));
	EXPECT_FALSE(Says(harness.context, harness.root, "Rendering"));
	EXPECT_TRUE(Says(harness.context, harness.root, "no longer available"));

	harness.Tick();
	EXPECT_TRUE(Says(harness.context, harness.root, "no longer available"));
}

TEST_F(SettingsWindowFixture, RedeclaredCategoryReturns) {
	WriteDocument(_paths.project, "[Probe/Rendering]\nbudget = 17\n");
	Settings::Initialize(_paths);
	DeclareProbes();
	RegisterProbePresentation();

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Select(*panel.Get(), "Probe/Rendering");
	Harness harness(panel);
	harness.Tick();

	ASSERT_EQ(Settings::UnregisterOwnedBy(ThisImageBase()), 2u);
	harness.Tick();
	ASSERT_TRUE(Says(harness.context, harness.root, "no longer available"));

	DeclareProbes();
	harness.Tick();

	EXPECT_TRUE(Says(harness.context, harness.root, "Detail Level"));
	EXPECT_FALSE(Says(harness.context, harness.root, "no longer available"));

	SettingFieldState state;
	ASSERT_TRUE(Settings::GetFieldState("Probe/Rendering", "budget", state));
	EXPECT_TRUE(state.isOverride);
}

TEST(SettingsVocabularyTest, ScopesNamedAndExplained) {
	const SettingScope scopes[] = {SettingScope::UserProject, SettingScope::User, SettingScope::Project};
	for (SettingScope scope : scopes) {
		EXPECT_FALSE(SettingsOps::ScopeName(scope).empty());
		EXPECT_FALSE(SettingsOps::ScopeDescription(scope).empty());
		EXPECT_NE(SettingsOps::ScopeName(scope), SettingsOps::ScopeDescription(scope));
	}
	for (SettingScope a : scopes) {
		for (SettingScope b : scopes) {
			if (a == b) {
				continue;
			}
			EXPECT_NE(SettingsOps::ScopeName(a), SettingsOps::ScopeName(b));
			EXPECT_NE(SettingsOps::ScopeDescription(a), SettingsOps::ScopeDescription(b));
		}
	}
}

TEST(SettingsVocabularyTest, SourceBadgeExplainsDefaultAndOverrideDifferently) {
	SettingFieldState untouched;
	untouched.isOverride = false;
	SettingFieldState overridden;
	overridden.isOverride = true;
	overridden.resolvedFrom = SettingScope::User;

	EXPECT_FALSE(SettingsOps::SourceBadgeDescription(untouched).empty());
	EXPECT_NE(SettingsOps::SourceBadgeDescription(untouched), SettingsOps::SourceBadgeDescription(overridden));
	EXPECT_EQ(SettingsOps::SourceBadgeDescription(overridden), SettingsOps::ScopeDescription(SettingScope::User));
}

TEST(SettingsVocabularyTest, PrecedenceHintNamesEveryScopeItOrders) {
	std::string_view hint = SettingsOps::ScopePrecedenceHint();
	ASSERT_FALSE(hint.empty());
	for (SettingScope scope : {SettingScope::UserProject, SettingScope::User, SettingScope::Project}) {
		str lowered;
		for (char c : SettingsOps::ScopeName(scope)) {
			lowered += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		str hintLowered;
		for (char c : hint) {
			hintLowered += static_cast<char>(std::tolower(static_cast<unsigned char>(c)));
		}
		EXPECT_NE(hintLowered.find(lowered), str::npos) << SettingsOps::ScopeName(scope);
	}
}

TEST(SettingsVocabularyTest, PagesSayIfGameReads) {
	SettingsPresentation::RegisterBuiltIns();

	std::string_view graphics = SettingsOps::AudienceLine("Graphics");
	EXPECT_FALSE(graphics.empty());

	for (const char* path : {"Editor/Scripting", "Editor/Build", "Editor/Module"}) {
		std::string_view line = SettingsOps::AudienceLine(path);
		EXPECT_FALSE(line.empty()) << path;
		EXPECT_NE(line, graphics);
	}

	std::string_view player = SettingsOps::AudienceLine("Player");
	EXPECT_FALSE(player.empty());
	EXPECT_NE(player, graphics);
	EXPECT_NE(player, SettingsOps::AudienceLine("Editor/Build"));
	EXPECT_NE(SettingsOps::RestartLine("Player"), SettingsOps::RestartLine("Graphics"));

	EXPECT_TRUE(SettingsOps::AudienceLine("Nobody/Registered/This").empty());
}

TEST_F(SettingsWindowFixture, StartupPageSaysSo) {
	Settings::Initialize(_paths);
	DeclareProbes();
	ASSERT_NE(Settings::DeclareCategory<WindowClassifiedSettings>("Probe/Classified", SettingScope::Project), nullptr);

	Vector<SettingsCategoryInfo> categories{GetMemoryResource(MemTag::Editor)};
	Settings::GetCategories(categories);
	auto find = [&categories](std::string_view path) -> const SettingsCategoryInfo* {
		for (const SettingsCategoryInfo& info : categories) {
			if (info.path == path) {
				return &info;
			}
		}
		return nullptr;
	};

	const SettingsCategoryInfo* probe = find("Probe/Rendering");
	ASSERT_NE(probe, nullptr);
	EXPECT_FALSE(SettingsOps::AnyFieldTakesEffectOnRestart(*probe));

	const SettingsCategoryInfo* classified = find("Probe/Classified");
	ASSERT_NE(classified, nullptr);
	EXPECT_TRUE(SettingsOps::AnyFieldTakesEffectOnRestart(*classified));
}

TEST_F(SettingsWindowFixture, EveryMutabilityClassHasBadgeAndSentence) {
	const TypeInfo* type = WindowClassifiedSettings::GetReflectType();
	ASSERT_NE(type, nullptr);

	auto fieldNamed = [type](std::string_view name) -> const FieldInfo* {
		for (u32 index = 0; index < type->fieldCount; ++index) {
			if (type->fields[index].displayName && name == type->fields[index].displayName) {
				return &type->fields[index];
			}
		}
		return nullptr;
	};

	struct Expected {
		const char* field;
		MutabilityClass mutability;
	};
	for (const Expected& expected : {Expected{"hot", MutabilityClass::Hot},
									 Expected{"nextFrame", MutabilityClass::FrameBoundary},
									 Expected{"gpuPause", MutabilityClass::DeviceIdle},
									 Expected{"onRestart", MutabilityClass::Restart}}) {
		const FieldInfo* field = fieldNamed(expected.field);
		ASSERT_NE(field, nullptr) << expected.field;
		MutabilityClass read = MutabilityClass::Hot;
		EXPECT_TRUE(Settings::GetFieldMutability(field, read)) << expected.field;
		EXPECT_EQ(read, expected.mutability) << expected.field;
	}

	EXPECT_TRUE(SettingsOps::MutabilityBadge(MutabilityClass::Hot).empty());
	EXPECT_TRUE(SettingsOps::MutabilityLine(MutabilityClass::Hot).empty());
	std::string_view badges[3] = {SettingsOps::MutabilityBadge(MutabilityClass::FrameBoundary),
								  SettingsOps::MutabilityBadge(MutabilityClass::DeviceIdle),
								  SettingsOps::MutabilityBadge(MutabilityClass::Restart)};
	std::string_view lines[3] = {SettingsOps::MutabilityLine(MutabilityClass::FrameBoundary),
								 SettingsOps::MutabilityLine(MutabilityClass::DeviceIdle),
								 SettingsOps::MutabilityLine(MutabilityClass::Restart)};
	for (u32 index = 0; index < 3; ++index) {
		EXPECT_FALSE(badges[index].empty());
		EXPECT_FALSE(lines[index].empty());
		for (u32 other = index + 1; other < 3; ++other) {
			EXPECT_NE(badges[index], badges[other]);
			EXPECT_NE(lines[index], lines[other]);
		}
	}

	const FieldInfo* unclassified = nullptr;
	const TypeInfo* plain = WindowProbeSettings::GetReflectType();
	ASSERT_NE(plain, nullptr);
	ASSERT_GT(plain->fieldCount, 0u);
	unclassified = &plain->fields[0];
	MutabilityClass ignored = MutabilityClass::Hot;
	EXPECT_FALSE(Settings::GetFieldMutability(unclassified, ignored));
	EXPECT_TRUE(SettingsOps::FieldMutabilityBadge(*unclassified).empty());
	EXPECT_TRUE(SettingsOps::FieldMutabilityLine(*unclassified).empty());
}

TEST_F(SettingsWindowFixture, FrameThrottlingShowsOnlyUnderCustom) {
	Settings::Initialize(_paths);
	auto* live = Settings::DeclareCategory<EditorConfig::GeneralPreferences>("Editor/General", SettingScope::User);
	ASSERT_NE(live, nullptr);
	SettingsPresentation::RegisterBuiltIns();

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Select(*panel.Get(), "Editor/General");
	Harness harness(panel);
	harness.Tick();
	auto shows = [&harness](std::string_view label) { return FieldRowNamed(harness.context, harness.root, label).IsValid(); };

	ASSERT_TRUE(shows("Interaction Mode"));
	EXPECT_FALSE(shows("Frame Throttling (ms)"));

	live->interactionMode = InteractionMode::Custom;
	harness.Tick();
	EXPECT_TRUE(shows("Frame Throttling (ms)"));

	live->interactionMode = InteractionMode::MonitorRefreshRate;
	harness.Tick();
	EXPECT_FALSE(shows("Frame Throttling (ms)"));
	EXPECT_TRUE(shows("Interaction Mode"));
}

TEST_F(SettingsWindowFixture, RowShowsWhileAnyOfClausesHolds) {
	Settings::Initialize(_paths);
	DeclareProbes();
	SettingsFieldPresentation nickname;
	nickname.label = "Nickname";
	nickname.shownWhen.push_back({"quality", static_cast<i64>(Quality::High)});
	nickname.shownWhen.push_back({"budget", 4, false});
	SettingsPresentation::RegisterField("Probe/Rendering", "nickname", std::move(nickname));

	Vector<SettingsCategoryInfo> categories{GetMemoryResource(MemTag::Editor)};
	Settings::GetCategories(categories);
	const SettingsCategoryInfo* rendering = nullptr;
	for (const SettingsCategoryInfo& info : categories) {
		if (info.path == "Probe/Rendering") {
			rendering = &info;
		}
	}
	ASSERT_NE(rendering, nullptr);
	auto* live = static_cast<WindowProbeSettings*>(rendering->live);
	const FieldInfo* field = FindField(rendering->type, StringHash("nickname"));
	ASSERT_NE(field, nullptr);

	live->quality = Quality::Low;
	live->budget = 4;
	EXPECT_FALSE(SettingsOps::IsFieldShown(*rendering, *field));
	live->quality = Quality::High;
	EXPECT_TRUE(SettingsOps::IsFieldShown(*rendering, *field));
	live->quality = Quality::Low;
	live->budget = 8;
	EXPECT_TRUE(SettingsOps::IsFieldShown(*rendering, *field));
}

TEST_F(SettingsWindowFixture, ThePlayerPageShowsTheResolutionRows) {
	Settings::Initialize(_paths);
	PlayerSettings* player = Settings::DeclareCategory<PlayerSettings>("Player", SettingScope::Project);
	ASSERT_NE(player, nullptr);
	SettingsPresentation::RegisterBuiltIns();
	Vector<SettingsCategoryInfo> categories{GetMemoryResource(MemTag::Editor)};
	Settings::GetCategories(categories);
	const SettingsCategoryInfo* page = nullptr;
	for (const SettingsCategoryInfo& info : categories) {
		if (info.path == "Player") {
			page = &info;
		}
	}
	ASSERT_NE(page, nullptr);
	auto shown = [&](const char* name) {
		const FieldInfo* field = FindField(page->type, StringHash(name));
		return field && SettingsOps::IsFieldShown(*page, *field);
	};

	player->fullScreenMode = FullScreenMode::Windowed;
	EXPECT_FALSE(shown("defaultIsNativeResolution"));
	EXPECT_TRUE(shown("defaultScreenWidth"));
	EXPECT_TRUE(shown("defaultScreenHeight"));

	player->fullScreenMode = FullScreenMode::FullScreenWindow;
	player->defaultIsNativeResolution = true;
	EXPECT_TRUE(shown("defaultIsNativeResolution"));
	EXPECT_FALSE(shown("defaultScreenWidth"));

	player->defaultIsNativeResolution = false;
	EXPECT_TRUE(shown("defaultScreenWidth"));
	EXPECT_TRUE(shown("allowFullscreenSwitch"));
}

TEST_F(SettingsWindowFixture, UnboundConditionStaysShown) {
	Settings::Initialize(_paths);
	DeclareProbes();
	RegisterProbePresentation();
	SettingsFieldPresentation budget;
	budget.label = "Budget";
	budget.shownWhen.push_back({"noSuchField", 1});
	SettingsPresentation::RegisterField("Probe/Rendering", "budget", std::move(budget));

	Handle<EditorPanel> panel = OpenPanel();
	ASSERT_TRUE(panel.IsValid());
	Select(*panel.Get(), "Probe/Rendering");
	Harness harness(panel);
	harness.Tick();
	EXPECT_TRUE(FieldRowNamed(harness.context, harness.root, "Detail Level").IsValid());
	EXPECT_TRUE(FieldRowNamed(harness.context, harness.root, "Budget").IsValid());
}
