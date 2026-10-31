#pragma once

#include "GraphsAPI.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/GraphDocument.h"

#include <span>
#include <string_view>

namespace Shalltear {

	struct GraphNodeDefinition;
	class GraphDefinitions;

	namespace EffectGraphEditOps {

		SHALLTEAR_GRAPHS_API std::span<const std::string_view> Contexts();
		SHALLTEAR_GRAPHS_API u32 AddContext(GraphDocument& document, std::string_view def, Vec2 position);
		SHALLTEAR_GRAPHS_API std::span<const std::string_view> FlowInputs(const GraphDocument& document, u32 context,
																		  GraphDefinitions* definitions = nullptr);
		SHALLTEAR_GRAPHS_API bool FlowOutput(const GraphDocument& document, u32 context);

		SHALLTEAR_GRAPHS_API bool CanLink(const GraphDocument& document, u32 from, u32 to, std::string_view slot, str* why = nullptr,
										  GraphDefinitions* definitions = nullptr);
		SHALLTEAR_GRAPHS_API bool Link(GraphDocument& document, u32 from, u32 to, std::string_view slot, GraphDefinitions* definitions = nullptr);

		SHALLTEAR_GRAPHS_API bool CanUseSystemSubgraph(const GraphDocument& document, const Guid& asset, GraphDefinitions& definitions,
													   const Guid& editing = {});
		SHALLTEAR_GRAPHS_API u32 AddSystemSubgraph(GraphDocument& document, const Guid& asset, Vec2 position, GraphDefinitions& definitions,
												   const Guid& editing = {});

		struct BlockChoice {
			str name;
			str category;
			str synonyms;
			str def;
			Vector<GraphSetting> settings{GetMemoryResource(MemTag::Editor)};
		};

		SHALLTEAR_GRAPHS_API void ContextChoices(Vector<BlockChoice>& out);
		SHALLTEAR_GRAPHS_API u32 AddContext(GraphDocument& document, const BlockChoice& choice, Vec2 position);

		SHALLTEAR_GRAPHS_API void BlockChoices(const GraphDocument& document, u32 context, Vector<BlockChoice>& out);
		SHALLTEAR_GRAPHS_API u32 AddBlock(GraphDocument& document, u32 context, u32 index, const BlockChoice& choice,
										  GraphDefinitions* definitions = nullptr);
		SHALLTEAR_GRAPHS_API bool CanMoveBlocks(const GraphDocument& document, std::span<const u32> blocks, u32 context,
												GraphDefinitions* definitions = nullptr);
		SHALLTEAR_GRAPHS_API bool MoveBlocks(GraphDocument& document, std::span<const u32> blocks, u32 context, u32 index,
											 GraphDefinitions* definitions = nullptr);

		SHALLTEAR_GRAPHS_API bool IsOperator(const GraphNodeDefinition& definition);

		SHALLTEAR_GRAPHS_API void OperatorChoices(Vector<BlockChoice>& out);
		SHALLTEAR_GRAPHS_API bool IsOfferedAsVariants(std::string_view definition);
		SHALLTEAR_GRAPHS_API u32 AddOperator(GraphDocument& document, const BlockChoice& choice, Vec2 graph);

		SHALLTEAR_GRAPHS_API void SubgraphChoices(const GraphDocument& document, const Guid& editing, u32 context, GraphDefinitions& definitions,
												  Vector<BlockChoice>& out);
		SHALLTEAR_GRAPHS_API str OperatorSubgraphTemplate();
		SHALLTEAR_GRAPHS_API str BlockSubgraphTemplate();

		SHALLTEAR_GRAPHS_API std::span<const std::string_view> PropertyTypes();
		SHALLTEAR_GRAPHS_API u32 AddOutputProperty(GraphDocument& document, std::string_view type);

		SHALLTEAR_GRAPHS_API u32 Delete(GraphDocument& document, std::span<const u32> ids);
		SHALLTEAR_GRAPHS_API str Copy(const GraphDocument& document, std::span<const u32> ids);
		SHALLTEAR_GRAPHS_API bool IsBlockFragment(std::string_view fragment);
		SHALLTEAR_GRAPHS_API bool PasteBlocks(GraphDocument& document, std::string_view fragment, std::span<const u32> selection,
											  Vector<u32>& pasted, str& error, GraphDefinitions* definitions = nullptr);

		SHALLTEAR_GRAPHS_API bool SetUpdateMode(GraphDocument& document, std::string_view key, bool on);
		enum class PrewarmField : u8 {
			TotalTime,
			StepCount,
			DeltaTime
		};
		SHALLTEAR_GRAPHS_API bool SetPrewarm(GraphDocument& document, PrewarmField field, f64 value);
		SHALLTEAR_GRAPHS_API bool SetCullingFlags(GraphDocument& document, u32 flags);
		SHALLTEAR_GRAPHS_API bool SetInstancingMode(GraphDocument& document, i32 mode);
		SHALLTEAR_GRAPHS_API bool SetInstancingCapacity(GraphDocument& document, u32 capacity);

		SHALLTEAR_GRAPHS_API bool IgnoreError(GraphDocument& document, u32 node, std::string_view key);
		SHALLTEAR_GRAPHS_API bool ClearIgnoredErrors(GraphDocument& document, u32 node);

		enum class BoundsExclusion : u8 {
			None,
			Automatic,
			Manual,
			GraphComputed
		};
		SHALLTEAR_GRAPHS_API BoundsExclusion BoundsExclusionOf(const GraphDocument& document, u32 initialize);
		SHALLTEAR_GRAPHS_API std::string_view BoundsExclusionLabel(BoundsExclusion exclusion);
		SHALLTEAR_GRAPHS_API str BoundsExclusionTooltip(BoundsExclusion exclusion);
		SHALLTEAR_GRAPHS_API bool ApplyRecordedBounds(GraphDocument& document, u32 initialize, const AABB& box, bool worldSystem,
													  const Mat4& localToWorld);

		SHALLTEAR_GRAPHS_API u32 TemplateCount();
		SHALLTEAR_GRAPHS_API std::string_view TemplateLabel(u32 index);
		SHALLTEAR_GRAPHS_API str TemplateDocument(u32 index);

	} // namespace EffectGraphEditOps

} // namespace Shalltear
