#include "EffectNoise.h"

#include <algorithm>
#include <bit>
#include <cmath>

namespace Shalltear::VisualEffectsImpl::EffectNoises {

	namespace {

		struct F2 {
			f32 x = 0.0f;
			f32 y = 0.0f;
		};
		struct F3 {
			f32 x = 0.0f;
			f32 y = 0.0f;
			f32 z = 0.0f;
		};
		struct F4 {
			f32 x = 0.0f;
			f32 y = 0.0f;
			f32 z = 0.0f;
			f32 w = 0.0f;
		};

		F2 operator+(F2 a, F2 b) { return {a.x + b.x, a.y + b.y}; }
		F2 operator-(F2 a, F2 b) { return {a.x - b.x, a.y - b.y}; }
		F2 operator*(F2 a, F2 b) { return {a.x * b.x, a.y * b.y}; }
		F2 operator*(F2 a, f32 s) { return {a.x * s, a.y * s}; }
		F2 operator*(f32 s, F2 a) { return {s * a.x, s * a.y}; }
		F3 operator+(F3 a, F3 b) { return {a.x + b.x, a.y + b.y, a.z + b.z}; }
		F3 operator-(F3 a, F3 b) { return {a.x - b.x, a.y - b.y, a.z - b.z}; }
		F3 operator*(F3 a, F3 b) { return {a.x * b.x, a.y * b.y, a.z * b.z}; }
		F3 operator*(F3 a, f32 s) { return {a.x * s, a.y * s, a.z * s}; }
		F3 operator*(f32 s, F3 a) { return {s * a.x, s * a.y, s * a.z}; }
		F4 operator+(F4 a, F4 b) { return {a.x + b.x, a.y + b.y, a.z + b.z, a.w + b.w}; }
		F4 operator-(F4 a, F4 b) { return {a.x - b.x, a.y - b.y, a.z - b.z, a.w - b.w}; }
		F4 operator*(F4 a, F4 b) { return {a.x * b.x, a.y * b.y, a.z * b.z, a.w * b.w}; }
		F4 operator*(F4 a, f32 s) { return {a.x * s, a.y * s, a.z * s, a.w * s}; }
		F4 operator*(f32 s, F4 a) { return {s * a.x, s * a.y, s * a.z, s * a.w}; }
		F4 operator-(F4 a, f32 s) { return {a.x - s, a.y - s, a.z - s, a.w - s}; }

		f32 Frac(f32 x) { return x - std::floor(x); }
		F2 Frac(F2 v) { return {Frac(v.x), Frac(v.y)}; }
		F4 Frac(F4 v) { return {Frac(v.x), Frac(v.y), Frac(v.z), Frac(v.w)}; }
		F2 Floor(F2 v) { return {std::floor(v.x), std::floor(v.y)}; }
		F3 Floor(F3 v) { return {std::floor(v.x), std::floor(v.y), std::floor(v.z)}; }
		F4 Floor(F4 v) { return {std::floor(v.x), std::floor(v.y), std::floor(v.z), std::floor(v.w)}; }
		f32 Lerp(f32 a, f32 b, f32 t) { return a + t * (b - a); }
		F2 Lerp(F2 a, F2 b, F2 t) { return {Lerp(a.x, b.x, t.x), Lerp(a.y, b.y, t.y)}; }
		F4 Lerp(F4 a, F4 b, F4 t) { return {Lerp(a.x, b.x, t.x), Lerp(a.y, b.y, t.y), Lerp(a.z, b.z, t.z), Lerp(a.w, b.w, t.w)}; }
		F4 Lerp(F4 a, F4 b, f32 t) { return Lerp(a, b, F4{t, t, t, t}); }
		f32 Rsqrt(f32 x) { return 1.0f / std::sqrt(x); }
		F4 Rsqrt(F4 v) { return {Rsqrt(v.x), Rsqrt(v.y), Rsqrt(v.z), Rsqrt(v.w)}; }
		f32 Sign(f32 x) { return x > 0.0f ? 1.0f : (x < 0.0f ? -1.0f : 0.0f); }
		f32 Dot(F2 a, F2 b) { return a.x * b.x + a.y * b.y; }
		f32 Dot(F4 a, F4 b) { return a.x * b.x + a.y * b.y + a.z * b.z + a.w * b.w; }

