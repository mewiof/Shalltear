#include "GraphEffectBlocks.h"

#include "GraphCurves.h"

#include <algorithm>
#include <format>
#include <numbers>

namespace Shalltear::GraphEffectBlocks {

	namespace {

		using T = GraphValueType;

		constexpr f32 EPSILON = 1.0e-5f;
		constexpr f32 FLT_MIN_VALUE = 1.175494351e-38f;
		constexpr f32 DEGREES = std::numbers::pi_v<f32> / 180.0f;

		GraphValueId In(GraphBodyContext& c, std::string_view name) {
			const i32 at = GraphNodeLibrary::FindInput(*c.definition, name);
			return at < 0 ? GRAPH_NO_VALUE : c.In(static_cast<u32>(at));
		}

		u32 IndexOf(GraphBodyContext& c, std::string_view name) {
			const i32 at = GraphNodeLibrary::FindInput(*c.definition, name);
			return at < 0 ? 0u : static_cast<u32>(at);
		}

		void Out(GraphBodyContext& c, std::string_view name, GraphValueId value) {
			const i32 at = GraphNodeLibrary::FindOutput(*c.definition, name);
			if (at >= 0) {
				c.outputs[static_cast<u32>(at)] = value;
			}
		}

		GraphValueId Current(GraphBodyContext& c, std::string_view attribute) { return c.Binding(GraphEffectAttributes::CurrentBinding(attribute)); }

		GraphValueId DeltaTime(GraphBodyContext& c) { return c.builder.Swizzle(c.Binding("TimeParameters"), "w"); }

		GraphValueId Draw(GraphBodyContext& c, u32 k) {
			return c.builder.Random(c.Binding(GRAPH_EFFECT_RANDOM_PARTICLE), c.builder.Scalar(0.0f), c.builder.Identity() * 8u + k);
		}

		GraphValueId Vec3(GraphProgramBuilder& b, f32 x, f32 y, f32 z) {
			const f32 components[3] = {x, y, z};
			return b.Constant(T::Float3, components);
		}

		GraphValueId Vec3(GraphProgramBuilder& b, GraphValueId x, GraphValueId y, GraphValueId z) {
			const GraphValueId parts[] = {x, y, z};
			return b.Construct(T::Float3, parts);
		}

		GraphValueId Comp(GraphProgramBuilder& b, GraphValueId v, std::string_view which) { return b.Swizzle(v, which); }

		GraphValueId Compose(GraphProgramBuilder& b, GraphValueId current, GraphValueId value, u32 composition, GraphValueId blend) {
			switch (composition) {
			case 0:
				return value;
			case 1:
				return b.Add(current, value);
			case 2:
				return b.Multiply(current, value);
			default:
				return b.Lerp(current, value, blend);
			}
		}

		GraphValueId Less(GraphProgramBuilder& b, GraphValueId x, GraphValueId y) { return b.Binary(GraphOp::Less, x, y); }
		GraphValueId Greater(GraphProgramBuilder& b, GraphValueId x, GraphValueId y) { return b.Binary(GraphOp::Less, y, x); }
		GraphValueId And(GraphProgramBuilder& b, GraphValueId x, GraphValueId y) { return b.Binary(GraphOp::LogicalAnd, x, y); }
		GraphValueId Or(GraphProgramBuilder& b, GraphValueId x, GraphValueId y) { return b.Binary(GraphOp::LogicalOr, x, y); }
		GraphValueId Not(GraphProgramBuilder& b, GraphValueId x) { return b.Unary(GraphOp::LogicalNot, x); }

		GraphValueId SafeNormalize(GraphProgramBuilder& b, GraphValueId v) {
			return b.Multiply(v, b.Unary(GraphOp::ReciprocalSquareRoot, b.Maximum(b.Dot(v, v), b.Scalar(FLT_MIN_VALUE))));
		}

		GraphValueId FastSign(GraphProgramBuilder& b, GraphValueId v) {
			GraphValueId parts[3];
			for (u32 i = 0; i < 3; ++i) {
				const GraphValueId component = b.Swizzle(v, std::string_view("xyz").substr(i, 1));
				parts[i] = b.Select(Less(b, component, b.Scalar(0.0f)), b.Scalar(-1.0f), b.Scalar(1.0f));
			}
			return Vec3(b, parts[0], parts[1], parts[2]);
		}

		// TRS columns, degrees Z -> X -> Y
		struct Frame {
			GraphValueId origin = GRAPH_NO_VALUE;
			GraphValueId axes[3] = {GRAPH_NO_VALUE, GRAPH_NO_VALUE, GRAPH_NO_VALUE};
		};

		Frame TrsFrame(GraphProgramBuilder& b, GraphValueId position, GraphValueId angles, GraphValueId scale) {
			const GraphValueId radians = b.Multiply(angles, b.Scalar(DEGREES));
			const GraphValueId s = b.Unary(GraphOp::Sine, radians);
			const GraphValueId k = b.Unary(GraphOp::Cosine, radians);
			const GraphValueId sx = Comp(b, s, "x"), sy = Comp(b, s, "y"), sz = Comp(b, s, "z");
			const GraphValueId cx = Comp(b, k, "x"), cy = Comp(b, k, "y"), cz = Comp(b, k, "z");
			const auto mul = [&](GraphValueId x, GraphValueId y) { return b.Multiply(x, y); };
			const GraphValueId column0 = Vec3(b, b.Add(mul(cy, cz), mul(mul(sx, sy), sz)), mul(cx, sz), b.Add(b.Unary(GraphOp::Negate, mul(cz, sy)), mul(mul(cy, sx), sz)));
			const GraphValueId column1 = Vec3(b, b.Subtract(mul(mul(cz, sx), sy), mul(cy, sz)), mul(cx, cz), b.Add(mul(mul(cy, cz), sx), mul(sy, sz)));
			const GraphValueId column2 = Vec3(b, mul(cx, sy), b.Unary(GraphOp::Negate, sx), mul(cx, cy));
			Frame frame;
			frame.origin = position;
			frame.axes[0] = b.Multiply(column0, Comp(b, scale, "x"));
			frame.axes[1] = b.Multiply(column1, Comp(b, scale, "y"));
			frame.axes[2] = b.Multiply(column2, Comp(b, scale, "z"));
			return frame;
		}

		Frame Converted(GraphBodyContext& c, Frame frame, u32 port) {
			if (!c.IsForeign(port)) {
				return frame;
			}
			frame.origin = c.FromForeign(frame.origin, GraphPortSpace::Position);
			for (GraphValueId& axis : frame.axes) {
				axis = c.FromForeign(axis, GraphPortSpace::Vector);
			}
			return frame;
		}

		Frame TransformInput(GraphBodyContext& c, std::string_view position, std::string_view angles, std::string_view scale) {
			return Converted(c, TrsFrame(c.builder, In(c, position), In(c, angles), In(c, scale)), IndexOf(c, position));
		}

		Frame Scaled(GraphProgramBuilder& b, Frame frame, GraphValueId by) {
			for (GraphValueId& axis : frame.axes) {
				axis = b.Multiply(axis, by);
			}
			return frame;
		}

		GraphValueId ApplyVector(GraphProgramBuilder& b, const Frame& frame, GraphValueId v) {
			return b.Add(b.Add(b.Multiply(frame.axes[0], Comp(b, v, "x")), b.Multiply(frame.axes[1], Comp(b, v, "y"))),
						 b.Multiply(frame.axes[2], Comp(b, v, "z")));
		}

		GraphValueId ApplyPoint(GraphProgramBuilder& b, const Frame& frame, GraphValueId p) { return b.Add(frame.origin, ApplyVector(b, frame, p)); }

		GraphValueId Determinant(GraphProgramBuilder& b, const Frame& frame) {
			return b.Dot(frame.axes[0], b.Binary(GraphOp::Cross, frame.axes[1], frame.axes[2]));
		}

		GraphValueId ApplyNormal(GraphProgramBuilder& b, const Frame& frame, GraphValueId n) {
			const GraphValueId yz = b.Binary(GraphOp::Cross, frame.axes[1], frame.axes[2]);
			const GraphValueId zx = b.Binary(GraphOp::Cross, frame.axes[2], frame.axes[0]);
			const GraphValueId xy = b.Binary(GraphOp::Cross, frame.axes[0], frame.axes[1]);
			const GraphValueId sum = b.Add(b.Add(b.Multiply(yz, Comp(b, n, "x")), b.Multiply(zx, Comp(b, n, "y"))), b.Multiply(xy, Comp(b, n, "z")));
			return b.Multiply(b.Unary(GraphOp::Normalize, sum), b.Unary(GraphOp::Sign, Determinant(b, frame)));
		}

		GraphValueId InverseVector(GraphProgramBuilder& b, const Frame& frame, GraphValueId v) {
			const GraphValueId yz = b.Binary(GraphOp::Cross, frame.axes[1], frame.axes[2]);
			const GraphValueId zx = b.Binary(GraphOp::Cross, frame.axes[2], frame.axes[0]);
			const GraphValueId xy = b.Binary(GraphOp::Cross, frame.axes[0], frame.axes[1]);
			return b.Divide(Vec3(b, b.Dot(v, yz), b.Dot(v, zx), b.Dot(v, xy)), Determinant(b, frame));
		}

		GraphValueId InversePoint(GraphProgramBuilder& b, const Frame& frame, GraphValueId p) {
			return InverseVector(b, frame, b.Subtract(p, frame.origin));
		}

		GraphValueId ScaleOf(GraphProgramBuilder& b, const Frame& frame) {
			return Vec3(b, b.Unary(GraphOp::Length, frame.axes[0]), b.Unary(GraphOp::Length, frame.axes[1]), b.Unary(GraphOp::Length, frame.axes[2]));
		}

		GraphValueId InverseTranspose(GraphProgramBuilder& b, const Frame& frame, GraphValueId v) {
			const GraphValueId yz = b.Binary(GraphOp::Cross, frame.axes[1], frame.axes[2]);
			const GraphValueId zx = b.Binary(GraphOp::Cross, frame.axes[2], frame.axes[0]);
			const GraphValueId xy = b.Binary(GraphOp::Cross, frame.axes[0], frame.axes[1]);
			const GraphValueId sum = b.Add(b.Add(b.Multiply(yz, Comp(b, v, "x")), b.Multiply(zx, Comp(b, v, "y"))), b.Multiply(xy, Comp(b, v, "z")));
			return b.Divide(sum, Determinant(b, frame));
		}

		GraphValueId Max3(GraphProgramBuilder& b, GraphValueId v) { return b.Maximum(Comp(b, v, "x"), b.Maximum(Comp(b, v, "y"), Comp(b, v, "z"))); }

		GraphValueId SampleSDF(GraphProgramBuilder& b, GraphValueId field, GraphValueId uvw, GraphValueId level) {
			const GraphValueId operands[] = {field, uvw, level};
			return b.Swizzle(b.TextureOp(GraphOp::SampleTextureLevel, operands), "x");
		}

		GraphValueId SampleSDF(GraphProgramBuilder& b, GraphValueId field, GraphValueId uvw) { return SampleSDF(b, field, uvw, b.Scalar(0.0f)); }

		GraphValueId Taps(GraphProgramBuilder& b, GraphValueId field, GraphValueId uvw, GraphValueId offset) {
			GraphValueId taps[3];
			for (u32 i = 0; i < 3; ++i) {
				const GraphValueId zero = b.Scalar(0.0f);
				GraphValueId parts[3] = {zero, zero, zero};
				parts[i] = Comp(b, offset, std::string_view("xyz").substr(i, 1));
				taps[i] = SampleSDF(b, field, b.Add(uvw, Vec3(b, parts[0], parts[1], parts[2])));
			}
			return Vec3(b, taps[0], taps[1], taps[2]);
		}

		GraphValueId DerivativesFast(GraphProgramBuilder& b, GraphValueId field, GraphValueId uvw, GraphValueId distance) {
			return b.Subtract(Taps(b, field, uvw, Vec3(b, 0.01f, 0.01f, 0.01f)), distance);
		}

		GraphValueId CentralDerivatives(GraphProgramBuilder& b, GraphValueId field, GraphValueId uvw, GraphValueId step) {
			return b.Subtract(Taps(b, field, uvw, step), Taps(b, field, uvw, b.Unary(GraphOp::Negate, step)));
		}

		GraphValueId TexelCounts(GraphProgramBuilder& b, GraphValueId field) {
			const GraphValueId operands[] = {field};
			return b.Swizzle(b.TextureOp(GraphOp::TextureSize, operands), "xyz");
		}

		GraphValueId VolumeFactor(GraphBodyContext& c, u32 mode, GraphValueId radius, f32 dimensions) {
			GraphProgramBuilder& b = c.builder;
			GraphValueId factor = b.Scalar(mode == 0 ? 0.0f : 1.0f);
			if (mode >= 2) {
				GraphValueId thickness = In(c, "Thickness");
				if (mode == 2) {
					thickness = b.Divide(thickness, radius);
				}
				factor = b.Unary(GraphOp::Saturate, thickness);
			}
			return b.Binary(GraphOp::Power, b.Subtract(b.Scalar(1.0f), factor), b.Scalar(dimensions));
		}

		struct Placed {
			GraphValueId position = GRAPH_NO_VALUE;
			GraphValueId axisX = GRAPH_NO_VALUE;
			GraphValueId axisY = GRAPH_NO_VALUE;
			GraphValueId axisZ = GRAPH_NO_VALUE;
			GraphValueId direction = GRAPH_NO_VALUE;
		};

		Placed SphereShape(GraphBodyContext& c, u32 mode, bool custom) {
			GraphProgramBuilder& b = c.builder;
			const Frame frame = TransformInput(c, "Position", "Angles", "Scale");
			const GraphValueId radius = In(c, "Radius");
			const GraphValueId arc = b.Multiply(In(c, "Arc"), b.Scalar(DEGREES));
			const GraphValueId cosPhi = b.Subtract(b.Multiply(b.Scalar(2.0f), custom ? In(c, "Height Sequencer") : Draw(c, 0)), b.Scalar(1.0f));
			const GraphValueId theta = b.Multiply(arc, custom ? In(c, "Arc Sequencer") : Draw(c, 1));
			const GraphValueId volume = VolumeFactor(c, mode, radius, 3.0f);
			const GraphValueId rNorm = b.Binary(GraphOp::Power, b.Unary(GraphOp::Absolute, b.Add(volume, b.Multiply(b.Subtract(b.Scalar(1.0f), volume), Draw(c, 2)))),
												b.Scalar(1.0f / 3.0f));
			const GraphValueId sinPhi = b.Unary(GraphOp::SquareRoot, b.Subtract(b.Scalar(1.0f), b.Multiply(cosPhi, cosPhi)));
			const GraphValueId sinTheta = b.Multiply(b.Unary(GraphOp::Sine, theta), sinPhi);
			const GraphValueId cosTheta = b.Multiply(b.Unary(GraphOp::Cosine, theta), sinPhi);
			const GraphValueId axisY = Vec3(b, sinTheta, cosTheta, cosPhi);
			const GraphValueId pole = Vec3(b, b.Unary(GraphOp::Sign, cosPhi), b.Scalar(0.0f), b.Scalar(0.0f));
			const GraphValueId axisZ = b.Select(Greater(b, b.Unary(GraphOp::Absolute, cosPhi), b.Scalar(0.99999f)), pole,
												Vec3(b, b.Unary(GraphOp::Negate, cosTheta), sinTheta, b.Scalar(0.0f)));
			Placed out;
			out.position = ApplyPoint(b, Scaled(b, frame, radius), b.Multiply(axisY, rNorm));
			out.axisY = ApplyNormal(b, frame, axisY);
			out.axisZ = ApplyNormal(b, frame, axisZ);
			out.axisX = b.Binary(GraphOp::Cross, out.axisY, out.axisZ);
			out.direction = out.axisY;
			return out;
		}

		Placed CircleShape(GraphBodyContext& c, u32 mode, bool custom) {
			GraphProgramBuilder& b = c.builder;
			const Frame frame = TransformInput(c, "Position", "Angles", "Scale");
			const GraphValueId radius = In(c, "Radius");
			const GraphValueId theta = b.Multiply(b.Multiply(In(c, "Arc"), b.Scalar(DEGREES)), custom ? In(c, "Arc Sequencer") : Draw(c, 0));
			const GraphValueId volume = VolumeFactor(c, mode, radius, 2.0f);
			const GraphValueId rNorm = b.Unary(GraphOp::SquareRoot, b.Add(volume, b.Multiply(b.Subtract(b.Scalar(1.0f), volume), Draw(c, 1))));
			const GraphValueId axisY = Vec3(b, b.Unary(GraphOp::Sine, theta), b.Unary(GraphOp::Cosine, theta), b.Scalar(0.0f));
			Placed out;
			out.position = ApplyPoint(b, Scaled(b, frame, radius), b.Multiply(axisY, rNorm));
			out.axisY = ApplyNormal(b, frame, axisY);
			out.axisZ = ApplyNormal(b, frame, Vec3(b, 0.0f, 0.0f, 1.0f));
			out.axisX = b.Binary(GraphOp::Cross, out.axisY, out.axisZ);
			out.direction = out.axisY;
			return out;
		}

