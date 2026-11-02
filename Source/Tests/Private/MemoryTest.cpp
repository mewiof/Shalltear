#include "TestsPCH.h"

using namespace Shalltear;

TEST(MemoryHelpersTest, IsPowerOfTwoKnownValues) {
	EXPECT_FALSE(IsPowerOfTwo(0));
	EXPECT_TRUE(IsPowerOfTwo(1));
	EXPECT_TRUE(IsPowerOfTwo(2));
	EXPECT_TRUE(IsPowerOfTwo(4));
	EXPECT_TRUE(IsPowerOfTwo(16));
	EXPECT_TRUE(IsPowerOfTwo(256));
	EXPECT_TRUE(IsPowerOfTwo(1ull << 40));

	EXPECT_FALSE(IsPowerOfTwo(3));
	EXPECT_FALSE(IsPowerOfTwo(6));
	EXPECT_FALSE(IsPowerOfTwo(100));
}

TEST(MemoryHelpersTest, AlignForwardLeavesAlreadyAlignedPointerUntouched) {
	alignas(16) u8 buffer[64];
	void* aligned = AlignForward(buffer, 16);
	EXPECT_EQ(aligned, static_cast<void*>(buffer));
}

TEST(MemoryHelpersTest, AlignForwardRoundsUpMisalignedPointer) {
	alignas(32) u8 buffer[64];
	void* offset = buffer + 1;
	void* aligned = AlignForward(offset, 16);

	uintptr_t result = reinterpret_cast<uintptr_t>(aligned);
	EXPECT_EQ(result % 16u, 0u);
	EXPECT_GE(result, reinterpret_cast<uintptr_t>(offset));
	EXPECT_LT(result - reinterpret_cast<uintptr_t>(offset), 16u);
}

TEST(MemoryHelpersTest, AlignForwardSupportsLargeAlignments) {
	alignas(256) u8 buffer[512];
	void* offset = buffer + 3;
	void* aligned = AlignForward(offset, 128);

	EXPECT_EQ(reinterpret_cast<uintptr_t>(aligned) % 128u, 0u);
}

TEST(MemoryAllocateTest, ReturnsNonNullPointer) {
	void* p = Allocate(64, MemTag::Debug);
	ASSERT_NE(p, nullptr);
	Free(p);
}

TEST(MemoryAllocateTest, DefaultAlignmentIsSixteenBytes) {
	void* p = Allocate(32, MemTag::Debug);
	EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % 16u, 0u);
	Free(p);
}

TEST(MemoryAllocateTest, RespectsCustomAlignment) {
	for (size_t alignment : {size_t(2), size_t(4), size_t(8), size_t(32), size_t(64), size_t(128), size_t(256)}) {
		void* p = Allocate(128, MemTag::Debug, alignment);
		ASSERT_NE(p, nullptr);
		EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % alignment, 0u)
			<< "alignment=" << alignment;
		Free(p);
	}
}

TEST(MemoryAllocateTest, AllocationsAreWritable) {
	constexpr size_t SIZE = 256;
	u8* p = static_cast<u8*>(Allocate(SIZE, MemTag::Debug));
	ASSERT_NE(p, nullptr);

	for (size_t i = 0; i < SIZE; ++i)
		p[i] = static_cast<u8>(i & 0xFF);

	for (size_t i = 0; i < SIZE; ++i)
		EXPECT_EQ(p[i], static_cast<u8>(i & 0xFF));

	Free(p);
}

TEST(MemoryAllocateTest, DistinctAllocationsHaveDistinctAddresses) {
	void* a = Allocate(64, MemTag::Debug);
	void* b = Allocate(64, MemTag::Debug);
	EXPECT_NE(a, b);
	Free(a);
	Free(b);
}

TEST(MemoryAllocateTest, FreeOfNullIsSafe) {
	Free(nullptr);
	SUCCEED();
}

TEST(MemoryAllocateTest, ZeroSizeAllocationReturnsValidPointer) {
	void* p = Allocate(0, MemTag::Debug);
	EXPECT_NE(p, nullptr);
	Free(p);
}

TEST(MemoryManagerTest, TagNameIsStable) {
	EXPECT_STREQ(MemoryManager::GetTagName(MemTag::Unknown), "Unknown");
	EXPECT_STREQ(MemoryManager::GetTagName(MemTag::Core), "Core");
	EXPECT_STREQ(MemoryManager::GetTagName(MemTag::Graphics), "Graphics");
	EXPECT_STREQ(MemoryManager::GetTagName(MemTag::Debug), "Debug");
}

