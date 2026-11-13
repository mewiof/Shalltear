#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/UI/Docking.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/Window.h"

#include "Shalltear/Editor/EditorConsole.h"
#include "Shalltear/Editor/EditorFonts.h"
#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/UndoSystem.h"

#include "TestPlatform.h"

#include <atomic>
#include <chrono>
#include <filesystem>

using namespace Shalltear;
using namespace UITest;

namespace {

	class AssetBrowserNavigationTest : public ::testing::Test {
	protected:
		std::filesystem::path _root;
		Handle<Window> _window;
		Handle<EditorPanel> _browser;
		Handle<EditorPanel> _console;

		void SetUp() override {
			static std::atomic<u64> counter{0};
			const u64 unique = counter.fetch_add(1);
			const u64 stamp = static_cast<u64>(std::chrono::steady_clock::now().time_since_epoch().count());
			_root = std::filesystem::temp_directory_path() /
					("shalltear_browsernav_" + std::to_string(stamp) + "_" + std::to_string(unique));
			std::filesystem::create_directories(_root / "Assets");
			FileSystem::Mount("project", Path(_root.generic_string()));

			TypeRegistry::InitializePrimitives();
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			UISystem::Initialize();
			EditorConsole::Initialize();
			EditorSelection::Initialize();
			UndoSystem::Initialize();
			PanelManager::Initialize();
			RegisterBuiltInPanels();

			WindowDesc desc;
			desc.title = "AssetBrowserNavigation";
			desc.clientSize = {1200, 700};
			desc.flags = WindowFlags::Hidden;
			_window = WindowSystem::Create(desc);
			ASSERT_TRUE(_window.IsValid());
			ASSERT_NE(UISystem::CreateDockManager(_window), nullptr);
			PanelManager::SetDefaultWindow(_window);
		}

		void TearDown() override {
			PanelManager::Terminate();
			UndoSystem::Terminate();
			EditorSelection::Terminate();
			EditorConsole::Terminate();
			EditorIcons::Shutdown();
			EditorFonts::Shutdown();
			UISystem::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			AssetDatabase::Terminate();
			FileSystem::Unmount("project");
			std::error_code ec;
			std::filesystem::remove_all(_root, ec);
		}

		void Write(const str& virtualPath) {
			Path resolved = FileSystem::Resolve(Path(virtualPath));
			std::error_code ec;
			std::filesystem::create_directories(std::filesystem::path(resolved.ToString()).parent_path(), ec);
			FileSystem::WriteTextFile(resolved, "x");
		}

		UI::Context& UI() { return *UISystem::GetContext(_window); }

		void Frame() {
			WindowSystem::PumpEvents();
			PanelManager::TickAll(0.016f);
			UISystem::Update();
		}

		void OpenBoth() {
			AssetDatabase::ScanProjectDirectory();
			_browser = PanelManager::OpenPanel(TypeRegistry::FindType("AssetBrowserPanel"_hash));
			_console = PanelManager::OpenPanel(TypeRegistry::FindType("ConsolePanel"_hash));
			ASSERT_TRUE(_browser.IsValid() && _console.IsValid());
			UI::DockManager* dock = UISystem::GetDockManager(_window);
			dock->DockPanel(_console.Get()->GetDockId(), dock->GetRootNode(), UI::DockZone::Top);
			for (int i = 0; i < 4; ++i) {
				Frame();
			}
		}

		str Folder() {
			Variant value;
			EditorPanel* panel = _browser.Get();
			return panel && GetField(panel, panel->GetType(), "selectedFolder"_hash, &value) ? value.As<str>() : str();
		}

		void Show(const str& folder) {
			Variant value{folder};
			ASSERT_TRUE(SetField(_browser.Get(), _browser.Get()->GetType(), "selectedFolder"_hash, &value));
			Frame();
			Frame();
		}

		UI::ElementHandle BackButton() {
			return ControlWearing(UI(), UI().GetRoot(), EditorIcons::GetGlyph(EditorIcons::Glyph::Back), EditorTheme::Class::ToolButton);
		}
		UI::ElementHandle Up(UI::ElementHandle from, u32 levels) {
			for (u32 i = 0; i < levels && UI().IsAlive(from); ++i) {
				const UI::Element* pod = UI().GetElement(from);
				from = pod && pod->parent != UI::INVALID_UI_INDEX ? UI().MakeHandle(pod->parent) : UI::ElementHandle{};
			}
			return from;
		}
		Vec2 BrowserPoint() {
			UI::ElementHandle filePane = Up(BackButton(), 3);
			const UI::Element* split = UI().IsAlive(filePane) ? UI().GetElement(Up(filePane, 1)) : nullptr;
			if (!split) {
				ADD_FAILURE() << "browser panes not found";
				return {};
			}
			const UI::Rect tree = UI().GetLayout(UI().MakeHandle(split->firstChild)).borderBox;
			return {tree.x + 12.0f, tree.y + tree.h - 12.0f};
		}
		Vec2 ConsolePoint() {
			UI::ElementHandle field = FindByClass(UI(), UI().GetRoot(), StringHash("slt-textfield"), 0);
			if (!UI().IsAlive(field)) {
				ADD_FAILURE() << "no Console filter field";
				return {};
			}
			return CenterOf(UI(), field);
		}

