#include "Physics3DEditor.h"

#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Scene/SceneObject.h"

#include "Shalltear/Editor/EditorGizmos.h"
#include "Shalltear/Editor/EditorTheme.h"

#include "Shalltear/Physics3D/Colliders.h"

#include "PhysicsBackend.h"

namespace Shalltear::Physics3DEditor {

	namespace {

		Color32 ColliderWire() { return EditorTheme::GetColor32(EditorTheme::ColorToken::GizmoColliderWire); }

		struct GizmoScratch {
			Vector<Vec3> points{GetMemoryResource(MemTag::Editor)};
			Vector<i32> indices{GetMemoryResource(MemTag::Editor)};
		};

		GizmoScratch& Scratch() {
			static GizmoScratch scratch;
			return scratch;
		}

		bool DescribeForGizmo(const Collider& collider, PhysicsShapeRequest& request, Vec3& worldPosition, Quat& worldRotation) {
			SceneObject* owner = collider.GetOwner().Get();
			if (!owner) {
				return false;
			}
			GizmoScratch& scratch = Scratch();
			request.pointScratch = &scratch.points;
			request.indexScratch = &scratch.indices;
			if (!collider.DescribeShape(owner->GetLossyScale(), request)) {
				return false;
			}
			worldPosition = owner->GetWorldPosition();
			worldRotation = owner->GetWorldRotation();
			return true;
		}

		void DrawPrimitiveColliderGizmo(GizmoDrawContext& context, Handle<Component> component) {
			Collider* collider = static_cast<Collider*>(component.Get());
			if (!collider || !context.selected) {
				return;
			}
			PhysicsShapeRequest request;
			Vec3 objectPosition;
			Quat objectRotation;
			if (!DescribeForGizmo(*collider, request, objectPosition, objectRotation)) {
				return;
			}
			const Color32 color = ColliderWire();
			const Vec3 center = objectPosition + objectRotation * request.localPosition;
			const Quat rotation = objectRotation * request.localRotation;

			switch (request.kind) {
			case PhysicsShapeKind::Box:
				EditorGizmos::AppendOrientedBox(*context.lines, center, rotation, request.halfExtents, color);
				break;
			case PhysicsShapeKind::Sphere:
				EditorGizmos::AppendCircle(*context.lines, center, rotation * Vec3::Right(), request.radius, color);
				EditorGizmos::AppendCircle(*context.lines, center, rotation * Vec3::Up(), request.radius, color);
				EditorGizmos::AppendCircle(*context.lines, center, rotation * Vec3::Forward(), request.radius, color);
				break;
			case PhysicsShapeKind::Capsule:
				EditorGizmos::AppendCapsule(*context.lines,
											objectPosition + objectRotation * request.capsulePoint0,
											objectPosition + objectRotation * request.capsulePoint1,
											request.radius, color);
				break;
			default:
				break;
			}
		}

		void DrawMeshColliderGizmo(GizmoDrawContext& context, Handle<Component> component) {
			MeshCollider* collider = static_cast<MeshCollider*>(component.Get());
			if (!collider || !context.selected) {
				return;
			}
			const MeshAsset* asset = collider->GetMesh().Get();
			if (!asset) {
				return;
			}
			PhysicsShapeRequest request;
			Vec3 objectPosition;
			Quat objectRotation;
			if (!DescribeForGizmo(*collider, request, objectPosition, objectRotation)) {
				return;
			}
			const Color32 color = ColliderWire();
			const Vector<Vec3>& points = Scratch().points;
			if (points.empty() || asset->indices.size() < 3) {
				return;
			}

			const size_t pointCount = points.size();
			for (size_t triangle = 0; triangle + 2 < asset->indices.size(); triangle += 3) {
				const size_t a = asset->indices[triangle];
				const size_t b = asset->indices[triangle + 1];
				const size_t c = asset->indices[triangle + 2];
				if (a >= pointCount || b >= pointCount || c >= pointCount) {
					continue;
				}
				const Vec3 pa = objectPosition + objectRotation * points[a];
				const Vec3 pb = objectPosition + objectRotation * points[b];
				const Vec3 pc = objectPosition + objectRotation * points[c];
				context.lines->push_back({pa, pb, color});
				context.lines->push_back({pb, pc, color});
				context.lines->push_back({pc, pa, color});
			}
		}

	} // anonymous namespace

	void RegisterColliderGizmos(const void* owner) {
		EditorGizmos::RegisterComponentGizmo(StringHash("BoxCollider"), &DrawPrimitiveColliderGizmo, owner);
		EditorGizmos::RegisterComponentGizmo(StringHash("SphereCollider"), &DrawPrimitiveColliderGizmo, owner);
		EditorGizmos::RegisterComponentGizmo(StringHash("CapsuleCollider"), &DrawPrimitiveColliderGizmo, owner);
		EditorGizmos::RegisterComponentGizmo(StringHash("MeshCollider"), &DrawMeshColliderGizmo, owner);
	}

} // namespace Shalltear::Physics3DEditor
