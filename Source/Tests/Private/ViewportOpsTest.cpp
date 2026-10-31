#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Graphics/PrimitiveMesh.h"
#include "Shalltear/Core/Scene.h"

#include "Shalltear/Editor/EditorGizmoOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorViewportOps.h"
#include "Shalltear/Editor/PanelManager.h"

#include <cmath>

using namespace Shalltear;
using namespace UITest;

namespace Ops = Shalltear::EditorViewportOps;

namespace {

	class ViewportOpsFixture : public ::testing::Test {
	protected:
		Handle<Scene> _scene;
		Handle<MeshAsset> _mesh;
		Handle<MeshAsset> _sphere;

		void SetUp() override {
			SceneSystem::Initialize();
			EditorSelection::Initialize();
			_scene = SceneSystem::CreateScene("ViewportOps");

			MeshAsset* mesh = New<MeshAsset>(MemTag::Asset);
			BuildPrimitiveMesh(PrimitiveShape::Cube, *mesh);
			_mesh = Handle<MeshAsset>(mesh);

			MeshAsset* sphere = New<MeshAsset>(MemTag::Asset);
			BuildPrimitiveMesh(PrimitiveShape::Sphere, *sphere);
			_sphere = Handle<MeshAsset>(sphere);
		}

		void TearDown() override {
			SceneSystem::UnloadAllScenes();
			SceneSystem::EndFrame();
			EditorSelection::Terminate();
			SceneSystem::Terminate();
			if (MeshAsset* mesh = _mesh.Get()) {
				Delete(mesh);
			}
			if (MeshAsset* sphere = _sphere.Get()) {
				Delete(sphere);
			}
		}

		Handle<SceneObject> MakeBox(std::string_view name, Vec3 position) {
			Handle<SceneObject> object = SceneSystem::CreateObject(name, _scene);
			object.Get()->SetLocalPosition(position);
			object.Get()->AddComponent<MeshRenderer>().Get()->SetMesh(_mesh);
			return object;
		}

		Handle<SceneObject> MakeSphere(std::string_view name, Vec3 position) {
			Handle<SceneObject> object = SceneSystem::CreateObject(name, _scene);
			object.Get()->SetLocalPosition(position);
			object.Get()->AddComponent<MeshRenderer>().Get()->SetMesh(_sphere);
			return object;
		}
	};

	struct TestView {
		Mat4 view;
		Mat4 projection;
		Vec2 sizePx{200.0f, 100.0f};
	};

	TestView MakeView(Vec3 eye = Vec3(0.0f, 0.0f, -10.0f), f32 yawDeg = 0.0f, f32 pitchDeg = 0.0f) {
		TestView out;
		out.view = Ops::BuildViewMatrix(eye, Ops::BuildCameraRotation(yawDeg, pitchDeg));
		out.projection = Ops::BuildProjection(out.sizePx.x / out.sizePx.y);
		return out;
	}

} // anonymous namespace

TEST(ViewportOpsCameraTest, RotationYawsWithoutRoll) {
	Vec3 forward = Ops::BuildCameraRotation(90.0f, 0.0f) * Vec3::Forward();
	EXPECT_NEAR(forward.x, 1.0f, 1e-4f);
	EXPECT_NEAR(forward.y, 0.0f, 1e-4f);
	EXPECT_NEAR(forward.z, 0.0f, 1e-4f);

	Quat pitched = Ops::BuildCameraRotation(0.0f, 45.0f);
	EXPECT_LT((pitched * Vec3::Forward()).y, -0.5f);
	EXPECT_NEAR((pitched * Vec3::Right()).y, 0.0f, 1e-4f);
}

TEST(ViewportOpsCameraTest, ViewMatrixInvertsTheCameraTransform) {
	Vec3 eye(3.0f, 4.0f, -5.0f);
	Quat rotation = Ops::BuildCameraRotation(30.0f, 10.0f);
	Mat4 view = Ops::BuildViewMatrix(eye, rotation);
	Vec3 origin = view.TransformPoint(eye);
	EXPECT_NEAR(origin.Length(), 0.0f, 1e-3f);
	Vec3 ahead = view.TransformPoint(eye + rotation * Vec3::Forward());
	EXPECT_NEAR(ahead.z, 1.0f, 1e-3f);
}

TEST(ViewportOpsCameraTest, ProjectionIsReverseZ) {
	Mat4 projection = Ops::BuildProjection(1.0f);
	Vec3 nearPoint = projection.ProjectPoint(Vec3(0.0f, 0.0f, Ops::EDITOR_NEAR_PLANE));
	Vec3 farPoint = projection.ProjectPoint(Vec3(0.0f, 0.0f, Ops::EDITOR_FAR_PLANE));
	EXPECT_NEAR(nearPoint.z, 1.0f, 1e-4f);
	EXPECT_NEAR(farPoint.z, 0.0f, 1e-4f);
	EXPECT_TRUE(std::isfinite(Ops::BuildProjection(0.0f).ProjectPoint(Vec3(0.0f, 0.0f, 1.0f)).z));
}

TEST(ViewportOpsCameraTest, OrthographicShowsAskedSize) {
	Ops::Viewpoint viewpoint;
	viewpoint.orthographic = true;
	viewpoint.orthographicSize = 6.0f;
	viewpoint.aspect = 2.0f;
	const Mat4 projection = Ops::BuildProjection(viewpoint);

	EXPECT_NEAR(projection.ProjectPoint(Vec3(0.0f, 6.0f, 50.0f)).y, 1.0f, 1e-4f);
	EXPECT_NEAR(projection.ProjectPoint(Vec3(12.0f, 0.0f, 50.0f)).x, 1.0f, 1e-4f);
	EXPECT_NEAR(projection.ProjectPoint(Vec3(0.0f, 6.0f, 500.0f)).y, 1.0f, 1e-4f);

	EXPECT_NEAR(projection.ProjectPoint(Vec3(0.0f, 0.0f, -Ops::EDITOR_FAR_PLANE)).z, 1.0f, 1e-4f);
	EXPECT_NEAR(projection.ProjectPoint(Vec3(0.0f, 0.0f, Ops::EDITOR_FAR_PLANE)).z, 0.0f, 1e-4f);
	viewpoint.orthographicSize = 0.0f;
	EXPECT_TRUE(std::isfinite(Ops::BuildProjection(viewpoint).ProjectPoint(Vec3(0.0f, 0.0f, 1.0f)).y));
}

