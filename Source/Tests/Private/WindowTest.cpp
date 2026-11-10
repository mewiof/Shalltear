#include "TestsPCH.h"

#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Input.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/StringUtil.h"
#include "Shalltear/Core/Window.h"

#include "Window/CursorWrap.h"
#include "Window/WindowInternal.h"

#include "TestPlatform.h"
#include "WindowTestSupport.h"

#include <algorithm>
#include <chrono>
#include <string>
#include <thread>
#include <vector>

using namespace Shalltear;
using WindowTestSupport::HiddenDesc;

namespace {

	class WindowEventRecorder : public Object {
	public:
		void SubscribeAll() {
			EventBus& bus = EventBus::GetGlobal();
			Handle<WindowEventRecorder> self(this);
			WindowEvents::Created.Subscribe(bus, self, &WindowEventRecorder::OnCreated);
			WindowEvents::CloseRequested.Subscribe(bus, self, &WindowEventRecorder::OnCloseRequested);
			WindowEvents::Destroying.Subscribe(bus, self, &WindowEventRecorder::OnDestroying);
			WindowEvents::Destroyed.Subscribe(bus, self, &WindowEventRecorder::OnDestroyed);
			WindowEvents::Resized.Subscribe(bus, self, &WindowEventRecorder::OnResized);
		}

		~WindowEventRecorder() override {
			EventBus::GetGlobal().UnsubscribeAll(GetInstanceId());
		}

		void OnCreated(const WindowCreatedEvent& event) {
			createdCount++;
			lastCreated = event.window;
		}
		void OnCloseRequested(const WindowCloseRequestedEvent& event) {
			closeRequestedCount++;
			lastCloseRequested = event.window;
			if (destroyOnCloseRequest) {
				WindowSystem::RequestDestroy(event.window);
			}
		}
		void OnDestroying(const WindowDestroyingEvent& event) {
			sequence.push_back("Destroying");
			destroyingHandleWasValid = event.window.IsValid();
		}
		void OnDestroyed(const WindowDestroyedEvent& event) {
			sequence.push_back("Destroyed");
			lastDestroyedId = event.windowId;
		}
		void OnResized(const WindowResizedEvent& event) {
			resizedCount++;
			lastResize = event.clientSizePx;
			lastResizeLive = event.live;
		}

		int createdCount = 0;
		int closeRequestedCount = 0;
		int resizedCount = 0;
		bool destroyOnCloseRequest = false;
		bool destroyingHandleWasValid = false;
		u64 lastDestroyedId = 0;
		Handle<Window> lastCreated;
		Handle<Window> lastCloseRequested;
		Vec2I lastResize;
		bool lastResizeLive = true;
		std::vector<str> sequence;
	};

} // anonymous namespace

TEST_F(WindowTest, CreateQueryDestroyRoundTrip) {
	auto recorder = MakeShared<WindowEventRecorder>();
	recorder->SubscribeAll();

	WindowDesc desc = HiddenDesc({320, 240});
	desc.position = Vec2I{120, 140};
	Handle<Window> window = WindowSystem::Create(desc);
	ASSERT_TRUE(window.IsValid());
	EXPECT_EQ(WindowSystem::GetWindowCount(), 1u);
	EXPECT_EQ(recorder->createdCount, 1);
	EXPECT_EQ(recorder->lastCreated, window);

	EXPECT_FALSE(window->IsVisible());
	EXPECT_EQ(window->GetClientSize(), Vec2I(320, 240));
	EXPECT_EQ(window->GetPosition(), Vec2I(120, 140));
	EXPECT_GT(window->GetContentScale(), 0.0f);
	EXPECT_EQ(window->GetTitle(), "WindowTest");
	window->SetTitle("Renamed");
	EXPECT_EQ(window->GetTitle(), "Renamed");

	NativeWindowHandle native = window->GetNativeHandle();
	EXPECT_NE(native.window, nullptr);
	EXPECT_NE(native.display, nullptr);

	Vec2I probe{17, 23};
	EXPECT_EQ(window->ScreenToClient(window->ClientToScreen(probe)), probe);

	u64 id = window.GetInstanceId();
	WindowSystem::RequestDestroy(window);
	EXPECT_TRUE(window.IsValid());
	WindowSystem::PumpEvents();
	EXPECT_FALSE(window.IsValid());
	EXPECT_EQ(WindowSystem::GetWindowCount(), 0u);
	EXPECT_EQ(recorder->lastDestroyedId, id);
}

