#include "TestsPCH.h"

#include "Shalltear/Core/Math/Matrix.h"

using namespace Shalltear;

#define EXPECT_MAT4_NEAR(m1, m2) EXPECT_TRUE(Mat4::Approximately(m1, m2, 1e-4f)) << (m1).ToString() << "\nvs\n" \
																				 << (m2).ToString()
#define EXPECT_MAT3_NEAR(m1, m2) EXPECT_TRUE(Mat3::Approximately(m1, m2, 1e-4f))
#define EXPECT_VEC3_NEAR(v, ex, ey, ez) \
	EXPECT_NEAR(v.x, ex, 1e-4f);        \
	EXPECT_NEAR(v.y, ey, 1e-4f);        \
	EXPECT_NEAR(v.z, ez, 1e-4f)

TEST(MatrixTest, DefaultIsZeroIdentityIsIdentity) {
	Mat4 zero;
	for (size_t i = 0; i < 16; ++i) {
		EXPECT_FLOAT_EQ(zero[i], 0.0f);
	}
	Mat4 identity = Mat4::Identity();
	for (size_t r = 0; r < 4; ++r) {
		for (size_t c = 0; c < 4; ++c) {
			EXPECT_FLOAT_EQ(identity(r, c), r == c ? 1.0f : 0.0f);
		}
	}
}

TEST(MatrixTest, ColumnMajorStorage) {
	Mat4 m = Mat4::FromRows(
		Vec4(11.0f, 12.0f, 13.0f, 14.0f),
		Vec4(21.0f, 22.0f, 23.0f, 24.0f),
		Vec4(31.0f, 32.0f, 33.0f, 34.0f),
		Vec4(41.0f, 42.0f, 43.0f, 44.0f));
	EXPECT_FLOAT_EQ(m(0, 1), 12.0f);
	EXPECT_FLOAT_EQ(m(1, 0), 21.0f);
	EXPECT_FLOAT_EQ(m[4], 12.0f);
	EXPECT_FLOAT_EQ(m[5], 22.0f);
	EXPECT_FLOAT_EQ(m[6], 32.0f);
	EXPECT_FLOAT_EQ(m[7], 42.0f);

	EXPECT_EQ(m.GetRow(1), Vec4(21.0f, 22.0f, 23.0f, 24.0f));
	EXPECT_EQ(m.GetColumn(2), Vec4(13.0f, 23.0f, 33.0f, 43.0f));

	Mat4 fromColumns = Mat4::FromColumns(
		Vec4(11.0f, 21.0f, 31.0f, 41.0f),
		Vec4(12.0f, 22.0f, 32.0f, 42.0f),
		Vec4(13.0f, 23.0f, 33.0f, 43.0f),
		Vec4(14.0f, 24.0f, 34.0f, 44.0f));
	EXPECT_EQ(m, fromColumns);
}

TEST(MatrixTest, SettersAndTranspose) {
	Mat4 m = Mat4::Identity();
	m.SetColumn(3, Vec4(1.0f, 2.0f, 3.0f, 1.0f));
	EXPECT_FLOAT_EQ(m(0, 3), 1.0f);
	EXPECT_FLOAT_EQ(m(2, 3), 3.0f);
	m.SetRow(0, Vec4(9.0f, 8.0f, 7.0f, 6.0f));
	EXPECT_FLOAT_EQ(m(0, 2), 7.0f);

	Mat4 t = m.Transposed();
	for (size_t r = 0; r < 4; ++r) {
		for (size_t c = 0; c < 4; ++c) {
			EXPECT_FLOAT_EQ(t(r, c), m(c, r));
		}
	}
}

TEST(MatrixTest, MultiplyIdentityAndZero) {
	Mat4 m = Mat4::FromRows(
		Vec4(1.0f, 2.0f, 3.0f, 4.0f),
		Vec4(5.0f, 6.0f, 7.0f, 8.0f),
		Vec4(9.0f, 10.0f, 11.0f, 12.0f),
		Vec4(13.0f, 14.0f, 15.0f, 16.0f));
	EXPECT_MAT4_NEAR((m * Mat4::Identity()), m);
	EXPECT_MAT4_NEAR((Mat4::Identity() * m), m);
	EXPECT_MAT4_NEAR((m * Mat4()), Mat4());
}

