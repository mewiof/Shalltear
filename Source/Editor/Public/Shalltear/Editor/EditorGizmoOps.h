#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"
#include "Shalltear/Editor/EditorGizmos.h"
#include "Shalltear/Editor/EditorViewportOps.h"

#include <span>

namespace Shalltear {

	class SceneObject;

	enum class GizmoMode : i32 {
		Translate = 0,
		Rotate,
		Scale
	};

	enum class GizmoSpace : i32 {
		Local = 0,
		World
	};

	namespace EditorGizmoOps {

		enum class GizmoHandle : u8 {
			None = 0,
			AxisX,
			AxisY,
			AxisZ,
			PlaneYZ,
			PlaneZX,
			PlaneXY,
			Screen,
			ScreenRotate
		};

		inline constexpr f32 HANDLE_PICK_PX = 10.0f;
		inline constexpr f32 GIZMO_VIEW_FRACTION = 0.155885f;
		inline constexpr f32 RING_GRAZING_MINIMUM = 0.02f;
		inline constexpr f32 RING_BACK_HALF_COS = 0.05f;
		inline constexpr f32 PLANE_HANDLE_OFFSET = 0.35f;
		inline constexpr f32 PLANE_HANDLE_SIZE = 0.30f;
		inline constexpr f32 SCREEN_HANDLE_HALF = 0.12f;
		inline constexpr f32 VERTEX_MARKER_HALF = 0.07f;
		inline constexpr f32 SCREEN_RING_SCALE = 1.2f;

		struct GizmoDesc {
			Vec3 origin;
			Quat orientation = Quat::Identity();
			EditorViewportOps::Viewpoint viewpoint;
			GizmoMode mode = GizmoMode::Translate;
			f32 scale = 1.0f;
			f32 pickTolerance = 0.0f; // world units at the origin's depth
		};

		struct GizmoHit {
			GizmoHandle handle = GizmoHandle::None;
			f32 distance = 0.0f;
		};

		struct DragValue {
			Vec3 translation;
			f32 angleRadians = 0.0f;
			Vec3 scaleMultiplier{1.0f, 1.0f, 1.0f};
		};

		struct DragState {
			GizmoHandle handle = GizmoHandle::None;
			Vec3 origin;

			Vec3 axisWorld;
			Vec3 scaleMask;

			Vec3 planeNormal;
			Vec3 grabPoint;
			Vec3 dragDirection;

			f32 previousAngle = 0.0f;
			f32 accumulatedAngle = 0.0f;

			f32 referenceScale = 1.0f;
		};

		SHALLTEAR_EDITOR_API bool HandleIsInMode(GizmoHandle handle, GizmoMode mode);

		SHALLTEAR_EDITOR_API Quat OrientationForSpace(GizmoSpace space, GizmoMode mode,
													  const Quat& targetWorldRotation);

		SHALLTEAR_EDITOR_API f32 SnapToIncrement(f32 value, f32 increment);
		SHALLTEAR_EDITOR_API Vec3 SnapChangedComponents(const Vec3& value, const Vec3& reference, f32 increment);

		SHALLTEAR_EDITOR_API f32 HandleScaleAt(const EditorViewportOps::Viewpoint& viewpoint, const Vec3& origin);
		SHALLTEAR_EDITOR_API f32 WorldRadiusForPixels(const EditorViewportOps::Viewpoint& viewpoint,
													  const Vec3& point, f32 viewportHeightPx, f32 pixels);

		SHALLTEAR_EDITOR_API void AppendGizmo(Vector<DebugLine>& out, const GizmoDesc& desc,
											  GizmoHandle hovered, GizmoHandle dragged);

		SHALLTEAR_EDITOR_API void AppendVertexMarker(Vector<DebugLine>& out, const Vec3& world,
													 const Quat& cameraRotation, f32 halfSize, Color32 color);

		SHALLTEAR_EDITOR_API GizmoHit HitTest(const GizmoDesc& desc, const Ray& ray);

		SHALLTEAR_EDITOR_API bool BeginDrag(const GizmoDesc& desc, GizmoHandle handle,
											const Ray& ray, DragState& out);
		SHALLTEAR_EDITOR_API bool UpdateDrag(const GizmoDesc& desc, DragState& state,
											 const Ray& ray, DragValue& outValue);

