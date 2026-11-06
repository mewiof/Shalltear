#include "Shalltear/Core/Math/Gradient.h"

#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Math/Vector.h"

#include <cmath>

SLT_REGISTER_TYPE(Shalltear::GradientColorKey)
SLT_REGISTER_TYPE(Shalltear::GradientAlphaKey)
SLT_REGISTER_TYPE(Shalltear::Gradient)

namespace Shalltear {

	namespace {

		constexpr f32 TO_LMS[] = {0.4122214708f, 0.5363325363f, 0.0514459929f, 0.2119034982f, 0.6806995451f,
								  0.1073969566f, 0.0883024619f, 0.2817188376f, 0.6299787005f};
		constexpr f32 TO_LAB[] = {0.2104542553f, 0.7936177850f, -0.0040720468f, 1.9779984951f, -2.4285922050f,
								  0.4505937099f, 0.0259040371f, 0.7827717662f, -0.8086757660f};
		constexpr f32 FROM_LAB[] = {1.0f, 0.3963377774f, 0.2158037573f, 1.0f, -0.1055613458f, -0.0638541728f, 1.0f, -0.0894841775f, -1.2914855480f};
		constexpr f32 FROM_LMS[] = {4.0767416621f, -3.3077115913f, 0.2309699292f, -1.2684380046f, 2.6097574011f,
									-0.3413193965f, -0.0041960863f, -0.7034186147f, 1.7076147010f};

		Vec3 Transform(const f32 (&matrix)[9], const Vec3& in) {
			return Vec3(matrix[0] * in.x + matrix[1] * in.y + matrix[2] * in.z, matrix[3] * in.x + matrix[4] * in.y + matrix[5] * in.z,
						matrix[6] * in.x + matrix[7] * in.y + matrix[8] * in.z);
		}

		Vec3 ToOklab(const Color& color) {
			const Vec3 cone = Transform(TO_LMS, Vec3(color.r, color.g, color.b));
			return Transform(TO_LAB, Vec3(std::cbrt(cone.x), std::cbrt(cone.y), std::cbrt(cone.z)));
		}

		Vec3 FromOklab(const Vec3& lab) {
			const Vec3 root = Transform(FROM_LAB, lab);
			return Transform(FROM_LMS, Vec3(root.x * root.x * root.x, root.y * root.y * root.y, root.z * root.z * root.z));
		}

		f32 Position(f32 time, f32 from, f32 to, bool fixed) {
			const f32 span = to - from;
			const f32 at = span > 0.0f ? Math::Clamp((time - from) / span, 0.0f, 1.0f) : (time >= to ? 1.0f : 0.0f);
			return fixed ? (at >= 0.01f ? 1.0f : 0.0f) : at;
		}

	} // anonymous namespace

	Color Gradient::Evaluate(f32 time) const {
		Color out = Color::White();
		const bool fixed = mode == GradientMode::Fixed;
		const bool perceptual = mode == GradientMode::PerceptualBlend;
		if (!colorKeys.empty()) {
			const auto keyColor = [&](size_t key) {
				const Color& stored = colorKeys[key].color;
				return perceptual ? ToOklab(stored) : Vec3(stored.r, stored.g, stored.b);
			};
			Vec3 color = keyColor(0);
			for (size_t key = 1; key < colorKeys.size(); ++key) {
				const f32 t = Position(time, colorKeys[key - 1].time, colorKeys[key].time, fixed);
				color = color + (keyColor(key) - color) * t;
			}
			if (perceptual) {
				color = FromOklab(color);
			}
			out.r = color.x;
			out.g = color.y;
			out.b = color.z;
		}
		if (!alphaKeys.empty()) {
			f32 alpha = alphaKeys[0].alpha;
			for (size_t key = 1; key < alphaKeys.size(); ++key) {
				const f32 t = Position(time, alphaKeys[key - 1].time, alphaKeys[key].time, fixed);
				alpha += (alphaKeys[key].alpha - alpha) * t;
			}
			out.a = alpha;
		}
		return out;
	}

} // namespace Shalltear
