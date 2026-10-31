#include <gtest/gtest.h>
#include "Shalltear/Core/Event.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Handle.h"
#include <thread>
#include <vector>

using namespace Shalltear;

namespace {
	struct TestPayload {
		int value;
	};

	struct DifferentPayload {
		float fValue;
	};

	static const Event<TestPayload> TestEvent("TestEvent"_hash);
	static const Event<DifferentPayload> InnerEvent("InnerEvent"_hash);

	class TestSubscriber : public Object {
	public:
		void OnTestEvent(const TestPayload& payload) {
			receivedValue = payload.value;
			callCount++;
		}

		void OnTestEventWithReentrancy(const TestPayload& payload) {
			receivedValue = payload.value;
			callCount++;

			InnerEvent.Publish(bus, {3.14f});
		}

		void OnInnerEvent(const DifferentPayload& payload) {
			innerCallCount++;
			innerValue.store(payload.fValue, std::memory_order_relaxed);
		}

		std::atomic<int> receivedValue{0};
		std::atomic<int> callCount{0};

		std::atomic<int> innerCallCount{0};
		std::atomic<float> innerValue{0.0f};

		EventBus bus;
	};
} //namespace

class EventBusTest : public ::testing::Test {
protected:
	void SetUp() override {
	}

	void TearDown() override {
	}
};

TEST_F(EventBusTest, BasicPublishSubscribe) {
	EventBus bus;
	auto subscriber = MakeShared<TestSubscriber>();
	Handle<TestSubscriber> handle(subscriber.get());

	TestEvent.Subscribe(bus, handle, &TestSubscriber::OnTestEvent);

	TestEvent.Publish(bus, {42});

	EXPECT_EQ(subscriber->receivedValue.load(), 42);
	EXPECT_EQ(subscriber->callCount.load(), 1);
}

TEST_F(EventBusTest, AutoUnsubscribeOnDeadHandle) {
	EventBus bus;
	auto subscriber = MakeShared<TestSubscriber>();
	Handle<TestSubscriber> handle(subscriber.get());

	TestEvent.Subscribe(bus, handle, &TestSubscriber::OnTestEvent);

	subscriber.reset();

	TestEvent.Publish(bus, {42});
}

TEST_F(EventBusTest, Reentrancy) {
	auto subscriber = MakeShared<TestSubscriber>();
	Handle<TestSubscriber> handle(subscriber.get());

	TestEvent.Subscribe(subscriber->bus, handle, &TestSubscriber::OnTestEventWithReentrancy);
	InnerEvent.Subscribe(subscriber->bus, handle, &TestSubscriber::OnInnerEvent);

	TestEvent.Publish(subscriber->bus, {42});

	EXPECT_EQ(subscriber->receivedValue.load(), 42);
	EXPECT_EQ(subscriber->callCount.load(), 1);

	EXPECT_EQ(subscriber->innerCallCount.load(), 1);
	EXPECT_FLOAT_EQ(subscriber->innerValue.load(), 3.14f);
}

TEST_F(EventBusTest, ManualUnsubscribe) {
	EventBus bus;
	auto subscriber = MakeShared<TestSubscriber>();
	Handle<TestSubscriber> handle(subscriber.get());

	TestEvent.Subscribe(bus, handle, &TestSubscriber::OnTestEvent);
	TestEvent.Unsubscribe(bus, subscriber->GetInstanceId());

	TestEvent.Publish(bus, {42});

	EXPECT_EQ(subscriber->receivedValue.load(), 0);
	EXPECT_EQ(subscriber->callCount.load(), 0);
}

TEST_F(EventBusTest, UnsubscribeAll) {
	EventBus bus;
	auto subscriber = MakeShared<TestSubscriber>();
	Handle<TestSubscriber> handle(subscriber.get());

	TestEvent.Subscribe(bus, handle, &TestSubscriber::OnTestEvent);
	InnerEvent.Subscribe(bus, handle, &TestSubscriber::OnInnerEvent);

	bus.UnsubscribeAll(subscriber->GetInstanceId());

	TestEvent.Publish(bus, {42});
	InnerEvent.Publish(bus, {3.14f});

	EXPECT_EQ(subscriber->callCount.load(), 0);
	EXPECT_EQ(subscriber->innerCallCount.load(), 0);
}

TEST_F(EventBusTest, ThreadSafety) {
	EventBus bus;
	auto subscriber = MakeShared<TestSubscriber>();
	Handle<TestSubscriber> handle(subscriber.get());

	TestEvent.Subscribe(bus, handle, &TestSubscriber::OnTestEvent);

	auto publishTask = [&bus]() {
		for (int i = 0; i < 1000; ++i) {
			TestEvent.Publish(bus, {i});
		}
	};

	std::vector<std::thread> threads;
	for (int i = 0; i < 10; ++i) {
		threads.emplace_back(publishTask);
	}

	for (auto& t : threads) {
		t.join();
	}

	EXPECT_EQ(subscriber->callCount.load(), 10000);
}
