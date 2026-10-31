#pragma once

#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include <cassert>
#include <cmath>
#include <limits>

namespace Shalltear {

	// physical pxs
	struct RectI {
		Vec2I position;
		Vec2I size;

		constexpr RectI() noexcept : position(), size() {}
		constexpr RectI(Vec2I position, Vec2I size) noexcept : position(position), size(size) {}
		constexpr RectI(i32 x, i32 y, i32 width, i32 height) noexcept
			: position(x, y), size(width, height) {}

		[[nodiscard]] constexpr i32 Left() const noexcept { return position.x; }
		[[nodiscard]] constexpr i32 Top() const noexcept { return position.y; }
		[[nodiscard]] constexpr i32 Right() const noexcept { return position.x + size.x; }
		[[nodiscard]] constexpr i32 Bottom() const noexcept { return position.y + size.y; }
		[[nodiscard]] constexpr Vec2I Center() const noexcept { return position + size / 2; }

		[[nodiscard]] constexpr bool operator==(const RectI& rhs) const noexcept {
			return position == rhs.position && size == rhs.size;
		}
		[[nodiscard]] constexpr bool operator!=(const RectI& rhs) const noexcept { return !(*this == rhs); }

		// half open on right/bottom
		[[nodiscard]] constexpr bool Contains(Vec2I point) const noexcept {
			return point.x >= Left() && point.x < Right() &&
				   point.y >= Top() && point.y < Bottom();
		}

		[[nodiscard]] constexpr bool Intersects(const RectI& other) const noexcept {
			return Left() < other.Right() && Right() > other.Left() &&
				   Top() < other.Bottom() && Bottom() > other.Top();
		}
	};

	static_assert(sizeof(RectI) == 4 * sizeof(i32), "RectI must stay 4 contiguous i32s for the flat serialization path");

	struct Ray {
		Vec3 origin;
		Vec3 direction;

		constexpr Ray() noexcept : origin(), direction(Vec3::Forward()) {}
		Ray(const Vec3& origin, const Vec3& direction) noexcept
			: origin(origin), direction(direction.Normalized()) {}

		[[nodiscard]] constexpr Vec3 GetPoint(f32 distance) const noexcept {
			return origin + direction * distance;
		}
	};

	struct Plane {
		Vec3 normal;
		f32 distance;

		constexpr Plane() noexcept : normal(Vec3::Up()), distance(0.0f) {}
		constexpr Plane(const Vec3& normal, f32 distance) noexcept : normal(normal), distance(distance) {}

		[[nodiscard]] static Plane FromPointNormal(const Vec3& point, const Vec3& normal) noexcept {
			Vec3 n = normal.Normalized();
			return Plane(n, -Vec3::Dot(n, point));
		}

		[[nodiscard]] static Plane FromPoints(const Vec3& a, const Vec3& b, const Vec3& c) noexcept {
			Vec3 n = Vec3::Cross(b - a, c - a).Normalized();
			return Plane(n, -Vec3::Dot(n, a));
		}

		[[nodiscard]] Plane Normalized() const noexcept {
			f32 length = normal.Length();
			if (length <= Math::EPSILON) {
				return Plane();
			}
			f32 inverse = 1.0f / length;
			return Plane(normal * inverse, distance * inverse);
		}

		[[nodiscard]] constexpr f32 SignedDistance(const Vec3& point) const noexcept {
			return Vec3::Dot(normal, point) + distance;
		}

		[[nodiscard]] constexpr bool GetSide(const Vec3& point) const noexcept {
			return SignedDistance(point) > 0.0f;
		}

		[[nodiscard]] constexpr Vec3 ClosestPoint(const Vec3& point) const noexcept {
			return point - normal * SignedDistance(point);
		}

		constexpr bool Raycast(const Ray& ray, f32& enter) const noexcept {
			f32 alignment = Vec3::Dot(ray.direction, normal);
			if (Math::Abs(alignment) <= Math::EPSILON) {
				return false;
			}
			f32 t = -SignedDistance(ray.origin) / alignment;
			if (t < 0.0f) {
				return false;
			}
			enter = t;
			return true;
		}
	};

	struct Sphere {
		Vec3 center;
		f32 radius;

		constexpr Sphere() noexcept : center(), radius(0.0f) {}
		constexpr Sphere(const Vec3& center, f32 radius) noexcept : center(center), radius(radius) {}

