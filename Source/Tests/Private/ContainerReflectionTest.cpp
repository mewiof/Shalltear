#include "TestsPCH.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Serialization/BinarySerializer.h"
#include "Shalltear/Core/Serialization/HandleResolver.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"

#include <cstring>

using namespace Shalltear;

namespace {

	class ContainerRefComponent : public Object {
	public:
		Vector<Handle<Object>> targets{GetMemoryResource(MemTag::Core)};
		i32 marker = 0;

		SLT_REFLECT_CLASS(ContainerRefComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_ARRAY(targets)
		SLT_REFLECT_FIELD(marker)
		SLT_END_REFLECT()
	};

	class ContainerDroppedComponent : public Object {
	public:
		i32 marker = 0;

		SLT_REFLECT_CLASS(ContainerDroppedComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(marker)
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

	void MapObject(Object& object, u64 low) {
		Guid guid{low, 0x00000000cafe0001ULL};
		_idToGuid[object.GetInstanceId()] = guid;
		_guidToId[guid] = object.GetInstanceId();
	}

	struct HandleHookScope {
		HandleHookScope() { HandleResolver::SetHooks(FakeToGuid, FakeToHandle); }
		~HandleHookScope() {
			_idToGuid.clear();
			_guidToId.clear();
			HandleResolver::ResetHooks();
		}
	};

} // namespace

TEST(ContainerReflectionTest, ArrayFieldIsFlaggedAndDescribesElement) {
	const TypeInfo* type = ContainerRefComponent::GetReflectType();
	const FieldInfo* field = FindField(type, StringHash("targets"));
	ASSERT_NE(field, nullptr);

	EXPECT_TRUE(field->flags & FieldFlagContainer);
	ASSERT_NE(field->container, nullptr);
	EXPECT_EQ(field->container->elementSize, sizeof(Handle<Object>));

	EXPECT_TRUE(field->container->elementFlags & FieldFlagHandle);

	EXPECT_EQ(field->getFn, nullptr);
	EXPECT_EQ(field->setFn, nullptr);
	EXPECT_EQ(field->fieldType, nullptr);
	EXPECT_NE(field->addrFn, nullptr);
}

TEST(ContainerReflectionTest, GetAndSetFieldRefuseAContainer) {
	ContainerRefComponent instance;
	const TypeInfo* type = ContainerRefComponent::GetReflectType();

	Variant out;
	EXPECT_FALSE(GetField(&instance, type, StringHash("targets"), &out));

	Variant in(i64{3});
	EXPECT_FALSE(SetField(&instance, type, StringHash("targets"), &in));

	EXPECT_TRUE(instance.targets.empty());
}

TEST(ContainerReflectionTest, ThunksReadAndResizeThroughTheSeam) {
	ContainerRefComponent instance;
	const FieldInfo* field = FindField(ContainerRefComponent::GetReflectType(), StringHash("targets"));
	ASSERT_NE(field, nullptr);
	ASSERT_NE(field->container, nullptr);

	void* containerPtr = field->addrFn(&instance);
	EXPECT_EQ(field->container->sizeFn(containerPtr), 0u);

	field->container->resizeFn(containerPtr, 3);
	EXPECT_EQ(field->container->sizeFn(containerPtr), 3u);
	EXPECT_EQ(instance.targets.size(), 3u);

	Object target;
	*static_cast<Handle<Object>*>(field->container->atFn(containerPtr, 1)) = Handle<Object>(&target);
	EXPECT_EQ(instance.targets[1].GetInstanceId(), target.GetInstanceId());

	field->container->resizeFn(containerPtr, 1);
	EXPECT_EQ(instance.targets.size(), 1u);
}

TEST(ContainerReflectionTest, TextRoundTripsAnEmptyContainer) {
	ContainerRefComponent src;
	src.marker = 7;

	str json = TextSerializer::Serialize(&src, ContainerRefComponent::GetReflectType());

	ContainerRefComponent dst;
	dst.targets.resize(4);
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, ContainerRefComponent::GetReflectType(), json));
	EXPECT_EQ(dst.marker, 7);
	EXPECT_TRUE(dst.targets.empty());
}

TEST(ContainerReflectionTest, TextRoundTripsHandleElementsInOrder) {
	HandleHookScope hooks;

	Object a;
	Object b;
	Object c;
	MapObject(a, 1);
	MapObject(b, 2);
	MapObject(c, 3);

	ContainerRefComponent src;
	src.targets.push_back(Handle<Object>(&a));
	src.targets.push_back(Handle<Object>(&b));
	src.targets.push_back(Handle<Object>(&c));
	src.marker = 9;

	str json = TextSerializer::Serialize(&src, ContainerRefComponent::GetReflectType());
	EXPECT_NE(json.find("\"_handle\""), str::npos);

	ContainerRefComponent dst;
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, ContainerRefComponent::GetReflectType(), json));
	EXPECT_EQ(dst.marker, 9);
	ASSERT_EQ(dst.targets.size(), 3u);
	EXPECT_EQ(dst.targets[0].GetInstanceId(), a.GetInstanceId());
	EXPECT_EQ(dst.targets[1].GetInstanceId(), b.GetInstanceId());
	EXPECT_EQ(dst.targets[2].GetInstanceId(), c.GetInstanceId());
}

