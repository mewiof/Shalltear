#include "Graphics/Exposure.h"

#include "Shalltear/Core/Math/MathUtil.h"

#include <cmath>

namespace Shalltear::GraphicsInternal {

	namespace {

		constexpr f32 LENS_STOPS = 0.263034405833794f;

	} // namespace

	f32 PhysicalCameraEV100(f32 aperture, f32 shutterSpeed, f32 iso) {
		const f32 fNumber = Math::Clamp(aperture, 0.7f, 32.0f);
		const f32 seconds = Math::Max(shutterSpeed, 1e-6f);
		const f32 sensitivity = Math::Max(iso, 1.0f);
		return std::log2(fNumber * fNumber / seconds * 100.0f / sensitivity);
	}

	f32 ExposureStops(const ExposureLayerInput& layer, bool physicalUnits, f32 cameraEV100) {
		if (!physicalUnits) {
			return layer.compensation;
		}
		const f32 ev100 = layer.mode == ExposureMode::PhysicalCamera ? cameraEV100 : layer.fixedEV100;
		return layer.compensation - LENS_STOPS - ev100;
	}

	f32 BlendExposure(std::span<const ExposureLayerInput> layers, std::span<const f32> weights, f32 remainder, bool physicalUnits,
					  f32 cameraEV100) {
		f32 stops = remainder * ExposureStops(ExposureLayerInput{}, physicalUnits, cameraEV100);
		const size_t count = Math::Min(layers.size(), weights.size());
		for (size_t i = 0; i < count; ++i) {
			if (weights[i] > 0.0f) {
				stops += weights[i] * ExposureStops(layers[i], physicalUnits, cameraEV100);
			}
		}
		return std::exp2(stops);
	}

	f32 ReferenceExposure(bool physicalUnits) {
		return physicalUnits ? 1.0f / (1.2f * std::exp2(DEFAULT_PHYSICAL_EV100)) : 1.0f;
	}

} // namespace Shalltear::GraphicsInternal
