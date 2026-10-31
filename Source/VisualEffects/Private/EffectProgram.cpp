#include "EffectProgram.h"

#include "EffectNoise.h"

#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"

#include <algorithm>
#include <array>
#include <bit>
#include <charconv>
#include <cmath>
#include <cstring>
#include <format>

namespace Shalltear::VisualEffectsImpl {

	namespace {

		enum class Shape : u8 {
			Leaf,
			Componentwise,
			Dot,
			Cross,
			Length,
			Distance,
			Normalize,
			Reflect,
			Swizzle,
			Construct,
			Scalar,
			Select,
			Reduce,
			Random,
			ShaderNoise,
			EffectNoise,
			Mesh
		};

		struct OpInfo {
			std::string_view mnemonic;
			Shape shape;
			u8 operands;
		};

		constexpr std::array<OpInfo, static_cast<size_t>(EffectOp::Count)> OPS = {{
			{"const", Shape::Leaf, 0},
			{"input", Shape::Leaf, 0},
			{"negate", Shape::Componentwise, 1},
			{"abs", Shape::Componentwise, 1},
			{"sign", Shape::Componentwise, 1},
			{"floor", Shape::Componentwise, 1},
			{"ceil", Shape::Componentwise, 1},
			{"round", Shape::Componentwise, 1},
			{"trunc", Shape::Componentwise, 1},
			{"frac", Shape::Componentwise, 1},
			{"saturate", Shape::Componentwise, 1},
			{"sqrt", Shape::Componentwise, 1},
			{"rsqrt", Shape::Componentwise, 1},
			{"rcp", Shape::Componentwise, 1},
			{"exp", Shape::Componentwise, 1},
			{"exp2", Shape::Componentwise, 1},
			{"log", Shape::Componentwise, 1},
			{"log2", Shape::Componentwise, 1},
			{"log10", Shape::Componentwise, 1},
			{"sin", Shape::Componentwise, 1},
			{"cos", Shape::Componentwise, 1},
			{"tan", Shape::Componentwise, 1},
			{"asin", Shape::Componentwise, 1},
			{"acos", Shape::Componentwise, 1},
			{"atan", Shape::Componentwise, 1},
			{"sinh", Shape::Componentwise, 1},
			{"cosh", Shape::Componentwise, 1},
			{"tanh", Shape::Componentwise, 1},
			{"add", Shape::Componentwise, 2},
			{"sub", Shape::Componentwise, 2},
			{"mul", Shape::Componentwise, 2},
			{"div", Shape::Componentwise, 2},
			{"pow", Shape::Componentwise, 2},
			{"min", Shape::Componentwise, 2},
			{"max", Shape::Componentwise, 2},
			{"fmod", Shape::Componentwise, 2},
			{"atan2", Shape::Componentwise, 2},
			{"step", Shape::Componentwise, 2},
			{"lerp", Shape::Componentwise, 3},
			{"clamp", Shape::Componentwise, 3},
			{"smoothstep", Shape::Componentwise, 3},
			{"dot", Shape::Dot, 2},
			{"cross", Shape::Cross, 2},
			{"length", Shape::Length, 1},
			{"distance", Shape::Distance, 2},
			{"normalize", Shape::Normalize, 1},
			{"reflect", Shape::Reflect, 2},
			{"swizzle", Shape::Swizzle, 1},
			{"construct", Shape::Construct, 0},
			{"less", Shape::Scalar, 2},
			{"lessequal", Shape::Scalar, 2},
			{"equal", Shape::Scalar, 2},
			{"select", Shape::Select, 3},
			{"and", Shape::Scalar, 2},
			{"or", Shape::Scalar, 2},
			{"not", Shape::Scalar, 1},
			{"all", Shape::Reduce, 1},
			{"any", Shape::Reduce, 1},
			{"isnan", Shape::Scalar, 1},
			{"isinf", Shape::Scalar, 1},
			{"random", Shape::Random, 2},
			{"valuenoise", Shape::ShaderNoise, 1},
			{"gradientnoise", Shape::ShaderNoise, 1},
			{"voronoi", Shape::ShaderNoise, 3},
			{"fractalnoise", Shape::EffectNoise, 3},
			{"curlnoise", Shape::EffectNoise, 3},
			{"meshindex", Shape::Mesh, 1},
			{"meshvertex", Shape::Mesh, 1},
		}};
		static_assert(OPS[static_cast<size_t>(EffectOp::IsInfinite)].mnemonic == "isinf", "OPS is out of step with EffectOp");
		static_assert(OPS[static_cast<size_t>(EffectOp::Random)].mnemonic == "random", "OPS is out of step with EffectOp");
		static_assert(OPS[static_cast<size_t>(EffectOp::MeshVertex)].mnemonic == "meshvertex", "OPS is out of step with EffectOp");

