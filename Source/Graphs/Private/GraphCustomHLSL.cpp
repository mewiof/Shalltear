#include "GraphCustomHLSL.h"

#include "GraphDefinitions.h"
#include "GraphEffect.h"
#include "GraphNodeLibrary.h"

#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Editor/GraphDocument.h"

#include <algorithm>
#include <format>
#include <iterator>

namespace Shalltear {

	namespace {

		using P = GraphPortType;
		using S = GraphSettingKind;
		using T = GraphValueType;
		using GraphCustomHLSL::Access;
		using GraphCustomHLSL::FieldAccess;
		using GraphCustomHLSL::Function;
		using GraphCustomHLSL::Parameter;

		bool IsIdentifierStart(char c) { return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '_'; }
		bool IsIdentifierCharacter(char c) { return IsIdentifierStart(c) || (c >= '0' && c <= '9'); }
		bool IsSpace(char c) { return c == ' ' || c == '\t' || c == '\r' || c == '\n' || c == '\v' || c == '\f'; }

		std::string_view IdentifierAt(std::string_view text, size_t at) {
			if (at >= text.size() || !IsIdentifierStart(text[at])) {
				return {};
			}
			size_t end = at + 1;
			while (end < text.size() && IsIdentifierCharacter(text[end])) {
				++end;
			}
			return text.substr(at, end - at);
		}

		size_t SkipSpace(std::string_view text, size_t at) {
			while (at < text.size() && IsSpace(text[at])) {
				++at;
			}
			return at;
		}

		std::string_view Trim(std::string_view text) {
			while (!text.empty() && IsSpace(text.front())) {
				text.remove_prefix(1);
			}
			while (!text.empty() && IsSpace(text.back())) {
				text.remove_suffix(1);
			}
			return text;
		}

		bool EqualsIgnoringCase(std::string_view a, std::string_view b) {
			return a.size() == b.size() && std::equal(a.begin(), a.end(), b.begin(), [](char x, char y) {
					   return std::tolower(static_cast<unsigned char>(x)) == std::tolower(static_cast<unsigned char>(y));
				   });
		}

		str WithoutComments(std::string_view text) {
			str out(text);
			for (size_t i = 0; i + 1 < out.size(); ++i) {
				if (out[i] == '/' && out[i + 1] == '/') {
					while (i < out.size() && out[i] != '\n') {
						out[i++] = ' ';
					}
				} else if (out[i] == '/' && out[i + 1] == '*') {
					const size_t close = out.find("*/", i + 2);
					const size_t end = close == str::npos ? out.size() : close + 2;
					for (; i < end; ++i) {
						out[i] = out[i] == '\n' ? '\n' : ' ';
					}
					--i;
				}
			}
			return out;
		}

		constexpr std::string_view EFFECT_ATTRIBUTES = "EffectAttributes";

		constexpr std::string_view KNOWN_TYPES[] = {
			"void", "float", "float2", "float3",
			"float4", "float4x4", "EffectSampler2D", "EffectSampler3D",
			"EffectSampler2DArray", "EffectSamplerCube", "EffectGradient", "EffectCurve",
			"bool", "uint", "int", "EffectAttributes",
			"StructuredBuffer", "RWStructuredBuffer", "ByteAddressBuffer", "RWByteAddressBuffer",
			"Buffer", "RWBuffer", "AppendStructuredBuffer", "ConsumeStructuredBuffer",
			"Texture1D", "Texture2D", "Texture3D", "TextureCube",
			"Texture1DArray", "Texture2DArray", "TextureCubeArray", "RWTexture1D",
			"RWTexture2D", "RWTexture3D", "RWTextureCube", "RWTexture1DArray",
			"RWTexture2DArray", "RWTextureCubeArray"};

		bool IsKnown(std::string_view type) { return std::find(std::begin(KNOWN_TYPES), std::end(KNOWN_TYPES), type) != std::end(KNOWN_TYPES); }

