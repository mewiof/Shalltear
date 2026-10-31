#include "TestsPCH.h"

#include "Shalltear/Core/Serialization/HandleResolver.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"

#include <algorithm>

using namespace Shalltear;

namespace {

	class TextPrimComponent : public Object {
	public:
		f32 health = 100.0f;
		i32 damage = 10;
		u32 level = 1;
		bool alive = true;

		SLT_REFLECT_CLASS(TextPrimComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(health)
		SLT_REFLECT_FIELD(damage)
		SLT_REFLECT_FIELD(level)
		SLT_REFLECT_FIELD(alive)
		SLT_END_REFLECT()
	};

	struct TextVec {
		f32 x = 0.0f;
		f32 y = 0.0f;

		SLT_REFLECT_VALUE(TextVec)
		SLT_REFLECT_FIELD(x)
		SLT_REFLECT_FIELD(y)
		SLT_END_REFLECT()
	};

	class TextNestedComponent : public Object {
	public:
		TextVec position;
		str label = "origin";

		SLT_REFLECT_CLASS(TextNestedComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_SUBOBJECT(position)
		SLT_REFLECT_FIELD(label)
		SLT_END_REFLECT()
	};

	class TextRefComponent : public Object {
	public:
		Handle<Object> target;
		i32 marker = 0;

		SLT_REFLECT_CLASS(TextRefComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(target)
		SLT_REFLECT_FIELD(marker)
		SLT_END_REFLECT()
	};

	class TextGuidComponent : public Object {
	public:
		Guid id;
		i32 tag = 0;

		SLT_REFLECT_CLASS(TextGuidComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(id)
		SLT_REFLECT_FIELD(tag)
		SLT_END_REFLECT()
	};

	class TextHexNameComponent : public Object {
	public:
		i32 deadbeefcafebabe = 0;
		i32 plain = 0;

		SLT_REFLECT_CLASS(TextHexNameComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(deadbeefcafebabe)
		SLT_REFLECT_FIELD(plain)
		SLT_END_REFLECT()
	};

	class TextReservedKeyComponent : public Object {
	public:
		i32 _type = 0;
		str _opaque;
		bool _opaqueBinary = false;
		i32 ordinary = 0;

		SLT_REFLECT_CLASS(TextReservedKeyComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(_type)
		SLT_REFLECT_FIELD(_opaque)
		SLT_REFLECT_FIELD(_opaqueBinary)
		SLT_REFLECT_FIELD(ordinary)
		SLT_END_REFLECT()
	};

	class TextTransientComponent : public Object {
	public:
		i32 persisted = 5;
		i32 runtimeOnly = 9;

		SLT_REFLECT_CLASS(TextTransientComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(persisted)
		SLT_REFLECT_FIELD(runtimeOnly, SLT_TRANSIENT)
		SLT_END_REFLECT()
	};

	class Cafef00dbaadf00d : public Object {
	public:
		SLT_REFLECT_CLASS(Cafef00dbaadf00d)
		SLT_REFLECT_PARENT(Object)
		SLT_END_REFLECT()
	};

	std::unordered_map<u64, Guid> _idToGuid;
	std::unordered_map<Guid, u64> _guidToId;

	StringHash _writePointee = StringHash("FakeAsset");
	StringHash _readPointee;

	bool FakeToGuid(u64 instanceId, Guid* outGuid, StringHash* outPointee) {
		auto it = _idToGuid.find(instanceId);
		if (it == _idToGuid.end()) {
			return false;
		}
		*outGuid = it->second;
		*outPointee = _writePointee;
		return true;
	}

	u64 FakeToHandle(const Guid& guid, StringHash pointee) {
		_readPointee = pointee;
		auto it = _guidToId.find(guid);
		return it != _guidToId.end() ? it->second : 0;
	}

