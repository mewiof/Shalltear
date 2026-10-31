#include "PhysicsBackend.h"

#include "Shalltear/Physics3D/PhysicsMaterial.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"

#include <box3d/box3d.h>

#include <cmath>
#include <new>

#if defined(BOX3D_DOUBLE_PRECISION)
#error "BOX3D_DOUBLE_PRECISION is unsupported (ABI-affecting, Shalltear's Vec3 is single precision)"
#endif

namespace Shalltear {

	namespace {

		static_assert(sizeof(b3WorldId) == sizeof(u32), "the world id packing below assumes 2 u16 fields");

		static_assert(PhysicsBackend::PHYSICS_MAX_WORKERS == B3_MAX_WORKERS,
					  "PhysicsBackend.h's copy must match the simulation's worker ceiling");

		u32 Pack(b3WorldId id) {
			return static_cast<u32>(id.index1) | (static_cast<u32>(id.generation) << 16);
		}

		b3WorldId Unpack(PhysicsBackend::WorldHandle world) {
			b3WorldId id;
			id.index1 = static_cast<uint16_t>(world.bits & 0xFFFFu);
			id.generation = static_cast<uint16_t>(world.bits >> 16);
			return id;
		}

		static_assert(sizeof(b3BodyId) == sizeof(u64), "the body id packing below assumes an i32 and 2 u16 fields");

		u64 PackBody(b3BodyId id) {
			return (static_cast<u64>(static_cast<u32>(id.index1))) |
				   (static_cast<u64>(id.world0) << 32) |
				   (static_cast<u64>(id.generation) << 48);
		}

		b3BodyId UnpackBody(PhysicsBackend::BodyHandle body) {
			b3BodyId id;
			id.index1 = static_cast<int32_t>(static_cast<uint32_t>(body.bits & 0xFFFFFFFFull));
			id.world0 = static_cast<uint16_t>((body.bits >> 32) & 0xFFFFull);
			id.generation = static_cast<uint16_t>((body.bits >> 48) & 0xFFFFull);
			return id;
		}

		static_assert(sizeof(b3ShapeId) == sizeof(u64), "the shape id packing below assumes an i32 and 2 u16 fields");

		u64 PackShape(b3ShapeId id) {
			return (static_cast<u64>(static_cast<u32>(id.index1))) |
				   (static_cast<u64>(id.world0) << 32) |
				   (static_cast<u64>(id.generation) << 48);
		}

		b3ShapeId UnpackShape(PhysicsBackend::ShapeHandle shape) {
			b3ShapeId id;
			id.index1 = static_cast<int32_t>(static_cast<uint32_t>(shape.bits & 0xFFFFFFFFull));
			id.world0 = static_cast<uint16_t>((shape.bits >> 32) & 0xFFFFull);
			id.generation = static_cast<uint16_t>((shape.bits >> 48) & 0xFFFFull);
			return id;
		}

		b3BodyType ToBackend(PhysicsBackend::BodyKind kind) {
			switch (kind) {
			case PhysicsBackend::BodyKind::Static:
				return b3_staticBody;
			case PhysicsBackend::BodyKind::Kinematic:
				return b3_kinematicBody;
			case PhysicsBackend::BodyKind::Dynamic:
			default:
				return b3_dynamicBody;
			}
		}

		b3Vec3 ToBackend(const Vec3& v) {
			return b3Vec3{v.x, v.y, v.z};
		}

		Vec3 FromBackend(b3Vec3 v) {
			return Vec3{v.x, v.y, v.z};
		}

		b3Quat ToBackend(const Quat& q) {
			b3Quat out;
			out.v.x = q.x;
			out.v.y = q.y;
			out.v.z = q.z;
			out.s = q.w;
			return out;
		}

		Quat FromBackend(b3Quat q) {
			return Quat{q.v.x, q.v.y, q.v.z, q.s};
		}

		Vector<b3Vec3> _pointScratch{GetMemoryResource(MemTag::Physics)};

		void FillPointScratch(const Vec3* points, u32 count) {
			_pointScratch.clear();
			_pointScratch.reserve(count);
			for (u32 index = 0; index < count; ++index) {
				_pointScratch.push_back(ToBackend(points[index]));
			}
		}

		b3Matrix3 ScaleMatrix(b3Matrix3 matrix, float scale) {
			matrix.cx = b3MulSV(scale, matrix.cx);
			matrix.cy = b3MulSV(scale, matrix.cy);
			matrix.cz = b3MulSV(scale, matrix.cz);
			return matrix;
		}

		b3Matrix3 ToInertiaTensor(const Vec3& principalMoments, const Quat& rotation) {
			const Vec3 axes[3] = {
				rotation * Vec3{1.0f, 0.0f, 0.0f},
				rotation * Vec3{0.0f, 1.0f, 0.0f},
				rotation * Vec3{0.0f, 0.0f, 1.0f}};
			const f32 moments[3] = {principalMoments.x, principalMoments.y, principalMoments.z};

			f32 out[3][3] = {};
			for (u32 axis = 0; axis < 3; ++axis) {
				const Vec3& e = axes[axis];
				const f32 m = moments[axis];
				const f32 components[3] = {e.x, e.y, e.z};
				for (u32 column = 0; column < 3; ++column) {
					out[column][0] += m * components[column] * e.x;
					out[column][1] += m * components[column] * e.y;
					out[column][2] += m * components[column] * e.z;
				}
			}

			b3Matrix3 tensor;
			tensor.cx = b3Vec3{out[0][0], out[0][1], out[0][2]};
			tensor.cy = b3Vec3{out[1][0], out[1][1], out[1][2]};
			tensor.cz = b3Vec3{out[2][0], out[2][1], out[2][2]};
			return tensor;
		}

		float CombineFriction(float frictionA, uint64_t materialA, float frictionB, uint64_t materialB) {
			return CombinePhysicsMaterialValue(frictionA, materialA, frictionB, materialB,
											   PHYSICS_COMBINE_FRICTION_SHIFT);
		}

		float CombineRestitution(float restitutionA, uint64_t materialA, float restitutionB, uint64_t materialB) {
			return CombinePhysicsMaterialValue(restitutionA, materialA, restitutionB, materialB,
											   PHYSICS_COMBINE_BOUNCE_SHIFT);
		}

		void* HostAlloc(size_t size, int32_t alignment) {
			try {
				return Allocate(size, MemTag::Physics, static_cast<size_t>(alignment));
			} catch (const std::bad_alloc&) {
				return nullptr;
			}
		}

		void HostFree(void* memory, size_t) {
			Free(memory);
		}

		void HostLog(const char* message) {
			SLT_LOG_WARN("Physics", "{}", message);
		}

#if defined(SLT_WITH_EDITOR)

		// must be armed before the world is created
		bool _debugDrawArmed = false;

		static_assert(sizeof(b3ShapeId) <= sizeof(void*), "the debug shape token boxes a shape id into a pointer");

		void* CreateDebugShape(const b3DebugShape* debugShape, void*) {
			void* token = nullptr;
			std::memcpy(&token, &debugShape->shapeId, sizeof(b3ShapeId));
			return token;
		}

		void DestroyDebugShape(void*, void*) {
		}

#endif

	} // anonymous namespace

	namespace PhysicsBackend {

		std::string_view Name() {
			return "built-in";
		}

		void InstallHostServices() {
			// before the backend allocates anything
			b3SetAllocator(&HostAlloc, &HostFree);
			b3SetLogFcn(&HostLog);
		}

