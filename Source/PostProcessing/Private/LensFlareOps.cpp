#include "LensFlareOps.h"

#include "Shalltear/Core/Math/MathUtil.h"

#include <cmath>

namespace Shalltear::LensFlareOps {

	namespace {

		constexpr f32 GOLDEN_ANGLE = 2.39996322972865332f;

		f32 EvaluateOr(const AnimationCurve* curve, f32 time, f32 fallback) {
			return curve && !curve->keys.empty() ? curve->Evaluate(time) : fallback;
		}

		Vec2 RotateClockwise(Vec2 v, f32 radians) {
			const f32 c = std::cos(radians);
			const f32 s = std::sin(radians);
			return {v.x * c + v.y * s, -v.x * s + v.y * c};
		}

		u32 Mix(u32 x) {
			x ^= x >> 16;
			x *= 0x7feb352du;
			x ^= x >> 15;
			x *= 0x846ca68bu;
			x ^= x >> 16;
			return x;
		}

		f32 Signed(i32 seed, u32 index, u32 channel) {
			return HashUnit(seed, index, channel) * 2.0f - 1.0f;
		}

		f32 SpotFacing(const FlareInput& flare, const Vec3& toCamera) {
			const f32 cosOuter = std::cos(flare.spotAngleDeg * 0.5f * Math::DEG_TO_RAD);
			const f32 cosInner = std::cos(flare.innerSpotAngleDeg * 0.5f * Math::DEG_TO_RAD);
			const f32 facing = Vec3::Dot(flare.forward.Normalized(), toCamera);
			return Math::Clamp01((facing - cosOuter) / Math::Max(cosInner - cosOuter, 1e-4f));
		}

	} // anonymous namespace

	f32 HashUnit(i32 seed, u32 index, u32 channel) {
		const u32 hashed = Mix(Mix(static_cast<u32>(seed) ^ 0x9e3779b9u) + index * 0x85ebca6bu + channel * 0xc2b2ae35u);
		return static_cast<f32>(hashed >> 8) * (1.0f / 16777216.0f);
	}

	Vec2 OcclusionSample(u32 index, u32 count) {
		const f32 radius = std::sqrt((static_cast<f32>(index) + 0.5f) / static_cast<f32>(Math::Max(count, 1u)));
		const f32 angle = static_cast<f32>(index) * GOLDEN_ANGLE;
		return {radius * std::cos(angle), radius * std::sin(angle)};
	}

	f32 FalloffExponent(f32 falloff) {
		return std::exp2(4.0f * (1.0f - Math::Clamp01(falloff)));
	}

	bool Project(const ViewInput& view, const FlareInput& flare, ProjectedFlare& out) {
		out = ProjectedFlare{};
		if (!(flare.intensity > 0.0f)) {
			return false;
		}

		const Mat4& projection = view.projection;
		const bool perspective = projection(3, 2) != 0.0f;
		const bool reverseZ = projection(2, 2) < 0.0f;
		const f32 farDepth = reverseZ ? 0.0f : 1.0f;
		const Vec3 cameraForward = Vec3(view.view(2, 0), view.view(2, 1), view.view(2, 2)).Normalized();
		const bool directional = flare.hasLight && flare.lightType == LightType::Directional;

		f32 fade = 1.0f;
		f32 scale = flare.scale;
		f32 depth = farDepth;
		f32 viewDepth = 1.0f;
		f32 radius = Math::Max(flare.occlusionRadius, 0.0f);
		Vec4 clip{};

		if (directional) {
			const Vec3 toLight = -flare.forward.Normalized();
			radius = std::tan(Math::Min(radius, 89.0f) * Math::DEG_TO_RAD);
			if (perspective) {
				const Vec4 inView = view.view * Vec4(toLight, 0.0f);
				if (inView.z <= 0.0f) {
					return false;
				}
				clip = projection * inView;
				viewDepth = inView.z;
			} else {
				const f32 farDistance = (farDepth - projection(2, 3)) / projection(2, 2);
				const Vec4 inView = view.view * Vec4(view.cameraPosition + toLight * (farDistance * 0.999f), 1.0f);
				if (inView.z <= 0.0f) {
					return false;
				}
				clip = projection * inView;
				radius *= farDistance;
			}
			if (flare.attenuationByLightShape) {
				fade *= Math::Max(Vec3::Dot(toLight, cameraForward), 0.0f);
			}
		} else {
			const Vec4 inView = view.view * Vec4(flare.position, 1.0f);
			if (inView.z <= 0.0f) {
				return false;
			}
			clip = projection * inView;
			viewDepth = perspective ? inView.z : 1.0f;

			const Vec3 toCamera = view.cameraPosition - flare.position;
			const f32 distance = toCamera.Length();
			const Vec3 towardCamera = perspective && distance > 0.0f ? toCamera / distance : -cameraForward;
			const Vec4 plane = projection * (view.view * Vec4(flare.position + towardCamera * flare.occlusionOffset, 1.0f));
			depth = plane.w != 0.0f ? Math::Clamp01(plane.z / plane.w) : farDepth;

			if (flare.attenuationDistance > 0.0f) {
				fade *= EvaluateOr(flare.attenuationDistanceCurve, distance / flare.attenuationDistance, 1.0f);
			}
			if (flare.scaleDistance > 0.0f) {
				scale *= Math::Max(EvaluateOr(flare.scaleDistanceCurve, distance / flare.scaleDistance, 1.0f), 0.0f);
			}
			if (flare.attenuationByLightShape && flare.hasLight && flare.lightType == LightType::Spot && distance > 0.0f) {
				fade *= SpotFacing(flare, toCamera / distance);
			}
		}

		if (clip.w == 0.0f) {
			return false;
		}
		const Vec2 screen{clip.x / clip.w, clip.y / clip.w};
		const bool onScreen = std::abs(screen.x) <= 1.0f && std::abs(screen.y) <= 1.0f;
		if (!flare.allowOffScreen && !onScreen) {
			return false;
		}

		fade *= EvaluateOr(flare.screenAttenuationCurve, Math::Max(std::abs(screen.x), std::abs(screen.y)), 1.0f);
		fade = Math::Max(fade, 0.0f);
		if (!(fade > 0.0f) || !(scale > 0.0f)) {
			return false;
		}

		out.screen = screen;
		out.depth = depth;
		out.depthSign = reverseZ ? 1.0f : -1.0f;
		out.occlusionRadius = {radius * projection(0, 0) / viewDepth, radius * projection(1, 1) / viewDepth};
		out.samples = flare.occlusion ? static_cast<u32>(Math::Clamp(flare.sampleCount, 1, static_cast<i32>(MAX_SAMPLES))) : 0u;
		out.offScreenVisible = flare.allowOffScreen;
		out.fade = fade;
		out.intensity = flare.intensity;
		out.scale = scale;
		out.hasLight = flare.hasLight;
		out.lightColor = flare.lightColor;
		return true;
	}

