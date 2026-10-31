#include "GraphProgram.h"

#include <array>

namespace Shalltear {

	namespace GraphTypes {

		u32 Components(GraphValueType type) {
			switch (type) {
			case GraphValueType::Bool:
			case GraphValueType::Float:
				return 1;
			case GraphValueType::Float2:
				return 2;
			case GraphValueType::Float3:
				return 3;
			case GraphValueType::Float4:
				return 4;
			case GraphValueType::Float2x2:
				return 4;
			case GraphValueType::Float3x3:
				return 9;
			case GraphValueType::Float4x4:
				return 16;
			case GraphValueType::Gradient:
				return GRAPH_GRADIENT_COMPONENTS;
			case GraphValueType::Curve:
				return GRAPH_CURVE_COMPONENTS;
			case GraphValueType::Texture2D:
			case GraphValueType::Texture2DArray:
			case GraphValueType::Texture3D:
			case GraphValueType::TextureCube:
			case GraphValueType::Mesh:
			case GraphValueType::None:
				break;
			}
			return 0;
		}

		u32 VectorWidth(GraphValueType type) {
			switch (type) {
			case GraphValueType::Float:
				return 1;
			case GraphValueType::Float2:
				return 2;
			case GraphValueType::Float3:
				return 3;
			case GraphValueType::Float4:
				return 4;
			default:
				return 0;
			}
		}

		u32 MatrixDimension(GraphValueType type) {
			switch (type) {
			case GraphValueType::Float2x2:
				return 2;
			case GraphValueType::Float3x3:
				return 3;
			case GraphValueType::Float4x4:
				return 4;
			default:
				return 0;
			}
		}

		GraphValueType Vector(u32 width) {
			switch (width) {
			case 1:
				return GraphValueType::Float;
			case 2:
				return GraphValueType::Float2;
			case 3:
				return GraphValueType::Float3;
			case 4:
				return GraphValueType::Float4;
			default:
				return GraphValueType::None;
			}
		}

		GraphValueType Matrix(u32 dimension) {
			switch (dimension) {
			case 2:
				return GraphValueType::Float2x2;
			case 3:
				return GraphValueType::Float3x3;
			case 4:
				return GraphValueType::Float4x4;
			default:
				return GraphValueType::None;
			}
		}

		std::string_view HLSLName(GraphValueType type) {
			switch (type) {
			case GraphValueType::Bool:
				return "bool";
			case GraphValueType::Float:
				return "float";
			case GraphValueType::Float2:
				return "float2";
			case GraphValueType::Float3:
				return "float3";
			case GraphValueType::Float4:
				return "float4";
			case GraphValueType::Float2x2:
				return "float2x2";
			case GraphValueType::Float3x3:
				return "float3x3";
			case GraphValueType::Float4x4:
				return "float4x4";
			case GraphValueType::Texture2D:
				return "Texture2D";
			case GraphValueType::Texture2DArray:
				return "Texture2DArray";
			case GraphValueType::Texture3D:
				return "Texture3D";
			case GraphValueType::TextureCube:
				return "TextureCube";
			case GraphValueType::Mesh:
				return "uint";
			case GraphValueType::Gradient:
			case GraphValueType::Curve:
			case GraphValueType::None:
				break;
			}
			return "void";
		}

		std::string_view DisplayName(GraphValueType type) {
			switch (type) {
			case GraphValueType::Bool:
				return "Boolean";
			case GraphValueType::Float:
				return "Float";
			case GraphValueType::Float2:
				return "Vector 2";
			case GraphValueType::Float3:
				return "Vector 3";
			case GraphValueType::Float4:
				return "Vector 4";
			case GraphValueType::Float2x2:
				return "Matrix 2";
			case GraphValueType::Float3x3:
				return "Matrix 3";
			case GraphValueType::Float4x4:
				return "Matrix 4";
			case GraphValueType::Texture2D:
				return "Texture 2D";
			case GraphValueType::Texture2DArray:
				return "Texture 2D Array";
			case GraphValueType::Texture3D:
				return "Texture 3D";
			case GraphValueType::TextureCube:
				return "Cubemap";
			case GraphValueType::Gradient:
				return "Gradient";
			case GraphValueType::Curve:
				return "AnimationCurve";
			case GraphValueType::Mesh:
				return "Mesh";
			case GraphValueType::None:
				break;
			}
			return "nothing";
		}

		bool IsTexture(GraphValueType type) {
			return type == GraphValueType::Texture2D || type == GraphValueType::Texture2DArray || type == GraphValueType::Texture3D ||
				   type == GraphValueType::TextureCube;
		}

