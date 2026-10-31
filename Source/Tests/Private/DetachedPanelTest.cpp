#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Input.h"
#include "Shalltear/Core/UI/Docking.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"
#include "Shalltear/Core/Window.h"
#include "Shalltear/Core/Window/WindowEvents.h"
#include "Shalltear/Core/Window/WindowSystem.h"

#include "Shalltear/Editor/EditorListView.h"
#include "Shalltear/Editor/EditorPresentPolicy.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/PanelManager.h"

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

using namespace Shalltear;
using namespace UITest;

namespace {

	constexpr Vec2I Parked = {-20000, -20000};
	constexpr Vec2I ParkedDetach = {-19000, -19000};
	constexpr Vec2I ParkedDetach2 = {-18000, -18000};

	void Frame() {
		WindowSystem::PumpEvents();
		PanelManager::TickAll(0.016f);
		UISystem::Update();
	}

	class DetachTestPanel : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(DetachTestPanel)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("Detach Probe")
		SLT_END_REFLECT()

		DetachTestPanel() = default;

		u32 chromeClicks = 0;
		u32 footerClicks = 0;
		u32 scrolledClicks = 0;
		u32 menuClicks = 0;

		UI::ElementHandle chromeButton;
		UI::ElementHandle footerButton;
		UI::ElementHandle scrolledButton;
		UI::ElementHandle menuButton;
		UI::ElementHandle openedMenu;
		UI::ElementHandle viewport;

