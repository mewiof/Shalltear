#include "PhysicsBackend.h"

#include <cmath>
#include <cfloat>

namespace Shalltear {

	namespace {

		constexpr u32 STUB_WORLD_CAPACITY = 16;

		constexpr u32 STUB_BODY_CAPACITY = 256;

		constexpr f32 STUB_TIME_TO_SLEEP = 0.5f;

		struct StubWorld {
			PhysicsBackend::WorldParams params;
			u16 generation = 0;
		};

		constexpr u32 STUB_SHAPE_CAPACITY = 512;

		struct StubBody {
			PhysicsBackend::BodyParams params;
			Vec3 position{};
			Quat rotation = Quat::Identity();
			Vec3 linearVelocity{};
			Vec3 angularVelocity{};
			f32 sleepTime = 0.0f;
			u32 worldSlot = 0;
			u32 shapeCount = 0;
			u16 generation = 0;
			bool awake = true;
		};

		struct StubShape {
			u32 bodySlot = 0;
			u16 bodyGeneration = 0;
			u16 generation = 0;

			Vec3 localCenter{};
			f32 boundingRadius = 0.0f;

			u64 categoryBits = 0;
			u64 maskBits = ~0ull;

			bool isSensor = false;

			bool contactEvents = false;
			bool sensorEvents = false;

			u64 owner = 0;
		};

		Quat IntegrateRotation(const Quat& rotation, const Vec3& angularVelocity, f32 timeStep) {
			const Quat spin(angularVelocity.x, angularVelocity.y, angularVelocity.z, 0.0f);
			const Quat delta = spin * rotation * (0.5f * timeStep);
			return (rotation + delta).Normalized();
		}

		StubWorld _worlds[STUB_WORLD_CAPACITY];
		StubBody _bodies[STUB_BODY_CAPACITY];
		StubShape _shapes[STUB_SHAPE_CAPACITY];

		u32 Pack(u32 slot, u16 generation) {
			return ((slot + 1) & 0xFFFFu) | (static_cast<u32>(generation) << 16);
		}

		StubWorld* Resolve(PhysicsBackend::WorldHandle world) {
			if (world.IsNull()) {
				return nullptr;
			}
			u32 slot = (world.bits & 0xFFFFu) - 1u;
			if (slot >= STUB_WORLD_CAPACITY) {
				return nullptr;
			}
			u16 generation = static_cast<u16>(world.bits >> 16);
			StubWorld& entry = _worlds[slot];
			if (entry.generation != generation || (entry.generation & 1u) == 0u) {
				return nullptr;
			}
			return &entry;
		}

		u32 WorldSlot(PhysicsBackend::WorldHandle world) {
			return (world.bits & 0xFFFFu) - 1u;
		}

		u64 PackBody(u32 slot, u16 generation) {
			return (static_cast<u64>(slot) + 1ull) | (static_cast<u64>(generation) << 32);
		}

		void FreeShapesOfBody(u32 bodySlot, u16 bodyGeneration) {
			for (StubShape& shape : _shapes) {
				if ((shape.generation & 1u) != 0u && shape.bodySlot == bodySlot && shape.bodyGeneration == bodyGeneration) {
					++shape.generation;
					shape.bodySlot = 0;
					shape.bodyGeneration = 0;
				}
			}
		}

		StubBody* ResolveBody(PhysicsBackend::BodyHandle body) {
			if (body.IsNull()) {
				return nullptr;
			}
			u64 slot = (body.bits & 0xFFFFFFFFull) - 1ull;
			if (slot >= STUB_BODY_CAPACITY) {
				return nullptr;
			}
			u16 generation = static_cast<u16>((body.bits >> 32) & 0xFFFFull);
			StubBody& entry = _bodies[static_cast<size_t>(slot)];
			if (entry.generation != generation || (entry.generation & 1u) == 0u) {
				return nullptr;
			}
			return &entry;
		}

		StubShape* ResolveShape(PhysicsBackend::ShapeHandle shape) {
			if (shape.IsNull()) {
				return nullptr;
			}
			const u64 slot = (shape.bits & 0xFFFFFFFFull) - 1ull;
			if (slot >= STUB_SHAPE_CAPACITY) {
				return nullptr;
			}
			StubShape& entry = _shapes[static_cast<size_t>(slot)];
			const u16 generation = static_cast<u16>((shape.bits >> 32) & 0xFFFFull);
			if (entry.generation != generation || (entry.generation & 1u) == 0u) {
				return nullptr;
			}
			return &entry;
		}

		void DescribeBoundingSphere(const PhysicsShapeRequest& request, Vec3& center, f32& radius) {
			switch (request.kind) {
			case PhysicsShapeKind::Box:
				center = request.localPosition;
				radius = request.halfExtents.Length();
				break;
			case PhysicsShapeKind::Sphere:
				center = request.localPosition;
				radius = request.radius;
				break;
			case PhysicsShapeKind::Capsule:
				center = (request.capsulePoint0 + request.capsulePoint1) * 0.5f;
				radius = request.radius + (request.capsulePoint1 - request.capsulePoint0).Length() * 0.5f;
				break;
			case PhysicsShapeKind::ConvexHull:
			case PhysicsShapeKind::TriangleMesh: {
				center = request.localPosition;
				f32 farthest = 0.0f;
				for (u32 index = 0; index < request.pointCount; ++index) {
					farthest = request.points[index].LengthSquared() > farthest
								   ? request.points[index].LengthSquared()
								   : farthest;
				}
				radius = std::sqrt(farthest);
				break;
			}
			}
		}

