#pragma once

#include "NodeShaderAPI.h"

#include "GraphCompiler.h"
#include "GraphHLSL.h"
#include "GraphProperties.h"
#include "GraphShading.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear {

	struct GraphDocument;
	class GraphDefinitions;

	namespace GraphSurface {

		inline constexpr std::string_view DOCUMENT_TYPE = GRAPH_SHADER_DOCUMENT_TYPE;

		SHALLTEAR_NODESHADER_API bool Generate(const GraphDocument& document, std::string_view sourceName, str& out,
											   Vector<GraphDiagnostic>& diagnostics, GraphDefinitions* definitions = nullptr,
											   Vector<u32>* lines = nullptr);

		SHALLTEAR_NODESHADER_API bool GenerateFunctions(const GraphDocument& document, GraphSurfaceFunctions& out, Vector<GraphDiagnostic>& diagnostics,
														GraphDefinitions* definitions = nullptr);

		SHALLTEAR_NODESHADER_API bool SupportsEffects(const GraphDocument& document);

		SHALLTEAR_NODESHADER_API bool CanShadeEffects(const GraphDocument& document, std::string_view graph, str& why);

		SHALLTEAR_NODESHADER_API bool GenerateSubgraphPreview(const GraphDocument& document, std::string_view sourceName, str& out,
															  Vector<GraphDiagnostic>& diagnostics, GraphDefinitions* definitions = nullptr,
															  const Guid& asset = {}, Vector<u32>* lines = nullptr);

		SHALLTEAR_NODESHADER_API bool IsMaterialShaderSource(std::string_view text);

		SHALLTEAR_NODESHADER_API bool IsFullscreen(const GraphDocument& document);

		SHALLTEAR_NODESHADER_API bool IsSky(const GraphDocument& document);

		SHALLTEAR_NODESHADER_API bool HasVertexStage(const GraphDocument& document);

		SHALLTEAR_NODESHADER_API bool ReadsClock(std::string_view generated);

		SHALLTEAR_NODESHADER_API str PassDeclarations(const GraphProgram& program);

		SHALLTEAR_NODESHADER_API std::span<const GraphBinding> Bindings();

		SHALLTEAR_NODESHADER_API bool IsBlockActive(const GraphDocument& document, std::string_view def);

		SHALLTEAR_NODESHADER_API std::span<const std::string_view> FragmentBlocks();
		SHALLTEAR_NODESHADER_API std::span<const std::string_view> VertexBlocks();
		inline constexpr std::string_view INTERPOLATOR_BLOCK = "Block/CustomInterpolator";
		inline constexpr std::string_view RADIANCE_BLOCK_ID = "Block/Radiance";
		SHALLTEAR_NODESHADER_API std::span<const GraphBinding> LightingBindings();

		SHALLTEAR_NODESHADER_API std::span<const GraphBinding> FullscreenBindings();

		SHALLTEAR_NODESHADER_API std::span<const GraphBinding> SkyBindings();

	} // namespace GraphSurface

} // namespace Shalltear