		void OnBuildContent(UI::Context& context, UI::ElementHandle root) override {
			UI::Style rootStyle;
			rootStyle.flexDirection = UI::FlexDirection::Column;
			rootStyle.alignItems = UI::Align::Stretch;
			rootStyle.width = UI::Length::Percent(1.0f);
			rootStyle.height = UI::Length::Percent(1.0f);
			context.SetStyle(root, rootStyle);

			chromeButton = MakeButton(context, root, chromeClicks);

			menuButton = MakeButton(context, root, menuClicks);
			UI::Context* contextPtr = &context;
			context.Events(menuButton).onClick = [this, contextPtr]() {
				++menuClicks;
				Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Debug)};
				UI::Widgets::MenuItem item;
				item.label = "Probe";
				items.push_back(std::move(item));
				openedMenu = UI::Widgets::OpenMenu(*contextPtr, {24.0f, 24.0f}, {items.data(), items.size()}, {}, 12.0f);
			};

			UI::Widgets::ScrollViewParts scroll = UI::Widgets::ScrollView(context, root);
			UI::Style scrollStyle = context.GetStyle(scroll.root);
			scrollStyle.flexGrow = 1.0f;
			context.SetStyle(scroll.root, scrollStyle);
			viewport = scroll.viewport;

			footerButton = MakeButton(context, root, footerClicks);
		}

		void OnTick(f32) override {
			UI::Context* context = GetPanelContext();
			if (!context || !context->IsAlive(viewport) || context->IsAlive(scrolledButton)) {
				return;
			}
			scrolledButton = MakeButton(*context, viewport, scrolledClicks);
		}
	private:
		static UI::ElementHandle MakeButton(UI::Context& context, UI::ElementHandle parent, u32& counter) {
			UI::ElementHandle button = context.CreateElement();
			context.AppendChild(parent, button);
			UI::Style style;
			style.width = UI::Length::Pixel(80.0f);
			style.height = UI::Length::Pixel(24.0f);
			style.flexShrink = 0.0f;
			context.SetStyle(button, style);
			context.Events(button).onClick = [&counter]() { ++counter; };
			return button;
		}
	};

	class ListDetachTestPanel : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(ListDetachTestPanel)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("List Detach Probe")
		SLT_END_REFLECT()

		ListDetachTestPanel() = default;

		StringHash decorClass{"probe-before"};
		Vector<UI::Context*> decorated{GetMemoryResource(MemTag::Debug)};
		Vector<UI::Context*> menuedIn{GetMemoryResource(MemTag::Debug)};

		void OnBuildContent(UI::Context& context, UI::ElementHandle root) override {
			UI::Style rootStyle;
			rootStyle.flexDirection = UI::FlexDirection::Column;
			rootStyle.width = UI::Length::Percent(1.0f);
			rootStyle.height = UI::Length::Percent(1.0f);
			context.SetStyle(root, rootStyle);
			ListViewDesc desc;
			desc.layout = ListLayout::Rows;
			list.Build(this, context, root, std::move(desc));
		}

		void OnTick(f32) override {
			if (_filled || GetPanelContext() == nullptr) {
				return;
			}
			_filled = true;
			Vector<ListRow> rows{GetMemoryResource(MemTag::Debug)};
			for (u64 id = 1; id <= 6; ++id) {
				ListRow row;
				row.id = id;
				row.label = "Row " + std::to_string(id);
				row.onDecorate = [this](UI::Context& context, const UI::Widgets::TreeRowParts& parts) {
					decorated.push_back(&context);
					context.AddClass(parts.row, decorClass);
				};
				row.onRightClick = [this](UI::Context& context) { menuedIn.push_back(&context); };
				rows.push_back(std::move(row));
			}
			list.SetRows(rows);
		}

		EditorListView list;
	private:
		bool _filled = false;
	};

	class FloatingProbePanel : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(FloatingProbePanel)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("Floating Probe")
		SLT_EDITOR_PANEL_FLOATING_SIZE(300.0f, 200.0f)
		SLT_END_REFLECT()

		FloatingProbePanel() = default;

		void OnBuildContent(UI::Context&, UI::ElementHandle) override {}
	};

	class DetachedPanelFixture : public ::testing::Test {
	protected:
		Handle<Window> _main;

		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			DetachTestPanel::GetReflectType();
			ListDetachTestPanel::GetReflectType();
			FloatingProbePanel::GetReflectType();
			WindowSystem::Initialize();
			RenderSystem::Initialize();
			UISystem::Initialize();
			EditorSelection::Initialize();
			PanelManager::Initialize();

			WindowDesc desc;
			desc.title = "DetachHost";
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

		static DetachTestPanel* Probe(Handle<EditorPanel> panel) {
			return static_cast<DetachTestPanel*>(panel.Get());
		}

		static void FloatingWindows(Vector<Handle<Window>>& out) {
			out.clear();
			Vector<Handle<Window>> windows{GetMemoryResource(MemTag::Debug)};
			UISystem::GetDockWindows(windows);
			for (Handle<Window> window : windows) {
				if (UISystem::IsFloatingDockWindow(window)) {
					out.push_back(window);
				}
			}
		}

		static Handle<Window> WindowHolding(Handle<EditorPanel> panel) {
			EditorPanel* resolved = panel.Get();
			if (!resolved) {
				return {};
			}
			const StringHash dockId = resolved->GetDockId();
			Vector<Handle<Window>> windows{GetMemoryResource(MemTag::Debug)};
			UISystem::GetDockWindows(windows);
			for (Handle<Window> window : windows) {
				UI::DockManager* dock = UISystem::GetDockManager(window);
				if (dock && dock->IsPanelDocked(dockId)) {
					return window;
				}
			}
			return {};
		}

		static bool RightClickFirstRow(UI::Context& context) {
			UI::ElementHandle row = FindByClass(context, context.GetRoot(), StringHash("slt-tree-row"), 0);
			if (!context.IsAlive(row)) {
				return false;
			}
			const UI::Rect box = context.GetLayout(row).borderBox;
			InputEvent event{};
			event.button = MouseButton::Right;
			event.mousePosPx = {static_cast<i32>(box.x + box.w * 0.5f), static_cast<i32>(box.y + box.h * 0.5f)};
			event.type = InputEventType::MouseMove;
			context.InjectInput(event);
			event.type = InputEventType::MouseDown;
			context.InjectInput(event);
			event.type = InputEventType::MouseUp;
			context.InjectInput(event);
			context.Update();
			return true;
		}

		Handle<Window> TearOut(u32 tabIndex = 0, Vec2I landAt = ParkedDetach) {
			Vector<Handle<Window>> before{GetMemoryResource(MemTag::Debug)};
			FloatingWindows(before);

			UI::DockManager* dock = UISystem::GetDockManager(_main);
			UI::Context* context = UISystem::GetContext(_main);
			if (!dock || !context) {
				return {};
			}
			UI::ElementHandle tab = FindByClass(*context, context->GetRoot(), StringHash("dock-tab"), tabIndex);
			if (!context->IsAlive(tab)) {
				return {};
			}
			const UI::Rect box = context->GetLayout(tab).borderBox;
			Vec2 tabPoint{box.x + box.w * 0.5f, box.y + box.h * 0.5f};

			dock->UpdateInteraction(tabPoint, Parked, false, false);
			InputEvent move{};
			move.type = InputEventType::MouseMove;
			move.mousePosPx = {static_cast<i32>(tabPoint.x), static_cast<i32>(tabPoint.y)};
			context->InjectInput(move);
			InputEvent down{};
			down.type = InputEventType::MouseDown;
			down.button = MouseButton::Left;
			down.mousePosPx = move.mousePosPx;
			context->InjectInput(down);
			context->Update();

			dock->UpdateInteraction(tabPoint, Parked, true, true);
			const Vec2 outside{900.0f, 700.0f};
			dock->UpdateInteraction(outside, landAt, true, false);
			dock->UpdateInteraction(outside, landAt, false, false);

			Frame();
			Frame();
			Frame();

			Vector<Handle<Window>> after{GetMemoryResource(MemTag::Debug)};
			FloatingWindows(after);
			for (Handle<Window> window : after) {
				bool existed = false;
				for (Handle<Window> old : before) {
					existed = existed || old == window;
				}
				if (!existed) {
					return window;
				}
			}
			return {};
		}
	};

	class DetachedPanelPresentFixture : public DetachedPanelFixture {
	protected:
		void SetUp() override {
			_policy.Install();
			DetachedPanelFixture::SetUp();
		}

		EditorPresentPolicy _policy;
	};

	void ClickPointInWindow(Handle<Window> window, i32 x, i32 y) {
		Window* resolved = window.Get();
		ASSERT_NE(resolved, nullptr);
		HWND hwnd = static_cast<HWND>(resolved->GetNativeHandle().hwnd);
		::PostMessageW(hwnd, WM_MOUSEMOVE, 0, MAKELPARAM(x, y));
		Frame();
		::PostMessageW(hwnd, WM_LBUTTONDOWN, MK_LBUTTON, MAKELPARAM(x, y));
		Frame();
		Frame();
		::PostMessageW(hwnd, WM_LBUTTONUP, 0, MAKELPARAM(x, y));
		Frame();
	}

	void ClickThroughTheWindow(Handle<Window> window, UI::Context& context, UI::ElementHandle element) {
		const UI::Rect box = context.GetLayout(element).borderBox;
		ClickPointInWindow(window, static_cast<i32>(box.x + box.w * 0.5f), static_cast<i32>(box.y + box.h * 0.5f));
	}

	void ExpectChromeTakesClicks(Handle<Window> window, DetachTestPanel* probe, const char* state) {
		UI::Context* context = UISystem::GetContext(window);
		ASSERT_NE(context, nullptr) << state;
		ASSERT_TRUE(context->IsAlive(probe->chromeButton)) << state;

		for (UI::ElementHandle part : {probe->chromeButton, probe->footerButton}) {
			const UI::Rect box = context->GetLayout(part).borderBox;
			ASSERT_GT(box.w, 0.0f) << state;
			ASSERT_GT(box.h, 0.0f) << state;
			ASSERT_EQ(context->HitTest({box.x + box.w * 0.5f, box.y + box.h * 0.5f}), part) << state;
		}

		const u32 chrome = probe->chromeClicks;
		const u32 footer = probe->footerClicks;
		ClickThroughTheWindow(window, *context, probe->chromeButton);
		ClickThroughTheWindow(window, *context, probe->footerButton);
		EXPECT_EQ(probe->chromeClicks, chrome + 1) << state;
		EXPECT_EQ(probe->footerClicks, footer + 1) << state;
	}

} // namespace

