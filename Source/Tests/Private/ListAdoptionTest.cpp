#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Window/WindowSystem.h"

#include "Shalltear/Editor/AssetBrowserOps.h"
#include "Shalltear/Editor/EditorDragData.h"
#include "Shalltear/Editor/EditorInputOps.h"
#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/EditorPreviews.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/UIBuilderOps.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <atomic>
#include <chrono>
#include <filesystem>
#include <span>

using namespace Shalltear;
using namespace UITest;

namespace {

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

	UI::ElementHandle EntryLabelled(UI::Context& context, UI::ElementHandle from, std::string_view label) {
		UI::ElementHandle text = FirstWithText(context, from, label);
		for (UI::ElementHandle walk = text; context.IsAlive(walk);) {
			if (context.HasClass(walk, StringHash("slt-tree-row")) ||
				context.HasClass(walk, StringHash("ed-asset-cell"))) {
				return walk;
			}
			const UI::Element* element = context.GetElement(walk);
			if (!element || element->parent == UI::INVALID_UI_INDEX) {
				break;
			}
			walk = context.MakeHandle(element->parent);
		}
		return {};
	}

	bool IsLit(UI::Context& context, UI::ElementHandle entry) {
		return context.IsAlive(entry) && context.HasClass(entry, StringHash("slt-selected"));
	}

	Vec2 CenterOf(UI::Context& context, UI::ElementHandle handle) {
		const UI::Rect& box = context.GetLayout(handle).borderBox;
		return {box.x + box.w * 0.5f, box.y + box.h * 0.5f};
	}

	class AssetBrowserListTest : public ::testing::TestWithParam<f32> {
	protected:
		std::filesystem::path _root;
		MockFontSystem _fonts;
		std::unique_ptr<UI::Context> _context;
		UI::ElementHandle _panelRoot;
		Handle<EditorPanel> _panel;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_m4browser_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root / "Assets");
			FileSystem::Mount("project", Path(_root.generic_string()));

			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			AssetDatabase::Initialize();
			EditorSelection::Initialize();
			PanelManager::Initialize();
			UndoSystem::Initialize();
		}

		void TearDown() override {
			_context.reset();
			UndoSystem::Terminate();
			PanelManager::Terminate();
			EditorSelection::Terminate();
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		void Write(const str& virtualPath, const str& content) {
			Path resolved = FileSystem::Resolve(Path(virtualPath));
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(resolved.ToString()).parent_path(), ec);
			FileSystem::WriteTextFile(resolved, content);
		}

		void WriteProject() {
			Write("project://Assets/a.txt", "a");
			Write("project://Assets/b.txt", "b");
			Write("project://Assets/c.txt", "c");
			Write("project://Assets/d.txt", "d");
			Write("project://Assets/One/inner.txt", "i");
			Write("project://Assets/Two/other.txt", "o");
			AssetDatabase::ScanProjectDirectory();
		}

		void OpenPanel() {
			_panel = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("AssetBrowserPanel")));
			ASSERT_TRUE(_panel.IsValid());
			Variant boxed{GetParam()};
			ASSERT_TRUE(SetField(_panel.Get(), _panel.Get()->GetType(), StringHash("iconSize"), &boxed));

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

		UI::ElementHandle Entry(std::string_view label) { return EntryLabelled(*_context, _panelRoot, label); }

		void ClickEntry(std::string_view label, KeyMod mods = KeyMod::None) {
			UI::ElementHandle entry = Entry(label);
			ASSERT_TRUE(_context->IsAlive(entry)) << label;
			ClickAt(CenterOf(*_context, entry), mods);
		}

		void ClickAt(Vec2 point, KeyMod mods = KeyMod::None, MouseButton button = MouseButton::Left) {
			InputEvent event{};
			event.button = button;
			event.mousePosPx = {static_cast<i32>(point.x), static_cast<i32>(point.y)};
			event.mods = mods;
			event.type = InputEventType::MouseMove;
			_context->InjectInput(event);
			event.type = InputEventType::MouseDown;
			_context->InjectInput(event);
			event.type = InputEventType::MouseUp;
			_context->InjectInput(event);
			_context->Update();
			Tick();
		}

		void Key(Shalltear::Key key, KeyMod mods = KeyMod::None) {
			InputEvent event{};
			event.type = InputEventType::KeyDown;
			event.key = key;
			event.mods = mods;
			_context->InjectInput(event);
			_context->Update();
			Tick();
		}

		bool Command(PanelCommand command) {
			bool answered = _panel.Get()->OnCommand(command);
			_context->Update();
			Tick();
			return answered;
		}
	};

	INSTANTIATE_TEST_SUITE_P(BothLayoutModes, AssetBrowserListTest, ::testing::Values(0.0f, 0.6f),
							 [](const ::testing::TestParamInfo<f32>& info) {
								 return AssetBrowserOps::SliderIsGrid(info.param) ? "Grid" : "List";
							 });

} // namespace

