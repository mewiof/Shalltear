#include "GraphCompiler.h"

#include <algorithm>
#include <array>
#include <bit>
#include <cmath>
#include <memory_resource>
#include <unordered_map>

#pragma clang fp contract(off)

namespace Shalltear {

	namespace {

		struct Operand {
			std::span<const f32> values;
			f32 At(u32 i) const { return values.size() == 1 ? values[0] : values[i]; }
		};

		f32 Saturate(f32 x) { return std::min(std::max(x, 0.0f), 1.0f); }

		f32 Dot(std::span<const f32> a, std::span<const f32> b) {
			f32 sum = 0.0f;
			for (size_t i = 0; i < a.size(); ++i) {
				sum = sum + a[i] * b[i];
			}
			return sum;
		}

		f32 Determinant(std::span<const f32> m, u32 n) {
			if (n == 2) {
				return m[0] * m[3] - m[1] * m[2];
			}
			if (n == 3) {
				return m[0] * (m[4] * m[8] - m[5] * m[7]) - m[1] * (m[3] * m[8] - m[5] * m[6]) + m[2] * (m[3] * m[7] - m[4] * m[6]);
			}
			f32 sum = 0.0f;
			for (u32 column = 0; column < 4; ++column) {
				std::array<f32, 9> minor{};
				u32 k = 0;
				for (u32 row = 1; row < 4; ++row) {
					for (u32 c = 0; c < 4; ++c) {
						if (c != column) {
							minor[k++] = m[row * 4 + c];
						}
					}
				}
				f32 term = m[column] * Determinant(minor, 3);
				sum = (column % 2 == 0) ? sum + term : sum - term;
			}
			return sum;
		}

		f32 Componentwise(GraphOp op, f32 a, f32 b, f32 c) {
			switch (op) {
			case GraphOp::Negate:
				return -a;
			case GraphOp::Absolute:
				return std::fabs(a);
			case GraphOp::Sign:
				return a > 0.0f ? 1.0f : (a < 0.0f ? -1.0f : 0.0f);
			case GraphOp::Floor:
				return std::floor(a);
			case GraphOp::Ceiling:
				return std::ceil(a);
			case GraphOp::Round:
				return std::nearbyint(a);
			case GraphOp::Truncate:
				return std::trunc(a);
			case GraphOp::Fraction:
				return a - std::floor(a);
			case GraphOp::Saturate:
				return Saturate(a);
			case GraphOp::SquareRoot:
				return std::sqrt(a);
			case GraphOp::ReciprocalSquareRoot:
				return 1.0f / std::sqrt(a);
			case GraphOp::Reciprocal:
				return 1.0f / a;
			case GraphOp::Add:
				return a + b;
			case GraphOp::Subtract:
				return a - b;
			case GraphOp::Multiply:
				return a * b;
			case GraphOp::Divide:
				return a / b;
			case GraphOp::Minimum:
				return b < a ? b : a;
			case GraphOp::Maximum:
				return a < b ? b : a;
			case GraphOp::Modulo:
				return std::fmod(a, b);
			case GraphOp::Step:
				return b >= a ? 1.0f : 0.0f;
			case GraphOp::Lerp:
				return a + c * (b - a);
			case GraphOp::Clamp:
				return std::min(std::max(a, b), c);
			case GraphOp::Smoothstep: {
				f32 t = Saturate((c - a) / (b - a));
				return t * t * (3.0f - 2.0f * t);
			}
			default:
				return 0.0f;
			}
		}