TEST_F(WindowTest, DestructionEventOrder) {
	auto recorder = MakeShared<WindowEventRecorder>();
	recorder->SubscribeAll();

	Handle<Window> window = WindowSystem::Create(HiddenDesc());
	ASSERT_TRUE(window.IsValid());
	WindowSystem::RequestDestroy(window);
	WindowSystem::PumpEvents();

	ASSERT_EQ(recorder->sequence.size(), 2u);
	EXPECT_EQ(recorder->sequence[0], "Destroying");
	EXPECT_EQ(recorder->sequence[1], "Destroyed");
	EXPECT_TRUE(recorder->destroyingHandleWasValid);
}

TEST_F(WindowTest, PostedCloseBecomesCloseRequestedEvent) {
	auto recorder = MakeShared<WindowEventRecorder>();
	recorder->SubscribeAll();

	Handle<Window> window = WindowSystem::Create(HiddenDesc());
	ASSERT_TRUE(window.IsValid());

	TestPlatform::PostClose(window);
	WindowSystem::PumpEvents();

	EXPECT_EQ(recorder->closeRequestedCount, 1);
	EXPECT_EQ(recorder->lastCloseRequested, window);
	EXPECT_TRUE(window.IsValid());
	EXPECT_EQ(WindowSystem::GetWindowCount(), 1u);
}

TEST_F(WindowTest, DestroyFromInsideCallbackIsSafe) {
	auto recorder = MakeShared<WindowEventRecorder>();
	recorder->SubscribeAll();
	recorder->destroyOnCloseRequest = true;

	Handle<Window> window = WindowSystem::Create(HiddenDesc());
	TestPlatform::PostClose(window);
	WindowSystem::PumpEvents();
	EXPECT_TRUE(window.IsValid());
	WindowSystem::PumpEvents();
	EXPECT_FALSE(window.IsValid());
	ASSERT_EQ(recorder->sequence.size(), 2u);
	EXPECT_EQ(recorder->sequence[0], "Destroying");
	EXPECT_EQ(recorder->sequence[1], "Destroyed");
}

TEST_F(WindowTest, SetClientSizePublishesResizedEvent) {
	auto recorder = MakeShared<WindowEventRecorder>();
	recorder->SubscribeAll();

	Handle<Window> window = WindowSystem::Create(HiddenDesc({320, 240}));
	ASSERT_TRUE(window.IsValid());

	window->SetClientSize({640, 480});
	WindowSystem::PumpEvents();

	EXPECT_EQ(window->GetClientSize(), Vec2I(640, 480));
	EXPECT_GE(recorder->resizedCount, 1);
	EXPECT_EQ(recorder->lastResize, Vec2I(640, 480));
	EXPECT_FALSE(recorder->lastResizeLive);
}

TEST_F(WindowTest, SetPositionMoves) {
	Handle<Window> window = WindowSystem::Create(HiddenDesc());
	ASSERT_TRUE(window.IsValid());
	window->SetPosition({260, 310});
	EXPECT_EQ(window->GetPosition(), Vec2I(260, 310));
	RectI rect = window->GetRect();
	EXPECT_EQ(rect.position, Vec2I(260, 310));
	EXPECT_GT(rect.size.x, 0);
	EXPECT_GT(rect.size.y, 0);
}

TEST_F(WindowTest, PlacementRoundTripsThroughTheRestoredRect) {
	Handle<Window> window = WindowSystem::Create(HiddenDesc({320, 240}));
	ASSERT_TRUE(window.IsValid());

	WindowPlacement placement;
	placement.position = {180, 220};
	placement.clientSize = {700, 500};
	window->SetPlacement(placement);

	WindowPlacement read = window->GetPlacement();
	EXPECT_EQ(read.position, placement.position);
	EXPECT_EQ(read.clientSize, placement.clientSize);
	EXPECT_FALSE(window->IsVisible());
	EXPECT_EQ(read.state, WindowState::Normal);
}

