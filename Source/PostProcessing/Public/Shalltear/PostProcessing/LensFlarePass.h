#pragma once

#include "Shalltear/Core/Graphics/RenderPass.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/PostProcessing/PostProcessingAPI.h"

namespace Shalltear {

	class SHALLTEAR_POSTPROCESSING_API LensFlarePass : public RenderPass {
	public:
		SLT_REFLECT_CLASS(LensFlarePass)
		SLT_REFLECT_PARENT(RenderPass)
		SLT_REFLECT_CREATABLE()
		SLT_END_REFLECT()

		LensFlarePass();
		~LensFlarePass() override;

		PassRequests GetRequests() const override;

		void Record(PassContext& context) override;

		bool Cook(CookWriter& descriptor, CookWriter& payload) const override;
	private:
		Handle<ShaderAsset> _shader;
		bool _shaderResolved = false;
		bool _warnedNoShader = false;

		struct Materials;
		UniquePtr<Materials> _materials;
	};

} // namespace Shalltear