TEST(MemoryManagerTest, AllocateBumpsTrackerByExactSize) {
	size_t before = MemoryManager::GetBytesForTag(MemTag::Debug);
	size_t countBefore = MemoryManager::GetAllocationCountForTag(MemTag::Debug);

	constexpr size_t SIZE = 512;
	void* p = Allocate(SIZE, MemTag::Debug);

	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Debug), before + SIZE);
	EXPECT_EQ(MemoryManager::GetAllocationCountForTag(MemTag::Debug), countBefore + 1);

	Free(p);

	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Debug), before);
	EXPECT_EQ(MemoryManager::GetAllocationCountForTag(MemTag::Debug), countBefore);
}

TEST(MemoryManagerTest, TagsAreIsolatedFromEachOther) {
	size_t graphicsBefore = MemoryManager::GetBytesForTag(MemTag::Graphics);
	size_t audioBefore = MemoryManager::GetBytesForTag(MemTag::Audio);

	void* p = Allocate(1024, MemTag::Graphics);

	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Graphics), graphicsBefore + 1024);
	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Audio), audioBefore);

	Free(p);
}

TEST(MemoryManagerTest, TotalBytesIncludesAllTags) {
	size_t totalBefore = MemoryManager::GetTotalBytes();
	size_t totalCountBefore = MemoryManager::GetTotalAllocations();

	void* a = Allocate(100, MemTag::Audio);
	void* b = Allocate(200, MemTag::Physics);

	EXPECT_EQ(MemoryManager::GetTotalBytes(), totalBefore + 300);
	EXPECT_EQ(MemoryManager::GetTotalAllocations(), totalCountBefore + 2);

	Free(a);
	Free(b);

	EXPECT_EQ(MemoryManager::GetTotalBytes(), totalBefore);
	EXPECT_EQ(MemoryManager::GetTotalAllocations(), totalCountBefore);
}

TEST(MemoryManagerTest, ManualTrackHooksAdjustCounters) {
	size_t before = MemoryManager::GetBytesForTag(MemTag::Asset);
	size_t countBefore = MemoryManager::GetAllocationCountForTag(MemTag::Asset);

	MemoryManager::TrackAlloc(MemTag::Asset, 4096);
	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Asset), before + 4096);
	EXPECT_EQ(MemoryManager::GetAllocationCountForTag(MemTag::Asset), countBefore + 1);

	MemoryManager::TrackFree(MemTag::Asset, 4096);
	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Asset), before);
	EXPECT_EQ(MemoryManager::GetAllocationCountForTag(MemTag::Asset), countBefore);
}

namespace {
	struct LifetimeProbe {
		static inline int constructCount = 0;
		static inline int destructCount = 0;

		int value;

		LifetimeProbe() : value(-1) { ++constructCount; }
		explicit LifetimeProbe(int v) : value(v) { ++constructCount; }
		LifetimeProbe(int a, int b) : value(a + b) { ++constructCount; }

		~LifetimeProbe() { ++destructCount; }
	};

	struct TrivialProbe {
		static inline int constructCount = 0;
		int value;

		TrivialProbe() : value(-1) { ++constructCount; }
		explicit TrivialProbe(int v) : value(v) { ++constructCount; }
	};
} // namespace

TEST(MemoryTypedTest, NewCallsConstructor) {
	LifetimeProbe::constructCount = 0;
	LifetimeProbe::destructCount = 0;

	LifetimeProbe* p = New<LifetimeProbe>(MemTag::Debug, 42);
	ASSERT_NE(p, nullptr);
	EXPECT_EQ(p->value, 42);
	EXPECT_EQ(LifetimeProbe::constructCount, 1);

	Delete(p);
	EXPECT_EQ(LifetimeProbe::destructCount, 1);
}

TEST(MemoryTypedTest, NewPerfectlyForwardsMultipleArgs) {
	LifetimeProbe::constructCount = 0;
	LifetimeProbe::destructCount = 0;

	LifetimeProbe* p = New<LifetimeProbe>(MemTag::Debug, 10, 32);
	EXPECT_EQ(p->value, 42);

	Delete(p);
}

TEST(MemoryTypedTest, DeleteNullptrIsSafe) {
	LifetimeProbe::destructCount = 0;
	LifetimeProbe* null = nullptr;
	Delete(null);
	EXPECT_EQ(LifetimeProbe::destructCount, 0);
}

TEST(LinearAllocatorTest, InitialStateIsEmpty) {
	LinearAllocator arena(1024, MemTag::Debug);
	EXPECT_EQ(arena.GetCapacity(), 1024u);
	EXPECT_EQ(arena.GetUsedBytes(), 0u);
	EXPECT_EQ(arena.GetRemainingBytes(), 1024u);
	EXPECT_EQ(arena.GetTag(), MemTag::Debug);
}