TEST(ContainerReflectionTest, TextShrinksContainerToDocumentLength) {
	HandleHookScope hooks;

	Object a;
	MapObject(a, 1);

	ContainerRefComponent src;
	src.targets.push_back(Handle<Object>(&a));

	str json = TextSerializer::Serialize(&src, ContainerRefComponent::GetReflectType());

	ContainerRefComponent dst;
	dst.targets.resize(5);
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, ContainerRefComponent::GetReflectType(), json));
	ASSERT_EQ(dst.targets.size(), 1u);
	EXPECT_EQ(dst.targets[0].GetInstanceId(), a.GetInstanceId());
}

TEST(ContainerReflectionTest, BinaryRoundTripsHandleElementsInOrder) {
	HandleHookScope hooks;

	Object a;
	Object b;
	MapObject(a, 11);
	MapObject(b, 22);

	ContainerRefComponent src;
	src.targets.push_back(Handle<Object>(&a));
	src.targets.push_back(Handle<Object>(&b));
	src.marker = -4;

	Buffer blob = BinarySerializer::Serialize(&src, ContainerRefComponent::GetReflectType());
	ASSERT_TRUE(blob);

	ContainerRefComponent dst;
	ASSERT_TRUE(BinarySerializer::Deserialize(&dst, ContainerRefComponent::GetReflectType(), blob));
	EXPECT_EQ(dst.marker, -4);
	ASSERT_EQ(dst.targets.size(), 2u);
	EXPECT_EQ(dst.targets[0].GetInstanceId(), a.GetInstanceId());
	EXPECT_EQ(dst.targets[1].GetInstanceId(), b.GetInstanceId());
}

TEST(ContainerReflectionTest, BinaryRoundTripsAnEmptyContainer) {
	ContainerRefComponent src;
	src.marker = 3;

	Buffer blob = BinarySerializer::Serialize(&src, ContainerRefComponent::GetReflectType());
	ASSERT_TRUE(blob);

	ContainerRefComponent dst;
	dst.targets.resize(2);
	ASSERT_TRUE(BinarySerializer::Deserialize(&dst, ContainerRefComponent::GetReflectType(), blob));
	EXPECT_EQ(dst.marker, 3);
	EXPECT_TRUE(dst.targets.empty());
}

TEST(ContainerReflectionTest, MissingArrayReadsRest) {
	HandleHookScope hooks;

	Object a;
	Object b;
	MapObject(a, 31);
	MapObject(b, 32);

	ContainerRefComponent src;
	src.targets.push_back(Handle<Object>(&a));
	src.targets.push_back(Handle<Object>(&b));
	src.marker = 77;

	Buffer blob = BinarySerializer::Serialize(&src, ContainerRefComponent::GetReflectType());
	ASSERT_TRUE(blob);

	ContainerDroppedComponent dst;
	ASSERT_TRUE(BinarySerializer::Deserialize(&dst, ContainerDroppedComponent::GetReflectType(), blob));
	EXPECT_EQ(dst.marker, 77);
}