		bool SweptHit(const Vec3& queryPoint0, const Vec3& queryPoint1, f32 queryRadius,
					  const Vec3& translation, const Vec3& targetCenter, f32 targetRadius,
					  f32& outFraction, Vec3& outNormal, bool& outInitialOverlap) {
			outInitialOverlap = false;
			const f32 radiusSum = queryRadius + targetRadius;
			const f32 radiusSumSq = radiusSum * radiusSum;

			auto separationSquaredAt = [&](f32 t) {
				const Vec3 center = targetCenter - translation * t;
				const Vec3 axis = queryPoint1 - queryPoint0;
				const f32 axisLengthSq = axis.LengthSquared();
				f32 along = 0.0f;
				if (axisLengthSq > 0.0f) {
					along = Vec3::Dot(center - queryPoint0, axis) / axisLengthSq;
					along = along < 0.0f ? 0.0f : (along > 1.0f ? 1.0f : along);
				}
				return (center - (queryPoint0 + axis * along)).LengthSquared();
			};

			if (separationSquaredAt(0.0f) <= radiusSumSq) {
				outInitialOverlap = true;
				outFraction = 0.0f;
				outNormal = Vec3{};
				return true;
			}

			f32 lowest = 0.0f;
			f32 highest = 1.0f;
			for (u32 step = 0; step < 64; ++step) {
				const f32 third = (highest - lowest) / 3.0f;
				const f32 left = lowest + third;
				const f32 right = highest - third;
				if (separationSquaredAt(left) < separationSquaredAt(right)) {
					highest = right;
				} else {
					lowest = left;
				}
			}
			const f32 closest = (lowest + highest) * 0.5f;
			if (separationSquaredAt(closest) > radiusSumSq) {
				return false;
			}

			f32 low = 0.0f;
			f32 high = closest;
			for (u32 step = 0; step < 40; ++step) {
				const f32 mid = (low + high) * 0.5f;
				if (separationSquaredAt(mid) <= radiusSumSq) {
					high = mid;
				} else {
					low = mid;
				}
			}
			outFraction = high;

			const Vec3 center = targetCenter - translation * high;
			const Vec3 axis = queryPoint1 - queryPoint0;
			const f32 axisLengthSq = axis.LengthSquared();
			f32 along = 0.0f;
			if (axisLengthSq > 0.0f) {
				along = Vec3::Dot(center - queryPoint0, axis) / axisLengthSq;
				along = along < 0.0f ? 0.0f : (along > 1.0f ? 1.0f : along);
			}
			const Vec3 offset = (queryPoint0 + axis * along) - center;
			outNormal = offset.LengthSquared() > 0.0f ? offset.Normalized() : Vec3::Up();
			return true;
		}

		bool ShouldQueryCollide(const StubShape& shape, const PhysicsBackend::QueryFilter& filter) {
			return (shape.categoryBits & filter.maskBits) != 0 && (shape.maskBits & filter.categoryBits) != 0;
		}

		bool ShapeWorldSphere(const StubShape& shape, Vec3& center, f32& radius) {
			if (shape.bodySlot >= STUB_BODY_CAPACITY) {
				return false;
			}
			const StubBody& body = _bodies[shape.bodySlot];
			if (body.generation != shape.bodyGeneration || (body.generation & 1u) == 0u) {
				return false;
			}
			center = body.position + body.rotation * shape.localCenter;
			radius = shape.boundingRadius;
			return true;
		}

		constexpr u32 STUB_JOINT_CAPACITY = 128;

		constexpr u32 JOINT_PROPERTY_COUNT = static_cast<u32>(PhysicsBackend::JointProperty::LengthRange) + 1u;

		struct StubJoint {
			u32 bodySlotA = 0;
			u32 bodySlotB = 0;
			u16 bodyGenerationA = 0;
			u16 bodyGenerationB = 0;
			Vec3 localAnchorA{};
			Vec3 localAnchorB{};
			f32 forceThreshold = 0.0f;
			u64 owner = 0;
			u32 worldSlot = 0;
			u16 generation = 0;

			f32 force = 0.0f;

			PhysicsBackend::JointKind kind = PhysicsBackend::JointKind::Weld;
			PhysicsBackend::PropertyValue properties[JOINT_PROPERTY_COUNT];
		};

		void Record(StubJoint& joint, PhysicsBackend::JointProperty property, const PhysicsBackend::PropertyValue& value) {
			const u32 index = static_cast<u32>(property);
			if (index < JOINT_PROPERTY_COUNT) {
				joint.properties[index] = value;
			}
		}

		StubJoint _joints[STUB_JOINT_CAPACITY];

		u64 _jointEvents[STUB_JOINT_CAPACITY];
		u32 _jointEventCount = 0;

		u64 PackJoint(u32 slot, u16 generation) {
			return (static_cast<u64>(slot) + 1ull) | (static_cast<u64>(generation) << 32);
		}

		StubJoint* ResolveJoint(PhysicsBackend::JointHandle joint) {
			if (joint.IsNull()) {
				return nullptr;
			}
			const u64 slot = (joint.bits & 0xFFFFFFFFull) - 1ull;
			if (slot >= STUB_JOINT_CAPACITY) {
				return nullptr;
			}
			StubJoint& entry = _joints[static_cast<size_t>(slot)];
			const u16 generation = static_cast<u16>((joint.bits >> 32) & 0xFFFFull);
			if (entry.generation != generation || (entry.generation & 1u) == 0u) {
				return nullptr;
			}
			return &entry;
		}

		void FreeJointsOfBody(u32 bodySlot, u16 bodyGeneration) {
			for (StubJoint& joint : _joints) {
				if ((joint.generation & 1u) == 0u) {
					continue;
				}
				const bool onA = joint.bodySlotA == bodySlot && joint.bodyGenerationA == bodyGeneration;
				const bool onB = joint.bodySlotB == bodySlot && joint.bodyGenerationB == bodyGeneration;
				if (onA || onB) {
					const u16 next = static_cast<u16>(joint.generation + 1u);
					joint = StubJoint{};
					joint.generation = next;
				}
			}
		}

