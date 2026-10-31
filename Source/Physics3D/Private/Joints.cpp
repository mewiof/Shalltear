#include "Shalltear/Physics3D/Joints.h"

#include "PhysicsBackend.h"
#include "PhysicsWorld.h"

#include "Shalltear/Core/Math/MathUtil.h"

namespace Shalltear {

	namespace {

		constexpr Vec3 CanonicalRotationAxis{0.0f, 0.0f, 1.0f};
		constexpr Vec3 CanonicalSlideAxis{1.0f, 0.0f, 0.0f};

		constexpr Vec3 CanonicalSwingAxis{1.0f, 0.0f, 0.0f};

		constexpr bool SpringArmed(bool use, f32 stiffness) { return use && stiffness > 0.0f; }

	} // anonymous namespace

	Joint::~Joint() {
		if (_jointBits != 0) {
			GetPhysicsWorld().RemoveJoint(*this);
		}
	}

	void Joint::OnEnable() {
		GetPhysicsWorld().AddJoint(*this);
	}

	void Joint::OnDisable() {
		GetPhysicsWorld().RemoveJoint(*this);
	}

	HingeJoint::~HingeJoint() = default;

	bool HingeJoint::DescribeJoint(PhysicsJointRequest& out) const {
		out.kind = PhysicsJointKind::Revolute;
		out.axis = _axis;
		out.canonicalAxis = CanonicalRotationAxis;

		out.enableSpring = SpringArmed(_useSpring, _spring);
		out.springStiffness = _spring;
		out.springDamper = _damper;
		out.springTarget = Math::DegToRad(_springTargetPosition);
		out.angularSpring = true;

		out.enableMotor = _useMotor;
		out.motorSpeed = Math::DegToRad(_motorTargetVelocity);
		out.maxMotorEffort = _motorForce;

		out.enableLimit = _useLimits;
		out.lowerLimit = Math::DegToRad(_limitMin);
		out.upperLimit = Math::DegToRad(_limitMax);
		out.angularLimit = true;
		return true;
	}

	FixedJoint::~FixedJoint() = default;

	bool FixedJoint::DescribeJoint(PhysicsJointRequest& out) const {
		out.kind = PhysicsJointKind::Weld;

		out.linearHertz = 0.0f;
		out.angularHertz = 0.0f;
		out.linearDampingRatio = 0.0f;
		out.angularDampingRatio = 0.0f;
		return true;
	}

	SpringJoint::~SpringJoint() = default;

	bool SpringJoint::DescribeJoint(PhysicsJointRequest& out) const {
		out.kind = PhysicsJointKind::Distance;

		out.enableSpring = true;
		out.springStiffness = _spring;
		out.springDamper = _damper;
		out.angularSpring = false;

		out.restLengthFromAnchors = true;

		out.enableLengthRange = _minDistance != 0.0f || _maxDistance != 0.0f;
		out.minLengthOffset = _minDistance;
		out.maxLengthOffset = _maxDistance;
		return true;
	}

	CharacterJoint::~CharacterJoint() = default;

	bool CharacterJoint::DescribeJoint(PhysicsJointRequest& out) const {
		out.kind = PhysicsJointKind::Spherical;
		out.axis = _axis;
		out.canonicalAxis = CanonicalRotationAxis;

		out.secondaryAxis = _swingAxis;
		out.canonicalSecondaryAxis = CanonicalSwingAxis;

		out.enableConeLimit = _useSwingLimit;
		out.coneAngleX = Math::DegToRad(_swing1Limit);
		out.coneAngleY = Math::DegToRad(_swing2Limit);

		out.enableLimit = _useTwistLimit;
		out.lowerLimit = Math::DegToRad(_lowTwistLimit);
		out.upperLimit = Math::DegToRad(_highTwistLimit);
		out.angularLimit = true;

		out.enableSpring = SpringArmed(_useDrive, _driveSpring);
		out.springStiffness = _driveSpring;
		out.springDamper = _driveDamper;
		out.angularSpring = true;

		out.targetRotation = _targetRotation.Normalized();
		return true;
	}

	PrismaticJoint::~PrismaticJoint() = default;

	bool PrismaticJoint::DescribeJoint(PhysicsJointRequest& out) const {
		out.kind = PhysicsJointKind::Prismatic;
		out.axis = _axis;
		out.canonicalAxis = CanonicalSlideAxis;

		out.enableSpring = SpringArmed(_useSpring, _spring);
		out.springStiffness = _spring;
		out.springDamper = _damper;
		out.springTarget = _springTargetPosition;
		out.angularSpring = false;

		out.enableMotor = _useMotor;
		out.motorSpeed = _motorTargetVelocity;
		out.maxMotorEffort = _motorForce;

		out.enableLimit = _useLimits;
		out.lowerLimit = _limitMin;
		out.upperLimit = _limitMax;
		out.angularLimit = false;
		return true;
	}