		bool CreateWorld(const WorldParams& params, WorldHandle& out) {
			out = WorldHandle{};

			b3WorldDef def = b3DefaultWorldDef();
			def.gravity.x = params.gravityX;
			def.gravity.y = params.gravityY;
			def.gravity.z = params.gravityZ;
			def.restitutionThreshold = params.bounceThreshold;
			def.hitEventThreshold = params.hitEventThreshold;
			def.contactHertz = params.contactHertz;
			def.contactDampingRatio = params.contactDampingRatio;
			def.contactSpeed = params.contactSpeed;
			def.maximumLinearSpeed = params.maxLinearSpeed;
			def.enableSleep = params.enableSleeping;
			def.enableContinuous = params.enableContinuous;

			def.workerCount = params.workerCount;

			def.frictionCallback = &CombineFriction;
			def.restitutionCallback = &CombineRestitution;

#if defined(SLT_WITH_EDITOR)
			if (_debugDrawArmed) {
				def.createDebugShape = &CreateDebugShape;
				def.destroyDebugShape = &DestroyDebugShape;
				def.userDebugShapeContext = nullptr;
			}
#endif

			b3WorldId id = b3CreateWorld(&def);
			if (!b3World_IsValid(id)) {
				return false;
			}
			out.bits = Pack(id);
			return true;
		}

		void DestroyWorld(WorldHandle world) {
			if (world.IsNull()) {
				return;
			}
			b3WorldId id = Unpack(world);
			if (b3World_IsValid(id)) {
				b3DestroyWorld(id);
			}
		}

		bool IsWorldValid(WorldHandle world) {
			return !world.IsNull() && b3World_IsValid(Unpack(world));
		}

		void StepWorld(WorldHandle world, f32 timeStep, i32 substeps) {
			if (world.IsNull()) {
				return;
			}
			b3WorldId id = Unpack(world);
			if (!b3World_IsValid(id)) {
				return;
			}
			b3World_Step(id, timeStep, substeps);
		}

		void UpdateWorld(WorldHandle world, const WorldParams& params) {
			if (!IsWorldValid(world)) {
				return;
			}
			b3WorldId id = Unpack(world);

			b3World_SetGravity(id, b3Vec3{params.gravityX, params.gravityY, params.gravityZ});
			b3World_SetRestitutionThreshold(id, params.bounceThreshold);
			b3World_SetHitEventThreshold(id, params.hitEventThreshold);
			b3World_SetContactTuning(id, params.contactHertz, params.contactDampingRatio, params.contactSpeed);
			b3World_SetMaximumLinearSpeed(id, params.maxLinearSpeed);
			b3World_EnableSleeping(id, params.enableSleeping);
			b3World_EnableContinuous(id, params.enableContinuous);
		}

		bool CreateBody(WorldHandle world, const BodyParams& params, BodyHandle& out) {
			out = BodyHandle{};
			if (!IsWorldValid(world)) {
				return false;
			}

			b3BodyDef def = b3DefaultBodyDef();
			def.type = ToBackend(params.kind);
			def.position = ToBackend(params.position);
			def.rotation = ToBackend(params.rotation);
			def.linearVelocity = ToBackend(params.linearVelocity);
			def.angularVelocity = ToBackend(params.angularVelocity);
			def.linearDamping = params.linearDamping;
			def.angularDamping = params.angularDamping;
			def.gravityScale = params.gravityScale;
			def.sleepThreshold = params.sleepThreshold;
			def.motionLocks.linearX = params.freezePositionX;
			def.motionLocks.linearY = params.freezePositionY;
			def.motionLocks.linearZ = params.freezePositionZ;
			def.motionLocks.angularX = params.freezeRotationX;
			def.motionLocks.angularY = params.freezeRotationY;
			def.motionLocks.angularZ = params.freezeRotationZ;
			def.isBullet = params.isBullet;

			def.userData = reinterpret_cast<void*>(static_cast<uintptr_t>(params.owner));

			b3BodyId id = b3CreateBody(Unpack(world), &def);
			if (!b3Body_IsValid(id)) {
				return false;
			}

			if (params.kind != BodyKind::Static) {
				b3MassData mass{};
				mass.mass = params.mass;
				mass.center = ToBackend(params.centerOfMass);
				mass.inertia = b3Mat3_zero;
				b3Body_SetMassData(id, mass);
			}

			out.bits = PackBody(id);
			return true;
		}

		void DestroyBody(BodyHandle body) {
			if (body.IsNull()) {
				return;
			}
			b3BodyId id = UnpackBody(body);
			if (b3Body_IsValid(id)) {
				b3DestroyBody(id);
			}
		}

		bool IsBodyValid(BodyHandle body) {
			return !body.IsNull() && b3Body_IsValid(UnpackBody(body));
		}

		void GetBodyTransform(BodyHandle body, Vec3& position, Quat& rotation) {
			if (!IsBodyValid(body)) {
				return;
			}
			b3WorldTransform transform = b3Body_GetTransform(UnpackBody(body));
			position = FromBackend(transform.p);
			rotation = FromBackend(transform.q);
		}

		void SetBodyTransform(BodyHandle body, const Vec3& position, const Quat& rotation) {
			if (!IsBodyValid(body)) {
				return;
			}
			b3BodyId id = UnpackBody(body);
			b3Body_SetTransform(id, ToBackend(position), ToBackend(rotation));
			b3Body_SetAwake(id, true);
		}

		void SetBodyTargetTransform(BodyHandle body, const Vec3& position, const Quat& rotation, f32 timeStep) {
			if (!IsBodyValid(body)) {
				return;
			}
			b3WorldTransform target;
			target.p = ToBackend(position);
			target.q = ToBackend(rotation);
			b3Body_SetTargetTransform(UnpackBody(body), target, timeStep, true);
		}

		Vec3 GetBodyLinearVelocity(BodyHandle body) {
			if (!IsBodyValid(body)) {
				return Vec3{};
			}
			return FromBackend(b3Body_GetLinearVelocity(UnpackBody(body)));
		}

		void SetBodyLinearVelocity(BodyHandle body, const Vec3& velocity) {
			if (!IsBodyValid(body)) {
				return;
			}
			b3BodyId id = UnpackBody(body);
			b3Body_SetLinearVelocity(id, ToBackend(velocity));
			b3Body_SetAwake(id, true);
		}

		Vec3 GetBodyAngularVelocity(BodyHandle body) {
			if (!IsBodyValid(body)) {
				return Vec3{};
			}
			return FromBackend(b3Body_GetAngularVelocity(UnpackBody(body)));
		}

		Vec3 GetBodyPointVelocity(BodyHandle body, const Vec3& worldPoint) {
			if (!IsBodyValid(body)) {
				return Vec3{};
			}
			return FromBackend(b3Body_GetWorldPointVelocity(UnpackBody(body), ToBackend(worldPoint)));
		}

		void SetBodyAngularVelocity(BodyHandle body, const Vec3& velocity) {
			if (!IsBodyValid(body)) {
				return;
			}
			b3BodyId id = UnpackBody(body);
			b3Body_SetAngularVelocity(id, ToBackend(velocity));
			b3Body_SetAwake(id, true);
		}