		void SolveJoints(u32 worldSlot, f32 timeStep) {
			_jointEventCount = 0;
			if (timeStep <= 0.0f) {
				return;
			}

			for (StubJoint& joint : _joints) {
				joint.force = 0.0f;
				if ((joint.generation & 1u) == 0u || joint.worldSlot != worldSlot) {
					continue;
				}
				if (joint.bodySlotA >= STUB_BODY_CAPACITY || joint.bodySlotB >= STUB_BODY_CAPACITY) {
					continue;
				}

				StubBody& bodyA = _bodies[joint.bodySlotA];
				StubBody& bodyB = _bodies[joint.bodySlotB];
				if (bodyA.generation != joint.bodyGenerationA || bodyB.generation != joint.bodyGenerationB) {
					continue;
				}

				if (bodyB.params.kind != PhysicsBackend::BodyKind::Dynamic) {
					continue;
				}

				const Vec3 anchorA = bodyA.position + bodyA.rotation * joint.localAnchorA;
				const Vec3 anchorB = bodyB.position + bodyB.rotation * joint.localAnchorB;

				const Vec3 deltaVelocity = bodyA.linearVelocity - bodyB.linearVelocity;
				bodyB.linearVelocity = bodyA.linearVelocity;
				bodyB.position = bodyB.position + (anchorA - anchorB);

				bodyB.sleepTime = 0.0f;

				joint.force = deltaVelocity.Length() * bodyB.params.mass / timeStep;
				if (joint.forceThreshold < FLT_MAX && joint.force >= joint.forceThreshold &&
					_jointEventCount < STUB_JOINT_CAPACITY) {
					_jointEvents[_jointEventCount++] = joint.owner;
				}
			}
		}

		constexpr u32 STUB_TOUCH_CAPACITY = 512;

		struct StubTouch {
			u32 shapeSlotA = 0;
			u32 shapeSlotB = 0;
			u16 generationA = 0;
			u16 generationB = 0;
			bool sensor = false;
			bool live = false;
			bool present = false;
		};

		StubTouch _touching[STUB_TOUCH_CAPACITY];

		struct StubTouchEvent {
			u64 ownerA = 0;
			u64 ownerB = 0;
			u32 worldSlot = 0;
			bool sensor = false;
			bool begin = false;
		};

		StubTouchEvent _touchEvents[STUB_TOUCH_CAPACITY * 2];
		u32 _touchEventCount = 0;

		void PushTouchEvent(u32 worldSlot, u64 ownerA, u64 ownerB, bool sensor, bool begin) {
			if (_touchEventCount >= STUB_TOUCH_CAPACITY * 2) {
				return;
			}
			StubTouchEvent& event = _touchEvents[_touchEventCount++];
			event.ownerA = ownerA;
			event.ownerB = ownerB;
			event.worldSlot = worldSlot;
			event.sensor = sensor;
			event.begin = begin;
		}

		bool ShouldShapesCollide(const StubShape& a, const StubShape& b) {
			return (a.categoryBits & b.maskBits) != 0 && (b.categoryBits & a.maskBits) != 0;
		}

		bool OrderSensorPair(u32& slotA, u32& slotB) {
			if (_shapes[slotA].isSensor) {
				return true;
			}
			if (_shapes[slotB].isSensor) {
				const u32 swap = slotA;
				slotA = slotB;
				slotB = swap;
				return true;
			}
			return false;
		}

		bool PairReports(const StubShape& a, const StubShape& b, bool sensor) {
			if (sensor) {
				return a.sensorEvents && b.sensorEvents;
			}
			return a.contactEvents || b.contactEvents;
		}

		void UpdateTouches(u32 worldSlot) {
			for (StubTouch& row : _touching) {
				row.live = false;
			}

			for (u32 slotA = 0; slotA < STUB_SHAPE_CAPACITY; ++slotA) {
				if ((_shapes[slotA].generation & 1u) == 0u || _shapes[slotA].bodySlot >= STUB_BODY_CAPACITY) {
					continue;
				}
				if (_bodies[_shapes[slotA].bodySlot].worldSlot != worldSlot) {
					continue;
				}

				for (u32 slotB = slotA + 1; slotB < STUB_SHAPE_CAPACITY; ++slotB) {
					if ((_shapes[slotB].generation & 1u) == 0u || _shapes[slotB].bodySlot >= STUB_BODY_CAPACITY) {
						continue;
					}
					if (_bodies[_shapes[slotB].bodySlot].worldSlot != worldSlot ||
						_shapes[slotA].bodySlot == _shapes[slotB].bodySlot) {
						continue;
					}

					u32 orderedA = slotA;
					u32 orderedB = slotB;
					const bool sensor = OrderSensorPair(orderedA, orderedB);

					const StubBody& bodyA = _bodies[_shapes[orderedA].bodySlot];
					const StubBody& bodyB = _bodies[_shapes[orderedB].bodySlot];
					if (!sensor && bodyA.params.kind == PhysicsBackend::BodyKind::Static &&
						bodyB.params.kind == PhysicsBackend::BodyKind::Static) {
						continue;
					}

					if (!ShouldShapesCollide(_shapes[orderedA], _shapes[orderedB]) ||
						!PairReports(_shapes[orderedA], _shapes[orderedB], sensor)) {
						continue;
					}

					Vec3 centerA;
					Vec3 centerB;
					f32 radiusA = 0.0f;
					f32 radiusB = 0.0f;
					if (!ShapeWorldSphere(_shapes[orderedA], centerA, radiusA) ||
						!ShapeWorldSphere(_shapes[orderedB], centerB, radiusB)) {
						continue;
					}
					const Vec3 delta = centerB - centerA;
					const f32 reach = radiusA + radiusB;
					if (delta.LengthSquared() > reach * reach) {
						continue;
					}

					StubTouch* existing = nullptr;
					StubTouch* vacant = nullptr;
					for (StubTouch& row : _touching) {
						if (!row.present) {
							if (!vacant) {
								vacant = &row;
							}
							continue;
						}
						if (row.shapeSlotA == orderedA && row.shapeSlotB == orderedB &&
							row.generationA == _shapes[orderedA].generation &&
							row.generationB == _shapes[orderedB].generation) {
							existing = &row;
							break;
						}
					}
					if (existing) {
						existing->live = true;
						continue;
					}
					if (!vacant) {
						continue;
					}
					vacant->present = true;
					vacant->live = true;
					vacant->sensor = sensor;
					vacant->shapeSlotA = orderedA;
					vacant->shapeSlotB = orderedB;
					vacant->generationA = _shapes[orderedA].generation;
					vacant->generationB = _shapes[orderedB].generation;
					PushTouchEvent(worldSlot, _shapes[orderedA].owner, _shapes[orderedB].owner, sensor, true);
				}
			}

			for (StubTouch& row : _touching) {
				if (!row.present || row.live) {
					continue;
				}
				const StubShape& shapeA = _shapes[row.shapeSlotA];
				const StubShape& shapeB = _shapes[row.shapeSlotB];
				const bool aliveA = shapeA.generation == row.generationA && (row.generationA & 1u) != 0u;
				const bool aliveB = shapeB.generation == row.generationB && (row.generationB & 1u) != 0u;

				if (aliveA && aliveB && shapeA.bodySlot < STUB_BODY_CAPACITY &&
					_bodies[shapeA.bodySlot].worldSlot != worldSlot) {
					continue;
				}

				PushTouchEvent(worldSlot, aliveA ? shapeA.owner : 0ull, aliveB ? shapeB.owner : 0ull, row.sensor,
							   false);
				row.present = false;
			}
		}

	} // anonymous namespace

