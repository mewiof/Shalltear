#pragma once

#include "NodeShaderAPI.h"

#include "GraphCompiler.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear {

	struct GraphDocument;
	class GraphDefinitions;

	namespace GraphPreview {

		inline constexpr u32 TILE_SIZE = 128;
		inline constexpr u32 ATLAS_COLUMNS = 8;

		struct Push {
			f32 time[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		};

		struct Tile {
			u32 node = 0;
			bool threeD = false;
			bool timed = false;
		};

		struct Program {
			str text;
			Vector<Tile> tiles{GetMemoryResource(MemTag::Editor)};
			u32 rows = 0;
			Vector<GraphDiagnostic> diagnostics{GetMemoryResource(MemTag::Editor)};
			Vector<u32> lineNodes{GetMemoryResource(MemTag::Editor)};
		};

		SHALLTEAR_NODESHADER_API bool Generate(const GraphDocument& document, std::string_view sourceName, Program& out,
											   GraphDefinitions* definitions = nullptr, const Guid& asset = {});

		SHALLTEAR_NODESHADER_API std::span<const GraphBinding> Bindings();

		SHALLTEAR_NODESHADER_API void Previewable(const GraphDocument& document, Vector<Tile>& out, GraphDefinitions* definitions = nullptr);

	} // namespace GraphPreview

} // namespace Shalltear
