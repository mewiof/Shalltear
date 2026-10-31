#include "TestsPCH.h"

#include "Shalltear/Core/Math/Geometry.h"

using namespace Shalltear;

#define EXPECT_VEC3_NEAR(v, ex, ey, ez) \
	EXPECT_NEAR(v.x, ex, 1e-4f);        \
	EXPECT_NEAR(v.y, ey, 1e-4f);        \
	EXPECT_NEAR(v.z, ez, 1e-4f)

TEST(GeometryTest, Ray) {
	Ray ray(Vec3::Zero(), Vec3(0.0f, 0.0f, 10.0f));
	EXPECT_NEAR(ray.direction.Length(), 1.0f, 1e-5f);
	EXPECT_VEC3_NEAR(ray.GetPoint(5.0f), 0.0f, 0.0f, 5.0f);
}

TEST(GeometryTest, PlaneSignedDistance) {
	Plane floor = Plane::FromPointNormal(Vec3(0.0f, 2.0f, 0.0f), Vec3::Up());
	EXPECT_NEAR(floor.SignedDistance(Vec3(0.0f, 5.0f, 0.0f)), 3.0f, 1e-5f);
	EXPECT_NEAR(floor.SignedDistance(Vec3(10.0f, -1.0f, 5.0f)), -3.0f, 1e-5f);
	EXPECT_TRUE(floor.GetSide(Vec3(0.0f, 3.0f, 0.0f)));
	EXPECT_FALSE(floor.GetSide(Vec3(0.0f, 1.0f, 0.0f)));

	EXPECT_VEC3_NEAR(floor.ClosestPoint(Vec3(4.0f, 7.0f, -2.0f)), 4.0f, 2.0f, -2.0f);
}

TEST(GeometryTest, PlaneFromPoints) {
	Plane plane = Plane::FromPoints(Vec3::Zero(), Vec3(0.0f, 0.0f, 1.0f), Vec3(1.0f, 0.0f, 0.0f));
	EXPECT_TRUE(Vec3::Approximately(plane.normal, Vec3::Up(), 1e-5f));
	EXPECT_NEAR(plane.distance, 0.0f, 1e-5f);
}

TEST(GeometryTest, PlaneRaycast) {
	Plane floor = Plane::FromPointNormal(Vec3::Zero(), Vec3::Up());
	f32 enter = 0.0f;

	ASSERT_TRUE(floor.Raycast(Ray(Vec3(0.0f, 5.0f, 0.0f), Vec3::Down()), enter));
	EXPECT_NEAR(enter, 5.0f, 1e-4f);

	EXPECT_FALSE(floor.Raycast(Ray(Vec3(0.0f, 5.0f, 0.0f), Vec3::Up()), enter));
	EXPECT_FALSE(floor.Raycast(Ray(Vec3(0.0f, 5.0f, 0.0f), Vec3::Forward()), enter));
}

TEST(GeometryTest, SphereContainsAndIntersects) {
	Sphere sphere(Vec3(1.0f, 1.0f, 1.0f), 2.0f);
	EXPECT_TRUE(sphere.Contains(Vec3(1.0f, 1.0f, 1.0f)));
	EXPECT_TRUE(sphere.Contains(Vec3(1.0f, 1.0f, 3.0f)));
	EXPECT_FALSE(sphere.Contains(Vec3(1.0f, 1.0f, 3.1f)));

	EXPECT_TRUE(sphere.Intersects(Sphere(Vec3(4.0f, 1.0f, 1.0f), 1.0f)));
	EXPECT_FALSE(sphere.Intersects(Sphere(Vec3(5.0f, 1.0f, 1.0f), 1.0f)));
}

TEST(GeometryTest, SphereRaycast) {
	Sphere sphere(Vec3(0.0f, 0.0f, 10.0f), 2.0f);
	f32 enter = 0.0f;

	ASSERT_TRUE(sphere.Raycast(Ray(Vec3::Zero(), Vec3::Forward()), enter));
	EXPECT_NEAR(enter, 8.0f, 1e-4f);

	ASSERT_TRUE(sphere.Raycast(Ray(Vec3(0.0f, 0.0f, 10.0f), Vec3::Forward()), enter));
	EXPECT_NEAR(enter, 2.0f, 1e-4f);

	EXPECT_FALSE(sphere.Raycast(Ray(Vec3(0.0f, 5.0f, 0.0f), Vec3::Forward()), enter));
	EXPECT_FALSE(sphere.Raycast(Ray(Vec3::Zero(), Vec3::Back()), enter));
}

TEST(GeometryTest, AABBAccessors) {
	AABB box(Vec3(-1.0f, -2.0f, -3.0f), Vec3(1.0f, 2.0f, 3.0f));
	EXPECT_EQ(box.Center(), Vec3::Zero());
	EXPECT_EQ(box.Size(), Vec3(2.0f, 4.0f, 6.0f));
	EXPECT_EQ(box.Extents(), Vec3(1.0f, 2.0f, 3.0f));
	EXPECT_TRUE(box.IsValid());

	EXPECT_EQ(AABB::FromCenterExtents(Vec3::One(), Vec3(0.5f, 0.5f, 0.5f)).min, Vec3(0.5f, 0.5f, 0.5f));
	EXPECT_FALSE(AABB::Empty().IsValid());
}