	namespace PhysicsBackend {

		std::string_view Name() {
			return "stub";
		}

		void InstallHostServices() {
		}

		bool CreateWorld(const WorldParams& params, WorldHandle& out) {
			out = WorldHandle{};
			for (u32 slot = 0; slot < STUB_WORLD_CAPACITY; ++slot) {
				StubWorld& entry = _worlds[slot];
				if ((entry.generation & 1u) != 0u) {
					continue;
				}
				++entry.generation;
				entry.params = params;
				out.bits = Pack(slot, entry.generation);
				return true;
			}
			return false;
		}

		void DestroyWorld(WorldHandle world) {
			if (StubWorld* entry = Resolve(world)) {
				const u32 slot = WorldSlot(world);
				for (u32 bodySlot = 0; bodySlot < STUB_BODY_CAPACITY; ++bodySlot) {
					StubBody& body = _bodies[bodySlot];
					if ((body.generation & 1u) != 0u && body.worldSlot == slot) {
						FreeShapesOfBody(bodySlot, body.generation);
						++body.generation;
					}
				}
				++entry->generation;
				entry->params = WorldParams{};
			}
		}

		bool IsWorldValid(WorldHandle world) {
			return Resolve(world) != nullptr;
		}

		void UpdateWorld(WorldHandle world, const WorldParams& params) {
			if (StubWorld* entry = Resolve(world)) {
				entry->params = params;
			}
		}

		void StepWorld(WorldHandle world, f32 timeStep, i32) {
			StubWorld* entry = Resolve(world);
			if (!entry || timeStep <= 0.0f) {
				return;
			}
			const u32 slot = WorldSlot(world);

			_touchEventCount = 0;
			const Vec3 gravity{entry->params.gravityX, entry->params.gravityY, entry->params.gravityZ};

			for (StubBody& body : _bodies) {
				if ((body.generation & 1u) == 0u || body.worldSlot != slot || !body.awake) {
					continue;
				}

				if (body.params.kind == PhysicsBackend::BodyKind::Static) {
					continue;
				}

				if (body.params.kind != PhysicsBackend::BodyKind::Kinematic) {
					Vec3 velocity = body.linearVelocity + gravity * (timeStep * body.params.gravityScale);
					velocity = velocity * (1.0f / (1.0f + timeStep * body.params.linearDamping));
					if (body.params.freezePositionX) {
						velocity.x = 0.0f;
					}
					if (body.params.freezePositionY) {
						velocity.y = 0.0f;
					}
					if (body.params.freezePositionZ) {
						velocity.z = 0.0f;
					}
					body.linearVelocity = velocity;

					Vec3 spin = body.angularVelocity * (1.0f / (1.0f + timeStep * body.params.angularDamping));
					if (body.params.freezeRotationX) {
						spin.x = 0.0f;
					}
					if (body.params.freezeRotationY) {
						spin.y = 0.0f;
					}
					if (body.params.freezeRotationZ) {
						spin.z = 0.0f;
					}
					body.angularVelocity = spin;
				}

				body.position = body.position + body.linearVelocity * timeStep;
				if (body.angularVelocity.LengthSquared() > 0.0f) {
					body.rotation = IntegrateRotation(body.rotation, body.angularVelocity, timeStep);
				}

				if (!entry->params.enableSleeping) {
					body.sleepTime = 0.0f;
					continue;
				}
				const f32 speed = body.linearVelocity.Length();
				if (speed > body.params.sleepThreshold) {
					body.sleepTime = 0.0f;
					continue;
				}
				body.sleepTime += timeStep;
				if (body.sleepTime >= STUB_TIME_TO_SLEEP) {
					body.awake = false;
					body.linearVelocity = Vec3{};
					body.angularVelocity = Vec3{};
				}
			}

			SolveJoints(slot, timeStep);
			UpdateTouches(slot);
		}

