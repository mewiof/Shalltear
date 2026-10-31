#include "TestsPCH.h"

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Math/AnimationCurve.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Reflection/TypeInfo.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Serialization/BinarySerializer.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"

#include <limits>

#ifdef SHALLTEAR_SHARED
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif

using namespace Shalltear;

namespace {

	Keyframe Key(f32 time, f32 value, f32 inTangent = 0.0f, f32 outTangent = 0.0f) {
		Keyframe key;
		key.time = time;
		key.value = value;
		key.inTangent = inTangent;
		key.outTangent = outTangent;
		return key;
	}

	class CurveHolder : public Object {
	public:
		SLT_REFLECT_CLASS(CurveHolder)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_SUBOBJECT(curve)
		SLT_REFLECT_FIELD(marker)
		SLT_END_REFLECT()

		AnimationCurve curve;
		i32 marker = 0;
	};

	void Fill(CurveHolder& holder) {
		Keyframe first = Key(0.0f, 1.0f, 0.0f, -0.5f);
		first.outTangentMode = KeyframeTangentMode::ClampedAuto;
		first.inTangentMode = KeyframeTangentMode::Auto;
		Keyframe middle = Key(0.5f, 0.25f, 1.5f, 0.0f);
		middle.outTangentMode = KeyframeTangentMode::Constant;
		middle.inTangentMode = KeyframeTangentMode::Linear;
		middle.broken = true;
		holder.curve.keys = {first, middle, Key(1.0f, 0.75f, 2.0f, 2.0f)};
		holder.curve.preWrapMode = AnimationCurveWrapMode::PingPong;
		holder.curve.postWrapMode = AnimationCurveWrapMode::Loop;
		holder.marker = 7;
	}

	void ExpectIntact(const CurveHolder& actual, const CurveHolder& expected, const char* via) {
		EXPECT_TRUE(actual.curve == expected.curve) << via;
		EXPECT_EQ(actual.marker, 7) << via;
		EXPECT_FLOAT_EQ(actual.curve.Evaluate(0.75f), 0.25f) << via;
	}

} // anonymous namespace

TEST(AnimationCurveTest, KeysOnCurveSingleKeyConstant) {
	const AnimationCurve curve{Key(0.0f, 1.0f), Key(1.0f, 0.0f)};
	EXPECT_FLOAT_EQ(curve.Evaluate(0.0f), 1.0f);
	EXPECT_FLOAT_EQ(curve.Evaluate(1.0f), 0.0f);

	const AnimationCurve single{Key(0.3f, 0.6f)};
	EXPECT_FLOAT_EQ(single.Evaluate(-5.0f), 0.6f);
	EXPECT_FLOAT_EQ(single.Evaluate(0.3f), 0.6f);
	EXPECT_FLOAT_EQ(single.Evaluate(9.0f), 0.6f);

	EXPECT_FLOAT_EQ(AnimationCurve{}.Evaluate(0.5f), 0.0f);
}

TEST(AnimationCurveTest, SegmentIsScaledHermite) {
	const AnimationCurve curve{Key(0.0f, 0.0f, 0.0f, 2.0f), Key(2.0f, 4.0f, 0.0f, 0.0f)};
	EXPECT_FLOAT_EQ(curve.Evaluate(1.0f), 2.5f);
}

TEST(AnimationCurveTest, TwoFlatKeysEase) {
	const AnimationCurve curve{Key(0.0f, 1.0f), Key(1.0f, 0.0f)};
	EXPECT_FLOAT_EQ(curve.Evaluate(0.25f), 0.84375f);
	EXPECT_FLOAT_EQ(curve.Evaluate(0.5f), 0.5f);
}

TEST(AnimationCurveTest, ConstantSideHoldsLeftKeyAcrossSegment) {
	AnimationCurve curve{Key(0.0f, 1.0f, 0.0f, 1.0f), Key(1.0f, 3.0f, 1.0f, 1.0f), Key(2.0f, 2.0f)};

	curve.keys[0].outTangentMode = KeyframeTangentMode::Constant;
	EXPECT_FLOAT_EQ(curve.Evaluate(0.5f), 1.0f);
	EXPECT_NE(curve.Evaluate(1.5f), 3.0f);

	curve.keys[0].outTangentMode = KeyframeTangentMode::Free;
	curve.keys[1].inTangentMode = KeyframeTangentMode::Constant;
	EXPECT_FLOAT_EQ(curve.Evaluate(0.5f), 1.0f);

	curve.keys[1].inTangentMode = KeyframeTangentMode::Free;
	curve.keys[0].outTangent = std::numeric_limits<f32>::infinity();
	EXPECT_FLOAT_EQ(curve.Evaluate(0.5f), 1.0f);
}

