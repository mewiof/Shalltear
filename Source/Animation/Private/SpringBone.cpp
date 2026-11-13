#include "Shalltear/Animation/SpringBone.h"

#include "AnimationPass.h"

#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Graphics/Skeleton.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Time.h"

#include <algorithm>
#include <cmath>

namespace Shalltear {

	namespace {

		constexpr u32 MAX_STEPS_PER_FRAME = 8;

		Vector<Handle<SpringBone>>& LiveSprings() {
			static Vector<Handle<SpringBone>> live{GetMemoryResource(MemTag::Scene)};
			return live;
		}

		Quat RotationBetween(const Vec3& from, const Vec3& to) {
			const f32 lengths = std::sqrt(from.LengthSquared() * to.LengthSquared());
			if (lengths < 1e-12f) {
				return Quat::Identity();
			}
			const f32 dot = Vec3::Dot(from, to);
			if (dot >= lengths * 0.999999f) {
				return Quat::Identity();
			}
			if (dot <= -lengths * 0.999999f) {
				Vec3 axis = Vec3::Cross(Vec3(1.0f, 0.0f, 0.0f), from);
				if (axis.LengthSquared() < 1e-8f) {
					axis = Vec3::Cross(Vec3(0.0f, 1.0f, 0.0f), from);
				}
				return Quat::AngleAxis(180.0f, axis);
			}
			const Vec3 axis = Vec3::Cross(from, to);
			return Quat(axis.x, axis.y, axis.z, lengths + dot).Normalized();
		}

		Vec3 ClosestOnSegment(const Vec3& point, const Vec3& a, const Vec3& b) {
			const Vec3 ab = b - a;
			const f32 span = ab.LengthSquared();
			const f32 t = span > 0.0f ? Math::Clamp01(Vec3::Dot(point - a, ab) / span) : 0.0f;
			return a + ab * t;
		}

		Vec3 PushOut(Vec3 point, const SpringBoneCollider& collider, f32 boneRadius) {
			const SceneObject* owner = collider.GetOwner().Get();
			if (!owner) {
				return point;
			}
			const Vec3 scale = owner->GetLossyScale();
			const f32 widest = Math::Max(Math::Abs(scale.x), Math::Max(Math::Abs(scale.y), Math::Abs(scale.z)));
			const f32 reach = collider.GetRadius() * widest + boneRadius;
			const Vec3 center = owner->TransformPoint(collider.GetCenter());
			Vec3 nearest = center;
			if (collider.GetShape() == SpringBoneColliderShape::Capsule) {
				const f32 half = Math::Max(collider.GetHeight() * 0.5f - collider.GetRadius(), 0.0f);
				const Vec3 axis = owner->TransformDirection(Vec3::Up()).Normalized() * (half * widest);
				nearest = ClosestOnSegment(point, center - axis, center + axis);
			}
			const Vec3 away = point - nearest;
			const f32 distance = away.Length();
			if (distance >= reach) {
				return point;
			}
			return nearest + (distance > 1e-6f ? away / distance : Vec3::Up()) * reach;
		}

		void SimulateAll(const FrameStageArgs&, void*) {
			const f32 delta = Time::GetDeltaTime();
			Vector<Handle<SpringBone>>& live = LiveSprings();
			for (size_t i = 0; i < live.size(); ++i) {
				if (SpringBone* spring = live[i].Get()) {
					spring->Simulate(delta);
				}
			}
		}

	} // anonymous namespace

	namespace AnimationImpl {

		void RegisterSpringBones(const void* owner) {
			FrameStages::Register(FrameStage::LateUpdate, FrameStagePosition::Early, &SimulateAll, nullptr, owner);
		}

		void ShutdownSpringBones() {
			LiveSprings().clear();
			LiveSprings().shrink_to_fit();
		}

	} // namespace AnimationImpl

	SpringBoneCollider::~SpringBoneCollider() = default;

	SpringBone::~SpringBone() {
		std::erase(LiveSprings(), Handle<SpringBone>(this));
	}

	void SpringBone::SetRootBone(std::string_view bone) {
		if (_rootBone != bone) {
			_rootBone = str(bone);
			_joints.clear();
			_boundSkeleton = 0;
			_primed = false;
			_warnedRoot = false;
		}
	}