		Placed LineShape(GraphBodyContext& c, bool custom) {
			GraphProgramBuilder& b = c.builder;
			const GraphValueId start = In(c, "Start");
			const GraphValueId end = In(c, "End");
			const GraphValueId direction = SafeNormalize(b, b.Subtract(end, start));
			const GraphValueId fromUp = b.Binary(GraphOp::Cross, Vec3(b, 0.0f, 1.0f, 0.0f), direction);
			const GraphValueId fromRight = b.Binary(GraphOp::Cross, Vec3(b, 1.0f, 0.0f, 0.0f), direction);
			const GraphValueId tangent = b.Unary(GraphOp::Normalize, b.Select(Less(b, b.Dot(fromUp, fromUp), b.Scalar(EPSILON * EPSILON)), fromRight, fromUp));
			Placed out;
			out.position = b.Lerp(start, end, custom ? In(c, "Line Sequencer") : Draw(c, 0));
			out.direction = direction;
			out.axisX = tangent;
			out.axisY = direction;
			out.axisZ = b.Binary(GraphOp::Cross, tangent, direction);
			return out;
		}

		Placed ConeShape(GraphBodyContext& c, u32 mode, bool custom, bool onBase) {
			GraphProgramBuilder& b = c.builder;
			const Frame frame = TransformInput(c, "Position", "Angles", "Scale");
			const GraphValueId base = In(c, "Base Radius");
			const GraphValueId top = In(c, "Top Radius");
			const GraphValueId height = In(c, "Height");
			const GraphValueId theta = b.Multiply(b.Multiply(In(c, "Arc"), b.Scalar(DEGREES)), custom ? In(c, "Arc Sequencer") : Draw(c, 0));
			const GraphValueId volume = VolumeFactor(c, mode, base, 2.0f);
			const GraphValueId rNorm = b.Unary(GraphOp::SquareRoot, b.Add(volume, b.Multiply(b.Subtract(b.Scalar(1.0f), volume), Draw(c, 1))));
			const GraphValueId sinTheta = b.Unary(GraphOp::Sine, theta);
			const GraphValueId cosTheta = b.Unary(GraphOp::Cosine, theta);
			const GraphValueId px = b.Multiply(sinTheta, rNorm);
			const GraphValueId py = b.Multiply(cosTheta, rNorm);

			GraphValueId hNorm = b.Scalar(0.0f);
			if (!onBase && custom) {
				hNorm = In(c, "Height Sequencer");
			} else if (!onBase) {
				const f32 exponent = mode == 0 ? 2.0f : 3.0f;
				const GraphValueId draw = Draw(c, 2);
				const GraphValueId factor = b.Divide(base, b.Maximum(b.Scalar(EPSILON), top));
				const GraphValueId factorPow = b.Binary(GraphOp::Power, factor, b.Scalar(exponent));
				GraphValueId h = b.Binary(GraphOp::Power, b.Unary(GraphOp::Absolute, b.Add(factorPow, b.Multiply(b.Subtract(b.Scalar(1.0f), factorPow), draw))),
										  b.Scalar(1.0f / exponent));
				h = b.Divide(b.Subtract(h, factor), b.Subtract(b.Scalar(1.0f), factor));
				hNorm = b.Select(Greater(b, b.Unary(GraphOp::Absolute, b.Subtract(base, top)), b.Scalar(EPSILON)), h, draw);
			}
			const GraphValueId slope = b.Unary(GraphOp::Arctangent, b.Divide(b.Subtract(top, base), height));
			const GraphValueId sinSlope = b.Unary(GraphOp::Sine, slope);
			const GraphValueId cosSlope = b.Unary(GraphOp::Cosine, slope);
			const GraphValueId low = Vec3(b, b.Multiply(px, base), b.Scalar(0.0f), b.Multiply(py, base));
			const GraphValueId high = Vec3(b, b.Multiply(px, top), height, b.Multiply(py, top));
			const GraphValueId local = b.Lerp(low, high, hNorm);
			const GraphValueId axisZ = Vec3(b, b.Multiply(sinTheta, sinSlope), cosSlope, b.Multiply(cosTheta, sinSlope));
			const GraphValueId axisY = Vec3(b, sinTheta, b.Unary(GraphOp::Negate, sinSlope), cosTheta);
			Placed out;
			out.position = ApplyPoint(b, frame, local);
			out.axisY = b.Unary(GraphOp::Normalize, ApplyVector(b, frame, axisY));
			out.axisZ = b.Unary(GraphOp::Normalize, ApplyVector(b, frame, axisZ));
			out.axisX = b.Binary(GraphOp::Cross, out.axisY, out.axisZ);
			out.direction = out.axisZ;
			return out;
		}

		Placed TorusShape(GraphBodyContext& c, u32 mode, bool custom) {
			GraphProgramBuilder& b = c.builder;
			const Frame frame = TransformInput(c, "Position", "Angles", "Scale");
			const GraphValueId major = In(c, "Major Radius");
			const GraphValueId minor = In(c, "Minor Radius");
			GraphValueId arc = b.Multiply(In(c, "Arc"), b.Scalar(DEGREES));
			GraphValueId ux = GRAPH_NO_VALUE;
			GraphValueId uy = GRAPH_NO_VALUE;
			GraphValueId uz = GRAPH_NO_VALUE;
			if (custom) {
				ux = In(c, "Height Sequencer");
				uy = b.Scalar(1.0f);
				uz = Draw(c, 0);
				arc = b.Multiply(arc, In(c, "Arc Sequencer"));
			} else {
				ux = Draw(c, 0);
				uy = Draw(c, 1);
				uz = Draw(c, 2);
			}
			const GraphValueId volume = VolumeFactor(c, mode, major, 2.0f);
			const GraphValueId big = b.Unary(GraphOp::SquareRoot, b.Add(volume, b.Multiply(b.Subtract(b.Scalar(1.0f), volume), uz)));
			const GraphValueId ratio = b.Select(Greater(b, b.Unary(GraphOp::Absolute, major), b.Scalar(EPSILON)), b.Unary(GraphOp::Saturate, b.Divide(minor, major)),
												b.Scalar(0.0f));
			const GraphValueId tube = b.Multiply(ux, b.Scalar(2.0f * std::numbers::pi_v<f32>));
			const GraphValueId sinTheta = b.Unary(GraphOp::Sine, tube);
			const GraphValueId cosTheta = b.Unary(GraphOp::Cosine, tube);
			const GraphValueId reach = b.Multiply(big, ratio);
			const GraphValueId outerX = b.Add(b.Multiply(reach, cosTheta), b.Scalar(1.0f));
			const GraphValueId innerX = b.Add(b.Multiply(reach, b.Unary(GraphOp::Negate, cosTheta)), b.Scalar(1.0f));
			const GraphValueId sideY = b.Multiply(reach, sinTheta);
			const GraphValueId w = b.Divide(outerX, b.Add(outerX, innerX));
			const GraphValueId outer = Less(b, uy, w);
			const GraphValueId phi = b.Select(outer, b.Divide(b.Multiply(arc, uy), w), b.Divide(b.Multiply(arc, b.Subtract(uy, w)), b.Subtract(b.Scalar(1.0f), w)));
			const GraphValueId t = b.Select(outer, Vec3(b, outerX, b.Scalar(0.0f), sideY), Vec3(b, innerX, b.Scalar(0.0f), sideY));
			const GraphValueId named = b.Unary(GraphOp::Sine, phi);
			const GraphValueId other = b.Unary(GraphOp::Cosine, phi);
			const GraphValueId tx = Comp(b, t, "x");
			const GraphValueId ty = Comp(b, t, "y");
			const GraphValueId direction = Vec3(b, b.Subtract(b.Multiply(named, tx), b.Multiply(other, ty)), b.Add(b.Multiply(named, ty), b.Multiply(other, tx)),
												Comp(b, t, "z"));
			const GraphValueId axisY = Vec3(b, b.Unary(GraphOp::Negate, b.Multiply(named, cosTheta)), b.Unary(GraphOp::Negate, b.Multiply(other, cosTheta)), sinTheta);
			const GraphValueId around = b.Add(tx, b.Multiply(ty, cosTheta));
			const GraphValueId axisZ = Vec3(b, b.Multiply(around, other), b.Unary(GraphOp::Negate, b.Multiply(around, named)), b.Scalar(0.0f));
			Placed out;
			out.position = ApplyPoint(b, frame, b.Multiply(direction, major));
			out.direction = ApplyNormal(b, frame, direction);
			out.axisY = ApplyNormal(b, frame, axisY);
			out.axisZ = ApplyNormal(b, frame, axisZ);
			out.axisX = b.Binary(GraphOp::Cross, out.axisY, out.axisZ);
			return out;
		}

		Placed SDFShape(GraphBodyContext& c, u32 mode, bool custom, GraphValueId& outlier) {
			GraphProgramBuilder& b = c.builder;
			const Frame field = TransformInput(c, "Center", "Angles", "Size");
			const GraphValueId sdf = In(c, "SDF");
			const GraphValueId scaling = Max3(b, ScaleOf(b, field));
			const GraphValueId counts = TexelCounts(b, sdf);
			const GraphValueId least = b.Minimum(Comp(b, counts, "x"), b.Minimum(Comp(b, counts, "y"), Comp(b, counts, "z")));
			const GraphValueId reach = b.Subtract(b.Scalar(0.5f), b.Add(b.Divide(b.Scalar(0.5f), least), b.Scalar(0.01f + 0.001f)));
			GraphValueId thickness = b.Scalar(0.0f);
			if (mode == 1) {
				thickness = scaling;
			} else if (mode == 2) {
				thickness = In(c, "Thickness");
			} else if (mode == 3) {
				thickness = b.Multiply(In(c, "Thickness"), b.Multiply(scaling, b.Scalar(0.5f)));
			}
			const GraphValueId cosPhi = b.Subtract(b.Multiply(b.Scalar(2.0f), Draw(c, 0)), b.Scalar(1.0f));
			const GraphValueId theta = b.Multiply(b.Scalar(2.0f * std::numbers::pi_v<f32>), custom ? In(c, "Arc Sequencer") : Draw(c, 1));
			const GraphValueId sinPhi = b.Unary(GraphOp::SquareRoot, b.Subtract(b.Scalar(1.0f), b.Multiply(cosPhi, cosPhi)));
			const GraphValueId sinTheta = b.Multiply(b.Unary(GraphOp::Sine, theta), sinPhi);
			const GraphValueId cosTheta = b.Multiply(b.Unary(GraphOp::Cosine, theta), sinPhi);
			GraphValueId ray = b.Multiply(Vec3(b, sinTheta, cosTheta, cosPhi), b.Scalar(std::numbers::sqrt3_v<f32> * 0.5f));
			const GraphValueId longest = b.Maximum(b.Scalar(0.5f), Max3(b, b.Unary(GraphOp::Absolute, ray)));
			ray = b.Multiply(ray, b.Divide(reach, longest));
			GraphValueId local = b.Multiply(ray, b.Binary(GraphOp::Power, Draw(c, 2), b.Scalar(1.0f / 3.0f)));
			GraphValueId coord = b.Add(local, b.Scalar(0.5f));
			GraphValueId world = GRAPH_NO_VALUE;
			GraphValueId normal = GRAPH_NO_VALUE;
			const u32 steps = std::clamp(static_cast<u32>(std::max(c.Number(ProjectionStepsSetting), 1.0f)), 1u, MAX_PROJECTION_STEPS);
			for (u32 step = 0; step < steps; ++step) {
				GraphValueId distance = SampleSDF(b, sdf, coord);
				const GraphValueId n = b.Unary(GraphOp::Negate, b.Unary(GraphOp::Normalize, DerivativesFast(b, sdf, coord, distance)));
				distance = b.Multiply(distance, scaling);
				normal = SafeNormalize(b, InverseTranspose(b, field, n));
				const GraphValueId below = b.Minimum(b.Add(distance, thickness), b.Scalar(0.0f));
				const GraphValueId delta = b.Multiply(b.Select(Greater(b, distance, b.Scalar(0.0f)), distance, below), normal);
				world = b.Add(ApplyPoint(b, field, local), delta);
				local = InversePoint(b, field, world);
				coord = b.Add(local, b.Scalar(0.5f));
			}
			outlier = Greater(b, b.Multiply(SampleSDF(b, sdf, coord), scaling), b.Scalar(0.01f));
			Placed out;
			out.position = world;
			out.axisY = b.Unary(GraphOp::Negate, normal);
			const GraphValueId tangent = InverseTranspose(b, field, Vec3(b, b.Unary(GraphOp::Negate, cosTheta), sinTheta, b.Scalar(0.0f)));
			const GraphValueId pole = InverseTranspose(b, field, Vec3(b, b.Unary(GraphOp::Sign, cosPhi), b.Scalar(0.0f), b.Scalar(0.0f)));
			const GraphValueId fromTangent = b.Binary(GraphOp::Cross, tangent, out.axisY);
			const GraphValueId atPole = Less(b, b.Dot(fromTangent, fromTangent), b.Scalar(0.00001f));
			const GraphValueId axisZ = b.Select(atPole, pole, tangent);
			out.axisX = b.Unary(GraphOp::Normalize, b.Binary(GraphOp::Cross, axisZ, out.axisY));
			out.axisZ = b.Unary(GraphOp::Normalize, b.Binary(GraphOp::Cross, out.axisX, out.axisY));
			out.direction = out.axisY;
			return out;
		}

		struct Face {
			GraphValueId position = GRAPH_NO_VALUE;
			GraphValueId direction = GRAPH_NO_VALUE;
			GraphValueId up = GRAPH_NO_VALUE;
			GraphValueId tangent = GRAPH_NO_VALUE;
		};

		Face Pick(GraphProgramBuilder& b, GraphValueId condition, const Face& yes, const Face& no) {
			return {b.Select(condition, yes.position, no.position), b.Select(condition, yes.direction, no.direction), b.Select(condition, yes.up, no.up),
					b.Select(condition, yes.tangent, no.tangent)};
		}

