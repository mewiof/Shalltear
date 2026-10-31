#pragma once

#include "Shalltear/Core/Graphics/RenderPass.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/PostProcessing/PostProcessingAPI.h"

namespace Shalltear {

	class SHALLTEAR_POSTPROCESSING_API AmbientOcclusionPass : public RenderPass {
	public:
		SLT_REFLECT_CLASS(AmbientOcclusionPass)
		SLT_REFLECT_PARENT(RenderPass)
		SLT_REFLECT_CREATABLE()
		SLT_REFLECT_FIELD(intensity)
		SLT_REFLECT_FIELD(radius)
		SLT_REFLECT_FIELD(falloffDistance)
		SLT_REFLECT_FIELD(directions)
		SLT_REFLECT_FIELD(downsample)
		SLT_REFLECT_FIELD(afterOpaque)
		SLT_END_REFLECT()

		AmbientOcclusionPass();
		~AmbientOcclusionPass() override;

		PassRequests GetRequests() const override;

		void DeclarePlan(PassPlan& plan) override;

		void Record(PassContext& context) override;

		bool Cook(CookWriter& descriptor, CookWriter& payload) const override;

		f32 intensity = 1.0f;

		f32 radius = 0.3f;

		// along the view
		f32 falloffDistance = 100.0f;

		i32 directions = 4;

		bool downsample = true;

		bool afterOpaque = false;
	private:
		Handle<ShaderAsset> _shader;
		bool _shaderResolved = false;

		bool _warnedNoShader = false;
		bool _warnedOrthographic = false;
		bool _warnedPointDisagrees = false;
	};

} // namespace Shalltear
