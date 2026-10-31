#include "PreviewRig.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/Environment.h"
#include "Shalltear/Core/Graphics/FullScreenPass.h"
#include "Shalltear/Core/Graphics/Light.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Scene/SceneSystem.h"

#include "Shalltear/Editor/EditorPreviews.h"
#include "Shalltear/Editor/EditorTheme.h"

namespace Shalltear {

	namespace {

		constexpr f32 KEY_PITCH_DEG = 28.0f;
		constexpr f32 KEY_YAW_DEG = -35.0f;
		constexpr f32 KEY_INTENSITY = 3.0f;
		constexpr f32 FILL_PITCH_DEG = -20.0f;
		constexpr f32 FILL_YAW_DEG = 150.0f;
		constexpr f32 FILL_INTENSITY = 1.2f;
		constexpr f32 FILL_RANGE = 12.0f;

		constexpr f32 ENVIRONMENT_INTENSITY = 0.35f;

		constexpr f32 PREVIEW_FOV_DEG = 30.0f;
		constexpr f32 PREVIEW_MARGIN = 1.15f;

		Handle<Asset> _defaultMaterial;

		void AddDirectional(Handle<Scene> stage, std::string_view name, f32 pitch, f32 yaw, f32 intensity) {
			Handle<SceneObject> object = SceneSystem::CreateObject(name, stage);
			if (!object.IsValid()) {
				return;
			}
			object->SetLocalRotation(Quat::FromEuler(Vec3(pitch, yaw, 0.0f)));
			if (Handle<Light> light = object->AddComponent<Light>(); light.IsValid()) {
				light->SetLightType(LightType::Directional);
				light->SetIntensity(intensity);
			}
		}

		void AddFill(Handle<Scene> stage, f32 pitch, f32 yaw, f32 intensity, f32 distance) {
			Handle<SceneObject> object = SceneSystem::CreateObject("Fill", stage);
			if (!object.IsValid()) {
				return;
			}
			Quat rotation = Quat::FromEuler(Vec3(pitch, yaw, 0.0f));
			object->SetLocalRotation(rotation);
			object->SetLocalPosition(rotation * Vec3::Forward() * -distance * 0.5f);
			if (Handle<Light> light = object->AddComponent<Light>(); light.IsValid()) {
				light->SetLightType(LightType::Point);
				light->SetIntensity(intensity);
				light->SetRange(distance);
			}
		}

		constexpr std::string_view ENVIRONMENT_OBJECT = "Environment";

		void ApplySky(Environment& environment, Handle<MaterialAsset> sky) {
			environment.SetSkyMode(sky.IsValid() ? SkyMode::Material : SkyMode::None);
			environment.SetSkyMaterial(sky);
		}

		void AddEnvironment(Handle<Scene> stage, Handle<MaterialAsset> sky) {
			Handle<SceneObject> object = SceneSystem::CreateObject(ENVIRONMENT_OBJECT, stage);
			if (!object.IsValid()) {
				return;
			}
			Handle<Environment> environment = object->AddComponent<Environment>();
			if (!environment.IsValid()) {
				return;
			}
			ApplySky(*environment, sky);
			environment->SetAmbientMode(AmbientMode::Color);
			environment->SetAmbientColor(Color::White() * ENVIRONMENT_INTENSITY);
		}

	} // anonymous namespace

	namespace EditorPreviewsInternal {

		void AddPreviewLights(Handle<Scene> stage, Handle<MaterialAsset> sky) {
			AddDirectional(stage, "Key", KEY_PITCH_DEG, KEY_YAW_DEG, KEY_INTENSITY);
			AddFill(stage, FILL_PITCH_DEG, FILL_YAW_DEG, FILL_INTENSITY, FILL_RANGE);
			AddEnvironment(stage, sky);
		}

		Handle<MaterialAsset> SharedDefaultMaterial() {
			Asset* material = _defaultMaterial.Get();
			if (!material) {
				material = AssetDatabase::LoadAsset(Path("engine://Materials/Default.mat"));
				_defaultMaterial = Handle<Asset>(material);
				if (!material) {
					SLT_LOG_WARN("Editor", "mesh preview: engine://Materials/Default.mat didn't load");
					return {};
				}
			}
			return Handle<MaterialAsset>(static_cast<MaterialAsset*>(material));
		}

		void ReleaseSharedDefaultMaterial() {
			if (Asset* material = _defaultMaterial.Get()) {
				AssetDatabase::UnloadAsset(material);
			}
			_defaultMaterial = {};
		}

		LentRows::LentRows(FullScreenPass* row) {
			if (row && row->material.IsValid()) {
				_rows[0] = Handle<RenderPass>(row);
				_count = 1;
			}
		}

		bool SubmitPreviewView(Handle<Scene> stage, ViewTargetId target, Vec2I targetSize, const AABB& bounds,
							   const AssetPreviewOrbit& orbit, const f64* time, std::span<const Handle<RenderPass>> passes) {
			if (target == INVALID_VIEW_TARGET || targetSize.x <= 0 || targetSize.y <= 0) {
				return false;
			}
			ViewDesc desc;
			f32 aspect = static_cast<f32>(targetSize.x) / static_cast<f32>(targetSize.y);
			if (!EditorPreviewOps::FitCameraToBounds(bounds, aspect, PREVIEW_FOV_DEG, PREVIEW_MARGIN, orbit, desc)) {
				return false;
			}
			desc.clearColor = EditorTheme::GetColor(EditorTheme::ColorToken::ViewportClear);
			const Handle<Scene> onlyStage[] = {stage};
			desc.scenes = {onlyStage, 1};
			desc.time = time;
			desc.passes = passes;
			RenderSystem::SubmitView(target, desc);
			return true;
		}

		bool RenderPreview(const AssetPreviewRequest& request, const AABB& bounds, AssetPreviewImage& out) {
			if (!SubmitPreviewView(request.stage, request.target, request.targetSize, bounds, AssetPreviewOrbit{}, nullptr,
								   LentRows(request.row).Get())) {
				return false;
			}

			out.subjectBounds = bounds;

			out.texture = RenderSystem::GetViewTargetTexture(request.target);
			out.size = request.targetSize;
			out.sampler = UI::SamplerKind::LinearClamp;
			return true;
		}

	} // namespace EditorPreviewsInternal

	namespace EditorPreviewStage {

		void AddLights(Handle<Scene> stage) { EditorPreviewsInternal::AddPreviewLights(stage); }

		void SetSky(Handle<Scene> stage, Handle<MaterialAsset> sky) {
			Handle<SceneObject> object = stage.IsValid() ? stage->FindObjectByName(ENVIRONMENT_OBJECT) : Handle<SceneObject>{};
			Handle<Environment> environment = object.IsValid() ? object->GetComponent<Environment>() : Handle<Environment>{};
			if (environment.IsValid()) {
				ApplySky(*environment, sky);
			}
		}

		bool Submit(Handle<Scene> stage, ViewTargetId target, Vec2I targetSize, const AABB& bounds, const AssetPreviewOrbit& orbit,
					const f64* time, std::span<const Handle<RenderPass>> passes) {
			return EditorPreviewsInternal::SubmitPreviewView(stage, target, targetSize, bounds, orbit, time, passes);
		}

	} // namespace EditorPreviewStage

} // namespace Shalltear