		bool IsLeaf(GraphValueType type) {
			return IsTexture(type) || type == GraphValueType::Gradient || type == GraphValueType::Curve || type == GraphValueType::Mesh;
		}

		GraphValueType SampleCoordinate(GraphValueType texture) {
			switch (texture) {
			case GraphValueType::Texture2D:
				return GraphValueType::Float2;
			case GraphValueType::Texture2DArray:
			case GraphValueType::Texture3D:
			case GraphValueType::TextureCube:
				return GraphValueType::Float3;
			default:
				return GraphValueType::None;
			}
		}

	} // namespace GraphTypes

	namespace {

		struct OpInfo {
			std::string_view name;
			bool foldable;
			bool fragmentOnly;
		};

		constexpr std::array<OpInfo, static_cast<size_t>(GraphOp::Count)> OP_INFO = {{
			{"constant", true, false},
			{"external", false, false},
			{"-", true, false},
			{"abs", true, false},
			{"sign", true, false},
			{"floor", true, false},
			{"ceil", true, false},
			{"round", true, false},
			{"trunc", true, false},
			{"frac", true, false},
			{"saturate", true, false},
			{"sqrt", true, false},
			{"rsqrt", true, false},
			{"rcp", true, false},
			{"exp", false, false},
			{"exp2", false, false},
			{"log", false, false},
			{"log2", false, false},
			{"log10", false, false},
			{"sin", false, false},
			{"cos", false, false},
			{"tan", false, false},
			{"asin", false, false},
			{"acos", false, false},
			{"atan", false, false},
			{"sinh", false, false},
			{"cosh", false, false},
			{"tanh", false, false},
			{"ddx", false, true},
			{"ddy", false, true},
			{"fwidth", false, true},
			{"+", true, false},
			{"-", true, false},
			{"*", true, false},
			{"/", true, false},
			{"pow", false, false},
			{"min", true, false},
			{"max", true, false},
			{"fmod", true, false},
			{"atan2", false, false},
			{"step", true, false},
			{"lerp", true, false},
			{"clamp", true, false},
			{"smoothstep", true, false},
			{"dot", true, false},
			{"cross", true, false},
			{"length", true, false},
			{"distance", true, false},
			{"normalize", true, false},
			{"reflect", true, false},
			{"mul", true, false},
			{"transpose", true, false},
			{"determinant", true, false},
			{"row", true, false},
			{"swizzle", true, false},
			{"construct", true, false},
			{"<", true, false},
			{"<=", true, false},
			{"==", true, false},
			{"select", true, false},
			{"float", true, false},
			{"value noise", false, false},
			{"gradient noise", false, false},
			{"voronoi", false, false},
			{"Sample", false, true},
			{"SampleLevel", false, false},
			{"GetDimensions", false, false},
			{"CalculateLevelOfDetail", false, true},
			{"ParallaxOcclusionMapping", false, false},
			{"SampleBias", false, true},
			{"SampleGrad", false, false},
			{"keyword", false, false},
			{"&&", true, false},
			{"||", true, false},
			{"!", true, false},
			{"all", true, false},
			{"any", true, false},
			{"isnan", true, false},
			{"isinf", true, false},
			{"call", false, false},
			{"argument", false, false},
			{"result", false, false},
			{"random", false, false},
			{"fractal noise", false, false},
			{"curl noise", false, false},
			{"Load", false, false},
			{"mesh count", false, false},
			{"mesh index", false, false},
			{"mesh vertex", false, false},
		}};
		static_assert(OP_INFO[static_cast<size_t>(GraphOp::SampleTextureGrad)].name == "SampleGrad", "OP_INFO is out of step with GraphOp");
		static_assert(OP_INFO[static_cast<size_t>(GraphOp::Keyword)].name == "keyword", "OP_INFO is out of step with GraphOp");
		static_assert(OP_INFO[static_cast<size_t>(GraphOp::IsInfinite)].name == "isinf", "OP_INFO is out of step with GraphOp");
		static_assert(OP_INFO[static_cast<size_t>(GraphOp::CallResult)].name == "result", "OP_INFO is out of step with GraphOp");
		static_assert(OP_INFO[static_cast<size_t>(GraphOp::Random)].name == "random", "OP_INFO is out of step with GraphOp");
		static_assert(OP_INFO[static_cast<size_t>(GraphOp::CurlNoise)].name == "curl noise", "OP_INFO is out of step with GraphOp");
		static_assert(OP_INFO[static_cast<size_t>(GraphOp::LoadTexture)].name == "Load", "OP_INFO is out of step with GraphOp");
		static_assert(OP_INFO[static_cast<size_t>(GraphOp::MeshVertex)].name == "mesh vertex", "OP_INFO is out of step with GraphOp");