TEST(MatrixTest, MultiplyArbitraryMatrices) {
	Mat4 a = Mat4::FromRows(
		Vec4(1.0f, 2.0f, 3.0f, 4.0f),
		Vec4(5.0f, 6.0f, 7.0f, 8.0f),
		Vec4(9.0f, 10.0f, 11.0f, 12.0f),
		Vec4(13.0f, 14.0f, 15.0f, 16.0f));
	Mat4 b = Mat4::FromRows(
		Vec4(2.0f, 0.0f, -1.0f, 3.0f),
		Vec4(1.0f, 2.0f, 3.0f, -2.0f),
		Vec4(-2.0f, 1.0f, 0.0f, 4.0f),
		Vec4(3.0f, -3.0f, 2.0f, 1.0f));
	Mat4 expected = Mat4::FromRows(
		Vec4(10.0f, -5.0f, 13.0f, 15.0f),
		Vec4(26.0f, -5.0f, 29.0f, 39.0f),
		Vec4(42.0f, -5.0f, 45.0f, 63.0f),
		Vec4(58.0f, -5.0f, 61.0f, 87.0f));
	EXPECT_MAT4_NEAR((a * b), expected);

	Mat4 compound = a;
	compound *= b;
	EXPECT_MAT4_NEAR(compound, expected);
}

TEST(MatrixTest, MatrixVectorMultiply) {
	Mat4 m = Mat4::Translation(Vec3(10.0f, 20.0f, 30.0f));
	Vec4 p = m * Vec4(1.0f, 2.0f, 3.0f, 1.0f);
	EXPECT_EQ(p, Vec4(11.0f, 22.0f, 33.0f, 1.0f));
	Vec4 d = m * Vec4(1.0f, 2.0f, 3.0f, 0.0f);
	EXPECT_EQ(d, Vec4(1.0f, 2.0f, 3.0f, 0.0f));
}

TEST(MatrixTest, TranslationScalePoint) {
	EXPECT_VEC3_NEAR(Mat4::Translation(Vec3(50.0f, -10.0f, 1.0f)).TransformPoint(Vec3::Zero()), 50.0f, -10.0f, 1.0f);
	EXPECT_VEC3_NEAR(Mat4::Scale(Vec3(1.0f, 2.0f, -3.0f)).TransformPoint(Vec3::One()), 1.0f, 2.0f, -3.0f);
	EXPECT_VEC3_NEAR(Mat4::Translation(Vec3(50.0f, -10.0f, 1.0f)).TransformVector(Vec3::Forward()), 0.0f, 0.0f, 1.0f);
}

TEST(MatrixTest, FromQuatAgreesWithQuatRotation) {
	Quat q = Quat::FromEuler(Vec3(50.0f, -10.0f, 1.0f));
	Mat4 m = Mat4::FromQuat(q);
	const Vec3 probes[] = {Vec3::Right(), Vec3::Up(), Vec3::Forward(), Vec3(1.0f, -2.0f, 3.0f)};
	for (const Vec3& v : probes) {
		Vec3 viaQuat = q * v;
		Vec3 viaMatrix = m.TransformVector(v);
		EXPECT_TRUE(Vec3::Approximately(viaQuat, viaMatrix, 1e-4f)) << v.ToString();
	}
}

TEST(MatrixTest, TRSComposition) {
	Vec3 t(1.0f, 2.0f, 3.0f);
	Quat r = Quat::AngleAxis(90.0f, Vec3::Up());
	Vec3 s(2.0f, 2.0f, 2.0f);

	Mat4 composed = Mat4::Translation(t) * Mat4::FromQuat(r) * Mat4::Scale(s);
	EXPECT_MAT4_NEAR(Mat4::TRS(t, r, s), composed);

	Vec3 p = Mat4::TRS(t, r, s).TransformPoint(Vec3::Forward());
	EXPECT_VEC3_NEAR(p, 3.0f, 2.0f, 3.0f);
}

TEST(MatrixTest, Determinant) {
	EXPECT_FLOAT_EQ(Mat4::Identity().Determinant(), 1.0f);
	EXPECT_FLOAT_EQ(Mat4::Scale(Vec3(2.0f, 3.0f, 4.0f)).Determinant(), 24.0f);
	EXPECT_NEAR(Mat4::FromQuat(Quat::AngleAxis(33.0f, Vec3::Up())).Determinant(), 1.0f, 1e-5f);
	EXPECT_FLOAT_EQ(Mat4().Determinant(), 0.0f);
}