TEST(GeometryTest, AABBEncapsulate) {
	AABB box = AABB::Empty();
	box.Encapsulate(Vec3(1.0f, 2.0f, 3.0f));
	EXPECT_EQ(box.min, Vec3(1.0f, 2.0f, 3.0f));
	EXPECT_EQ(box.max, Vec3(1.0f, 2.0f, 3.0f));

	box.Encapsulate(Vec3(-1.0f, 5.0f, 0.0f));
	EXPECT_EQ(box.min, Vec3(-1.0f, 2.0f, 0.0f));
	EXPECT_EQ(box.max, Vec3(1.0f, 5.0f, 3.0f));

	box.Encapsulate(AABB(Vec3(-10.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 1.0f)));
	EXPECT_EQ(box.min, Vec3(-10.0f, 0.0f, 0.0f));

	AABB expanded(Vec3::Zero(), Vec3::One());
	expanded.Expand(1.0f);
	EXPECT_EQ(expanded.min, Vec3(-1.0f, -1.0f, -1.0f));
	EXPECT_EQ(expanded.max, Vec3(2.0f, 2.0f, 2.0f));
}

TEST(GeometryTest, AABBContainsAndIntersects) {
	AABB box(Vec3::Zero(), Vec3(10.0f, 10.0f, 10.0f));
	EXPECT_TRUE(box.Contains(Vec3(5.0f, 5.0f, 5.0f)));
	EXPECT_TRUE(box.Contains(Vec3(0.0f, 0.0f, 0.0f)));
	EXPECT_FALSE(box.Contains(Vec3(5.0f, 11.0f, 5.0f)));
	EXPECT_TRUE(box.Contains(AABB(Vec3::One(), Vec3(2.0f, 2.0f, 2.0f))));
	EXPECT_FALSE(box.Contains(AABB(Vec3::One(), Vec3(11.0f, 2.0f, 2.0f))));

	EXPECT_TRUE(box.Intersects(AABB(Vec3(9.0f, 9.0f, 9.0f), Vec3(12.0f, 12.0f, 12.0f))));
	EXPECT_FALSE(box.Intersects(AABB(Vec3(11.0f, 0.0f, 0.0f), Vec3(12.0f, 1.0f, 1.0f))));

	EXPECT_TRUE(box.Intersects(Sphere(Vec3(12.0f, 5.0f, 5.0f), 3.0f)));
	EXPECT_FALSE(box.Intersects(Sphere(Vec3(15.0f, 5.0f, 5.0f), 3.0f)));

	EXPECT_EQ(box.ClosestPoint(Vec3(15.0f, 5.0f, -3.0f)), Vec3(10.0f, 5.0f, 0.0f));
}

TEST(GeometryTest, AABBRaycast) {
	AABB box(Vec3(-1.0f, -1.0f, 4.0f), Vec3(1.0f, 1.0f, 6.0f));
	f32 enter = 0.0f;

	ASSERT_TRUE(box.Raycast(Ray(Vec3::Zero(), Vec3::Forward()), enter));
	EXPECT_NEAR(enter, 4.0f, 1e-4f);

	ASSERT_TRUE(box.Raycast(Ray(Vec3(0.0f, 0.0f, 5.0f), Vec3::Forward()), enter));
	EXPECT_FLOAT_EQ(enter, 0.0f);

	EXPECT_FALSE(box.Raycast(Ray(Vec3(0.0f, 2.0f, 0.0f), Vec3::Forward()), enter));
	EXPECT_FALSE(box.Raycast(Ray(Vec3::Zero(), Vec3::Back()), enter));
	ASSERT_TRUE(box.Raycast(Ray(Vec3(-2.0f, 0.0f, 3.0f), Vec3(1.0f, 0.0f, 1.0f)), enter));
}

TEST(GeometryTest, AABBTransformed) {
	AABB box(Vec3(-1.0f, -1.0f, -1.0f), Vec3::One());

	AABB translated = box.Transformed(Mat4::Translation(Vec3(10.0f, 0.0f, 0.0f)));
	EXPECT_EQ(translated.Center(), Vec3(10.0f, 0.0f, 0.0f));
	EXPECT_EQ(translated.Size(), box.Size());

	AABB slab(Vec3(-2.0f, -1.0f, -0.5f), Vec3(2.0f, 1.0f, 0.5f));
	AABB rotated = slab.Transformed(Mat4::FromQuat(Quat::AngleAxis(90.0f, Vec3::Up())));
	EXPECT_TRUE(Vec3::Approximately(rotated.Extents(), Vec3(0.5f, 1.0f, 2.0f), 1e-4f));
}

