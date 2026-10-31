#include "PreviewRig.h"

#include "Shalltear/Editor/EditorPreviews.h"

#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Scene/SceneSystem.h"

#include "Shalltear/Editor/EditorViewportOps.h"

namespace Shalltear {

	namespace EditorPreviewsInternal {

		bool ProducePrefabPreview(const AssetPreviewRequest& request, AssetPreviewImage& out) {
			Scene* stage = request.stage.Get();
			if (!stage || !request.guid.IsValid()) {
				return false;
			}
			SLT_CORE_ASSERT_MSG(stage->GetKind() == SceneKind::Isolated,
								"a preview producer was handed a WORLD scene to populate");

			Handle<SceneObject> instance = SceneSystem::Instantiate(request.guid, request.stage);
			if (!instance.IsValid()) {
				return false;
			}

			AABB bounds = EditorViewportOps::GetGraphWorldBounds(instance);

			AddPreviewLights(request.stage);

			return RenderPreview(request, bounds, out);
		}

	} // namespace EditorPreviewsInternal

} // namespace Shalltear
