#pragma once

#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/MathReflect.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Physics3D/Physics3DAPI.h"
#include "Shalltear/Physics3D/Rigidbody.h"

namespace Shalltear {

	struct PhysicsJointRequest;

	class SHALLTEAR_PHYSICS3D_API Joint : public Component {
	public:
		SLT_REFLECT_COMPONENT_ABSTRACT(Joint, Component)
		SLT_REFLECT_FIELD(_connectedBody)
		SLT_REFLECT_FIELD(_anchor)
		SLT_REFLECT_FIELD(_autoConfigureConnectedAnchor)
		SLT_REFLECT_FIELD(_connectedAnchor)
		SLT_REFLECT_FIELD(_breakForce)
		SLT_REFLECT_FIELD(_breakTorque)
		SLT_REFLECT_FIELD(_enableCollision)
		SLT_END_REFLECT()

		~Joint() override;

		// FLT_MAX (JSON has no infinity)
		static constexpr f32 NEVER_BREAKS = 3.4028234663852886e+38f;

		Handle<Rigidbody> GetConnectedBody() const { return _connectedBody; }
		void SetConnectedBody(Handle<Rigidbody> body) { _connectedBody = body; }

		Vec3 GetAnchor() const { return _anchor; }
		void SetAnchor(const Vec3& anchor) { _anchor = anchor; }

		bool GetAutoConfigureConnectedAnchor() const { return _autoConfigureConnectedAnchor; }
		void SetAutoConfigureConnectedAnchor(bool automatic) { _autoConfigureConnectedAnchor = automatic; }

		Vec3 GetConnectedAnchor() const { return _connectedAnchor; }
		void SetConnectedAnchor(const Vec3& anchor) { _connectedAnchor = anchor; }

		f32 GetBreakForce() const { return _breakForce; }
		void SetBreakForce(f32 force);

		f32 GetBreakTorque() const { return _breakTorque; }
		void SetBreakTorque(f32 torque);

		bool GetEnableCollision() const { return _enableCollision; }
		void SetEnableCollision(bool enable);

		bool IsInSimulation() const { return _jointBits != 0; }

		// SI units
		virtual bool DescribeJoint(PhysicsJointRequest& out) const = 0;
	protected:
		void OnEnable() override;
		void OnDisable() override;
	private:
		friend class PhysicsWorld;

		Handle<Rigidbody> _connectedBody;
		Vec3 _anchor{};
		bool _autoConfigureConnectedAnchor = true;
		Vec3 _connectedAnchor{};
		f32 _breakForce = NEVER_BREAKS;
		f32 _breakTorque = NEVER_BREAKS;
		bool _enableCollision = false;

		u64 _jointBits = 0;

		f32 _springEffectiveMass = 0.0f;

		bool _anchoredToWorld = false;
	};

	class SHALLTEAR_PHYSICS3D_API HingeJoint : public Joint {
	public:
		SLT_REFLECT_COMPONENT(HingeJoint, Joint)
		SLT_REFLECT_FIELD(_axis)
		SLT_REFLECT_FIELD(_useSpring)
		SLT_REFLECT_FIELD(_spring)
		SLT_REFLECT_FIELD(_damper)
		SLT_REFLECT_FIELD(_springTargetPosition)
		SLT_REFLECT_FIELD(_useMotor)
		SLT_REFLECT_FIELD(_motorTargetVelocity)
		SLT_REFLECT_FIELD(_motorForce)
		SLT_REFLECT_FIELD(_useLimits)
		SLT_REFLECT_FIELD(_limitMin)
		SLT_REFLECT_FIELD(_limitMax)
		SLT_END_REFLECT()

		HingeJoint() = default;
		~HingeJoint() override;

		Vec3 GetAxis() const { return _axis; }
		void SetAxis(const Vec3& axis) { _axis = axis; }

		bool GetUseSpring() const { return _useSpring; }
		void SetUseSpring(bool use);

		// torque per rad, torque per rad per second
		f32 GetSpring() const { return _spring; }
		void SetSpring(f32 spring);
		f32 GetDamper() const { return _damper; }
		void SetDamper(f32 damper);

		// degrees
		f32 GetSpringTargetPosition() const { return _springTargetPosition; }
		void SetSpringTargetPosition(f32 target);

		bool GetUseMotor() const { return _useMotor; }
		void SetUseMotor(bool use);

