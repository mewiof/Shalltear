#include "TestsPCH.h"

#include "Shalltear/Core/Math/AnimationCurve.h"
#include "Shalltear/Editor/AnimationCurveOps.h"

using namespace Shalltear;
using namespace Shalltear::AnimationCurveOps;

namespace {

	Keyframe Key(f32 time, f32 value) {
		Keyframe key;
		key.time = time;
		key.value = value;
		return key;
	}

	bool Ascending(const AnimationCurve& curve) {
		for (size_t i = 1; i < curve.keys.size(); ++i) {
			if (curve.keys[i].time < curve.keys[i - 1].time) {
				return false;
			}
		}
		return true;
	}

} // anonymous namespace

TEST(AnimationCurveOpsTest, AddedKeyClampedAutoInOrder) {
	AnimationCurve curve{Key(0.0f, 0.0f), Key(2.0f, 2.0f)};
	const i32 added = AddKey(curve, 1.0f, 5.0f);
	ASSERT_EQ(added, 1);
	ASSERT_EQ(curve.keys.size(), 3u);
	EXPECT_TRUE(Ascending(curve));
	EXPECT_EQ(curve.keys[1].inTangentMode, KeyframeTangentMode::ClampedAuto);
	EXPECT_EQ(curve.keys[1].outTangentMode, KeyframeTangentMode::ClampedAuto);
	EXPECT_EQ(curve.keys[1].outTangent, 0.0f);

	EXPECT_EQ(AddKey(curve, 2.0f, 7.0f), 2);
	EXPECT_EQ(curve.keys.size(), 3u);
	EXPECT_EQ(curve.keys[2].value, 7.0f);
}

TEST(AnimationCurveOpsTest, MovedKeyAnswersNewIndexPastNeighbour) {
	AnimationCurve curve{Key(0.0f, 0.0f), Key(1.0f, 1.0f), Key(2.0f, 0.0f)};
	const i32 moved = MoveKey(curve, 0, 1.5f, 0.25f);
	EXPECT_EQ(moved, 1);
	EXPECT_TRUE(Ascending(curve));
	EXPECT_EQ(curve.keys[1].time, 1.5f);
	EXPECT_EQ(curve.keys[1].value, 0.25f);
	EXPECT_EQ(MoveKey(curve, 7, 0.0f, 0.0f), -1);
	EXPECT_TRUE(RemoveKey(curve, 0));
	EXPECT_EQ(curve.keys.size(), 2u);
}

TEST(AnimationCurveOpsTest, ClampedAutoFlattensTurningPoint) {
	AnimationCurve curve{Key(0.0f, 0.0f), Key(1.0f, 1.5f), Key(2.0f, 1.0f)};
	SetSmooth(curve, 1, KeyframeTangentMode::ClampedAuto);
	EXPECT_EQ(curve.keys[1].outTangent, 0.0f);
	SetSmooth(curve, 1, KeyframeTangentMode::Auto);
	EXPECT_FLOAT_EQ(curve.keys[1].outTangent, 0.5f);
	EXPECT_FLOAT_EQ(curve.keys[1].inTangent, curve.keys[1].outTangent);

	AnimationCurve run{Key(0.0f, 0.0f), Key(1.0f, 1.0f), Key(2.0f, 2.0f)};
	SetSmooth(run, 1, KeyframeTangentMode::ClampedAuto);
	EXPECT_FLOAT_EQ(run.keys[1].outTangent, 1.0f);
}

TEST(AnimationCurveOpsTest, LinearSideFollowsNeighbour) {
	AnimationCurve curve{Key(0.0f, 0.0f), Key(2.0f, 4.0f)};
	curve.keys[0].inTangent = 9.0f;
	SetSideMode(curve, 0, KeySide::Out, KeyframeTangentMode::Linear);
	EXPECT_TRUE(curve.keys[0].broken);
	EXPECT_FLOAT_EQ(curve.keys[0].outTangent, 2.0f);
	EXPECT_EQ(curve.keys[0].inTangent, 9.0f);
	MoveKey(curve, 1, 2.0f, 8.0f);
	EXPECT_FLOAT_EQ(curve.keys[0].outTangent, 4.0f);
}

