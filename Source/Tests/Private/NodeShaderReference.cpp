#include "TestsPCH.h"

#include "NodeShaderTestUtil.h"

#include <cmath>
#include <deque>
#include <format>
#include <limits>
#include <vector>

namespace Shalltear::NodeShaderTest {

	namespace {

		RefValue Of(u32 n) {
			RefValue v;
			v.n = n;
			return v;
		}

		RefValue Scalar(f32 x) {
			RefValue v;
			v.c[0] = x;
			return v;
		}

		template <typename F>
		RefValue Map(const RefValue& a, F f) {
			RefValue out = Of(a.n);
			for (u32 i = 0; i < a.n; ++i) {
				out.c[i] = f(a[i]);
			}
			return out;
		}

		template <typename F>
		RefValue Map(const RefValue& a, const RefValue& b, F f) {
			RefValue out = Of(std::max(a.n, b.n));
			for (u32 i = 0; i < out.n; ++i) {
				out.c[i] = f(a[i], b[i]);
			}
			return out;
		}

		template <typename F>
		RefValue Map(const RefValue& a, const RefValue& b, const RefValue& c, F f) {
			RefValue out = Of(std::max({a.n, b.n, c.n}));
			for (u32 i = 0; i < out.n; ++i) {
				out.c[i] = f(a[i], b[i], c[i]);
			}
			return out;
		}

		f32 Dot(const RefValue& a, const RefValue& b) {
			f32 sum = 0.0f;
			for (u32 i = 0; i < a.n; ++i) {
				sum += a.c[i] * b.c[i];
			}
			return sum;
		}

		f32 Frac(f32 x) { return x - std::floor(x); }
		f32 Saturate(f32 x) { return std::min(std::max(x, 0.0f), 1.0f); }
		f32 Lerp(f32 a, f32 b, f32 t) { return a + t * (b - a); }
		f32 Sin(f32 x) { return static_cast<f32>(std::sin(static_cast<double>(x))); }
		f32 Cos(f32 x) { return static_cast<f32>(std::cos(static_cast<double>(x))); }

		RefValue Normalize(const RefValue& a) {
			f32 length = std::sqrt(Dot(a, a));
			return Map(a, [length](f32 x) { return x / length; });
		}

		constexpr f32 PI_F = 3.14159265358979323846f;

		u32 Tchou21(u32 x, u32 y) {
			y ^= 1103515245u;
			x += y;
			x *= y;
			x ^= x >> 5u;
			x *= 0x27d4eb2du;
			return x;
		}

		void Tchou22(u32& x, u32& y) {
			y ^= 1103515245u;
			x += y;
			x *= y;
			x ^= x >> 5u;
			x *= 0x27d4eb2du;
			y ^= (x << 3u);
		}

		u32 Lattice(f32 v) { return static_cast<u32>(static_cast<i32>(std::nearbyint(v))); }

		f32 Hash21(f32 a, f32 b, GraphNoiseHash hash) {
			if (hash == GraphNoiseHash::LegacyMod) {
				a = std::fmod(a, 289.0f);
				b = std::fmod(b, 289.0f);
				f32 x = std::fmod((34.0f * a + 1.0f) * a, 289.0f) + b;
				x = std::fmod((34.0f * x + 1.0f) * x, 289.0f);
				return Frac(x / 41.0f) * 2.0f - 1.0f;
			}
			if (hash == GraphNoiseHash::LegacySine) {
				return Frac(Sin(a * 12.9898f + b * 78.233f) * 43758.5453f);
			}
			return static_cast<f32>(Tchou21(Lattice(a), Lattice(b)) >> 8) * (1.0f / 16777215.0f);
		}

		f32 ValueNoise(f32 u, f32 v, GraphNoiseHash hash) {
			f32 ix = std::floor(u), iy = std::floor(v);
			f32 fx = Frac(u), fy = Frac(v);
			fx = fx * fx * (3.0f - 2.0f * fx);
			fy = fy * fy * (3.0f - 2.0f * fy);
			f32 r0 = Hash21(ix, iy, hash);
			f32 r1 = Hash21(ix + 1.0f, iy, hash);
			f32 r2 = Hash21(ix, iy + 1.0f, hash);
			f32 r3 = Hash21(ix + 1.0f, iy + 1.0f, hash);
			return Lerp(Lerp(r0, r1, fx), Lerp(r2, r3, fx), fy);
		}

		f32 GradientCorner(f32 px, f32 py, f32 dx, f32 dy, GraphNoiseHash hash) {
			f32 x = Hash21(px, py, hash);
			f32 gx = x - std::floor(x + 0.5f);
			f32 gy = std::fabs(x) - 0.5f;
			f32 length = std::sqrt(gx * gx + gy * gy);
			return gx / length * dx + gy / length * dy;
		}

		f32 GradientNoise(f32 u, f32 v, GraphNoiseHash hash) {
			f32 ix = std::floor(u), iy = std::floor(v);
			f32 fx = Frac(u), fy = Frac(v);
			f32 d00 = GradientCorner(ix, iy, fx, fy, hash);
			f32 d01 = GradientCorner(ix, iy + 1.0f, fx, fy - 1.0f, hash);
			f32 d10 = GradientCorner(ix + 1.0f, iy, fx - 1.0f, fy, hash);
			f32 d11 = GradientCorner(ix + 1.0f, iy + 1.0f, fx - 1.0f, fy - 1.0f, hash);
			fx = fx * fx * fx * (fx * (fx * 6.0f - 15.0f) + 10.0f);
			fy = fy * fy * fy * (fy * (fy * 6.0f - 15.0f) + 10.0f);
			return Lerp(Lerp(d00, d01, fy), Lerp(d10, d11, fy), fx) + 0.5f;
		}

		struct VoronoiResult {
			f32 out = 0.0f;
			f32 cells = 0.0f;
			f32 gap = 0.0f;
		};

		VoronoiResult Voronoi(f32 u, f32 v, f32 angleOffset, f32 cellDensity, GraphNoiseHash hash) {
			f32 gx = std::floor(u * cellDensity), gy = std::floor(v * cellDensity);
			f32 fx = Frac(u * cellDensity), fy = Frac(v * cellDensity);
			f32 best = 8.0f, second = 8.0f, cells = 0.0f;
			for (int y = -1; y <= 1; ++y) {
				for (int x = -1; x <= 1; ++x) {
					f32 lx = static_cast<f32>(x) + gx;
					f32 ly = static_cast<f32>(y) + gy;
					f32 hx, hy;
					if (hash == GraphNoiseHash::LegacySine) {
						hx = Frac(Sin(lx * 15.27f + ly * 99.41f));
						hy = Frac(Sin(lx * 47.63f + ly * 89.98f));
					} else {
						u32 a = Lattice(lx), b = Lattice(ly);
						Tchou22(a, b);
						hx = static_cast<f32>(a >> 8) * (1.0f / 16777215.0f);
						hy = static_cast<f32>(b >> 8) * (1.0f / 16777215.0f);
					}
					f32 ox = Sin(hy * angleOffset) * 0.5f + 0.5f;
					f32 oy = Cos(hx * angleOffset) * 0.5f + 0.5f;
					f32 ddx = static_cast<f32>(x) + ox - fx;
					f32 ddy = static_cast<f32>(y) + oy - fy;
					f32 d = std::sqrt(ddx * ddx + ddy * ddy);
					if (d < best) {
						second = best;
						best = d;
						cells = ox;
					} else if (d < second) {
						second = d;
					}
				}
			}
			return {best, cells, second - best};
		}

		using P = Feed;

		constexpr RefShape VECTOR_1[] = {{{P::F4}}, {{P::F1}}};
		constexpr RefShape VECTOR_2[] = {{{P::F4, P::F4}}, {{P::F1, P::F1}}};
		constexpr RefShape VECTOR_3[] = {{{P::F4, P::F4, P::F4}}, {{P::F1, P::F1, P::F1}}};
		constexpr RefShape VECTOR_PAIR[] = {{{P::F4, P::F4}}, {{P::F3, P::F3}}};
		constexpr RefShape MATRICES[] = {{{P::M2}}, {{P::M3}}, {{P::M4}}};
		constexpr RefShape MULTIPLY[] = {{{P::F4, P::F4}}, {{P::F1, P::F1}}, {{P::M2, P::M2}}, {{P::M3, P::M3}}, {{P::M4, P::M4}}, {{P::F3, P::M3}}, {{P::M4, P::F4}}};
		constexpr RefShape REMAP[] = {{{P::F4, P::F2, P::F2}}, {{P::F1, P::F2, P::F2}}};
		constexpr RefShape RANDOM_RANGE[] = {{{P::F2, P::F1, P::F1}}};
		constexpr RefShape NOISE_SINE[] = {{{P::F4, P::F2}}, {{P::F1, P::F2}}};
		constexpr RefShape MATRIX_CONSTRUCTION[] = {{{P::F4, P::F4, P::F4, P::F4}}};
		constexpr RefShape CROSS[] = {{{P::F3, P::F3}}};
		constexpr RefShape FRESNEL[] = {{{P::F3, P::F3, P::F1}}};
		constexpr RefShape REFRACT[] = {{{P::F3, P::F3, P::F1, P::F1}}};
		constexpr RefShape ROTATE[] = {{{P::F3, P::F3, P::F1}}};
		constexpr RefShape SPHERE_MASK[] = {{{P::F4, P::F4, P::F1, P::F1}}, {{P::F2, P::F2, P::F1, P::F1}}};
		constexpr RefShape COMBINE[] = {{{P::F1, P::F1, P::F1, P::F1}}};
		constexpr RefShape FLIP[] = {{{P::F4}}, {{P::F2}}};
		constexpr RefShape SPLIT[] = {{{P::F4}}, {{P::F3}}, {{P::F1}}};
		constexpr RefShape SWIZZLE[] = {{{P::F4}}};
		constexpr RefShape CHECKERBOARD[] = {{{P::F2, P::F3, P::F3, P::F2}}};
		constexpr RefShape NOISE[] = {{{P::F2, P::F1}}};
		constexpr RefShape VORONOI[] = {{{P::F2, P::F1, P::F1}}};
		constexpr RefShape SHAPE_2[] = {{{P::F2, P::F1, P::F1}}};
		constexpr RefShape SHAPE_3[] = {{{P::F2, P::F1, P::F1, P::F1}}};
		constexpr RefShape SHAPE_4[] = {{{P::F2, P::F1, P::F1, P::F1, P::F1}}};

		constexpr RefShape NO_INPUTS[] = {{}};
		constexpr RefShape ONE_FLOAT[] = {{{P::F1}}};
		constexpr RefShape TWO_FLOATS[] = {{{P::F1, P::F1}}};
		constexpr RefShape THREE_FLOATS[] = {{{P::F1, P::F1, P::F1}}};
		constexpr RefShape FOUR_FLOATS[] = {{{P::F1, P::F1, P::F1, P::F1}}};

		constexpr RefVariant ONE[] = {{"", 0}};
		constexpr RefVariant BOOLEANS[] = {{"Value=false", 0}, {"Value=true", 1}};
		constexpr RefVariant COLORS[] = {{"Color=[0.25,0.5,0.75,1]", 0}, {"Color=[2,0,-1,0.5];Mode=\"HDR\"", 1}};
		constexpr RefVariant CONSTANTS[] = {{"Constant=\"PI\"", 0}, {"Constant=\"TAU\"", 1}, {"Constant=\"PHI\"", 2}, {"Constant=\"E\"", 3}, {"Constant=\"SQRT2\"", 4}};
		constexpr RefVariant INTEGERS[] = {{"Value=3", 0}, {"Value=-2.7", 1}};
		constexpr RefVariant SLIDERS[] = {{"Value=0.25;Min=-1;Max=2", 0}};
		constexpr RefVariant DIELECTRICS[] = {{"Material=\"Common\";Range=0.25", 0}, {"Material=\"RustedMetal\"", 1}, {"Material=\"Water\"", 2}, {"Material=\"Ice\"", 3}, {"Material=\"Glass\"", 4}, {"Material=\"Custom\";IOR=1.5", 5}};
		constexpr RefVariant METALS[] = {{"Material=\"Iron\"", 0}, {"Material=\"Gold\"", 1}, {"Material=\"Platinum\"", 2}};
		constexpr RefVariant EXPONENTIAL[] = {{"Base=\"BaseE\"", 0}, {"Base=\"Base2\"", 1}};
		constexpr RefVariant LOG[] = {{"Base=\"BaseE\"", 0}, {"Base=\"Base2\"", 1}, {"Base=\"Base10\"", 2}};
		constexpr RefVariant RECIPROCAL[] = {{"Method=\"Default\"", 0}, {"Method=\"Fast\"", 1}};
		constexpr RefVariant AXIS[] = {{"Axis=\"Row\"", 0}, {"Axis=\"Column\"", 1}};
		constexpr RefVariant REFRACT_MODES[] = {{"Mode=\"Safe\"", 1}, {"Mode=\"CriticalAngle\"", 0}};
		constexpr RefVariant UNITS[] = {{"Unit=\"Radians\"", 0}, {"Unit=\"Degrees\"", 1}};
		constexpr RefVariant FLIPS[] = {{"Red=false;Green=false;Blue=false;Alpha=false", 0b0000},
										{"Red=true;Green=false;Blue=true;Alpha=false", 0b0101},
										{"Red=true;Green=true;Blue=true;Alpha=true", 0b1111}};
		constexpr RefVariant MASKS[] = {{"Mask=\"wzyx\"", 0}, {"Mask=\"xxyy\"", 1}, {"Mask=\"b\"", 2}, {"Mask=\"rg\"", 3}};
		constexpr std::string_view MASK_TEXT[] = {"wzyx", "xxyy", "b", "rg"};
		constexpr RefVariant DETERMINISTIC[] = {{"Hash Type=\"Deterministic\"", 0}};
		constexpr RefVariant LEGACY_SINE[] = {{"Hash Type=\"LegacySine\"", 1}};
		constexpr RefVariant LEGACY_MOD[] = {{"Hash Type=\"LegacyMod\"", 2}};
		constexpr RefVariant CLAMP_TYPES[] = {{"Clamp Type=\"Fastest\"", 0}, {"Clamp Type=\"Nicest\"", 1}};

		constexpr RefShape NONE_THEN_FLOAT[] = {{{P::None, P::F1}}};
		constexpr RefShape ONE_VECTOR3[] = {{{P::F3}}};
		constexpr RefShape FLIPBOOK[] = {{{P::F2, P::F1, P::F1, P::F1}}};
		constexpr RefShape POLAR[] = {{{P::F2, P::F2, P::F1, P::F1}}};
		constexpr RefShape UV_FOUR_PAIRS[] = {{{P::F2, P::F2, P::F2, P::F2}}};
		constexpr RefShape UV_ROTATE[] = {{{P::F2, P::F2, P::F1}}};
		constexpr RefShape UV_TILING[] = {{{P::F2, P::F2, P::F2}}};
		constexpr RefShape UV_TWIRL[] = {{{P::F2, P::F2, P::F1, P::F2}}};

		constexpr RefVariant POSITION_SPACES[] = {
			{"Space=\"Object\"", 0}, {"Space=\"View\"", 1}, {"Space=\"World\"", 2}, {"Space=\"Tangent\"", 3}, {"Space=\"AbsoluteWorld\"", 4}};
		constexpr std::span<const RefVariant> DIRECTION_SPACES = std::span(POSITION_SPACES).first(4);
		constexpr RefVariant SCREEN_MODES[] = {{"Mode=\"Default\"", 0}, {"Mode=\"Raw\"", 1}, {"Mode=\"Center\"", 2}, {"Mode=\"Tiled\"", 3}, {"Mode=\"Pixel\"", 4}, {"Mode=\"Clip\"", 5}};
		constexpr RefVariant TRANSFORMATION_MATRICES[] = {
			{"Matrix=\"Model\"", 0}, {"Matrix=\"InverseModel\"", 1}, {"Matrix=\"View\"", 2}, {"Matrix=\"InverseView\"", 3}, {"Matrix=\"Projection\"", 4}, {"Matrix=\"InverseProjection\"", 5}, {"Matrix=\"ViewProjection\"", 6}, {"Matrix=\"InverseViewProjection\"", 7}};
		constexpr RefVariant GRADIENT_CASES[] = {{"", 0}, {"", 1}, {"", 2}};
		constexpr RefVariant MATRIX2_ROWS[] = {{"Row 0=[0.5,-0.5];Row 1=[1.5,-1]", 0}};
		constexpr RefVariant MATRIX3_ROWS[] = {{"Row 0=[0.5,-0.5,1.5];Row 1=[-1,2.5,-1.5];Row 2=[3.5,-2,4.5]", 0}};
		constexpr RefVariant MATRIX4_ROWS[] = {{"Row 0=[0.5,-0.5,1.5,-1];Row 1=[2.5,-1.5,3.5,-2];Row 2=[4.5,-2.5,5.5,-3];Row 3=[6.5,-3.5,7.5,-4]", 0}};
		constexpr RefVariant FLIPBOOK_INVERTS[] = {{"", 0},
												   {"Invert X=false;Invert Y=false", 0},
												   {"Invert X=true;Invert Y=false", 1},
												   {"Invert X=false;Invert Y=true", 2},
												   {"Invert X=true;Invert Y=true", 3}};