TEST_P(AssetBrowserListTest, CtrlATakesDrawnFiles) {
	WriteProject();
	OpenPanel();

	ClickEntry("a.txt");
	ASSERT_EQ(EditorSelection::GetAssetSelectionCount(), 1u);

	ASSERT_TRUE(Command(PanelCommand::SelectAll));
	EXPECT_TRUE(IsLit(*_context, Entry("a.txt")));
	EXPECT_TRUE(IsLit(*_context, Entry("b.txt")));
	EXPECT_TRUE(IsLit(*_context, Entry("c.txt")));
	EXPECT_TRUE(IsLit(*_context, Entry("d.txt")));
	EXPECT_EQ(EditorSelection::GetAssetSelectionCount(), 4u);
	EXPECT_FALSE(IsLit(*_context, Entry("Two")));
}

TEST_P(AssetBrowserListTest, ArrowingFilesMovesSelectionAndPublishesIt) {
	WriteProject();
	OpenPanel();

	ClickEntry("a.txt");
	Key(Key::Down);
	EXPECT_TRUE(IsLit(*_context, Entry("b.txt")));
	EXPECT_FALSE(IsLit(*_context, Entry("a.txt")));
	EXPECT_EQ(EditorSelection::GetSelectedAsset(), AssetDatabase::LookupGuid(Path("project://Assets/b.txt")));

	Key(Key::Down, KeyMod::Shift);
	EXPECT_EQ(EditorSelection::GetAssetSelectionCount(), 2u);
	EXPECT_TRUE(IsLit(*_context, Entry("c.txt")));
	Key(Key::Down, KeyMod::Ctrl);
	EXPECT_EQ(EditorSelection::GetAssetSelectionCount(), 3u);
	EXPECT_TRUE(IsLit(*_context, Entry("d.txt")));
}

