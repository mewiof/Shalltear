#pragma once

#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Types.h"

#include <cassert>
#include <format>
#include <type_traits>

namespace Shalltear {

	template <typename T>
	struct Vec2Generic {
		static_assert(std::is_arithmetic_v<T>, "Vec2Generic can only be instantiated with arithmetic types");

		static constexpr bool IsFloat = std::is_floating_point_v<T>;

		T x, y;

		constexpr Vec2Generic(T x, T y) noexcept : x(x), y(y) {}
		constexpr explicit Vec2Generic(T v) noexcept : x(v), y(v) {}
		constexpr Vec2Generic() noexcept : x(T(0)), y(T(0)) {}

		template <typename U>
		constexpr explicit Vec2Generic(const Vec2Generic<U>& v) noexcept
			: x(static_cast<T>(v.x)), y(static_cast<T>(v.y)) {}

		[[nodiscard]] static constexpr Vec2Generic Zero() noexcept { return Vec2Generic(T(0), T(0)); }
		[[nodiscard]] static constexpr Vec2Generic One() noexcept { return Vec2Generic(T(1), T(1)); }
		[[nodiscard]] static constexpr Vec2Generic Right() noexcept { return Vec2Generic(T(1), T(0)); }
		[[nodiscard]] static constexpr Vec2Generic Left() noexcept { return Vec2Generic(T(-1), T(0)); }
		[[nodiscard]] static constexpr Vec2Generic Up() noexcept { return Vec2Generic(T(0), T(1)); }
		[[nodiscard]] static constexpr Vec2Generic Down() noexcept { return Vec2Generic(T(0), T(-1)); }

		[[nodiscard]] constexpr T* Data() noexcept { return &x; }
		[[nodiscard]] constexpr const T* Data() const noexcept { return &x; }

		[[nodiscard]] constexpr T& operator[](size_t index) noexcept {
			assert(index < 2 && "Vec2 idx out of bounds");
			return index == 0 ? x : y;
		}
		[[nodiscard]] constexpr const T& operator[](size_t index) const noexcept {
			assert(index < 2 && "Vec2 idx out of bounds");
			return index == 0 ? x : y;
		}

		[[nodiscard]] constexpr bool operator==(const Vec2Generic& rhs) const noexcept { return x == rhs.x && y == rhs.y; }
		[[nodiscard]] constexpr bool operator!=(const Vec2Generic& rhs) const noexcept { return !(*this == rhs); }

		[[nodiscard]] constexpr Vec2Generic operator-() const noexcept { return Vec2Generic(-x, -y); }

		[[nodiscard]] friend constexpr Vec2Generic operator+(const Vec2Generic& lhs, const Vec2Generic& rhs) noexcept { return Vec2Generic(lhs.x + rhs.x, lhs.y + rhs.y); }
		[[nodiscard]] friend constexpr Vec2Generic operator-(const Vec2Generic& lhs, const Vec2Generic& rhs) noexcept { return Vec2Generic(lhs.x - rhs.x, lhs.y - rhs.y); }
		[[nodiscard]] friend constexpr Vec2Generic operator*(const Vec2Generic& lhs, const Vec2Generic& rhs) noexcept { return Vec2Generic(lhs.x * rhs.x, lhs.y * rhs.y); }
		[[nodiscard]] friend constexpr Vec2Generic operator/(const Vec2Generic& lhs, const Vec2Generic& rhs) noexcept { return Vec2Generic(lhs.x / rhs.x, lhs.y / rhs.y); }
		[[nodiscard]] friend constexpr Vec2Generic operator*(const Vec2Generic& lhs, T rhs) noexcept { return Vec2Generic(lhs.x * rhs, lhs.y * rhs); }
		[[nodiscard]] friend constexpr Vec2Generic operator*(T lhs, const Vec2Generic& rhs) noexcept { return Vec2Generic(lhs * rhs.x, lhs * rhs.y); }
		[[nodiscard]] friend constexpr Vec2Generic operator/(const Vec2Generic& lhs, T rhs) noexcept { return Vec2Generic(lhs.x / rhs, lhs.y / rhs); }

