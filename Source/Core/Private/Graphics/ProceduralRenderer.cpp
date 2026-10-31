#include "Shalltear/Core/Graphics/ProceduralRenderer.h"

#include "Graphics/ProceduralRendererInternal.h"

namespace Shalltear {

	ProceduralRenderer::~ProceduralRenderer() = default;

	void ProceduralRenderer::RecordView(ComputeContext&, const ProceduralView&) {}

	void ProceduralRenderer::OnEnable() {
		_tested = false;
		_drawn = false;
		GraphicsInternal::RegisterProceduralRenderer(Handle<ProceduralRenderer>(this));
	}

	void ProceduralRenderer::OnDisable() {
		GraphicsInternal::UnregisterProceduralRenderer(Handle<ProceduralRenderer>(this));
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::ProceduralRenderer)
