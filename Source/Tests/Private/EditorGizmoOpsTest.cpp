#include "TestsPCH.h"

#include "Shalltear/Editor/EditorGizmoOps.h"
#include "Shalltear/Editor/EditorViewportOps.h"

#include <cmath>

using namespace Shalltear;

namespace Gizmo = Shalltear::EditorGizmoOps;
namespace Ops = Shalltear::EditorViewportOps;
using Gizmo::GizmoHandle;

namespace {

	Ops::Viewpoint MakeViewpoint(const Vec3& position, const Quat& rotation) {
		Ops::Viewpoint viewpoint;
		viewpoint.position = position;
		viewpoint.rotation = rotation;
		return viewpoint;
	}

	Ops::Viewpoint MakeOrthographic(const Vec3& position, const Quat& rotation, f32 size) {
		Ops::Viewpoint viewpoint = MakeViewpoint(position, rotation);
		viewpoint.orthographic = true;
		viewpoint.orthographicSize = size;
		return viewpoint;
	}

	Gizmo::GizmoDesc MakeDesc(GizmoMode mode, const Vec3& cameraPosition, const Quat& cameraRotation) {
		Gizmo::GizmoDesc desc;
		desc.origin = Vec3::Zero();
		desc.orientation = Quat::Identity();
		desc.viewpoint = MakeViewpoint(cameraPosition, cameraRotation);
		desc.mode = mode;
		desc.scale = 1.0f;
		desc.pickTolerance = 0.05f;
		return desc;
	}

	Ray RayTo(const Vec3& from, const Vec3& at) {
		return Ray(from, at - from);
	}

} // anonymous namespace

TEST(EditorGizmoOpsSizingTest, HandleScaleGrowsWithCameraDistance) {
	const f32 near = Gizmo::HandleScaleAt(MakeViewpoint(Vec3(0.0f, 0.0f, -5.0f), Quat::Identity()), Vec3::Zero());
	const f32 far = Gizmo::HandleScaleAt(MakeViewpoint(Vec3(0.0f, 0.0f, -50.0f), Quat::Identity()), Vec3::Zero());
	EXPECT_GT(far, near);
	EXPECT_NEAR(far, near * 10.0f, 1.0e-3f);
	EXPECT_GT(Gizmo::HandleScaleAt(MakeViewpoint(Vec3::Zero(), Quat::Identity()), Vec3::Zero()), 0.0f);
}

TEST(EditorGizmoOpsSizingTest, HandleScaleIgnoresDistanceUnderOrthographicCamera) {
	const Quat rotation = Quat::Identity();
	const f32 near = Gizmo::HandleScaleAt(MakeOrthographic(Vec3(0.0f, 0.0f, -5.0f), rotation, 8.0f), Vec3::Zero());
	const f32 far = Gizmo::HandleScaleAt(MakeOrthographic(Vec3(0.0f, 0.0f, -500.0f), rotation, 8.0f), Vec3::Zero());
	EXPECT_NEAR(near, far, 1.0e-4f);
	const f32 zoomed = Gizmo::HandleScaleAt(MakeOrthographic(Vec3(0.0f, 0.0f, -5.0f), rotation, 4.0f), Vec3::Zero());
	EXPECT_NEAR(zoomed, near * 0.5f, 1.0e-4f);
}

TEST(EditorGizmoOpsSizingTest, PixelBudgetTracksDepth) {
	const Quat rotation = Quat::Identity();
	const Ops::Viewpoint viewpoint = MakeViewpoint(Vec3::Zero(), rotation);
	const f32 near = Gizmo::WorldRadiusForPixels(viewpoint, Vec3(0.0f, 0.0f, 10.0f), 1000.0f, 10.0f);
	const f32 far = Gizmo::WorldRadiusForPixels(viewpoint, Vec3(0.0f, 0.0f, 100.0f), 1000.0f, 10.0f);
	EXPECT_GT(near, 0.0f);
	EXPECT_NEAR(far, near * 10.0f, 1.0e-3f);
	EXPECT_EQ(Gizmo::WorldRadiusForPixels(viewpoint, Vec3(0.0f, 0.0f, 10.0f), 0.0f, 10.0f), 0.0f);
}

TEST(EditorGizmoOpsSizingTest, GrabBudgetIsPixelsUnderEitherProjection) {
	constexpr f32 SIDE_PX = 800.0f;
	const Vec2 sizePx{SIDE_PX, SIDE_PX};
	const Quat rotation = Quat::Identity();

	auto pixelsSpannedBy = [&](const Ops::Viewpoint& viewpoint, const Vec3& at) {
		const f32 world = Gizmo::WorldRadiusForPixels(viewpoint, at, SIDE_PX, Gizmo::HANDLE_PICK_PX);
		const Mat4 viewProjection = Ops::BuildProjection(viewpoint) * Ops::BuildViewMatrix(viewpoint);
		Vec2 center{};
		Vec2 edge{};
		EXPECT_TRUE(Ops::ProjectToViewport(viewProjection, at, sizePx, center));
		EXPECT_TRUE(Ops::ProjectToViewport(viewProjection, at + Vec3(0.0f, world, 0.0f), sizePx, edge));
		return (edge - center).Length();
	};

	Ops::Viewpoint perspective = MakeViewpoint(Vec3(0.0f, 0.0f, -20.0f), rotation);
	EXPECT_NEAR(pixelsSpannedBy(perspective, Vec3(0.0f, 0.0f, 0.0f)), Gizmo::HANDLE_PICK_PX, 0.02f);
	EXPECT_NEAR(pixelsSpannedBy(perspective, Vec3(0.0f, 0.0f, 180.0f)), Gizmo::HANDLE_PICK_PX, 0.02f);

	for (f32 size : {2.0f, 40.0f}) {
		Ops::Viewpoint orthographic = MakeOrthographic(Vec3(0.0f, 0.0f, -20.0f), rotation, size);
		EXPECT_NEAR(pixelsSpannedBy(orthographic, Vec3::Zero()), Gizmo::HANDLE_PICK_PX, 0.02f);
	}
}

TEST(EditorGizmoOpsHitTest, AxisGrabbedAlongShaft) {
	const Gizmo::GizmoDesc desc = MakeDesc(GizmoMode::Translate, Vec3(0.0f, 10.0f, 0.0f),
										   Quat::FromEuler(90.0f, 0.0f, 0.0f));

	const Gizmo::GizmoHit onShaft = Gizmo::HitTest(desc, RayTo(Vec3(0.5f, 10.0f, 0.0f), Vec3(0.5f, 0.0f, 0.0f)));
	EXPECT_EQ(onShaft.handle, GizmoHandle::AxisX);

	const Gizmo::GizmoHit pastTip = Gizmo::HitTest(desc, RayTo(Vec3(2.0f, 10.0f, 0.0f), Vec3(2.0f, 0.0f, 0.0f)));
	EXPECT_EQ(pastTip.handle, GizmoHandle::None);

	const Gizmo::GizmoHit beside = Gizmo::HitTest(desc, RayTo(Vec3(0.5f, 10.0f, 1.0f), Vec3(0.5f, 0.0f, 1.0f)));
	EXPECT_EQ(beside.handle, GizmoHandle::None);
}