		bool CreateBody(WorldHandle world, const BodyParams& params, BodyHandle& out) {
			out = BodyHandle{};
			if (!Resolve(world)) {
				return false;
			}
			for (u32 slot = 0; slot < STUB_BODY_CAPACITY; ++slot) {
				StubBody& entry = _bodies[slot];
				if ((entry.generation & 1u) != 0u) {
					continue;
				}
				++entry.generation;
				entry.params = params;
				entry.position = params.position;
				entry.rotation = params.rotation;
				entry.linearVelocity = params.linearVelocity;
				entry.angularVelocity = params.angularVelocity;
				entry.sleepTime = 0.0f;
				entry.worldSlot = WorldSlot(world);
				entry.shapeCount = 0;
				entry.awake = true;
				out.bits = PackBody(slot, entry.generation);
				return true;
			}
			return false;
		}

		void DestroyBody(BodyHandle body) {
			if (StubBody* entry = ResolveBody(body)) {
				FreeShapesOfBody(static_cast<u32>((body.bits & 0xFFFFFFFFull) - 1ull), entry->generation);
				FreeJointsOfBody(static_cast<u32>((body.bits & 0xFFFFFFFFull) - 1ull), entry->generation);
				const u16 next = static_cast<u16>(entry->generation + 1u);
				*entry = StubBody{};
				entry->generation = next;
			}
		}

		bool IsBodyValid(BodyHandle body) {
			return ResolveBody(body) != nullptr;
		}

		void GetBodyTransform(BodyHandle body, Vec3& position, Quat& rotation) {
			if (StubBody* entry = ResolveBody(body)) {
				position = entry->position;
				rotation = entry->rotation;
			}
		}

		void SetBodyTransform(BodyHandle body, const Vec3& position, const Quat& rotation) {
			if (StubBody* entry = ResolveBody(body)) {
				entry->position = position;
				entry->rotation = rotation;
				entry->awake = true;
				entry->sleepTime = 0.0f;
			}
		}

		void SetBodyTargetTransform(BodyHandle body, const Vec3& position, const Quat& rotation, f32 timeStep) {
			StubBody* entry = ResolveBody(body);
			if (!entry || timeStep <= 0.0f) {
				return;
			}
			entry->linearVelocity = (position - entry->position) * (1.0f / timeStep);
			entry->rotation = rotation;
			if (entry->linearVelocity.Length() > entry->params.sleepThreshold) {
				entry->awake = true;
				entry->sleepTime = 0.0f;
			}
		}

		Vec3 GetBodyLinearVelocity(BodyHandle body) {
			StubBody* entry = ResolveBody(body);
			return entry ? entry->linearVelocity : Vec3{};
		}

		void SetBodyLinearVelocity(BodyHandle body, const Vec3& velocity) {
			if (StubBody* entry = ResolveBody(body)) {
				entry->linearVelocity = velocity;
				entry->awake = true;
				entry->sleepTime = 0.0f;
			}
		}

		Vec3 GetBodyAngularVelocity(BodyHandle body) {
			StubBody* entry = ResolveBody(body);
			return entry ? entry->angularVelocity : Vec3{};
		}

		Vec3 GetBodyPointVelocity(BodyHandle body, const Vec3& worldPoint) {
			StubBody* entry = ResolveBody(body);
			if (!entry) {
				return Vec3{};
			}
			const Vec3 arm = worldPoint - entry->position;
			return entry->linearVelocity + Vec3::Cross(entry->angularVelocity, arm);
		}

		void SetBodyAngularVelocity(BodyHandle body, const Vec3& velocity) {
			if (StubBody* entry = ResolveBody(body)) {
				entry->angularVelocity = velocity;
				entry->awake = true;
				entry->sleepTime = 0.0f;
			}
		}

		bool CreateShape(BodyHandle body, const PhysicsShapeRequest& request, ShapeHandle& out) {
			out = ShapeHandle{};
			StubBody* owner = ResolveBody(body);
			if (!owner) {
				return false;
			}
			if (request.kind == PhysicsShapeKind::TriangleMesh && !SupportsTriangleMeshOn(owner->params.kind)) {
				return false;
			}

			for (u32 slot = 0; slot < STUB_SHAPE_CAPACITY; ++slot) {
				StubShape& entry = _shapes[slot];
				if ((entry.generation & 1u) != 0u) {
					continue;
				}
				++entry.generation;
				entry.bodySlot = static_cast<u32>((body.bits & 0xFFFFFFFFull) - 1ull);
				entry.bodyGeneration = static_cast<u16>((body.bits >> 32) & 0xFFFFull);
				DescribeBoundingSphere(request, entry.localCenter, entry.boundingRadius);
				entry.categoryBits = request.categoryBits;
				entry.maskBits = request.maskBits;
				entry.isSensor = request.isTrigger;
				entry.owner = request.owner;
				++owner->shapeCount;
				out.bits = PackBody(slot, entry.generation);
				return true;
			}
			return false;
		}

		void DestroyShape(ShapeHandle shape) {
			StubShape* resolved = ResolveShape(shape);
			if (!resolved) {
				return;
			}
			StubShape& entry = *resolved;

			if (entry.bodySlot < STUB_BODY_CAPACITY) {
				StubBody& owner = _bodies[entry.bodySlot];
				if (owner.generation == entry.bodyGeneration && (owner.generation & 1u) != 0u && owner.shapeCount > 0) {
					--owner.shapeCount;
				}
			}
			++entry.generation;
			entry.bodySlot = 0;
			entry.bodyGeneration = 0;
		}

		void SetShapeFilter(ShapeHandle shape, u64 categoryBits, u64 maskBits, i32 groupIndex) {
			(void)groupIndex;

			StubShape* entry = ResolveShape(shape);
			if (!entry) {
				return;
			}
			entry->categoryBits = categoryBits;
			entry->maskBits = maskBits;
		}

		void SetShapeSurface(ShapeHandle shape, const PhysicsSurface& surface) {
			(void)shape;
			(void)surface;
		}