TEST(AnimationCurveTest, EndsClampLoopOrPingPong) {
	AnimationCurve curve{Key(1.0f, 0.0f, 1.0f, 1.0f), Key(3.0f, 2.0f, 1.0f, 1.0f)};
	EXPECT_FLOAT_EQ(curve.Evaluate(0.0f), 0.0f);
	EXPECT_FLOAT_EQ(curve.Evaluate(5.0f), 2.0f);

	curve.postWrapMode = AnimationCurveWrapMode::Loop;
	EXPECT_FLOAT_EQ(curve.Evaluate(4.5f), 1.5f);
	EXPECT_FLOAT_EQ(curve.Evaluate(0.0f), 0.0f);

	curve.postWrapMode = AnimationCurveWrapMode::PingPong;
	EXPECT_FLOAT_EQ(curve.Evaluate(4.5f), 0.5f);

	curve.preWrapMode = AnimationCurveWrapMode::Loop;
	EXPECT_FLOAT_EQ(curve.Evaluate(0.5f), 1.5f);
	curve.preWrapMode = AnimationCurveWrapMode::PingPong;
	EXPECT_FLOAT_EQ(curve.Evaluate(0.5f), 0.5f);
}

TEST(AnimationCurveTest, NonFiniteTimeReadsEnd) {
	AnimationCurve curve{Key(0.0f, 1.0f), Key(1.0f, 4.0f)};
	curve.preWrapMode = AnimationCurveWrapMode::Loop;
	curve.postWrapMode = AnimationCurveWrapMode::PingPong;
	EXPECT_FLOAT_EQ(curve.Evaluate(std::numeric_limits<f32>::quiet_NaN()), 1.0f);
	EXPECT_FLOAT_EQ(curve.Evaluate(std::numeric_limits<f32>::infinity()), 4.0f);
	EXPECT_FLOAT_EQ(curve.Evaluate(-std::numeric_limits<f32>::infinity()), 1.0f);
}

TEST(AnimationCurveTest, CopyKeepsSourcesMemoryAndAssignmentTargets) {
	const AnimationCurve source{{Key(0.0f, 1.0f), Key(1.0f, 0.0f)}, GetMemoryResource(MemTag::Scene)};
	const AnimationCurve copy = source;
	EXPECT_EQ(copy.keys.get_allocator().resource(), GetMemoryResource(MemTag::Scene));
	EXPECT_TRUE(copy == source);

	AnimationCurve target{GetMemoryResource(MemTag::Audio)};
	target = source;
	EXPECT_EQ(target.keys.get_allocator().resource(), GetMemoryResource(MemTag::Audio));
	EXPECT_TRUE(target == source);
}

TEST(AnimationCurveTest, CurveRoundTripsThroughTextAndBinary) {
	CurveHolder source;
	Fill(source);

	const str text = TextSerializer::Serialize(&source, CurveHolder::GetReflectType());
	EXPECT_EQ(text.find("null"), str::npos) << text;
	CurveHolder fromText;
	ASSERT_TRUE(TextSerializer::Deserialize(&fromText, CurveHolder::GetReflectType(), text));
	ExpectIntact(fromText, source, "text");

	const Buffer blob = BinarySerializer::Serialize(&source, CurveHolder::GetReflectType());
	ASSERT_TRUE(blob);
	CurveHolder fromBinary;
	ASSERT_TRUE(BinarySerializer::Deserialize(&fromBinary, CurveHolder::GetReflectType(), blob));
	ExpectIntact(fromBinary, source, "binary");
}

TEST(AnimationCurveTest, CoreRegistersAndOwnsTheCurvesTypes) {
	TypeRegistry::InitializePrimitives();
	const TypeInfo* object = TypeRegistry::FindType(StringHash("Object"));
	ASSERT_NE(object, nullptr);
	for (const char* name : {"Keyframe", "AnimationCurve", "KeyframeTangentMode", "AnimationCurveWrapMode"}) {
		const TypeInfo* type = TypeRegistry::FindType(StringHash(name));
		ASSERT_NE(type, nullptr) << name;
		EXPECT_EQ(type->ownerModule, object->ownerModule) << name;
	}
#ifdef SHALLTEAR_SHARED
	const TypeInfo* curve = TypeRegistry::FindType(StringHash("AnimationCurve"));
	EXPECT_NE(curve->ownerModule, static_cast<const void*>(::GetModuleHandleW(nullptr)));
#endif
}