		constexpr RefBinding TIME_BINDINGS[] = {{"TimeParameters", P::F4, 0}};
		constexpr RefBinding POSITION_BINDINGS[] = {{"ObjectSpacePosition", P::F3, 0},
													{"ViewSpacePosition", P::F3, 1},
													{"WorldSpacePosition", P::F3, 2},
													{"TangentSpacePosition", P::F3, 3},
													{"AbsoluteWorldSpacePosition", P::F3, 4}};
		constexpr RefBinding NORMAL_BINDINGS[] = {
			{"ObjectSpaceNormal", P::F3, 0}, {"ViewSpaceNormal", P::F3, 1}, {"WorldSpaceNormal", P::F3, 2}, {"TangentSpaceNormal", P::F3, 3}};
		constexpr RefBinding TANGENT_BINDINGS[] = {
			{"ObjectSpaceTangent", P::F3, 0}, {"ViewSpaceTangent", P::F3, 1}, {"WorldSpaceTangent", P::F3, 2}, {"TangentSpaceTangent", P::F3, 3}};
		constexpr RefBinding BITANGENT_BINDINGS[] = {{"ObjectSpaceBiTangent", P::F3, 0},
													 {"ViewSpaceBiTangent", P::F3, 1},
													 {"WorldSpaceBiTangent", P::F3, 2},
													 {"TangentSpaceBiTangent", P::F3, 3}};
		constexpr RefBinding VIEW_DIRECTION_BINDINGS[] = {{"ObjectSpaceViewDirection", P::F3, 0},
														  {"ViewSpaceViewDirection", P::F3, 1},
														  {"WorldSpaceViewDirection", P::F3, 2},
														  {"TangentSpaceViewDirection", P::F3, 3}};
		constexpr RefBinding VIEW_VECTOR_BINDINGS[] = {{"CameraPosition", P::F3, 0}, {"WorldSpacePosition", P::F3, 1}, {"View", P::M4, 2}, {"ProjectionParams", P::F4, 3}, {"ViewSpacePosition", P::F3, 4}, {"WorldToObject", P::M4, 5}, {"WorldSpaceTangent", P::F3, 6}, {"WorldSpaceBiTangent", P::F3, 7}, {"WorldSpaceNormal", P::F3, 8}};
		constexpr RefBinding SCREEN_POSITION_BINDINGS[] = {
			{"ScreenPosition", P::F4, 0}, {"NDCPosition", P::F2, 1}, {"PixelPosition", P::F2, 2}, {"ClipPosition", P::F2, 3}, {"ScreenParams", P::F4, 4}};
		constexpr RefBinding INSTANCE_BINDINGS[] = {{"InstanceID", P::F1, 0}};
		constexpr RefBinding EYE_BINDINGS[] = {{"EyeIndex", P::F1, 0}};
		constexpr RefBinding FACE_BINDINGS[] = {{"IsFrontFace", P::B1, 0}};
		constexpr RefBinding TRANSFORMATION_BINDINGS[] = {{"ObjectToWorld", P::M4, 0}, {"WorldToObject", P::M4, 1}, {"View", P::M4, 2}, {"InverseView", P::M4, 3}, {"Projection", P::M4, 4}, {"InverseProjection", P::M4, 5}, {"ViewProjection", P::M4, 6}, {"InverseViewProjection", P::M4, 7}};
		constexpr RefBinding CAMERA_BINDINGS[] = {{"CameraPosition", P::F3, 0}, {"View", P::M4, 1}, {"ProjectionParams", P::F4, 2}, {"ScreenParams", P::F4, 3}};
		constexpr RefBinding OBJECT_BINDINGS[] = {{"ObjectToWorld", P::M4, 0}};
		constexpr RefBinding SCREEN_BINDINGS[] = {{"ScreenParams", P::F4, 0}};
		constexpr RefBinding TRANSFORM_BINDINGS[] = {{"ObjectToWorld", P::M4, 1}, {"WorldToObject", P::M4, 2}, {"View", P::M4, 3}, {"InverseView", P::M4, 4}, {"Projection", P::M4, 5}, {"ViewProjection", P::M4, 6}, {"InverseViewProjection", P::M4, 7}, {"WorldSpaceTangent", P::F3, 8}, {"WorldSpaceBiTangent", P::F3, 9}, {"WorldSpaceNormal", P::F3, 10}, {"WorldSpacePosition", P::F3, 11}};

		GraphNoiseHash HashOf(u32 tag) { return static_cast<GraphNoiseHash>(tag); }

#define SLT_REF(Name) void Name(const RefCase& c, [[maybe_unused]] u32 variant, std::span<RefValue> out)

		SLT_REF(Add) {
			out[0] = Map(c.In(0), c.In(1), [](f32 a, f32 b) { return a + b; });
		}
		SLT_REF(Subtract) {
			out[0] = Map(c.In(0), c.In(1), [](f32 a, f32 b) { return a - b; });
		}
		SLT_REF(Divide) {
			out[0] = Map(c.In(0), c.In(1), [](f32 a, f32 b) { return a / b; });
		}
		SLT_REF(Power) {
			out[0] = Map(c.In(0), c.In(1), [](f32 a, f32 b) { return static_cast<f32>(std::pow(double(a), double(b))); });
		}
		SLT_REF(SquareRoot) {
			out[0] = Map(c.In(0), [](f32 a) { return std::sqrt(a); });
		}

		SLT_REF(Multiply) {
			const RefValue& a = c.In(0);
			const RefValue& b = c.In(1);
			bool matrixA = c.IsMatrix(0);
			bool matrixB = c.IsMatrix(1);
			if (!matrixA && !matrixB) {
				out[0] = Map(a, b, [](f32 x, f32 y) { return x * y; });
				return;
			}
			u32 n = matrixA ? static_cast<u32>(std::lround(std::sqrt(double(a.n)))) : static_cast<u32>(std::lround(std::sqrt(double(b.n))));
			if (matrixA && matrixB) {
				out[0] = Of(n * n);
				for (u32 r = 0; r < n; ++r) {
					for (u32 col = 0; col < n; ++col) {
						f32 sum = 0.0f;
						for (u32 i = 0; i < n; ++i) {
							sum += a.c[r * n + i] * b.c[i * n + col];
						}
						out[0].c[r * n + col] = sum;
					}
				}
			} else if (matrixB) {
				out[0] = Of(n);
				for (u32 col = 0; col < n; ++col) {
					f32 sum = 0.0f;
					for (u32 i = 0; i < n; ++i) {
						sum += a.c[i] * b.c[i * n + col];
					}
					out[0].c[col] = sum;
				}
			} else {
				out[0] = Of(n);
				for (u32 r = 0; r < n; ++r) {
					f32 sum = 0.0f;
					for (u32 i = 0; i < n; ++i) {
						sum += a.c[r * n + i] * b.c[i];
					}
					out[0].c[r] = sum;
				}
			}
		}

		SLT_REF(Absolute) {
			out[0] = Map(c.In(0), [](f32 a) { return std::fabs(a); });
		}
		SLT_REF(Exponential) {
			out[0] = Map(c.In(0), [variant](f32 a) { return static_cast<f32>(variant == 1 ? std::exp2(double(a)) : std::exp(double(a))); });
		}
		SLT_REF(Length) { out[0] = Scalar(std::sqrt(Dot(c.In(0), c.In(0)))); }
		SLT_REF(Log) {
			out[0] = Map(c.In(0), [variant](f32 a) {
				double x = a;
				return static_cast<f32>(variant == 0 ? std::log(x) : (variant == 1 ? std::log2(x) : std::log10(x)));
			});
		}
		SLT_REF(Modulo) {
			out[0] = Map(c.In(0), c.In(1), [](f32 a, f32 b) { return std::fmod(a, b); });
		}
		SLT_REF(Negate) {
			out[0] = Map(c.In(0), [](f32 a) { return -a; });
		}
		SLT_REF(NormalizeNode) { out[0] = Normalize(c.In(0)); }
		SLT_REF(Posterize) {
			out[0] = Map(c.In(0), c.In(1), [](f32 in, f32 steps) {
				f32 step = 1.0f / steps;
				return std::floor(in / step) * step;
			});
		}
		SLT_REF(Reciprocal) {
			out[0] = Map(c.In(0), [](f32 a) { return 1.0f / a; });
		}
		SLT_REF(ReciprocalSquareRoot) {
			out[0] = Map(c.In(0), [](f32 a) { return static_cast<f32>(1.0 / std::sqrt(double(a))); });
		}

		SLT_REF(Ceiling) {
			out[0] = Map(c.In(0), [](f32 a) { return std::ceil(a); });
		}
		SLT_REF(Floor) {
			out[0] = Map(c.In(0), [](f32 a) { return std::floor(a); });
		}
		SLT_REF(Round) {
			out[0] = Map(c.In(0), [](f32 a) { return std::nearbyint(a); });
		}
		SLT_REF(Sign) {
			out[0] = Map(c.In(0), [](f32 a) { return a > 0.0f ? 1.0f : (a < 0.0f ? -1.0f : 0.0f); });
		}
		SLT_REF(Step) {
			out[0] = Map(c.In(0), c.In(1), [](f32 edge, f32 in) { return in >= edge ? 1.0f : 0.0f; });
		}
		SLT_REF(Truncate) {
			out[0] = Map(c.In(0), [](f32 a) { return std::trunc(a); });
		}

		SLT_REF(InverseLerp) {
			out[0] = Map(c.In(0), c.In(1), c.In(2), [](f32 a, f32 b, f32 t) { return (t - a) / (b - a); });
		}
		SLT_REF(LerpNode) {
			out[0] = Map(c.In(0), c.In(1), c.In(2), [](f32 a, f32 b, f32 t) { return Lerp(a, b, t); });
		}
		SLT_REF(Smoothstep) {
			out[0] = Map(c.In(0), c.In(1), c.In(2), [](f32 e0, f32 e1, f32 x) {
				f32 t = Saturate((x - e0) / (e1 - e0));
				return t * t * (3.0f - 2.0f * t);
			});
		}

		SLT_REF(Clamp) {
			out[0] = Map(c.In(0), c.In(1), c.In(2), [](f32 x, f32 lo, f32 hi) { return std::min(std::max(x, lo), hi); });
		}
		SLT_REF(Fraction) {
			out[0] = Map(c.In(0), [](f32 a) { return Frac(a); });
		}
		SLT_REF(Maximum) {
			out[0] = Map(c.In(0), c.In(1), [](f32 a, f32 b) { return std::max(a, b); });
		}
		SLT_REF(Minimum) {
			out[0] = Map(c.In(0), c.In(1), [](f32 a, f32 b) { return std::min(a, b); });
		}
		SLT_REF(OneMinus) {
			out[0] = Map(c.In(0), [](f32 a) { return 1.0f - a; });
		}
		SLT_REF(Remap) {
			const RefValue& in = c.In(0);
			f32 inMin = c.In(1).c[0], inMax = c.In(1).c[1], outMin = c.In(2).c[0], outMax = c.In(2).c[1];
			out[0] = Map(in, [&](f32 x) { return outMin + (x - inMin) * (outMax - outMin) / (inMax - inMin); });
		}
		SLT_REF(SaturateNode) {
			out[0] = Map(c.In(0), [](f32 a) { return Saturate(a); });
		}

		void RemapPrepare(std::array<RefValue, REF_INPUTS>& inputs, u32) {
			RefValue& range = inputs[1];
			range.c[1] = range.c[0] + 1.0f + std::fabs(range.c[1]);
		}

		SLT_REF(RandomRange) {
			f32 angle = c.In(0).c[0] * 12.9898f + c.In(0).c[1] * 78.233f;
			out[0] = Scalar(Lerp(c.In(1).c[0], c.In(2).c[0], Frac(Sin(angle) * 43758.5453f)));
		}
		void RandomRangeBounds(const RefCase& c, std::span<std::array<f32, 2>> out) {
			f32 lo = c.In(1).c[0], hi = c.In(2).c[0];
			out[0] = {std::min(lo, hi), std::max(lo, hi)};
		}

		SLT_REF(Arccosine) {
			out[0] = Map(c.In(0), [](f32 a) { return static_cast<f32>(std::acos(double(a))); });
		}
		SLT_REF(Arcsine) {
			out[0] = Map(c.In(0), [](f32 a) { return static_cast<f32>(std::asin(double(a))); });
		}
		SLT_REF(Arctangent) {
			out[0] = Map(c.In(0), [](f32 a) { return static_cast<f32>(std::atan(double(a))); });
		}
		SLT_REF(Arctangent2) {
			out[0] = Map(c.In(0), c.In(1), [](f32 a, f32 b) { return static_cast<f32>(std::atan2(double(a), double(b))); });
		}
		SLT_REF(Cosine) {
			out[0] = Map(c.In(0), [](f32 a) { return Cos(a); });
		}
		SLT_REF(DegreesToRadians) {
			out[0] = Map(c.In(0), [](f32 a) { return a * static_cast<f32>(3.14159265358979323846 / 180.0); });
		}
		SLT_REF(HyperbolicCosine) {
			out[0] = Map(c.In(0), [](f32 a) { return static_cast<f32>(std::cosh(double(a))); });
		}
		SLT_REF(HyperbolicSine) {
			out[0] = Map(c.In(0), [](f32 a) { return static_cast<f32>(std::sinh(double(a))); });
		}
		SLT_REF(HyperbolicTangent) {
			out[0] = Map(c.In(0), [](f32 a) { return static_cast<f32>(std::tanh(double(a))); });
		}
		SLT_REF(RadiansToDegrees) {
			out[0] = Map(c.In(0), [](f32 a) { return a * static_cast<f32>(180.0 / 3.14159265358979323846); });
		}
		SLT_REF(Sine) {
			out[0] = Map(c.In(0), [](f32 a) { return Sin(a); });
		}
		SLT_REF(Tangent) {
			out[0] = Map(c.In(0), [](f32 a) { return static_cast<f32>(std::tan(double(a))); });
		}

		SLT_REF(NoiseSineWave) {
			const RefValue& in = c.In(0);
			f32 lo = c.In(1).c[0], hi = c.In(1).c[1];
			out[0] = Map(in, [&](f32 x) {
				f32 sinIn = Sin(x);
				f32 random = Frac(Sin((sinIn - Sin(x + 1.0f)) * static_cast<f32>(12.9898 + 78.233)) * 43758.5453f);
				return sinIn + Lerp(lo, hi, random);
			});
		}
		void NoiseSineWaveBounds(const RefCase& c, std::span<std::array<f32, 2>> out) {
			f32 lo = c.In(1).c[0], hi = c.In(1).c[1];
			out[0] = {-1.0f + std::min(lo, hi) - 1e-3f, 1.0f + std::max(lo, hi) + 1e-3f};
		}
		SLT_REF(SawtoothWave) {
			out[0] = Map(c.In(0), [](f32 x) { return 2.0f * (x - std::floor(0.5f + x)); });
		}
		SLT_REF(SquareWave) {
			out[0] = Map(c.In(0), [](f32 x) { return 1.0f - 2.0f * std::nearbyint(Frac(x)); });
		}
		SLT_REF(TriangleWave) {
			out[0] = Map(c.In(0), [](f32 x) { return 2.0f * std::fabs(2.0f * (x - std::floor(0.5f + x))) - 1.0f; });
		}

		SLT_REF(Ddx) {
			out[0] = Map(c.In(0, 1), c.In(0, 0), [](f32 a, f32 b) { return a - b; });
		}
		SLT_REF(Ddy) {
			out[0] = Map(c.In(0, 2), c.In(0, 0), [](f32 a, f32 b) { return a - b; });
		}
		SLT_REF(Ddxy) {
			RefValue x = Map(c.In(0, 1), c.In(0, 0), [](f32 a, f32 b) { return std::fabs(a - b); });
			RefValue y = Map(c.In(0, 2), c.In(0, 0), [](f32 a, f32 b) { return std::fabs(a - b); });
			out[0] = Map(x, y, [](f32 a, f32 b) { return a + b; });
		}

		SLT_REF(MatrixConstruction) {
			bool columns = variant == 1;
			for (u32 n = 4; n >= 2; --n) {
				RefValue m = Of(n * n);
				for (u32 r = 0; r < n; ++r) {
					for (u32 col = 0; col < n; ++col) {
						m.c[r * n + col] = columns ? c.In(col).c[r] : c.In(r).c[col];
					}
				}
				out[4 - n] = m;
			}
		}