		constexpr f32 Folded(double value) { return static_cast<f32>(value); }

		constexpr f32 MOD_2D = Folded(1.0 / 71.0);
		constexpr f32 MOD_3D = Folded(1.0 / 69.0);
		constexpr f32 HASH_A = Folded(1.0 / 951.135664);
		constexpr f32 HASH_B = Folded(1.0 / 642.949883);

		F3 C2(F3 x) { return x * x * x * (x * (x * 6.0f - F3{15.0f, 15.0f, 15.0f}) + F3{10.0f, 10.0f, 10.0f}); }

		F4 C2Pair(F2 x) {
			const F4 v{x.x, x.y, x.x, x.y};
			const F4 a{6.0f, 6.0f, 0.0f, 0.0f};
			const F4 b{-15.0f, -15.0f, 30.0f, 30.0f};
			const F4 c{10.0f, 10.0f, -60.0f, -60.0f};
			const F4 d{0.0f, 0.0f, 30.0f, 30.0f};
			return v * v * (v * (v * (v * a + b) + c) + d);
		}

		F2 C2Pair(f32 x) {
			const F2 v{x, x};
			return v * v * (v * (v * (v * F2{6.0f, 0.0f} + F2{-15.0f, 30.0f}) + F2{10.0f, -60.0f}) + F2{0.0f, 30.0f});
		}

		F3 C2Derivative(F3 x) { return x * x * (x * (x * 30.0f - F3{60.0f, 60.0f, 60.0f}) + F3{30.0f, 30.0f, 30.0f}); }

		F2 Hash1D(f32 cell) {
			F2 p{cell, cell + 1.0f};
			p = p - Floor(p * MOD_2D) * 71.0f;
			F3 p3{p.x + 26.0f, 0.0f + 161.0f, p.y + 26.0f};
			p3 = p3 * p3;
			return Frac(F2{p3.x, p3.z} * p3.y * HASH_A);
		}

		F4 Corners2D(F2 cell) {
			F4 p{cell.x, cell.y, cell.x + 1.0f, cell.y + 1.0f};
			p = p - Floor(p * MOD_2D) * 71.0f;
			p = p + F4{26.0f, 161.0f, 26.0f, 161.0f};
			p = p * p;
			return F4{p.x, p.z, p.x, p.z} * F4{p.y, p.y, p.w, p.w};
		}

		F4 Hash2D(F2 cell) { return Frac(Corners2D(cell) * HASH_A); }

		void Hash2D(F2 cell, F4& hash0, F4& hash1) {
			const F4 p = Corners2D(cell);
			hash0 = Frac(p * HASH_A);
			hash1 = Frac(p * HASH_B);
		}

		struct Lattice3D {
			F4 p;
			f32 z0 = 0.0f;
			f32 z1 = 0.0f;
		};

		Lattice3D Corners3D(F3 cell) {
			cell = cell - Floor(cell * MOD_3D) * 69.0f;
			const auto next = [](f32 c) { return (67.5f >= c ? 1.0f : 0.0f) * (c + 1.0f); };
			const F3 cell1{next(cell.x), next(cell.y), next(cell.z)};
			F4 p = F4{cell.x, cell.y, cell1.x, cell1.y} + F4{50.0f, 161.0f, 50.0f, 161.0f};
			p = p * p;
			return {F4{p.x, p.z, p.x, p.z} * F4{p.y, p.y, p.w, p.w}, cell.z, cell1.z};
		}

		f32 ZModulus(f32 offset, f32 scale, f32 z) { return 1.0f / (offset + z * scale); }

		void Hash3D(F3 cell, F4& lowz, F4& highz) {
			const Lattice3D lattice = Corners3D(cell);
			const f32 low = ZModulus(635.298681f, 48.500388f, lattice.z0);
			const f32 high = ZModulus(635.298681f, 48.500388f, lattice.z1);
			lowz = Frac(lattice.p * low);
			highz = Frac(lattice.p * high);
		}