	void ExpandElement(const LensFlareElement& element, const ProjectedFlare& flare, f32 aspect, f32 textureAspect,
					   Vector<Quad>& out) {
		if (!element.enabled || !(aspect > 0.0f)) {
			return;
		}

		Vec2 baseHalf = element.size * (SIZE_UNIT * element.scale * flare.scale);
		if (element.shape == LensFlareShape::Image && element.preserveAspectRatio && textureAspect > 0.0f) {
			baseHalf.x = baseHalf.y * textureAspect;
		}

		Color base = element.tint;
		f32 gain = Math::Max(element.intensity, 0.0f) * flare.intensity;
		base.r *= gain;
		base.g *= gain;
		base.b *= gain;
		if (element.modulateByLightColor && flare.hasLight) {
			base.r *= flare.lightColor.r;
			base.g *= flare.lightColor.g;
			base.b *= flare.lightColor.b;
		}
		base.a *= flare.fade;

		const u32 copies = element.multiple ? static_cast<u32>(Math::Clamp(element.count, 1, static_cast<i32>(MAX_COPIES))) : 1u;
		const f32 spread = 2.0f * element.lengthSpread;
		const f32 autoAngle = element.autoRotate && (flare.screen.x != 0.0f || flare.screen.y != 0.0f)
								  ? std::atan2(flare.screen.y, flare.screen.x * aspect)
								  : 0.0f;

		for (u32 copy = 0; copy < copies; ++copy) {
			const f32 t = copies >= 2 ? static_cast<f32>(copy) / static_cast<f32>(copies - 1) : 0.5f;
			f32 position = element.position;
			Vec2 half = baseHalf;
			f32 rotationDeg = element.rotation;
			Vec2 offset = element.positionOffset;
			Color color = base;

			if (element.multiple) {
				switch (element.distribution) {
				case LensFlareDistribution::Uniform:
					position += copies >= 2 ? spread * t : 0.0f;
					rotationDeg += element.rotationStep * static_cast<f32>(copy);
					color = color * element.colors.Evaluate(t);
					break;
				case LensFlareDistribution::Curve:
					position += spread * EvaluateOr(&element.positionCurve, t, t);
					half = half * Math::Max(EvaluateOr(&element.scaleCurve, t, 1.0f), 0.0f);
					rotationDeg += EvaluateOr(&element.rotationCurve, t, 0.0f) * 180.0f;
					color = color * element.colors.Evaluate(t);
					break;
				case LensFlareDistribution::Random: {
					const f32 boost = 1.0f + Signed(element.seed, copy, 0) * element.intensityVariation;
					if (!(boost > 0.0f)) {
						continue;
					}
					color.r *= boost;
					color.g *= boost;
					color.b *= boost;
					half = half * Math::Max(1.0f + Signed(element.seed, copy, 1) * element.scaleVariation, 0.0f);
					color = color * element.colors.Evaluate(HashUnit(element.seed, copy, 2));
					const f32 slot = copies >= 2 ? element.lengthSpread / static_cast<f32>(copies - 1) : element.lengthSpread;
					position += (copies >= 2 ? spread * t : 0.0f) + Signed(element.seed, copy, 3) * slot * element.positionVariation.x;
					offset.y += Signed(element.seed, copy, 4) * element.positionVariation.y;
					rotationDeg += Signed(element.seed, copy, 5) * element.rotationVariation;
					break;
				}
				}
			}

			Vec2 ray{-flare.screen.x * position * element.translationScale.x, -flare.screen.y * position * element.translationScale.y};
			if (element.angularOffset != 0.0f) {
				const Vec2 square = RotateClockwise({ray.x * aspect, ray.y}, element.angularOffset * Math::DEG_TO_RAD);
				ray = {square.x / aspect, square.y};
			}

			Quad quad;
			quad.center = flare.screen + ray + offset;
			quad.halfSize = half;
			quad.rotation = rotationDeg * Math::DEG_TO_RAD - autoAngle;
			quad.color = color;
			if (!(half.x > 0.0f) || !(half.y > 0.0f) || !(color.a > 0.0f)) {
				continue;
			}
			out.push_back(quad);
		}
	}

} // namespace Shalltear::LensFlareOps