		[[nodiscard]] constexpr bool Contains(const Vec3& point) const noexcept {
			return Vec3::DistanceSquared(center, point) <= radius * radius;
		}

		[[nodiscard]] constexpr bool Intersects(const Sphere& other) const noexcept {
			f32 radiusSum = radius + other.radius;
			return Vec3::DistanceSquared(center, other.center) <= radiusSum * radiusSum;
		}

		bool Raycast(const Ray& ray, f32& enter) const noexcept {
			Vec3 toCenter = center - ray.origin;
			f32 projection = Vec3::Dot(toCenter, ray.direction);
			f32 discriminant = radius * radius - (toCenter.LengthSquared() - projection * projection);
			if (discriminant < 0.0f) {
				return false;
			}
			f32 offset = Math::Sqrt(discriminant);
			f32 t = projection - offset;
			if (t < 0.0f) {
				t = projection + offset;
			}
			if (t < 0.0f) {
				return false;
			}
			enter = t;
			return true;
		}
	};

	struct AABB {
		Vec3 min;
		Vec3 max;

		constexpr AABB() noexcept : min(), max() {}
		constexpr AABB(const Vec3& min, const Vec3& max) noexcept : min(min), max(max) {}

		[[nodiscard]] static constexpr AABB Empty() noexcept {
			constexpr f32 MAX = std::numeric_limits<f32>::max();
			return AABB(Vec3(MAX, MAX, MAX), Vec3(-MAX, -MAX, -MAX));
		}

		[[nodiscard]] static constexpr AABB FromCenterExtents(const Vec3& center, const Vec3& extents) noexcept {
			return AABB(center - extents, center + extents);
		}

		[[nodiscard]] constexpr Vec3 Center() const noexcept { return (min + max) * 0.5f; }
		[[nodiscard]] constexpr Vec3 Size() const noexcept { return max - min; }
		[[nodiscard]] constexpr Vec3 Extents() const noexcept { return (max - min) * 0.5f; }

		[[nodiscard]] constexpr bool IsValid() const noexcept {
			return min.x <= max.x && min.y <= max.y && min.z <= max.z;
		}

		constexpr void Encapsulate(const Vec3& point) noexcept {
			min = Vec3::Min(min, point);
			max = Vec3::Max(max, point);
		}

		constexpr void Encapsulate(const AABB& other) noexcept {
			min = Vec3::Min(min, other.min);
			max = Vec3::Max(max, other.max);
		}

		constexpr void Expand(f32 amount) noexcept {
			Vec3 delta(amount, amount, amount);
			min -= delta;
			max += delta;
		}

		[[nodiscard]] constexpr bool Contains(const Vec3& point) const noexcept {
			return point.x >= min.x && point.x <= max.x &&
				   point.y >= min.y && point.y <= max.y &&
				   point.z >= min.z && point.z <= max.z;
		}

		[[nodiscard]] constexpr bool Contains(const AABB& other) const noexcept {
			return Contains(other.min) && Contains(other.max);
		}

		[[nodiscard]] constexpr bool Intersects(const AABB& other) const noexcept {
			return min.x <= other.max.x && max.x >= other.min.x &&
				   min.y <= other.max.y && max.y >= other.min.y &&
				   min.z <= other.max.z && max.z >= other.min.z;
		}

		[[nodiscard]] constexpr bool Intersects(const Sphere& sphere) const noexcept {
			return Vec3::DistanceSquared(ClosestPoint(sphere.center), sphere.center) <= sphere.radius * sphere.radius;
		}

		[[nodiscard]] constexpr Vec3 ClosestPoint(const Vec3& point) const noexcept {
			return Vec3::Max(min, Vec3::Min(point, max));
		}

		constexpr bool Raycast(const Ray& ray, f32& enter) const noexcept {
			f32 tMin = 0.0f;
			f32 tMax = std::numeric_limits<f32>::max();
			for (size_t axis = 0; axis < 3; ++axis) {
				f32 originComponent = ray.origin[axis];
				f32 directionComponent = ray.direction[axis];
				if (Math::Abs(directionComponent) <= Math::EPSILON) {
					if (originComponent < min[axis] || originComponent > max[axis]) {
						return false;
					}
					continue;
				}
				f32 inverse = 1.0f / directionComponent;
				f32 t0 = (min[axis] - originComponent) * inverse;
				f32 t1 = (max[axis] - originComponent) * inverse;
				if (t0 > t1) {
					f32 swap = t0;
					t0 = t1;
					t1 = swap;
				}
				tMin = Math::Max(tMin, t0);
				tMax = Math::Min(tMax, t1);
				if (tMin > tMax) {
					return false;
				}
			}
			enter = tMin;
			return true;
		}