		u32 GetBodyShapeCount(BodyHandle body) {
			StubBody* entry = ResolveBody(body);
			return entry ? entry->shapeCount : 0u;
		}

		bool SupportsTriangleMeshOn(BodyKind kind) {
			return kind == BodyKind::Static;
		}

		void RefreshBodyMass(BodyHandle body, const MassParams& params) {
			if (StubBody* entry = ResolveBody(body)) {
				entry->params.mass = params.mass;
				entry->params.centerOfMass = params.automaticCenterOfMass ? Vec3{} : params.centerOfMass;
			}
		}

		bool IsBodyAwake(BodyHandle body) {
			StubBody* entry = ResolveBody(body);
			return entry != nullptr && entry->awake;
		}

		void SetBodyAwake(BodyHandle body, bool awake) {
			if (StubBody* entry = ResolveBody(body)) {
				entry->awake = awake;
				entry->sleepTime = 0.0f;
				if (!awake) {
					entry->linearVelocity = Vec3{};
					entry->angularVelocity = Vec3{};
				}
			}
		}

		void SetBodyProperty(BodyHandle body, BodyProperty property, const PropertyValue& value) {
			StubBody* entry = ResolveBody(body);
			if (!entry) {
				return;
			}
			if (BodyPropertyWakesBody(property)) {
				entry->awake = true;
				entry->sleepTime = 0.0f;
			}

			switch (property) {
			case BodyProperty::LinearDamping:
				entry->params.linearDamping = value.AsScalar();
				break;
			case BodyProperty::AngularDamping:
				entry->params.angularDamping = value.AsScalar();
				break;
			case BodyProperty::GravityScale:
				entry->params.gravityScale = value.AsScalar();
				break;
			case BodyProperty::SleepThreshold:
				entry->params.sleepThreshold = value.AsScalar();
				break;
			case BodyProperty::IsBullet:
				entry->params.isBullet = value.AsFlag();
				break;
			case BodyProperty::FreezePositionX:
				entry->params.freezePositionX = value.AsFlag();
				break;
			case BodyProperty::FreezePositionY:
				entry->params.freezePositionY = value.AsFlag();
				break;
			case BodyProperty::FreezePositionZ:
				entry->params.freezePositionZ = value.AsFlag();
				break;
			case BodyProperty::FreezeRotationX:
				entry->params.freezeRotationX = value.AsFlag();
				break;
			case BodyProperty::FreezeRotationY:
				entry->params.freezeRotationY = value.AsFlag();
				break;
			case BodyProperty::FreezeRotationZ:
				entry->params.freezeRotationZ = value.AsFlag();
				break;
			}
		}

		PropertyValue GetBodyProperty(BodyHandle body, BodyProperty property) {
			StubBody* entry = ResolveBody(body);
			if (!entry) {
				return PropertyValue{};
			}
			switch (property) {
			case BodyProperty::LinearDamping:
				return PropertyValue::Scalar(entry->params.linearDamping);
			case BodyProperty::AngularDamping:
				return PropertyValue::Scalar(entry->params.angularDamping);
			case BodyProperty::GravityScale:
				return PropertyValue::Scalar(entry->params.gravityScale);
			case BodyProperty::SleepThreshold:
				return PropertyValue::Scalar(entry->params.sleepThreshold);
			case BodyProperty::IsBullet:
				return PropertyValue::Flag(entry->params.isBullet);
			case BodyProperty::FreezePositionX:
				return PropertyValue::Flag(entry->params.freezePositionX);
			case BodyProperty::FreezePositionY:
				return PropertyValue::Flag(entry->params.freezePositionY);
			case BodyProperty::FreezePositionZ:
				return PropertyValue::Flag(entry->params.freezePositionZ);
			case BodyProperty::FreezeRotationX:
				return PropertyValue::Flag(entry->params.freezeRotationX);
			case BodyProperty::FreezeRotationY:
				return PropertyValue::Flag(entry->params.freezeRotationY);
			case BodyProperty::FreezeRotationZ:
				return PropertyValue::Flag(entry->params.freezeRotationZ);
			}
			return PropertyValue{};
		}

		void SetBodyKind(BodyHandle body, BodyKind kind) {
			if (StubBody* entry = ResolveBody(body)) {
				entry->params.kind = kind;
				entry->params.mass = 0.0f;
				entry->awake = true;
				entry->sleepTime = 0.0f;
			}
		}

		BodyKind GetBodyKind(BodyHandle body) {
			StubBody* entry = ResolveBody(body);
			return entry ? entry->params.kind : BodyKind::Static;
		}

		i32 GetAllocatedBytes() {
			return -1;
		}