		T ValueOf(std::string_view type) {
			if (type == "float" || type == "int" || type == "uint") {
				return T::Float;
			}
			if (type == "float2") {
				return T::Float2;
			}
			if (type == "float3") {
				return T::Float3;
			}
			if (type == "float4") {
				return T::Float4;
			}
			if (type == "float4x4") {
				return T::Float4x4;
			}
			if (type == "bool") {
				return T::Bool;
			}
			return T::None;
		}

		P PortOf(T type) {
			switch (type) {
			case T::Float2:
				return P::Float2;
			case T::Float3:
				return P::Float3;
			case T::Float4:
				return P::Float4;
			case T::Float4x4:
				return P::Float4x4;
			case T::Bool:
				return P::Boolean;
			default:
				return P::Float;
			}
		}

		str Cast(std::string_view type, std::string_view value) {
			return type == "int" || type == "uint" ? "(" + str(type) + ")" + str(value) : str(value);
		}

		constexpr std::string_view VARIADIC[] = {"angle", "angularVelocity", "pivot", "scale"};
		constexpr std::string_view UNSIGNED[] = {"particleId", "spawnIndex", "collisionEventCount", "stripIndex", "particleIndexInStrip",
												 "particleCountInStrip", "spawnIndexInStrip"};
		constexpr std::string_view AXES = "XYZ";

		bool IsVariadic(std::string_view attribute) { return std::find(std::begin(VARIADIC), std::end(VARIADIC), attribute) != std::end(VARIADIC); }

		struct Field {
			str name;
			const GraphEffectAttributeInfo* attribute = nullptr;
			i32 component = -1;
			std::string_view type;
		};

		void Fields(Vector<Field>& out) {
			out.clear();
			for (const GraphEffectAttributeInfo& attribute : GraphEffectAttributes::All()) {
				if (IsVariadic(attribute.name)) {
					for (i32 c = 0; c < 3; ++c) {
						out.push_back({str(attribute.name) + AXES[static_cast<size_t>(c)], &attribute, c, "float"});
					}
					continue;
				}
				const bool unsignedField = std::find(std::begin(UNSIGNED), std::end(UNSIGNED), attribute.name) != std::end(UNSIGNED);
				out.push_back({str(attribute.name), &attribute, -1, unsignedField ? std::string_view("uint") : GraphTypes::HLSLName(attribute.type)});
			}
		}

		const Field* FindField(const Vector<Field>& fields, std::string_view name) {
			for (const Field& field : fields) {
				if (field.name == name) {
					return &field;
				}
			}
			return nullptr;
		}

		str Upper(std::string_view text) {
			str out(text);
			for (char& c : out) {
				c = static_cast<char>(std::toupper(static_cast<unsigned char>(c)));
			}
			return out;
		}

		str Prelude(const Vector<Field>& fields) {
			str out = "#ifndef SLT_EFFECT_ATTRIBUTES\n#define SLT_EFFECT_ATTRIBUTES\nstruct EffectAttributes\n{\n";
			for (const Field& field : fields) {
				out += "\t" + str(field.type) + " " + field.name + ";\n";
			}
			out += "\tuint sltRandom;\n\tuint sltFixedRandom;\n};\n";
			for (const Field& field : fields) {
				out += "#define EFFECT_USE_" + Upper(field.name) + "_CURRENT 1\n";
			}
			out += R"(uint SltEffectRandomHash(uint h)
{
	h = ((h >> 16) ^ h) * 0x45d9f3bu;
	h = ((h >> 16) ^ h) * 0x45d9f3bu;
	return (h >> 16) ^ h;
}
float SltEffectRandom(inout uint state)
{
	state = SltEffectRandomHash(state + 0x9e3779b9u);
	return asfloat(0x3f800000u | (state >> 9)) - 1.0;
}
float SltEffectFixedRandom(uint state)
{
	return asfloat(0x3f800000u | (SltEffectRandomHash(state) >> 9)) - 1.0;
}
#define EFFECT_RAND SltEffectRandom(attributes.sltRandom)
#define EFFECT_RAND2 float2(EFFECT_RAND, EFFECT_RAND)
#define EFFECT_RAND3 float3(EFFECT_RAND, EFFECT_RAND, EFFECT_RAND)
#define EFFECT_RAND4 float4(EFFECT_RAND, EFFECT_RAND, EFFECT_RAND, EFFECT_RAND)
#define EFFECT_FIXED_RAND(h) SltEffectFixedRandom(attributes.sltFixedRandom ^ (h))
#define EFFECT_FIXED_RAND2(h) float2(EFFECT_FIXED_RAND(h), EFFECT_FIXED_RAND(h))
#define EFFECT_FIXED_RAND3(h) float3(EFFECT_FIXED_RAND(h), EFFECT_FIXED_RAND(h), EFFECT_FIXED_RAND(h))
#define EFFECT_FIXED_RAND4(h) float4(EFFECT_FIXED_RAND(h), EFFECT_FIXED_RAND(h), EFFECT_FIXED_RAND(h), EFFECT_FIXED_RAND(h))
#endif
)";
			return out;
		}

