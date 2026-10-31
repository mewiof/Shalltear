#pragma once

#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/PostProcessing/PostProcessProfile.h"

#include <span>

namespace Shalltear::PostProcessOps {

	struct ResolvedPostProcess {
		bool colorGrading = false;
		f32 postExposure = 0.0f;
		f32 contrast = 0.0f;
		Color colorFilter = Color::White();
		f32 hueShift = 0.0f;
		f32 saturation = 0.0f;

		bool vignette = false;
		Color vignetteColor = Color::Black();
		Vec2 vignetteCenter{0.5f, 0.5f};
		f32 vignetteIntensity = 0.0f;
		f32 vignetteSmoothness = 0.2f;
		bool vignetteRounded = false;

		bool bloom = false;
		f32 bloomThreshold = 0.9f;
		f32 bloomIntensity = 0.0f;
		f32 bloomScatter = 0.7f;
		Color bloomTint = Color::White();
		f32 bloomClamp = 65472.0f;
		i32 bloomMaxIterations = 6;

		bool AnyEnabled() const { return colorGrading || vignette || bloom; }

		bool BloomContributes() const { return bloom && bloomIntensity > 0.0f; }
	};

	struct VolumeSample {
		f32 priority = 0.0f;
		f32 contribution = 0.0f;
		const PostProcessProfile* profile = nullptr;
	};

	SHALLTEAR_POSTPROCESSING_API ResolvedPostProcess Blend(std::span<VolumeSample> samples);

} // namespace Shalltear::PostProcessOps