		bool CreateShape(BodyHandle body, const PhysicsShapeRequest& request, ShapeHandle& out) {
			out = ShapeHandle{};
			if (!IsBodyValid(body)) {
				return false;
			}
			const b3BodyId id = UnpackBody(body);

			b3ShapeDef def = b3DefaultShapeDef();
			def.isSensor = request.isTrigger;
			def.userData = reinterpret_cast<void*>(static_cast<uintptr_t>(request.owner));
			def.baseMaterial.friction = request.surface.friction;
			def.baseMaterial.restitution = request.surface.bounciness;
			def.baseMaterial.rollingResistance = request.surface.rollingResistance;
			def.baseMaterial.userMaterialId = request.surface.combineBits;
			def.filter.categoryBits = request.categoryBits;
			def.filter.maskBits = request.maskBits;
			def.filter.groupIndex = request.groupIndex;

			const b3Transform local{ToBackend(request.localPosition), ToBackend(request.localRotation)};
			b3ShapeId shape = b3_nullShapeId;

			switch (request.kind) {
			case PhysicsShapeKind::Box: {
				const b3BoxHull box = b3MakeScaledBoxHull(ToBackend(request.halfExtents), local, b3Vec3_one);
				shape = b3CreateHullShape(id, &def, &box.base);
				break;
			}
			case PhysicsShapeKind::Sphere: {
				const b3Sphere sphere{ToBackend(request.localPosition), request.radius};
				shape = b3CreateSphereShape(id, &def, &sphere);
				break;
			}
			case PhysicsShapeKind::Capsule: {
				const b3Capsule capsule{ToBackend(request.capsulePoint0), ToBackend(request.capsulePoint1), request.radius};
				shape = b3CreateCapsuleShape(id, &def, &capsule);
				break;
			}
			case PhysicsShapeKind::ConvexHull: {
				if (!request.points || request.pointCount < 4) {
					return false;
				}
				FillPointScratch(request.points, request.pointCount);
				b3HullData* hull = b3CreateHull(_pointScratch.data(), static_cast<int>(_pointScratch.size()),
												B3_MAX_HULL_VERTICES);
				if (!hull) {
					return false;
				}
				shape = b3CreateTransformedHullShape(id, &def, hull, local, b3Vec3_one);
				b3DestroyHull(hull);
				break;
			}
			case PhysicsShapeKind::TriangleMesh: {
				if (!request.points || !request.indices || request.pointCount < 3 || request.indexCount < 3) {
					return false;
				}
				FillPointScratch(request.points, request.pointCount);

				b3MeshDef meshDef{};
				meshDef.vertices = _pointScratch.data();
				meshDef.vertexCount = static_cast<int>(_pointScratch.size());
				// safe, b3CreateMesh clones before returning
				meshDef.indices = const_cast<int32_t*>(request.indices);
				meshDef.triangleCount = static_cast<int>(request.indexCount / 3);

				b3MeshData* mesh = b3CreateMesh(&meshDef, nullptr, 0);
				if (!mesh) {
					return false;
				}
				shape = b3CreateMeshShape(id, &def, mesh, b3Vec3_one);
				if (!b3Shape_IsValid(shape)) {
					b3DestroyMesh(mesh);
					return false;
				}
				break;
			}
			}

			if (!b3Shape_IsValid(shape)) {
				return false;
			}
			out.bits = PackShape(shape);
			return true;
		}

		void DestroyShape(ShapeHandle shape) {
			if (shape.IsNull()) {
				return;
			}
			const b3ShapeId id = UnpackShape(shape);
			if (!b3Shape_IsValid(id)) {
				return;
			}

			b3MeshData* owned = nullptr;
			if (b3Shape_GetType(id) == b3_meshShape) {
				owned = const_cast<b3MeshData*>(b3Shape_GetMesh(id).data);
			}

			b3DestroyShape(id, true);

			if (owned) {
				b3DestroyMesh(owned);
			}
		}

		void SetShapeFilter(ShapeHandle shape, u64 categoryBits, u64 maskBits, i32 groupIndex) {
			if (shape.IsNull()) {
				return;
			}
			const b3ShapeId id = UnpackShape(shape);
			if (!b3Shape_IsValid(id)) {
				return;
			}
			b3Filter filter;
			filter.categoryBits = categoryBits;
			filter.maskBits = maskBits;
			filter.groupIndex = groupIndex;
			b3Shape_SetFilter(id, filter, true);
		}

		void SetShapeSurface(ShapeHandle shape, const PhysicsSurface& surface) {
			if (shape.IsNull()) {
				return;
			}
			const b3ShapeId id = UnpackShape(shape);
			if (!b3Shape_IsValid(id)) {
				return;
			}
			b3SurfaceMaterial material = b3Shape_GetSurfaceMaterial(id);
			material.friction = surface.friction;
			material.restitution = surface.bounciness;
			material.rollingResistance = surface.rollingResistance;
			material.userMaterialId = surface.combineBits;
			b3Shape_SetSurfaceMaterial(id, material);
		}

		u32 GetBodyShapeCount(BodyHandle body) {
			if (!IsBodyValid(body)) {
				return 0;
			}
			return static_cast<u32>(b3Body_GetShapeCount(UnpackBody(body)));
		}

		bool SupportsTriangleMeshOn(BodyKind kind) {
			return kind == BodyKind::Static;
		}

		void RefreshBodyMass(BodyHandle body, const MassParams& params) {
			if (!IsBodyValid(body)) {
				return;
			}
			const b3BodyId id = UnpackBody(body);

			if (b3Body_GetType(id) != b3_dynamicBody) {
				return;
			}

			b3MassData derived{};
			if (params.hasShapes) {
				b3Body_ApplyMassFromShapes(id);
				derived = b3Body_GetMassData(id);
			}

			b3MassData out{};
			out.mass = params.mass;
			out.center = params.automaticCenterOfMass ? derived.center : ToBackend(params.centerOfMass);
			if (params.automaticInertiaTensor) {
				const float scale = derived.mass > 0.0f ? params.mass / derived.mass : 0.0f;
				out.inertia = ScaleMatrix(derived.inertia, scale);
			} else {
				out.inertia = ToInertiaTensor(params.inertiaTensor, params.inertiaTensorRotation);
			}
			b3Body_SetMassData(id, out);
		}

		bool IsBodyAwake(BodyHandle body) {
			return IsBodyValid(body) && b3Body_IsAwake(UnpackBody(body));
		}

		void SetBodyAwake(BodyHandle body, bool awake) {
			if (IsBodyValid(body)) {
				b3Body_SetAwake(UnpackBody(body), awake);
			}
		}

		void SetBodyProperty(BodyHandle body, BodyProperty property, const PropertyValue& value) {
			if (!IsBodyValid(body)) {
				return;
			}
			const b3BodyId id = UnpackBody(body);
			if (BodyPropertyWakesBody(property)) {
				b3Body_SetAwake(id, true);
			}

			switch (property) {
			case BodyProperty::LinearDamping:
				b3Body_SetLinearDamping(id, value.AsScalar());
				return;
			case BodyProperty::AngularDamping:
				b3Body_SetAngularDamping(id, value.AsScalar());
				return;
			case BodyProperty::GravityScale:
				b3Body_SetGravityScale(id, value.AsScalar());
				return;
			case BodyProperty::SleepThreshold:
				b3Body_SetSleepThreshold(id, value.AsScalar());
				return;
			case BodyProperty::IsBullet:
				b3Body_SetBullet(id, value.AsFlag());
				return;
			default:
				break;
			}

			b3MotionLocks locks = b3Body_GetMotionLocks(id);
			switch (property) {
			case BodyProperty::FreezePositionX:
				locks.linearX = value.AsFlag();
				break;
			case BodyProperty::FreezePositionY:
				locks.linearY = value.AsFlag();
				break;
			case BodyProperty::FreezePositionZ:
				locks.linearZ = value.AsFlag();
				break;
			case BodyProperty::FreezeRotationX:
				locks.angularX = value.AsFlag();
				break;
			case BodyProperty::FreezeRotationY:
				locks.angularY = value.AsFlag();
				break;
			case BodyProperty::FreezeRotationZ:
				locks.angularZ = value.AsFlag();
				break;
			default:
				return;
			}
			b3Body_SetMotionLocks(id, locks);
		}

		PropertyValue GetBodyProperty(BodyHandle body, BodyProperty property) {
			if (!IsBodyValid(body)) {
				return PropertyValue{};
			}
			const b3BodyId id = UnpackBody(body);
			switch (property) {
			case BodyProperty::LinearDamping:
				return PropertyValue::Scalar(b3Body_GetLinearDamping(id));
			case BodyProperty::AngularDamping:
				return PropertyValue::Scalar(b3Body_GetAngularDamping(id));
			case BodyProperty::GravityScale:
				return PropertyValue::Scalar(b3Body_GetGravityScale(id));
			case BodyProperty::SleepThreshold:
				return PropertyValue::Scalar(b3Body_GetSleepThreshold(id));
			case BodyProperty::IsBullet:
				return PropertyValue::Flag(b3Body_IsBullet(id));
			default:
				break;
			}

			const b3MotionLocks locks = b3Body_GetMotionLocks(id);
			switch (property) {
			case BodyProperty::FreezePositionX:
				return PropertyValue::Flag(locks.linearX);
			case BodyProperty::FreezePositionY:
				return PropertyValue::Flag(locks.linearY);
			case BodyProperty::FreezePositionZ:
				return PropertyValue::Flag(locks.linearZ);
			case BodyProperty::FreezeRotationX:
				return PropertyValue::Flag(locks.angularX);
			case BodyProperty::FreezeRotationY:
				return PropertyValue::Flag(locks.angularY);
			case BodyProperty::FreezeRotationZ:
				return PropertyValue::Flag(locks.angularZ);
			default:
				break;
			}
			return PropertyValue{};
		}