TEST(EditorGizmoOpsHitTest, EachAxisIsGrabbedOnShaft) {
	const Gizmo::GizmoDesc desc = MakeDesc(GizmoMode::Translate, Vec3(6.0f, 6.0f, -6.0f),
										   Quat::FromEuler(35.0f, -45.0f, 0.0f));
	EXPECT_EQ(Gizmo::HitTest(desc, RayTo(desc.viewpoint.position, Vec3(0.9f, 0.0f, 0.0f))).handle, GizmoHandle::AxisX);
	EXPECT_EQ(Gizmo::HitTest(desc, RayTo(desc.viewpoint.position, Vec3(0.0f, 0.9f, 0.0f))).handle, GizmoHandle::AxisY);
	EXPECT_EQ(Gizmo::HitTest(desc, RayTo(desc.viewpoint.position, Vec3(0.0f, 0.0f, 0.9f))).handle, GizmoHandle::AxisZ);
}

TEST(EditorGizmoOpsHitTest, QuadInFrontOfShaftTakesClick) {
	const Gizmo::GizmoDesc desc = MakeDesc(GizmoMode::Translate, Vec3(6.0f, 6.0f, -6.0f),
										   Quat::FromEuler(35.0f, -45.0f, 0.0f));
	EXPECT_EQ(Gizmo::HitTest(desc, RayTo(desc.viewpoint.position, Vec3(0.0f, 0.0f, 0.6f))).handle, GizmoHandle::PlaneXY);
}

TEST(EditorGizmoOpsHitTest, AxisHandlesFollowTheGizmoOrientation) {
	Gizmo::GizmoDesc desc = MakeDesc(GizmoMode::Translate, Vec3(0.0f, 10.0f, 0.0f),
									 Quat::FromEuler(90.0f, 0.0f, 0.0f));
	desc.orientation = Quat::FromEuler(0.0f, 90.0f, 0.0f);
	EXPECT_EQ(Gizmo::HitTest(desc, RayTo(Vec3(0.0f, 10.0f, -0.5f), Vec3(0.0f, 0.0f, -0.5f))).handle, GizmoHandle::AxisX);
	EXPECT_EQ(Gizmo::HitTest(desc, RayTo(Vec3(0.5f, 10.0f, 0.0f), Vec3(0.5f, 0.0f, 0.0f))).handle, GizmoHandle::AxisZ);
}

TEST(EditorGizmoOpsHitTest, TheNEARESTHandleAlongTheRayWins) {
	const Gizmo::GizmoDesc desc = MakeDesc(GizmoMode::Translate, Vec3(0.0f, 0.0f, -10.0f), Quat::Identity());
	const Gizmo::GizmoHit hit = Gizmo::HitTest(desc, RayTo(Vec3(0.0f, 0.0f, -10.0f), Vec3(0.0f, 0.0f, 0.5f)));
	ASSERT_NE(hit.handle, GizmoHandle::None);
	EXPECT_GT(hit.distance, 0.0f);
}

TEST(EditorGizmoOpsRingTest, RingFacingCameraIsGrabbableAtRim) {
	const Gizmo::GizmoDesc desc = MakeDesc(GizmoMode::Rotate, Vec3(0.0f, 10.0f, 0.0f),
										   Quat::FromEuler(90.0f, 0.0f, 0.0f));

	const Gizmo::GizmoHit atRim = Gizmo::HitTest(desc, RayTo(Vec3(1.0f, 10.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f)));
	EXPECT_EQ(atRim.handle, GizmoHandle::AxisY);

	EXPECT_EQ(Gizmo::HitTest(desc, RayTo(Vec3(0.0f, 10.0f, -1.0f), Vec3(0.0f, 0.0f, -1.0f))).handle, GizmoHandle::AxisY);
	EXPECT_EQ(Gizmo::HitTest(desc, RayTo(Vec3(0.707f, 10.0f, 0.707f), Vec3(0.707f, 0.0f, 0.707f))).handle, GizmoHandle::AxisY);
}

TEST(EditorGizmoOpsRingTest, InsideOfRingIsNotRing) {
	const Gizmo::GizmoDesc desc = MakeDesc(GizmoMode::Rotate, Vec3(0.0f, 10.0f, 0.0f),
										   Quat::FromEuler(90.0f, 0.0f, 0.0f));
	EXPECT_EQ(Gizmo::HitTest(desc, RayTo(Vec3(0.0f, 10.0f, 0.0f), Vec3::Zero())).handle, GizmoHandle::None);
	EXPECT_EQ(Gizmo::HitTest(desc, RayTo(Vec3(0.5f, 10.0f, 0.0f), Vec3(0.5f, 0.0f, 0.0f))).handle, GizmoHandle::None);
	EXPECT_EQ(Gizmo::HitTest(desc, RayTo(Vec3(3.0f, 10.0f, 0.0f), Vec3(3.0f, 0.0f, 0.0f))).handle, GizmoHandle::None);
}

TEST(EditorGizmoOpsRingTest, EdgeOnRingIsNotTarget) {
	const Gizmo::GizmoDesc desc = MakeDesc(GizmoMode::Rotate, Vec3(0.0f, 0.0f, -10.0f), Quat::Identity());

	EXPECT_NE(Gizmo::HitTest(desc, RayTo(Vec3(0.0f, 0.0f, -10.0f), Vec3(1.0f, 0.0f, 0.0f))).handle, GizmoHandle::AxisY);

	EXPECT_EQ(Gizmo::HitTest(desc, RayTo(Vec3(0.0f, 0.0f, -10.0f), Vec3(0.0f, 1.0f, 0.0f))).handle, GizmoHandle::AxisZ);
}

TEST(EditorGizmoOpsRingTest, RingsFollowTheGizmoOrientation) {
	Gizmo::GizmoDesc desc = MakeDesc(GizmoMode::Rotate, Vec3(0.0f, 10.0f, 0.0f),
									 Quat::FromEuler(90.0f, 0.0f, 0.0f));
	desc.orientation = Quat::FromEuler(90.0f, 0.0f, 0.0f);
	const Gizmo::GizmoHit atRim = Gizmo::HitTest(desc, RayTo(Vec3(1.0f, 10.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f)));
	EXPECT_EQ(atRim.handle, GizmoHandle::AxisZ);
}

TEST(EditorGizmoOpsDragTest, TranslateDragMeasuresDistanceAlongGrabbedAxis) {
	const Gizmo::GizmoDesc desc = MakeDesc(GizmoMode::Translate, Vec3(0.0f, 10.0f, 0.0f),
										   Quat::FromEuler(90.0f, 0.0f, 0.0f));
	Gizmo::DragState drag;
	ASSERT_TRUE(Gizmo::BeginDrag(desc, GizmoHandle::AxisX, RayTo(Vec3(0.5f, 10.0f, 0.0f), Vec3(0.5f, 0.0f, 0.0f)), drag));

	Gizmo::DragValue value;
	ASSERT_TRUE(Gizmo::UpdateDrag(desc, drag, RayTo(Vec3(0.5f, 10.0f, 0.0f), Vec3(0.5f, 0.0f, 0.0f)), value));
	EXPECT_NEAR(value.translation.Length(), 0.0f, 1.0e-3f);

	ASSERT_TRUE(Gizmo::UpdateDrag(desc, drag, RayTo(Vec3(2.5f, 10.0f, 0.0f), Vec3(2.5f, 0.0f, 0.0f)), value));
	EXPECT_NEAR(value.translation.x, 2.0f, 1.0e-3f);
	EXPECT_NEAR(value.translation.y, 0.0f, 1.0e-3f);
	EXPECT_NEAR(value.translation.z, 0.0f, 1.0e-3f);

	ASSERT_TRUE(Gizmo::UpdateDrag(desc, drag, RayTo(Vec3(-1.5f, 10.0f, 0.0f), Vec3(-1.5f, 0.0f, 0.0f)), value));
	EXPECT_NEAR(value.translation.x, -2.0f, 1.0e-3f);
}

