#include "Shalltear/Editor/EditorViewportOps.h"

#include "Shalltear/Core/Graphics/Camera.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Reflection.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Scene/SceneSystem.h"

#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"

#include <algorithm>
#include <cmath>
#include <limits>

namespace Shalltear::EditorViewportOps {

	namespace {

		struct PickContext {
			Ray ray;
			Handle<SceneObject> best;
			f32 bestDistance = 0.0f;
			bool hit = false;
		};

		MeshAsset* MeshUnderRay(SceneObject& object, const Ray& ray, f32& outEnter) {
			if (!object.IsActiveInHierarchy()) {
				return nullptr;
			}
			MeshRenderer* renderer = object.GetComponent<MeshRenderer>().Get();
			if (!renderer) {
				return nullptr;
			}
			MeshAsset* mesh = renderer->GetMesh().Get();
			if (!mesh || !mesh->bounds.IsValid()) {
				return nullptr;
			}
			if (!mesh->bounds.Transformed(object.GetWorldMatrix()).Raycast(ray, outEnter)) {
				return nullptr;
			}
			return mesh;
		}

		bool RayHitsTriangle(const Ray& ray, const Vec3& a, const Vec3& b, const Vec3& c, f32& outT) {
			constexpr f32 PARALLEL_EPSILON = 1.0e-8f;
			const Vec3 edge1 = b - a;
			const Vec3 edge2 = c - a;
			const Vec3 across = Vec3::Cross(ray.direction, edge2);
			const f32 determinant = Vec3::Dot(edge1, across);
			if (Math::Abs(determinant) < PARALLEL_EPSILON) {
				return false;
			}
			const f32 inverse = 1.0f / determinant;
			const Vec3 toA = ray.origin - a;
			const f32 u = Vec3::Dot(toA, across) * inverse;
			if (u < 0.0f || u > 1.0f) {
				return false;
			}
			const Vec3 alongEdge = Vec3::Cross(toA, edge1);
			const f32 v = Vec3::Dot(ray.direction, alongEdge) * inverse;
			if (v < 0.0f || u + v > 1.0f) {
				return false;
			}
			const f32 t = Vec3::Dot(edge2, alongEdge) * inverse;
			if (t < 0.0f) {
				return false;
			}
			outT = t;
			return true;
		}

		bool NearestTriangleDistance(const MeshAsset& mesh, const Ray& localRay, f32& outT) {
			const size_t vertexCount = mesh.vertices.size();
			f32 best = 0.0f;
			bool found = false;
			for (const SubMesh& submesh : mesh.submeshes) {
				f32 enter = 0.0f;
				if (submesh.bounds.IsValid()) {
					if (!submesh.bounds.Raycast(localRay, enter)) {
						continue;
					}
					if (found && enter >= best) {
						continue;
					}
				}
				const size_t end = static_cast<size_t>(submesh.firstIndex) + submesh.indexCount;
				if (end > mesh.indices.size()) {
					continue;
				}
				for (size_t i = submesh.firstIndex; i + 2 < end; i += 3) {
					const u32 ia = mesh.indices[i];
					const u32 ib = mesh.indices[i + 1];
					const u32 ic = mesh.indices[i + 2];
					if (ia >= vertexCount || ib >= vertexCount || ic >= vertexCount) {
						continue;
					}
					f32 t = 0.0f;
					if (!RayHitsTriangle(localRay, mesh.vertices[ia].position, mesh.vertices[ib].position,
										 mesh.vertices[ic].position, t)) {
						continue;
					}
					if (!found || t < best) {
						found = true;
						best = t;
					}
				}
			}
			outT = best;
			return found;
		}

		bool NearestSurfaceDistance(const MeshAsset& mesh, const Mat4& toWorld, const Ray& ray, f32& outDistance) {
			Mat4 toLocal = Mat4::Identity();
			if (!toWorld.TryInvert(toLocal)) {
				return false;
			}
			const Vec3 localDirection = toLocal.TransformVector(ray.direction);
			if (localDirection.LengthSquared() <= Math::EPSILON * Math::EPSILON) {
				return false;
			}
			// Ray normalizes, t is in local units
			const Ray localRay(toLocal.TransformPoint(ray.origin), localDirection);
			f32 localT = 0.0f;
			if (!NearestTriangleDistance(mesh, localRay, localT)) {
				return false;
			}
			const Vec3 world = toWorld.TransformPoint(localRay.GetPoint(localT));
			outDistance = Math::Max(0.0f, Vec3::Dot(world - ray.origin, ray.direction));
			return true;
		}