TEST(MatrixTest, Inverse) {
	Mat4 m = Mat4::TRS(
		Vec3(41.2f, 65.8f, 51.2f),
		Quat::FromEuler(Vec3(-0.3f, 55.8f, -81.7f)),
		Vec3(2.0f, 0.5f, 1.5f));
	Mat4 inverse;
	ASSERT_TRUE(m.TryInvert(inverse));
	EXPECT_MAT4_NEAR((m * inverse), Mat4::Identity());
	EXPECT_MAT4_NEAR((inverse * m), Mat4::Identity());
	EXPECT_MAT4_NEAR(m.Inverse(), inverse);

	Vec3 world = m.TransformPoint(Vec3(1.0f, 2.0f, 3.0f));
	EXPECT_VEC3_NEAR(inverse.TransformPoint(world), 1.0f, 2.0f, 3.0f);
}

TEST(MatrixTest, InverseSingularFails) {
	Mat4 singular;
	Mat4 out;
	EXPECT_FALSE(singular.TryInvert(out));
	EXPECT_MAT4_NEAR(out, Mat4::Identity());

	Mat4 flat = Mat4::Scale(Vec3(1.0f, 0.0f, 1.0f));
	EXPECT_FALSE(flat.TryInvert(out));
}

TEST(MatrixTest, LookAt) {
	Mat4 view = Mat4::LookAt(Vec3(0.0f, 0.0f, -5.0f), Vec3::Zero());
	EXPECT_VEC3_NEAR(view.TransformPoint(Vec3::Zero()), 0.0f, 0.0f, 5.0f);
	EXPECT_VEC3_NEAR(view.TransformPoint(Vec3(0.0f, 0.0f, -5.0f)), 0.0f, 0.0f, 0.0f);
	EXPECT_VEC3_NEAR(view.TransformPoint(Vec3(1.0f, 0.0f, 0.0f)), 1.0f, 0.0f, 5.0f);

	EXPECT_MAT4_NEAR(Mat4::LookAt(Vec3::Zero(), Vec3::Forward()), Mat4::Identity());
}

TEST(MatrixTest, PerspectiveDepthRange) {
	Mat4 proj = Mat4::Perspective(90.0f, 1.0f, 0.1f, 100.0f);
	EXPECT_NEAR(proj.ProjectPoint(Vec3(0.0f, 0.0f, 0.1f)).z, 0.0f, 1e-5f);
	EXPECT_NEAR(proj.ProjectPoint(Vec3(0.0f, 0.0f, 100.0f)).z, 1.0f, 1e-5f);

	Vec3 edge = proj.ProjectPoint(Vec3(10.0f, 0.0f, 10.0f));
	EXPECT_NEAR(edge.x, 1.0f, 1e-4f);
	Vec3 center = proj.ProjectPoint(Vec3(0.0f, 0.0f, 50.0f));
	EXPECT_NEAR(center.x, 0.0f, 1e-5f);
	EXPECT_NEAR(center.y, 0.0f, 1e-5f);
}

TEST(MatrixTest, Orthographic) {
	Mat4 proj = Mat4::Orthographic(-10.0f, 10.0f, -5.0f, 5.0f, 0.0f, 100.0f);
	EXPECT_VEC3_NEAR(proj.TransformPoint(Vec3(-10.0f, -5.0f, 0.0f)), -1.0f, -1.0f, 0.0f);
	EXPECT_VEC3_NEAR(proj.TransformPoint(Vec3(10.0f, 5.0f, 100.0f)), 1.0f, 1.0f, 1.0f);
	EXPECT_VEC3_NEAR(proj.TransformPoint(Vec3(0.0f, 0.0f, 50.0f)), 0.0f, 0.0f, 0.5f);
}

TEST(MatrixTest, Mat3Basics) {
	Mat3 identity = Mat3::Identity();
	EXPECT_EQ(identity * Vec3(1.0f, 2.0f, 3.0f), Vec3(1.0f, 2.0f, 3.0f));

	Mat3 m = Mat3::FromRows(
		Vec3(1.0f, 2.0f, 3.0f),
		Vec3(4.0f, 5.0f, 6.0f),
		Vec3(7.0f, 8.0f, 10.0f));
	EXPECT_FLOAT_EQ(m(0, 1), 2.0f);
	EXPECT_FLOAT_EQ(m[1], 4.0f);
	EXPECT_EQ(m.GetColumn(2), Vec3(3.0f, 6.0f, 10.0f));
	EXPECT_EQ(m.GetRow(2), Vec3(7.0f, 8.0f, 10.0f));
	EXPECT_MAT3_NEAR(m.Transposed().Transposed(), m);
	EXPECT_FLOAT_EQ(m.Determinant(), -3.0f);

	Mat3 inverse;
	ASSERT_TRUE(m.TryInvert(inverse));
	EXPECT_MAT3_NEAR((m * inverse), Mat3::Identity());

	Mat3 singular;
	EXPECT_FALSE(singular.TryInvert(inverse));
}