		const OpInfo& Info(GraphOp op) { return OP_INFO[static_cast<size_t>(op)]; }

		bool IsVector(GraphValueType type) { return GraphTypes::VectorWidth(type) != 0; }

	} // anonymous namespace

	namespace GraphOps {

		std::string_view Name(GraphOp op) { return Info(op).name; }

		GraphStageMask Stages(GraphOp op) { return Info(op).fragmentOnly ? GraphStageFragment : GraphStageAll; }

		bool Foldable(GraphOp op) { return Info(op).foldable; }

	} // namespace GraphOps

	std::span<const f32> GraphProgram::ConstantOf(GraphValueId id) const {
		if (id >= values.size() || values[id].op != GraphOp::Constant) {
			return {};
		}
		const GraphValue& value = values[id];
		return {constants.data() + value.payload, GraphTypes::Components(value.type)};
	}

	GraphValueType GraphProgramBuilder::TypeOf(GraphValueId value) const {
		return value < _program.values.size() ? _program.values[value].type : GraphValueType::None;
	}

	GraphValueId GraphProgramBuilder::Fail(std::string_view message) {
		if (_error.empty()) {
			_error.assign(message);
		}
		return GRAPH_NO_VALUE;
	}

	GraphValueId GraphProgramBuilder::Append(GraphValue value) {
		value.node = _node;
		_program.values.push_back(value);
		_program.labels.emplace_back();
		return static_cast<GraphValueId>(_program.values.size() - 1);
	}

	GraphValueType GraphProgramBuilder::BroadcastType(GraphValueType a, GraphValueType b) const {
		if (!IsVector(a) || !IsVector(b)) {
			return GraphValueType::None;
		}
		if (a == b || b == GraphValueType::Float) {
			return a;
		}
		if (a == GraphValueType::Float) {
			return b;
		}
		return GraphValueType::None;
	}

	GraphValueId GraphProgramBuilder::Constant(GraphValueType type, std::span<const f32> components) {
		if (components.size() != GraphTypes::Components(type) || type == GraphValueType::None || GraphTypes::IsTexture(type) || type == GraphValueType::Mesh) {
			return Fail("a constant doesn't match its type");
		}
		GraphValue value;
		value.op = GraphOp::Constant;
		value.type = type;
		value.payload = static_cast<u32>(_program.constants.size());
		_program.constants.insert(_program.constants.end(), components.begin(), components.end());
		return Append(value);
	}

	GraphValueId GraphProgramBuilder::Scalar(f32 value) {
		return Constant(GraphValueType::Float, {&value, 1});
	}

	GraphValueId GraphProgramBuilder::External(GraphValueType type, std::string_view name, std::string_view expression,
											   GraphStageMask stages, std::string_view unavailable) {
		u32 index = 0;
		for (; index < _program.externals.size(); ++index) {
			if (_program.externals[index].name == name) {
				break;
			}
		}
		if (index == _program.externals.size()) {
			GraphExternal external;
			external.name.assign(name);
			external.expression.assign(expression);
			external.stages = unavailable.empty() ? stages : GraphStageNone;
			external.unavailable.assign(unavailable);
			_program.externals.push_back(std::move(external));
		}
		GraphValue value;
		value.op = GraphOp::External;
		value.type = type;
		value.payload = index;
		return Append(value);
	}

	GraphValueId GraphProgramBuilder::Texture(GraphValueType type, std::string_view name, std::string_view expression,
											  std::string_view sampler, GraphStageMask stages, std::string_view unavailable) {
		if (!GraphTypes::IsTexture(type)) {
			return Fail("a texture external that isn't a texture");
		}
		GraphValueId value = External(type, name, expression, stages, unavailable);
		if (value != GRAPH_NO_VALUE) {
			_program.externals[_program.values[value].payload].sampler.assign(sampler);
		}
		return value;
	}