		constexpr Vec2Generic& operator+=(const Vec2Generic& rhs) noexcept {
			x += rhs.x;
			y += rhs.y;
			return *this;
		}
		constexpr Vec2Generic& operator-=(const Vec2Generic& rhs) noexcept {
			x -= rhs.x;
			y -= rhs.y;
			return *this;
		}
		constexpr Vec2Generic& operator*=(const Vec2Generic& rhs) noexcept {
			x *= rhs.x;
			y *= rhs.y;
			return *this;
		}
		constexpr Vec2Generic& operator/=(const Vec2Generic& rhs) noexcept {
			x /= rhs.x;
			y /= rhs.y;
			return *this;
		}
		constexpr Vec2Generic& operator*=(T rhs) noexcept {
			x *= rhs;
			y *= rhs;
			return *this;
		}
		constexpr Vec2Generic& operator/=(T rhs) noexcept {
			x /= rhs;
			y /= rhs;
			return *this;
		}

		[[nodiscard]] constexpr T LengthSquared() const noexcept { return x * x + y * y; }

		[[nodiscard]] f32 Length() const noexcept
			requires IsFloat
		{
			return Math::Sqrt(x * x + y * y);
		}

		[[nodiscard]] Vec2Generic Normalized() const noexcept
			requires IsFloat
		{
			f32 length = Length();
			return length > Math::EPSILON ? *this / length : Zero();
		}

		void Normalize() noexcept
			requires IsFloat
		{
			*this = Normalized();
		}

		[[nodiscard]] static constexpr T Dot(const Vec2Generic& a, const Vec2Generic& b) noexcept {
			return a.x * b.x + a.y * b.y;
		}

		[[nodiscard]] static constexpr T Cross(const Vec2Generic& a, const Vec2Generic& b) noexcept {
			return a.x * b.y - a.y * b.x;
		}

		[[nodiscard]] static constexpr T DistanceSquared(const Vec2Generic& a, const Vec2Generic& b) noexcept {
			return (b - a).LengthSquared();
		}

		[[nodiscard]] static f32 Distance(const Vec2Generic& a, const Vec2Generic& b) noexcept
			requires IsFloat
		{
			return (b - a).Length();
		}

		[[nodiscard]] static constexpr Vec2Generic Lerp(const Vec2Generic& a, const Vec2Generic& b, f32 t) noexcept
			requires IsFloat
		{
			return LerpUnclamped(a, b, Math::Clamp01(t));
		}

		[[nodiscard]] static constexpr Vec2Generic LerpUnclamped(const Vec2Generic& a, const Vec2Generic& b, f32 t) noexcept
			requires IsFloat
		{
			return a + (b - a) * t;
		}

		[[nodiscard]] static constexpr Vec2Generic Min(const Vec2Generic& a, const Vec2Generic& b) noexcept {
			return Vec2Generic(Math::Min(a.x, b.x), Math::Min(a.y, b.y));
		}

		[[nodiscard]] static constexpr Vec2Generic Max(const Vec2Generic& a, const Vec2Generic& b) noexcept {
			return Vec2Generic(Math::Max(a.x, b.x), Math::Max(a.y, b.y));
		}

		[[nodiscard]] static constexpr Vec2Generic Abs(const Vec2Generic& v) noexcept {
			return Vec2Generic(Math::Abs(v.x), Math::Abs(v.y));
		}

		// counter clockwise by 90 degrees
		[[nodiscard]] constexpr Vec2Generic Perpendicular() const noexcept {
			return Vec2Generic(-y, x);
		}

		[[nodiscard]] static constexpr Vec2Generic Reflect(const Vec2Generic& direction, const Vec2Generic& normal) noexcept
			requires IsFloat
		{
			return direction - normal * (T(2) * Dot(direction, normal));
		}

		[[nodiscard]] static constexpr bool Approximately(const Vec2Generic& a, const Vec2Generic& b, f32 epsilon = Math::EPSILON) noexcept
			requires IsFloat
		{
			return Math::Approximately(a.x, b.x, epsilon) && Math::Approximately(a.y, b.y, epsilon);
		}

