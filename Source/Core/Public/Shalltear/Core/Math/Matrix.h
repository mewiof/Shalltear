#pragma once

// column major data[col * N + row], projection * view * model

#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include <cassert>
#include <cmath>
#include <format>

namespace Shalltear {

	struct Mat3 {
		f32 data[9];

		constexpr Mat3() noexcept : data{} {}

		[[nodiscard]] static constexpr Mat3 Identity() noexcept {
			Mat3 m;
			m.data[0] = 1.0f;
			m.data[4] = 1.0f;
			m.data[8] = 1.0f;
			return m;
		}

		[[nodiscard]] static constexpr Mat3 FromColumns(const Vec3& c0, const Vec3& c1, const Vec3& c2) noexcept {
			Mat3 m;
			m.data[0] = c0.x;
			m.data[1] = c0.y;
			m.data[2] = c0.z;
			m.data[3] = c1.x;
			m.data[4] = c1.y;
			m.data[5] = c1.z;
			m.data[6] = c2.x;
			m.data[7] = c2.y;
			m.data[8] = c2.z;
			return m;
		}

		[[nodiscard]] static constexpr Mat3 FromRows(const Vec3& r0, const Vec3& r1, const Vec3& r2) noexcept {
			Mat3 m;
			m.data[0] = r0.x;
			m.data[1] = r1.x;
			m.data[2] = r2.x;
			m.data[3] = r0.y;
			m.data[4] = r1.y;
			m.data[5] = r2.y;
			m.data[6] = r0.z;
			m.data[7] = r1.z;
			m.data[8] = r2.z;
			return m;
		}

		[[nodiscard]] static constexpr Mat3 FromQuat(const Quat& q) noexcept {
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
			return FromColumns(
				Vec3(1.0f - (yY + zZ), xY + wZ, xZ - wY),
				Vec3(xY - wZ, 1.0f - (xX + zZ), yZ + wX),
				Vec3(xZ + wY, yZ - wX, 1.0f - (xX + yY)));
		}

		[[nodiscard]] static constexpr Mat3 FromScale(const Vec3& s) noexcept {
			Mat3 m;
			m.data[0] = s.x;
			m.data[4] = s.y;
			m.data[8] = s.z;
			return m;
		}

		[[nodiscard]] constexpr f32& operator()(size_t row, size_t col) noexcept {
			assert(row < 3 && col < 3 && "Mat3 idx out of bounds");
			return data[col * 3 + row];
		}
		[[nodiscard]] constexpr const f32& operator()(size_t row, size_t col) const noexcept {
			assert(row < 3 && col < 3 && "Mat3 idx out of bounds");
			return data[col * 3 + row];
		}
		[[nodiscard]] constexpr f32& operator[](size_t index) noexcept {
			assert(index < 9 && "Mat3 idx out of bounds");
			return data[index];
		}
		[[nodiscard]] constexpr const f32& operator[](size_t index) const noexcept {
			assert(index < 9 && "Mat3 idx out of bounds");
			return data[index];
		}

		[[nodiscard]] constexpr Vec3 GetColumn(size_t col) const noexcept {
			assert(col < 3 && "Mat3 column out of bounds");
			return Vec3(data[col * 3 + 0], data[col * 3 + 1], data[col * 3 + 2]);
		}
		constexpr void SetColumn(size_t col, const Vec3& v) noexcept {
			assert(col < 3 && "Mat3 column out of bounds");
			data[col * 3 + 0] = v.x;
			data[col * 3 + 1] = v.y;
			data[col * 3 + 2] = v.z;
		}
		[[nodiscard]] constexpr Vec3 GetRow(size_t row) const noexcept {
			assert(row < 3 && "Mat3 row out of bounds");
			return Vec3(data[0 * 3 + row], data[1 * 3 + row], data[2 * 3 + row]);
		}
		constexpr void SetRow(size_t row, const Vec3& v) noexcept {
			assert(row < 3 && "Mat3 row out of bounds");
			data[0 * 3 + row] = v.x;
			data[1 * 3 + row] = v.y;
			data[2 * 3 + row] = v.z;
		}