	GraphValueId GraphProgramBuilder::TextureOp(GraphOp op, std::span<const GraphValueId> operands, u32 payload) {
		for (GraphValueId operand : operands) {
			if (operand == GRAPH_NO_VALUE) {
				return GRAPH_NO_VALUE;
			}
		}
		auto is = [&](size_t index, GraphValueType type) { return index < operands.size() && TypeOf(operands[index]) == type; };
		const GraphValueType texture = operands.empty() ? GraphValueType::None : TypeOf(operands[0]);
		const GraphValueType coordinate = GraphTypes::SampleCoordinate(texture);
		const GraphValueType gradient = texture == GraphValueType::Texture2D || texture == GraphValueType::Texture2DArray ? GraphValueType::Float2
																														  : GraphValueType::Float3;
		const bool sampled = coordinate != GraphValueType::None && is(1, coordinate);
		GraphValueType result = GraphValueType::None;
		switch (op) {
		case GraphOp::SampleTexture:
			result = operands.size() == 2 && sampled ? GraphValueType::Float4 : GraphValueType::None;
			break;
		case GraphOp::SampleTextureLevel:
			result = operands.size() == 3 && sampled && is(2, GraphValueType::Float) ? GraphValueType::Float4 : GraphValueType::None;
			break;
		case GraphOp::SampleTextureBias:
			result = operands.size() == 3 && sampled && is(2, GraphValueType::Float) ? GraphValueType::Float4 : GraphValueType::None;
			break;
		case GraphOp::SampleTextureGrad:
			result = operands.size() == 4 && sampled && is(2, gradient) && is(3, gradient) ? GraphValueType::Float4 : GraphValueType::None;
			break;
		case GraphOp::TextureSize:
			result = operands.size() == 1 && (is(0, GraphValueType::Texture2D) || is(0, GraphValueType::Texture3D)) ? GraphValueType::Float4
																													: GraphValueType::None;
			break;
		case GraphOp::TextureLevelOfDetail:
			result = operands.size() == 2 && is(0, GraphValueType::Texture2D) && is(1, GraphValueType::Float2) ? GraphValueType::Float
																											   : GraphValueType::None;
			break;
		case GraphOp::LoadTexture:
			result = operands.size() == 2 && is(0, GraphValueType::Texture2D) && is(1, GraphValueType::Float2) ? GraphValueType::Float4
																											   : GraphValueType::None;
			break;
		case GraphOp::ParallaxOcclusion:
			result = operands.size() == 4 && is(0, GraphValueType::Texture2D) && is(1, GraphValueType::Float2) && is(2, GraphValueType::Float3) &&
							 is(3, GraphValueType::Float3)
						 ? GraphValueType::Float3
						 : GraphValueType::None;
			break;
		default:
			break;
		}
		if (result != GraphValueType::None && _program.values[operands[0]].op != GraphOp::External) {
			result = GraphValueType::None;
		}
		if (result == GraphValueType::None) {
			return Fail(str("`") + str(GraphOps::Name(op)) + "` was given the wrong operands");
		}
		GraphValue value;
		value.op = op;
		value.type = result;
		value.operandCount = static_cast<u8>(operands.size());
		for (size_t i = 0; i < operands.size(); ++i) {
			value.operands[i] = operands[i];
		}
		value.payload = payload;
		return Append(value);
	}

	GraphValueId GraphProgramBuilder::MeshOp(GraphOp op, GraphValueId mesh, GraphValueId index, u32 payload) {
		if (mesh == GRAPH_NO_VALUE || (op != GraphOp::MeshCount && index == GRAPH_NO_VALUE)) {
			return GRAPH_NO_VALUE;
		}
		const bool leaf = TypeOf(mesh) == GraphValueType::Mesh && _program.values[mesh].op == GraphOp::External;
		GraphValueType result = GraphValueType::None;
		switch (op) {
		case GraphOp::MeshCount:
			result = leaf && payload <= 1 ? GraphValueType::Float : GraphValueType::None;
			break;
		case GraphOp::MeshIndex:
			result = leaf && TypeOf(index) == GraphValueType::Float ? GraphValueType::Float : GraphValueType::None;
			break;
		case GraphOp::MeshVertex:
			if (leaf && TypeOf(index) == GraphValueType::Float && payload <= static_cast<u32>(GraphMeshChannel::TexCoord0)) {
				const auto channel = static_cast<GraphMeshChannel>(payload);
				result = channel == GraphMeshChannel::Position || channel == GraphMeshChannel::Normal ? GraphValueType::Float3 : GraphValueType::Float4;
			}
			break;
		default:
			break;
		}
		if (result == GraphValueType::None) {
			return Fail(str("`") + str(GraphOps::Name(op)) + "` was given the wrong operands");
		}
		GraphValue value;
		value.op = op;
		value.type = result;
		value.operandCount = op == GraphOp::MeshCount ? 1 : 2;
		value.operands[0] = mesh;
		value.operands[1] = op == GraphOp::MeshCount ? GRAPH_NO_VALUE : index;
		value.payload = payload;
		return Append(value);
	}