TEST_F(WindowTest, PlacementMatchesTheLiveRectWhileNormal) {
	Handle<Window> window = WindowSystem::Create(HiddenDesc({320, 240}));
	ASSERT_TRUE(window.IsValid());
	window->SetPosition({240, 260});
	window->SetClientSize({640, 480});

	WindowPlacement placement = window->GetPlacement();
	EXPECT_EQ(placement.position, window->GetPosition());
	EXPECT_EQ(placement.clientSize, window->GetClientSize());
}

TEST_F(WindowTest, PlacementSurvivesMinimize) {
	WindowDesc desc = HiddenDesc({640, 480});
	desc.position = Vec2I{200, 180};
	Handle<Window> window = WindowSystem::Create(desc);
	ASSERT_TRUE(window.IsValid());

	window->Show();
	window->Minimize();
	WindowSystem::PumpEvents();
	ASSERT_TRUE(window->IsMinimized());

	WindowPlacement placement = window->GetPlacement();
	EXPECT_EQ(placement.position, Vec2I(200, 180));
	EXPECT_EQ(placement.clientSize, Vec2I(640, 480));
	EXPECT_EQ(placement.state, WindowState::Minimized);
}

TEST_F(WindowTest, PlacementReportsTheRestoredRectWhileMaximized) {
	WindowDesc desc = HiddenDesc({640, 480});
	desc.position = Vec2I{220, 160};
	Handle<Window> window = WindowSystem::Create(desc);
	ASSERT_TRUE(window.IsValid());

	window->Show();
	window->Maximize();
	WindowSystem::PumpEvents();
	ASSERT_TRUE(window->IsMaximized());

	WindowPlacement placement = window->GetPlacement();
	EXPECT_EQ(placement.state, WindowState::Maximized);
	EXPECT_EQ(placement.position, Vec2I(220, 160));
	EXPECT_EQ(placement.clientSize, Vec2I(640, 480));
	EXPECT_GT(window->GetClientSize().x, placement.clientSize.x);
}

TEST_F(WindowTest, MaximizedFillsWorkArea) {
	std::span<const MonitorInfo> monitors = WindowSystem::GetMonitors();
	ASSERT_FALSE(monitors.empty());
	const MonitorInfo& monitor = monitors[0];
	ASSERT_GT(monitor.workArea.size.y, 0);
	if (monitor.workArea.size.y >= monitor.bounds.size.y) {
		GTEST_SKIP() << "no reserved taskbar space on this display";
	}

	for (WindowFlags maximizeStyle : {WindowFlags::NoMaximize, WindowFlags::None}) {
		const bool noMaximize = maximizeStyle == WindowFlags::NoMaximize;
		SCOPED_TRACE(noMaximize ? "NoMaximize (floating dock panel)" : "main window");

		WindowDesc desc = HiddenDesc({560, 420});
		desc.flags = WindowFlags::Resizable | WindowFlags::Borderless | maximizeStyle;
		Handle<Window> window = WindowSystem::Create(desc);
		ASSERT_TRUE(window.IsValid());

		window->Show();
		window->Maximize();
		WindowSystem::PumpEvents();
		ASSERT_TRUE(window->IsMaximized());

		Vec2I client = window->GetClientSize();
		EXPECT_EQ(client.y, monitor.workArea.size.y);
		EXPECT_EQ(client.x, monitor.workArea.size.x);
		EXPECT_GT(client.y, 420);

		window->Restore();
		WindowSystem::PumpEvents();
		EXPECT_FALSE(window->IsMaximized());
		WindowSystem::RequestDestroy(window);
		WindowSystem::PumpEvents();
	}
}

TEST_F(WindowTest, PlacementKeepsRestoredRect) {
	Handle<Window> window = WindowSystem::Create(HiddenDesc({320, 240}));
	ASSERT_TRUE(window.IsValid());
	window->Show();

	WindowPlacement placement;
	placement.position = {260, 200};
	placement.clientSize = {800, 600};
	placement.state = WindowState::Maximized;
	window->SetPlacement(placement);
	WindowSystem::PumpEvents();

	EXPECT_TRUE(window->IsMaximized());
	WindowPlacement read = window->GetPlacement();
	EXPECT_EQ(read.state, WindowState::Maximized);
	EXPECT_EQ(read.position, placement.position);
	EXPECT_EQ(read.clientSize, placement.clientSize);

	window->Restore();
	WindowSystem::PumpEvents();
	EXPECT_EQ(window->GetPosition(), placement.position);
	EXPECT_EQ(window->GetClientSize(), placement.clientSize);
}