		bool Evaluate(const GraphProgram& program, const GraphValue& value, Vector<f32>& out) {
			std::array<Operand, GRAPH_MAX_OPERANDS> operands{};
			for (u32 i = 0; i < value.operandCount; ++i) {
				operands[i].values = program.ConstantOf(value.operands[i]);
			}
			u32 count = GraphTypes::Components(value.type);
			out.assign(count, 0.0f);
			switch (value.op) {
			case GraphOp::Dot:
				out[0] = Dot(operands[0].values, operands[1].values);
				return true;
			case GraphOp::Length:
				out[0] = std::sqrt(Dot(operands[0].values, operands[0].values));
				return true;
			case GraphOp::Distance: {
				f32 sum = 0.0f;
				for (size_t i = 0; i < operands[0].values.size(); ++i) {
					f32 d = operands[0].values[i] - operands[1].values[i];
					sum = sum + d * d;
				}
				out[0] = std::sqrt(sum);
				return true;
			}
			case GraphOp::Normalize: {
				f32 length = std::sqrt(Dot(operands[0].values, operands[0].values));
				for (u32 i = 0; i < count; ++i) {
					out[i] = operands[0].values[i] / length;
				}
				return true;
			}
			case GraphOp::Cross: {
				std::span<const f32> a = operands[0].values;
				std::span<const f32> b = operands[1].values;
				out[0] = a[1] * b[2] - a[2] * b[1];
				out[1] = a[2] * b[0] - a[0] * b[2];
				out[2] = a[0] * b[1] - a[1] * b[0];
				return true;
			}
			case GraphOp::Reflect: {
				f32 d = Dot(operands[1].values, operands[0].values);
				for (u32 i = 0; i < count; ++i) {
					out[i] = operands[0].values[i] - 2.0f * d * operands[1].values[i];
				}
				return true;
			}
			case GraphOp::MatrixMultiply: {
				const GraphValue& a = program.At(value.operands[0]);
				const GraphValue& b = program.At(value.operands[1]);
				u32 n = std::max(GraphTypes::MatrixDimension(a.type), GraphTypes::MatrixDimension(b.type));
				std::span<const f32> x = operands[0].values;
				std::span<const f32> y = operands[1].values;
				if (GraphTypes::VectorWidth(a.type) != 0) {
					for (u32 column = 0; column < n; ++column) {
						f32 sum = 0.0f;
						for (u32 i = 0; i < n; ++i) {
							sum = sum + x[i] * y[i * n + column];
						}
						out[column] = sum;
					}
				} else if (GraphTypes::VectorWidth(b.type) != 0) {
					for (u32 row = 0; row < n; ++row) {
						f32 sum = 0.0f;
						for (u32 i = 0; i < n; ++i) {
							sum = sum + x[row * n + i] * y[i];
						}
						out[row] = sum;
					}
				} else {
					for (u32 row = 0; row < n; ++row) {
						for (u32 column = 0; column < n; ++column) {
							f32 sum = 0.0f;
							for (u32 i = 0; i < n; ++i) {
								sum = sum + x[row * n + i] * y[i * n + column];
							}
							out[row * n + column] = sum;
						}
					}
				}
				return true;
			}
			case GraphOp::Transpose: {
				u32 n = GraphTypes::MatrixDimension(value.type);
				for (u32 row = 0; row < n; ++row) {
					for (u32 column = 0; column < n; ++column) {
						out[column * n + row] = operands[0].values[row * n + column];
					}
				}
				return true;
			}
			case GraphOp::Determinant:
				out[0] = Determinant(operands[0].values, GraphTypes::MatrixDimension(program.At(value.operands[0]).type));
				return true;
			case GraphOp::MatrixRow:
				for (u32 i = 0; i < count; ++i) {
					out[i] = operands[0].values[value.payload * count + i];
				}
				return true;
			case GraphOp::Swizzle:
				for (u32 i = 0; i < count; ++i) {
					out[i] = operands[0].values[SwizzlePattern::Component(value.payload, i)];
				}
				return true;
			case GraphOp::Construct: {
				u32 k = 0;
				for (u32 i = 0; i < value.operandCount; ++i) {
					for (f32 component : operands[i].values) {
						out[k++] = component;
					}
				}
				return true;
			}
			case GraphOp::Less:
				out[0] = operands[0].At(0) < operands[1].At(0) ? 1.0f : 0.0f;
				return true;
			case GraphOp::LessEqual:
				out[0] = operands[0].At(0) <= operands[1].At(0) ? 1.0f : 0.0f;
				return true;
			case GraphOp::Equal:
				out[0] = operands[0].At(0) == operands[1].At(0) ? 1.0f : 0.0f;
				return true;
			case GraphOp::Select:
				for (u32 i = 0; i < count; ++i) {
					out[i] = operands[0].At(0) != 0.0f ? operands[1].values[i] : operands[2].values[i];
				}
				return true;
			case GraphOp::BoolToFloat:
				out[0] = operands[0].At(0);
				return true;
			case GraphOp::LogicalAnd:
				out[0] = operands[0].At(0) != 0.0f && operands[1].At(0) != 0.0f ? 1.0f : 0.0f;
				return true;
			case GraphOp::LogicalOr:
				out[0] = operands[0].At(0) != 0.0f || operands[1].At(0) != 0.0f ? 1.0f : 0.0f;
				return true;
			case GraphOp::LogicalNot:
				out[0] = operands[0].At(0) != 0.0f ? 0.0f : 1.0f;
				return true;
			case GraphOp::All:
			case GraphOp::Any: {
				bool all = true;
				bool any = false;
				for (f32 component : operands[0].values) {
					all = all && component != 0.0f;
					any = any || component != 0.0f;
				}
				out[0] = (value.op == GraphOp::All ? all : any) ? 1.0f : 0.0f;
				return true;
			}
			case GraphOp::IsNaN:
				out[0] = std::isnan(operands[0].At(0)) ? 1.0f : 0.0f;
				return true;
			case GraphOp::IsInfinite:
				out[0] = std::isinf(operands[0].At(0)) ? 1.0f : 0.0f;
				return true;
			default:
				break;
			}
			if (!GraphOps::Foldable(value.op)) {
				return false;
			}
			for (u32 i = 0; i < count; ++i) {
				out[i] = Componentwise(value.op, operands[0].At(i), value.operandCount > 1 ? operands[1].At(i) : 0.0f,
									   value.operandCount > 2 ? operands[2].At(i) : 0.0f);
			}
			return true;
		}