	GraphValueId GraphProgramBuilder::Unary(GraphOp op, GraphValueId a) {
		if (a == GRAPH_NO_VALUE) {
			return GRAPH_NO_VALUE;
		}
		GraphValueType type = TypeOf(a);
		GraphValueType result = GraphValueType::None;
		switch (op) {
		case GraphOp::Length:
			result = IsVector(type) ? GraphValueType::Float : GraphValueType::None;
			break;
		case GraphOp::Transpose:
			result = GraphTypes::MatrixDimension(type) != 0 ? type : GraphValueType::None;
			break;
		case GraphOp::Determinant:
			result = GraphTypes::MatrixDimension(type) != 0 ? GraphValueType::Float : GraphValueType::None;
			break;
		case GraphOp::BoolToFloat:
			result = type == GraphValueType::Bool ? GraphValueType::Float : GraphValueType::None;
			break;
		case GraphOp::LogicalNot:
			result = type == GraphValueType::Bool ? GraphValueType::Bool : GraphValueType::None;
			break;
		case GraphOp::All:
		case GraphOp::Any:
			result = IsVector(type) ? GraphValueType::Bool : GraphValueType::None;
			break;
		case GraphOp::IsNaN:
		case GraphOp::IsInfinite:
			result = type == GraphValueType::Float ? GraphValueType::Bool : GraphValueType::None;
			break;
		default:
			result = IsVector(type) ? type : GraphValueType::None;
			break;
		}
		if (result == GraphValueType::None) {
			return Fail(str("`") + str(GraphOps::Name(op)) + "` can't take a " + str(GraphTypes::DisplayName(type)));
		}
		GraphValue value;
		value.op = op;
		value.type = result;
		value.operandCount = 1;
		value.operands[0] = a;
		return Append(value);
	}

	GraphValueId GraphProgramBuilder::Binary(GraphOp op, GraphValueId a, GraphValueId b) {
		if (a == GRAPH_NO_VALUE || b == GRAPH_NO_VALUE) {
			return GRAPH_NO_VALUE;
		}
		GraphValueType typeA = TypeOf(a);
		GraphValueType typeB = TypeOf(b);
		GraphValueType result = GraphValueType::None;
		switch (op) {
		case GraphOp::Dot:
		case GraphOp::Distance:
			result = IsVector(typeA) && typeA == typeB ? GraphValueType::Float : GraphValueType::None;
			break;
		case GraphOp::Cross:
			result = typeA == GraphValueType::Float3 && typeB == GraphValueType::Float3 ? GraphValueType::Float3 : GraphValueType::None;
			break;
		case GraphOp::Reflect:
			result = IsVector(typeA) && typeA == typeB ? typeA : GraphValueType::None;
			break;
		case GraphOp::MatrixMultiply: {
			u32 dimA = GraphTypes::MatrixDimension(typeA);
			u32 dimB = GraphTypes::MatrixDimension(typeB);
			if (dimA != 0 && dimA == dimB) {
				result = typeA;
			} else if (dimB != 0 && GraphTypes::VectorWidth(typeA) == dimB) {
				result = typeA;
			} else if (dimA != 0 && GraphTypes::VectorWidth(typeB) == dimA) {
				result = typeB;
			}
			break;
		}
		case GraphOp::Less:
		case GraphOp::LessEqual:
		case GraphOp::Equal:
			result = typeA == GraphValueType::Float && typeB == GraphValueType::Float ? GraphValueType::Bool : GraphValueType::None;
			break;
		case GraphOp::LogicalAnd:
		case GraphOp::LogicalOr:
			result = typeA == GraphValueType::Bool && typeB == GraphValueType::Bool ? GraphValueType::Bool : GraphValueType::None;
			break;
		default:
			result = BroadcastType(typeA, typeB);
			break;
		}
		if (result == GraphValueType::None) {
			return Fail(str("`") + str(GraphOps::Name(op)) + "` can't take a " + str(GraphTypes::DisplayName(typeA)) + " and a " +
						str(GraphTypes::DisplayName(typeB)));
		}
		GraphValue value;
		value.op = op;
		value.type = result;
		value.operandCount = 2;
		value.operands[0] = a;
		value.operands[1] = b;
		return Append(value);
	}