		Placed BoxShape(GraphBodyContext& c, u32 mode) {
			GraphProgramBuilder& b = c.builder;
			const u32 port = IndexOf(c, "Center");
			const GraphValueId center = In(c, "Center");
			const GraphValueId angles = In(c, "Angles");
			const Frame sized = Converted(c, TrsFrame(b, center, angles, In(c, "Size")), port);
			const Frame turned = Converted(c, TrsFrame(b, center, angles, Vec3(b, 1.0f, 1.0f, 1.0f)), port);
			Frame frame = sized;
			GraphValueId size = Vec3(b, 1.0f, 1.0f, 1.0f);
			if (mode == 2) {
				size = ScaleOf(b, sized);
				for (u32 i = 0; i < 3; ++i) {
					frame.axes[i] = b.Divide(sized.axes[i], Comp(b, size, std::string_view("xyz").substr(i, 1)));
				}
			}
			const GraphValueId sx = Comp(b, size, "x"), sy = Comp(b, size, "y"), sz = Comp(b, size, "z");
			const GraphValueId zero = b.Scalar(0.0f);
			Face face;
			if (mode == 1) {
				const GraphValueId r3 = b.Subtract(Vec3(b, Draw(c, 0), Draw(c, 1), Draw(c, 2)), b.Scalar(0.5f));
				const GraphValueId sizedRandom = b.Multiply(b.Maximum(size, b.Scalar(EPSILON)), r3);
				const GraphValueId bound = b.Multiply(size, b.Scalar(0.5f));
				const auto distance = [&](std::string_view axis, bool plus) {
					return plus ? b.Add(Comp(b, bound, axis), Comp(b, sizedRandom, axis)) : b.Subtract(Comp(b, bound, axis), Comp(b, sizedRandom, axis));
				};
				const Face candidates[] = {
					{GRAPH_NO_VALUE, Vec3(b, 0.0f, 0.0f, 1.0f), Vec3(b, -1.0f, 0.0f, 0.0f), Vec3(b, 0.0f, -1.0f, 0.0f)},
					{GRAPH_NO_VALUE, Vec3(b, 0.0f, 0.0f, -1.0f), Vec3(b, -1.0f, 0.0f, 0.0f), Vec3(b, 0.0f, 1.0f, 0.0f)},
					{GRAPH_NO_VALUE, Vec3(b, 0.0f, 1.0f, 0.0f), Vec3(b, -1.0f, 0.0f, 0.0f), Vec3(b, 0.0f, 0.0f, 1.0f)},
					{GRAPH_NO_VALUE, Vec3(b, 0.0f, -1.0f, 0.0f), Vec3(b, -1.0f, 0.0f, 0.0f), Vec3(b, 0.0f, 0.0f, -1.0f)},
					{GRAPH_NO_VALUE, Vec3(b, 1.0f, 0.0f, 0.0f), Vec3(b, 0.0f, 0.0f, 1.0f), Vec3(b, 0.0f, -1.0f, 0.0f)},
					{GRAPH_NO_VALUE, Vec3(b, -1.0f, 0.0f, 0.0f), Vec3(b, 0.0f, 0.0f, -1.0f), Vec3(b, 0.0f, -1.0f, 0.0f)},
				};
				const GraphValueId distances[] = {distance("z", false), distance("z", true), distance("y", false),
												  distance("y", true), distance("x", false), distance("x", true)};
				face = candidates[0];
				GraphValueId nearest = distances[0];
				for (u32 i = 1; i < 6; ++i) {
					const GraphValueId closer = Less(b, distances[i], nearest);
					face = Pick(b, closer, candidates[i], face);
					nearest = b.Select(closer, distances[i], nearest);
				}
				face.position = b.Multiply(size, r3);
			} else if (mode == 0) {
				const GraphValueId areaXY = b.Maximum(b.Multiply(sx, sy), b.Scalar(EPSILON));
				const GraphValueId areaXZ = b.Maximum(b.Multiply(sx, sz), b.Scalar(EPSILON));
				const GraphValueId areaYZ = b.Maximum(b.Multiply(sy, sz), b.Scalar(EPSILON));
				const GraphValueId pick = b.Multiply(Draw(c, 0), b.Add(b.Add(areaXY, areaXZ), areaYZ));
				const GraphValueId flip = b.Select(Less(b, Draw(c, 1), b.Scalar(0.5f)), b.Scalar(-1.0f), b.Scalar(1.0f));
				const GraphValueId cube = Vec3(b, b.Subtract(Draw(c, 2), b.Scalar(0.5f)), b.Subtract(Draw(c, 3), b.Scalar(0.5f)), b.Multiply(flip, b.Scalar(0.5f)));
				const GraphValueId negFlip = b.Unary(GraphOp::Negate, flip);
				const Face xy{cube, Vec3(b, zero, zero, flip), Vec3(b, -1.0f, 0.0f, 0.0f), Vec3(b, zero, negFlip, zero)};
				const Face xz{Comp(b, cube, "xzy"), Vec3(b, zero, flip, zero), Vec3(b, -1.0f, 0.0f, 0.0f), Vec3(b, zero, zero, flip)};
				const Face yz{Comp(b, cube, "zxy"), Vec3(b, flip, zero, zero), Vec3(b, zero, zero, flip), Vec3(b, 0.0f, -1.0f, 0.0f)};
				face = Pick(b, Less(b, pick, areaXY), xy, Pick(b, Less(b, pick, b.Add(areaXY, areaXZ)), xz, yz));
				face.position = b.Multiply(face.position, size);
			} else {
				const GraphValueId thickness = In(c, "Thickness");
				GraphValueId factor = mode == 2 ? b.Clamp(Vec3(b, b.Multiply(thickness, b.Scalar(2.0f)), b.Multiply(thickness, b.Scalar(2.0f)), b.Multiply(thickness, b.Scalar(2.0f))),
														  Vec3(b, 0.0f, 0.0f, 0.0f), size)
												: b.Multiply(b.Unary(GraphOp::Saturate, thickness), size);
				factor = b.Maximum(factor, Vec3(b, 0.0001f, 0.0001f, 0.0001f));
				const GraphValueId fx = Comp(b, factor, "x"), fy = Comp(b, factor, "y"), fz = Comp(b, factor, "z");
				const GraphValueId volumeXY = Vec3(b, sx, sy, fz);
				const GraphValueId volumeXZ = Vec3(b, sx, b.Subtract(sz, fz), fy);
				const GraphValueId volumeYZ = Vec3(b, b.Subtract(sy, fy), b.Subtract(sz, fz), fx);
				const auto product = [&](GraphValueId v) { return b.Multiply(b.Multiply(Comp(b, v, "x"), Comp(b, v, "y")), Comp(b, v, "z")); };
				const GraphValueId first = product(volumeXY);
				const GraphValueId second = b.Add(first, product(volumeXZ));
				const GraphValueId third = b.Add(second, product(volumeYZ));
				const GraphValueId pick = b.Multiply(Draw(c, 0), third);
				const GraphValueId flip = b.Select(Less(b, Draw(c, 1), b.Scalar(0.5f)), b.Scalar(-1.0f), b.Scalar(1.0f));
				const GraphValueId cube = Vec3(b, b.Subtract(b.Multiply(Draw(c, 2), b.Scalar(2.0f)), b.Scalar(1.0f)), b.Subtract(b.Multiply(Draw(c, 3), b.Scalar(2.0f)), b.Scalar(1.0f)),
											   b.Unary(GraphOp::Negate, Draw(c, 4)));
				const GraphValueId negFlip = b.Unary(GraphOp::Negate, flip);
				const auto flipped = [&](GraphValueId v, std::string_view axis) {
					const GraphValueId parts[3] = {Comp(b, v, "x"), Comp(b, v, "y"), Comp(b, v, "z")};
					const u32 at = axis == "x" ? 0u : (axis == "y" ? 1u : 2u);
					GraphValueId out[3] = {parts[0], parts[1], parts[2]};
					out[at] = b.Multiply(parts[at], flip);
					return Vec3(b, out[0], out[1], out[2]);
				};
				const Face xy{flipped(b.Add(b.Multiply(cube, volumeXY), Vec3(b, zero, zero, sz)), "z"), Vec3(b, zero, zero, flip), Vec3(b, -1.0f, 0.0f, 0.0f),
							  Vec3(b, zero, negFlip, zero)};
				const Face xz{flipped(b.Add(Comp(b, b.Multiply(cube, volumeXZ), "xzy"), Vec3(b, zero, sy, zero)), "y"), Vec3(b, zero, flip, zero),
							  Vec3(b, -1.0f, 0.0f, 0.0f), Vec3(b, zero, zero, flip)};
				const Face yz{flipped(b.Add(Comp(b, b.Multiply(cube, volumeYZ), "zxy"), Vec3(b, sx, zero, zero)), "x"), Vec3(b, flip, zero, zero),
							  Vec3(b, zero, zero, flip), Vec3(b, 0.0f, -1.0f, 0.0f)};
				face = Pick(b, Less(b, pick, first), xy, Pick(b, Less(b, pick, second), xz, yz));
				face.position = b.Multiply(face.position, b.Scalar(0.5f));
			}
			Placed out;
			out.position = ApplyPoint(b, frame, face.position);
			const GraphValueId up = ApplyNormal(b, turned, face.up);
			out.direction = ApplyNormal(b, turned, face.direction);
			out.axisX = b.Binary(GraphOp::Cross, out.direction, up);
			out.axisY = out.direction;
			out.axisZ = up;
			return out;
		}

	} // anonymous namespace

	ElementSphere ElementBounds(GraphProgramBuilder& b, GraphValueId position, GraphValueId size, GraphValueId scale, GraphValueId angle,
								GraphValueId pivot, GraphValueId axisX, GraphValueId axisY, GraphValueId axisZ) {
		const Frame rotated = TrsFrame(b, position, angle, b.Multiply(scale, size));
		GraphValueId columns[3];
		for (u32 j = 0; j < 3; ++j) {
			const GraphValueId along = rotated.axes[j];
			columns[j] = b.Add(b.Add(b.Multiply(axisX, Comp(b, along, "x")), b.Multiply(axisY, Comp(b, along, "y"))), b.Multiply(axisZ, Comp(b, along, "z")));
		}
		const GraphValueId laid = b.Add(b.Add(b.Multiply(columns[0], Comp(b, pivot, "x")), b.Multiply(columns[1], Comp(b, pivot, "y"))),
										b.Multiply(columns[2], Comp(b, pivot, "z")));
		const GraphValueId lengths = b.Add(b.Add(b.Dot(columns[0], columns[0]), b.Dot(columns[1], columns[1])), b.Dot(columns[2], columns[2]));
		ElementSphere sphere;
		sphere.center = b.Subtract(position, laid);
		sphere.radius = b.Multiply(b.Scalar(0.5f), b.Unary(GraphOp::SquareRoot, lengths));
		return sphere;
	}

	void PositionShapeBody(GraphBodyContext& c) {
		GraphProgramBuilder& b = c.builder;
		const u32 shape = c.Setting(ShapeSetting);
		const u32 mode = shape == Line ? 0u : c.Setting(PositionModeSetting);
		const bool custom = shape != OrientedBox && c.Setting(SpawnModeSetting) == 1;
		Placed placed;
		switch (shape) {
		case Sphere:
			placed = SphereShape(c, mode, custom);
			break;
		case OrientedBox:
			placed = BoxShape(c, mode);
			break;
		case Cone:
			placed = ConeShape(c, mode, custom, c.Setting(HeightModeSetting) == 0);
			break;
		case Torus:
			placed = TorusShape(c, mode, custom);
			break;
		case Circle:
			placed = CircleShape(c, mode, custom);
			break;
		case PositionSDF: {
			GraphValueId outlier = GRAPH_NO_VALUE;
			placed = SDFShape(c, mode, custom, outlier);
			if (c.Setting(KillOutliersSetting) != 0) {
				Out(c, "alive", And(b, Current(c, "alive"), Not(b, outlier)));
			}
			break;
		}
		default:
			placed = LineShape(c, custom);
			break;
		}
		Out(c, "position", Compose(b, Current(c, "position"), placed.position, c.Setting(CompositionPositionSetting), In(c, "Blend Position")));
		const u32 orientation = c.Setting(OrientationSetting);
		if (orientation == 1 || orientation == 3) {
			Out(c, "direction", Compose(b, Current(c, "direction"), placed.direction, c.Setting(CompositionDirectionSetting), In(c, "Blend Direction")));
		}
		if (orientation >= 2) {
			const u32 composition = c.Setting(CompositionAxesSetting);
			const GraphValueId blend = In(c, "Blend Axes");
			Out(c, "axisX", Compose(b, Current(c, "axisX"), placed.axisX, composition, blend));
			Out(c, "axisY", Compose(b, Current(c, "axisY"), placed.axisY, composition, blend));
			Out(c, "axisZ", Compose(b, Current(c, "axisZ"), placed.axisZ, composition, blend));
		}
	}

	namespace {

		GraphValueId MeshCount(GraphProgramBuilder& b, GraphValueId mesh, u32 which) { return b.MeshOp(GraphOp::MeshCount, mesh, GRAPH_NO_VALUE, which); }

		GraphValueId TriangleCount(GraphProgramBuilder& b, GraphValueId mesh) {
			return b.Unary(GraphOp::Floor, b.Divide(MeshCount(b, mesh, 1), b.Scalar(3.0f)));
		}

		GraphValueId Address(GraphProgramBuilder& b, GraphValueId index, GraphValueId count, u32 mode) {
			const GraphValueId n = b.Maximum(count, b.Scalar(1.0f));
			const GraphValueId i = b.Unary(GraphOp::Floor, b.Maximum(index, b.Scalar(0.0f)));
			switch (mode) {
			case AddressWrap:
				return b.Binary(GraphOp::Modulo, i, n);
			case AddressClamp:
				return b.Minimum(i, b.Subtract(n, b.Scalar(1.0f)));
			default: {
				const GraphValueId cycle = b.Maximum(b.Subtract(b.Multiply(n, b.Scalar(2.0f)), b.Scalar(2.0f)), b.Scalar(1.0f));
				const GraphValueId at = b.Binary(GraphOp::Modulo, i, cycle);
				return b.Select(Less(b, at, n), at, b.Subtract(cycle, at));
			}
			}
		}

		GraphValueId RandomIndex(GraphBodyContext& c, GraphValueId count, u32 k) {
			GraphProgramBuilder& b = c.builder;
			return Address(b, b.Multiply(Draw(c, k), b.Maximum(count, b.Scalar(1.0f))), count, AddressClamp);
		}

		struct MeshPlace {
			GraphValueId mesh = GRAPH_NO_VALUE;
			u32 placement = PlaceVertex;
			GraphValueId vertices[3] = {GRAPH_NO_VALUE, GRAPH_NO_VALUE, GRAPH_NO_VALUE};
			GraphValueId weights = GRAPH_NO_VALUE;
		};

		GraphValueId UniformWeights(GraphProgramBuilder& b, GraphValueId square) {
			const GraphValueId t = b.Multiply(square, b.Scalar(0.5f));
			const GraphValueId tx = Comp(b, t, "x");
			const GraphValueId ty = Comp(b, t, "y");
			const GraphValueId offset = b.Subtract(ty, tx);
			const GraphValueId ahead = Greater(b, offset, b.Scalar(0.0f));
			const GraphValueId t2 = b.Select(ahead, b.Add(ty, offset), ty);
			const GraphValueId t1 = b.Select(ahead, tx, b.Subtract(tx, offset));
			return Vec3(b, t1, t2, b.Subtract(b.Subtract(b.Scalar(1.0f), t2), t1));
		}

		MeshPlace Place(GraphProgramBuilder& b, GraphValueId mesh, u32 placement, GraphValueId index, GraphValueId coordinate, u32 surface) {
			MeshPlace place;
			place.mesh = mesh;
			place.placement = placement;
			const auto read = [&](GraphValueId at) { return b.MeshOp(GraphOp::MeshIndex, mesh, at, 0); };
			switch (placement) {
			case PlaceVertex:
				place.vertices[0] = index;
				break;
			case PlaceEdge: {
				const GraphValueId next = b.Add(index, b.Scalar(1.0f));
				const GraphValueId wraps = b.Binary(GraphOp::Equal, b.Binary(GraphOp::Modulo, next, b.Scalar(3.0f)), b.Scalar(0.0f));
				place.vertices[0] = read(index);
				place.vertices[1] = read(b.Select(wraps, b.Subtract(next, b.Scalar(3.0f)), next));
				place.weights = coordinate;
				break;
			}
			default: {
				const GraphValueId first = b.Multiply(index, b.Scalar(3.0f));
				for (u32 corner = 0; corner < 3; ++corner) {
					place.vertices[corner] = read(b.Add(first, b.Scalar(static_cast<f32>(corner))));
				}
				place.weights = surface == SurfaceUniform
									? UniformWeights(b, coordinate)
									: Vec3(b, Comp(b, coordinate, "x"), Comp(b, coordinate, "y"),
										   b.Subtract(b.Subtract(b.Scalar(1.0f), Comp(b, coordinate, "x")), Comp(b, coordinate, "y")));
				break;
			}
			}
			return place;
		}

		GraphValueId Fetch(GraphProgramBuilder& b, const MeshPlace& place, GraphMeshChannel channel) {
			const auto at = [&](u32 corner) { return b.MeshOp(GraphOp::MeshVertex, place.mesh, place.vertices[corner], static_cast<u32>(channel)); };
			switch (place.placement) {
			case PlaceVertex:
				return at(0);
			case PlaceEdge:
				return b.Lerp(at(0), at(1), place.weights);
			default:
				return b.Add(b.Add(b.Multiply(at(0), Comp(b, place.weights, "x")), b.Multiply(at(1), Comp(b, place.weights, "y"))),
							 b.Multiply(at(2), Comp(b, place.weights, "z")));
			}
		}

		GraphValueId MeshOutputValue(GraphProgramBuilder& b, const MeshPlace& place, const Frame& frame, u32 output) {
			switch (output) {
			case OutPosition:
				return ApplyPoint(b, frame, Fetch(b, place, GraphMeshChannel::Position));
			case OutNormal:
				return ApplyVector(b, frame, b.Unary(GraphOp::Normalize, Fetch(b, place, GraphMeshChannel::Normal)));
			case OutTangent:
				return ApplyVector(b, frame, Comp(b, Fetch(b, place, GraphMeshChannel::Tangent), "xyz"));
			case OutBitangent: {
				const GraphValueId tangent = Fetch(b, place, GraphMeshChannel::Tangent);
				const GraphValueId cross = b.Binary(GraphOp::Cross, Fetch(b, place, GraphMeshChannel::Normal), Comp(b, tangent, "xyz"));
				return ApplyVector(b, frame, b.Multiply(cross, Comp(b, tangent, "w")));
			}
			case OutBitangentSign:
				return Comp(b, Fetch(b, place, GraphMeshChannel::Tangent), "w");
			case OutColor:
				return Fetch(b, place, GraphMeshChannel::Color);
			default:
				return Fetch(b, place, GraphMeshChannel::TexCoord0);
			}
		}

		void MeshAxes(GraphProgramBuilder& b, const MeshPlace& place, const Frame& frame, GraphValueId axes[3]) {
			constexpr f32 EPSILON_SQUARED = 1.0e-10f;
			GraphValueId normal = Fetch(b, place, GraphMeshChannel::Normal);
			const GraphValueId tangent4 = Fetch(b, place, GraphMeshChannel::Tangent);
			normal = b.Select(Less(b, b.Dot(normal, normal), b.Scalar(EPSILON_SQUARED)), Vec3(b, 0.0f, 1.0f, 0.0f), normal);
			GraphValueId tangent = b.Select(Less(b, b.Dot(tangent4, tangent4), b.Scalar(EPSILON_SQUARED)), Vec3(b, 0.0f, 0.0f, 1.0f), Comp(b, tangent4, "xyz"));
			normal = b.Unary(GraphOp::Normalize, normal);
			const GraphValueId bitangent = b.Unary(GraphOp::Normalize, b.Binary(GraphOp::Cross, normal, tangent));
			tangent = b.Binary(GraphOp::Cross, bitangent, normal);
			axes[0] = ApplyVector(b, frame, bitangent);
			axes[1] = ApplyVector(b, frame, normal);
			axes[2] = ApplyVector(b, frame, tangent);
		}

		Frame MeshTransform(GraphBodyContext& c) { return TransformInput(c, "Transform Position", "Transform Angles", "Transform Scale"); }

	} // anonymous namespace