		constexpr u32 NOISE_HASHES = 3;
		constexpr u32 VORONOI_HASHES = 2;
		constexpr u32 EFFECT_NOISE_TYPES = 3;
		constexpr u32 MESH_CHANNELS = 5;

		const OpInfo& Info(EffectOp op) { return OPS[static_cast<size_t>(op)]; }

		u32 SwizzleCount(u32 pattern) { return pattern & 7u; }
		u32 SwizzleComponent(u32 pattern, u32 index) { return (pattern >> (4 + 2 * index)) & 3u; }

		bool Fits(const EffectProgram& program, u32 index, str& error) {
			const EffectInstruction& instruction = program.code[index];
			if (instruction.op >= EffectOp::Count) {
				error = "has no op " + std::to_string(static_cast<u32>(instruction.op));
				return false;
			}
			const OpInfo& info = Info(instruction.op);
			if (instruction.width < 1 || instruction.width > 4) {
				error = std::format("{} is {} wide, where a value is 1 to 4", info.mnemonic, instruction.width);
				return false;
			}
			if (info.shape == Shape::Construct ? (instruction.operandCount < 1 || instruction.operandCount > 4) : instruction.operandCount != info.operands) {
				error = std::format("{} takes {} operand(s)", info.mnemonic, info.shape == Shape::Construct ? str("1 to 4") : std::to_string(info.operands));
				return false;
			}
			u32 widths[4] = {};
			for (u32 i = 0; i < instruction.operandCount; ++i) {
				if (instruction.operands[i] >= index) {
					error = std::format("{} reads instruction {} (not before it)", info.mnemonic, instruction.operands[i]);
					return false;
				}
				widths[i] = program.code[instruction.operands[i]].width;
			}
			const u32 width = instruction.width;
			auto fitsOrBroadcasts = [&](u32 operand) { return widths[operand] == width || widths[operand] == 1; };
			bool ok = true;
			switch (info.shape) {
			case Shape::Leaf:
				if (instruction.op == EffectOp::Constant) {
					ok = instruction.payload + width <= program.constants.size();
				} else {
					ok = instruction.payload < program.inputs.size() && program.inputs[instruction.payload].width == width;
				}
				break;
			case Shape::Componentwise:
				for (u32 i = 0; i < instruction.operandCount; ++i) {
					ok = ok && fitsOrBroadcasts(i);
				}
				break;
			case Shape::Dot:
			case Shape::Distance:
				ok = width == 1 && widths[0] == widths[1];
				break;
			case Shape::Cross:
				ok = width == 3 && widths[0] == 3 && widths[1] == 3;
				break;
			case Shape::Length:
				ok = width == 1;
				break;
			case Shape::Normalize:
				ok = widths[0] == width;
				break;
			case Shape::Reflect:
				ok = widths[0] == width && widths[1] == width;
				break;
			case Shape::Swizzle:
				ok = SwizzleCount(instruction.payload) == width;
				for (u32 i = 0; ok && i < width; ++i) {
					ok = widths[0] == 1 ? SwizzleComponent(instruction.payload, i) == 0 : SwizzleComponent(instruction.payload, i) < widths[0];
				}
				break;
			case Shape::Construct: {
				u32 sum = 0;
				for (u32 i = 0; i < instruction.operandCount; ++i) {
					sum += widths[i];
				}
				ok = sum == width;
				break;
			}
			case Shape::Scalar:
				ok = width == 1;
				for (u32 i = 0; i < instruction.operandCount; ++i) {
					ok = ok && widths[i] == 1;
				}
				break;
			case Shape::Select:
				ok = widths[0] == 1 && fitsOrBroadcasts(1) && fitsOrBroadcasts(2);
				break;
			case Shape::Reduce:
				ok = width == 1;
				break;
			case Shape::Random:
				ok = width == 1 && widths[0] == 1 && widths[1] == 1;
				break;
			case Shape::ShaderNoise:
				if (instruction.op == EffectOp::Voronoi) {
					ok = width == 2 && widths[0] == 2 && widths[1] == 1 && widths[2] == 1 && instruction.payload < VORONOI_HASHES;
				} else {
					ok = width == 1 && widths[0] == 2 && instruction.payload < NOISE_HASHES;
				}
				break;
			case Shape::EffectNoise: {
				const bool common = widths[1] == 3 && widths[2] == 1 && instruction.payload < EFFECT_NOISE_TYPES;
				ok = instruction.op == EffectOp::FractalNoise ? common && widths[0] >= 1 && widths[0] <= 3 && width == widths[0] + 1
															  : common && (widths[0] == 2 || widths[0] == 3) && width == widths[0];
				break;
			}
			case Shape::Mesh: {
				const bool vertex = instruction.op == EffectOp::MeshVertex;
				const u32 mesh = vertex ? instruction.payload >> EFFECT_MESH_CHANNEL_BITS : instruction.payload;
				const u32 channel = vertex ? instruction.payload & ((1u << EFFECT_MESH_CHANNEL_BITS) - 1u) : 0u;
				ok = widths[0] == 1 && mesh < program.meshes.size() && channel < MESH_CHANNELS && width == (!vertex ? 1u : (channel <= 1 ? 3u : 4u));
				break;
			}
			}
			if (!ok) {
				error = std::format("{} doesn't fit its operands or its payload", info.mnemonic);
			}
			return ok;
		}