		static Vec2 CenterOf(UI::Context& context, UI::ElementHandle handle) {
			const UI::Rect& box = context.GetLayout(handle).borderBox;
			return {box.x + box.w * 0.5f, box.y + box.h * 0.5f};
		}

		static Vec2I At(Vec2 point) { return {static_cast<i32>(point.x), static_cast<i32>(point.y)}; }

		void Press(Vec2 point) {
			TestPlatform::PostMouseMove(_window, At(point));
			TestPlatform::PostMouseButton(_window, MouseButton::Left, true, At(point));
			Frame();
			TestPlatform::PostMouseButton(_window, MouseButton::Left, false, At(point));
			Frame();
		}

		void Chord(Key key, bool alt) {
			if (alt) {
				TestPlatform::PostKey(_window, Key::LeftAlt, true);
			}
			TestPlatform::PostKey(_window, key, true);
			TestPlatform::PostKey(_window, key, false);
			if (alt) {
				TestPlatform::PostKey(_window, Key::LeftAlt, false);
			}
			Frame();
			Frame();
		}

		void SideButton(Vec2 point, bool forward) {
			const MouseButton which = forward ? MouseButton::X2 : MouseButton::X1;
			TestPlatform::PostMouseMove(_window, At(point));
			TestPlatform::PostMouseButton(_window, which, true, At(point));
			Frame();
			TestPlatform::PostMouseButton(_window, which, false, At(point));
			Frame();
		}
	};

} // anonymous namespace

TEST_F(AssetBrowserNavigationTest, KeysStepOnlyWithKeyboardFocus) {
	Write("project://Assets/Sub/Deep/x.txt");
	ASSERT_NO_FATAL_FAILURE(OpenBoth());
	ASSERT_TRUE(UI().IsAlive(BackButton()));
	ASSERT_NO_FATAL_FAILURE(Show("Assets/Sub"));
	ASSERT_NO_FATAL_FAILURE(Show("Assets/Sub/Deep"));
	ASSERT_EQ(Folder(), "Assets/Sub/Deep");

	Press(ConsolePoint());
	ASSERT_EQ(PanelManager::GetFocusedPanel().Get(), _console.Get());
	Chord(Key::Left, true);
	EXPECT_EQ(Folder(), "Assets/Sub/Deep");

	Press(BrowserPoint());
	ASSERT_EQ(PanelManager::GetFocusedPanel().Get(), _browser.Get());
	Chord(Key::Left, true);
	EXPECT_EQ(Folder(), "Assets/Sub");
	Chord(Key::Right, true);
	EXPECT_EQ(Folder(), "Assets/Sub/Deep");
	Chord(Key::Left, false);
	EXPECT_EQ(Folder(), "Assets/Sub/Deep");

	Chord(Key::Backspace, false);
	EXPECT_EQ(Folder(), "Assets/Sub");
	Chord(Key::Backspace, false);
	EXPECT_EQ(Folder(), "Assets");
	Chord(Key::Backspace, false);
	EXPECT_EQ(Folder(), "Assets");
	Chord(Key::Left, true);
	EXPECT_EQ(Folder(), "Assets/Sub");
}

TEST_F(AssetBrowserNavigationTest, SideButtonsStepBrowserUnderPointer) {
	Write("project://Assets/Sub/Deep/x.txt");
	ASSERT_NO_FATAL_FAILURE(OpenBoth());
	ASSERT_NO_FATAL_FAILURE(Show("Assets/Sub"));
	ASSERT_NO_FATAL_FAILURE(Show("Assets/Sub/Deep"));

	Press(ConsolePoint());
	SideButton(ConsolePoint(), false);
	EXPECT_EQ(Folder(), "Assets/Sub/Deep");

	SideButton(BrowserPoint(), false);
	EXPECT_EQ(Folder(), "Assets/Sub");
	SideButton(BrowserPoint(), true);
	EXPECT_EQ(Folder(), "Assets/Sub/Deep");

	UI::ElementHandle card = UI().CreateElement();
	UI().OpenPopup(card, {}, UI::PopupFlags::NoOutsideDismiss | UI::PopupFlags::Center);
	Frame();
	SideButton(BrowserPoint(), false);
	EXPECT_EQ(Folder(), "Assets/Sub/Deep");
	UI().ClosePopup(card);
	Frame();
	SideButton(BrowserPoint(), false);
	EXPECT_EQ(Folder(), "Assets/Sub");
}
