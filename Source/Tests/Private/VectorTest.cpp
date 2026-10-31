#include "TestsPCH.h"

#include "Shalltear/Core/Math/Vector.h"

using namespace Shalltear;

#define EXPECT_VEC2_NEAR(v, ex, ey)      \
	EXPECT_NEAR(v.x, ex, Math::EPSILON); \
	EXPECT_NEAR(v.y, ey, Math::EPSILON)
#define EXPECT_VEC3_NEAR(v, ex, ey, ez)  \
	EXPECT_NEAR(v.x, ex, Math::EPSILON); \
	EXPECT_NEAR(v.y, ey, Math::EPSILON); \
	EXPECT_NEAR(v.z, ez, Math::EPSILON)

TEST(VectorTest, LayoutAndAlignment) {
	EXPECT_EQ(sizeof(Vec2), 8u);
	EXPECT_EQ(sizeof(Vec3), 12u);
	EXPECT_EQ(sizeof(Vec4), 16u);
	EXPECT_EQ(alignof(Vec4), 16u);

	Vec3 v(1.0f, 2.0f, 3.0f);
	const f32* data = v.Data();
	EXPECT_FLOAT_EQ(data[0], 1.0f);
	EXPECT_FLOAT_EQ(data[1], 2.0f);
	EXPECT_FLOAT_EQ(data[2], 3.0f);
}

TEST(VectorTest, Constructors) {
	Vec3 zero;
	EXPECT_EQ(zero, Vec3::Zero());

	Vec3 splat(5.0f);
	EXPECT_EQ(splat, Vec3(5.0f, 5.0f, 5.0f));

	Vec3 widened(Vec2(1.0f, 2.0f), 3.0f);
	EXPECT_EQ(widened, Vec3(1.0f, 2.0f, 3.0f));
	EXPECT_EQ(Vec4(widened, 4.0f).XYZ(), widened);

	Vec3I gridPos(1, -2, 3);
	Vec3 converted(gridPos);
	EXPECT_EQ(converted, Vec3(1.0f, -2.0f, 3.0f));
	EXPECT_EQ(Vec3I(converted), gridPos);
}

TEST(VectorTest, DirectionalConstants) {
	EXPECT_EQ(Vec3::Cross(Vec3::Right(), Vec3::Up()), Vec3::Forward());
	EXPECT_EQ(Vec3::Up(), -Vec3::Down());
	EXPECT_EQ(Vec3::Right(), -Vec3::Left());
	EXPECT_EQ(Vec3::Forward(), -Vec3::Back());
}

TEST(VectorTest, Indexing) {
	Vec4 v(1.0f, 2.0f, 3.0f, 4.0f);
	EXPECT_FLOAT_EQ(v[0], 1.0f);
	EXPECT_FLOAT_EQ(v[3], 4.0f);
	v[2] = 30.0f;
	EXPECT_FLOAT_EQ(v.z, 30.0f);

	Vec2 v2(9.0f, 8.0f);
	EXPECT_FLOAT_EQ(v2[1], 8.0f);
}

TEST(VectorTest, Arithmetic) {
	Vec3 a(1.0f, 2.0f, 3.0f);
	Vec3 b(4.0f, 5.0f, 6.0f);

	EXPECT_EQ(a + b, Vec3(5.0f, 7.0f, 9.0f));
	EXPECT_EQ(b - a, Vec3(3.0f, 3.0f, 3.0f));
	EXPECT_EQ(a * b, Vec3(4.0f, 10.0f, 18.0f));
	EXPECT_EQ(b / a, Vec3(4.0f, 2.5f, 2.0f));
	EXPECT_EQ(a * 2.0f, Vec3(2.0f, 4.0f, 6.0f));
	EXPECT_EQ(2.0f * a, Vec3(2.0f, 4.0f, 6.0f));
	EXPECT_EQ(a / 2.0f, Vec3(0.5f, 1.0f, 1.5f));
	EXPECT_EQ(-a, Vec3(-1.0f, -2.0f, -3.0f));
}

