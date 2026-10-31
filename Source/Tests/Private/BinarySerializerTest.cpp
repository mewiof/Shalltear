#include "TestsPCH.h"

#include "Shalltear/Core/Serialization/BinarySerializer.h"
#include "Shalltear/Core/Serialization/HandleResolver.h"

using namespace Shalltear;

namespace {

	class PrimitiveComponent : public Object {
	public:
		f32 health = 100.0f;
		i32 damage = 10;
		u32 level = 1;
		bool alive = true;

		SLT_REFLECT_CLASS(PrimitiveComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(health)
		SLT_REFLECT_FIELD(damage)
		SLT_REFLECT_FIELD(level)
		SLT_REFLECT_FIELD(alive)
		SLT_END_REFLECT()
	};

	struct Transform {
		f32 x = 0.0f;
		f32 y = 0.0f;
		f32 z = 0.0f;

		SLT_REFLECT_VALUE(Transform)
		SLT_REFLECT_FIELD(x)
		SLT_REFLECT_FIELD(y)
		SLT_REFLECT_FIELD(z)
		SLT_END_REFLECT()
	};

	class TransformComponent : public Object {
	public:
		Transform transform;
		i32 tag = 7;

		SLT_REFLECT_CLASS(TransformComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_SUBOBJECT(transform)
		SLT_REFLECT_FIELD(tag)
		SLT_END_REFLECT()
	};

	class NamedComponent : public Object {
	public:
		str name = "default";
		i32 id = 0;

		SLT_REFLECT_CLASS(NamedComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(name)
		SLT_REFLECT_FIELD(id)
		SLT_END_REFLECT()
	};

	class WideComponent : public Object {
	public:
		i32 a = 11;
		i32 b = 22;
		i32 c = 33;

		SLT_REFLECT_CLASS(WideComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(a)
		SLT_REFLECT_FIELD(b)
		SLT_REFLECT_FIELD(c)
		SLT_END_REFLECT()
	};

	class NarrowComponent : public Object {
	public:
		i32 a = 0;
		i32 b = 0;

		SLT_REFLECT_CLASS(NarrowComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(a)
		SLT_REFLECT_FIELD(b)
		SLT_END_REFLECT()
	};

	struct Leaf {
		i32 value = 1;
		SLT_REFLECT_VALUE(Leaf)
		SLT_REFLECT_FIELD(value)
		SLT_END_REFLECT()
	};
	struct Mid {
		Leaf leaf;
		i32 value = 2;
		SLT_REFLECT_VALUE(Mid)
		SLT_REFLECT_SUBOBJECT(leaf)
		SLT_REFLECT_FIELD(value)
		SLT_END_REFLECT()
	};
	class DeepComponent : public Object {
	public:
		Mid mid;
		i32 value = 3;
		SLT_REFLECT_CLASS(DeepComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_SUBOBJECT(mid)
		SLT_REFLECT_FIELD(value)
		SLT_END_REFLECT()
	};

	class TransientComponent : public Object {
	public:
		i32 persisted = 5;
		i32 runtimeOnly = 9;

		SLT_REFLECT_CLASS(TransientComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(persisted)
		SLT_REFLECT_FIELD(runtimeOnly, SLT_TRANSIENT)
		SLT_END_REFLECT()
	};

	class TransientBeforeComponent : public Object {
	public:
		i32 persisted = 5;
		i32 runtimeOnly = 9;

		SLT_REFLECT_CLASS(TransientBeforeComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(persisted)
		SLT_REFLECT_FIELD(runtimeOnly)
		SLT_END_REFLECT()
	};

	class RefComponent : public Object {
	public:
		Handle<Object> target;
		i32 marker = 5;

		SLT_REFLECT_CLASS(RefComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(target)
		SLT_REFLECT_FIELD(marker)
		SLT_END_REFLECT()
	};

	class GuidComponent : public Object {
	public:
		Guid id;
		i32 tag = 0;

		SLT_REFLECT_CLASS(GuidComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(id)
		SLT_REFLECT_FIELD(tag)
		SLT_END_REFLECT()
	};

	std::unordered_map<u64, Guid> _idToGuid;
	std::unordered_map<Guid, u64> _guidToId;

	bool FakeToGuid(u64 instanceId, Guid* outGuid, StringHash* outPointee) {
		auto it = _idToGuid.find(instanceId);
		if (it == _idToGuid.end()) {
			return false;
		}
		*outGuid = it->second;
		*outPointee = StringHash("FakeAsset");
		return true;
	}

