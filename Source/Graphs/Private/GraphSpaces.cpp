#include "GraphSpaces.h"

namespace Shalltear {

	namespace {

		using B = GraphProgramBuilder;

		constexpr f32 FLOAT_MIN = 1.175494351e-38f;

		GraphValueId Point(B& b, GraphValueId v, f32 w = 1.0f) {
			const GraphValueId parts[] = {v, b.Scalar(w)};
			return b.Construct(GraphValueType::Float4, parts);
		}

		GraphValueId Mul(B& b, GraphValueId left, GraphValueId right) { return b.Binary(GraphOp::MatrixMultiply, left, right); }

		GraphValueId Upper(B& b, GraphValueId matrix) { return b.Convert(matrix, GraphValueType::Float3x3); }

		GraphValueId TransformPoint(B& b, GraphValueId matrix, GraphValueId p) { return b.Swizzle(Mul(b, matrix, Point(b, p)), "xyz"); }

		GraphValueId Normalized(B& b, GraphValueId v) { return b.Unary(GraphOp::Normalize, v); }

		GraphValueId SignOf(B& b, GraphValueId value) {
			return b.Select(b.Binary(GraphOp::Less, value, b.Scalar(0.0f)), b.Scalar(-1.0f), b.Scalar(1.0f));
		}

		// rows T, B, N in world space
		struct Frame {
			GraphValueId tangent = GRAPH_NO_VALUE;
			GraphValueId bitangent = GRAPH_NO_VALUE;
			GraphValueId normal = GRAPH_NO_VALUE;
			GraphValueId matrix = GRAPH_NO_VALUE;
		};

		Frame ReadFrame(GraphBodyContext& c) {
			Frame frame;
			frame.tangent = c.Binding("WorldSpaceTangent");
			frame.bitangent = c.Binding("WorldSpaceBiTangent");
			frame.normal = c.Binding("WorldSpaceNormal");
			const GraphValueId rows[] = {frame.tangent, frame.bitangent, frame.normal};
			frame.matrix = c.builder.Construct(GraphValueType::Float3x3, rows);
			return frame;
		}

		struct Cofactors {
			GraphValueId matrix = GRAPH_NO_VALUE;
			GraphValueId determinant = GRAPH_NO_VALUE;
		};

		Cofactors Cofactor(B& b, const Frame& frame) {
			GraphValueId column0 = b.Binary(GraphOp::Cross, frame.bitangent, frame.normal);
			GraphValueId column1 = b.Binary(GraphOp::Cross, frame.normal, frame.tangent);
			GraphValueId column2 = b.Binary(GraphOp::Cross, frame.tangent, frame.bitangent);
			const GraphValueId rows[] = {column0, column1, column2};
			return {b.Construct(GraphValueType::Float3x3, rows), b.Dot(frame.tangent, column0)};
		}

		GraphValueId WorldToObject(GraphBodyContext& c, GraphValueId in, GraphConversion type, bool normalize) {
			B& b = c.builder;
			switch (type) {
			case GraphConversion::Position:
				return TransformPoint(b, c.Binding("WorldToObject"), in);
			case GraphConversion::Direction: {
				GraphValueId out = Mul(b, Upper(b, c.Binding("WorldToObject")), in);
				return normalize ? Normalized(b, out) : out;
			}
			case GraphConversion::Normal: {
				GraphValueId out = Mul(b, in, Upper(b, c.Binding("ObjectToWorld")));
				return normalize ? GraphSpaces::SafeNormalize(b, out) : out;
			}
			}
			return GRAPH_NO_VALUE;
		}

		GraphValueId ObjectToWorld(GraphBodyContext& c, GraphValueId in, GraphConversion type, bool normalize) {
			B& b = c.builder;
			switch (type) {
			case GraphConversion::Position:
				return TransformPoint(b, c.Binding("ObjectToWorld"), in);
			case GraphConversion::Direction: {
				GraphValueId out = Mul(b, Upper(b, c.Binding("ObjectToWorld")), in);
				return normalize ? GraphSpaces::SafeNormalize(b, out) : out;
			}
			case GraphConversion::Normal: {
				GraphValueId out = Mul(b, in, Upper(b, c.Binding("WorldToObject")));
				return normalize ? GraphSpaces::SafeNormalize(b, out) : out;
			}
			}
			return GRAPH_NO_VALUE;
		}

