#pragma once

#include "GraphsAPI.h"

#include "GraphProgram.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear {

	struct GraphDocument;
	class GraphDefinitions;

	struct GraphCompileRoot {
		u32 node = 0;
		str port;
		GraphStageMask stage = GraphStageFragment;
		bool input = false;
		std::span<const u32> within;
	};

	struct GraphBinding {
		str name;
		str expression;
		GraphValueType type = GraphValueType::None;
		GraphStageMask stages = GraphStageAll;
		str unavailable;
		str sampler;

		SHALLTEAR_GRAPHS_API GraphValueId Read(GraphProgramBuilder& builder) const;
	};

	enum class GraphSpace : u8 {
		None = 0,
		Local,
		World
	};

	struct GraphCompileRequest {
		std::span<const GraphCompileRoot> roots;
		std::span<const GraphBinding> bindings;
		GraphDefinitions* definitions = nullptr;
		Guid asset{};
		GraphSpace space = GraphSpace::None;
	};

	enum class GraphDiagnosticSeverity : u8 {
		Error,
		Warning
	};

	struct GraphDiagnostic {
		u32 node = 0;
		str message;
		GraphDiagnosticSeverity severity = GraphDiagnosticSeverity::Error;
		str key;
	};

	inline u32 CountDiagnostics(std::span<const GraphDiagnostic> diagnostics, GraphDiagnosticSeverity severity) {
		u32 count = 0;
		for (const GraphDiagnostic& diagnostic : diagnostics) {
			count += diagnostic.severity == severity ? 1u : 0u;
		}
		return count;
	}
	inline bool HasErrorSince(std::span<const GraphDiagnostic> diagnostics, size_t first = 0) {
		return first < diagnostics.size() && CountDiagnostics(diagnostics.subspan(first), GraphDiagnosticSeverity::Error) > 0;
	}

	inline constexpr std::string_view GRAPH_IGNORED_ERRORS_SETTING = "Ignored Errors";

	namespace GraphCompiler {

		SHALLTEAR_GRAPHS_API bool Compile(const GraphDocument& document, const GraphCompileRequest& request, GraphProgram& out,
										  Vector<GraphDiagnostic>& diagnostics);

		SHALLTEAR_GRAPHS_API str PortTextureBinding(u32 node, std::string_view port, std::span<const u32> within = {});
		SHALLTEAR_GRAPHS_API str PortMeshBinding(u32 node, std::string_view port, std::span<const u32> within = {});

		SHALLTEAR_GRAPHS_API u32 Identity(u32 node, std::span<const u32> within);

	} // namespace GraphCompiler

	namespace GraphPasses {

		SHALLTEAR_GRAPHS_API bool AssignStages(GraphProgram& program, Vector<GraphDiagnostic>& diagnostics);
		SHALLTEAR_GRAPHS_API u32 Fold(GraphProgram& program);
		SHALLTEAR_GRAPHS_API u32 Deduplicate(GraphProgram& program);
		SHALLTEAR_GRAPHS_API u32 Eliminate(GraphProgram& program);

	} // namespace GraphPasses

} // namespace Shalltear
