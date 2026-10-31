#pragma once

#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Types.h"

#include <cmath>
#include <format>
#include <string_view>

namespace Shalltear {

	struct Color32;

	struct Color {
		f32 r, g, b, a;

		constexpr Color(f32 r, f32 g, f32 b, f32 a = 1.0f) noexcept : r(r), g(g), b(b), a(a) {}
		constexpr Color() noexcept : r(0.0f), g(0.0f), b(0.0f), a(0.0f) {}

		[[nodiscard]] static constexpr Color White() noexcept { return Color(1.0f, 1.0f, 1.0f); }
		[[nodiscard]] static constexpr Color Black() noexcept { return Color(0.0f, 0.0f, 0.0f); }
		[[nodiscard]] static constexpr Color Red() noexcept { return Color(1.0f, 0.0f, 0.0f); }
		[[nodiscard]] static constexpr Color Green() noexcept { return Color(0.0f, 1.0f, 0.0f); }
		[[nodiscard]] static constexpr Color Blue() noexcept { return Color(0.0f, 0.0f, 1.0f); }
		[[nodiscard]] static constexpr Color Yellow() noexcept { return Color(1.0f, 1.0f, 0.0f); }
		[[nodiscard]] static constexpr Color Cyan() noexcept { return Color(0.0f, 1.0f, 1.0f); }
		[[nodiscard]] static constexpr Color Magenta() noexcept { return Color(1.0f, 0.0f, 1.0f); }
		[[nodiscard]] static constexpr Color Gray() noexcept { return Color(0.5f, 0.5f, 0.5f); }
		[[nodiscard]] static constexpr Color Clear() noexcept { return Color(0.0f, 0.0f, 0.0f, 0.0f); }

		[[nodiscard]] constexpr f32* Data() noexcept { return &r; }
		[[nodiscard]] constexpr const f32* Data() const noexcept { return &r; }

		[[nodiscard]] constexpr bool operator==(const Color& rhs) const noexcept { return r == rhs.r && g == rhs.g && b == rhs.b && a == rhs.a; }
		[[nodiscard]] constexpr bool operator!=(const Color& rhs) const noexcept { return !(*this == rhs); }

		[[nodiscard]] friend constexpr Color operator+(const Color& lhs, const Color& rhs) noexcept { return Color(lhs.r + rhs.r, lhs.g + rhs.g, lhs.b + rhs.b, lhs.a + rhs.a); }
		[[nodiscard]] friend constexpr Color operator-(const Color& lhs, const Color& rhs) noexcept { return Color(lhs.r - rhs.r, lhs.g - rhs.g, lhs.b - rhs.b, lhs.a - rhs.a); }
		[[nodiscard]] friend constexpr Color operator*(const Color& lhs, const Color& rhs) noexcept { return Color(lhs.r * rhs.r, lhs.g * rhs.g, lhs.b * rhs.b, lhs.a * rhs.a); }
		[[nodiscard]] friend constexpr Color operator*(const Color& lhs, f32 rhs) noexcept { return Color(lhs.r * rhs, lhs.g * rhs, lhs.b * rhs, lhs.a * rhs); }
		[[nodiscard]] friend constexpr Color operator*(f32 lhs, const Color& rhs) noexcept { return rhs * lhs; }

		constexpr Color& operator+=(const Color& rhs) noexcept {
			r += rhs.r;
			g += rhs.g;
			b += rhs.b;
			a += rhs.a;
			return *this;
		}
		constexpr Color& operator-=(const Color& rhs) noexcept {
			r -= rhs.r;
			g -= rhs.g;
			b -= rhs.b;
			a -= rhs.a;
			return *this;
		}
		constexpr Color& operator*=(const Color& rhs) noexcept {
			r *= rhs.r;
			g *= rhs.g;
			b *= rhs.b;
			a *= rhs.a;
			return *this;
		}
		constexpr Color& operator*=(f32 rhs) noexcept {
			r *= rhs;
			g *= rhs;
			b *= rhs;
			a *= rhs;
			return *this;
		}

		[[nodiscard]] static constexpr Color Lerp(const Color& x, const Color& y, f32 t) noexcept {
			return LerpUnclamped(x, y, Math::Clamp01(t));
		}

		[[nodiscard]] static constexpr Color LerpUnclamped(const Color& x, const Color& y, f32 t) noexcept {
			return x + (y - x) * t;
		}

		[[nodiscard]] static constexpr bool Approximately(const Color& x, const Color& y, f32 epsilon = Math::EPSILON) noexcept {
			return Math::Approximately(x.r, y.r, epsilon) && Math::Approximately(x.g, y.g, epsilon) &&
				   Math::Approximately(x.b, y.b, epsilon) && Math::Approximately(x.a, y.a, epsilon);
		}

		[[nodiscard]] constexpr Color Saturated() const noexcept {
			return Color(Math::Clamp01(r), Math::Clamp01(g), Math::Clamp01(b), Math::Clamp01(a));
		}

		[[nodiscard]] Color32 ToColor32() const noexcept;

		static constexpr bool TryParseHex(std::string_view text, Color& out) noexcept;

		[[nodiscard]] static constexpr Color FromHex(std::string_view text) noexcept {
			Color color = Magenta();
			TryParseHex(text, color);
			return color;
		}