	void ResetHandleFakes() {
		_idToGuid.clear();
		_guidToId.clear();
		_writePointee = StringHash("FakeAsset");
		_readPointee = StringHash();
		HandleResolver::ResetHooks();
	}

} // namespace

TEST(TextSerializerTest, RoundTripsPrimitives) {
	TextPrimComponent src;
	src.health = 12.5f;
	src.damage = -3;
	src.level = 42;
	src.alive = false;

	str json = TextSerializer::Serialize(&src, TextPrimComponent::GetReflectType());

	TextPrimComponent dst;
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, TextPrimComponent::GetReflectType(), json));

	EXPECT_FLOAT_EQ(dst.health, 12.5f);
	EXPECT_EQ(dst.damage, -3);
	EXPECT_EQ(dst.level, 42u);
	EXPECT_FALSE(dst.alive);
}

TEST(TextSerializerTest, RoundTripsSubobjectAndString) {
	TextNestedComponent src;
	src.position.x = 3.0f;
	src.position.y = 4.0f;
	src.label = "spawn point";

	str json = TextSerializer::Serialize(&src, TextNestedComponent::GetReflectType());

	TextNestedComponent dst;
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, TextNestedComponent::GetReflectType(), json));

	EXPECT_FLOAT_EQ(dst.position.x, 3.0f);
	EXPECT_FLOAT_EQ(dst.position.y, 4.0f);
	EXPECT_EQ(dst.label, "spawn point");
}

TEST(TextSerializerTest, OutputSpellsTypeAndFieldNames) {
	TextPrimComponent src;
	str json = TextSerializer::Serialize(&src, TextPrimComponent::GetReflectType());

	EXPECT_NE(json.find("\"_type\": \"TextPrimComponent\""), str::npos) << json;

	EXPECT_NE(json.find("\"health\""), str::npos) << json;
	EXPECT_NE(json.find("\"alive\""), str::npos) << json;
	EXPECT_EQ(json.find(std::format("{:016x}", StringHash("health").GetValue())), str::npos) << json;
	EXPECT_EQ(json.find(std::format("{:016x}", StringHash("TextPrimComponent").GetValue())), str::npos) << json;
}

TEST(TextSerializerTest, AHashKeyedDocumentStillLoads) {
	str json = std::format(R"({{"_type":"{:016x}","{:016x}":55.0,"{:016x}":-7,"{:016x}":false}})",
						   StringHash("TextPrimComponent").GetValue(), StringHash("health").GetValue(),
						   StringHash("damage").GetValue(), StringHash("alive").GetValue());

	TextPrimComponent dst;
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, TextPrimComponent::GetReflectType(), json));
	EXPECT_FLOAT_EQ(dst.health, 55.0f);
	EXPECT_EQ(dst.damage, -7);
	EXPECT_FALSE(dst.alive);
	EXPECT_EQ(dst.level, 1u);
}

TEST(TextSerializerTest, StaleKeyCannotWriteIntoTransientField) {
	TextTransientComponent src;
	src.persisted = 50;
	src.runtimeOnly = 999;
	str written = TextSerializer::Serialize(&src, TextTransientComponent::GetReflectType());
	EXPECT_EQ(written.find("runtimeOnly"), str::npos) << written;

	str stale = R"({"_type":"TextTransientComponent","persisted":50,"runtimeOnly":999})";
	TextTransientComponent dst;
	dst.runtimeOnly = 1;
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, TextTransientComponent::GetReflectType(), stale));
	EXPECT_EQ(dst.persisted, 50);
	EXPECT_EQ(dst.runtimeOnly, 1);

	str staleHash = std::format(R"({{"{:016x}":777}})", StringHash("runtimeOnly").GetValue());
	TextTransientComponent fromHash;
	fromHash.runtimeOnly = 2;
	ASSERT_TRUE(TextSerializer::Deserialize(&fromHash, TextTransientComponent::GetReflectType(), staleHash));
	EXPECT_EQ(fromHash.runtimeOnly, 2);
}

