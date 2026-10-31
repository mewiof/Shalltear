#include "TestsPCH.h"

#include "Shalltear/Core/Math/MathUtil.h"

#include <limits>

using namespace Shalltear;

TEST(MathUtilTest, Constants) {
	EXPECT_NEAR(Math::PI, 3.14159265f, 1e-6f);
	EXPECT_FLOAT_EQ(Math::TAU, Math::PI * 2.0f);
	EXPECT_FLOAT_EQ(Math::HALF_PI, Math::PI * 0.5f);
	EXPECT_NEAR(Math::DEG_TO_RAD * Math::RAD_TO_DEG, 1.0f, 1e-6f);
	EXPECT_NEAR(Math::DegToRad(180.0f), Math::PI, 1e-6f);
	EXPECT_NEAR(Math::RadToDeg(Math::PI), 180.0f, 1e-4f);
}

TEST(MathUtilTest, AbsSignMinMax) {
	EXPECT_EQ(Math::Abs(-5), 5);
	EXPECT_FLOAT_EQ(Math::Abs(-5.5f), 5.5f);
	EXPECT_FLOAT_EQ(Math::Abs(5.5f), 5.5f);

	EXPECT_EQ(Math::Sign(42), 1);
	EXPECT_EQ(Math::Sign(-42), -1);
	EXPECT_EQ(Math::Sign(0), 0);

	EXPECT_EQ(Math::Min(3, 7), 3);
	EXPECT_EQ(Math::Max(3, 7), 7);
	EXPECT_FLOAT_EQ(Math::Min(-1.0f, 1.0f), -1.0f);
}

TEST(MathUtilTest, Clamp) {
	EXPECT_EQ(Math::Clamp(5, 0, 10), 5);
	EXPECT_EQ(Math::Clamp(-5, 0, 10), 0);
	EXPECT_EQ(Math::Clamp(15, 0, 10), 10);
	EXPECT_FLOAT_EQ(Math::Clamp01(1.5f), 1.0f);
	EXPECT_FLOAT_EQ(Math::Clamp01(-0.5f), 0.0f);
	EXPECT_FLOAT_EQ(Math::Clamp01(0.25f), 0.25f);
}

TEST(MathUtilTest, Lerp) {
	EXPECT_FLOAT_EQ(Math::Lerp(0.0f, 10.0f, 0.5f), 5.0f);
	EXPECT_FLOAT_EQ(Math::Lerp(0.0f, 10.0f, 0.0f), 0.0f);
	EXPECT_FLOAT_EQ(Math::Lerp(0.0f, 10.0f, 1.0f), 10.0f);
	EXPECT_FLOAT_EQ(Math::Lerp(0.0f, 10.0f, 2.0f), 10.0f);
	EXPECT_FLOAT_EQ(Math::LerpUnclamped(0.0f, 10.0f, 2.0f), 20.0f);
	EXPECT_FLOAT_EQ(Math::LerpUnclamped(0.0f, 10.0f, -1.0f), -10.0f);
}

TEST(MathUtilTest, InverseLerp) {
	EXPECT_FLOAT_EQ(Math::InverseLerp(0.0f, 10.0f, 5.0f), 0.5f);
	EXPECT_FLOAT_EQ(Math::InverseLerp(0.0f, 10.0f, 20.0f), 1.0f);
	EXPECT_FLOAT_EQ(Math::InverseLerp(3.0f, 3.0f, 3.0f), 0.0f);
}

TEST(MathUtilTest, SmoothStep) {
	EXPECT_FLOAT_EQ(Math::SmoothStep(0.0f, 10.0f, 0.0f), 0.0f);
	EXPECT_FLOAT_EQ(Math::SmoothStep(0.0f, 10.0f, 1.0f), 10.0f);
	EXPECT_FLOAT_EQ(Math::SmoothStep(0.0f, 10.0f, 0.5f), 5.0f);
	EXPECT_LT(Math::SmoothStep(0.0f, 10.0f, 0.25f), 2.5f);
}

TEST(MathUtilTest, MoveTowards) {
	EXPECT_FLOAT_EQ(Math::MoveTowards(0.0f, 10.0f, 3.0f), 3.0f);
	EXPECT_FLOAT_EQ(Math::MoveTowards(0.0f, 10.0f, 30.0f), 10.0f);
	EXPECT_FLOAT_EQ(Math::MoveTowards(0.0f, -10.0f, 3.0f), -3.0f);
}

TEST(MathUtilTest, Repeat) {
	EXPECT_NEAR(Math::Repeat(5.0f, 3.0f), 2.0f, 1e-6f);
	EXPECT_NEAR(Math::Repeat(-1.0f, 3.0f), 2.0f, 1e-6f);
	EXPECT_NEAR(Math::Repeat(1.0f, 3.0f), 1.0f, 1e-6f);
}

TEST(MathUtilTest, Approximately) {
	EXPECT_TRUE(Math::Approximately(1.0f, 1.0f + Math::EPSILON * 0.5f));
	EXPECT_FALSE(Math::Approximately(1.0f, 1.001f));
	EXPECT_TRUE(Math::Approximately(1.0f, 1.5f, 0.6f));
}

TEST(MathUtilTest, IsFinite) {
	EXPECT_TRUE(Math::IsFinite(0.0f));
	EXPECT_TRUE(Math::IsFinite(-1000000.0f));
	EXPECT_TRUE(Math::IsFinite(std::numeric_limits<f32>::max()));
	EXPECT_TRUE(Math::IsFinite(std::numeric_limits<f32>::denorm_min()));
	EXPECT_FALSE(Math::IsFinite(std::numeric_limits<f32>::infinity()));
	EXPECT_FALSE(Math::IsFinite(-std::numeric_limits<f32>::infinity()));
	EXPECT_FALSE(Math::IsFinite(std::numeric_limits<f32>::quiet_NaN()));
	EXPECT_FALSE(Math::IsFinite(std::numeric_limits<f32>::signaling_NaN()));
}

TEST(MathUtilTest, InverseSafe) {
	EXPECT_FLOAT_EQ(Math::InverseSafe(2.0f), 0.5f);
	EXPECT_FLOAT_EQ(Math::InverseSafe(0.0f), 0.0f);
	EXPECT_FLOAT_EQ(Math::InverseSafe(Math::EPSILON * 0.5f), 0.0f);
}
