#pragma once

// the effect noises are Brian Sharpe's GPU-Noise-Lib (github.com/BrianSharpe/GPU-Noise-Lib)
// must compute the same values as GraphHLSL.cpp

#include "Shalltear/Core/Types.h"

namespace Shalltear::VisualEffectsImpl::EffectNoises {

	void Fractal(u32 type, u32 dimension, const f32* coordinate, const f32* parameters, f32 octaves, f32* out);

	void Curl(u32 type, u32 dimension, const f32* coordinate, const f32* parameters, f32 octaves, f32* out);

	inline constexpr f32 MAX_OCTAVES = 64.0f;

	f32 ShaderValue(u32 hash, f32 x, f32 y);
	f32 ShaderGradient(u32 hash, f32 x, f32 y);
	void ShaderVoronoi(u32 hash, f32 x, f32 y, f32 angleOffset, f32 cellDensity, f32* out);

} // namespace Shalltear::VisualEffectsImpl::EffectNoises