		void Hash3D(F3 cell, F4& lowz0, F4& lowz1, F4& lowz2, F4& highz0, F4& highz1, F4& highz2) {
			const Lattice3D lattice = Corners3D(cell);
			constexpr f32 OFFSETS[3] = {635.298681f, 682.357502f, 668.926525f};
			constexpr f32 SCALES[3] = {48.500388f, 65.294118f, 63.934599f};
			F4* lows[3] = {&lowz0, &lowz1, &lowz2};
			F4* highs[3] = {&highz0, &highz1, &highz2};
			for (u32 i = 0; i < 3; ++i) {
				*lows[i] = Frac(lattice.p * ZModulus(OFFSETS[i], SCALES[i], lattice.z0));
				*highs[i] = Frac(lattice.p * ZModulus(OFFSETS[i], SCALES[i], lattice.z1));
			}
		}

		F4 CellularWeight(F4 samples) {
			samples = samples * 2.0f - 1.0f;
			return samples * samples * samples - F4{Sign(samples.x), Sign(samples.y), Sign(samples.z), Sign(samples.w)};
		}

		F2 ValueNoise1D(f32 coordinate) {
			const f32 i = std::floor(coordinate);
			const f32 f = coordinate - i;
			const F2 hash = Hash1D(i);
			const F2 blend = C2Pair(f);
			const f32 delta = hash.y - hash.x;
			return {hash.x + delta * blend.x, delta * blend.y};
		}

		F3 ValueNoise2D(F2 coordinate) {
			const F2 i = Floor(coordinate);
			const F2 f = coordinate - i;
			const F4 hash = Hash2D(i);
			const F4 blend = C2Pair(f);
			const F4 res0 = Lerp(F4{hash.x, hash.y, hash.x, hash.z}, F4{hash.z, hash.w, hash.y, hash.w}, F4{blend.y, blend.y, blend.x, blend.x});
			const F2 delta = F2{res0.y, res0.w} - F2{res0.x, res0.z};
			const F2 slope = delta * F2{blend.z, blend.w};
			return {res0.x + delta.x * blend.x, slope.x, slope.y};
		}

		F4 ValueNoise3D(F3 coordinate) {
			const F3 i = Floor(coordinate);
			const F3 f = coordinate - i;
			F4 lowz;
			F4 highz;
			Hash3D(i, lowz, highz);
			const F3 blend = C2(f);
			const F4 res0 = Lerp(lowz, highz, blend.z);
			const F4 res1 = Lerp(F4{res0.x, res0.y, res0.x, res0.z}, F4{res0.z, res0.w, res0.y, res0.w}, F4{blend.y, blend.y, blend.x, blend.x});
			const F4 res3 = Lerp(F4{lowz.x, lowz.y, highz.x, highz.y}, F4{lowz.z, lowz.w, highz.z, highz.w}, blend.y);
			const F2 res4 = Lerp(F2{res3.x, res3.z}, F2{res3.y, res3.w}, F2{blend.x, blend.x});
			const F3 delta = F3{res1.y, res1.w, res4.y} - F3{res1.x, res1.z, res4.x};
			const F3 slope = delta * C2Derivative(f);
			return {res1.x + delta.x * blend.x, slope.x, slope.y, slope.z};
		}

		F2 PerlinNoise1D(f32 coordinate) {
			const F2 i = Floor(F2{coordinate, 0.0f});
			const F4 f = F4{coordinate, 0.0f, coordinate, 0.0f} - F4{i.x, i.y, i.x + 1.0f, i.y + 1.0f};
			F4 hashX;
			F4 hashY;
			Hash2D(i, hashX, hashY);
			F4 gradX = hashX - 0.49999f;
			F4 gradY = hashY - 0.49999f;
			const F4 norm = Rsqrt(gradX * gradX + gradY * gradY);
			gradX = gradX * norm;
			gradY = gradY * norm;
			const F4 dots = gradX * F4{f.x, f.z, f.x, f.z} + gradY * F4{f.y, f.y, f.w, f.w};
			const F2 d0{dots.x, gradX.x};
			const F2 k0 = F2{dots.y, gradX.y} - d0;
			const F2 k1 = F2{dots.z, gradX.z} - d0;
			const F2 k2 = F2{dots.w, gradX.w} - F2{dots.z, gradX.z} - k0;
			const F4 blend = C2Pair(F2{f.x, f.y});
			F2 result = d0 + blend.x * k0 + blend.y * (k1 + blend.x * k2);
			result.y += blend.z * (k0.x + blend.y * k2.x);
			return result * 2.0f;
		}