	u64 FakeToHandle(const Guid& guid, StringHash) {
		auto it = _guidToId.find(guid);
		return it != _guidToId.end() ? it->second : 0;
	}

} // namespace

TEST(BinarySerializerTest, RoundTripsPrimitives) {
	PrimitiveComponent src;
	src.health = 73.5f;
	src.damage = -42;
	src.level = 9000;
	src.alive = false;

	Buffer buffer = BinarySerializer::Serialize(&src, PrimitiveComponent::GetReflectType());
	ASSERT_TRUE(buffer);

	PrimitiveComponent dst;
	ASSERT_TRUE(BinarySerializer::Deserialize(&dst, PrimitiveComponent::GetReflectType(), buffer));

	EXPECT_FLOAT_EQ(dst.health, 73.5f);
	EXPECT_EQ(dst.damage, -42);
	EXPECT_EQ(dst.level, 9000u);
	EXPECT_FALSE(dst.alive);
}

TEST(BinarySerializerTest, HeaderBeginsWithMagic) {
	PrimitiveComponent src;
	Buffer buffer = BinarySerializer::Serialize(&src, PrimitiveComponent::GetReflectType());
	ASSERT_TRUE(buffer);
	ASSERT_GE(buffer.size, 4u);
	EXPECT_EQ(buffer.data[0], 'B');
	EXPECT_EQ(buffer.data[1], 'T');
	EXPECT_EQ(buffer.data[2], 'L');
	EXPECT_EQ(buffer.data[3], 'S');
}

TEST(BinarySerializerTest, RoundTripsSubobject) {
	TransformComponent src;
	src.transform.x = 1.5f;
	src.transform.y = -2.5f;
	src.transform.z = 100.0f;
	src.tag = 256;

	Buffer buffer = BinarySerializer::Serialize(&src, TransformComponent::GetReflectType());
	ASSERT_TRUE(buffer);

	TransformComponent dst;
	ASSERT_TRUE(BinarySerializer::Deserialize(&dst, TransformComponent::GetReflectType(), buffer));

	EXPECT_FLOAT_EQ(dst.transform.x, 1.5f);
	EXPECT_FLOAT_EQ(dst.transform.y, -2.5f);
	EXPECT_FLOAT_EQ(dst.transform.z, 100.0f);
	EXPECT_EQ(dst.tag, 256);
}

TEST(BinarySerializerTest, RoundTripsNestedSubobjects) {
	DeepComponent src;
	src.mid.leaf.value = 111;
	src.mid.value = 222;
	src.value = 333;

	Buffer buffer = BinarySerializer::Serialize(&src, DeepComponent::GetReflectType());
	ASSERT_TRUE(buffer);

	DeepComponent dst;
	ASSERT_TRUE(BinarySerializer::Deserialize(&dst, DeepComponent::GetReflectType(), buffer));

	EXPECT_EQ(dst.mid.leaf.value, 111);
	EXPECT_EQ(dst.mid.value, 222);
	EXPECT_EQ(dst.value, 333);
}

TEST(BinarySerializerTest, RoundTripsString) {
	NamedComponent src;
	src.name = "Shalltear the Bloodfallen";
	src.id = 1;

	Buffer buffer = BinarySerializer::Serialize(&src, NamedComponent::GetReflectType());
	ASSERT_TRUE(buffer);

	NamedComponent dst;
	ASSERT_TRUE(BinarySerializer::Deserialize(&dst, NamedComponent::GetReflectType(), buffer));

	EXPECT_EQ(dst.name, "Shalltear the Bloodfallen");
	EXPECT_EQ(dst.id, 1);
}

TEST(BinarySerializerTest, RoundTripsEmptyString) {
	NamedComponent src;
	src.name = "";
	src.id = 2;

	Buffer buffer = BinarySerializer::Serialize(&src, NamedComponent::GetReflectType());
	ASSERT_TRUE(buffer);

	NamedComponent dst;
	dst.name = "stale";
	ASSERT_TRUE(BinarySerializer::Deserialize(&dst, NamedComponent::GetReflectType(), buffer));

	EXPECT_EQ(dst.name, "");
	EXPECT_EQ(dst.id, 2);
}

TEST(BinarySerializerTest, UnknownFieldsAreSkipped) {
	WideComponent src;
	src.a = 100;
	src.b = 200;
	src.c = 300;

	Buffer buffer = BinarySerializer::Serialize(&src, WideComponent::GetReflectType());
	ASSERT_TRUE(buffer);

	NarrowComponent dst;
	ASSERT_TRUE(BinarySerializer::Deserialize(&dst, NarrowComponent::GetReflectType(), buffer));

	EXPECT_EQ(dst.a, 100);
	EXPECT_EQ(dst.b, 200);
}

TEST(BinarySerializerTest, MissingFieldsKeepDefaults) {
	NarrowComponent src;
	src.a = 7;
	src.b = 8;

	Buffer buffer = BinarySerializer::Serialize(&src, NarrowComponent::GetReflectType());
	ASSERT_TRUE(buffer);

	WideComponent dst;
	ASSERT_TRUE(BinarySerializer::Deserialize(&dst, WideComponent::GetReflectType(), buffer));

	EXPECT_EQ(dst.a, 7);
	EXPECT_EQ(dst.b, 8);
	EXPECT_EQ(dst.c, 33);
}

TEST(BinarySerializerTest, TransientFieldIsNotPersisted) {
	TransientComponent src;
	src.persisted = 50;
	src.runtimeOnly = 999;

	Buffer buffer = BinarySerializer::Serialize(&src, TransientComponent::GetReflectType());
	ASSERT_TRUE(buffer);

	TransientComponent dst;
	dst.runtimeOnly = 1;
	ASSERT_TRUE(BinarySerializer::Deserialize(&dst, TransientComponent::GetReflectType(), buffer));

	EXPECT_EQ(dst.persisted, 50);
	EXPECT_EQ(dst.runtimeOnly, 1);
}

TEST(BinarySerializerTest, StaleRecordCannotWriteIntoTransientField) {
	TransientBeforeComponent src;
	src.persisted = 50;
	src.runtimeOnly = 999;
	Buffer stale = BinarySerializer::Serialize(&src, TransientBeforeComponent::GetReflectType());
	ASSERT_TRUE(stale);

	TransientComponent dst;
	dst.runtimeOnly = 1;
	ASSERT_TRUE(BinarySerializer::Deserialize(&dst, TransientComponent::GetReflectType(), stale));

	EXPECT_EQ(dst.persisted, 50);
	EXPECT_EQ(dst.runtimeOnly, 1);
}

TEST(BinarySerializerTest, RoundTripsHandleThroughResolver) {
	HandleResolver::SetHooks(FakeToGuid, FakeToHandle);

	Object target;
	u64 targetId = target.GetInstanceId();
	Guid guid{0x1111222233334444ULL, 0x5555666677778888ULL};
	_idToGuid[targetId] = guid;
	_guidToId[guid] = targetId;

	RefComponent src;
	src.target = Handle<Object>(&target);
	src.marker = 77;

	Buffer buffer = BinarySerializer::Serialize(&src, RefComponent::GetReflectType());
	ASSERT_TRUE(buffer);

	RefComponent dst;
	dst.marker = 0;
	ASSERT_TRUE(BinarySerializer::Deserialize(&dst, RefComponent::GetReflectType(), buffer));

	EXPECT_EQ(dst.marker, 77);
	EXPECT_EQ(dst.target.GetInstanceId(), targetId);
	EXPECT_EQ(dst.target.Get(), &target);

	_idToGuid.clear();
	_guidToId.clear();
	HandleResolver::ResetHooks();
}

TEST(BinarySerializerTest, NullHandleRoundTripsAsNull) {
	HandleResolver::SetHooks(FakeToGuid, FakeToHandle);

	RefComponent src;
	src.marker = 12;

	Buffer buffer = BinarySerializer::Serialize(&src, RefComponent::GetReflectType());
	ASSERT_TRUE(buffer);

	RefComponent dst;
	ASSERT_TRUE(BinarySerializer::Deserialize(&dst, RefComponent::GetReflectType(), buffer));

	EXPECT_EQ(dst.marker, 12);
	EXPECT_EQ(dst.target.GetInstanceId(), 0u);

	HandleResolver::ResetHooks();
}

TEST(BinarySerializerTest, RejectsEmptyAndTruncatedBuffers) {
	PrimitiveComponent dst;

	Buffer empty;
	EXPECT_FALSE(BinarySerializer::Deserialize(&dst, PrimitiveComponent::GetReflectType(), empty));

	Buffer tooSmall(4, MemTag::Core);
	EXPECT_FALSE(BinarySerializer::Deserialize(&dst, PrimitiveComponent::GetReflectType(), tooSmall));
}

TEST(BinarySerializerTest, RejectsBadMagic) {
	PrimitiveComponent src;
	Buffer buffer = BinarySerializer::Serialize(&src, PrimitiveComponent::GetReflectType());
	ASSERT_TRUE(buffer);

	buffer.data[0] = 0xFF;

	PrimitiveComponent dst;
	EXPECT_FALSE(BinarySerializer::Deserialize(&dst, PrimitiveComponent::GetReflectType(), buffer));
}

TEST(BinarySerializerTest, RejectsNullArguments) {
	PrimitiveComponent src;
	Buffer buffer = BinarySerializer::Serialize(&src, PrimitiveComponent::GetReflectType());

	EXPECT_FALSE(BinarySerializer::Serialize(nullptr, PrimitiveComponent::GetReflectType()));
	EXPECT_FALSE(BinarySerializer::Deserialize(nullptr, PrimitiveComponent::GetReflectType(), buffer));
}

TEST(BinarySerializerTest, VariantLayoutInvariantHolds) {
	EXPECT_EQ(sizeof(Variant), 48u);
}

TEST(BinarySerializerTest, RoundTripsGuidField) {
	GuidComponent src;
	src.id = Guid{0xAABBCCDD11223344ULL, 0x5566778899AABBCCULL};
	src.tag = 7;

	Buffer buffer = BinarySerializer::Serialize(&src, GuidComponent::GetReflectType());
	ASSERT_TRUE(buffer);

	GuidComponent dst;
	ASSERT_TRUE(BinarySerializer::Deserialize(&dst, GuidComponent::GetReflectType(), buffer));

	EXPECT_EQ(dst.id, src.id);
	EXPECT_EQ(dst.tag, 7);
}
