#include "Shalltear/VisualEffects/LineRenderer.h"

#include "Ribbon.h"

#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/SceneObject.h"

SLT_REGISTER_TYPE(Shalltear::LineRenderer)

namespace Shalltear {

	using namespace VisualEffectsImpl;

	LineRenderer::~LineRenderer() {
		if (_ribbon) {
			Delete(_ribbon);
			_ribbon = nullptr;
		}
	}

	void LineRenderer::OnDisable() {
		if (_ribbon) {
			Delete(_ribbon);
			_ribbon = nullptr;
		}
		ProceduralRenderer::OnDisable();
	}

	void LineRenderer::Declare(ProceduralPlan& plan) {
		const SceneObject* owner = GetOwner().Get();
		if (!owner || positions.size() < 2) {
			if (_ribbon) {
				_ribbon->points.clear();
			}
			return;
		}
		if (!_ribbon) {
			_ribbon = New<RibbonState>(MemTag::Graphics);
		}
		const Mat4& world = owner->GetWorldMatrix();
		_ribbon->transformZ = Vec3(world(0, 2), world(1, 2), world(2, 2));
		_ribbon->positions.assign(positions.begin(), positions.end());
		if (!useWorldSpace) {
			for (Vec3& position : _ribbon->positions) {
				position = world.TransformPoint(position);
			}
		}
		const RibbonStyle style{&width, widthMultiplier, &color, textureMode};
		FillRibbonPoints(_ribbon->positions, {}, style, loop, _ribbon->points);
		DeclareRibbon(plan, _ribbon->points, materials, castShadows);
	}

	void LineRenderer::Record(ProceduralContext& context) {
		if (!_ribbon) {
			return;
		}
		const RibbonShape shape{cornerVertices, endCapVertices, loop, textureScale};
		RecordRibbon(context, _ribbon->points, shape, alignment, _ribbon->transformZ, shadowBias, receiveShadows);
	}

} // namespace Shalltear