TEST(TextSerializerTest, HashLikeFieldResolvesToField) {
	TextHexNameComponent src;
	src.deadbeefcafebabe = 7;
	src.plain = 3;

	str json = TextSerializer::Serialize(&src, TextHexNameComponent::GetReflectType());
	ASSERT_NE(json.find("\"deadbeefcafebabe\""), str::npos) << json;

	TextHexNameComponent dst;
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, TextHexNameComponent::GetReflectType(), json));
	EXPECT_EQ(dst.deadbeefcafebabe, 7);
	EXPECT_EQ(dst.plain, 3);

	str legacy = std::format(R"({{"{:016x}":9}})", StringHash("deadbeefcafebabe").GetValue());
	TextHexNameComponent fromHash;
	ASSERT_TRUE(TextSerializer::Deserialize(&fromHash, TextHexNameComponent::GetReflectType(), legacy));
	EXPECT_EQ(fromHash.deadbeefcafebabe, 9);
}

TEST(TextSerializerTest, ReservedKeyFieldKeepsHash) {
	TextReservedKeyComponent src;
	src._type = 3;
	src._opaque = "payload";
	src._opaqueBinary = true;
	src.ordinary = 5;

	str json = TextSerializer::Serialize(&src, TextReservedKeyComponent::GetReflectType());

	EXPECT_NE(json.find("\"_type\": \"TextReservedKeyComponent\""), str::npos) << json;
	EXPECT_EQ(json.find("\"_opaque\""), str::npos) << json;
	EXPECT_EQ(json.find("\"_opaqueBinary\""), str::npos) << json;
	EXPECT_NE(json.find(std::format("{:016x}", StringHash("_opaque").GetValue())), str::npos) << json;

	EXPECT_NE(json.find("\"ordinary\""), str::npos) << json;

	TextReservedKeyComponent dst;
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, TextReservedKeyComponent::GetReflectType(), json));
	EXPECT_EQ(dst._type, 3);
	EXPECT_EQ(dst._opaque, "payload");
	EXPECT_TRUE(dst._opaqueBinary);
	EXPECT_EQ(dst.ordinary, 5);
}

TEST(TextSerializerTest, PrettyOutputIsLineOriented) {
	TextPrimComponent src;
	str json = TextSerializer::Serialize(&src, TextPrimComponent::GetReflectType());

	EXPECT_NE(json.find("\n\t\""), str::npos);

	size_t newlines = static_cast<size_t>(std::count(json.begin(), json.end(), '\n'));
	EXPECT_GE(newlines, 5u);
}

TEST(TextSerializerTest, HandleSerializesAsHandleObject) {
	HandleResolver::SetHooks(FakeToGuid, FakeToHandle);

	Object target;
	u64 targetId = target.GetInstanceId();
	Guid guid{0x00000000abcdef01ULL, 0x0000000012345678ULL};
	_idToGuid[targetId] = guid;
	_guidToId[guid] = targetId;

	TextRefComponent src;
	src.target = Handle<Object>(&target);
	src.marker = 5;

	str json = TextSerializer::Serialize(&src, TextRefComponent::GetReflectType());

	EXPECT_NE(json.find("\"_handle\""), str::npos);
	EXPECT_NE(json.find("\"assetId\""), str::npos);
	EXPECT_NE(json.find(guid.ToString()), str::npos);

	TextRefComponent dst;
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, TextRefComponent::GetReflectType(), json));
	EXPECT_EQ(dst.marker, 5);
	EXPECT_EQ(dst.target.GetInstanceId(), targetId);

	ResetHandleFakes();
}

TEST(TextSerializerTest, HandleSpellsPointeeTypeRegistryKnows) {
	const TypeInfo* pointee = TextPrimComponent::GetReflectType();
	ASSERT_NE(TypeRegistry::FindType(pointee->name), nullptr);

	_writePointee = pointee->name;
	HandleResolver::SetHooks(FakeToGuid, FakeToHandle);

	Object target;
	Guid guid{0x00000000abcdef02ULL, 0x0000000012345679ULL};
	_idToGuid[target.GetInstanceId()] = guid;
	_guidToId[guid] = target.GetInstanceId();

	TextRefComponent src;
	src.target = Handle<Object>(&target);
	str json = TextSerializer::Serialize(&src, TextRefComponent::GetReflectType());
	EXPECT_NE(json.find("\"pointeeType\": \"TextPrimComponent\""), str::npos) << json;

	TextRefComponent dst;
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, TextRefComponent::GetReflectType(), json));
	EXPECT_EQ(_readPointee, pointee->name);
	EXPECT_EQ(dst.target.GetInstanceId(), target.GetInstanceId());

	ResetHandleFakes();
}

