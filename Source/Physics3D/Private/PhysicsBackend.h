#pragma once

// package private and never included by a public header

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear {

	enum class PhysicsShapeKind : u8 {
		Box,
		Sphere,
		Capsule,

		ConvexHull,

		// static bodies only
		TriangleMesh,
	};

	struct PhysicsSurface {
		f32 friction = 0.6f;
		f32 bounciness = 0.0f;
		f32 rollingResistance = 0.0f;
		u64 combineBits = 0;
	};

	inline bool operator==(const PhysicsSurface& a, const PhysicsSurface& b) {
		return a.friction == b.friction && a.bounciness == b.bounciness &&
			   a.rollingResistance == b.rollingResistance && a.combineBits == b.combineBits;
	}
	inline bool operator!=(const PhysicsSurface& a, const PhysicsSurface& b) { return !(a == b); }

	struct PhysicsShapeRequest {
		PhysicsShapeKind kind = PhysicsShapeKind::Box;

		Vec3 localPosition{};
		Quat localRotation = Quat::Identity();

		Vec3 halfExtents{};
		f32 radius = 0.0f;
		Vec3 capsulePoint0{};
		Vec3 capsulePoint1{};

		// must outlive the create call
		const Vec3* points = nullptr;
		u32 pointCount = 0;
		const i32* indices = nullptr;
		u32 indexCount = 0;

		Vector<Vec3>* pointScratch = nullptr;
		Vector<i32>* indexScratch = nullptr;

		PhysicsSurface surface;

		u64 categoryBits = 0;
		u64 maskBits = ~0ull;
		i32 groupIndex = 0;

		bool isTrigger = false;

		u64 owner = 0;
	};

	enum class PhysicsJointKind : u8 {
		Revolute,
		Prismatic,
		Spherical,
		Weld,
		Distance,
	};

	struct PhysicsJointRequest {
		PhysicsJointKind kind = PhysicsJointKind::Weld;

		Vec3 axis{0.0f, 1.0f, 0.0f};

		Vec3 canonicalAxis{0.0f, 0.0f, 1.0f};

		Vec3 secondaryAxis{0.0f, 0.0f, 0.0f};

		Vec3 canonicalSecondaryAxis{1.0f, 0.0f, 0.0f};

		// per meter, per radian when angularSpring
		bool enableSpring = false;
		f32 springStiffness = 0.0f;
		f32 springDamper = 0.0f;

		// rads (hinge), meters (slide)
		f32 springTarget = 0.0f;

		Quat targetRotation = Quat::Identity();

		bool angularSpring = false;

		// rads or meters, unclamped
		bool enableLimit = false;
		f32 lowerLimit = 0.0f;
		f32 upperLimit = 0.0f;
		bool angularLimit = false;

		// rads, 2 half angles about frame A x/y
		bool enableConeLimit = false;
		f32 coneAngleX = 0.0f;
		f32 coneAngleY = 0.0f;

		// rads or m/s, matching effort cap
		bool enableMotor = false;
		f32 motorSpeed = 0.0f;
		f32 maxMotorEffort = 0.0f;

		bool enableLengthRange = false;
		f32 minLengthOffset = 0.0f;
		f32 maxLengthOffset = 0.0f;

		bool restLengthFromAnchors = false;

		f32 linearHertz = 0.0f;
		f32 linearDampingRatio = 0.0f;
		f32 angularHertz = 0.0f;
		f32 angularDampingRatio = 0.0f;
	};

	namespace PhysicsBackend {

		struct WorldHandle {
			u32 bits = 0;

			bool IsNull() const { return bits == 0; }
		};

		inline bool operator==(WorldHandle a, WorldHandle b) { return a.bits == b.bits; }
		inline bool operator!=(WorldHandle a, WorldHandle b) { return a.bits != b.bits; }

		// backend's ceiling (BackendBox3D.cpp static asserts)
		inline constexpr i32 PHYSICS_MAX_WORKERS = 32;

		inline constexpr i32 PHYSICS_AUTO_WORKER_CEILING = 8;

		// includes the calling thread
		inline u32 ResolveWorkerCount(i32 requested, u32 hardwareThreads) {
			if (requested <= 0) {
				requested = (hardwareThreads < 2) ? 1 : static_cast<i32>(hardwareThreads - 1);
				if (requested > PHYSICS_AUTO_WORKER_CEILING) {
					requested = PHYSICS_AUTO_WORKER_CEILING;
				}
			}

			if (requested > PHYSICS_MAX_WORKERS) {
				requested = PHYSICS_MAX_WORKERS;
			}
			return static_cast<u32>(requested);
		}

		struct WorldParams {
			f32 gravityX = 0.0f;
			f32 gravityY = -9.81f;
			f32 gravityZ = 0.0f;
			f32 bounceThreshold = 1.0f;
			f32 hitEventThreshold = 1.0f;
			f32 contactHertz = 30.0f;
			f32 contactDampingRatio = 10.0f;
			f32 contactSpeed = 3.0f;
			f32 maxLinearSpeed = 400.0f;
			bool enableSleeping = true;
			bool enableContinuous = true;
			u32 workerCount = 1;
		};

		// field added above needs a line here
		inline bool operator==(const WorldParams& a, const WorldParams& b) {
			return a.gravityX == b.gravityX && a.gravityY == b.gravityY && a.gravityZ == b.gravityZ &&
				   a.bounceThreshold == b.bounceThreshold && a.hitEventThreshold == b.hitEventThreshold &&
				   a.contactHertz == b.contactHertz && a.contactDampingRatio == b.contactDampingRatio &&
				   a.contactSpeed == b.contactSpeed && a.maxLinearSpeed == b.maxLinearSpeed &&
				   a.enableSleeping == b.enableSleeping && a.enableContinuous == b.enableContinuous &&
				   a.workerCount == b.workerCount;
		}

		inline bool operator!=(const WorldParams& a, const WorldParams& b) { return !(a == b); }

		struct BodyHandle {
			u64 bits = 0;

			bool IsNull() const { return bits == 0; }
		};

		inline bool operator==(BodyHandle a, BodyHandle b) { return a.bits == b.bits; }
		inline bool operator!=(BodyHandle a, BodyHandle b) { return a.bits != b.bits; }

		enum class BodyKind : u8 {
			Static,
			Kinematic,
			Dynamic,
		};

		struct BodyParams {
			Vec3 position{};
			Quat rotation = Quat::Identity();
			Vec3 linearVelocity{};
			Vec3 angularVelocity{};
			Vec3 centerOfMass{};
			f32 mass = 1.0f;
			f32 linearDamping = 0.0f;
			f32 angularDamping = 0.05f;
			f32 gravityScale = 1.0f;
			f32 sleepThreshold = 0.05f;
			BodyKind kind = BodyKind::Dynamic;
			bool freezePositionX = false;
			bool freezePositionY = false;
			bool freezePositionZ = false;
			bool freezeRotationX = false;
			bool freezeRotationY = false;
			bool freezeRotationZ = false;

			bool isBullet = false;

			u64 owner = 0;
		};

		struct PropertyValue {
			f32 v[4] = {0.0f, 0.0f, 0.0f, 0.0f};

			static PropertyValue Scalar(f32 value) { return PropertyValue{{value, 0.0f, 0.0f, 0.0f}}; }
			static PropertyValue Flag(bool value) { return PropertyValue{{value ? 1.0f : 0.0f, 0.0f, 0.0f, 0.0f}}; }
			static PropertyValue Pair(f32 first, f32 second) { return PropertyValue{{first, second, 0.0f, 0.0f}}; }
			static PropertyValue Vector(const Vec3& value) { return PropertyValue{{value.x, value.y, value.z, 0.0f}}; }
			static PropertyValue Rotation(const Quat& value) { return PropertyValue{{value.x, value.y, value.z, value.w}}; }

			f32 AsScalar() const { return v[0]; }
			bool AsFlag() const { return v[0] != 0.0f; }
			Vec3 AsVector() const { return Vec3{v[0], v[1], v[2]}; }
			Quat AsRotation() const { return Quat{v[0], v[1], v[2], v[3]}; }
		};

		struct ShapeHandle {
			u64 bits = 0;

			bool IsNull() const { return bits == 0; }
		};

		inline bool operator==(ShapeHandle a, ShapeHandle b) { return a.bits == b.bits; }
		inline bool operator!=(ShapeHandle a, ShapeHandle b) { return a.bits != b.bits; }

		bool CreateShape(BodyHandle body, const PhysicsShapeRequest& request, ShapeHandle& out);

		void DestroyShape(ShapeHandle shape);

		void SetShapeFilter(ShapeHandle shape, u64 categoryBits, u64 maskBits, i32 groupIndex);

		void SetShapeSurface(ShapeHandle shape, const PhysicsSurface& surface);

		u32 GetBodyShapeCount(BodyHandle body);

		bool SupportsTriangleMeshOn(BodyKind kind);

		struct MassParams {
			f32 mass = 1.0f;
			Vec3 centerOfMass{};
			Vec3 inertiaTensor{};
			Quat inertiaTensorRotation = Quat::Identity();
			bool automaticCenterOfMass = true;
			bool automaticInertiaTensor = true;
			bool hasShapes = false;
		};

		void RefreshBodyMass(BodyHandle body, const MassParams& params);

		std::string_view Name();

		// must run before the backend's 1st allocation
		void InstallHostServices();

		bool CreateWorld(const WorldParams& params, WorldHandle& out);

		void DestroyWorld(WorldHandle world);

		bool IsWorldValid(WorldHandle world);

		void StepWorld(WorldHandle world, f32 timeStep, i32 substeps);

		void UpdateWorld(WorldHandle world, const WorldParams& params);

		bool CreateBody(WorldHandle world, const BodyParams& params, BodyHandle& out);

		void DestroyBody(BodyHandle body);

		bool IsBodyValid(BodyHandle body);

		void GetBodyTransform(BodyHandle body, Vec3& position, Quat& rotation);

		void SetBodyTransform(BodyHandle body, const Vec3& position, const Quat& rotation);

		void SetBodyTargetTransform(BodyHandle body, const Vec3& position, const Quat& rotation, f32 timeStep);

		Vec3 GetBodyLinearVelocity(BodyHandle body);
		void SetBodyLinearVelocity(BodyHandle body, const Vec3& velocity);

		// rads/s about the center of mass
		Vec3 GetBodyAngularVelocity(BodyHandle body);
		void SetBodyAngularVelocity(BodyHandle body, const Vec3& velocity);

		Vec3 GetBodyPointVelocity(BodyHandle body, const Vec3& worldPoint);

		bool IsBodyAwake(BodyHandle body);
		void SetBodyAwake(BodyHandle body, bool awake);

		enum class BodyProperty : u8 {
			LinearDamping,
			AngularDamping,
			GravityScale,
			SleepThreshold,
			IsBullet,

			FreezePositionX,
			FreezePositionY,
			FreezePositionZ,
			FreezeRotationX,
			FreezeRotationY,
			FreezeRotationZ,
		};

		constexpr bool BodyPropertyWakesBody(BodyProperty property) {
			switch (property) {
			case BodyProperty::GravityScale:
			case BodyProperty::FreezePositionX:
			case BodyProperty::FreezePositionY:
			case BodyProperty::FreezePositionZ:
			case BodyProperty::FreezeRotationX:
			case BodyProperty::FreezeRotationY:
			case BodyProperty::FreezeRotationZ:
				return true;
			default:
				return false;
			}
		}

		void SetBodyProperty(BodyHandle body, BodyProperty property, const PropertyValue& value);

		PropertyValue GetBodyProperty(BodyHandle body, BodyProperty property);

		// discards set mass
		void SetBodyKind(BodyHandle body, BodyKind kind);

		BodyKind GetBodyKind(BodyHandle body);

		i32 GetAllocatedBytes();

		struct QueryFilter {
			u64 categoryBits = ~0ull;
			u64 maskBits = ~0ull;
			bool hitTriggers = true;
		};

		struct QueryHit {
			u64 shapeOwner = 0;
			Vec3 point{};
			Vec3 normal{};

			f32 distance = 0.0f;
		};

		bool CastRay(WorldHandle world, const Vec3& origin, const Vec3& translation,
					 const QueryFilter& filter, QueryHit& out);

		bool CastShape(WorldHandle world, const Vec3* points, u32 pointCount, f32 radius,
					   const Vec3& translation, const QueryFilter& filter, QueryHit& out);

		using QueryOverlapFn = void (*)(void* user, u64 shapeOwner);

		u32 OverlapShape(WorldHandle world, const Vec3* points, u32 pointCount, f32 radius,
						 const QueryFilter& filter, QueryOverlapFn sink, void* user);

		// contacts report when either asks and sensors only when both do
		void SetShapeEvents(ShapeHandle shape, bool contactEvents, bool sensorEvents);

		enum class TouchKind : u8 {
			Contact,
			Sensor,
		};

		struct TouchEvent {
			u64 shapeOwnerA = 0;
			u64 shapeOwnerB = 0;
			TouchKind kind = TouchKind::Contact;
			bool begin = false;
		};

		using TouchSinkFn = void (*)(void* user, const TouchEvent& event);

		void DrainTouchEvents(WorldHandle world, TouchSinkFn sink, void* user);

		// normal points away from self
		struct ContactGeometry {
			Vec3 normal{};
			f32 separation[4]{};
			Vec3 point[4]{};
			f32 normalImpulse[4]{};
			u32 pointCount = 0;
		};

		bool GetContactGeometry(ShapeHandle self, ShapeHandle other, ContactGeometry& out);

		enum class JointKind : u8 {
			Revolute,

			Prismatic,

			Spherical,

			Weld,

			Distance,
		};

		struct JointHandle {
			u64 bits = 0;

			bool IsNull() const { return bits == 0; }
		};

		inline bool operator==(JointHandle a, JointHandle b) { return a.bits == b.bits; }
		inline bool operator!=(JointHandle a, JointHandle b) { return a.bits != b.bits; }

		struct JointParams {
			JointKind kind = JointKind::Weld;

			BodyHandle bodyA;
			BodyHandle bodyB;

			Vec3 localAnchorA{};
			Quat localRotationA = Quat::Identity();
			Vec3 localAnchorB{};
			Quat localRotationB = Quat::Identity();

			f32 forceThreshold = 0.0f;
			f32 torqueThreshold = 0.0f;

			bool collideConnected = false;

			u64 owner = 0;

			bool enableSpring = false;
			f32 springHertz = 0.0f;
			f32 springDampingRatio = 0.0f;

			f32 springStiffness = 0.0f;
			f32 springDamping = 0.0f;

			// rads (hinge), meters (slide/rest length)
			f32 springTarget = 0.0f;

			Quat targetRotation = Quat::Identity();

			// rads or meters, clamped by the caller
			bool enableLimit = false;
			f32 lowerLimit = 0.0f;
			f32 upperLimit = 0.0f;

			// rads
			bool enableConeLimit = false;
			f32 coneAngleX = 0.0f;
			f32 coneAngleY = 0.0f;

			// rads or m/s, matching effort cap
			bool enableMotor = false;
			f32 motorSpeed = 0.0f;
			f32 maxMotorEffort = 0.0f;

			f32 linearHertz = 0.0f;
			f32 linearDampingRatio = 0.0f;
			f32 angularHertz = 0.0f;
			f32 angularDampingRatio = 0.0f;

			// absolute, rest length included
			f32 minLength = 0.0f;
			f32 maxLength = 0.0f;
		};

		enum class JointProperty : u8 {
			ForceThreshold,
			TorqueThreshold,
			CollideConnected,

			EnableSpring,
			SpringHertz,
			SpringDampingRatio,

			// rads (revolute), meters (prismatic/rest length)
			SpringTarget,

			TargetRotation,

			// x torque per rad, y per rad second
			SpringConstants,

			EnableMotor,
			MotorSpeed,
			MaxMotorEffort,

			EnableLimit,
			Limits,

			EnableConeLimit,
			ConeAngles,
			EnableTwistLimit,
			TwistLimits,

			// absolute, x min, y max
			LengthRange,
		};

		// both backends gate on this
		constexpr bool JointKindAcceptsProperty(JointKind kind, JointProperty property) {
			switch (property) {
			case JointProperty::ForceThreshold:
			case JointProperty::TorqueThreshold:
			case JointProperty::CollideConnected:
				return true;

			case JointProperty::EnableSpring:
				return kind != JointKind::Weld;

			case JointProperty::SpringHertz:
			case JointProperty::SpringDampingRatio:
				return kind != JointKind::Weld && kind != JointKind::Spherical;

			case JointProperty::SpringTarget:
				return kind == JointKind::Revolute || kind == JointKind::Prismatic || kind == JointKind::Distance;

			case JointProperty::TargetRotation:
			case JointProperty::SpringConstants:
				return kind == JointKind::Spherical;

			case JointProperty::EnableMotor:
			case JointProperty::MaxMotorEffort:
				return kind == JointKind::Revolute || kind == JointKind::Prismatic || kind == JointKind::Spherical;

			case JointProperty::MotorSpeed:
				return kind == JointKind::Revolute || kind == JointKind::Prismatic;

			case JointProperty::EnableLimit:
				return kind == JointKind::Revolute || kind == JointKind::Prismatic || kind == JointKind::Distance;

			case JointProperty::Limits:
				return kind == JointKind::Revolute || kind == JointKind::Prismatic;

			case JointProperty::EnableConeLimit:
			case JointProperty::ConeAngles:
			case JointProperty::EnableTwistLimit:
			case JointProperty::TwistLimits:
				return kind == JointKind::Spherical;

			case JointProperty::LengthRange:
				return kind == JointKind::Distance;
			}
			return false;
		}

		constexpr bool JointPropertyWakesBodies(JointProperty property) {
			switch (property) {
			case JointProperty::ForceThreshold:
			case JointProperty::TorqueThreshold:
			case JointProperty::CollideConnected:
				return false;
			default:
				return true;
			}
		}

		void SetJointProperty(JointHandle joint, JointProperty property, const PropertyValue& value);

		PropertyValue GetJointProperty(JointHandle joint, JointProperty property);

		bool CreateJoint(WorldHandle world, const JointParams& params, JointHandle& out);

		void DestroyJoint(JointHandle joint, bool wakeAttached);

		bool IsJointValid(JointHandle joint);

		f32 GetJointForce(JointHandle joint);

		f32 GetBodyInverseMass(BodyHandle body);

		f32 GetBodyInverseInertiaAbout(BodyHandle body, const Vec3& worldAxis);

		using JointEventSinkFn = void (*)(void* user, u64 owner);

		void DrainJointEvents(WorldHandle world, JointEventSinkFn sink, void* user);

#if defined(SLT_WITH_EDITOR)

		struct DebugDrawOptions {
			bool shapes = true;
			bool bounds = false;
			bool contacts = false;
			bool contactNormals = false;
			bool contactImpulses = false;
			bool mass = false;
			bool sleep = false;
			bool islands = false;

			bool joints = false;
		};

		struct DebugDrawSink {
			void* user = nullptr;
			void (*line)(void* user, const Vec3& a, const Vec3& b, u32 rgb) = nullptr;
			void (*sphere)(void* user, const Vec3& center, f32 radius, u32 rgb) = nullptr;
			void (*capsule)(void* user, const Vec3& pointA, const Vec3& pointB, f32 radius, u32 rgb) = nullptr;
			void (*point)(void* user, const Vec3& position, f32 size, u32 rgb) = nullptr;
		};

		void EnableDebugDraw();

		void DrawWorld(WorldHandle world, const DebugDrawOptions& options, const DebugDrawSink& sink);

#endif

	} // namespace PhysicsBackend

} // namespace Shalltear