		[[nodiscard]] str ToString() const {
			return std::format("({}, {})", x, y);
		}
	};

	template <typename T>
	struct Vec3Generic {
		static_assert(std::is_arithmetic_v<T>, "Vec3Generic can only be instantiated with arithmetic types");

		static constexpr bool IsFloat = std::is_floating_point_v<T>;

		T x, y, z;

		constexpr Vec3Generic(T x, T y, T z) noexcept : x(x), y(y), z(z) {}
		constexpr explicit Vec3Generic(T v) noexcept : x(v), y(v), z(v) {}
		constexpr Vec3Generic() noexcept : x(T(0)), y(T(0)), z(T(0)) {}
		constexpr Vec3Generic(const Vec2Generic<T>& xy, T z) noexcept : x(xy.x), y(xy.y), z(z) {}

		template <typename U>
		constexpr explicit Vec3Generic(const Vec3Generic<U>& v) noexcept
			: x(static_cast<T>(v.x)), y(static_cast<T>(v.y)), z(static_cast<T>(v.z)) {}

		[[nodiscard]] static constexpr Vec3Generic Zero() noexcept { return Vec3Generic(T(0), T(0), T(0)); }
		[[nodiscard]] static constexpr Vec3Generic One() noexcept { return Vec3Generic(T(1), T(1), T(1)); }
		[[nodiscard]] static constexpr Vec3Generic Right() noexcept { return Vec3Generic(T(1), T(0), T(0)); }
		[[nodiscard]] static constexpr Vec3Generic Left() noexcept { return Vec3Generic(T(-1), T(0), T(0)); }
		[[nodiscard]] static constexpr Vec3Generic Up() noexcept { return Vec3Generic(T(0), T(1), T(0)); }
		[[nodiscard]] static constexpr Vec3Generic Down() noexcept { return Vec3Generic(T(0), T(-1), T(0)); }
		[[nodiscard]] static constexpr Vec3Generic Forward() noexcept { return Vec3Generic(T(0), T(0), T(1)); }
		[[nodiscard]] static constexpr Vec3Generic Back() noexcept { return Vec3Generic(T(0), T(0), T(-1)); }

		[[nodiscard]] constexpr T* Data() noexcept { return &x; }
		[[nodiscard]] constexpr const T* Data() const noexcept { return &x; }

		[[nodiscard]] constexpr T& operator[](size_t index) noexcept {
			assert(index < 3 && "Vec3 idx out of bounds");
			switch (index) {
			case 0:
				return x;
			case 1:
				return y;
			default:
				return z;
			}
		}
		[[nodiscard]] constexpr const T& operator[](size_t index) const noexcept {
			assert(index < 3 && "Vec3 idx out of bounds");
			switch (index) {
			case 0:
				return x;
			case 1:
				return y;
			default:
				return z;
			}
		}

		[[nodiscard]] constexpr bool operator==(const Vec3Generic& rhs) const noexcept { return x == rhs.x && y == rhs.y && z == rhs.z; }
		[[nodiscard]] constexpr bool operator!=(const Vec3Generic& rhs) const noexcept { return !(*this == rhs); }

		[[nodiscard]] constexpr Vec3Generic operator-() const noexcept { return Vec3Generic(-x, -y, -z); }