		void SetBodyKind(BodyHandle body, BodyKind kind) {
			if (!IsBodyValid(body)) {
				return;
			}
			const b3BodyId id = UnpackBody(body);
			b3Body_SetType(id, ToBackend(kind));
			b3Body_SetAwake(id, true);
		}

		BodyKind GetBodyKind(BodyHandle body) {
			if (!IsBodyValid(body)) {
				return BodyKind::Static;
			}
			switch (b3Body_GetType(UnpackBody(body))) {
			case b3_staticBody:
				return BodyKind::Static;
			case b3_kinematicBody:
				return BodyKind::Kinematic;
			default:
				return BodyKind::Dynamic;
			}
		}

		i32 GetAllocatedBytes() {
			return static_cast<i32>(b3GetByteCount());
		}

		namespace {

			struct CastContext {
				bool hitTriggers = true;
				Vec3 origin{};
				f32 translationLength = 0.0f;
				bool fromFraction = false;
				PhysicsBackend::QueryHit hit;
				bool hasHit = false;
			};

			bool IsInitialOverlap(b3Vec3 normal, float fraction) {
				return fraction <= 0.0f && normal.x == 0.0f && normal.y == 0.0f && normal.z == 0.0f;
			}

			bool ShapeIsFilteredOut(const CastContext& context, b3ShapeId shapeId) {
				return !context.hitTriggers && b3Shape_IsSensor(shapeId);
			}

			u64 ShapeOwnerOf(b3ShapeId shapeId) {
				return static_cast<u64>(reinterpret_cast<uintptr_t>(b3Shape_GetUserData(shapeId)));
			}

			float CastResult(b3ShapeId shapeId, b3Pos point, b3Vec3 normal, float fraction,
							 uint64_t, int, int, void* userContext) {
				CastContext& context = *static_cast<CastContext*>(userContext);
				if (ShapeIsFilteredOut(context, shapeId) || IsInitialOverlap(normal, fraction)) {
					return -1.0f;
				}

				context.hit.shapeOwner = ShapeOwnerOf(shapeId);
				context.hit.point = FromBackend(point);
				context.hit.normal = FromBackend(normal);
				context.hit.distance = context.fromFraction ? fraction * context.translationLength
															: (context.hit.point - context.origin).Length();
				context.hasHit = true;
				return fraction;
			}

			struct OverlapContext {
				bool hitTriggers = true;
				PhysicsBackend::QueryOverlapFn sink = nullptr;
				void* user = nullptr;
				u32 count = 0;
			};

			bool OverlapResult(b3ShapeId shapeId, void* userContext) {
				OverlapContext& context = *static_cast<OverlapContext*>(userContext);
				if (!context.hitTriggers && b3Shape_IsSensor(shapeId)) {
					return true;
				}
				++context.count;
				if (context.sink) {
					context.sink(context.user, ShapeOwnerOf(shapeId));
				}
				return true;
			}

			b3QueryFilter ToBackendFilter(const PhysicsBackend::QueryFilter& filter) {
				b3QueryFilter out = b3DefaultQueryFilter();
				out.categoryBits = filter.categoryBits;
				out.maskBits = filter.maskBits;
				return out;
			}

		} // anonymous namespace

		bool CastRay(WorldHandle world, const Vec3& origin, const Vec3& translation,
					 const QueryFilter& filter, QueryHit& out) {
			out = QueryHit{};
			if (!IsWorldValid(world)) {
				return false;
			}

			CastContext context;
			context.hitTriggers = filter.hitTriggers;
			context.origin = origin;
			context.fromFraction = false;

			b3World_CastRay(Unpack(world), ToBackend(origin), ToBackend(translation),
							ToBackendFilter(filter), &CastResult, &context);
			if (!context.hasHit) {
				return false;
			}
			out = context.hit;
			return true;
		}

		bool CastShape(WorldHandle world, const Vec3* points, u32 pointCount, f32 radius,
					   const Vec3& translation, const QueryFilter& filter, QueryHit& out) {
			out = QueryHit{};
			if (!IsWorldValid(world) || !points || pointCount == 0 || pointCount > 2) {
				return false;
			}

			const Vec3 origin = points[0];
			b3Vec3 relative[2];
			for (u32 index = 0; index < pointCount; ++index) {
				relative[index] = ToBackend(points[index] - origin);
			}

			b3ShapeProxy proxy;
			proxy.points = relative;
			proxy.count = static_cast<int>(pointCount);
			proxy.radius = radius;

			CastContext context;
			context.hitTriggers = filter.hitTriggers;
			context.origin = origin;
			context.translationLength = translation.Length();
			context.fromFraction = true;

			b3World_CastShape(Unpack(world), ToBackend(origin), &proxy, ToBackend(translation),
							  ToBackendFilter(filter), &CastResult, &context);
			if (!context.hasHit) {
				return false;
			}
			out = context.hit;
			return true;
		}

		u32 OverlapShape(WorldHandle world, const Vec3* points, u32 pointCount, f32 radius,
						 const QueryFilter& filter, QueryOverlapFn sink, void* user) {
			if (!IsWorldValid(world) || !points || pointCount == 0 || pointCount > 2) {
				return 0;
			}

			const Vec3 origin = points[0];
			b3Vec3 relative[2];
			for (u32 index = 0; index < pointCount; ++index) {
				relative[index] = ToBackend(points[index] - origin);
			}

			b3ShapeProxy proxy;
			proxy.points = relative;
			proxy.count = static_cast<int>(pointCount);
			proxy.radius = radius;

			OverlapContext context;
			context.hitTriggers = filter.hitTriggers;
			context.sink = sink;
			context.user = user;

			b3World_OverlapShape(Unpack(world), ToBackend(origin), &proxy,
								 ToBackendFilter(filter), &OverlapResult, &context);
			return context.count;
		}

		namespace {

			Vector<b3ContactData> _contactScratch{GetMemoryResource(MemTag::Physics)};

			u64 LiveShapeOwnerOf(b3ShapeId shapeId) {
				return b3Shape_IsValid(shapeId) ? ShapeOwnerOf(shapeId) : 0ull;
			}

		} // anonymous namespace

		void SetShapeEvents(ShapeHandle shape, bool contactEvents, bool sensorEvents) {
			const b3ShapeId id = UnpackShape(shape);
			if (!b3Shape_IsValid(id)) {
				return;
			}
			b3Shape_EnableContactEvents(id, contactEvents);
			b3Shape_EnableSensorEvents(id, sensorEvents);
		}

		void DrainTouchEvents(WorldHandle world, TouchSinkFn sink, void* user) {
			if (!IsWorldValid(world) || !sink) {
				return;
			}
			const b3WorldId id = Unpack(world);

			TouchEvent event;

			// begin before end
			const b3ContactEvents contacts = b3World_GetContactEvents(id);
			event.kind = TouchKind::Contact;
			event.begin = true;
			for (int index = 0; index < contacts.beginCount; ++index) {
				const b3ContactBeginTouchEvent& begin = contacts.beginEvents[index];
				event.shapeOwnerA = LiveShapeOwnerOf(begin.shapeIdA);
				event.shapeOwnerB = LiveShapeOwnerOf(begin.shapeIdB);
				sink(user, event);
			}
			event.begin = false;
			for (int index = 0; index < contacts.endCount; ++index) {
				const b3ContactEndTouchEvent& end = contacts.endEvents[index];
				event.shapeOwnerA = LiveShapeOwnerOf(end.shapeIdA);
				event.shapeOwnerB = LiveShapeOwnerOf(end.shapeIdB);
				sink(user, event);
			}

			const b3SensorEvents sensors = b3World_GetSensorEvents(id);
			event.kind = TouchKind::Sensor;
			event.begin = true;
			for (int index = 0; index < sensors.beginCount; ++index) {
				const b3SensorBeginTouchEvent& begin = sensors.beginEvents[index];
				event.shapeOwnerA = LiveShapeOwnerOf(begin.sensorShapeId);
				event.shapeOwnerB = LiveShapeOwnerOf(begin.visitorShapeId);
				sink(user, event);
			}
			event.begin = false;
			for (int index = 0; index < sensors.endCount; ++index) {
				const b3SensorEndTouchEvent& end = sensors.endEvents[index];
				event.shapeOwnerA = LiveShapeOwnerOf(end.sensorShapeId);
				event.shapeOwnerB = LiveShapeOwnerOf(end.visitorShapeId);
				sink(user, event);
			}
		}

