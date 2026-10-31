#include "TestsPCH.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/Docking.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/Window.h"

#include "Shalltear/Editor/EditorEvents.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PanelManager.h"

#include "UITestUtil.h"

using namespace Shalltear;

namespace {

	std::filesystem::path FindRepoDir(const char* relative) {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
			std::filesystem::path candidate = probe / relative;
			if (std::filesystem::exists(candidate, ec)) {
				return candidate;
			}
			std::filesystem::path parent = probe.parent_path();
			if (parent == probe) {
				break;
			}
			probe = parent;
		}
		return {};
	}

	u32 _clicks = 0;

	constexpr StringHash PanelMark = StringHash("tint-test-row");
	constexpr StringHash ViewportMark = StringHash("tint-viewport-mark");

	class TintTestPanel : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(TintTestPanel)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("Tint Test")
		SLT_END_REFLECT()

		TintTestPanel() = default;

		void OnBuildContent(UI::Context& context, UI::ElementHandle root) override {
			UI::ElementHandle row = context.CreateElement();
			context.AppendChild(root, row);
			UI::Style style;
			style.width = UI::Length::Percent(1.0f);
			style.height = UI::Length::Percent(1.0f);
			context.SetStyle(row, style);
			context.AddClass(row, PanelMark);
			context.Events(row).onClick = []() { ++_clicks; };
		}
	};

	class TintViewportPanel : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(TintViewportPanel)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("Tint Viewport")
		SLT_END_REFLECT()

		TintViewportPanel() = default;

		bool WantsPlayModeTint() const override { return false; }

		void OnBuildContent(UI::Context& context, UI::ElementHandle root) override {
			UI::ElementHandle mark = context.CreateElement();
			context.AppendChild(root, mark);
			context.AddClass(mark, ViewportMark);
		}
	};

	class PlayModeTintTest : public ::testing::Test {
	protected:
		Handle<Window> _window;
		Handle<EditorPanel> _panel;
		Handle<EditorPanel> _viewport;

		void SetUp() override {
			_clicks = 0;
			TypeRegistry::InitializePrimitives();
			TintTestPanel::GetReflectType();
			TintViewportPanel::GetReflectType();
			std::filesystem::path engineAssets = FindRepoDir("EngineAssets");
			ASSERT_FALSE(engineAssets.empty());
			FileSystem::Mount("engine", Path(engineAssets.string()));
			JobSystem::Initialize();
			AssetDatabase::Initialize();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			UISystem::Initialize();
			UISystem::SetContextInitializer(
				[](UI::Context& context) { EditorTheme::ApplyToContext(context); });
			EditorSelection::Initialize();
			PanelManager::Initialize();

			WindowDesc desc;
			desc.title = "PlayModeTint";
			desc.clientSize = {800, 600};
			desc.flags = WindowFlags::Hidden;
			_window = WindowSystem::Create(desc);
			ASSERT_TRUE(_window.IsValid());
			ASSERT_NE(UISystem::CreateDockManager(_window), nullptr);
			PanelManager::SetDefaultWindow(_window);

			_panel = PanelManager::OpenPanel(TintTestPanel::GetReflectType(), _window);
			_viewport = PanelManager::OpenPanel(TintViewportPanel::GetReflectType(), _window);
			ASSERT_TRUE(_panel.IsValid());
			ASSERT_TRUE(_viewport.IsValid());
			if (UI::DockManager* dock = UISystem::GetDockManager(_window)) {
				dock->DockPanel(_viewport.Get()->GetDockId(), dock->GetRootNode(), UI::DockZone::Right);
			}
			Frame();
		}

		void TearDown() override {
			PanelManager::Terminate();
			EditorSelection::Terminate();
			UISystem::SetContextInitializer(nullptr);
			UISystem::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
			AssetDatabase::Terminate();
			JobSystem::Terminate();
			FileSystem::Unmount("engine");
		}

		void Frame() {
			PanelManager::TickAll(0.016f);
			UISystem::Update();
		}

		void SetPlayState(PlayModeState state) {
			EditorEvents::PlayModeChanged.Publish(EventBus::GetGlobal(), {state});
			Frame();
		}

		UI::Context& Context() { return *UISystem::GetContext(_window); }

		UI::ElementHandle ContentRootOf(StringHash marker) {
			UI::Context& context = Context();
			UI::ElementHandle built = UITest::FindByClass(context, context.GetRoot(), marker, 0);
			if (!context.IsAlive(built)) {
				return {};
			}
			const UI::Element* pod = context.GetElement(built);
			return pod->parent == UI::INVALID_UI_INDEX ? UI::ElementHandle{} : context.MakeHandle(pod->parent);
		}

		u32 WashCount() {
			UI::Context& context = Context();
			return UITest::CountByClass(context, context.GetRoot(), EditorTheme::Class::PlayTint);
		}

		UI::ElementHandle WashOver(StringHash marker) {
			UI::Context& context = Context();
			UI::ElementHandle root = ContentRootOf(marker);
			if (!context.IsAlive(root)) {
				return {};
			}
			return UITest::FindByClass(context, root, EditorTheme::Class::PlayTint, 0);
		}
	};

} // anonymous namespace