TEST_P(AssetBrowserListTest, DeleteReachesFilteredFile) {
	WriteProject();
	OpenPanel();

	ClickEntry("a.txt");
	ClickEntry("b.txt", KeyMod::Shift);
	ASSERT_EQ(EditorSelection::GetAssetSelectionCount(), 2u);

	UI::ElementHandle magnifier = UITest::ControlWearing(
		*_context, _panelRoot, EditorIcons::GetGlyph(EditorIcons::Glyph::Search),
		StringHash("slt-button"));
	ASSERT_TRUE(_context->IsAlive(magnifier));
	ClickAt(CenterOf(*_context, magnifier));
	InputEvent typed{};
	typed.type = InputEventType::Text;
	typed.codepoint = U'a';
	_context->InjectInput(typed);
	_context->Update();
	Tick();

	ASSERT_TRUE(_context->IsAlive(Entry("a.txt")));
	ASSERT_FALSE(_context->IsAlive(Entry("b.txt")));
	ASSERT_EQ(EditorSelection::GetAssetSelectionCount(), 2u);

	const UI::Rect& drawn = _context->GetLayout(Entry("a.txt")).borderBox;
	InputEvent press{};
	press.button = MouseButton::Left;
	press.mousePosPx = {static_cast<i32>(drawn.x + drawn.w * 0.5f), static_cast<i32>(drawn.y + drawn.h + 200.0f)};
	press.type = InputEventType::MouseMove;
	_context->InjectInput(press);
	press.type = InputEventType::MouseDown;
	_context->InjectInput(press);
	press.mousePosPx = {1, 1};
	press.type = InputEventType::MouseMove;
	_context->InjectInput(press);
	press.type = InputEventType::MouseUp;
	_context->InjectInput(press);
	_context->Update();
	Tick();
	ASSERT_EQ(EditorSelection::GetAssetSelectionCount(), 2u);

	ASSERT_TRUE(Command(PanelCommand::Delete));

	UI::ElementHandle confirm = FirstWithText(*_context, _context->GetOpenPopup(0), "Delete");
	ASSERT_TRUE(_context->IsAlive(confirm));
	ClickAt(CenterOf(*_context, confirm));

	EXPECT_FALSE(FileSystem::Exists(Path("project://Assets/a.txt")));
	EXPECT_FALSE(FileSystem::Exists(Path("project://Assets/b.txt")));
	EXPECT_TRUE(FileSystem::Exists(Path("project://Assets/c.txt")));
}

TEST_P(AssetBrowserListTest, DuplicateCopiesSelectedFiles) {
	WriteProject();
	OpenPanel();

	ClickEntry("a.txt");
	ClickEntry("b.txt", KeyMod::Ctrl);
	ASSERT_EQ(EditorSelection::GetAssetSelectionCount(), 2u);

	ASSERT_TRUE(Command(PanelCommand::Duplicate));
	u32 copies = 0;
	for (const auto& entry : std::filesystem::directory_iterator(_root / "Assets")) {
		str name = entry.path().filename().generic_string();
		if (name.find(".meta") == str::npos && (name.starts_with("a") || name.starts_with("b")) &&
			name != "a.txt" && name != "b.txt") {
			++copies;
		}
	}
	EXPECT_EQ(copies, 2u) << copies;
}

TEST_P(AssetBrowserListTest, RenameNeedsSingleFile) {
	WriteProject();
	OpenPanel();

	ClickEntry("a.txt");
	ASSERT_TRUE(Command(PanelCommand::Rename));
	ASSERT_EQ(_context->GetOpenPopupCount(), 1u);
	_context->CloseAllPopups();
	_context->Update();

	ClickEntry("a.txt");
	ClickEntry("b.txt", KeyMod::Ctrl);
	EXPECT_FALSE(Command(PanelCommand::Rename));
}

TEST_P(AssetBrowserListTest, CtrlAAnsweredByFocusedBrowser) {
	WriteProject();
	OpenPanel();

	Handle<EditorPanel> second = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("AssetBrowserPanel")));
	ASSERT_TRUE(second.IsValid());
	ASSERT_NE(second.GetInstanceId(), _panel.GetInstanceId());
	Variant boxed{GetParam()};
	ASSERT_TRUE(SetField(second.Get(), second.Get()->GetType(), StringHash("iconSize"), &boxed));

	UI::ContextDesc desc;
	desc.sizePx = {900, 700};
	MockFontSystem fonts;
	UI::Context other{desc, &fonts};
	EditorTheme::ApplyToContext(other);
	UI::ElementHandle otherRoot = other.CreateElement();
	other.AppendChild(other.GetRoot(), otherRoot);
	UI::Style fill;
	fill.width = UI::Length::Percent(1.0f);
	fill.height = UI::Length::Percent(1.0f);
	other.SetStyle(otherRoot, fill);
	second.Get()->BuildContent(other, otherRoot);
	other.Update();
	second.Get()->OnTick(0.016f);
	other.Update();

	ClickEntry("a.txt");
	ASSERT_TRUE(Command(PanelCommand::SelectAll));
	ASSERT_EQ(EditorSelection::GetAssetSelectionCount(), 4u);

	second.Get()->OnSelectionChanged();
	second.Get()->OnTick(0.016f);
	other.Update();
	EXPECT_TRUE(IsLit(other, EntryLabelled(other, otherRoot, "a.txt")));
	EXPECT_TRUE(IsLit(other, EntryLabelled(other, otherRoot, "b.txt")));
	EXPECT_TRUE(IsLit(other, EntryLabelled(other, otherRoot, "c.txt")));
	EXPECT_TRUE(IsLit(other, EntryLabelled(other, otherRoot, "d.txt")));
}