TEST(ViewportOpsCameraTest, ViewHeightFollowsProjection) {
	Ops::Viewpoint perspective;
	perspective.position = Vec3::Zero();
	perspective.rotation = Quat::Identity();
	const f32 near = Ops::ViewHeightAt(perspective, Vec3(0.0f, 0.0f, 10.0f));
	EXPECT_NEAR(Ops::ViewHeightAt(perspective, Vec3(0.0f, 0.0f, 100.0f)), near * 10.0f, 1e-3f);
	EXPECT_NEAR(Ops::ViewHeightAt(perspective, Vec3(40.0f, 0.0f, 10.0f)), near, 1e-3f);

	Ops::Viewpoint orthographic = perspective;
	orthographic.orthographic = true;
	orthographic.orthographicSize = 7.0f;
	EXPECT_NEAR(Ops::ViewHeightAt(orthographic, Vec3(0.0f, 0.0f, 10.0f)), 14.0f, 1e-4f);
	EXPECT_NEAR(Ops::ViewHeightAt(orthographic, Vec3(0.0f, 0.0f, 1000.0f)), 14.0f, 1e-4f);
}

TEST(ViewportOpsCameraTest, ViewAxisNeedsParallax) {
	Ops::Viewpoint perspective;
	perspective.position = Vec3::Zero();
	perspective.rotation = Quat::Identity();
	const Vec3 skewed = Ops::ViewAxisAt(perspective, Vec3(10.0f, 0.0f, 10.0f));
	EXPECT_LT(skewed.x, -0.1f);

	Ops::Viewpoint orthographic = perspective;
	orthographic.orthographic = true;
	const Vec3 flat = Ops::ViewAxisAt(orthographic, Vec3(10.0f, 0.0f, 10.0f));
	EXPECT_NEAR(flat.x, 0.0f, 1e-5f);
	EXPECT_NEAR(flat.z, -1.0f, 1e-5f);
	EXPECT_NEAR(Ops::ViewAxisAt(orthographic, Vec3(-400.0f, 90.0f, 3.0f)).z, -1.0f, 1e-5f);
}

TEST(ViewportOpsCameraTest, OrthographicRaysParallel) {
	Ops::Viewpoint viewpoint;
	viewpoint.position = Vec3(0.0f, 0.0f, -10.0f);
	viewpoint.orthographic = true;
	viewpoint.orthographicSize = 5.0f;
	viewpoint.aspect = 1.0f;
	const Mat4 view = Ops::BuildViewMatrix(viewpoint);
	const Mat4 projection = Ops::BuildProjection(viewpoint);
	const Vec2 sizePx{800.0f, 800.0f};

	const Ray center = Ops::BuildViewportRay(view, projection, Vec2{400.0f, 400.0f}, sizePx);
	const Ray corner = Ops::BuildViewportRay(view, projection, Vec2{10.0f, 10.0f}, sizePx);
	EXPECT_NEAR(center.direction.Normalized().z, 1.0f, 1e-3f);
	EXPECT_NEAR(corner.direction.Normalized().z, 1.0f, 1e-3f);
	EXPECT_LT(corner.origin.x, -1.0f);
	EXPECT_GT(corner.origin.y, 1.0f);
}

namespace {

	Ops::Viewpoint LookingDown(const Vec3& axis, bool orthographic) {
		f32 yawDeg = 0.0f;
		f32 pitchDeg = 0.0f;
		Ops::ViewAnglesFromAxis(axis, 0.0f, yawDeg, pitchDeg);
		Ops::Viewpoint viewpoint;
		viewpoint.position = axis * 20.0f;
		viewpoint.rotation = Ops::BuildCameraRotation(yawDeg, pitchDeg);
		viewpoint.orthographic = orthographic;
		return viewpoint;
	}

	Ops::GridPlane PlaneOf(const Ops::Viewpoint& viewpoint) {
		return Ops::GridPlaneFor(Ops::BuildViewMatrix(viewpoint), Ops::BuildProjection(viewpoint));
	}

} // anonymous namespace

TEST(ViewportOpsGridPlaneTest, AlignedViewTakesFacedPlane) {
	EXPECT_EQ(PlaneOf(LookingDown(Vec3(0.0f, 1.0f, 0.0f), true)), Ops::GridPlane::XZ);
	EXPECT_EQ(PlaneOf(LookingDown(Vec3(0.0f, -1.0f, 0.0f), true)), Ops::GridPlane::XZ);
	EXPECT_EQ(PlaneOf(LookingDown(Vec3(0.0f, 0.0f, -1.0f), true)), Ops::GridPlane::XY);
	EXPECT_EQ(PlaneOf(LookingDown(Vec3(0.0f, 0.0f, 1.0f), true)), Ops::GridPlane::XY);
	EXPECT_EQ(PlaneOf(LookingDown(Vec3(1.0f, 0.0f, 0.0f), true)), Ops::GridPlane::ZY);
	EXPECT_EQ(PlaneOf(LookingDown(Vec3(-1.0f, 0.0f, 0.0f), true)), Ops::GridPlane::ZY);
}

TEST(ViewportOpsGridPlaneTest, EverythingElseIsTheGround) {
	EXPECT_EQ(PlaneOf(LookingDown(Vec3(0.0f, 1.0f, 0.0f), false)), Ops::GridPlane::XZ);
	EXPECT_EQ(PlaneOf(LookingDown(Vec3(0.0f, 0.0f, -1.0f), false)), Ops::GridPlane::XZ);

	Ops::Viewpoint between;
	between.position = Vec3(20.0f, 20.0f, -20.0f);
	between.rotation = Ops::BuildCameraRotation(35.0f, 30.0f);
	between.orthographic = true;
	EXPECT_EQ(PlaneOf(between), Ops::GridPlane::XZ);

	EXPECT_EQ(Ops::GridPlaneFor(Mat4(), Ops::BuildProjection(between)), Ops::GridPlane::XZ);
}

TEST(ViewportOpsGridPlaneTest, BasesAreProperRotations) {
	for (Ops::GridPlane plane : {Ops::GridPlane::XZ, Ops::GridPlane::XY, Ops::GridPlane::ZY}) {
		const Ops::GridBasis basis = Ops::GridBasisFor(plane);
		const Vec3 axes[3] = {Vec3::Right(), Vec3::Up(), Vec3::Forward()};

		EXPECT_NEAR(basis.gridFromWorld.TransformVector(axes[basis.alongX]).x, 1.0f, 1e-5f);
		EXPECT_NEAR(basis.gridFromWorld.TransformVector(basis.alongZ == 0	? axes[0]
														: basis.alongZ == 1 ? axes[1]
																			: axes[2])
						.z,
					1.0f, 1e-5f);
		EXPECT_NEAR(basis.gridFromWorld.TransformVector(axes[basis.alongX]).y, 0.0f, 1e-5f);

		const Vec3 row0(basis.gridFromWorld(0, 0), basis.gridFromWorld(0, 1), basis.gridFromWorld(0, 2));
		const Vec3 row1(basis.gridFromWorld(1, 0), basis.gridFromWorld(1, 1), basis.gridFromWorld(1, 2));
		const Vec3 row2(basis.gridFromWorld(2, 0), basis.gridFromWorld(2, 1), basis.gridFromWorld(2, 2));
		EXPECT_NEAR(Vec3::Dot(Vec3::Cross(row1, row2), row0), 1.0f, 1e-5f);
	}
	EXPECT_EQ(Ops::GridBasisFor(Ops::GridPlane::XZ).alongX, 0u);
	EXPECT_EQ(Ops::GridBasisFor(Ops::GridPlane::XZ).alongZ, 2u);
}