		[[nodiscard]] constexpr bool operator==(const Mat3& rhs) const noexcept {
			for (size_t i = 0; i < 9; ++i) {
				if (data[i] != rhs.data[i]) {
					return false;
				}
			}
			return true;
		}
		[[nodiscard]] constexpr bool operator!=(const Mat3& rhs) const noexcept { return !(*this == rhs); }

		[[nodiscard]] static constexpr bool Approximately(const Mat3& a, const Mat3& b, f32 epsilon = Math::EPSILON) noexcept {
			for (size_t i = 0; i < 9; ++i) {
				if (!Math::Approximately(a.data[i], b.data[i], epsilon)) {
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] friend constexpr Mat3 operator*(const Mat3& lhs, const Mat3& rhs) noexcept {
			Mat3 result;
			for (size_t c = 0; c < 3; ++c) {
				for (size_t r = 0; r < 3; ++r) {
					result(r, c) = lhs(r, 0) * rhs(0, c) + lhs(r, 1) * rhs(1, c) + lhs(r, 2) * rhs(2, c);
				}
			}
			return result;
		}
		constexpr Mat3& operator*=(const Mat3& rhs) noexcept {
			*this = *this * rhs;
			return *this;
		}

		[[nodiscard]] friend constexpr Vec3 operator*(const Mat3& m, const Vec3& v) noexcept {
			return Vec3(
				m(0, 0) * v.x + m(0, 1) * v.y + m(0, 2) * v.z,
				m(1, 0) * v.x + m(1, 1) * v.y + m(1, 2) * v.z,
				m(2, 0) * v.x + m(2, 1) * v.y + m(2, 2) * v.z);
		}

		[[nodiscard]] constexpr Mat3 Transposed() const noexcept {
			Mat3 result;
			for (size_t c = 0; c < 3; ++c) {
				for (size_t r = 0; r < 3; ++r) {
					result(r, c) = (*this)(c, r);
				}
			}
			return result;
		}

		[[nodiscard]] constexpr f32 Determinant() const noexcept {
			const Mat3& m = *this;
			return m(0, 0) * (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1)) -
				   m(0, 1) * (m(1, 0) * m(2, 2) - m(1, 2) * m(2, 0)) +
				   m(0, 2) * (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0));
		}

		constexpr bool TryInvert(Mat3& out) const noexcept {
			f32 det = Determinant();
			if (Math::Abs(det) <= 1e-12f) {
				out = Identity();
				return false;
			}
			const Mat3& m = *this;
			f32 invDet = 1.0f / det;
			Mat3 result;
			result(0, 0) = (m(1, 1) * m(2, 2) - m(1, 2) * m(2, 1)) * invDet;
			result(0, 1) = (m(0, 2) * m(2, 1) - m(0, 1) * m(2, 2)) * invDet;
			result(0, 2) = (m(0, 1) * m(1, 2) - m(0, 2) * m(1, 1)) * invDet;
			result(1, 0) = (m(1, 2) * m(2, 0) - m(1, 0) * m(2, 2)) * invDet;
			result(1, 1) = (m(0, 0) * m(2, 2) - m(0, 2) * m(2, 0)) * invDet;
			result(1, 2) = (m(0, 2) * m(1, 0) - m(0, 0) * m(1, 2)) * invDet;
			result(2, 0) = (m(1, 0) * m(2, 1) - m(1, 1) * m(2, 0)) * invDet;
			result(2, 1) = (m(0, 1) * m(2, 0) - m(0, 0) * m(2, 1)) * invDet;
			result(2, 2) = (m(0, 0) * m(1, 1) - m(0, 1) * m(1, 0)) * invDet;
			out = result;
			return true;
		}

		[[nodiscard]] constexpr Mat3 Inverse() const noexcept {
			Mat3 result = Identity();
			[[maybe_unused]] bool ok = TryInvert(result);
			assert(ok && "Mat3::Inverse called on a singular matrix");
			return result;
		}