		str ValueKey(const GraphProgram& program, const GraphValue& value) {
			str key;
			auto put = [&key](u32 word) {
				key.append(reinterpret_cast<const char*>(&word), sizeof(word));
			};
			put(static_cast<u32>(value.op) | (static_cast<u32>(value.type) << 8) | (static_cast<u32>(value.operandCount) << 16));
			for (u32 i = 0; i < value.operandCount; ++i) {
				put(value.operands[i]);
			}
			if (value.op == GraphOp::Constant) {
				for (f32 component : std::span<const f32>(program.constants.data() + value.payload, GraphTypes::Components(value.type))) {
					put(std::bit_cast<u32>(component));
				}
			} else {
				put(value.payload);
			}
			return key;
		}

		str StageNames(GraphStageMask stages) {
			constexpr std::pair<GraphStageMask, std::string_view> NAMES[] = {
				{GraphStageVertex, "vertex"}, {GraphStageFragment, "fragment"}, {GraphStageLighting, "lighting"}};
			str out;
			for (const auto& [stage, name] : NAMES) {
				if ((stages & stage) != 0) {
					out += (out.empty() ? "" : " and ") + str(name);
				}
			}
			return out;
		}

		u32 StageCount(GraphStageMask stages) {
			return ((stages & GraphStageVertex) != 0 ? 1u : 0u) + ((stages & GraphStageFragment) != 0 ? 1u : 0u) +
				   ((stages & GraphStageLighting) != 0 ? 1u : 0u);
		}

	} // anonymous namespace

	namespace GraphPasses {

		bool AssignStages(GraphProgram& program, Vector<GraphDiagnostic>& diagnostics) {
			for (GraphValue& value : program.values) {
				value.stages = GraphStageNone;
			}
			for (const GraphRoot& root : program.roots) {
				if (root.value != GRAPH_NO_VALUE) {
					program.values[root.value].stages = static_cast<GraphStageMask>(program.values[root.value].stages | root.stage);
				}
			}
			for (size_t i = program.values.size(); i-- > 0;) {
				const GraphValue& value = program.values[i];
				for (u32 operand = 0; operand < value.operandCount; ++operand) {
					GraphValue& read = program.values[value.operands[operand]];
					read.stages = static_cast<GraphStageMask>(read.stages | value.stages);
				}
			}

			bool valid = true;
			for (const GraphValue& value : program.values) {
				GraphStageMask allowed = value.op == GraphOp::External ? program.externals[value.payload].stages : GraphOps::Stages(value.op);
				GraphStageMask outside = static_cast<GraphStageMask>(value.stages & ~allowed);
				if (outside == GraphStageNone) {
					continue;
				}
				valid = false;
				str what = value.op == GraphOp::External ? "reads " + program.externals[value.payload].name
														 : "uses `" + str(GraphOps::Name(value.op)) + "`";
				str message = what + ", which only the " + StageNames(allowed) + (StageCount(allowed) > 1 ? " stages have" : " stage has") +
							  ", and feeds the " + StageNames(outside) + " stage";
				if (value.op == GraphOp::External && !program.externals[value.payload].unavailable.empty()) {
					message = what + ": " + program.externals[value.payload].unavailable;
				}
				bool reported = false;
				for (const GraphDiagnostic& diagnostic : diagnostics) {
					reported = reported || (diagnostic.node == value.node && diagnostic.message == message);
				}
				if (!reported) {
					diagnostics.push_back({value.node, std::move(message)});
				}
			}
			return valid;
		}