TEST(ViewportOpsFramingTest, OrthographicFrameGrowsWithBounds) {
	const AABB small(Vec3(-1.0f, -1.0f, -1.0f), Vec3(1.0f, 1.0f, 1.0f));
	const AABB large(Vec3(-10.0f, -10.0f, -10.0f), Vec3(10.0f, 10.0f, 10.0f));
	EXPECT_GT(Ops::FrameSizeFor(small), 1.0f);
	EXPECT_NEAR(Ops::FrameSizeFor(large), Ops::FrameSizeFor(small) * 10.0f, 1e-3f);
	EXPECT_GE(Ops::FrameSizeFor(AABB(Vec3::Zero(), Vec3::Zero())), Ops::MIN_ORTHOGRAPHIC_SIZE);
}

TEST(ViewportOpsOrientationTest, KnobsMatchAxes) {
	Ops::OrientationKnob knobs[Ops::ORIENTATION_KNOB_COUNT];
	const Vec2 center{50.0f, 50.0f};
	Ops::LayOutOrientationKnobs(Ops::BuildCameraRotation(0.0f, 0.0f), center, 20.0f, knobs);
	EXPECT_NEAR(knobs[0].centerPx.x, 70.0f, 1e-3f);
	EXPECT_NEAR(knobs[0].centerPx.y, 50.0f, 1e-3f);
	EXPECT_NEAR(knobs[1].centerPx.x, 30.0f, 1e-3f);
	EXPECT_NEAR(knobs[2].centerPx.y, 30.0f, 1e-3f);
	EXPECT_NEAR(knobs[3].centerPx.y, 70.0f, 1e-3f);
	EXPECT_NEAR((knobs[4].centerPx - center).Length(), 0.0f, 1e-3f);
	EXPECT_NEAR(knobs[4].depth, 1.0f, 1e-3f);
	EXPECT_NEAR(knobs[5].depth, -1.0f, 1e-3f);
	Ops::LayOutOrientationKnobs(Ops::BuildCameraRotation(90.0f, 0.0f), center, 20.0f, knobs);
	EXPECT_NEAR((knobs[0].centerPx - center).Length(), 0.0f, 1e-3f);
	EXPECT_NEAR(knobs[0].depth, 1.0f, 1e-3f);
	EXPECT_NEAR(knobs[4].centerPx.x, 30.0f, 1e-3f);
}

TEST(ViewportOpsOrientationTest, AxisViewsLookBack) {
	const Vec3 axes[] = {Vec3(1, 0, 0), Vec3(-1, 0, 0), Vec3(0, 1, 0), Vec3(0, -1, 0), Vec3(0, 0, 1), Vec3(0, 0, -1)};
	for (const Vec3& axis : axes) {
		f32 yaw = 0.0f;
		f32 pitch = 0.0f;
		Ops::ViewAnglesFromAxis(axis, 37.0f, yaw, pitch);
		const Vec3 forward = Ops::BuildCameraRotation(yaw, pitch) * Vec3::Forward();
		EXPECT_NEAR(Vec3::Dot(forward, axis), -1.0f, 1e-3f) << axis.x << ", " << axis.y << ", " << axis.z;
	}
	f32 yaw = 0.0f;
	f32 pitch = 0.0f;
	Ops::ViewAnglesFromAxis(Vec3(0, 1, 0), 37.0f, yaw, pitch);
	EXPECT_NEAR(yaw, 37.0f, 1e-3f);
	EXPECT_NEAR(pitch, 90.0f, 1e-3f);
}

TEST(ViewportOpsOrientationTest, NearerKnobTakesHitWhere2Overlap) {
	Ops::OrientationKnob knobs[Ops::ORIENTATION_KNOB_COUNT];
	const Vec2 center{50.0f, 50.0f};
	Ops::LayOutOrientationKnobs(Ops::BuildCameraRotation(0.0f, 0.0f), center, 20.0f, knobs);
	EXPECT_EQ(Ops::HitOrientationKnob(knobs, center, 6.0f), 5u);
	EXPECT_EQ(Ops::HitOrientationKnob(knobs, Vec2{70.0f, 52.0f}, 6.0f), 0u);
	EXPECT_EQ(Ops::HitOrientationKnob(knobs, Vec2{60.0f, 60.0f}, 6.0f), Ops::ORIENTATION_KNOB_NONE);
}

TEST(ViewportOpsRayTest, ViewportCenterRayPointsAlongViewAxis) {
	TestView view = MakeView();
	Ray ray = Ops::BuildViewportRay(view.view, view.projection,
									Vec2{view.sizePx.x * 0.5f, view.sizePx.y * 0.5f}, view.sizePx);
	EXPECT_NEAR(ray.direction.x, 0.0f, 1e-3f);
	EXPECT_NEAR(ray.direction.y, 0.0f, 1e-3f);
	EXPECT_NEAR(ray.direction.z, 1.0f, 1e-3f);
	EXPECT_NEAR(ray.origin.z, -10.0f + Ops::EDITOR_NEAR_PLANE, 1e-2f);
}

TEST(ViewportOpsRayTest, TopOfTheViewportLooksUp) {
	TestView view = MakeView();
	Ray up = Ops::BuildViewportRay(view.view, view.projection, Vec2{view.sizePx.x * 0.5f, 1.0f}, view.sizePx);
	Ray down = Ops::BuildViewportRay(view.view, view.projection,
									 Vec2{view.sizePx.x * 0.5f, view.sizePx.y - 1.0f}, view.sizePx);
	EXPECT_GT(up.direction.y, 0.1f);
	EXPECT_LT(down.direction.y, -0.1f);

	Ray left = Ops::BuildViewportRay(view.view, view.projection, Vec2{1.0f, view.sizePx.y * 0.5f}, view.sizePx);
	EXPECT_LT(left.direction.x, -0.1f);
}

TEST(ViewportOpsRayTest, ProjectAndUnprojectRoundTrip) {
	TestView view = MakeView(Vec3(1.0f, 2.0f, -6.0f), 25.0f, -10.0f);
	Mat4 viewProjection = view.projection * view.view;

	const Vec3 worldPoints[] = {
		Vec3(0.0f, 0.0f, 0.0f),
		Vec3(2.0f, 1.0f, 3.0f),
		Vec3(-1.5f, -0.5f, 1.0f),
	};
	for (const Vec3& world : worldPoints) {
		Vec2 px{};
		ASSERT_TRUE(Ops::ProjectToViewport(viewProjection, world, view.sizePx, px));
		Ray ray = Ops::BuildViewportRay(view.view, view.projection, px, view.sizePx);
		Vec3 toPoint = world - ray.origin;
		f32 along = Vec3::Dot(toPoint, ray.direction);
		f32 perpendicular = (toPoint - ray.direction * along).Length();
		EXPECT_LT(perpendicular, 0.02f);
		EXPECT_GT(along, 0.0f);
	}
}

