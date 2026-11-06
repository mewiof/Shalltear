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

#include "Shalltear/VisualEffects/VisualEffectsAPI.h"

namespace Shalltear {

	enum class LineAlignment : u8 {
		View = 0,
		TransformZ = 1,
	};

	enum class LineTextureMode : u8 {
		Stretch = 0,
		Tile = 1,
		DistributePerSegment = 2,
		RepeatPerSegment = 3,
	};

} // namespace Shalltear

SLT_REFLECT_ENUM(::Shalltear::LineAlignment)
SLT_ENUM_VALUE(View)
SLT_ENUM_VALUE(TransformZ)
SLT_END_ENUM()

SLT_REFLECT_ENUM(::Shalltear::LineTextureMode)
SLT_ENUM_VALUE(Stretch)
SLT_ENUM_VALUE(Tile)
SLT_ENUM_VALUE(DistributePerSegment)
SLT_ENUM_VALUE(RepeatPerSegment)
SLT_END_ENUM()

namespace Shalltear {

	namespace VisualEffectsImpl {
		struct RibbonState;
	} // namespace VisualEffectsImpl

	class SHALLTEAR_VISUALEFFECTS_API LineRenderer : public ProceduralRenderer {
	public:
		SLT_REFLECT_COMPONENT(LineRenderer, ProceduralRenderer)
		SLT_REFLECT_FIELD(loop)
		SLT_REFLECT_ARRAY(positions)
		SLT_REFLECT_SUBOBJECT(width)
		SLT_REFLECT_FIELD(widthMultiplier)
		SLT_REFLECT_SUBOBJECT(color)
		SLT_REFLECT_FIELD(cornerVertices)
		SLT_REFLECT_FIELD(endCapVertices)
		SLT_REFLECT_FIELD(alignment)
		SLT_REFLECT_FIELD(textureMode)
		SLT_REFLECT_FIELD(textureScale)
		SLT_REFLECT_FIELD(shadowBias)
		SLT_REFLECT_FIELD(useWorldSpace)
		SLT_REFLECT_ARRAY(materials)
		SLT_REFLECT_FIELD(castShadows)
		SLT_REFLECT_FIELD(receiveShadows)
		SLT_END_REFLECT()

		bool loop = false;
		Vector<Vec3> positions{{Vec3(0.0f, 0.0f, 0.0f), Vec3(0.0f, 0.0f, 1.0f)}, GetMemoryResource(MemTag::Scene)};
		AnimationCurve width{{Keyframe{.time = 0.0f, .value = 1.0f}}, GetMemoryResource(MemTag::Scene)};
		f32 widthMultiplier = 0.1f;
		Gradient color{GetMemoryResource(MemTag::Scene)};
		u32 cornerVertices = 0;
		u32 endCapVertices = 0;
		LineAlignment alignment = LineAlignment::View;
		LineTextureMode textureMode = LineTextureMode::Stretch;
		Vec2 textureScale{1.0f, 1.0f};
		// caster offset from light by this fraction of width
		f32 shadowBias = 0.5f;
		bool useWorldSpace = true;
		Vector<Handle<MaterialAsset>> materials{GetMemoryResource(MemTag::Scene)};
		ShadowCasting castShadows = ShadowCasting::On;
		bool receiveShadows = true;

		~LineRenderer() override;

		void Declare(ProceduralPlan& plan) override;
		void Record(ProceduralContext& context) override;
	protected:
		void OnDisable() override;
	private:
		VisualEffectsImpl::RibbonState* _ribbon = nullptr;
	};

} // namespace Shalltear