	void SampleMeshBody(GraphBodyContext& c) {
		GraphProgramBuilder& b = c.builder;
		const GraphValueId mesh = In(c, "Mesh");
		const u32 placement = c.Setting(SampleMeshPlacementKey);
		const u32 surface = c.Setting(SampleMeshSurfaceKey);
		const GraphValueId count = placement == PlaceVertex ? MeshCount(b, mesh, 0) : (placement == PlaceEdge ? MeshCount(b, mesh, 1) : TriangleCount(b, mesh));
		const GraphValueId asked = In(c, placement == PlaceVertex ? "Vertex" : (placement == PlaceEdge ? "Index" : "Triangle"));
		const GraphValueId index = Address(b, asked, count, c.Setting(SampleMeshModeKey));
		GraphValueId coordinate = GRAPH_NO_VALUE;
		if (placement == PlaceEdge) {
			coordinate = In(c, "Edge");
		} else if (placement == PlaceSurface) {
			coordinate = In(c, surface == SurfaceUniform ? "Square" : "Barycentric");
		}
		const MeshPlace place = Place(b, mesh, placement, index, coordinate, surface);
		const Frame frame = MeshTransform(c);
		for (u32 output = 0; output < MESH_OUTPUT_COUNT; ++output) {
			if (c.Setting(output) != 0) {
				Out(c, MESH_OUTPUTS[output], MeshOutputValue(b, place, frame, output));
			}
		}
	}

	void PositionMeshBody(GraphBodyContext& c) {
		GraphProgramBuilder& b = c.builder;
		const GraphValueId mesh = In(c, "Mesh");
		const bool custom = c.Setting(MeshSpawnModeKey) == 1;
		const u32 placement = c.Setting(MeshPlacementKey);
		const u32 surface = c.Setting(MeshSurfaceKey);
		const GraphValueId count = placement == PlaceVertex ? MeshCount(b, mesh, 0) : (placement == PlaceEdge ? MeshCount(b, mesh, 1) : TriangleCount(b, mesh));
		GraphValueId index = GRAPH_NO_VALUE;
		if (custom) {
			const GraphValueId asked = In(c, placement == PlaceVertex ? "Vertex" : (placement == PlaceEdge ? "Index" : "Triangle"));
			index = Address(b, asked, count, c.Setting(MeshAddressingKey));
		} else {
			index = RandomIndex(c, count, placement == PlaceVertex ? 0u : (placement == PlaceEdge ? 1u : 3u));
		}
		GraphValueId coordinate = GRAPH_NO_VALUE;
		if (placement == PlaceEdge) {
			coordinate = custom ? In(c, "Edge") : Draw(c, 2);
		} else if (placement == PlaceSurface && custom) {
			coordinate = In(c, surface == SurfaceUniform ? "Square" : "Barycentric");
		} else if (placement == PlaceSurface) {
			const GraphValueId x = Draw(c, 4);
			const GraphValueId draws[] = {x, Draw(c, 5)};
			coordinate = b.Construct(T::Float2, draws);
		}
		const MeshPlace place = Place(b, mesh, placement, index, coordinate, surface);
		const Frame frame = MeshTransform(c);
		Out(c, "position", Compose(b, Current(c, "position"), MeshOutputValue(b, place, frame, OutPosition), c.Setting(MeshCompositionPositionKey), In(c, "Blend Position")));
		const u32 orientation = c.Setting(MeshOrientationKey);
		GraphValueId axes[3] = {GRAPH_NO_VALUE, GRAPH_NO_VALUE, GRAPH_NO_VALUE};
		if (orientation >= 2) {
			MeshAxes(b, place, frame, axes);
			const u32 composition = c.Setting(MeshCompositionAxesKey) == 0 ? 0u : 3u;
			const GraphValueId blend = In(c, "Blend Axes");
			Out(c, "axisX", Compose(b, Current(c, "axisX"), axes[0], composition, blend));
			Out(c, "axisY", Compose(b, Current(c, "axisY"), axes[1], composition, blend));
			Out(c, "axisZ", Compose(b, Current(c, "axisZ"), axes[2], composition, blend));
		}
		if (orientation == 1 || orientation == 3) {
			const GraphValueId direction = orientation == 3 ? axes[1] : MeshOutputValue(b, place, frame, OutNormal);
			Out(c, "direction", Compose(b, Current(c, "direction"), direction, c.Setting(MeshCompositionDirectionKey), In(c, "Blend Direction")));
		}
	}

	void SampleMeshIndexBody(GraphBodyContext& c) {
		GraphProgramBuilder& b = c.builder;
		const GraphValueId mesh = In(c, "Mesh");
		c.outputs[0] = b.MeshOp(GraphOp::MeshIndex, mesh, Address(b, In(c, "Index"), MeshCount(b, mesh, 1), AddressWrap), 0);
	}

	void MeshVertexCountBody(GraphBodyContext& c) { c.outputs[0] = MeshCount(c.builder, In(c, "Mesh"), 0); }

	void MeshIndexCountBody(GraphBodyContext& c) { c.outputs[0] = MeshCount(c.builder, In(c, "Mesh"), 1); }

	void MeshTriangleCountBody(GraphBodyContext& c) { c.outputs[0] = TriangleCount(c.builder, In(c, "Mesh")); }

	namespace {

		// 0 below length 0.00001 (must match the CPU expressions)
		GraphValueId NormalizedOrZero(GraphProgramBuilder& b, GraphValueId v) {
			return b.Select(Less(b, b.Dot(v, v), b.Scalar(1.0e-10f)), Vec3(b, 0.0f, 0.0f, 0.0f), b.Unary(GraphOp::Normalize, v));
		}

		void SequentialPlace(GraphBodyContext& c, GraphValueId at, u32 shape, u32 mode, GraphValueId& position, GraphValueId& direction) {
			GraphProgramBuilder& b = c.builder;
			if (shape == SequentialLine) {
				const GraphValueId count = In(c, "Count");
				const GraphValueId start = In(c, "Start");
				const GraphValueId end = In(c, "End");
				const GraphValueId t = b.Divide(Address(b, at, count, mode), b.Maximum(b.Subtract(count, b.Scalar(1.0f)), b.Scalar(1.0f)));
				position = b.Lerp(start, end, Vec3(b, t, t, t));
				direction = NormalizedOrZero(b, b.Subtract(end, start));
				return;
			}
			if (shape == SequentialCircle) {
				const GraphValueId count = In(c, "Count");
				const GraphValueId center = In(c, "Center");
				const GraphValueId addressed = mode == AddressWrap ? count : b.Add(count, b.Scalar(1.0f));
				const GraphValueId turn = b.Multiply(b.Divide(Address(b, at, addressed, mode), count), b.Scalar(2.0f * std::numbers::pi_v<f32>));
				const GraphValueId left = NormalizedOrZero(b, b.Binary(GraphOp::Cross, In(c, "Normal"), In(c, "Up")));
				const GraphValueId around = b.Add(b.Multiply(In(c, "Up"), b.Unary(GraphOp::Cosine, turn)), b.Multiply(left, b.Unary(GraphOp::Sine, turn)));
				position = b.Add(center, b.Multiply(around, In(c, "Radius")));
				direction = NormalizedOrZero(b, b.Subtract(position, center));
				return;
			}
			const GraphValueId counts = Vec3(b, In(c, "Count X"), In(c, "Count Y"), In(c, "Count Z"));
			const GraphValueId countY = Comp(b, counts, "y");
			const GraphValueId countZ = Comp(b, counts, "z");
			const GraphValueId cell = Address(b, at, b.Multiply(b.Multiply(Comp(b, counts, "x"), countY), countZ), mode);
			const GraphValueId z = b.Binary(GraphOp::Modulo, cell, countZ);
			const GraphValueId y = b.Binary(GraphOp::Modulo, b.Unary(GraphOp::Floor, b.Divide(cell, countZ)), countY);
			const GraphValueId x = b.Unary(GraphOp::Floor, b.Divide(cell, b.Multiply(countY, countZ)));
			const GraphValueId spans = b.Subtract(counts, Vec3(b, 1.0f, 1.0f, 1.0f));
			const GraphValueId spread = b.Unary(GraphOp::Saturate, spans);
			const GraphValueId t = b.Subtract(b.Multiply(b.Divide(Vec3(b, x, y, z), b.Maximum(spans, Vec3(b, 1.0f, 1.0f, 1.0f))), b.Scalar(2.0f)), b.Scalar(1.0f));
			const GraphValueId reach = b.Multiply(t, spread);
			const GraphValueId origin = In(c, "Origin");
			position = b.Add(b.Add(b.Add(origin, b.Multiply(In(c, "Axis X"), Comp(b, reach, "x"))), b.Multiply(In(c, "Axis Y"), Comp(b, reach, "y"))),
							 b.Multiply(In(c, "Axis Z"), Comp(b, reach, "z")));
			direction = NormalizedOrZero(b, b.Subtract(position, origin));
		}

	} // anonymous namespace

	void PositionSequentialBody(GraphBodyContext& c) {
		GraphProgramBuilder& b = c.builder;
		const u32 shape = c.Setting(SequentialShapeKey);
		const u32 mode = c.Setting(SequentialModeKey);
		const GraphValueId index = c.Setting(SequentialIndexKey) == 1 ? In(c, "Index") : Current(c, "particleId");
		if (c.Setting(SequentialWritePositionKey) != 0) {
			GraphValueId position = GRAPH_NO_VALUE;
			GraphValueId direction = GRAPH_NO_VALUE;
			SequentialPlace(c, b.Add(index, In(c, "Offset Index")), shape, mode, position, direction);
			Out(c, "position", Compose(b, Current(c, "position"), position, c.Setting(SequentialCompositionPositionKey), In(c, "Blend Position")));
			Out(c, "direction", Compose(b, Current(c, "direction"), direction, c.Setting(SequentialCompositionDirectionKey), In(c, "Blend Direction")));
		}
		if (c.Setting(SequentialWriteTargetKey) != 0) {
			GraphValueId target = GRAPH_NO_VALUE;
			GraphValueId ignored = GRAPH_NO_VALUE;
			SequentialPlace(c, b.Add(index, In(c, "Offset Target Index")), shape, mode, target, ignored);
			Out(c, "targetPosition",
				Compose(b, Current(c, "targetPosition"), target, c.Setting(SequentialCompositionTargetKey), In(c, "Blend Target Position")));
		}
	}

	namespace {
		GraphValueId ApplyForce(GraphBodyContext& c, u32 mode, GraphValueId value, GraphValueId drag) {
			GraphProgramBuilder& b = c.builder;
			const GraphValueId mass = Current(c, "mass");
			const GraphValueId delta = DeltaTime(c);
			if (mode == 0) {
				return b.Multiply(b.Divide(value, mass), delta);
			}
			return b.Multiply(b.Subtract(value, Current(c, "velocity")), b.Minimum(b.Scalar(1.0f), b.Divide(b.Multiply(drag, delta), mass)));
		}
	} // anonymous namespace

	void ForceBody(GraphBodyContext& c) {
		const u32 mode = c.Setting(0);
		const GraphValueId value = mode == 0 ? In(c, "Force") : In(c, "Velocity");
		c.outputs[0] = c.builder.Add(Current(c, "velocity"), ApplyForce(c, mode, value, In(c, "Drag")));
	}

	void GravityBody(GraphBodyContext& c) {
		c.outputs[0] = c.builder.Add(Current(c, "velocity"), c.builder.Multiply(c.In(0), DeltaTime(c)));
	}

	void LinearDragBody(GraphBodyContext& c) {
		GraphProgramBuilder& b = c.builder;
		GraphValueId coefficient = c.In(0);
		if (c.Setting(0) != 0) {
			const GraphValueId size = Current(c, "size");
			const GraphValueId scale = Current(c, "scale");
			coefficient = b.Multiply(coefficient, b.Multiply(b.Multiply(size, Comp(b, scale, "x")), b.Multiply(size, Comp(b, scale, "y"))));
		}
		const GraphValueId decay = b.Unary(GraphOp::Exponential, b.Unary(GraphOp::Negate, b.Divide(b.Multiply(coefficient, DeltaTime(c)), Current(c, "mass"))));
		c.outputs[0] = b.Multiply(Current(c, "velocity"), decay);
	}

	void TurbulenceBody(GraphBodyContext& c) {
		GraphProgramBuilder& b = c.builder;
		const Frame field = TransformInput(c, "Position", "Angles", "Scale");
		const GraphValueId coordinate = b.Add(InversePoint(b, field, Current(c, "position")), b.Scalar(0.5f));
		const GraphValueId parts[] = {In(c, "Frequency"), In(c, "Roughness"), In(c, "Lacunarity")};
		const GraphValueId curl =
			b.EffectNoise(GraphOp::CurlNoise, static_cast<GraphEffectNoise>(c.Setting(1)), coordinate, b.Construct(T::Float3, parts), In(c, "Octaves"));
		const GraphValueId value = b.Multiply(ApplyVector(b, field, curl), In(c, "Intensity"));
		c.outputs[0] = b.Add(Current(c, "velocity"), ApplyForce(c, c.Setting(0), value, In(c, "Drag")));
	}

	void ConformToSphereBody(GraphBodyContext& c) {
		GraphProgramBuilder& b = c.builder;
		const GraphValueId velocity = Current(c, "velocity");
		GraphValueId direction = b.Subtract(In(c, "Center"), Current(c, "position"));
		const GraphValueId toCenter = b.Unary(GraphOp::Length, direction);
		const GraphValueId toSurface = b.Subtract(toCenter, In(c, "Radius"));
		direction = b.Divide(direction, b.Maximum(b.Scalar(FLT_MIN_VALUE), toCenter));
		const GraphValueId normalSpeed = b.Dot(direction, velocity);
		const GraphValueId ratio = b.Ternary(GraphOp::Smoothstep, b.Scalar(0.0f), b.Multiply(In(c, "Stick Distance"), b.Scalar(2.0f)),
											 b.Unary(GraphOp::Absolute, toSurface));
		const GraphValueId target = b.Multiply(b.Multiply(b.Unary(GraphOp::Sign, toSurface), In(c, "Attraction Speed")), ratio);
		const GraphValueId change = b.Subtract(target, normalSpeed);
		const GraphValueId cap = b.Multiply(DeltaTime(c), b.Lerp(In(c, "Stick Force"), In(c, "Attraction Force"), ratio));
		const GraphValueId step = b.Multiply(b.Unary(GraphOp::Sign, change), b.Minimum(b.Unary(GraphOp::Absolute, change), cap));
		c.outputs[0] = b.Add(velocity, b.Divide(b.Multiply(step, direction), Current(c, "mass")));
	}

	void SampleSDFBody(GraphBodyContext& c) {
		GraphProgramBuilder& b = c.builder;
		const Frame field = TransformInput(c, "Center", "Angles", "Size");
		const GraphValueId sdf = In(c, "Texture");
		const GraphValueId level = In(c, "Mip Level");
		const GraphValueId scale = ScaleOf(b, field);
		const GraphValueId uvw = b.Add(InversePoint(b, field, In(c, "Position")), b.Scalar(0.5f));
		const GraphValueId clamped = b.Unary(GraphOp::Saturate, uvw);
		const GraphValueId fromMiddle = b.Subtract(uvw, b.Scalar(0.5f));
		const GraphValueId outside = Greater(b, Max3(b, b.Unary(GraphOp::Absolute, fromMiddle)), b.Scalar(0.5f));
		const GraphValueId sampled = SampleSDF(b, sdf, clamped, level);
		const GraphValueId beyond = b.Select(outside, b.Unary(GraphOp::Length, b.Multiply(scale, b.Subtract(uvw, clamped))), b.Scalar(0.0f));
		c.outputs[0] = b.Add(b.Multiply(sampled, Max3(b, scale)), beyond);
		GraphValueId gradient = CentralDerivatives(b, sdf, clamped, Vec3(b, 0.01f, 0.01f, 0.01f));
		gradient = b.Select(Less(b, sampled, b.Scalar(0.0f)), b.Unary(GraphOp::Negate, gradient), gradient);
		const GraphValueId normal = b.Select(outside, SafeNormalize(b, fromMiddle), SafeNormalize(b, gradient));
		c.outputs[1] = b.Unary(GraphOp::Negate, SafeNormalize(b, InverseTranspose(b, field, normal)));
	}

	void VectorFieldForceBody(GraphBodyContext& c) {
		GraphProgramBuilder& b = c.builder;
		const Frame field = TransformInput(c, "Center", "Angles", "Size");
		const GraphValueId velocity = Current(c, "velocity");
		const GraphValueId coord = InversePoint(b, field, Current(c, "position"));
		const GraphValueId operands[] = {In(c, "Vector Field"), b.Add(coord, b.Scalar(0.5f)), b.Scalar(0.0f)};
		GraphValueId value = Comp(b, b.TextureOp(GraphOp::SampleTextureLevel, operands), "xyz");
		if (c.Setting(DataEncodingKey) == 0) {
			value = b.Subtract(b.Multiply(value, b.Scalar(2.0f)), b.Scalar(1.0f));
		}
		const GraphValueId squared = b.Dot(value, value);
		value = ApplyVector(b, field, value);
		if (c.Setting(ConserveMagnitudeKey) != 0) {
			value = b.Multiply(value, b.Unary(GraphOp::SquareRoot, b.Divide(squared, b.Maximum(b.Scalar(EPSILON), b.Dot(value, value)))));
		}
		const u32 mode = c.Setting(VectorFieldModeKey);
		GraphValueId moved = b.Add(velocity, ApplyForce(c, mode, b.Multiply(value, In(c, "Intensity")), mode == 1 ? In(c, "Drag") : GRAPH_NO_VALUE));
		if (c.Setting(ClosedFieldKey) != 0) {
			const GraphValueId outside = Greater(b, Max3(b, b.Unary(GraphOp::Absolute, coord)), b.Scalar(0.5f));
			moved = b.Select(outside, velocity, moved);
		}
		c.outputs[0] = moved;
	}