		void PickVisitor(SceneObject& object, void* user) {
			PickContext& pick = *static_cast<PickContext*>(user);
			f32 enter = 0.0f;
			MeshAsset* mesh = MeshUnderRay(object, pick.ray, enter);
			if (!mesh) {
				return;
			}
			if (pick.hit && enter >= pick.bestDistance) {
				return;
			}
			f32 distance = 0.0f;
			if (!NearestSurfaceDistance(*mesh, object.GetWorldMatrix(), pick.ray, distance)) {
				return;
			}
			if (!pick.hit || distance < pick.bestDistance) {
				pick.hit = true;
				pick.bestDistance = distance;
				pick.best = Handle<SceneObject>(&object);
			}
		}

		struct CameraSearch {
			Camera* best = nullptr;
		};

		void CameraVisitor(SceneObject& object, void* user) {
			CameraSearch& search = *static_cast<CameraSearch*>(user);
			if (!object.IsActiveInHierarchy()) {
				return;
			}
			Camera* camera = object.GetComponent<Camera>().Get();
			if (!camera || !camera->GetOwner().IsValid()) {
				return;
			}
			if (!search.best || camera->GetPriority() > search.best->GetPriority()) {
				search.best = camera;
			}
		}

		bool HasSelectedAncestor(Handle<SceneObject> object) {
			SceneObject* raw = object.Get();
			if (!raw) {
				return false;
			}
			for (SceneObject* parent = raw->GetParent().Get(); parent; parent = parent->GetParent().Get()) {
				for (Handle<Object> selected : EditorSelection::GetSelection()) {
					if (ResolveSceneObject(selected).Get() == parent) {
						return true;
					}
				}
			}
			return false;
		}

		constexpr f32 VERTEX_TIE_PX = 1.0f;

		struct VertexSearch {
			Mat4 viewProjection = Mat4::Identity();
			Vec2 sizePx;
			Vec2 cursorPx;

			Mat4 bestToWorld = Mat4::Identity();
			Vec3 bestLocal;
			f32 nearestPx = std::numeric_limits<f32>::max();
			f32 bestDepth = 0.0f;
			bool found = false;
		};

		void ConsiderVertices(SceneObject& object, VertexSearch& search) {
			MeshRenderer* renderer = object.GetComponent<MeshRenderer>().Get();
			if (!renderer) {
				return;
			}
			MeshAsset* mesh = renderer->GetMesh().Get();
			if (!mesh || mesh->vertices.empty()) {
				return;
			}
			const Mat4 toWorld = object.GetWorldMatrix();
			const Mat4 toClip = search.viewProjection * toWorld;
			for (const MeshVertex& vertex : mesh->vertices) {
				Vec2 px;
				f32 depth = 0.0f;
				if (!ProjectToViewport(toClip, vertex.position, search.sizePx, px, depth)) {
					continue;
				}
				const f32 distance = (px - search.cursorPx).Length();
				const bool accept = !search.found || distance < search.nearestPx - VERTEX_TIE_PX ||
									(distance <= search.nearestPx + VERTEX_TIE_PX && depth > search.bestDepth);
				if (accept) {
					search.bestToWorld = toWorld;
					search.bestLocal = vertex.position;
					search.bestDepth = depth;
					search.found = true;
				}
				search.nearestPx = Math::Min(search.nearestPx, distance);
			}
		}

		void ConsiderSubtree(SceneObject& object, VertexSearch& search) {
			if (object.IsActiveInHierarchy()) {
				ConsiderVertices(object, search);
			}
			for (u32 index = 0; index < object.GetChildCount(); ++index) {
				if (SceneObject* child = object.GetChild(index).Get()) {
					ConsiderSubtree(*child, search);
				}
			}
		}