		bool CreateJoint(WorldHandle world, const JointParams& params, JointHandle& out) {
			out = JointHandle{};
			if (!Resolve(world)) {
				return false;
			}

			StubBody* bodyA = ResolveBody(params.bodyA);
			StubBody* bodyB = ResolveBody(params.bodyB);
			if (!bodyA || !bodyB) {
				return false;
			}

			for (u32 slot = 0; slot < STUB_JOINT_CAPACITY; ++slot) {
				StubJoint& entry = _joints[slot];
				if ((entry.generation & 1u) != 0u) {
					continue;
				}
				++entry.generation;
				entry.bodySlotA = static_cast<u32>((params.bodyA.bits & 0xFFFFFFFFull) - 1ull);
				entry.bodySlotB = static_cast<u32>((params.bodyB.bits & 0xFFFFFFFFull) - 1ull);
				entry.bodyGenerationA = bodyA->generation;
				entry.bodyGenerationB = bodyB->generation;
				entry.localAnchorA = params.localAnchorA;
				entry.localAnchorB = params.localAnchorB;
				entry.forceThreshold = params.forceThreshold;
				entry.owner = params.owner;
				entry.worldSlot = WorldSlot(world);
				entry.force = 0.0f;
				for (PropertyValue& slotValue : entry.properties) {
					slotValue = PropertyValue{};
				}
				entry.kind = params.kind;
				Record(entry, JointProperty::ForceThreshold, PropertyValue::Scalar(params.forceThreshold));
				Record(entry, JointProperty::TorqueThreshold, PropertyValue::Scalar(params.torqueThreshold));
				Record(entry, JointProperty::CollideConnected, PropertyValue::Flag(params.collideConnected));
				Record(entry, JointProperty::EnableSpring, PropertyValue::Flag(params.enableSpring));
				Record(entry, JointProperty::SpringHertz, PropertyValue::Scalar(params.springHertz));
				Record(entry, JointProperty::SpringDampingRatio, PropertyValue::Scalar(params.springDampingRatio));
				Record(entry, JointProperty::SpringTarget, PropertyValue::Scalar(params.springTarget));
				Record(entry, JointProperty::TargetRotation, PropertyValue::Rotation(params.targetRotation));
				Record(entry, JointProperty::SpringConstants,
					   PropertyValue::Pair(params.springStiffness, params.springDamping));
				Record(entry, JointProperty::EnableMotor, PropertyValue::Flag(params.enableMotor));
				Record(entry, JointProperty::MotorSpeed, PropertyValue::Scalar(params.motorSpeed));
				Record(entry, JointProperty::MaxMotorEffort, PropertyValue::Scalar(params.maxMotorEffort));
				Record(entry, JointProperty::EnableConeLimit, PropertyValue::Flag(params.enableConeLimit));
				Record(entry, JointProperty::ConeAngles, PropertyValue::Pair(params.coneAngleX, params.coneAngleY));
				if (params.kind == JointKind::Spherical) {
					Record(entry, JointProperty::EnableTwistLimit, PropertyValue::Flag(params.enableLimit));
					Record(entry, JointProperty::TwistLimits, PropertyValue::Pair(params.lowerLimit, params.upperLimit));
				} else {
					Record(entry, JointProperty::EnableLimit, PropertyValue::Flag(params.enableLimit));
					Record(entry, JointProperty::Limits, PropertyValue::Pair(params.lowerLimit, params.upperLimit));
				}
				Record(entry, JointProperty::LengthRange, PropertyValue::Pair(params.minLength, params.maxLength));
				out.bits = PackJoint(slot, entry.generation);
				return true;
			}
			return false;
		}

		void DestroyJoint(JointHandle joint, bool wakeAttached) {
			StubJoint* entry = ResolveJoint(joint);
			if (!entry) {
				return;
			}
			if (wakeAttached) {
				for (u32 slot : {entry->bodySlotA, entry->bodySlotB}) {
					if (slot < STUB_BODY_CAPACITY && (_bodies[slot].generation & 1u) != 0u) {
						_bodies[slot].awake = true;
						_bodies[slot].sleepTime = 0.0f;
					}
				}
			}
			const u16 next = static_cast<u16>(entry->generation + 1u);
			*entry = StubJoint{};
			entry->generation = next;
		}

		bool IsJointValid(JointHandle joint) {
			return ResolveJoint(joint) != nullptr;
		}

		f32 GetJointForce(JointHandle joint) {
			StubJoint* entry = ResolveJoint(joint);
			return entry ? entry->force : 0.0f;
		}

		void SetJointProperty(JointHandle joint, JointProperty property, const PropertyValue& value) {
			StubJoint* entry = ResolveJoint(joint);
			if (!entry || !JointKindAcceptsProperty(entry->kind, property)) {
				return;
			}
			Record(*entry, property, value);
		}

		PropertyValue GetJointProperty(JointHandle joint, JointProperty property) {
			StubJoint* entry = ResolveJoint(joint);
			const u32 index = static_cast<u32>(property);
			if (!entry || index >= JOINT_PROPERTY_COUNT || !JointKindAcceptsProperty(entry->kind, property)) {
				return PropertyValue{};
			}
			return entry->properties[index];
		}

		f32 GetBodyInverseMass(BodyHandle body) {
			StubBody* entry = ResolveBody(body);
			if (!entry || entry->params.kind != BodyKind::Dynamic || entry->params.mass <= 0.0f) {
				return 0.0f;
			}
			return 1.0f / entry->params.mass;
		}

		f32 GetBodyInverseInertiaAbout(BodyHandle body, const Vec3&) {
			StubBody* entry = ResolveBody(body);
			if (!entry || entry->params.kind != BodyKind::Dynamic || entry->params.mass <= 0.0f) {
				return 0.0f;
			}

			f32 largestRadius = 0.0f;
			const u32 bodySlot = static_cast<u32>((body.bits & 0xFFFFFFFFull) - 1ull);
			for (const StubShape& shape : _shapes) {
				if ((shape.generation & 1u) != 0u && shape.bodySlot == bodySlot && shape.bodyGeneration == entry->generation) {
					largestRadius = shape.boundingRadius > largestRadius ? shape.boundingRadius : largestRadius;
				}
			}
			if (largestRadius <= 0.0f) {
				return 0.0f;
			}
			return 1.0f / (0.4f * entry->params.mass * largestRadius * largestRadius);
		}

		void DrainJointEvents(WorldHandle world, JointEventSinkFn sink, void* user) {
			if (!Resolve(world) || !sink) {
				return;
			}
			for (u32 index = 0; index < _jointEventCount; ++index) {
				sink(user, _jointEvents[index]);
			}
		}

		bool CastRay(WorldHandle world, const Vec3& origin, const Vec3& translation,
					 const QueryFilter& filter, QueryHit& out) {
			const Vec3 points[2] = {origin, origin};
			if (!CastShape(world, points, 2, 0.0f, translation, filter, out)) {
				return false;
			}
			out.distance = (out.point - origin).Length();
			return true;
		}

