#include "Shalltear/Physics3D/CharacterController.h"

#include "PhysicsBackend.h"
#include "PhysicsSettings.h"
#include "PhysicsWorld.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Time.h"

#include <cmath>
#include <span>

namespace Shalltear {

	namespace {

		constexpr u32 PLANE_CAPACITY = 32;
		constexpr u32 CONTACT_CAPACITY = 16;
		constexpr u32 SLIDE_ITERATIONS = 5;
		constexpr u32 SOLVE_ITERATIONS = 20;

		constexpr f32 SOLVE_TOLERANCE = 1.0e-4f;
		constexpr f32 MOVE_TOLERANCE = 1.0e-4f;
		constexpr f32 OPPOSE_EPSILON = 1.0e-3f;
		constexpr f32 WALKABLE_EPSILON = 1.0e-4f;
		constexpr f32 STEP_TOLERANCE = 1.0e-3f;
		constexpr f32 STEP_PROBE = 0.01f;
		constexpr f32 SUPPORT_PROBE_RISE = 0.02f;
		constexpr f32 SUPPORT_PROBE_INSET = 0.01f;
		constexpr f32 NO_CEILING = 3.0e38f;

		constexpr f32 SIDE_EPSILON = 0.05f;

		enum class PassKind : u8 {
			Up,
			Side,
			Down,
		};

		bool IsWalkable(f32 up, f32 walkableCos) {
			return up > SIDE_EPSILON && up >= walkableCos - WALKABLE_EPSILON;
		}

		struct SolverPlane {
			Vec3 normal{};
			f32 offset = 0.0f;
			f32 push = 0.0f;
		};

		// resting overlap (slop) should not push
		SolverPlane ToSolverPlane(const PhysicsBackend::MoverPlane& plane, PassKind kind, bool supported, f32 slop) {
			const f32 up = plane.normal.y;
			const f32 excess = plane.offset - slop;

			if (kind == PassKind::Side && up > SIDE_EPSILON && !supported) {
				const Vec3 flat{plane.normal.x, 0.0f, plane.normal.z};
				const f32 length = flat.Length();
				return SolverPlane{flat * (1.0f / length), excess / length};
			}
			if (kind == PassKind::Down && supported) {
				return SolverPlane{Vec3::Up(), excess / up};
			}
			return SolverPlane{plane.normal, excess};
		}

		Vec3 SolvePlanes(const Vec3& target, SolverPlane* planes, u32 count) {
			Vec3 delta = target;
			for (u32 iteration = 0; iteration < SOLVE_ITERATIONS; ++iteration) {
				f32 total = 0.0f;
				for (u32 index = 0; index < count; ++index) {
					SolverPlane& plane = planes[index];
					const f32 separation = Vec3::Dot(plane.normal, delta) - plane.offset;
					const f32 accumulated = plane.push;
					plane.push = Math::Max(0.0f, accumulated - separation);
					const f32 applied = plane.push - accumulated;
					delta += plane.normal * applied;
					total += Math::Abs(applied);
				}
				if (total < SOLVE_TOLERANCE) {
					break;
				}
			}
			return delta;
		}

		struct PassResult {
			ControllerContact hits[CONTACT_CAPACITY];
			u32 hitCount = 0;

			CollisionFlags flags = CollisionFlags::None;

			bool landed = false;

			bool walled = false;

			u64 groundOwner = 0;
			Vec3 groundNormal{};

			void Note(const PhysicsBackend::MoverPlane& plane, const Vec3& direction, f32 travelled, bool supported) {
				const f32 up = plane.normal.y;
				if (up > SIDE_EPSILON) {
					flags |= CollisionFlags::Below;
				} else if (up < -SIDE_EPSILON) {
					flags |= CollisionFlags::Above;
				} else {
					flags |= CollisionFlags::Sides;
				}

				if (supported) {
					if (!landed || up > groundNormal.y) {
						groundOwner = plane.shapeOwner;
						groundNormal = plane.normal;
					}
					landed = true;
				} else if (up >= -SIDE_EPSILON) {
					walled = true;
				}

				AddHit(ControllerContact{plane.shapeOwner, plane.point, plane.normal, direction, travelled});
			}