		f32 Determinant(std::span<const f32> m, u32 n) {
			if (n == 2) {
				return m[0] * m[3] - m[1] * m[2];
			}
			double sum = 0.0;
			for (u32 col = 0; col < n; ++col) {
				std::array<f32, 9> minor{};
				u32 k = 0;
				for (u32 r = 1; r < n; ++r) {
					for (u32 cc = 0; cc < n; ++cc) {
						if (cc != col) {
							minor[k++] = m[r * n + cc];
						}
					}
				}
				double term = double(m[col]) * Determinant(minor, n - 1);
				sum += (col % 2 == 0) ? term : -term;
			}
			return static_cast<f32>(sum);
		}

		u32 DimensionOf(const RefValue& m) { return m.n == 4 ? 2 : (m.n == 9 ? 3 : 4); }

		SLT_REF(MatrixDeterminant) { out[0] = Scalar(Determinant({c.In(0).c.data(), c.In(0).n}, DimensionOf(c.In(0)))); }
		SLT_REF(MatrixTranspose) {
			u32 n = DimensionOf(c.In(0));
			out[0] = Of(n * n);
			for (u32 r = 0; r < n; ++r) {
				for (u32 col = 0; col < n; ++col) {
					out[0].c[col * n + r] = c.In(0).c[r * n + col];
				}
			}
		}
		SLT_REF(MatrixSplit) {
			u32 n = DimensionOf(c.In(0));
			for (u32 o = 0; o < 4; ++o) {
				out[o] = Of(n);
				for (u32 i = 0; o < n && i < n; ++i) {
					out[o].c[i] = variant == 1 ? c.In(0).c[i * n + o] : c.In(0).c[o * n + i];
				}
			}
		}

		SLT_REF(CrossProduct) {
			const RefValue& a = c.In(0);
			const RefValue& b = c.In(1);
			out[0] = Of(3);
			out[0].c = {a.c[1] * b.c[2] - a.c[2] * b.c[1], a.c[2] * b.c[0] - a.c[0] * b.c[2], a.c[0] * b.c[1] - a.c[1] * b.c[0]};
		}
		SLT_REF(Distance) {
			RefValue d = Map(c.In(0), c.In(1), [](f32 a, f32 b) { return a - b; });
			out[0] = Scalar(std::sqrt(Dot(d, d)));
		}
		SLT_REF(DotProduct) { out[0] = Scalar(Dot(c.In(0), c.In(1))); }
		SLT_REF(FresnelEffect) {
			f32 facing = Saturate(Dot(Normalize(c.In(0)), c.In(1)));
			out[0] = Scalar(static_cast<f32>(std::pow(double(1.0f - facing), double(c.In(2).c[0]))));
		}
		RefValue Projection(const RefValue& a, const RefValue& b) {
			f32 ab = Dot(a, b), bb = Dot(b, b);
			return Map(b, [&](f32 x) { return x * ab / bb; });
		}
		SLT_REF(ProjectionNode) { out[0] = Projection(c.In(0), c.In(1)); }
		SLT_REF(Rejection) {
			out[0] = Map(c.In(0), Projection(c.In(0), c.In(1)), [](f32 a, f32 p) { return a - p; });
		}
		SLT_REF(Reflection) {
			f32 d = Dot(c.In(1), c.In(0));
			out[0] = Map(c.In(0), c.In(1), [d](f32 i, f32 n) { return i - 2.0f * d * n; });
		}
		SLT_REF(Refract) {
			const RefValue& incident = c.In(0);
			const RefValue& normal = c.In(1);
			f32 source = std::max(c.In(2).c[0], 1.0f), medium = std::max(c.In(3).c[0], 1.0f);
			f32 eta = source / medium;
			f32 cos0 = Dot(incident, normal);
			f32 k = 1.0f - eta * eta * (1.0f - cos0 * cos0);
			bool safe = variant == 1;
			f32 root = std::sqrt(safe ? std::max(k, 0.0f) : k);
			RefValue refracted = Map(incident, normal, [&](f32 i, f32 n) { return eta * i - (eta * cos0 + root) * n; });
			if (!safe && k < 0.0f) {
				refracted = Map(incident, normal, [&](f32 i, f32 n) { return i - 2.0f * cos0 * n; });
			}
			out[0] = refracted;
			f32 u = -cos0;
			f32 intensity;
			if (source <= medium) {
				f32 f0 = (medium - source) / (medium + source);
				f0 = f0 * f0;
				f32 x = 1.0f - u;
				f32 x5 = x * (x * x) * (x * x);
				intensity = Saturate((1.0f - x5) - f0 * (1.0f - x5));
			} else if (k >= 0.0f) {
				f32 ior = medium / source;
				f32 g = std::sqrt(ior * ior + u * u - 1.0f);
				f32 first = (g - u) / (g + u);
				f32 second = ((g + u) * u - 1.0f) / ((g - u) * u + 1.0f);
				intensity = 1.0f - Saturate(1.0f - 0.5f * (first * first) * (1.0f + second * second));
			} else {
				intensity = safe ? 0.0f : 1.0f;
			}
			out[1] = Scalar(intensity);
		}
		SLT_REF(RotateAboutAxis) {
			f32 rotation = c.In(2).c[0];
			if (variant == 1) {
				rotation *= static_cast<f32>(3.14159265358979323846 / 180.0);
			}
			f32 s = Sin(rotation), co = Cos(rotation);
			RefValue axis = Normalize(c.In(1));
			const RefValue& in = c.In(0);
			f32 along = Dot(axis, in);
			std::array<f32, 3> crossed = {axis.c[1] * in.c[2] - axis.c[2] * in.c[1], axis.c[2] * in.c[0] - axis.c[0] * in.c[2],
										  axis.c[0] * in.c[1] - axis.c[1] * in.c[0]};
			out[0] = Of(3);
			for (u32 i = 0; i < 3; ++i) {
				out[0].c[i] = in.c[i] * co + crossed[i] * s + axis.c[i] * along * (1.0f - co);
			}
		}
		SLT_REF(SphereMask) {
			RefValue d = Map(c.In(0), c.In(1), [](f32 a, f32 b) { return a - b; });
			f32 distance = std::sqrt(Dot(d, d));
			out[0] = Scalar(1.0f - Saturate((distance - c.In(2).c[0]) / (1.0f - c.In(3).c[0])));
		}

		SLT_REF(Combine) {
			out[0] = Of(4);
			out[1] = Of(3);
			out[2] = Of(2);
			for (u32 i = 0; i < 4; ++i) {
				out[0].c[i] = c.In(i).c[0];
				out[1].c[i] = i < 3 ? c.In(i).c[0] : 0.0f;
				out[2].c[i] = i < 2 ? c.In(i).c[0] : 0.0f;
			}
		}
		SLT_REF(Flip) {
			const RefValue& in = c.In(0);
			out[0] = Of(in.n);
			for (u32 i = 0; i < in.n; ++i) {
				out[0].c[i] = ((variant >> i) & 1u) != 0 ? -in.c[i] : in.c[i];
			}
		}
		SLT_REF(Split) {
			for (u32 i = 0; i < 4; ++i) {
				out[i] = Scalar(i < c.In(0).n ? c.In(0).c[i] : 0.0f);
			}
		}
		SLT_REF(Swizzle) {
			std::string_view mask = MASK_TEXT[variant];
			out[0] = Of(static_cast<u32>(mask.size()));
			for (u32 i = 0; i < mask.size(); ++i) {
				size_t index = std::string_view("xyzw").find(mask[i]);
				if (index == std::string_view::npos) {
					index = std::string_view("rgba").find(mask[i]);
				}
				out[0].c[i] = c.In(0).c[index];
			}
		}

		SLT_REF(Checkerboard) {
			auto uvAt = [&c](u32 corner, u32 axis) { return (c.In(0, corner).c[axis] + 0.5f) * c.In(3, corner).c[axis]; };
			f32 u = uvAt(0, 0), v = uvAt(0, 1);
			f32 ddxU = uvAt(1, 0) - u, ddxV = uvAt(1, 1) - v, ddyU = uvAt(2, 0) - u, ddyV = uvAt(2, 1) - v;
			f32 distanceU = 4.0f * std::fabs(Frac(u + 0.25f) - 0.5f) - 1.0f;
			f32 distanceV = 4.0f * std::fabs(Frac(v + 0.25f) - 0.5f) - 1.0f;
			f32 lengthU = std::sqrt(ddxU * ddxU + ddyU * ddyU);
			f32 lengthV = std::sqrt(ddxV * ddxV + ddyV * ddyV);
			f32 limiter = std::sqrt(std::min(std::max(1.1f - std::max(lengthU, lengthV), 0.0f), 1.0f));
			f32 alphaU = std::min(std::max(distanceU * (0.35f / lengthU), -1.0f), 1.0f);
			f32 alphaV = std::min(std::max(distanceV * (0.35f / lengthV), -1.0f), 1.0f);
			f32 alpha = Saturate(0.5f + 0.5f * alphaU * alphaV * limiter);
			out[0] = Map(c.In(1), c.In(2), [alpha](f32 a, f32 b) { return Lerp(a, b, alpha); });
		}

		SLT_REF(SimpleNoise) {
			f32 u = c.In(0).c[0], v = c.In(0).c[1], scale = c.In(1).c[0];
			f32 sum = 0.0f;
			for (u32 octave = 0; octave < 3; ++octave) {
				f32 s = scale / static_cast<f32>(1u << octave);
				sum += ValueNoise(u * s, v * s, HashOf(variant)) * (0.125f * static_cast<f32>(1u << octave));
			}
			out[0] = Scalar(sum);
		}
		void SimpleNoiseBounds(const RefCase&, std::span<std::array<f32, 2>> out) { out[0] = {0.0f, 0.875f}; }

		SLT_REF(GradientNoiseNode) {
			f32 scale = c.In(1).c[0];
			out[0] = Scalar(GradientNoise(c.In(0).c[0] * scale, c.In(0).c[1] * scale, HashOf(variant)));
		}

		SLT_REF(VoronoiNode) {
			VoronoiResult r = Voronoi(c.In(0).c[0], c.In(0).c[1], c.In(1).c[0], c.In(2).c[0], HashOf(variant));
			out[0] = Scalar(r.out);
			out[1] = Scalar(r.cells);
		}
		bool VoronoiDecidable(const RefCase& c, u32 variant) {
			return Voronoi(c.In(0).c[0], c.In(0).c[1], c.In(1).c[0], c.In(2).c[0], HashOf(variant)).gap > 4e-3f;
		}
		void VoronoiBounds(const RefCase&, std::span<std::array<f32, 2>> out) {
			out[0] = {0.0f, 1.5f};
			out[1] = {0.0f, 1.0f};
		}

		template <typename Distance>
		f32 Edge(const RefCase& c, Distance distance) {
			f32 d0 = distance(0), d1 = distance(1), d2 = distance(2);
			return Saturate((1.0f - d0) / (std::fabs(d1 - d0) + std::fabs(d2 - d0)));
		}

		SLT_REF(Ellipse) {
			out[0] = Scalar(Edge(c, [&c](u32 k) {
				f32 x = (c.In(0, k).c[0] * 2.0f - 1.0f) / c.In(1, k).c[0];
				f32 y = (c.In(0, k).c[1] * 2.0f - 1.0f) / c.In(2, k).c[0];
				return std::sqrt(x * x + y * y);
			}));
		}

		SLT_REF(Polygon) {
			out[0] = Scalar(Edge(c, [&c](u32 k) {
				f32 sides = c.In(1, k).c[0];
				f32 fit = Cos(PI_F / sides);
				f32 x = (c.In(0, k).c[0] * 2.0f - 1.0f) / (c.In(2, k).c[0] * fit);
				f32 y = -((c.In(0, k).c[1] * 2.0f - 1.0f) / (c.In(3, k).c[0] * fit));
				f32 angle = static_cast<f32>(std::atan2(double(x), double(y)));
				f32 r = 2.0f * PI_F / sides;
				return Cos(std::floor(0.5f + angle / r) * r - angle) * std::sqrt(x * x + y * y);
			}));
		}

		SLT_REF(Rectangle) {
			auto centered = [&c](u32 k, u32 axis) { return c.In(0, k).c[axis] * 2.0f - 1.0f; };
			f32 w = c.In(1).c[0], h = c.In(2).c[0];
			if (variant == 0) {
				std::array<f32, 2> d{};
				for (u32 axis = 0; axis < 2; ++axis) {
					f32 size = axis == 0 ? w : h;
					f32 d0 = std::fabs(centered(0, axis)) - size;
					f32 d1 = std::fabs(centered(1, axis)) - (axis == 0 ? c.In(1, 1).c[0] : c.In(2, 1).c[0]);
					f32 d2 = std::fabs(centered(2, axis)) - (axis == 0 ? c.In(1, 2).c[0] : c.In(2, 2).c[0]);
					d[axis] = Saturate(1.0f - d0 / (std::fabs(d1 - d0) + std::fabs(d2 - d0)));
				}
				out[0] = Scalar(std::min(d[0], d[1]));
				return;
			}
			std::array<f32, 2> o{};
			for (u32 axis = 0; axis < 2; ++axis) {
				f32 size = axis == 0 ? w : h;
				f32 p0 = centered(0, axis);
				f32 width = std::fabs(centered(1, axis) - p0) + std::fabs(centered(2, axis) - p0);
				f32 k = 1.0f / std::min(width, 0.5f);
				o[axis] = std::min(Saturate(0.5f + k * (size - std::fabs(p0))), k * size * 2.0f);
			}
			out[0] = Scalar(o[0] * o[1]);
		}

		SLT_REF(RoundedRectangle) {
			auto distance = [&c](u32 k) {
				f32 w = c.In(1, k).c[0], h = c.In(2, k).c[0];
				f32 radius = std::max(std::min(std::min(std::fabs(c.In(3, k).c[0] * 2.0f), std::fabs(w)), std::fabs(h)), 1e-5f);
				f32 x = std::max(0.0f, std::fabs(c.In(0, k).c[0] * 2.0f - 1.0f) - w + radius);
				f32 y = std::max(0.0f, std::fabs(c.In(0, k).c[1] * 2.0f - 1.0f) - h + radius);
				return std::sqrt(x * x + y * y) / radius;
			};
			f32 d0 = distance(0), d1 = distance(1), d2 = distance(2);
			f32 width = std::max(std::fabs(d1 - d0) + std::fabs(d2 - d0), 1e-5f);
			out[0] = Scalar(Saturate((1.0f - d0) / width));
		}

		f32 RoundedPolygonDistance(const RefCase& c, u32 k) {
			const f32 pi = PI_F;
			f32 x = c.In(0, k).c[0] * 2.0f + -1.0f;
			f32 y = c.In(0, k).c[1] * 2.0f + -1.0f;
			f32 width = c.In(1, k).c[0], height = c.In(2, k).c[0];
			x = x / (width + (width == 0.0f ? 1.0f : 0.0f) * 1e-6f);
			y = y / (height + (height == 0.0f ? 1.0f : 0.0f) * 1e-6f);
			f32 roundness = std::min(std::max(c.In(4, k).c[0], 1e-6f), 1.0f);
			f32 sides = std::floor(std::fabs(c.In(3, k).c[0]));
			f32 fullAngle = static_cast<f32>(2.0 * 3.14159265358979323846) / sides;
			f32 halfAngle = fullAngle / 2.0f;
			f32 diagonal = 1.0f / Cos(halfAngle);
			f32 chamferAngle = roundness * halfAngle;
			f32 remainingAngle = halfAngle - chamferAngle;
			f32 ratio = static_cast<f32>(std::tan(double(remainingAngle))) / static_cast<f32>(std::tan(double(halfAngle)));
			f32 cx = Cos(halfAngle) * ratio * diagonal;
			f32 cy = Sin(halfAngle) * ratio * diagonal;
			f32 distA = std::sqrt(cx * cx + cy * cy);
			f32 distB = 1.0f - cx;
			x *= diagonal;
			y *= diagonal;
			f32 angle = static_cast<f32>(std::atan2(double(y), double(x)));
			f32 radius = std::sqrt(x * x + y * y);
			angle += static_cast<f32>(1.57079632679489661923 + 2.0 * 3.14159265358979323846);
			angle = std::fmod(angle + halfAngle, fullAngle);
			angle = std::fabs(angle - halfAngle);
			f32 ux = Cos(angle) * radius;
			f32 angleRatio = 1.0f - (angle - remainingAngle) / chamferAngle;
			f32 distC = std::sqrt(distA * distA + distB * distB - 2.0f * distA * distB * Cos(pi - halfAngle * angleRatio));
			f32 zone = (halfAngle - angle) < chamferAngle ? 1.0f : 0.0f;
			return Lerp(ux, radius / distC, zone);
		}