		bool ParseNumber(std::string_view token, u32& out, int base = 10) {
			if (base == 16) {
				if (token.size() < 3 || token[0] != '0' || (token[1] != 'x' && token[1] != 'X')) {
					return false;
				}
				token.remove_prefix(2);
			}
			const auto [end, code] = std::from_chars(token.data(), token.data() + token.size(), out, base);
			return code == std::errc() && end == token.data() + token.size();
		}

		void Split(std::string_view line, Vector<std::string_view>& out) {
			out.clear();
			size_t at = 0;
			while (at < line.size()) {
				while (at < line.size() && (line[at] == ' ' || line[at] == '\t' || line[at] == '\r')) {
					++at;
				}
				const size_t start = at;
				while (at < line.size() && line[at] != ' ' && line[at] != '\t' && line[at] != '\r') {
					++at;
				}
				if (at > start) {
					out.push_back(line.substr(start, at - start));
				}
			}
		}

		f32 Unary(EffectOp op, f32 a) {
			switch (op) {
			case EffectOp::Negate:
				return -a;
			case EffectOp::Absolute:
				return std::fabs(a);
			case EffectOp::Sign:
				return a > 0.0f ? 1.0f : (a < 0.0f ? -1.0f : 0.0f);
			case EffectOp::Floor:
				return std::floor(a);
			case EffectOp::Ceiling:
				return std::ceil(a);
			case EffectOp::Round:
				return std::nearbyint(a);
			case EffectOp::Truncate:
				return std::trunc(a);
			case EffectOp::Fraction:
				return a - std::floor(a);
			case EffectOp::Saturate:
				return std::min(std::max(a, 0.0f), 1.0f);
			case EffectOp::SquareRoot:
				return std::sqrt(a);
			case EffectOp::ReciprocalSquareRoot:
				return 1.0f / std::sqrt(a);
			case EffectOp::Reciprocal:
				return 1.0f / a;
			case EffectOp::Exponential:
				return std::exp(a);
			case EffectOp::Exponential2:
				return std::exp2(a);
			case EffectOp::Logarithm:
				return std::log(a);
			case EffectOp::Logarithm2:
				return std::log2(a);
			case EffectOp::Logarithm10:
				return std::log10(a);
			case EffectOp::Sine:
				return std::sin(a);
			case EffectOp::Cosine:
				return std::cos(a);
			case EffectOp::Tangent:
				return std::tan(a);
			case EffectOp::Arcsine:
				return std::asin(a);
			case EffectOp::Arccosine:
				return std::acos(a);
			case EffectOp::Arctangent:
				return std::atan(a);
			case EffectOp::HyperbolicSine:
				return std::sinh(a);
			case EffectOp::HyperbolicCosine:
				return std::cosh(a);
			case EffectOp::HyperbolicTangent:
				return std::tanh(a);
			default:
				return 0.0f;
			}
		}