TEST_P(AssetBrowserListTest, TreeAddsFolderKeepsActive) {
	WriteProject();
	OpenPanel();

	ClickEntry("One");
	ASSERT_TRUE(_context->IsAlive(Entry("inner.txt")));
	UI::ElementHandle one = Entry("One");
	ASSERT_TRUE(IsLit(*_context, one));

	ClickEntry("Two", KeyMod::Ctrl);
	EXPECT_TRUE(IsLit(*_context, Entry("One")));
	EXPECT_TRUE(IsLit(*_context, Entry("Two")));
	EXPECT_TRUE(_context->IsAlive(Entry("other.txt")));
	EXPECT_FALSE(_context->IsAlive(Entry("inner.txt")));

	Variant stored;
	ASSERT_TRUE(GetField(_panel.Get(), _panel.Get()->GetType(), StringHash("selectedFolder"), &stored));
	EXPECT_EQ(stored.As<str>(), str("Assets/Two"));
}

TEST_P(AssetBrowserListTest, DeleteReachesFocusedList) {
	WriteProject();
	OpenPanel();

	ClickEntry("c.txt");
	ASSERT_TRUE(Command(PanelCommand::Delete));
	UI::ElementHandle confirm = FirstWithText(*_context, _context->GetOpenPopup(0), "Delete");
	ASSERT_TRUE(_context->IsAlive(confirm));
	ClickAt(CenterOf(*_context, confirm));
	EXPECT_FALSE(FileSystem::Exists(Path("project://Assets/c.txt")));
	EXPECT_TRUE(std::filesystem::exists(_root / "Assets" / "One"));

	ClickEntry("One");
	ASSERT_TRUE(Command(PanelCommand::Delete));
	UI::ElementHandle folderConfirm = FirstWithText(*_context, _context->GetOpenPopup(0), "Delete");
	ASSERT_TRUE(_context->IsAlive(folderConfirm));
	ClickAt(CenterOf(*_context, folderConfirm));
	EXPECT_FALSE(std::filesystem::exists(_root / "Assets" / "One"));
	EXPECT_TRUE(FileSystem::Exists(Path("project://Assets/d.txt")));
}

TEST_P(AssetBrowserListTest, ArrowAfterDelContinuesBesideDeletedFile) {
	WriteProject();
	OpenPanel();

	ClickEntry("b.txt");
	ASSERT_TRUE(Command(PanelCommand::Delete));
	ASSERT_EQ(_context->GetOpenPopupCount(), 1u);
	Key(Key::Enter);
	ASSERT_EQ(_context->GetOpenPopupCount(), 0u);
	ASSERT_FALSE(FileSystem::Exists(Path("project://Assets/b.txt")));
	ASSERT_EQ(EditorSelection::GetAssetSelectionCount(), 0u);

	Key(Key::Down);
	EXPECT_TRUE(IsLit(*_context, Entry("d.txt")));
	EXPECT_EQ(EditorSelection::GetSelectedAsset(), AssetDatabase::LookupGuid(Path("project://Assets/d.txt")));
}

TEST_P(AssetBrowserListTest, ArrowAfterClickedConfirmStillReachesList) {
	WriteProject();
	OpenPanel();

	ClickEntry("b.txt");
	ASSERT_TRUE(Command(PanelCommand::Delete));
	UI::ElementHandle confirm = FirstWithText(*_context, _context->GetOpenPopup(0), "Delete");
	ASSERT_TRUE(_context->IsAlive(confirm));
	ClickAt(CenterOf(*_context, confirm));
	ASSERT_EQ(_context->GetOpenPopupCount(), 0u);
	ASSERT_FALSE(FileSystem::Exists(Path("project://Assets/b.txt")));

	Key(Key::Down);
	EXPECT_TRUE(IsLit(*_context, Entry("d.txt")));
}

