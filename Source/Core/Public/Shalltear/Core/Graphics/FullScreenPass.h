#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/RenderPass.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	class SHALLTEAR_CORE_API FullScreenPass : public RenderPass {
	public:
		SLT_REFLECT_CLASS(FullScreenPass)
		SLT_REFLECT_PARENT(RenderPass)
		SLT_REFLECT_CREATABLE()
		SLT_REFLECT_FIELD(material)
		SLT_END_REFLECT()

		~FullScreenPass() override;

		void Record(PassContext& context) override;

		PassRequests GetRequests() const override;

		static bool Draws(const ShaderAsset& shader);

		Handle<MaterialAsset> material;
	private:
		bool _warnedNoMaterial = false;
	};

} // namespace Shalltear