TEST(ViewportOpsRayTest, PointsBehindTheEyeDoNotProject) {
	TestView view = MakeView();
	Mat4 viewProjection = view.projection * view.view;
	Vec2 px{};
	EXPECT_FALSE(Ops::ProjectToViewport(viewProjection, Vec3(0.0f, 0.0f, -20.0f), view.sizePx, px));
	EXPECT_FALSE(Ops::ProjectToViewport(viewProjection, Vec3(0.0f, 0.0f, Ops::EDITOR_FAR_PLANE * 2.0f), view.sizePx, px));
}

TEST(ViewportOpsRayTest, DegenerateViewportFallsBackToViewAxis) {
	TestView view = MakeView();
	Ray ray = Ops::BuildViewportRay(view.view, view.projection, Vec2{0.0f, 0.0f}, Vec2{0.0f, 0.0f});
	EXPECT_NEAR(ray.direction.z, 1.0f, 1e-3f);
	EXPECT_NEAR(ray.origin.z, -10.0f, 1e-3f);
}

TEST_F(ViewportOpsFixture, PicksTheNearestBoundedObject) {
	Handle<SceneObject> near = MakeBox("Near", Vec3(0.0f, 0.0f, 0.0f));
	Handle<SceneObject> far = MakeBox("Far", Vec3(0.0f, 0.0f, 5.0f));

	TestView view = MakeView();
	Ray center = Ops::BuildViewportRay(view.view, view.projection,
									   Vec2{view.sizePx.x * 0.5f, view.sizePx.y * 0.5f}, view.sizePx);
	EXPECT_EQ(Ops::PickObject(center), near);

	SceneSystem::Destroy(near);
	SceneSystem::EndFrame();
	EXPECT_EQ(Ops::PickObject(center), far);
}

TEST_F(ViewportOpsFixture, MissesAndInactiveObjectsPickNothing) {
	Handle<SceneObject> box = MakeBox("Box", Vec3(0.0f, 0.0f, 0.0f));
	TestView view = MakeView();

	Ray corner = Ops::BuildViewportRay(view.view, view.projection, Vec2{1.0f, 1.0f}, view.sizePx);
	EXPECT_FALSE(Ops::PickObject(corner).IsValid());

	Ray center = Ops::BuildViewportRay(view.view, view.projection,
									   Vec2{view.sizePx.x * 0.5f, view.sizePx.y * 0.5f}, view.sizePx);
	ASSERT_TRUE(Ops::PickObject(center).IsValid());
	box.Get()->SetActive(false);
	EXPECT_FALSE(Ops::PickObject(center).IsValid());
}

TEST_F(ViewportOpsFixture, PickingHonorsTheWorldTransform) {
	Handle<SceneObject> box = MakeBox("Box", Vec3(0.0f, 0.0f, 0.0f));
	TestView view = MakeView();
	Vec2 offCenter{view.sizePx.x * 0.8f, view.sizePx.y * 0.5f};
	Ray ray = Ops::BuildViewportRay(view.view, view.projection, offCenter, view.sizePx);
	EXPECT_FALSE(Ops::PickObject(ray).IsValid());
	Vec3 target = ray.GetPoint(10.0f);
	box.Get()->SetLocalPosition(target);
	EXPECT_EQ(Ops::PickObject(ray), box);
}

TEST_F(ViewportOpsFixture, EnclosingObjectDoesNotWinClicks) {
	Handle<SceneObject> room = MakeBox("Room", Vec3::Zero());
	room.Get()->SetLocalScale(Vec3(200.0f, 200.0f, 200.0f));
	Handle<SceneObject> prop = MakeBox("Prop", Vec3(0.0f, 0.0f, 5.0f));

	Ray atTheProp(Vec3(0.1f, 0.07f, 0.0f), Vec3(0.0f, 0.0f, 1.0f));
	EXPECT_EQ(Ops::PickObject(atTheProp), prop);

	Ray atTheWall(Vec3(0.0f, 0.0f, 0.0f), Vec3(1.0f, 0.2f, 0.0f));
	EXPECT_EQ(Ops::PickObject(atTheWall), room);
}

TEST_F(ViewportOpsFixture, PartInsideAnothersBoundsIsStillClickable) {
	Handle<SceneObject> sphere = MakeSphere("Sphere", Vec3::Zero());
	Handle<SceneObject> part = MakeBox("Part", Vec3(0.42f, 0.42f, 0.0f));
	part.Get()->SetLocalScale(Vec3(0.1f, 0.1f, 0.1f));

	Ray atThePart(Vec3(0.42f, 0.42f, -10.0f), Vec3(0.0f, 0.0f, 1.0f));
	EXPECT_EQ(Ops::PickObject(atThePart), part);

	Ray atTheSphere(Vec3(0.0f, 0.0f, -10.0f), Vec3(0.0f, 0.0f, 1.0f));
	EXPECT_EQ(Ops::PickObject(atTheSphere), sphere);
}

TEST_F(ViewportOpsFixture, PlacementPointOnCurvedSurfaceIsSurface) {
	MakeSphere("Sphere", Vec3::Zero());

	Ray down(Vec3(0.17f, 5.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f));
	Vec3 world{};
	ASSERT_TRUE(Ops::FindPlacementPoint(down, world));
	EXPECT_LT(world.y, 0.49f);
	EXPECT_NEAR(world.y, 0.4611f, 0.02f);
}

TEST_F(ViewportOpsFixture, SubMeshesReached) {
	MeshAsset* mesh = New<MeshAsset>(MemTag::Asset);
	auto quad = [mesh](f32 minX, f32 maxX, f32 z) {
		const u32 base = static_cast<u32>(mesh->vertices.size());
		const u32 first = static_cast<u32>(mesh->indices.size());
		AABB box = AABB::Empty();
		for (u32 corner = 0; corner < 4; ++corner) {
			MeshVertex vertex;
			vertex.position = {(corner == 1 || corner == 2) ? maxX : minX,
							   (corner >= 2) ? 0.5f : -0.5f, z};
			vertex.normal = Vec3(0.0f, 0.0f, -1.0f);
			box.Encapsulate(vertex.position);
			mesh->bounds.Encapsulate(vertex.position);
			mesh->vertices.push_back(vertex);
		}
		for (u32 index : {0u, 1u, 2u, 0u, 2u, 3u}) {
			mesh->indices.push_back(base + index);
		}
		SubMesh submesh;
		submesh.firstIndex = first;
		submesh.indexCount = 6;
		submesh.bounds = box;
		mesh->submeshes.push_back(submesh);
	};
	mesh->bounds = AABB::Empty();
	quad(-1.0f, -0.1f, 0.0f);
	quad(0.1f, 1.0f, 1.0f);

	Handle<SceneObject> object = SceneSystem::CreateObject("TwoQuads", _scene);
	object.Get()->AddComponent<MeshRenderer>().Get()->SetMesh(Handle<MeshAsset>(mesh));

	Vec3 world{};
	ASSERT_TRUE(Ops::FindPlacementPoint(Ray(Vec3(-0.5f, 0.0f, -10.0f), Vec3(0.0f, 0.0f, 1.0f)), world));
	EXPECT_NEAR(world.z, 0.0f, 1e-4f);

	ASSERT_TRUE(Ops::FindPlacementPoint(Ray(Vec3(0.5f, 0.0f, -10.0f), Vec3(0.0f, 0.0f, 1.0f)), world));
	EXPECT_NEAR(world.z, 1.0f, 1e-4f);

	mesh->submeshes[1].bounds = AABB::Empty();
	ASSERT_TRUE(Ops::FindPlacementPoint(Ray(Vec3(0.5f, 0.0f, -10.0f), Vec3(0.0f, 0.0f, 1.0f)), world));
	EXPECT_NEAR(world.z, 1.0f, 1e-4f);

	SceneSystem::UnloadAllScenes();
	SceneSystem::EndFrame();
	Delete(mesh);
}