			void AddHit(const ControllerContact& contact) {
				for (u32 index = 0; index < hitCount; ++index) {
					if (hits[index].shapeOwner == contact.shapeOwner) {
						return;
					}
				}
				if (hitCount < CONTACT_CAPACITY) {
					hits[hitCount++] = contact;
				}
			}

			void Merge(const PassResult& other) {
				flags |= other.flags;
				if (other.landed && (!landed || other.groundNormal.y > groundNormal.y)) {
					groundOwner = other.groundOwner;
					groundNormal = other.groundNormal;
				}
				landed = landed || other.landed;
				walled = walled || other.walled;
				for (u32 index = 0; index < other.hitCount; ++index) {
					AddHit(other.hits[index]);
				}
			}
		};

		struct Mover {
			PhysicsWorld& world;
			const CharacterController& controller;
			PhysicsBackend::MoverCapsule capsule;
			f32 walkableCos = 0.0f;
			f32 slop = 0.0f;
		};

		// edge of walkable surface counts as support
		bool ProbeSupport(const Mover& mover, const PhysicsBackend::MoverPlane& plane) {
			Vec3 inward{-plane.normal.x, 0.0f, -plane.normal.z};
			const f32 length = inward.Length();
			inward = length > 1.0e-4f ? inward * (1.0f / length) : Vec3{};
			const Vec3 origin = plane.point + Vec3::Up() * SUPPORT_PROBE_RISE + inward * SUPPORT_PROBE_INSET;
			Vec3 surface;
			if (!mover.world.ProbeController(mover.controller, origin, Vec3::Up() * (-2.0f * SUPPORT_PROBE_RISE), surface)) {
				return false;
			}
			return IsWalkable(surface.y, mover.walkableCos);
		}

		Vec3 Slide(const Mover& mover, const Vec3& start, const Vec3& motion, PassKind kind, PassResult& result,
				   f32 supportCeiling = NO_CEILING) {
			const f32 length = motion.Length();
			if (!(length > 0.0f)) {
				return start;
			}
			const Vec3 direction = motion * (1.0f / length);
			const Vec3 target = start + motion;

			PhysicsBackend::MoverPlane contacts[PLANE_CAPACITY];
			SolverPlane planes[PLANE_CAPACITY];
			bool supported[PLANE_CAPACITY];

			Vec3 position = start;
			f32 travelled = 0.0f;
			for (u32 iteration = 0;; ++iteration) {
				const u32 count = mover.world.CollideController(mover.controller, position, mover.capsule, contacts, PLANE_CAPACITY);
				for (u32 index = 0; index < count; ++index) {
					const PhysicsBackend::MoverPlane& contact = contacts[index];
					bool support = IsWalkable(contact.normal.y, mover.walkableCos);
					if (kind == PassKind::Down) {
						if (contact.point.y > supportCeiling) {
							support = false;
						} else if (!support && contact.normal.y > SIDE_EPSILON) {
							support = ProbeSupport(mover, contact);
						}
					}
					supported[index] = support;
					planes[index] = ToSolverPlane(contact, kind, support, mover.slop);
				}

				const Vec3 delta = SolvePlanes(target - position, planes, count);

				for (u32 index = 0; index < count; ++index) {
					if (planes[index].push > 0.0f && Vec3::Dot(planes[index].normal, direction) < -OPPOSE_EPSILON) {
						result.Note(contacts[index], direction, travelled, supported[index]);
					}
				}

				if (iteration == SLIDE_ITERATIONS || delta.LengthSquared() < MOVE_TOLERANCE * MOVE_TOLERANCE) {
					break;
				}

				const f32 fraction = mover.world.CastController(mover.controller, position, mover.capsule, delta);
				position += delta * fraction;
				travelled += delta.Length() * fraction;
			}
			return position;
		}

		Quat YawOf(const Quat& rotation) {
			const f32 length = Math::Sqrt(rotation.y * rotation.y + rotation.w * rotation.w);
			if (length < 1.0e-6f) {
				return Quat::Identity();
			}
			return Quat{0.0f, rotation.y / length, 0.0f, rotation.w / length};
		}

	} // anonymous namespace

