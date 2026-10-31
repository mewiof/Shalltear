#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear::GraphicsInternal {

	inline constexpr u32 MAX_ENVIRONMENT_LAYERS = 8;

	struct EnvironmentLayerInput {
		f32 priority = 0.0f;
		f32 contribution = 0.0f;
		u32 registration = 0;
	};

	SHALLTEAR_CORE_API f32 ComputeEnvironmentWeights(std::span<const EnvironmentLayerInput> layers, std::span<f32> outWeights,
													 std::span<u32> outOrder);

	inline constexpr u32 NO_REFLECTION_LAYER = ~0u;

	SHALLTEAR_CORE_API void SelectReflectionCubes(std::span<const f32> weights, std::span<const bool> hasCube, u32 outIndex[2],
												  f32 outWeight[2]);

} // namespace Shalltear::GraphicsInternal