TEST(EditorGizmoOpsDragTest, DragKeepsGrabbedHandle) {
	const Gizmo::GizmoDesc desc = MakeDesc(GizmoMode::Translate, Vec3(0.0f, 10.0f, 0.0f),
										   Quat::FromEuler(90.0f, 0.0f, 0.0f));
	Gizmo::DragState drag;
	ASSERT_TRUE(Gizmo::BeginDrag(desc, GizmoHandle::AxisX, RayTo(Vec3(0.5f, 10.0f, 0.0f), Vec3(0.5f, 0.0f, 0.0f)), drag));
	EXPECT_EQ(drag.handle, GizmoHandle::AxisX);

	Gizmo::DragValue value;
	ASSERT_TRUE(Gizmo::UpdateDrag(desc, drag, RayTo(Vec3(3.0f, 10.0f, 5.0f), Vec3(3.0f, 0.0f, 5.0f)), value));
	EXPECT_EQ(drag.handle, GizmoHandle::AxisX);
	EXPECT_NEAR(value.translation.x, 2.5f, 1.0e-3f);
	EXPECT_NEAR(value.translation.z, 0.0f, 1.0e-3f);
}

TEST(EditorGizmoOpsDragTest, RotationDragAccumulatesAcrossAtan2Seam) {
	const Gizmo::GizmoDesc desc = MakeDesc(GizmoMode::Rotate, Vec3(0.0f, 10.0f, 0.0f),
										   Quat::FromEuler(90.0f, 0.0f, 0.0f));
	Gizmo::DragState drag;
	const auto rimRayAt = [](f32 radians) {
		const Vec3 rim(std::cos(radians), 0.0f, std::sin(radians));
		return RayTo(rim + Vec3(0.0f, 10.0f, 0.0f), rim);
	};
	ASSERT_TRUE(Gizmo::BeginDrag(desc, GizmoHandle::AxisY, rimRayAt(0.0f), drag));

	f32 previous = 0.0f;
	Gizmo::DragValue value;
	for (u32 step = 1; step <= 32; ++step) {
		const f32 angle = static_cast<f32>(step) / 32.0f * Math::TAU;
		ASSERT_TRUE(Gizmo::UpdateDrag(desc, drag, rimRayAt(angle), value));
		EXPECT_LT(Math::Abs(value.angleRadians - previous), 0.5f) << "step " << step;
		previous = value.angleRadians;
	}

	EXPECT_NEAR(Math::Abs(value.angleRadians), Math::TAU, 0.05f);
}

TEST(EditorGizmoOpsDragTest, RotationDragWindsPastFullTurn) {
	const Gizmo::GizmoDesc desc = MakeDesc(GizmoMode::Rotate, Vec3(0.0f, 10.0f, 0.0f),
										   Quat::FromEuler(90.0f, 0.0f, 0.0f));
	Gizmo::DragState drag;
	const auto rimRayAt = [](f32 radians) {
		const Vec3 rim(std::cos(radians), 0.0f, std::sin(radians));
		return RayTo(rim + Vec3(0.0f, 10.0f, 0.0f), rim);
	};
	ASSERT_TRUE(Gizmo::BeginDrag(desc, GizmoHandle::AxisY, rimRayAt(0.0f), drag));

	Gizmo::DragValue value;
	for (u32 step = 1; step <= 64; ++step) {
		ASSERT_TRUE(Gizmo::UpdateDrag(desc, drag, rimRayAt(static_cast<f32>(step) / 32.0f * Math::TAU), value));
	}
	EXPECT_NEAR(Math::Abs(value.angleRadians), 2.0f * Math::TAU, 0.05f);
}

TEST(EditorGizmoOpsDragTest, UnsolvableDragRefused) {
	const Gizmo::GizmoDesc desc = MakeDesc(GizmoMode::Translate, Vec3(-10.0f, 0.0f, 0.0f),
										   Quat::FromEuler(0.0f, 90.0f, 0.0f));
	Gizmo::DragState drag;
	EXPECT_FALSE(Gizmo::BeginDrag(desc, GizmoHandle::AxisX, RayTo(Vec3(-10.0f, 0.0f, 0.0f), Vec3(0.5f, 0.0f, 0.0f)), drag));
	EXPECT_EQ(drag.handle, GizmoHandle::None);

	EXPECT_FALSE(Gizmo::BeginDrag(desc, GizmoHandle::None, RayTo(Vec3(-10.0f, 0.0f, 0.0f), Vec3::Zero()), drag));
}

TEST(EditorGizmoOpsGeometryTest, EveryModeEmits3Axes) {
	Vector<DebugLine> lines;
	for (GizmoMode mode : {GizmoMode::Translate, GizmoMode::Rotate, GizmoMode::Scale}) {
		lines.clear();
		Gizmo::GizmoDesc desc = MakeDesc(mode, Vec3(0.0f, 0.0f, -10.0f), Quat::Identity());
		Gizmo::AppendGizmo(lines, desc, GizmoHandle::None, GizmoHandle::None);
		ASSERT_FALSE(lines.empty());

		f32 reach[3] = {};
		for (const DebugLine& line : lines) {
			for (size_t axis = 0; axis < 3; ++axis) {
				reach[axis] = Math::Max(reach[axis], Math::Max(Math::Abs(line.a[axis]), Math::Abs(line.b[axis])));
			}
		}
		for (size_t axis = 0; axis < 3; ++axis) {
			EXPECT_GT(reach[axis], 0.5f) << "mode " << static_cast<i32>(mode) << " axis " << axis;
		}
	}
}

TEST(EditorGizmoOpsGeometryTest, HoverRecolorsDragWins) {
	Vector<DebugLine> plain;
	Vector<DebugLine> hovered;
	const Gizmo::GizmoDesc desc = MakeDesc(GizmoMode::Translate, Vec3(0.0f, 0.0f, -10.0f), Quat::Identity());
	Gizmo::AppendGizmo(plain, desc, GizmoHandle::None, GizmoHandle::None);
	Gizmo::AppendGizmo(hovered, desc, GizmoHandle::AxisX, GizmoHandle::None);
	ASSERT_EQ(plain.size(), hovered.size());

	size_t recolored = 0;
	for (size_t i = 0; i < plain.size(); ++i) {
		recolored += plain[i].color.ToU32() != hovered[i].color.ToU32() ? 1 : 0;
	}
	EXPECT_GT(recolored, 0u);

	Vector<DebugLine> dragging;
	Gizmo::AppendGizmo(dragging, desc, GizmoHandle::AxisX, GizmoHandle::AxisY);
	ASSERT_EQ(dragging.size(), plain.size());
	for (size_t i = 0; i < plain.size(); ++i) {
		const bool litWhileDragging = dragging[i].color.ToU32() != plain[i].color.ToU32();
		const bool litWhileHovering = hovered[i].color.ToU32() != plain[i].color.ToU32();
		EXPECT_FALSE(litWhileDragging && litWhileHovering) << "line " << i;
	}
}

TEST(EditorGizmoOpsGeometryTest, TranslateAndScaleCapsAreThreeDimensional) {
	for (GizmoMode mode : {GizmoMode::Translate, GizmoMode::Scale}) {
		Vector<DebugLine> lines;
		const Gizmo::GizmoDesc desc = MakeDesc(mode, Vec3(0.0f, 0.0f, -10.0f), Quat::Identity());
		Gizmo::AppendGizmo(lines, desc, GizmoHandle::None, GizmoHandle::None);

		f32 spreadY = 0.0f;
		f32 spreadZ = 0.0f;
		for (const DebugLine& line : lines) {
			for (const Vec3& point : {line.a, line.b}) {
				if (point.x > 0.7f) {
					spreadY = Math::Max(spreadY, Math::Abs(point.y));
					spreadZ = Math::Max(spreadZ, Math::Abs(point.z));
				}
			}
		}
		EXPECT_GT(spreadY, 0.01f) << "mode " << static_cast<i32>(mode);
		EXPECT_GT(spreadZ, 0.01f) << "mode " << static_cast<i32>(mode);
	}
}