	void ConformToSDFBody(GraphBodyContext& c) {
		GraphProgramBuilder& b = c.builder;
		const Frame field = TransformInput(c, "Center", "Angles", "Size");
		const GraphValueId sdf = In(c, "Distance Field");
		const GraphValueId position = Current(c, "position");
		const GraphValueId velocity = Current(c, "velocity");
		const GraphValueId local = InversePoint(b, field, position);
		const GraphValueId coord = b.Unary(GraphOp::Saturate, b.Add(local, b.Scalar(0.5f)));
		const GraphValueId inside = SampleSDF(b, sdf, coord);
		const GraphValueId fromMiddle = Max3(b, b.Unary(GraphOp::Absolute, local));
		const GraphValueId outside = Greater(b, fromMiddle, b.Scalar(0.5f));
		const GraphValueId distance = b.Select(outside, b.Add(inside, b.Subtract(fromMiddle, b.Scalar(0.5f))), inside);
		GraphValueId gradient = DerivativesFast(b, sdf, coord, inside);
		gradient = b.Select(Greater(b, inside, b.Scalar(0.0f)), b.Unary(GraphOp::Negate, gradient), gradient);
		const GraphValueId direction = b.Select(outside, b.Unary(GraphOp::Normalize, b.Subtract(field.origin, position)),
												SafeNormalize(b, InverseTranspose(b, field, gradient)));
		const GraphValueId toSurface = b.Multiply(b.Unary(GraphOp::Absolute, distance), Max3(b, ScaleOf(b, field)));
		const GraphValueId normalSpeed = b.Dot(direction, velocity);
		const GraphValueId ratio = b.Ternary(GraphOp::Smoothstep, b.Scalar(0.0f), b.Multiply(In(c, "Stick Distance"), b.Scalar(2.0f)),
											 b.Unary(GraphOp::Absolute, toSurface));
		const GraphValueId target = b.Multiply(b.Multiply(b.Unary(GraphOp::Sign, toSurface), In(c, "Attraction Speed")), ratio);
		const GraphValueId change = b.Subtract(target, normalSpeed);
		const GraphValueId cap = b.Multiply(DeltaTime(c), b.Lerp(In(c, "Stick Force"), In(c, "Attraction Force"), ratio));
		const GraphValueId step = b.Multiply(b.Unary(GraphOp::Sign, change), b.Minimum(b.Unary(GraphOp::Absolute, change), cap));
		c.outputs[0] = b.Add(velocity, b.Divide(b.Multiply(step, direction), Current(c, "mass")));
	}

	namespace {

		GraphValueId ParticleRadius(GraphBodyContext& c, u32 mode) {
			GraphProgramBuilder& b = c.builder;
			switch (mode) {
			case 1: {
				const GraphValueId scale = Current(c, "scale");
				const GraphValueId largest = b.Maximum(Comp(b, scale, "z"), b.Maximum(Comp(b, scale, "y"), Comp(b, scale, "x")));
				return b.Multiply(b.Multiply(largest, Current(c, "size")), b.Scalar(0.5f));
			}
			case 2:
				return In(c, "Particle Radius");
			default:
				return b.Scalar(0.0f);
			}
		}

		GraphValueId True(GraphProgramBuilder& b) {
			const f32 one = 1.0f;
			return b.Constant(T::Bool, {&one, 1});
		}

		GraphValueId Squared(GraphProgramBuilder& b, GraphValueId v) { return b.Dot(v, v); }

		GraphValueId ZeroScaled(GraphProgramBuilder& b, const Frame& frame) {
			const GraphValueId epsilon = b.Scalar(1.0e-10f);
			return Or(b, Less(b, Squared(b, frame.axes[0]), epsilon), Or(b, Less(b, Squared(b, frame.axes[1]), epsilon), Less(b, Squared(b, frame.axes[2]), epsilon)));
		}

		GraphValueId UniformScaled(GraphProgramBuilder& b, const Frame& frame) {
			const GraphValueId x = Squared(b, frame.axes[0]), y = Squared(b, frame.axes[1]), z = Squared(b, frame.axes[2]);
			const GraphValueId longest = b.Maximum(x, b.Maximum(y, z));
			const GraphValueId shortest = b.Minimum(x, b.Minimum(y, z));
			return Less(b, b.Divide(b.Unary(GraphOp::SquareRoot, longest), b.Unary(GraphOp::SquareRoot, shortest)), b.Scalar(1.0f + EPSILON));
		}

		GraphValueId NormalOut(GraphProgramBuilder& b, const Frame& frame, GraphValueId n) {
			const GraphValueId yz = b.Binary(GraphOp::Cross, frame.axes[1], frame.axes[2]);
			const GraphValueId zx = b.Binary(GraphOp::Cross, frame.axes[2], frame.axes[0]);
			const GraphValueId xy = b.Binary(GraphOp::Cross, frame.axes[0], frame.axes[1]);
			const GraphValueId sum = b.Add(b.Add(b.Multiply(yz, Comp(b, n, "x")), b.Multiply(zx, Comp(b, n, "y"))), b.Multiply(xy, Comp(b, n, "z")));
			return SafeNormalize(b, b.Divide(sum, Determinant(b, frame)));
		}

		GraphValueId PickLeast(GraphProgramBuilder& b, GraphValueId a, GraphValueId x, GraphValueId y, GraphValueId z) {
			const GraphValueId ax = Comp(b, a, "x"), ay = Comp(b, a, "y"), az = Comp(b, a, "z");
			const GraphValueId pickX = And(b, Less(b, ax, ay), Less(b, ax, az));
			return b.Select(pickX, x, b.Select(Less(b, ay, az), y, z));
		}

		struct Contact {
			GraphValueId hit = GRAPH_NO_VALUE;
			GraphValueId time = GRAPH_NO_VALUE;
			GraphValueId position = GRAPH_NO_VALUE;
			GraphValueId normal = GRAPH_NO_VALUE;
		};

		// unrolled bisection after Geometric Tools DistancePointEllipsoid
		GraphValueId EllipsoidPoint(GraphProgramBuilder& b, GraphValueId local, GraphValueId scale, GraphValueId grow, GraphValueId sign, bool inverted) {
			const GraphValueId z = b.Add(b.Unary(GraphOp::Absolute, local), b.Scalar(EPSILON));
			GraphValueId r = b.Multiply(scale, grow);
			const GraphValueId least = b.Minimum(Comp(b, r, "x"), b.Minimum(Comp(b, r, "y"), Comp(b, r, "z")));
			r = b.Multiply(r, b.Unary(GraphOp::Reciprocal, least));
			r = b.Multiply(r, r);
			const GraphValueId m = b.Multiply(r, z);
			const GraphValueId rx = Comp(b, r, "x"), ry = Comp(b, r, "y"), rz = Comp(b, r, "z");
			const GraphValueId start = b.Select(Less(b, ry, rx), b.Select(Less(b, rz, ry), Comp(b, z, "z"), Comp(b, z, "y")),
												b.Select(Less(b, rz, rx), Comp(b, z, "z"), Comp(b, z, "x")));
			GraphValueId t0 = b.Add(b.Scalar(-1.0f), start);
			GraphValueId t1 = inverted ? b.Subtract(b.Unary(GraphOp::Length, m), b.Scalar(1.0f)) : b.Scalar(0.0f);
			GraphValueId t = b.Scalar(0.0f);
			GraphValueId done = GRAPH_NO_VALUE;
			constexpr u32 ITERATION_COUNT = 16;
			for (u32 i = 0; i < ITERATION_COUNT; ++i) {
				const GraphValueId middle = b.Multiply(b.Add(t0, t1), b.Scalar(0.5f));
				t = done == GRAPH_NO_VALUE ? middle : b.Select(done, t, middle);
				const GraphValueId ratio = b.Multiply(m, b.Unary(GraphOp::Reciprocal, b.Add(t, r)));
				const GraphValueId dr = b.Subtract(b.Dot(ratio, ratio), b.Scalar(1.0f));
				const GraphValueId stop = Less(b, b.Unary(GraphOp::Absolute, b.Minimum(dr, b.Subtract(t1, t0))), b.Scalar(EPSILON * 10.0f));
				const GraphValueId go = done == GRAPH_NO_VALUE ? Not(b, stop) : And(b, Not(b, done), Not(b, stop));
				const GraphValueId outward = Greater(b, dr, b.Scalar(0.0f));
				t0 = b.Select(And(b, go, outward), t, t0);
				t1 = b.Select(And(b, go, Not(b, outward)), t, t1);
				done = done == GRAPH_NO_VALUE ? stop : Or(b, done, stop);
			}
			return b.Multiply(local, b.Divide(b.Add(r, b.Multiply(b.Scalar(EPSILON), sign)), b.Add(t, r)));
		}

		Contact SphereContact(GraphBodyContext& c, GraphValueId sign, GraphValueId radius, GraphValueId position, GraphValueId velocity, bool inverted) {
			GraphProgramBuilder& b = c.builder;
			Frame field = Scaled(b, TransformInput(c, "Position", "Angles", "Scale"), In(c, "Radius"));
			const GraphValueId zero = ZeroScaled(b, field);
			const GraphValueId uniform = UniformScaled(b, field);
			const GraphValueId scale = ScaleOf(b, field);
			GraphValueId grow = Vec3(b, 1.0f, 1.0f, 1.0f);
			if (c.Setting(CollisionRadiusModeKey) != 0) {
				grow = b.Add(b.Multiply(b.Multiply(sign, radius), b.Unary(GraphOp::Reciprocal, scale)), b.Scalar(1.0f));
				for (u32 i = 0; i < 3; ++i) {
					field.axes[i] = b.Multiply(field.axes[i], Comp(b, grow, std::string_view("xyz").substr(i, 1)));
				}
			}
			const GraphValueId local = InversePoint(b, field, position);
			const GraphValueId step = b.Multiply(InverseVector(b, field, velocity), DeltaTime(c));
			const GraphValueId qa = b.Dot(step, step);
			const GraphValueId qb = b.Dot(local, step);
			const GraphValueId qc = b.Subtract(b.Dot(local, local), b.Scalar(1.0f));
			const GraphValueId discriminant = b.Subtract(b.Multiply(qb, qb), b.Multiply(qa, qc));
			const GraphValueId root = b.Unary(GraphOp::SquareRoot, b.Maximum(discriminant, b.Scalar(0.0f)));
			const GraphValueId t0 = b.Unary(GraphOp::Negate, b.Add(qb, root));
			const GraphValueId t1 = b.Unary(GraphOp::Negate, b.Subtract(qb, root));
			const GraphValueId t = b.Select(Less(b, discriminant, b.Scalar(0.0f)), b.Scalar(-1.0f), b.Select(Less(b, t0, b.Scalar(0.0f)), t1, t0));
			const GraphValueId crosses = And(b, Not(b, Less(b, t, b.Scalar(0.0f))), Less(b, t, qa));
			const GraphValueId inside = Less(b, b.Multiply(qc, sign), b.Scalar(0.0f));
			const GraphValueId when = b.Divide(t, qa);
			const GraphValueId round = b.Multiply(local, b.Unary(GraphOp::ReciprocalSquareRoot, b.Add(qc, b.Scalar(1.0f))));
			const GraphValueId surface = b.Select(uniform, round, EllipsoidPoint(b, local, scale, grow, sign, inverted));
			const GraphValueId point = b.Select(crosses, b.Add(local, b.Multiply(when, step)), surface);
			Contact out;
			out.hit = And(b, Not(b, zero), Or(b, crosses, inside));
			out.time = b.Select(crosses, when, b.Scalar(0.0f));
			out.position = ApplyPoint(b, field, point);
			out.normal = NormalOut(b, field, b.Multiply(point, sign));
			return out;
		}

		Contact PlaneContact(GraphBodyContext& c, GraphValueId sign, GraphValueId radius, GraphValueId position, GraphValueId velocity) {
			GraphProgramBuilder& b = c.builder;
			const GraphValueId normal = b.Multiply(SafeNormalize(b, In(c, "Normal")), sign);
			const GraphValueId delta = DeltaTime(c);
			const GraphValueId distanceNow = b.Subtract(b.Subtract(b.Dot(position, normal), b.Dot(In(c, "Position"), normal)), radius);
			const GraphValueId distanceNext = b.Add(distanceNow, b.Multiply(b.Dot(velocity, normal), delta));
			const GraphValueId crosses = And(b, Greater(b, distanceNow, b.Scalar(0.0f)), Less(b, distanceNext, b.Scalar(0.0f)));
			const GraphValueId when = b.Unary(GraphOp::Saturate, b.Divide(distanceNow, b.Subtract(distanceNow, distanceNext)));
			Contact out;
			out.hit = Or(b, crosses, Less(b, distanceNow, b.Scalar(EPSILON)));
			out.time = b.Select(crosses, when, b.Scalar(0.0f));
			out.position = b.Select(crosses, b.Add(position, b.Multiply(b.Multiply(when, delta), velocity)), b.Subtract(position, b.Multiply(normal, distanceNow)));
			out.normal = normal;
			return out;
		}

		GraphValueId DistanceToBox(GraphProgramBuilder& b, GraphValueId p, GraphValueId extents) {
			const GraphValueId q = b.Subtract(b.Unary(GraphOp::Absolute, p), extents);
			const GraphValueId outside = b.Unary(GraphOp::Length, b.Maximum(q, b.Scalar(0.0f)));
			const GraphValueId largest = b.Maximum(Comp(b, q, "x"), b.Maximum(Comp(b, q, "y"), Comp(b, q, "z")));
			return b.Add(outside, b.Minimum(largest, b.Scalar(0.0f)));
		}

		Contact RayBox(GraphProgramBuilder& b, GraphValueId origin, GraphValueId step, GraphValueId extents, GraphValueId sign) {
			const GraphValueId signs = FastSign(b, step);
			const GraphValueId d = b.Multiply(signs, b.Maximum(b.Multiply(step, signs), b.Scalar(EPSILON)));
			const GraphValueId faces = b.Multiply(b.Unary(GraphOp::Negate, sign), signs);
			const GraphValueId ts = b.Divide(b.Subtract(b.Multiply(faces, extents), origin), d);
			constexpr std::string_view AXES[] = {"x", "y", "z"};
			constexpr std::string_view OTHERS[] = {"yz", "xz", "xy"};
			GraphValueId hits[3];
			GraphValueId filtered[3];
			for (u32 i = 0; i < 3; ++i) {
				const GraphValueId t = Comp(b, ts, AXES[i]);
				const GraphValueId at = b.Add(origin, b.Multiply(t, d));
				const GraphValueId inPlane = And(b, Less(b, b.Unary(GraphOp::Absolute, Comp(b, at, OTHERS[i].substr(0, 1))), Comp(b, extents, OTHERS[i].substr(0, 1))),
												 Less(b, b.Unary(GraphOp::Absolute, Comp(b, at, OTHERS[i].substr(1, 1))), Comp(b, extents, OTHERS[i].substr(1, 1))));
				hits[i] = And(b, And(b, Not(b, Less(b, t, b.Scalar(0.0f))), Less(b, t, b.Scalar(1.0f))), inPlane);
				filtered[i] = b.Add(b.Unary(GraphOp::Absolute, t), b.Select(hits[i], b.Scalar(0.0f), b.Scalar(1.0f)));
			}
			const GraphValueId tf = Vec3(b, filtered[0], filtered[1], filtered[2]);
			const GraphValueId zero = b.Scalar(0.0f);
			Contact out;
			out.hit = Or(b, hits[0], Or(b, hits[1], hits[2]));
			out.time = b.Select(out.hit, b.Minimum(filtered[0], b.Minimum(filtered[1], filtered[2])), zero);
			out.normal = b.Multiply(PickLeast(b, tf, Vec3(b, Comp(b, faces, "x"), zero, zero), Vec3(b, zero, Comp(b, faces, "y"), zero),
											  Vec3(b, zero, zero, Comp(b, faces, "z"))),
									sign);
			return out;
		}