TEST_F(DetachedPanelFixture, ChromeOutsideViewportTakesClicksWhileDocked) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	ASSERT_TRUE(panel.IsValid());
	Frame();
	Frame();

	UI::Context* context = UISystem::GetContext(_main);
	ASSERT_NE(context, nullptr);
	DetachTestPanel* probe = Probe(panel);
	ASSERT_TRUE(context->IsAlive(probe->scrolledButton));

	ClickThroughTheWindow(_main, *context, probe->chromeButton);
	ClickThroughTheWindow(_main, *context, probe->scrolledButton);
	ClickThroughTheWindow(_main, *context, probe->footerButton);

	EXPECT_EQ(probe->chromeClicks, 1u);
	EXPECT_EQ(probe->scrolledClicks, 1u);
	EXPECT_EQ(probe->footerClicks, 1u);
}

TEST_F(DetachedPanelFixture, ChromeOutsideViewportTakesClicksWhileDetached) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	ASSERT_TRUE(panel.IsValid());
	Frame();
	Frame();

	Handle<Window> floating = TearOut();
	ASSERT_TRUE(floating.IsValid());
	floating->SetClientSize({900, 620});
	Frame();
	Frame();

	UI::Context* context = UISystem::GetContext(floating);
	ASSERT_NE(context, nullptr);
	DetachTestPanel* probe = Probe(panel);
	ASSERT_TRUE(context->IsAlive(probe->chromeButton));
	ASSERT_TRUE(context->IsAlive(probe->scrolledButton));

	for (UI::ElementHandle part : {probe->chromeButton, probe->scrolledButton, probe->footerButton}) {
		const UI::Rect box = context->GetLayout(part).borderBox;
		ASSERT_GT(box.w, 0.0f);
		ASSERT_GT(box.h, 0.0f);
		EXPECT_EQ(context->HitTest({box.x + box.w * 0.5f, box.y + box.h * 0.5f}), part) << box.x << "," << box.y;
	}

	ClickThroughTheWindow(floating, *context, probe->chromeButton);
	ClickThroughTheWindow(floating, *context, probe->scrolledButton);
	ClickThroughTheWindow(floating, *context, probe->footerButton);

	EXPECT_EQ(probe->scrolledClicks, 1u);
	EXPECT_EQ(probe->chromeClicks, 1u);
	EXPECT_EQ(probe->footerClicks, 1u);
}