		F3 PerlinNoise2D(F2 coordinate) {
			const F2 i = Floor(coordinate);
			const F4 f = F4{coordinate.x, coordinate.y, coordinate.x, coordinate.y} - F4{i.x, i.y, i.x + 1.0f, i.y + 1.0f};
			F4 hashX;
			F4 hashY;
			Hash2D(i, hashX, hashY);
			F4 gradX = hashX - 0.49999f;
			F4 gradY = hashY - 0.49999f;
			const F4 norm = Rsqrt(gradX * gradX + gradY * gradY);
			gradX = gradX * norm;
			gradY = gradY * norm;
			const F4 dots = gradX * F4{f.x, f.z, f.x, f.z} + gradY * F4{f.y, f.y, f.w, f.w};
			const F3 d0{dots.x, gradX.x, gradY.x};
			const F3 d2{dots.z, gradX.z, gradY.z};
			const F3 k0 = F3{dots.y, gradX.y, gradY.y} - d0;
			const F3 k1 = d2 - d0;
			const F3 k2 = F3{dots.w, gradX.w, gradY.w} - d2 - k0;
			const F4 blend = C2Pair(F2{f.x, f.y});
			F3 result = d0 + blend.x * k0 + blend.y * (k1 + blend.x * k2);
			const F2 extra = F2{blend.z, blend.w} * (F2{k0.x, k1.x} + F2{blend.y, blend.x} * F2{k2.x, k2.x});
			result.y += extra.x;
			result.z += extra.y;
			return result * Folded(1.4142135623730950488);
		}

		F4 PerlinNoise3D(F3 coordinate) {
			const F3 i = Floor(coordinate);
			const F3 f = coordinate - i;
			const F3 f1 = f - F3{1.0f, 1.0f, 1.0f};
			F4 hashX0;
			F4 hashY0;
			F4 hashZ0;
			F4 hashX1;
			F4 hashY1;
			F4 hashZ1;
			Hash3D(i, hashX0, hashY0, hashZ0, hashX1, hashY1, hashZ1);
			F4 gradX0 = hashX0 - 0.49999f;
			F4 gradY0 = hashY0 - 0.49999f;
			F4 gradZ0 = hashZ0 - 0.49999f;
			F4 gradX1 = hashX1 - 0.49999f;
			F4 gradY1 = hashY1 - 0.49999f;
			F4 gradZ1 = hashZ1 - 0.49999f;
			const F4 norm0 = Rsqrt(gradX0 * gradX0 + gradY0 * gradY0 + gradZ0 * gradZ0);
			const F4 norm1 = Rsqrt(gradX1 * gradX1 + gradY1 * gradY1 + gradZ1 * gradZ1);
			gradX0 = gradX0 * norm0;
			gradY0 = gradY0 * norm0;
			gradZ0 = gradZ0 * norm0;
			gradX1 = gradX1 * norm1;
			gradY1 = gradY1 * norm1;
			gradZ1 = gradZ1 * norm1;
			const F4 xs{f.x, f1.x, f.x, f1.x};
			const F4 ys{f.y, f.y, f1.y, f1.y};
			const F4 dots0 = xs * gradX0 + ys * gradY0 + F4{f.z, f.z, f.z, f.z} * gradZ0;
			const F4 dots1 = xs * gradX1 + ys * gradY1 + F4{f1.z, f1.z, f1.z, f1.z} * gradZ1;
			const F4 d0{dots0.x, gradX0.x, gradY0.x, gradZ0.x};
			const F4 d1{dots0.y, gradX0.y, gradY0.y, gradZ0.y};
			const F4 d2{dots0.z, gradX0.z, gradY0.z, gradZ0.z};
			const F4 d3{dots0.w, gradX0.w, gradY0.w, gradZ0.w};
			const F4 d4{dots1.x, gradX1.x, gradY1.x, gradZ1.x};
			const F4 d5{dots1.y, gradX1.y, gradY1.y, gradZ1.y};
			const F4 d6{dots1.z, gradX1.z, gradY1.z, gradZ1.z};
			const F4 d7{dots1.w, gradX1.w, gradY1.w, gradZ1.w};
			const F4 k0 = d1 - d0;
			const F4 k1 = d2 - d0;
			const F4 k2 = d4 - d0;
			const F4 k3 = d3 - d2 - k0;
			const F4 k4 = d5 - d4 - k0;
			const F4 k5 = d6 - d4 - k1;
			const F4 k6 = (d7 - d6) - (d5 - d4) - k3;
			const F3 blend = C2(f);
			const F3 blendDerivative = C2Derivative(f);
			const f32 u = blend.x;
			const f32 v = blend.y;
			const f32 w = blend.z;
			F4 result = d0 + u * (k0 + v * k3) + v * (k1 + w * k5) + w * (k2 + u * (k4 + v * k6));
			result.y += Dot(F4{k0.x, k3.x * v, k4.x * w, k6.x * v * w}, F4{blendDerivative.x, blendDerivative.x, blendDerivative.x, blendDerivative.x});
			result.z += Dot(F4{k1.x, k3.x * u, k5.x * w, k6.x * u * w}, F4{blendDerivative.y, blendDerivative.y, blendDerivative.y, blendDerivative.y});
			result.w += Dot(F4{k2.x, k4.x * u, k5.x * v, k6.x * u * v}, F4{blendDerivative.z, blendDerivative.z, blendDerivative.z, blendDerivative.z});
			return result * Folded(1.1547005383792515290);
		}

