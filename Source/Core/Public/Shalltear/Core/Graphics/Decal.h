#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/MathReflect.h"
#include "Shalltear/Core/Scene/Component.h"

namespace Shalltear {

	class SHALLTEAR_CORE_API Decal : public Component {
	public:
		SLT_REFLECT_COMPONENT(Decal, Component)
		SLT_REFLECT_FIELD(_material)
		SLT_REFLECT_FIELD(_size)
		SLT_REFLECT_FIELD(_pivot)
		SLT_REFLECT_FIELD(_tiling)
		SLT_REFLECT_FIELD(_offset)
		SLT_REFLECT_FIELD(_opacity, SLT_RANGE(0.0f, 1.0f))
		SLT_REFLECT_FIELD(_drawDistance)
		SLT_REFLECT_FIELD(_startFade, SLT_RANGE(0.0f, 1.0f))
		SLT_REFLECT_FIELD(_angleFade)
		SLT_REFLECT_FIELD(_priority)
		SLT_REFLECT_FIELD(_renderingLayerMask)
		SLT_END_REFLECT()

		Decal() = default;
		~Decal() override;

		// must use engine Decal shader
		Handle<MaterialAsset> GetMaterial() const { return _material; }
		void SetMaterial(Handle<MaterialAsset> material) { _material = material; }

		// width, height, depth (projected along local +z)
		Vec3 GetSize() const { return _size; }
		void SetSize(const Vec3& size) { _size = size; }

		// box center in local units
		Vec3 GetPivot() const { return _pivot; }
		void SetPivot(const Vec3& pivot) { _pivot = pivot; }

		Vec2 GetTiling() const { return _tiling; }
		void SetTiling(const Vec2& tiling) { _tiling = tiling; }
		Vec2 GetOffset() const { return _offset; }
		void SetOffset(const Vec2& offset) { _offset = offset; }

		f32 GetOpacity() const { return _opacity; }
		void SetOpacity(f32 opacity) { _opacity = opacity; }

		f32 GetDrawDistance() const { return _drawDistance; }
		void SetDrawDistance(f32 distance) { _drawDistance = distance; }

		// fade start as fraction of draw distance
		f32 GetStartFade() const { return _startFade; }
		void SetStartFade(f32 fraction) { _startFade = fraction; }

		// deg between projector and surface normal, start/end
		Vec2 GetAngleFade() const { return _angleFade; }
		void SetAngleFade(const Vec2& degrees) { _angleFade = degrees; }

		// higher drawn over lower
		i32 GetPriority() const { return _priority; }
		void SetPriority(i32 priority) { _priority = priority; }

		// first 8 layers only (all bits set = all layers)
		u32 GetRenderingLayerMask() const { return _renderingLayerMask; }
		void SetRenderingLayerMask(u32 mask) { _renderingLayerMask = mask; }
	protected:
		void OnEnable() override;
		void OnDisable() override;
	private:
		Handle<MaterialAsset> _material;
		Vec3 _size{1.0f, 1.0f, 1.0f};
		Vec3 _pivot{0.0f, 0.0f, 0.5f};
		Vec2 _tiling{1.0f, 1.0f};
		Vec2 _offset{0.0f, 0.0f};
		f32 _opacity = 1.0f;
		f32 _drawDistance = 1000.0f;
		f32 _startFade = 0.9f;
		Vec2 _angleFade{180.0f, 180.0f};
		i32 _priority = 0;
		u32 _renderingLayerMask = ~0u;
	};

} // namespace Shalltear
