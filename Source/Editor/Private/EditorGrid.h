#pragma once

#include "Shalltear/Core/Graphics/RenderPass.h"

namespace Shalltear {

	class EditorGridPass : public RenderPass {
	public:
		EditorGridPass();

		static void Shutdown();

		void Record(PassContext& context) override;
	};

} // namespace Shalltear