TEST(MatrixTest, Mat3FromQuatMatchesMat4) {
	Quat q = Quat::FromEuler(Vec3(25.0f, -40.0f, 10.0f));
	Mat3 m3 = Mat3::FromQuat(q);
	Mat4 m4 = Mat4::FromQuat(q);
	EXPECT_MAT3_NEAR(m3, m4.ToMat3());
	EXPECT_TRUE(Vec3::Approximately(m3 * Vec3::Forward(), q * Vec3::Forward(), 1e-4f));
	EXPECT_MAT3_NEAR(m3.Inverse(), m3.Transposed());
}

TEST(MatrixTest, DecomposeTRSRoundTripsWhatTRSComposed) {
	const Vec3 translation(3.0f, -7.5f, 2.25f);
	const Quat rotation = Quat::FromEuler(Vec3(25.0f, -40.0f, 10.0f));
	const Vec3 scale(2.0f, 0.5f, 1.25f);

	Vec3 outTranslation;
	Quat outRotation;
	Vec3 outScale;
	EXPECT_TRUE(Mat4::DecomposeTRS(Mat4::TRS(translation, rotation, scale), outTranslation, outRotation, outScale));
	EXPECT_TRUE(Vec3::Approximately(outTranslation, translation, 1e-4f));
	EXPECT_TRUE(Vec3::Approximately(outScale, scale, 1e-4f));
	EXPECT_TRUE(Vec3::Approximately(outRotation * Vec3::Forward(), rotation * Vec3::Forward(), 1e-4f));
	EXPECT_TRUE(Vec3::Approximately(outRotation * Vec3::Right(), rotation * Vec3::Right(), 1e-4f));
}

TEST(MatrixTest, DecomposeFoldsReflectionIntoScale) {
	const Mat4 mirrored = Mat4::TRS(Vec3(), Quat::Identity(), Vec3(-1.0f, 1.0f, 1.0f));

	Vec3 translation;
	Quat rotation;
	Vec3 scale;
	EXPECT_TRUE(Mat4::DecomposeTRS(mirrored, translation, rotation, scale));
	EXPECT_LT(scale.x, 0.0f);
	EXPECT_TRUE(Vec3::Approximately(rotation * Vec3::Up(), Vec3::Up(), 1e-4f));
}

TEST(MatrixTest, DecomposeShearGivesClosestRotation) {
	const Quat authored = Quat::FromEuler(Vec3(0.0f, 35.0f, 0.0f));
	Mat4 sheared = Mat4::FromQuat(authored);
	sheared(0, 1) += 0.2f;

	Vec3 translation;
	Quat rotation;
	Vec3 scale;
	EXPECT_FALSE(Mat4::DecomposeTRS(sheared, translation, rotation, scale));

	EXPECT_NEAR(rotation.Length(), 1.0f, 1e-4f);

	const f32 fitted = Quat::Angle(rotation, authored);
	const f32 identity = Quat::Angle(Quat::Identity(), authored);
	const f32 tilt = std::atan(0.2f) * Math::RAD_TO_DEG;
	EXPECT_LT(fitted, tilt);
	EXPECT_LT(fitted, identity);
}

TEST(MatrixTest, DecomposeTRSRefusesACollapsedAxis) {
	Vec3 translation;
	Quat rotation = Quat::FromEuler(Vec3(10.0f, 20.0f, 30.0f));
	Vec3 scale;
	EXPECT_FALSE(Mat4::DecomposeTRS(Mat4::TRS(Vec3(1.0f, 2.0f, 3.0f), Quat::Identity(), Vec3(1.0f, 0.0f, 1.0f)),
									translation, rotation, scale));
	EXPECT_TRUE(Vec3::Approximately(translation, Vec3(1.0f, 2.0f, 3.0f), 1e-4f));
	EXPECT_TRUE(Vec3::Approximately(rotation * Vec3::Forward(), Vec3::Forward(), 1e-4f));
}