TEST(EditorGizmoOpsSnapTest, SnapToMultiplesZeroIsOff) {
	EXPECT_NEAR(Gizmo::SnapToIncrement(2.4f, 1.0f), 2.0f, 1.0e-4f);
	EXPECT_NEAR(Gizmo::SnapToIncrement(2.6f, 1.0f), 3.0f, 1.0e-4f);
	EXPECT_NEAR(Gizmo::SnapToIncrement(-2.6f, 1.0f), -3.0f, 1.0e-4f);
	EXPECT_NEAR(Gizmo::SnapToIncrement(37.0f, 15.0f), 30.0f, 1.0e-4f);
	EXPECT_NEAR(Gizmo::SnapToIncrement(2.4f, 0.0f), 2.4f, 1.0e-4f);
	EXPECT_NEAR(Gizmo::SnapToIncrement(2.4f, -1.0f), 2.4f, 1.0e-4f);
}

TEST(EditorGizmoOpsSnapTest, UntouchedAxisIsNeverDraggedOntoGrid) {
	const Vec3 start(0.3f, 0.7f, -0.2f);
	const Vec3 moved(2.4f, 0.7f, -0.2f);
	const Vec3 snapped = Gizmo::SnapChangedComponents(moved, start, 1.0f);
	EXPECT_NEAR(snapped.x, 2.0f, 1.0e-4f);
	EXPECT_NEAR(snapped.y, 0.7f, 1.0e-4f);
	EXPECT_NEAR(snapped.z, -0.2f, 1.0e-4f);

	const Vec3 planeMoved(2.4f, 0.7f, 3.6f);
	const Vec3 planeSnapped = Gizmo::SnapChangedComponents(planeMoved, start, 1.0f);
	EXPECT_NEAR(planeSnapped.x, 2.0f, 1.0e-4f);
	EXPECT_NEAR(planeSnapped.y, 0.7f, 1.0e-4f);
	EXPECT_NEAR(planeSnapped.z, 4.0f, 1.0e-4f);

	EXPECT_NEAR(Gizmo::SnapChangedComponents(moved, start, 0.0f).x, 2.4f, 1.0e-4f);
}

TEST(EditorGizmoOpsHandleSetTest, EachModeCarriesHandlesItCanAct) {
	for (GizmoMode mode : {GizmoMode::Translate, GizmoMode::Rotate, GizmoMode::Scale}) {
		EXPECT_TRUE(Gizmo::HandleIsInMode(GizmoHandle::AxisX, mode));
		EXPECT_FALSE(Gizmo::HandleIsInMode(GizmoHandle::None, mode));
	}
	EXPECT_TRUE(Gizmo::HandleIsInMode(GizmoHandle::PlaneZX, GizmoMode::Translate));
	EXPECT_TRUE(Gizmo::HandleIsInMode(GizmoHandle::PlaneZX, GizmoMode::Scale));
	EXPECT_FALSE(Gizmo::HandleIsInMode(GizmoHandle::PlaneZX, GizmoMode::Rotate));
	EXPECT_TRUE(Gizmo::HandleIsInMode(GizmoHandle::Screen, GizmoMode::Translate));
	EXPECT_FALSE(Gizmo::HandleIsInMode(GizmoHandle::Screen, GizmoMode::Rotate));
	EXPECT_TRUE(Gizmo::HandleIsInMode(GizmoHandle::ScreenRotate, GizmoMode::Rotate));
	EXPECT_FALSE(Gizmo::HandleIsInMode(GizmoHandle::ScreenRotate, GizmoMode::Translate));
}

TEST(EditorGizmoOpsHandleSetTest, PlaneQuadIsGrabbedOverFace) {
	const Gizmo::GizmoDesc desc = MakeDesc(GizmoMode::Translate, Vec3(0.0f, 10.0f, 0.0f),
										   Quat::FromEuler(90.0f, 0.0f, 0.0f));
	EXPECT_EQ(Gizmo::HitTest(desc, RayTo(Vec3(0.5f, 10.0f, 0.5f), Vec3(0.5f, 0.0f, 0.5f))).handle, GizmoHandle::PlaneZX);

	EXPECT_EQ(Gizmo::HitTest(desc, RayTo(Vec3(0.5f, 10.0f, 0.0f), Vec3(0.5f, 0.0f, 0.0f))).handle, GizmoHandle::AxisX);

	EXPECT_EQ(Gizmo::HitTest(desc, RayTo(Vec3(2.0f, 10.0f, 2.0f), Vec3(2.0f, 0.0f, 2.0f))).handle, GizmoHandle::None);
}

TEST(EditorGizmoOpsHandleSetTest, PlaneQuadFlipsToCameraSide) {
	Gizmo::GizmoDesc mirrored = MakeDesc(GizmoMode::Translate, Vec3(0.0f, 10.0f, 0.0f),
										 Quat::FromEuler(90.0f, 0.0f, 0.0f));
	mirrored.viewpoint.position = Vec3(-6.0f, 10.0f, -6.0f);

	EXPECT_EQ(Gizmo::HitTest(mirrored, RayTo(Vec3(-0.5f, 10.0f, -0.5f), Vec3(-0.5f, 0.0f, -0.5f))).handle,
			  GizmoHandle::PlaneZX);
	EXPECT_NE(Gizmo::HitTest(mirrored, RayTo(Vec3(0.5f, 10.0f, 0.5f), Vec3(0.5f, 0.0f, 0.5f))).handle,
			  GizmoHandle::PlaneZX);
}

TEST(EditorGizmoOpsHandleSetTest, ScreenHandleWinsAtCenter) {
	const Gizmo::GizmoDesc desc = MakeDesc(GizmoMode::Translate, Vec3(0.0f, 10.0f, 0.0f),
										   Quat::FromEuler(90.0f, 0.0f, 0.0f));
	EXPECT_EQ(Gizmo::HitTest(desc, RayTo(Vec3(0.0f, 10.0f, 0.0f), Vec3::Zero())).handle, GizmoHandle::Screen);

	EXPECT_EQ(Gizmo::HitTest(desc, RayTo(Vec3(0.9f, 10.0f, 0.0f), Vec3(0.9f, 0.0f, 0.0f))).handle, GizmoHandle::AxisX);
}

TEST(EditorGizmoOpsHandleSetTest, ViewRingSitsOutsideAxisRings) {
	const Gizmo::GizmoDesc desc = MakeDesc(GizmoMode::Rotate, Vec3(0.0f, 10.0f, 0.0f),
										   Quat::FromEuler(90.0f, 0.0f, 0.0f));
	const f32 outer = desc.scale * Gizmo::SCREEN_RING_SCALE;
	EXPECT_EQ(Gizmo::HitTest(desc, RayTo(Vec3(outer, 10.0f, 0.0f), Vec3(outer, 0.0f, 0.0f))).handle,
			  GizmoHandle::ScreenRotate);
	EXPECT_EQ(Gizmo::HitTest(desc, RayTo(Vec3(1.0f, 10.0f, 0.0f), Vec3(1.0f, 0.0f, 0.0f))).handle, GizmoHandle::AxisY);
}

