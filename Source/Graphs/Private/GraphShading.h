#pragma once

#include "GraphsAPI.h"

#include "GraphCompiler.h"
#include "GraphHLSL.h"
#include "GraphProperties.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/RHITypes.h"
#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear {

	struct GraphDocument;
	class GraphDefinitions;

	inline constexpr std::string_view GRAPH_SHADER_DOCUMENT_TYPE = "NodeShader";

	struct GraphSurfaceFunctions {
		enum class Target : u8 {
			Lit,
			Unlit,
			CustomLighting
		};
		Target target = Target::Lit;
		RHI::BlendMode blend = RHI::BlendMode::Opaque;
		RHI::CullMode cull = RHI::CullMode::Back;
		RHI::DepthMode depth = RHI::DepthMode::TestWrite;
		bool supportsEffects = false;
		Vector<GraphPropertyInfo> properties{GetMemoryResource(MemTag::Editor)};
		Vector<GraphStageMask> propertyStages{GetMemoryResource(MemTag::Editor)};
		bool vertexFunction = false;
		u32 interpolators[3] = {0, 0, 0};
		bool fragmentReadsObject = false;
		str text;
		Vector<u32> lines{GetMemoryResource(MemTag::Editor)};
		GraphHLSLHelpers helpers;
	};

	struct GraphShadingTarget {
		bool (*canShadeEffects)(const GraphDocument& document, std::string_view graph, str& why) = nullptr;
		bool (*generateFunctions)(const GraphDocument& document, GraphSurfaceFunctions& out, Vector<GraphDiagnostic>& diagnostics,
								  GraphDefinitions* definitions) = nullptr;
	};

	namespace GraphShadingTargets {

		// documentType must outlive the registration
		SHALLTEAR_GRAPHS_API bool Register(std::string_view documentType, const GraphShadingTarget& target, const void* owner);

		SHALLTEAR_GRAPHS_API u32 UnregisterOwnedBy(const void* owner);

		SHALLTEAR_GRAPHS_API const GraphShadingTarget* Find(std::string_view documentType);

		SHALLTEAR_GRAPHS_API u64 Fingerprint();

	} // namespace GraphShadingTargets

} // namespace Shalltear