		u32 RandomHash(u32 h) {
			h = ((h >> 16) ^ h) * 0x45d9f3bu;
			h = ((h >> 16) ^ h) * 0x45d9f3bu;
			return (h >> 16) ^ h;
		}

		f32 Random(f32 key, f32 seed, u32 salt) {
			const u32 h = RandomHash(std::bit_cast<u32>(key) ^ RandomHash(std::bit_cast<u32>(seed) + salt * 0x9e3779b9u));
			return std::bit_cast<f32>(0x3f800000u | (h >> 9)) - 1.0f;
		}

		void MeshRead(const EffectInstruction& instruction, const EffectProgram& program, std::span<const EffectMeshView> meshes, f32 position,
					  EffectValue& out) {
			const bool vertex = instruction.op == EffectOp::MeshVertex;
			const EffectMeshView& mesh = meshes[program.meshes[vertex ? instruction.payload >> EFFECT_MESH_CHANNEL_BITS : instruction.payload].mesh];
			const u32 at = position >= 4294967296.0f ? ~0u : (position >= 0.0f ? static_cast<u32>(position) : 0u);
			if (!vertex) {
				out.v[0] = at < mesh.indexCount ? static_cast<f32>(mesh.indices[at]) : 0.0f;
				return;
			}
			if (at >= mesh.vertexCount) {
				return;
			}
			const MeshVertex& read = mesh.vertices[at];
			switch (instruction.payload & ((1u << EFFECT_MESH_CHANNEL_BITS) - 1u)) {
			case 0:
				out = EffectValue{{read.position.x, read.position.y, read.position.z, 0.0f}};
				break;
			case 1:
				out = EffectValue{{read.normal.x, read.normal.y, read.normal.z, 0.0f}};
				break;
			case 2:
				out = EffectValue{{read.tangent.x, read.tangent.y, read.tangent.z, read.tangentSign}};
				break;
			case 3:
				out = EffectValue{{static_cast<f32>(read.color.r) / 255.0f, static_cast<f32>(read.color.g) / 255.0f, static_cast<f32>(read.color.b) / 255.0f,
								   static_cast<f32>(read.color.a) / 255.0f}};
				break;
			default:
				out = EffectValue{{read.u, read.v, 0.0f, 0.0f}};
				break;
			}
		}

		f32 Nary(EffectOp op, f32 a, f32 b, f32 c) {
			switch (op) {
			case EffectOp::Add:
				return a + b;
			case EffectOp::Subtract:
				return a - b;
			case EffectOp::Multiply:
				return a * b;
			case EffectOp::Divide:
				return a / b;
			case EffectOp::Power:
				return std::pow(a, b);
			case EffectOp::Minimum:
				return std::min(a, b);
			case EffectOp::Maximum:
				return std::max(a, b);
			case EffectOp::Modulo:
				return std::fmod(a, b);
			case EffectOp::Arctangent2:
				return std::atan2(a, b);
			case EffectOp::Step:
				return b >= a ? 1.0f : 0.0f;
			case EffectOp::Lerp:
				return a + c * (b - a);
			case EffectOp::Clamp:
				return std::min(std::max(a, b), c);
			case EffectOp::Smoothstep: {
				const f32 t = std::min(std::max((c - a) / (b - a), 0.0f), 1.0f);
				return t * t * (3.0f - 2.0f * t);
			}
			default:
				return Unary(op, a);
			}
		}

	} // anonymous namespace