		bool CastShape(WorldHandle world, const Vec3* points, u32 pointCount, f32 radius,
					   const Vec3& translation, const QueryFilter& filter, QueryHit& out) {
			out = QueryHit{};
			if (!Resolve(world) || !points || pointCount == 0 || pointCount > 2) {
				return false;
			}
			const u32 worldSlot = WorldSlot(world);
			const Vec3 queryPoint0 = points[0];
			const Vec3 queryPoint1 = points[pointCount - 1];
			const f32 translationLength = translation.Length();

			bool found = false;
			f32 bestFraction = 0.0f;
			for (const StubShape& shape : _shapes) {
				if ((shape.generation & 1u) == 0u) {
					continue;
				}
				if (shape.bodySlot >= STUB_BODY_CAPACITY || _bodies[shape.bodySlot].worldSlot != worldSlot) {
					continue;
				}
				if (!filter.hitTriggers && shape.isSensor) {
					continue;
				}
				if (!ShouldQueryCollide(shape, filter)) {
					continue;
				}

				Vec3 targetCenter;
				f32 targetRadius = 0.0f;
				if (!ShapeWorldSphere(shape, targetCenter, targetRadius)) {
					continue;
				}

				f32 fraction = 0.0f;
				Vec3 normal;
				bool initialOverlap = false;
				if (!SweptHit(queryPoint0, queryPoint1, radius, translation, targetCenter, targetRadius,
							  fraction, normal, initialOverlap)) {
					continue;
				}
				if (initialOverlap) {
					continue;
				}
				if (found && fraction >= bestFraction) {
					continue;
				}

				found = true;
				bestFraction = fraction;
				out.shapeOwner = shape.owner;
				out.normal = normal;
				out.point = targetCenter + normal * targetRadius;
				out.distance = fraction * translationLength;
			}
			return found;
		}

		u32 OverlapShape(WorldHandle world, const Vec3* points, u32 pointCount, f32 radius,
						 const QueryFilter& filter, QueryOverlapFn sink, void* user) {
			if (!Resolve(world) || !points || pointCount == 0 || pointCount > 2) {
				return 0;
			}
			const u32 worldSlot = WorldSlot(world);
			const Vec3 queryPoint0 = points[0];
			const Vec3 queryPoint1 = points[pointCount - 1];

			u32 count = 0;
			for (const StubShape& shape : _shapes) {
				if ((shape.generation & 1u) == 0u) {
					continue;
				}
				if (shape.bodySlot >= STUB_BODY_CAPACITY || _bodies[shape.bodySlot].worldSlot != worldSlot) {
					continue;
				}
				if (!filter.hitTriggers && shape.isSensor) {
					continue;
				}
				if (!ShouldQueryCollide(shape, filter)) {
					continue;
				}

				Vec3 targetCenter;
				f32 targetRadius = 0.0f;
				if (!ShapeWorldSphere(shape, targetCenter, targetRadius)) {
					continue;
				}

				f32 fraction = 0.0f;
				Vec3 normal;
				bool initialOverlap = false;
				if (!SweptHit(queryPoint0, queryPoint1, radius, Vec3{}, targetCenter, targetRadius,
							  fraction, normal, initialOverlap)) {
					continue;
				}

				++count;
				if (sink) {
					sink(user, shape.owner);
				}
			}
			return count;
		}

		void SetShapeEvents(ShapeHandle shape, bool contactEvents, bool sensorEvents) {
			StubShape* entry = ResolveShape(shape);
			if (!entry) {
				return;
			}
			entry->contactEvents = contactEvents;
			entry->sensorEvents = sensorEvents;
		}

		void DrainTouchEvents(WorldHandle world, TouchSinkFn sink, void* user) {
			if (!Resolve(world) || !sink) {
				return;
			}
			const u32 slot = WorldSlot(world);

			// begin before end
			for (u32 pass = 0; pass < 2; ++pass) {
				const bool wantBegin = pass == 0;
				for (u32 index = 0; index < _touchEventCount; ++index) {
					const StubTouchEvent& source = _touchEvents[index];
					if (source.worldSlot != slot || source.begin != wantBegin) {
						continue;
					}
					TouchEvent event;
					event.shapeOwnerA = source.ownerA;
					event.shapeOwnerB = source.ownerB;
					event.kind = source.sensor ? TouchKind::Sensor : TouchKind::Contact;
					event.begin = source.begin;
					sink(user, event);
				}
			}
		}

		bool GetContactGeometry(ShapeHandle self, ShapeHandle other, ContactGeometry& out) {
			out = ContactGeometry{};

			const StubShape* selfShape = ResolveShape(self);
			const StubShape* otherShape = ResolveShape(other);
			if (!selfShape || !otherShape) {
				return false;
			}

			Vec3 selfCenter;
			Vec3 otherCenter;
			f32 selfRadius = 0.0f;
			f32 otherRadius = 0.0f;
			if (!ShapeWorldSphere(*selfShape, selfCenter, selfRadius) ||
				!ShapeWorldSphere(*otherShape, otherCenter, otherRadius)) {
				return false;
			}

			const Vec3 delta = otherCenter - selfCenter;
			const f32 distance = delta.Length();
			if (distance > selfRadius + otherRadius) {
				return false;
			}

			out.normal = distance > 0.0f ? delta * (1.0f / distance) : Vec3{0.0f, 1.0f, 0.0f};
			out.separation[0] = distance - (selfRadius + otherRadius);
			out.point[0] = selfCenter + out.normal * (selfRadius + out.separation[0] * 0.5f);

			out.normalImpulse[0] = 0.0f;
			out.pointCount = 1;
			return true;
		}

#if defined(SLT_WITH_EDITOR)

		void EnableDebugDraw() {
		}

		void DrawWorld(WorldHandle, const DebugDrawOptions&, const DebugDrawSink&) {
		}

#endif

	} // namespace PhysicsBackend

} // namespace Shalltear