		[[nodiscard]] friend constexpr Vec3Generic operator+(const Vec3Generic& lhs, const Vec3Generic& rhs) noexcept { return Vec3Generic(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z); }
		[[nodiscard]] friend constexpr Vec3Generic operator-(const Vec3Generic& lhs, const Vec3Generic& rhs) noexcept { return Vec3Generic(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z); }
		[[nodiscard]] friend constexpr Vec3Generic operator*(const Vec3Generic& lhs, const Vec3Generic& rhs) noexcept { return Vec3Generic(lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z); }
		[[nodiscard]] friend constexpr Vec3Generic operator/(const Vec3Generic& lhs, const Vec3Generic& rhs) noexcept { return Vec3Generic(lhs.x / rhs.x, lhs.y / rhs.y, lhs.z / rhs.z); }
		[[nodiscard]] friend constexpr Vec3Generic operator*(const Vec3Generic& lhs, T rhs) noexcept { return Vec3Generic(lhs.x * rhs, lhs.y * rhs, lhs.z * rhs); }
		[[nodiscard]] friend constexpr Vec3Generic operator*(T lhs, const Vec3Generic& rhs) noexcept { return Vec3Generic(lhs * rhs.x, lhs * rhs.y, lhs * rhs.z); }
		[[nodiscard]] friend constexpr Vec3Generic operator/(const Vec3Generic& lhs, T rhs) noexcept { return Vec3Generic(lhs.x / rhs, lhs.y / rhs, lhs.z / rhs); }

		constexpr Vec3Generic& operator+=(const Vec3Generic& rhs) noexcept {
			x += rhs.x;
			y += rhs.y;
			z += rhs.z;
			return *this;
		}
		constexpr Vec3Generic& operator-=(const Vec3Generic& rhs) noexcept {
			x -= rhs.x;
			y -= rhs.y;
			z -= rhs.z;
			return *this;
		}
		constexpr Vec3Generic& operator*=(const Vec3Generic& rhs) noexcept {
			x *= rhs.x;
			y *= rhs.y;
			z *= rhs.z;
			return *this;
		}
		constexpr Vec3Generic& operator/=(const Vec3Generic& rhs) noexcept {
			x /= rhs.x;
			y /= rhs.y;
			z /= rhs.z;
			return *this;
		}
		constexpr Vec3Generic& operator*=(T rhs) noexcept {
			x *= rhs;
			y *= rhs;
			z *= rhs;
			return *this;
		}
		constexpr Vec3Generic& operator/=(T rhs) noexcept {
			x /= rhs;
			y /= rhs;
			z /= rhs;
			return *this;
		}

		[[nodiscard]] constexpr T LengthSquared() const noexcept { return x * x + y * y + z * z; }

		[[nodiscard]] f32 Length() const noexcept
			requires IsFloat
		{
			return Math::Sqrt(x * x + y * y + z * z);
		}

		[[nodiscard]] Vec3Generic Normalized() const noexcept
			requires IsFloat
		{
			f32 length = Length();
			return length > Math::EPSILON ? *this / length : Zero();
		}

		void Normalize() noexcept
			requires IsFloat
		{
			*this = Normalized();
		}

		[[nodiscard]] static constexpr T Dot(const Vec3Generic& a, const Vec3Generic& b) noexcept {
			return a.x * b.x + a.y * b.y + a.z * b.z;
		}

		[[nodiscard]] static constexpr Vec3Generic Cross(const Vec3Generic& a, const Vec3Generic& b) noexcept {
			return Vec3Generic(
				a.y * b.z - a.z * b.y,
				a.z * b.x - a.x * b.z,
				a.x * b.y - a.y * b.x);
		}

		[[nodiscard]] static constexpr T DistanceSquared(const Vec3Generic& a, const Vec3Generic& b) noexcept {
			return (b - a).LengthSquared();
		}

		[[nodiscard]] static f32 Distance(const Vec3Generic& a, const Vec3Generic& b) noexcept
			requires IsFloat
		{
			return (b - a).Length();
		}

		[[nodiscard]] static constexpr Vec3Generic Lerp(const Vec3Generic& a, const Vec3Generic& b, f32 t) noexcept
			requires IsFloat
		{
			return LerpUnclamped(a, b, Math::Clamp01(t));
		}

		[[nodiscard]] static constexpr Vec3Generic LerpUnclamped(const Vec3Generic& a, const Vec3Generic& b, f32 t) noexcept
			requires IsFloat
		{
			return a + (b - a) * t;
		}

		[[nodiscard]] static constexpr Vec3Generic Min(const Vec3Generic& a, const Vec3Generic& b) noexcept {
			return Vec3Generic(Math::Min(a.x, b.x), Math::Min(a.y, b.y), Math::Min(a.z, b.z));
		}