	namespace EffectPrograms {

		bool Assemble(std::string_view text, EffectProgram& out, str& error) {
			out = EffectProgram{};
			EffectProgram program;
			Vector<std::string_view> tokens{GetMemoryResource(MemTag::Asset)};
			u32 lineNumber = 0;
			size_t at = 0;
			auto refuse = [&](std::string_view why) {
				error = std::format("line {}: {}", lineNumber, why);
				return false;
			};
			while (at <= text.size()) {
				const size_t end = std::min(text.find('\n', at), text.size());
				const std::string_view line = text.substr(at, end - at);
				at = end + 1;
				++lineNumber;
				Split(line, tokens);
				if (tokens.empty() || tokens[0].starts_with('#')) {
					if (end == text.size()) {
						break;
					}
					continue;
				}
				if (tokens[0] == "root") {
					u32 root = 0;
					if (tokens.size() != 2 || !ParseNumber(tokens[1], root) || root >= program.code.size()) {
						return refuse("a root points at no instruction");
					}
					program.roots.push_back(root);
					continue;
				}
				if (tokens[0] == "mesh") {
					if (tokens.size() != 2) {
						return refuse("a mesh is a name");
					}
					program.meshes.push_back({str(tokens[1]), 0});
					continue;
				}
				EffectInstruction instruction;
				const auto found = std::find_if(OPS.begin(), OPS.end(), [&](const OpInfo& info) { return info.mnemonic == tokens[0]; });
				if (found == OPS.end()) {
					return refuse("'" + str(tokens[0]) + "' is no op");
				}
				instruction.op = static_cast<EffectOp>(found - OPS.begin());
				u32 width = 0;
				if (tokens.size() < 2 || !ParseNumber(tokens[1], width) || width < 1 || width > 4) {
					return refuse("a value is 1 to 4 wide");
				}
				instruction.width = static_cast<u8>(width);
				if (instruction.op == EffectOp::Input) {
					if (tokens.size() != 3) {
						return refuse("an input is a width and a name");
					}
					instruction.payload = static_cast<u32>(program.inputs.size());
					program.inputs.push_back({str(tokens[2]), width});
				} else if (instruction.op == EffectOp::Constant) {
					if (tokens.size() != 2 + width) {
						return refuse("a constant has 1 component per width");
					}
					instruction.payload = static_cast<u32>(program.constants.size());
					for (u32 i = 0; i < width; ++i) {
						u32 bits = 0;
						if (!ParseNumber(tokens[2 + i], bits, 16)) {
							return refuse("a constant's component is no hex bits");
						}
						f32 value = 0.0f;
						std::memcpy(&value, &bits, sizeof(value));
						program.constants.push_back(value);
					}
				} else {
					if (tokens.size() < 3 || tokens.size() > 7 || !ParseNumber(tokens[2], instruction.payload)) {
						return refuse("an op is a width, a payload and its operands");
					}
					instruction.operandCount = static_cast<u8>(tokens.size() - 3);
					for (u32 i = 0; i < instruction.operandCount; ++i) {
						if (!ParseNumber(tokens[3 + i], instruction.operands[i])) {
							return refuse("an operand is no instruction number");
						}
					}
				}
				program.code.push_back(instruction);
				str why;
				if (!Fits(program, static_cast<u32>(program.code.size() - 1), why)) {
					return refuse(why);
				}
				if (end == text.size()) {
					break;
				}
			}
			out = std::move(program);
			return true;
		}

		bool Validate(const EffectProgram& program, str& error) {
			for (u32 i = 0; i < program.code.size(); ++i) {
				if (!Fits(program, i, error)) {
					error = std::format("instruction {}: {}", i, error);
					return false;
				}
			}
			for (u32 root : program.roots) {
				if (root >= program.code.size()) {
					error = "a root points at no instruction";
					return false;
				}
			}
			return true;
		}

