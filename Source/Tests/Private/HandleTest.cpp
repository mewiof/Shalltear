#include "TestsPCH.h"

using namespace Shalltear;

namespace {
	class HandleTestObject : public Object {
	public:
		explicit HandleTestObject(int v = 0) : payload(v) {}
		int payload;
	};
} // namespace

TEST(HandleTest, DefaultConstructedIsInvalid) {
	Handle<HandleTestObject> handle;
	EXPECT_EQ(handle.GetInstanceId(), 0u);
	EXPECT_FALSE(handle.IsValid());
	EXPECT_FALSE(static_cast<bool>(handle));
	EXPECT_EQ(handle.Get(), nullptr);
}

TEST(HandleTest, NullptrConstructedIsInvalid) {
	Handle<HandleTestObject> handle(nullptr);
	EXPECT_EQ(handle.GetInstanceId(), 0u);
	EXPECT_FALSE(handle.IsValid());
}

TEST(HandleTest, FromLivePointerCapturesInstanceId) {
	HandleTestObject obj(99);
	Handle<HandleTestObject> handle(&obj);
	EXPECT_EQ(handle.GetInstanceId(), obj.GetInstanceId());
	EXPECT_TRUE(handle.IsValid());
}

TEST(HandleTest, FromNullPointerDoesNotCrash) {
	HandleTestObject* nullPtr = nullptr;
	Handle<HandleTestObject> handle(nullPtr);
	EXPECT_EQ(handle.GetInstanceId(), 0u);
	EXPECT_FALSE(handle.IsValid());
}

TEST(HandleTest, GetReturnsLivePointer) {
	HandleTestObject obj(7);
	Handle<HandleTestObject> handle(&obj);
	EXPECT_EQ(handle.Get(), &obj);
	EXPECT_EQ(handle.Get()->payload, 7);
}

TEST(HandleTest, ArrowOperatorAccessesMember) {
	HandleTestObject obj(33);
	Handle<HandleTestObject> handle(&obj);
	EXPECT_EQ(handle->payload, 33);
}

TEST(HandleTest, StarOperatorReturnsReference) {
	HandleTestObject obj(11);
	Handle<HandleTestObject> handle(&obj);
	EXPECT_EQ((*handle).payload, 11);
	EXPECT_EQ(&(*handle), &obj);
}

TEST(HandleTest, BecomesInvalidAfterObjectDestroyed) {
	Handle<HandleTestObject> handle;
	{
		HandleTestObject obj(5);
		handle = Handle<HandleTestObject>(&obj);
		EXPECT_TRUE(handle.IsValid());
	}
	EXPECT_FALSE(handle.IsValid());
	EXPECT_EQ(handle.Get(), nullptr);
}

TEST(HandleTest, IdOutlivesObjectButResolvesToNull) {
	Handle<HandleTestObject> handle;
	u64 capturedId = 0;
	{
		HandleTestObject obj;
		handle = Handle<HandleTestObject>(&obj);
		capturedId = handle.GetInstanceId();
	}
	EXPECT_EQ(handle.GetInstanceId(), capturedId);
	EXPECT_EQ(handle.Get(), nullptr);
}

TEST(HandleTest, EqualityBetweenHandlesOfSameObject) {
	HandleTestObject obj;
	Handle<HandleTestObject> a(&obj);
	Handle<HandleTestObject> b(&obj);
	EXPECT_TRUE(a == b);
	EXPECT_FALSE(a != b);
}

TEST(HandleTest, InequalityBetweenHandlesOfDifferentObjects) {
	HandleTestObject a, b;
	Handle<HandleTestObject> ha(&a);
	Handle<HandleTestObject> hb(&b);
	EXPECT_TRUE(ha != hb);
	EXPECT_FALSE(ha == hb);
}

TEST(HandleTest, ComparisonAgainstNullptr) {
	HandleTestObject obj;
	Handle<HandleTestObject> live(&obj);
	Handle<HandleTestObject> empty;

	EXPECT_TRUE(empty == nullptr);
	EXPECT_FALSE(empty != nullptr);

	EXPECT_TRUE(live != nullptr);
	EXPECT_FALSE(live == nullptr);
}

TEST(HandleTest, ComparisonSeesHandleInvalidationAfterDestroy) {
	Handle<HandleTestObject> handle;
	{
		HandleTestObject obj;
		handle = Handle<HandleTestObject>(&obj);
	}
	EXPECT_TRUE(handle == nullptr);
}

TEST(HandleTest, StdHashMatchesInstanceId) {
	HandleTestObject obj;
	Handle<HandleTestObject> h(&obj);
	std::hash<Handle<HandleTestObject>> hasher;
	EXPECT_EQ(hasher(h), static_cast<std::size_t>(h.GetInstanceId()));
}

TEST(HandleTest, UsableAsUnorderedMapKey) {
	HandleTestObject a, b;
	std::unordered_map<Handle<HandleTestObject>, int> lookup;
	lookup[Handle<HandleTestObject>(&a)] = 1;
	lookup[Handle<HandleTestObject>(&b)] = 2;

	EXPECT_EQ(lookup[Handle<HandleTestObject>(&a)], 1);
	EXPECT_EQ(lookup[Handle<HandleTestObject>(&b)], 2);
	EXPECT_EQ(lookup.size(), 2u);
}