		bool IsInOrUnder(SceneObject& object, std::span<const Handle<SceneObject>> roots) {
			for (SceneObject* walk = &object; walk; walk = walk->GetParent().Get()) {
				for (const Handle<SceneObject>& root : roots) {
					if (root.Get() == walk) {
						return true;
					}
				}
			}
			return false;
		}

		struct VertexUnderCursor {
			VertexSearch search;
			Ray ray;
			std::span<const Handle<SceneObject>> excluded;
		};

		void VertexVisitor(SceneObject& object, void* user) {
			VertexUnderCursor& query = *static_cast<VertexUnderCursor*>(user);
			if (IsInOrUnder(object, query.excluded)) {
				return;
			}
			f32 enter = 0.0f;
			if (!MeshUnderRay(object, query.ray, enter)) {
				return;
			}
			ConsiderVertices(object, query.search);
		}

		bool VertexResult(const VertexSearch& search, Vec3& outWorld) {
			if (!search.found) {
				return false;
			}
			outWorld = search.bestToWorld.TransformPoint(search.bestLocal);
			return true;
		}

	} // anonymous namespace

	Quat BuildCameraRotation(f32 yawDeg, f32 pitchDeg) {
		return Quat::FromEuler(pitchDeg, yawDeg, 0.0f);
	}

	Mat4 BuildViewMatrix(const Vec3& position, const Quat& rotation) {
		return Mat4::TRS(position, rotation, Vec3::One()).Inverse();
	}

	Mat4 BuildViewMatrix(const Viewpoint& viewpoint) {
		return BuildViewMatrix(viewpoint.position, viewpoint.rotation);
	}

	Mat4 BuildProjection(const Viewpoint& viewpoint) {
		const f32 aspect = viewpoint.aspect <= 0.0f ? 1.0f : viewpoint.aspect;
		if (!viewpoint.orthographic) {
			return Mat4::PerspectiveReverseZ(EDITOR_FOV_DEG, aspect, EDITOR_NEAR_PLANE, EDITOR_FAR_PLANE);
		}
		const f32 halfHeight = Math::Clamp(viewpoint.orthographicSize, MIN_ORTHOGRAPHIC_SIZE, MAX_ORTHOGRAPHIC_SIZE);
		const f32 halfWidth = halfHeight * aspect;
		return Mat4::OrthographicReverseZ(-halfWidth, halfWidth, -halfHeight, halfHeight,
										  -EDITOR_FAR_PLANE, EDITOR_FAR_PLANE);
	}

	Mat4 BuildProjection(f32 aspect) {
		Viewpoint viewpoint;
		viewpoint.aspect = aspect;
		return BuildProjection(viewpoint);
	}

	f32 ViewHeightAt(const Viewpoint& viewpoint, const Vec3& point) {
		if (viewpoint.orthographic) {
			return 2.0f * Math::Clamp(viewpoint.orthographicSize, MIN_ORTHOGRAPHIC_SIZE, MAX_ORTHOGRAPHIC_SIZE);
		}
		const Vec3 forward = viewpoint.rotation * Vec3::Forward();
		const f32 depth = Math::Max(Vec3::Dot(point - viewpoint.position, forward), 1.0e-3f);
		return 2.0f * std::tan(EDITOR_FOV_DEG * 0.5f * Math::DEG_TO_RAD) * depth;
	}

	GridPlane GridPlaneFor(const Mat4& view, const Mat4& projection) {
		if (Math::Abs(projection(3, 2)) > 1.0e-6f) {
			return GridPlane::XZ;
		}
		// view matrix row 3 = camera forward in world space
		const Vec3 forward(view(2, 0), view(2, 1), view(2, 2));
		const f32 length = forward.Length();
		if (length < 1.0e-6f) {
			return GridPlane::XZ;
		}
		const Vec3 axis = forward / length;
		if (Math::Abs(axis.y) >= GRID_PLANE_AXIS_COS) {
			return GridPlane::XZ;
		}
		if (Math::Abs(axis.z) >= GRID_PLANE_AXIS_COS) {
			return GridPlane::XY;
		}
		if (Math::Abs(axis.x) >= GRID_PLANE_AXIS_COS) {
			return GridPlane::ZY;
		}
		return GridPlane::XZ;
	}