		u32 Fold(GraphProgram& program) {
			u32 folded = 0;
			Vector<f32> result{GetMemoryResource(MemTag::Editor)};
			for (GraphValue& value : program.values) {
				if (value.op == GraphOp::Constant || value.op == GraphOp::External || value.operandCount == 0) {
					continue;
				}
				bool constant = true;
				for (u32 i = 0; i < value.operandCount; ++i) {
					constant = constant && program.values[value.operands[i]].op == GraphOp::Constant;
				}
				if (!constant || !Evaluate(program, value, result)) {
					continue;
				}
				value.op = GraphOp::Constant;
				value.operandCount = 0;
				value.payload = static_cast<u32>(program.constants.size());
				program.constants.insert(program.constants.end(), result.begin(), result.end());
				++folded;
			}
			return folded;
		}

		u32 Deduplicate(GraphProgram& program) {
			Vector<GraphValueId> remap{GetMemoryResource(MemTag::Editor)};
			remap.resize(program.values.size());
			std::pmr::unordered_map<str, GraphValueId> seen{GetMemoryResource(MemTag::Editor)};
			u32 merged = 0;
			for (GraphValueId id = 0; id < program.values.size(); ++id) {
				GraphValue& value = program.values[id];
				for (u32 i = 0; i < value.operandCount; ++i) {
					value.operands[i] = remap[value.operands[i]];
				}
				auto [found, inserted] = seen.try_emplace(ValueKey(program, value), id);
				remap[id] = found->second;
				merged += inserted ? 0 : 1;
			}
			for (GraphRoot& root : program.roots) {
				if (root.value != GRAPH_NO_VALUE) {
					root.value = remap[root.value];
				}
			}
			return merged;
		}

		u32 Eliminate(GraphProgram& program) {
			Vector<u8> live{GetMemoryResource(MemTag::Editor)};
			live.assign(program.values.size(), 0);
			for (const GraphRoot& root : program.roots) {
				if (root.value != GRAPH_NO_VALUE) {
					live[root.value] = 1;
				}
			}
			for (size_t i = program.values.size(); i-- > 0;) {
				if (live[i] == 0) {
					continue;
				}
				const GraphValue& value = program.values[i];
				for (u32 operand = 0; operand < value.operandCount; ++operand) {
					live[value.operands[operand]] = 1;
				}
			}

			Vector<GraphValueId> remap{GetMemoryResource(MemTag::Editor)};
			remap.assign(program.values.size(), GRAPH_NO_VALUE);
			GraphValueId next = 0;
			for (GraphValueId id = 0; id < program.values.size(); ++id) {
				if (live[id] == 0) {
					continue;
				}
				GraphValue value = program.values[id];
				for (u32 i = 0; i < value.operandCount; ++i) {
					value.operands[i] = remap[value.operands[i]];
				}
				remap[id] = next;
				program.values[next] = value;
				if (next != id) {
					program.labels[next] = std::move(program.labels[id]);
				}
				++next;
			}
			u32 removed = static_cast<u32>(program.values.size()) - next;
			program.values.resize(next);
			program.labels.resize(next);
			for (GraphRoot& root : program.roots) {
				if (root.value != GRAPH_NO_VALUE) {
					root.value = remap[root.value];
				}
			}
			return removed;
		}

	} // namespace GraphPasses

} // namespace Shalltear
