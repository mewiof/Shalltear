#pragma once

#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/MathReflect.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Physics3D/Physics3DAPI.h"
#include "Shalltear/Physics3D/PhysicsCategoryMatch.h"
#include "Shalltear/Physics3D/PhysicsMaterial.h"

namespace Shalltear {

	struct PhysicsShapeRequest;

	enum class CapsuleDirection : u8 {
		XAxis = 0,
		YAxis = 1,
		ZAxis = 2,
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::CapsuleDirection)
SLT_ENUM_VALUE(XAxis)
SLT_ENUM_VALUE(YAxis)
SLT_ENUM_VALUE(ZAxis)
SLT_END_ENUM()

namespace Shalltear {

	class SHALLTEAR_PHYSICS3D_API Collider : public Component {
	public:
		SLT_REFLECT_COMPONENT_ABSTRACT(Collider, Component)
		SLT_REFLECT_FIELD(_isTrigger)
		SLT_REFLECT_FIELD(_material)
		SLT_REFLECT_FIELD(_categories)
		SLT_REFLECT_FIELD(_collidesWithMode)
		SLT_REFLECT_FIELD(_collidesWith)
		SLT_END_REFLECT()

		~Collider() override;

		bool IsTrigger() const { return _isTrigger; }
		void SetTrigger(bool trigger);

		Handle<PhysicsMaterial> GetMaterial() const { return _material; }
		void SetMaterial(Handle<PhysicsMaterial> material);

		// comma separated category names
		const str& GetCategories() const { return _categories; }
		void SetCategories(const str& categories);

		PhysicsCategoryMatch GetCollidesWithMode() const { return _collidesWithMode; }
		void SetCollidesWithMode(PhysicsCategoryMatch mode);
		const str& GetCollidesWith() const { return _collidesWith; }
		void SetCollidesWith(const str& collidesWith);

		bool IsInSimulation() const { return _shapeBits != 0; }

		virtual bool DescribeShape(const Vec3& lossyScale, PhysicsShapeRequest& out) const = 0;
	protected:
		void OnEnable() override;
		void OnDisable() override;
	private:
		friend class PhysicsWorld;

		bool _isTrigger = false;
		Handle<PhysicsMaterial> _material;
		str _categories = "Default";
		PhysicsCategoryMatch _collidesWithMode = PhysicsCategoryMatch::AllExcept;
		str _collidesWith;

		u64 _shapeBits = 0;
		u64 _ownerBodyBits = 0;
	};

	class SHALLTEAR_PHYSICS3D_API BoxCollider : public Collider {
	public:
		SLT_REFLECT_COMPONENT(BoxCollider, Collider)
		SLT_REFLECT_FIELD(_center)
		SLT_REFLECT_FIELD(_size)
		SLT_END_REFLECT()

		BoxCollider() = default;
		~BoxCollider() override;

		Vec3 GetCenter() const { return _center; }
		void SetCenter(const Vec3& center);

		// full size (not half extent)
		Vec3 GetSize() const { return _size; }
		void SetSize(const Vec3& size);

		bool DescribeShape(const Vec3& lossyScale, PhysicsShapeRequest& out) const override;
	private:
		Vec3 _center{};
		Vec3 _size{1.0f, 1.0f, 1.0f};
	};

	class SHALLTEAR_PHYSICS3D_API SphereCollider : public Collider {
	public:
		SLT_REFLECT_COMPONENT(SphereCollider, Collider)
		SLT_REFLECT_FIELD(_center)
		SLT_REFLECT_FIELD(_radius)
		SLT_END_REFLECT()

		SphereCollider() = default;
		~SphereCollider() override;

		Vec3 GetCenter() const { return _center; }
		void SetCenter(const Vec3& center);

		f32 GetRadius() const { return _radius; }
		void SetRadius(f32 radius);

		bool DescribeShape(const Vec3& lossyScale, PhysicsShapeRequest& out) const override;
	private:
		Vec3 _center{};
		f32 _radius = 0.5f;
	};

	class SHALLTEAR_PHYSICS3D_API CapsuleCollider : public Collider {
	public:
		SLT_REFLECT_COMPONENT(CapsuleCollider, Collider)
		SLT_REFLECT_FIELD(_center)
		SLT_REFLECT_FIELD(_radius)
		SLT_REFLECT_FIELD(_height)
		SLT_REFLECT_FIELD(_direction)
		SLT_END_REFLECT()

		CapsuleCollider() = default;
		~CapsuleCollider() override;

		Vec3 GetCenter() const { return _center; }
		void SetCenter(const Vec3& center);
		f32 GetRadius() const { return _radius; }
		void SetRadius(f32 radius);

		// total height
		f32 GetHeight() const { return _height; }
		void SetHeight(f32 height);

		CapsuleDirection GetDirection() const { return _direction; }
		void SetDirection(CapsuleDirection direction);

		bool DescribeShape(const Vec3& lossyScale, PhysicsShapeRequest& out) const override;
	private:
		Vec3 _center{};
		f32 _radius = 0.5f;
		f32 _height = 2.0f;
		CapsuleDirection _direction = CapsuleDirection::YAxis;
	};

	class SHALLTEAR_PHYSICS3D_API MeshCollider : public Collider {
	public:
		SLT_REFLECT_COMPONENT(MeshCollider, Collider)
		SLT_REFLECT_FIELD(_mesh)
		SLT_REFLECT_FIELD(_convex)
		SLT_END_REFLECT()

		MeshCollider() = default;
		~MeshCollider() override;

		Handle<MeshAsset> GetMesh() const { return _mesh; }
		void SetMesh(Handle<MeshAsset> mesh);

		// convex hull capped at 128 vertices
		bool GetConvex() const { return _convex; }
		void SetConvex(bool convex);

		bool DescribeShape(const Vec3& lossyScale, PhysicsShapeRequest& out) const override;
	private:
		Handle<MeshAsset> _mesh;
		bool _convex = false;
	};

} // namespace Shalltear