TEST_F(ViewportOpsFixture, PlacementFallsToGround) {
	Vec3 world{1.0f, 1.0f, 1.0f};
	ASSERT_TRUE(Ops::FindPlacementPoint(Ray(Vec3(0.0f, 5.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f)), world));
	EXPECT_NEAR(world.x, 0.0f, 1e-4f);
	EXPECT_NEAR(world.y, 0.0f, 1e-4f);
	EXPECT_NEAR(world.z, 0.0f, 1e-4f);

	ASSERT_TRUE(Ops::FindPlacementPoint(Ray(Vec3(2.0f, 4.0f, -3.0f), Vec3(0.0f, -1.0f, 0.0f)), world));
	EXPECT_NEAR(world.x, 2.0f, 1e-4f);
	EXPECT_NEAR(world.y, 0.0f, 1e-4f);
	EXPECT_NEAR(world.z, -3.0f, 1e-4f);
}

TEST_F(ViewportOpsFixture, PlacementOnObjectSurface) {
	MakeBox("Box", Vec3(0.0f, 0.0f, 0.0f));

	Ray down(Vec3(0.0f, 5.0f, 0.0f), Vec3(0.0f, -1.0f, 0.0f));
	Vec3 world{};
	ASSERT_TRUE(Ops::FindPlacementPoint(down, world));
	EXPECT_NEAR(world.y, 0.5f, 1e-4f);

	EXPECT_TRUE(Ops::PickObject(down).IsValid());
}

TEST_F(ViewportOpsFixture, MissingRayHasNoAnswer) {
	Vec3 world{};
	EXPECT_FALSE(Ops::FindPlacementPoint(Ray(Vec3(0.0f, 5.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f)), world));
	EXPECT_FALSE(Ops::FindPlacementPoint(Ray(Vec3(0.0f, 5.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)), world));

	MakeBox("Box", Vec3(0.0f, 5.0f, 20.0f));
	EXPECT_TRUE(Ops::FindPlacementPoint(Ray(Vec3(0.0f, 5.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)), world));
	EXPECT_NEAR(world.z, 19.5f, 1e-4f);
}

TEST_F(ViewportOpsFixture, BoundsFollowTransform) {
	Handle<SceneObject> box = MakeBox("Box", Vec3(4.0f, 0.0f, 0.0f));
	box.Get()->SetLocalScale(Vec3(2.0f, 2.0f, 2.0f));
	AABB bounds = Ops::GetObjectWorldBounds(box);
	EXPECT_NEAR(bounds.Center().x, 4.0f, 1e-4f);
	EXPECT_NEAR(bounds.Size().x, 2.0f, 1e-4f);

	Handle<SceneObject> empty = SceneSystem::CreateObject("Empty", _scene);
	empty.Get()->SetLocalPosition(Vec3(0.0f, 7.0f, 0.0f));
	AABB emptyBounds = Ops::GetObjectWorldBounds(empty);
	EXPECT_TRUE(emptyBounds.IsValid());
	EXPECT_NEAR(emptyBounds.Center().y, 7.0f, 1e-4f);

	EXPECT_FALSE(Ops::GetObjectWorldBounds({}).IsValid());
}

TEST_F(ViewportOpsFixture, SelectionBoundsUnionAndResolveComponents) {
	Handle<SceneObject> a = MakeBox("A", Vec3(-5.0f, 0.0f, 0.0f));
	Handle<SceneObject> b = MakeBox("B", Vec3(5.0f, 0.0f, 0.0f));
	EXPECT_FALSE(Ops::GetSelectionWorldBounds().IsValid());

	EditorSelection::Select(Handle<Object>(a.Get()));
	EditorSelection::AddToSelection(Handle<Object>(b.Get()));
	AABB bounds = Ops::GetSelectionWorldBounds();
	ASSERT_TRUE(bounds.IsValid());
	EXPECT_NEAR(bounds.Center().x, 0.0f, 1e-4f);
	EXPECT_NEAR(bounds.min.x, -5.5f, 1e-4f);
	EXPECT_NEAR(bounds.max.x, 5.5f, 1e-4f);

	MeshRenderer* renderer = a.Get()->GetComponent<MeshRenderer>().Get();
	ASSERT_NE(renderer, nullptr);
	EditorSelection::Select(Handle<Object>(renderer));
	EXPECT_EQ(Ops::ResolveSceneObject(Handle<Object>(renderer)), a);
	AABB componentBounds = Ops::GetSelectionWorldBounds();
	ASSERT_TRUE(componentBounds.IsValid());
	EXPECT_NEAR(componentBounds.Center().x, -5.0f, 1e-4f);
}

TEST(ViewportOpsFramingTest, FramePositionPutsTheBoundsInFront) {
	Quat rotation = Ops::BuildCameraRotation(35.0f, 12.0f);
	AABB bounds = AABB::FromCenterExtents(Vec3(2.0f, 1.0f, 3.0f), Vec3(1.0f, 1.0f, 1.0f));
	Vec3 position = Ops::FramePositionFor(bounds, rotation);

	Vec3 toCenter = bounds.Center() - position;
	Vec3 forward = rotation * Vec3::Forward();
	EXPECT_GT(Vec3::Dot(toCenter.Normalized(), forward), 0.999f);
	f32 radius = bounds.Extents().Length();
	f32 minimum = radius / std::tan(Ops::EDITOR_FOV_DEG * 0.5f * Math::DEG_TO_RAD);
	EXPECT_GT(toCenter.Length(), minimum);

	AABB bigger = AABB::FromCenterExtents(bounds.Center(), Vec3(4.0f, 4.0f, 4.0f));
	EXPECT_GT(Vec3::Distance(Ops::FramePositionFor(bigger, rotation), bounds.Center()),
			  Vec3::Distance(position, bounds.Center()));

	EXPECT_EQ(Ops::FramePositionFor(AABB::Empty(), rotation), Vec3::Zero());
}