TEST(ContainerReflectionTest, CorruptCountIsNeverTrustedIntoAllocation) {
	HandleHookScope hooks;

	Object a;
	Object b;
	MapObject(a, 41);
	MapObject(b, 42);

	ContainerRefComponent src;
	src.targets.push_back(Handle<Object>(&a));
	src.targets.push_back(Handle<Object>(&b));
	src.marker = 5;

	Buffer blob = BinarySerializer::Serialize(&src, ContainerRefComponent::GetReflectType());
	ASSERT_TRUE(blob);
	ASSERT_GT(blob.size, 16u);

	for (size_t cut = 1; cut < blob.size; ++cut) {
		Buffer truncated(cut, MemTag::Core);
		std::memcpy(truncated.data, blob.data, cut);

		ContainerRefComponent dst;
		BinarySerializer::Deserialize(&dst, ContainerRefComponent::GetReflectType(), truncated);
		ASSERT_LE(dst.targets.size(), src.targets.size());
		for (const Handle<Object>& element : dst.targets) {
			ASSERT_TRUE(!element.IsValid() || element.GetInstanceId() == a.GetInstanceId() ||
						element.GetInstanceId() == b.GetInstanceId())
				<< cut;
		}
	}
}

namespace {

	struct SequenceProbeElement {
		i32 value = 0;
	};

	class SequenceProbeComponent : public Object {
	public:
		i32 marker = 0;

		SLT_REFLECT_CLASS(SequenceProbeComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(marker)
		SLT_END_REFLECT()
	};

	using namespace Shalltear::ReflectionDetail;

	static_assert(ReflectableSequence<Vector<u8>>);
	static_assert(ReflectableSequence<Vector<Handle<Object>>>);
	static_assert(ReflectableSequence<Vector<SequenceProbeElement>>);

	static_assert(!ReflectableSequence<u8>);
	static_assert(!ReflectableSequence<u64>);
	static_assert(!ReflectableSequence<SequenceProbeElement>);
	static_assert(!ReflectableSequence<SequenceProbeComponent>);
	static_assert(!ReflectableSequence<Handle<Object>>);

	static_assert(ReflectableSequence<str>);

} // namespace

TEST(ContainerReflectionTest, StringResolvesAsPrimitiveNotAsSequence) {
	const TypeInfo* stringType = GetStaticType<str>();
	ASSERT_NE(stringType, nullptr);
	EXPECT_EQ(stringType->name, StringHash("str"));
}

namespace ElementKinds {

	enum class Season : u32 { Spring = 0,
							  Summer = 1 };

	class ElementKindComponent : public Object {
	public:
		Vector<u8> bytes{GetMemoryResource(MemTag::Core)};
		Vector<f32> floats{GetMemoryResource(MemTag::Core)};
		Vector<Vec3> points{GetMemoryResource(MemTag::Core)};
		Vector<Season> seasons{GetMemoryResource(MemTag::Core)};

		SLT_REFLECT_CLASS(ElementKindComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_ARRAY(bytes)
		SLT_REFLECT_ARRAY(floats)
		SLT_REFLECT_ARRAY(points)
		SLT_REFLECT_ARRAY(seasons)
		SLT_END_REFLECT()
	};

	void Fill(ElementKindComponent& out) {
		out.bytes = {1, 2, 250};
		out.floats = {1.5f, 2.5f};
		out.points = {Vec3{1.0f, 2.0f, 3.0f}, Vec3{4.0f, 5.0f, 6.0f}};
		out.seasons = {Season::Summer, Season::Spring};
	}