TEST_P(AssetBrowserListTest, DeletingContentRootIsRefusedNotConfirmed) {
	WriteProject();
	OpenPanel();

	ClickEntry("Assets");
	Command(PanelCommand::Delete);
	EXPECT_EQ(_context->GetOpenPopupCount(), 0u);
	EXPECT_TRUE(std::filesystem::exists(_root / "Assets"));
}

namespace {

	class UIBuilderListTest : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		bool _mountedEngine = false;
		Handle<Window> _window;
		Handle<EditorPanel> _panel;
		UI::ElementHandle _panelRoot;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			u64 unique = counter.fetch_add(1);
			u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_m4builder_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root / "Assets");
			FileSystem::Mount("project", Path(_root.generic_string()));

			std::error_code ec;
			std::filesystem::path probe = std::filesystem::current_path(ec);
			for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
				if (std::filesystem::exists(probe / "EngineAssets", ec)) {
					FileSystem::Mount("engine", Path((probe / "EngineAssets").generic_string()));
					_mountedEngine = true;
					break;
				}
				std::filesystem::path parent = probe.parent_path();
				if (parent == probe) {
					break;
				}
				probe = parent;
			}

			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			UISystem::Initialize();
			PanelManager::Initialize();
			UndoSystem::Initialize();

			WindowDesc windowDesc;
			windowDesc.title = "UIBuilderListTest";
			windowDesc.clientSize = {1400, 900};
			windowDesc.flags = WindowFlags::Hidden;
			_window = WindowSystem::Create(windowDesc);
			UISystem::CreateContext(_window);
		}

		void TearDown() override {
			UndoSystem::Terminate();
			PanelManager::Terminate();
			UISystem::Terminate();
			AssetDatabase::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			if (_mountedEngine) {
				FileSystem::Unmount("engine");
			}
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		UI::Context& Context() { return *UISystem::GetContext(_window); }

		void WriteDocument(const str& virtualPath, const str& body) {
			WriteRaw(virtualPath, str("<ui version=\"1\">") + body + str("</ui>"));
		}

		void WriteRaw(const str& virtualPath, const str& body) {
			Path resolved = FileSystem::Resolve(Path(virtualPath));
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(resolved.ToString()).parent_path(), ec);
			FileSystem::WriteTextFile(resolved, body);
			AssetDatabase::ScanProjectDirectory();
		}

		void OpenBuilder(const str& virtualPath) {
			_panel = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("UIBuilderPanel")));
			ASSERT_TRUE(_panel.IsValid());
			UI::Context& context = Context();
			_panelRoot = context.CreateElement();
			context.AppendChild(context.GetRoot(), _panelRoot);
			UI::Style fill;
			fill.width = UI::Length::Percent(1.0f);
			fill.height = UI::Length::Percent(1.0f);
			context.SetStyle(_panelRoot, fill);
			_panel.Get()->BuildContent(context, _panelRoot);
			UISystem::Update();
			ASSERT_TRUE(UIBuilderOps::OpenDocumentInBuilder(virtualPath));
			Frame();
		}

		void Frame() {
			_panel.Get()->OnTick(0.0f);
			UISystem::Update();
		}

		UI::ElementHandle Row(std::string_view label) { return EntryLabelled(Context(), _panelRoot, label); }

		void ClickRow(std::string_view label, KeyMod mods = KeyMod::None) {
			UI::ElementHandle row = Row(label);
			ASSERT_TRUE(Context().IsAlive(row)) << label;
			ClickAt(CenterOf(Context(), row), mods);
		}

		void ClickAt(Vec2 point, KeyMod mods = KeyMod::None, MouseButton button = MouseButton::Left) {
			UI::Context& context = Context();
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
			UISystem::Update();
			Frame();
		}

		void Key(Shalltear::Key key, KeyMod mods = KeyMod::None) {
			InputEvent event{};
			event.type = InputEventType::KeyDown;
			event.key = key;
			event.mods = mods;
			Context().InjectInput(event);
			UISystem::Update();
			Frame();
		}

		UI::DragPayload GrabRow(std::string_view label) {
			UI::Context& context = Context();
			UI::ElementHandle row = Row(label);
			EXPECT_TRUE(context.IsAlive(row)) << label;
			const Vec2 point = CenterOf(context, row);
			InputEvent event{};
			event.button = MouseButton::Left;
			event.mousePosPx = {static_cast<i32>(point.x), static_cast<i32>(point.y)};
			event.type = InputEventType::MouseMove;
			context.InjectInput(event);
			event.type = InputEventType::MouseDown;
			context.InjectInput(event);
			UISystem::Update();
			event.mousePosPx = {static_cast<i32>(point.x) + 12, static_cast<i32>(point.y) + 4};
			event.type = InputEventType::MouseMove;
			context.InjectInput(event);
			UISystem::Update();
			const UI::DragPayload* began = UISystem::GetDragPayload();
			const UI::DragPayload payload = began ? *began : UI::DragPayload{};
			UISystem::CancelDragPayload();
			event.type = InputEventType::MouseUp;
			context.InjectInput(event);
			UISystem::Update();
			Frame();
			return payload;
		}

		static std::span<const u32> Carried(const UI::DragPayload& payload) {
			if (payload.type != "UIBuilderNode"_hash || !payload.data.Is<UIBuilderNodeDragData>()) {
				return {};
			}
			const Vector<u32>& nodes = payload.data.As<UIBuilderNodeDragData>().nodes;
			return {nodes.data(), nodes.size()};
		}

		static str Shape(const UIDocumentAsset& document) {
			Vector<UIBuilderOps::Row> rows{GetMemoryResource(MemTag::Debug)};
			UIBuilderOps::BuildRows(document, rows);
			str shape;
			for (const UIBuilderOps::Row& row : rows) {
				shape += (shape.empty() ? "" : " ") + row.node->id + "@" + std::to_string(row.depth);
			}
			return shape;
		}

		bool Command(PanelCommand command) {
			bool answered = _panel.Get()->OnCommand(command);
			UISystem::Update();
			Frame();
			return answered;
		}

		UIDocumentAsset* Document(const str& virtualPath) {
			Asset* asset = AssetDatabase::LoadAsset(Path(virtualPath));
			return asset ? static_cast<UIDocumentAsset*>(asset) : nullptr;
		}

		static u32 CountElementsWithId(const UIDocumentAsset& document, std::string_view id) {
			Vector<UIBuilderOps::Row> rows{GetMemoryResource(MemTag::Debug)};
			UIBuilderOps::BuildRows(document, rows);
			u32 count = 0;
			for (const UIBuilderOps::Row& row : rows) {
				if (row.node->id == id) {
					++count;
				}
			}
			return count;
		}
	};

} // namespace