namespace {

	class WindowStateRecorder : public Object {
	public:
		void Subscribe() {
			WindowEvents::StateChanged.Subscribe(EventBus::GetGlobal(), Handle<WindowStateRecorder>(this),
												 &WindowStateRecorder::OnStateChanged);
		}
		~WindowStateRecorder() override { EventBus::GetGlobal().UnsubscribeAll(GetInstanceId()); }
		void OnStateChanged(const WindowStateEvent& event) { states.push_back(event.state); }
		std::vector<WindowState> states;
	};

	void PlaceFullscreen(const Handle<Window>& window, bool fullscreen) {
		WindowPlacement placement = window->GetPlacement();
		placement.state = fullscreen ? WindowState::Fullscreen : WindowState::Normal;
		window->SetPlacement(placement);
		WindowSystem::PumpEvents();
	}

} // namespace

TEST_F(WindowTest, FullscreenRoundTrips) {
	WindowDesc desc = HiddenDesc({640, 480});
	desc.position = Vec2I{200, 180};
	Handle<Window> window = WindowSystem::Create(desc);
	ASSERT_TRUE(window.IsValid());
	const RectI monitor = window->GetMonitor().bounds;
	ASSERT_GT(monitor.size.x, 640);

	WindowEventRecorder resizes;
	resizes.SubscribeAll();
	WindowStateRecorder states;
	states.Subscribe();

	PlaceFullscreen(window, true);
	EXPECT_EQ(window->GetRect().position, monitor.position);
	EXPECT_EQ(window->GetClientSize(), monitor.size);
	EXPECT_EQ(resizes.lastResize, monitor.size);

	WindowPlacement placement = window->GetPlacement();
	EXPECT_EQ(placement.state, WindowState::Fullscreen);
	EXPECT_EQ(placement.position, Vec2I(200, 180));
	EXPECT_EQ(placement.clientSize, Vec2I(640, 480));

	PlaceFullscreen(window, false);
	EXPECT_EQ(window->GetPosition(), Vec2I(200, 180));
	EXPECT_EQ(window->GetClientSize(), Vec2I(640, 480));
	EXPECT_EQ(window->GetPlacement().state, WindowState::Normal);

	ASSERT_EQ(states.states.size(), 2u);
	EXPECT_EQ(states.states[0], WindowState::Fullscreen);
	EXPECT_EQ(states.states[1], WindowState::Normal);
}

TEST_F(WindowTest, FullscreenWindowIgnoresWindowedMaximumSize) {
	WindowDesc desc = HiddenDesc({400, 300});
	desc.maxClientSize = {640, 480};
	Handle<Window> window = WindowSystem::Create(desc);
	ASSERT_TRUE(window.IsValid());
	const RectI monitor = window->GetMonitor().bounds;
	ASSERT_GT(monitor.size.x, 640);

	PlaceFullscreen(window, true);
	EXPECT_EQ(window->GetClientSize(), monitor.size);
}

TEST_F(WindowTest, MaximizedFullscreenRoundTrips) {
	Handle<Window> window = WindowSystem::Create(HiddenDesc({640, 480}));
	ASSERT_TRUE(window.IsValid());
	window->Show();
	window->Maximize();
	WindowSystem::PumpEvents();
	ASSERT_TRUE(window->IsMaximized());
	const MonitorInfo& monitor = window->GetMonitor();

	PlaceFullscreen(window, true);
	EXPECT_FALSE(window->IsMaximized());
	EXPECT_EQ(window->GetClientSize(), monitor.bounds.size);
	EXPECT_EQ(window->GetPlacement().state, WindowState::Fullscreen);

	WindowPlacement maximized = window->GetPlacement();
	maximized.state = WindowState::Maximized;
	window->SetPlacement(maximized);
	WindowSystem::PumpEvents();
	EXPECT_TRUE(window->IsMaximized());
	EXPECT_EQ(window->GetPlacement().state, WindowState::Maximized);
	EXPECT_LE(window->GetClientSize().y, monitor.workArea.size.y);

	PlaceFullscreen(window, true);
	window->Maximize();
	WindowSystem::PumpEvents();
	EXPECT_EQ(window->GetPlacement().state, WindowState::Maximized);
	EXPECT_TRUE(window->IsMaximized());
	EXPECT_LE(window->GetClientSize().y, monitor.workArea.size.y);
}