		SLT_REF(RoundedPolygon) {
			out[0] = Scalar(Edge(c, [&c](u32 k) { return RoundedPolygonDistance(c, k); }));
		}

		SLT_REF(BooleanNode) { out[0] = Scalar(variant == 1 ? 1.0f : 0.0f); }

		SLT_REF(ColorNode) {
			constexpr f32 COLORS[2][4] = {{0.25f, 0.5f, 0.75f, 1.0f}, {2.0f, 0.0f, -1.0f, 0.5f}};
			out[0] = Of(4);
			for (u32 i = 0; i < 4; ++i) {
				out[0].c[i] = COLORS[variant][i];
			}
		}

		SLT_REF(ConstantNode) {
			constexpr f32 VALUES[] = {3.1415926f, 6.28318530f, 1.618034f, 2.718282f, 1.414214f};
			out[0] = Scalar(VALUES[variant]);
		}

		SLT_REF(IntegerNode) { out[0] = Scalar(variant == 0 ? 3.0f : -2.0f); }

		SLT_REF(FloatNode) { out[0] = Scalar(c.In(0).c[0]); }

		SLT_REF(SliderNode) { out[0] = Scalar(0.25f); }

		template <u32 Width>
		void VectorNode(const RefCase& c, [[maybe_unused]] u32 variant, std::span<RefValue> out) {
			out[0] = Of(Width);
			for (u32 i = 0; i < Width; ++i) {
				out[0].c[i] = c.In(i).c[0];
			}
		}

		SLT_REF(DielectricSpecular) {
			switch (variant) {
			case 0:
				out[0] = Scalar(Lerp(0.034f, 0.048f, 0.25f));
				return;
			case 5: {
				f32 above = static_cast<f32>(std::pow(0.5, 2.0));
				f32 below = static_cast<f32>(std::pow(2.5, 2.0));
				out[0] = Scalar(above / below);
				return;
			}
			default: {
				constexpr f32 VALUES[] = {0.0f, 0.030f, 0.020f, 0.018f, 0.040f};
				out[0] = Scalar(VALUES[variant]);
				return;
			}
			}
		}

		SLT_REF(MetalReflectance) {
			constexpr f32 VALUES[][3] = {{0.560f, 0.570f, 0.580f}, {1.000f, 0.766f, 0.336f}, {0.672f, 0.637f, 0.585f}};
			out[0] = Of(3);
			for (u32 i = 0; i < 3; ++i) {
				out[0].c[i] = VALUES[variant][i];
			}
		}

		using V3 = std::array<f32, 3>;

		V3 Three(const RefValue& v) { return {v.c[0], v.c[1], v.c[2]}; }

		RefValue FromThree(const V3& v) {
			RefValue out = Of(3);
			out.c[0] = v[0];
			out.c[1] = v[1];
			out.c[2] = v[2];
			return out;
		}

		f32 Dot3(const V3& a, const V3& b) { return a[0] * b[0] + a[1] * b[1] + a[2] * b[2]; }
		V3 Cross3(const V3& a, const V3& b) { return {a[1] * b[2] - a[2] * b[1], a[2] * b[0] - a[0] * b[2], a[0] * b[1] - a[1] * b[0]}; }
		V3 Scale3(const V3& a, f32 s) { return {a[0] * s, a[1] * s, a[2] * s}; }
		V3 Sub3(const V3& a, const V3& b) { return {a[0] - b[0], a[1] - b[1], a[2] - b[2]}; }
		V3 Add3(const V3& a, const V3& b) { return {a[0] + b[0], a[1] + b[1], a[2] + b[2]}; }
		f32 Length3(const V3& a) { return std::sqrt(Dot3(a, a)); }
		V3 Normalize3(const V3& a) { return Scale3(a, 1.0f / Length3(a)); }
		V3 SafeNormalize3(const V3& a) { return Scale3(a, 1.0f / std::sqrt(std::max(Dot3(a, a), 1.175494351e-38f))); }

		std::array<f32, 4> MulPoint(const RefValue& m, const V3& v, f32 w) {
			std::array<f32, 4> out{};
			for (u32 row = 0; row < 4; ++row) {
				out[row] = m.c[row * 4 + 0] * v[0] + m.c[row * 4 + 1] * v[1] + m.c[row * 4 + 2] * v[2] + m.c[row * 4 + 3] * w;
			}
			return out;
		}

		V3 MulUpper(const RefValue& m, const V3& v) {
			return {m.c[0] * v[0] + m.c[1] * v[1] + m.c[2] * v[2], m.c[4] * v[0] + m.c[5] * v[1] + m.c[6] * v[2],
					m.c[8] * v[0] + m.c[9] * v[1] + m.c[10] * v[2]};
		}

		V3 MulUpperRow(const V3& v, const RefValue& m) {
			return {v[0] * m.c[0] + v[1] * m.c[4] + v[2] * m.c[8], v[0] * m.c[1] + v[1] * m.c[5] + v[2] * m.c[9],
					v[0] * m.c[2] + v[1] * m.c[6] + v[2] * m.c[10]};
		}

		V3 TransformPoint3(const RefValue& m, const V3& p) {
			std::array<f32, 4> h = MulPoint(m, p, 1.0f);
			return {h[0], h[1], h[2]};
		}

		enum Space : u32 { OBJECT,
						   VIEW,
						   WORLD,
						   TANGENT,
						   ABSOLUTE_WORLD,
						   SCREEN };
		enum Conversion : u32 { POSITION,
								DIRECTION,
								NORMAL };

		struct SpaceInputs {
			RefValue objectToWorld, worldToObject, view, inverseView, projection, viewProjection, inverseViewProjection;
			V3 tangent, bitangent, normal, position;
		};

		SpaceInputs SpaceInputsOf(const RefCase& c) {
			return {c.In(1), c.In(2), c.In(3), c.In(4), c.In(5), c.In(6), c.In(7), Three(c.In(8)), Three(c.In(9)), Three(c.In(10)), Three(c.In(11))};
		}

		void Cofactors(const SpaceInputs& s, V3 rows[3], f32& determinant) {
			rows[0] = Cross3(s.bitangent, s.normal);
			rows[1] = Cross3(s.normal, s.tangent);
			rows[2] = Cross3(s.tangent, s.bitangent);
			determinant = Dot3(s.tangent, rows[0]);
		}

		V3 ToScreen(const std::array<f32, 4>& clip) {
			V3 ndc = {clip[0] / clip[3], clip[1] / clip[3], clip[2] / clip[3]};
			return {ndc[0] * 0.5f + 0.5f, 0.5f - ndc[1] * 0.5f, ndc[2]};
		}

		V3 ConvertSpace(const SpaceInputs& s, V3 in, Space from, Space to, Conversion type, bool normalize);

		V3 ViaWorld(const SpaceInputs& s, const V3& in, Space from, Space to, Conversion type, bool normalize) {
			return ConvertSpace(s, ConvertSpace(s, in, from, WORLD, type, false), WORLD, to, type, normalize);
		}

		V3 ConvertSpace(const SpaceInputs& s, V3 in, Space from, Space to, Conversion type, bool normalize) {
			if (from == to) {
				return normalize && type != POSITION ? SafeNormalize3(in) : in;
			}
			const bool position = type == POSITION;
			if (from == WORLD) {
				switch (to) {
				case OBJECT:
					if (position) {
						return TransformPoint3(s.worldToObject, in);
					}
					if (type == DIRECTION) {
						V3 out = MulUpper(s.worldToObject, in);
						return normalize ? Normalize3(out) : out;
					} else {
						V3 out = MulUpperRow(in, s.objectToWorld);
						return normalize ? SafeNormalize3(out) : out;
					}
				case VIEW: {
					if (position) {
						return TransformPoint3(s.view, in);
					}
					V3 out = MulUpper(s.view, in);
					return normalize ? Normalize3(out) : out;
				}
				case TANGENT: {
					if (position) {
						V3 d = Sub3(in, s.position);
						return {Dot3(s.tangent, d), Dot3(s.bitangent, d), Dot3(s.normal, d)};
					}
					if (type == DIRECTION) {
						V3 out = {Dot3(s.tangent, in), Dot3(s.bitangent, in), Dot3(s.normal, in)};
						return normalize ? SafeNormalize3(out) : out;
					}
					V3 rows[3];
					f32 determinant = 0.0f;
					Cofactors(s, rows, determinant);
					V3 out = {Dot3(rows[0], in), Dot3(rows[1], in), Dot3(rows[2], in)};
					return normalize ? SafeNormalize3(Scale3(out, determinant < 0.0f ? -1.0f : 1.0f)) : Scale3(out, 1.0f / determinant);
				}
				case ABSOLUTE_WORLD:
					return position || !normalize ? in : SafeNormalize3(in);
				case SCREEN:
					if (position) {
						return ToScreen(MulPoint(s.viewProjection, in, 1.0f));
					} else {
						V3 out = MulUpper(s.viewProjection, in);
						out = normalize ? Normalize3(out) : out;
						return ToScreen({out[0], out[1], out[2], 1.0f});
					}
				case WORLD:
					break;
				}
				return in;
			}
			switch (from) {
			case OBJECT:
				if (to == WORLD || (to == ABSOLUTE_WORLD && !position)) {
					if (position) {
						return TransformPoint3(s.objectToWorld, in);
					}
					V3 out = type == DIRECTION ? MulUpper(s.objectToWorld, in) : MulUpperRow(in, s.worldToObject);
					return normalize ? SafeNormalize3(out) : out;
				}
				if (to == SCREEN && position) {
					std::array<f32, 4> world = MulPoint(s.objectToWorld, in, 1.0f);
					return ToScreen(MulPoint(s.viewProjection, {world[0], world[1], world[2]}, world[3]));
				}
				return ViaWorld(s, in, from, to, type, normalize);
			case VIEW:
				if (to == WORLD) {
					if (position) {
						return TransformPoint3(s.inverseView, in);
					}
					V3 out = MulUpper(s.inverseView, in);
					return normalize ? Normalize3(out) : out;
				}
				if (to == SCREEN && position) {
					return ToScreen(MulPoint(s.projection, in, 1.0f));
				}
				return ViaWorld(s, in, from, to, type, normalize);
			case TANGENT:
				if (to == WORLD) {
					if (type == NORMAL) {
						V3 out = Add3(Add3(Scale3(s.tangent, in[0]), Scale3(s.bitangent, in[1])), Scale3(s.normal, in[2]));
						return normalize ? SafeNormalize3(out) : out;
					}
					V3 rows[3];
					f32 determinant = 0.0f;
					Cofactors(s, rows, determinant);
					V3 out = Add3(Add3(Scale3(rows[0], in[0]), Scale3(rows[1], in[1])), Scale3(rows[2], in[2]));
					if (position) {
						return Add3(Scale3(out, 1.0f / determinant), s.position);
					}
					return normalize ? SafeNormalize3(Scale3(out, determinant < 0.0f ? -1.0f : 1.0f)) : Scale3(out, 1.0f / determinant);
				}
				return ViaWorld(s, in, from, to, type, normalize);
			case ABSOLUTE_WORLD:
				if (to == WORLD) {
					return position || !normalize ? in : SafeNormalize3(in);
				}
				return ViaWorld(s, in, from, to, type, normalize);
			case SCREEN:
				if (to == WORLD) {
					std::array<f32, 4> clip = {in[0] * 2.0f - 1.0f, 1.0f - in[1] * 2.0f, in[2], position ? 1.0f : 0.0f};
					std::array<f32, 4> world = MulPoint(s.inverseViewProjection, {clip[0], clip[1], clip[2]}, clip[3]);
					return position ? V3{world[0] / world[3], world[1] / world[3], world[2] / world[3]} : V3{world[0], world[1], world[2]};
				}
				return ViaWorld(s, in, from, to, type, normalize);
			default:
				return ViaWorld(s, in, from, to, type, normalize);
			}
		}

		SLT_REF(TransformNode) {
			const u32 normalize = variant % 2;
			const u32 type = (variant / 2) % 3;
			const u32 to = (variant / 6) % 6;
			const u32 from = variant / 36;
			out[0] = FromThree(ConvertSpace(SpaceInputsOf(c), Three(c.In(0)), static_cast<Space>(from), static_cast<Space>(to),
											static_cast<Conversion>(type), normalize != 0));
		}

		SLT_REF(ReadsBinding) { out[0] = c.In(variant); }

		SLT_REF(ViewVectorNode) {
			V3 camera = Three(c.In(0));
			V3 position = Three(c.In(1));
			const RefValue& view = c.In(2);
			const bool orthographic = c.In(3).c[2] == 1.0f;
			V3 viewPosition = Three(c.In(4));
			if (variant == 1) {
				out[0] = FromThree(orthographic ? V3{-0.0f, -0.0f, -viewPosition[2]} : Scale3(viewPosition, -1.0f));
				return;
			}
			V3 forward = {view.c[8], view.c[9], view.c[10]};
			V3 world = Sub3(camera, position);
			if (orthographic) {
				world = Scale3(forward, Dot3(world, forward));
			}
			if (variant == 0) {
				out[0] = FromThree(MulUpper(c.In(5), world));
				return;
			}
			if (variant == 3) {
				SpaceInputs s{};
				s.tangent = Three(c.In(6));
				s.bitangent = Three(c.In(7));
				s.normal = Three(c.In(8));
				out[0] = FromThree(Scale3(ConvertSpace(s, world, WORLD, TANGENT, NORMAL, true), Length3(world)));
				return;
			}
			out[0] = FromThree(world);
		}

		SLT_REF(ScreenPositionNode) {
			const RefValue& raw = c.In(0);
			const RefValue& ndc = c.In(1);
			const RefValue& pixel = c.In(2);
			const RefValue& clip = c.In(3);
			const RefValue& screen = c.In(4);
			out[0] = Of(4);
			switch (variant) {
			case 1:
				out[0] = raw;
				return;
			case 2:
				out[0].c[0] = ndc.c[0] * 2.0f - 1.0f;
				out[0].c[1] = ndc.c[1] * 2.0f - 1.0f;
				return;
			case 3:
				out[0].c[0] = Frac((ndc.c[0] * 2.0f - 1.0f) * screen.c[0] / screen.c[1]);
				out[0].c[1] = Frac(ndc.c[1] * 2.0f - 1.0f);
				return;
			case 4:
				out[0].c[0] = pixel.c[0];
				out[0].c[1] = pixel.c[1];
				return;
			case 5:
				out[0].c[0] = clip.c[0];
				out[0].c[1] = clip.c[1];
				return;
			default:
				out[0].c[0] = ndc.c[0];
				out[0].c[1] = ndc.c[1];
				return;
			}
		}

		SLT_REF(TimeNode) {
			for (u32 i = 0; i < 4; ++i) {
				out[i] = Scalar(c.In(0).c[i]);
			}
		}

		SLT_REF(CameraNode) {
			const RefValue& view = c.In(1);
			const RefValue& projection = c.In(2);
			const RefValue& screen = c.In(3);
			out[0] = FromThree(Three(c.In(0)));
			out[1] = FromThree({view.c[8], view.c[9], view.c[10]});
			out[2] = Scalar(projection.c[2]);
			out[3] = Scalar(projection.c[0]);
			out[4] = Scalar(projection.c[1]);
			out[5] = Scalar(-1.0f);
			out[6] = Scalar(screen.c[2]);
			out[7] = Scalar(screen.c[3]);
		}

		SLT_REF(ObjectNode) {
			const RefValue& m = c.In(0);
			out[0] = FromThree({m.c[3], m.c[7], m.c[11]});
			out[1] = FromThree({Length3({m.c[0], m.c[4], m.c[8]}), Length3({m.c[1], m.c[5], m.c[9]}), Length3({m.c[2], m.c[6], m.c[10]})});
		}

		SLT_REF(ScreenNode) {
			out[0] = Scalar(c.In(0).c[0]);
			out[1] = Scalar(c.In(0).c[1]);
		}

		SLT_REF(FogNode) {
			out[0] = Of(4);
			out[1] = Scalar(0.0f);
		}

		SLT_REF(ReadsOneBinding) { out[0] = c.In(0); }

		SLT_REF(Blackbody) {
			const double t = c.In(0).c[0];
			double color[3] = {56100000.0 * std::pow(t, -1.5) + 148.0, 100.04 * std::log(t) - 623.6, 194.18 * std::log(t) - 1448.6};
			if (t > 6500.0) {
				color[1] = 35200000.0 * std::pow(t, -1.5) + 184.0;
			}
			out[0] = Of(3);
			for (u32 i = 0; i < 3; ++i) {
				double value = std::min(std::max(color[i], 0.0), 255.0) / 255.0;
				out[0].c[i] = static_cast<f32>(t < 1000.0 ? value * (t / 1000.0) : value);
			}
		}

