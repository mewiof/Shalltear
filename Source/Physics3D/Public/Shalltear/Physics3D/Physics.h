#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Physics3D/Colliders.h"
#include "Shalltear/Physics3D/Physics3DAPI.h"
#include "Shalltear/Physics3D/PhysicsCategoryMatch.h"
#include "Shalltear/Physics3D/Rigidbody.h"

namespace Shalltear {

	class SceneObject;

	enum class PhysicsQueryTriggers : u8 {
		UseGlobal = 0,

		Ignore = 1,

		Collide = 2,
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::PhysicsQueryTriggers)
SLT_ENUM_VALUE(UseGlobal)
SLT_ENUM_VALUE(Ignore)
SLT_ENUM_VALUE(Collide)
SLT_END_ENUM()

namespace Shalltear {

	struct PhysicsQueryFilter {
		str categories;

		PhysicsCategoryMatch collidesWithMode = PhysicsCategoryMatch::AllExcept;
		str collidesWith;

		PhysicsQueryTriggers triggerInteraction = PhysicsQueryTriggers::UseGlobal;
	};

	struct RaycastHit {
		Handle<Collider> collider;

		Handle<SceneObject> object;

		Handle<Rigidbody> rigidbody;

		Vec3 point;

		Vec3 normal;

		// casts: sweep distance (not distance to point)
		f32 distance = 0.0f;
	};

	namespace Physics {

		inline constexpr f32 MAX_QUERY_DISTANCE = 1.0e6f;

		// ray starting inside a collider misses it
		SHALLTEAR_PHYSICS3D_API bool Raycast(const Ray& ray, RaycastHit& hit,
											 f32 maxDistance = MAX_QUERY_DISTANCE,
											 const PhysicsQueryFilter& filter = {});

		SHALLTEAR_PHYSICS3D_API bool SphereCast(const Ray& ray, f32 radius, RaycastHit& hit,
												f32 maxDistance = MAX_QUERY_DISTANCE,
												const PhysicsQueryFilter& filter = {});

		SHALLTEAR_PHYSICS3D_API bool CapsuleCast(const Vec3& point1, const Vec3& point2, f32 radius,
												 const Vec3& direction, RaycastHit& hit,
												 f32 maxDistance = MAX_QUERY_DISTANCE,
												 const PhysicsQueryFilter& filter = {});

		SHALLTEAR_PHYSICS3D_API u32 OverlapSphere(const Vec3& center, f32 radius,
												  Vector<Handle<Collider>>& results,
												  const PhysicsQueryFilter& filter = {});

	} // namespace Physics

} // namespace Shalltear
