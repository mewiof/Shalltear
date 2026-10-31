#pragma once

#include "Shalltear/Core/Math/MathReflect.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Physics3D/Physics3DAPI.h"

namespace Shalltear {

	enum class RigidbodyInterpolation : u8 {
		None = 0,

		Interpolate,

		Extrapolate,
	};

	enum class RigidbodyCollisionDetection : u8 {
		Continuous = 0,

		ContinuousDynamic = 1,
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::RigidbodyInterpolation)
SLT_ENUM_VALUE(None)
SLT_ENUM_VALUE(Interpolate)
SLT_ENUM_VALUE(Extrapolate)
SLT_END_ENUM()

SLT_REFLECT_ENUM(::Shalltear::RigidbodyCollisionDetection)
SLT_ENUM_VALUE(Continuous)
SLT_ENUM_VALUE(ContinuousDynamic)
SLT_END_ENUM()

namespace Shalltear {

	class SHALLTEAR_PHYSICS3D_API Rigidbody : public Component {
	public:
		SLT_REFLECT_COMPONENT(Rigidbody, Component)
		SLT_REFLECT_FIELD(_mass, SLT_RANGE(0.0f, 1000000.0f))
		SLT_REFLECT_FIELD(_linearDamping)
		SLT_REFLECT_FIELD(_angularDamping)
		SLT_REFLECT_FIELD(_automaticCenterOfMass)
		SLT_REFLECT_FIELD(_centerOfMass)
		SLT_REFLECT_FIELD(_automaticInertiaTensor)
		SLT_REFLECT_FIELD(_inertiaTensor)
		SLT_REFLECT_FIELD(_inertiaTensorRotation)
		SLT_REFLECT_FIELD(_useGravity)
		SLT_REFLECT_FIELD(_isKinematic)
		SLT_REFLECT_FIELD(_interpolation)
		SLT_REFLECT_FIELD(_collisionDetectionMode)
		SLT_REFLECT_FIELD(_carryRiders)
		SLT_REFLECT_FIELD(_sleepThreshold)
		SLT_REFLECT_FIELD(_freezePositionX)
		SLT_REFLECT_FIELD(_freezePositionY)
		SLT_REFLECT_FIELD(_freezePositionZ)
		SLT_REFLECT_FIELD(_freezeRotationX)
		SLT_REFLECT_FIELD(_freezeRotationY)
		SLT_REFLECT_FIELD(_freezeRotationZ)
		SLT_END_REFLECT()

		Rigidbody() = default;
		~Rigidbody() override;

		f32 GetMass() const { return _mass; }
		void SetMass(f32 mass);

		f32 GetLinearDamping() const { return _linearDamping; }
		void SetLinearDamping(f32 damping);
		f32 GetAngularDamping() const { return _angularDamping; }
		void SetAngularDamping(f32 damping);

		bool GetAutomaticCenterOfMass() const { return _automaticCenterOfMass; }
		void SetAutomaticCenterOfMass(bool automatic);

		Vec3 GetCenterOfMass() const { return _centerOfMass; }
		void SetCenterOfMass(const Vec3& center);

		bool GetAutomaticInertiaTensor() const { return _automaticInertiaTensor; }
		void SetAutomaticInertiaTensor(bool automatic);

		// principal moments
		Vec3 GetInertiaTensor() const { return _inertiaTensor; }
		void SetInertiaTensor(const Vec3& tensor);
		Quat GetInertiaTensorRotation() const { return _inertiaTensorRotation; }
		void SetInertiaTensorRotation(const Quat& rotation);

		bool GetUseGravity() const { return _useGravity; }
		void SetUseGravity(bool useGravity);
		bool IsKinematic() const { return _isKinematic; }
		void SetKinematic(bool kinematic);

		RigidbodyInterpolation GetInterpolation() const { return _interpolation; }
		void SetInterpolation(RigidbodyInterpolation interpolation) { _interpolation = interpolation; }

		RigidbodyCollisionDetection GetCollisionDetectionMode() const { return _collisionDetectionMode; }
		void SetCollisionDetectionMode(RigidbodyCollisionDetection mode);

		bool GetCarryRiders() const { return _carryRiders; }
		void SetCarryRiders(bool carry) { _carryRiders = carry; }

		// m/s (not an energy)
		f32 GetSleepThreshold() const { return _sleepThreshold; }
		void SetSleepThreshold(f32 threshold);

		bool GetFreezePositionX() const { return _freezePositionX; }
		void SetFreezePositionX(bool freeze);
		bool GetFreezePositionY() const { return _freezePositionY; }
		void SetFreezePositionY(bool freeze);
		bool GetFreezePositionZ() const { return _freezePositionZ; }
		void SetFreezePositionZ(bool freeze);
		bool GetFreezeRotationX() const { return _freezeRotationX; }
		void SetFreezeRotationX(bool freeze);
		bool GetFreezeRotationY() const { return _freezeRotationY; }
		void SetFreezeRotationY(bool freeze);
		bool GetFreezeRotationZ() const { return _freezeRotationZ; }
		void SetFreezeRotationZ(bool freeze);

		Vec3 GetLinearVelocity() const;
		void SetLinearVelocity(const Vec3& velocity);

		// rads/s about the center of mass
		Vec3 GetAngularVelocity() const;
		void SetAngularVelocity(const Vec3& velocity);

		bool IsSleeping() const;
		void Sleep();
		void WakeUp();
	protected:
		void OnEnable() override;
		void OnDisable() override;
	private:
		friend class PhysicsWorld;

		f32 _mass = 1.0f;
		f32 _linearDamping = 0.0f;
		f32 _angularDamping = 0.05f;
		bool _automaticCenterOfMass = true;
		Vec3 _centerOfMass{};
		bool _automaticInertiaTensor = true;
		Vec3 _inertiaTensor{1.0f, 1.0f, 1.0f};
		Quat _inertiaTensorRotation = Quat::Identity();
		bool _useGravity = true;
		bool _isKinematic = false;
		RigidbodyInterpolation _interpolation = RigidbodyInterpolation::None;
		RigidbodyCollisionDetection _collisionDetectionMode = RigidbodyCollisionDetection::Continuous;
		bool _carryRiders = false;
		f32 _sleepThreshold = 0.05f;
		bool _freezePositionX = false;
		bool _freezePositionY = false;
		bool _freezePositionZ = false;
		bool _freezeRotationX = false;
		bool _freezeRotationY = false;
		bool _freezeRotationZ = false;

		u64 _bodyBits = 0;

		// compared bitwise (read back after the write)
		Vec3 _syncedPosition{};
		Quat _syncedRotation = Quat::Identity();

		Vec3 _stepPosition{};
		Quat _stepRotation = Quat::Identity();
		Vec3 _previousStepPosition{};
		Quat _previousStepRotation = Quat::Identity();

		Vec3 _pendingLinearVelocity{};
		Vec3 _pendingAngularVelocity{};
	};

} // namespace Shalltear