		[[nodiscard]] static constexpr Vec3Generic Max(const Vec3Generic& a, const Vec3Generic& b) noexcept {
			return Vec3Generic(Math::Max(a.x, b.x), Math::Max(a.y, b.y), Math::Max(a.z, b.z));
		}

		[[nodiscard]] static constexpr Vec3Generic Abs(const Vec3Generic& v) noexcept {
			return Vec3Generic(Math::Abs(v.x), Math::Abs(v.y), Math::Abs(v.z));
		}

		[[nodiscard]] static constexpr Vec3Generic Reflect(const Vec3Generic& direction, const Vec3Generic& normal) noexcept
			requires IsFloat
		{
			return direction - normal * (T(2) * Dot(direction, normal));
		}

		[[nodiscard]] static constexpr Vec3Generic Project(const Vec3Generic& v, const Vec3Generic& onto) noexcept
			requires IsFloat
		{
			T denominator = Dot(onto, onto);
			if (denominator <= Math::EPSILON) {
				return Zero();
			}
			return onto * (Dot(v, onto) / denominator);
		}

		[[nodiscard]] static constexpr Vec3Generic ProjectOnPlane(const Vec3Generic& v, const Vec3Generic& planeNormal) noexcept
			requires IsFloat
		{
			return v - Project(v, planeNormal);
		}

		// unsigned degrees
		[[nodiscard]] static f32 Angle(const Vec3Generic& a, const Vec3Generic& b) noexcept
			requires IsFloat
		{
			f32 denominator = Math::Sqrt(static_cast<f32>(a.LengthSquared() * b.LengthSquared()));
			if (denominator <= Math::EPSILON) {
				return 0.0f;
			}
			f32 cosine = Math::Clamp(static_cast<f32>(Dot(a, b)) / denominator, -1.0f, 1.0f);
			return std::acos(cosine) * Math::RAD_TO_DEG;
		}

		[[nodiscard]] static constexpr bool Approximately(const Vec3Generic& a, const Vec3Generic& b, f32 epsilon = Math::EPSILON) noexcept
			requires IsFloat
		{
			return Math::Approximately(a.x, b.x, epsilon) && Math::Approximately(a.y, b.y, epsilon) && Math::Approximately(a.z, b.z, epsilon);
		}

		[[nodiscard]] str ToString() const {
			return std::format("({}, {}, {})", x, y, z);
		}
	};

	template <typename T>
	struct alignas(sizeof(T) * 4) Vec4Generic {
		static_assert(std::is_arithmetic_v<T>, "Vec4Generic can only be instantiated with arithmetic types");

		static constexpr bool IsFloat = std::is_floating_point_v<T>;

		T x, y, z, w;

		constexpr Vec4Generic(T x, T y, T z, T w) noexcept : x(x), y(y), z(z), w(w) {}
		constexpr explicit Vec4Generic(T v) noexcept : x(v), y(v), z(v), w(v) {}
		constexpr Vec4Generic() noexcept : x(T(0)), y(T(0)), z(T(0)), w(T(0)) {}
		constexpr Vec4Generic(const Vec3Generic<T>& xyz, T w) noexcept : x(xyz.x), y(xyz.y), z(xyz.z), w(w) {}

		template <typename U>
		constexpr explicit Vec4Generic(const Vec4Generic<U>& v) noexcept
			: x(static_cast<T>(v.x)), y(static_cast<T>(v.y)), z(static_cast<T>(v.z)), w(static_cast<T>(v.w)) {}

		[[nodiscard]] static constexpr Vec4Generic Zero() noexcept { return Vec4Generic(T(0), T(0), T(0), T(0)); }
		[[nodiscard]] static constexpr Vec4Generic One() noexcept { return Vec4Generic(T(1), T(1), T(1), T(1)); }

		[[nodiscard]] constexpr T* Data() noexcept { return &x; }
		[[nodiscard]] constexpr const T* Data() const noexcept { return &x; }