	GraphValueId GraphProgramBuilder::Ternary(GraphOp op, GraphValueId a, GraphValueId b, GraphValueId c) {
		if (a == GRAPH_NO_VALUE || b == GRAPH_NO_VALUE || c == GRAPH_NO_VALUE) {
			return GRAPH_NO_VALUE;
		}
		GraphValueType result = GraphValueType::None;
		if (op == GraphOp::Select) {
			const bool leaf = GraphTypes::IsLeaf(TypeOf(b));
			result = TypeOf(a) == GraphValueType::Bool && TypeOf(b) == TypeOf(c) && !leaf ? TypeOf(b) : GraphValueType::None;
		} else {
			result = BroadcastType(BroadcastType(TypeOf(a), TypeOf(b)), TypeOf(c));
		}
		if (result == GraphValueType::None) {
			return Fail(str("`") + str(GraphOps::Name(op)) + "` can't take a " + str(GraphTypes::DisplayName(TypeOf(a))) + ", a " +
						str(GraphTypes::DisplayName(TypeOf(b))) + " and a " + str(GraphTypes::DisplayName(TypeOf(c))));
		}
		GraphValue value;
		value.op = op;
		value.type = result;
		value.operandCount = 3;
		value.operands[0] = a;
		value.operands[1] = b;
		value.operands[2] = c;
		return Append(value);
	}

	GraphValueId GraphProgramBuilder::Keyword(std::string_view macro, GraphValueId on, GraphValueId off) {
		if (on == GRAPH_NO_VALUE || off == GRAPH_NO_VALUE) {
			return GRAPH_NO_VALUE;
		}
		const bool leaf = GraphTypes::IsLeaf(TypeOf(on));
		if (TypeOf(on) != TypeOf(off) || leaf) {
			return Fail(str("a keyword's branch can't take a ") + str(GraphTypes::DisplayName(TypeOf(on))) + " and a " +
						str(GraphTypes::DisplayName(TypeOf(off))));
		}
		GraphValue value;
		value.op = GraphOp::Keyword;
		value.type = TypeOf(on);
		value.operandCount = 3;
		value.operands[0] = External(GraphValueType::Bool, str("Keyword/") + str(macro), macro);
		value.operands[1] = on;
		value.operands[2] = off;
		return Append(value);
	}

	void GraphProgramBuilder::Call(GraphCall call, std::span<const GraphValueId> inputs, std::span<GraphValueId> results) {
		for (GraphValueId& result : results) {
			result = GRAPH_NO_VALUE;
		}
		for (GraphValueId input : inputs) {
			if (input == GRAPH_NO_VALUE) {
				return;
			}
		}
		if (results.size() != call.outputs.size()) {
			Fail("`" + call.function + "` was given the wrong outputs");
			return;
		}
		for (const GraphCall& other : _program.calls) {
			if (other.function == call.function && !call.definition.empty() && !other.definition.empty() && other.definition != call.definition) {
				Fail("defines `" + call.function + "` (another custom function in this graph defines it differently)");
				return;
			}
		}
		GraphValueId last = GRAPH_NO_VALUE;
		for (GraphValueId input : inputs) {
			GraphValue argument;
			argument.op = GraphOp::Argument;
			argument.operands[0] = input;
			argument.operands[1] = last;
			argument.operandCount = last == GRAPH_NO_VALUE ? 1 : 2;
			last = Append(argument);
		}
		GraphValue statement;
		statement.op = GraphOp::Call;
		statement.operands[0] = last;
		statement.operandCount = last == GRAPH_NO_VALUE ? 0 : 1;
		statement.payload = static_cast<u32>(_program.calls.size());
		const Vector<GraphValueType> outputs = call.outputs;
		_program.calls.push_back(std::move(call));
		const GraphValueId made = Append(statement);
		for (size_t i = 0; i < results.size(); ++i) {
			GraphValue result;
			result.op = GraphOp::CallResult;
			result.type = outputs[i];
			result.operands[0] = made;
			result.operandCount = 1;
			result.payload = static_cast<u32>(i);
			results[i] = Append(result);
		}
	}