		F2 CellularNoise1D(f32 coordinate) {
			const F2 i = Floor(F2{coordinate, 0.0f});
			const F2 f = F2{coordinate, 0.0f} - i;
			F4 hashX;
			F4 hashY;
			Hash2D(i, hashX, hashY);
			hashX = CellularWeight(hashX) * 0.25f + F4{0.0f, 1.0f, 0.0f, 1.0f};
			hashY = CellularWeight(hashY) * 0.25f + F4{0.0f, 0.0f, 1.0f, 1.0f};
			const F4 dx = F4{f.x, f.x, f.x, f.x} - hashX;
			const F4 dy = F4{f.y, f.y, f.y, f.y} - hashY;
			const F4 d = dx * dx + dy * dy;
			const F2 t1 = d.x < d.y ? F2{d.x, dx.x} : F2{d.y, dx.y};
			const F2 t2 = d.z < d.w ? F2{d.z, dx.z} : F2{d.w, dx.w};
			return (t1.x < t2.x ? t1 : t2) * F2{1.0f, 2.0f} * Folded(1.0 / 1.125);
		}

		F3 CellularNoise2D(F2 coordinate) {
			const F2 i = Floor(coordinate);
			const F2 f = coordinate - i;
			F4 hashX;
			F4 hashY;
			Hash2D(i, hashX, hashY);
			hashX = CellularWeight(hashX) * 0.25f + F4{0.0f, 1.0f, 0.0f, 1.0f};
			hashY = CellularWeight(hashY) * 0.25f + F4{0.0f, 0.0f, 1.0f, 1.0f};
			const F4 dx = F4{f.x, f.x, f.x, f.x} - hashX;
			const F4 dy = F4{f.y, f.y, f.y, f.y} - hashY;
			const F4 d = dx * dx + dy * dy;
			const F3 t1 = d.x < d.y ? F3{d.x, dx.x, dy.x} : F3{d.y, dx.y, dy.y};
			const F3 t2 = d.z < d.w ? F3{d.z, dx.z, dy.z} : F3{d.w, dx.w, dy.w};
			return (t1.x < t2.x ? t1 : t2) * F3{1.0f, 2.0f, 2.0f} * Folded(1.0 / 1.125);
		}