		[[nodiscard]] constexpr T& operator[](size_t index) noexcept {
			assert(index < 4 && "Vec4 idx out of bounds");
			switch (index) {
			case 0:
				return x;
			case 1:
				return y;
			case 2:
				return z;
			default:
				return w;
			}
		}
		[[nodiscard]] constexpr const T& operator[](size_t index) const noexcept {
			assert(index < 4 && "Vec4 idx out of bounds");
			switch (index) {
			case 0:
				return x;
			case 1:
				return y;
			case 2:
				return z;
			default:
				return w;
			}
		}

		[[nodiscard]] constexpr Vec3Generic<T> XYZ() const noexcept { return Vec3Generic<T>(x, y, z); }

		[[nodiscard]] constexpr bool operator==(const Vec4Generic& rhs) const noexcept { return x == rhs.x && y == rhs.y && z == rhs.z && w == rhs.w; }
		[[nodiscard]] constexpr bool operator!=(const Vec4Generic& rhs) const noexcept { return !(*this == rhs); }

		[[nodiscard]] constexpr Vec4Generic operator-() const noexcept { return Vec4Generic(-x, -y, -z, -w); }

		[[nodiscard]] friend constexpr Vec4Generic operator+(const Vec4Generic& lhs, const Vec4Generic& rhs) noexcept { return Vec4Generic(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, lhs.w + rhs.w); }
		[[nodiscard]] friend constexpr Vec4Generic operator-(const Vec4Generic& lhs, const Vec4Generic& rhs) noexcept { return Vec4Generic(lhs.x - rhs.x, lhs.y - rhs.y, lhs.z - rhs.z, lhs.w - rhs.w); }
		[[nodiscard]] friend constexpr Vec4Generic operator*(const Vec4Generic& lhs, const Vec4Generic& rhs) noexcept { return Vec4Generic(lhs.x * rhs.x, lhs.y * rhs.y, lhs.z * rhs.z, lhs.w * rhs.w); }
		[[nodiscard]] friend constexpr Vec4Generic operator/(const Vec4Generic& lhs, const Vec4Generic& rhs) noexcept { return Vec4Generic(lhs.x / rhs.x, lhs.y / rhs.y, lhs.z / rhs.z, lhs.w / rhs.w); }
		[[nodiscard]] friend constexpr Vec4Generic operator*(const Vec4Generic& lhs, T rhs) noexcept { return Vec4Generic(lhs.x * rhs, lhs.y * rhs, lhs.z * rhs, lhs.w * rhs); }
		[[nodiscard]] friend constexpr Vec4Generic operator*(T lhs, const Vec4Generic& rhs) noexcept { return Vec4Generic(lhs * rhs.x, lhs * rhs.y, lhs * rhs.z, lhs * rhs.w); }
		[[nodiscard]] friend constexpr Vec4Generic operator/(const Vec4Generic& lhs, T rhs) noexcept { return Vec4Generic(lhs.x / rhs, lhs.y / rhs, lhs.z / rhs, lhs.w / rhs); }

		constexpr Vec4Generic& operator+=(const Vec4Generic& rhs) noexcept {
			x += rhs.x;
			y += rhs.y;
			z += rhs.z;
			w += rhs.w;
			return *this;
		}
		constexpr Vec4Generic& operator-=(const Vec4Generic& rhs) noexcept {
			x -= rhs.x;
			y -= rhs.y;
			z -= rhs.z;
			w -= rhs.w;
			return *this;
		}
		constexpr Vec4Generic& operator*=(const Vec4Generic& rhs) noexcept {
			x *= rhs.x;
			y *= rhs.y;
			z *= rhs.z;
			w *= rhs.w;
			return *this;
		}
		constexpr Vec4Generic& operator/=(const Vec4Generic& rhs) noexcept {
			x /= rhs.x;
			y /= rhs.y;
			z /= rhs.z;
			w /= rhs.w;
			return *this;
		}
		constexpr Vec4Generic& operator*=(T rhs) noexcept {
			x *= rhs;
			y *= rhs;
			z *= rhs;
			w *= rhs;
			return *this;
		}
		constexpr Vec4Generic& operator/=(T rhs) noexcept {
			x /= rhs;
			y /= rhs;
			z /= rhs;
			w /= rhs;
			return *this;
		}