		void RandomsOf(std::string_view body, bool& random, bool& fixedRandom) {
			const str text = WithoutComments(body);
			random = false;
			fixedRandom = false;
			for (size_t at = 0; at < text.size();) {
				const std::string_view word = IdentifierAt(text, at);
				if (word.empty() || (at > 0 && IsIdentifierCharacter(text[at - 1]))) {
					++at;
					continue;
				}
				auto stem = [&word](std::string_view name) {
					return word == name || (word.size() == name.size() + 1 && word.starts_with(name) && word.back() >= '2' && word.back() <= '4');
				};
				random = random || stem("EFFECT_RAND");
				fixedRandom = fixedRandom || stem("EFFECT_FIXED_RAND");
				at += word.size();
			}
		}

		str SettingText(const GraphNode& node, std::string_view key) {
			for (const GraphSetting& setting : node.settings) {
				if (setting.key != key) {
					continue;
				}
				LinearAllocator arena(setting.value.size() + 64, MemTag::Editor);
				const JSONValue value = JSONParse(setting.value, arena);
				return value.IsString() ? str(value.AsString()) : str();
			}
			return {};
		}

		bool IsBlock(std::string_view def) { return def == GRAPH_EFFECT_CUSTOM_HLSL_BLOCK; }

		T PortValue(P type) {
			switch (type) {
			case P::Float2:
				return T::Float2;
			case P::Float3:
				return T::Float3;
			case P::Float4:
				return T::Float4;
			case P::Float4x4:
				return T::Float4x4;
			case P::Boolean:
				return T::Bool;
			default:
				return T::Float;
			}
		}

		str Hex(u64 value) { return std::format("{:016X}", value); }

		str Declared(const Parameter& parameter) {
			constexpr std::string_view ACCESS[] = {"", "in ", "out ", "inout "};
			return str(ACCESS[static_cast<size_t>(parameter.access)]) + parameter.type + (parameter.templateType.empty() ? "" : "<" + parameter.templateType + ">") +
				   " " + parameter.name;
		}

