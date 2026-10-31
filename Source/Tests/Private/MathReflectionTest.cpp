#include "TestsPCH.h"

#include "Shalltear/Core/Math.h"
#include "Shalltear/Core/Serialization/BinarySerializer.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"

using namespace Shalltear;

namespace {

	class MathComponent : public Object {
	public:
		Vec3 position = Vec3::Zero();
		Quat rotation = Quat::Identity();
		Vec3 scale = Vec3::One();
		Color tint = Color::White();
		Vec2 uvOffset = Vec2::Zero();
		Vec4 shaderParams = Vec4::Zero();
		Vec3I gridCell = Vec3I(0, 0, 0);
		Color32 packedTint = Color32::White();

		SLT_REFLECT_CLASS(MathComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(position)
		SLT_REFLECT_FIELD(rotation)
		SLT_REFLECT_FIELD(scale)
		SLT_REFLECT_FIELD(tint)
		SLT_REFLECT_FIELD(uvOffset)
		SLT_REFLECT_FIELD(shaderParams)
		SLT_REFLECT_FIELD(gridCell)
		SLT_REFLECT_FIELD(packedTint)
		SLT_END_REFLECT()
	};

	MathComponent MakePopulated() {
		MathComponent c;
		c.position = Vec3(1.5f, -2.25f, 3.75f);
		c.rotation = Quat::FromEuler(Vec3(50.0f, -10.0f, 1.0f));
		c.scale = Vec3(2.0f, 0.5f, 1.0f);
		c.tint = Color(0.25f, 0.5f, 0.75f, 0.125f);
		c.uvOffset = Vec2(0.1f, 0.9f);
		c.shaderParams = Vec4(-1.0f, 2.0f, -3.0f, 4.0f);
		c.gridCell = Vec3I(-4, 7, 12);
		c.packedTint = Color32(12, 34, 56, 78);
		return c;
	}

	void ExpectEqualToPopulated(const MathComponent& c) {
		MathComponent expected = MakePopulated();
		EXPECT_EQ(c.position, expected.position);
		EXPECT_EQ(c.rotation, expected.rotation);
		EXPECT_EQ(c.scale, expected.scale);
		EXPECT_EQ(c.tint, expected.tint);
		EXPECT_EQ(c.uvOffset, expected.uvOffset);
		EXPECT_EQ(c.shaderParams, expected.shaderParams);
		EXPECT_EQ(c.gridCell, expected.gridCell);
		EXPECT_EQ(c.packedTint, expected.packedTint);
	}

} // anonymous namespace

TEST(MathReflectionTest, MathTypesRegisterAsPrimitives) {
	const TypeInfo* vec3Info = GetStaticType<Vec3>();
	ASSERT_NE(vec3Info, nullptr);
	EXPECT_EQ(vec3Info->name, StringHash("Vec3"));
	EXPECT_EQ(vec3Info->size, sizeof(Vec3));
	EXPECT_TRUE(vec3Info->flags & TypeFlagPrimitive);
	EXPECT_TRUE(vec3Info->flags & TypeFlagTriviallyCopyable);

	EXPECT_EQ(GetStaticType<Quat>()->name, StringHash("Quat"));
	EXPECT_EQ(GetStaticType<Color>()->name, StringHash("Color"));
	EXPECT_EQ(GetStaticType<Color32>()->name, StringHash("Color32"));
	EXPECT_EQ(GetStaticType<Vec2>()->name, StringHash("Vec2"));
	EXPECT_EQ(GetStaticType<Vec4>()->name, StringHash("Vec4"));
	EXPECT_EQ(GetStaticType<Vec2I>()->name, StringHash("Vec2I"));
	EXPECT_EQ(GetStaticType<Vec3I>()->name, StringHash("Vec3I"));
	EXPECT_EQ(GetStaticType<Vec4I>()->name, StringHash("Vec4I"));
}

TEST(MathReflectionTest, VariantStoresMathTypes) {
	Variant v = Vec3(1.0f, 2.0f, 3.0f);
	ASSERT_TRUE(v.Is<Vec3>());
	EXPECT_FALSE(v.IsOnHeap());
	EXPECT_EQ(v.As<Vec3>(), Vec3(1.0f, 2.0f, 3.0f));

	Variant q = Quat::AngleAxis(45.0f, Vec3::Up());
	ASSERT_TRUE(q.Is<Quat>());
	EXPECT_FALSE(q.Is<Vec4>());

	Variant c = Color::Red();
	ASSERT_TRUE(c.Is<Color>());
	EXPECT_EQ(c.As<Color>(), Color::Red());
}

TEST(MathReflectionTest, FieldGetSetThroughReflection) {
	MathComponent component;
	const TypeInfo* type = MathComponent::GetReflectType();
	const FieldInfo* positionField = FindField(type, StringHash("position"));
	ASSERT_NE(positionField, nullptr);
	EXPECT_TRUE(positionField->flags & FieldFlagTriviallyCopyable);

	Variant newValue = Vec3(9.0f, 8.0f, 7.0f);
	positionField->setFn(&component, &newValue);
	EXPECT_EQ(component.position, Vec3(9.0f, 8.0f, 7.0f));

	Variant readBack;
	positionField->getFn(&component, &readBack);
	ASSERT_TRUE(readBack.Is<Vec3>());
	EXPECT_EQ(readBack.As<Vec3>(), Vec3(9.0f, 8.0f, 7.0f));
}

TEST(MathReflectionTest, BinaryRoundTrip) {
	MathComponent src = MakePopulated();
	Buffer buffer = BinarySerializer::Serialize(&src, MathComponent::GetReflectType());
	ASSERT_TRUE(buffer);

	MathComponent dst;
	ASSERT_TRUE(BinarySerializer::Deserialize(&dst, MathComponent::GetReflectType(), buffer));
	ExpectEqualToPopulated(dst);
}

TEST(MathReflectionTest, TextRoundTrip) {
	MathComponent src = MakePopulated();
	str json = TextSerializer::Serialize(&src, MathComponent::GetReflectType());
	ASSERT_FALSE(json.empty());
	EXPECT_NE(json, "null");

	MathComponent dst;
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, MathComponent::GetReflectType(), json));

	ExpectEqualToPopulated(dst);
}

