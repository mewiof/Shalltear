#pragma once

#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/PostProcessing/PostProcessingAPI.h"

namespace Shalltear::PostProcessOps {

	// linear Rec.709 in/out
	SHALLTEAR_POSTPROCESSING_API Mat3 WhiteBalanceMatrix(f32 temperature, f32 tint);

	SHALLTEAR_POSTPROCESSING_API Vec3 ToneMultiplier(const Color& color, f32 offset);

} // namespace Shalltear::PostProcessOps
