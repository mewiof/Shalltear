#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Physics3D/Physics.h"

#include "PhysicsBackend.h"
#include "PhysicsCategories.h"

#include <span>

namespace Shalltear {

	struct PhysicsSettings;
	class Collider;
	class PhysicsMaterial;
	class Joint;
	class Component;
	class Rigidbody;
	class SceneObject;

	inline constexpr u32 PHYSICS_DEFAULT_ISLAND = 0;

	class PhysicsWorld {
	public:
		// runs before EngineContext binds, must not allocate or log
		PhysicsWorld() = default;
		~PhysicsWorld();

		PhysicsWorld(const PhysicsWorld&) = delete;
		PhysicsWorld& operator=(const PhysicsWorld&) = delete;

		bool Initialize(const PhysicsSettings& settings);

		void Shutdown();

		u32 GetWorkerCount() const { return _applied.workerCount; }

		bool IsUp() const { return _islandCount > 0; }

		void Step(f32 deltaSeconds);

		void Interpolate(f32 alpha);

		void AddBody(Rigidbody& body);
		void RemoveBody(Rigidbody& body);

		void AddCollider(Collider& collider);
		void RemoveCollider(Collider& collider);

		void AddJoint(Joint& joint);
		void RemoveJoint(Joint& joint);

		void PushJointProperty(const Joint& joint, PhysicsBackend::JointProperty property,
							   const PhysicsBackend::PropertyValue& value);

		void PushJointSpring(const Joint& joint, bool armed, f32 stiffness, f32 damper);
		void PushJointDrive(const Joint& joint, bool armed, f32 stiffness, f32 damper);

		// rads
		void PushJointAngularLimits(const Joint& joint, PhysicsBackend::JointProperty property, f32 lower, f32 upper);
		void PushJointConeAngles(const Joint& joint, f32 angleX, f32 angleY);

		void PushJointLengthRange(const Joint& joint, f32 minOffset, f32 maxOffset);

		PhysicsBackend::PropertyValue ReadJointProperty(const Joint& joint,
														PhysicsBackend::JointProperty property) const;

		void PushBodyProperty(const Rigidbody& body, PhysicsBackend::BodyProperty property,
							  const PhysicsBackend::PropertyValue& value);

		void PushBodyKind(Rigidbody& body);

		void RefreshMass(Rigidbody& body);

		PhysicsBackend::PropertyValue ReadBodyProperty(const Rigidbody& body,
													   PhysicsBackend::BodyProperty property) const;
		PhysicsBackend::BodyKind ReadBodyKind(const Rigidbody& body) const;
		f32 ReadBodyInverseMass(const Rigidbody& body) const;

		void RebuildShape(Collider& collider);

		void PushColliderSurface(Collider& collider);

		void PushColliderFilter(Collider& collider);

		u32 GetJointCount() const { return static_cast<u32>(_joints.size()); }

		u32 GetBodyCount() const { return static_cast<u32>(_bodies.size()); }

		u32 GetStaticBodyCount() const { return static_cast<u32>(_staticBodies.size()); }

		u32 GetColliderCount() const { return static_cast<u32>(_colliders.size()); }

		u32 GetShapeCountOnBodyOf(const Collider& collider) const;

		bool CastRay(const Ray& ray, f32 maxDistance, const PhysicsQueryFilter& filter, RaycastHit& hit);

		bool CastShape(const Vec3* points, u32 pointCount, f32 radius, const Vec3& direction,
					   f32 maxDistance, const PhysicsQueryFilter& filter, RaycastHit& hit);

		u32 OverlapShape(const Vec3* points, u32 pointCount, f32 radius,
						 const PhysicsQueryFilter& filter, Vector<Handle<Collider>>& results);

		const PhysicsSettings* GetSettings() const { return _settings; }

		u32 GetIslandCount() const { return _islandCount; }

#if defined(SLT_WITH_EDITOR)
		void DrawDebug(const PhysicsBackend::DebugDrawOptions& options, const PhysicsBackend::DebugDrawSink& sink) const;

		void SetHeldBodies(std::span<const Handle<SceneObject>> objects);

		void ReleaseHeldBodies();
#endif
	private:
		static constexpr u32 ISLAND_CAPACITY = 1;

		struct Island {
			u32 id = PHYSICS_DEFAULT_ISLAND;
			PhysicsBackend::WorldHandle world;
		};

		void PushTransforms(f32 deltaSeconds);
		void PullTransforms();

		struct TouchPair {
			u64 ownerA = 0;
			u64 ownerB = 0;

			bool sensor = false;

			bool fresh = false;

			bool ended = false;

			bool announced = false;
		};

		void DispatchContacts();

		static void OnTouchEvent(void* user, const PhysicsBackend::TouchEvent& event);

		TouchPair* FindTouch(u64 ownerA, u64 ownerB, bool sensor);

		struct Rider {
			Handle<Rigidbody> rider;
			Handle<Rigidbody> carrier;

			Vec3 pointVelocity{};
			Vec3 angularVelocity{};

			Vec3 anchor{};

			bool stamped = false;

			bool refreshed = false;

			bool applied = false;
		};

		void CarryRiders();