		// deg/s
		f32 GetMotorTargetVelocity() const { return _motorTargetVelocity; }
		void SetMotorTargetVelocity(f32 velocity);

		f32 GetMotorForce() const { return _motorForce; }
		void SetMotorForce(f32 force);

		bool GetUseLimits() const { return _useLimits; }
		void SetUseLimits(bool use);

		// degrees, solver clamps to +/- 178.2
		f32 GetLimitMin() const { return _limitMin; }
		void SetLimitMin(f32 degrees);
		f32 GetLimitMax() const { return _limitMax; }
		void SetLimitMax(f32 degrees);

		bool DescribeJoint(PhysicsJointRequest& out) const override;
	private:
		Vec3 _axis{1.0f, 0.0f, 0.0f};
		bool _useSpring = false;
		f32 _spring = 0.0f;
		f32 _damper = 0.0f;
		f32 _springTargetPosition = 0.0f;
		bool _useMotor = false;
		f32 _motorTargetVelocity = 0.0f;
		f32 _motorForce = 0.0f;
		bool _useLimits = false;
		f32 _limitMin = 0.0f;
		f32 _limitMax = 0.0f;
	};

	class SHALLTEAR_PHYSICS3D_API FixedJoint : public Joint {
	public:
		SLT_REFLECT_COMPONENT(FixedJoint, Joint)
		SLT_END_REFLECT()

		FixedJoint() = default;
		~FixedJoint() override;

		bool DescribeJoint(PhysicsJointRequest& out) const override;
	};

	class SHALLTEAR_PHYSICS3D_API SpringJoint : public Joint {
	public:
		SLT_REFLECT_COMPONENT(SpringJoint, Joint)
		SLT_REFLECT_FIELD(_spring)
		SLT_REFLECT_FIELD(_damper)
		SLT_REFLECT_FIELD(_minDistance)
		SLT_REFLECT_FIELD(_maxDistance)
		SLT_END_REFLECT()

		SpringJoint() = default;
		~SpringJoint() override;

		f32 GetSpring() const { return _spring; }
		void SetSpring(f32 spring);
		f32 GetDamper() const { return _damper; }
		void SetDamper(f32 damper);

		f32 GetMinDistance() const { return _minDistance; }
		void SetMinDistance(f32 distance);
		f32 GetMaxDistance() const { return _maxDistance; }
		void SetMaxDistance(f32 distance);

		bool DescribeJoint(PhysicsJointRequest& out) const override;
	private:
		f32 _spring = 10.0f;
		f32 _damper = 0.0f;
		f32 _minDistance = 0.0f;
		f32 _maxDistance = 0.0f;
	};

	class SHALLTEAR_PHYSICS3D_API CharacterJoint : public Joint {
	public:
		SLT_REFLECT_COMPONENT(CharacterJoint, Joint)
		SLT_REFLECT_FIELD(_axis)
		SLT_REFLECT_FIELD(_swingAxis)
		SLT_REFLECT_FIELD(_useSwingLimit)
		SLT_REFLECT_FIELD(_swing1Limit)
		SLT_REFLECT_FIELD(_swing2Limit)
		SLT_REFLECT_FIELD(_useTwistLimit)
		SLT_REFLECT_FIELD(_lowTwistLimit)
		SLT_REFLECT_FIELD(_highTwistLimit)
		SLT_REFLECT_FIELD(_useDrive)
		SLT_REFLECT_FIELD(_driveSpring)
		SLT_REFLECT_FIELD(_driveDamper)
		SLT_REFLECT_FIELD(_targetRotation)
		SLT_END_REFLECT()

		CharacterJoint() = default;
		~CharacterJoint() override;

		Vec3 GetAxis() const { return _axis; }
		void SetAxis(const Vec3& axis) { _axis = axis; }

		Vec3 GetSwingAxis() const { return _swingAxis; }
		void SetSwingAxis(const Vec3& axis) { _swingAxis = axis; }

		// elliptical cone, 2 symmetric half angles in degrees
		bool GetUseSwingLimit() const { return _useSwingLimit; }
		void SetUseSwingLimit(bool use);
		f32 GetSwing1Limit() const { return _swing1Limit; }
		void SetSwing1Limit(f32 degrees);
		f32 GetSwing2Limit() const { return _swing2Limit; }
		void SetSwing2Limit(f32 degrees);