TEST(EditorGizmoOpsRingTest, RingBackHalfIgnored) {
	const Gizmo::GizmoDesc desc = MakeDesc(GizmoMode::Rotate, Vec3(0.0f, 6.0f, -8.0f),
										   Quat::FromEuler(37.0f, 0.0f, 0.0f));

	EXPECT_EQ(Gizmo::HitTest(desc, RayTo(desc.viewpoint.position, Vec3(0.0f, 0.0f, -1.0f))).handle, GizmoHandle::AxisY);
	EXPECT_NE(Gizmo::HitTest(desc, RayTo(desc.viewpoint.position, Vec3(0.0f, 0.0f, 1.0f))).handle, GizmoHandle::AxisY);

	Vector<DebugLine> tilted;
	Gizmo::AppendGizmo(tilted, desc, GizmoHandle::None, GizmoHandle::None);
	const Gizmo::GizmoDesc squareOn = MakeDesc(GizmoMode::Rotate, Vec3(0.0f, 10.0f, 0.0f),
											   Quat::FromEuler(90.0f, 0.0f, 0.0f));
	Vector<DebugLine> facing;
	Gizmo::AppendGizmo(facing, squareOn, GizmoHandle::None, GizmoHandle::None);
	EXPECT_LT(tilted.size(), facing.size());
}

TEST(EditorGizmoOpsDragTest, PlaneDragMovesTwoAxes) {
	const Gizmo::GizmoDesc desc = MakeDesc(GizmoMode::Translate, Vec3(0.0f, 10.0f, 0.0f),
										   Quat::FromEuler(90.0f, 0.0f, 0.0f));
	Gizmo::DragState drag;
	ASSERT_TRUE(Gizmo::BeginDrag(desc, GizmoHandle::PlaneZX, RayTo(Vec3(0.5f, 10.0f, 0.5f), Vec3(0.5f, 0.0f, 0.5f)), drag));

	Gizmo::DragValue value;
	ASSERT_TRUE(Gizmo::UpdateDrag(desc, drag, RayTo(Vec3(2.5f, 10.0f, 3.5f), Vec3(2.5f, 0.0f, 3.5f)), value));
	EXPECT_NEAR(value.translation.x, 2.0f, 1.0e-3f);
	EXPECT_NEAR(value.translation.z, 3.0f, 1.0e-3f);
	EXPECT_NEAR(value.translation.y, 0.0f, 1.0e-3f);
}

TEST(EditorGizmoOpsDragTest, ScreenScalesUniformPlaneTwoAxes) {
	const Gizmo::GizmoDesc desc = MakeDesc(GizmoMode::Scale, Vec3(0.0f, 10.0f, 0.0f),
										   Quat::FromEuler(90.0f, 0.0f, 0.0f));

	Gizmo::DragState planeDrag;
	ASSERT_TRUE(Gizmo::BeginDrag(desc, GizmoHandle::PlaneZX, RayTo(Vec3(0.5f, 10.0f, 0.5f), Vec3(0.5f, 0.0f, 0.5f)), planeDrag));
	Gizmo::DragValue value;
	ASSERT_TRUE(Gizmo::UpdateDrag(desc, planeDrag, RayTo(Vec3(1.5f, 10.0f, 1.5f), Vec3(1.5f, 0.0f, 1.5f)), value));
	EXPECT_GT(value.scaleMultiplier.x, 1.0f);
	EXPECT_GT(value.scaleMultiplier.z, 1.0f);
	EXPECT_NEAR(value.scaleMultiplier.y, 1.0f, 1.0e-4f);

	Gizmo::DragState screenDrag;
	ASSERT_TRUE(Gizmo::BeginDrag(desc, GizmoHandle::Screen, RayTo(Vec3(0.05f, 10.0f, 0.0f), Vec3(0.05f, 0.0f, 0.0f)), screenDrag));
	ASSERT_TRUE(Gizmo::UpdateDrag(desc, screenDrag, RayTo(Vec3(1.05f, 10.0f, 0.0f), Vec3(1.05f, 0.0f, 0.0f)), value));
	EXPECT_GT(value.scaleMultiplier.x, 1.0f);
	EXPECT_NEAR(value.scaleMultiplier.y, value.scaleMultiplier.x, 1.0e-4f);
	EXPECT_NEAR(value.scaleMultiplier.z, value.scaleMultiplier.x, 1.0e-4f);

	ASSERT_TRUE(Gizmo::UpdateDrag(desc, screenDrag, RayTo(Vec3(-0.45f, 10.0f, 0.0f), Vec3(-0.45f, 0.0f, 0.0f)), value));
	EXPECT_LT(value.scaleMultiplier.x, 1.0f);
	EXPECT_GT(value.scaleMultiplier.x, 0.0f);
}

TEST(EditorGizmoOpsDragTest, ScaleDragKeepsHandleLengthItGrabbed) {
	Gizmo::GizmoDesc desc = MakeDesc(GizmoMode::Scale, Vec3(0.0f, 10.0f, 0.0f), Quat::FromEuler(90.0f, 0.0f, 0.0f));
	Gizmo::DragState drag;
	ASSERT_TRUE(Gizmo::BeginDrag(desc, GizmoHandle::AxisX, RayTo(Vec3(0.5f, 10.0f, 0.0f), Vec3(0.5f, 0.0f, 0.0f)), drag));
	EXPECT_NEAR(drag.referenceScale, desc.scale, 1.0e-4f);

	Gizmo::DragValue before;
	ASSERT_TRUE(Gizmo::UpdateDrag(desc, drag, RayTo(Vec3(1.5f, 10.0f, 0.0f), Vec3(1.5f, 0.0f, 0.0f)), before));

	desc.scale = 4.0f;
	Gizmo::DragValue after;
	ASSERT_TRUE(Gizmo::UpdateDrag(desc, drag, RayTo(Vec3(1.5f, 10.0f, 0.0f), Vec3(1.5f, 0.0f, 0.0f)), after));
	EXPECT_NEAR(after.scaleMultiplier.x, before.scaleMultiplier.x, 1.0e-4f);
}

TEST(EditorGizmoOpsRingTest, ARingStaysGrabbableAsItTilts) {
	constexpr f32 DISTANCE = 5.0f;
	constexpr f32 VIEWPORT_PX = 1000.0f;
	u32 faceOnBand = 0;
	for (f32 yawDeg : {90.0f, 75.0f, 60.0f, 45.0f, 30.0f, 20.0f, 12.0f, 6.0f}) {
		const Quat rotation = Ops::BuildCameraRotation(yawDeg, 0.0f);
		const Vec3 camera = -(rotation * Vec3::Forward()) * DISTANCE;
		Gizmo::GizmoDesc desc = MakeDesc(GizmoMode::Rotate, camera, rotation);
		desc.scale = Gizmo::HandleScaleAt(desc.viewpoint, Vec3::Zero());
		desc.pickTolerance = Gizmo::WorldRadiusForPixels(desc.viewpoint, Vec3::Zero(),
														 VIEWPORT_PX, Gizmo::HANDLE_PICK_PX);

		const Mat4 view = Ops::BuildViewMatrix(camera, rotation);
		const Mat4 projection = Ops::BuildProjection(1.0f);
		u32 band = 0;
		u32 run = 0;
		for (i32 x = 0; x < static_cast<i32>(VIEWPORT_PX); ++x) {
			const Ray ray = Ops::BuildViewportRay(view, projection,
												  Vec2{static_cast<f32>(x), VIEWPORT_PX * 0.5f},
												  Vec2{VIEWPORT_PX, VIEWPORT_PX});
			run = Gizmo::HitTest(desc, ray).handle == GizmoHandle::AxisX ? run + 1 : 0;
			band = Math::Max(band, run);
		}
		if (yawDeg == 90.0f) {
			faceOnBand = band;
			ASSERT_NEAR(static_cast<f32>(band), 2.0f * Gizmo::HANDLE_PICK_PX, 4.0f);
		}
		EXPECT_GE(band, faceOnBand * 3 / 4) << band << ' ' << yawDeg;
	}
}