TEST(TextSerializerTest, HashLikeTypeResolvesToType) {
	const TypeInfo* pointee = Cafef00dbaadf00d::GetReflectType();
	ASSERT_NE(TypeRegistry::FindType(pointee->name), nullptr);
	ASSERT_NE(pointee->name, StringHash(0xcafef00dbaadf00dULL));

	_writePointee = pointee->name;
	HandleResolver::SetHooks(FakeToGuid, FakeToHandle);

	Object target;
	Guid guid{0x00000000abcdef04ULL, 0x000000001234567bULL};
	_idToGuid[target.GetInstanceId()] = guid;
	_guidToId[guid] = target.GetInstanceId();

	TextRefComponent src;
	src.target = Handle<Object>(&target);
	str json = TextSerializer::Serialize(&src, TextRefComponent::GetReflectType());
	EXPECT_NE(json.find("\"pointeeType\": \"Cafef00dbaadf00d\""), str::npos) << json;

	TextRefComponent dst;
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, TextRefComponent::GetReflectType(), json));
	EXPECT_EQ(_readPointee, pointee->name);

	ResetHandleFakes();
}

TEST(TextSerializerTest, HandleKeepsUnknownTypeHash) {
	StringHash unregistered("NoSuchTypeIsRegisteredUnderThisName");
	ASSERT_EQ(TypeRegistry::FindType(unregistered), nullptr);

	_writePointee = unregistered;
	HandleResolver::SetHooks(FakeToGuid, FakeToHandle);

	Object target;
	Guid guid{0x00000000abcdef03ULL, 0x000000001234567aULL};
	_idToGuid[target.GetInstanceId()] = guid;
	_guidToId[guid] = target.GetInstanceId();

	TextRefComponent src;
	src.target = Handle<Object>(&target);
	str json = TextSerializer::Serialize(&src, TextRefComponent::GetReflectType());
	EXPECT_NE(json.find(std::format("\"pointeeType\": \"{:016x}\"", unregistered.GetValue())), str::npos) << json;

	TextRefComponent dst;
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, TextRefComponent::GetReflectType(), json));
	EXPECT_EQ(_readPointee, unregistered);
	EXPECT_EQ(dst.target.GetInstanceId(), target.GetInstanceId());

	ResetHandleFakes();
}

TEST(TextSerializerTest, IgnoresUnknownKeys) {
	str healthKey = std::format("{:016x}", StringHash("health").GetValue());
	str unknownKey = std::format("{:016x}", StringHash("totally_unknown_field").GetValue());
	str json = "{\n\t\"" + unknownKey + "\": 999,\n\t\"" + healthKey + "\": 55.0\n}";

	TextPrimComponent dst;
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, TextPrimComponent::GetReflectType(), json));
	EXPECT_FLOAT_EQ(dst.health, 55.0f);
}

TEST(TextSerializerTest, RejectsMalformedJSON) {
	TextPrimComponent dst;
	EXPECT_FALSE(TextSerializer::Deserialize(&dst, TextPrimComponent::GetReflectType(), "{ not json"));
	EXPECT_FALSE(TextSerializer::Deserialize(&dst, TextPrimComponent::GetReflectType(), "[1,2,3]"));
	EXPECT_FALSE(TextSerializer::Deserialize(&dst, TextPrimComponent::GetReflectType(), ""));
}

TEST(TextSerializerTest, SerializeRejectsNull) {
	EXPECT_EQ(TextSerializer::Serialize(nullptr, TextPrimComponent::GetReflectType()), "null");
}

