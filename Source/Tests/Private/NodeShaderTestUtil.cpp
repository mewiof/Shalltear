#include "TestsPCH.h"

#include "NodeShaderTestUtil.h"

#include "GraphSurface.h"

#include <vector>

namespace Shalltear::NodeShaderTest {

	Compiled Compile(const GraphDocument& document, std::initializer_list<std::pair<u32, std::string_view>> roots, GraphStageMask stage,
					 std::span<const GraphBinding> bindings) {
		Vector<GraphCompileRoot> requested{GetMemoryResource(MemTag::Editor)};
		for (const auto& [node, port] : roots) {
			GraphCompileRoot root;
			root.node = node;
			root.port.assign(port);
			root.stage = stage;
			requested.push_back(std::move(root));
		}
		GraphCompileRequest request;
		request.roots = requested;
		request.bindings = bindings;
		Compiled out;
		out.ok = GraphCompiler::Compile(document, request, out.program, out.diagnostics);
		if (out.ok) {
			GraphHLSL::Emit(out.program, stage, out.text);
		}
		return out;
	}

	std::span<const GraphBinding> TestBindings() {
		static const std::vector<GraphBinding> BINDINGS = [] {
			std::vector<GraphBinding> out;
			std::vector<GraphBinding> every(GraphSurface::Bindings().begin(), GraphSurface::Bindings().end());
			every.insert(every.end(), GraphSurface::LightingBindings().begin(), GraphSurface::LightingBindings().end());
			for (const GraphBinding& binding : every) {
				GraphBinding test = binding;
				test.expression = "t" + binding.name;
				test.stages = binding.stages == GraphStageVertex ? GraphStageVertex : GraphStageAll;
				out.push_back(std::move(test));
			}
			return out;
		}();
		return {BINDINGS.data(), BINDINGS.size()};
	}

	std::string_view TestBindingExpression(std::string_view name) {
		for (const GraphBinding& binding : TestBindings()) {
			if (binding.name == name) {
				return binding.expression;
			}
		}
		return {};
	}

} // namespace Shalltear::NodeShaderTest
