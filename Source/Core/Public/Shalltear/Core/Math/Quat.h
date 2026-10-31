#pragma once

#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include <cassert>
#include <cmath>
#include <format>

namespace Shalltear {

	enum class RotationOrder : u8 {
		XYZ,
		XZY,
		YXZ,
		YZX,
		ZXY,
		ZYX,
		Default = ZXY,
	};

	struct alignas(16) Quat {
		f32 x, y, z, w;

		constexpr Quat(f32 x, f32 y, f32 z, f32 w) noexcept : x(x), y(y), z(z), w(w) {}

		constexpr Quat() noexcept : x(0.0f), y(0.0f), z(0.0f), w(1.0f) {}

		[[nodiscard]] static constexpr Quat Identity() noexcept { return Quat(0.0f, 0.0f, 0.0f, 1.0f); }

		[[nodiscard]] constexpr f32* Data() noexcept { return &x; }
		[[nodiscard]] constexpr const f32* Data() const noexcept { return &x; }

		[[nodiscard]] constexpr bool operator==(const Quat& rhs) const noexcept { return x == rhs.x && y == rhs.y && z == rhs.z && w == rhs.w; }
		[[nodiscard]] constexpr bool operator!=(const Quat& rhs) const noexcept { return !(*this == rhs); }

		[[nodiscard]] friend constexpr Quat operator*(const Quat& lhs, const Quat& rhs) noexcept {
			return Quat(
				lhs.w * rhs.x + lhs.x * rhs.w + lhs.y * rhs.z - lhs.z * rhs.y,
				lhs.w * rhs.y + lhs.y * rhs.w + lhs.z * rhs.x - lhs.x * rhs.z,
				lhs.w * rhs.z + lhs.z * rhs.w + lhs.x * rhs.y - lhs.y * rhs.x,
				lhs.w * rhs.w - lhs.x * rhs.x - lhs.y * rhs.y - lhs.z * rhs.z);
		}
		constexpr Quat& operator*=(const Quat& rhs) noexcept {
			*this = *this * rhs;
			return *this;
		}

		[[nodiscard]] friend constexpr Vec3 operator*(const Quat& q, const Vec3& v) noexcept {
			f32 xx = q.x * 2.0f;
			f32 yy = q.y * 2.0f;
			f32 zz = q.z * 2.0f;
			f32 xX = q.x * xx;
			f32 yY = q.y * yy;
			f32 zZ = q.z * zz;
			f32 xY = q.x * yy;
			f32 xZ = q.x * zz;
			f32 yZ = q.y * zz;
			f32 wX = q.w * xx;
			f32 wY = q.w * yy;
			f32 wZ = q.w * zz;
			return Vec3(
				(1.0f - (yY + zZ)) * v.x + (xY - wZ) * v.y + (xZ + wY) * v.z,
				(xY + wZ) * v.x + (1.0f - (xX + zZ)) * v.y + (yZ - wX) * v.z,
				(xZ - wY) * v.x + (yZ + wX) * v.y + (1.0f - (xX + yY)) * v.z);
		}

		[[nodiscard]] constexpr Quat operator-() const noexcept { return Quat(-x, -y, -z, -w); }
		[[nodiscard]] friend constexpr Quat operator*(const Quat& lhs, f32 rhs) noexcept { return Quat(lhs.x * rhs, lhs.y * rhs, lhs.z * rhs, lhs.w * rhs); }
		[[nodiscard]] friend constexpr Quat operator+(const Quat& lhs, const Quat& rhs) noexcept { return Quat(lhs.x + rhs.x, lhs.y + rhs.y, lhs.z + rhs.z, lhs.w + rhs.w); }

		[[nodiscard]] static constexpr f32 Dot(const Quat& a, const Quat& b) noexcept {
			return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w;
		}

		[[nodiscard]] constexpr f32 LengthSquared() const noexcept { return Dot(*this, *this); }
		[[nodiscard]] f32 Length() const noexcept { return Math::Sqrt(LengthSquared()); }