		struct RefGradient {
			std::string_view json;
			u32 mode;
			u32 colors;
			f32 colorKeys[8][4];
			u32 alphas;
			f32 alphaKeys[8][2];
		};

		constexpr RefGradient GRADIENTS[] = {
			{R"({"mode":"Blend","colors":[[1,0.5,0,0],[0,0.25,1,1]],"alphas":[[1,0],[0.25,1]]})", 0, 2, {{1, 0.5f, 0, 0}, {0, 0.25f, 1, 1}}, 2, {{1, 0}, {0.25f, 1}}},
			{R"({"mode":"Fixed","colors":[[1,0,0,0.2],[0,1,0,0.5],[0,0,1,0.9]],"alphas":[[0.5,0.3],[1,0.6]]})", 1, 3, {{1, 0, 0, 0.2f}, {0, 1, 0, 0.5f}, {0, 0, 1, 0.9f}}, 2, {{0.5f, 0.3f}, {1, 0.6f}}},
			{R"({"mode":"PerceptualBlend","colors":[[0.9,0.1,0.2,0.1],[0.2,0.8,0.3,0.6],[0.1,0.2,0.9,0.95]],"alphas":[[1,0],[1,1]]})", 2, 3, {{0.9f, 0.1f, 0.2f, 0.1f}, {0.2f, 0.8f, 0.3f, 0.6f}, {0.1f, 0.2f, 0.9f, 0.95f}}, 2, {{1, 0}, {1, 1}}},
		};

		V3 Oklab(const V3& rgb) {
			f32 l = 0.4122214708f * rgb[0] + 0.5363325363f * rgb[1] + 0.0514459929f * rgb[2];
			f32 m = 0.2119034982f * rgb[0] + 0.6806995451f * rgb[1] + 0.1073969566f * rgb[2];
			f32 s = 0.0883024619f * rgb[0] + 0.2817188376f * rgb[1] + 0.6299787005f * rgb[2];
			f32 l_ = static_cast<f32>(std::pow(static_cast<double>(l), 0.333333));
			f32 m_ = static_cast<f32>(std::pow(static_cast<double>(m), 0.333333));
			f32 s_ = static_cast<f32>(std::pow(static_cast<double>(s), 0.333333));
			return {0.2104542553f * l_ + 0.7936177850f * m_ - 0.0040720468f * s_, 1.9779984951f * l_ - 2.4285922050f * m_ + 0.4505937099f * s_,
					0.0259040371f * l_ + 0.7827717662f * m_ - 0.8086757660f * s_};
		}

		V3 FromOklab(const V3& lab) {
			f32 l_ = lab[0] + 0.3963377774f * lab[1] + 0.2158037573f * lab[2];
			f32 m_ = lab[0] - 0.1055613458f * lab[1] - 0.0638541728f * lab[2];
			f32 s_ = lab[0] - 0.0894841775f * lab[1] - 1.2914855480f * lab[2];
			f32 l = l_ * l_ * l_;
			f32 m = m_ * m_ * m_;
			f32 s = s_ * s_ * s_;
			return {4.0767416621f * l - 3.3077115913f * m + 0.2309699292f * s, -1.2684380046f * l + 2.6097574011f * m - 0.3413193965f * s,
					-0.0041960863f * l - 0.7034186147f * m + 1.7076147010f * s};
		}

		SLT_REF(SampleGradient) {
			const RefGradient& g = GRADIENTS[variant];
			const f32 time = c.In(1).c[0];
			const bool fixed = g.mode % 2 == 1;
			const bool perceptual = g.mode == 2;
			auto key = [&](u32 k) {
				V3 rgb = {g.colorKeys[k][0], g.colorKeys[k][1], g.colorKeys[k][2]};
				return perceptual ? Oklab(rgb) : rgb;
			};
			auto blend = [&](f32 position) { return fixed ? (position >= 0.01f ? 1.0f : 0.0f) : position; };
			V3 color = key(0);
			for (u32 k = 1; k < g.colors; ++k) {
				f32 position = Saturate((time - g.colorKeys[k - 1][3]) / (g.colorKeys[k][3] - g.colorKeys[k - 1][3]));
				V3 next = key(k);
				f32 t = blend(position);
				color = {Lerp(color[0], next[0], t), Lerp(color[1], next[1], t), Lerp(color[2], next[2], t)};
			}
			if (perceptual) {
				color = FromOklab(color);
			}
			f32 alpha = g.alphaKeys[0][0];
			for (u32 k = 1; k < g.alphas; ++k) {
				f32 position = Saturate((time - g.alphaKeys[k - 1][1]) / (g.alphaKeys[k][1] - g.alphaKeys[k - 1][1]));
				alpha = Lerp(alpha, g.alphaKeys[k][0], blend(position));
			}
			out[0] = Of(4);
			out[0].c[0] = color[0];
			out[0].c[1] = color[1];
			out[0].c[2] = color[2];
			out[0].c[3] = alpha;
		}

		void WireGradient(DocumentBuilder& builder, u32 variant) {
			GraphNode& gradient = builder.Node(60, "Input/Gradient");
			DocumentBuilder::Setting(gradient, "Gradient", GRADIENTS[variant].json);
			builder.Connect(60, "Out", 100, "Gradient");
		}

		template <u32 Dimension>
		void MatrixNode(const RefCase& c, [[maybe_unused]] u32 variant, std::span<RefValue> out) {
			out[0] = Of(Dimension * Dimension);
			for (u32 i = 0; i < Dimension * Dimension; ++i) {
				out[0].c[i] = static_cast<f32>(i + 1) * (i % 2 == 0 ? 0.5f : -0.25f);
			}
		}

		SLT_REF(Flipbook) {
			f32 width = c.In(1).c[0];
			f32 height = c.In(2).c[0];
			f32 tile = std::floor(std::fmod(c.In(3).c[0] + 0.00001f, width * height));
			f32 countX = 1.0f / width;
			f32 countY = 1.0f / height;
			f32 base = std::floor((tile + 0.5f) * countX);
			f32 column = tile - width * base;
			f32 tileX = (variant & 1) != 0 ? width - (column + 1.0f) : column;
			f32 tileY = (variant & 2) != 0 ? height - (base + 1.0f) : base;
			out[0] = Of(2);
			out[0].c[0] = (c.In(0).c[0] + tileX) * countX;
			out[0].c[1] = (c.In(0).c[1] + tileY) * countY;
		}

		void FlipbookPrepare(std::array<RefValue, REF_INPUTS>& inputs, u32) {
			for (u32 i = 1; i <= 3; ++i) {
				inputs[i].c[0] = std::round(inputs[i].c[0]);
			}
		}

		SLT_REF(PolarCoordinates) {
			f32 dx = c.In(0).c[0] - c.In(1).c[0];
			f32 dy = c.In(0).c[1] - c.In(1).c[1];
			out[0] = Of(2);
			out[0].c[0] = std::sqrt(dx * dx + dy * dy) * 2.0f * c.In(2).c[0];
			out[0].c[1] = static_cast<f32>(std::atan2(static_cast<double>(dx), static_cast<double>(dy))) / 6.28f * c.In(3).c[0];
		}

		SLT_REF(RadialShear) {
			f32 dx = c.In(0).c[0] - c.In(1).c[0];
			f32 dy = c.In(0).c[1] - c.In(1).c[1];
			f32 d2 = dx * dx + dy * dy;
			out[0] = Of(2);
			out[0].c[0] = c.In(0).c[0] + dy * (d2 * c.In(2).c[0]) + c.In(3).c[0];
			out[0].c[1] = c.In(0).c[1] + -dx * (d2 * c.In(2).c[1]) + c.In(3).c[1];
		}

		SLT_REF(Spherize) {
			f32 dx = c.In(0).c[0] - c.In(1).c[0];
			f32 dy = c.In(0).c[1] - c.In(1).c[1];
			f32 d2 = dx * dx + dy * dy;
			f32 d4 = d2 * d2;
			out[0] = Of(2);
			out[0].c[0] = c.In(0).c[0] + dx * (d4 * c.In(2).c[0]) + c.In(3).c[0];
			out[0].c[1] = c.In(0).c[1] + dy * (d4 * c.In(2).c[1]) + c.In(3).c[1];
		}

		SLT_REF(Rotate) {
			f32 rotation = c.In(2).c[0];
			if (variant == 1) {
				rotation = rotation * (3.1415926f / 180.0f);
			}
			f32 ux = c.In(0).c[0] - c.In(1).c[0];
			f32 uy = c.In(0).c[1] - c.In(1).c[1];
			f32 s = Sin(rotation);
			f32 cs = Cos(rotation);
			out[0] = Of(2);
			out[0].c[0] = (ux * cs + uy * s) + c.In(1).c[0];
			out[0].c[1] = (ux * -s + uy * cs) + c.In(1).c[1];
		}

		SLT_REF(TilingAndOffset) {
			out[0] = Of(2);
			out[0].c[0] = c.In(0).c[0] * c.In(1).c[0] + c.In(2).c[0];
			out[0].c[1] = c.In(0).c[1] * c.In(1).c[1] + c.In(2).c[1];
		}

		SLT_REF(Twirl) {
			f32 dx = c.In(0).c[0] - c.In(1).c[0];
			f32 dy = c.In(0).c[1] - c.In(1).c[1];
			f32 angle = c.In(2).c[0] * std::sqrt(dx * dx + dy * dy);
			f32 cs = Cos(angle);
			f32 s = Sin(angle);
			out[0] = Of(2);
			out[0].c[0] = (cs * dx - s * dy) + c.In(1).c[0] + c.In(3).c[0];
			out[0].c[1] = (s * dx + cs * dy) + c.In(1).c[1] + c.In(3).c[1];
		}

		Mat4 MatrixOf(const RefValue& v) {
			Mat4 m;
			for (u32 row = 0; row < 4; ++row) {
				for (u32 column = 0; column < 4; ++column) {
					m(row, column) = v.c[row * 4 + column];
				}
			}
			return m;
		}

		RefValue ValueOf(const Mat4& m) {
			RefValue v = Of(16);
			for (u32 row = 0; row < 4; ++row) {
				for (u32 column = 0; column < 4; ++column) {
					v.c[row * 4 + column] = m(row, column);
				}
			}
			return v;
		}

		void TransformPrepare(std::array<RefValue, REF_INPUTS>& inputs, u32) {
			const RefValue& seed = inputs[1];
			const Quat rotation = Quat::AngleAxis(seed.c[0] * 40.0f, Vec3(seed.c[1], seed.c[2], seed.c[3] + 2.0f).Normalized());
			const Vec3 scale(1.0f + std::fabs(seed.c[4]) * 0.3f, 1.0f + std::fabs(seed.c[5]) * 0.3f, -1.0f - std::fabs(seed.c[6]) * 0.3f);
			const Mat4 objectToWorld = Mat4::TRS(Vec3(seed.c[7], seed.c[8], seed.c[9]) * 0.25f, rotation, scale);
			const Vec3 eye = Vec3(seed.c[10], seed.c[11] * 0.5f + 1.0f, 5.0f);
			const Mat4 view = Mat4::LookAt(eye, Vec3(0.0f, 0.0f, 0.0f));
			const Mat4 projection = Mat4::PerspectiveReverseZ(60.0f, 1.5f, 0.1f, 100.0f);
			const Mat4 viewProjection = projection * view;
			inputs[1] = ValueOf(objectToWorld);
			inputs[2] = ValueOf(objectToWorld.Inverse());
			inputs[3] = ValueOf(view);
			inputs[4] = ValueOf(view.Inverse());
			inputs[5] = ValueOf(projection);
			inputs[6] = ValueOf(viewProjection);
			inputs[7] = ValueOf(viewProjection.Inverse());
			V3 normal = Normalize3(Three(inputs[10]));
			V3 helper = std::fabs(normal[1]) < 0.9f ? V3{0.0f, 1.0f, 0.0f} : V3{1.0f, 0.0f, 0.0f};
			V3 tangent = Normalize3(Cross3(helper, normal));
			f32 hand = inputs[8].c[0] < 0.0f ? -1.0f : 1.0f;
			inputs[8] = FromThree(tangent);
			inputs[9] = FromThree(Scale3(Cross3(normal, tangent), hand));
			inputs[10] = FromThree(normal);
		}

		void ViewVectorPrepare(std::array<RefValue, REF_INPUTS>& inputs, u32) {
			const Vec3 eye(inputs[0].c[0], inputs[0].c[1], inputs[0].c[2] + 6.0f);
			const Mat4 view = Mat4::LookAt(eye, Vec3(0.0f, 0.0f, 0.0f));
			inputs[0] = FromThree({eye.x, eye.y, eye.z});
			inputs[2] = ValueOf(view);
			inputs[3].c[2] = inputs[3].c[0] < 0.0f ? 1.0f : 0.0f;
			const Vec3 world(inputs[1].c[0], inputs[1].c[1], inputs[1].c[2]);
			const Vec4 inView = view * Vec4(world, 1.0f);
			inputs[4] = FromThree({inView.x, inView.y, inView.z});
			inputs[5] = ValueOf(Mat4::TRS(Vec3(0.3f, -0.2f, 0.1f), Quat::AngleAxis(25.0f, Vec3(0.0f, 1.0f, 0.0f)), Vec3(1.5f, 0.8f, 1.2f)).Inverse());
			V3 normal = Normalize3(Three(inputs[8]));
			V3 tangent = Normalize3(Cross3(std::fabs(normal[1]) < 0.9f ? V3{0.0f, 1.0f, 0.0f} : V3{1.0f, 0.0f, 0.0f}, normal));
			inputs[6] = FromThree(tangent);
			inputs[7] = FromThree(Cross3(normal, tangent));
			inputs[8] = FromThree(normal);
		}

		void ScreenPrepare(std::array<RefValue, REF_INPUTS>& inputs, u32) {
			inputs[4].c[0] = 64.0f + std::fabs(inputs[4].c[0]) * 100.0f;
			inputs[4].c[1] = 48.0f + std::fabs(inputs[4].c[1]) * 100.0f;
		}

		struct TransformCases {
			std::deque<str> texts;
			std::vector<RefVariant> variants;
		};

		const TransformCases& BuildTransformCases() {
			static const TransformCases cases = [] {
				constexpr std::string_view SPACES[] = {"Object", "View", "World", "Tangent", "AbsoluteWorld", "Screen"};
				constexpr std::string_view TYPES[] = {"Position", "Direction", "Normal"};
				TransformCases out;
				for (u32 from = 0; from < 6; ++from) {
					for (u32 to = 0; to < 6; ++to) {
						for (u32 type = 0; type < 3; ++type) {
							for (u32 normalize = 0; normalize < (type == 0 ? 1u : 2u); ++normalize) {
								out.texts.push_back(std::format("From=\"{}\";To=\"{}\";Type=\"{}\";Normalize={}", SPACES[from], SPACES[to],
																TYPES[type], normalize != 0 ? "true" : "false"));
								out.variants.push_back({out.texts.back(), ((from * 6 + to) * 3 + type) * 2 + normalize});
							}
						}
					}
				}
				return out;
			}();
			return cases;
		}

		V3 Hsv(const V3& in, f32 e, bool epsilonV) {
			auto step = [](f32 edge, f32 x) { return x >= edge ? 1.0f : 0.0f; };
			const f32 k[4] = {0.0f, -1.0f / 3.0f, 2.0f / 3.0f, -1.0f};
			const f32 s1 = step(in[2], in[1]);
			const f32 a[4] = {in[2], in[1], k[3], k[2]};
			const f32 b[4] = {in[1], in[2], k[0], k[1]};
			f32 p[4];
			for (u32 i = 0; i < 4; ++i) {
				p[i] = Lerp(a[i], b[i], s1);
			}
			const f32 s2 = step(p[0], in[0]);
			const f32 qa[4] = {p[0], p[1], p[3], in[0]};
			const f32 qb[4] = {in[0], p[1], p[2], p[0]};
			f32 q[4];
			for (u32 i = 0; i < 4; ++i) {
				q[i] = Lerp(qa[i], qb[i], s2);
			}
			const f32 d = q[0] - std::min(q[3], q[1]);
			const f32 v = epsilonV ? (d == 0.0f ? q[0] : q[0] + e) : q[0];
			return {std::fabs(q[2] + (q[3] - q[1]) / (6.0f * d + e)), d / (q[0] + e), v};
		}

		V3 Rgb(const V3& hsv) {
			V3 out{};
			const f32 k[4] = {1.0f, 2.0f / 3.0f, 1.0f / 3.0f, 3.0f};
			for (u32 i = 0; i < 3; ++i) {
				const f32 p = std::fabs(Frac(hsv[0] + k[i]) * 6.0f - k[3]);
				out[i] = hsv[2] * Lerp(k[0], Saturate(p - k[0]), hsv[1]);
			}
			return out;
		}