		Contact BoxContact(GraphBodyContext& c, GraphValueId sign, GraphValueId radius, GraphValueId position, GraphValueId velocity, bool inverted) {
			GraphProgramBuilder& b = c.builder;
			const Frame field = TransformInput(c, "Center", "Angles", "Size");
			const GraphValueId scale = ScaleOf(b, field);
			const GraphValueId move = b.Multiply(velocity, DeltaTime(c));
			const GraphValueId next = InversePoint(b, field, b.Add(position, move));
			const GraphValueId half = b.Add(b.Scalar(0.5f), b.Multiply(b.Multiply(radius, b.Unary(GraphOp::Reciprocal, scale)), sign));
			const GraphValueId step = InverseVector(b, field, move);
			const GraphValueId local = InversePoint(b, field, position);
			const Contact ray = RayBox(b, local, step, half, sign);
			const GraphValueId reached = b.Add(local, b.Multiply(ray.time, step));
			const GraphValueId signs = FastSign(b, local);
			const GraphValueId zero = b.Scalar(0.0f);
			GraphValueId hit = GRAPH_NO_VALUE;
			GraphValueId point = GRAPH_NO_VALUE;
			GraphValueId normal = GRAPH_NO_VALUE;
			GraphValueId time = GRAPH_NO_VALUE;
			if (!inverted) {
				const GraphValueId distance = DistanceToBox(b, local, half);
				const GraphValueId near = Not(b, Greater(b, distance, b.Unary(GraphOp::Length, step)));
				const GraphValueId outside = Not(b, Less(b, distance, zero));
				const GraphValueId toEdge = b.Subtract(b.Unary(GraphOp::Absolute, local), half);
				const GraphValueId across = b.Multiply(b.Unary(GraphOp::Absolute, toEdge), scale);
				const GraphValueId pushed = b.Multiply(PickLeast(b, across, Vec3(b, Comp(b, signs, "x"), zero, zero), Vec3(b, zero, Comp(b, signs, "y"), zero),
																 Vec3(b, zero, zero, Comp(b, signs, "z"))),
													   sign);
				hit = And(b, near, b.Select(outside, ray.hit, True(b)));
				time = b.Select(outside, ray.time, zero);
				point = b.Select(outside, reached, b.Subtract(local, b.Multiply(pushed, toEdge)));
				normal = b.Select(outside, ray.normal, pushed);
			} else {
				const GraphValueId absNext = b.Unary(GraphOp::Absolute, next);
				const GraphValueId absNow = b.Unary(GraphOp::Absolute, local);
				GraphValueId leaving = GRAPH_NO_VALUE;
				GraphValueId within = GRAPH_NO_VALUE;
				for (std::string_view axis : {std::string_view("x"), std::string_view("y"), std::string_view("z")}) {
					const GraphValueId out = Greater(b, Comp(b, absNext, axis), Comp(b, half, axis));
					const GraphValueId in = Not(b, Greater(b, Comp(b, absNow, axis), Comp(b, half, axis)));
					leaving = leaving == GRAPH_NO_VALUE ? out : Or(b, leaving, out);
					within = within == GRAPH_NO_VALUE ? in : And(b, within, in);
				}
				const GraphValueId toEdge = b.Maximum(b.Subtract(absNow, half), zero);
				hit = And(b, leaving, b.Select(within, ray.hit, True(b)));
				time = b.Select(within, ray.time, zero);
				point = b.Select(within, reached, b.Subtract(local, b.Multiply(toEdge, signs)));
				normal = b.Select(within, ray.normal, b.Unary(GraphOp::Negate, b.Unary(GraphOp::Normalize, b.Multiply(signs, toEdge))));
			}
			Contact out;
			out.hit = And(b, Not(b, ZeroScaled(b, field)), hit);
			out.time = time;
			out.position = ApplyPoint(b, field, point);
			out.normal = NormalOut(b, field, normal);
			return out;
		}

		Contact ConeContact(GraphBodyContext& c, GraphValueId sign, GraphValueId radius, GraphValueId position, GraphValueId velocity, bool inverted) {
			GraphProgramBuilder& b = c.builder;
			const Frame field = TransformInput(c, "Position", "Angles", "Scale");
			const GraphValueId base = In(c, "Base Radius");
			const GraphValueId top = In(c, "Top Radius");
			const GraphValueId height = In(c, "Height");
			const GraphValueId next = InversePoint(b, field, b.Add(position, b.Multiply(velocity, DeltaTime(c))));
			const GraphValueId nx = Comp(b, next, "x"), ny = Comp(b, next, "y"), nz = Comp(b, next, "z");
			const GraphValueId coneRadius = b.Lerp(base, top, b.Unary(GraphOp::Saturate, b.Divide(ny, height)));
			const GraphValueId halfHeight = b.Multiply(height, b.Scalar(0.5f));
			GraphValueId fromMiddle = b.Unary(GraphOp::Absolute, b.Subtract(ny, halfHeight));
			GraphValueId distance = GRAPH_NO_VALUE;
			GraphValueId correctionXZ = b.Scalar(0.0f);
			GraphValueId correctionY = b.Scalar(0.0f);
			GraphValueId hit = GRAPH_NO_VALUE;
			if (c.Setting(CollisionRadiusModeKey) == 0) {
				const GraphValueId squared = b.Add(b.Multiply(nx, nx), b.Multiply(nz, nz));
				const GraphValueId limit = b.Multiply(coneRadius, coneRadius);
				hit = inverted ? Or(b, Greater(b, fromMiddle, halfHeight), Greater(b, squared, limit))
							   : And(b, Less(b, fromMiddle, halfHeight), Less(b, squared, limit));
				distance = b.Unary(GraphOp::SquareRoot, squared);
			} else {
				const GraphValueId inverseScale = b.Unary(GraphOp::Reciprocal, ScaleOf(b, field));
				distance = b.Maximum(b.Unary(GraphOp::Length, Vec3(b, nx, b.Scalar(0.0f), nz)), b.Scalar(EPSILON));
				const GraphValueId relative = b.Multiply(b.Divide(Vec3(b, nx, b.Scalar(0.0f), nz), distance), inverseScale);
				correctionXZ = b.Multiply(radius, b.Unary(GraphOp::Length, Vec3(b, Comp(b, relative, "x"), b.Scalar(0.0f), Comp(b, relative, "z"))));
				distance = b.Subtract(distance, b.Multiply(correctionXZ, sign));
				correctionY = b.Multiply(radius, Comp(b, inverseScale, "y"));
				fromMiddle = b.Subtract(fromMiddle, b.Multiply(correctionY, sign));
				hit = inverted ? Or(b, Greater(b, fromMiddle, halfHeight), Greater(b, distance, coneRadius))
							   : And(b, Less(b, fromMiddle, halfHeight), Less(b, distance, coneRadius));
			}
			const GraphValueId toCap = b.Multiply(sign, b.Subtract(halfHeight, fromMiddle));
			const GraphValueId toSide = b.Multiply(sign, b.Subtract(coneRadius, distance));
			GraphValueId point = InversePoint(b, field, position);
			const GraphValueId side = b.Unary(GraphOp::Normalize, Vec3(b, b.Multiply(b.Divide(nx, distance), height), b.Subtract(base, top), b.Multiply(b.Divide(nz, distance), height)));
			const GraphValueId cap = Vec3(b, b.Scalar(0.0f), b.Select(Less(b, ny, halfHeight), b.Scalar(-1.0f), b.Scalar(1.0f)), b.Scalar(0.0f));
			const GraphValueId bySide = Less(b, b.Multiply(sign, toSide), b.Multiply(sign, toCap));
			const GraphValueId normal = b.Multiply(b.Select(bySide, side, cap), sign);
			point = b.Select(bySide, b.Add(point, b.Multiply(b.Multiply(normal, Vec3(b, 1.0f, 0.0f, 1.0f)), toSide)), b.Add(point, b.Multiply(normal, toCap)));
			const GraphValueId px = Comp(b, point, "x"), py = Comp(b, point, "y"), pz = Comp(b, point, "z");
			const GraphValueId reach = b.Maximum(b.Unary(GraphOp::Length, Vec3(b, px, b.Scalar(0.0f), pz)), b.Scalar(EPSILON));
			const GraphValueId radiusThere = b.Lerp(base, top, b.Unary(GraphOp::Saturate, b.Divide(py, height)));
			if (!inverted) {
				const GraphValueId within = And(b, And(b, Greater(b, py, b.Unary(GraphOp::Negate, correctionY)), Less(b, py, b.Add(height, correctionY))),
												Less(b, reach, b.Add(radiusThere, correctionXZ)));
				const GraphValueId capped = Vec3(b, px, b.Select(Greater(b, py, b.Scalar(0.5f)), b.Unary(GraphOp::Negate, correctionY), b.Add(height, correctionY)), pz);
				const GraphValueId out = b.Divide(b.Add(radiusThere, correctionXZ), reach);
				const GraphValueId sided = Vec3(b, b.Multiply(px, out), py, b.Multiply(pz, out));
				const GraphValueId nearer = Less(b, Squared(b, b.Subtract(capped, point)), Squared(b, b.Subtract(sided, point)));
				point = b.Select(within, b.Select(nearer, capped, sided), point);
			} else {
				const GraphValueId y = b.Clamp(py, correctionY, b.Subtract(height, correctionY));
				const GraphValueId radiusAtY = b.Lerp(base, top, b.Divide(y, height));
				const GraphValueId in = b.Divide(b.Minimum(reach, b.Subtract(radiusAtY, correctionXZ)), reach);
				point = Vec3(b, b.Multiply(px, in), y, b.Multiply(pz, in));
			}
			Contact out;
			out.hit = And(b, Not(b, ZeroScaled(b, field)), hit);
			out.time = b.Scalar(0.0f);
			out.position = ApplyPoint(b, field, point);
			out.normal = NormalOut(b, field, normal);
			return out;
		}

	} // anonymous namespace

	namespace {

		Contact SDFContact(GraphBodyContext& c, GraphValueId sign, GraphValueId radius, GraphValueId position, GraphValueId velocity, bool inverted) {
			GraphProgramBuilder& b = c.builder;
			const Frame field = TransformInput(c, "Center", "Angles", "Size");
			const GraphValueId sdf = In(c, "Distance Field");
			const GraphValueId zero = b.Scalar(0.0f);
			const GraphValueId one = b.Scalar(1.0f);
			const GraphValueId inverseScale = b.Unary(GraphOp::Reciprocal, b.Unary(GraphOp::Absolute, ScaleOf(b, field)));
			const GraphValueId counts = TexelCounts(b, sdf);
			const GraphValueId uvStep = b.Unary(GraphOp::Reciprocal, counts);
			const GraphValueId largest = Max3(b, counts);
			const GraphValueId dimScale = b.Multiply(uvStep, largest);
			const GraphValueId dimInverseScale = b.Unary(GraphOp::Reciprocal, dimScale);
			const GraphValueId stepSize = b.Unary(GraphOp::Reciprocal, largest);

			GraphValueId local = InversePoint(b, field, position);
			const GraphValueId localVelocity = InverseVector(b, field, velocity);
			const GraphValueId delta = b.Multiply(localVelocity, DeltaTime(c));
			const GraphValueId travel = b.Unary(GraphOp::Length, delta);
			const GraphValueId direction = SafeNormalize(b, localVelocity);
			const GraphValueId offset = b.Multiply(b.Multiply(sign, b.Dot(b.Multiply(direction, direction), b.Multiply(inverseScale, dimInverseScale))), radius);
			const GraphValueId half = b.Add(b.Scalar(0.5f), b.Multiply(b.Multiply(radius, inverseScale), sign));
			const GraphValueId toBox = DistanceToBox(b, local, half);
			const GraphValueId here = b.Subtract(SampleSDF(b, sdf, b.Unary(GraphOp::Saturate, b.Add(local, b.Scalar(0.5f)))), offset);

			GraphValueId march = GRAPH_NO_VALUE;
			GraphValueId project = GRAPH_NO_VALUE;
			GraphValueId time = zero;
			if (!inverted) {
				const GraphValueId near = Not(b, Greater(b, toBox, travel));
				const GraphValueId outside = Not(b, Less(b, toBox, zero));
				const Contact entry = RayBox(b, local, delta, half, one);
				march = And(b, near, b.Select(outside, entry.hit, Greater(b, here, zero)));
				project = And(b, near, And(b, Not(b, outside), Not(b, Greater(b, here, zero))));
				time = b.Select(And(b, near, outside), entry.time, zero);
				local = b.Select(And(b, near, outside), b.Add(local, b.Multiply(entry.time, delta)), local);
			} else {
				const GraphValueId outside = Greater(b, toBox, zero);
				const GraphValueId distanceToEdge = b.Maximum(zero, b.Subtract(b.Unary(GraphOp::Absolute, local), half));
				const GraphValueId onBox = b.Subtract(local, b.Multiply(distanceToEdge, FastSign(b, local)));
				march = And(b, Not(b, outside), And(b, Less(b, here, zero), Not(b, Greater(b, b.Unary(GraphOp::Absolute, here), travel))));
				project = Or(b, outside, Not(b, Less(b, here, zero)));
				local = b.Select(outside, onBox, local);
			}

			GraphValueId uvw = b.Unary(GraphOp::Saturate, b.Add(local, b.Scalar(0.5f)));
			GraphValueId distance = b.Multiply(sign, b.Subtract(SampleSDF(b, sdf, uvw), offset));
			const GraphValueId reach = b.Unary(GraphOp::Length, b.Multiply(delta, dimInverseScale));
			const GraphValueId arrived = Less(b, distance, b.Scalar(EPSILON));
			GraphValueId marched = arrived;
			GraphValueId marchTime = b.Select(arrived, zero, time);
			GraphValueId done = arrived;
			constexpr u32 MARCH_STEPS = 8;
			for (u32 i = 0; i < MARCH_STEPS; ++i) {
				const GraphValueId moved = b.Add(uvw, b.Multiply(b.Multiply(direction, dimScale), distance));
				const GraphValueId next = b.Multiply(sign, b.Subtract(SampleSDF(b, sdf, moved), offset));
				const GraphValueId later = b.Add(marchTime, b.Divide(distance, reach));
				const GraphValueId met = Less(b, next, b.Scalar(EPSILON));
				const GraphValueId past = Greater(b, later, one);
				const GraphValueId go = Not(b, done);
				const GraphValueId within = And(b, Not(b, Greater(b, later, one)), Not(b, Less(b, later, zero)));
				marched = b.Select(And(b, go, met), within, b.Select(And(b, go, past), Not(b, True(b)), marched));
				uvw = b.Select(go, moved, uvw);
				marchTime = b.Select(go, later, marchTime);
				distance = b.Select(And(b, go, Not(b, Or(b, met, past))), next, distance);
				done = Or(b, done, Or(b, met, past));
			}
			const GraphValueId marchNormal = b.Multiply(sign, NormalOut(b, field, b.Multiply(CentralDerivatives(b, sdf, uvw, uvStep), dimScale)));
			const GraphValueId marchPoint = ApplyPoint(b, field, b.Subtract(uvw, b.Scalar(0.5f)));

			GraphValueId projected = b.Unary(GraphOp::Saturate, b.Add(local, b.Scalar(0.5f)));
			GraphValueId gradient = b.Unary(GraphOp::Normalize, CentralDerivatives(b, sdf, projected, uvStep));
			const GraphValueId held = b.Multiply(b.Multiply(sign, b.Dot(b.Multiply(gradient, gradient), b.Multiply(inverseScale, dimInverseScale))), radius);
			GraphValueId settled = GRAPH_NO_VALUE;
			constexpr u32 PROJECTION_STEPS = 4;
			for (u32 i = 0; i < PROJECTION_STEPS; ++i) {
				const GraphValueId apart = b.Multiply(sign, b.Subtract(SampleSDF(b, sdf, projected), held));
				const GraphValueId stop = Less(b, b.Unary(GraphOp::Absolute, apart), b.Scalar(EPSILON));
				settled = settled == GRAPH_NO_VALUE ? stop : Or(b, settled, stop);
				const GraphValueId remaining = b.Subtract(SampleSDF(b, sdf, projected), held);
				const GraphValueId turned = b.Unary(GraphOp::Normalize, CentralDerivatives(b, sdf, projected, uvStep));
				const GraphValueId stepped = b.Add(projected, b.Multiply(b.Multiply(b.Unary(GraphOp::Negate, remaining), turned), b.Divide(uvStep, stepSize)));
				projected = b.Select(settled, projected, stepped);
				gradient = b.Select(settled, gradient, turned);
			}
			const GraphValueId projectNormal = b.Multiply(sign, NormalOut(b, field, b.Multiply(gradient, dimScale)));
			const GraphValueId projectPoint = ApplyPoint(b, field, b.Subtract(projected, b.Scalar(0.5f)));

			Contact out;
			out.hit = And(b, Not(b, ZeroScaled(b, field)), Or(b, And(b, march, marched), And(b, Not(b, march), project)));
			out.time = b.Select(And(b, march, marched), marchTime, zero);
			out.position = b.Select(march, marchPoint, projectPoint);
			out.normal = b.Select(march, marchNormal, projectNormal);
			return out;
		}

	} // anonymous namespace

	namespace {

		struct CollisionSettings {
			u32 behavior = BehaviorCollision;
			u32 radiusMode = 0;
			u32 attributes = 0;
			bool rough = false;
			bool writeRoughNormal = true;
			bool overrideThreshold = false;
		};