		[[nodiscard]] constexpr T LengthSquared() const noexcept { return x * x + y * y + z * z + w * w; }

		[[nodiscard]] f32 Length() const noexcept
			requires IsFloat
		{
			return Math::Sqrt(x * x + y * y + z * z + w * w);
		}

		[[nodiscard]] Vec4Generic Normalized() const noexcept
			requires IsFloat
		{
			f32 length = Length();
			return length > Math::EPSILON ? *this / length : Zero();
		}

		void Normalize() noexcept
			requires IsFloat
		{
			*this = Normalized();
		}

		[[nodiscard]] static constexpr T Dot(const Vec4Generic& a, const Vec4Generic& b) noexcept {
			return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
		}

		[[nodiscard]] static constexpr Vec4Generic Lerp(const Vec4Generic& a, const Vec4Generic& b, f32 t) noexcept
			requires IsFloat
		{
			return LerpUnclamped(a, b, Math::Clamp01(t));
		}

		[[nodiscard]] static constexpr Vec4Generic LerpUnclamped(const Vec4Generic& a, const Vec4Generic& b, f32 t) noexcept
			requires IsFloat
		{
			return a + (b - a) * t;
		}

		[[nodiscard]] static constexpr Vec4Generic Min(const Vec4Generic& a, const Vec4Generic& b) noexcept {
			return Vec4Generic(Math::Min(a.x, b.x), Math::Min(a.y, b.y), Math::Min(a.z, b.z), Math::Min(a.w, b.w));
		}

		[[nodiscard]] static constexpr Vec4Generic Max(const Vec4Generic& a, const Vec4Generic& b) noexcept {
			return Vec4Generic(Math::Max(a.x, b.x), Math::Max(a.y, b.y), Math::Max(a.z, b.z), Math::Max(a.w, b.w));
		}

		[[nodiscard]] static constexpr bool Approximately(const Vec4Generic& a, const Vec4Generic& b, f32 epsilon = Math::EPSILON) noexcept
			requires IsFloat
		{
			return Math::Approximately(a.x, b.x, epsilon) && Math::Approximately(a.y, b.y, epsilon) &&
				   Math::Approximately(a.z, b.z, epsilon) && Math::Approximately(a.w, b.w, epsilon);
		}

		[[nodiscard]] str ToString() const {
			return std::format("({}, {}, {}, {})", x, y, z, w);
		}
	};

	using Vec2 = Vec2Generic<f32>;
	using Vec3 = Vec3Generic<f32>;
	using Vec4 = Vec4Generic<f32>;

	using Vec2I = Vec2Generic<i32>;
	using Vec3I = Vec3Generic<i32>;
	using Vec4I = Vec4Generic<i32>;

	static_assert(sizeof(Vec2) == 8 && sizeof(Vec3) == 12 && sizeof(Vec4) == 16);
	static_assert(alignof(Vec4) == 16, "Vec4 must be 16-byte aligned for SIMD/GPU upload");
} //namespace Shalltear

template <typename T>
struct std::formatter<Shalltear::Vec2Generic<T>> : std::formatter<std::string> {
	auto format(const Shalltear::Vec2Generic<T>& v, std::format_context& ctx) const {
		return std::formatter<std::string>::format(v.ToString(), ctx);
	}
};

template <typename T>
struct std::formatter<Shalltear::Vec3Generic<T>> : std::formatter<std::string> {
	auto format(const Shalltear::Vec3Generic<T>& v, std::format_context& ctx) const {
		return std::formatter<std::string>::format(v.ToString(), ctx);
	}
};

template <typename T>
struct std::formatter<Shalltear::Vec4Generic<T>> : std::formatter<std::string> {
	auto format(const Shalltear::Vec4Generic<T>& v, std::format_context& ctx) const {
		return std::formatter<std::string>::format(v.ToString(), ctx);
	}
};