	void Joint::SetBreakForce(f32 force) {
		_breakForce = force;
		GetPhysicsWorld().PushJointProperty(*this, PhysicsBackend::JointProperty::ForceThreshold,
											PhysicsBackend::PropertyValue::Scalar(Math::Max(0.0f, force)));
	}

	void Joint::SetBreakTorque(f32 torque) {
		_breakTorque = torque;
		GetPhysicsWorld().PushJointProperty(*this, PhysicsBackend::JointProperty::TorqueThreshold,
											PhysicsBackend::PropertyValue::Scalar(Math::Max(0.0f, torque)));
	}

	void Joint::SetEnableCollision(bool enable) {
		_enableCollision = enable;
		GetPhysicsWorld().PushJointProperty(*this, PhysicsBackend::JointProperty::CollideConnected,
											PhysicsBackend::PropertyValue::Flag(enable));
	}

	void HingeJoint::SetUseSpring(bool use) {
		_useSpring = use;
		GetPhysicsWorld().PushJointSpring(*this, SpringArmed(_useSpring, _spring), _spring, _damper);
	}

	void HingeJoint::SetSpring(f32 spring) {
		_spring = spring;
		GetPhysicsWorld().PushJointSpring(*this, SpringArmed(_useSpring, _spring), _spring, _damper);
	}

	void HingeJoint::SetDamper(f32 damper) {
		_damper = damper;
		GetPhysicsWorld().PushJointSpring(*this, SpringArmed(_useSpring, _spring), _spring, _damper);
	}

	void HingeJoint::SetSpringTargetPosition(f32 target) {
		_springTargetPosition = target;
		GetPhysicsWorld().PushJointProperty(*this, PhysicsBackend::JointProperty::SpringTarget,
											PhysicsBackend::PropertyValue::Scalar(Math::DegToRad(target)));
	}

	void HingeJoint::SetUseMotor(bool use) {
		_useMotor = use;
		GetPhysicsWorld().PushJointProperty(*this, PhysicsBackend::JointProperty::EnableMotor,
											PhysicsBackend::PropertyValue::Flag(use));
	}

	void HingeJoint::SetMotorTargetVelocity(f32 velocity) {
		_motorTargetVelocity = velocity;
		GetPhysicsWorld().PushJointProperty(*this, PhysicsBackend::JointProperty::MotorSpeed,
											PhysicsBackend::PropertyValue::Scalar(Math::DegToRad(velocity)));
	}

	void HingeJoint::SetMotorForce(f32 force) {
		_motorForce = force;
		GetPhysicsWorld().PushJointProperty(*this, PhysicsBackend::JointProperty::MaxMotorEffort,
											PhysicsBackend::PropertyValue::Scalar(force));
	}

	void HingeJoint::SetUseLimits(bool use) {
		_useLimits = use;
		GetPhysicsWorld().PushJointProperty(*this, PhysicsBackend::JointProperty::EnableLimit,
											PhysicsBackend::PropertyValue::Flag(use));
	}

	void HingeJoint::SetLimitMin(f32 degrees) {
		_limitMin = degrees;
		GetPhysicsWorld().PushJointAngularLimits(*this, PhysicsBackend::JointProperty::Limits,
												 Math::DegToRad(_limitMin), Math::DegToRad(_limitMax));
	}

	void HingeJoint::SetLimitMax(f32 degrees) {
		_limitMax = degrees;
		GetPhysicsWorld().PushJointAngularLimits(*this, PhysicsBackend::JointProperty::Limits,
												 Math::DegToRad(_limitMin), Math::DegToRad(_limitMax));
	}

	void SpringJoint::SetSpring(f32 spring) {
		_spring = spring;
		GetPhysicsWorld().PushJointSpring(*this, true, _spring, _damper);
	}

	void SpringJoint::SetDamper(f32 damper) {
		_damper = damper;
		GetPhysicsWorld().PushJointSpring(*this, true, _spring, _damper);
	}

	void SpringJoint::SetMinDistance(f32 distance) {
		_minDistance = distance;
		GetPhysicsWorld().PushJointLengthRange(*this, _minDistance, _maxDistance);
	}

	void SpringJoint::SetMaxDistance(f32 distance) {
		_maxDistance = distance;
		GetPhysicsWorld().PushJointLengthRange(*this, _minDistance, _maxDistance);
	}

	void CharacterJoint::SetUseSwingLimit(bool use) {
		_useSwingLimit = use;
		GetPhysicsWorld().PushJointProperty(*this, PhysicsBackend::JointProperty::EnableConeLimit,
											PhysicsBackend::PropertyValue::Flag(use));
	}

	// backend sets both half angles as a pair
	void CharacterJoint::SetSwing1Limit(f32 degrees) {
		_swing1Limit = degrees;
		GetPhysicsWorld().PushJointConeAngles(*this, Math::DegToRad(_swing1Limit), Math::DegToRad(_swing2Limit));
	}