	GraphValueId GraphProgramBuilder::Swizzle(GraphValueId a, std::string_view pattern) {
		if (a == GRAPH_NO_VALUE) {
			return GRAPH_NO_VALUE;
		}
		u32 width = GraphTypes::VectorWidth(TypeOf(a));
		if (width == 0 || pattern.empty() || pattern.size() > 4) {
			return Fail("a swizzle needs a vector and 1-4 components");
		}
		std::array<u8, 4> components{};
		for (size_t i = 0; i < pattern.size(); ++i) {
			u8 component = 4;
			switch (pattern[i]) {
			case 'x':
			case 'r':
				component = 0;
				break;
			case 'y':
			case 'g':
				component = 1;
				break;
			case 'z':
			case 'b':
				component = 2;
				break;
			case 'w':
			case 'a':
				component = 3;
				break;
			default:
				break;
			}
			if (component >= width) {
				return Fail(str("swizzle `") + str(pattern) + "` reads past a " + str(GraphTypes::DisplayName(TypeOf(a))));
			}
			components[i] = component;
		}
		if (pattern.size() == width) {
			bool identity = true;
			for (u32 i = 0; i < width; ++i) {
				identity = identity && components[i] == i;
			}
			if (identity) {
				return a;
			}
		}
		GraphValue value;
		value.op = GraphOp::Swizzle;
		value.type = GraphTypes::Vector(static_cast<u32>(pattern.size()));
		value.operandCount = 1;
		value.operands[0] = a;
		value.payload = SwizzlePattern::Pack({components.data(), pattern.size()});
		return Append(value);
	}

	GraphValueId GraphProgramBuilder::Construct(GraphValueType type, std::span<const GraphValueId> parts) {
		if (parts.empty() || parts.size() > GRAPH_MAX_OPERANDS) {
			return Fail("a construct takes 1-4 parts");
		}
		u32 total = 0;
		for (GraphValueId part : parts) {
			if (part == GRAPH_NO_VALUE) {
				return GRAPH_NO_VALUE;
			}
			if (!IsVector(TypeOf(part))) {
				return Fail(str("a ") + str(GraphTypes::DisplayName(type)) + " can't be built from a " + str(GraphTypes::DisplayName(TypeOf(part))));
			}
			total += GraphTypes::Components(TypeOf(part));
		}
		if (total != GraphTypes::Components(type) || type == GraphValueType::Bool) {
			return Fail(str("a ") + str(GraphTypes::DisplayName(type)) + " can't be built from " + std::to_string(total) + " components");
		}
		GraphValue value;
		value.op = GraphOp::Construct;
		value.type = type;
		value.operandCount = static_cast<u8>(parts.size());
		for (size_t i = 0; i < parts.size(); ++i) {
			value.operands[i] = parts[i];
		}
		return Append(value);
	}

	GraphValueId GraphProgramBuilder::MatrixRow(GraphValueId matrix, u32 row) {
		if (matrix == GRAPH_NO_VALUE) {
			return GRAPH_NO_VALUE;
		}
		u32 dimension = GraphTypes::MatrixDimension(TypeOf(matrix));
		if (dimension == 0 || row >= dimension) {
			return Fail("a matrix row out of range");
		}
		GraphValue value;
		value.op = GraphOp::MatrixRow;
		value.type = GraphTypes::Vector(dimension);
		value.operandCount = 1;
		value.operands[0] = matrix;
		value.payload = row;
		return Append(value);
	}

	GraphValueId GraphProgramBuilder::Noise(GraphOp op, GraphNoiseHash hash, std::span<const GraphValueId> operands) {
		for (GraphValueId operand : operands) {
			if (operand == GRAPH_NO_VALUE) {
				return GRAPH_NO_VALUE;
			}
		}
		GraphValueType result = GraphValueType::None;
		switch (op) {
		case GraphOp::ValueNoise:
		case GraphOp::GradientNoise:
			if (operands.size() == 1 && TypeOf(operands[0]) == GraphValueType::Float2) {
				result = GraphValueType::Float;
			}
			break;
		case GraphOp::Voronoi:
			if (operands.size() == 3 && TypeOf(operands[0]) == GraphValueType::Float2 && TypeOf(operands[1]) == GraphValueType::Float &&
				TypeOf(operands[2]) == GraphValueType::Float && hash != GraphNoiseHash::LegacyMod) {
				result = GraphValueType::Float2;
			}
			break;
		default:
			break;
		}
		if (result == GraphValueType::None) {
			return Fail(str("`") + str(GraphOps::Name(op)) + "` was given the wrong operands");
		}
		GraphValue value;
		value.op = op;
		value.type = result;
		value.operandCount = static_cast<u8>(operands.size());
		for (size_t i = 0; i < operands.size(); ++i) {
			value.operands[i] = operands[i];
		}
		value.payload = static_cast<u32>(hash);
		return Append(value);
	}

