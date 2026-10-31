#include "TestsPCH.h"

#include "Shalltear/Core/Math/Quat.h"

using namespace Shalltear;

#define EXPECT_QUAT_NEAR(q, ex, ey, ez, ew) \
	EXPECT_NEAR(q.x, ex, 1e-4f);            \
	EXPECT_NEAR(q.y, ey, 1e-4f);            \
	EXPECT_NEAR(q.z, ez, 1e-4f);            \
	EXPECT_NEAR(q.w, ew, 1e-4f)
#define EXPECT_VEC3_NEAR(v, ex, ey, ez) \
	EXPECT_NEAR(v.x, ex, 1e-4f);        \
	EXPECT_NEAR(v.y, ey, 1e-4f);        \
	EXPECT_NEAR(v.z, ez, 1e-4f)

TEST(QuatTest, DefaultIsIdentity) {
	Quat q;
	EXPECT_EQ(q, Quat::Identity());
	EXPECT_EQ(q * Vec3::Forward(), Vec3::Forward());
}

TEST(QuatTest, LayoutAndAlignment) {
	EXPECT_EQ(sizeof(Quat), 16u);
	EXPECT_EQ(alignof(Quat), 16u);
}

TEST(QuatTest, Multiply) {
	Quat result = Quat(-1.0f, 2.0f, -3.0f, 4.0f) * Quat(-5.0f, 6.0f, -7.0f, 8.0f);
	EXPECT_QUAT_NEAR(result, -24.0f, 48.0f, -48.0f, -6.0f);
}

TEST(QuatTest, IdentityIsNeutral) {
	Quat q = Quat::AngleAxis(37.0f, Vec3(1.0f, 2.0f, 3.0f));
	EXPECT_TRUE(Quat::Approximately(q * Quat::Identity(), q));
	EXPECT_TRUE(Quat::Approximately(Quat::Identity() * q, q));
}

TEST(QuatTest, RotateVector) {
	Quat yaw90 = Quat::AngleAxis(90.0f, Vec3::Up());
	EXPECT_VEC3_NEAR((yaw90 * Vec3::Forward()), 1.0f, 0.0f, 0.0f);
	EXPECT_VEC3_NEAR((yaw90 * Vec3::Right()), 0.0f, 0.0f, -1.0f);

	Quat pitch90 = Quat::AngleAxis(90.0f, Vec3::Right());
	EXPECT_VEC3_NEAR((pitch90 * Vec3::Up()), 0.0f, 0.0f, 1.0f);

	Vec3 rotated = Quat::AngleAxis(33.0f, Vec3(1.0f, 1.0f, 0.0f)) * Vec3(2.0f, -3.0f, 4.0f);
	EXPECT_NEAR(rotated.Length(), Vec3(2.0f, -3.0f, 4.0f).Length(), 1e-4f);
}

TEST(QuatTest, ComposedRotationAppliesRightToLeft) {
	Quat yaw = Quat::AngleAxis(90.0f, Vec3::Up());
	Quat pitch = Quat::AngleAxis(90.0f, Vec3::Right());
	Vec3 result = (yaw * pitch) * Vec3::Up();
	EXPECT_VEC3_NEAR(result, 1.0f, 0.0f, 0.0f);
}

TEST(QuatTest, FromEulerMatchesTheExpectedRotations) {
	Quat result = Quat::FromEuler(Vec3(50.0f, -10.0f, 1.0f));
	EXPECT_QUAT_NEAR(result, 0.42030f, -0.08266f, 0.04471f, 0.90250f);
	EXPECT_NEAR(result.Length(), 1.0f, 1e-5f);
}