		[[nodiscard]] str ToString() const {
			return std::format(
				"({}, {}, {},\n {}, {}, {},\n {}, {}, {})",
				(*this)(0, 0), (*this)(0, 1), (*this)(0, 2),
				(*this)(1, 0), (*this)(1, 1), (*this)(1, 2),
				(*this)(2, 0), (*this)(2, 1), (*this)(2, 2));
		}
	};

	struct alignas(16) Mat4 {
		f32 data[16];

		constexpr Mat4() noexcept : data{} {}

		[[nodiscard]] static constexpr Mat4 Identity() noexcept {
			Mat4 m;
			m.data[0] = 1.0f;
			m.data[5] = 1.0f;
			m.data[10] = 1.0f;
			m.data[15] = 1.0f;
			return m;
		}

		[[nodiscard]] static constexpr Mat4 FromColumns(const Vec4& c0, const Vec4& c1, const Vec4& c2, const Vec4& c3) noexcept {
			Mat4 m;
			for (size_t r = 0; r < 4; ++r) {
				m.data[0 * 4 + r] = c0[r];
				m.data[1 * 4 + r] = c1[r];
				m.data[2 * 4 + r] = c2[r];
				m.data[3 * 4 + r] = c3[r];
			}
			return m;
		}

		[[nodiscard]] static constexpr Mat4 FromRows(const Vec4& r0, const Vec4& r1, const Vec4& r2, const Vec4& r3) noexcept {
			Mat4 m;
			for (size_t c = 0; c < 4; ++c) {
				m.data[c * 4 + 0] = r0[c];
				m.data[c * 4 + 1] = r1[c];
				m.data[c * 4 + 2] = r2[c];
				m.data[c * 4 + 3] = r3[c];
			}
			return m;
		}

		[[nodiscard]] static constexpr Mat4 FromMat3(const Mat3& m3) noexcept {
			Mat4 m = Identity();
			for (size_t c = 0; c < 3; ++c) {
				for (size_t r = 0; r < 3; ++r) {
					m(r, c) = m3(r, c);
				}
			}
			return m;
		}

		[[nodiscard]] constexpr f32& operator()(size_t row, size_t col) noexcept {
			assert(row < 4 && col < 4 && "Mat4 idx out of bounds");
			return data[col * 4 + row];
		}
		[[nodiscard]] constexpr const f32& operator()(size_t row, size_t col) const noexcept {
			assert(row < 4 && col < 4 && "Mat4 idx out of bounds");
			return data[col * 4 + row];
		}
		[[nodiscard]] constexpr f32& operator[](size_t index) noexcept {
			assert(index < 16 && "Mat4 idx out of bounds");
			return data[index];
		}
		[[nodiscard]] constexpr const f32& operator[](size_t index) const noexcept {
			assert(index < 16 && "Mat4 idx out of bounds");
			return data[index];
		}

		[[nodiscard]] constexpr Vec4 GetColumn(size_t col) const noexcept {
			assert(col < 4 && "Mat4 column out of bounds");
			return Vec4(data[col * 4 + 0], data[col * 4 + 1], data[col * 4 + 2], data[col * 4 + 3]);
		}
		constexpr void SetColumn(size_t col, const Vec4& v) noexcept {
			assert(col < 4 && "Mat4 column out of bounds");
			for (size_t r = 0; r < 4; ++r) {
				data[col * 4 + r] = v[r];
			}
		}
		[[nodiscard]] constexpr Vec4 GetRow(size_t row) const noexcept {
			assert(row < 4 && "Mat4 row out of bounds");
			return Vec4(data[0 * 4 + row], data[1 * 4 + row], data[2 * 4 + row], data[3 * 4 + row]);
		}
		constexpr void SetRow(size_t row, const Vec4& v) noexcept {
			assert(row < 4 && "Mat4 row out of bounds");
			for (size_t c = 0; c < 4; ++c) {
				data[c * 4 + row] = v[c];
			}
		}

		[[nodiscard]] constexpr Mat3 ToMat3() const noexcept {
			Mat3 m;
			for (size_t c = 0; c < 3; ++c) {
				for (size_t r = 0; r < 3; ++r) {
					m(r, c) = (*this)(r, c);
				}
			}
			return m;
		}