		void Respond(GraphBodyContext& c, const CollisionSettings& settings, const Contact& contact, GraphValueId radius) {
			GraphProgramBuilder& b = c.builder;
			const GraphValueId position = Current(c, "position");
			const GraphValueId velocity = Current(c, "velocity");
			const GraphValueId hit = contact.hit;
			GraphValueId punctual = Less(b, b.Dot(contact.normal, Current(c, "oldVelocity")), b.Scalar(-EPSILON));
			const GraphValueId geometric = contact.normal;
			GraphValueId normal = contact.normal;
			if (settings.rough) {
				GraphValueId random = b.Subtract(b.Multiply(Vec3(b, Draw(c, 0), Draw(c, 1), Draw(c, 2)), b.Scalar(2.0f)), b.Scalar(1.0f));
				random = b.Unary(GraphOp::Normalize, random);
				random = b.Select(Less(b, b.Dot(random, normal), b.Scalar(0.0f)), b.Unary(GraphOp::Negate, random), random);
				normal = b.Select(punctual, b.Unary(GraphOp::Normalize, b.Lerp(geometric, random, In(c, "Roughness"))), normal);
			}
			if (settings.behavior == BehaviorCollision) {
				const GraphValueId delta = DeltaTime(c);
				const GraphValueId threshold = settings.overrideThreshold ? In(c, "Bounce Speed Threshold") : b.Scalar(1.0f);
				const GraphValueId along = b.Dot(normal, velocity);
				const GraphValueId normalPart = b.Multiply(along, normal);
				const GraphValueId tangentPart = b.Subtract(velocity, normalPart);
				const GraphValueId approaching = Less(b, along, b.Scalar(0.0f));
				const GraphValueId slow = Greater(b, along, b.Unary(GraphOp::Negate, threshold));
				GraphValueId restitution = b.Select(punctual, In(c, "Bounce"), b.Scalar(0.0f));
				restitution = b.Select(slow, b.Multiply(restitution, b.Divide(b.Unary(GraphOp::Negate, along), b.Add(threshold, b.Scalar(EPSILON)))), restitution);
				punctual = And(b, punctual, And(b, approaching, Not(b, slow)));
				GraphValueId bounced = b.Select(approaching, b.Subtract(velocity, b.Multiply(b.Add(b.Scalar(1.0f), restitution), normalPart)), velocity);
				const GraphValueId friction = b.Subtract(b.Scalar(1.0f), b.Unary(GraphOp::Exponential, b.Unary(GraphOp::Negate, b.Divide(b.Multiply(In(c, "Friction"), delta), Current(c, "mass")))));
				bounced = b.Subtract(bounced, b.Multiply(friction, tangentPart));
				const GraphValueId aged = b.Add(Current(c, "age"), b.Multiply(In(c, "Lifetime Loss"), Current(c, "lifetime")));
				const GraphValueId backtracked = b.Subtract(contact.position, b.Multiply(b.Multiply(delta, contact.time), bounced));
				Out(c, "velocity", b.Select(hit, bounced, velocity));
				Out(c, "age", b.Select(hit, aged, Current(c, "age")));
				Out(c, "position", b.Select(hit, backtracked, position));
			} else if (settings.behavior == BehaviorKill) {
				Out(c, "alive", And(b, Current(c, "alive"), Not(b, hit)));
			}
			if (settings.attributes != 0) {
				const GraphValueId written = settings.attributes == 1 && settings.behavior == BehaviorCollision ? And(b, hit, punctual) : hit;
				const GraphValueId count = Current(c, "collisionEventCount");
				Out(c, "hasCollisionEvent", Or(b, written, Current(c, "hasCollisionEvent")));
				Out(c, "collisionEventNormal", b.Select(written, settings.writeRoughNormal ? normal : geometric, Current(c, "collisionEventNormal")));
				Out(c, "collisionEventPosition", b.Select(written, b.Subtract(contact.position, b.Multiply(geometric, radius)), Current(c, "collisionEventPosition")));
				Out(c, "collisionEventCount", b.Select(written, b.Add(count, b.Scalar(1.0f)), count));
			}
		}

	} // anonymous namespace

	void CollisionShapeBody(GraphBodyContext& c) {
		GraphProgramBuilder& b = c.builder;
		CollisionSettings settings;
		settings.behavior = c.Setting(BehaviorKey);
		settings.radiusMode = c.Setting(CollisionRadiusModeKey);
		settings.attributes = c.Setting(CollisionAttributesKey);
		settings.rough = c.Setting(RoughSurfaceKey) != 0;
		settings.writeRoughNormal = c.Setting(WriteRoughNormalKey) != 0;
		settings.overrideThreshold = c.Setting(OverrideBounceThresholdKey) != 0;
		const bool inverted = c.Setting(CollisionModeKey) == 1;
		const GraphValueId sign = b.Scalar(inverted ? -1.0f : 1.0f);
		const GraphValueId radius = ParticleRadius(c, settings.radiusMode);
		const GraphValueId position = Current(c, "position");
		const GraphValueId velocity = Current(c, "velocity");
		Contact contact;
		switch (c.Setting(CollisionShapeKey)) {
		case CollideSphere:
			contact = SphereContact(c, sign, radius, position, velocity, inverted);
			break;
		case CollideBox:
			contact = BoxContact(c, sign, radius, position, velocity, inverted);
			break;
		case CollideCone:
			contact = ConeContact(c, sign, radius, position, velocity, inverted);
			break;
		case CollideSDF:
			contact = SDFContact(c, sign, radius, position, velocity, inverted);
			break;
		default:
			contact = PlaneContact(c, sign, radius, position, velocity);
			break;
		}
		Respond(c, settings, contact, radius);
	}

	namespace {

		struct Rows {
			GraphValueId row[4] = {GRAPH_NO_VALUE, GRAPH_NO_VALUE, GRAPH_NO_VALUE, GRAPH_NO_VALUE};
		};

		Rows CameraMatrix(GraphBodyContext& c, u32 matrix) {
			Rows out;
			for (u32 i = 0; i < 4; ++i) {
				out.row[i] = c.Binding(str(GRAPH_EFFECT_CAMERA_DEPTH_MATRICES[matrix]) + std::to_string(i));
			}
			return out;
		}

		GraphValueId Vec2(GraphProgramBuilder& b, GraphValueId x, GraphValueId y) {
			const GraphValueId parts[] = {x, y};
			return b.Construct(T::Float2, parts);
		}

		GraphValueId Transform(GraphProgramBuilder& b, const Rows& m, GraphValueId v) {
			const GraphValueId parts[] = {Comp(b, v, "x"), Comp(b, v, "y"), Comp(b, v, "z"), b.Scalar(1.0f)};
			const GraphValueId point = b.Construct(T::Float4, parts);
			const GraphValueId row[] = {b.Dot(m.row[0], point), b.Dot(m.row[1], point), b.Dot(m.row[2], point), b.Dot(m.row[3], point)};
			return b.Construct(T::Float4, row);
		}

		GraphValueId Rotate(GraphProgramBuilder& b, const Rows& m, GraphValueId v) {
			return Vec3(b, b.Dot(Comp(b, m.row[0], "xyz"), v), b.Dot(Comp(b, m.row[1], "xyz"), v), b.Dot(Comp(b, m.row[2], "xyz"), v));
		}

		GraphValueId Divided(GraphProgramBuilder& b, GraphValueId h) { return b.Divide(Comp(b, h, "xyz"), Comp(b, h, "w")); }

		GraphValueId DepthAt(GraphBodyContext& c, GraphValueId clip, GraphValueId size) {
			GraphProgramBuilder& b = c.builder;
			const GraphValueId half = b.Scalar(0.5f);
			const GraphValueId uv = Vec2(b, b.Add(b.Multiply(Comp(b, clip, "x"), half), half), b.Subtract(half, b.Multiply(Comp(b, clip, "y"), half)));
			const GraphValueId extent = Comp(b, size, "xy");
			const GraphValueId pixel = b.Clamp(b.Multiply(uv, extent), Vec2(b, b.Scalar(0.0f), b.Scalar(0.0f)), b.Subtract(extent, b.Scalar(1.0f)));
			const GraphValueId operands[] = {c.Binding(GRAPH_EFFECT_CAMERA_DEPTH), pixel};
			return Comp(b, b.TextureOp(GraphOp::LoadTexture, operands), "x");
		}

		GraphValueId Unproject(GraphProgramBuilder& b, const Rows& clipToView, GraphValueId clip, GraphValueId depth) {
			return Divided(b, Transform(b, clipToView, Vec3(b, Comp(b, clip, "x"), Comp(b, clip, "y"), depth)));
		}

	} // anonymous namespace

	void CollisionDepthBody(GraphBodyContext& c) {
		GraphProgramBuilder& b = c.builder;
		CollisionSettings settings;
		settings.behavior = c.Setting(DepthBehaviorKey);
		settings.radiusMode = c.Setting(DepthRadiusModeKey);
		settings.attributes = c.Setting(DepthAttributesKey);
		settings.rough = c.Setting(DepthRoughSurfaceKey) != 0;
		settings.writeRoughNormal = c.Setting(DepthWriteRoughNormalKey) != 0;
		settings.overrideThreshold = c.Setting(DepthOverrideBounceThresholdKey) != 0;
		const GraphValueId radius = ParticleRadius(c, settings.radiusMode);
		const Rows effectToView = CameraMatrix(c, 0);
		const Rows viewToEffect = CameraMatrix(c, 1);
		const Rows viewToClip = CameraMatrix(c, 2);
		const Rows clipToView = CameraMatrix(c, 3);
		const GraphValueId size = c.Binding(GRAPH_EFFECT_CAMERA_DEPTH_SIZE);
		const GraphValueId drawn = Less(b, b.Scalar(0.5f), Comp(b, size, "z"));
		const GraphValueId orthographic = Less(b, b.Scalar(0.5f), Comp(b, size, "w"));

		const GraphValueId next = b.Add(Current(c, "position"), b.Multiply(DeltaTime(c), Current(c, "velocity")));
		GraphValueId view = Comp(b, Transform(b, effectToView, next), "xyz");
		const GraphValueId clip = Divided(b, Transform(b, viewToClip, view));
		const GraphValueId onScreen = And(b, drawn, And(b, Less(b, b.Unary(GraphOp::Absolute, Comp(b, clip, "x")), b.Scalar(1.0f)), Less(b, b.Unary(GraphOp::Absolute, Comp(b, clip, "y")), b.Scalar(1.0f))));
		const GraphValueId eyeDepth = Comp(b, Unproject(b, clipToView, clip, DepthAt(c, clip, size)), "z");
		const GraphValueId viewZ = Comp(b, view, "z");
		GraphValueId behind = Greater(b, viewZ, b.Subtract(eyeDepth, radius));
		if (c.Setting(SurfaceThicknessKey) == 1) {
			behind = And(b, behind, Less(b, viewZ, b.Add(b.Add(eyeDepth, radius), In(c, "Surface Thickness"))));
		}

		const GraphValueId zero = b.Scalar(0.0f);
		const GraphValueId pixelOffset = b.Divide(b.Select(orthographic, b.Scalar(32.0f), b.Scalar(2.0f)), Comp(b, size, "xy"));
		const GraphValueId clip10 = b.Add(Comp(b, clip, "xy"), Vec2(b, Comp(b, pixelOffset, "x"), zero));
		const GraphValueId clip01 = b.Add(Comp(b, clip, "xy"), Vec2(b, zero, Comp(b, pixelOffset, "y")));
		const GraphValueId view10 = Unproject(b, clipToView, clip10, DepthAt(c, clip10, size));
		const GraphValueId view01 = Unproject(b, clipToView, clip01, DepthAt(c, clip01, size));

		const GraphValueId onPlane = Vec3(b, Comp(b, view, "x"), Comp(b, view, "y"), eyeDepth);
		const GraphValueId estimate = b.Unary(GraphOp::Normalize, b.Binary(GraphOp::Cross, b.Subtract(view01, onPlane), b.Subtract(view10, onPlane)));
		const GraphValueId ratio = b.Select(orthographic, b.Divide(b.Dot(onPlane, estimate), b.Dot(view, estimate)), b.Divide(eyeDepth, viewZ));
		view = b.Multiply(view, ratio);
		const GraphValueId viewNormal = b.Unary(GraphOp::Normalize, b.Binary(GraphOp::Cross, b.Subtract(view01, view), b.Subtract(view10, view)));
		view = b.Multiply(view, b.Subtract(b.Scalar(1.0f), b.Divide(radius, eyeDepth)));

		Contact contact;
		contact.hit = And(b, onScreen, behind);
		contact.time = zero;
		contact.position = Comp(b, Transform(b, viewToEffect, view), "xyz");
		contact.normal = b.Unary(GraphOp::Normalize, Rotate(b, viewToEffect, viewNormal));
		Respond(c, settings, contact, radius);
	}

	void PositionDepthBody(GraphBodyContext& c) {
		GraphProgramBuilder& b = c.builder;
		const Rows viewToEffect = CameraMatrix(c, 1);
		const Rows clipToView = CameraMatrix(c, 3);
		const GraphValueId size = c.Binding(GRAPH_EFFECT_CAMERA_DEPTH_SIZE);
		const GraphValueId extent = Comp(b, size, "xy");
		const GraphValueId one = b.Scalar(1.0f);
		GraphValueId uv = GRAPH_NO_VALUE;
		switch (c.Setting(DepthPositionModeKey)) {
		case DepthSequential: {
			const GraphValueId step = b.Maximum(b.Unary(GraphOp::Floor, In(c, "Grid Step")), one);
			const GraphValueId cells = b.Unary(GraphOp::Floor, b.Divide(extent, step));
			const GraphValueId across = b.Maximum(Comp(b, cells, "x"), one);
			const GraphValueId count = b.Maximum(b.Multiply(across, Comp(b, cells, "y")), one);
			const GraphValueId id = Current(c, "particleId");
			const GraphValueId wrapped = b.Subtract(id, b.Multiply(b.Unary(GraphOp::Floor, b.Divide(id, count)), count));
			const GraphValueId row = b.Unary(GraphOp::Floor, b.Divide(wrapped, across));
			const GraphValueId column = b.Subtract(wrapped, b.Multiply(row, across));
			const GraphValueId pixel = b.Add(b.Multiply(Vec2(b, column, row), step), b.Unary(GraphOp::Floor, b.Multiply(step, b.Scalar(0.5f))));
			uv = b.Divide(b.Add(pixel, b.Scalar(0.5f)), extent);
			break;
		}
		case DepthCustom:
			uv = In(c, "UV Spawn");
			break;
		default:
			uv = Vec2(b, Draw(c, 0), Draw(c, 1));
			break;
		}
		const GraphValueId clip = b.Subtract(b.Multiply(uv, b.Scalar(2.0f)), one);
		const GraphValueId depth = b.Subtract(one, DepthAt(c, clip, size));
		GraphValueId culled = GRAPH_NO_VALUE;
		switch (c.Setting(DepthCullModeKey)) {
		case DepthCullFarPlane:
			culled = Not(b, Less(b, depth, b.Scalar(1.0f - EPSILON)));
			break;
		case DepthCullRange: {
			const GraphValueId range = In(c, "Depth Range");
			culled = Or(b, Less(b, depth, Comp(b, range, "x")), Greater(b, depth, Comp(b, range, "y")));
			break;
		}
		default:
			break;
		}
		const GraphValueId scaled = b.Subtract(one, b.Multiply(depth, In(c, "Z Multiplier")));
		const GraphValueId placed = Divided(b, Transform(b, viewToEffect, Unproject(b, clipToView, clip, scaled)));
		Out(c, "position", Compose(b, Current(c, "position"), placed, c.Setting(DepthCompositionKey), In(c, "Blend Position")));
		if (culled != GRAPH_NO_VALUE) {
			Out(c, "alive", And(b, Current(c, "alive"), Not(b, culled)));
		}
	}

	void TriggerEventBody(GraphBodyContext& c) {
		GraphProgramBuilder& b = c.builder;
		const u32 mode = c.Setting(0);
		c.outputs[1] = b.Scalar(0.0f);
		switch (mode) {
		case TriggerOverTime:
		case TriggerOverDistance: {
			GraphValueId step = b.Multiply(DeltaTime(c), In(c, "Rate"));
			if (mode == TriggerOverDistance) {
				const GraphValueId moved = b.Add(b.Subtract(Current(c, "position"), Current(c, "oldPosition")), b.Multiply(Current(c, "velocity"), DeltaTime(c)));
				step = b.Multiply(b.Unary(GraphOp::Length, moved), In(c, "Rate"));
			}
			const GraphValueId accumulated = b.Add(Current(c, GraphEffectAttributes::RateCount(b.Node())), step);
			const GraphValueId whole = b.Unary(GraphOp::Floor, accumulated);
			c.outputs[1] = b.Subtract(accumulated, whole);
			c.outputs[0] = c.Setting(1) == 1 ? b.Binary(GraphOp::Minimum, whole, b.Scalar(1.0f)) : whole;
			return;
		}
		case TriggerOnDie: {
			const GraphValueId expiring = Less(b, Current(c, "lifetime"), b.Add(Current(c, "age"), DeltaTime(c)));
			const GraphValueId reaped = b.Binary(GraphOp::LogicalAnd, c.Binding(GRAPH_EFFECT_IMPLICIT_REAP), expiring);
			const GraphValueId died = b.Binary(GraphOp::LogicalOr, reaped, b.Unary(GraphOp::LogicalNot, Current(c, "alive")));
			c.outputs[0] = b.Select(died, In(c, "Count"), b.Scalar(0.0f));
			return;
		}
		case TriggerOnCollide:
			c.outputs[0] = b.Select(Current(c, "hasCollisionEvent"), In(c, "Count"), b.Scalar(0.0f));
			return;
		default:
			c.outputs[0] = In(c, "Count");
			return;
		}
	}

	void CameraFadeBody(GraphBodyContext& c) {
		GraphProgramBuilder& b = c.builder;
		const GraphValueId row = c.Binding(GRAPH_EFFECT_CAMERA_CLIP_W);
		const GraphValueId position = Current(c, "position");
		const GraphValueId axis = b.Swizzle(row, "xyz");
		const GraphValueId along = b.Dot(axis, position);
		const GraphValueId offset = b.Swizzle(row, "w");
		const GraphValueId distance = b.Add(along, offset);
		const GraphValueId faded = In(c, "Faded Distance");
		const GraphValueId visible = In(c, "Visible Distance");
		const GraphValueId past = b.Subtract(distance, faded);
		const GraphValueId span = b.Subtract(visible, faded);
		GraphValueId fade = b.Unary(GraphOp::Saturate, b.Divide(past, span));
		if (c.Setting(2) == 0) {
			fade = b.Select(c.Binding(GRAPH_EFFECT_SHADOW_PASS), b.Scalar(1.0f), fade);
		}
		const u32 mode = c.Setting(0);
		const GraphValueId color = Current(c, "color");
		const GraphValueId alpha = Current(c, "alpha");
		const GraphValueId alive = Current(c, "alive");
		Out(c, "color", mode == FadeAlpha ? color : b.Multiply(color, fade));
		Out(c, "alpha", mode == FadeColor ? alpha : b.Multiply(alpha, fade));
		if (c.Setting(1) != 0) {
			const GraphValueId gone = b.Binary(GraphOp::Equal, fade, b.Scalar(0.0f));
			Out(c, "alive", And(b, alive, Not(b, gone)));
		} else {
			Out(c, "alive", alive);
		}
	}

