#include "Shalltear/Physics3D/Physics.h"

#include "Shalltear/Core/Log.h"

#include "PhysicsWorld.h"

namespace Shalltear::Physics {

	namespace {

		bool RadiusIsUsable(f32 radius, const char* query) {
			if (radius > 0.0f) {
				return true;
			}
			SLT_LOG_WARN("Physics", "{} was given a radius of {} (a 0 radius returns no hit). Use Raycast for it",
						 query, radius);
			return false;
		}

	} // anonymous namespace

	bool Raycast(const Ray& ray, RaycastHit& hit, f32 maxDistance, const PhysicsQueryFilter& filter) {
		return GetPhysicsWorld().CastRay(ray, maxDistance, filter, hit);
	}

	bool SphereCast(const Ray& ray, f32 radius, RaycastHit& hit, f32 maxDistance, const PhysicsQueryFilter& filter) {
		hit = RaycastHit{};
		if (!RadiusIsUsable(radius, "SphereCast")) {
			return false;
		}
		const Vec3 center = ray.origin;
		return GetPhysicsWorld().CastShape(&center, 1, radius, ray.direction, maxDistance, filter, hit);
	}

	bool CapsuleCast(const Vec3& point1, const Vec3& point2, f32 radius, const Vec3& direction, RaycastHit& hit,
					 f32 maxDistance, const PhysicsQueryFilter& filter) {
		hit = RaycastHit{};
		if (!RadiusIsUsable(radius, "CapsuleCast")) {
			return false;
		}
		const Vec3 swept = direction.Normalized();
		if (swept.LengthSquared() <= 0.0f) {
			SLT_LOG_WARN("Physics", "CapsuleCast was given a 0-length direction. Nothing was cast");
			return false;
		}

		const Vec3 points[2] = {point1, point2};
		return GetPhysicsWorld().CastShape(points, 2, radius, swept, maxDistance, filter, hit);
	}

	u32 OverlapSphere(const Vec3& center, f32 radius, Vector<Handle<Collider>>& results,
					  const PhysicsQueryFilter& filter) {
		results.clear();
		if (!RadiusIsUsable(radius, "OverlapSphere")) {
			return 0;
		}
		return GetPhysicsWorld().OverlapShape(&center, 1, radius, filter, results);
	}

} // namespace Shalltear::Physics