		bool GetContactGeometry(ShapeHandle self, ShapeHandle other, ContactGeometry& out) {
			out = ContactGeometry{};

			const b3ShapeId selfId = UnpackShape(self);
			const b3ShapeId otherId = UnpackShape(other);
			if (!b3Shape_IsValid(selfId) || !b3Shape_IsValid(otherId)) {
				return false;
			}

			const int capacity = b3Shape_GetContactCapacity(selfId);
			if (capacity <= 0) {
				return false;
			}
			_contactScratch.resize(static_cast<size_t>(capacity));
			const int count = b3Shape_GetContactData(selfId, _contactScratch.data(), capacity);

			for (int index = 0; index < count; ++index) {
				const b3ContactData& data = _contactScratch[static_cast<size_t>(index)];
				const bool selfIsA = B3_ID_EQUALS(data.shapeIdA, selfId);
				const b3ShapeId opposite = selfIsA ? data.shapeIdB : data.shapeIdA;
				if (!B3_ID_EQUALS(opposite, otherId)) {
					continue;
				}

				const b3BodyId ownerBody = b3Shape_GetBody(selfIsA ? data.shapeIdA : data.shapeIdB);
				const Vec3 center = FromBackend(b3Body_GetWorldCenter(ownerBody));

				for (int manifoldIndex = 0; manifoldIndex < data.manifoldCount; ++manifoldIndex) {
					const b3Manifold& manifold = data.manifolds[manifoldIndex];

					if (out.pointCount == 0) {
						const Vec3 normal = FromBackend(manifold.normal);
						out.normal = selfIsA ? normal : Vec3{-normal.x, -normal.y, -normal.z};
					}

					for (int pointIndex = 0; pointIndex < manifold.pointCount; ++pointIndex) {
						if (out.pointCount >= 4) {
							break;
						}
						const b3ManifoldPoint& point = manifold.points[pointIndex];
						const Vec3 anchor = FromBackend(selfIsA ? point.anchorA : point.anchorB);
						out.point[out.pointCount] = center + anchor;
						out.separation[out.pointCount] = point.separation;

						out.normalImpulse[out.pointCount] = point.totalNormalImpulse;
						++out.pointCount;
					}
				}
				return out.pointCount > 0;
			}

			return false;
		}

		namespace {

			static_assert(sizeof(b3JointId) == sizeof(u64), "the joint id packing below assumes an i32 and 2 u16 fields");

			u64 PackJoint(b3JointId id) {
				return (static_cast<u64>(static_cast<u32>(id.index1))) |
					   (static_cast<u64>(id.world0) << 32) |
					   (static_cast<u64>(id.generation) << 48);
			}

			b3JointId UnpackJoint(PhysicsBackend::JointHandle joint) {
				b3JointId id;
				id.index1 = static_cast<int32_t>(static_cast<uint32_t>(joint.bits & 0xFFFFFFFFull));
				id.world0 = static_cast<uint16_t>((joint.bits >> 32) & 0xFFFFull);
				id.generation = static_cast<uint16_t>((joint.bits >> 48) & 0xFFFFull);
				return id;
			}

			b3Transform ToBackendFrame(const Vec3& position, const Quat& rotation) {
				b3Transform frame;
				frame.p = ToBackend(position);
				frame.q = ToBackend(rotation);
				return frame;
			}

			void FillJointBase(b3JointDef& base, const JointParams& params) {
				base.bodyIdA = UnpackBody(params.bodyA);
				base.bodyIdB = UnpackBody(params.bodyB);
				base.localFrameA = ToBackendFrame(params.localAnchorA, params.localRotationA);
				base.localFrameB = ToBackendFrame(params.localAnchorB, params.localRotationB);
				base.collideConnected = params.collideConnected;

				base.forceThreshold = params.forceThreshold;
				base.torqueThreshold = params.torqueThreshold;

				base.userData = reinterpret_cast<void*>(static_cast<uintptr_t>(params.owner));
			}

			JointKind KindOf(b3JointId id) {
				switch (b3Joint_GetType(id)) {
				case b3_revoluteJoint:
					return JointKind::Revolute;
				case b3_prismaticJoint:
					return JointKind::Prismatic;
				case b3_sphericalJoint:
					return JointKind::Spherical;
				case b3_distanceJoint:
					return JointKind::Distance;
				default:
					return JointKind::Weld;
				}
			}

		} // anonymous namespace

		bool CreateJoint(WorldHandle world, const JointParams& params, JointHandle& out) {
			out = JointHandle{};
			if (!IsWorldValid(world)) {
				return false;
			}

			if (!IsBodyValid(params.bodyA) || !IsBodyValid(params.bodyB)) {
				return false;
			}

			const b3WorldId worldId = Unpack(world);
			b3JointId id{};

			switch (params.kind) {
			case JointKind::Revolute: {
				b3RevoluteJointDef def = b3DefaultRevoluteJointDef();
				FillJointBase(def.base, params);
				def.enableSpring = params.enableSpring;
				def.hertz = params.springHertz;
				def.dampingRatio = params.springDampingRatio;
				def.targetAngle = params.springTarget;
				def.enableLimit = params.enableLimit;
				def.lowerAngle = params.lowerLimit;
				def.upperAngle = params.upperLimit;
				def.enableMotor = params.enableMotor;
				def.motorSpeed = params.motorSpeed;
				def.maxMotorTorque = params.maxMotorEffort;
				id = b3CreateRevoluteJoint(worldId, &def);
				break;
			}
			case JointKind::Prismatic: {
				b3PrismaticJointDef def = b3DefaultPrismaticJointDef();
				FillJointBase(def.base, params);
				def.enableSpring = params.enableSpring;
				def.hertz = params.springHertz;
				def.dampingRatio = params.springDampingRatio;
				def.targetTranslation = params.springTarget;
				def.enableLimit = params.enableLimit;
				def.lowerTranslation = params.lowerLimit;
				def.upperTranslation = params.upperLimit;
				def.enableMotor = params.enableMotor;
				def.motorSpeed = params.motorSpeed;
				def.maxMotorForce = params.maxMotorEffort;
				id = b3CreatePrismaticJoint(worldId, &def);
				break;
			}
			case JointKind::Spherical: {
				b3SphericalJointDef def = b3DefaultSphericalJointDef();
				FillJointBase(def.base, params);
				def.enableSpring = params.enableSpring;
				// torque units (not a frequency)
				def.springMode = b3_springTorque;
				def.springStiffness = params.springStiffness;
				def.springDamping = params.springDamping;
				def.targetRotation = ToBackend(params.targetRotation);
				def.enableConeLimit = params.enableConeLimit;
				def.coneAngleX = params.coneAngleX;
				def.coneAngleY = params.coneAngleY;
				def.enableTwistLimit = params.enableLimit;
				def.lowerTwistAngle = params.lowerLimit;
				def.upperTwistAngle = params.upperLimit;
				def.enableMotor = params.enableMotor;
				def.maxMotorTorque = params.maxMotorEffort;
				id = b3CreateSphericalJoint(worldId, &def);
				break;
			}
			case JointKind::Weld: {
				b3WeldJointDef def = b3DefaultWeldJointDef();
				FillJointBase(def.base, params);
				def.linearHertz = params.linearHertz;
				def.linearDampingRatio = params.linearDampingRatio;
				def.angularHertz = params.angularHertz;
				def.angularDampingRatio = params.angularDampingRatio;
				id = b3CreateWeldJoint(worldId, &def);
				break;
			}
			case JointKind::Distance:
			default: {
				b3DistanceJointDef def = b3DefaultDistanceJointDef();
				FillJointBase(def.base, params);
				def.length = params.springTarget;
				def.enableSpring = params.enableSpring;
				def.hertz = params.springHertz;
				def.dampingRatio = params.springDampingRatio;
				def.enableLimit = params.enableLimit;
				def.minLength = params.minLength;
				def.maxLength = params.maxLength;
				id = b3CreateDistanceJoint(worldId, &def);
				break;
			}
			}

			if (!b3Joint_IsValid(id)) {
				return false;
			}
			out.bits = PackJoint(id);
			return true;
		}