		V3 ToLinear(const V3& in) {
			V3 out{};
			for (u32 i = 0; i < 3; ++i) {
				out[i] = in[i] <= 0.04045f ? in[i] / 12.92f : std::pow(std::max(std::fabs((in[i] + 0.055f) / 1.055f), 1.192092896e-07f), 2.4f);
			}
			return out;
		}

		V3 ToSrgb(const V3& in) {
			V3 out{};
			for (u32 i = 0; i < 3; ++i) {
				out[i] = in[i] <= 0.0031308f ? in[i] * 12.92f : std::pow(std::max(std::fabs(in[i]), 1.192092896e-07f), 1.0f / 2.4f) * 1.055f - 0.055f;
			}
			return out;
		}

		SLT_REF(ChannelMixer) {
			const V3 rows[2][3] = {{{1, 0, 0}, {0, 1, 0}, {0, 0, 1}}, {{0.5f, 0.25f, -0.5f}, {0, 2, 0.5f}, {-1, 0.25f, 1.5f}}};
			const V3 in = Three(c.In(0));
			out[0] = FromThree({Dot3(in, rows[variant][0]), Dot3(in, rows[variant][1]), Dot3(in, rows[variant][2])});
		}

		SLT_REF(Contrast) {
			const f32 midpoint = std::pow(0.5f, 2.2f);
			out[0] = Map(c.In(0), [&](f32 x) { return (x - midpoint) * c.In(1).c[0] + midpoint; });
		}

		SLT_REF(Hue) {
			V3 hsv = Hsv(Three(c.In(0)), 1e-4f, true);
			const f32 hue = hsv[0] + (variant == 1 ? c.In(1).c[0] : c.In(1).c[0] / 360.0f);
			hsv[0] = hue < 0.0f ? hue + 1.0f : (hue > 1.0f ? hue - 1.0f : hue);
			out[0] = FromThree(Rgb(hsv));
		}

		SLT_REF(InvertColors) {
			const RefValue& in = c.In(0);
			out[0] = Of(in.n);
			for (u32 i = 0; i < in.n; ++i) {
				const f32 flag = ((variant >> i) & 1u) != 0 ? 1.0f : 0.0f;
				out[0].c[i] = std::fabs(flag - in.c[i]);
			}
		}

		SLT_REF(ReplaceColor) {
			const V3 in = Three(c.In(0));
			const V3 from = Three(c.In(1));
			const V3 to = Three(c.In(2));
			const f32 distance = Length3(Sub3(from, in));
			const f32 t = Saturate((distance - c.In(3).c[0]) / std::max(c.In(4).c[0], 1e-5f));
			out[0] = FromThree({Lerp(to[0], in[0], t), Lerp(to[1], in[1], t), Lerp(to[2], in[2], t)});
		}

		SLT_REF(Saturation) {
			const V3 in = Three(c.In(0));
			const f32 luma = Dot3(in, {0.2126729f, 0.7151522f, 0.0721750f});
			const f32 s = c.In(1).c[0];
			out[0] = FromThree({luma + s * (in[0] - luma), luma + s * (in[1] - luma), luma + s * (in[2] - luma)});
		}

		SLT_REF(WhiteBalance) {
			const f32 t1 = c.In(1).c[0] * 10.0f / 6.0f;
			const f32 t2 = c.In(2).c[0] * 10.0f / 6.0f;
			const f32 x = 0.31271f - t1 * (t1 < 0.0f ? 0.1f : 0.05f);
			const f32 y = 2.87f * x - 3.0f * x * x - 0.27509507f + t2 * 0.05f;
			const f32 bigX = x / y;
			const f32 bigZ = (1.0f - x - y) / y;
			const V3 lms = {0.7328f * bigX + 0.4296f - 0.1624f * bigZ, -0.7036f * bigX + 1.6975f + 0.0061f * bigZ, 0.0030f * bigX + 0.0136f + 0.9834f * bigZ};
			const V3 balance = {0.949237f / lms[0], 1.03542f / lms[1], 1.08728f / lms[2]};
			const V3 in = Three(c.In(0));
			const V3 toLms[3] = {{3.90405e-1f, 5.49941e-1f, 8.92632e-3f}, {7.08416e-2f, 9.63172e-1f, 1.35775e-3f}, {2.31082e-2f, 1.28021e-1f, 9.36245e-1f}};
			const V3 toLinear[3] = {{2.85847e+0f, -1.62879e+0f, -2.48910e-2f}, {-2.10182e-1f, 1.15820e+0f, 3.24281e-4f}, {-4.18120e-2f, -1.18169e-1f, 1.06867e+0f}};
			const V3 scaled = {Dot3(toLms[0], in) * balance[0], Dot3(toLms[1], in) * balance[1], Dot3(toLms[2], in) * balance[2]};
			out[0] = FromThree({Dot3(toLinear[0], scaled), Dot3(toLinear[1], scaled), Dot3(toLinear[2], scaled)});
		}

		f32 BlendChannel(u32 mode, f32 base, f32 blend, f32 opacity) {
			auto step = [](f32 edge, f32 x) { return x >= edge ? 1.0f : 0.0f; };
			f32 out = 0.0f;
			switch (mode) {
			case 0:
				out = 1.0f - (1.0f - blend) / (base + 0.000000000001f);
				break;
			case 1:
				out = std::min(blend, base);
				break;
			case 2:
				out = std::fabs(blend - base);
				break;
			case 3:
				out = base / (1.0f - std::clamp(blend, 0.000001f, 0.999999f));
				break;
			case 4:
				out = base / (blend + 0.000000000001f);
				break;
			case 5:
				out = blend + base - (2.0f * blend * base);
				break;
			case 6: {
				const f32 zeroOrOne = step(blend, 0.5f);
				out = 2.0f * base * blend * zeroOrOne + (1.0f - zeroOrOne) * (1.0f - 2.0f * (1.0f - base) * (1.0f - blend));
				break;
			}
			case 7:
				out = step(1.0f - base, blend);
				break;
			case 8:
				out = std::max(blend, base);
				break;
			case 9:
				out = base + blend - 1.0f;
				break;
			case 10:
				out = base + blend;
				break;
			case 11:
				out = blend < 0.5f ? std::max(base + 2.0f * blend - 1.0f, 0.0f) : std::min(base + 2.0f * (blend - 0.5f), 1.0f);
				break;
			case 12:
				out = blend + 2.0f * base - 1.0f;
				break;
			case 13:
				out = base * blend;
				break;
			case 14:
				out = 1.0f - std::fabs(1.0f - blend - base);
				break;
			case 15: {
				const f32 zeroOrOne = step(base, 0.5f);
				out = 2.0f * base * blend * zeroOrOne + (1.0f - zeroOrOne) * (1.0f - 2.0f * (1.0f - base) * (1.0f - blend));
				break;
			}
			case 16: {
				const f32 check = step(0.5f, blend);
				out = check * std::max(2.0f * (base - 0.5f), blend) + (1.0f - check) * std::min(2.0f * base, blend);
				break;
			}
			case 17:
				out = 1.0f - (1.0f - blend) * (1.0f - base);
				break;
			case 18: {
				const f32 zeroOrOne = step(0.5f, blend);
				const f32 result1 = 2.0f * base * blend + base * base * (1.0f - 2.0f * blend);
				const f32 result2 = std::sqrt(base) * (2.0f * blend - 1.0f) + 2.0f * base * (1.0f - blend);
				out = result2 * zeroOrOne + (1.0f - zeroOrOne) * result1;
				break;
			}
			case 19:
				out = base - blend;
				break;
			case 20: {
				base = std::clamp(base, 0.000001f, 0.999999f);
				const f32 zeroOrOne = step(0.5f, base);
				out = blend / (2.0f * (1.0f - base)) * zeroOrOne + (1.0f - zeroOrOne) * (1.0f - (1.0f - blend) / (2.0f * base));
				break;
			}
			default:
				out = blend;
				break;
			}
			return Lerp(base, out, opacity);
		}

		SLT_REF(BlendNode) {
			const RefValue& base = c.In(0);
			const RefValue& blend = c.In(1);
			out[0] = Of(base.n);
			for (u32 i = 0; i < base.n; ++i) {
				out[0].c[i] = BlendChannel(variant, base.c[i], blend.c[i], c.In(2).c[0]);
			}
		}

		SLT_REF(Dither) {
			constexpr f32 THRESHOLDS[16] = {1.0f / 17.0f, 9.0f / 17.0f, 3.0f / 17.0f, 11.0f / 17.0f, 13.0f / 17.0f, 5.0f / 17.0f,
											15.0f / 17.0f, 7.0f / 17.0f, 4.0f / 17.0f, 12.0f / 17.0f, 2.0f / 17.0f, 10.0f / 17.0f,
											16.0f / 17.0f, 8.0f / 17.0f, 14.0f / 17.0f, 6.0f / 17.0f};
			const f32 u = c.In(1).c[0] * c.In(2).c[0];
			const f32 v = c.In(1).c[1] * c.In(2).c[1];
			const u32 index = (static_cast<u32>(u) % 4u) * 4u + static_cast<u32>(v) % 4u;
			out[0] = Map(c.In(0), [&](f32 x) { return x - THRESHOLDS[index]; });
		}

		SLT_REF(FadeTransition) {
			const f32 contrast = c.In(2).c[0];
			out[0] = Scalar(Saturate(c.In(1).c[0] * (contrast + 1.0f) + (c.In(0).c[0] - 1.0f) * contrast));
		}

		SLT_REF(ChannelMask) {
			const RefValue& in = c.In(0);
			u32 mask = variant;
			out[0] = Of(in.n);
			if (mask == 0) {
				return;
			}
			if (mask >= (1u << in.n) || mask == 0xf || in.n == 1) {
				out[0] = in;
				return;
			}
			for (u32 i = 0; i < in.n; ++i) {
				out[0].c[i] = (mask & (1u << i)) != 0 ? in.c[i] : 0.0f;
			}
		}

		SLT_REF(ColorMask) {
			const f32 distance = Length3(Sub3(Three(c.In(1)), Three(c.In(0))));
			out[0] = Scalar(Saturate(1.0f - (distance - c.In(2).c[0]) / std::max(c.In(3).c[0], 1e-5f)));
		}

		SLT_REF(NormalBlend) {
			const V3 a = Three(c.In(0));
			const V3 b = Three(c.In(1));
			if (variant == 1) {
				const V3 t = {a[0], a[1], a[2] + 1.0f};
				const V3 u = {-b[0], -b[1], b[2]};
				out[0] = FromThree(Sub3(Scale3(Scale3(t, 1.0f / t[2]), Dot3(t, u)), u));
				return;
			}
			out[0] = FromThree(SafeNormalize3({a[0] + b[0], a[1] + b[1], a[2] * b[2]}));
		}

		SLT_REF(NormalFromHeight) {
			const V3 position = Three(c.In(2, 0));
			const V3 dx = Sub3(Three(c.In(2, 1)), position);
			const V3 dy = Sub3(Three(c.In(2, 2)), position);
			const V3 normal = Three(c.In(3));
			const V3 crossX = Cross3(normal, dx);
			const V3 crossY = Cross3(dy, normal);
			const f32 d = Dot3(dx, crossY);
			const f32 surface = (d < 0.0f ? -1.0f : 1.0f) / std::max(0.000000000000001192093f, std::fabs(d));
			const f32 dHdx = c.In(0, 1).c[0] - c.In(0, 0).c[0];
			const f32 dHdy = c.In(0, 2).c[0] - c.In(0, 0).c[0];
			const V3 gradient = Scale3(Add3(Scale3(crossY, dHdx), Scale3(crossX, dHdy)), surface);
			const V3 world = SafeNormalize3(Sub3(normal, Scale3(gradient, c.In(1).c[0])));
			if (variant == 1) {
				out[0] = FromThree(world);
				return;
			}
			const V3 tangent = Three(c.In(4));
			const V3 bitangent = Three(c.In(5));
			const V3 col0 = Cross3(bitangent, normal);
			const V3 col1 = Cross3(normal, tangent);
			const V3 col2 = Cross3(tangent, bitangent);
			const f32 sign = Dot3(tangent, col0) < 0.0f ? -1.0f : 1.0f;
			out[0] = FromThree(Scale3({Dot3(world, col0), Dot3(world, col1), Dot3(world, col2)}, sign));
		}

		void NormalFromHeightPrepare(std::array<RefValue, REF_INPUTS>& inputs, u32) {
			V3 normal = Normalize3(Three(inputs[3]));
			V3 tangent = Sub3(Three(inputs[4]), Scale3(normal, Dot3(normal, Three(inputs[4]))));
			if (Length3(tangent) < 1e-3f) {
				const V3 axis = std::fabs(normal[0]) < 0.9f ? V3{1, 0, 0} : V3{0, 1, 0};
				tangent = Sub3(axis, Scale3(normal, Dot3(normal, axis)));
			}
			tangent = Normalize3(tangent);
			V3 bitangent = Cross3(normal, tangent);
			if (inputs[5].c[0] < 0.0f) {
				bitangent = Scale3(bitangent, -1.0f);
			}
			inputs[3] = FromThree(normal);
			inputs[4] = FromThree(tangent);
			inputs[5] = FromThree(bitangent);
		}

		SLT_REF(NormalReconstructZ) {
			const f32 x = c.In(0).c[0];
			const f32 y = c.In(0).c[1];
			out[0] = FromThree(Normalize3({x, y, std::sqrt(1.0f - Saturate(x * x + y * y))}));
		}

		SLT_REF(NormalStrength) {
			const V3 in = Three(c.In(0));
			const f32 s = c.In(1).c[0];
			out[0] = FromThree({in[0] * s, in[1] * s, Lerp(1.0f, in[2], Saturate(s))});
		}

		SLT_REF(NormalUnpack) {
			const RefValue& in = c.In(0);
			if (variant == 1) {
				out[0] = FromThree({in.c[0] * 2.0f - 1.0f, in.c[1] * 2.0f - 1.0f, in.c[2] * 2.0f - 1.0f});
				return;
			}
			const f32 x = in.c[0] * 2.0f - 1.0f;
			const f32 y = in.c[1] * 2.0f - 1.0f;
			out[0] = FromThree({x, y, std::max(1.0e-16f, std::sqrt(1.0f - Saturate(x * x + y * y)))});
		}

		SLT_REF(ColorspaceConversion) {
			const u32 from = variant / 3;
			const u32 to = variant % 3;
			const V3 in = Three(c.In(0));
			V3 result = in;
			if (from == 0 && to == 1) {
				result = ToLinear(in);
			} else if (from == 0 && to == 2) {
				result = Hsv(in, 1e-10f, true);
			} else if (from == 1 && to == 0) {
				result = ToSrgb(in);
			} else if (from == 1 && to == 2) {
				result = Hsv(ToSrgb(in), 1e-10f, false);
			} else if (from == 2 && to == 0) {
				result = Rgb(in);
			} else if (from == 2 && to == 1) {
				result = ToLinear(Rgb(in));
			}
			out[0] = FromThree(result);
		}

		constexpr RefShape ARTISTIC_3[] = {{{P::F3}}};
		constexpr RefShape ARTISTIC_3_1[] = {{{P::F3, P::F1}}};
		constexpr RefShape ARTISTIC_3_1_1[] = {{{P::F3, P::F1, P::F1}}};
		constexpr RefShape REPLACE_COLOR[] = {{{P::F3, P::F3, P::F3, P::F1, P::F1}}};
		constexpr RefShape COLOR_MASK[] = {{{P::F3, P::F3, P::F1, P::F1}}};
		constexpr RefShape BLEND_SHAPES[] = {{{P::F4, P::F4, P::F1}}, {{P::F1, P::F1, P::F1}}};
		constexpr RefShape DITHER_SHAPES[] = {{{P::F4, P::F2}}, {{P::F1, P::F2}}};
		constexpr RefShape FADE_SHAPES[] = {{{P::F1, P::F1, P::F1}}};
		constexpr RefShape VECTOR_WIDTHS[] = {{{P::F4}}, {{P::F3}}, {{P::F2}}, {{P::F1}}};
		constexpr RefShape NORMAL_PAIR[] = {{{P::F3, P::F3}}};
		constexpr RefShape NORMAL_FROM_HEIGHT[] = {{{P::F1, P::F1}}};
		constexpr RefShape ONE_VECTOR2[] = {{{P::F2}}};
		constexpr RefShape ONE_VECTOR4[] = {{{P::F4}}};

