#include "TestsPCH.h"

#include "Shalltear/Core/Containers.h"

using namespace Shalltear;

TEST(ContainersTest, VectorAllocationsAreTrackedPerTag) {
	size_t bytesBefore = MemoryManager::GetBytesForTag(MemTag::Debug);

	{
		Vector<u64> values{GetMemoryResource(MemTag::Debug)};
		values.reserve(1024);

		EXPECT_GE(MemoryManager::GetBytesForTag(MemTag::Debug), bytesBefore + 1024 * sizeof(u64));
	}

	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Debug), bytesBefore);
}

TEST(ContainersTest, HashMapAllocationsAreTrackedPerTag) {
	size_t bytesBefore = MemoryManager::GetBytesForTag(MemTag::Debug);

	{
		HashMap<u32, u64> map{GetMemoryResource(MemTag::Debug)};
		for (u32 i = 0; i < 256; ++i) {
			map[i] = u64(i) * 2;
		}
		EXPECT_GT(MemoryManager::GetBytesForTag(MemTag::Debug), bytesBefore);
	}

	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Debug), bytesBefore);
}

TEST(ContainersTest, StringAllocationsAreTrackedPerTag) {
	size_t bytesBefore = MemoryManager::GetBytesForTag(MemTag::Debug);

	{
		String text{GetMemoryResource(MemTag::Debug)};
		text.assign(512, 'x');
		EXPECT_GT(MemoryManager::GetBytesForTag(MemTag::Debug), bytesBefore);
	}

	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Debug), bytesBefore);
}

TEST(ContainersTest, ResourcesCompareEqualByTag) {
	EXPECT_TRUE(GetMemoryResource(MemTag::Core)->is_equal(*GetMemoryResource(MemTag::Core)));
	EXPECT_FALSE(GetMemoryResource(MemTag::Core)->is_equal(*GetMemoryResource(MemTag::Asset)));
	EXPECT_FALSE(GetMemoryResource(MemTag::Core)->is_equal(*std::pmr::get_default_resource()));
}

TEST(ContainersTest, ResourceTableIsStableAcrossCalls) {
	EXPECT_EQ(GetMemoryResource(MemTag::Graphics), GetMemoryResource(MemTag::Graphics));
}

TEST(ContainersTest, MoveBetweenSameTagContainersKeepsAllocator) {
	size_t bytesBefore = MemoryManager::GetBytesForTag(MemTag::Debug);

	{
		Vector<u64> source{GetMemoryResource(MemTag::Debug)};
		source.resize(128);

		Vector<u64> destination = std::move(source);
		EXPECT_EQ(destination.get_allocator().resource(), GetMemoryResource(MemTag::Debug));
		EXPECT_GT(MemoryManager::GetBytesForTag(MemTag::Debug), bytesBefore);
	}

	EXPECT_EQ(MemoryManager::GetBytesForTag(MemTag::Debug), bytesBefore);
}