TEST(VectorTest, CompoundAssignment) {
	Vec3 v(1.0f, 2.0f, 3.0f);
	v += Vec3(1.0f, 1.0f, 1.0f);
	EXPECT_EQ(v, Vec3(2.0f, 3.0f, 4.0f));
	v -= Vec3(1.0f, 1.0f, 1.0f);
	v *= 2.0f;
	EXPECT_EQ(v, Vec3(2.0f, 4.0f, 6.0f));
	v /= 2.0f;
	v *= Vec3(2.0f, 3.0f, 4.0f);
	EXPECT_EQ(v, Vec3(2.0f, 6.0f, 12.0f));
	v /= Vec3(2.0f, 3.0f, 4.0f);
	EXPECT_EQ(v, Vec3(1.0f, 2.0f, 3.0f));

	Vec2 v2(1.0f, 2.0f);
	v2 += Vec2(3.0f, 4.0f);
	EXPECT_EQ(v2, Vec2(4.0f, 6.0f));

	Vec4 v4(1.0f, 2.0f, 3.0f, 4.0f);
	v4 *= 2.0f;
	EXPECT_EQ(v4, Vec4(2.0f, 4.0f, 6.0f, 8.0f));
}

TEST(VectorTest, IntegerVectors) {
	Vec3I a(1, 2, 3);
	Vec3I b(4, 5, 6);
	EXPECT_EQ(a + b, Vec3I(5, 7, 9));
	EXPECT_EQ(Vec3I::Dot(a, b), 32);
	EXPECT_EQ(a.LengthSquared(), 14);
	EXPECT_EQ(Vec2I(3, 4).LengthSquared(), 25);
}

TEST(VectorTest, LengthAndNormalize) {
	EXPECT_FLOAT_EQ(Vec3(3.0f, 4.0f, 0.0f).Length(), 5.0f);
	EXPECT_FLOAT_EQ(Vec3(3.0f, 4.0f, 0.0f).LengthSquared(), 25.0f);
	EXPECT_FLOAT_EQ(Vec2(3.0f, 4.0f).Length(), 5.0f);
	EXPECT_FLOAT_EQ(Vec4(2.0f, 0.0f, 0.0f, 0.0f).Length(), 2.0f);

	Vec3 n = Vec3(10.0f, 0.0f, 0.0f).Normalized();
	EXPECT_EQ(n, Vec3::Right());
	EXPECT_NEAR(Vec3(1.0f, 2.0f, 3.0f).Normalized().Length(), 1.0f, Math::EPSILON);

	EXPECT_EQ(Vec3::Zero().Normalized(), Vec3::Zero());

	Vec3 inPlace(0.0f, 5.0f, 0.0f);
	inPlace.Normalize();
	EXPECT_EQ(inPlace, Vec3::Up());
}

TEST(VectorTest, Distance) {
	EXPECT_FLOAT_EQ(Vec3::Distance(Vec3(1.0f, 1.0f, 1.0f), Vec3(4.0f, 5.0f, 1.0f)), 5.0f);
	EXPECT_FLOAT_EQ(Vec3::DistanceSquared(Vec3::Zero(), Vec3(1.0f, 2.0f, 2.0f)), 9.0f);
	EXPECT_FLOAT_EQ(Vec2::Distance(Vec2::Zero(), Vec2(3.0f, 4.0f)), 5.0f);
}

TEST(VectorTest, DotAndCross) {
	EXPECT_FLOAT_EQ(Vec3::Dot(Vec3(1.0f, 2.0f, 3.0f), Vec3(4.0f, -5.0f, 6.0f)), 12.0f);
	EXPECT_FLOAT_EQ(Vec3::Dot(Vec3::Right(), Vec3::Up()), 0.0f);

	EXPECT_EQ(Vec3::Cross(Vec3::Right(), Vec3::Up()), Vec3::Forward());
	EXPECT_EQ(Vec3::Cross(Vec3::Up(), Vec3::Right()), Vec3::Back());

	EXPECT_FLOAT_EQ(Vec2::Cross(Vec2::Right(), Vec2::Up()), 1.0f);
	EXPECT_FLOAT_EQ(Vec2::Cross(Vec2::Up(), Vec2::Right()), -1.0f);
	EXPECT_FLOAT_EQ(Vec4::Dot(Vec4::One(), Vec4(1.0f, 2.0f, 3.0f, 4.0f)), 10.0f);
}

