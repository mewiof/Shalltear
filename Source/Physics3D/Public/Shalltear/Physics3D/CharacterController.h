#pragma once

#include "Shalltear/Core/EnumFlags.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/MathReflect.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Physics3D/Colliders.h"
#include "Shalltear/Physics3D/Physics3DAPI.h"
#include "Shalltear/Physics3D/Rigidbody.h"

namespace Shalltear {

	class SceneObject;
	class CharacterController;

	enum class CollisionFlags : u8 {
		None = 0,
		Sides = 1,
		Above = 2,
		Below = 4,
	};

	SLT_ENUM_CLASS_FLAGS(CollisionFlags)

	struct ControllerColliderHit {
		Handle<CharacterController> controller;

		Handle<Collider> collider;

		Handle<SceneObject> object;

		Handle<Rigidbody> rigidbody;

		Vec3 point;

		Vec3 normal;

		// direction of the move pass that hit (up, sideways or down)
		Vec3 moveDirection;

		f32 moveLength = 0.0f;
	};

	class SHALLTEAR_PHYSICS3D_API CharacterController : public Collider {
	public:
		SLT_REFLECT_COMPONENT(CharacterController, Collider)
		SLT_REFLECT_FIELD(_slopeLimit, SLT_RANGE(0.0f, 180.0f))
		SLT_REFLECT_FIELD(_stepOffset)
		SLT_REFLECT_FIELD(_skinWidth)
		SLT_REFLECT_FIELD(_minMoveDistance)
		SLT_REFLECT_FIELD(_center)
		SLT_REFLECT_FIELD(_radius)
		SLT_REFLECT_FIELD(_height)
		SLT_END_REFLECT()

		CharacterController() = default;
		~CharacterController() override;

		// absolute delta (not a speed), no gravity
		CollisionFlags Move(const Vec3& motion);

		// m/s, y ignored, gravity applied
		bool SimpleMove(const Vec3& speed);

		bool IsGrounded() const { return HasAnyFlag(_collisionFlags, CollisionFlags::Below); }

		CollisionFlags GetCollisionFlags() const { return _collisionFlags; }

		// relative to a carrying platform
		Vec3 GetVelocity() const { return _velocity; }

		// degrees
		f32 GetSlopeLimit() const { return _slopeLimit; }
		void SetSlopeLimit(f32 degrees) { _slopeLimit = degrees; }

		f32 GetStepOffset() const { return _stepOffset; }
		void SetStepOffset(f32 offset) { _stepOffset = offset; }

		f32 GetSkinWidth() const { return _skinWidth; }
		void SetSkinWidth(f32 width) { _skinWidth = width; }

		f32 GetMinMoveDistance() const { return _minMoveDistance; }
		void SetMinMoveDistance(f32 distance) { _minMoveDistance = distance; }

		// offset from the pivot, never rotated
		Vec3 GetCenter() const { return _center; }
		void SetCenter(const Vec3& center);

		f32 GetRadius() const { return _radius; }
		void SetRadius(f32 radius);

		// total height
		f32 GetHeight() const { return _height; }
		void SetHeight(f32 height);

		bool GetDetectCollisions() const { return _detectCollisions; }
		void SetDetectCollisions(bool detect);

		// upright along world y (rotation ignored)
		bool DescribeShape(const Vec3& lossyScale, PhysicsShapeRequest& out) const override;
	protected:
		void OnEnable() override;
		void OnDisable() override;
	private:
		friend class PhysicsWorld;

		f32 _slopeLimit = 45.0f;
		f32 _stepOffset = 0.3f;
		f32 _skinWidth = 0.08f;
		f32 _minMoveDistance = 0.0f;
		Vec3 _center{};
		f32 _radius = 0.5f;
		f32 _height = 2.0f;

		bool _detectCollisions = true;

		CollisionFlags _collisionFlags = CollisionFlags::None;
		Vec3 _velocity{};
		Vec3 _fallVelocity{};

		u64 _bodyBits = 0;
		u64 _queryCategoryBits = 0;
		u64 _queryMaskBits = 0;
		Vec3 _syncedPosition{};

		Vec3 _movedPosition{};
		bool _hasMoved = false;

		Handle<Rigidbody> _carrier;
		Vec3 _carrierAnchor{};
		Quat _carrierRotation = Quat::Identity();

		bool _reportedOutside = false;
	};

} // namespace Shalltear
