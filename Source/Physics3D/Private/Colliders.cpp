#include "Shalltear/Physics3D/Colliders.h"

#include "PhysicsBackend.h"
#include "PhysicsWorld.h"

#include "Shalltear/Core/Math/MathUtil.h"

#include <utility>

namespace Shalltear {

	namespace {

		f32 LargestScale(const Vec3& scale) {
			return Math::Max(Math::Abs(scale.x), Math::Max(Math::Abs(scale.y), Math::Abs(scale.z)));
		}

		Vec3 AxisOf(CapsuleDirection direction) {
			switch (direction) {
			case CapsuleDirection::XAxis:
				return Vec3{1.0f, 0.0f, 0.0f};
			case CapsuleDirection::ZAxis:
				return Vec3{0.0f, 0.0f, 1.0f};
			case CapsuleDirection::YAxis:
			default:
				return Vec3{0.0f, 1.0f, 0.0f};
			}
		}

		void CapsuleScales(CapsuleDirection direction, const Vec3& scale, f32& along, f32& across) {
			const f32 x = Math::Abs(scale.x);
			const f32 y = Math::Abs(scale.y);
			const f32 z = Math::Abs(scale.z);
			switch (direction) {
			case CapsuleDirection::XAxis:
				along = x;
				across = Math::Max(y, z);
				break;
			case CapsuleDirection::ZAxis:
				along = z;
				across = Math::Max(x, y);
				break;
			case CapsuleDirection::YAxis:
			default:
				along = y;
				across = Math::Max(x, z);
				break;
			}
		}

	} // anonymous namespace

	Collider::~Collider() {
		if (_shapeBits != 0) {
			GetPhysicsWorld().RemoveCollider(*this);
		}
	}

	void Collider::OnEnable() {
		GetPhysicsWorld().AddCollider(*this);
	}

	void Collider::OnDisable() {
		GetPhysicsWorld().RemoveCollider(*this);
	}

	void Collider::SetTrigger(bool trigger) {
		_isTrigger = trigger;
		GetPhysicsWorld().RebuildShape(*this);
	}

	void Collider::SetMaterial(Handle<PhysicsMaterial> material) {
		_material = material;
		GetPhysicsWorld().PushColliderSurface(*this);
	}

	void Collider::SetCategories(const str& categories) {
		_categories = categories;
		GetPhysicsWorld().PushColliderFilter(*this);
	}

	void Collider::SetCollidesWithMode(PhysicsCategoryMatch mode) {
		_collidesWithMode = mode;
		GetPhysicsWorld().PushColliderFilter(*this);
	}

	void Collider::SetCollidesWith(const str& collidesWith) {
		_collidesWith = collidesWith;
		GetPhysicsWorld().PushColliderFilter(*this);
	}

	BoxCollider::~BoxCollider() = default;

	void BoxCollider::SetCenter(const Vec3& center) {
		_center = center;
		GetPhysicsWorld().RebuildShape(*this);
	}

	void BoxCollider::SetSize(const Vec3& size) {
		_size = size;
		GetPhysicsWorld().RebuildShape(*this);
	}

	bool BoxCollider::DescribeShape(const Vec3& lossyScale, PhysicsShapeRequest& out) const {
		out.kind = PhysicsShapeKind::Box;
		out.halfExtents = Vec3{
			Math::Abs(_size.x * lossyScale.x) * 0.5f,
			Math::Abs(_size.y * lossyScale.y) * 0.5f,
			Math::Abs(_size.z * lossyScale.z) * 0.5f};
		out.localPosition = _center * lossyScale;
		return out.halfExtents.x > 0.0f && out.halfExtents.y > 0.0f && out.halfExtents.z > 0.0f;
	}

	SphereCollider::~SphereCollider() = default;

	void SphereCollider::SetCenter(const Vec3& center) {
		_center = center;
		GetPhysicsWorld().RebuildShape(*this);
	}

	void SphereCollider::SetRadius(f32 radius) {
		_radius = radius;
		GetPhysicsWorld().RebuildShape(*this);
	}

	bool SphereCollider::DescribeShape(const Vec3& lossyScale, PhysicsShapeRequest& out) const {
		out.kind = PhysicsShapeKind::Sphere;
		out.radius = _radius * LargestScale(lossyScale);
		out.localPosition = _center * lossyScale;
		return out.radius > 0.0f;
	}

	CapsuleCollider::~CapsuleCollider() = default;

	void CapsuleCollider::SetCenter(const Vec3& center) {
		_center = center;
		GetPhysicsWorld().RebuildShape(*this);
	}

	void CapsuleCollider::SetRadius(f32 radius) {
		_radius = radius;
		GetPhysicsWorld().RebuildShape(*this);
	}

	void CapsuleCollider::SetHeight(f32 height) {
		_height = height;
		GetPhysicsWorld().RebuildShape(*this);
	}

	void CapsuleCollider::SetDirection(CapsuleDirection direction) {
		_direction = direction;
		GetPhysicsWorld().RebuildShape(*this);
	}

	bool CapsuleCollider::DescribeShape(const Vec3& lossyScale, PhysicsShapeRequest& out) const {
		f32 along = 1.0f;
		f32 across = 1.0f;
		CapsuleScales(_direction, lossyScale, along, across);

		out.kind = PhysicsShapeKind::Capsule;
		out.radius = _radius * across;

		const f32 scaledHeight = _height * along;
		const f32 cylinderHalfLength = Math::Max(0.0f, scaledHeight * 0.5f - out.radius);
		const Vec3 axis = AxisOf(_direction) * cylinderHalfLength;

		const Vec3 scaledCenter = _center * lossyScale;
		out.localPosition = scaledCenter;
		out.capsulePoint0 = scaledCenter + axis;
		out.capsulePoint1 = scaledCenter - axis;
		return out.radius > 0.0f;
	}

	MeshCollider::~MeshCollider() = default;

	void MeshCollider::SetMesh(Handle<MeshAsset> mesh) {
		_mesh = mesh;
		GetPhysicsWorld().RebuildShape(*this);
	}

	void MeshCollider::SetConvex(bool convex) {
		_convex = convex;
		GetPhysicsWorld().RebuildShape(*this);
	}

	bool MeshCollider::DescribeShape(const Vec3& lossyScale, PhysicsShapeRequest& out) const {
		out.kind = _convex ? PhysicsShapeKind::ConvexHull : PhysicsShapeKind::TriangleMesh;

		MeshAsset* asset = _mesh.Get();
		if (!asset || asset->vertices.empty() || !out.pointScratch) {
			return false;
		}

		Vector<Vec3>& points = *out.pointScratch;
		points.clear();
		points.reserve(asset->vertices.size());
		for (const MeshVertex& vertex : asset->vertices) {
			points.push_back(vertex.position * lossyScale);
		}
		out.points = points.data();
		out.pointCount = static_cast<u32>(points.size());

		if (!_convex && out.indexScratch) {
			Vector<i32>& indices = *out.indexScratch;
			indices.clear();
			indices.reserve(asset->indices.size());
			for (u32 index : asset->indices) {
				indices.push_back(static_cast<i32>(index));
			}
			if (lossyScale.x * lossyScale.y * lossyScale.z < 0.0f) {
				for (size_t first = 0; first + 2 < indices.size(); first += 3) {
					std::swap(indices[first + 1], indices[first + 2]);
				}
			}
			out.indices = indices.data();
			out.indexCount = static_cast<u32>(indices.size());
			return out.indexCount >= 3;
		}
		return out.pointCount >= 3;
	}

} // namespace Shalltear