TEST(TextSerializerTest, RoundTripsGuidFieldAsHexString) {
	TextGuidComponent src;
	src.id = Guid{0x0123456789abcdefULL, 0xfedcba9876543210ULL};
	src.tag = 5;

	str json = TextSerializer::Serialize(&src, TextGuidComponent::GetReflectType());

	EXPECT_NE(json.find(src.id.ToString()), str::npos);

	TextGuidComponent dst;
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, TextGuidComponent::GetReflectType(), json));
	EXPECT_EQ(dst.id, src.id);
	EXPECT_EQ(dst.tag, 5);
}

namespace {

	struct ScopeMap {
		u64 instanceId = 0;
		Guid localGuid{};
		bool sawWrite = false;
		bool sawRead = false;
	};

	bool ScopeToGuid(void* user, u64 instanceId, Guid* outGuid, StringHash* outPointee) {
		ScopeMap* map = static_cast<ScopeMap*>(user);
		if (instanceId != map->instanceId) {
			return false;
		}
		map->sawWrite = true;
		*outGuid = map->localGuid;
		*outPointee = StringHash("ScopeLocal");
		return true;
	}

	bool ScopeToHandle(void* user, const Guid& guid, StringHash, u64* outId) {
		ScopeMap* map = static_cast<ScopeMap*>(user);
		if (guid != map->localGuid) {
			return false;
		}
		map->sawRead = true;
		*outId = map->instanceId;
		return true;
	}

} // anonymous namespace

TEST(HandleResolverScopeTest, TopScopeInterceptsBothDirections) {
	Object target;
	ScopeMap map;
	map.instanceId = target.GetInstanceId();
	map.localGuid = Guid{42, 0x534C544C4F43414CULL};

	{
		ScopedHandleResolver guard({&ScopeToGuid, &ScopeToHandle, &map});
		ASSERT_EQ(HandleResolver::GetScopeDepth(), 1u);

		Guid guid{};
		StringHash pointee{};
		EXPECT_TRUE(HandleResolver::ToGuid(map.instanceId, &guid, &pointee));
		EXPECT_EQ(guid, map.localGuid);
		EXPECT_TRUE(map.sawWrite);

		EXPECT_EQ(HandleResolver::ToHandle(map.localGuid, StringHash("ScopeLocal")), map.instanceId);
		EXPECT_TRUE(map.sawRead);
	}
	EXPECT_EQ(HandleResolver::GetScopeDepth(), 0u);
}

TEST(HandleResolverScopeTest, UnhandledFallsThroughToBaseHooks) {
	_idToGuid.clear();
	_guidToId.clear();
	HandleResolver::SetHooks(&FakeToGuid, &FakeToHandle);

	Object baseTarget;
	Guid baseGuid{7, 7};
	_idToGuid[baseTarget.GetInstanceId()] = baseGuid;
	_guidToId[baseGuid] = baseTarget.GetInstanceId();

	ScopeMap map;
	map.instanceId = baseTarget.GetInstanceId() + 999;
	map.localGuid = Guid{1, 0x534C544C4F43414CULL};

	{
		ScopedHandleResolver guard({&ScopeToGuid, &ScopeToHandle, &map});

		Guid guid{};
		StringHash pointee{};
		EXPECT_TRUE(HandleResolver::ToGuid(baseTarget.GetInstanceId(), &guid, &pointee));
		EXPECT_EQ(guid, baseGuid);
		EXPECT_EQ(HandleResolver::ToHandle(baseGuid, StringHash("FakeAsset")), baseTarget.GetInstanceId());
	}

	HandleResolver::ResetHooks();
	_idToGuid.clear();
	_guidToId.clear();
}

TEST(HandleResolverScopeTest, ScopeRoundTripsHandleFieldThroughSerializer) {
	Object target;
	ScopeMap map;
	map.instanceId = target.GetInstanceId();
	map.localGuid = Guid{3, 0x534C544C4F43414CULL};

	TextRefComponent src;
	src.target = Handle<Object>(&target);
	src.marker = 21;

	str json;
	{
		ScopedHandleResolver guard({&ScopeToGuid, &ScopeToHandle, &map});
		json = TextSerializer::Serialize(&src, TextRefComponent::GetReflectType());
	}
	EXPECT_TRUE(map.sawWrite);
	EXPECT_NE(json.find(map.localGuid.ToString()), str::npos);

	TextRefComponent dst;
	{
		ScopedHandleResolver guard({&ScopeToGuid, &ScopeToHandle, &map});
		ASSERT_TRUE(TextSerializer::Deserialize(&dst, TextRefComponent::GetReflectType(), json));
	}
	EXPECT_TRUE(map.sawRead);
	EXPECT_EQ(dst.target.GetInstanceId(), target.GetInstanceId());
	EXPECT_EQ(dst.marker, 21);
}