		str WithoutIncludes(std::string_view text) {
			str out;
			size_t at = 0;
			while (at < text.size()) {
				size_t end = text.find('\n', at);
				end = end == std::string_view::npos ? text.size() : end + 1;
				const std::string_view line = text.substr(at, end - at);
				if (!Trim(line).starts_with("#include")) {
					out += line;
				}
				at = end;
			}
			return out;
		}

	} // anonymous namespace

	namespace GraphCustomHLSL {

		void BlockBody(GraphBodyContext& c) {
			const GraphCustomHLSLCall* built = c.definition->customHLSL;
			if (!built) {
				c.error = "has no code it can run";
				return;
			}
			if (!built->error.empty()) {
				c.error = built->error;
				return;
			}
			GraphProgramBuilder& b = c.builder;
			Vector<GraphValueId> inputs{GetMemoryResource(MemTag::Editor)};
			for (const str& attribute : built->attributes) {
				inputs.push_back(c.Binding(GraphEffectAttributes::CurrentBinding(attribute)));
			}
			if (built->random) {
				inputs.push_back(b.Random(c.Binding(GRAPH_EFFECT_RANDOM_PARTICLE), b.Scalar(0.0f), b.Identity() * 8u));
			}
			if (built->fixedRandom) {
				inputs.push_back(b.Random(c.Binding(GRAPH_EFFECT_RANDOM_PARTICLE_CONSTANT), b.Scalar(0.0f), 0u));
			}
			if (!c.error.empty()) {
				return;
			}
			inputs.insert(inputs.end(), c.inputs.begin(), c.inputs.end());
			b.Call(built->call, inputs, c.outputs);
		}

		void OperatorBody(GraphBodyContext& c) {
			const GraphCustomHLSLCall* built = c.definition->customHLSL;
			if (!built) {
				c.error = "has no code it can run";
				return;
			}
			if (!built->error.empty()) {
				c.error = built->error;
				return;
			}
			c.builder.Call(built->call, c.inputs, c.outputs);
		}

		void Parse(std::string_view code, Vector<Function>& out) {
			out.clear();
			Vector<size_t> starts{GetMemoryResource(MemTag::Editor)};
			Vector<size_t> bodies{GetMemoryResource(MemTag::Editor)};
			size_t line = 0;
			while (line < code.size()) {
				size_t next = code.find('\n', line);
				next = next == std::string_view::npos ? code.size() : next + 1;
				const std::string_view returnType = IdentifierAt(code, line);
				const size_t named = SkipSpace(code, line + returnType.size());
				const std::string_view name = IsKnown(returnType) && named > line + returnType.size() ? IdentifierAt(code, named) : std::string_view();
				const size_t open = name.empty() ? std::string_view::npos : SkipSpace(code, named + name.size());
				const size_t close = open < code.size() && code[open] == '(' ? code.find(')', open + 1) : std::string_view::npos;
				if (close == std::string_view::npos) {
					line = next;
					continue;
				}
				Function& function = out.emplace_back();
				function.name = str(name);
				function.returnType = str(returnType);
				function.returnName = "out";
				size_t start = line;
				while (start > 0) {
					const size_t end = start - 1;
					const size_t newline = end == 0 ? std::string_view::npos : code.rfind('\n', end - 1);
					const size_t from = newline == std::string_view::npos ? 0 : newline + 1;
					const std::string_view above = code.substr(from, end - from);
					if (!above.starts_with("///")) {
						break;
					}
					const std::string_view text = Trim(above.substr(3));
					if (EqualsIgnoringCase(text, "Hidden")) {
						function.hidden = true;
					} else if (const std::string_view key = IdentifierAt(text, 0); !key.empty() && key == "return") {
						const size_t colon = SkipSpace(text, key.size());
						if (colon < text.size() && text[colon] == ':') {
							const std::string_view value = Trim(text.substr(colon + 1));
							function.returnName = value.empty() ? str("out") : str(value);
						}
					}
					start = from;
				}
				const std::string_view list = code.substr(open + 1, close - open - 1);
				size_t at = 0;
				while (at <= list.size()) {
					size_t comma = list.find(',', at);
					comma = comma == std::string_view::npos ? list.size() : comma;
					const std::string_view piece = Trim(list.substr(at, comma - at));
					at = comma + 1;
					if (piece.empty()) {
						continue;
					}
					Parameter& parameter = function.parameters.emplace_back();
					size_t word = 0;
					std::string_view token = IdentifierAt(piece, word);
					if (token == "in" || token == "out" || token == "inout") {
						parameter.access = token == "in" ? Access::In : (token == "out" ? Access::Out : Access::InOut);
						word = SkipSpace(piece, word + token.size());
						token = IdentifierAt(piece, word);
					}
					parameter.type = str(token);
					word = SkipSpace(piece, word + token.size());
					if (word < piece.size() && piece[word] == '<') {
						const size_t end = piece.find('>', word);
						parameter.templateType = str(Trim(piece.substr(word + 1, end == std::string_view::npos ? 0 : end - word - 1)));
						word = end == std::string_view::npos ? piece.size() : SkipSpace(piece, end + 1);
					}
					parameter.name = str(IdentifierAt(piece, word));
				}
				starts.push_back(start);
				bodies.push_back(SkipSpace(code, close + 1));
				line = code.find('\n', close);
				line = line == std::string_view::npos ? code.size() : line + 1;
			}
			for (size_t i = 0; i < out.size(); ++i) {
				const size_t end = i + 1 < out.size() ? starts[i + 1] : code.size();
				out[i].body = str(Trim(code.substr(std::min(bodies[i], end), end - std::min(bodies[i], end))));
			}
		}

		void Accesses(std::string_view body, std::string_view parameter, Vector<FieldAccess>& out) {
			out.clear();
			if (parameter.empty()) {
				return;
			}
			const str text = WithoutComments(body);
			const std::string_view view = text;
			auto touch = [&out](std::string_view field, bool read, bool written) {
				for (FieldAccess& held : out) {
					if (held.field == field) {
						held.read = held.read || read;
						held.written = held.written || written;
						return;
					}
				}
				out.push_back({str(field), read, written});
			};
			for (size_t at = view.find(parameter); at != std::string_view::npos; at = view.find(parameter, at + parameter.size())) {
				if (at > 0 && IsIdentifierCharacter(view[at - 1])) {
					continue;
				}
				const size_t dot = SkipSpace(view, at + parameter.size());
				if (dot >= view.size() || view[dot] != '.') {
					continue;
				}
				const size_t fieldAt = SkipSpace(view, dot + 1);
				const std::string_view field = IdentifierAt(view, fieldAt);
				if (field.empty()) {
					continue;
				}
				size_t before = at;
				while (before > 0 && IsSpace(view[before - 1])) {
					--before;
				}
				const bool prefix = before >= 2 && view[before - 1] == view[before - 2] && (view[before - 1] == '+' || view[before - 1] == '-');
				size_t after = SkipSpace(view, fieldAt + field.size());
				bool partial = false;
				while (after < view.size()) {
					if (view[after] == '.') {
						const size_t member = SkipSpace(view, after + 1);
						const std::string_view swizzle = IdentifierAt(view, member);
						if (swizzle.empty()) {
							break;
						}
						after = SkipSpace(view, member + swizzle.size());
						partial = true;
					} else if (view[after] == '[') {
						i32 depth = 1;
						size_t i = after + 1;
						for (; i < view.size() && depth > 0; ++i) {
							depth += view[i] == '[' ? 1 : (view[i] == ']' ? -1 : 0);
						}
						after = SkipSpace(view, i);
						partial = true;
					} else {
						break;
					}
				}
				auto next = [&view, after](std::string_view op) { return view.substr(after).starts_with(op); };
				bool read = true;
				bool written = false;
				if (prefix || next("++") || next("--") || next("<<=") || next(">>=")) {
					written = true;
				} else if (next("==") || next("<=") || next(">=") || next("!=")) {
					written = false;
				} else if (after + 1 < view.size() && std::string_view("+-*/%&|^").find(view[after]) != std::string_view::npos && view[after + 1] == '=') {
					written = true;
				} else if (next("=")) {
					written = true;
					read = partial;
				}
				touch(field, read, written);
			}
		}

		void Includes(std::string_view code, Vector<str>& out) {
			out.clear();
			constexpr std::string_view DIRECTIVE = "#include \"";
			size_t at = 0;
			while (at < code.size()) {
				size_t end = code.find('\n', at);
				end = end == std::string_view::npos ? code.size() : end + 1;
				const std::string_view line = code.substr(at, end - at);
				if (line.starts_with(DIRECTIVE)) {
					const size_t close = line.find('"', DIRECTIVE.size());
					if (close != std::string_view::npos) {
						const str path(line.substr(DIRECTIVE.size(), close - DIRECTIVE.size()));
						if (std::find(out.begin(), out.end(), path) == out.end()) {
							out.push_back(path);
						}
					}
				}
				at = end;
			}
		}

		str IncludeGuarded(std::string_view path) {
			const str guard = "SLT_EFFECT_INCLUDE_" + Hex(StringHash(path).GetValue());
			return "#ifndef " + guard + "\n#define " + guard + "\n#include \"" + str(path) + "\"\n#endif\n";
		}

		void Build(const GraphNode& node, std::string_view code, std::string_view file, GraphBuiltDefinition& out) {
			const bool block = IsBlock(node.def);
			GraphCustomHLSLCall& built = out.customHLSL;
			Vector<Function> functions{GetMemoryResource(MemTag::Editor)};
			Parse(code, functions);
			Vector<const Function*> offered{GetMemoryResource(MemTag::Editor)};
			for (const Function& function : functions) {
				const bool named = std::any_of(offered.begin(), offered.end(), [&function](const Function* held) { return held->name == function.name; });
				if (!function.hidden && !named) {
					offered.push_back(&function);
				}
				const str warning = "More than one function is named '" + function.name + "' (only the 1st can be picked)";
				if (!function.hidden && named && std::find(built.warnings.begin(), built.warnings.end(), warning) == built.warnings.end()) {
					built.warnings.push_back(warning);
				}
			}
			const str chosen = SettingText(node, GRAPH_CUSTOM_HLSL_FUNCTION);
			u32 selected = 0;
			for (u32 i = 0; i < offered.size(); ++i) {
				selected = offered[i]->name == chosen ? i : selected;
			}

			const GraphNodeDefinition& table = *GraphNodeLibrary::Find(node.def);
			out.settings.assign(table.settings.begin(), table.settings.end());
			Vector<std::string_view>& names = out.options.emplace_back();
			for (const Function* function : offered) {
				names.push_back(out.strings.emplace_back(function->name));
			}
			GraphSettingDefinition available;
			available.key = GRAPH_CUSTOM_HLSL_FUNCTION;
			available.kind = S::Choice;
			available.options = names;
			available.defaultIndex = selected;
			available.inGraph = true;
			available.optionsMove = true;
			out.settings.push_back(available);

			GraphNodeDefinition& definition = out.definition;
			definition = table;
			const std::string_view nameKey = block ? GRAPH_CUSTOM_HLSL_BLOCK_NAME : GRAPH_CUSTOM_HLSL_OPERATOR_NAME;
			const str title = SettingText(node, nameKey);
			definition.name = out.strings.emplace_back(title.empty() ? str("Custom HLSL") : title);
			definition.settings = out.settings;
			definition.customHLSL = &built;
			auto finish = [&]() {
				definition.inputs = out.inputs;
				definition.outputs = out.outputs;
			};
			if (offered.empty()) {
				built.error = block ? "declares no HLSL function a block can run (one returning void + taking an inout EffectAttributes)"
									: "declares no HLSL function an operator can run (one returning a value or writing an out parameter)";
				finish();
				return;
			}
			const Function& function = *offered[selected];
			const str called = "calls `" + function.name + "`. ";
			auto refuse = [&built](str message) {
				if (built.error.empty()) {
					built.error = std::move(message);
				}
			};

			const Parameter* attributes = nullptr;
			for (const Parameter& parameter : function.parameters) {
				if (parameter.name.empty()) {
					refuse(called + "It has a parameter it can't read");
					continue;
				}
				if (parameter.type == EFFECT_ATTRIBUTES) {
					if (!block) {
						refuse(called + "It takes EffectAttributes (an operator reads no attribute, a Custom HLSL block does)");
					} else if (attributes) {
						refuse(called + "It takes EffectAttributes twice");
					}
					attributes = attributes ? attributes : &parameter;
					continue;
				}
				const T type = ValueOf(parameter.type);
				if (type == T::None) {
					refuse(called + (IsKnown(parameter.type) ? "Its parameter `" + parameter.name + "` is a " + parameter.type +
																   " (a Custom HLSL node can't be passed one yet)"
															 : "Its parameter `" + parameter.name + "` has the unknown type '" + parameter.type + "'"));
					continue;
				}
				const bool output = parameter.access == Access::Out || parameter.access == Access::InOut;
				if (block && output) {
					refuse(called + "Its parameter `" + parameter.name + "` is out or inout (only a block's EffectAttributes may be)");
					continue;
				}
				GraphPortDefinition port;
				port.name = out.strings.emplace_back(parameter.name);
				port.type = PortOf(type);
				(output ? out.outputs : out.inputs).push_back(port);
			}
			if (block && function.returnType != "void") {
				refuse(called + "It must return void (a block's function writes the particle through its EffectAttributes)");
			}
			if (block && !attributes) {
				refuse(called + "It takes no EffectAttributes (a block's function reads/writes the particle through one)");
			}
			if (!block && function.returnType == "void" && out.outputs.empty()) {
				refuse(called + "It returns nothing + writes no out parameter (the operator has no output)");
			}
			if (!block && function.returnType != "void") {
				const T type = ValueOf(function.returnType);
				if (type == T::None) {
					refuse(called + "It returns a " + function.returnType + " (a Custom HLSL operator can't output one yet)");
				} else {
					GraphPortDefinition port;
					port.name = out.strings.emplace_back(function.returnName);
					port.type = PortOf(type);
					out.outputs.push_back(port);
				}
			}
			for (size_t i = 0; i < out.outputs.size(); ++i) {
				for (size_t j = 0; j < i; ++j) {
					if (out.outputs[i].name == out.outputs[j].name) {
						refuse(called + "It has 2 outputs named '" + str(out.outputs[i].name) + "'");
					}
				}
			}

			Vector<Field> fields{GetMemoryResource(MemTag::Editor)};
			Fields(fields);
			Vector<const GraphEffectAttributeInfo*> touched{GetMemoryResource(MemTag::Editor)};
			Vector<const GraphEffectAttributeInfo*> written{GetMemoryResource(MemTag::Editor)};
			if (block && attributes) {
				Vector<FieldAccess> accesses{GetMemoryResource(MemTag::Editor)};
				Accesses(function.body, attributes->name, accesses);
				for (const FieldAccess& access : accesses) {
					const Field* field = FindField(fields, access.field);
					const str named = "`" + attributes->name + "." + access.field + "`";
					if (!field) {
						refuse((IsVariadic(access.field) ? "uses " + named + " (custom HLSL reads it as " + access.field + "X, " + access.field + "Y and " +
															   access.field + "Z)"
														 : "uses " + named + " (not an attribute)"));
						continue;
					}
					if (access.written && field->attribute->readOnly) {
						refuse("writes " + named + " (read-only)");
						continue;
					}
					if (std::find(touched.begin(), touched.end(), field->attribute) == touched.end()) {
						touched.push_back(field->attribute);
					}
					if (access.written && std::find(written.begin(), written.end(), field->attribute) == written.end()) {
						written.push_back(field->attribute);
					}
				}
				if (!written.empty() && attributes->access != Access::InOut && attributes->access != Access::Out) {
					refuse(called + "It writes an attribute through an EffectAttributes that isn't inout");
				}
				RandomsOf(function.body, built.random, built.fixedRandom);
			}
			std::sort(touched.begin(), touched.end());
			std::sort(written.begin(), written.end());
			for (const GraphEffectAttributeInfo* attribute : written) {
				GraphPortDefinition port;
				port.name = attribute->name;
				port.type = PortOf(attribute->type);
				out.outputs.push_back(port);
			}
			for (const GraphEffectAttributeInfo* attribute : touched) {
				built.attributes.push_back(str(attribute->name));
			}
			finish();
			if (!built.error.empty()) {
				return;
			}

			const str identity = std::format("{}\n{}\n{}\n{}", block ? "Block" : "Operator", function.name, file, code);
			const u64 hash = StringHash(std::string_view(identity)).GetValue();
			const str wrapper = "SltCustomHLSL_" + Hex(hash);
			str text = Prelude(fields);
			str callee = function.name;
			if (!file.empty()) {
				text += "#include \"" + str(file) + "\"\n";
			} else {
				Vector<str> includes{GetMemoryResource(MemTag::Editor)};
				Includes(code, includes);
				for (const str& include : includes) {
					text += "#include \"" + include + "\"\n";
				}
				callee = function.name + "_" + Hex(hash);
				text += function.returnType + " " + callee + "(";
				for (size_t i = 0; i < function.parameters.size(); ++i) {
					text += (i == 0 ? "" : ", ") + Declared(function.parameters[i]);
				}
				text += ")\n" + WithoutIncludes(function.body) + "\n";
			}

			GraphCall& call = built.call;
			call.function = wrapper;
			str signature;
			auto parameter = [&signature](std::string_view type, std::string_view name) {
				signature += (signature.empty() ? "" : ", ") + str(type) + " " + str(name);
			};
			for (size_t i = 0; i < touched.size(); ++i) {
				parameter(GraphTypes::HLSLName(touched[i]->type), std::format("sltAttribute{}", i));
			}
			if (built.random) {
				parameter("float", "sltRandom");
			}
			if (built.fixedRandom) {
				parameter("float", "sltFixedRandom");
			}
			for (size_t i = 0; i < out.inputs.size(); ++i) {
				parameter(GraphTypes::HLSLName(PortValue(out.inputs[i].type)), std::format("sltPort{}", i));
			}
			for (size_t i = 0; i < out.outputs.size(); ++i) {
				const T type = PortValue(out.outputs[i].type);
				call.outputs.push_back(type);
				parameter("out " + str(GraphTypes::HLSLName(type)), std::format("sltOut{}", i));
			}
			text += "void " + wrapper + "(" + signature + ")\n{\n";
			if (block) {
				text += "\tEffectAttributes attributes = (EffectAttributes)0;\n";
				for (size_t i = 0; i < touched.size(); ++i) {
					for (const Field& field : fields) {
						if (field.attribute != touched[i]) {
							continue;
						}
						const str value = std::format("sltAttribute{}", i) + (field.component < 0 ? "" : "." + str(1, "xyz"[field.component]));
						text += "\tattributes." + field.name + " = " + (field.type == "uint" ? "(uint)" + value : value) + ";\n";
					}
				}
				if (built.random) {
					text += "\tattributes.sltRandom = asuint(sltRandom);\n";
				}
				if (built.fixedRandom) {
					text += "\tattributes.sltFixedRandom = asuint(sltFixedRandom);\n";
				}
				str arguments;
				u32 port = 0;
				for (const Parameter& parameter : function.parameters) {
					arguments += (arguments.empty() ? "" : ", ") + (&parameter == attributes ? str("attributes") : Cast(parameter.type, std::format("sltPort{}", port++)));
				}
				text += "\t" + callee + "(" + arguments + ");\n";
				for (size_t i = 0; i < written.size(); ++i) {
					str value;
					if (IsVariadic(written[i]->name)) {
						value = "float3(attributes." + str(written[i]->name) + "X, attributes." + str(written[i]->name) + "Y, attributes." + str(written[i]->name) + "Z)";
					} else {
						const Field* field = FindField(fields, written[i]->name);
						value = field->type == "uint" ? "(float)attributes." + field->name : "attributes." + field->name;
					}
					text += std::format("\tsltOut{} = {};\n", i, value);
				}
			} else {
				str arguments;
				u32 input = 0;
				u32 output = 0;
				str copies;
				for (size_t i = 0; i < function.parameters.size(); ++i) {
					const Parameter& parameter = function.parameters[i];
					const str local = std::format("sltArgument{}", i);
					const bool isOutput = parameter.access == Access::Out || parameter.access == Access::InOut;
					const str initial = isOutput ? "(" + parameter.type + ")0" : Cast(parameter.type, std::format("sltPort{}", input++));
					text += "\t" + parameter.type + " " + local + " = " + initial + ";\n";
					arguments += (arguments.empty() ? "" : ", ") + local;
					if (isOutput) {
						const T type = PortValue(out.outputs[output].type);
						copies += std::format("\tsltOut{} = ({}){};\n", output, GraphTypes::HLSLName(type), local);
						++output;
					}
				}
				if (function.returnType != "void") {
					const T type = PortValue(out.outputs[output].type);
					text += "\t" + function.returnType + " sltReturn = " + callee + "(" + arguments + ");\n";
					copies += std::format("\tsltOut{} = ({})sltReturn;\n", output, GraphTypes::HLSLName(type));
				} else {
					text += "\t" + callee + "(" + arguments + ");\n";
				}
				text += copies;
			}
			text += "}\n\n";
			call.definition = std::move(text);
		}

	} // namespace GraphCustomHLSL

} // namespace Shalltear
