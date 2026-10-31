#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/RenderPass.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	class SHALLTEAR_CORE_API ObjectPass : public RenderPass {
	public:
		SLT_REFLECT_CLASS(ObjectPass)
		SLT_REFLECT_PARENT(RenderPass)
		SLT_REFLECT_CREATABLE()
		SLT_REFLECT_FIELD(renderingLayerMask)
		SLT_REFLECT_FIELD(overrideMaterial)
		SLT_REFLECT_FIELD(overrideCamera)
		SLT_REFLECT_FIELD(fieldOfViewDeg, SLT_RANGE(1.0f, 179.0f))
		SLT_REFLECT_FIELD(depthScale, SLT_RANGE(0.001f, 1.0f))
		SLT_END_REFLECT()

		~ObjectPass() override;

		void Record(PassContext& context) override;

		u32 renderingLayerMask = ~0u;

		Handle<MaterialAsset> overrideMaterial;

		bool overrideCamera = false;
		f32 fieldOfViewDeg = 60.0f;
		f32 depthScale = 1.0f;
	};

} // namespace Shalltear