TEST_F(UIBuilderListTest, CtrlClickAddsSecondElementToSelection) {
	WriteDocument("project://Assets/Screen.slxml",
				  R"(<panel id="root"><panel id="a"/><panel id="b"/><panel id="c"/></panel>)");
	OpenBuilder("project://Assets/Screen.slxml");

	ClickRow("#a");
	ASSERT_TRUE(IsLit(Context(), Row("#a")));

	ClickRow("#c", KeyMod::Ctrl);
	EXPECT_TRUE(IsLit(Context(), Row("#a")));
	EXPECT_TRUE(IsLit(Context(), Row("#c")));
	EXPECT_FALSE(IsLit(Context(), Row("#b")));

	ClickRow("#b", KeyMod::Shift);
	EXPECT_TRUE(IsLit(Context(), Row("#b")));
	EXPECT_TRUE(IsLit(Context(), Row("#c")));
}

TEST_F(UIBuilderListTest, CtrlATakesDrawnRows) {
	WriteDocument("project://Assets/Screen.slxml", R"(<panel id="root"><panel id="a"/><panel id="b"/></panel>)");
	OpenBuilder("project://Assets/Screen.slxml");

	ClickRow("#a");
	ASSERT_TRUE(Command(PanelCommand::SelectAll));
	EXPECT_TRUE(IsLit(Context(), Row("#root")));
	EXPECT_TRUE(IsLit(Context(), Row("#a")));
	EXPECT_TRUE(IsLit(Context(), Row("#b")));
}

