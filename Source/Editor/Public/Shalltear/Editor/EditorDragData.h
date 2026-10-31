#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	struct AssetDragData {
		Guid guid{};
		u64 assetTypeHash = 0;

		SLT_REFLECT_VALUE(AssetDragData)
		SLT_REFLECT_FIELD(guid)
		SLT_REFLECT_FIELD(assetTypeHash)
		SLT_END_REFLECT()
	};

	struct SceneObjectDragData {
		Vector<u64> objects{GetMemoryResource(MemTag::Editor)};

		SLT_REFLECT_VALUE(SceneObjectDragData)
		SLT_REFLECT_ARRAY(objects)
		SLT_END_REFLECT()
	};

	struct SceneFolderDragData {
		u64 sceneId = 0;
		u64 folderId = 0;

		SLT_REFLECT_VALUE(SceneFolderDragData)
		SLT_REFLECT_FIELD(sceneId)
		SLT_REFLECT_FIELD(folderId)
		SLT_END_REFLECT()
	};

	struct UIBuilderNodeDragData {
		u64 panelId = 0;
		Vector<u32> nodes{GetMemoryResource(MemTag::Editor)};

		SLT_REFLECT_VALUE(UIBuilderNodeDragData)
		SLT_REFLECT_FIELD(panelId)
		SLT_REFLECT_ARRAY(nodes)
		SLT_END_REFLECT()
	};

	struct GraphBlackboardDragData {
		u64 board = 0;
		u64 item = 0;

		SLT_REFLECT_VALUE(GraphBlackboardDragData)
		SLT_REFLECT_FIELD(board)
		SLT_REFLECT_FIELD(item)
		SLT_END_REFLECT()
	};

} // namespace Shalltear
