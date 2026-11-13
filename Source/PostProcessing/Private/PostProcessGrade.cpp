#include "PostProcessGrade.h"

#include "Shalltear/Core/Math/MathUtil.h"

namespace Shalltear::PostProcessOps {

	namespace {

		struct Matrix3D {
			f64 m[3][3];
		};

		constexpr Matrix3D Cat02 = {{{0.7328, 0.4296, -0.1624}, {-0.7036, 1.6975, 0.0061}, {0.0030, 0.0136, 0.9834}}};

		constexpr Matrix3D Rec709ToXYZ = {{{0.4124, 0.3576, 0.1805}, {0.2126, 0.7152, 0.0722}, {0.0193, 0.1192, 0.9505}}};

		constexpr f64 D65_X = 0.31271;

		constexpr f64 TEMPERATURE_SCALE = 65.0;

		Matrix3D Multiply(const Matrix3D& a, const Matrix3D& b) {
			Matrix3D out{};
			for (u32 row = 0; row < 3; ++row) {
				for (u32 column = 0; column < 3; ++column) {
					out.m[row][column] = a.m[row][0] * b.m[0][column] + a.m[row][1] * b.m[1][column] + a.m[row][2] * b.m[2][column];
				}
			}
			return out;
		}

		Matrix3D Inverse(const Matrix3D& a) {
			const f64(&m)[3][3] = a.m;
			Matrix3D out{};
			out.m[0][0] = m[1][1] * m[2][2] - m[1][2] * m[2][1];
			out.m[0][1] = m[0][2] * m[2][1] - m[0][1] * m[2][2];
			out.m[0][2] = m[0][1] * m[1][2] - m[0][2] * m[1][1];
			out.m[1][0] = m[1][2] * m[2][0] - m[1][0] * m[2][2];
			out.m[1][1] = m[0][0] * m[2][2] - m[0][2] * m[2][0];
			out.m[1][2] = m[0][2] * m[1][0] - m[0][0] * m[1][2];
			out.m[2][0] = m[1][0] * m[2][1] - m[1][1] * m[2][0];
			out.m[2][1] = m[0][1] * m[2][0] - m[0][0] * m[2][1];
			out.m[2][2] = m[0][0] * m[1][1] - m[0][1] * m[1][0];
			const f64 determinant = m[0][0] * out.m[0][0] + m[0][1] * out.m[1][0] + m[0][2] * out.m[2][0];
			for (auto& row : out.m) {
				for (f64& value : row) {
					value /= determinant;
				}
			}
			return out;
		}

		f64 DaylightY(f64 x) {
			return 2.870 * x - 3.000 * x * x - 0.275;
		}

		void WhiteToLms(f64 x, f64 y, f64 (&out)[3]) {
			const f64 xyz[3] = {x / y, 1.0, (1.0 - x - y) / y};
			for (u32 row = 0; row < 3; ++row) {
				out[row] = Cat02.m[row][0] * xyz[0] + Cat02.m[row][1] * xyz[1] + Cat02.m[row][2] * xyz[2];
			}
		}

	} // anonymous namespace

	Mat3 WhiteBalanceMatrix(f32 temperature, f32 tint) {
		const f64 warmth = Math::Clamp(temperature, -100.0f, 100.0f) / TEMPERATURE_SCALE;
		const f64 green = Math::Clamp(tint, -100.0f, 100.0f) / TEMPERATURE_SCALE;
		const f64 x = D65_X - warmth * (warmth < 0.0 ? 0.1 : 0.05);
		const f64 y = DaylightY(x) + green * 0.05;

		f64 neutral[3];
		f64 target[3];
		WhiteToLms(D65_X, DaylightY(D65_X), neutral);
		WhiteToLms(x, y, target);

		const Matrix3D toLms = Multiply(Cat02, Rec709ToXYZ);
		Matrix3D adapted = toLms;
		for (u32 row = 0; row < 3; ++row) {
			for (f64& value : adapted.m[row]) {
				value *= neutral[row] / target[row];
			}
		}
		const Matrix3D result = Multiply(Inverse(toLms), adapted);
		return Mat3::FromRows(
			Vec3(static_cast<f32>(result.m[0][0]), static_cast<f32>(result.m[0][1]), static_cast<f32>(result.m[0][2])),
			Vec3(static_cast<f32>(result.m[1][0]), static_cast<f32>(result.m[1][1]), static_cast<f32>(result.m[1][2])),
			Vec3(static_cast<f32>(result.m[2][0]), static_cast<f32>(result.m[2][1]), static_cast<f32>(result.m[2][2])));
	}

	Vec3 ToneMultiplier(const Color& color, f32 offset) {
		const f32 clamped = Math::Clamp(offset, -1.0f, 1.0f);
		const f32 lift = clamped < 0.0f ? clamped : clamped * 4.0f;
		return Vec3(Math::Max(color.r + lift, 0.0f), Math::Max(color.g + lift, 0.0f), Math::Max(color.b + lift, 0.0f));
	}

} // namespace Shalltear::PostProcessOps