		[[nodiscard]] Quat Normalized() const noexcept {
			f32 length = Length();
			if (length <= Math::EPSILON) {
				return Identity();
			}
			f32 inverse = 1.0f / length;
			return Quat(x * inverse, y * inverse, z * inverse, w * inverse);
		}

		void Normalize() noexcept { *this = Normalized(); }

		[[nodiscard]] constexpr Quat Conjugated() const noexcept { return Quat(-x, -y, -z, w); }

		[[nodiscard]] constexpr Quat Inverse() const noexcept {
			f32 lengthSquared = LengthSquared();
			if (lengthSquared <= Math::EPSILON) {
				return Identity();
			}
			f32 inverse = 1.0f / lengthSquared;
			return Quat(-x * inverse, -y * inverse, -z * inverse, w * inverse);
		}

		[[nodiscard]] static Quat AngleAxis(f32 degrees, const Vec3& axis) noexcept {
			Vec3 unitAxis = axis.Normalized();
			if (unitAxis == Vec3::Zero()) {
				return Identity();
			}
			f32 half = degrees * Math::DEG_TO_RAD * 0.5f;
			f32 sine = std::sin(half);
			return Quat(unitAxis.x * sine, unitAxis.y * sine, unitAxis.z * sine, std::cos(half));
		}

		// degrees, ZXY = qY * qX * qZ
		[[nodiscard]] static Quat FromEuler(const Vec3& eulerDegrees, RotationOrder order = RotationOrder::Default) noexcept {
			Vec3 half = eulerDegrees * (Math::DEG_TO_RAD * 0.5f);
			Quat qX(std::sin(half.x), 0.0f, 0.0f, std::cos(half.x));
			Quat qY(0.0f, std::sin(half.y), 0.0f, std::cos(half.y));
			Quat qZ(0.0f, 0.0f, std::sin(half.z), std::cos(half.z));

			switch (order) {
			case RotationOrder::XYZ:
				return qZ * qY * qX;
			case RotationOrder::XZY:
				return qY * qZ * qX;
			case RotationOrder::YXZ:
				return qZ * qX * qY;
			case RotationOrder::YZX:
				return qX * qZ * qY;
			case RotationOrder::ZXY:
				return qY * qX * qZ;
			case RotationOrder::ZYX:
				return qX * qY * qZ;
			}
			return Identity();
		}

		[[nodiscard]] static Quat FromEuler(f32 xDegrees, f32 yDegrees, f32 zDegrees, RotationOrder order = RotationOrder::Default) noexcept {
			return FromEuler(Vec3(xDegrees, yDegrees, zDegrees), order);
		}

		// degrees, default ZXY order only
		[[nodiscard]] Vec3 ToEuler() const noexcept {
			f32 xx = x * x;
			f32 yy = y * y;
			f32 zz = z * z;
			f32 ww = w * w;
			f32 m02 = 2.0f * (x * z + w * y);
			f32 m10 = 2.0f * (x * y + w * z);
			f32 m11 = (ww - xx) + (yy - zz);
			f32 m12 = 2.0f * (y * z - w * x);
			f32 m22 = (ww - xx) - (yy - zz);

			f32 cosineX = std::sqrt(m10 * m10 + m11 * m11);
			f32 angleX = std::atan2(-m12, cosineX);

			f32 angleY;
			f32 angleZ;
			if (cosineX > 0.01414f * (xx + yy + zz + ww)) {
				angleY = std::atan2(m02, m22);
				angleZ = std::atan2(m10, m11);
			} else {
				f32 m00 = (ww + xx) - (yy + zz);
				f32 m20 = 2.0f * (x * z - w * y);
				angleY = std::atan2(-m20, m00);
				angleZ = 0.0f;
			}
			return Vec3(angleX, angleY, angleZ) * Math::RAD_TO_DEG;
		}