		void Cook(const EffectProgram& program, CookWriter& out) {
			out.Write(static_cast<u32>(program.code.size()));
			out.WriteBytes(program.code.data(), program.code.size() * sizeof(EffectInstruction));
			out.Write(static_cast<u32>(program.constants.size()));
			out.WriteBytes(program.constants.data(), program.constants.size() * sizeof(f32));
			out.Write(static_cast<u32>(program.inputs.size()));
			for (const EffectInput& input : program.inputs) {
				out.WriteString(input.name);
				out.Write(input.width);
			}
			out.Write(static_cast<u32>(program.roots.size()));
			out.WriteBytes(program.roots.data(), program.roots.size() * sizeof(u32));
			out.Write(static_cast<u32>(program.meshes.size()));
			for (const EffectMeshInput& mesh : program.meshes) {
				out.WriteString(mesh.name);
			}
		}

		bool LoadCooked(CookReader& in, EffectProgram& out) {
			out = EffectProgram{};
			EffectProgram program;
			u32 count = 0;
			in.Read(count);
			in.ReadInto(program.code, count);
			in.Read(count);
			in.ReadInto(program.constants, count);
			in.Read(count);
			for (u32 i = 0; i < count && !in.Failed(); ++i) {
				EffectInput input;
				input.name = str(in.ReadString());
				in.Read(input.width);
				program.inputs.push_back(std::move(input));
			}
			in.Read(count);
			in.ReadInto(program.roots, count);
			in.Read(count);
			for (u32 i = 0; i < count && !in.Failed(); ++i) {
				program.meshes.push_back({str(in.ReadString()), 0});
			}
			str error;
			if (in.Failed() || !Validate(program, error)) {
				return false;
			}
			out = std::move(program);
			return true;
		}

	} // namespace EffectPrograms