TEST_F(WindowTest, AFullscreenPlacementRemembersItsWindowedRect) {
	Handle<Window> window = WindowSystem::Create(HiddenDesc({320, 240}));
	ASSERT_TRUE(window.IsValid());
	const RectI monitor = window->GetMonitor().bounds;

	WindowPlacement placement;
	placement.position = {300, 200};
	placement.clientSize = {800, 600};
	placement.state = WindowState::Fullscreen;
	window->SetPlacement(placement);
	WindowSystem::PumpEvents();
	EXPECT_EQ(window->GetClientSize(), monitor.size);
	WindowPlacement read = window->GetPlacement();
	EXPECT_EQ(read.state, WindowState::Fullscreen);
	EXPECT_EQ(read.position, placement.position);
	EXPECT_EQ(read.clientSize, placement.clientSize);

	placement.position = {120, 90};
	placement.clientSize = {500, 400};
	placement.state = WindowState::Normal;
	window->SetPlacement(placement);
	WindowSystem::PumpEvents();
	EXPECT_EQ(window->GetPlacement().state, WindowState::Normal);
	EXPECT_EQ(window->GetPosition(), placement.position);
	EXPECT_EQ(window->GetClientSize(), placement.clientSize);
}

TEST(WindowLetterboxTest, LetterboxKeepsAspect) {
	using WindowInternal::LetterboxRect;
	EXPECT_EQ(LetterboxRect({1920, 1080}, {1280, 720}), RectI(0, 0, 1920, 1080));
	EXPECT_EQ(LetterboxRect({1920, 1080}, {1024, 768}), RectI(240, 0, 1440, 1080));
	EXPECT_EQ(LetterboxRect({1920, 1200}, {1920, 1080}), RectI(0, 60, 1920, 1080));
	EXPECT_EQ(LetterboxRect({800, 600}, {0, 0}), RectI(0, 0, 800, 600));
}

TEST(WindowLetterboxTest, PointsMapIntoContent) {
	using WindowInternal::MapIntoContent;
	const Vec2I target{1920, 1080};
	const Vec2I content{1024, 768};
	EXPECT_EQ(MapIntoContent({240, 0}, target, content), Vec2I(0, 0));
	EXPECT_EQ(MapIntoContent({960, 540}, target, content), Vec2I(512, 384));
	EXPECT_EQ(MapIntoContent({1679, 1079}, target, content), Vec2I(1023, 767));
	EXPECT_LT(MapIntoContent({100, 500}, target, content).x, 0);
	EXPECT_GE(MapIntoContent({1800, 500}, target, content).x, 1024);
}

TEST_F(WindowTest, RenderSizeDefaultsToClient) {
	Handle<Window> window = WindowSystem::Create(HiddenDesc({400, 300}));
	ASSERT_TRUE(window.IsValid());
	EXPECT_EQ(WindowInternal::GetRenderSize(*window.Get()), Vec2I(400, 300));
	EXPECT_EQ(WindowInternal::ClientToRender(*window.Get(), {10, 20}), Vec2I(10, 20));

	WindowInternal::SetRenderSize(*window.Get(), {200, 150});
	EXPECT_EQ(WindowInternal::GetRenderSize(*window.Get()), Vec2I(200, 150));
	EXPECT_EQ(WindowInternal::ClientToRender(*window.Get(), {10, 20}), Vec2I(5, 10));
	EXPECT_FLOAT_EQ(WindowInternal::RenderScaleOf(*window.Get()), 0.5f);

	WindowInternal::SetRenderSize(*window.Get(), {});
	EXPECT_EQ(WindowInternal::GetRenderSize(*window.Get()), Vec2I(400, 300));
	EXPECT_FLOAT_EQ(WindowInternal::RenderScaleOf(*window.Get()), 1.0f);
}

