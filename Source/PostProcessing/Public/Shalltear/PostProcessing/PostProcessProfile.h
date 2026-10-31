#pragma once

#include "Shalltear/Core/Asset/DataAsset.h"
#include "Shalltear/Core/Math/MathReflect.h"
#include "Shalltear/Core/Reflection/Reflect.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/PostProcessing/PostProcessingAPI.h"

namespace Shalltear {

	class SHALLTEAR_POSTPROCESSING_API PostProcessProfile : public DataAsset {
	public:
		SLT_REFLECT_CLASS(PostProcessProfile)
		SLT_REFLECT_PARENT(DataAsset)
		SLT_CREATE_ASSET_MENU("Post Process Profile", ".slpp")

		SLT_REFLECT_FIELD(colorGrading, SLT_HEADER("Color Grading"))
		SLT_REFLECT_FIELD(postExposure)
		SLT_REFLECT_FIELD(contrast, SLT_RANGE(-100.0f, 100.0f))
		SLT_REFLECT_FIELD(colorFilter)
		SLT_REFLECT_FIELD(hueShift, SLT_RANGE(-180.0f, 180.0f))
		SLT_REFLECT_FIELD(saturation, SLT_RANGE(-100.0f, 100.0f))

		SLT_REFLECT_FIELD(vignette, SLT_HEADER("Vignette"))
		SLT_REFLECT_FIELD(vignetteColor)
		SLT_REFLECT_FIELD(vignetteCenter)
		SLT_REFLECT_FIELD(vignetteIntensity, SLT_RANGE(0.0f, 1.0f))
		SLT_REFLECT_FIELD(vignetteSmoothness, SLT_RANGE(0.01f, 1.0f))
		SLT_REFLECT_FIELD(vignetteRounded)
		SLT_REFLECT_FIELD(bloom, SLT_HEADER("Bloom"))
		SLT_REFLECT_FIELD(bloomThreshold, SLT_RANGE(0.0f, 10.0f))
		SLT_REFLECT_FIELD(bloomIntensity, SLT_RANGE(0.0f, 1.0f))
		SLT_REFLECT_FIELD(bloomScatter, SLT_RANGE(0.0f, 1.0f))
		SLT_REFLECT_FIELD(bloomTint)
		SLT_REFLECT_FIELD(bloomClamp)
		SLT_REFLECT_FIELD(bloomMaxIterations, SLT_RANGE(1.0f, 10.0f))
		SLT_END_REFLECT()

		PostProcessProfile() = default;
		~PostProcessProfile() override;

		bool colorGrading = false;

		// EV stops, after bloom, before tonemapping
		f32 postExposure = 0.0f;

		f32 contrast = 0.0f;

		Color colorFilter = Color::White();

		// degrees
		f32 hueShift = 0.0f;

		f32 saturation = 0.0f;

		bool vignette = false;

		Color vignetteColor = Color::Black();

		Vec2 vignetteCenter{0.5f, 0.5f};

		f32 vignetteIntensity = 0.0f;

		f32 vignetteSmoothness = 0.2f;

		bool vignetteRounded = false;

		bool bloom = false;

		// gamma space brightness
		f32 bloomThreshold = 0.9f;

		f32 bloomIntensity = 0.0f;

		f32 bloomScatter = 0.7f;

		Color bloomTint = Color::White();

		f32 bloomClamp = 65472.0f;

		i32 bloomMaxIterations = 6;
	};

} // namespace Shalltear
