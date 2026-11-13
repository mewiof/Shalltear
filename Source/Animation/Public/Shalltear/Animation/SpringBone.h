#pragma once

#include "Shalltear/Animation/AnimationAPI.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/Skeleton.h"
#include "Shalltear/Core/Graphics/SkeletonAsset.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/MathReflect.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	enum class SpringBoneColliderShape : u8 {
		Sphere = 0,
		Capsule = 1,
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::SpringBoneColliderShape)
SLT_ENUM_VALUE(Sphere)
SLT_ENUM_VALUE(Capsule)
SLT_END_ENUM()

namespace Shalltear {

	class SHALLTEAR_ANIMATION_API SpringBoneCollider : public Component {
	public:
		SLT_REFLECT_COMPONENT(SpringBoneCollider, Component)
		SLT_REFLECT_FIELD(_shape)
		SLT_REFLECT_FIELD(_radius, SLT_RANGE(0.0f, 10.0f))
		SLT_REFLECT_FIELD(_height, SLT_RANGE(0.0f, 10.0f))
		SLT_REFLECT_FIELD(_center)
		SLT_END_REFLECT()

		SpringBoneCollider() = default;
		~SpringBoneCollider() override;

		SpringBoneColliderShape GetShape() const { return _shape; }
		void SetShape(SpringBoneColliderShape shape) { _shape = shape; }

		f32 GetRadius() const { return _radius; }
		void SetRadius(f32 radius) { _radius = radius; }

		// capsule length along local Y, end caps included
		f32 GetHeight() const { return _height; }
		void SetHeight(f32 height) { _height = height; }

		Vec3 GetCenter() const { return _center; }
		void SetCenter(const Vec3& center) { _center = center; }
	private:
		SpringBoneColliderShape _shape = SpringBoneColliderShape::Sphere;
		f32 _radius = 0.05f;
		f32 _height = 0.2f;
		Vec3 _center{};
	};

	class SHALLTEAR_ANIMATION_API SpringBone : public Component {
	public:
		SLT_REFLECT_COMPONENT(SpringBone, Component)
		SLT_REFLECT_FIELD(_rootBone)
		SLT_REFLECT_FIELD(_stiffness, SLT_RANGE(0.0f, 1.0f))
		SLT_REFLECT_FIELD(_damping, SLT_RANGE(0.0f, 1.0f))
		SLT_REFLECT_FIELD(_gravity)
		SLT_REFLECT_FIELD(_maxAngleDeg, SLT_RANGE(0.0f, 180.0f))
		SLT_REFLECT_FIELD(_radius, SLT_RANGE(0.0f, 1.0f))
		SLT_REFLECT_FIELD(_weight, SLT_RANGE(0.0f, 1.0f))
		SLT_REFLECT_FIELD(_stepRate, SLT_RANGE(15.0f, 240.0f))
		SLT_REFLECT_ARRAY(_colliders)
		SLT_END_REFLECT()

		SpringBone() = default;
		~SpringBone() override;

		// chain root (bone of owner's Skeleton) + all bones below
		const str& GetRootBone() const { return _rootBone; }
		void SetRootBone(std::string_view bone);

		// share of distance to animated pose closed per step
		f32 GetStiffness() const { return _stiffness; }
		void SetStiffness(f32 stiffness) { _stiffness = stiffness; }

		// share of velocity lost per step
		f32 GetDamping() const { return _damping; }
		void SetDamping(f32 damping) { _damping = damping; }

		Vec3 GetGravity() const { return _gravity; }
		void SetGravity(const Vec3& gravity) { _gravity = gravity; }

		// widest swing from animated direction
		f32 GetMaxAngleDeg() const { return _maxAngleDeg; }
		void SetMaxAngleDeg(f32 degrees) { _maxAngleDeg = degrees; }

		// bone thickness for collisions
		f32 GetRadius() const { return _radius; }
		void SetRadius(f32 radius) { _radius = radius; }

		f32 GetWeight() const { return _weight; }
		void SetWeight(f32 weight) { _weight = weight; }

		// steps per second, independent of frame rate
		f32 GetStepRate() const { return _stepRate; }
		void SetStepRate(f32 hertz) { _stepRate = hertz; }

		u32 GetColliderCount() const { return static_cast<u32>(_colliders.size()); }
		Handle<SpringBoneCollider> GetCollider(u32 index) const {
			return index < _colliders.size() ? _colliders[index] : Handle<SpringBoneCollider>{};
		}
		void AddCollider(Handle<SpringBoneCollider> collider) { _colliders.push_back(collider); }
		void ClearColliders() { _colliders.clear(); }

		void Reset();

		void Simulate(f32 deltaSeconds);
	protected:
		void OnEnable() override;
		void OnDisable() override;
	private:
		struct Joint {
			u32 bone = 0;
			i32 parent = -1;
			i32 firstChild = -1;
			Vec3 position{};
			Vec3 previous{};
			Quat swing = Quat::Identity();
			BoneTransform base;
			BoneTransform written;
			bool wrote = false;
		};

		bool Bind(const SkeletonAsset& skeleton);

		str _rootBone;
		f32 _stiffness = 0.1f;
		f32 _damping = 0.1f;
		Vec3 _gravity{};
		f32 _maxAngleDeg = 60.0f;
		f32 _radius = 0.02f;
		f32 _weight = 1.0f;
		f32 _stepRate = 60.0f;
		Vector<Handle<SpringBoneCollider>> _colliders{GetMemoryResource(MemTag::Scene)};

		Vector<Joint> _joints{GetMemoryResource(MemTag::Scene)};
		Vector<Vec3> _offsets{GetMemoryResource(MemTag::Scene)};
		u64 _boundSkeleton = 0;
		f32 _accumulator = 0.0f;
		bool _primed = false;
		bool _warnedRoot = false;
		Vec3 _rootFrom{};
		Quat _rootRotationFrom = Quat::Identity();
	};

} // namespace Shalltear