TEST_F(WindowTest, MultipleWindowsTrackCountAndOrder) {
	Handle<Window> first = WindowSystem::Create(HiddenDesc());
	Handle<Window> second = WindowSystem::Create(HiddenDesc());
	ASSERT_TRUE(first.IsValid());
	ASSERT_TRUE(second.IsValid());
	EXPECT_EQ(WindowSystem::GetWindowCount(), 2u);

	WindowSystem::RequestDestroy(first);
	WindowSystem::PumpEvents();
	EXPECT_FALSE(first.IsValid());
	EXPECT_TRUE(second.IsValid());
	EXPECT_EQ(WindowSystem::GetWindowCount(), 1u);
}

TEST_F(WindowTest, PrimaryIsOldest) {
	EXPECT_FALSE(WindowSystem::GetPrimaryWindow().IsValid());

	Handle<Window> first = WindowSystem::Create(HiddenDesc());
	Handle<Window> second = WindowSystem::Create(HiddenDesc());
	ASSERT_TRUE(first.IsValid());
	ASSERT_TRUE(second.IsValid());
	EXPECT_EQ(WindowSystem::GetPrimaryWindow(), first);

	EXPECT_FALSE(WindowSystem::GetFocusedWindow().IsValid());

	WindowSystem::RequestDestroy(first);
	WindowSystem::PumpEvents();
	EXPECT_FALSE(first.IsValid());
	EXPECT_EQ(WindowSystem::GetPrimaryWindow(), second);

	WindowSystem::RequestDestroy(second);
	WindowSystem::PumpEvents();
	EXPECT_FALSE(WindowSystem::GetPrimaryWindow().IsValid());
}

TEST_F(WindowTest, HiddenWindowIsNotAHitTarget) {
	Handle<Window> window = WindowSystem::Create(HiddenDesc());
	ASSERT_TRUE(window.IsValid());
	Vec2I center = window->ClientToScreen(window->GetClientSize() / 2);
	EXPECT_FALSE(WindowSystem::GetWindowAtPoint(center).IsValid());
}

TEST_F(WindowTest, MonitorsEnumerate) {
	std::span<const MonitorInfo> monitors = WindowSystem::GetMonitors();
	if (monitors.empty()) {
		GTEST_SKIP() << "no monitors visible";
	}
	bool anyPrimary = false;
	bool anyRate = false;
	for (const MonitorInfo& monitor : monitors) {
		EXPECT_GT(monitor.bounds.size.x, 0);
		EXPECT_GT(monitor.bounds.size.y, 0);
		EXPECT_GT(monitor.scale, 0.0f);
		EXPECT_NE(monitor.id, 0u);
		EXPECT_TRUE(monitor.refreshRate == 0.0f || monitor.refreshRate >= 20.0f) << monitor.refreshRate;
		anyPrimary |= monitor.primary;
		anyRate |= monitor.refreshRate > 0.0f;
	}
	EXPECT_TRUE(anyPrimary);
	if (TestPlatform::MonitorsAlwaysReportRefreshRate()) {
		EXPECT_TRUE(anyRate);
	}

	Handle<Window> window = WindowSystem::Create(HiddenDesc());
	ASSERT_TRUE(window.IsValid());
	const MonitorInfo& hosting = window->GetMonitor();
	EXPECT_GT(hosting.bounds.size.x, 0);
}

TEST_F(WindowTest, ClipboardTextRoundTrip) {
	const str payload = "Shalltear clipboard \xE3\x83\x86\xE3\x82\xB9\xE3\x83\x88";
	bool wrote = WindowSystem::SetClipboardText(payload);
	if (!wrote) {
		wrote = WindowSystem::SetClipboardText(payload);
	}
	if (!wrote) {
		GTEST_SKIP() << "clipboard busy";
	}
	str readBack = WindowSystem::GetClipboardText();
	if (readBack != payload) {
		readBack = WindowSystem::GetClipboardText();
	}
	if (readBack.empty()) {
		GTEST_SKIP() << "clipboard contention";
	}
	EXPECT_EQ(readBack, payload);
}

namespace {

	constexpr RectI SecondMonitor{-1920, 200, 1920, 1080};
	constexpr i32 SECOND_RIGHT = SecondMonitor.position.x + SecondMonitor.size.x - 1;
	constexpr i32 SECOND_BOTTOM = SecondMonitor.position.y + SecondMonitor.size.y - 1;

} // namespace