TEST(GeometryTest, FrustumContainsPoint) {
	Mat4 viewProj = Mat4::Perspective(90.0f, 1.0f, 0.1f, 100.0f) * Mat4::LookAt(Vec3::Zero(), Vec3::Forward());
	Frustum frustum = Frustum::FromMatrix(viewProj);

	EXPECT_TRUE(frustum.Contains(Vec3(0.0f, 0.0f, 10.0f)));
	EXPECT_TRUE(frustum.Contains(Vec3(5.0f, -5.0f, 10.0f)));
	EXPECT_FALSE(frustum.Contains(Vec3(0.0f, 0.0f, -5.0f)));
	EXPECT_FALSE(frustum.Contains(Vec3(0.0f, 0.0f, 0.05f)));
	EXPECT_FALSE(frustum.Contains(Vec3(0.0f, 0.0f, 150.0f)));
	EXPECT_FALSE(frustum.Contains(Vec3(20.0f, 0.0f, 10.0f)));
	EXPECT_FALSE(frustum.Contains(Vec3(0.0f, -20.0f, 10.0f)));
}

TEST(GeometryTest, FrustumIntersectsSphere) {
	Mat4 viewProj = Mat4::Perspective(90.0f, 1.0f, 0.1f, 100.0f);
	Frustum frustum = Frustum::FromMatrix(viewProj);

	EXPECT_TRUE(frustum.Intersects(Sphere(Vec3(0.0f, 0.0f, 50.0f), 1.0f)));
	EXPECT_TRUE(frustum.Intersects(Sphere(Vec3(-12.0f, 0.0f, 10.0f), 5.0f)));
	EXPECT_FALSE(frustum.Intersects(Sphere(Vec3(0.0f, 0.0f, -20.0f), 5.0f)));
	EXPECT_FALSE(frustum.Intersects(Sphere(Vec3(0.0f, 0.0f, 120.0f), 5.0f)));
}

TEST(GeometryTest, FrustumIntersectsAABB) {
	Mat4 viewProj = Mat4::Perspective(90.0f, 1.0f, 0.1f, 100.0f);
	Frustum frustum = Frustum::FromMatrix(viewProj);

	EXPECT_TRUE(frustum.Intersects(AABB::FromCenterExtents(Vec3(0.0f, 0.0f, 50.0f), Vec3::One())));
	EXPECT_TRUE(frustum.Intersects(AABB::FromCenterExtents(Vec3(0.0f, 0.0f, 0.0f), Vec3::One())));
	EXPECT_FALSE(frustum.Intersects(AABB::FromCenterExtents(Vec3(0.0f, 0.0f, -10.0f), Vec3::One())));
	EXPECT_FALSE(frustum.Intersects(AABB::FromCenterExtents(Vec3(50.0f, 0.0f, 10.0f), Vec3::One())));
}

TEST(GeometryTest, FrustumMovesWithCamera) {
	Mat4 view = Mat4::LookAt(Vec3(100.0f, 0.0f, 0.0f), Vec3(100.0f, 0.0f, 10.0f));
	Frustum frustum = Frustum::FromMatrix(Mat4::Perspective(60.0f, 1.0f, 0.1f, 100.0f) * view);
	EXPECT_TRUE(frustum.Contains(Vec3(100.0f, 0.0f, 10.0f)));
	EXPECT_FALSE(frustum.Contains(Vec3(0.0f, 0.0f, 10.0f)));
}

TEST(GeometryTest, RectIAccessors) {
	RectI rect(10, 20, 100, 50);
	EXPECT_EQ(rect.Left(), 10);
	EXPECT_EQ(rect.Top(), 20);
	EXPECT_EQ(rect.Right(), 110);
	EXPECT_EQ(rect.Bottom(), 70);
	EXPECT_EQ(rect.Center(), Vec2I(60, 45));
	EXPECT_EQ(rect, RectI(Vec2I(10, 20), Vec2I(100, 50)));
	EXPECT_NE(rect, RectI());
}

TEST(GeometryTest, RectIContainsIsHalfOpen) {
	RectI rect(0, 0, 10, 10);
	EXPECT_TRUE(rect.Contains(Vec2I(0, 0)));
	EXPECT_TRUE(rect.Contains(Vec2I(9, 9)));
	EXPECT_FALSE(rect.Contains(Vec2I(10, 0)));
	EXPECT_FALSE(rect.Contains(Vec2I(0, 10)));
	EXPECT_FALSE(rect.Contains(Vec2I(-1, 5)));

	RectI neighbor(10, 0, 10, 10);
	EXPECT_TRUE(neighbor.Contains(Vec2I(10, 0)));
}

TEST(GeometryTest, RectIIntersects) {
	RectI a(0, 0, 10, 10);
	EXPECT_TRUE(a.Intersects(RectI(5, 5, 10, 10)));
	EXPECT_TRUE(a.Intersects(RectI(-5, -5, 10, 10)));
	EXPECT_FALSE(a.Intersects(RectI(10, 0, 10, 10)));
	EXPECT_FALSE(a.Intersects(RectI(0, 10, 10, 10)));
	EXPECT_FALSE(a.Intersects(RectI(20, 20, 5, 5)));
}