TEST(VectorTest, Lerp) {
	Vec3 a = Vec3::Zero();
	Vec3 b(10.0f, 20.0f, 30.0f);
	EXPECT_EQ(Vec3::Lerp(a, b, 0.5f), Vec3(5.0f, 10.0f, 15.0f));
	EXPECT_EQ(Vec3::Lerp(a, b, 2.0f), b);
	EXPECT_EQ(Vec3::LerpUnclamped(a, b, 2.0f), b * 2.0f);
	EXPECT_EQ(Vec2::Lerp(Vec2::Zero(), Vec2(4.0f, 8.0f), 0.25f), Vec2(1.0f, 2.0f));
	EXPECT_EQ(Vec4::Lerp(Vec4::Zero(), Vec4::One(), 0.5f), Vec4(0.5f, 0.5f, 0.5f, 0.5f));
}

TEST(VectorTest, MinMaxAbs) {
	Vec3 a(1.0f, 5.0f, -3.0f);
	Vec3 b(2.0f, 4.0f, -6.0f);
	EXPECT_EQ(Vec3::Min(a, b), Vec3(1.0f, 4.0f, -6.0f));
	EXPECT_EQ(Vec3::Max(a, b), Vec3(2.0f, 5.0f, -3.0f));
	EXPECT_EQ(Vec3::Abs(Vec3(-1.0f, 2.0f, -3.0f)), Vec3(1.0f, 2.0f, 3.0f));
}

TEST(VectorTest, ReflectAndProject) {
	Vec3 reflected = Vec3::Reflect(Vec3(1.0f, -1.0f, 0.0f).Normalized(), Vec3::Up());
	EXPECT_VEC3_NEAR(reflected, 0.70710678f, 0.70710678f, 0.0f);

	Vec3 projected = Vec3::Project(Vec3(3.0f, 4.0f, 0.0f), Vec3::Right());
	EXPECT_EQ(projected, Vec3(3.0f, 0.0f, 0.0f));
	EXPECT_EQ(Vec3::Project(Vec3::One(), Vec3::Zero()), Vec3::Zero());

	Vec3 onPlane = Vec3::ProjectOnPlane(Vec3(3.0f, 4.0f, 5.0f), Vec3::Up());
	EXPECT_VEC3_NEAR(onPlane, 3.0f, 0.0f, 5.0f);

	Vec2 reflected2 = Vec2::Reflect(Vec2(1.0f, -1.0f), Vec2::Up());
	EXPECT_VEC2_NEAR(reflected2, 1.0f, 1.0f);
}

TEST(VectorTest, Angle) {
	EXPECT_NEAR(Vec3::Angle(Vec3::Right(), Vec3::Up()), 90.0f, 1e-3f);
	EXPECT_NEAR(Vec3::Angle(Vec3::Right(), Vec3::Right() * 5.0f), 0.0f, 1e-3f);
	EXPECT_NEAR(Vec3::Angle(Vec3::Right(), Vec3::Left()), 180.0f, 1e-3f);
	EXPECT_FLOAT_EQ(Vec3::Angle(Vec3::Zero(), Vec3::Up()), 0.0f);
}

TEST(VectorTest, Perpendicular) {
	EXPECT_EQ(Vec2::Right().Perpendicular(), Vec2::Up());
	EXPECT_EQ(Vec2::Up().Perpendicular(), Vec2::Left());
}

TEST(VectorTest, EqualityAndApproximately) {
	EXPECT_TRUE(Vec3(1.0f, 2.0f, 3.0f) == Vec3(1.0f, 2.0f, 3.0f));
	EXPECT_TRUE(Vec3(1.0f, 2.0f, 3.0f) != Vec3(1.0f, 2.0f, 3.1f));
	EXPECT_TRUE(Vec3::Approximately(Vec3(1.0f, 2.0f, 3.0f), Vec3(1.0f + Math::EPSILON * 0.5f, 2.0f, 3.0f)));
	EXPECT_FALSE(Vec3::Approximately(Vec3::Zero(), Vec3(0.001f, 0.0f, 0.0f)));
}

TEST(VectorTest, Formatting) {
	EXPECT_EQ(Vec3I(1, 2, 3).ToString(), "(1, 2, 3)");
	EXPECT_EQ(std::format("{}", Vec2I(-1, 4)), "(-1, 4)");
}