TEST(CursorWrapTest, MiddleOfMonitorIsNotEdge) {
	Vec2I middle{-960, 740};
	EXPECT_EQ(WindowInternal::CursorWrapLanding(middle, SecondMonitor), middle);
}

TEST(CursorWrapTest, EachEdgeLandsInsideTheOppositeOne) {
	using WindowInternal::CURSOR_WRAP_LANDING_PX;
	using WindowInternal::CursorWrapLanding;

	Vec2I fromLeft = CursorWrapLanding({SecondMonitor.position.x, 740}, SecondMonitor);
	EXPECT_EQ(fromLeft.x, SECOND_RIGHT - CURSOR_WRAP_LANDING_PX);
	EXPECT_EQ(fromLeft.y, 740);

	Vec2I fromRight = CursorWrapLanding({SECOND_RIGHT, 740}, SecondMonitor);
	EXPECT_EQ(fromRight.x, SecondMonitor.position.x + CURSOR_WRAP_LANDING_PX);

	Vec2I fromTop = CursorWrapLanding({-960, SecondMonitor.position.y}, SecondMonitor);
	EXPECT_EQ(fromTop.y, SECOND_BOTTOM - CURSOR_WRAP_LANDING_PX);
	EXPECT_EQ(fromTop.x, -960);

	Vec2I fromBottom = CursorWrapLanding({-960, SECOND_BOTTOM}, SecondMonitor);
	EXPECT_EQ(fromBottom.y, SecondMonitor.position.y + CURSOR_WRAP_LANDING_PX);
}

TEST(CursorWrapTest, LandingDoesNotRetrigger) {
	using WindowInternal::CursorWrapLanding;
	for (Vec2I edge : {Vec2I{SecondMonitor.position.x, 740}, Vec2I{SECOND_RIGHT, 740},
					   Vec2I{-960, SecondMonitor.position.y}, Vec2I{-960, SECOND_BOTTOM}}) {
		Vec2I landing = CursorWrapLanding(edge, SecondMonitor);
		ASSERT_NE(landing, edge);
		EXPECT_EQ(CursorWrapLanding(landing, SecondMonitor), landing);
	}
}

TEST(CursorWrapTest, ACornerWrapsBothAxes) {
	Vec2I landing = WindowInternal::CursorWrapLanding({SECOND_RIGHT, SECOND_BOTTOM}, SecondMonitor);
	EXPECT_EQ(landing.x, SecondMonitor.position.x + WindowInternal::CURSOR_WRAP_LANDING_PX);
	EXPECT_EQ(landing.y, SecondMonitor.position.y + WindowInternal::CURSOR_WRAP_LANDING_PX);
}

TEST(CursorWrapTest, TinyMonitorDeclinesWrap) {
	using WindowInternal::CURSOR_WRAP_LANDING_PX;
	using WindowInternal::CURSOR_WRAP_MARGIN_PX;
	using WindowInternal::CursorWrapLanding;

	constexpr i32 SMALLEST = CURSOR_WRAP_MARGIN_PX + CURSOR_WRAP_LANDING_PX + 2;
	RectI tooSmall{0, 0, SMALLEST - 1, SMALLEST - 1};
	Vec2I edge{0, 3};
	EXPECT_EQ(CursorWrapLanding(edge, tooSmall), edge);
	EXPECT_EQ(CursorWrapLanding({0, 0}, RectI{0, 0, 0, 0}), (Vec2I{0, 0}));

	RectI smallest{0, 0, SMALLEST, SMALLEST};
	Vec2I landing = CursorWrapLanding({0, 0}, smallest);
	EXPECT_NE(landing, (Vec2I{0, 0}));
	EXPECT_EQ(CursorWrapLanding(landing, smallest), landing);
}