TEST_F(DetachedPanelFixture, ChromeClickableWithSecondWindow) {
	Handle<EditorPanel> first = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	Handle<EditorPanel> second = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	ASSERT_TRUE(first.IsValid());
	ASSERT_TRUE(second.IsValid());
	Frame();
	Frame();

	Handle<Window> firstWindow = TearOut(0, ParkedDetach);
	ASSERT_TRUE(firstWindow.IsValid());
	Handle<Window> secondWindow = TearOut(0, ParkedDetach2);
	ASSERT_TRUE(secondWindow.IsValid());
	ASSERT_NE(firstWindow, secondWindow);

	Handle<Window> firstHome = WindowHolding(first);
	Handle<Window> secondHome = WindowHolding(second);
	ASSERT_TRUE(firstHome.IsValid());
	ASSERT_TRUE(secondHome.IsValid());
	ASSERT_NE(firstHome, secondHome);

	ExpectChromeTakesClicks(firstHome, Probe(first), "two floating windows, pressing the first");
	ExpectChromeTakesClicks(secondHome, Probe(second), "two floating windows, pressing the second");
}

TEST_F(DetachedPanelFixture, ChromeClickableAfterRedock) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	ASSERT_TRUE(panel.IsValid());
	Frame();
	Frame();
	const StringHash dockId = panel.Get()->GetDockId();

	Handle<Window> floating = TearOut();
	ASSERT_TRUE(floating.IsValid());

	Window* mainResolved = _main.Get();
	ASSERT_NE(mainResolved, nullptr);
	const Vec2I client = mainResolved->GetClientSize();
	const Vec2I centerClient{client.x / 2, client.y / 2};
	WindowDragMovedEvent moved;
	moved.window = floating;
	moved.screenCursorPx = mainResolved->ClientToScreen(centerClient);
	WindowEvents::DragMoved.Publish(EventBus::GetGlobal(), moved);
	WindowDragEndedEvent ended;
	ended.window = floating;
	ended.screenCursorPx = moved.screenCursorPx;
	WindowEvents::DragEnded.Publish(EventBus::GetGlobal(), ended);
	Frame();
	Frame();

	UI::DockManager* mainDock = UISystem::GetDockManager(_main);
	ASSERT_NE(mainDock, nullptr);
	ASSERT_TRUE(mainDock->IsPanelDocked(dockId));

	Handle<Window> again = TearOut();
	ASSERT_TRUE(again.IsValid());
	ExpectChromeTakesClicks(again, Probe(panel), "torn out, re-docked, torn out again");
}

TEST_F(DetachedPanelFixture, ChromeClickableAfterPopup) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	ASSERT_TRUE(panel.IsValid());
	Frame();
	Frame();

	Handle<Window> floating = TearOut();
	ASSERT_TRUE(floating.IsValid());
	UI::Context* context = UISystem::GetContext(floating);
	ASSERT_NE(context, nullptr);
	DetachTestPanel* probe = Probe(panel);

	ClickThroughTheWindow(floating, *context, probe->menuButton);
	ASSERT_EQ(probe->menuClicks, 1u);
	ASSERT_TRUE(context->IsAlive(probe->openedMenu));

	const UI::Rect view = context->GetLayout(probe->viewport).borderBox;
	ASSERT_GT(view.h, 0.0f);
	const u32 scrolledBefore = probe->scrolledClicks;
	ClickPointInWindow(floating, static_cast<i32>(view.x + view.w * 0.5f), static_cast<i32>(view.y + view.h - 4.0f));
	EXPECT_FALSE(context->IsAlive(probe->openedMenu));
	EXPECT_EQ(probe->scrolledClicks, scrolledBefore);

	ExpectChromeTakesClicks(floating, probe, "after a popup was opened and dismissed");
}

TEST_F(DetachedPanelFixture, ClosingADetachedWindowReleasesTheInstance) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	ASSERT_TRUE(panel.IsValid());
	ASSERT_EQ(panel.Get()->GetInstanceSerial(), 1u);
	Frame();
	Frame();

	Handle<Window> floating = TearOut();
	ASSERT_TRUE(floating.IsValid());
	ASSERT_EQ(PanelManager::GetOpenPanelCount(), 1u);

	WindowSystem::RequestDestroy(floating);
	Frame();
	Frame();

	EXPECT_EQ(PanelManager::GetOpenPanelCount(), 0u);
	EXPECT_FALSE(panel.IsValid());

	EXPECT_TRUE(PanelManager::FocusPanelOfType(StringHash("DetachTestPanel")));
	Vector<Handle<EditorPanel>> open{GetMemoryResource(MemTag::Debug)};
	PanelManager::GetOpenPanels(open);
	ASSERT_EQ(open.size(), 1u);
	EXPECT_EQ(open[0].Get()->GetInstanceSerial(), 1u);
	EXPECT_EQ(open[0].Get()->GetTitle(), "Detach Probe");
}

