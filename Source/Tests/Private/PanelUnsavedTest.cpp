#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/UI/Docking.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/Window.h"
#include "Shalltear/Core/Window/WindowSystem.h"

#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/PanelManager.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	constexpr Vec2I Parked = {-20000, -20000};

	void Frame() {
		WindowSystem::PumpEvents();
		PanelManager::TickAll(0.016f);
		UISystem::Update();
	}

	class UnsavedProbePanel : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(UnsavedProbePanel)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("Unsaved Probe")
		SLT_END_REFLECT()

		UnsavedProbePanel() = default;

		bool unsaved = false;
		bool saveFails = false;
		u32 saves = 0;
		u32 discards = 0;

		bool HasUnsavedChanges() const override { return unsaved; }
		void SaveChanges() override {
			++saves;
			unsaved = saveFails;
		}
		void DiscardChanges() override {
			++discards;
			unsaved = false;
		}
		void OnBuildContent(UI::Context&, UI::ElementHandle) override {}
	};

	class PanelUnsavedFixture : public ::testing::Test {
	protected:
		Handle<Window> _main;

		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			UnsavedProbePanel::GetReflectType();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			UISystem::Initialize();
			EditorSelection::Initialize();
			PanelManager::Initialize();

			WindowDesc desc;
			desc.title = "UnsavedHost";
			desc.clientSize = {800, 600};
			desc.position = Parked;
			desc.flags = WindowFlags::Resizable;
			_main = WindowSystem::Create(desc);
			ASSERT_TRUE(_main.IsValid());
			ASSERT_NE(UISystem::CreateDockManager(_main), nullptr);
			PanelManager::SetDefaultWindow(_main);
		}

		void TearDown() override {
			PanelManager::Terminate();
			Vector<Handle<Window>> windows{GetMemoryResource(MemTag::Debug)};
			UISystem::GetDockWindows(windows);
			for (Handle<Window> window : windows) {
				WindowSystem::RequestDestroy(window);
			}
			WindowSystem::PumpEvents();
			EditorSelection::Terminate();
			UISystem::Terminate();
			RenderSystem::Terminate();
			WindowSystem::Terminate();
		}

		Handle<EditorPanel> Open() {
			Handle<EditorPanel> panel = PanelManager::OpenPanel(UnsavedProbePanel::GetReflectType(), _main);
			Frame();
			Frame();
			return panel;
		}

		static UnsavedProbePanel* Probe(Handle<EditorPanel> panel) { return static_cast<UnsavedProbePanel*>(panel.Get()); }

		UI::Context& Ui() { return *UISystem::GetContext(_main); }

		bool Answer(std::string_view label) {
			UI::ElementHandle button = FindByText(Ui(), Ui().GetRoot(), label);
			if (!Ui().IsAlive(button)) {
				return false;
			}
			ClickElement(Ui(), button);
			Frame();
			return true;
		}
	};

} // anonymous namespace

TEST_F(PanelUnsavedFixture, ACleanPanelClosesWithoutAsking) {
	Handle<EditorPanel> panel = Open();
	ASSERT_TRUE(panel.IsValid());
	PanelManager::CloseByDockId(panel->GetDockId());
	Frame();
	EXPECT_EQ(PanelManager::GetOpenPanelCount(), 0u);
	EXPECT_FALSE(Ui().IsAlive(FindByText(Ui(), Ui().GetRoot(), "Discard")));
}

TEST_F(PanelUnsavedFixture, UnsavedPanelAsks) {
	Handle<EditorPanel> panel = Open();
	ASSERT_TRUE(panel.IsValid());

	Probe(panel)->unsaved = true;
	PanelManager::CloseByDockId(panel->GetDockId());
	Frame();
	ASSERT_EQ(PanelManager::GetOpenPanelCount(), 1u);
	ASSERT_TRUE(Answer("Cancel"));
	EXPECT_EQ(PanelManager::GetOpenPanelCount(), 1u);
	EXPECT_EQ(Probe(panel)->saves + Probe(panel)->discards, 0u);

	PanelManager::CloseByDockId(panel->GetDockId());
	Frame();
	ASSERT_TRUE(Answer("Discard"));
	EXPECT_EQ(PanelManager::GetOpenPanelCount(), 0u);

	Handle<EditorPanel> second = Open();
	Probe(second)->unsaved = true;
	PanelManager::CloseByDockId(second->GetDockId());
	Frame();
	UnsavedProbePanel* probe = Probe(second);
	ASSERT_TRUE(Answer("Save"));
	EXPECT_EQ(probe->saves, 1u);
	EXPECT_EQ(PanelManager::GetOpenPanelCount(), 0u);
}

TEST_F(PanelUnsavedFixture, SaveThatFailsKeepsPanelOpen) {
	Handle<EditorPanel> panel = Open();
	Probe(panel)->unsaved = true;
	Probe(panel)->saveFails = true;
	PanelManager::CloseByDockId(panel->GetDockId());
	Frame();
	ASSERT_TRUE(Answer("Save"));
	EXPECT_EQ(Probe(panel)->saves, 1u);
	EXPECT_EQ(PanelManager::GetOpenPanelCount(), 1u);
}

TEST_F(PanelUnsavedFixture, TheTabCarriesTheUnsavedMark) {
	Handle<EditorPanel> panel = Open();
	ASSERT_TRUE(Ui().IsAlive(FindByText(Ui(), Ui().GetRoot(), "Unsaved Probe")));

	Probe(panel)->unsaved = true;
	Frame();
	EXPECT_TRUE(Ui().IsAlive(FindByText(Ui(), Ui().GetRoot(), "Unsaved Probe*")));

	Probe(panel)->unsaved = false;
	Frame();
	EXPECT_TRUE(Ui().IsAlive(FindByText(Ui(), Ui().GetRoot(), "Unsaved Probe")));
	EXPECT_FALSE(Ui().IsAlive(FindByText(Ui(), Ui().GetRoot(), "Unsaved Probe*")));
}

TEST_F(PanelUnsavedFixture, QuitAsksPerUnsavedPanel) {
	u32 proceeded = 0;
	PanelManager::ConfirmUnsavedPanelsThen(Ui(), [&proceeded]() { ++proceeded; });
	EXPECT_EQ(proceeded, 1u);

	Handle<EditorPanel> first = Open();
	Handle<EditorPanel> second = Open();
	Probe(first)->unsaved = true;
	Probe(second)->unsaved = true;
	PanelManager::ConfirmUnsavedPanelsThen(Ui(), [&proceeded]() { ++proceeded; });
	Frame();
	EXPECT_EQ(proceeded, 1u);
	ASSERT_TRUE(Answer("Save All & Continue"));
	EXPECT_EQ(Probe(first)->saves, 1u);
	EXPECT_EQ(Probe(second)->saves, 1u);
	EXPECT_EQ(proceeded, 2u);

	Probe(first)->unsaved = true;
	Probe(first)->saveFails = true;
	PanelManager::ConfirmUnsavedPanelsThen(Ui(), [&proceeded]() { ++proceeded; });
	Frame();
	ASSERT_TRUE(Answer("Save All & Continue"));
	EXPECT_EQ(proceeded, 2u);
}