		constexpr RefVariant CHANNEL_MIXES[] = {{"", 0}, {"Red=[0.5,0.25,-0.5,0];Green=[0,2,0.5,0];Blue=[-1,0.25,1.5,0]", 1}};
		constexpr RefVariant HUE_RANGES[] = {{"Range=\"Degrees\"", 0}, {"Range=\"Normalized\"", 1}};
		constexpr RefVariant INVERTS[] = {{"", 0b0000}, {"Red=true;Blue=true", 0b0101}, {"Red=true;Green=true;Blue=true;Alpha=true", 0b1111}};
		constexpr RefVariant BLEND_CASES[] = {
			{"Mode=\"Burn\"", 0}, {"Mode=\"Darken\"", 1}, {"Mode=\"Difference\"", 2}, {"Mode=\"Dodge\"", 3}, {"Mode=\"Divide\"", 4}, {"Mode=\"Exclusion\"", 5}, {"Mode=\"HardLight\"", 6}, {"Mode=\"HardMix\"", 7}, {"Mode=\"Lighten\"", 8}, {"Mode=\"LinearBurn\"", 9}, {"Mode=\"LinearDodge\"", 10}, {"Mode=\"LinearLight\"", 11}, {"Mode=\"LinearLightAddSub\"", 12}, {"Mode=\"Multiply\"", 13}, {"Mode=\"Negation\"", 14}, {"", 15}, {"Mode=\"PinLight\"", 16}, {"Mode=\"Screen\"", 17}, {"Mode=\"SoftLight\"", 18}, {"Mode=\"Subtract\"", 19}, {"Mode=\"VividLight\"", 20}, {"Mode=\"Overwrite\"", 21}};
		constexpr RefVariant CHANNEL_MASKS[] = {{"", 0xf},
												{"Red=false;Green=false;Blue=false;Alpha=false", 0},
												{"Green=false;Alpha=false", 0b0101},
												{"Red=false;Green=false;Alpha=false", 0b0100},
												{"Blue=false;Alpha=false", 0b0011}};
		constexpr RefVariant NORMAL_BLEND_MODES[] = {{"Mode=\"Default\"", 0}, {"Mode=\"Reoriented\"", 1}};
		constexpr RefVariant OUTPUT_SPACES[] = {{"Output Space=\"Tangent\"", 0}, {"Output Space=\"World\"", 1}};
		constexpr RefVariant UNPACK_SPACES[] = {{"Space=\"Tangent\"", 0}, {"Space=\"Object\"", 1}};
		constexpr RefVariant COLORSPACE_CASES[] = {
			{"From=\"RGB\";To=\"RGB\"", 0}, {"From=\"RGB\";To=\"Linear\"", 1}, {"From=\"RGB\";To=\"HSV\"", 2}, {"From=\"Linear\";To=\"RGB\"", 3}, {"From=\"Linear\";To=\"Linear\"", 4}, {"From=\"Linear\";To=\"HSV\"", 5}, {"From=\"HSV\";To=\"RGB\"", 6}, {"From=\"HSV\";To=\"Linear\"", 7}, {"From=\"HSV\";To=\"HSV\"", 8}};

		constexpr RefBinding DITHER_BINDINGS[] = {{"ScreenParams", P::F4, 2}};
		constexpr RefBinding NORMAL_FROM_HEIGHT_BINDINGS[] = {
			{"WorldSpacePosition", P::F3, 2}, {"WorldSpaceNormal", P::F3, 3}, {"WorldSpaceTangent", P::F3, 4}, {"WorldSpaceBiTangent", P::F3, 5}};

		f32 Truth(bool value) { return value ? 1.0f : 0.0f; }
		bool Set(f32 value) { return value != 0.0f; }

		SLT_REF(AllNode) {
			bool all = true;
			for (u32 i = 0; i < c.In(0).n; ++i) {
				all = all && Set(c.In(0).c[i]);
			}
			out[0] = Scalar(Truth(all));
		}
		SLT_REF(AnyNode) {
			bool any = false;
			for (u32 i = 0; i < c.In(0).n; ++i) {
				any = any || Set(c.In(0).c[i]);
			}
			out[0] = Scalar(Truth(any));
		}
		SLT_REF(AndNode) { out[0] = Scalar(Truth(Set(c.In(0)[0]) && Set(c.In(1)[0]))); }
		SLT_REF(OrNode) { out[0] = Scalar(Truth(Set(c.In(0)[0]) || Set(c.In(1)[0]))); }
		SLT_REF(NandNode) { out[0] = Scalar(Truth(!(Set(c.In(0)[0]) && Set(c.In(1)[0])))); }
		SLT_REF(NotNode) { out[0] = Scalar(Truth(!Set(c.In(0)[0]))); }
		SLT_REF(BranchNode) { out[0] = Set(c.In(0)[0]) ? c.In(1) : c.In(2); }
		SLT_REF(ComparisonNode) {
			const f32 a = c.In(0)[0];
			const f32 b = c.In(1)[0];
			const bool results[] = {a == b, a != b, a<b, a <= b, a> b, a >= b};
			out[0] = Scalar(Truth(results[variant]));
		}
		SLT_REF(IsNaNNode) { out[0] = Scalar(Truth(std::isnan(c.In(0)[0]))); }
		SLT_REF(IsInfiniteNode) { out[0] = Scalar(Truth(std::isinf(c.In(0)[0]))); }
		SLT_REF(PreviewNode) { out[0] = c.In(0); }

		void ZeroPrepare(std::array<RefValue, REF_INPUTS>& inputs, u32 index) {
			RefValue& value = inputs[0];
			for (u32 j = 0; j < value.n; ++j) {
				if (index % 4 == 1 || (index % 2 == 0 && j % 2 == 1)) {
					value.c[j] = 0.0f;
				}
			}
		}

		void ComparisonPrepare(std::array<RefValue, REF_INPUTS>& inputs, u32 index) {
			if (index % 2 == 1) {
				inputs[1].c[0] = inputs[0].c[0];
			}
		}

		void SpecialPrepare(std::array<RefValue, REF_INPUTS>& inputs, u32 index) {
			constexpr f32 SPECIAL[] = {std::numeric_limits<f32>::quiet_NaN(), std::numeric_limits<f32>::infinity(),
									   -std::numeric_limits<f32>::infinity()};
			if (index % 4 != 0) {
				inputs[0].c[0] = SPECIAL[index % 4 - 1];
			}
		}

		constexpr RefShape ONE_BOOL[] = {{{P::B1}}};
		constexpr RefShape TWO_BOOLS[] = {{{P::B1, P::B1}}};
		constexpr RefShape BRANCH[] = {{{P::B1, P::F4, P::F4}}, {{P::B1, P::F1, P::F1}}};
		constexpr RefVariant COMPARISONS[] = {{"Comparison Type=\"Equal\"", 0}, {"Comparison Type=\"NotEqual\"", 1}, {"Comparison Type=\"Less\"", 2}, {"Comparison Type=\"LessOrEqual\"", 3}, {"Comparison Type=\"Greater\"", 4}, {"Comparison Type=\"GreaterOrEqual\"", 5}};

#undef SLT_REF

		using D = std::array<f32, 2>;
		constexpr D ANY = {-4.0f, 4.0f};
		constexpr D POSITIVE = {0.1f, 8.0f};
		constexpr D UNIT = {-0.95f, 0.95f};
		constexpr D ANGLE = {-3.1f, 3.1f};
		constexpr D NONZERO = {0.5f, 4.0f};
		constexpr D UV = {0.05f, 0.95f};

		constexpr f32 SIN_ABSOLUTE = 1.0f / 2048.0f;
		constexpr f32 ULP_4096 = 4096.0f / 8388608.0f;