TEST_F(PlayModeTintTest, PanelsTintedWhilePlaying) {
	ASSERT_TRUE(Context().IsAlive(ContentRootOf(PanelMark)));
	EXPECT_FALSE(Context().IsAlive(WashOver(PanelMark)));

	SetPlayState(PlayModeState::Playing);
	EXPECT_TRUE(Context().IsAlive(WashOver(PanelMark)));

	SetPlayState(PlayModeState::Paused);
	EXPECT_TRUE(Context().IsAlive(WashOver(PanelMark)));

	SetPlayState(PlayModeState::Editing);
	EXPECT_FALSE(Context().IsAlive(WashOver(PanelMark)));
	EXPECT_EQ(WashCount(), 0u);
}

TEST_F(PlayModeTintTest, PanelThatDeclinesWashNeverGetsOne) {
	ASSERT_TRUE(Context().IsAlive(ContentRootOf(ViewportMark)));

	SetPlayState(PlayModeState::Playing);
	EXPECT_TRUE(Context().IsAlive(WashOver(PanelMark)));
	EXPECT_FALSE(Context().IsAlive(WashOver(ViewportMark)));
	EXPECT_EQ(WashCount(), 1u);
}

TEST_F(PlayModeTintTest, ClickReachesRowUnderWash) {
	UI::Context& context = Context();
	UI::ElementHandle row = UITest::FindByClass(context, context.GetRoot(), PanelMark, 0);
	ASSERT_TRUE(context.IsAlive(row));
	UITest::ClickElement(context, row);
	Frame();
	ASSERT_EQ(_clicks, 1u);

	SetPlayState(PlayModeState::Playing);
	ASSERT_TRUE(context.IsAlive(WashOver(PanelMark)));
	row = UITest::FindByClass(context, context.GetRoot(), PanelMark, 0);
	ASSERT_TRUE(context.IsAlive(row));
	UITest::ClickElement(context, row);
	Frame();
	EXPECT_EQ(_clicks, 2u);
}

TEST_F(PlayModeTintTest, PanelRebuiltDuringPlayComesUpWashed) {
	SetPlayState(PlayModeState::Playing);
	ASSERT_EQ(WashCount(), 1u);

	Handle<EditorPanel> second = PanelManager::OpenPanel(TintTestPanel::GetReflectType(), _window);
	ASSERT_TRUE(second.IsValid());
	if (UI::DockManager* dock = UISystem::GetDockManager(_window)) {
		dock->DockPanel(second.Get()->GetDockId(), dock->GetRootNode(), UI::DockZone::Bottom);
	}
	Frame();

	EXPECT_EQ(WashCount(), 2u);
	EXPECT_TRUE(Context().IsAlive(WashOver(PanelMark)));
}