		GraphValueId WorldToView(GraphBodyContext& c, GraphValueId in, GraphConversion type, bool normalize) {
			B& b = c.builder;
			if (type == GraphConversion::Position) {
				return TransformPoint(b, c.Binding("View"), in);
			}
			GraphValueId out = Mul(b, Upper(b, c.Binding("View")), in);
			return normalize ? Normalized(b, out) : out;
		}

		GraphValueId ViewToWorld(GraphBodyContext& c, GraphValueId in, GraphConversion type, bool normalize) {
			B& b = c.builder;
			if (type == GraphConversion::Position) {
				return TransformPoint(b, c.Binding("InverseView"), in);
			}
			GraphValueId out = Mul(b, Upper(b, c.Binding("InverseView")), in);
			return normalize ? Normalized(b, out) : out;
		}

		GraphValueId WorldToTangent(GraphBodyContext& c, GraphValueId in, GraphConversion type, bool normalize) {
			B& b = c.builder;
			Frame frame = ReadFrame(c);
			switch (type) {
			case GraphConversion::Position:
				return Mul(b, frame.matrix, b.Subtract(in, c.Binding("WorldSpacePosition")));
			case GraphConversion::Direction: {
				GraphValueId out = Mul(b, frame.matrix, in);
				return normalize ? GraphSpaces::SafeNormalize(b, out) : out;
			}
			case GraphConversion::Normal: {
				Cofactors cofactors = Cofactor(b, frame);
				GraphValueId out = Mul(b, cofactors.matrix, in);
				return normalize ? GraphSpaces::SafeNormalize(b, b.Multiply(out, SignOf(b, cofactors.determinant)))
								 : b.Divide(out, cofactors.determinant);
			}
			}
			return GRAPH_NO_VALUE;
		}

		GraphValueId TangentToWorld(GraphBodyContext& c, GraphValueId in, GraphConversion type, bool normalize) {
			B& b = c.builder;
			Frame frame = ReadFrame(c);
			switch (type) {
			case GraphConversion::Position: {
				Cofactors cofactors = Cofactor(b, frame);
				return b.Add(b.Divide(Mul(b, in, cofactors.matrix), cofactors.determinant), c.Binding("WorldSpacePosition"));
			}
			case GraphConversion::Direction: {
				Cofactors cofactors = Cofactor(b, frame);
				GraphValueId out = Mul(b, in, cofactors.matrix);
				return normalize ? GraphSpaces::SafeNormalize(b, b.Multiply(out, SignOf(b, cofactors.determinant)))
								 : b.Divide(out, cofactors.determinant);
			}
			case GraphConversion::Normal: {
				GraphValueId out = Mul(b, in, frame.matrix);
				return normalize ? GraphSpaces::SafeNormalize(b, out) : out;
			}
			}
			return GRAPH_NO_VALUE;
		}

		GraphValueId WorldToWorld(GraphBodyContext& c, GraphValueId in, GraphConversion type, bool normalize) {
			if (type == GraphConversion::Position || !normalize) {
				return in;
			}
			return GraphSpaces::SafeNormalize(c.builder, in);
		}

		// y flipped (row 0 = 0), z stays device depth
		GraphValueId ClipToScreen(B& b, GraphValueId clip) {
			GraphValueId ndc = b.Divide(b.Swizzle(clip, "xyz"), b.Swizzle(clip, "w"));
			GraphValueId half = b.Scalar(0.5f);
			const GraphValueId parts[] = {b.Add(b.Multiply(b.Swizzle(ndc, "x"), half), half),
										  b.Subtract(half, b.Multiply(b.Swizzle(ndc, "y"), half)), b.Swizzle(ndc, "z")};
			return b.Construct(GraphValueType::Float3, parts);
		}