	void SpringBone::OnEnable() {
		Vector<Handle<SpringBone>>& live = LiveSprings();
		const Handle<SpringBone> self(this);
		if (std::find(live.begin(), live.end(), self) == live.end()) {
			live.push_back(self);
		}
		_primed = false;
	}

	void SpringBone::OnDisable() {
		std::erase(LiveSprings(), Handle<SpringBone>(this));
	}

	void SpringBone::Reset() {
		_primed = false;
	}

	bool SpringBone::Bind(const SkeletonAsset& skeleton) {
		const u64 id = skeleton.GetInstanceId() ^ static_cast<u64>(skeleton.GetBoneCount()) << 48;
		if (id == _boundSkeleton && !_joints.empty()) {
			return true;
		}
		_boundSkeleton = id;
		_joints.clear();
		_primed = false;
		const i32 root = skeleton.FindBone(_rootBone);
		if (root < 0) {
			if (!_warnedRoot) {
				const SceneObject* owner = GetOwner().Get();
				SLT_LOG_WARN("Animation", "SpringBone on '{}': Skeleton has no bone '{}' (nothing simulated)",
							 owner ? std::string_view(owner->name) : std::string_view(), _rootBone);
				_warnedRoot = true;
			}
			return false;
		}
		Vector<i32> jointOf{GetMemoryResource(MemTag::Scene)};
		jointOf.assign(skeleton.GetBoneCount(), -1);
		Joint& first = _joints.emplace_back();
		first.bone = static_cast<u32>(root);
		jointOf[static_cast<size_t>(root)] = 0;
		for (u32 bone = static_cast<u32>(root) + 1; bone < skeleton.GetBoneCount(); ++bone) {
			const i16 parent = skeleton.parents[bone];
			if (parent < 0 || jointOf[static_cast<size_t>(parent)] < 0) {
				continue;
			}
			const i32 parentJoint = jointOf[static_cast<size_t>(parent)];
			jointOf[bone] = static_cast<i32>(_joints.size());
			Joint& joint = _joints.emplace_back();
			joint.bone = bone;
			joint.parent = parentJoint;
			if (_joints[static_cast<size_t>(parentJoint)].firstChild < 0) {
				_joints[static_cast<size_t>(parentJoint)].firstChild = jointOf[bone];
			}
		}
		return _joints.size() > 1;
	}

	bool SpringBone::BindObjects(SceneObject& owner) {
		bool bound = _boundSkeleton == 0 && !_joints.empty() && _joints[0].object.Get() == &owner;
		for (size_t j = 0; bound && j < _joints.size(); ++j) {
			const Joint& joint = _joints[j];
			const SceneObject* object = joint.object.Get();
			bound = object && object->GetChildCount() == joint.children &&
					(j == 0 || object->GetParent() == _joints[static_cast<size_t>(joint.parent)].object);
		}
		if (bound) {
			return _joints.size() > 1;
		}
		_boundSkeleton = 0;
		_joints.clear();
		_primed = false;
		_joints.emplace_back().object = Handle<SceneObject>(&owner);
		for (size_t j = 0; j < _joints.size(); ++j) {
			const SceneObject* object = _joints[j].object.Get();
			const u32 children = object->GetChildCount();
			_joints[j].children = children;
			for (u32 child = 0; child < children; ++child) {
				if (_joints[j].firstChild < 0) {
					_joints[j].firstChild = static_cast<i32>(_joints.size());
				}
				Joint& joint = _joints.emplace_back();
				joint.object = object->GetChild(child);
				joint.parent = static_cast<i32>(j);
			}
		}
		return _joints.size() > 1;
	}