		void DestroyJoint(JointHandle joint, bool wakeAttached) {
			if (joint.IsNull()) {
				return;
			}
			const b3JointId id = UnpackJoint(joint);
			if (b3Joint_IsValid(id)) {
				b3DestroyJoint(id, wakeAttached);
			}
		}

		bool IsJointValid(JointHandle joint) {
			return !joint.IsNull() && b3Joint_IsValid(UnpackJoint(joint));
		}

		f32 GetJointForce(JointHandle joint) {
			if (!IsJointValid(joint)) {
				return 0.0f;
			}
			const b3Vec3 force = b3Joint_GetConstraintForce(UnpackJoint(joint));
			return std::sqrt(force.x * force.x + force.y * force.y + force.z * force.z);
		}

		void SetJointProperty(JointHandle joint, JointProperty property, const PropertyValue& value) {
			if (!IsJointValid(joint)) {
				return;
			}
			const b3JointId id = UnpackJoint(joint);
			if (!JointKindAcceptsProperty(KindOf(id), property)) {
				return;
			}
			if (JointPropertyWakesBodies(property)) {
				b3Joint_WakeBodies(id);
			}

			switch (property) {
			case JointProperty::ForceThreshold:
				b3Joint_SetForceThreshold(id, value.AsScalar());
				return;
			case JointProperty::TorqueThreshold:
				b3Joint_SetTorqueThreshold(id, value.AsScalar());
				return;
			case JointProperty::CollideConnected:
				b3Joint_SetCollideConnected(id, value.AsFlag());
				return;
			default:
				break;
			}

			switch (b3Joint_GetType(id)) {
			case b3_revoluteJoint:
				switch (property) {
				case JointProperty::EnableSpring:
					b3RevoluteJoint_EnableSpring(id, value.AsFlag());
					break;
				case JointProperty::SpringHertz:
					b3RevoluteJoint_SetSpringHertz(id, value.AsScalar());
					break;
				case JointProperty::SpringDampingRatio:
					b3RevoluteJoint_SetSpringDampingRatio(id, value.AsScalar());
					break;
				case JointProperty::SpringTarget:
					b3RevoluteJoint_SetTargetAngle(id, value.AsScalar());
					break;
				case JointProperty::EnableMotor:
					b3RevoluteJoint_EnableMotor(id, value.AsFlag());
					break;
				case JointProperty::MotorSpeed:
					b3RevoluteJoint_SetMotorSpeed(id, value.AsScalar());
					break;
				case JointProperty::MaxMotorEffort:
					b3RevoluteJoint_SetMaxMotorTorque(id, value.AsScalar());
					break;
				case JointProperty::EnableLimit:
					b3RevoluteJoint_EnableLimit(id, value.AsFlag());
					break;
				case JointProperty::Limits:
					b3RevoluteJoint_SetLimits(id, value.v[0], value.v[1]);
					break;
				default:
					break;
				}
				break;
			case b3_prismaticJoint:
				switch (property) {
				case JointProperty::EnableSpring:
					b3PrismaticJoint_EnableSpring(id, value.AsFlag());
					break;
				case JointProperty::SpringHertz:
					b3PrismaticJoint_SetSpringHertz(id, value.AsScalar());
					break;
				case JointProperty::SpringDampingRatio:
					b3PrismaticJoint_SetSpringDampingRatio(id, value.AsScalar());
					break;
				case JointProperty::SpringTarget:
					b3PrismaticJoint_SetTargetTranslation(id, value.AsScalar());
					break;
				case JointProperty::EnableMotor:
					b3PrismaticJoint_EnableMotor(id, value.AsFlag());
					break;
				case JointProperty::MotorSpeed:
					b3PrismaticJoint_SetMotorSpeed(id, value.AsScalar());
					break;
				case JointProperty::MaxMotorEffort:
					b3PrismaticJoint_SetMaxMotorForce(id, value.AsScalar());
					break;
				case JointProperty::EnableLimit:
					b3PrismaticJoint_EnableLimit(id, value.AsFlag());
					break;
				case JointProperty::Limits:
					b3PrismaticJoint_SetLimits(id, value.v[0], value.v[1]);
					break;
				default:
					break;
				}
				break;
			case b3_sphericalJoint:
				switch (property) {
				case JointProperty::EnableSpring:
					b3SphericalJoint_EnableSpring(id, value.AsFlag());
					break;
				case JointProperty::SpringConstants:
					b3SphericalJoint_SetSpringStiffness(id, value.v[0]);
					b3SphericalJoint_SetSpringDamping(id, value.v[1]);
					break;
				case JointProperty::TargetRotation:
					b3SphericalJoint_SetTargetRotation(id, ToBackend(value.AsRotation()));
					break;
				case JointProperty::EnableMotor:
					b3SphericalJoint_EnableMotor(id, value.AsFlag());
					break;
				case JointProperty::MaxMotorEffort:
					b3SphericalJoint_SetMaxMotorTorque(id, value.AsScalar());
					break;
				case JointProperty::EnableConeLimit:
					b3SphericalJoint_EnableConeLimit(id, value.AsFlag());
					break;
				case JointProperty::ConeAngles:
					b3SphericalJoint_SetConeLimits(id, value.v[0], value.v[1]);
					break;
				case JointProperty::EnableTwistLimit:
					b3SphericalJoint_EnableTwistLimit(id, value.AsFlag());
					break;
				case JointProperty::TwistLimits:
					b3SphericalJoint_SetTwistLimits(id, value.v[0], value.v[1]);
					break;
				default:
					break;
				}
				break;
			case b3_distanceJoint:
				switch (property) {
				case JointProperty::EnableSpring:
					b3DistanceJoint_EnableSpring(id, value.AsFlag());
					break;
				case JointProperty::SpringHertz:
					b3DistanceJoint_SetSpringHertz(id, value.AsScalar());
					break;
				case JointProperty::SpringDampingRatio:
					b3DistanceJoint_SetSpringDampingRatio(id, value.AsScalar());
					break;
				case JointProperty::SpringTarget:
					b3DistanceJoint_SetLength(id, value.AsScalar());
					break;
				case JointProperty::EnableLimit:
					b3DistanceJoint_EnableLimit(id, value.AsFlag());
					break;
				case JointProperty::LengthRange:
					b3DistanceJoint_SetLengthRange(id, value.v[0], value.v[1]);
					break;
				default:
					break;
				}
				break;
			default:
				break;
			}
		}