TEST(LinearAllocatorTest, AllocateBumpsCursor) {
	LinearAllocator arena(1024, MemTag::Debug);
	void* a = arena.Allocate(100, 1);
	ASSERT_NE(a, nullptr);
	EXPECT_EQ(arena.GetUsedBytes(), 100u);

	void* b = arena.Allocate(200, 1);
	ASSERT_NE(b, nullptr);
	EXPECT_EQ(arena.GetUsedBytes(), 300u);
	EXPECT_NE(a, b);
}

TEST(LinearAllocatorTest, AllocationsRespectRequestedAlignment) {
	LinearAllocator arena(4096, MemTag::Debug);

	arena.Allocate(3, 1);

	for (size_t alignment : {size_t(4), size_t(8), size_t(16), size_t(64), size_t(256)}) {
		void* p = arena.Allocate(8, alignment);
		ASSERT_NE(p, nullptr);
		EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % alignment, 0u)
			<< "alignment=" << alignment;
	}
}

TEST(LinearAllocatorTest, ResetRecyclesBufferWithoutFreeing) {
	LinearAllocator arena(256, MemTag::Debug);
	arena.Allocate(200, 1);
	EXPECT_EQ(arena.GetUsedBytes(), 200u);

	arena.Reset();
	EXPECT_EQ(arena.GetUsedBytes(), 0u);
	EXPECT_EQ(arena.GetRemainingBytes(), 256u);

	void* p = arena.Allocate(200, 1);
	ASSERT_NE(p, nullptr);
}

TEST(LinearAllocatorTest, AllocateAsReturnsTypedPointer) {
	LinearAllocator arena(1024, MemTag::Debug);
	u32* ints = arena.AllocateAs<u32>(16);
	ASSERT_NE(ints, nullptr);
	EXPECT_EQ(reinterpret_cast<uintptr_t>(ints) % alignof(u32), 0u);

	for (int i = 0; i < 16; ++i)
		ints[i] = static_cast<u32>(i);
	for (int i = 0; i < 16; ++i)
		EXPECT_EQ(ints[i], static_cast<u32>(i));
}

TEST(LinearAllocatorTest, ConstructRunsTypeConstructor) {
	TrivialProbe::constructCount = 0;

	LinearAllocator arena(1024, MemTag::Debug);
	TrivialProbe* p = arena.Construct<TrivialProbe>(7);
	ASSERT_NE(p, nullptr);
	EXPECT_EQ(p->value, 7);
	EXPECT_EQ(TrivialProbe::constructCount, 1);
}

TEST(LinearAllocatorTest, ResetDoesNotCallDestructors) {
	LifetimeProbe::constructCount = 0;
	LifetimeProbe::destructCount = 0;

	LinearAllocator arena(1024, MemTag::Debug);
	void* mem = arena.Allocate(sizeof(LifetimeProbe), alignof(LifetimeProbe));
	ASSERT_NE(mem, nullptr);
	LifetimeProbe* p = new (mem) LifetimeProbe(7);
	EXPECT_EQ(LifetimeProbe::constructCount, 1);

	arena.Reset();
	EXPECT_EQ(LifetimeProbe::destructCount, 0);
}

TEST(LinearAllocatorTest, ArenaChargesTagOnceAtCapacity) {
	size_t before = MemoryManager::GetBytesForTag(MemTag::Physics);
	size_t countBefore = MemoryManager::GetAllocationCountForTag(MemTag::Physics);

	{
		LinearAllocator arena(8192, MemTag::Physics);
		EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Physics), before + 8192);
		EXPECT_EQ(MemoryManager::GetAllocationCountForTag(MemTag::Physics), countBefore + 1);

		arena.Allocate(512, 16);
		arena.Allocate(1024, 16);
		EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Physics), before + 8192);
		EXPECT_EQ(MemoryManager::GetAllocationCountForTag(MemTag::Physics), countBefore + 1);
	}

	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Physics), before);
	EXPECT_EQ(MemoryManager::GetAllocationCountForTag(MemTag::Physics), countBefore);
}

