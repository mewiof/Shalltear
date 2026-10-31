#pragma once

#include "NodeShaderAPI.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear {

	struct GraphDocument;

	namespace NodeShaderStackOps {

		SHALLTEAR_NODESHADER_API u32 FragmentContext(const GraphDocument& document);
		SHALLTEAR_NODESHADER_API u32 VertexContext(const GraphDocument& document);
		SHALLTEAR_NODESHADER_API bool EnsureVertexStack(GraphDocument& document);
		SHALLTEAR_NODESHADER_API u32 LightingContext(const GraphDocument& document);
		SHALLTEAR_NODESHADER_API bool EnsureLightingStack(GraphDocument& document);
		SHALLTEAR_NODESHADER_API bool IsBlockActive(const GraphDocument& document, std::string_view def);
		SHALLTEAR_NODESHADER_API void MissingBlocks(const GraphDocument& document, u32 context, Vector<std::string_view>& out);
		SHALLTEAR_NODESHADER_API u32 AddBlock(GraphDocument& document, u32 context, std::string_view def);
		SHALLTEAR_NODESHADER_API bool RemoveBlock(GraphDocument& document, u32 block);

		inline constexpr std::string_view INTERPOLATOR_NODE = "CustomInterpolators/Instance";

		SHALLTEAR_NODESHADER_API void Interpolators(const GraphDocument& document, Vector<u32>& out);
		SHALLTEAR_NODESHADER_API u32 InterpolatorOf(const GraphDocument& document, u32 node);
		SHALLTEAR_NODESHADER_API u32 AddInterpolatorNode(GraphDocument& document, u32 block, Vec2 position);
		SHALLTEAR_NODESHADER_API bool RenameInterpolator(GraphDocument& document, u32 block, std::string_view name);
		SHALLTEAR_NODESHADER_API bool SetInterpolatorType(GraphDocument& document, u32 block, std::string_view type);
		SHALLTEAR_NODESHADER_API bool SetInterpolatorMode(GraphDocument& document, u32 block, std::string_view mode);

		SHALLTEAR_NODESHADER_API void UpgradeOnOpen(GraphDocument& document);

	} // namespace NodeShaderStackOps

} // namespace Shalltear