		PropertyValue GetJointProperty(JointHandle joint, JointProperty property) {
			if (!IsJointValid(joint)) {
				return PropertyValue{};
			}
			const b3JointId id = UnpackJoint(joint);
			if (!JointKindAcceptsProperty(KindOf(id), property)) {
				return PropertyValue{};
			}

			switch (property) {
			case JointProperty::ForceThreshold:
				return PropertyValue::Scalar(b3Joint_GetForceThreshold(id));
			case JointProperty::TorqueThreshold:
				return PropertyValue::Scalar(b3Joint_GetTorqueThreshold(id));
			case JointProperty::CollideConnected:
				return PropertyValue::Flag(b3Joint_GetCollideConnected(id));
			default:
				break;
			}

			switch (b3Joint_GetType(id)) {
			case b3_revoluteJoint:
				switch (property) {
				case JointProperty::EnableSpring:
					return PropertyValue::Flag(b3RevoluteJoint_IsSpringEnabled(id));
				case JointProperty::SpringHertz:
					return PropertyValue::Scalar(b3RevoluteJoint_GetSpringHertz(id));
				case JointProperty::SpringDampingRatio:
					return PropertyValue::Scalar(b3RevoluteJoint_GetSpringDampingRatio(id));
				case JointProperty::SpringTarget:
					return PropertyValue::Scalar(b3RevoluteJoint_GetTargetAngle(id));
				case JointProperty::EnableMotor:
					return PropertyValue::Flag(b3RevoluteJoint_IsMotorEnabled(id));
				case JointProperty::MotorSpeed:
					return PropertyValue::Scalar(b3RevoluteJoint_GetMotorSpeed(id));
				case JointProperty::MaxMotorEffort:
					return PropertyValue::Scalar(b3RevoluteJoint_GetMaxMotorTorque(id));
				case JointProperty::EnableLimit:
					return PropertyValue::Flag(b3RevoluteJoint_IsLimitEnabled(id));
				case JointProperty::Limits:
					return PropertyValue::Pair(b3RevoluteJoint_GetLowerLimit(id), b3RevoluteJoint_GetUpperLimit(id));
				default:
					break;
				}
				break;
			case b3_prismaticJoint:
				switch (property) {
				case JointProperty::EnableSpring:
					return PropertyValue::Flag(b3PrismaticJoint_IsSpringEnabled(id));
				case JointProperty::SpringHertz:
					return PropertyValue::Scalar(b3PrismaticJoint_GetSpringHertz(id));
				case JointProperty::SpringDampingRatio:
					return PropertyValue::Scalar(b3PrismaticJoint_GetSpringDampingRatio(id));
				case JointProperty::SpringTarget:
					return PropertyValue::Scalar(b3PrismaticJoint_GetTargetTranslation(id));
				case JointProperty::EnableMotor:
					return PropertyValue::Flag(b3PrismaticJoint_IsMotorEnabled(id));
				case JointProperty::MotorSpeed:
					return PropertyValue::Scalar(b3PrismaticJoint_GetMotorSpeed(id));
				case JointProperty::MaxMotorEffort:
					return PropertyValue::Scalar(b3PrismaticJoint_GetMaxMotorForce(id));
				case JointProperty::EnableLimit:
					return PropertyValue::Flag(b3PrismaticJoint_IsLimitEnabled(id));
				case JointProperty::Limits:
					return PropertyValue::Pair(b3PrismaticJoint_GetLowerLimit(id), b3PrismaticJoint_GetUpperLimit(id));
				default:
					break;
				}
				break;
			case b3_sphericalJoint:
				switch (property) {
				case JointProperty::EnableSpring:
					return PropertyValue::Flag(b3SphericalJoint_IsSpringEnabled(id));
				case JointProperty::SpringConstants:
					return PropertyValue::Pair(b3SphericalJoint_GetSpringStiffness(id), b3SphericalJoint_GetSpringDamping(id));
				case JointProperty::TargetRotation:
					return PropertyValue::Rotation(FromBackend(b3SphericalJoint_GetTargetRotation(id)));
				case JointProperty::EnableMotor:
					return PropertyValue::Flag(b3SphericalJoint_IsMotorEnabled(id));
				case JointProperty::MaxMotorEffort:
					return PropertyValue::Scalar(b3SphericalJoint_GetMaxMotorTorque(id));
				case JointProperty::EnableConeLimit:
					return PropertyValue::Flag(b3SphericalJoint_IsConeLimitEnabled(id));
				case JointProperty::ConeAngles:
					return PropertyValue::Pair(b3SphericalJoint_GetConeLimitX(id), b3SphericalJoint_GetConeLimitY(id));
				case JointProperty::EnableTwistLimit:
					return PropertyValue::Flag(b3SphericalJoint_IsTwistLimitEnabled(id));
				case JointProperty::TwistLimits:
					return PropertyValue::Pair(b3SphericalJoint_GetLowerTwistLimit(id), b3SphericalJoint_GetUpperTwistLimit(id));
				default:
					break;
				}
				break;
			case b3_distanceJoint:
				switch (property) {
				case JointProperty::EnableSpring:
					return PropertyValue::Flag(b3DistanceJoint_IsSpringEnabled(id));
				case JointProperty::SpringHertz:
					return PropertyValue::Scalar(b3DistanceJoint_GetSpringHertz(id));
				case JointProperty::SpringDampingRatio:
					return PropertyValue::Scalar(b3DistanceJoint_GetSpringDampingRatio(id));
				case JointProperty::SpringTarget:
					return PropertyValue::Scalar(b3DistanceJoint_GetLength(id));
				case JointProperty::EnableLimit:
					return PropertyValue::Flag(b3DistanceJoint_IsLimitEnabled(id));
				case JointProperty::LengthRange:
					return PropertyValue::Pair(b3DistanceJoint_GetMinLength(id), b3DistanceJoint_GetMaxLength(id));
				default:
					break;
				}
				break;
			default:
				break;
			}
			return PropertyValue{};
		}

		f32 GetBodyInverseMass(BodyHandle body) {
			if (!IsBodyValid(body)) {
				return 0.0f;
			}
			return b3Body_GetInverseMass(UnpackBody(body));
		}

		f32 GetBodyInverseInertiaAbout(BodyHandle body, const Vec3& worldAxis) {
			if (!IsBodyValid(body)) {
				return 0.0f;
			}

			const b3Matrix3 inverseInertia = b3Body_GetWorldInverseRotationalInertia(UnpackBody(body));
			const b3Vec3 product{
				inverseInertia.cx.x * worldAxis.x + inverseInertia.cy.x * worldAxis.y + inverseInertia.cz.x * worldAxis.z,
				inverseInertia.cx.y * worldAxis.x + inverseInertia.cy.y * worldAxis.y + inverseInertia.cz.y * worldAxis.z,
				inverseInertia.cx.z * worldAxis.x + inverseInertia.cy.z * worldAxis.y + inverseInertia.cz.z * worldAxis.z};
			return worldAxis.x * product.x + worldAxis.y * product.y + worldAxis.z * product.z;
		}

		void DrainJointEvents(WorldHandle world, JointEventSinkFn sink, void* user) {
			if (!IsWorldValid(world) || !sink) {
				return;
			}
			const b3JointEvents events = b3World_GetJointEvents(Unpack(world));
			for (int index = 0; index < events.count; ++index) {
				sink(user, static_cast<u64>(reinterpret_cast<uintptr_t>(events.jointEvents[index].userData)));
			}
		}

#if defined(SLT_WITH_EDITOR)

		namespace {

			struct DrawContext {
				const DebugDrawSink* sink = nullptr;
			};

			const DebugDrawSink& SinkOf(void* context) {
				return *static_cast<DrawContext*>(context)->sink;
			}

			u32 ToRGB(b3HexColor color) { return static_cast<u32>(color) & 0x00FFFFFFu; }

			// b3Pos aliases b3Vec3 while single precision is pinned

			void DrawSegment(b3Pos p1, b3Pos p2, b3HexColor color, void* context) {
				const DebugDrawSink& sink = SinkOf(context);
				sink.line(sink.user, FromBackend(p1), FromBackend(p2), ToRGB(color));
			}

			void DrawPoint(b3Pos p, float size, b3HexColor color, void* context) {
				const DebugDrawSink& sink = SinkOf(context);
				sink.point(sink.user, FromBackend(p), size, ToRGB(color));
			}

			void DrawSphere(b3Pos p, float radius, b3HexColor color, float, void* context) {
				const DebugDrawSink& sink = SinkOf(context);
				sink.sphere(sink.user, FromBackend(p), radius, ToRGB(color));
			}

			void DrawCapsule(b3Pos p1, b3Pos p2, float radius, b3HexColor color, float, void* context) {
				const DebugDrawSink& sink = SinkOf(context);
				sink.capsule(sink.user, FromBackend(p1), FromBackend(p2), radius, ToRGB(color));
			}

