#pragma once

#include "Shalltear/Core/Handle.h"

namespace Shalltear {

	class ProceduralRenderer;

	namespace GraphicsInternal {

		void RegisterProceduralRenderer(Handle<ProceduralRenderer> renderer);
		void UnregisterProceduralRenderer(Handle<ProceduralRenderer> renderer);

	} // namespace GraphicsInternal

} // namespace Shalltear