	GraphValueId GraphProgramBuilder::EffectNoise(GraphOp op, GraphEffectNoise type, GraphValueId coordinate, GraphValueId parameters,
												  GraphValueId octaves) {
		if (coordinate == GRAPH_NO_VALUE || parameters == GRAPH_NO_VALUE || octaves == GRAPH_NO_VALUE) {
			return GRAPH_NO_VALUE;
		}
		const u32 width = GraphTypes::VectorWidth(TypeOf(coordinate));
		const bool common = TypeOf(parameters) == GraphValueType::Float3 && TypeOf(octaves) == GraphValueType::Float &&
							static_cast<u32>(type) <= static_cast<u32>(GraphEffectNoise::Cellular);
		GraphValueType result = GraphValueType::None;
		if (op == GraphOp::FractalNoise && common && width >= 1 && width <= 3) {
			result = GraphTypes::Vector(width + 1);
		} else if (op == GraphOp::CurlNoise && common && (width == 2 || width == 3)) {
			result = TypeOf(coordinate);
		}
		if (result == GraphValueType::None) {
			return Fail(str("`") + str(GraphOps::Name(op)) + "` was given the wrong operands");
		}
		GraphValue value;
		value.op = op;
		value.type = result;
		value.operandCount = 3;
		value.operands[0] = coordinate;
		value.operands[1] = parameters;
		value.operands[2] = octaves;
		value.payload = static_cast<u32>(type);
		return Append(value);
	}

	GraphValueId GraphProgramBuilder::Random(GraphValueId key, GraphValueId seed, u32 salt) {
		if (key == GRAPH_NO_VALUE || seed == GRAPH_NO_VALUE) {
			return GRAPH_NO_VALUE;
		}
		if (TypeOf(key) != GraphValueType::Float || TypeOf(seed) != GraphValueType::Float) {
			return Fail("`random` hashes a Float key and a Float seed");
		}
		GraphValue value;
		value.op = GraphOp::Random;
		value.type = GraphValueType::Float;
		value.operandCount = 2;
		value.operands[0] = key;
		value.operands[1] = seed;
		value.payload = salt;
		return Append(value);
	}

	bool GraphProgramBuilder::CanConvert(GraphValueType from, GraphValueType to) {
		if (from == to) {
			return true;
		}
		if (IsVector(to)) {
			return IsVector(from) || from == GraphValueType::Bool;
		}
		u32 fromDimension = GraphTypes::MatrixDimension(from);
		u32 toDimension = GraphTypes::MatrixDimension(to);
		return fromDimension != 0 && toDimension != 0 && toDimension <= fromDimension;
	}

	GraphValueId GraphProgramBuilder::Convert(GraphValueId value, GraphValueType to) {
		if (value == GRAPH_NO_VALUE) {
			return GRAPH_NO_VALUE;
		}
		GraphValueType from = TypeOf(value);
		if (from == to) {
			return value;
		}
		if (!CanConvert(from, to)) {
			return Fail(str("a ") + str(GraphTypes::DisplayName(from)) + " can't connect to a " + str(GraphTypes::DisplayName(to)));
		}
		if (from == GraphValueType::Bool) {
			return Convert(Unary(GraphOp::BoolToFloat, value), to);
		}

		u32 fromWidth = GraphTypes::VectorWidth(from);
		u32 toWidth = GraphTypes::VectorWidth(to);
		if (fromWidth != 0) {
			constexpr std::string_view LEADING = "xyzw";
			if (toWidth < fromWidth) {
				return Swizzle(value, LEADING.substr(0, toWidth));
			}
			if (fromWidth == 1) {
				constexpr std::string_view SPLAT = "xxxx";
				return Swizzle(value, SPLAT.substr(0, toWidth));
			}
			// widening fills from 0 0 0 1
			GraphValueId parts[GRAPH_MAX_OPERANDS] = {value};
			u32 count = 1;
			for (u32 component = fromWidth; component < toWidth; ++component) {
				parts[count++] = Scalar(component == 3 ? 1.0f : 0.0f);
			}
			return Construct(to, {parts, count});
		}

		u32 toDimension = GraphTypes::MatrixDimension(to);
		GraphValueId rows[GRAPH_MAX_OPERANDS] = {};
		for (u32 row = 0; row < toDimension; ++row) {
			rows[row] = Convert(MatrixRow(value, row), GraphTypes::Vector(toDimension));
		}
		return Construct(to, {rows, toDimension});
	}

} // namespace Shalltear