		[[nodiscard]] constexpr AABB Transformed(const Mat4& m) const noexcept {
			Vec3 center = m.TransformPoint(Center());
			Vec3 extents = Extents();
			Vec3 newExtents(
				Math::Abs(m(0, 0)) * extents.x + Math::Abs(m(0, 1)) * extents.y + Math::Abs(m(0, 2)) * extents.z,
				Math::Abs(m(1, 0)) * extents.x + Math::Abs(m(1, 1)) * extents.y + Math::Abs(m(1, 2)) * extents.z,
				Math::Abs(m(2, 0)) * extents.x + Math::Abs(m(2, 1)) * extents.y + Math::Abs(m(2, 2)) * extents.z);
			return FromCenterExtents(center, newExtents);
		}
	};

	struct Frustum {
		enum PlaneIndex : u32 {
			Left = 0,
			Right,
			Bottom,
			Top,
			NearZ,
			FarZ,
			Count,
		};

		Plane planes[Count];

		constexpr Frustum() noexcept : planes{} {}

		[[nodiscard]] static Frustum FromMatrix(const Mat4& viewProjection) noexcept {
			Vec4 row0 = viewProjection.GetRow(0);
			Vec4 row1 = viewProjection.GetRow(1);
			Vec4 row2 = viewProjection.GetRow(2);
			Vec4 row3 = viewProjection.GetRow(3);

			auto toPlane = [](const Vec4& v) noexcept {
				return Plane(v.XYZ(), v.w).Normalized();
			};

			Frustum frustum;
			frustum.planes[Left] = toPlane(row3 + row0);
			frustum.planes[Right] = toPlane(row3 - row0);
			frustum.planes[Bottom] = toPlane(row3 + row1);
			frustum.planes[Top] = toPlane(row3 - row1);
			frustum.planes[NearZ] = toPlane(row2);
			frustum.planes[FarZ] = toPlane(row3 - row2);
			return frustum;
		}

		[[nodiscard]] constexpr bool Contains(const Vec3& point) const noexcept {
			for (const Plane& plane : planes) {
				if (plane.SignedDistance(point) < 0.0f) {
					return false;
				}
			}
			return true;
		}

		// conservative (may pass a sphere past a corner)
		[[nodiscard]] constexpr bool Intersects(const Sphere& sphere) const noexcept {
			for (const Plane& plane : planes) {
				if (plane.SignedDistance(sphere.center) < -sphere.radius) {
					return false;
				}
			}
			return true;
		}

		[[nodiscard]] constexpr bool Intersects(const AABB& box) const noexcept {
			for (const Plane& plane : planes) {
				Vec3 positive(
					plane.normal.x >= 0.0f ? box.max.x : box.min.x,
					plane.normal.y >= 0.0f ? box.max.y : box.min.y,
					plane.normal.z >= 0.0f ? box.max.z : box.min.z);
				if (plane.SignedDistance(positive) < 0.0f) {
					return false;
				}
			}
			return true;
		}
	};

	inline f32 VolumeContribution(bool isGlobal, const Mat4& localToWorld, const Vec3& size, f32 blendDistance, f32 weight,
								  const Vec3& point) {
		weight = Math::Clamp(weight, 0.0f, 1.0f);
		if (weight <= 0.0f) {
			return 0.0f;
		}
		if (isGlobal) {
			return weight;
		}
		const Vec3 local = localToWorld.Inverse().TransformPoint(point);
		const Vec3 half(std::fabs(size.x) * 0.5f, std::fabs(size.y) * 0.5f, std::fabs(size.z) * 0.5f);
		const Vec3 closest(Math::Clamp(local.x, -half.x, half.x), Math::Clamp(local.y, -half.y, half.y), Math::Clamp(local.z, -half.z, half.z));
		const f32 distance = (localToWorld.TransformPoint(local) - localToWorld.TransformPoint(closest)).Length();
		if (distance <= 0.0f) {
			return weight;
		}
		if (blendDistance <= 0.0f || distance >= blendDistance) {
			return 0.0f;
		}
		const f32 fraction = distance / blendDistance;
		return weight * (1.0f - fraction * fraction);
	}

} //namespace Shalltear