		// degrees from the start, clamped to +/- 178.2
		bool GetUseTwistLimit() const { return _useTwistLimit; }
		void SetUseTwistLimit(bool use);
		f32 GetLowTwistLimit() const { return _lowTwistLimit; }
		void SetLowTwistLimit(f32 degrees);
		f32 GetHighTwistLimit() const { return _highTwistLimit; }
		void SetHighTwistLimit(f32 degrees);

		bool GetUseDrive() const { return _useDrive; }
		void SetUseDrive(bool use);

		// torque per rad, unconverted
		f32 GetDriveSpring() const { return _driveSpring; }
		void SetDriveSpring(f32 spring);
		f32 GetDriveDamper() const { return _driveDamper; }
		void SetDriveDamper(f32 damper);

		Quat GetTargetRotation() const { return _targetRotation; }
		void SetTargetRotation(const Quat& rotation);

		bool DescribeJoint(PhysicsJointRequest& out) const override;
	private:
		Vec3 _axis{1.0f, 0.0f, 0.0f};
		Vec3 _swingAxis{0.0f, 1.0f, 0.0f};
		bool _useSwingLimit = false;
		f32 _swing1Limit = 0.0f;
		f32 _swing2Limit = 0.0f;
		bool _useTwistLimit = false;
		f32 _lowTwistLimit = 0.0f;
		f32 _highTwistLimit = 0.0f;
		bool _useDrive = false;
		f32 _driveSpring = 0.0f;
		f32 _driveDamper = 0.0f;
		Quat _targetRotation = Quat::Identity();
	};

	class SHALLTEAR_PHYSICS3D_API PrismaticJoint : public Joint {
	public:
		SLT_REFLECT_COMPONENT(PrismaticJoint, Joint)
		SLT_REFLECT_FIELD(_axis)
		SLT_REFLECT_FIELD(_useSpring)
		SLT_REFLECT_FIELD(_spring)
		SLT_REFLECT_FIELD(_damper)
		SLT_REFLECT_FIELD(_springTargetPosition)
		SLT_REFLECT_FIELD(_useMotor)
		SLT_REFLECT_FIELD(_motorTargetVelocity)
		SLT_REFLECT_FIELD(_motorForce)
		SLT_REFLECT_FIELD(_useLimits)
		SLT_REFLECT_FIELD(_limitMin)
		SLT_REFLECT_FIELD(_limitMax)
		SLT_END_REFLECT()

		PrismaticJoint() = default;
		~PrismaticJoint() override;

		Vec3 GetAxis() const { return _axis; }
		void SetAxis(const Vec3& axis) { _axis = axis; }

		// N/m and N/(m/s)
		bool GetUseSpring() const { return _useSpring; }
		void SetUseSpring(bool use);
		f32 GetSpring() const { return _spring; }
		void SetSpring(f32 spring);
		f32 GetDamper() const { return _damper; }
		void SetDamper(f32 damper);
		f32 GetSpringTargetPosition() const { return _springTargetPosition; }
		void SetSpringTargetPosition(f32 target);

		bool GetUseMotor() const { return _useMotor; }
		void SetUseMotor(bool use);
		f32 GetMotorTargetVelocity() const { return _motorTargetVelocity; }
		void SetMotorTargetVelocity(f32 velocity);
		f32 GetMotorForce() const { return _motorForce; }
		void SetMotorForce(f32 force);

		// not clamped by the solver
		bool GetUseLimits() const { return _useLimits; }
		void SetUseLimits(bool use);
		f32 GetLimitMin() const { return _limitMin; }
		void SetLimitMin(f32 metres);
		f32 GetLimitMax() const { return _limitMax; }
		void SetLimitMax(f32 metres);

		bool DescribeJoint(PhysicsJointRequest& out) const override;
	private:
		Vec3 _axis{1.0f, 0.0f, 0.0f};
		bool _useSpring = false;
		f32 _spring = 0.0f;
		f32 _damper = 0.0f;
		f32 _springTargetPosition = 0.0f;
		bool _useMotor = false;
		f32 _motorTargetVelocity = 0.0f;
		f32 _motorForce = 0.0f;
		bool _useLimits = false;
		f32 _limitMin = 0.0f;
		f32 _limitMax = 0.0f;
	};

} // namespace Shalltear