	CharacterController::~CharacterController() {
		if (_bodyBits != 0) {
			GetPhysicsWorld().RemoveController(*this);
		}
	}

	void CharacterController::OnEnable() {
		_hasMoved = false;
		if (_stepOffset > _height) {
			const SceneObject* owner = GetOwner().Get();
			SLT_LOG_WARN("Physics", "'{}' has a CharacterController step offset ({}) above its height ({}). Lower the step offset",
						 owner ? owner->name : str("<destroyed>"), _stepOffset, _height);
		}
		GetPhysicsWorld().AddController(*this);
	}

	void CharacterController::OnDisable() {
		GetPhysicsWorld().RemoveController(*this);
	}

	void CharacterController::SetCenter(const Vec3& center) {
		_center = center;
		GetPhysicsWorld().RebuildShape(*this);
	}

	void CharacterController::SetRadius(f32 radius) {
		_radius = radius;
		GetPhysicsWorld().RebuildShape(*this);
	}

	void CharacterController::SetHeight(f32 height) {
		_height = height;
		GetPhysicsWorld().RebuildShape(*this);
	}

	void CharacterController::SetDetectCollisions(bool detect) {
		_detectCollisions = detect;
		GetPhysicsWorld().RebuildShape(*this);
	}

	bool CharacterController::DescribeShape(const Vec3& lossyScale, PhysicsShapeRequest& out) const {
		const f32 across = Math::Max(Math::Abs(lossyScale.x), Math::Abs(lossyScale.z));
		const f32 along = Math::Abs(lossyScale.y);

		out.kind = PhysicsShapeKind::Capsule;
		out.radius = _radius * across;

		const f32 halfSegment = Math::Max(0.0f, _height * along * 0.5f - out.radius);
		const Vec3 center = _center * lossyScale;
		out.localPosition = center;
		out.capsulePoint0 = center + Vec3{0.0f, halfSegment, 0.0f};
		out.capsulePoint1 = center - Vec3{0.0f, halfSegment, 0.0f};
		return out.radius > 0.0f;
	}