	namespace {

		GraphValueId SafeNormalizedCross(GraphProgramBuilder& b, GraphValueId x, GraphValueId y, GraphValueId fallback) {
			const GraphValueId crossed = b.Binary(GraphOp::Cross, x, y);
			return b.Select(Less(b, b.Dot(crossed, crossed), b.Scalar(EPSILON)), fallback, b.Unary(GraphOp::Normalize, crossed));
		}

		GraphValueId Normalized(GraphProgramBuilder& b, GraphValueId v) { return b.Unary(GraphOp::Normalize, v); }

		bool Supplied(const GraphBodyContext& c, std::string_view name) {
			return std::any_of(c.bindings.begin(), c.bindings.end(),
							   [name](const GraphBinding& binding) { return binding.name == name && binding.unavailable.empty(); });
		}

		GraphValueId StripTangent(GraphBodyContext& c, GraphValueId position) {
			GraphProgramBuilder& b = c.builder;
			const auto toward = [&](GraphValueId neighbor, bool after) {
				const GraphValueId at = Comp(b, neighbor, "xyz");
				const GraphValueId way = after ? b.Subtract(at, position) : b.Subtract(position, at);
				const GraphValueId square = b.Dot(way, way);
				const GraphValueId there = And(b, Greater(b, Comp(b, neighbor, "w"), b.Scalar(0.0f)), Greater(b, square, b.Scalar(EPSILON * EPSILON)));
				return b.Select(there, b.Multiply(way, b.Unary(GraphOp::ReciprocalSquareRoot, b.Maximum(square, b.Scalar(FLT_MIN_VALUE)))), Vec3(b, 0.0f, 0.0f, 0.0f));
			};
			return SafeNormalize(b, b.Add(toward(c.Binding(GRAPH_EFFECT_STRIP_PREVIOUS), false), toward(c.Binding(GRAPH_EFFECT_STRIP_NEXT), true)));
		}

	} // anonymous namespace

	void OrientBody(GraphBodyContext& c) {
		GraphProgramBuilder& b = c.builder;
		const u32 mode = c.Setting(0);
		const bool strip = Supplied(c, GRAPH_EFFECT_STRIP_PREVIOUS);
		if (strip && (mode == FaceCameraPlane || mode == FixedAxis || mode == AlongVelocity)) {
			c.error = std::format("is Orient: {} (a strip output doesn't support it, its quads run along the strip)", ORIENT_MODE_NAMES[mode]);
			return;
		}
		if (!strip && (mode == CustomZ || mode == CustomY)) {
			c.error = std::format("is Orient: {} (only a strip output supports it)", ORIENT_MODE_NAMES[mode]);
			return;
		}
		const GraphValueId position = Current(c, "position");
		const GraphValueId right = c.Binding(GRAPH_EFFECT_CAMERA_RIGHT);
		const GraphValueId up = c.Binding(GRAPH_EFFECT_CAMERA_UP);
		const GraphValueId forward = c.Binding(GRAPH_EFFECT_CAMERA_FORWARD);
		const GraphValueId eye = c.Binding(GRAPH_EFFECT_CAMERA_POSITION);
		const GraphValueId fallbackX = Vec3(b, 1.0f, 0.0f, 0.0f);
		GraphValueId axes[3] = {GRAPH_NO_VALUE, GRAPH_NO_VALUE, GRAPH_NO_VALUE};
		const auto lookAt = [&](GraphValueId toward) {
			axes[2] = Normalized(b, toward);
			axes[0] = SafeNormalizedCross(b, up, axes[2], fallbackX);
			axes[1] = b.Binary(GraphOp::Cross, axes[2], axes[0]);
		};
		const auto turnAbout = [&](GraphValueId along) {
			axes[1] = along;
			axes[0] = SafeNormalizedCross(b, along, b.Subtract(position, eye), fallbackX);
			axes[2] = b.Binary(GraphOp::Cross, axes[0], along);
		};
		const GraphValueId tangent = strip ? StripTangent(c, position) : GRAPH_NO_VALUE;
		const auto alongStrip = [&](GraphValueId face) {
			axes[0] = tangent;
			axes[1] = SafeNormalizedCross(b, face, tangent, Vec3(b, 0.0f, 1.0f, 0.0f));
			axes[2] = b.Binary(GraphOp::Cross, axes[0], axes[1]);
		};
		if (strip && mode != OrientAdvanced) {
			switch (mode) {
			case FaceCameraPosition:
				alongStrip(b.Select(c.Binding(GRAPH_EFFECT_CAMERA_ORTHOGRAPHIC), Normalized(b, forward), b.Subtract(position, eye)));
				break;
			case LookAtPosition:
				alongStrip(b.Unary(GraphOp::Negate, Normalized(b, b.Subtract(position, In(c, "Position")))));
				break;
			case LookAtLine: {
				const GraphValueId start = In(c, "Start");
				const GraphValueId direction = Normalized(b, b.Subtract(In(c, "End"), start));
				const GraphValueId target = b.Add(b.Multiply(b.Dot(b.Subtract(position, start), direction), direction), start);
				alongStrip(Normalized(b, b.Subtract(position, target)));
				break;
			}
			case CustomZ:
				alongStrip(b.Unary(GraphOp::Negate, In(c, "Front")));
				break;
			case CustomY:
				axes[0] = tangent;
				axes[2] = SafeNormalizedCross(b, tangent, In(c, "Up"), Vec3(b, 0.0f, 0.0f, 1.0f));
				axes[1] = b.Binary(GraphOp::Cross, axes[2], tangent);
				break;
			default:
				break;
			}
			Out(c, "axisX", axes[0]);
			Out(c, "axisY", axes[1]);
			Out(c, "axisZ", axes[2]);
			return;
		}
		switch (mode) {
		case FaceCameraPosition: {
			lookAt(b.Subtract(position, eye));
			const GraphValueId orthographic = c.Binding(GRAPH_EFFECT_CAMERA_ORTHOGRAPHIC);
			const GraphValueId plane[3] = {right, up, forward};
			for (u32 i = 0; i < 3; ++i) {
				axes[i] = Normalized(b, b.Select(orthographic, plane[i], axes[i]));
			}
			break;
		}
		case LookAtPosition:
			lookAt(b.Subtract(position, In(c, "Position")));
			break;
		case LookAtLine: {
			const GraphValueId start = In(c, "Start");
			const GraphValueId direction = Normalized(b, b.Subtract(In(c, "End"), start));
			const GraphValueId target = b.Add(b.Multiply(b.Dot(b.Subtract(position, start), direction), direction), start);
			lookAt(b.Subtract(position, target));
			break;
		}
		case OrientAdvanced: {
			constexpr u32 FIRST[] = {0, 1, 2, 1, 2, 0};
			constexpr u32 SECOND[] = {1, 2, 0, 0, 1, 2};
			constexpr u32 SECOND_PAIR[] = {AxesXZ, AxesYX, AxesZY, AxesYZ, AxesZX, AxesXY};
			constexpr std::string_view NAMES[] = {"Axis X", "Axis Y", "Axis Z"};
			const u32 pair = c.Setting(1);
			const u32 first = FIRST[pair];
			const u32 second = SECOND[pair];
			const u32 third = 3 - first - second;
			const GraphValueId given = In(c, NAMES[first]);
			const GraphValueId other = In(c, NAMES[second]);
			axes[first] = Normalized(b, given);
			axes[third] = Normalized(b, pair <= AxesZX ? b.Binary(GraphOp::Cross, given, other) : b.Binary(GraphOp::Cross, other, given));
			axes[second] = SECOND_PAIR[pair] <= AxesZX ? b.Binary(GraphOp::Cross, axes[first], axes[third]) : b.Binary(GraphOp::Cross, axes[third], axes[first]);
			break;
		}
		case FixedAxis:
			turnAbout(SafeNormalize(b, In(c, "Up")));
			break;
		case AlongVelocity:
			turnAbout(Normalized(b, Current(c, "velocity")));
			break;
		default:
			axes[0] = Normalized(b, right);
			axes[1] = Normalized(b, up);
			axes[2] = Normalized(b, forward);
			break;
		}
		Out(c, "axisX", axes[0]);
		Out(c, "axisY", axes[1]);
		Out(c, "axisZ", axes[2]);
	}

	namespace {

		GraphValueId RemapCurve(GraphProgramBuilder& b, std::span<const f32> curve, GraphValueId u, GraphValueId t) {
			const GraphValueId end = b.Add(u, t);
			const GraphValueId startWhole = b.Unary(GraphOp::Truncate, u);
			const GraphValueId endWhole = b.Unary(GraphOp::Truncate, end);
			const GraphValueId sampled = b.Subtract(GraphCurves::Sample(b, curve, b.Subtract(end, endWhole)), GraphCurves::Sample(b, curve, b.Subtract(u, startWhole)));
			return b.Add(sampled, b.Subtract(endWhole, startWhole));
		}

	} // anonymous namespace

	void FlipbookPlayerBody(GraphBodyContext& c) {
		GraphProgramBuilder& b = c.builder;
		const bool cycles = c.Setting(FlipbookModeKey) == ByCycles;
		const u32 range = c.Setting(AnimationRangeKey);
		const bool reverse = c.Setting(ReverseKey) != 0;
		const bool customCurve = cycles && c.Setting(CustomCurveKey) != 0;
		const bool clamp = c.Setting(ClampBlendingKey) != 0;
		const bool ranged = cycles || c.Setting(UseCustomRangeKey) != 0;
		const GraphValueId blending = cycles ? c.Binding(GRAPH_EFFECT_FLIPBOOK_BLENDING) : GRAPH_NO_VALUE;
		const GraphValueId age = Current(c, "age");
		const GraphValueId lifetime = Current(c, "lifetime");
		const GraphValueId zero = b.Scalar(0.0f);
		const GraphValueId one = b.Scalar(1.0f);
		const auto drawn = [&](GraphValueId between) {
			const GraphValueId r = b.Random(c.Binding(GRAPH_EFFECT_RANDOM_PARTICLE_CONSTANT), In(c, "Seed"), 0);
			return b.Add(b.Multiply(r, b.Subtract(Comp(b, between, "y"), Comp(b, between, "x"))), Comp(b, between, "x"));
		};
		GraphValueId texIndex = Current(c, "texIndex");

		GraphValueId first = GRAPH_NO_VALUE;
		GraphValueId length = GRAPH_NO_VALUE;
		GraphValueId stride = one;
		GraphValueId last = GRAPH_NO_VALUE;
		GraphValueId startFrame = GRAPH_NO_VALUE;
		GraphValueId endFrame = GRAPH_NO_VALUE;
		GraphValueId previous = GRAPH_NO_VALUE;
		if (ranged) {
			const GraphValueId size = c.Binding(GRAPH_EFFECT_FLIPBOOK_SIZE);
			const GraphValueId across = Comp(b, size, "x");
			switch (range) {
			case FlipbookRow:
				first = b.Multiply(b.Unary(GraphOp::Truncate, In(c, "Index")), across);
				length = across;
				break;
			case FlipbookColumn:
				first = b.Unary(GraphOp::Truncate, In(c, "Index"));
				length = Comp(b, size, "y");
				stride = across;
				break;
			case StartEndFrames: {
				const GraphValueId frames = In(c, "Frame Range");
				first = Comp(b, frames, "x");
				length = b.Add(b.Subtract(Comp(b, frames, "y"), first), one);
				break;
			}
			default:
				first = zero;
				length = b.Multiply(across, Comp(b, size, "y"));
				break;
			}
			last = b.Add(first, b.Multiply(b.Subtract(length, one), stride));
			if (cycles) {
				const GraphValueId startOffset = In(c, "Start Offset");
				const GraphValueId offset =
					b.Multiply(b.Binary(GraphOp::Modulo, reverse ? b.Unary(GraphOp::Negate, startOffset) : startOffset, length), stride);
				const GraphValueId fromStart = b.Select(Less(b, offset, zero), b.Add(b.Add(last, offset), stride), b.Add(first, offset));
				const GraphValueId toEnd = b.Select(Greater(b, offset, zero), b.Subtract(b.Add(first, offset), stride), b.Add(last, offset));
				startFrame = reverse ? b.Select(blending, toEnd, fromStart) : fromStart;
				endFrame = reverse ? b.Select(blending, fromStart, toEnd) : toEnd;
				texIndex = b.Select(Not(b, Greater(b, age, zero)), startFrame, texIndex);
			}
			if (range == FlipbookColumn) {
				previous = b.Unary(GraphOp::Floor, texIndex);
			}
		}

		GraphValueId rate = GRAPH_NO_VALUE;
		GraphValueId cycleCount = GRAPH_NO_VALUE;
		const auto curve = [&](GraphValueId t) { return GraphCurves::Sample(b, b.ConstantOf(In(c, "Frame Rate Curve")), t); };
		if (!cycles) {
			switch (c.Setting(FrameRateModeKey)) {
			case RateRandom:
				rate = drawn(In(c, "Frame Rate Range"));
				break;
			case RateOverLifetime:
				rate = curve(b.Divide(age, lifetime));
				break;
			case RateBySpeed: {
				const GraphValueId speeds = In(c, "Speed Range");
				const GraphValueId slowest = Comp(b, speeds, "x");
				const GraphValueId across = b.Unary(GraphOp::Reciprocal, b.Subtract(Comp(b, speeds, "y"), slowest));
				rate = curve(b.Unary(GraphOp::Saturate, b.Multiply(b.Subtract(b.Unary(GraphOp::Length, Current(c, "velocity")), slowest), across)));
				break;
			}
			case RateCustom:
				rate = curve(b.Unary(GraphOp::Saturate, In(c, "Sample Time")));
				break;
			default:
				rate = In(c, "Frame Rate");
				break;
			}
		} else {
			switch (c.Setting(CyclesModeKey)) {
			case CyclesRandom:
				cycleCount = drawn(In(c, "Cycles Range"));
				break;
			case CyclesRandomFullCycle: {
				const GraphValueId between = In(c, "Cycles Range");
				const GraphValueId count = b.Unary(GraphOp::Truncate, b.Add(b.Subtract(Comp(b, between, "y"), Comp(b, between, "x")), one));
				const GraphValueId r = b.Random(c.Binding(GRAPH_EFFECT_RANDOM_PARTICLE_CONSTANT), In(c, "Seed"), 0);
				cycleCount = b.Add(b.Unary(GraphOp::Floor, b.Multiply(r, count)), Comp(b, between, "x"));
				break;
			}
			default:
				cycleCount = In(c, "Cycles");
				break;
			}
			rate = b.Divide(b.Multiply(cycleCount, length), lifetime);
		}

		const GraphValueId delta = DeltaTime(c);
		const GraphValueId step = b.Multiply(rate, reverse ? b.Unary(GraphOp::Negate, delta) : delta);
		if (customCurve) {
			texIndex = b.Add(texIndex, RemapCurve(b, b.ConstantOf(In(c, "Custom Curve")), b.Divide(b.Multiply(age, rate), length), step));
		} else {
			texIndex = b.Add(texIndex, step);
		}
		GraphValueId blendTo = GRAPH_NO_VALUE;
		if (ranged && range != FlipbookColumn) {
			texIndex = b.Add(b.Binary(GraphOp::Modulo, b.Add(b.Subtract(texIndex, first), length), length), first);
			if (range != EntireFlipbook) {
				const GraphValueId current = b.Unary(GraphOp::Floor, texIndex);
				blendTo = b.Select(Greater(b, texIndex, last), b.Subtract(first, current), one);
			}
		} else if (ranged) {
			const GraphValueId current = b.Unary(GraphOp::Floor, texIndex);
			const GraphValueId within = b.Subtract(texIndex, current);
			const GraphValueId row = b.Unary(GraphOp::Floor, b.Divide(previous, stride));
			const GraphValueId advanced = b.Add(row, b.Subtract(current, previous));
			texIndex = b.Add(b.Add(b.Multiply(b.Binary(GraphOp::Modulo, b.Add(advanced, length), length), stride), first), within);
			blendTo = stride;
		}

		if (customCurve) {
			const GraphValueId held = clamp ? True(b) : Not(b, blending);
			const GraphValueId cycle = b.Divide(b.Multiply(cycleCount, age), lifetime);
			const GraphValueId closing = And(b, held, Greater(b, cycle, b.Subtract(cycleCount, b.Scalar(0.9f))));
			texIndex = b.Select(closing, reverse ? b.Maximum(texIndex, endFrame) : b.Minimum(texIndex, endFrame), texIndex);
		} else if (cycles && clamp) {
			const GraphValueId half = b.Divide(b.Scalar(0.5f), rate);
			texIndex = b.Select(And(b, blending, Not(b, Greater(b, age, half))), startFrame, texIndex);
			texIndex = b.Select(And(b, blending, Not(b, Greater(b, b.Subtract(lifetime, age), half))), endFrame, texIndex);
		}
		Out(c, "texIndex", texIndex);
		if (blendTo != GRAPH_NO_VALUE) {
			Out(c, "texIndexBlend", blendTo);
		}
	}

} // namespace Shalltear::GraphEffectBlocks