		F4 CellularNoise3D(F3 coordinate) {
			const F3 i = Floor(coordinate);
			const F3 f = coordinate - i;
			F4 hashX0;
			F4 hashY0;
			F4 hashZ0;
			F4 hashX1;
			F4 hashY1;
			F4 hashZ1;
			Hash3D(i, hashX0, hashY0, hashZ0, hashX1, hashY1, hashZ1);
			constexpr f32 SIXTH = Folded(0.166666666);
			hashX0 = CellularWeight(hashX0) * SIXTH + F4{0.0f, 1.0f, 0.0f, 1.0f};
			hashY0 = CellularWeight(hashY0) * SIXTH + F4{0.0f, 0.0f, 1.0f, 1.0f};
			hashX1 = CellularWeight(hashX1) * SIXTH + F4{0.0f, 1.0f, 0.0f, 1.0f};
			hashY1 = CellularWeight(hashY1) * SIXTH + F4{0.0f, 0.0f, 1.0f, 1.0f};
			hashZ0 = CellularWeight(hashZ0) * SIXTH + F4{0.0f, 0.0f, 0.0f, 0.0f};
			hashZ1 = CellularWeight(hashZ1) * SIXTH + F4{1.0f, 1.0f, 1.0f, 1.0f};
			const F4 fx{f.x, f.x, f.x, f.x};
			const F4 fy{f.y, f.y, f.y, f.y};
			const F4 fz{f.z, f.z, f.z, f.z};
			const F4 dx1 = fx - hashX0;
			const F4 dy1 = fy - hashY0;
			const F4 dz1 = fz - hashZ0;
			const F4 dx2 = fx - hashX1;
			const F4 dy2 = fy - hashY1;
			const F4 dz2 = fz - hashZ1;
			const F4 d1 = dx1 * dx1 + dy1 * dy1 + dz1 * dz1;
			const F4 d2 = dx2 * dx2 + dy2 * dy2 + dz2 * dz2;
			const F4 r1 = d1.x < d1.y ? F4{d1.x, dx1.x, dy1.x, dz1.x} : F4{d1.y, dx1.y, dy1.y, dz1.y};
			const F4 r2 = d1.z < d1.w ? F4{d1.z, dx1.z, dy1.z, dz1.z} : F4{d1.w, dx1.w, dy1.w, dz1.w};
			const F4 r3 = d2.x < d2.y ? F4{d2.x, dx2.x, dy2.x, dz2.x} : F4{d2.y, dx2.y, dy2.y, dz2.y};
			const F4 r4 = d2.z < d2.w ? F4{d2.z, dx2.z, dy2.z, dz2.z} : F4{d2.w, dx2.w, dy2.w, dz2.w};
			const F4 t1 = r1.x < r2.x ? r1 : r2;
			const F4 t2 = r3.x < r4.x ? r3 : r4;
			return (t1.x < t2.x ? t1 : t2) * F4{1.0f, 2.0f, 2.0f, 2.0f} * Folded(9.0 / 12.0);
		}

		void Octave(u32 type, u32 dimension, const f32* at, f32* out) {
			if (dimension == 1) {
				const F2 r = type == 0 ? ValueNoise1D(at[0]) : (type == 1 ? PerlinNoise1D(at[0]) : CellularNoise1D(at[0]));
				out[0] = r.x;
				out[1] = r.y;
			} else if (dimension == 2) {
				const F2 c{at[0], at[1]};
				const F3 r = type == 0 ? ValueNoise2D(c) : (type == 1 ? PerlinNoise2D(c) : CellularNoise2D(c));
				out[0] = r.x;
				out[1] = r.y;
				out[2] = r.z;
			} else {
				const F3 c{at[0], at[1], at[2]};
				const F4 r = type == 0 ? ValueNoise3D(c) : (type == 1 ? PerlinNoise3D(c) : CellularNoise3D(c));
				out[0] = r.x;
				out[1] = r.y;
				out[2] = r.z;
				out[3] = r.w;
			}
		}

		i32 OctaveCount(f32 octaves) { return octaves >= 1.0f ? static_cast<i32>(std::min(octaves, MAX_OCTAVES)) : 0; }