TEST_F(ViewportOpsFixture, ActiveCameraIsTheHighestPriorityOne) {
	EXPECT_FALSE(Ops::FindActiveSceneCamera().IsValid());

	Handle<SceneObject> lowObject = SceneSystem::CreateObject("Low", _scene);
	Handle<Camera> low = lowObject.Get()->AddComponent<Camera>();
	low.Get()->SetPriority(-5);
	EXPECT_EQ(Ops::FindActiveSceneCamera(), low);

	Handle<SceneObject> highObject = SceneSystem::CreateObject("High", _scene);
	Handle<Camera> high = highObject.Get()->AddComponent<Camera>();
	high.Get()->SetPriority(10);
	EXPECT_EQ(Ops::FindActiveSceneCamera(), high);

	highObject.Get()->SetActive(false);
	EXPECT_EQ(Ops::FindActiveSceneCamera(), low);

	Handle<Scene> other = SceneSystem::CreateScene("Other");
	Handle<SceneObject> otherObject = SceneSystem::CreateObject("Cam", other);
	Handle<Camera> otherCamera = otherObject.Get()->AddComponent<Camera>();
	otherCamera.Get()->SetPriority(50);
	EXPECT_EQ(Ops::FindActiveSceneCamera(), otherCamera);
}

TEST_F(ViewportOpsFixture, ResolveSceneObjectRejectsUnrelatedObjects) {
	Handle<SceneObject> box = MakeBox("Box", Vec3::Zero());
	EXPECT_EQ(Ops::ResolveSceneObject(Handle<Object>(box.Get())), box);
	EXPECT_FALSE(Ops::ResolveSceneObject({}).IsValid());
	EXPECT_FALSE(Ops::ResolveSceneObject(Handle<Object>(_scene.Get())).IsValid());
}

TEST(ViewportOpsLinesTest, BoxWireframeIs12EdgesInsideBounds) {
	Vector<DebugLine> lines{GetMemoryResource(MemTag::Editor)};
	AABB box(Vec3(-1.0f, -2.0f, -3.0f), Vec3(1.0f, 2.0f, 3.0f));
	Ops::AppendBoxWireframe(lines, box, Color32(1, 2, 3, 4));
	ASSERT_EQ(lines.size(), 12u);
	for (const DebugLine& line : lines) {
		EXPECT_TRUE(box.Contains(line.a));
		EXPECT_TRUE(box.Contains(line.b));
		EXPECT_EQ(line.color, Color32(1, 2, 3, 4));
		Vec3 delta = line.b - line.a;
		i32 movingAxes = (Math::Abs(delta.x) > 1e-4f) + (Math::Abs(delta.y) > 1e-4f) + (Math::Abs(delta.z) > 1e-4f);
		EXPECT_EQ(movingAxes, 1);
	}

	Vector<DebugLine> empty{GetMemoryResource(MemTag::Editor)};
	Ops::AppendBoxWireframe(empty, AABB::Empty(), Color32::White());
	EXPECT_TRUE(empty.empty());
}

TEST(ViewportSizingTest, ViewTargetSizeIsPaintersSnappedFootprint) {
	EXPECT_EQ(Ops::ViewTargetSizeFor({0.0f, 0.0f, 100.0f, 80.0f}), (Vec2I{100, 80}));

	EXPECT_EQ(Ops::ViewTargetSizeFor({31.25f, 31.25f, 368.75f, 268.75f}), (Vec2I{369, 269}));
	EXPECT_EQ(Ops::ViewTargetSizeFor({31.5f, 0.0f, 368.75f, 100.0f}), (Vec2I{368, 100}));

	EXPECT_EQ(Ops::ViewTargetSizeFor({0.0f, 0.0f, 0.0f, 0.0f}), (Vec2I{0, 0}));

	for (i32 step = 0; step < 16; ++step) {
		f32 offset = static_cast<f32>(step) * 0.0625f;
		UI::Rect rect{10.0f + offset, 20.0f + offset, 300.5f, 200.25f};
		UI::Rect painted = UI::SnapRectToPixels(rect);
		Vec2I size = Ops::ViewTargetSizeFor(rect);
		EXPECT_EQ(size.x, static_cast<i32>(painted.w)) << "phase " << offset;
		EXPECT_EQ(size.y, static_cast<i32>(painted.h)) << "phase " << offset;
	}
}

TEST(ViewportSizingTest, ACollapsedViewportIsNotOnScreen) {
	EXPECT_TRUE(Ops::ViewportIsOnScreen({0.0f, 0.0f, 640.0f, 480.0f}));

	EXPECT_FALSE(Ops::ViewportIsOnScreen({}));
	EXPECT_FALSE(Ops::ViewportIsOnScreen({100.0f, 100.0f, 0.0f, 0.0f}));

	EXPECT_FALSE(Ops::ViewportIsOnScreen({0.0f, 0.0f, 640.0f, 0.0f}));
	EXPECT_FALSE(Ops::ViewportIsOnScreen({0.0f, 0.0f, 0.0f, 480.0f}));

	EXPECT_FALSE(Ops::ViewportIsOnScreen({0.0f, 0.0f, 0.4f, 0.4f}));

	for (i32 step = 0; step < 8; ++step) {
		f32 offset = static_cast<f32>(step) * 0.125f;
		UI::Rect rect{offset, offset, 320.5f, 180.25f};
		EXPECT_EQ(Ops::ViewportIsOnScreen(rect), Ops::ViewTargetSizeFor(rect).x > 0) << "phase " << offset;
	}
}

TEST(ViewportSizingTest, BackgroundedPanelsViewportElementStopsBeingLive) {
	UI::ContextDesc desc;
	desc.sizePx = {400, 300};
	desc.contentScale = 1.0f;
	UI::Context ctx(desc, nullptr);

	UI::Style leafStyle;
	leafStyle.flexDirection = UI::FlexDirection::Column;
	leafStyle.alignItems = UI::Align::Stretch;
	leafStyle.width = UI::Length::Percent(1.0f);
	leafStyle.height = UI::Length::Percent(1.0f);
	UI::ElementHandle content = MakeChild(ctx, ctx.GetRoot(), leafStyle);

	UI::Style viewStyle;
	viewStyle.width = UI::Length::Percent(1.0f);
	viewStyle.height = UI::Length::Pixel(0.0f);
	viewStyle.flexGrow = 1.0f;
	UI::ElementHandle viewport = MakeChild(ctx, content, viewStyle);

	ctx.Update();
	ASSERT_TRUE(ctx.IsAlive(viewport));
	ASSERT_TRUE(Ops::ViewportIsOnScreen(ctx.GetLayout(viewport).content));

	ctx.ClearChildren(content);
	ctx.Update();

	EXPECT_FALSE(ctx.IsAlive(viewport));
}