TEST(MathReflectionTest, TextEmitsComponentArrays) {
	MathComponent src;
	src.position = Vec3(1.0f, 2.0f, 3.0f);
	str json = TextSerializer::Serialize(&src, MathComponent::GetReflectType());
	EXPECT_NE(json.find('['), str::npos);
	EXPECT_NE(json.find(']'), str::npos);
}

TEST(MathReflectionTest, TextWritesVectorsComponentsAtPrecision) {
	MathComponent src;
	src.position = Vec3(0.6f, 0.067f, -0.13f);
	src.rotation = Quat::FromEuler(Vec3(50.0f, -30.0f, 0.0f));
	str json = TextSerializer::Serialize(&src, MathComponent::GetReflectType());
	EXPECT_NE(json.find("0.6,"), str::npos) << json;
	EXPECT_NE(json.find("0.067,"), str::npos) << json;
	EXPECT_EQ(json.find("0.6000000238418579"), str::npos);

	MathComponent dst;
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, MathComponent::GetReflectType(), json));
	EXPECT_EQ(dst.position, src.position);
	EXPECT_EQ(dst.rotation.x, src.rotation.x);
	EXPECT_EQ(dst.rotation.y, src.rotation.y);
	EXPECT_EQ(dst.rotation.z, src.rotation.z);
	EXPECT_EQ(dst.rotation.w, src.rotation.w);
}

TEST(MathReflectionTest, TextIgnoresMalformedArray) {
	MathComponent src = MakePopulated();
	str json = TextSerializer::Serialize(&src, MathComponent::GetReflectType());

	size_t open = json.find('[');
	ASSERT_NE(open, str::npos);
	size_t close = json.find(']', open);
	ASSERT_NE(close, str::npos);
	str corrupted = json.substr(0, open + 1) + json.substr(close);

	MathComponent dst;
	ASSERT_TRUE(TextSerializer::Deserialize(&dst, MathComponent::GetReflectType(), corrupted));
}

namespace {

	class RectComponent : public Object {
	public:
		RectI windowRect;

		SLT_REFLECT_CLASS(RectComponent)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(windowRect)
		SLT_END_REFLECT()
	};

} // anonymous namespace

TEST(MathReflectionTest, RectIRegistersAsPrimitive) {
	const TypeInfo* info = GetStaticType<RectI>();
	ASSERT_NE(info, nullptr);
	EXPECT_EQ(info->name, StringHash("RectI"));
	EXPECT_EQ(info->size, sizeof(RectI));
	EXPECT_TRUE(info->flags & TypeFlagPrimitive);
	EXPECT_TRUE(info->flags & TypeFlagTriviallyCopyable);
}

TEST(MathReflectionTest, RectIRoundTripsBothSerializers) {
	RectComponent src;
	src.windowRect = RectI(-1920, 32, 1280, 720);

	Buffer binary = BinarySerializer::Serialize(&src, RectComponent::GetReflectType());
	ASSERT_TRUE(binary);
	RectComponent fromBinary;
	ASSERT_TRUE(BinarySerializer::Deserialize(&fromBinary, RectComponent::GetReflectType(), binary));
	EXPECT_EQ(fromBinary.windowRect, src.windowRect);

	str json = TextSerializer::Serialize(&src, RectComponent::GetReflectType());
	RectComponent fromText;
	ASSERT_TRUE(TextSerializer::Deserialize(&fromText, RectComponent::GetReflectType(), json));
	EXPECT_EQ(fromText.windowRect, src.windowRect);
}