		SHALLTEAR_EDITOR_API Vec3 PivotFor(std::span<const Handle<SceneObject>> targets);

		struct DragSnapshot {
			Handle<SceneObject> object;
			Vec3 localPosition;
			Quat localRotation = Quat::Identity();
			Vec3 localScale{1.0f, 1.0f, 1.0f};
			Vec3 worldPosition;
			Quat worldRotation = Quat::Identity();
			Mat4 parentToWorld = Mat4::Identity();
			Mat4 worldToParent = Mat4::Identity();
			Quat parentWorldRotation = Quat::Identity();
		};
		SHALLTEAR_EDITOR_API void SnapshotTargets(std::span<const Handle<SceneObject>> targets,
												  Vector<DragSnapshot>& out);

		struct SnapSettings {
			f32 translate = 0.0f;
			f32 rotateDeg = 0.0f;
			f32 scale = 0.0f;
		};

		struct DragOutcome {
			Vec3 localPosition;
			Quat localRotation = Quat::Identity();
			Vec3 localScale{1.0f, 1.0f, 1.0f};
			bool positionChanged = false;
			bool rotationChanged = false;
			bool scaleChanged = false;
		};

		SHALLTEAR_EDITOR_API void SolveDragTargets(std::span<const DragSnapshot> starts, GizmoMode mode,
												   const DragState& drag, const DragValue& value, const Vec3& pivot,
												   const Quat& gizmoOrientation, const SnapSettings& snap,
												   Vector<DragOutcome>& out);

		inline constexpr f32 RADIUS_AXIS_MIN_DEGREES = 5.0f;
		inline constexpr f32 RADIUS_DOT_HALF = 0.03f;

		struct RadiusDot {
			Vec3 position;
			Vec3 axis;
			bool front = true;
		};

		SHALLTEAR_EDITOR_API u32 RadiusDots(const EditorViewportOps::Viewpoint& viewpoint, const Vec3& center, const Quat& rotation,
											f32 radius, RadiusDot (&out)[6]);

		SHALLTEAR_EDITOR_API i32 HitRadiusDots(std::span<const RadiusDot> dots, const EditorViewportOps::Viewpoint& viewpoint,
											   f32 viewportHeightPx, const Ray& ray, f32& outRayT);

		SHALLTEAR_EDITOR_API void AppendRadiusDots(Vector<DebugLine>& out, std::span<const RadiusDot> dots,
												   const EditorViewportOps::Viewpoint& viewpoint, Color32 color, i32 highlighted);

		struct RadiusDrag {
			Vec3 center;
			Vec3 axis;
			Vec3 planeNormal;
			f32 startRadius = 0.0f;
			f32 grabAlong = 0.0f;
		};
		SHALLTEAR_EDITOR_API bool BeginRadiusDrag(const EditorViewportOps::Viewpoint& viewpoint, const Vec3& center, const RadiusDot& dot,
												  f32 radius, const Ray& ray, RadiusDrag& out);
		SHALLTEAR_EDITOR_API bool UpdateRadiusDrag(const RadiusDrag& drag, const Ray& ray, f32& outRadius);

		SHALLTEAR_EDITOR_API bool SceneHandleMode(SceneHandleKind kind, GizmoMode tool, GizmoMode& out);
		SHALLTEAR_EDITOR_API Vec3 SceneHandleOrigin(const SceneHandle& handle);
		SHALLTEAR_EDITOR_API Quat SceneHandleRotation(const SceneHandle& handle);
		SHALLTEAR_EDITOR_API SceneHandle SolveSceneHandleDrag(const SceneHandle& start, GizmoMode mode, const DragState& drag,
															  const DragValue& value, const Quat& gizmoOrientation, const SnapSettings& snap);

		SHALLTEAR_EDITOR_API RadiusDot SliderDot(const SceneHandle& handle);
		SHALLTEAR_EDITOR_API bool BeginSliderDrag(const EditorViewportOps::Viewpoint& viewpoint, const SceneHandle& handle, const Ray& ray,
												  RadiusDrag& out);
		SHALLTEAR_EDITOR_API bool UpdateSliderDrag(const RadiusDrag& drag, const SceneHandle& start, const Ray& ray, f32& outDistance);

	} // namespace EditorGizmoOps

} // namespace Shalltear
