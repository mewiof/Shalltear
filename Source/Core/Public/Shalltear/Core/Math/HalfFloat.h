#pragma once

#include "Shalltear/Core/Types.h"

#include <bit>

namespace Shalltear::Math {

	inline u16 FloatToHalf(f32 value) {
		u32 bits = std::bit_cast<u32>(value);
		u32 sign = (bits >> 16) & 0x8000;
		i32 exponent = static_cast<i32>((bits >> 23) & 0xFF) - 127 + 15;
		u32 mantissa = bits & 0x7FFFFF;

		if (exponent <= 0) {
			return static_cast<u16>(sign);
		}
		if (exponent >= 31) {
			return static_cast<u16>(sign | 0x7C00);
		}
		u16 half = static_cast<u16>(sign | (exponent << 10) | (mantissa >> 13));
		if (mantissa & 0x1000) {
			++half;
		}
		return half;
	}

	inline f32 HalfToFloat(u16 half) {
		u32 sign = static_cast<u32>(half & 0x8000) << 16;
		u32 exponent = (half >> 10) & 0x1F;
		u32 mantissa = half & 0x3FF;

		if (exponent == 0) {
			if (mantissa == 0) {
				return std::bit_cast<f32>(sign);
			}
			exponent = 1;
			while ((mantissa & 0x400) == 0) {
				mantissa <<= 1;
				--exponent;
			}
			mantissa &= 0x3FF;
		} else if (exponent == 31) {
			return std::bit_cast<f32>(sign | 0x7F800000u | (mantissa << 13));
		}
		return std::bit_cast<f32>(sign | ((exponent + 127 - 15) << 23) | (mantissa << 13));
	}

} // namespace Shalltear::Math