		[[nodiscard]] constexpr bool operator==(const Mat4& rhs) const noexcept {
			for (size_t i = 0; i < 16; ++i) {
				if (data[i] != rhs.data[i]) {
					return false;
				}
			}
			return true;
		}
		[[nodiscard]] constexpr bool operator!=(const Mat4& rhs) const noexcept { return !(*this == rhs); }

		[[nodiscard]] static constexpr bool Approximately(const Mat4& a, const Mat4& b, f32 epsilon = Math::EPSILON) noexcept {
			for (size_t i = 0; i < 16; ++i) {
				if (!Math::Approximately(a.data[i], b.data[i], epsilon)) {
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] friend constexpr Mat4 operator*(const Mat4& lhs, const Mat4& rhs) noexcept {
			Mat4 result;
			for (size_t c = 0; c < 4; ++c) {
				for (size_t r = 0; r < 4; ++r) {
					result(r, c) = lhs(r, 0) * rhs(0, c) + lhs(r, 1) * rhs(1, c) + lhs(r, 2) * rhs(2, c) + lhs(r, 3) * rhs(3, c);
				}
			}
			return result;
		}
		constexpr Mat4& operator*=(const Mat4& rhs) noexcept {
			*this = *this * rhs;
			return *this;
		}

		[[nodiscard]] friend constexpr Vec4 operator*(const Mat4& m, const Vec4& v) noexcept {
			return Vec4(
				m(0, 0) * v.x + m(0, 1) * v.y + m(0, 2) * v.z + m(0, 3) * v.w,
				m(1, 0) * v.x + m(1, 1) * v.y + m(1, 2) * v.z + m(1, 3) * v.w,
				m(2, 0) * v.x + m(2, 1) * v.y + m(2, 2) * v.z + m(2, 3) * v.w,
				m(3, 0) * v.x + m(3, 1) * v.y + m(3, 2) * v.z + m(3, 3) * v.w);
		}

		[[nodiscard]] constexpr Vec3 TransformPoint(const Vec3& p) const noexcept {
			const Mat4& m = *this;
			return Vec3(
				m(0, 0) * p.x + m(0, 1) * p.y + m(0, 2) * p.z + m(0, 3),
				m(1, 0) * p.x + m(1, 1) * p.y + m(1, 2) * p.z + m(1, 3),
				m(2, 0) * p.x + m(2, 1) * p.y + m(2, 2) * p.z + m(2, 3));
		}

		[[nodiscard]] constexpr Vec3 TransformVector(const Vec3& v) const noexcept {
			const Mat4& m = *this;
			return Vec3(
				m(0, 0) * v.x + m(0, 1) * v.y + m(0, 2) * v.z,
				m(1, 0) * v.x + m(1, 1) * v.y + m(1, 2) * v.z,
				m(2, 0) * v.x + m(2, 1) * v.y + m(2, 2) * v.z);
		}

		[[nodiscard]] constexpr Vec3 ProjectPoint(const Vec3& p) const noexcept {
			Vec4 v = *this * Vec4(p, 1.0f);
			if (Math::Abs(v.w) <= Math::EPSILON) {
				return Vec3::Zero();
			}
			return v.XYZ() / v.w;
		}

		[[nodiscard]] constexpr Mat4 Transposed() const noexcept {
			Mat4 result;
			for (size_t c = 0; c < 4; ++c) {
				for (size_t r = 0; r < 4; ++r) {
					result(r, c) = (*this)(c, r);
				}
			}
			return result;
		}

		[[nodiscard]] constexpr f32 Determinant() const noexcept {
			const f32* m = data;
			f32 s0 = m[10] * m[15] - m[14] * m[11];
			f32 s1 = m[6] * m[15] - m[14] * m[7];
			f32 s2 = m[6] * m[11] - m[10] * m[7];
			f32 s3 = m[2] * m[15] - m[14] * m[3];
			f32 s4 = m[2] * m[11] - m[10] * m[3];
			f32 s5 = m[2] * m[7] - m[6] * m[3];
			return m[0] * (m[5] * s0 - m[9] * s1 + m[13] * s2) -
				   m[4] * (m[1] * s0 - m[9] * s3 + m[13] * s4) +
				   m[8] * (m[1] * s1 - m[5] * s3 + m[13] * s5) -
				   m[12] * (m[1] * s2 - m[5] * s4 + m[9] * s5);
		}

		constexpr bool TryInvert(Mat4& out) const noexcept {
			const f32* m = data;
			f32 inv[16];

			inv[0] = m[5] * m[10] * m[15] - m[5] * m[11] * m[14] - m[9] * m[6] * m[15] + m[9] * m[7] * m[14] + m[13] * m[6] * m[11] - m[13] * m[7] * m[10];
			inv[4] = -m[4] * m[10] * m[15] + m[4] * m[11] * m[14] + m[8] * m[6] * m[15] - m[8] * m[7] * m[14] - m[12] * m[6] * m[11] + m[12] * m[7] * m[10];
			inv[8] = m[4] * m[9] * m[15] - m[4] * m[11] * m[13] - m[8] * m[5] * m[15] + m[8] * m[7] * m[13] + m[12] * m[5] * m[11] - m[12] * m[7] * m[9];
			inv[12] = -m[4] * m[9] * m[14] + m[4] * m[10] * m[13] + m[8] * m[5] * m[14] - m[8] * m[6] * m[13] - m[12] * m[5] * m[10] + m[12] * m[6] * m[9];
			inv[1] = -m[1] * m[10] * m[15] + m[1] * m[11] * m[14] + m[9] * m[2] * m[15] - m[9] * m[3] * m[14] - m[13] * m[2] * m[11] + m[13] * m[3] * m[10];
			inv[5] = m[0] * m[10] * m[15] - m[0] * m[11] * m[14] - m[8] * m[2] * m[15] + m[8] * m[3] * m[14] + m[12] * m[2] * m[11] - m[12] * m[3] * m[10];
			inv[9] = -m[0] * m[9] * m[15] + m[0] * m[11] * m[13] + m[8] * m[1] * m[15] - m[8] * m[3] * m[13] - m[12] * m[1] * m[11] + m[12] * m[3] * m[9];
			inv[13] = m[0] * m[9] * m[14] - m[0] * m[10] * m[13] - m[8] * m[1] * m[14] + m[8] * m[2] * m[13] + m[12] * m[1] * m[10] - m[12] * m[2] * m[9];
			inv[2] = m[1] * m[6] * m[15] - m[1] * m[7] * m[14] - m[5] * m[2] * m[15] + m[5] * m[3] * m[14] + m[13] * m[2] * m[7] - m[13] * m[3] * m[6];
			inv[6] = -m[0] * m[6] * m[15] + m[0] * m[7] * m[14] + m[4] * m[2] * m[15] - m[4] * m[3] * m[14] - m[12] * m[2] * m[7] + m[12] * m[3] * m[6];
			inv[10] = m[0] * m[5] * m[15] - m[0] * m[7] * m[13] - m[4] * m[1] * m[15] + m[4] * m[3] * m[13] + m[12] * m[1] * m[7] - m[12] * m[3] * m[5];
			inv[14] = -m[0] * m[5] * m[14] + m[0] * m[6] * m[13] + m[4] * m[1] * m[14] - m[4] * m[2] * m[13] - m[12] * m[1] * m[6] + m[12] * m[2] * m[5];
			inv[3] = -m[1] * m[6] * m[11] + m[1] * m[7] * m[10] + m[5] * m[2] * m[11] - m[5] * m[3] * m[10] - m[9] * m[2] * m[7] + m[9] * m[3] * m[6];
			inv[7] = m[0] * m[6] * m[11] - m[0] * m[7] * m[10] - m[4] * m[2] * m[11] + m[4] * m[3] * m[10] + m[8] * m[2] * m[7] - m[8] * m[3] * m[6];
			inv[11] = -m[0] * m[5] * m[11] + m[0] * m[7] * m[9] + m[4] * m[1] * m[11] - m[4] * m[3] * m[9] - m[8] * m[1] * m[7] + m[8] * m[3] * m[5];
			inv[15] = m[0] * m[5] * m[10] - m[0] * m[6] * m[9] - m[4] * m[1] * m[10] + m[4] * m[2] * m[9] + m[8] * m[1] * m[6] - m[8] * m[2] * m[5];

			f32 det = m[0] * inv[0] + m[1] * inv[4] + m[2] * inv[8] + m[3] * inv[12];
			if (Math::Abs(det) <= 1e-12f) {
				out = Identity();
				return false;
			}

			f32 invDet = 1.0f / det;
			for (size_t i = 0; i < 16; ++i) {
				out.data[i] = inv[i] * invDet;
			}
			return true;
		}

		[[nodiscard]] constexpr Mat4 Inverse() const noexcept {
			Mat4 result = Identity();
			[[maybe_unused]] bool ok = TryInvert(result);
			assert(ok && "Mat4::Inverse called on a singular matrix");
			return result;
		}

		[[nodiscard]] static constexpr Mat4 Translation(const Vec3& t) noexcept {
			Mat4 m = Identity();
			m(0, 3) = t.x;
			m(1, 3) = t.y;
			m(2, 3) = t.z;
			return m;
		}

		[[nodiscard]] static constexpr Mat4 FromQuat(const Quat& q) noexcept {
			return FromMat3(Mat3::FromQuat(q));
		}

		[[nodiscard]] static constexpr Mat4 Scale(const Vec3& s) noexcept {
			Mat4 m = Identity();
			m(0, 0) = s.x;
			m(1, 1) = s.y;
			m(2, 2) = s.z;
			return m;
		}

		[[nodiscard]] static constexpr Mat4 TRS(const Vec3& translation, const Quat& rotation, const Vec3& scale) noexcept {
			Mat3 r = Mat3::FromQuat(rotation);
			Mat4 m;
			m.SetColumn(0, Vec4(r.GetColumn(0) * scale.x, 0.0f));
			m.SetColumn(1, Vec4(r.GetColumn(1) * scale.y, 0.0f));
			m.SetColumn(2, Vec4(r.GetColumn(2) * scale.z, 0.0f));
			m.SetColumn(3, Vec4(translation, 1.0f));
			return m;
		}

		[[nodiscard]] static bool DecomposeTRS(const Mat4& m, Vec3& outTranslation, Quat& outRotation, Vec3& outScale) noexcept {
			outTranslation = Vec3(m(0, 3), m(1, 3), m(2, 3));

			const Mat3 basis = m.ToMat3();
			Vec3 x = basis.GetColumn(0);
			Vec3 y = basis.GetColumn(1);
			Vec3 z = basis.GetColumn(2);

			outScale = Vec3(x.Length(), y.Length(), z.Length());
			constexpr f32 DEGENERATE = 1e-6f;
			if (outScale.x < DEGENERATE || outScale.y < DEGENERATE || outScale.z < DEGENERATE) {
				outRotation = Quat::Identity();
				return false;
			}
			x = x / outScale.x;
			y = y / outScale.y;
			z = z / outScale.z;

			if (Vec3::Dot(Vec3::Cross(x, y), z) < 0.0f) {
				outScale.x = -outScale.x;
				x = -x;
			}

			constexpr f32 SHEAR_TOLERANCE = 1e-3f;
			const bool sheared = Math::Abs(Vec3::Dot(x, y)) > SHEAR_TOLERANCE ||
								 Math::Abs(Vec3::Dot(x, z)) > SHEAR_TOLERANCE ||
								 Math::Abs(Vec3::Dot(y, z)) > SHEAR_TOLERANCE;

			outRotation = Quat::FromBasis(x, y, z).Normalized();
			return !sheared;
		}

		[[nodiscard]] static Mat4 LookAt(const Vec3& eye, const Vec3& target, const Vec3& up = Vec3::Up()) noexcept {
			Vec3 f = (target - eye).Normalized();
			if (f == Vec3::Zero()) {
				return Identity();
			}
			Vec3 r = Vec3::Cross(up, f).Normalized();
			if (r == Vec3::Zero()) {
				r = Vec3::Cross(Vec3::Forward(), f).Normalized();
				if (r == Vec3::Zero()) {
					r = Vec3::Right();
				}
			}
			Vec3 u = Vec3::Cross(f, r);
			return FromRows(
				Vec4(r, -Vec3::Dot(r, eye)),
				Vec4(u, -Vec3::Dot(u, eye)),
				Vec4(f, -Vec3::Dot(f, eye)),
				Vec4(0.0f, 0.0f, 0.0f, 1.0f));
		}

		// vertical FOV in degrees, depth in [0, 1]
		[[nodiscard]] static Mat4 Perspective(f32 fovYDegrees, f32 aspect, f32 zNear, f32 zFar) noexcept {
			assert(zNear > 0.0f && zFar > zNear && aspect > 0.0f && "invalid perspective parameters");
			f32 half = fovYDegrees * Math::DEG_TO_RAD * 0.5f;
			f32 cotangent = std::cos(half) / std::sin(half);
			f32 rangeInv = 1.0f / (zFar - zNear);

			Mat4 m;
			m(0, 0) = cotangent / aspect;
			m(1, 1) = cotangent;
			m(2, 2) = zFar * rangeInv;
			m(2, 3) = -(zNear * zFar) * rangeInv;
			m(3, 2) = 1.0f;
			return m;
		}

		[[nodiscard]] static Mat4 PerspectiveReverseZ(f32 fovYDegrees, f32 aspect, f32 zNear, f32 zFar) noexcept {
			assert(zNear > 0.0f && zFar > zNear && aspect > 0.0f && "invalid perspective parameters");
			f32 half = fovYDegrees * Math::DEG_TO_RAD * 0.5f;
			f32 cotangent = std::cos(half) / std::sin(half);
			f32 rangeInv = 1.0f / (zFar - zNear);

			Mat4 m;
			m(0, 0) = cotangent / aspect;
			m(1, 1) = cotangent;
			m(2, 2) = -zNear * rangeInv;
			m(2, 3) = (zNear * zFar) * rangeInv;
			m(3, 2) = 1.0f;
			return m;
		}

		[[nodiscard]] static constexpr Mat4 Orthographic(f32 left, f32 right, f32 bottom, f32 top, f32 zNear, f32 zFar) noexcept {
			assert(right != left && top != bottom && zFar != zNear && "invalid orthographic parameters");
			Mat4 m = Identity();
			m(0, 0) = 2.0f / (right - left);
			m(0, 3) = -(right + left) / (right - left);
			m(1, 1) = 2.0f / (top - bottom);
			m(1, 3) = -(top + bottom) / (top - bottom);
			m(2, 2) = 1.0f / (zFar - zNear);
			m(2, 3) = -zNear / (zFar - zNear);
			return m;
		}

		[[nodiscard]] static constexpr Mat4 OrthographicReverseZ(f32 left, f32 right, f32 bottom, f32 top, f32 zNear, f32 zFar) noexcept {
			assert(right != left && top != bottom && zFar != zNear && "invalid orthographic parameters");
			Mat4 m = Identity();
			m(0, 0) = 2.0f / (right - left);
			m(0, 3) = -(right + left) / (right - left);
			m(1, 1) = 2.0f / (top - bottom);
			m(1, 3) = -(top + bottom) / (top - bottom);
			m(2, 2) = -1.0f / (zFar - zNear);
			m(2, 3) = zFar / (zFar - zNear);
			return m;
		}

		[[nodiscard]] str ToString() const {
			const Mat4& m = *this;
			return std::format(
				"({}, {}, {}, {},\n {}, {}, {}, {},\n {}, {}, {}, {},\n {}, {}, {}, {})",
				m(0, 0), m(0, 1), m(0, 2), m(0, 3),
				m(1, 0), m(1, 1), m(1, 2), m(1, 3),
				m(2, 0), m(2, 1), m(2, 2), m(2, 3),
				m(3, 0), m(3, 1), m(3, 2), m(3, 3));
		}
	};

	static_assert(sizeof(Mat3) == 36);
	static_assert(sizeof(Mat4) == 64 && alignof(Mat4) == 16);
} //namespace Shalltear