TEST_F(UIBuilderListTest, DuplicateCopiesSelectedElements) {
	WriteDocument("project://Assets/Screen.slxml", R"(<panel id="root"><panel id="a"/><panel id="b"/></panel>)");
	OpenBuilder("project://Assets/Screen.slxml");

	ClickRow("#a");
	ClickRow("#b", KeyMod::Ctrl);
	ASSERT_TRUE(Command(PanelCommand::Duplicate));

	UIDocumentAsset* document = Document("project://Assets/Screen.slxml");
	ASSERT_NE(document, nullptr);
	EXPECT_EQ(CountElementsWithId(*document, "a"), 2u);
	EXPECT_EQ(CountElementsWithId(*document, "b"), 2u);

	ASSERT_TRUE(UndoSystem::CanUndo());
	EXPECT_EQ(UndoSystem::GetUndoLabel(), "Duplicate 2 Elements");
	UndoSystem::Undo();
	EXPECT_EQ(CountElementsWithId(*document, "a"), 1u);
	EXPECT_EQ(CountElementsWithId(*document, "b"), 1u);
	AssetDatabase::UnloadAsset(document);
}

TEST_F(UIBuilderListTest, DeleteRemovesSelectedElements) {
	WriteDocument("project://Assets/Screen.slxml",
				  R"(<panel id="root"><panel id="a"/><panel id="b"/><panel id="keep"/></panel>)");
	OpenBuilder("project://Assets/Screen.slxml");

	ClickRow("#a");
	ClickRow("#b", KeyMod::Ctrl);
	ASSERT_TRUE(Command(PanelCommand::Delete));

	UIDocumentAsset* document = Document("project://Assets/Screen.slxml");
	ASSERT_NE(document, nullptr);
	EXPECT_EQ(CountElementsWithId(*document, "a"), 0u);
	EXPECT_EQ(CountElementsWithId(*document, "b"), 0u);
	EXPECT_EQ(CountElementsWithId(*document, "keep"), 1u);

	ASSERT_TRUE(UndoSystem::CanUndo());
	EXPECT_EQ(UndoSystem::GetUndoLabel(), "Delete 2 Elements");
	UndoSystem::Undo();
	EXPECT_EQ(CountElementsWithId(*document, "a"), 1u);
	EXPECT_EQ(CountElementsWithId(*document, "b"), 1u);
	AssetDatabase::UnloadAsset(document);
}

TEST_F(UIBuilderListTest, DraggingRowInsideSelectionMovesWholeRun) {
	WriteDocument("project://Assets/Screen.slxml",
				  R"(<panel id="root"><panel id="a"/><panel id="b"/><panel id="c"/><panel id="host"/></panel>)");
	OpenBuilder("project://Assets/Screen.slxml");
	UIDocumentAsset* document = Document("project://Assets/Screen.slxml");
	ASSERT_NE(document, nullptr);
	const str before = Shape(*document);

	ClickRow("#c");
	ClickRow("#a", KeyMod::Ctrl);
	const UI::DragPayload payload = GrabRow("#a");
	ASSERT_EQ(Carried(payload).size(), 2u);
	ASSERT_TRUE(Context().DeliverDrop(payload, CenterOf(Context(), Row("#host"))));
	Frame();

	EXPECT_EQ(Shape(*document), "root@0 b@1 host@1 a@2 c@2");
	EXPECT_EQ(UndoSystem::GetUndoLabel(), "Move 2 Elements");
	UndoSystem::Undo();
	EXPECT_EQ(Shape(*document), before);
	AssetDatabase::UnloadAsset(document);
}