TEST(EditorGizmoOpsSpaceTest, SpaceSetsFrameExceptScale) {
	const Quat yawed = Quat::FromEuler(0.0f, 90.0f, 0.0f);

	EXPECT_EQ(Gizmo::OrientationForSpace(GizmoSpace::Local, GizmoMode::Translate, yawed), yawed);
	EXPECT_EQ(Gizmo::OrientationForSpace(GizmoSpace::Local, GizmoMode::Rotate, yawed), yawed);
	EXPECT_EQ(Gizmo::OrientationForSpace(GizmoSpace::World, GizmoMode::Translate, yawed), Quat::Identity());
	EXPECT_EQ(Gizmo::OrientationForSpace(GizmoSpace::World, GizmoMode::Rotate, yawed), Quat::Identity());
	EXPECT_EQ(Gizmo::OrientationForSpace(GizmoSpace::World, GizmoMode::Scale, yawed), yawed);
}

TEST(EditorGizmoOpsSpaceTest, WorldHandlesUseWorldAxes) {
	const Quat yawed = Quat::FromEuler(0.0f, 90.0f, 0.0f);
	Gizmo::GizmoDesc local = MakeDesc(GizmoMode::Translate, Vec3(0.0f, 10.0f, 0.0f),
									  Quat::FromEuler(90.0f, 0.0f, 0.0f));
	Gizmo::GizmoDesc world = local;
	local.orientation = Gizmo::OrientationForSpace(GizmoSpace::Local, GizmoMode::Translate, yawed);
	world.orientation = Gizmo::OrientationForSpace(GizmoSpace::World, GizmoMode::Translate, yawed);

	const Ray downWorldX = RayTo(Vec3(0.9f, 10.0f, 0.0f), Vec3(0.9f, 0.0f, 0.0f));
	EXPECT_EQ(Gizmo::HitTest(local, downWorldX).handle, GizmoHandle::AxisZ);
	EXPECT_EQ(Gizmo::HitTest(world, downWorldX).handle, GizmoHandle::AxisX);
}

TEST(EditorGizmoOpsSpaceTest, WorldSpaceDragMovesAlongWorldAxis) {
	const Quat yawed = Quat::FromEuler(0.0f, 90.0f, 0.0f);
	Gizmo::GizmoDesc desc = MakeDesc(GizmoMode::Translate, Vec3(0.0f, 10.0f, 0.0f),
									 Quat::FromEuler(90.0f, 0.0f, 0.0f));
	desc.orientation = Gizmo::OrientationForSpace(GizmoSpace::World, GizmoMode::Translate, yawed);

	Gizmo::DragState state;
	ASSERT_TRUE(Gizmo::BeginDrag(desc, GizmoHandle::AxisX, RayTo(Vec3(0.5f, 10.0f, 0.0f), Vec3(0.5f, 0.0f, 0.0f)), state));
	Gizmo::DragValue value;
	ASSERT_TRUE(Gizmo::UpdateDrag(desc, state, RayTo(Vec3(1.5f, 10.0f, 0.0f), Vec3(1.5f, 0.0f, 0.0f)), value));
	EXPECT_NEAR(value.translation.x, 1.0f, 1.0e-3f);
	EXPECT_NEAR(value.translation.y, 0.0f, 1.0e-3f);
	EXPECT_NEAR(value.translation.z, 0.0f, 1.0e-3f);
}

namespace {

	Gizmo::DragSnapshot SnapshotAt(Vec3 position, Vec3 scale = Vec3::One()) {
		Gizmo::DragSnapshot snapshot;
		snapshot.localPosition = position;
		snapshot.localScale = scale;
		snapshot.worldPosition = position;
		return snapshot;
	}

	Vector<Gizmo::DragOutcome> SolveOver(std::span<const Gizmo::DragSnapshot> starts, GizmoMode mode,
										 const Vec3& axisWorld, const Gizmo::DragValue& value, const Vec3& pivot,
										 const Gizmo::SnapSettings& snap = {}) {
		Gizmo::DragState drag;
		drag.origin = pivot;
		drag.axisWorld = axisWorld;
		Vector<Gizmo::DragOutcome> out{GetMemoryResource(MemTag::Editor)};
		Gizmo::SolveDragTargets(starts, mode, drag, value, pivot, Quat::Identity(), snap, out);
		return out;
	}

} // anonymous namespace

TEST(EditorGizmoDragTargetsTest, TranslateMovesEveryTargetBySameDelta) {
	const Gizmo::DragSnapshot starts[] = {SnapshotAt(Vec3(0.0f, 0.0f, 0.0f)), SnapshotAt(Vec3(4.0f, 0.0f, 0.0f)),
										  SnapshotAt(Vec3(0.0f, 0.0f, 6.0f))};
	Gizmo::DragValue value;
	value.translation = Vec3(2.0f, 0.0f, 0.0f);

	const Vector<Gizmo::DragOutcome> out = SolveOver(starts, GizmoMode::Translate, Vec3::Right(), value, Vec3(1.5f, 0.0f, 2.0f));

	ASSERT_EQ(out.size(), 3u);
	for (size_t i = 0; i < 3; ++i) {
		EXPECT_TRUE(out[i].positionChanged);
		EXPECT_FALSE(out[i].rotationChanged);
		EXPECT_FALSE(out[i].scaleChanged);
		EXPECT_NEAR(out[i].localPosition.x, starts[i].localPosition.x + 2.0f, 1.0e-4f);
		EXPECT_NEAR(out[i].localPosition.z, starts[i].localPosition.z, 1.0e-4f);
	}
}

TEST(EditorGizmoDragTargetsTest, RotateOrbitsSharedPivotNotEachOwn) {
	const Vec3 pivot(4.0f, 0.0f, 0.0f);
	const Gizmo::DragSnapshot starts[] = {SnapshotAt(Vec3(3.0f, 0.0f, 0.0f)), SnapshotAt(Vec3(5.0f, 0.0f, 0.0f))};
	Gizmo::DragValue value;
	value.angleRadians = 90.0f * Math::DEG_TO_RAD;

	const Vector<Gizmo::DragOutcome> out = SolveOver(starts, GizmoMode::Rotate, Vec3::Up(), value, pivot);

	ASSERT_EQ(out.size(), 2u);
	const Quat spin = Quat::AngleAxis(90.0f, Vec3::Up());
	for (size_t i = 0; i < 2; ++i) {
		EXPECT_TRUE(out[i].rotationChanged);
		EXPECT_TRUE(out[i].positionChanged);
		const Vec3 expected = pivot + spin * (starts[i].worldPosition - pivot);
		EXPECT_NEAR(out[i].localPosition.x, expected.x, 1.0e-4f);
		EXPECT_NEAR(out[i].localPosition.y, expected.y, 1.0e-4f);
		EXPECT_NEAR(out[i].localPosition.z, expected.z, 1.0e-4f);
		EXPECT_NEAR(out[i].localRotation.w, spin.w, 1.0e-4f);
	}
}