TEST(ViewportSizingTest, ScaledLayoutStaysOnGrid) {
	UI::ContextDesc desc;
	desc.sizePx = {400, 300};
	desc.contentScale = 1.25f;
	UI::Context ctx(desc, nullptr);

	UI::Style rowStyle;
	rowStyle.flexDirection = UI::FlexDirection::Row;
	rowStyle.alignItems = UI::Align::Stretch;
	rowStyle.width = UI::Length::Percent(1.0f);
	rowStyle.height = UI::Length::Percent(1.0f);
	UI::ElementHandle row = MakeChild(ctx, ctx.GetRoot(), rowStyle);

	UI::Style sideStyle;
	sideStyle.width = UI::Length::Pixel(25.0f);
	sideStyle.height = UI::Length::Percent(1.0f);
	MakeChild(ctx, row, sideStyle);

	UI::Style panelStyle;
	panelStyle.flexDirection = UI::FlexDirection::Column;
	panelStyle.alignItems = UI::Align::Stretch;
	panelStyle.width = UI::Length::Pixel(295.0f);
	panelStyle.height = UI::Length::Percent(1.0f);
	UI::ElementHandle panel = MakeChild(ctx, row, panelStyle);

	UI::Style barStyle;
	barStyle.width = UI::Length::Percent(1.0f);
	barStyle.height = UI::Length::Pixel(25.0f);
	MakeChild(ctx, panel, barStyle);

	UI::Style viewStyle;
	viewStyle.width = UI::Length::Percent(1.0f);
	viewStyle.height = UI::Length::Pixel(0.0f);
	viewStyle.flexGrow = 1.0f;
	viewStyle.background = UI::Brush::SolidColor(Color::White());
	UI::ElementHandle viewport = MakeChild(ctx, panel, viewStyle);

	ctx.Update();

	const UI::Rect content = ctx.GetLayout(viewport).content;
	ASSERT_NE(content.w, std::floor(content.w));
	ASSERT_NE(content.h, std::floor(content.h));

	UI::RenderList list;
	ctx.BuildRenderList(list);
	ASSERT_EQ(list.quads.size(), 1u);
	const UI::Rect painted = list.quads[0].rect;

	Vec2I size = Ops::ViewTargetSizeFor(content);
	EXPECT_EQ(size.x, static_cast<i32>(painted.w));
	EXPECT_EQ(size.y, static_cast<i32>(painted.h));

	EXPECT_NE(static_cast<i32>(content.w), static_cast<i32>(painted.w));
	EXPECT_NE(static_cast<i32>(content.h), static_cast<i32>(painted.h));
}

TEST_F(ViewportOpsFixture, DragTargetsCollapseObjectAndComponent) {
	Handle<SceneObject> box = MakeBox("Box", Vec3::Zero());
	MeshRenderer* renderer = box.Get()->GetComponent<MeshRenderer>().Get();
	ASSERT_NE(renderer, nullptr);

	EditorSelection::Select(Handle<Object>(box.Get()));
	EditorSelection::AddToSelection(Handle<Object>(renderer));

	Vector<Handle<SceneObject>> targets{GetMemoryResource(MemTag::Editor)};
	Ops::GatherDragTargets(targets);

	ASSERT_EQ(targets.size(), 1u);
	EXPECT_EQ(targets[0], box);
}

TEST_F(ViewportOpsFixture, DragTargetsDropAnythingUnderSelectedObject) {
	Handle<SceneObject> parent = SceneSystem::CreateObject("Parent", _scene);
	Handle<SceneObject> child = SceneSystem::CreateObject("Child", _scene);
	Handle<SceneObject> grandchild = SceneSystem::CreateObject("Grandchild", _scene);
	child.Get()->SetParent(parent);
	grandchild.Get()->SetParent(child);

	EditorSelection::Select(Handle<Object>(parent.Get()));
	EditorSelection::AddToSelection(Handle<Object>(grandchild.Get()));

	Vector<Handle<SceneObject>> targets{GetMemoryResource(MemTag::Editor)};
	Ops::GatherDragTargets(targets);

	ASSERT_EQ(targets.size(), 1u);
	EXPECT_EQ(targets[0], parent);
}

TEST_F(ViewportOpsFixture, DragTargetsLeadWithTheActiveObject) {
	Handle<SceneObject> first = SceneSystem::CreateObject("First", _scene);
	Handle<SceneObject> second = SceneSystem::CreateObject("Second", _scene);
	Handle<SceneObject> third = SceneSystem::CreateObject("Third", _scene);

	EditorSelection::Select(Handle<Object>(first.Get()));
	EditorSelection::AddToSelection(Handle<Object>(second.Get()));
	EditorSelection::AddToSelection(Handle<Object>(third.Get()));
	ASSERT_EQ(EditorSelection::GetActive().GetInstanceId(), third.GetInstanceId());

	Vector<Handle<SceneObject>> targets{GetMemoryResource(MemTag::Editor)};
	Ops::GatherDragTargets(targets);

	ASSERT_EQ(targets.size(), 3u);
	EXPECT_EQ(targets[0], third);
	EXPECT_EQ(targets[1], first);
	EXPECT_EQ(targets[2], second);
}

TEST_F(ViewportOpsFixture, DragTargetsSurviveActiveObjectBeingPruned) {
	Handle<SceneObject> parent = SceneSystem::CreateObject("Parent", _scene);
	Handle<SceneObject> child = SceneSystem::CreateObject("Child", _scene);
	child.Get()->SetParent(parent);

	EditorSelection::Select(Handle<Object>(parent.Get()));
	EditorSelection::AddToSelection(Handle<Object>(child.Get()));
	ASSERT_EQ(EditorSelection::GetActive().GetInstanceId(), child.GetInstanceId());

	Vector<Handle<SceneObject>> targets{GetMemoryResource(MemTag::Editor)};
	Ops::GatherDragTargets(targets);

	ASSERT_EQ(targets.size(), 1u);
	EXPECT_EQ(targets[0], parent);
}

TEST_F(ViewportOpsFixture, DragTargetPivotIsMeanOfPivots) {
	Handle<SceneObject> left = SceneSystem::CreateObject("Left", _scene);
	Handle<SceneObject> right = SceneSystem::CreateObject("Right", _scene);
	left.Get()->SetLocalPosition(Vec3(-3.0f, 0.0f, 0.0f));
	right.Get()->SetLocalPosition(Vec3(5.0f, 2.0f, 0.0f));

	EditorSelection::Select(Handle<Object>(left.Get()));
	EditorSelection::AddToSelection(Handle<Object>(right.Get()));

	Vector<Handle<SceneObject>> targets{GetMemoryResource(MemTag::Editor)};
	Ops::GatherDragTargets(targets);
	ASSERT_EQ(targets.size(), 2u);

	const Vec3 pivot = EditorGizmoOps::PivotFor(targets);
	EXPECT_NEAR(pivot.x, 1.0f, 1.0e-4f);
	EXPECT_NEAR(pivot.y, 1.0f, 1.0e-4f);

	EditorSelection::Select(Handle<Object>(right.Get()));
	Ops::GatherDragTargets(targets);
	ASSERT_EQ(targets.size(), 1u);
	const Vec3 alone = EditorGizmoOps::PivotFor(targets);
	EXPECT_NEAR(alone.x, 5.0f, 1.0e-4f);
	EXPECT_NEAR(alone.y, 2.0f, 1.0e-4f);
}