	void ExpectIntact(const ElementKindComponent& actual, const char* via) {
		ASSERT_EQ(actual.bytes.size(), 3u) << via;
		EXPECT_EQ(actual.bytes[2], 250) << via;
		ASSERT_EQ(actual.floats.size(), 2u) << via;
		EXPECT_FLOAT_EQ(actual.floats[1], 2.5f) << via;
		ASSERT_EQ(actual.points.size(), 2u) << via;
		EXPECT_FLOAT_EQ(actual.points[1].z, 6.0f) << via;
		ASSERT_EQ(actual.seasons.size(), 2u) << via;
		EXPECT_EQ(actual.seasons[0], Season::Summer) << via;
	}

} // namespace ElementKinds

SLT_REFLECT_ENUM(::ElementKinds::Season)
SLT_ENUM_VALUE(Spring)
SLT_ENUM_VALUE(Summer)
SLT_END_ENUM()

TEST(ContainerReflectionTest, PodAndEnumRoundTripText) {
	ElementKinds::ElementKindComponent src;
	ElementKinds::Fill(src);

	str text = TextSerializer::Serialize(&src, ElementKinds::ElementKindComponent::GetReflectType());
	EXPECT_NE(text.find("\"Summer\""), str::npos) << text;

	ElementKinds::ElementKindComponent dst;
	TextSerializer::Deserialize(&dst, ElementKinds::ElementKindComponent::GetReflectType(), text);
	ElementKinds::ExpectIntact(dst, "text");
}

TEST(ContainerReflectionTest, PodAndEnumRoundTripBinary) {
	ElementKinds::ElementKindComponent src;
	ElementKinds::Fill(src);

	Buffer blob = BinarySerializer::Serialize(&src, ElementKinds::ElementKindComponent::GetReflectType());
	ASSERT_TRUE(blob);

	ElementKinds::ElementKindComponent dst;
	BinarySerializer::Deserialize(&dst, ElementKinds::ElementKindComponent::GetReflectType(), blob);
	ElementKinds::ExpectIntact(dst, "binary");
}

namespace StructElements {

	struct Entry {
		str name;
		ElementKinds::Season season = ElementKinds::Season::Spring;
		Handle<Object> target;
		f32 weight = 1.0f;
		Vector<Handle<Object>> extras{GetMemoryResource(MemTag::Core)};

		SLT_REFLECT_VALUE(Entry)
		SLT_REFLECT_FIELD(name)
		SLT_REFLECT_FIELD(season)
		SLT_REFLECT_FIELD(target)
		SLT_REFLECT_FIELD(weight)
		SLT_REFLECT_ARRAY(extras)
		SLT_END_REFLECT()
	};

	class EntryListComponent : public Object {
	public:
		Vector<Entry> entries{GetMemoryResource(MemTag::Core)};
		i32 marker = 0;

		SLT_REFLECT_CLASS(EntryListComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_ARRAY(entries)
		SLT_REFLECT_FIELD(marker)
		SLT_END_REFLECT()
	};

	void Fill(EntryListComponent& out, Object& a, Object& b) {
		out.entries.resize(2);
		out.entries[0].name = "first";
		out.entries[0].season = ElementKinds::Season::Summer;
		out.entries[0].target = Handle<Object>(&a);
		out.entries[0].weight = 0.25f;
		out.entries[1].name = "second";
		out.entries[1].target = Handle<Object>(&b);
		out.entries[1].extras.push_back(Handle<Object>(&a));
		out.entries[1].extras.push_back(Handle<Object>(&b));
		out.marker = 9;
	}

