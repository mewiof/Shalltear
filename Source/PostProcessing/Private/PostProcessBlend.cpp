#include "PostProcessBlend.h"

#include "Shalltear/Core/Math/MathUtil.h"

#include <algorithm>

namespace Shalltear::PostProcessOps {

	namespace {

		f32 Lerp(f32 a, f32 b, f32 t) { return a + (b - a) * t; }

		Vec2 Lerp(const Vec2& a, const Vec2& b, f32 t) { return Vec2(Lerp(a.x, b.x, t), Lerp(a.y, b.y, t)); }

		Color Lerp(const Color& a, const Color& b, f32 t) {
			return Color(Lerp(a.r, b.r, t), Lerp(a.g, b.g, t), Lerp(a.b, b.b, t), Lerp(a.a, b.a, t));
		}

		bool Lerp(bool a, bool b, f32 t) { return t >= 0.5f ? b : a; }

		void AccumulateColorGrading(ResolvedPostProcess& into, const PostProcessProfile& from, f32 t) {
			into.postExposure = Lerp(into.postExposure, from.postExposure, t);
			into.contrast = Lerp(into.contrast, from.contrast, t);
			into.colorFilter = Lerp(into.colorFilter, from.colorFilter, t);
			into.hueShift = Lerp(into.hueShift, from.hueShift, t);
			into.saturation = Lerp(into.saturation, from.saturation, t);
			into.colorGrading = true;
		}

		void AccumulateVignette(ResolvedPostProcess& into, const PostProcessProfile& from, f32 t) {
			into.vignetteColor = Lerp(into.vignetteColor, from.vignetteColor, t);
			into.vignetteCenter = Lerp(into.vignetteCenter, from.vignetteCenter, t);
			into.vignetteIntensity = Lerp(into.vignetteIntensity, from.vignetteIntensity, t);
			into.vignetteSmoothness = Lerp(into.vignetteSmoothness, from.vignetteSmoothness, t);
			into.vignetteRounded = Lerp(into.vignetteRounded, from.vignetteRounded, t);
			into.vignette = true;
		}

		void AccumulateBloom(ResolvedPostProcess& into, const PostProcessProfile& from, f32 t) {
			into.bloomThreshold = Lerp(into.bloomThreshold, from.bloomThreshold, t);
			into.bloomIntensity = Lerp(into.bloomIntensity, from.bloomIntensity, t);
			into.bloomScatter = Lerp(into.bloomScatter, from.bloomScatter, t);
			into.bloomTint = Lerp(into.bloomTint, from.bloomTint, t);
			into.bloomClamp = Lerp(into.bloomClamp, from.bloomClamp, t);
			into.bloomMaxIterations = t >= 0.5f ? from.bloomMaxIterations : into.bloomMaxIterations;
			into.bloom = true;
		}

	} // anonymous namespace

	ResolvedPostProcess Blend(std::span<VolumeSample> samples) {
		std::stable_sort(samples.begin(), samples.end(), [](const VolumeSample& a, const VolumeSample& b) {
			return a.priority < b.priority;
		});

		ResolvedPostProcess result;
		for (const VolumeSample& sample : samples) {
			if (!sample.profile || sample.contribution <= 0.0f) {
				continue;
			}
			const f32 t = Math::Clamp(sample.contribution, 0.0f, 1.0f);
			if (sample.profile->colorGrading) {
				AccumulateColorGrading(result, *sample.profile, t);
			}
			if (sample.profile->vignette) {
				AccumulateVignette(result, *sample.profile, t);
			}
			if (sample.profile->bloom) {
				AccumulateBloom(result, *sample.profile, t);
			}
		}
		return result;
	}

} // namespace Shalltear::PostProcessOps