		const RefEntry ENTRIES[] = {
			{"Math/Add", Add, VECTOR_2, ONE, {ANY, ANY}},
			{"Math/Subtract", Subtract, VECTOR_2, ONE, {ANY, ANY}},
			{"Math/Multiply", Multiply, MULTIPLY, ONE, {ANY, ANY}, 1e-5f, 1e-5f},
			{"Math/Divide", Divide, VECTOR_2, ONE, {ANY, NONZERO}, 1e-6f, 1e-6f},
			{"Math/Power", Power, VECTOR_2, ONE, {POSITIVE, D{-2.0f, 2.0f}}, 1e-5f, 1e-4f},
			{"Math/SquareRoot", SquareRoot, VECTOR_1, ONE, {POSITIVE}, 1e-6f, 1e-6f},

			{"Math/Absolute", Absolute, VECTOR_1, ONE, {ANY}},
			{"Math/Exponential", Exponential, VECTOR_1, EXPONENTIAL, {ANY}, 1e-6f, 2e-6f},
			{"Math/Length", Length, VECTOR_1, ONE, {ANY}, 1e-6f, 1e-6f},
			{"Math/Log", Log, VECTOR_1, LOG, {POSITIVE}, 2e-6f, 2e-6f},
			{"Math/Modulo", Modulo, VECTOR_2, ONE, {D{-8.0f, 8.0f}, NONZERO}, 1e-5f, 1e-6f},
			{"Math/Negate", Negate, VECTOR_1, ONE, {ANY}},
			{"Math/Normalize", NormalizeNode, VECTOR_1, ONE, {ANY}, 1e-6f, 1e-6f},
			{"Math/Posterize", Posterize, VECTOR_2, ONE, {D{0.0f, 1.0f}, D{1.0f, 8.0f}}, 1e-5f, 1e-6f},
			{"Math/Reciprocal", Reciprocal, VECTOR_1, RECIPROCAL, {NONZERO}, 1e-6f, 1e-6f},
			{"Math/ReciprocalSquareRoot", ReciprocalSquareRoot, VECTOR_1, ONE, {POSITIVE}, 1e-6f, 1e-6f},

			{"Math/Ceiling", Ceiling, VECTOR_1, ONE, {ANY}},
			{"Math/Floor", Floor, VECTOR_1, ONE, {ANY}},
			{"Math/Round", Round, VECTOR_1, ONE, {ANY}},
			{"Math/Sign", Sign, VECTOR_1, ONE, {ANY}},
			{"Math/Step", Step, VECTOR_2, ONE, {ANY, ANY}},
			{"Math/Truncate", Truncate, VECTOR_1, ONE, {ANY}},

			{"Math/InverseLerp", InverseLerp, VECTOR_3, ONE, {D{-2.0f, -0.5f}, D{0.5f, 2.0f}, D{-3.0f, 3.0f}}, 1e-6f, 1e-5f},
			{"Math/Lerp", LerpNode, VECTOR_3, ONE, {ANY, ANY, D{-0.5f, 1.5f}}, 2e-6f, 1e-6f},
			{"Math/Smoothstep", Smoothstep, VECTOR_3, ONE, {D{-1.0f, -0.2f}, D{0.2f, 1.0f}, D{-1.5f, 1.5f}}, 2e-6f, 1e-6f},

			{"Math/Clamp", Clamp, VECTOR_3, ONE, {ANY, D{-1.0f, 0.0f}, D{0.1f, 1.0f}}},
			{"Math/Fraction", Fraction, VECTOR_1, ONE, {ANY}},
			{"Math/Maximum", Maximum, VECTOR_2, ONE, {ANY, ANY}},
			{"Math/Minimum", Minimum, VECTOR_2, ONE, {ANY, ANY}},
			{"Math/OneMinus", OneMinus, VECTOR_1, ONE, {ANY}},
			{"Math/RandomRange", RandomRange, RANDOM_RANGE, ONE, {ANY, D{-2.0f, 0.0f}, D{0.5f, 3.0f}}, 0, 0, false, RefCheck::Range, RandomRangeBounds},
			{"Math/Remap", Remap, REMAP, ONE, {ANY, D{-1.0f, 1.0f}, ANY}, 1e-5f, 1e-5f, false, RefCheck::Value, nullptr, nullptr, RemapPrepare},
			{"Math/Saturate", SaturateNode, VECTOR_1, ONE, {D{-2.0f, 2.0f}}},

			{"Math/Arccosine", Arccosine, VECTOR_1, ONE, {UNIT}, 1e-6f, ULP_4096},
			{"Math/Arcsine", Arcsine, VECTOR_1, ONE, {UNIT}, 1e-6f, ULP_4096},
			{"Math/Arctangent", Arctangent, VECTOR_1, ONE, {ANY}, 1e-6f, ULP_4096},
			{"Math/Arctangent2", Arctangent2, VECTOR_2, ONE, {ANY, ANY}, 1e-6f, ULP_4096},
			{"Math/Cosine", Cosine, VECTOR_1, ONE, {ANGLE}, SIN_ABSOLUTE, 0},
			{"Math/DegreesToRadians", DegreesToRadians, VECTOR_1, ONE, {D{-360.0f, 360.0f}}},
			{"Math/HyperbolicCosine", HyperbolicCosine, VECTOR_1, ONE, {D{-3.0f, 3.0f}}, 1e-5f, 1e-5f},
			{"Math/HyperbolicSine", HyperbolicSine, VECTOR_1, ONE, {D{-3.0f, 3.0f}}, 1e-5f, 1e-5f},
			{"Math/HyperbolicTangent", HyperbolicTangent, VECTOR_1, ONE, {D{-3.0f, 3.0f}}, 1e-5f, 1e-5f},
			{"Math/RadiansToDegrees", RadiansToDegrees, VECTOR_1, ONE, {ANGLE}, 1e-6f, 1e-6f},
			{"Math/Sine", Sine, VECTOR_1, ONE, {ANGLE}, SIN_ABSOLUTE, 0},
			{"Math/Tangent", Tangent, VECTOR_1, ONE, {D{-1.2f, 1.2f}}, 8.0f * SIN_ABSOLUTE, 0},

			{"Math/NoiseSineWave", NoiseSineWave, NOISE_SINE, ONE, {ANGLE, D{-1.0f, 1.0f}}, 0, 0, false, RefCheck::Range, NoiseSineWaveBounds},
			{"Math/SawtoothWave", SawtoothWave, VECTOR_1, ONE, {ANY}, 1e-6f, 1e-6f},
			{"Math/SquareWave", SquareWave, VECTOR_1, ONE, {ANY}},
			{"Math/TriangleWave", TriangleWave, VECTOR_1, ONE, {ANY}, 1e-6f, 1e-6f},

			{"Math/DDX", Ddx, VECTOR_1, ONE, {ANY}, 1e-6f, 1e-6f, true},
			{"Math/DDXY", Ddxy, VECTOR_1, ONE, {ANY}, 1e-6f, 1e-6f, true},
			{"Math/DDY", Ddy, VECTOR_1, ONE, {ANY}, 1e-6f, 1e-6f, true},

			{"Math/MatrixConstruction", MatrixConstruction, MATRIX_CONSTRUCTION, AXIS, {ANY, ANY, ANY, ANY}},
			{"Math/MatrixDeterminant", MatrixDeterminant, MATRICES, ONE, {D{-2.0f, 2.0f}}, 1e-4f, 1e-5f},
			{"Math/MatrixSplit", MatrixSplit, MATRICES, AXIS, {ANY}},
			{"Math/MatrixTranspose", MatrixTranspose, MATRICES, ONE, {ANY}},

			{"Math/CrossProduct", CrossProduct, CROSS, ONE, {ANY, ANY}, 1e-5f, 1e-6f},
			{"Math/Distance", Distance, VECTOR_PAIR, ONE, {ANY, ANY}, 1e-6f, 1e-6f},
			{"Math/DotProduct", DotProduct, VECTOR_PAIR, ONE, {ANY, ANY}, 1e-5f, 1e-6f},
			{"Math/FresnelEffect", FresnelEffect, FRESNEL, ONE, {NONZERO, UNIT, D{0.5f, 4.0f}}, 1e-5f, 1e-4f},
			{"Math/Projection", ProjectionNode, VECTOR_PAIR, ONE, {ANY, NONZERO}, 1e-5f, 1e-5f},
			{"Math/Reflection", Reflection, VECTOR_PAIR, ONE, {ANY, ANY}, 1e-5f, 1e-5f},
			{"Math/Refract", Refract, REFRACT, REFRACT_MODES, {UNIT, UNIT, D{0.8f, 2.0f}, D{0.8f, 2.0f}}, 1e-4f, 1e-4f},
			{"Math/Rejection", Rejection, VECTOR_PAIR, ONE, {ANY, NONZERO}, 1e-5f, 1e-5f},
			{"Math/RotateAboutAxis", RotateAboutAxis, ROTATE, UNITS, {ANY, NONZERO, ANGLE}, 16.0f * SIN_ABSOLUTE, 0},
			{"Math/SphereMask", SphereMask, SPHERE_MASK, ONE, {ANY, ANY, D{0.5f, 3.0f}, D{0.0f, 0.9f}}, 1e-5f, 1e-5f},

			{"Channel/Combine", Combine, COMBINE, ONE, {ANY, ANY, ANY, ANY}},
			{"Channel/Flip", Flip, FLIP, FLIPS, {ANY}},
			{"Channel/Split", Split, SPLIT, ONE, {ANY}},
			{"Channel/Swizzle", Swizzle, SWIZZLE, MASKS, {ANY}},

			{"Procedural/Checkerboard", Checkerboard, CHECKERBOARD, ONE, {UV, D{0.0f, 1.0f}, D{0.0f, 1.0f}, D{1.0f, 4.0f}}, 1e-3f, 1e-3f, true},
			{"Procedural/SimpleNoise", SimpleNoise, NOISE, DETERMINISTIC, {UV, D{2.0f, 60.0f}}, 1e-5f, 1e-5f},
			{"Procedural/SimpleNoise", SimpleNoise, NOISE, LEGACY_SINE, {UV, D{2.0f, 60.0f}}, 0, 0, false, RefCheck::Range, SimpleNoiseBounds},
			{"Procedural/GradientNoise", GradientNoiseNode, NOISE, DETERMINISTIC, {UV, D{2.0f, 30.0f}}, 1e-4f, 1e-4f},
			{"Procedural/GradientNoise", GradientNoiseNode, NOISE, LEGACY_MOD, {UV, D{2.0f, 30.0f}}, 1e-4f, 1e-4f},
			{"Procedural/Voronoi", VoronoiNode, VORONOI, DETERMINISTIC, {UV, D{0.5f, 3.0f}, D{2.0f, 8.0f}}, 8.0f * SIN_ABSOLUTE, 0, false, RefCheck::Value, nullptr, VoronoiDecidable},
			{"Procedural/Voronoi", VoronoiNode, VORONOI, LEGACY_SINE, {UV, D{0.5f, 3.0f}, D{2.0f, 8.0f}}, 0, 0, false, RefCheck::Range, VoronoiBounds},
			{"Procedural/Ellipse", Ellipse, SHAPE_2, ONE, {UV, D{0.2f, 0.9f}, D{0.2f, 0.9f}}, 1e-3f, 1e-3f, true},
			{"Procedural/Polygon", Polygon, SHAPE_3, ONE, {UV, D{3.0f, 8.0f}, D{0.2f, 0.9f}, D{0.2f, 0.9f}}, 64.0f * SIN_ABSOLUTE, 0, true},
			{"Procedural/Rectangle", Rectangle, SHAPE_2, CLAMP_TYPES, {UV, D{0.2f, 0.9f}, D{0.2f, 0.9f}}, 1e-3f, 1e-3f, true},
			{"Procedural/RoundedPolygon", RoundedPolygon, SHAPE_4, ONE, {UV, D{0.3f, 0.9f}, D{0.3f, 0.9f}, D{3.0f, 8.0f}, D{0.1f, 0.9f}}, 64.0f * SIN_ABSOLUTE, 0, true},
			{"Procedural/RoundedRectangle", RoundedRectangle, SHAPE_3, ONE, {UV, D{0.2f, 0.9f}, D{0.2f, 0.9f}, D{0.05f, 0.4f}}, 1e-3f, 1e-3f, true},

			{"Input/Boolean", BooleanNode, NO_INPUTS, BOOLEANS},
			{"Input/Color", ColorNode, NO_INPUTS, COLORS},
			{"Input/Constant", ConstantNode, NO_INPUTS, CONSTANTS},
			{"Input/Integer", IntegerNode, NO_INPUTS, INTEGERS},
			{"Input/Float", FloatNode, ONE_FLOAT, ONE, {ANY}},
			{"Input/Slider", SliderNode, NO_INPUTS, SLIDERS},
			{"Input/Vector2", VectorNode<2>, TWO_FLOATS, ONE, {ANY, ANY}},
			{"Input/Vector3", VectorNode<3>, THREE_FLOATS, ONE, {ANY, ANY, ANY}},
			{"Input/Vector4", VectorNode<4>, FOUR_FLOATS, ONE, {ANY, ANY, ANY, ANY}},
			{"Input/DielectricSpecular", DielectricSpecular, NO_INPUTS, DIELECTRICS, {}, 1e-6f, 1e-5f},
			{"Input/MetalReflectance", MetalReflectance, NO_INPUTS, METALS},

			{"Input/Time", TimeNode, NO_INPUTS, ONE, {ANY}, 1e-6f, 1e-6f, false, RefCheck::Value, nullptr, nullptr, nullptr, TIME_BINDINGS, 1u << 4},
			{"Input/Position", ReadsBinding, NO_INPUTS, POSITION_SPACES, {ANY, ANY, ANY, ANY, ANY}, 1e-6f, 1e-6f, false, RefCheck::Value, nullptr, nullptr, nullptr, POSITION_BINDINGS},
			{"Input/NormalVector", ReadsBinding, NO_INPUTS, DIRECTION_SPACES, {ANY, ANY, ANY, ANY}, 1e-6f, 1e-6f, false, RefCheck::Value, nullptr, nullptr, nullptr, NORMAL_BINDINGS},
			{"Input/TangentVector", ReadsBinding, NO_INPUTS, DIRECTION_SPACES, {ANY, ANY, ANY, ANY}, 1e-6f, 1e-6f, false, RefCheck::Value, nullptr, nullptr, nullptr, TANGENT_BINDINGS},
			{"Input/BitangentVector", ReadsBinding, NO_INPUTS, DIRECTION_SPACES, {ANY, ANY, ANY, ANY}, 1e-6f, 1e-6f, false, RefCheck::Value, nullptr, nullptr, nullptr, BITANGENT_BINDINGS},
			{"Input/ViewDirection", ReadsBinding, NO_INPUTS, DIRECTION_SPACES, {ANY, ANY, ANY, ANY}, 1e-6f, 1e-6f, false, RefCheck::Value, nullptr, nullptr, nullptr, VIEW_DIRECTION_BINDINGS},
			{"Input/ViewVector", ViewVectorNode, NO_INPUTS, DIRECTION_SPACES, {ANY, ANY, ANY, ANY, ANY, ANY, ANY, ANY, ANY}, 1e-4f, 1e-4f, false, RefCheck::Value, nullptr, nullptr, ViewVectorPrepare, VIEW_VECTOR_BINDINGS},
			{"Input/ScreenPosition", ScreenPositionNode, NO_INPUTS, SCREEN_MODES, {ANY, UV, ANY, ANY, ANY}, 1e-5f, 1e-5f, false, RefCheck::Value, nullptr, nullptr, ScreenPrepare, SCREEN_POSITION_BINDINGS},
			{"Input/InstanceID", ReadsOneBinding, NO_INPUTS, ONE, {ANY}, 1e-6f, 1e-6f, false, RefCheck::Value, nullptr, nullptr, nullptr, INSTANCE_BINDINGS},
			{"Input/Blackbody", Blackbody, ONE_FLOAT, ONE, {D{800.0f, 12000.0f}}, 1e-4f, 1e-4f},
			{"Input/SampleGradient", SampleGradient, NONE_THEN_FLOAT, GRADIENT_CASES, {ANY, D{-0.2f, 1.2f}}, 1e-4f, 1e-4f, false, RefCheck::Value, nullptr, nullptr, nullptr, {}, 0, WireGradient},
			{"Input/Matrix2x2", MatrixNode<2>, NO_INPUTS, MATRIX2_ROWS},
			{"Input/Matrix3x3", MatrixNode<3>, NO_INPUTS, MATRIX3_ROWS},
			{"Input/Matrix4x4", MatrixNode<4>, NO_INPUTS, MATRIX4_ROWS},
			{"Input/TransformationMatrix", ReadsBinding, NO_INPUTS, TRANSFORMATION_MATRICES, {ANY, ANY, ANY, ANY, ANY, ANY, ANY, ANY}, 1e-6f, 1e-6f, false, RefCheck::Value, nullptr, nullptr, nullptr, TRANSFORMATION_BINDINGS},
			{"Input/Camera", CameraNode, NO_INPUTS, ONE, {ANY, ANY, ANY, ANY}, 1e-6f, 1e-6f, false, RefCheck::Value, nullptr, nullptr, nullptr, CAMERA_BINDINGS},
			{"Input/Fog", FogNode, ONE_VECTOR3, ONE, {ANY}},
			{"Input/Object", ObjectNode, NO_INPUTS, ONE, {ANY}, 1e-5f, 1e-5f, false, RefCheck::Value, nullptr, nullptr, nullptr, OBJECT_BINDINGS, (1u << 2) | (1u << 3) | (1u << 4)},
			{"Input/Screen", ScreenNode, NO_INPUTS, ONE, {ANY}, 1e-6f, 1e-6f, false, RefCheck::Value, nullptr, nullptr, nullptr, SCREEN_BINDINGS},
			{"Input/EyeIndex", ReadsOneBinding, NO_INPUTS, ONE, {ANY}, 1e-6f, 1e-6f, false, RefCheck::Value, nullptr, nullptr, nullptr, EYE_BINDINGS},

			{"Math/Transform", TransformNode, ONE_VECTOR3, std::span<const RefVariant>(BuildTransformCases().variants.data(), BuildTransformCases().variants.size()), {UV, ANY, ANY, ANY, ANY, ANY, ANY, ANY, ANY, ANY, ANY, D{-0.5f, 0.5f}}, 1e-4f, 1e-4f, false, RefCheck::Value, nullptr, nullptr, TransformPrepare, TRANSFORM_BINDINGS},

			{"UV/Flipbook", Flipbook, FLIPBOOK, FLIPBOOK_INVERTS, {UV, D{1.0f, 8.0f}, D{1.0f, 8.0f}, D{0.0f, 64.0f}}, 1e-5f, 1e-5f, false, RefCheck::Value, nullptr, nullptr, FlipbookPrepare},
			{"UV/PolarCoordinates", PolarCoordinates, POLAR, ONE, {UV, UV, D{0.5f, 2.0f}, D{0.5f, 2.0f}}, 1e-5f, ULP_4096},
			{"UV/RadialShear", RadialShear, UV_FOUR_PAIRS, ONE, {UV, UV, ANY, ANY}, 1e-5f, 1e-5f},
			{"UV/Rotate", Rotate, UV_ROTATE, UNITS, {UV, UV, ANGLE}, 2.0f * SIN_ABSOLUTE, 0},
			{"UV/Spherize", Spherize, UV_FOUR_PAIRS, ONE, {UV, UV, ANY, ANY}, 1e-5f, 1e-5f},
			{"UV/TilingAndOffset", TilingAndOffset, UV_TILING, ONE, {UV, ANY, ANY}, 1e-6f, 1e-6f},
			{"UV/Twirl", Twirl, UV_TWIRL, ONE, {UV, UV, D{-4.0f, 4.0f}, ANY}, 4.0f * SIN_ABSOLUTE, 0},

			{"Artistic/ChannelMixer", ChannelMixer, ARTISTIC_3, CHANNEL_MIXES, {ANY}, 1e-5f, 1e-5f},
			{"Artistic/Contrast", Contrast, ARTISTIC_3_1, ONE, {D{0.0f, 1.0f}, D{0.0f, 3.0f}}, 1e-5f, 1e-5f},
			{"Artistic/Hue", Hue, ARTISTIC_3_1, HUE_RANGES, {D{0.0f, 1.0f}, D{-0.75f, 0.75f}}, 1e-4f, 1e-4f},
			{"Artistic/InvertColors", InvertColors, VECTOR_WIDTHS, INVERTS, {D{0.0f, 1.0f}}},
			{"Artistic/ReplaceColor", ReplaceColor, REPLACE_COLOR, ONE, {D{0.0f, 1.0f}, D{0.0f, 1.0f}, D{0.0f, 1.0f}, D{0.0f, 0.5f}, D{0.01f, 0.5f}}, 1e-5f, 1e-5f},
			{"Artistic/Saturation", Saturation, ARTISTIC_3_1, ONE, {D{0.0f, 1.0f}, D{0.0f, 2.0f}}, 1e-5f, 1e-5f},
			{"Artistic/WhiteBalance", WhiteBalance, ARTISTIC_3_1_1, ONE, {D{0.0f, 1.0f}, D{-1.0f, 1.0f}, D{-1.0f, 1.0f}}, 1e-4f, 1e-4f},
			{"Artistic/Blend", BlendNode, BLEND_SHAPES, BLEND_CASES, {D{0.05f, 1.0f}, D{0.0f, 1.0f}, D{0.0f, 1.0f}}, 1e-5f, 1e-5f},
			{"Artistic/Dither", Dither, DITHER_SHAPES, ONE, {ANY, UV, D{64.0f, 512.0f}}, 1e-6f, 1e-6f, false, RefCheck::Value, nullptr, nullptr, nullptr, DITHER_BINDINGS},
			{"Artistic/FadeTransition", FadeTransition, FADE_SHAPES, ONE, {D{0.0f, 1.0f}, D{0.0f, 1.0f}, D{0.0f, 4.0f}}, 1e-6f, 1e-6f},
			{"Artistic/ChannelMask", ChannelMask, VECTOR_WIDTHS, CHANNEL_MASKS, {ANY}},
			{"Artistic/ColorMask", ColorMask, COLOR_MASK, ONE, {D{0.0f, 1.0f}, D{0.0f, 1.0f}, D{0.0f, 0.5f}, D{0.01f, 0.5f}}, 1e-5f, 1e-5f},
			{"Artistic/NormalBlend", NormalBlend, NORMAL_PAIR, NORMAL_BLEND_MODES, {UNIT, UNIT}, 1e-5f, 1e-5f},
			{"Artistic/NormalFromHeight", NormalFromHeight, NORMAL_FROM_HEIGHT, OUTPUT_SPACES, {ANY, D{0.0f, 0.5f}, ANY, NONZERO, ANY, ANY}, 1e-4f, 1e-4f, true, RefCheck::Value, nullptr, nullptr, NormalFromHeightPrepare, NORMAL_FROM_HEIGHT_BINDINGS},
			{"Artistic/NormalReconstructZ", NormalReconstructZ, ONE_VECTOR2, ONE, {UNIT}, 1e-5f, 1e-5f},
			{"Artistic/NormalStrength", NormalStrength, ARTISTIC_3_1, ONE, {UNIT, D{-1.0f, 3.0f}}, 1e-6f, 1e-6f},
			{"Artistic/NormalUnpack", NormalUnpack, ONE_VECTOR4, UNPACK_SPACES, {D{0.0f, 1.0f}}, 1e-5f, 1e-5f},
			{"Artistic/ColorspaceConversion", ColorspaceConversion, ARTISTIC_3, COLORSPACE_CASES, {D{0.0f, 1.0f}}, 1e-4f, 1e-4f},

			{"Utility/All", AllNode, VECTOR_1, ONE, {D{-1.0f, 1.0f}}, 0, 0, false, RefCheck::Value, nullptr, nullptr, ZeroPrepare},
			{"Utility/And", AndNode, TWO_BOOLS, ONE, {}, 0, 0},
			{"Utility/Any", AnyNode, VECTOR_1, ONE, {D{-1.0f, 1.0f}}, 0, 0, false, RefCheck::Value, nullptr, nullptr, ZeroPrepare},
			{"Utility/Branch", BranchNode, BRANCH, ONE, {ANY, ANY, ANY}, 0, 0},
			{"Utility/Comparison", ComparisonNode, TWO_FLOATS, COMPARISONS, {D{-1.0f, 1.0f}, D{-1.0f, 1.0f}}, 0, 0, false, RefCheck::Value, nullptr, nullptr, ComparisonPrepare},
			{"Utility/IsFrontFace", ReadsOneBinding, NO_INPUTS, ONE, {}, 0, 0, false, RefCheck::Value, nullptr, nullptr, nullptr, FACE_BINDINGS},
			{"Utility/IsInfinite", IsInfiniteNode, ONE_FLOAT, ONE, {D{0.0f, 1.0f}}, 0, 0, false, RefCheck::Value, nullptr, nullptr, SpecialPrepare},
			{"Utility/IsNaN", IsNaNNode, ONE_FLOAT, ONE, {D{0.0f, 1.0f}}, 0, 0, false, RefCheck::Value, nullptr, nullptr, SpecialPrepare},
			{"Utility/Nand", NandNode, TWO_BOOLS, ONE, {}, 0, 0},
			{"Utility/Not", NotNode, ONE_BOOL, ONE, {}, 0, 0},
			{"Utility/Or", OrNode, TWO_BOOLS, ONE, {}, 0, 0},
			{"Utility/Preview", PreviewNode, VECTOR_1, ONE, {ANY}, 0, 0},
		};

	} // anonymous namespace

	u32 FeedComponents(Feed feed) {
		switch (feed) {
		case Feed::F1:
			return 1;
		case Feed::F2:
			return 2;
		case Feed::F3:
			return 3;
		case Feed::F4:
			return 4;
		case Feed::M2:
			return 4;
		case Feed::M3:
			return 9;
		case Feed::M4:
			return 16;
		case Feed::B1:
			return 1;
		case Feed::None:
			break;
		}
		return 0;
	}

	std::span<const RefEntry> ReferenceTable() { return ENTRIES; }

} // namespace Shalltear::NodeShaderTest
