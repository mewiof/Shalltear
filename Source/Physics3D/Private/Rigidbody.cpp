#include "Shalltear/Physics3D/Rigidbody.h"

#include "PhysicsBackend.h"
#include "PhysicsWorld.h"

namespace Shalltear {

	Rigidbody::~Rigidbody() {
		if (_bodyBits != 0) {
			GetPhysicsWorld().RemoveBody(*this);
		}
	}

	void Rigidbody::OnEnable() {
		GetPhysicsWorld().AddBody(*this);
	}

	void Rigidbody::OnDisable() {
		GetPhysicsWorld().RemoveBody(*this);
	}

	void Rigidbody::SetMass(f32 mass) {
		_mass = mass;
		GetPhysicsWorld().RefreshMass(*this);
	}

	void Rigidbody::SetAutomaticCenterOfMass(bool automatic) {
		_automaticCenterOfMass = automatic;
		GetPhysicsWorld().RefreshMass(*this);
	}

	void Rigidbody::SetCenterOfMass(const Vec3& center) {
		_centerOfMass = center;
		GetPhysicsWorld().RefreshMass(*this);
	}

	void Rigidbody::SetAutomaticInertiaTensor(bool automatic) {
		_automaticInertiaTensor = automatic;
		GetPhysicsWorld().RefreshMass(*this);
	}

	void Rigidbody::SetInertiaTensor(const Vec3& tensor) {
		_inertiaTensor = tensor;
		GetPhysicsWorld().RefreshMass(*this);
	}

	void Rigidbody::SetInertiaTensorRotation(const Quat& rotation) {
		_inertiaTensorRotation = rotation;
		GetPhysicsWorld().RefreshMass(*this);
	}

	void Rigidbody::SetKinematic(bool kinematic) {
		_isKinematic = kinematic;
		GetPhysicsWorld().PushBodyKind(*this);
	}

	void Rigidbody::SetLinearDamping(f32 damping) {
		_linearDamping = damping;
		GetPhysicsWorld().PushBodyProperty(*this, PhysicsBackend::BodyProperty::LinearDamping,
										   PhysicsBackend::PropertyValue::Scalar(damping));
	}

	void Rigidbody::SetAngularDamping(f32 damping) {
		_angularDamping = damping;
		GetPhysicsWorld().PushBodyProperty(*this, PhysicsBackend::BodyProperty::AngularDamping,
										   PhysicsBackend::PropertyValue::Scalar(damping));
	}

	void Rigidbody::SetUseGravity(bool useGravity) {
		_useGravity = useGravity;
		GetPhysicsWorld().PushBodyProperty(*this, PhysicsBackend::BodyProperty::GravityScale,
										   PhysicsBackend::PropertyValue::Scalar(useGravity ? 1.0f : 0.0f));
	}

	void Rigidbody::SetSleepThreshold(f32 threshold) {
		_sleepThreshold = threshold;
		GetPhysicsWorld().PushBodyProperty(*this, PhysicsBackend::BodyProperty::SleepThreshold,
										   PhysicsBackend::PropertyValue::Scalar(threshold));
	}

	void Rigidbody::SetCollisionDetectionMode(RigidbodyCollisionDetection mode) {
		_collisionDetectionMode = mode;
		GetPhysicsWorld().PushBodyProperty(*this, PhysicsBackend::BodyProperty::IsBullet,
										   PhysicsBackend::PropertyValue::Flag(mode == RigidbodyCollisionDetection::ContinuousDynamic));
	}

	void Rigidbody::SetFreezePositionX(bool freeze) {
		_freezePositionX = freeze;
		GetPhysicsWorld().PushBodyProperty(*this, PhysicsBackend::BodyProperty::FreezePositionX,
										   PhysicsBackend::PropertyValue::Flag(freeze));
	}

	void Rigidbody::SetFreezePositionY(bool freeze) {
		_freezePositionY = freeze;
		GetPhysicsWorld().PushBodyProperty(*this, PhysicsBackend::BodyProperty::FreezePositionY,
										   PhysicsBackend::PropertyValue::Flag(freeze));
	}

	void Rigidbody::SetFreezePositionZ(bool freeze) {
		_freezePositionZ = freeze;
		GetPhysicsWorld().PushBodyProperty(*this, PhysicsBackend::BodyProperty::FreezePositionZ,
										   PhysicsBackend::PropertyValue::Flag(freeze));
	}

	void Rigidbody::SetFreezeRotationX(bool freeze) {
		_freezeRotationX = freeze;
		GetPhysicsWorld().PushBodyProperty(*this, PhysicsBackend::BodyProperty::FreezeRotationX,
										   PhysicsBackend::PropertyValue::Flag(freeze));
	}

	void Rigidbody::SetFreezeRotationY(bool freeze) {
		_freezeRotationY = freeze;
		GetPhysicsWorld().PushBodyProperty(*this, PhysicsBackend::BodyProperty::FreezeRotationY,
										   PhysicsBackend::PropertyValue::Flag(freeze));
	}

	void Rigidbody::SetFreezeRotationZ(bool freeze) {
		_freezeRotationZ = freeze;
		GetPhysicsWorld().PushBodyProperty(*this, PhysicsBackend::BodyProperty::FreezeRotationZ,
										   PhysicsBackend::PropertyValue::Flag(freeze));
	}

	Vec3 Rigidbody::GetLinearVelocity() const {
		if (_bodyBits == 0) {
			return _pendingLinearVelocity;
		}
		return PhysicsBackend::GetBodyLinearVelocity(PhysicsBackend::BodyHandle{_bodyBits});
	}

	void Rigidbody::SetLinearVelocity(const Vec3& velocity) {
		if (_bodyBits == 0) {
			_pendingLinearVelocity = velocity;
			return;
		}
		PhysicsBackend::SetBodyLinearVelocity(PhysicsBackend::BodyHandle{_bodyBits}, velocity);
	}

	Vec3 Rigidbody::GetAngularVelocity() const {
		if (_bodyBits == 0) {
			return _pendingAngularVelocity;
		}
		return PhysicsBackend::GetBodyAngularVelocity(PhysicsBackend::BodyHandle{_bodyBits});
	}

	void Rigidbody::SetAngularVelocity(const Vec3& velocity) {
		if (_bodyBits == 0) {
			_pendingAngularVelocity = velocity;
			return;
		}
		PhysicsBackend::SetBodyAngularVelocity(PhysicsBackend::BodyHandle{_bodyBits}, velocity);
	}

	bool Rigidbody::IsSleeping() const {
		if (_bodyBits == 0) {
			return false;
		}
		return !PhysicsBackend::IsBodyAwake(PhysicsBackend::BodyHandle{_bodyBits});
	}

	void Rigidbody::Sleep() {
		if (_bodyBits != 0) {
			PhysicsBackend::SetBodyAwake(PhysicsBackend::BodyHandle{_bodyBits}, false);
		}
	}

	void Rigidbody::WakeUp() {
		if (_bodyBits != 0) {
			PhysicsBackend::SetBodyAwake(PhysicsBackend::BodyHandle{_bodyBits}, true);
		}
	}

} // namespace Shalltear