	GridBasis GridBasisFor(GridPlane plane) {
		GridBasis out;
		u32 rows[3] = {0, 1, 2};
		switch (plane) {
		case GridPlane::XY:
			rows[0] = 1;
			rows[1] = 2;
			rows[2] = 0;
			break;
		case GridPlane::ZY:
			rows[0] = 2;
			rows[1] = 0;
			rows[2] = 1;
			break;
		case GridPlane::XZ:
		default:
			break;
		}
		out.gridFromWorld = Mat4::Identity();
		for (u32 row = 0; row < 3; ++row) {
			for (u32 column = 0; column < 3; ++column) {
				out.gridFromWorld(row, column) = rows[row] == column ? 1.0f : 0.0f;
			}
		}
		out.alongX = rows[0];
		out.alongZ = rows[2];
		return out;
	}

	Vec3 ViewAxisAt(const Viewpoint& viewpoint, const Vec3& point) {
		const Vec3 forward = viewpoint.rotation * Vec3::Forward();
		if (viewpoint.orthographic) {
			return -forward;
		}
		const Vec3 toCamera = viewpoint.position - point;
		if (toCamera.LengthSquared() < 1.0e-8f) {
			return -forward;
		}
		return toCamera.Normalized();
	}

	Ray BuildViewportRay(const Mat4& view, const Mat4& projection, const Vec2& localPx, const Vec2& sizePx) {
		Mat4 viewProjection = projection * view;
		if (sizePx.x <= 0.0f || sizePx.y <= 0.0f) {
			Mat4 inverseView = view.Inverse();
			return Ray(inverseView.TransformPoint(Vec3::Zero()), inverseView.TransformVector(Vec3::Forward()));
		}
		f32 clipX = (localPx.x / sizePx.x) * 2.0f - 1.0f;
		f32 clipY = 1.0f - (localPx.y / sizePx.y) * 2.0f;

		Mat4 inverse = viewProjection.Inverse();
		Vec3 nearPoint = inverse.ProjectPoint(Vec3(clipX, clipY, 1.0f));
		Vec3 farPoint = inverse.ProjectPoint(Vec3(clipX, clipY, 0.0f));
		return Ray(nearPoint, farPoint - nearPoint);
	}

	bool ProjectToViewport(const Mat4& viewProjection, const Vec3& world, const Vec2& sizePx, Vec2& outLocalPx) {
		f32 depth = 0.0f;
		return ProjectToViewport(viewProjection, world, sizePx, outLocalPx, depth);
	}

	bool ProjectToViewport(const Mat4& viewProjection, const Vec3& world, const Vec2& sizePx,
						   Vec2& outLocalPx, f32& outDepth) {
		Vec4 clip = viewProjection * Vec4(world, 1.0f);
		if (clip.w <= 1.0e-5f) {
			return false;
		}
		Vec3 ndc = clip.XYZ() / clip.w;
		if (ndc.z < 0.0f || ndc.z > 1.0f) {
			return false;
		}
		outLocalPx = {(ndc.x * 0.5f + 0.5f) * sizePx.x, (0.5f - ndc.y * 0.5f) * sizePx.y};
		outDepth = ndc.z;
		return true;
	}

	namespace {

		void CastAtScene(PickContext& pick) {
			Vector<Handle<Scene>> scenes{GetMemoryResource(MemTag::Editor)};
			EditorSceneOps::GetStageScenes(scenes);
			for (const Handle<Scene>& sceneHandle : scenes) {
				if (Scene* scene = sceneHandle.Get()) {
					scene->ForEachObject(&PickVisitor, &pick);
				}
			}
		}

	} // anonymous namespace

	Handle<SceneObject> PickObject(const Ray& ray) {
		PickContext pick;
		pick.ray = ray;
		CastAtScene(pick);
		return pick.best;
	}

	bool FindPlacementPoint(const Ray& ray, Vec3& outWorld) {
		PickContext pick;
		pick.ray = ray;
		CastAtScene(pick);
		if (pick.hit) {
			outWorld = ray.GetPoint(pick.bestDistance);
			return true;
		}
		constexpr f32 PARALLEL_EPSILON = 1e-6f;
		if (std::abs(ray.direction.y) < PARALLEL_EPSILON) {
			return false;
		}
		f32 distance = -ray.origin.y / ray.direction.y;
		if (distance <= 0.0f) {
			return false;
		}
		outWorld = ray.GetPoint(distance);
		return true;
	}