TEST_F(DetachedPanelFixture, ReattachingDetachedWindowKeepsPanelOpen) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	ASSERT_TRUE(panel.IsValid());
	Frame();
	Frame();
	const StringHash dockId = panel.Get()->GetDockId();

	Handle<Window> floating = TearOut();
	ASSERT_TRUE(floating.IsValid());
	UI::DockManager* mainDock = UISystem::GetDockManager(_main);
	ASSERT_NE(mainDock, nullptr);
	ASSERT_FALSE(mainDock->IsPanelDocked(dockId));

	Window* mainResolved = _main.Get();
	ASSERT_NE(mainResolved, nullptr);
	const Vec2I client = mainResolved->GetClientSize();
	const Vec2I centerClient{client.x / 2, client.y / 2};
	ASSERT_EQ(mainDock->GetDropTargetAt({static_cast<f32>(centerClient.x), static_cast<f32>(centerClient.y)}).zone,
			  UI::DockZone::Center);

	WindowDragMovedEvent moved;
	moved.window = floating;
	moved.screenCursorPx = mainResolved->ClientToScreen(centerClient);
	WindowEvents::DragMoved.Publish(EventBus::GetGlobal(), moved);
	WindowDragEndedEvent ended;
	ended.window = floating;
	ended.screenCursorPx = moved.screenCursorPx;
	WindowEvents::DragEnded.Publish(EventBus::GetGlobal(), ended);

	Frame();
	Frame();

	EXPECT_EQ(PanelManager::GetOpenPanelCount(), 1u);
	ASSERT_TRUE(panel.IsValid());
	EXPECT_TRUE(mainDock->IsPanelDocked(dockId));
}

TEST_F(DetachedPanelFixture, EdgeDropDocksBesidePanel) {
	Handle<EditorPanel> left = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	Handle<EditorPanel> top = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	Handle<EditorPanel> under = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	ASSERT_TRUE(left.IsValid() && top.IsValid() && under.IsValid());
	UI::DockManager* mainDock = UISystem::GetDockManager(_main);
	ASSERT_NE(mainDock, nullptr);
	mainDock->DockPanel(top.Get()->GetDockId(), mainDock->GetRootNode(), UI::DockZone::Right);
	mainDock->DockPanel(under.Get()->GetDockId(), mainDock->FindNodeOfPanel(top.Get()->GetDockId()), UI::DockZone::Bottom);
	Frame();
	Frame();
	Vector<UI::DockNodeDesc> before{GetMemoryResource(MemTag::Debug)};
	mainDock->SaveLayout(before);
	ASSERT_EQ(before.size(), 5u);

	Handle<Window> floating = TearOut(2);
	ASSERT_TRUE(floating.IsValid());
	ASSERT_FALSE(mainDock->IsPanelDocked(under.Get()->GetDockId()));

	UI::Context* context = UISystem::GetContext(_main);
	ASSERT_NE(context, nullptr);
	Vector<UI::DockNodeDesc> torn{GetMemoryResource(MemTag::Debug)};
	mainDock->SaveLayout(torn);
	ASSERT_EQ(torn.size(), 3u);
	const u32 topLeaf = mainDock->FindNodeOfPanel(top.Get()->GetDockId());
	Vec2 point{};
	bool found = false;
	for (u32 k = 0; !found; ++k) {
		const UI::ElementHandle leaf = FindByClass(*context, context->GetRoot(), StringHash("dock-leaf"), k);
		if (!context->IsAlive(leaf)) {
			break;
		}
		const UI::Rect box = context->GetLayout(leaf).content;
		const Vec2 candidate{box.x + box.w * 0.5f, box.y + box.h - 30.0f};
		const UI::DockDropTarget hit = mainDock->GetDropTargetAt(candidate);
		if (hit.node == topLeaf && hit.zone == UI::DockZone::Bottom) {
			point = candidate;
			found = true;
		}
	}
	ASSERT_TRUE(found);

	Window* mainResolved = _main.Get();
	ASSERT_NE(mainResolved, nullptr);
	WindowDragMovedEvent moved;
	moved.window = floating;
	moved.screenCursorPx = mainResolved->ClientToScreen({static_cast<i32>(point.x), static_cast<i32>(point.y)});
	WindowEvents::DragMoved.Publish(EventBus::GetGlobal(), moved);
	WindowDragEndedEvent ended;
	ended.window = floating;
	ended.screenCursorPx = moved.screenCursorPx;
	WindowEvents::DragEnded.Publish(EventBus::GetGlobal(), ended);
	Frame();
	Frame();

	Vector<UI::DockNodeDesc> after{GetMemoryResource(MemTag::Debug)};
	mainDock->SaveLayout(after);
	ASSERT_EQ(after.size(), before.size());
	for (size_t i = 0; i < after.size(); ++i) {
		EXPECT_EQ(after[i].split, before[i].split) << "node " << i;
		EXPECT_EQ(after[i].tabs, before[i].tabs) << "node " << i;
	}
}