TEST(QuatTest, FromEulerOrderDecomposition) {
	Vec3 angles(20.0f, 40.0f, 60.0f);
	Quat qX = Quat::AngleAxis(angles.x, Vec3::Right());
	Quat qY = Quat::AngleAxis(angles.y, Vec3::Up());
	Quat qZ = Quat::AngleAxis(angles.z, Vec3::Forward());

	EXPECT_TRUE(Quat::Approximately(Quat::FromEuler(angles, RotationOrder::XYZ), qZ * qY * qX));
	EXPECT_TRUE(Quat::Approximately(Quat::FromEuler(angles, RotationOrder::XZY), qY * qZ * qX));
	EXPECT_TRUE(Quat::Approximately(Quat::FromEuler(angles, RotationOrder::YXZ), qZ * qX * qY));
	EXPECT_TRUE(Quat::Approximately(Quat::FromEuler(angles, RotationOrder::YZX), qX * qZ * qY));
	EXPECT_TRUE(Quat::Approximately(Quat::FromEuler(angles, RotationOrder::ZXY), qY * qX * qZ));
	EXPECT_TRUE(Quat::Approximately(Quat::FromEuler(angles, RotationOrder::ZYX), qX * qY * qZ));
}

TEST(QuatTest, ToEulerRoundTrip) {
	const Vec3 cases[] = {
		Vec3(0.0f, 0.0f, 0.0f),
		Vec3(30.0f, 0.0f, 0.0f),
		Vec3(0.0f, 45.0f, 0.0f),
		Vec3(0.0f, 0.0f, 60.0f),
		Vec3(50.0f, -10.0f, 1.0f),
		Vec3(-80.0f, 170.0f, -120.0f),
	};
	for (const Vec3& angles : cases) {
		Quat q = Quat::FromEuler(angles);
		Vec3 recovered = q.ToEuler();
		Quat q2 = Quat::FromEuler(recovered);
		EXPECT_TRUE(Quat::Approximately(q, q2, 1e-4f))
			<< "case " << angles.ToString() << " -> " << recovered.ToString();
	}
}

TEST(QuatTest, ToEulerSimpleAngles) {
	Vec3 euler = Quat::FromEuler(Vec3(30.0f, 40.0f, 20.0f)).ToEuler();
	EXPECT_VEC3_NEAR(euler, 30.0f, 40.0f, 20.0f);
}

TEST(QuatTest, ToEulerReadsQuarterTurnX) {
	EXPECT_NEAR(Quat::FromEuler(Vec3(90.0f, 0.0f, 0.0f)).ToEuler().x, 90.0f, 1e-5f);
	EXPECT_NEAR(Quat::FromEuler(Vec3(-90.0f, 0.0f, 0.0f)).ToEuler().x, -90.0f, 1e-5f);
}

TEST(QuatTest, ToEulerHoldsPrecisionAcrossXRange) {
	f32 worst = 0.0f;
	for (f32 angle = -89.5f; angle <= 89.5f; angle += 0.5f) {
		Vec3 euler = Quat::FromEuler(Vec3(angle, 0.0f, 0.0f)).ToEuler();
		worst = Math::Max(worst, Math::Abs(euler.x - angle));
	}
	EXPECT_LT(worst, 2e-5f);
}

TEST(QuatTest, ToEulerIgnoresTheQuaternionsLength) {
	Quat unit = Quat::FromEuler(Vec3(30.0f, 40.0f, 20.0f));
	Quat longer(unit.x * 1.5f, unit.y * 1.5f, unit.z * 1.5f, unit.w * 1.5f);
	Vec3 euler = longer.ToEuler();
	EXPECT_VEC3_NEAR(euler, 30.0f, 40.0f, 20.0f);
}

TEST(QuatTest, ConjugateAndInverse) {
	Quat q = Quat::AngleAxis(72.0f, Vec3(1.0f, -2.0f, 0.5f));
	EXPECT_TRUE(Quat::Approximately(q * q.Inverse(), Quat::Identity()));
	EXPECT_TRUE(Quat::Approximately(q.Inverse() * q, Quat::Identity()));
	EXPECT_TRUE(Quat::Approximately(q.Conjugated(), q.Inverse()));

	Quat scaled(q.x * 3.0f, q.y * 3.0f, q.z * 3.0f, q.w * 3.0f);
	Quat product = scaled * scaled.Inverse();
	EXPECT_QUAT_NEAR(product, 0.0f, 0.0f, 0.0f, 1.0f);
}

