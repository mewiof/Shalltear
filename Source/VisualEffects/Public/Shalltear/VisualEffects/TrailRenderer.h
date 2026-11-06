#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/ProceduralRenderer.h"
#include "Shalltear/Core/Graphics/ShadowCasting.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/AnimationCurve.h"
#include "Shalltear/Core/Math/Gradient.h"
#include "Shalltear/Core/Math/MathReflect.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/VisualEffects/LineRenderer.h"
#include "Shalltear/VisualEffects/VisualEffectsAPI.h"

namespace Shalltear {

	namespace VisualEffectsImpl {
		struct TrailState;
		struct TrailAccess;
	} // namespace VisualEffectsImpl

	class SHALLTEAR_VISUALEFFECTS_API TrailRenderer : public ProceduralRenderer {
	public:
		SLT_REFLECT_COMPONENT(TrailRenderer, ProceduralRenderer)
		SLT_REFLECT_SUBOBJECT(width)
		SLT_REFLECT_FIELD(widthMultiplier)
		SLT_REFLECT_FIELD(time)
		SLT_REFLECT_FIELD(minVertexDistance)
		SLT_REFLECT_FIELD(autodestruct)
		SLT_REFLECT_FIELD(emitting)
		SLT_REFLECT_SUBOBJECT(color)
		SLT_REFLECT_FIELD(cornerVertices)
		SLT_REFLECT_FIELD(endCapVertices)
		SLT_REFLECT_FIELD(alignment)
		SLT_REFLECT_FIELD(textureMode)
		SLT_REFLECT_FIELD(textureScale)
		SLT_REFLECT_FIELD(shadowBias)
		SLT_REFLECT_ARRAY(materials)
		SLT_REFLECT_FIELD(castShadows)
		SLT_REFLECT_FIELD(receiveShadows)
		SLT_END_REFLECT()

		AnimationCurve width{{Keyframe{.time = 0.0f, .value = 1.0f}}, GetMemoryResource(MemTag::Scene)};
		f32 widthMultiplier = 0.5f;
		f32 time = 5.0f;
		f32 minVertexDistance = 0.1f;
		bool autodestruct = false;
		bool emitting = true;
		Gradient color{GetMemoryResource(MemTag::Scene)};
		u32 cornerVertices = 0;
		u32 endCapVertices = 0;
		LineAlignment alignment = LineAlignment::View;
		LineTextureMode textureMode = LineTextureMode::Stretch;
		Vec2 textureScale{1.0f, 1.0f};
		// caster offset from light by this fraction of width
		f32 shadowBias = 0.5f;
		Vector<Handle<MaterialAsset>> materials{GetMemoryResource(MemTag::Scene)};
		ShadowCasting castShadows = ShadowCasting::On;
		bool receiveShadows = true;

		~TrailRenderer() override;

		void AddPosition(const Vec3& position);
		void Clear();
		// oldest first, head excluded
		[[nodiscard]] u32 GetPositionCount() const;
		[[nodiscard]] Vec3 GetPosition(u32 index) const;

		void Declare(ProceduralPlan& plan) override;
		void Record(ProceduralContext& context) override;
	protected:
		void OnEnable() override;
		void OnDisable() override;
	private:
		friend struct VisualEffectsImpl::TrailAccess;
		VisualEffectsImpl::TrailState* _trail = nullptr;
	};

} // namespace Shalltear