	CollisionFlags CharacterController::Move(const Vec3& motion) {
		SceneObject* owner = GetOwner().Get();
		if (!owner) {
			return CollisionFlags::None;
		}
		if (_bodyBits == 0) {
			if (!_reportedOutside) {
				_reportedOutside = true;
				SLT_LOG_WARN("Physics", "'{}' called Move on a CharacterController outside the simulation (ignored)", owner->name);
			}
			return CollisionFlags::None;
		}

		PhysicsWorld& world = GetPhysicsWorld();

		PhysicsShapeRequest shape;
		const Vec3 scale = owner->GetLossyScale();
		if (!DescribeShape(scale, shape)) {
			return CollisionFlags::None;
		}

		const Vec3 start = owner->GetWorldPosition();
		if (_hasMoved && start != _movedPosition) {
			_carrier = {};
		}
		world.PlaceController(*this);

		Vec3 carry{};
		Quat turn = Quat::Identity();
		if (Rigidbody* carrier = _carrier.Get()) {
			if (const SceneObject* carrierObject = carrier->GetOwner().Get()) {
				const Quat rotation = carrierObject->GetWorldRotation();
				carry = carrierObject->GetWorldPosition() + rotation * _carrierAnchor - start;
				turn = YawOf(rotation * _carrierRotation.Inverse());
			}
		}

		const bool wasGrounded = IsGrounded();
		const Vec3 requested = motion.Length() < _minMoveDistance ? Vec3{} : motion;
		const Vec3 total = requested + carry;

		Mover mover{world, *this, {}, 0.0f, PhysicsBackend::GetMoverSlop()};
		mover.capsule.center1 = shape.capsulePoint0;
		mover.capsule.center2 = shape.capsulePoint1;
		mover.capsule.radius = shape.radius + Math::Max(0.0f, _skinWidth);
		mover.walkableCos = std::cos(Math::DegToRad(Math::Clamp(_slopeLimit, 0.0f, 180.0f)));

		const f32 stepOffset = Math::Clamp(_stepOffset, 0.0f, _height * Math::Abs(scale.y));

		PassResult moved;
		Vec3 position = start;

		const f32 rise = total.y;
		if (rise > 0.0f) {
			PassResult up;
			position = Slide(mover, position, Vec3::Up() * rise, PassKind::Up, up);
			moved.Merge(up);
		}

		const Vec3 horizontal{total.x, 0.0f, total.z};
		const f32 horizontalLength = horizontal.Length();
		if (horizontalLength > 0.0f) {
			PassResult plain;
			const Vec3 plainEnd = Slide(mover, position, horizontal, PassKind::Side, plain);

			bool stepped = false;
			if (wasGrounded && stepOffset > 0.0f && rise <= 0.0f && plain.walled) {
				const Vec3 direction = horizontal * (1.0f / horizontalLength);
				const f32 plainProgress = Vec3::Dot(plainEnd - position, direction);
				if (plainProgress < horizontalLength - STEP_TOLERANCE) {
					PassResult lift;
					const Vec3 lifted = Slide(mover, position, Vec3::Up() * stepOffset, PassKind::Up, lift);
					const f32 raised = lifted.y - position.y;
					if (raised > STEP_TOLERANCE) {
						PassResult across;
						const Vec3 over = Slide(mover, lifted, horizontal, PassKind::Side, across);
						const f32 foot = position.y + Math::Min(mover.capsule.center1.y, mover.capsule.center2.y) - mover.capsule.radius;
						PassResult down;
						const Vec3 landed = Slide(mover, over, Vec3::Up() * -(raised + STEP_PROBE), PassKind::Down, down,
												  foot + stepOffset + STEP_TOLERANCE);
						if (down.landed && Vec3::Dot(landed - position, direction) > plainProgress + STEP_TOLERANCE) {
							position = landed;
							moved.Merge(across);
							moved.Merge(down);
							stepped = true;
						}
					}
				}
			}
			if (!stepped) {
				position = plainEnd;
				moved.Merge(plain);
			}
		}

		if (rise < 0.0f) {
			PassResult down;
			position = Slide(mover, position, Vec3::Up() * rise, PassKind::Down, down);
			moved.Merge(down);
		}

		const f32 deltaTime = Time::GetDeltaTime();
		_velocity = deltaTime > 0.0f ? (position - start - carry) * (1.0f / deltaTime) : Vec3{};
		_collisionFlags = moved.flags;

		if (position != start) {
			owner->SetWorldPosition(position);
		}
		if (turn != Quat::Identity()) {
			owner->SetWorldRotation((turn * owner->GetWorldRotation()).Normalized());
		}
		world.PlaceController(*this);

		const Vec3 placed = owner->GetWorldPosition();
		_carrier = moved.landed ? world.FindCarrier(moved.groundOwner, moved.groundNormal) : Handle<Rigidbody>{};
		if (const SceneObject* carrierObject = _carrier.Get() ? _carrier->GetOwner().Get() : nullptr) {
			_carrierRotation = carrierObject->GetWorldRotation();
			_carrierAnchor = _carrierRotation.Inverse() * (placed - carrierObject->GetWorldPosition());
		} else {
			_carrier = {};
		}
		_movedPosition = placed;
		_hasMoved = true;

		const CollisionFlags flags = _collisionFlags;
		if (moved.hitCount > 0) {
			world.DispatchControllerHits(Handle<CharacterController>(this),
										 std::span<const ControllerContact>(moved.hits, moved.hitCount));
		}
		return flags;
	}

	bool CharacterController::SimpleMove(const Vec3& speed) {
		const PhysicsSettings* settings = GetPhysicsWorld().GetSettings();
		const Vec3 gravity = settings ? Vec3{settings->gravityX, settings->gravityY, settings->gravityZ}
									  : Vec3{0.0f, -9.81f, 0.0f};
		const f32 deltaTime = Time::GetDeltaTime();
		if (IsGrounded()) {
			_fallVelocity = Vec3{};
		}
		_fallVelocity += gravity * deltaTime;
		const CollisionFlags flags = Move((Vec3{speed.x, 0.0f, speed.z} + _fallVelocity) * deltaTime);
		return HasAnyFlag(flags, CollisionFlags::Below);
	}

} // namespace Shalltear