TEST(EditorGizmoDragTargetsTest, ScaleMovesTargetsAwayFromSharedPivot) {
	const Vec3 pivot(4.0f, 0.0f, 0.0f);
	const Gizmo::DragSnapshot starts[] = {SnapshotAt(Vec3(2.0f, 0.0f, 0.0f)), SnapshotAt(Vec3(6.0f, 0.0f, 0.0f))};
	Gizmo::DragValue value;
	value.scaleMultiplier = Vec3(2.0f, 1.0f, 1.0f);

	const Vector<Gizmo::DragOutcome> out = SolveOver(starts, GizmoMode::Scale, Vec3::Right(), value, pivot);

	ASSERT_EQ(out.size(), 2u);
	EXPECT_NEAR(out[0].localPosition.x, 0.0f, 1.0e-4f);
	EXPECT_NEAR(out[1].localPosition.x, 8.0f, 1.0e-4f);
	for (size_t i = 0; i < 2; ++i) {
		EXPECT_TRUE(out[i].scaleChanged);
		EXPECT_TRUE(out[i].positionChanged);
		EXPECT_NEAR(out[i].localScale.x, 2.0f, 1.0e-4f);
		EXPECT_NEAR(out[i].localScale.y, 1.0f, 1.0e-4f);
	}
}

TEST(EditorGizmoDragTargetsTest, OneTargetRotatesAndScalesInPlace) {
	const Vec3 where(2.0f, 3.0f, 4.0f);
	const Gizmo::DragSnapshot starts[] = {SnapshotAt(where)};

	Gizmo::DragValue rotateValue;
	rotateValue.angleRadians = 40.0f * Math::DEG_TO_RAD;
	const Vector<Gizmo::DragOutcome> rotated = SolveOver(starts, GizmoMode::Rotate, Vec3::Up(), rotateValue, where);
	ASSERT_EQ(rotated.size(), 1u);
	EXPECT_TRUE(rotated[0].rotationChanged);
	EXPECT_FALSE(rotated[0].positionChanged);

	Gizmo::DragValue scaleValue;
	scaleValue.scaleMultiplier = Vec3(3.0f, 1.0f, 1.0f);
	const Vector<Gizmo::DragOutcome> scaled = SolveOver(starts, GizmoMode::Scale, Vec3::Right(), scaleValue, where);
	ASSERT_EQ(scaled.size(), 1u);
	EXPECT_TRUE(scaled[0].scaleChanged);
	EXPECT_FALSE(scaled[0].positionChanged);
	EXPECT_NEAR(scaled[0].localScale.x, 3.0f, 1.0e-4f);
}

TEST(EditorGizmoDragTargetsTest, SnappingSolvesOnceAndKeepsSelectionsSpacing) {
	const Gizmo::DragSnapshot starts[] = {SnapshotAt(Vec3(0.0f, 0.0f, 0.0f)), SnapshotAt(Vec3(5.4f, 0.0f, 0.0f))};
	Gizmo::DragValue value;
	value.translation = Vec3(0.9f, 0.0f, 0.0f);
	Gizmo::SnapSettings snap;
	snap.translate = 1.0f;

	const Vector<Gizmo::DragOutcome> out = SolveOver(starts, GizmoMode::Translate, Vec3::Right(), value, Vec3::Zero(), snap);

	ASSERT_EQ(out.size(), 2u);
	EXPECT_NEAR(out[0].localPosition.x, 1.0f, 1.0e-4f);
	EXPECT_NEAR(out[1].localPosition.x, 6.4f, 1.0e-4f);
}

TEST(EditorGizmoDragTargetsTest, SnappedScaleSharesMultiplier) {
	const Gizmo::DragSnapshot starts[] = {SnapshotAt(Vec3::Zero(), Vec3(1.0f, 1.0f, 1.0f)),
										  SnapshotAt(Vec3::Zero(), Vec3(2.0f, 1.0f, 1.0f))};
	Gizmo::DragValue value;
	value.scaleMultiplier = Vec3(1.14f, 1.0f, 1.0f);
	Gizmo::SnapSettings snap;
	snap.scale = 0.1f;

	const Vector<Gizmo::DragOutcome> out = SolveOver(starts, GizmoMode::Scale, Vec3::Right(), value, Vec3::Zero(), snap);

	ASSERT_EQ(out.size(), 2u);
	EXPECT_NEAR(out[0].localScale.x, 1.1f, 1.0e-4f);
	EXPECT_NEAR(out[1].localScale.x, 2.2f, 1.0e-4f);
	EXPECT_FALSE(out[0].positionChanged);
	EXPECT_FALSE(out[1].positionChanged);
}

TEST(EditorGizmoVertexMarkerTest, MarkerIsSquareOnPointFacingCamera) {
	Vector<DebugLine> lines{GetMemoryResource(MemTag::Editor)};
	const Vec3 point(2.0f, -1.0f, 3.0f);
	const Quat cameraRotation = Quat::FromEuler(20.0f, 35.0f, 0.0f);
	const f32 half = 0.25f;

	Gizmo::AppendVertexMarker(lines, point, cameraRotation, half, Color32::White());

	ASSERT_EQ(lines.size(), 4u);
	const Vec3 forward = cameraRotation * Vec3::Forward();
	f32 furthest = 0.0f;
	for (const DebugLine& line : lines) {
		EXPECT_NEAR(Vec3::Dot(line.a - point, forward), 0.0f, 1.0e-4f);
		EXPECT_NEAR(Vec3::Dot(line.b - point, forward), 0.0f, 1.0e-4f);
		furthest = Math::Max(furthest, (line.a - point).Length());
		EXPECT_NEAR((line.b - line.a).Length(), half * 2.0f, 1.0e-4f);
	}
	EXPECT_NEAR(furthest, half * std::sqrt(2.0f), 1.0e-4f);
}

namespace {

	const Gizmo::RadiusDot* DotAlong(std::span<const Gizmo::RadiusDot> dots, const Vec3& axis) {
		for (const Gizmo::RadiusDot& dot : dots) {
			if (Vec3::Dot(dot.axis, axis) > 0.99f) {
				return &dot;
			}
		}
		return nullptr;
	}

} // anonymous namespace

TEST(EditorGizmoOpsRadiusTest, DotsSkipLineOfSight) {
	Gizmo::RadiusDot dots[6];
	const Ops::Viewpoint headOn = MakeViewpoint(Vec3(0.0f, 0.0f, -10.0f), Quat::Identity());
	ASSERT_EQ(Gizmo::RadiusDots(headOn, Vec3::Zero(), Quat::Identity(), 2.0f, dots), 4u);
	for (u32 index = 0; index < 4; ++index) {
		EXPECT_NEAR(dots[index].axis.z, 0.0f, 1.0e-5f);
		EXPECT_NEAR(Vec3::Distance(dots[index].position, Vec3::Zero()), 2.0f, 1.0e-5f);
	}

	const Vec3 eye(0.0f, 6.0f, -8.0f);
	const Ops::Viewpoint above = MakeViewpoint(eye, Quat::LookRotation((Vec3::Zero() - eye).Normalized()));
	ASSERT_EQ(Gizmo::RadiusDots(above, Vec3::Zero(), Quat::Identity(), 2.0f, dots), 6u);
	const std::span<const Gizmo::RadiusDot> all(dots, 6);
	ASSERT_NE(DotAlong(all, Vec3::Up()), nullptr);
	ASSERT_NE(DotAlong(all, Vec3::Down()), nullptr);
	ASSERT_NE(DotAlong(all, Vec3::Forward()), nullptr);
	ASSERT_NE(DotAlong(all, Vec3::Back()), nullptr);
	EXPECT_TRUE(DotAlong(all, Vec3::Up())->front);
	EXPECT_TRUE(DotAlong(all, Vec3::Back())->front);
	EXPECT_FALSE(DotAlong(all, Vec3::Forward())->front);
	EXPECT_FALSE(DotAlong(all, Vec3::Down())->front);

	EXPECT_EQ(Gizmo::RadiusDots(above, Vec3::Zero(), Quat::Identity(), 0.0f, dots), 0u);
}