		void CurlPlane(u32 type, F2 at, const f32* parameters, i32 octaves, F2& total) {
			f32 frequency = parameters[0];
			f32 amplitude = 1.0f;
			for (i32 octave = 0; octave < octaves; ++octave) {
				const F3 r = type == 0 ? ValueNoise2D(at * frequency) : (type == 1 ? PerlinNoise2D(at * frequency) : CellularNoise2D(at * frequency));
				total = total + F2{r.y, r.z} * amplitude;
				amplitude *= parameters[1];
				frequency *= parameters[2];
			}
		}

		i32 ToInt(f32 x) {
			if (!(x == x)) {
				return 0;
			}
			if (x >= 2147483648.0f) {
				return 2147483647;
			}
			if (x < -2147483648.0f) {
				return static_cast<i32>(0x80000000u);
			}
			return static_cast<i32>(x);
		}

		u32 Tchou21(u32 x, u32 y) {
			y ^= 1103515245u;
			x += y;
			x *= y;
			x ^= x >> 5u;
			x *= 0x27d4eb2du;
			return x;
		}

		constexpr f32 UNIT_24 = 1.0f / static_cast<f32>(0x00ffffff);

		u32 Lattice(f32 x) { return static_cast<u32>(ToInt(std::nearbyint(x))); }

		f32 Hash21(u32 hash, F2 i) {
			if (hash == 1) {
				const f32 angle = Dot(i, F2{12.9898f, 78.233f});
				return Frac(std::sin(angle) * 43758.5453f);
			}
			if (hash == 2) {
				i = F2{std::fmod(i.x, 289.0f), std::fmod(i.y, 289.0f)};
				f32 x = std::fmod((34.0f * i.x + 1.0f) * i.x, 289.0f) + i.y;
				x = std::fmod((34.0f * x + 1.0f) * x, 289.0f);
				return Frac(x / 41.0f) * 2.0f - 1.0f;
			}
			return static_cast<f32>(Tchou21(Lattice(i.x), Lattice(i.y)) >> 8) * UNIT_24;
		}

		F2 Hash22(u32 hash, F2 i) {
			if (hash == 1) {
				return {Frac(std::sin(i.x * 15.27f + i.y * 99.41f)), Frac(std::sin(i.x * 47.63f + i.y * 89.98f))};
			}
			u32 x = Lattice(i.x);
			u32 y = Lattice(i.y);
			y ^= 1103515245u;
			x += y;
			x *= y;
			x ^= x >> 5u;
			x *= 0x27d4eb2du;
			y ^= x << 3u;
			return {static_cast<f32>(x >> 8) * UNIT_24, static_cast<f32>(y >> 8) * UNIT_24};
		}

		F2 GradientDirection(u32 hash, F2 p) {
			const f32 x = Hash21(hash, p);
			const F2 direction{x - std::floor(x + 0.5f), std::fabs(x) - 0.5f};
			const f32 length = std::sqrt(Dot(direction, direction));
			return {direction.x / length, direction.y / length};
		}

		F2 VoronoiRandomVector(u32 hash, F2 uv, f32 offset) {
			uv = Hash22(hash, uv);
			return F2{std::sin(uv.y * offset), std::cos(uv.x * offset)} * 0.5f + F2{0.5f, 0.5f};
		}

	} // anonymous namespace

	void Fractal(u32 type, u32 dimension, const f32* coordinate, const f32* parameters, f32 octaves, f32* out) {
		f32 total[4] = {};
		f32 frequency = parameters[0];
		f32 amplitude = 1.0f;
		f32 totalAmplitude = 0.0f;
		const i32 count = OctaveCount(octaves);
		for (i32 octave = 0; octave < count; ++octave) {
			f32 at[3] = {};
			for (u32 c = 0; c < dimension; ++c) {
				at[c] = coordinate[c] * frequency;
			}
			f32 one[4] = {};
			Octave(type, dimension, at, one);
			for (u32 c = 0; c <= dimension; ++c) {
				total[c] += one[c] * amplitude;
			}
			totalAmplitude += amplitude;
			amplitude *= parameters[1];
			frequency *= parameters[2];
		}
		for (u32 c = 0; c <= dimension; ++c) {
			out[c] = total[c] / totalAmplitude;
		}
	}

