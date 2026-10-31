#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/MathReflect.h"
#include "Shalltear/Core/Scene/Component.h"

namespace Shalltear {

	class SHALLTEAR_CORE_API ReflectionProbe : public Component {
	public:
		SLT_REFLECT_COMPONENT(ReflectionProbe, Component)
		SLT_REFLECT_FIELD(_bakedCubemap)
		SLT_REFLECT_FIELD(_boxSize)
		SLT_REFLECT_FIELD(_boxOffset)
		SLT_REFLECT_FIELD(_boxProjection)
		SLT_REFLECT_FIELD(_importance, SLT_RANGE(0.0f, 128.0f))
		SLT_REFLECT_FIELD(_nearPlane)
		SLT_REFLECT_FIELD(_farPlane)
		SLT_END_REFLECT()

		ReflectionProbe() = default;
		~ReflectionProbe() override;

		Handle<TextureAsset> GetBakedCubemap() const { return _bakedCubemap; }
		void SetBakedCubemap(Handle<TextureAsset> cubemap) { _bakedCubemap = cubemap; }

		Vec3 GetBoxSize() const { return _boxSize; }
		void SetBoxSize(const Vec3& size) { _boxSize = size; }

		Vec3 GetBoxOffset() const { return _boxOffset; }
		void SetBoxOffset(const Vec3& offset) { _boxOffset = offset; }

		bool GetBoxProjection() const { return _boxProjection; }
		void SetBoxProjection(bool enabled) { _boxProjection = enabled; }

		f32 GetImportance() const { return _importance; }
		void SetImportance(f32 importance) { _importance = importance; }

		f32 GetNearPlane() const { return _nearPlane; }
		void SetNearPlane(f32 distance) { _nearPlane = distance; }
		f32 GetFarPlane() const { return _farPlane; }
		void SetFarPlane(f32 distance) { _farPlane = distance; }
	protected:
		void OnEnable() override;
		void OnDisable() override;
	private:
		Handle<TextureAsset> _bakedCubemap;
		Vec3 _boxSize{10.0f, 10.0f, 10.0f};
		Vec3 _boxOffset{0.0f, 0.0f, 0.0f};
		bool _boxProjection = true;
		f32 _importance = 1.0f;
		f32 _nearPlane = 0.1f;
		f32 _farPlane = 1000.0f;
	};

} // namespace Shalltear