	bool EffectMachine::Run(const EffectProgram& program, std::span<const EffectValue> slots, std::span<const EffectMeshView> meshes) {
		for (const EffectInput& input : program.inputs) {
			if (input.slot >= slots.size()) {
				return false;
			}
		}
		for (const EffectMeshInput& mesh : program.meshes) {
			if (mesh.mesh >= meshes.size()) {
				return false;
			}
		}
		_registers.resize(std::max(_registers.size(), program.code.size()));
		EffectValue* r = _registers.data();
		for (u32 index = 0; index < program.code.size(); ++index) {
			const EffectInstruction& instruction = program.code[index];
			const u32 width = instruction.width;
			EffectValue& out = r[index];
			out = EffectValue{};
			auto at = [&](u32 o, u32 i) {
				const u32 source = instruction.operands[o];
				return program.code[source].width == 1 ? r[source].v[0] : r[source].v[i];
			};
			auto operandWidth = [&](u32 o) { return static_cast<u32>(program.code[instruction.operands[o]].width); };
			auto dot = [&](u32 a, u32 b) {
				f32 sum = 0.0f;
				for (u32 i = 0; i < operandWidth(a); ++i) {
					sum += at(a, i) * at(b, i);
				}
				return sum;
			};
			switch (Info(instruction.op).shape) {
			case Shape::Leaf:
				for (u32 i = 0; i < width; ++i) {
					out.v[i] = instruction.op == EffectOp::Constant ? program.constants[instruction.payload + i]
																	: slots[program.inputs[instruction.payload].slot].v[i];
				}
				break;
			case Shape::Componentwise:
				for (u32 i = 0; i < width; ++i) {
					const f32 a = at(0, i);
					const f32 b = instruction.operandCount > 1 ? at(1, i) : 0.0f;
					const f32 c = instruction.operandCount > 2 ? at(2, i) : 0.0f;
					out.v[i] = Nary(instruction.op, a, b, c);
				}
				break;
			case Shape::Dot:
				out.v[0] = dot(0, 1);
				break;
			case Shape::Cross:
				out.v[0] = at(0, 1) * at(1, 2) - at(0, 2) * at(1, 1);
				out.v[1] = at(0, 2) * at(1, 0) - at(0, 0) * at(1, 2);
				out.v[2] = at(0, 0) * at(1, 1) - at(0, 1) * at(1, 0);
				break;
			case Shape::Length:
				out.v[0] = std::sqrt(dot(0, 0));
				break;
			case Shape::Distance: {
				f32 sum = 0.0f;
				for (u32 i = 0; i < operandWidth(0); ++i) {
					const f32 d = at(0, i) - at(1, i);
					sum += d * d;
				}
				out.v[0] = std::sqrt(sum);
				break;
			}
			case Shape::Normalize: {
				const f32 length = std::sqrt(dot(0, 0));
				for (u32 i = 0; i < width; ++i) {
					out.v[i] = at(0, i) / length;
				}
				break;
			}
			case Shape::Reflect: {
				const f32 d = dot(1, 0);
				for (u32 i = 0; i < width; ++i) {
					out.v[i] = at(0, i) - 2.0f * d * at(1, i);
				}
				break;
			}
			case Shape::Swizzle:
				for (u32 i = 0; i < width; ++i) {
					out.v[i] = r[instruction.operands[0]].v[operandWidth(0) == 1 ? 0 : SwizzleComponent(instruction.payload, i)];
				}
				break;
			case Shape::Construct: {
				u32 next = 0;
				for (u32 o = 0; o < instruction.operandCount; ++o) {
					for (u32 i = 0; i < operandWidth(o); ++i) {
						out.v[next++] = r[instruction.operands[o]].v[i];
					}
				}
				break;
			}
			case Shape::Scalar: {
				const f32 a = at(0, 0);
				const f32 b = instruction.operandCount > 1 ? at(1, 0) : 0.0f;
				bool result = false;
				switch (instruction.op) {
				case EffectOp::Less:
					result = a < b;
					break;
				case EffectOp::LessEqual:
					result = a <= b;
					break;
				case EffectOp::Equal:
					result = a == b;
					break;
				case EffectOp::LogicalAnd:
					result = a != 0.0f && b != 0.0f;
					break;
				case EffectOp::LogicalOr:
					result = a != 0.0f || b != 0.0f;
					break;
				case EffectOp::LogicalNot:
					result = a == 0.0f;
					break;
				case EffectOp::IsNaN:
					result = std::isnan(a);
					break;
				case EffectOp::IsInfinite:
					result = std::isinf(a);
					break;
				default:
					break;
				}
				out.v[0] = result ? 1.0f : 0.0f;
				break;
			}
			case Shape::Select:
				for (u32 i = 0; i < width; ++i) {
					out.v[i] = at(0, 0) != 0.0f ? at(1, i) : at(2, i);
				}
				break;
			case Shape::Reduce: {
				bool all = true;
				bool any = false;
				for (u32 i = 0; i < operandWidth(0); ++i) {
					all = all && at(0, i) != 0.0f;
					any = any || at(0, i) != 0.0f;
				}
				out.v[0] = (instruction.op == EffectOp::All ? all : any) ? 1.0f : 0.0f;
				break;
			}
			case Shape::Random:
				out.v[0] = Random(at(0, 0), at(1, 0), instruction.payload);
				break;
			case Shape::ShaderNoise: {
				const f32 x = at(0, 0);
				const f32 y = at(0, 1);
				if (instruction.op == EffectOp::Voronoi) {
					EffectNoises::ShaderVoronoi(instruction.payload, x, y, at(1, 0), at(2, 0), out.v);
				} else {
					out.v[0] = instruction.op == EffectOp::ValueNoise ? EffectNoises::ShaderValue(instruction.payload, x, y)
																	  : EffectNoises::ShaderGradient(instruction.payload, x, y);
				}
				break;
			}
			case Shape::EffectNoise: {
				const EffectValue& coordinate = r[instruction.operands[0]];
				const EffectValue& parameters = r[instruction.operands[1]];
				const u32 dimension = operandWidth(0);
				if (instruction.op == EffectOp::FractalNoise) {
					EffectNoises::Fractal(instruction.payload, dimension, coordinate.v, parameters.v, at(2, 0), out.v);
				} else {
					EffectNoises::Curl(instruction.payload, dimension, coordinate.v, parameters.v, at(2, 0), out.v);
				}
				break;
			}
			case Shape::Mesh:
				MeshRead(instruction, program, meshes, at(0, 0), out);
				break;
			}
		}
		return true;
	}

} // namespace Shalltear::VisualEffectsImpl