	bool FindNearestVertexIn(std::span<const Handle<SceneObject>> roots, const Mat4& viewProjection,
							 const Vec2& sizePx, const Vec2& cursorPx, Vec3& outWorld) {
		VertexSearch search;
		search.viewProjection = viewProjection;
		search.sizePx = sizePx;
		search.cursorPx = cursorPx;
		for (const Handle<SceneObject>& root : roots) {
			if (SceneObject* object = root.Get()) {
				ConsiderSubtree(*object, search);
			}
		}
		return VertexResult(search, outWorld);
	}

	bool FindNearestVertexUnderCursor(std::span<const Handle<SceneObject>> excluded, const Ray& ray,
									  const Mat4& viewProjection, const Vec2& sizePx, const Vec2& cursorPx,
									  Vec3& outWorld) {
		VertexUnderCursor query;
		query.ray = ray;
		query.excluded = excluded;
		query.search.viewProjection = viewProjection;
		query.search.sizePx = sizePx;
		query.search.cursorPx = cursorPx;
		Vector<Handle<Scene>> scenes{GetMemoryResource(MemTag::Editor)};
		EditorSceneOps::GetStageScenes(scenes);
		for (const Handle<Scene>& sceneHandle : scenes) {
			if (Scene* scene = sceneHandle.Get()) {
				scene->ForEachObject(&VertexVisitor, &query);
			}
		}
		return VertexResult(query.search, outWorld);
	}

	AABB GetObjectWorldBounds(Handle<SceneObject> object) {
		SceneObject* resolved = object.Get();
		if (!resolved) {
			return AABB::Empty();
		}
		if (MeshRenderer* renderer = resolved->GetComponent<MeshRenderer>().Get()) {
			if (MeshAsset* mesh = renderer->GetMesh().Get()) {
				if (mesh->bounds.IsValid()) {
					return mesh->bounds.Transformed(resolved->GetWorldMatrix());
				}
			}
		}
		return AABB::FromCenterExtents(resolved->GetWorldPosition(), Vec3(0.5f, 0.5f, 0.5f));
	}

	AABB GetGraphWorldBounds(Handle<SceneObject> root) {
		AABB bounds = AABB::Empty();
		SceneObject* resolved = root.Get();
		if (!resolved) {
			return bounds;
		}
		if (resolved->GetComponent<MeshRenderer>().IsValid()) {
			bounds.Encapsulate(GetObjectWorldBounds(root));
		}
		for (u32 i = 0; i < resolved->GetChildCount(); ++i) {
			bounds.Encapsulate(GetGraphWorldBounds(resolved->GetChild(i)));
		}
		return bounds;
	}

	AABB GetSelectionWorldBounds() {
		AABB bounds = AABB::Empty();
		for (Handle<Object> selected : EditorSelection::GetSelection()) {
			Handle<SceneObject> object = ResolveSceneObject(selected);
			if (object.IsValid()) {
				bounds.Encapsulate(GetObjectWorldBounds(object));
			}
		}
		return bounds;
	}

	namespace {

		constexpr f32 FRAME_AIR = 1.4f;

		f32 FrameRadiusFor(const AABB& bounds) {
			return Math::Max(bounds.Extents().Length(), 0.25f);
		}

	} // anonymous namespace

	f32 FrameDistanceFor(const AABB& bounds) {
		return FrameRadiusFor(bounds) / std::tan(EDITOR_FOV_DEG * 0.5f * Math::DEG_TO_RAD) * FRAME_AIR;
	}

	f32 FrameSizeFor(const AABB& bounds) {
		return Math::Clamp(FrameRadiusFor(bounds) * FRAME_AIR, MIN_ORTHOGRAPHIC_SIZE, MAX_ORTHOGRAPHIC_SIZE);
	}

	Vec3 FramePositionFor(const AABB& bounds, const Quat& rotation) {
		if (!bounds.IsValid()) {
			return Vec3::Zero();
		}
		return bounds.Center() - (rotation * Vec3::Forward()) * FrameDistanceFor(bounds);
	}

