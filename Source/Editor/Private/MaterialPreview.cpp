#include "PreviewRig.h"

#include "Shalltear/Editor/EditorPreviews.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/FullScreenPass.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshAsset.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Graphics/PrimitiveMesh.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Scene/SceneSystem.h"

namespace Shalltear {

	namespace {

		Handle<Asset> _sphere;

		MeshAsset* SharedSphereMesh() {
			Asset* sphere = _sphere.Get();
			if (!sphere) {
				sphere = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Sphere)));
				_sphere = Handle<Asset>(sphere);
			}
			return static_cast<MeshAsset*>(sphere);
		}

	} // anonymous namespace

	namespace EditorPreviewsInternal {

		void ReleaseMaterialPreviewAssets() {
			if (Asset* sphere = _sphere.Get()) {
				AssetDatabase::UnloadAsset(sphere);
			}
			_sphere = {};
		}

		bool ProduceMaterialPreview(const AssetPreviewRequest& request, AssetPreviewImage& out) {
			Scene* stage = request.stage.Get();
			if (!stage || !request.asset) {
				return false;
			}
			SLT_CORE_ASSERT_MSG(stage->GetKind() == SceneKind::Isolated,
								"a preview producer was handed a WORLD scene to populate");
			MeshAsset* sphere = SharedSphereMesh();
			if (!sphere) {
				SLT_LOG_WARN("Editor", "material preview: engine://Meshes/Sphere.primitive didn't load");
				return false;
			}

			MaterialAsset* material = static_cast<MaterialAsset*>(request.asset);
			const ShaderAsset* shader = material->shader.Get();

			const bool asSky = shader && shader->integrator == ShaderTemplate::Sky;
			const bool acrossTheView = !asSky && shader && FullScreenPass::Draws(*shader);
			if (acrossTheView && !request.row) {
				return false;
			}

			if (!asSky) {
				Handle<SceneObject> subject = SceneSystem::CreateObject("Subject", request.stage);
				if (!subject.IsValid()) {
					return false;
				}
				Handle<MeshRenderer> renderer = subject->AddComponent<MeshRenderer>();
				if (!renderer.IsValid()) {
					return false;
				}
				renderer->SetMesh(Handle<MeshAsset>(sphere));
				renderer->SetMaterial(0, acrossTheView ? SharedDefaultMaterial() : Handle<MaterialAsset>(material));
				if (acrossTheView) {
					request.row->material = Handle<MaterialAsset>(material);
				}
			}

			AddPreviewLights(request.stage, asSky ? Handle<MaterialAsset>(material) : Handle<MaterialAsset>{});

			out.dependencies.push_back(material->shaderGuid);
			for (const MaterialAsset::TextureParam& param : material->textures) {
				out.dependencies.push_back(param.texture);
			}

			return RenderPreview(request, sphere->bounds, out);
		}

	} // namespace EditorPreviewsInternal

} // namespace Shalltear
