#pragma once

#include "GraphsAPI.h"

#include "GraphNodeLibrary.h"
#include "GraphProgram.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear {

	struct GraphNode;
	struct GraphBuiltDefinition;

	inline constexpr std::string_view GRAPH_EFFECT_CUSTOM_HLSL_BLOCK = "Block/CustomHLSL";
	inline constexpr std::string_view GRAPH_EFFECT_CUSTOM_HLSL_OPERATOR = "HLSL/CustomHLSL";

	inline constexpr std::string_view GRAPH_CUSTOM_HLSL_BLOCK_NAME = "Block Name";
	inline constexpr std::string_view GRAPH_CUSTOM_HLSL_OPERATOR_NAME = "Operator Name";
	inline constexpr std::string_view GRAPH_CUSTOM_HLSL_CODE = "HLSL Code";
	inline constexpr std::string_view GRAPH_CUSTOM_HLSL_FILE = "Shader File";
	inline constexpr std::string_view GRAPH_CUSTOM_HLSL_FUNCTION = "Available Function";

	inline constexpr std::string_view GRAPH_CUSTOM_HLSL_EXTENSIONS[] = {".hlsl"};

	struct GraphCustomHLSLCall {
		GraphCall call;
		Vector<str> attributes{GetMemoryResource(MemTag::Editor)};
		bool random = false;
		bool fixedRandom = false;
		str error;
		Vector<str> warnings{GetMemoryResource(MemTag::Editor)};
	};

	namespace GraphCustomHLSL {

		inline constexpr std::string_view DEFAULT_BLOCK_CODE = "void CustomHLSL(inout EffectAttributes attributes, in float3 offset, in float speedFactor)\n"
															   "{\n"
															   "  attributes.position += offset;\n"
															   "  attributes.velocity *= speedFactor;\n"
															   "}";
		inline constexpr std::string_view DEFAULT_OPERATOR_CODE = "float FloatFunction(in float value)\n"
																  "{\n"
																  "  return value;\n"
																  "}";

		inline constexpr GraphPortDefinition BLOCK_INPUTS[] = {{"offset", GraphPortType::Float3}, {"speedFactor", GraphPortType::Float}};
		inline constexpr GraphPortDefinition BLOCK_OUTPUTS[] = {{"position", GraphPortType::Float3}, {"velocity", GraphPortType::Float3}};
		inline constexpr GraphPortDefinition OPERATOR_INPUTS[] = {{"value", GraphPortType::Float}};
		inline constexpr GraphPortDefinition OPERATOR_OUTPUTS[] = {{"out", GraphPortType::Float}};

		inline constexpr GraphSettingDefinition BLOCK_SETTINGS[] = {
			{.key = GRAPH_CUSTOM_HLSL_BLOCK_NAME, .kind = GraphSettingKind::Text, .defaultText = "Custom HLSL"},
			{.key = GRAPH_CUSTOM_HLSL_CODE, .kind = GraphSettingKind::Text, .defaultText = DEFAULT_BLOCK_CODE, .multiline = true},
			{.key = GRAPH_CUSTOM_HLSL_FILE, .kind = GraphSettingKind::Asset, .options = GRAPH_CUSTOM_HLSL_EXTENSIONS, .defaultText = "None"},
		};
		inline constexpr GraphSettingDefinition OPERATOR_SETTINGS[] = {
			{.key = GRAPH_CUSTOM_HLSL_OPERATOR_NAME, .kind = GraphSettingKind::Text, .defaultText = "Custom HLSL"},
			{.key = GRAPH_CUSTOM_HLSL_CODE, .kind = GraphSettingKind::Text, .defaultText = DEFAULT_OPERATOR_CODE, .multiline = true},
			{.key = GRAPH_CUSTOM_HLSL_FILE, .kind = GraphSettingKind::Asset, .options = GRAPH_CUSTOM_HLSL_EXTENSIONS, .defaultText = "None"},
		};

		void BlockBody(GraphBodyContext& c);
		void OperatorBody(GraphBodyContext& c);

		enum class Access : u8 {
			None,
			In,
			Out,
			InOut
		};

		struct Parameter {
			Access access = Access::None;
			str type;
			str templateType;
			str name;
		};

		struct Function {
			str name;
			str returnType;
			str returnName;
			bool hidden = false;
			Vector<Parameter> parameters{GetMemoryResource(MemTag::Editor)};
			str body;
		};

		SHALLTEAR_GRAPHS_API void Parse(std::string_view code, Vector<Function>& out);

		struct FieldAccess {
			str field;
			bool read = false;
			bool written = false;
		};
		SHALLTEAR_GRAPHS_API void Accesses(std::string_view body, std::string_view parameter, Vector<FieldAccess>& out);

		SHALLTEAR_GRAPHS_API void Includes(std::string_view code, Vector<str>& out);

		void Build(const GraphNode& node, std::string_view code, std::string_view file, GraphBuiltDefinition& out);

		template <class Resolve>
		bool ResolveIncludes(str& definition, Resolve&& resolve, str& missing);

		SHALLTEAR_GRAPHS_API str IncludeGuarded(std::string_view path);

	} // namespace GraphCustomHLSL

	template <class Resolve>
	bool GraphCustomHLSL::ResolveIncludes(str& definition, Resolve&& resolve, str& missing) {
		constexpr std::string_view DIRECTIVE = "#include \"";
		str out;
		size_t at = 0;
		while (at < definition.size()) {
			size_t end = definition.find('\n', at);
			end = end == str::npos ? definition.size() : end + 1;
			const std::string_view line(definition.data() + at, end - at);
			if (line.starts_with(DIRECTIVE)) {
				const size_t close = line.find('"', DIRECTIVE.size());
				const std::string_view path = line.substr(DIRECTIVE.size(), close == std::string_view::npos ? 0 : close - DIRECTIVE.size());
				const str resolved = resolve(path);
				if (resolved.empty()) {
					missing = str(path);
					return false;
				}
				out += IncludeGuarded(resolved);
			} else {
				out += line;
			}
			at = end;
		}
		definition = std::move(out);
		return true;
	}

} // namespace Shalltear