TEST(EditorGizmoOpsRadiusTest, PressFindsOnlyDots) {
	const Vec3 eye(0.0f, 0.0f, -10.0f);
	const Ops::Viewpoint headOn = MakeViewpoint(eye, Quat::Identity());
	Gizmo::RadiusDot dots[6];
	const u32 count = Gizmo::RadiusDots(headOn, Vec3::Zero(), Quat::Identity(), 2.0f, dots);
	const std::span<const Gizmo::RadiusDot> shown(dots, count);
	const Gizmo::RadiusDot* right = DotAlong(shown, Vec3::Right());
	const Gizmo::RadiusDot* up = DotAlong(shown, Vec3::Up());
	ASSERT_NE(right, nullptr);
	ASSERT_NE(up, nullptr);
	f32 rayT = 0.0f;
	EXPECT_EQ(Gizmo::HitRadiusDots(shown, headOn, 600.0f, RayTo(eye, right->position), rayT), static_cast<i32>(right - dots));
	EXPECT_EQ(Gizmo::HitRadiusDots(shown, headOn, 600.0f, RayTo(eye, (right->position + up->position) * 0.5f), rayT), -1);
}

TEST(EditorGizmoOpsRadiusTest, DotDragsRadiusByTravelAlongAxis) {
	const Vec3 eye(0.0f, 0.0f, -10.0f);
	const Ops::Viewpoint headOn = MakeViewpoint(eye, Quat::Identity());
	Gizmo::RadiusDot dots[6];
	const u32 count = Gizmo::RadiusDots(headOn, Vec3::Zero(), Quat::Identity(), 2.0f, dots);
	const Gizmo::RadiusDot* right = DotAlong({dots, count}, Vec3::Right());
	ASSERT_NE(right, nullptr);

	Gizmo::RadiusDrag drag;
	ASSERT_TRUE(Gizmo::BeginRadiusDrag(headOn, Vec3::Zero(), *right, 2.0f, RayTo(eye, Vec3(2.1f, 0.3f, 0.0f)), drag));
	f32 radius = 0.0f;
	ASSERT_TRUE(Gizmo::UpdateRadiusDrag(drag, RayTo(eye, Vec3(3.1f, 0.3f, 0.0f)), radius));
	EXPECT_NEAR(radius, 3.0f, 1.0e-4f);
	ASSERT_TRUE(Gizmo::UpdateRadiusDrag(drag, RayTo(eye, Vec3(3.1f, 1.3f, 0.0f)), radius));
	EXPECT_NEAR(radius, 3.0f, 1.0e-4f);
	ASSERT_TRUE(Gizmo::UpdateRadiusDrag(drag, RayTo(eye, Vec3(-0.9f, 0.3f, 0.0f)), radius));
	EXPECT_NEAR(radius, 1.0f, 1.0e-4f);
}

namespace {

	SceneHandle UnderParent() {
		SceneHandle handle;
		handle.parentRotation = Quat::AngleAxis(90.0f, Vec3::Up());
		handle.parentToWorld = Mat4::TRS(Vec3(5.0f, 0.0f, 0.0f), handle.parentRotation, Vec3(2.0f, 2.0f, 2.0f));
		handle.position = Vec3(1.0f, 0.0f, 0.0f);
		return handle;
	}

} // anonymous namespace

TEST(EditorGizmoOpsSceneHandleTest, EachKindShowsGizmoUnderEachTool) {
	GizmoMode mode = GizmoMode::Translate;
	ASSERT_TRUE(Gizmo::SceneHandleMode(SceneHandleKind::Transform, GizmoMode::Scale, mode));
	EXPECT_EQ(mode, GizmoMode::Scale);
	ASSERT_TRUE(Gizmo::SceneHandleMode(SceneHandleKind::Position, GizmoMode::Rotate, mode));
	EXPECT_EQ(mode, GizmoMode::Translate);
	EXPECT_FALSE(Gizmo::SceneHandleMode(SceneHandleKind::Rotation, GizmoMode::Translate, mode));
	ASSERT_TRUE(Gizmo::SceneHandleMode(SceneHandleKind::Rotation, GizmoMode::Rotate, mode));
	EXPECT_EQ(mode, GizmoMode::Rotate);
	EXPECT_FALSE(Gizmo::SceneHandleMode(SceneHandleKind::Slider, GizmoMode::Translate, mode));
}

TEST(EditorGizmoOpsSceneHandleTest, FrameHandleSolvedInValueSpace) {
	const SceneHandle start = UnderParent();
	EXPECT_NEAR(Vec3::Distance(Gizmo::SceneHandleOrigin(start), start.parentToWorld.TransformPoint(start.position)), 0.0f, 1.0e-5f);
	Gizmo::DragState drag;
	drag.origin = Gizmo::SceneHandleOrigin(start);
	Gizmo::DragValue value;
	value.translation = Vec3(0.0f, 4.0f, 0.0f);
	const SceneHandle moved = Gizmo::SolveSceneHandleDrag(start, GizmoMode::Translate, drag, value, Quat::Identity(), Gizmo::SnapSettings{});
	EXPECT_NEAR(moved.position.y, 2.0f, 1.0e-5f);
	EXPECT_NEAR(moved.position.x, 1.0f, 1.0e-5f);
	Gizmo::SnapSettings snap;
	snap.translate = 1.5f;
	value.translation = Vec3(0.0f, 2.2f, 0.0f);
	EXPECT_NEAR(Gizmo::SolveSceneHandleDrag(start, GizmoMode::Translate, drag, value, Quat::Identity(), snap).position.y, 1.5f, 1.0e-5f);

	drag.axisWorld = Vec3::Up();
	value = {};
	value.angleRadians = Math::PI * 0.5f;
	const SceneHandle turned = Gizmo::SolveSceneHandleDrag(start, GizmoMode::Rotate, drag, value, Quat::Identity(), Gizmo::SnapSettings{});
	EXPECT_NEAR(Math::Abs(Quat::Dot(turned.rotation, Quat::AngleAxis(90.0f, Vec3::Up()))), 1.0f, 1.0e-5f);
	EXPECT_NEAR(Vec3::Distance(turned.position, start.position), 0.0f, 1.0e-5f);
}

TEST(EditorGizmoOpsSceneHandleTest, SliderMovesDistanceInSpaceAndSigned) {
	SceneHandle slider;
	slider.kind = SceneHandleKind::Slider;
	slider.parentToWorld = Mat4::Scale(Vec3(2.0f, 2.0f, 2.0f));
	slider.direction = Vec3::Right();
	slider.distance = 1.0f;
	const Gizmo::RadiusDot dot = Gizmo::SliderDot(slider);
	EXPECT_NEAR(Vec3::Distance(dot.position, Vec3(2.0f, 0.0f, 0.0f)), 0.0f, 1.0e-5f);
	EXPECT_NEAR(dot.axis.x, 1.0f, 1.0e-5f);

	const Vec3 eye(0.0f, 0.0f, -10.0f);
	const Ops::Viewpoint headOn = MakeViewpoint(eye, Quat::Identity());
	Gizmo::RadiusDrag drag;
	ASSERT_TRUE(Gizmo::BeginSliderDrag(headOn, slider, RayTo(eye, Vec3(2.0f, 0.2f, 0.0f)), drag));
	f32 distance = 0.0f;
	ASSERT_TRUE(Gizmo::UpdateSliderDrag(drag, slider, RayTo(eye, Vec3(4.0f, 0.2f, 0.0f)), distance));
	EXPECT_NEAR(distance, 2.0f, 1.0e-4f);
	ASSERT_TRUE(Gizmo::UpdateSliderDrag(drag, slider, RayTo(eye, Vec3(-2.0f, 0.2f, 0.0f)), distance));
	EXPECT_NEAR(distance, -1.0f, 1.0e-4f);
}
