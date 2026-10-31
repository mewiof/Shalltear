#pragma once

#include "GraphsAPI.h"

#include "GraphCompiler.h"
#include "GraphProgram.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	struct GraphHLSLHelpers {
		u32 hashes = 0;
		u32 noises = 0;
		bool textureSize = false;
		bool random = false;
		bool parallaxOcclusion = false;
		u32 effectBases = 0;
		u32 effectFractals = 0;
		u32 effectCurls = 0;
		Vector<str> includes{GetMemoryResource(MemTag::Editor)};
		Vector<str> definitions{GetMemoryResource(MemTag::Editor)};
	};

	struct GraphHLSLText {
		str functions;
		GraphHLSLHelpers helpers;
		Vector<u32> functionLineNodes{GetMemoryResource(MemTag::Editor)};
		str body;
		Vector<u32> lineNodes{GetMemoryResource(MemTag::Editor)};
		Vector<str> roots{GetMemoryResource(MemTag::Editor)};
	};

	namespace GraphHLSL {

		SHALLTEAR_GRAPHS_API void Emit(const GraphProgram& program, GraphStageMask stage, GraphHLSLText& out,
									   const GraphHLSLHelpers* alongside = nullptr);

		SHALLTEAR_GRAPHS_API str FloatLiteral(f32 value);

		SHALLTEAR_GRAPHS_API u32 MapCompilerMessages(std::string_view messages, std::string_view path, std::string_view text,
													 std::span<const u32> lines, Vector<GraphDiagnostic>& out);

	} // namespace GraphHLSL

} // namespace Shalltear