	void ExpectIntact(const EntryListComponent& actual, const Object& a, const Object& b, const char* via) {
		ASSERT_EQ(actual.entries.size(), 2u) << via;
		EXPECT_EQ(actual.entries[0].name, "first") << via;
		EXPECT_EQ(actual.entries[0].season, ElementKinds::Season::Summer) << via;
		EXPECT_EQ(actual.entries[0].target.GetInstanceId(), a.GetInstanceId()) << via;
		EXPECT_FLOAT_EQ(actual.entries[0].weight, 0.25f) << via;
		EXPECT_TRUE(actual.entries[0].extras.empty()) << via;
		EXPECT_EQ(actual.entries[1].name, "second") << via;
		EXPECT_EQ(actual.entries[1].target.GetInstanceId(), b.GetInstanceId()) << via;
		ASSERT_EQ(actual.entries[1].extras.size(), 2u) << via;
		EXPECT_EQ(actual.entries[1].extras[0].GetInstanceId(), a.GetInstanceId()) << via;
		EXPECT_EQ(actual.entries[1].extras[1].GetInstanceId(), b.GetInstanceId()) << via;
		EXPECT_EQ(actual.marker, 9) << via;
	}

} // namespace StructElements

TEST(ContainerReflectionTest, ValueStructElementIsClassifiedAsSubobject) {
	const FieldInfo* field = FindField(StructElements::EntryListComponent::GetReflectType(), StringHash("entries"));
	ASSERT_NE(field, nullptr);
	ASSERT_NE(field->container, nullptr);
	EXPECT_TRUE(field->container->elementFlags & FieldFlagSubobject);
	EXPECT_FALSE(field->container->elementFlags & FieldFlagTriviallyCopyable);

	const FieldInfo* floats = FindField(ElementKinds::ElementKindComponent::GetReflectType(), StringHash("floats"));
	const FieldInfo* points = FindField(ElementKinds::ElementKindComponent::GetReflectType(), StringHash("points"));
	const FieldInfo* seasons = FindField(ElementKinds::ElementKindComponent::GetReflectType(), StringHash("seasons"));
	const FieldInfo* targets = FindField(ContainerRefComponent::GetReflectType(), StringHash("targets"));
	for (const FieldInfo* scalar : {floats, points, seasons, targets}) {
		ASSERT_NE(scalar, nullptr);
		ASSERT_NE(scalar->container, nullptr);
		EXPECT_FALSE(scalar->container->elementFlags & FieldFlagSubobject);
	}
}

TEST(ContainerReflectionTest, ValueStructElementsRoundTripThroughText) {
	HandleHookScope hooks;
	Object a;
	Object b;
	MapObject(a, 31);
	MapObject(b, 32);

	StructElements::EntryListComponent src;
	StructElements::Fill(src, a, b);
	str text = TextSerializer::Serialize(&src, StructElements::EntryListComponent::GetReflectType());
	EXPECT_NE(text.find("\"first\""), str::npos) << text;
	EXPECT_EQ(text.find("null"), str::npos) << text;

	StructElements::EntryListComponent dst;
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, StructElements::EntryListComponent::GetReflectType(), text));
	StructElements::ExpectIntact(dst, a, b, "text");
}

TEST(ContainerReflectionTest, ValueStructElementsRoundTripThroughBinary) {
	HandleHookScope hooks;
	Object a;
	Object b;
	MapObject(a, 41);
	MapObject(b, 42);

	StructElements::EntryListComponent src;
	StructElements::Fill(src, a, b);
	Buffer blob = BinarySerializer::Serialize(&src, StructElements::EntryListComponent::GetReflectType());
	ASSERT_TRUE(blob);

	StructElements::EntryListComponent dst;
	ASSERT_TRUE(BinarySerializer::Deserialize(&dst, StructElements::EntryListComponent::GetReflectType(), blob));
	StructElements::ExpectIntact(dst, a, b, "binary");
}

TEST(ContainerReflectionTest, AShorterStructDocumentShrinksTheVector) {
	HandleHookScope hooks;
	Object a;
	Object b;
	MapObject(a, 51);
	MapObject(b, 52);

	StructElements::EntryListComponent src;
	StructElements::Fill(src, a, b);
	src.entries.pop_back();
	str text = TextSerializer::Serialize(&src, StructElements::EntryListComponent::GetReflectType());

	StructElements::EntryListComponent dst;
	StructElements::Fill(dst, a, b);
	dst.entries[0].name = "stale";
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, StructElements::EntryListComponent::GetReflectType(), text));
	ASSERT_EQ(dst.entries.size(), 1u);
	EXPECT_EQ(dst.entries[0].name, "first");
}
