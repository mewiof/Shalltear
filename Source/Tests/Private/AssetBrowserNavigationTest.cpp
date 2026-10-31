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

#include <atomic>
#include <chrono>
#include <filesystem>

#ifdef _WIN32
#include <windows.h>
#endif

using namespace Shalltear;
using namespace UITest;

#ifdef _WIN32

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

		HWND Hwnd() const { return static_cast<HWND>(_window->GetNativeHandle().hwnd); }
		UI::Context& Ui() { return *UISystem::GetContext(_window); }

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
			return ControlWearing(Ui(), Ui().GetRoot(), EditorIcons::GetGlyph(EditorIcons::Glyph::Back), EditorTheme::Class::ToolButton);
		}
		UI::ElementHandle Up(UI::ElementHandle from, u32 levels) {
			for (u32 i = 0; i < levels && Ui().IsAlive(from); ++i) {
				const UI::Element* pod = Ui().GetElement(from);
				from = pod && pod->parent != UI::INVALID_UI_INDEX ? Ui().MakeHandle(pod->parent) : UI::ElementHandle{};
			}
			return from;
		}
		Vec2 BrowserPoint() {
			UI::ElementHandle filePane = Up(BackButton(), 3);
			const UI::Element* split = Ui().IsAlive(filePane) ? Ui().GetElement(Up(filePane, 1)) : nullptr;
			if (!split) {
				ADD_FAILURE() << "browser panes not found";
				return {};
			}
			const UI::Rect tree = Ui().GetLayout(Ui().MakeHandle(split->firstChild)).borderBox;
			return {tree.x + 12.0f, tree.y + tree.h - 12.0f};
		}
		Vec2 ConsolePoint() {
			UI::ElementHandle field = FindByClass(Ui(), Ui().GetRoot(), StringHash("slt-textfield"), 0);
			if (!Ui().IsAlive(field)) {
				ADD_FAILURE() << "no Console filter field";
				return {};
			}
			return CenterOf(Ui(), field);
		}

		static Vec2 CenterOf(UI::Context& context, UI::ElementHandle handle) {
			const UI::Rect& box = context.GetLayout(handle).borderBox;
			return {box.x + box.w * 0.5f, box.y + box.h * 0.5f};
		}

		static LPARAM At(Vec2 point) { return MAKELPARAM(static_cast<i32>(point.x), static_cast<i32>(point.y)); }

		void Press(Vec2 point) {
			::PostMessageW(Hwnd(), WM_MOUSEMOVE, 0, At(point));
			::PostMessageW(Hwnd(), WM_LBUTTONDOWN, MK_LBUTTON, At(point));
			Frame();
			::PostMessageW(Hwnd(), WM_LBUTTONUP, 0, At(point));
			Frame();
		}

		void Chord(WPARAM vk, bool alt) {
			if (alt) {
				::PostMessageW(Hwnd(), WM_KEYDOWN, VK_MENU, 1 | (0x38 << 16));
			}
			::PostMessageW(Hwnd(), WM_KEYDOWN, vk, 1);
			::PostMessageW(Hwnd(), WM_KEYUP, vk, 0xC0000001);
			if (alt) {
				::PostMessageW(Hwnd(), WM_KEYUP, VK_MENU, 0xC0000001 | (0x38 << 16));
			}
			Frame();
			Frame();
		}

		void SideButton(Vec2 point, bool forward) {
			const WPARAM which = MAKEWPARAM(forward ? MK_XBUTTON2 : MK_XBUTTON1, forward ? XBUTTON2 : XBUTTON1);
			::PostMessageW(Hwnd(), WM_MOUSEMOVE, 0, At(point));
			::PostMessageW(Hwnd(), WM_XBUTTONDOWN, which, At(point));
			Frame();
			::PostMessageW(Hwnd(), WM_XBUTTONUP, MAKEWPARAM(0, forward ? XBUTTON2 : XBUTTON1), At(point));
			Frame();
		}
	};

} // anonymous namespace

TEST_F(AssetBrowserNavigationTest, KeysStepOnlyWithKeyboardFocus) {
	Write("project://Assets/Sub/Deep/x.txt");
	ASSERT_NO_FATAL_FAILURE(OpenBoth());
	ASSERT_TRUE(Ui().IsAlive(BackButton()));
	ASSERT_NO_FATAL_FAILURE(Show("Assets/Sub"));
	ASSERT_NO_FATAL_FAILURE(Show("Assets/Sub/Deep"));
	ASSERT_EQ(Folder(), "Assets/Sub/Deep");

	Press(ConsolePoint());
	ASSERT_EQ(PanelManager::GetFocusedPanel().Get(), _console.Get());
	Chord(VK_LEFT, true);
	EXPECT_EQ(Folder(), "Assets/Sub/Deep");

	Press(BrowserPoint());
	ASSERT_EQ(PanelManager::GetFocusedPanel().Get(), _browser.Get());
	Chord(VK_LEFT, true);
	EXPECT_EQ(Folder(), "Assets/Sub");
	Chord(VK_RIGHT, true);
	EXPECT_EQ(Folder(), "Assets/Sub/Deep");
	Chord(VK_LEFT, false);
	EXPECT_EQ(Folder(), "Assets/Sub/Deep");

	Chord(VK_BACK, false);
	EXPECT_EQ(Folder(), "Assets/Sub");
	Chord(VK_BACK, false);
	EXPECT_EQ(Folder(), "Assets");
	Chord(VK_BACK, false);
	EXPECT_EQ(Folder(), "Assets");
	Chord(VK_LEFT, true);
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

	UI::ElementHandle card = Ui().CreateElement();
	Ui().OpenPopup(card, {}, UI::PopupFlags::NoOutsideDismiss | UI::PopupFlags::Center);
	Frame();
	SideButton(BrowserPoint(), false);
	EXPECT_EQ(Folder(), "Assets/Sub/Deep");
	Ui().ClosePopup(card);
	Frame();
	SideButton(BrowserPoint(), false);
	EXPECT_EQ(Folder(), "Assets/Sub");
}

#endif