TEST_F(DetachedPanelFixture, TearAndReattachLeavesNoEmptyLeaf) {
	Handle<EditorPanel> first = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	Handle<EditorPanel> second = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	ASSERT_TRUE(first.IsValid());
	ASSERT_TRUE(second.IsValid());
	UI::DockManager* mainDock = UISystem::GetDockManager(_main);
	ASSERT_NE(mainDock, nullptr);
	mainDock->DockPanel(second.Get()->GetDockId(), mainDock->GetRootNode(), UI::DockZone::Right);
	Frame();
	Frame();

	Window* mainResolved = _main.Get();
	ASSERT_NE(mainResolved, nullptr);
	const Vec2I client = mainResolved->GetClientSize();
	const Vec2I rightEdge{client.x - 30, client.y / 2};

	for (int cycle = 0; cycle < 3; ++cycle) {
		Handle<Window> floating = TearOut(1);
		ASSERT_TRUE(floating.IsValid()) << "cycle " << cycle;
		Vector<UI::DockNodeDesc> torn{GetMemoryResource(MemTag::Debug)};
		mainDock->SaveLayout(torn);
		EXPECT_EQ(torn.size(), 1u) << "cycle " << cycle;

		ASSERT_EQ(mainDock->GetDropTargetAt({static_cast<f32>(rightEdge.x), static_cast<f32>(rightEdge.y)}).zone, UI::DockZone::Right);
		WindowDragMovedEvent moved;
		moved.window = floating;
		moved.screenCursorPx = mainResolved->ClientToScreen(rightEdge);
		WindowEvents::DragMoved.Publish(EventBus::GetGlobal(), moved);
		WindowDragEndedEvent ended;
		ended.window = floating;
		ended.screenCursorPx = moved.screenCursorPx;
		WindowEvents::DragEnded.Publish(EventBus::GetGlobal(), ended);
		Frame();
		Frame();

		Vector<UI::DockNodeDesc> back{GetMemoryResource(MemTag::Debug)};
		mainDock->SaveLayout(back);
		EXPECT_EQ(back.size(), 3u) << "cycle " << cycle;
		for (size_t i = 1; i < back.size(); ++i) {
			EXPECT_FALSE(back[i].split == UI::DockSplit::None && back[i].tabs.empty()) << "cycle " << cycle << ": node " << i;
		}
	}
	EXPECT_EQ(PanelManager::GetOpenPanelCount(), 2u);
}

TEST_F(DetachedPanelFixture, WithoutPolicyEveryWindowBlocksOnDisplay) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	ASSERT_TRUE(panel.IsValid());
	Frame();
	Frame();
	ASSERT_NE(_main.Get(), nullptr);
	EXPECT_TRUE(_main.Get()->GetVSyncHint());

	Handle<Window> floating = TearOut();
	ASSERT_TRUE(floating.IsValid());
	ASSERT_NE(floating.Get(), nullptr);
	EXPECT_TRUE(floating.Get()->GetVSyncHint());
}

TEST_F(DetachedPanelPresentFixture, WindowEditorOpensPresentsWithoutBlocking) {
	ASSERT_NE(_main.Get(), nullptr);
	EXPECT_FALSE(_main.Get()->GetVSyncHint());
}

TEST_F(DetachedPanelPresentFixture, TornOutWindowPresentsUnblocked) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	ASSERT_TRUE(panel.IsValid());
	Frame();
	Frame();

	Handle<Window> floating = TearOut();
	ASSERT_TRUE(floating.IsValid());
	ASSERT_NE(floating.Get(), nullptr);
	EXPECT_FALSE(floating.Get()->GetVSyncHint());

	Handle<EditorPanel> second = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	ASSERT_TRUE(second.IsValid());
	Frame();
	Frame();
	Handle<Window> secondFloating = TearOut(0, ParkedDetach2);
	ASSERT_TRUE(secondFloating.IsValid());
	ASSERT_NE(secondFloating.Get(), nullptr);
	EXPECT_FALSE(secondFloating.Get()->GetVSyncHint());
}

namespace {

	StringHash FocusedIn(Handle<Window> window) {
		const UI::DockManager* dock = UISystem::GetDockManager(window);
		return dock ? dock->GetFocusedPanel() : StringHash{};
	}

} // anonymous namespace