	void SpringBone::Simulate(f32 deltaSeconds) {
		SceneObject* owner = GetOwner().Get();
		if (!owner) {
			return;
		}
		Skeleton* rig = owner->GetComponent<Skeleton>().Get();
		std::span<const BoneTransform> local;
		Mat4 above = Mat4::Identity();
		if (rig) {
			const SkeletonAsset* skeleton = rig->GetSkeleton().Get();
			if (!skeleton || !Bind(*skeleton)) {
				return;
			}
			rig->UpdatePose();
			local = rig->GetLocalPose();
			const std::span<const Mat4> model = rig->GetModelPose();
			if (local.size() < skeleton->GetBoneCount() || model.size() < skeleton->GetBoneCount()) {
				return;
			}
			const i16 rootParent = skeleton->parents[_joints[0].bone];
			const Mat4 world = owner->GetWorldMatrix();
			above = rootParent >= 0 ? world * model[static_cast<size_t>(rootParent)] : world;
		} else {
			if (!BindObjects(*owner)) {
				return;
			}
			if (const SceneObject* parent = owner->GetParent().Get()) {
				above = parent->GetWorldMatrix();
			}
		}

		const size_t count = _joints.size();
		for (Joint& joint : _joints) {
			BoneTransform current;
			if (rig) {
				current = local[joint.bone];
			} else {
				const SceneObject* object = joint.object.Get();
				current = BoneTransform{object->GetLocalPosition(), object->GetLocalRotation(), object->GetLocalScale()};
			}
			if (!joint.wrote || !(current.rotation == joint.written.rotation)) {
				joint.base = current;
			} else {
				joint.base.position = current.position;
				joint.base.scale = current.scale;
			}
		}
		auto write = [rig](Joint& joint, const BoneTransform& pose) {
			if (rig) {
				rig->SetBoneLocalPose(joint.bone, pose);
			} else if (SceneObject* object = joint.object.Get(); !(object->GetLocalRotation() == pose.rotation)) {
				object->SetLocalRotation(pose.rotation);
			}
			joint.written = pose;
		};

		Vector<Mat4> animated{GetMemoryResource(MemTag::Scene)};
		animated.resize(count);
		Vector<Vec3> positions{GetMemoryResource(MemTag::Scene)};
		positions.resize(count);
		for (size_t j = 0; j < count; ++j) {
			const Mat4& parent = _joints[j].parent >= 0 ? animated[static_cast<size_t>(_joints[j].parent)] : above;
			animated[j] = parent * _joints[j].base.ToMatrix();
			positions[j] = animated[j].TransformPoint(Vec3{});
		}
		Vec3 ignoredPosition;
		Vec3 ignoredScale;
		Quat aboveRotation = Quat::Identity();
		Quat rootRotation = Quat::Identity();
		(void)Mat4::DecomposeTRS(above, ignoredPosition, aboveRotation, ignoredScale);
		(void)Mat4::DecomposeTRS(animated[0], ignoredPosition, rootRotation, ignoredScale);

		_offsets.resize(count);
		const Quat toRoot = rootRotation.Inverse();
		for (size_t j = 0; j < count; ++j) {
			_offsets[j] = toRoot * (positions[j] - positions[0]);
		}

		if (!_primed) {
			for (size_t j = 0; j < count; ++j) {
				_joints[j].position = positions[j];
				_joints[j].previous = positions[j];
				_joints[j].swing = Quat::Identity();
			}
			_accumulator = 0.0f;
			_rootFrom = positions[0];
			_rootRotationFrom = rootRotation;
			_primed = true;
		}

		const f32 step = 1.0f / Math::Clamp(_stepRate, 15.0f, 240.0f);
		_accumulator += Math::Max(deltaSeconds, 0.0f);
		u32 steps = static_cast<u32>(_accumulator / step);
		if (steps > MAX_STEPS_PER_FRAME) {
			steps = MAX_STEPS_PER_FRAME;
			_accumulator = 0.0f;
		} else {
			_accumulator -= static_cast<f32>(steps) * step;
		}

		const f32 stiffness = Math::Clamp01(_stiffness);
		const f32 keep = 1.0f - Math::Clamp01(_damping);
		const f32 maxAngle = Math::Clamp(_maxAngleDeg, 0.0f, 180.0f) * Math::DEG_TO_RAD;
		const Vec3 fall = _gravity * (step * step);
		Vector<Vec3> rest{GetMemoryResource(MemTag::Scene)};
		rest.resize(count);
		for (u32 s = 0; s < steps; ++s) {
			const f32 t = static_cast<f32>(s + 1) / static_cast<f32>(steps);
			const Vec3 rootAt = Vec3::Lerp(_rootFrom, positions[0], t);
			const Quat turnAt = Quat::Slerp(_rootRotationFrom, rootRotation, t);
			for (size_t j = 0; j < count; ++j) {
				rest[j] = rootAt + turnAt * _offsets[j];
			}
			_joints[0].previous = _joints[0].position;
			_joints[0].position = rootAt;
			for (size_t j = 1; j < count; ++j) {
				Joint& joint = _joints[j];
				const Joint& parent = _joints[static_cast<size_t>(joint.parent)];
				const Vec3 offset = rest[j] - rest[static_cast<size_t>(joint.parent)];
				const f32 length = offset.Length();
				const Quat inherited = parent.parent >= 0 ? _joints[static_cast<size_t>(parent.parent)].swing : Quat::Identity();
				const Vec3 goal = parent.position + inherited * offset;
				Vec3 next = joint.position + (joint.position - joint.previous) * keep + (goal - joint.position) * stiffness + fall;

				const Vec3 goalDirection = goal - parent.position;
				Vec3 direction = next - parent.position;
				if (direction.LengthSquared() < 1e-12f) {
					direction = goalDirection;
				}
				if (maxAngle < Math::PI && goalDirection.LengthSquared() > 1e-12f) {
					const f32 cosine = Math::Clamp(Vec3::Dot(direction.Normalized(), goalDirection.Normalized()), -1.0f, 1.0f);
					const f32 angle = std::acos(cosine);
					if (angle > maxAngle) {
						const Quat back = Quat::Slerp(Quat::Identity(), RotationBetween(direction, goalDirection), 1.0f - maxAngle / angle);
						direction = back * direction;
					}
				}
				next = parent.position + direction.Normalized() * length;
				for (const Handle<SpringBoneCollider>& handle : _colliders) {
					if (const SpringBoneCollider* collider = handle.Get()) {
						next = PushOut(next, *collider, _radius);
					}
				}
				const Vec3 settled = next - parent.position;
				if (settled.LengthSquared() > 1e-12f) {
					next = parent.position + settled.Normalized() * length;
				}
				joint.previous = joint.position;
				joint.position = next;
			}
			for (size_t j = 0; j < count; ++j) {
				Joint& joint = _joints[j];
				const Quat inherited = joint.parent >= 0 ? _joints[static_cast<size_t>(joint.parent)].swing : Quat::Identity();
				if (joint.firstChild < 0) {
					joint.swing = inherited;
					continue;
				}
				const size_t child = static_cast<size_t>(joint.firstChild);
				const Vec3 animatedDirection = inherited * (rest[child] - rest[j]);
				joint.swing = RotationBetween(animatedDirection, _joints[child].position - joint.position) * inherited;
			}
		}
		_rootFrom = positions[0];
		_rootRotationFrom = rootRotation;

		const f32 weight = Math::Clamp01(_weight);
		if (weight <= 0.0f) {
			for (Joint& joint : _joints) {
				if (joint.wrote) {
					write(joint, joint.base);
				}
			}
			if (rig) {
				rig->UpdatePose();
			}
			return;
		}

		const f32 alpha = Math::Clamp01(_accumulator / step);
		Vector<Vec3> drawn{GetMemoryResource(MemTag::Scene)};
		drawn.resize(count);
		drawn[0] = positions[0];
		for (size_t j = 1; j < count; ++j) {
			const Vec3 simulated = Vec3::Lerp(_joints[j].previous, _joints[j].position, alpha);
			drawn[j] = Vec3::Lerp(positions[j], simulated, weight);
		}

		Vector<Quat> finals{GetMemoryResource(MemTag::Scene)};
		finals.resize(count);
		for (size_t j = 0; j < count; ++j) {
			Joint& joint = _joints[j];
			const Quat parentFinal = joint.parent >= 0 ? finals[static_cast<size_t>(joint.parent)] : aboveRotation;
			const Quat inherited = parentFinal * joint.base.rotation;
			Quat final = inherited;
			if (joint.firstChild >= 0) {
				const size_t child = static_cast<size_t>(joint.firstChild);
				const Vec3 restDirection = inherited * _joints[child].base.position;
				final = RotationBetween(restDirection, drawn[child] - drawn[j]) * inherited;
			}
			finals[j] = final;
			BoneTransform written = joint.base;
			written.rotation = (parentFinal.Inverse() * final).Normalized();
			write(joint, written);
			joint.wrote = true;
		}
		if (rig) {
			rig->UpdatePose();
		}
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::SpringBoneCollider)
SLT_REGISTER_TYPE(Shalltear::SpringBone)