		void ApplyCarry(Rider& entry);

		void NoteRider(Rigidbody& rider, Rigidbody& carrier, const Vec3& anchor);

		u32 CollectListeners(SceneObject& object);

		enum class TouchPhase : u8 {
			Enter,
			Stay,
			Exit,
		};

		void DispatchToSide(TouchPhase phase, bool sensor, Collider& self, Rigidbody* selfBody,
							Handle<Collider> other, Rigidbody* otherBody,
							const PhysicsBackend::ContactGeometry* geometry, bool flipNormal);

		void AttachJoint(Joint& joint);

		void DetachJoint(Joint& joint, bool wakeAttached);

		void RehomeJoints();

		void RebuildJointsOn(Rigidbody& body);

		PhysicsBackend::BodyHandle AcquireWorldAnchor();
		void ReleaseWorldAnchor();

		enum class JointReport : u8 {
			None,
			NoBody,
			NoInertia,
		};

		struct PendingJointReport {
			Handle<Joint> joint;
			u64 jointBits = 0;
			JointReport what = JointReport::None;
		};

		void NoteJointReport(Joint& joint, JointReport what);
		void DrainJointReports();

		bool BuildJointParams(Joint& joint, PhysicsBackend::JointParams& params, bool& anchoredToWorld,
							  JointReport& report);

		static Quat RotationBetween(const Vec3& from, const Vec3& to);

		static Handle<Joint> ResolveJointOwner(u64 owner);

		void DispatchJointBreaks();

		static void OnJointEvent(void* user, u64 owner);

		struct StaticBody {
			Handle<SceneObject> object;
			PhysicsBackend::BodyHandle body;
			u32 colliderCount = 0;
			Vec3 syncedPosition{};
			Quat syncedRotation = Quat::Identity();
		};

		static Rigidbody* FindOwningBody(const Component& component);

		void AttachCollider(Collider& collider);

		void DetachCollider(Collider& collider);

		void RehomeColliders();

		void SyncCategoryVocabulary();

		void ReapplyCategoryFilters();

		static PhysicsSurface SurfaceFrom(const PhysicsMaterial& material);
		static PhysicsSurface SurfaceOf(const Collider& collider);

		void ReconcileSurfaces();

		void TrackSurface(const Collider& collider, const PhysicsSurface& applied);

		PhysicsCategoryFilter ResolveFilterFor(const Collider& collider) const;

		PhysicsBackend::QueryFilter ResolveQueryFilterFor(const PhysicsQueryFilter& filter) const;

		static Handle<Collider> ResolveShapeCollider(u64 shapeOwner);

		static bool FillHit(const PhysicsBackend::QueryHit& source, RaycastHit& hit);

		PhysicsBackend::BodyHandle AcquireStaticBody(Handle<SceneObject> object);
		void ReleaseStaticBody(PhysicsBackend::BodyHandle body);

		void BeginPopulation();

		bool SceneOwnsTransform(Rigidbody& body) const;

		// only writer of scene transforms + the synced baseline
		static void WriteTransform(Rigidbody& body, const Vec3& position, const Quat& rotation);

		Island _islands[ISLAND_CAPACITY];
		u32 _islandCount = 0;
		const PhysicsSettings* _settings = nullptr;

		bool _worldIsPristine = true;

		PhysicsBackend::WorldParams _applied;

		u32 _reportedWorkerCount = 0;

		PhysicsCategoryVocabulary _categories;
		str _appliedCategories;

		Vector<Handle<Rigidbody>> _bodies{GetMemoryResource(MemTag::Physics)};

		Vector<Handle<Collider>> _colliders{GetMemoryResource(MemTag::Physics)};

		struct SurfaceWatch {
			Handle<PhysicsMaterial> material;
			PhysicsSurface applied;
		};
		Vector<SurfaceWatch> _surfaces{GetMemoryResource(MemTag::Physics)};
		Vector<StaticBody> _staticBodies{GetMemoryResource(MemTag::Physics)};

		Vector<Vec3> _shapePoints{GetMemoryResource(MemTag::Physics)};
		Vector<i32> _shapeIndices{GetMemoryResource(MemTag::Physics)};

		u32 _interpolatingCount = 0;

		Vector<TouchPair> _touching{GetMemoryResource(MemTag::Physics)};

		Vector<Handle<Component>> _listenerScratch{GetMemoryResource(MemTag::Physics)};

		Vector<Rider> _riders{GetMemoryResource(MemTag::Physics)};

		Vector<Handle<Joint>> _joints{GetMemoryResource(MemTag::Physics)};

		Vector<u64> _brokenJoints{GetMemoryResource(MemTag::Physics)};

		Vector<PendingJointReport> _jointReports{GetMemoryResource(MemTag::Physics)};

		PhysicsBackend::BodyHandle _worldAnchor;
		u32 _worldAnchorUsers = 0;

#if defined(SLT_WITH_EDITOR)
		Vector<Handle<Rigidbody>> _heldBodies{GetMemoryResource(MemTag::Physics)};

		bool _heldSimulated = false;
#endif

		u32 _carrierCount = 0;
	};

	PhysicsWorld& GetPhysicsWorld();

} // namespace Shalltear
