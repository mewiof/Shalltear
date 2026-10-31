#include "GraphNodeLibrary.h"

namespace Shalltear {

	namespace {

		using P = GraphPortType;
		using B = GraphProgramBuilder;

		constexpr std::string_view UV0 = "UV0";

		constexpr double PI = 3.14159265358979323846;
		constexpr double HALF_PI = 1.57079632679489661923;

		constexpr GraphPortDefinition CHECKERBOARD_INPUTS[] = {{"UV", P::Float2, {}, UV0},
															   {"Color A", P::Float3, {0.2f, 0.2f, 0.2f, 1}},
															   {"Color B", P::Float3, {0.7f, 0.7f, 0.7f, 1}},
															   {"Frequency", P::Float2, {1, 1, 1, 1}}};
		constexpr GraphPortDefinition CHECKERBOARD_OUTPUTS[] = {{"Out", P::Float3}};

		void CheckerboardBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId uv = b.Multiply(b.Add(c.In(0), b.Scalar(0.5f)), c.In(3));
			GraphValueId wrapped = b.Subtract(b.Unary(GraphOp::Fraction, b.Add(uv, b.Scalar(0.25f))), b.Scalar(0.5f));
			GraphValueId distance = b.Subtract(b.Multiply(b.Scalar(4.0f), b.Unary(GraphOp::Absolute, wrapped)), b.Scalar(1.0f));
			const GraphValueId halves[] = {b.Unary(GraphOp::DerivativeX, uv), b.Unary(GraphOp::DerivativeY, uv)};
			GraphValueId derivatives = b.Construct(GraphValueType::Float4, halves);
			GraphValueId xz = b.Swizzle(derivatives, "xz");
			GraphValueId yw = b.Swizzle(derivatives, "yw");
			const GraphValueId lengths[] = {b.Dot(xz, xz), b.Dot(yw, yw)};
			GraphValueId duvLength = b.Unary(GraphOp::SquareRoot, b.Construct(GraphValueType::Float2, lengths));
			GraphValueId scale = b.Divide(b.Scalar(0.35f), duvLength);
			GraphValueId widest = b.Maximum(b.Swizzle(duvLength, "x"), b.Swizzle(duvLength, "y"));
			GraphValueId limiter =
				b.Unary(GraphOp::SquareRoot, b.Clamp(b.Subtract(b.Scalar(1.1f), widest), b.Scalar(0.0f), b.Scalar(1.0f)));
			GraphValueId alphas = b.Clamp(b.Multiply(distance, scale), b.Scalar(-1.0f), b.Scalar(1.0f));
			GraphValueId product = b.Multiply(b.Multiply(b.Multiply(b.Scalar(0.5f), b.Swizzle(alphas, "x")), b.Swizzle(alphas, "y")), limiter);
			GraphValueId alpha = b.Unary(GraphOp::Saturate, b.Add(b.Scalar(0.5f), product));
			c.outputs[0] = b.Lerp(c.In(1), c.In(2), b.Swizzle(alpha, "xxx"));
		}

		constexpr std::string_view HASHES_SINE[] = {"Deterministic", "LegacySine"};
		constexpr std::string_view HASHES_MOD[] = {"Deterministic", "LegacyMod"};
		constexpr GraphSettingDefinition SINE_HASH_SETTINGS[] = {{"Hash Type", GraphSettingKind::Choice, HASHES_SINE}};
		constexpr GraphSettingDefinition MOD_HASH_SETTINGS[] = {{"Hash Type", GraphSettingKind::Choice, HASHES_MOD}};

		constexpr GraphPortDefinition SIMPLE_NOISE_INPUTS[] = {{"UV", P::Float2, {}, UV0}, {"Scale", P::Float, {500, 0, 0, 0}}};
		constexpr GraphPortDefinition GRADIENT_NOISE_INPUTS[] = {{"UV", P::Float2, {}, UV0}, {"Scale", P::Float, {10, 0, 0, 0}}};
		constexpr GraphPortDefinition VORONOI_INPUTS[] = {
			{"UV", P::Float2, {}, UV0}, {"AngleOffset", P::Float, {2, 0, 0, 0}}, {"CellDensity", P::Float, {5, 0, 0, 0}}};
		constexpr GraphPortDefinition NOISE_OUTPUTS[] = {{"Out", P::Float}};
		constexpr GraphPortDefinition VORONOI_OUTPUTS[] = {{"Out", P::Float}, {"Cells", P::Float}};

