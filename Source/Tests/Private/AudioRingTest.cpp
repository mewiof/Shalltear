#include "TestsPCH.h"

#ifndef SHALLTEAR_SHARED

#include "AudioRing.h"

#include "Shalltear/Core/Memory.h"

#include <atomic>
#include <thread>

using namespace Shalltear;
using namespace Shalltear::AudioImpl;

namespace {

	struct Message {
		u64 sequence = 0;
		u32 check = 0;
	};

} // namespace

TEST(AudioRingTest, FullRingRefusesAndNothingIsOverwritten) {
	AudioRing<Message> ring;
	ring.Reserve(5);
	ASSERT_EQ(ring.GetCapacity(), 8u);

	for (u64 sequence = 0; sequence < 8; ++sequence) {
		EXPECT_TRUE(ring.TryPush(Message{sequence, 0})) << "push " << sequence;
	}
	EXPECT_FALSE(ring.TryPush(Message{99, 0}));

	Message out;
	for (u64 sequence = 0; sequence < 8; ++sequence) {
		ASSERT_TRUE(ring.TryPop(out));
		EXPECT_EQ(out.sequence, sequence);
	}
	EXPECT_FALSE(ring.TryPop(out));

	for (u64 round = 0; round < 100; ++round) {
		ASSERT_TRUE(ring.TryPush(Message{round, 0}));
		ASSERT_TRUE(ring.TryPop(out));
		EXPECT_EQ(out.sequence, round);
	}
}

TEST(AudioRingTest, AnUnreservedRingRefusesEverything) {
	AudioRing<Message> ring;
	Message out;
	EXPECT_EQ(ring.GetCapacity(), 0u);
	EXPECT_FALSE(ring.TryPush(Message{}));
	EXPECT_FALSE(ring.TryPop(out));
}

TEST(AudioRingTest, MillionMessagesCrossInOrder) {
	constexpr u64 COUNT = 1000000;
	AudioRing<Message> ring;
	ring.Reserve(64);

	std::atomic<bool> start{false};
	std::atomic<u64> refused{0};
	std::thread producer([&]() {
		while (!start.load()) {
			std::this_thread::yield();
		}
		for (u64 sequence = 0; sequence < COUNT; ++sequence) {
			const Message message{sequence, static_cast<u32>(sequence * 2654435761u)};
			while (!ring.TryPush(message)) {
				refused.fetch_add(1, std::memory_order_relaxed);
				std::this_thread::yield();
			}
		}
	});

	start.store(true);
	while (refused.load(std::memory_order_relaxed) == 0) {
		std::this_thread::yield();
	}
	u64 expected = 0;
	u64 torn = 0;
	u64 disordered = 0;
	Message out;
	while (expected < COUNT) {
		if (!ring.TryPop(out)) {
			continue;
		}
		if (out.sequence != expected) {
			++disordered;
		}
		if (out.check != static_cast<u32>(out.sequence * 2654435761u)) {
			++torn;
		}
		expected = out.sequence + 1;
	}
	producer.join();

	EXPECT_EQ(disordered, 0u);
	EXPECT_EQ(torn, 0u);
	EXPECT_FALSE(ring.TryPop(out));
}

TEST(AudioRingTest, BatchPublishedWhole) {
	constexpr u64 BATCHES = 100000;
	constexpr u32 LARGEST = 13;
	AudioRing<Message> ring;
	ring.Reserve(64);

	std::atomic<bool> start{false};
	std::atomic<u64> refused{0};
	std::thread producer([&]() {
		while (!start.load()) {
			std::this_thread::yield();
		}
		Message batch[LARGEST];
		for (u64 index = 0; index < BATCHES; ++index) {
			const u32 size = 1 + static_cast<u32>(index % LARGEST);
			for (u32 entry = 0; entry < size; ++entry) {
				batch[entry] = Message{index, size - 1 - entry};
			}
			while (!ring.TryPushAll(batch, size)) {
				refused.fetch_add(1, std::memory_order_relaxed);
				std::this_thread::yield();
			}
		}
	});

	start.store(true);
	while (refused.load(std::memory_order_relaxed) == 0) {
		std::this_thread::yield();
	}
	u64 total = 0;
	for (u64 index = 0; index < BATCHES; ++index) {
		total += 1 + index % LARGEST;
	}
	u64 received = 0;
	u64 prefixes = 0;
	u64 disordered = 0;
	Message out;
	while (received < total) {
		if (!ring.TryPop(out)) {
			continue;
		}
		++received;
		const u32 size = 1 + static_cast<u32>(out.sequence % LARGEST);
		if (out.check != size - 1) {
			continue;
		}
		const u64 index = out.sequence;
		for (u32 remaining = out.check; remaining > 0; --remaining) {
			Message next;
			if (!ring.TryPop(next)) {
				++prefixes;
				break;
			}
			++received;
			if (next.sequence != index || next.check != remaining - 1) {
				++disordered;
			}
		}
	}
	producer.join();

	EXPECT_EQ(prefixes, 0u);
	EXPECT_EQ(disordered, 0u);

	AudioRing<Message> small;
	small.Reserve(4);
	Message three[3] = {{1, 0}, {2, 0}, {3, 0}};
	ASSERT_TRUE(small.TryPushAll(three, 3));
	EXPECT_FALSE(small.TryPushAll(three, 2));
	Message popped;
	for (u64 sequence = 1; sequence <= 3; ++sequence) {
		ASSERT_TRUE(small.TryPop(popped));
		EXPECT_EQ(popped.sequence, sequence);
	}
	EXPECT_FALSE(small.TryPop(popped));
}

TEST(AudioRingTest, ReserveIsOnlyAllocation) {
	const size_t baseline = MemoryManager::GetBytesForTag(MemTag::Audio);
	{
		AudioRing<Message> ring;
		ring.Reserve(1000);
		const size_t reserved = MemoryManager::GetBytesForTag(MemTag::Audio);
		EXPECT_EQ(reserved - baseline, sizeof(Message) * 1024);
		for (u32 round = 0; round < 5000; ++round) {
			ring.TryPush(Message{round, 0});
			Message out;
			ring.TryPop(out);
		}
		EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Audio), reserved);
		ring.Release();
		EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Audio), baseline);
	}
	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Audio), baseline);
}

#endif
