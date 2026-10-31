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
#include "Shalltear/Core/UI/UITypes.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <span>

namespace Shalltear {

	class Camera;
	class Object;
	class SceneObject;

	namespace EditorViewportOps {

		inline constexpr f32 EDITOR_FOV_DEG = 60.0f;
		inline constexpr f32 EDITOR_NEAR_PLANE = 0.05f;
		inline constexpr f32 EDITOR_FAR_PLANE = 5000.0f;
		// half height
		inline constexpr f32 EDITOR_ORTHOGRAPHIC_SIZE = 5.0f;
		inline constexpr f32 MIN_ORTHOGRAPHIC_SIZE = 0.01f;
		inline constexpr f32 MAX_ORTHOGRAPHIC_SIZE = 5000.0f;

		struct Viewpoint {
			Vec3 position;
			Quat rotation = Quat::Identity();
			f32 aspect = 1.0f;
			bool orthographic = false;
			f32 orthographicSize = EDITOR_ORTHOGRAPHIC_SIZE;
		};

		SHALLTEAR_EDITOR_API Quat BuildCameraRotation(f32 yawDeg, f32 pitchDeg);
		SHALLTEAR_EDITOR_API Mat4 BuildViewMatrix(const Vec3& position, const Quat& rotation);
		SHALLTEAR_EDITOR_API Mat4 BuildViewMatrix(const Viewpoint& viewpoint);
		SHALLTEAR_EDITOR_API Mat4 BuildProjection(const Viewpoint& viewpoint);
		SHALLTEAR_EDITOR_API Mat4 BuildProjection(f32 aspect);

		SHALLTEAR_EDITOR_API f32 ViewHeightAt(const Viewpoint& viewpoint, const Vec3& point);
		SHALLTEAR_EDITOR_API Vec3 ViewAxisAt(const Viewpoint& viewpoint, const Vec3& point);

		enum class GridPlane : u8 { XZ,
									XY,
									ZY };

		SHALLTEAR_EDITOR_API GridPlane GridPlaneFor(const Mat4& view, const Mat4& projection);
		inline constexpr f32 GRID_PLANE_AXIS_COS = 0.999f;

		// gridFromWorld is rigid, grid y = plane normal
		struct GridBasis {
			Mat4 gridFromWorld = Mat4::Identity();
			u32 alongX = 0;
			u32 alongZ = 2;
		};
		SHALLTEAR_EDITOR_API GridBasis GridBasisFor(GridPlane plane);

		// sizePx must be the viewport's px size
		SHALLTEAR_EDITOR_API Ray BuildViewportRay(const Mat4& view, const Mat4& projection,
												  const Vec2& localPx, const Vec2& sizePx);
		SHALLTEAR_EDITOR_API bool ProjectToViewport(const Mat4& viewProjection, const Vec3& world,
													const Vec2& sizePx, Vec2& outLocalPx);
		SHALLTEAR_EDITOR_API bool ProjectToViewport(const Mat4& viewProjection, const Vec3& world,
													const Vec2& sizePx, Vec2& outLocalPx, f32& outDepth);

		SHALLTEAR_EDITOR_API Handle<SceneObject> PickObject(const Ray& ray);

		SHALLTEAR_EDITOR_API bool FindPlacementPoint(const Ray& ray, Vec3& outWorld);

		SHALLTEAR_EDITOR_API bool FindNearestVertexIn(std::span<const Handle<SceneObject>> roots,
													  const Mat4& viewProjection, const Vec2& sizePx,
													  const Vec2& cursorPx, Vec3& outWorld);

		SHALLTEAR_EDITOR_API bool FindNearestVertexUnderCursor(std::span<const Handle<SceneObject>> excluded,
															   const Ray& ray, const Mat4& viewProjection,
															   const Vec2& sizePx, const Vec2& cursorPx,
															   Vec3& outWorld);

		SHALLTEAR_EDITOR_API AABB GetObjectWorldBounds(Handle<SceneObject> object);
		SHALLTEAR_EDITOR_API AABB GetGraphWorldBounds(Handle<SceneObject> root);
		SHALLTEAR_EDITOR_API AABB GetSelectionWorldBounds();
		SHALLTEAR_EDITOR_API Vec3 FramePositionFor(const AABB& bounds, const Quat& rotation);
		SHALLTEAR_EDITOR_API f32 FrameDistanceFor(const AABB& bounds);
		SHALLTEAR_EDITOR_API f32 FrameSizeFor(const AABB& bounds);
		SHALLTEAR_EDITOR_API bool FollowPositionFor(Handle<SceneObject> object, const Quat& rotation, f32 distance,
													Vec3& outPosition);

		struct OrientationKnob {
			Vec3 axis;
			Vec2 centerPx;
			f32 depth = 0.0f;
		};
		inline constexpr u32 ORIENTATION_KNOB_COUNT = 6;
		inline constexpr u32 ORIENTATION_KNOB_NONE = ~0u;
		inline constexpr f32 ORIENTATION_ARM_FRACTION = 0.34f;
		inline constexpr f32 ORIENTATION_KNOB_FRACTION = 0.10f;
		inline constexpr f32 ORIENTATION_CUBE_FRACTION = 0.11f;
		SHALLTEAR_EDITOR_API void LayOutOrientationKnobs(const Quat& rotation, const Vec2& centerPx, f32 radiusPx,
														 std::span<OrientationKnob, ORIENTATION_KNOB_COUNT> out);
		SHALLTEAR_EDITOR_API u32 HitOrientationKnob(std::span<const OrientationKnob> knobs, const Vec2& localPx,
													f32 knobRadiusPx);
		SHALLTEAR_EDITOR_API void ViewAnglesFromAxis(const Vec3& axis, f32 currentYawDeg, f32& outYawDeg,
													 f32& outPitchDeg);

		SHALLTEAR_EDITOR_API Handle<Camera> FindActiveSceneCamera();

		SHALLTEAR_EDITOR_API void CollectCameraPassRows(const Handle<Camera>& camera,
														Vector<Handle<RenderPass>>& out);

		struct ShadingModeInfo {
			std::string_view label;
			ViewFill fill = ViewFill::Solid;
			DebugView debugView = DebugView::None;
		};

		SHALLTEAR_EDITOR_API u32 GetShadingModeCount();

		SHALLTEAR_EDITOR_API ShadingModeInfo GetShadingMode(i32 index);

		SHALLTEAR_EDITOR_API Handle<SceneObject> ResolveSceneObject(Handle<Object> selected);

		SHALLTEAR_EDITOR_API void GatherDragTargets(Vector<Handle<SceneObject>>& out);

		SHALLTEAR_EDITOR_API void BuildSelectionOutlines(std::span<const Handle<Object>> selection, Color32 selected, Color32 children,
														 Vector<ViewOutline>& out);

		SHALLTEAR_EDITOR_API Vec2I ViewTargetSizeFor(const UI::Rect& elementRect);

		SHALLTEAR_EDITOR_API bool ViewportIsOnScreen(const UI::Rect& elementRect);

		SHALLTEAR_EDITOR_API void AppendBoxWireframe(Vector<DebugLine>& out, const AABB& box, Color32 color);

	} // namespace EditorViewportOps

} // namespace Shalltear
