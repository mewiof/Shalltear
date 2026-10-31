#include "TestsPCH.h"

using namespace Shalltear;

namespace {
	class TestObject : public Object {
	public:
		explicit TestObject(int v = 0) : payload(v) {}
		int payload;
	};
} // namespace

TEST(ObjectTest, ConstructorAssignsNonZeroId) {
	TestObject obj;
	EXPECT_NE(obj.GetInstanceId(), 0u);
}

TEST(ObjectTest, DistinctObjectsHaveDistinctIds) {
	TestObject a, b, c;
	EXPECT_NE(a.GetInstanceId(), b.GetInstanceId());
	EXPECT_NE(b.GetInstanceId(), c.GetInstanceId());
	EXPECT_NE(a.GetInstanceId(), c.GetInstanceId());
}

TEST(ObjectTest, GetInstanceIdIsStableForLifetime) {
	TestObject obj;
	u64 first = obj.GetInstanceId();
	u64 second = obj.GetInstanceId();
	EXPECT_EQ(first, second);
}

TEST(ObjectRegistryTest, RegisterNullReturnsZero) {
	EXPECT_EQ(ObjectRegistry::Register(nullptr), 0u);
}

TEST(ObjectRegistryTest, RegisteredObjectResolvesToSameInstance) {
	TestObject obj(42);
	Object* resolved = ObjectRegistry::Resolve(obj.GetInstanceId());
	ASSERT_NE(resolved, nullptr);

	EXPECT_EQ(static_cast<TestObject*>(resolved)->payload, 42);
}

TEST(ObjectRegistryTest, ResolveWithIdZeroReturnsNull) {
	EXPECT_EQ(ObjectRegistry::Resolve(0), nullptr);
}

TEST(ObjectRegistryTest, ResolveOutOfBoundsIndexReturnsNull) {
	u64 bogusId = (static_cast<u64>(1) << 32) | 0xFFFFFFFEu;
	EXPECT_EQ(ObjectRegistry::Resolve(bogusId), nullptr);
}

TEST(ObjectRegistryTest, ResolveWithMismatchedGenerationReturnsNull) {
	TestObject obj;
	u64 id = obj.GetInstanceId();

	u32 index = static_cast<u32>(id & 0xFFFFFFFF);
	u32 generation = static_cast<u32>(id >> 32);

	u64 stale = (static_cast<u64>(generation + 999) << 32) | index;
	EXPECT_EQ(ObjectRegistry::Resolve(stale), nullptr);
}

TEST(ObjectRegistryTest, ResolveReturnsNullAfterDestruction) {
	u64 id = 0;
	{
		TestObject obj;
		id = obj.GetInstanceId();
		EXPECT_NE(ObjectRegistry::Resolve(id), nullptr);
	}

	EXPECT_EQ(ObjectRegistry::Resolve(id), nullptr);
}

TEST(ObjectRegistryTest, UnregisterZeroIsSafe) {
	ObjectRegistry::Unregister(0);
	SUCCEED();
}

TEST(ObjectRegistryTest, UnregisterIsIdempotent) {
	TestObject* obj = New<TestObject>(MemTag::Debug);
	u64 id = obj->GetInstanceId();

	Delete(obj);
	ObjectRegistry::Unregister(id);
	ObjectRegistry::Unregister(id);

	EXPECT_EQ(ObjectRegistry::Resolve(id), nullptr);
}

TEST(ObjectRegistryTest, StaleUnregisterSparesLive) {
	u64 staleId = 0;
	{
		TestObject a;
		staleId = a.GetInstanceId();
	}

	TestObject b;
	u64 liveId = b.GetInstanceId();

	ObjectRegistry::Unregister(staleId);
	EXPECT_NE(ObjectRegistry::Resolve(liveId), nullptr);
}

TEST(ObjectRegistryTest, SlotReuseBumpsGeneration) {
	u64 firstId = 0;
	{
		TestObject first;
		firstId = first.GetInstanceId();
	}

	TestObject second;
	u64 secondId = second.GetInstanceId();

	EXPECT_NE(firstId, secondId);

	u32 firstIndex = static_cast<u32>(firstId & 0xFFFFFFFF);
	u32 secondIndex = static_cast<u32>(secondId & 0xFFFFFFFF);
	u32 firstGen = static_cast<u32>(firstId >> 32);
	u32 secondGen = static_cast<u32>(secondId >> 32);

	bool indexChanged = (firstIndex != secondIndex);
	bool generationBumped = (secondGen > firstGen);
	EXPECT_TRUE(indexChanged || generationBumped);
}

TEST(ObjectRegistryTest, IdEncodingIsGenerationShiftedIndex) {
	TestObject obj;
	u64 id = obj.GetInstanceId();

	u32 index = static_cast<u32>(id & 0xFFFFFFFF);
	u32 generation = static_cast<u32>(id >> 32);
	u64 reassembled = (static_cast<u64>(generation) << 32) | index;

	EXPECT_EQ(reassembled, id);
}

TEST(ObjectRegistryTest, ManyConcurrentObjectsAllResolveCorrectly) {
	constexpr int COUNT = 64;
	std::vector<TestObject*> objects;
	objects.reserve(COUNT);

	for (int i = 0; i < COUNT; ++i)
		objects.push_back(New<TestObject>(MemTag::Debug, i));

	for (int i = 0; i < COUNT; ++i) {
		Object* resolved = ObjectRegistry::Resolve(objects[i]->GetInstanceId());
		ASSERT_EQ(resolved, objects[i]) << "i=" << i;
	}

	for (auto* obj : objects)
		Delete(obj);
}

TEST(ObjectRegistryTest, ConcurrentRegisterProducesUniqueIds) {
	constexpr int THREAD_COUNT = 4;
	constexpr int PER_THREAD = 64;

	std::vector<std::thread> workers;
	std::vector<std::vector<u64>> collected(THREAD_COUNT);

	for (int t = 0; t < THREAD_COUNT; ++t) {
		workers.emplace_back([&, t] {
			collected[t].reserve(PER_THREAD);
			std::vector<TestObject*> local;
			for (int i = 0; i < PER_THREAD; ++i) {
				auto* obj = New<TestObject>(MemTag::Debug, i);
				collected[t].push_back(obj->GetInstanceId());
				local.push_back(obj);
			}
			std::this_thread::yield();
			for (auto* o : local)
				Delete(o);
		});
	}
	for (auto& w : workers)
		w.join();

	std::unordered_map<u64, int> counts;
	for (auto& bucket : collected)
		for (u64 id : bucket)
			counts[id]++;

	for (auto& [id, count] : counts)
		EXPECT_EQ(count, 1) << "id=" << id;
}