	void CharacterJoint::SetSwing2Limit(f32 degrees) {
		_swing2Limit = degrees;
		GetPhysicsWorld().PushJointConeAngles(*this, Math::DegToRad(_swing1Limit), Math::DegToRad(_swing2Limit));
	}

	void CharacterJoint::SetUseTwistLimit(bool use) {
		_useTwistLimit = use;
		GetPhysicsWorld().PushJointProperty(*this, PhysicsBackend::JointProperty::EnableTwistLimit,
											PhysicsBackend::PropertyValue::Flag(use));
	}

	void CharacterJoint::SetLowTwistLimit(f32 degrees) {
		_lowTwistLimit = degrees;
		GetPhysicsWorld().PushJointAngularLimits(*this, PhysicsBackend::JointProperty::TwistLimits,
												 Math::DegToRad(_lowTwistLimit), Math::DegToRad(_highTwistLimit));
	}

	void CharacterJoint::SetHighTwistLimit(f32 degrees) {
		_highTwistLimit = degrees;
		GetPhysicsWorld().PushJointAngularLimits(*this, PhysicsBackend::JointProperty::TwistLimits,
												 Math::DegToRad(_lowTwistLimit), Math::DegToRad(_highTwistLimit));
	}

	void CharacterJoint::SetUseDrive(bool use) {
		_useDrive = use;
		GetPhysicsWorld().PushJointDrive(*this, SpringArmed(_useDrive, _driveSpring), _driveSpring, _driveDamper);
	}

	void CharacterJoint::SetDriveSpring(f32 spring) {
		_driveSpring = spring;
		GetPhysicsWorld().PushJointDrive(*this, SpringArmed(_useDrive, _driveSpring), _driveSpring, _driveDamper);
	}

	void CharacterJoint::SetDriveDamper(f32 damper) {
		_driveDamper = damper;
		GetPhysicsWorld().PushJointDrive(*this, SpringArmed(_useDrive, _driveSpring), _driveSpring, _driveDamper);
	}

	void CharacterJoint::SetTargetRotation(const Quat& rotation) {
		_targetRotation = rotation;
		GetPhysicsWorld().PushJointProperty(*this, PhysicsBackend::JointProperty::TargetRotation,
											PhysicsBackend::PropertyValue::Rotation(rotation.Normalized()));
	}

	void PrismaticJoint::SetUseSpring(bool use) {
		_useSpring = use;
		GetPhysicsWorld().PushJointSpring(*this, SpringArmed(_useSpring, _spring), _spring, _damper);
	}

	void PrismaticJoint::SetSpring(f32 spring) {
		_spring = spring;
		GetPhysicsWorld().PushJointSpring(*this, SpringArmed(_useSpring, _spring), _spring, _damper);
	}

	void PrismaticJoint::SetDamper(f32 damper) {
		_damper = damper;
		GetPhysicsWorld().PushJointSpring(*this, SpringArmed(_useSpring, _spring), _spring, _damper);
	}

	void PrismaticJoint::SetSpringTargetPosition(f32 target) {
		_springTargetPosition = target;
		GetPhysicsWorld().PushJointProperty(*this, PhysicsBackend::JointProperty::SpringTarget,
											PhysicsBackend::PropertyValue::Scalar(target));
	}

	void PrismaticJoint::SetUseMotor(bool use) {
		_useMotor = use;
		GetPhysicsWorld().PushJointProperty(*this, PhysicsBackend::JointProperty::EnableMotor,
											PhysicsBackend::PropertyValue::Flag(use));
	}

	void PrismaticJoint::SetMotorTargetVelocity(f32 velocity) {
		_motorTargetVelocity = velocity;
		GetPhysicsWorld().PushJointProperty(*this, PhysicsBackend::JointProperty::MotorSpeed,
											PhysicsBackend::PropertyValue::Scalar(velocity));
	}

	void PrismaticJoint::SetMotorForce(f32 force) {
		_motorForce = force;
		GetPhysicsWorld().PushJointProperty(*this, PhysicsBackend::JointProperty::MaxMotorEffort,
											PhysicsBackend::PropertyValue::Scalar(force));
	}

	void PrismaticJoint::SetUseLimits(bool use) {
		_useLimits = use;
		GetPhysicsWorld().PushJointProperty(*this, PhysicsBackend::JointProperty::EnableLimit,
											PhysicsBackend::PropertyValue::Flag(use));
	}

	void PrismaticJoint::SetLimitMin(f32 metres) {
		_limitMin = metres;
		GetPhysicsWorld().PushJointProperty(*this, PhysicsBackend::JointProperty::Limits,
											PhysicsBackend::PropertyValue::Pair(_limitMin, _limitMax));
	}

	void PrismaticJoint::SetLimitMax(f32 metres) {
		_limitMax = metres;
		GetPhysicsWorld().PushJointProperty(*this, PhysicsBackend::JointProperty::Limits,
											PhysicsBackend::PropertyValue::Pair(_limitMin, _limitMax));
	}

} // namespace Shalltear