		[[nodiscard]] static Quat FromBasis(const Vec3& right, const Vec3& up, const Vec3& forward) noexcept {
			f32 m00 = right.x, m01 = up.x, m02 = forward.x;
			f32 m10 = right.y, m11 = up.y, m12 = forward.y;
			f32 m20 = right.z, m21 = up.z, m22 = forward.z;

			f32 trace = m00 + m11 + m22;
			if (trace > 0.0f) {
				f32 s = Math::Sqrt(trace + 1.0f) * 2.0f;
				return Quat((m21 - m12) / s, (m02 - m20) / s, (m10 - m01) / s, 0.25f * s);
			}
			if (m00 > m11 && m00 > m22) {
				f32 s = Math::Sqrt(1.0f + m00 - m11 - m22) * 2.0f;
				return Quat(0.25f * s, (m01 + m10) / s, (m02 + m20) / s, (m21 - m12) / s);
			}
			if (m11 > m22) {
				f32 s = Math::Sqrt(1.0f + m11 - m00 - m22) * 2.0f;
				return Quat((m01 + m10) / s, 0.25f * s, (m12 + m21) / s, (m02 - m20) / s);
			}
			f32 s = Math::Sqrt(1.0f + m22 - m00 - m11) * 2.0f;
			return Quat((m02 + m20) / s, (m12 + m21) / s, 0.25f * s, (m10 - m01) / s);
		}

		[[nodiscard]] static Quat LookRotation(const Vec3& forward, const Vec3& up = Vec3::Up()) noexcept {
			Vec3 f = forward.Normalized();
			if (f == Vec3::Zero()) {
				return Identity();
			}

			Vec3 r = Vec3::Cross(up, f);
			if (r.LengthSquared() <= Math::EPSILON) {
				r = Vec3::Cross(Vec3::Forward(), f);
				if (r.LengthSquared() <= Math::EPSILON) {
					r = Vec3::Right();
				}
			}
			r.Normalize();
			Vec3 u = Vec3::Cross(f, r);
			return FromBasis(r, u, f);
		}

		// unsigned degrees
		[[nodiscard]] static f32 Angle(const Quat& a, const Quat& b) noexcept {
			f32 dot = Math::Min(Math::Abs(Dot(a, b)), 1.0f);
			if (dot > 1.0f - 1e-6f) {
				return 0.0f;
			}
			return 2.0f * std::acos(dot) * Math::RAD_TO_DEG;
		}

		// nlerp, t clamped
		[[nodiscard]] static Quat Lerp(const Quat& a, const Quat& b, f32 t) noexcept {
			return LerpUnclamped(a, b, Math::Clamp01(t));
		}

		[[nodiscard]] static Quat LerpUnclamped(const Quat& a, const Quat& b, f32 t) noexcept {
			Quat end = Dot(a, b) < 0.0f ? -b : b;
			return (a * (1.0f - t) + end * t).Normalized();
		}

		[[nodiscard]] static Quat Slerp(const Quat& a, const Quat& b, f32 t) noexcept {
			return SlerpUnclamped(a, b, Math::Clamp01(t));
		}

		[[nodiscard]] static Quat SlerpUnclamped(const Quat& a, const Quat& b, f32 t) noexcept {
			f32 dot = Dot(a, b);
			Quat end = b;
			if (dot < 0.0f) {
				end = -b;
				dot = -dot;
			}

			if (dot > 0.9995f) {
				return (a * (1.0f - t) + end * t).Normalized();
			}

			f32 theta = std::acos(Math::Clamp(dot, -1.0f, 1.0f));
			f32 sinTheta = std::sin(theta);
			f32 weightA = std::sin((1.0f - t) * theta) / sinTheta;
			f32 weightB = std::sin(t * theta) / sinTheta;
			return a * weightA + end * weightB;
		}

		[[nodiscard]] static bool Approximately(const Quat& a, const Quat& b, f32 epsilon = Math::EPSILON) noexcept {
			return Math::Abs(Dot(a, b)) >= 1.0f - epsilon;
		}

		[[nodiscard]] str ToString() const {
			return std::format("({}, {}, {}, {})", x, y, z, w);
		}
	};

	static_assert(sizeof(Quat) == 16 && alignof(Quat) == 16);
} //namespace Shalltear

template <>
struct std::formatter<Shalltear::Quat> : std::formatter<std::string> {
	auto format(const Shalltear::Quat& q, std::format_context& ctx) const {
		return std::formatter<std::string>::format(q.ToString(), ctx);
	}
};