TEST(QuatTest, Normalize) {
	Quat q(0.0f, 0.0f, 0.0f, 10.0f);
	EXPECT_TRUE(q.Normalized() == Quat::Identity());
	EXPECT_EQ(Quat(0.0f, 0.0f, 0.0f, 0.0f).Normalized(), Quat::Identity());
}

TEST(QuatTest, LookRotation) {
	EXPECT_TRUE(Quat::Approximately(Quat::LookRotation(Vec3::Forward()), Quat::Identity()));

	Quat lookRight = Quat::LookRotation(Vec3::Right());
	EXPECT_VEC3_NEAR((lookRight * Vec3::Forward()), 1.0f, 0.0f, 0.0f);

	Vec3 target = Vec3(1.0f, 2.0f, 3.0f).Normalized();
	Quat look = Quat::LookRotation(target);
	Vec3 forward = look * Vec3::Forward();
	EXPECT_TRUE(Vec3::Approximately(forward, target, 1e-4f));
	EXPECT_GT(Vec3::Dot(look * Vec3::Up(), Vec3::Up()), 0.0f);

	EXPECT_EQ(Quat::LookRotation(Vec3::Zero()), Quat::Identity());
	Quat lookUp = Quat::LookRotation(Vec3::Up());
	EXPECT_VEC3_NEAR((lookUp * Vec3::Forward()), 0.0f, 1.0f, 0.0f);
}

TEST(QuatTest, SlerpEndpointsAndMidpoint) {
	Quat a = Quat::Identity();
	Quat b = Quat::AngleAxis(90.0f, Vec3::Up());

	EXPECT_TRUE(Quat::Approximately(Quat::Slerp(a, b, 0.0f), a));
	EXPECT_TRUE(Quat::Approximately(Quat::Slerp(a, b, 1.0f), b));
	EXPECT_TRUE(Quat::Approximately(Quat::Slerp(a, b, 0.5f), Quat::AngleAxis(45.0f, Vec3::Up())));
	EXPECT_TRUE(Quat::Approximately(Quat::Slerp(a, b, 5.0f), b));
}

TEST(QuatTest, SlerpTakesShortestPath) {
	Quat a = Quat::AngleAxis(10.0f, Vec3::Up());
	Quat b = Quat::AngleAxis(30.0f, Vec3::Up());
	Quat negatedB(-b.x, -b.y, -b.z, -b.w);
	Quat viaB = Quat::Slerp(a, b, 0.5f);
	Quat viaNegated = Quat::Slerp(a, negatedB, 0.5f);
	EXPECT_TRUE(Quat::Approximately(viaB, viaNegated));
	EXPECT_TRUE(Quat::Approximately(viaB, Quat::AngleAxis(20.0f, Vec3::Up())));
}

TEST(QuatTest, SlerpNearlyParallelStaysStable) {
	Quat a = Quat::AngleAxis(10.0f, Vec3::Up());
	Quat b = Quat::AngleAxis(10.001f, Vec3::Up());
	Quat mid = Quat::Slerp(a, b, 0.5f);
	EXPECT_NEAR(mid.Length(), 1.0f, 1e-5f);
	EXPECT_TRUE(Quat::Approximately(mid, a, 1e-4f));
}

TEST(QuatTest, LerpApproximatesSlerpForSmallAngles) {
	Quat a = Quat::Identity();
	Quat b = Quat::AngleAxis(20.0f, Vec3::Up());
	EXPECT_TRUE(Quat::Approximately(Quat::Lerp(a, b, 0.5f), Quat::Slerp(a, b, 0.5f), 1e-4f));
	EXPECT_NEAR(Quat::Lerp(a, b, 0.5f).Length(), 1.0f, 1e-5f);
}

TEST(QuatTest, Angle) {
	Quat a = Quat::Identity();
	EXPECT_NEAR(Quat::Angle(a, Quat::AngleAxis(90.0f, Vec3::Up())), 90.0f, 1e-3f);
	EXPECT_NEAR(Quat::Angle(a, a), 0.0f, 1e-3f);
	Quat b = Quat::AngleAxis(45.0f, Vec3::Right());
	EXPECT_NEAR(Quat::Angle(b, Quat(-b.x, -b.y, -b.z, -b.w)), 0.0f, 1e-2f);
}