	bool FollowPositionFor(Handle<SceneObject> object, const Quat& rotation, f32 distance, Vec3& outPosition) {
		SceneObject* resolved = object.Get();
		if (!resolved || resolved->IsPendingDestroy()) {
			return false;
		}
		AABB bounds = GetObjectWorldBounds(object);
		if (!bounds.IsValid()) {
			return false;
		}
		outPosition = bounds.Center() - (rotation * Vec3::Forward()) * distance;
		return true;
	}

	void LayOutOrientationKnobs(const Quat& rotation, const Vec2& centerPx, f32 radiusPx,
								std::span<OrientationKnob, ORIENTATION_KNOB_COUNT> out) {
		static constexpr Vec3 AXES[ORIENTATION_KNOB_COUNT] = {
			Vec3(1.0f, 0.0f, 0.0f), Vec3(-1.0f, 0.0f, 0.0f), Vec3(0.0f, 1.0f, 0.0f),
			Vec3(0.0f, -1.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f), Vec3(0.0f, 0.0f, -1.0f)};
		const Quat toView = rotation.Inverse();
		for (u32 i = 0; i < ORIENTATION_KNOB_COUNT; ++i) {
			const Vec3 view = toView * AXES[i];
			out[i].axis = AXES[i];
			out[i].centerPx = Vec2(centerPx.x + view.x * radiusPx, centerPx.y - view.y * radiusPx);
			out[i].depth = view.z;
		}
	}

	u32 HitOrientationKnob(std::span<const OrientationKnob> knobs, const Vec2& localPx, f32 knobRadiusPx) {
		u32 best = ORIENTATION_KNOB_NONE;
		for (u32 i = 0; i < knobs.size(); ++i) {
			if ((knobs[i].centerPx - localPx).Length() > knobRadiusPx) {
				continue;
			}
			if (best == ORIENTATION_KNOB_NONE || knobs[i].depth < knobs[best].depth) {
				best = i;
			}
		}
		return best;
	}

	void ViewAnglesFromAxis(const Vec3& axis, f32 currentYawDeg, f32& outYawDeg, f32& outPitchDeg) {
		const Vec3 forward = Vec3(-axis.x, -axis.y, -axis.z).Normalized();
		outPitchDeg = -std::asin(Math::Clamp(forward.y, -1.0f, 1.0f)) * Math::RAD_TO_DEG;
		const f32 level = std::sqrt(forward.x * forward.x + forward.z * forward.z);
		outYawDeg = level > 1.0e-4f ? std::atan2(forward.x, forward.z) * Math::RAD_TO_DEG : currentYawDeg;
	}

	Handle<Camera> FindActiveSceneCamera() {
		CameraSearch search;
		Vector<Handle<Scene>> scenes{GetMemoryResource(MemTag::Editor)};
		EditorSceneOps::GetStageScenes(scenes);
		for (const Handle<Scene>& sceneHandle : scenes) {
			if (Scene* scene = sceneHandle.Get()) {
				scene->ForEachObject(&CameraVisitor, &search);
			}
		}
		return search.best ? Handle<Camera>(search.best) : Handle<Camera>{};
	}

	namespace {

		// saved layout stores the idx
		constexpr ShadingModeInfo SHADING_MODES[] = {
			{"Shaded", ViewFill::Solid, DebugView::None},
			{"Wireframe", ViewFill::Wireframe, DebugView::None},
			{"Shaded Wireframe", ViewFill::SolidAndWireframe, DebugView::None},
			{"World Normals", ViewFill::Solid, DebugView::Normals},
			{"Depth", ViewFill::Solid, DebugView::Depth},
			{"Shadow Cascades", ViewFill::Solid, DebugView::Cascades},
		};

	} // namespace

	u32 GetShadingModeCount() { return static_cast<u32>(std::size(SHADING_MODES)); }

	ShadingModeInfo GetShadingMode(i32 index) {
		if (index < 0 || index >= static_cast<i32>(std::size(SHADING_MODES))) {
			return SHADING_MODES[0];
		}
		return SHADING_MODES[index];
	}

