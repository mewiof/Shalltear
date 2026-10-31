#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Graphics/Environment.h"
#include "Shalltear/Core/Types.h"

#include <span>

namespace Shalltear::GraphicsInternal {

	inline constexpr f32 DEFAULT_PHYSICAL_EV100 = 16.0f;

	inline constexpr f32 DEFAULT_CAMERA_APERTURE = 16.0f;
	inline constexpr f32 DEFAULT_CAMERA_SHUTTER_SPEED = 1.0f / 200.0f;
	inline constexpr f32 DEFAULT_CAMERA_ISO = 200.0f;

	SHALLTEAR_CORE_API f32 PhysicalCameraEV100(f32 aperture, f32 shutterSpeed, f32 iso);

	struct ExposureLayerInput {
		ExposureMode mode = ExposureMode::Fixed;
		f32 fixedEV100 = DEFAULT_PHYSICAL_EV100;
		f32 compensation = 0.0f;
	};

	SHALLTEAR_CORE_API f32 ExposureStops(const ExposureLayerInput& layer, bool physicalUnits, f32 cameraEV100);

	SHALLTEAR_CORE_API f32 BlendExposure(std::span<const ExposureLayerInput> layers, std::span<const f32> weights, f32 remainder,
										 bool physicalUnits, f32 cameraEV100);

	SHALLTEAR_CORE_API f32 ReferenceExposure(bool physicalUnits);

} // namespace Shalltear::GraphicsInternal
