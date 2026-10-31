#include "TestsPCH.h"

#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Window.h"

#include "Window/Win32/Win32Internal.h"

#include <string>
#include <vector>

using namespace Shalltear;

namespace {

	class DragRecorder : public Object {
	public:
		void SubscribeAll() {
			EventBus& bus = EventBus::GetGlobal();
			Handle<DragRecorder> self(this);
			WindowEvents::DragBegan.Subscribe(bus, self, &DragRecorder::OnBegan);
			WindowEvents::DragMoved.Subscribe(bus, self, &DragRecorder::OnMoved);
			WindowEvents::DragEnded.Subscribe(bus, self, &DragRecorder::OnEnded);
		}

		~DragRecorder() override { EventBus::GetGlobal().UnsubscribeAll(GetInstanceId()); }

		void OnBegan(const WindowDragBeganEvent&) { order.push_back('B'); }

		void OnMoved(const WindowDragMovedEvent& event) {
			order.push_back('M');
			moves.push_back(event.windowRect);
		}

		void OnEnded(const WindowDragEndedEvent&) { order.push_back('E'); }

		std::string Order() const { return std::string(order.begin(), order.end()); }

		std::vector<char> order;
		std::vector<RectI> moves;
	};

} // namespace

class WindowDragCostTest : public ::testing::Test {
protected:
	void SetUp() override {
		WindowSystem::Initialize();
		_recorder = MakeShared<DragRecorder>();
		WindowDesc desc;
		desc.title = "WindowDragCostTest";
		desc.clientSize = {320, 240};
		desc.flags = WindowFlags::Resizable | WindowFlags::Hidden;
		_window = WindowSystem::Create(desc);
		ASSERT_TRUE(_window.IsValid());
		_hwnd = static_cast<HWND>(_window->GetNativeHandle().hwnd);
		ASSERT_NE(_hwnd, nullptr);
		_recorder->SubscribeAll();
	}

	void TearDown() override {
		if (_window.IsValid()) {
			::SendMessageW(_hwnd, WM_EXITSIZEMOVE, 0, 0);
			WindowSystem::RequestDestroy(_window);
			WindowSystem::PumpEvents();
			WindowSystem::PumpEvents();
		}
		_recorder.reset();
		WindowSystem::Terminate();
	}

	void EnterDrag() { ::SendMessageW(_hwnd, WM_ENTERSIZEMOVE, 0, 0); }
	void ExitDrag() { ::SendMessageW(_hwnd, WM_EXITSIZEMOVE, 0, 0); }

	void Move(i32 x, i32 y) {
		RECT rect{x, y, x + 320, y + 240};
		::SendMessageW(_hwnd, WM_MOVING, WMSZ_BOTTOMRIGHT, reinterpret_cast<LPARAM>(&rect));
	}

	void Tick() { ::SendMessageW(_hwnd, WM_TIMER, Win32::MODAL_TICK_TIMER_ID, 0); }

	Handle<Window> _window;
	HWND _hwnd = nullptr;
	SharedPtr<DragRecorder> _recorder;
};

TEST_F(WindowDragCostTest, TickDeliversOneMove) {
	for (i32 movesPerTick : {1, 8, 64}) {
		_recorder->moves.clear();
		_recorder->order.clear();
		EnterDrag();
		for (i32 i = 0; i < movesPerTick; ++i) {
			Move(100 + i, 200 + i);
		}
		Tick();
		EXPECT_EQ(_recorder->moves.size(), 1u);
		ExitDrag();
	}
}

TEST_F(WindowDragCostTest, TickPublishesNewestMoveAndNotOldest) {
	EnterDrag();
	Move(100, 100);
	Move(200, 200);
	Move(345, 678);
	Tick();

	ASSERT_EQ(_recorder->moves.size(), 1u);
	EXPECT_EQ(_recorder->moves[0].position, Vec2I(345, 678));
	EXPECT_EQ(_recorder->moves[0].size, Vec2I(320, 240));
}

TEST_F(WindowDragCostTest, LastMoveBeforeDragEnds) {
	EnterDrag();
	Move(100, 100);
	Tick();
	Move(900, 950);
	ExitDrag();

	EXPECT_EQ(_recorder->Order(), "BMME");
	ASSERT_EQ(_recorder->moves.size(), 2u);
	EXPECT_EQ(_recorder->moves[1].position, Vec2I(900, 950));
}

TEST_F(WindowDragCostTest, TickWithNoNewMoveDeliversNothing) {
	EnterDrag();
	Move(100, 100);
	Tick();
	ASSERT_EQ(_recorder->moves.size(), 1u);

	Tick();
	Tick();
	Tick();
	EXPECT_EQ(_recorder->moves.size(), 1u);

	ExitDrag();
	EXPECT_EQ(_recorder->Order(), "BME");
}

TEST_F(WindowDragCostTest, NonModalMoveDeliveredAtOnce) {
	Move(500, 500);

	ASSERT_EQ(_recorder->moves.size(), 1u);
	EXPECT_EQ(_recorder->moves[0].position, Vec2I(500, 500));
}

TEST_F(WindowDragCostTest, DeadWindowGetsNoSample) {
	EnterDrag();
	Move(400, 400);
	ASSERT_EQ(_recorder->moves.size(), 0u);

	WindowSystem::RequestDestroy(_window);
	WindowSystem::PumpEvents();
	WindowSystem::PumpEvents();
	_window = {};

	EXPECT_EQ(_recorder->moves.size(), 0u);
}

TEST_F(WindowDragCostTest, SecondDragStartsClean) {
	EnterDrag();
	Move(100, 100);
	Tick();
	Move(700, 700);
	_recorder->moves.clear();
	_recorder->order.clear();

	EnterDrag();
	Tick();
	EXPECT_EQ(_recorder->moves.size(), 0u);
	ExitDrag();
}