TEST(LinearAllocatorTest, GrowsWithoutMovingEarlierAllocations) {
	LinearAllocator arena(256, MemTag::Debug);
	u8* first = arena.AllocateAs<u8>(200);
	ASSERT_NE(first, nullptr);
	std::memset(first, 0xAB, 200);

	u8* second = arena.AllocateAs<u8>(200);
	ASSERT_NE(second, nullptr);
	std::memset(second, 0xCD, 200);
	u8* large = arena.AllocateAs<u8>(10000);
	ASSERT_NE(large, nullptr);
	std::memset(large, 0xEF, 10000);

	EXPECT_EQ(arena.GetUsedBytes(), 10400u);
	EXPECT_GE(arena.GetCapacity(), 10400u);
	EXPECT_EQ(std::count(first, first + 200, u8{0xAB}), 200);
	EXPECT_EQ(std::count(second, second + 200, u8{0xCD}), 200);
}

TEST(LinearAllocatorTest, GrownBlockHonorsAlignment) {
	LinearAllocator arena(64, MemTag::Debug);
	arena.Allocate(60, 1);
	for (size_t alignment : {size_t(16), size_t(64), size_t(256)}) {
		void* p = arena.Allocate(100, alignment);
		ASSERT_NE(p, nullptr);
		EXPECT_EQ(reinterpret_cast<uintptr_t>(p) % alignment, 0u) << alignment;
	}
}

TEST(LinearAllocatorTest, ResetFoldsGrownBlocksIntoOne) {
	const size_t before = MemoryManager::GetBytesForTag(MemTag::Physics);
	const size_t countBefore = MemoryManager::GetAllocationCountForTag(MemTag::Physics);
	{
		LinearAllocator arena(256, MemTag::Physics);
		for (int i = 0; i < 8; ++i) {
			arena.Allocate(200, 16);
		}
		const size_t peak = arena.GetUsedBytes();
		EXPECT_GT(MemoryManager::GetAllocationCountForTag(MemTag::Physics), countBefore + 1);

		arena.Reset();
		const size_t capacity = arena.GetCapacity();
		EXPECT_EQ(arena.GetUsedBytes(), 0u);
		EXPECT_GE(capacity, peak);
		EXPECT_EQ(arena.GetRemainingBytes(), capacity);
		EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Physics), before + capacity);
		EXPECT_EQ(MemoryManager::GetAllocationCountForTag(MemTag::Physics), countBefore + 1);

		for (int i = 0; i < 8; ++i) {
			arena.Allocate(200, 16);
		}
		EXPECT_EQ(arena.GetCapacity(), capacity);
		EXPECT_EQ(MemoryManager::GetAllocationCountForTag(MemTag::Physics), countBefore + 1);
	}
	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Physics), before);
	EXPECT_EQ(MemoryManager::GetAllocationCountForTag(MemTag::Physics), countBefore);
}

TEST(LinearAllocatorTest, GrownArenaReleasesAllBlocks) {
	const size_t before = MemoryManager::GetBytesForTag(MemTag::Physics);
	const size_t countBefore = MemoryManager::GetAllocationCountForTag(MemTag::Physics);
	{
		LinearAllocator arena(64, MemTag::Physics);
		arena.Allocate(48, 16);
		arena.Allocate(48, 16);
		arena.Allocate(1000, 16);
		arena.Allocate(5000, 256);
		EXPECT_EQ(MemoryManager::GetAllocationCountForTag(MemTag::Physics), countBefore + 4);
	}
	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Physics), before);
	EXPECT_EQ(MemoryManager::GetAllocationCountForTag(MemTag::Physics), countBefore);
}

TEST(MemorySmartPointerTest, MakeSharedStartsAtOne) {
	SharedPtr<int> ptr = MakeShared<int>(42);
	ASSERT_NE(ptr, nullptr);
	EXPECT_EQ(*ptr, 42);
	EXPECT_EQ(ptr.use_count(), 1);
}

TEST(MemorySmartPointerTest, SharedPtrCopyIncrementsRefCount) {
	SharedPtr<int> a = MakeShared<int>(99);
	SharedPtr<int> b = a;
	EXPECT_EQ(a.use_count(), 2);
	EXPECT_EQ(b.use_count(), 2);
	EXPECT_EQ(*b, 99);
}

TEST(MemorySmartPointerTest, MakeUniqueProducesUniquePtr) {
	UniquePtr<int> ptr = MakeUnique<int>(123);
	ASSERT_NE(ptr, nullptr);
	EXPECT_EQ(*ptr, 123);
}

TEST(MemorySmartPointerTest, WeakPtrLocksWhileSharedAlive) {
	SharedPtr<int> strong = MakeShared<int>(7);
	WeakPtr<int> weak = strong;

	EXPECT_FALSE(weak.expired());
	if (auto locked = weak.lock())
		EXPECT_EQ(*locked, 7);

	strong.reset();
	EXPECT_TRUE(weak.expired());
	EXPECT_EQ(weak.lock(), nullptr);
}