			void EmitBoxEdges(const DebugDrawSink& sink, const Vec3 corners[8], u32 rgb) {
				constexpr u32 EDGES[12][2] = {
					{0, 1}, {1, 2}, {2, 3}, {3, 0}, {4, 5}, {5, 6}, {6, 7}, {7, 4}, {0, 4}, {1, 5}, {2, 6}, {3, 7}};
				for (const auto& edge : EDGES) {
					sink.line(sink.user, corners[edge[0]], corners[edge[1]], rgb);
				}
			}

			void DrawBounds(b3AABB aabb, b3HexColor color, void* context) {
				const Vec3 lower = FromBackend(aabb.lowerBound);
				const Vec3 upper = FromBackend(aabb.upperBound);
				const Vec3 corners[8] = {
					{lower.x, lower.y, lower.z}, {upper.x, lower.y, lower.z}, {upper.x, upper.y, lower.z}, {lower.x, upper.y, lower.z}, {lower.x, lower.y, upper.z}, {upper.x, lower.y, upper.z}, {upper.x, upper.y, upper.z}, {lower.x, upper.y, upper.z}};
				EmitBoxEdges(SinkOf(context), corners, ToRGB(color));
			}

			void DrawBox(b3Vec3 extents, b3WorldTransform transform, b3HexColor color, void* context) {
				const Vec3 half = FromBackend(extents);
				Vec3 corners[8];
				u32 index = 0;
				for (i32 z = -1; z <= 1; z += 2) {
					for (i32 y = -1; y <= 1; y += 2) {
						for (i32 x = -1; x <= 1; x += 2) {
							const b3Vec3 local{half.x * static_cast<f32>(x), half.y * static_cast<f32>(y), half.z * static_cast<f32>(z)};
							corners[index++] = FromBackend(b3TransformWorldPoint(transform, local));
						}
					}
				}
				const Vec3 ordered[8] = {corners[0], corners[1], corners[3], corners[2],
										 corners[4], corners[5], corners[7], corners[6]};
				EmitBoxEdges(SinkOf(context), ordered, ToRGB(color));
			}

			void DrawTransform(b3WorldTransform transform, void* context) {
				const DebugDrawSink& sink = SinkOf(context);
				constexpr f32 AXIS_LENGTH = 0.25f;
				const Vec3 origin = FromBackend(b3TransformWorldPoint(transform, b3Vec3{0.0f, 0.0f, 0.0f}));
				const b3Vec3 axes[3] = {{AXIS_LENGTH, 0.0f, 0.0f}, {0.0f, AXIS_LENGTH, 0.0f}, {0.0f, 0.0f, AXIS_LENGTH}};
				const b3HexColor colors[3] = {b3_colorRed, b3_colorGreen, b3_colorBlue};
				for (u32 axis = 0; axis < 3; ++axis) {
					sink.line(sink.user, origin, FromBackend(b3TransformWorldPoint(transform, axes[axis])), ToRGB(colors[axis]));
				}
			}

			void DrawString(b3Pos, const char*, b3HexColor, void*) {
			}

			void EmitHull(const DebugDrawSink& sink, const b3HullData* hull, b3WorldTransform transform, u32 rgb) {
				if (!hull) {
					return;
				}
				const b3Vec3* points = b3GetHullPoints(hull);
				const b3HullHalfEdge* edges = b3GetHullEdges(hull);
				if (!points || !edges) {
					return;
				}
				for (i32 i = 0; i < hull->edgeCount; ++i) {
					const u32 twin = edges[i].twin;
					if (static_cast<u32>(i) >= twin) {
						continue;
					}
					const b3Vec3 a = points[edges[i].origin];
					const b3Vec3 b = points[edges[twin].origin];
					sink.line(sink.user, FromBackend(b3TransformWorldPoint(transform, a)),
							  FromBackend(b3TransformWorldPoint(transform, b)), rgb);
				}
			}

			void EmitMesh(const DebugDrawSink& sink, const b3Mesh& mesh, b3WorldTransform transform, u32 rgb) {
				const b3MeshData* data = mesh.data;
				if (!data) {
					return;
				}
				const b3Vec3* vertices = b3GetMeshVertices(data);
				const b3MeshTriangle* triangles = b3GetMeshTriangles(data);
				if (!vertices || !triangles) {
					return;
				}
				for (i32 i = 0; i < data->triangleCount; ++i) {
					const i32 index[3] = {triangles[i].index1, triangles[i].index2, triangles[i].index3};
					Vec3 corner[3];
					for (u32 corners = 0; corners < 3; ++corners) {
						const b3Vec3 v = vertices[index[corners]];
						const b3Vec3 scaled{v.x * mesh.scale.x, v.y * mesh.scale.y, v.z * mesh.scale.z};
						corner[corners] = FromBackend(b3TransformWorldPoint(transform, scaled));
					}
					sink.line(sink.user, corner[0], corner[1], rgb);
					sink.line(sink.user, corner[1], corner[2], rgb);
					sink.line(sink.user, corner[2], corner[0], rgb);
				}
			}

			void DrawShape(void* userShape, b3WorldTransform transform, b3HexColor color, void* context) {
				b3ShapeId id{};
				std::memcpy(&id, &userShape, sizeof(b3ShapeId));
				if (!b3Shape_IsValid(id)) {
					return;
				}
				const DebugDrawSink& sink = SinkOf(context);
				const u32 rgb = ToRGB(color);
				switch (b3Shape_GetType(id)) {
				case b3_sphereShape: {
					const b3Sphere sphere = b3Shape_GetSphere(id);
					sink.sphere(sink.user, FromBackend(b3TransformWorldPoint(transform, sphere.center)), sphere.radius, rgb);
					break;
				}
				case b3_capsuleShape: {
					const b3Capsule capsule = b3Shape_GetCapsule(id);
					sink.capsule(sink.user, FromBackend(b3TransformWorldPoint(transform, capsule.center1)),
								 FromBackend(b3TransformWorldPoint(transform, capsule.center2)),
								 capsule.radius, rgb);
					break;
				}
				case b3_hullShape:
					EmitHull(sink, b3Shape_GetHull(id), transform, rgb);
					break;
				case b3_meshShape:
					EmitMesh(sink, b3Shape_GetMesh(id), transform, rgb);
					break;
				default:
					break;
				}
			}

		} // anonymous namespace

		void EnableDebugDraw() {
			_debugDrawArmed = true;
		}

		void DrawWorld(WorldHandle world, const DebugDrawOptions& options, const DebugDrawSink& sink) {
			if (!IsWorldValid(world) || !sink.line || !sink.sphere || !sink.capsule || !sink.point) {
				return;
			}
			DrawContext context;
			context.sink = &sink;

			b3DebugDraw draw = b3DefaultDebugDraw();
			draw.DrawShapeFcn = &DrawShape;
			draw.DrawSegmentFcn = &DrawSegment;
			draw.DrawTransformFcn = &DrawTransform;
			draw.DrawPointFcn = &DrawPoint;
			draw.DrawSphereFcn = &DrawSphere;
			draw.DrawCapsuleFcn = &DrawCapsule;
			draw.DrawBoundsFcn = &DrawBounds;
			draw.DrawBoxFcn = &DrawBox;
			draw.DrawStringFcn = &DrawString;
			draw.context = &context;

			const float extent = 100000.0f * b3GetLengthUnitsPerMeter();
			draw.drawingBounds.lowerBound = b3Vec3{-extent, -extent, -extent};
			draw.drawingBounds.upperBound = b3Vec3{extent, extent, extent};

			draw.drawShapes = options.shapes;
			draw.drawBounds = options.bounds;
			draw.drawContacts = options.contacts;
			draw.drawContactNormals = options.contactNormals;
			draw.drawContactForces = options.contactImpulses;
			draw.drawMass = options.mass;
			draw.drawSleep = options.sleep;
			draw.drawIslands = options.islands;

			draw.drawJoints = options.joints;

			b3World_Draw(Unpack(world), &draw, ~0ull);
		}

#endif

	} // namespace PhysicsBackend

} // namespace Shalltear