namespace {

	Vector<Handle<SceneObject>> Roots(std::initializer_list<Handle<SceneObject>> objects) {
		Vector<Handle<SceneObject>> out{GetMemoryResource(MemTag::Editor)};
		for (Handle<SceneObject> object : objects) {
			out.push_back(object);
		}
		return out;
	}

} // anonymous namespace

TEST_F(ViewportOpsFixture, AnchorIsVertexNearestCursor) {
	Handle<SceneObject> box = MakeBox("Box", Vec3::Zero());
	TestView view = MakeView();
	const Mat4 viewProjection = view.projection * view.view;

	const Vec3 corner(-0.5f, 0.5f, -0.5f);
	Vec2 cornerPx{};
	ASSERT_TRUE(Ops::ProjectToViewport(viewProjection, corner, view.sizePx, cornerPx));

	Vector<Handle<SceneObject>> roots = Roots({box});
	Vec3 found{};
	ASSERT_TRUE(Ops::FindNearestVertexIn(roots, viewProjection, view.sizePx,
										 cornerPx - Vec2(2.0f, 1.0f), found));
	EXPECT_NEAR(found.x, corner.x, 1.0e-4f);
	EXPECT_NEAR(found.y, corner.y, 1.0e-4f);
	EXPECT_NEAR(found.z, corner.z, 1.0e-4f);

	const Vec3 other(0.5f, -0.5f, -0.5f);
	Vec2 otherPx{};
	ASSERT_TRUE(Ops::ProjectToViewport(viewProjection, other, view.sizePx, otherPx));
	ASSERT_TRUE(Ops::FindNearestVertexIn(roots, viewProjection, view.sizePx, otherPx, found));
	EXPECT_NEAR(found.x, other.x, 1.0e-4f);
	EXPECT_NEAR(found.y, other.y, 1.0e-4f);
}

TEST_F(ViewportOpsFixture, TheAnchorSearchDescendsIntoChildren) {
	Handle<SceneObject> root = SceneSystem::CreateObject("ImportRoot", _scene);
	root.Get()->SetLocalPosition(Vec3(3.0f, 0.0f, 0.0f));
	Handle<SceneObject> child = MakeBox("Mesh", Vec3::Zero());
	child.Get()->SetParent(root, false);

	TestView view = MakeView();
	const Mat4 viewProjection = view.projection * view.view;
	const Vec3 corner(3.0f - 0.5f, 0.5f, -0.5f);
	Vec2 cornerPx{};
	ASSERT_TRUE(Ops::ProjectToViewport(viewProjection, corner, view.sizePx, cornerPx));

	Vector<Handle<SceneObject>> roots = Roots({root});
	Vec3 found{};
	ASSERT_TRUE(Ops::FindNearestVertexIn(roots, viewProjection, view.sizePx, cornerPx, found));
	EXPECT_NEAR(found.x, corner.x, 1.0e-4f);
	EXPECT_NEAR(found.y, corner.y, 1.0e-4f);
	EXPECT_NEAR(found.z, corner.z, 1.0e-4f);
}

TEST_F(ViewportOpsFixture, TargetSearchSkipsWhatDragIsMoving) {
	Handle<SceneObject> dragged = MakeBox("Dragged", Vec3::Zero());
	TestView view = MakeView();
	const Mat4 viewProjection = view.projection * view.view;

	Vec2 cornerPx{};
	ASSERT_TRUE(Ops::ProjectToViewport(viewProjection, Vec3(-0.5f, 0.5f, -0.5f), view.sizePx, cornerPx));
	Ray ray = Ops::BuildViewportRay(view.view, view.projection, cornerPx, view.sizePx);

	Vec3 found{};
	EXPECT_TRUE(Ops::FindNearestVertexUnderCursor({}, ray, viewProjection, view.sizePx, cornerPx, found));

	Vector<Handle<SceneObject>> excluded = Roots({dragged});
	EXPECT_FALSE(Ops::FindNearestVertexUnderCursor(excluded, ray, viewProjection, view.sizePx, cornerPx, found));
}

TEST_F(ViewportOpsFixture, TheExclusionCarriesChildren) {
	Handle<SceneObject> root = SceneSystem::CreateObject("DraggedRoot", _scene);
	Handle<SceneObject> child = MakeBox("Mesh", Vec3::Zero());
	child.Get()->SetParent(root, false);

	TestView view = MakeView();
	const Mat4 viewProjection = view.projection * view.view;
	Vec2 cornerPx{};
	ASSERT_TRUE(Ops::ProjectToViewport(viewProjection, Vec3(-0.5f, 0.5f, -0.5f), view.sizePx, cornerPx));
	Ray ray = Ops::BuildViewportRay(view.view, view.projection, cornerPx, view.sizePx);

	Vector<Handle<SceneObject>> excluded = Roots({root});
	Vec3 found{};
	EXPECT_FALSE(Ops::FindNearestVertexUnderCursor(excluded, ray, viewProjection, view.sizePx, cornerPx, found));
}

TEST_F(ViewportOpsFixture, NothingUnderTheCursorIsNoTarget) {
	MakeBox("Box", Vec3::Zero());
	TestView view = MakeView();
	const Mat4 viewProjection = view.projection * view.view;

	const Vec2 cursorPx{1.0f, 1.0f};
	Ray ray = Ops::BuildViewportRay(view.view, view.projection, cursorPx, view.sizePx);
	Vec3 found{};
	EXPECT_FALSE(Ops::FindNearestVertexUnderCursor({}, ray, viewProjection, view.sizePx, cursorPx, found));
}

TEST_F(ViewportOpsFixture, NearerVertexWins) {
	const Vec3 eye(0.0f, 0.0f, -10.0f);
	const Vec3 localCorner(-0.5f, 0.5f, -0.5f);
	const Vec3 toCorner = localCorner - eye;
	const f32 t = (19.5f - eye.z) / toCorner.z;
	const Vec3 farCorner = eye + toCorner * t;

	Handle<SceneObject> far = MakeBox("Far", farCorner - localCorner);
	Handle<SceneObject> near = MakeBox("Near", Vec3::Zero());

	TestView view = MakeView(eye);
	const Mat4 viewProjection = view.projection * view.view;
	Vec2 nearPx{};
	Vec2 farPx{};
	ASSERT_TRUE(Ops::ProjectToViewport(viewProjection, localCorner, view.sizePx, nearPx));
	ASSERT_TRUE(Ops::ProjectToViewport(viewProjection, farCorner, view.sizePx, farPx));
	ASSERT_LT((nearPx - farPx).Length(), 1.0f);

	Vector<Handle<SceneObject>> roots = Roots({far, near});
	Vec3 found{};
	ASSERT_TRUE(Ops::FindNearestVertexIn(roots, viewProjection, view.sizePx, farPx, found));
	EXPECT_NEAR(found.z, localCorner.z, 1.0e-3f);
}