		void SimpleNoiseBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphNoiseHash hash = c.Setting(0) == 1 ? GraphNoiseHash::LegacySine : GraphNoiseHash::Deterministic;
			GraphValueId sum = GRAPH_NO_VALUE;
			for (u32 octave = 0; octave < 3; ++octave) {
				f32 frequency = static_cast<f32>(1u << octave);
				f32 amplitude = 0.125f * static_cast<f32>(1u << octave);
				const GraphValueId uv[] = {b.Multiply(c.In(0), b.Divide(c.In(1), b.Scalar(frequency)))};
				GraphValueId term = b.Multiply(b.Noise(GraphOp::ValueNoise, hash, uv), b.Scalar(amplitude));
				sum = sum == GRAPH_NO_VALUE ? term : b.Add(sum, term);
			}
			c.outputs[0] = sum;
		}

		void GradientNoiseBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphNoiseHash hash = c.Setting(0) == 1 ? GraphNoiseHash::LegacyMod : GraphNoiseHash::Deterministic;
			const GraphValueId p[] = {b.Multiply(c.In(0), c.In(1))};
			c.outputs[0] = b.Noise(GraphOp::GradientNoise, hash, p);
		}

		void VoronoiBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphNoiseHash hash = c.Setting(0) == 1 ? GraphNoiseHash::LegacySine : GraphNoiseHash::Deterministic;
			const GraphValueId operands[] = {c.In(0), c.In(1), c.In(2)};
			GraphValueId cell = b.Noise(GraphOp::Voronoi, hash, operands);
			c.outputs[0] = b.Swizzle(cell, "x");
			c.outputs[1] = b.Swizzle(cell, "y");
		}

		constexpr GraphPortDefinition ELLIPSE_INPUTS[] = {
			{"UV", P::Float2, {}, UV0}, {"Width", P::Float, {0.5f, 0, 0, 0}}, {"Height", P::Float, {0.5f, 0, 0, 0}}};
		constexpr GraphPortDefinition POLYGON_INPUTS[] = {
			{"UV", P::Float2, {}, UV0}, {"Sides", P::Float, {6, 0, 0, 0}}, {"Width", P::Float, {0.5f, 0, 0, 0}}, {"Height", P::Float, {0.5f, 0, 0, 0}}};
		constexpr GraphPortDefinition ROUNDED_POLYGON_INPUTS[] = {{"UV", P::Float2, {}, UV0},
																  {"Width", P::Float, {0.5f, 0, 0, 0}},
																  {"Height", P::Float, {0.5f, 0, 0, 0}},
																  {"Sides", P::Float, {5, 0, 0, 0}},
																  {"Roundness", P::Float, {0.3f, 0, 0, 0}}};
		constexpr GraphPortDefinition ROUNDED_RECTANGLE_INPUTS[] = {
			{"UV", P::Float2, {}, UV0}, {"Width", P::Float, {0.5f, 0, 0, 0}}, {"Height", P::Float, {0.5f, 0, 0, 0}}, {"Radius", P::Float, {0.1f, 0, 0, 0}}};
		constexpr GraphPortDefinition SHAPE_OUTPUTS[] = {{"Out", P::Float}};
		constexpr GraphPortDefinition POLYGON_OUTPUTS[] = {{"Out", P::DynamicVector}};
		constexpr std::string_view CLAMP_TYPES[] = {"Fastest", "Nicest"};
		constexpr GraphSettingDefinition RECTANGLE_SETTINGS[] = {{"Clamp Type", GraphSettingKind::Choice, CLAMP_TYPES}};

		GraphValueId Centered(B& b, GraphValueId uv) { return b.Subtract(b.Multiply(uv, b.Scalar(2.0f)), b.Scalar(1.0f)); }

		GraphValueId Edge(B& b, GraphValueId d) {
			return b.Unary(GraphOp::Saturate, b.Divide(b.Subtract(b.Scalar(1.0f), d), b.Unary(GraphOp::DerivativeWidth, d)));
		}

		GraphValueId Pair(B& b, GraphValueId x, GraphValueId y) {
			const GraphValueId parts[] = {x, y};
			return b.Construct(GraphValueType::Float2, parts);
		}

		void EllipseBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId d = b.Unary(GraphOp::Length, b.Divide(Centered(b, c.In(0)), Pair(b, c.In(1), c.In(2))));
			c.outputs[0] = Edge(b, d);
		}

		void PolygonBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId pi = b.Scalar(static_cast<f32>(PI));
			GraphValueId sides = c.In(1);
			GraphValueId fit = b.Unary(GraphOp::Cosine, b.Divide(pi, sides));
			GraphValueId uv = b.Divide(Centered(b, c.In(0)), Pair(b, b.Multiply(c.In(2), fit), b.Multiply(c.In(3), fit)));
			uv = Pair(b, b.Swizzle(uv, "x"), b.Unary(GraphOp::Negate, b.Swizzle(uv, "y")));
			GraphValueId angle = b.Binary(GraphOp::Arctangent2, b.Swizzle(uv, "x"), b.Swizzle(uv, "y"));
			GraphValueId r = b.Divide(b.Multiply(b.Scalar(2.0f), pi), sides);
			GraphValueId sector = b.Multiply(b.Unary(GraphOp::Floor, b.Add(b.Scalar(0.5f), b.Divide(angle, r))), r);
			GraphValueId distance = b.Multiply(b.Unary(GraphOp::Cosine, b.Subtract(sector, angle)), b.Unary(GraphOp::Length, uv));
			c.outputs[0] = Edge(b, distance);
		}

		void RectangleBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId size = Pair(b, c.In(1), c.In(2));
			if (c.Setting(0) == 0) {
				GraphValueId d = b.Subtract(b.Unary(GraphOp::Absolute, Centered(b, c.In(0))), size);
				d = b.Unary(GraphOp::Saturate, b.Subtract(b.Scalar(1.0f), b.Divide(d, b.Unary(GraphOp::DerivativeWidth, d))));
				c.outputs[0] = b.Minimum(b.Swizzle(d, "x"), b.Swizzle(d, "y"));
				return;
			}
			GraphValueId uv = b.Subtract(b.Multiply(c.In(0), b.Scalar(2.0f)), b.Scalar(1.0f));
			GraphValueId f = b.Minimum(b.Unary(GraphOp::DerivativeWidth, uv), b.Scalar(0.5f));
			GraphValueId k = b.Divide(b.Scalar(1.0f), f);
			GraphValueId o = b.Unary(GraphOp::Saturate, b.Add(b.Scalar(0.5f), b.Multiply(k, b.Subtract(size, b.Unary(GraphOp::Absolute, uv)))));
			o = b.Minimum(o, b.Multiply(b.Multiply(k, size), b.Scalar(2.0f)));
			c.outputs[0] = b.Multiply(b.Swizzle(o, "x"), b.Swizzle(o, "y"));
		}

		void RoundedRectangleBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId radius = b.Unary(GraphOp::Absolute, b.Multiply(c.In(3), b.Scalar(2.0f)));
			radius = b.Minimum(b.Minimum(radius, b.Unary(GraphOp::Absolute, c.In(1))), b.Unary(GraphOp::Absolute, c.In(2)));
			radius = b.Maximum(radius, b.Scalar(1e-5f));
			GraphValueId uv = b.Add(b.Subtract(b.Unary(GraphOp::Absolute, Centered(b, c.In(0))), Pair(b, c.In(1), c.In(2))), radius);
			GraphValueId d = b.Divide(b.Unary(GraphOp::Length, b.Maximum(b.Scalar(0.0f), uv)), radius);
			GraphValueId width = b.Maximum(b.Unary(GraphOp::DerivativeWidth, d), b.Scalar(1e-5f));
			c.outputs[0] = b.Unary(GraphOp::Saturate, b.Divide(b.Subtract(b.Scalar(1.0f), d), width));
		}

		void RoundedPolygonBody(GraphBodyContext& c) {
			B& b = c.builder;
			GraphValueId one = b.Scalar(1.0f);
			GraphValueId zero = b.Scalar(0.0f);
			GraphValueId epsilon = b.Scalar(1e-6f);
			const f32 minusOne[] = {-1.0f, -1.0f};
			GraphValueId uv = b.Add(b.Multiply(c.In(0), b.Scalar(2.0f)), b.Constant(GraphValueType::Float2, minusOne));
			auto guarded = [&](GraphValueId size) {
				return b.Add(size, b.Multiply(b.Unary(GraphOp::BoolToFloat, b.Binary(GraphOp::Equal, size, zero)), epsilon));
			};
			uv = Pair(b, b.Divide(b.Swizzle(uv, "x"), guarded(c.In(1))), b.Divide(b.Swizzle(uv, "y"), guarded(c.In(2))));
			GraphValueId roundness = b.Clamp(c.In(4), epsilon, one);
			GraphValueId sides = b.Unary(GraphOp::Floor, b.Unary(GraphOp::Absolute, c.In(3)));
			GraphValueId fullAngle = b.Divide(b.Scalar(static_cast<f32>(2.0 * PI)), sides);
			GraphValueId halfAngle = b.Divide(fullAngle, b.Scalar(2.0f));
			GraphValueId diagonal = b.Divide(one, b.Unary(GraphOp::Cosine, halfAngle));
			GraphValueId chamferAngle = b.Multiply(roundness, halfAngle);
			GraphValueId remainingAngle = b.Subtract(halfAngle, chamferAngle);
			GraphValueId ratio = b.Divide(b.Unary(GraphOp::Tangent, remainingAngle), b.Unary(GraphOp::Tangent, halfAngle));
			GraphValueId direction = Pair(b, b.Unary(GraphOp::Cosine, halfAngle), b.Unary(GraphOp::Sine, halfAngle));
			GraphValueId chamferCenter = b.Multiply(b.Multiply(direction, ratio), diagonal);
			GraphValueId distA = b.Unary(GraphOp::Length, chamferCenter);
			GraphValueId distB = b.Subtract(one, b.Swizzle(chamferCenter, "x"));
			uv = b.Multiply(uv, diagonal);
			GraphValueId polarAngle = b.Binary(GraphOp::Arctangent2, b.Swizzle(uv, "y"), b.Swizzle(uv, "x"));
			GraphValueId polarRadius = b.Unary(GraphOp::Length, uv);
			polarAngle = b.Add(polarAngle, b.Scalar(static_cast<f32>(HALF_PI + 2.0 * PI)));
			polarAngle = b.Binary(GraphOp::Modulo, b.Add(polarAngle, halfAngle), fullAngle);
			polarAngle = b.Unary(GraphOp::Absolute, b.Subtract(polarAngle, halfAngle));
			uv = b.Multiply(Pair(b, b.Unary(GraphOp::Cosine, polarAngle), b.Unary(GraphOp::Sine, polarAngle)), polarRadius);
			GraphValueId angleRatio = b.Subtract(one, b.Divide(b.Subtract(polarAngle, remainingAngle), chamferAngle));
			GraphValueId cosine = b.Unary(GraphOp::Cosine, b.Subtract(b.Scalar(static_cast<f32>(PI)), b.Multiply(halfAngle, angleRatio)));
			GraphValueId sides2 = b.Add(b.Multiply(distA, distA), b.Multiply(distB, distB));
			GraphValueId cross = b.Multiply(b.Multiply(b.Multiply(b.Scalar(2.0f), distA), distB), cosine);
			GraphValueId distC = b.Unary(GraphOp::SquareRoot, b.Subtract(sides2, cross));
			GraphValueId chamferZone = b.Unary(GraphOp::BoolToFloat, b.Binary(GraphOp::Less, b.Subtract(halfAngle, polarAngle), chamferAngle));
			GraphValueId out = b.Lerp(b.Swizzle(uv, "x"), b.Divide(polarRadius, distC), chamferZone);
			c.outputs[0] = Edge(b, out);
		}

		constexpr GraphNodeDefinition PROCEDURAL_DEFINITIONS[] = {
			{"Procedural/Checkerboard", "Checkerboard", "Procedural", "", CHECKERBOARD_INPUTS, CHECKERBOARD_OUTPUTS, {}, CheckerboardBody},
			{"Procedural/GradientNoise", "Gradient Noise", "Procedural/Noise", "perlin noise", GRADIENT_NOISE_INPUTS, NOISE_OUTPUTS, MOD_HASH_SETTINGS,
			 GradientNoiseBody},
			{"Procedural/SimpleNoise", "Simple Noise", "Procedural/Noise", "value noise", SIMPLE_NOISE_INPUTS, NOISE_OUTPUTS, SINE_HASH_SETTINGS,
			 SimpleNoiseBody},
			{"Procedural/Voronoi", "Voronoi", "Procedural/Noise", "worley noise", VORONOI_INPUTS, VORONOI_OUTPUTS, SINE_HASH_SETTINGS, VoronoiBody},
			{"Procedural/Ellipse", "Ellipse", "Procedural/Shape", "circle", ELLIPSE_INPUTS, SHAPE_OUTPUTS, {}, EllipseBody},
			{"Procedural/Polygon", "Polygon", "Procedural/Shape", "shape", POLYGON_INPUTS, POLYGON_OUTPUTS, {}, PolygonBody},
			{"Procedural/Rectangle", "Rectangle", "Procedural/Shape", "square", ELLIPSE_INPUTS, SHAPE_OUTPUTS, RECTANGLE_SETTINGS, RectangleBody},
			{"Procedural/RoundedPolygon", "Rounded Polygon", "Procedural/Shape", "shape", ROUNDED_POLYGON_INPUTS, SHAPE_OUTPUTS, {}, RoundedPolygonBody},
			{"Procedural/RoundedRectangle", "Rounded Rectangle", "Procedural/Shape", "square", ROUNDED_RECTANGLE_INPUTS, SHAPE_OUTPUTS, {}, RoundedRectangleBody},
		};

	} // anonymous namespace

	std::span<const GraphNodeDefinition> GraphProceduralDefinitions() { return PROCEDURAL_DEFINITIONS; }

} // namespace Shalltear