TEST_F(UIBuilderListTest, RowGrabbedOutsideSelectionDragsOnlyItself) {
	WriteDocument("project://Assets/Screen.slxml",
				  R"(<panel id="root"><panel id="a"/><panel id="b"/><panel id="c"/></panel>)");
	OpenBuilder("project://Assets/Screen.slxml");

	ClickRow("#a");
	ClickRow("#b", KeyMod::Ctrl);
	const std::span<const u32> carried = Carried(GrabRow("#c"));
	ASSERT_EQ(carried.size(), 1u);
	EXPECT_EQ(carried[0], 3u);
}

TEST_F(UIBuilderListTest, DuplicateRowCarriesChordThatNowFires) {
	WriteDocument("project://Assets/Screen.slxml", R"(<panel id="root"><panel id="a"/></panel>)");
	OpenBuilder("project://Assets/Screen.slxml");

	UI::Context& context = Context();
	UI::ElementHandle row = Row("#a");
	ASSERT_TRUE(context.IsAlive(row));
	ClickAt(CenterOf(context, row), KeyMod::None, MouseButton::Right);

	ASSERT_GT(context.GetOpenPopupCount(), 0u);
	str expected = EditorInputOps::ChordLabel(EditorInputOps::Shortcut::DuplicateSelection);
	ASSERT_FALSE(expected.empty());
	EXPECT_TRUE(FirstWithText(context, context.GetOpenPopup(0), expected).IsValid());
}

TEST_F(UIBuilderListTest, RulesPaneRefusesHierarchyDelete) {
	FileSystem::WriteTextFile(Path("project://Assets/Screen.slss"),
							  ".one { width: 10px; }\n.two { width: 20px; }\n");
	WriteDocument("project://Assets/Screen.slxml",
				  R"(<sheet src="project://Assets/Screen.slss"/><panel id="root"><panel id="a"/></panel>)");
	OpenBuilder("project://Assets/Screen.slxml");

	ClickRow(".one");
	ASSERT_TRUE(IsLit(Context(), Row(".one")));
	EXPECT_FALSE(Command(PanelCommand::Delete));

	UIDocumentAsset* document = Document("project://Assets/Screen.slxml");
	ASSERT_NE(document, nullptr);
	EXPECT_EQ(CountElementsWithId(*document, "a"), 1u);

	EXPECT_TRUE(Command(PanelCommand::SelectAll));
	EXPECT_TRUE(IsLit(Context(), Row(".one")));
	EXPECT_TRUE(IsLit(Context(), Row(".two")));

	ClickRow("#a");
	EXPECT_TRUE(Command(PanelCommand::Delete));
	EXPECT_EQ(CountElementsWithId(*document, "a"), 0u);
	AssetDatabase::UnloadAsset(document);
}

TEST_F(UIBuilderListTest, SheetHeadingUnselectable) {
	FileSystem::WriteTextFile(Path("project://Assets/Screen.slss"),
							  ".one { width: 10px; }\n.two { width: 20px; }\n");
	WriteDocument("project://Assets/Screen.slxml",
				  R"(<sheet src="project://Assets/Screen.slss"/><panel id="root"><panel id="a"/></panel>)");
	OpenBuilder("project://Assets/Screen.slxml");

	UI::ElementHandle heading = Row("project://Assets/Screen.slss");
	ASSERT_TRUE(Context().IsAlive(heading));

	ClickRow(".two");
	ASSERT_TRUE(Command(PanelCommand::SelectAll));
	EXPECT_FALSE(IsLit(Context(), Row("project://Assets/Screen.slss")));

	ClickRow(".one");
	Key(Key::Up);
	EXPECT_FALSE(IsLit(Context(), Row("project://Assets/Screen.slss")));
	EXPECT_TRUE(IsLit(Context(), Row(".one")));
}