TEST_F(DetachedPanelFixture, PressInsidePanelFocusesThatPanel) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	ASSERT_TRUE(panel.IsValid());
	Frame();
	Frame();
	ASSERT_EQ(FocusedIn(_main), StringHash{});

	UI::Context* context = UISystem::GetContext(_main);
	ASSERT_NE(context, nullptr);
	DetachTestPanel* probe = Probe(panel);
	ASSERT_NE(probe, nullptr);
	ClickThroughTheWindow(_main, *context, probe->chromeButton);

	EXPECT_EQ(FocusedIn(_main), panel.Get()->GetDockId());
	EXPECT_GT(probe->chromeClicks, 0u);
}

TEST_F(DetachedPanelFixture, FocusedPanelHasStripe) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	ASSERT_TRUE(panel.IsValid());
	Frame();
	Frame();
	EXPECT_FALSE(PanelManager::GetFocusedPanel().IsValid());

	UI::Context* context = UISystem::GetContext(_main);
	ASSERT_NE(context, nullptr);
	DetachTestPanel* probe = Probe(panel);
	ASSERT_NE(probe, nullptr);
	ClickThroughTheWindow(_main, *context, probe->chromeButton);

	EXPECT_EQ(PanelManager::GetFocusedPanel(), panel);
	EXPECT_EQ(PanelManager::GetFocusedPanel().Get()->GetDockId(), FocusedIn(_main));

	PanelManager::ClosePanel(panel);
	Frame();
	Frame();
	EXPECT_FALSE(PanelManager::GetFocusedPanel().IsValid());
}

TEST_F(DetachedPanelFixture, OutsidePressKeepsFocus) {
	Handle<EditorPanel> panel = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	ASSERT_TRUE(panel.IsValid());
	Frame();
	Frame();

	UI::Context* context = UISystem::GetContext(_main);
	ASSERT_NE(context, nullptr);
	DetachTestPanel* probe = Probe(panel);
	ClickThroughTheWindow(_main, *context, probe->chromeButton);
	ASSERT_EQ(FocusedIn(_main), panel.Get()->GetDockId());

	UI::ElementHandle bar = FindByClass(*context, context->GetRoot(), StringHash("dock-tab-bar"), 0);
	ASSERT_TRUE(context->IsAlive(bar));
	const UI::Rect barBox = context->GetLayout(bar).borderBox;
	ASSERT_GT(barBox.w, 40.0f);
	ClickPointInWindow(_main, static_cast<i32>(barBox.x + barBox.w - 4.0f),
					   static_cast<i32>(barBox.y + barBox.h * 0.5f));

	EXPECT_EQ(FocusedIn(_main), panel.Get()->GetDockId());
}

TEST_F(DetachedPanelFixture, OnlyOneWindowShowsStripeAtTime) {
	Handle<EditorPanel> stayer = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	Handle<EditorPanel> leaver = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	ASSERT_TRUE(stayer.IsValid());
	ASSERT_TRUE(leaver.IsValid());
	Frame();
	Frame();

	Handle<Window> floating = TearOut();
	ASSERT_TRUE(floating.IsValid());
	Handle<EditorPanel> moved = WindowHolding(stayer) == floating ? stayer : leaver;
	Handle<EditorPanel> stayed = moved == stayer ? leaver : stayer;
	ASSERT_EQ(WindowHolding(moved), floating);
	ASSERT_EQ(WindowHolding(stayed), _main);

	UI::Context* mainContext = UISystem::GetContext(_main);
	UI::Context* floatContext = UISystem::GetContext(floating);
	ASSERT_NE(mainContext, nullptr);
	ASSERT_NE(floatContext, nullptr);

	ClickThroughTheWindow(_main, *mainContext, Probe(stayed)->chromeButton);
	ASSERT_EQ(FocusedIn(_main), stayed.Get()->GetDockId());
	EXPECT_EQ(FocusedIn(floating), stayed.Get()->GetDockId());

	ClickThroughTheWindow(floating, *floatContext, Probe(moved)->chromeButton);
	EXPECT_EQ(FocusedIn(floating), moved.Get()->GetDockId());
	EXPECT_EQ(FocusedIn(_main), moved.Get()->GetDockId());
}

TEST_F(DetachedPanelFixture, ClosingFocusedDropsStripe) {
	Handle<EditorPanel> first = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	ASSERT_TRUE(first.IsValid());
	Frame();
	Frame();

	UI::Context* context = UISystem::GetContext(_main);
	ASSERT_NE(context, nullptr);
	ClickThroughTheWindow(_main, *context, Probe(first)->chromeButton);
	const StringHash dockId = first.Get()->GetDockId();
	ASSERT_EQ(FocusedIn(_main), dockId);

	PanelManager::ClosePanel(first);
	Frame();
	Frame();
	EXPECT_EQ(FocusedIn(_main), StringHash{});

	Handle<EditorPanel> second = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	ASSERT_TRUE(second.IsValid());
	Frame();
	Frame();
	ASSERT_EQ(second.Get()->GetDockId(), dockId);
	EXPECT_EQ(FocusedIn(_main), StringHash{});
}

