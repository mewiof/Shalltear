#pragma once

#include "Shalltear/Core/Types.h"

namespace Shalltear {

	class FramePass;

	namespace GraphicsInternal {

		class RenderGraph;
		struct GraphExecution;
		struct GraphPass;

		struct FramePassPlanInit {
			RenderGraph* graph = nullptr;
			u32 passIndex = 0;
			u32 zoneSite = 0;
		};

		struct ComputeContextInit {
			GraphExecution* exec = nullptr;
			const GraphPass* pass = nullptr;
			u32 step = 0;
		};

		u32 GetFramePassCount();
		FramePass* GetFramePass(u32 index);

	} // namespace GraphicsInternal

} // namespace Shalltear