	void Curl(u32 type, u32 dimension, const f32* coordinate, const f32* parameters, f32 octaves, f32* out) {
		const i32 count = OctaveCount(octaves);
		f32 totalAmplitude = 0.0f;
		f32 amplitude = 1.0f;
		for (i32 octave = 0; octave < count; ++octave) {
			totalAmplitude += amplitude;
			amplitude *= parameters[1];
		}
		if (dimension == 2) {
			F2 total;
			CurlPlane(type, F2{coordinate[0], coordinate[1]}, parameters, count, total);
			out[0] = total.y / totalAmplitude;
			out[1] = -total.x / totalAmplitude;
			return;
		}
		F2 total0;
		F2 total1;
		F2 total2;
		CurlPlane(type, F2{coordinate[2], coordinate[1]}, parameters, count, total0);
		CurlPlane(type, F2{coordinate[0] + 100.0f, coordinate[2] + 100.0f}, parameters, count, total1);
		CurlPlane(type, F2{coordinate[1] + 200.0f, coordinate[0] + 200.0f}, parameters, count, total2);
		out[0] = (total2.x - total1.y) / totalAmplitude;
		out[1] = (total0.x - total2.y) / totalAmplitude;
		out[2] = (total1.x - total0.y) / totalAmplitude;
	}

	f32 ShaderValue(u32 hash, f32 x, f32 y) {
		const F2 uv{x, y};
		const F2 i = Floor(uv);
		F2 f = Frac(uv);
		f = f * f * (F2{3.0f, 3.0f} - 2.0f * f);
		const f32 r0 = Hash21(hash, i + F2{0.0f, 0.0f});
		const f32 r1 = Hash21(hash, i + F2{1.0f, 0.0f});
		const f32 r2 = Hash21(hash, i + F2{0.0f, 1.0f});
		const f32 r3 = Hash21(hash, i + F2{1.0f, 1.0f});
		const f32 bottomOfGrid = Lerp(r0, r1, f.x);
		const f32 topOfGrid = Lerp(r2, r3, f.x);
		return Lerp(bottomOfGrid, topOfGrid, f.y);
	}

	f32 ShaderGradient(u32 hash, f32 x, f32 y) {
		const F2 p{x, y};
		const F2 ip = Floor(p);
		F2 fp = Frac(p);
		const f32 d00 = Dot(GradientDirection(hash, ip), fp);
		const f32 d01 = Dot(GradientDirection(hash, ip + F2{0.0f, 1.0f}), fp - F2{0.0f, 1.0f});
		const f32 d10 = Dot(GradientDirection(hash, ip + F2{1.0f, 0.0f}), fp - F2{1.0f, 0.0f});
		const f32 d11 = Dot(GradientDirection(hash, ip + F2{1.0f, 1.0f}), fp - F2{1.0f, 1.0f});
		fp = fp * fp * fp * (fp * (fp * 6.0f - F2{15.0f, 15.0f}) + F2{10.0f, 10.0f});
		return Lerp(Lerp(d00, d01, fp.y), Lerp(d10, d11, fp.y), fp.x) + 0.5f;
	}

	void ShaderVoronoi(u32 hash, f32 x, f32 y, f32 angleOffset, f32 cellDensity, f32* out) {
		const F2 uv{x, y};
		const F2 g = Floor(uv * cellDensity);
		const F2 f = Frac(uv * cellDensity);
		f32 res[3] = {8.0f, 0.0f, 0.0f};
		for (i32 row = -1; row <= 1; ++row) {
			for (i32 column = -1; column <= 1; ++column) {
				const F2 lattice{static_cast<f32>(column), static_cast<f32>(row)};
				const F2 offset = VoronoiRandomVector(hash, lattice + g, angleOffset);
				const F2 between = lattice + offset - f;
				const f32 d = std::sqrt(Dot(between, between));
				if (d < res[0]) {
					res[0] = d;
					res[1] = offset.x;
					res[2] = offset.y;
				}
			}
		}
		out[0] = res[0];
		out[1] = res[1];
	}

} // namespace Shalltear::VisualEffectsImpl::EffectNoises
