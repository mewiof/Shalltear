#pragma once

#include "GraphsAPI.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear {

	enum class GraphValueType : u8 {
		None = 0,
		Bool,
		Float,
		Float2,
		Float3,
		Float4,
		Float2x2,
		Float3x3,
		Float4x4,
		Texture2D,
		Gradient,
		Texture2DArray,
		Texture3D,
		TextureCube,
		Curve,
		Mesh
	};

	inline constexpr u32 GRAPH_GRADIENT_KEYS = 8;
	inline constexpr u32 GRAPH_GRADIENT_COLORS = 3;
	inline constexpr u32 GRAPH_GRADIENT_ALPHAS = GRAPH_GRADIENT_COLORS + GRAPH_GRADIENT_KEYS * 4;
	inline constexpr u32 GRAPH_GRADIENT_COMPONENTS = GRAPH_GRADIENT_ALPHAS + GRAPH_GRADIENT_KEYS * 2;

	inline constexpr u32 GRAPH_CURVE_KEYS = 64;
	inline constexpr u32 GRAPH_CURVE_HEADER = 3;
	inline constexpr u32 GRAPH_CURVE_KEY_FLOATS = 5;
	inline constexpr u32 GRAPH_CURVE_COMPONENTS = GRAPH_CURVE_HEADER + GRAPH_CURVE_KEYS * GRAPH_CURVE_KEY_FLOATS;

	namespace GraphTypes {
		SHALLTEAR_GRAPHS_API u32 Components(GraphValueType type);
		SHALLTEAR_GRAPHS_API u32 VectorWidth(GraphValueType type);
		SHALLTEAR_GRAPHS_API u32 MatrixDimension(GraphValueType type);
		SHALLTEAR_GRAPHS_API GraphValueType Vector(u32 width);
		SHALLTEAR_GRAPHS_API GraphValueType Matrix(u32 dimension);
		SHALLTEAR_GRAPHS_API std::string_view HLSLName(GraphValueType type);
		SHALLTEAR_GRAPHS_API std::string_view DisplayName(GraphValueType type);
		SHALLTEAR_GRAPHS_API bool IsTexture(GraphValueType type);
		SHALLTEAR_GRAPHS_API bool IsLeaf(GraphValueType type);
		SHALLTEAR_GRAPHS_API GraphValueType SampleCoordinate(GraphValueType texture);
	} // namespace GraphTypes

	enum GraphStageMask : u8 {
		GraphStageNone = 0,
		GraphStageVertex = 1u << 0,
		GraphStageFragment = 1u << 1,
		GraphStageLighting = 1u << 2,
		GraphStageAll = GraphStageVertex | GraphStageFragment | GraphStageLighting
	};

	enum class GraphOp : u8 {
		Constant,
		External,

		Negate,
		Absolute,
		Sign,
		Floor,
		Ceiling,
		Round,
		Truncate,
		Fraction,
		Saturate,
		SquareRoot,
		ReciprocalSquareRoot,
		Reciprocal,
		Exponential,
		Exponential2,
		Logarithm,
		Logarithm2,
		Logarithm10,
		Sine,
		Cosine,
		Tangent,
		Arcsine,
		Arccosine,
		Arctangent,
		HyperbolicSine,
		HyperbolicCosine,
		HyperbolicTangent,
		DerivativeX,
		DerivativeY,
		DerivativeWidth,

		Add,
		Subtract,
		Multiply,
		Divide,
		Power,
		Minimum,
		Maximum,
		Modulo,
		Arctangent2,
		Step,

		Lerp,
		Clamp,
		Smoothstep,

		Dot,
		Cross,
		Length,
		Distance,
		Normalize,
		Reflect,

		MatrixMultiply,
		Transpose,
		Determinant,
		MatrixRow,

		Swizzle,
		Construct,

		Less,
		LessEqual,
		Equal,
		Select,
		BoolToFloat,

		ValueNoise,
		GradientNoise,
		Voronoi,

		SampleTexture,
		SampleTextureLevel,
		TextureSize,
		TextureLevelOfDetail,

		ParallaxOcclusion,

		SampleTextureBias,
		SampleTextureGrad,

		Keyword,

		LogicalAnd,
		LogicalOr,
		LogicalNot,
		All,
		Any,
		IsNaN,
		IsInfinite,

		Call,
		Argument,
		CallResult,

		Random,

		FractalNoise,
		CurlNoise,

		LoadTexture,

		MeshCount,
		MeshIndex,
		MeshVertex,

		Count
	};

	enum class GraphMeshChannel : u8 {
		Position = 0,
		Normal,
		Tangent,
		Color,
		TexCoord0
	};

	enum class GraphEffectNoise : u8 {
		Value = 0,
		Perlin,
		Cellular
	};

	enum class GraphNoiseHash : u8 {
		Deterministic = 0,
		LegacySine,
		LegacyMod
	};

	namespace GraphOps {
		SHALLTEAR_GRAPHS_API std::string_view Name(GraphOp op);
		SHALLTEAR_GRAPHS_API GraphStageMask Stages(GraphOp op);
		// correctly rounded IEEE ops only
		SHALLTEAR_GRAPHS_API bool Foldable(GraphOp op);
	} // namespace GraphOps

	using GraphValueId = u32;
	inline constexpr GraphValueId GRAPH_NO_VALUE = 0xFFFFFFFFu;
	inline constexpr u32 GRAPH_MAX_OPERANDS = 4;

	struct GraphValue {
		GraphOp op = GraphOp::Constant;
		GraphValueType type = GraphValueType::None;
		u8 operandCount = 0;
		GraphValueId operands[GRAPH_MAX_OPERANDS] = {GRAPH_NO_VALUE, GRAPH_NO_VALUE, GRAPH_NO_VALUE, GRAPH_NO_VALUE};
		u32 payload = 0;
		u32 node = 0;
		GraphStageMask stages = GraphStageNone;
	};

	struct GraphExternal {
		str name;
		str expression;
		GraphStageMask stages = GraphStageAll;
		str sampler;
		str unavailable;
	};

	struct GraphCall {
		str function;
		str definition;
		str include;
		Vector<GraphValueType> outputs{GetMemoryResource(MemTag::Editor)};
	};

	struct GraphRoot {
		GraphValueId value = GRAPH_NO_VALUE;
		GraphStageMask stage = GraphStageFragment;
	};

	struct GraphProgram {
		Vector<GraphValue> values{GetMemoryResource(MemTag::Editor)};
		// parallel to values
		Vector<str> labels{GetMemoryResource(MemTag::Editor)};
		Vector<f32> constants{GetMemoryResource(MemTag::Editor)};
		Vector<GraphExternal> externals{GetMemoryResource(MemTag::Editor)};
		Vector<GraphCall> calls{GetMemoryResource(MemTag::Editor)};
		Vector<GraphRoot> roots{GetMemoryResource(MemTag::Editor)};

		const GraphValue& At(GraphValueId id) const { return values[id]; }
		SHALLTEAR_GRAPHS_API std::span<const f32> ConstantOf(GraphValueId id) const;
	};

	namespace SwizzlePattern {
		inline constexpr u32 Pack(std::span<const u8> components) {
			u32 packed = static_cast<u32>(components.size());
			for (size_t i = 0; i < components.size(); ++i) {
				packed |= static_cast<u32>(components[i] & 3u) << (4 + 2 * i);
			}
			return packed;
		}
		inline constexpr u32 Count(u32 packed) { return packed & 7u; }
		inline constexpr u32 Component(u32 packed, u32 index) { return (packed >> (4 + 2 * index)) & 3u; }
	} // namespace SwizzlePattern

	class SHALLTEAR_GRAPHS_API GraphProgramBuilder {
	public:
		explicit GraphProgramBuilder(GraphProgram& program) : _program(program) {}

		void SetNode(u32 node) {
			_node = node;
			_identity = node;
		}
		void SetIdentity(u32 identity) { _identity = identity; }
		u32 Node() const { return _node; }
		u32 Identity() const { return _identity; }
		bool Failed() const { return !_error.empty(); }
		const str& Error() const { return _error; }
		void ClearError() { _error.clear(); }

		GraphValueType TypeOf(GraphValueId value) const;

		GraphValueId Constant(GraphValueType type, std::span<const f32> components);
		GraphValueId Scalar(f32 value);
		GraphValueId External(GraphValueType type, std::string_view name, std::string_view expression,
							  GraphStageMask stages = GraphStageAll, std::string_view unavailable = {});
		GraphValueId Texture(GraphValueType type, std::string_view name, std::string_view expression, std::string_view sampler,
							 GraphStageMask stages = GraphStageAll, std::string_view unavailable = {});

		GraphValueId Unary(GraphOp op, GraphValueId a);
		GraphValueId Binary(GraphOp op, GraphValueId a, GraphValueId b);
		GraphValueId Ternary(GraphOp op, GraphValueId a, GraphValueId b, GraphValueId c);
		GraphValueId Swizzle(GraphValueId a, std::string_view pattern);
		GraphValueId Construct(GraphValueType type, std::span<const GraphValueId> parts);
		GraphValueId MatrixRow(GraphValueId matrix, u32 row);
		GraphValueId Noise(GraphOp op, GraphNoiseHash hash, std::span<const GraphValueId> operands);
		GraphValueId EffectNoise(GraphOp op, GraphEffectNoise type, GraphValueId coordinate, GraphValueId parameters, GraphValueId octaves);
		GraphValueId Random(GraphValueId key, GraphValueId seed, u32 salt);
		GraphValueId TextureOp(GraphOp op, std::span<const GraphValueId> operands, u32 payload = 0);
		GraphValueId MeshOp(GraphOp op, GraphValueId mesh, GraphValueId index, u32 payload);
		GraphValueId Keyword(std::string_view macro, GraphValueId on, GraphValueId off);
		void Call(GraphCall call, std::span<const GraphValueId> inputs, std::span<GraphValueId> results);
		std::span<const f32> ConstantOf(GraphValueId value) const { return _program.ConstantOf(value); }

		GraphValueId Convert(GraphValueId value, GraphValueType to);
		static bool CanConvert(GraphValueType from, GraphValueType to);

		GraphValueId Add(GraphValueId a, GraphValueId b) { return Binary(GraphOp::Add, a, b); }
		GraphValueId Subtract(GraphValueId a, GraphValueId b) { return Binary(GraphOp::Subtract, a, b); }
		GraphValueId Multiply(GraphValueId a, GraphValueId b) { return Binary(GraphOp::Multiply, a, b); }
		GraphValueId Divide(GraphValueId a, GraphValueId b) { return Binary(GraphOp::Divide, a, b); }
		GraphValueId Minimum(GraphValueId a, GraphValueId b) { return Binary(GraphOp::Minimum, a, b); }
		GraphValueId Maximum(GraphValueId a, GraphValueId b) { return Binary(GraphOp::Maximum, a, b); }
		GraphValueId Dot(GraphValueId a, GraphValueId b) { return Binary(GraphOp::Dot, a, b); }
		GraphValueId Lerp(GraphValueId a, GraphValueId b, GraphValueId t) { return Ternary(GraphOp::Lerp, a, b, t); }
		GraphValueId Clamp(GraphValueId x, GraphValueId lo, GraphValueId hi) { return Ternary(GraphOp::Clamp, x, lo, hi); }
		GraphValueId Select(GraphValueId condition, GraphValueId ifTrue, GraphValueId ifFalse) {
			return Ternary(GraphOp::Select, condition, ifTrue, ifFalse);
		}
		GraphValueId Fail(std::string_view message);
	private:
		GraphValueId Append(GraphValue value);
		GraphValueType BroadcastType(GraphValueType a, GraphValueType b) const;

		GraphProgram& _program;
		u32 _node = 0;
		u32 _identity = 0;
		str _error;
	};

} // namespace Shalltear