TEST(AnimationCurveOpsTest, ConstantSideHoldsSegmentItLeaves) {
	AnimationCurve curve{Key(0.0f, 1.0f), Key(1.0f, 3.0f)};
	SetSideMode(curve, 0, KeySide::Out, KeyframeTangentMode::Constant);
	EXPECT_EQ(curve.keys[0].outTangentMode, KeyframeTangentMode::Constant);
	EXPECT_EQ(curve.Evaluate(0.5f), 1.0f);
}

TEST(AnimationCurveOpsTest, HandleDragFreesAutoKey) {
	AnimationCurve curve{Key(0.0f, 0.0f), Key(1.0f, 1.0f), Key(2.0f, 2.0f)};
	SetSmooth(curve, 1, KeyframeTangentMode::ClampedAuto);
	SetTangent(curve, 1, KeySide::Out, 3.0f);
	EXPECT_EQ(curve.keys[1].inTangentMode, KeyframeTangentMode::Free);
	EXPECT_EQ(curve.keys[1].inTangent, 3.0f);
	EXPECT_EQ(curve.keys[1].outTangent, 3.0f);
	MoveKey(curve, 2, 2.0f, 9.0f);
	EXPECT_EQ(curve.keys[1].outTangent, 3.0f);

	SetBroken(curve, 1, true);
	SetTangent(curve, 1, KeySide::Out, -1.0f);
	EXPECT_EQ(curve.keys[1].outTangent, -1.0f);
	EXPECT_EQ(curve.keys[1].inTangent, 3.0f);
}

TEST(AnimationCurveOpsTest, BreakingAutoKeyFreesBothSides) {
	AnimationCurve curve{Key(0.0f, 0.0f), Key(1.0f, 1.0f), Key(2.0f, 2.0f)};
	SetSmooth(curve, 1, KeyframeTangentMode::Auto);
	const f32 slope = curve.keys[1].outTangent;
	SetBroken(curve, 1, true);
	EXPECT_TRUE(curve.keys[1].broken);
	EXPECT_EQ(curve.keys[1].inTangentMode, KeyframeTangentMode::Free);
	EXPECT_EQ(curve.keys[1].outTangentMode, KeyframeTangentMode::Free);
	EXPECT_EQ(curve.keys[1].outTangent, slope);
	MoveKey(curve, 2, 2.0f, 9.0f);
	EXPECT_EQ(curve.keys[1].outTangent, slope);
}

TEST(AnimationCurveOpsTest, FlatIs0JoinedIsFreeSmooth) {
	AnimationCurve curve{Key(0.0f, 0.0f), Key(1.0f, 1.0f), Key(2.0f, 0.0f)};
	curve.keys[1].inTangent = 2.0f;
	curve.keys[1].outTangent = -4.0f;
	Flatten(curve, 1);
	EXPECT_FALSE(curve.keys[1].broken);
	EXPECT_EQ(curve.keys[1].inTangentMode, KeyframeTangentMode::Free);
	EXPECT_EQ(curve.keys[1].inTangent, 0.0f);
	EXPECT_EQ(curve.keys[1].outTangent, 0.0f);

	curve.keys[1].inTangent = 2.0f;
	curve.keys[1].outTangent = -4.0f;
	SetSideMode(curve, 1, KeySide::In, KeyframeTangentMode::Free);
	SetSideMode(curve, 1, KeySide::Out, KeyframeTangentMode::Constant);
	SetBroken(curve, 1, false);
	EXPECT_FALSE(curve.keys[1].broken);
	EXPECT_EQ(curve.keys[1].outTangentMode, KeyframeTangentMode::Free);
	EXPECT_FLOAT_EQ(curve.keys[1].inTangent, -1.0f);
	EXPECT_FLOAT_EQ(curve.keys[1].outTangent, -1.0f);
}