	void CollectCameraPassRows(const Handle<Camera>& camera, Vector<Handle<RenderPass>>& out) {
		out.clear();
		Camera* resolved = camera.Get();
		if (!resolved) {
			return;
		}
		const u32 count = resolved->GetPassCount();
		for (u32 slot = 0; slot < count; ++slot) {
			if (Handle<RenderPass> row = resolved->GetPass(slot); row.IsValid()) {
				out.push_back(row);
			}
		}
	}

	Handle<SceneObject> ResolveSceneObject(Handle<Object> selected) {
		Object* raw = selected.Get();
		if (!raw) {
			return {};
		}
		const TypeInfo* type = raw->GetType();
		if (!type) {
			return {};
		}
		if (TypeRegistry::IsDerivedFrom(type, SceneObject::GetReflectType())) {
			return Handle<SceneObject>(static_cast<SceneObject*>(raw));
		}
		if (TypeRegistry::IsDerivedFrom(type, Component::GetReflectType())) {
			return static_cast<Component*>(raw)->GetOwner();
		}
		return {};
	}

	void GatherDragTargets(Vector<Handle<SceneObject>>& out) {
		out.clear();
		for (Handle<Object> selected : EditorSelection::GetSelection()) {
			Handle<SceneObject> object = ResolveSceneObject(selected);
			if (!object.IsValid()) {
				continue;
			}
			if (std::find(out.begin(), out.end(), object) == out.end()) {
				out.push_back(object);
			}
		}

		size_t kept = 0;
		for (size_t i = 0; i < out.size(); ++i) {
			if (!HasSelectedAncestor(out[i])) {
				out[kept++] = out[i];
			}
		}
		out.resize(kept);

		Handle<SceneObject> active = ResolveSceneObject(EditorSelection::GetActive());
		if (!active.IsValid()) {
			return;
		}
		for (size_t i = 1; i < out.size(); ++i) {
			if (out[i] == active) {
				for (size_t j = i; j > 0; --j) {
					out[j] = out[j - 1];
				}
				out[0] = active;
				break;
			}
		}
	}

	void BuildSelectionOutlines(std::span<const Handle<Object>> selection, Color32 selected, Color32 children,
								Vector<ViewOutline>& out) {
		out.clear();
		for (const Handle<Object>& entry : selection) {
			Handle<SceneObject> object = ResolveSceneObject(entry);
			if (object.IsValid()) {
				out.push_back({object, selected});
			}
		}
		const size_t selectedCount = out.size();
		Vector<Handle<SceneObject>> pending{GetMemoryResource(MemTag::Editor)};
		for (size_t index = 0; index < selectedCount; ++index) {
			pending.push_back(out[index].object);
			while (!pending.empty()) {
				const SceneObject* parent = pending.back().Get();
				pending.pop_back();
				if (!parent) {
					continue;
				}
				for (u32 child = 0; child < parent->GetChildCount(); ++child) {
					Handle<SceneObject> next = parent->GetChild(child);
					out.push_back({next, children});
					pending.push_back(next);
				}
			}
		}
	}

	Vec2I ViewTargetSizeFor(const UI::Rect& elementRect) {
		const UI::Rect painted = UI::SnapRectToPixels(elementRect);
		if (painted.w <= 0.0f || painted.h <= 0.0f) {
			return {};
		}
		return {static_cast<i32>(painted.w), static_cast<i32>(painted.h)};
	}

	bool ViewportIsOnScreen(const UI::Rect& elementRect) {
		const Vec2I size = ViewTargetSizeFor(elementRect);
		return size.x > 0 && size.y > 0;
	}

	void AppendBoxWireframe(Vector<DebugLine>& out, const AABB& box, Color32 color) {
		if (!box.IsValid()) {
			return;
		}
		const Vec3 corners[8] = {
			{box.min.x, box.min.y, box.min.z}, {box.max.x, box.min.y, box.min.z}, {box.max.x, box.max.y, box.min.z}, {box.min.x, box.max.y, box.min.z}, {box.min.x, box.min.y, box.max.z}, {box.max.x, box.min.y, box.max.z}, {box.max.x, box.max.y, box.max.z}, {box.min.x, box.max.y, box.max.z}};
		const u32 edges[12][2] = {
			{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
		for (const auto& edge : edges) {
			out.push_back({corners[edge[0]], corners[edge[1]], color});
		}
	}

} // namespace Shalltear::EditorViewportOps
