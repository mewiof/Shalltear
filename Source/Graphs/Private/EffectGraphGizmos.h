#pragma once

#include "GraphsAPI.h"

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorGizmos.h"

namespace Shalltear {

	struct GraphDocument;
	class GraphDefinitions;

	namespace EffectGraphGizmoOps {

		struct BlockGizmo {
			Vector<DebugLine> lines{GetMemoryResource(MemTag::Editor)};
			Vector<SceneHandle> handles{GetMemoryResource(MemTag::Editor)};
		};

		SHALLTEAR_GRAPHS_API bool Build(const GraphDocument& document, u32 block, GraphDefinitions& definitions, const Mat4& effectToWorld,
										const Quat& effectRotation, Color32 wire, Color32 dot, std::string_view label, BlockGizmo& out);

		SHALLTEAR_GRAPHS_API bool Write(GraphDocument& document, u32 block, GraphDefinitions& definitions, const Mat4& effectToWorld,
										const SceneHandle& dragged);

	} // namespace EffectGraphGizmoOps

} // namespace Shalltear
