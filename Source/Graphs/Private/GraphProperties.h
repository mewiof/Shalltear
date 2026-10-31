#pragma once

#include "GraphsAPI.h"

#include "GraphCompiler.h"
#include "GraphProgram.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear {

	struct GraphDocument;
	struct GraphProperty;

	enum class GraphPropertyKind : u8 {
		Float,
		Integer,
		Boolean,
		Vector2,
		Vector3,
		Vector4,
		Color,
		Texture2D,
		BooleanKeyword,
		Texture2DArray,
		Texture3D,
		Cubemap,
		EnumKeyword
	};

	struct GraphKeywordEntry {
		u32 id = 0;
		str name;
		str reference;
	};

	inline constexpr u32 GRAPH_KEYWORD_MIN_ENTRIES = 2;
	inline constexpr u32 GRAPH_KEYWORD_MAX_ENTRIES = 8;

	struct GraphPropertyInfo {
		u32 id = 0;
		str name;
		str reference;
		GraphPropertyKind kind = GraphPropertyKind::Float;
		f32 defaultValue[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		bool slider = false;
		f32 minValue = 0.0f;
		f32 maxValue = 1.0f;
		bool hdr = false;
		bool hidden = false;
		str group;
		str fallback = str("white");
		bool customBinding = false;
		str bindingLabel;
		Vector<GraphKeywordEntry> entries{GetMemoryResource(MemTag::Editor)};
	};

	namespace GraphProperties {

		SHALLTEAR_GRAPHS_API bool Parse(const GraphProperty& property, GraphPropertyInfo& out, str& error);

		SHALLTEAR_GRAPHS_API GraphValueType ValueType(GraphPropertyKind kind);
		SHALLTEAR_GRAPHS_API bool IsTexture(GraphPropertyKind kind);
		SHALLTEAR_GRAPHS_API bool IsKeyword(GraphPropertyKind kind);
		SHALLTEAR_GRAPHS_API str MemberName(const GraphPropertyInfo& info);

		SHALLTEAR_GRAPHS_API str EntryKeyword(const GraphPropertyInfo& info, size_t entry);
		SHALLTEAR_GRAPHS_API str DefaultKeyword(const GraphPropertyInfo& info);

		SHALLTEAR_GRAPHS_API bool ReadEntries(const GraphProperty& property, Vector<GraphKeywordEntry>& out, str& error);
		SHALLTEAR_GRAPHS_API str WriteEntries(std::span<const GraphKeywordEntry> entries);

		SHALLTEAR_GRAPHS_API std::string_view MemberType(GraphPropertyKind kind);

		SHALLTEAR_GRAPHS_API str ReadExpression(const GraphPropertyInfo& info);

		// materials pair the i-th sampler with the i-th texture
		SHALLTEAR_GRAPHS_API str SamplerName(const GraphPropertyInfo& info);

		SHALLTEAR_GRAPHS_API bool ParseBlackboard(const GraphDocument& document, Vector<GraphPropertyInfo>& out,
												  Vector<GraphDiagnostic>& diagnostics);
		SHALLTEAR_GRAPHS_API str Clash(const GraphPropertyInfo& a, const GraphPropertyInfo& b);

		SHALLTEAR_GRAPHS_API str Declarations(std::span<const GraphPropertyInfo> properties);
		SHALLTEAR_GRAPHS_API str HostDeclarations(std::span<const GraphPropertyInfo> properties, u32 firstTexture);

	} // namespace GraphProperties

} // namespace Shalltear
