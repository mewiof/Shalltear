#include "Graphics/EnvironmentBlend.h"

#include "Shalltear/Core/Math/MathUtil.h"

#include <algorithm>

namespace Shalltear::GraphicsInternal {

	f32 ComputeEnvironmentWeights(std::span<const EnvironmentLayerInput> layers, std::span<f32> outWeights, std::span<u32> outOrder) {
		const u32 count = static_cast<u32>(Math::Min(layers.size(), Math::Min(outWeights.size(), outOrder.size())));
		for (u32 i = 0; i < count; ++i) {
			outOrder[i] = i;
		}
		std::sort(outOrder.begin(), outOrder.begin() + count, [&](u32 a, u32 b) {
			if (layers[a].priority != layers[b].priority) {
				return layers[a].priority < layers[b].priority;
			}
			return layers[a].registration > layers[b].registration;
		});
		f32 uncovered = 1.0f;
		for (u32 i = count; i > 0; --i) {
			const u32 layer = outOrder[i - 1];
			const f32 contribution = Math::Clamp(layers[layer].contribution, 0.0f, 1.0f);
			outWeights[layer] = contribution * uncovered;
			uncovered *= 1.0f - contribution;
		}
		return uncovered;
	}

	void SelectReflectionCubes(std::span<const f32> weights, std::span<const bool> hasCube, u32 outIndex[2], f32 outWeight[2]) {
		outIndex[0] = NO_REFLECTION_LAYER;
		outIndex[1] = NO_REFLECTION_LAYER;
		outWeight[0] = 0.0f;
		outWeight[1] = 0.0f;
		const u32 count = static_cast<u32>(Math::Min(weights.size(), hasCube.size()));
		f32 total = 0.0f;
		for (u32 i = 0; i < count; ++i) {
			if (!hasCube[i] || weights[i] <= 0.0f) {
				continue;
			}
			total += weights[i];
			if (outIndex[0] == NO_REFLECTION_LAYER || weights[i] > outWeight[0]) {
				outIndex[1] = outIndex[0];
				outWeight[1] = outWeight[0];
				outIndex[0] = i;
				outWeight[0] = weights[i];
			} else if (outIndex[1] == NO_REFLECTION_LAYER || weights[i] > outWeight[1]) {
				outIndex[1] = i;
				outWeight[1] = weights[i];
			}
		}
		const f32 kept = outWeight[0] + outWeight[1];
		if (kept > 0.0f && total > kept) {
			const f32 scale = total / kept;
			outWeight[0] *= scale;
			outWeight[1] *= scale;
		}
	}

} // namespace Shalltear::GraphicsInternal