		[[nodiscard]] str ToHex() const;

		[[nodiscard]] str ToString() const {
			return std::format("({}, {}, {}, {})", r, g, b, a);
		}
	};

	struct Color32 {
		u8 r, g, b, a;

		constexpr Color32(u8 r, u8 g, u8 b, u8 a = 255) noexcept : r(r), g(g), b(b), a(a) {}
		constexpr Color32() noexcept : r(0), g(0), b(0), a(0) {}

		[[nodiscard]] static constexpr Color32 White() noexcept { return Color32(255, 255, 255); }
		[[nodiscard]] static constexpr Color32 Black() noexcept { return Color32(0, 0, 0); }
		[[nodiscard]] static constexpr Color32 Clear() noexcept { return Color32(0, 0, 0, 0); }

		[[nodiscard]] constexpr bool operator==(const Color32& rhs) const noexcept { return r == rhs.r && g == rhs.g && b == rhs.b && a == rhs.a; }
		[[nodiscard]] constexpr bool operator!=(const Color32& rhs) const noexcept { return !(*this == rhs); }

		[[nodiscard]] constexpr u32 ToU32() const noexcept {
			return static_cast<u32>(r) | (static_cast<u32>(g) << 8) | (static_cast<u32>(b) << 16) | (static_cast<u32>(a) << 24);
		}

		[[nodiscard]] static constexpr Color32 FromU32(u32 packed) noexcept {
			return Color32(
				static_cast<u8>(packed & 0xFF),
				static_cast<u8>((packed >> 8) & 0xFF),
				static_cast<u8>((packed >> 16) & 0xFF),
				static_cast<u8>((packed >> 24) & 0xFF));
		}

		[[nodiscard]] constexpr Color ToColor() const noexcept {
			constexpr f32 INV = 1.0f / 255.0f;
			return Color(static_cast<f32>(r) * INV, static_cast<f32>(g) * INV, static_cast<f32>(b) * INV, static_cast<f32>(a) * INV);
		}

		[[nodiscard]] static Color32 Lerp(const Color32& x, const Color32& y, f32 t) noexcept {
			return Color::LerpUnclamped(x.ToColor(), y.ToColor(), Math::Clamp01(t)).ToColor32();
		}

		[[nodiscard]] str ToString() const {
			return std::format("({}, {}, {}, {})", r, g, b, a);
		}
	};

	inline Color32 Color::ToColor32() const noexcept {
		return Color32(
			static_cast<u8>(std::lround(Math::Clamp01(r) * 255.0f)),
			static_cast<u8>(std::lround(Math::Clamp01(g) * 255.0f)),
			static_cast<u8>(std::lround(Math::Clamp01(b) * 255.0f)),
			static_cast<u8>(std::lround(Math::Clamp01(a) * 255.0f)));
	}

	constexpr bool Color::TryParseHex(std::string_view text, Color& out) noexcept {
		if (!text.empty() && text[0] == '#') {
			text.remove_prefix(1);
		}
		size_t length = text.size();
		if (length != 3 && length != 4 && length != 6 && length != 8) {
			return false;
		}

		u32 value = 0;
		for (char c : text) {
			i32 digit;
			if (c >= '0' && c <= '9') {
				digit = c - '0';
			} else if (c >= 'a' && c <= 'f') {
				digit = c - 'a' + 10;
			} else if (c >= 'A' && c <= 'F') {
				digit = c - 'A' + 10;
			} else {
				return false;
			}
			value = (value << 4) | static_cast<u32>(digit);
		}

		constexpr f32 INV15 = 1.0f / 15.0f;
		constexpr f32 INV255 = 1.0f / 255.0f;
		switch (length) {
		case 3:
			out = Color(((value >> 8) & 0xF) * INV15, ((value >> 4) & 0xF) * INV15, (value & 0xF) * INV15);
			return true;
		case 4:
			out = Color(((value >> 12) & 0xF) * INV15, ((value >> 8) & 0xF) * INV15, ((value >> 4) & 0xF) * INV15, (value & 0xF) * INV15);
			return true;
		case 6:
			out = Color(((value >> 16) & 0xFF) * INV255, ((value >> 8) & 0xFF) * INV255, (value & 0xFF) * INV255);
			return true;
		default:
			out = Color(((value >> 24) & 0xFF) * INV255, ((value >> 16) & 0xFF) * INV255, ((value >> 8) & 0xFF) * INV255, (value & 0xFF) * INV255);
			return true;
		}
	}

	inline str Color::ToHex() const {
		Color32 c = ToColor32();
		return std::format("#{:02x}{:02x}{:02x}{:02x}", c.r, c.g, c.b, c.a);
	}

	static_assert(sizeof(Color) == 16 && sizeof(Color32) == 4);
} //namespace Shalltear

template <>
struct std::formatter<Shalltear::Color> : std::formatter<std::string> {
	auto format(const Shalltear::Color& c, std::format_context& ctx) const {
		return std::formatter<std::string>::format(c.ToString(), ctx);
	}
};

template <>
struct std::formatter<Shalltear::Color32> : std::formatter<std::string> {
	auto format(const Shalltear::Color32& c, std::format_context& ctx) const {
		return std::formatter<std::string>::format(c.ToString(), ctx);
	}
};