		GraphValueId WorldToScreen(GraphBodyContext& c, GraphValueId in, GraphConversion type, bool normalize) {
			B& b = c.builder;
			if (type == GraphConversion::Position) {
				return ClipToScreen(b, Mul(b, c.Binding("ViewProjection"), Point(b, in)));
			}
			GraphValueId direction = Mul(b, Upper(b, c.Binding("ViewProjection")), in);
			return ClipToScreen(b, Point(b, normalize ? Normalized(b, direction) : direction));
		}

		GraphValueId ScreenToWorld(GraphBodyContext& c, GraphValueId in, GraphConversion type) {
			B& b = c.builder;
			GraphValueId one = b.Scalar(1.0f);
			GraphValueId two = b.Scalar(2.0f);
			const GraphValueId parts[] = {b.Subtract(b.Multiply(b.Swizzle(in, "x"), two), one), b.Subtract(one, b.Multiply(b.Swizzle(in, "y"), two)),
										  b.Swizzle(in, "z")};
			GraphValueId clip = b.Construct(GraphValueType::Float3, parts);
			GraphValueId inverse = c.Binding("InverseViewProjection");
			if (type == GraphConversion::Position) {
				GraphValueId world = Mul(b, inverse, Point(b, clip));
				return b.Divide(b.Swizzle(world, "xyz"), b.Swizzle(world, "w"));
			}
			return b.Swizzle(Mul(b, inverse, Point(b, clip, 0.0f)), "xyz");
		}

	} // anonymous namespace

	namespace GraphSpaces {

		GraphValueId SafeNormalize(GraphProgramBuilder& b, GraphValueId v) {
			GraphValueId lengthSquared = b.Maximum(b.Dot(v, v), b.Scalar(FLOAT_MIN));
			return b.Multiply(v, b.Unary(GraphOp::ReciprocalSquareRoot, lengthSquared));
		}

		GraphValueId Transform(GraphBodyContext& c, GraphValueId in, GraphSpace from, GraphSpace to, GraphConversion type, bool normalize) {
			B& b = c.builder;
			if (in == GRAPH_NO_VALUE) {
				return GRAPH_NO_VALUE;
			}
			if (from == to) {
				return normalize && type != GraphConversion::Position ? SafeNormalize(b, in) : in;
			}
			auto viaWorld = [&]() {
				GraphValueId world = Transform(c, in, from, GraphSpace::World, type, false);
				return Transform(c, world, GraphSpace::World, to, type, normalize);
			};
			const bool position = type == GraphConversion::Position;
			switch (from) {
			case GraphSpace::Object:
				if (to == GraphSpace::World || (to == GraphSpace::AbsoluteWorld && !position)) {
					return ObjectToWorld(c, in, type, normalize);
				}
				if (to == GraphSpace::Screen && position) {
					return ClipToScreen(b, Mul(b, c.Binding("ViewProjection"), Mul(b, c.Binding("ObjectToWorld"), Point(b, in))));
				}
				return viaWorld();
			case GraphSpace::View:
				if (to == GraphSpace::World) {
					return ViewToWorld(c, in, type, normalize);
				}
				if (to == GraphSpace::Screen && position) {
					return ClipToScreen(b, Mul(b, c.Binding("Projection"), Point(b, in)));
				}
				return viaWorld();
			case GraphSpace::World:
				switch (to) {
				case GraphSpace::Object:
					return WorldToObject(c, in, type, normalize);
				case GraphSpace::View:
					return WorldToView(c, in, type, normalize);
				case GraphSpace::Tangent:
					return WorldToTangent(c, in, type, normalize);
				case GraphSpace::AbsoluteWorld:
					return WorldToWorld(c, in, type, normalize);
				case GraphSpace::Screen:
					return WorldToScreen(c, in, type, normalize);
				case GraphSpace::World:
					break;
				}
				return in;
			case GraphSpace::Tangent:
				return to == GraphSpace::World ? TangentToWorld(c, in, type, normalize) : viaWorld();
			case GraphSpace::AbsoluteWorld:
				return to == GraphSpace::World ? WorldToWorld(c, in, type, normalize) : viaWorld();
			case GraphSpace::Screen:
				return to == GraphSpace::World ? ScreenToWorld(c, in, type) : viaWorld();
			}
			return GRAPH_NO_VALUE;
		}

	} // namespace GraphSpaces

} // namespace Shalltear