TEST(HandleResolverScopeTest, WithoutScopeSerializesNull) {
	HandleResolver::ResetHooks();

	Object target;
	TextRefComponent src;
	src.target = Handle<Object>(&target);

	str json = TextSerializer::Serialize(&src, TextRefComponent::GetReflectType());

	TextRefComponent dst;
	dst.target = Handle<Object>(&target);
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, TextRefComponent::GetReflectType(), json));
	EXPECT_EQ(dst.target.GetInstanceId(), 0u);
}

namespace TextEnumFixture {
	enum class Facing : u8 {
		North = 0,
		East = 1,
		South = 2,
		West = 3
	};
} // namespace TextEnumFixture

SLT_REFLECT_ENUM(::TextEnumFixture::Facing)
SLT_ENUM_VALUE(North)
SLT_ENUM_VALUE(East)
SLT_ENUM_VALUE(South)
SLT_ENUM_VALUE(West)
SLT_END_ENUM()

namespace {

	class TextEnumComponent : public Object {
	public:
		TextEnumFixture::Facing facing = TextEnumFixture::Facing::North;

		SLT_REFLECT_CLASS(TextEnumComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(facing)
		SLT_END_REFLECT()
	};

	str FacingJSON(std::string_view value) {
		return std::format(R"({{"_type":"{:016x}","{:016x}":{}}})",
						   StringHash("TextEnumComponent").GetValue(),
						   StringHash("facing").GetValue(), value);
	}

} // anonymous namespace

TEST(TextSerializerTest, AnEnumWritesItsConstantName) {
	TextEnumComponent src;
	src.facing = TextEnumFixture::Facing::South;
	str json = TextSerializer::Serialize(&src, TextEnumComponent::GetReflectType());

	EXPECT_NE(json.find("\"South\""), str::npos) << json;
	EXPECT_NE(json.find("\"facing\""), str::npos) << json;

	TextEnumComponent dst;
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, TextEnumComponent::GetReflectType(), json));
	EXPECT_EQ(dst.facing, TextEnumFixture::Facing::South);
}

TEST(TextSerializerTest, AnEnumNameSurvivesItsValueMoving) {
	TextEnumComponent dst;
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, TextEnumComponent::GetReflectType(), FacingJSON("\"West\"")));
	EXPECT_EQ(dst.facing, TextEnumFixture::Facing::West);
}

TEST(TextSerializerTest, IntegerIsStillAcceptedForEnum) {
	TextEnumComponent dst;
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, TextEnumComponent::GetReflectType(), FacingJSON("2")));
	EXPECT_EQ(dst.facing, TextEnumFixture::Facing::South);
}

TEST(TextSerializerTest, AnUnknownEnumNameLeavesTheDefault) {
	TextEnumComponent dst;
	dst.facing = TextEnumFixture::Facing::East;
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, TextEnumComponent::GetReflectType(), FacingJSON("\"Upward\"")));
	EXPECT_EQ(dst.facing, TextEnumFixture::Facing::East);
}

TEST(TextSerializerTest, UnnamedEnumValueRoundTrips) {
	TextEnumComponent src;
	src.facing = static_cast<TextEnumFixture::Facing>(9);
	str json = TextSerializer::Serialize(&src, TextEnumComponent::GetReflectType());
	EXPECT_NE(json.find("9"), str::npos) << json;

	TextEnumComponent dst;
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, TextEnumComponent::GetReflectType(), json));
	EXPECT_EQ(static_cast<u8>(dst.facing), 9u);
}