TEST_F(WindowTest, WrappedIsVisibleModeThatRoundTrips) {
	Handle<Window> window = WindowSystem::Create(HiddenDesc());
	ASSERT_TRUE(window.IsValid());
	Window* ptr = window.Get();
	ASSERT_NE(ptr, nullptr);
	EXPECT_EQ(ptr->GetCursorMode(), CursorMode::Normal);

	ptr->SetCursor(SystemCursor::ResizeEW);
	ptr->SetCursorMode(CursorMode::Wrapped);
	EXPECT_EQ(ptr->GetCursorMode(), CursorMode::Wrapped);
	EXPECT_EQ(ptr->GetCursor(), SystemCursor::ResizeEW);
	ptr->SetCursor(SystemCursor::Hand);
	EXPECT_EQ(ptr->GetCursor(), SystemCursor::Hand);

	ptr->SetCursorMode(CursorMode::Normal);
	EXPECT_EQ(ptr->GetCursorMode(), CursorMode::Normal);
}

TEST_F(WindowTest, DestroyedWhileWrappedKeepsPointer) {
	Handle<Window> window = WindowSystem::Create(HiddenDesc());
	ASSERT_TRUE(window.IsValid());
	window.Get()->SetCursorMode(CursorMode::Wrapped);
	Vec2I before = Input::GetMousePosition();

	WindowSystem::RequestDestroy(window);
	WindowSystem::PumpEvents();
	EXPECT_FALSE(window.IsValid());
	EXPECT_EQ(Input::GetMousePosition(), before);
}

TEST_F(WindowTest, TerminateDestroysStragglersInReverseOrder) {
	auto recorder = MakeShared<WindowEventRecorder>();
	recorder->SubscribeAll();

	Handle<Window> first = WindowSystem::Create(HiddenDesc());
	Handle<Window> second = WindowSystem::Create(HiddenDesc());
	u64 firstId = first.GetInstanceId();
	u64 secondId = second.GetInstanceId();

	WindowSystem::Terminate();

	EXPECT_FALSE(first.IsValid());
	EXPECT_FALSE(second.IsValid());
	ASSERT_EQ(recorder->sequence.size(), 4u);
	EXPECT_EQ(recorder->lastDestroyedId, firstId);
	(void)secondId;
}

TEST_F(WindowTest, TerminateLeavesNoPlatformBytes) {
	size_t baseline = MemoryManager::GetBytesForTag(MemTag::Platform);

	Handle<Window> window = WindowSystem::Create(HiddenDesc());
	ASSERT_TRUE(window.IsValid());
	EXPECT_GT(MemoryManager::GetBytesForTag(MemTag::Platform), baseline);

	WindowSystem::Terminate();
	EXPECT_LE(MemoryManager::GetBytesForTag(MemTag::Platform), baseline);
}

namespace {

	f64 SecondsSince(std::chrono::steady_clock::time_point start) {
		return std::chrono::duration<f64>(std::chrono::steady_clock::now() - start).count();
	}

	f64 MedianWait(f64 timeoutSeconds, int count) {
		std::vector<f64> elapsed;
		for (int i = 0; i < count; ++i) {
			auto start = std::chrono::steady_clock::now();
			WindowSystem::WaitEvents(timeoutSeconds);
			elapsed.push_back(SecondsSince(start));
		}
		std::sort(elapsed.begin(), elapsed.end());
		return elapsed[elapsed.size() / 2];
	}

} // anonymous namespace

TEST_F(WindowTest, WaitShorterThanSchedulerTickIsHonoured) {
	f64 median = MedianWait(0.004, 21);
	EXPECT_GE(median, 0.0035);
	EXPECT_LT(median, 0.010);
}

TEST_F(WindowTest, QueuedMessageEndsWaitAtOnce) {
	Handle<Window> window = WindowSystem::Create(HiddenDesc());
	ASSERT_TRUE(window.IsValid());
	WindowSystem::PumpEvents();

	TestPlatform::PostWake(window);
	auto start = std::chrono::steady_clock::now();
	WindowSystem::WaitEvents(1.0);
	EXPECT_LT(SecondsSince(start), 0.1);
}

TEST_F(WindowTest, EarlyWakeKeepsNextWait) {
	Handle<Window> window = WindowSystem::Create(HiddenDesc());
	ASSERT_TRUE(window.IsValid());
	WindowSystem::PumpEvents();

	TestPlatform::PostWake(window);
	WindowSystem::WaitEvents(0.010);
	std::this_thread::sleep_for(std::chrono::milliseconds(40));
	auto start = std::chrono::steady_clock::now();
	WindowSystem::WaitEvents(0.020);
	EXPECT_GE(SecondsSince(start), 0.015);
}
