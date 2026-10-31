#include "PreviewRig.h"

#include "Shalltear/Editor/EditorPreviews.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Scene/SceneSystem.h"

namespace Shalltear {

	namespace EditorPreviewsInternal {

		bool ProduceMeshPreview(const AssetPreviewRequest& request, AssetPreviewImage& out) {
			Scene* stage = request.stage.Get();
			if (!stage || !request.asset) {
				return false;
			}
			SLT_CORE_ASSERT_MSG(stage->GetKind() == SceneKind::Isolated,
								"a preview producer was handed a WORLD scene to populate");
			MeshAsset* mesh = static_cast<MeshAsset*>(request.asset);

			Handle<SceneObject> subject = SceneSystem::CreateObject("Subject", request.stage);
			if (!subject.IsValid()) {
				return false;
			}
			Handle<MeshRenderer> renderer = subject->AddComponent<MeshRenderer>();
			if (!renderer.IsValid()) {
				return false;
			}
			renderer->SetMesh(Handle<MeshAsset>(mesh));

			if (Handle<MaterialAsset> material = SharedDefaultMaterial(); material.IsValid()) {
				u32 slotCount = 1;
				for (const SubMesh& submesh : mesh->submeshes) {
					if (submesh.materialSlot + 1 > slotCount) {
						slotCount = submesh.materialSlot + 1;
					}
				}
				for (u32 slot = 0; slot < slotCount; ++slot) {
					renderer->SetMaterial(slot, material);
				}
			}

			AddPreviewLights(request.stage);

			return RenderPreview(request, mesh->bounds, out);
		}

	} // namespace EditorPreviewsInternal

} // namespace Shalltear