TEST_F(DetachedPanelFixture, TornOutListDecoratesNewWindow) {
	Handle<EditorPanel> listPanel = PanelManager::OpenPanel(ListDetachTestPanel::GetReflectType(), _main);
	Handle<EditorPanel> stays = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	ASSERT_TRUE(listPanel.IsValid());
	ASSERT_TRUE(stays.IsValid());
	PanelManager::FocusPanel(listPanel);
	for (u32 i = 0; i < 6; ++i) {
		Frame();
	}

	auto* probe = static_cast<ListDetachTestPanel*>(listPanel.Get());
	ASSERT_NE(probe, nullptr);
	ASSERT_FALSE(probe->decorated.empty());

	UI::Context* mainContext = UISystem::GetContext(_main);
	ASSERT_NE(mainContext, nullptr);
	const StringHash after{"probe-after-tearout"};
	probe->decorClass = after;
	probe->decorated.clear();

	UI::DockManager* dock = UISystem::GetDockManager(_main);
	ASSERT_NE(dock, nullptr);
	Handle<Window> floating;
	for (u32 tab = 0; tab < 2 && !floating.IsValid(); ++tab) {
		floating = TearOut(tab);
		if (floating.IsValid() && WindowHolding(listPanel) != floating) {
			floating = {};
			break;
		}
	}
	ASSERT_TRUE(floating.IsValid());

	UI::Context* floatingContext = UISystem::GetContext(floating);
	ASSERT_NE(floatingContext, nullptr);
	ASSERT_FALSE(probe->decorated.empty());
	for (UI::Context* context : probe->decorated) {
		EXPECT_EQ(context, floatingContext);
	}

	EXPECT_GT(CountByClass(*floatingContext, floatingContext->GetRoot(), after), 0u);
	EXPECT_EQ(CountByClass(*mainContext, mainContext->GetRoot(), after), 0u);
}

TEST_F(DetachedPanelFixture, TornOutRowMenuInNewWindow) {
	Handle<EditorPanel> listPanel = PanelManager::OpenPanel(ListDetachTestPanel::GetReflectType(), _main);
	Handle<EditorPanel> stays = PanelManager::OpenPanel(DetachTestPanel::GetReflectType(), _main);
	ASSERT_TRUE(listPanel.IsValid());
	ASSERT_TRUE(stays.IsValid());
	PanelManager::FocusPanel(listPanel);
	for (u32 i = 0; i < 6; ++i) {
		Frame();
	}

	auto* probe = static_cast<ListDetachTestPanel*>(listPanel.Get());
	ASSERT_NE(probe, nullptr);
	UI::Context* mainContext = UISystem::GetContext(_main);
	ASSERT_NE(mainContext, nullptr);

	ASSERT_TRUE(RightClickFirstRow(*mainContext));
	ASSERT_EQ(probe->menuedIn.size(), 1u);
	EXPECT_EQ(probe->menuedIn[0], mainContext);

	probe->menuedIn.clear();
	Handle<Window> floating;
	for (u32 tab = 0; tab < 2 && !floating.IsValid(); ++tab) {
		floating = TearOut(tab);
		if (floating.IsValid() && WindowHolding(listPanel) != floating) {
			floating = {};
			break;
		}
	}
	ASSERT_TRUE(floating.IsValid());
	UI::Context* floatingContext = UISystem::GetContext(floating);
	ASSERT_NE(floatingContext, nullptr);

	ASSERT_TRUE(RightClickFirstRow(*floatingContext));
	ASSERT_EQ(probe->menuedIn.size(), 1u);
	EXPECT_EQ(probe->menuedIn[0], floatingContext);
}

TEST_F(DetachedPanelFixture, FloatingSizePanelOpensWindow) {
	const TypeInfo* type = FloatingProbePanel::GetReflectType();
	Handle<EditorPanel> panel = PanelManager::OpenPanel(type);
	ASSERT_TRUE(panel.IsValid());
	Frame();
	Frame();

	Vector<Handle<Window>> floating{GetMemoryResource(MemTag::Debug)};
	FloatingWindows(floating);
	ASSERT_EQ(floating.size(), 1u);
	EXPECT_EQ(WindowHolding(panel), floating[0]);
	const f32 scale = _main.Get()->GetContentScale();
	const Vec2I client = floating[0].Get()->GetClientSize();
	EXPECT_EQ(client.x, static_cast<i32>(std::lround(300.0f * scale)));
	EXPECT_EQ(client.y, static_cast<i32>(std::lround(200.0f * scale)));

	Handle<EditorPanel> named = PanelManager::OpenPanel(type, _main);
	ASSERT_TRUE(named.IsValid());
	Frame();
	EXPECT_EQ(WindowHolding(named), _main);
	FloatingWindows(floating);
	EXPECT_EQ(floating.size(), 1u);
}
