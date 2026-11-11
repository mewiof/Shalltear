#pragma once

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/RenderPass.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Graphics/SkinnedMeshRenderer.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Scene/SceneSystem.h"

#include "Shalltear/Editor/EditorViewportOps.h"

#include <filesystem>
#include <span>

namespace PassProbes {

	using namespace Shalltear;

	struct GridPush {
		Mat4 invGridProjection;
		Vec3 gridCameraPosition;
		f32 cellPixelBudget;
		f32 fadeDistance;
		f32 minorLineWidthPx;
		f32 majorLineWidthPx;
		u32 minorColor;
		u32 majorColor;
		u32 axisAlongXColor;
		u32 axisAlongZColor;
	};
	inline constexpr size_t GRID_PUSH_BYTES = 108;
	static_assert(sizeof(GridPush) == 112 && GRID_PUSH_BYTES < sizeof(GridPush),
				  "the grid push block must match the shader's field for field");

	inline GridPush MakeVisiblePush(const Mat4& view, const Mat4& projection, const Vec3& position) {
		GridPush push{};
		const EditorViewportOps::GridBasis basis =
			EditorViewportOps::GridBasisFor(EditorViewportOps::GridPlaneFor(view, projection));
		push.invGridProjection = EditorViewportOps::InverseGridProjection(basis, view, projection, position);
		push.gridCameraPosition = basis.gridFromWorld.TransformPoint(position);
		push.cellPixelBudget = 24.0f;
		push.fadeDistance = 4000.0f;
		push.minorLineWidthPx = 1.0f;
		push.majorLineWidthPx = 1.8f;
		push.minorColor = Color32(200, 200, 210, 255).ToU32();
		push.majorColor = Color32(255, 255, 255, 255).ToU32();
		push.axisAlongXColor = Color32(255, 0, 0, 255).ToU32();
		push.axisAlongZColor = Color32(0, 0, 255, 255).ToU32();
		return push;
	}

	class DrawingProbePass : public RenderPass {
	public:
		Handle<ShaderAsset> shader;
		std::span<const u8> bytes;
		bool drew = false;

		void Record(PassContext& context) override {
			drew = context.DrawProcedural(shader, bytes);
		}
	};

	inline constexpr const char* CHANNEL_ROTATE_SHADER =
		"#pragma shalltear_blend opaque\n"
		"#pragma shalltear_depth off\n"
		"#pragma shalltear_cull off\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
		"[[vk::binding(0, 2)]] Texture2D sltSceneColor;\n"
		"[[vk::binding(1, 2)]] SamplerState sltSceneSampler;\n"
		"struct VSOutput { float4 positionCS : SV_Position; float2 uv : TEXCOORD0; };\n"
		"VSOutput VSMain(uint vertexId : SV_VertexID) {\n"
		"\tVSOutput output;\n"
		"\tfloat2 corner = float2((vertexId << 1) & 2, vertexId & 2);\n"
		"\toutput.positionCS = float4(corner * 2.0 - 1.0, 0.0, 1.0);\n"
		"\toutput.uv = float2(corner.x, 1.0 - corner.y);\n"
		"\treturn output;\n"
		"}\n"
		"float4 PSMain(VSOutput input) : SV_Target {\n"
		"\tfloat4 scene = sltSceneColor.Sample(sltSceneSampler, input.uv);\n"
		"\treturn float4(scene.g, scene.b, scene.r, 1) * tint;\n"
		"}\n";

	inline Handle<SkinnedMeshRenderer> FindSkinnedRenderer(const Handle<SceneObject>& object) {
		if (Handle<SkinnedMeshRenderer> found = object->GetComponent<SkinnedMeshRenderer>(); found.IsValid()) {
			return found;
		}
		for (u32 i = 0; i < object->GetChildCount(); ++i) {
			if (Handle<SkinnedMeshRenderer> found = FindSkinnedRenderer(object->GetChild(i)); found.IsValid()) {
				return found;
			}
		}
		return {};
	}

	inline Handle<SkinnedMeshRenderer> AddSkinnedMesh(const std::filesystem::path& fixtures, const std::filesystem::path& projectRoot,
													  const Handle<Scene>& scene, const Color& color) {
		Asset* unlitShader = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
		std::error_code ec;
		std::filesystem::copy_file(fixtures / "SimpleSkin.gltf", projectRoot / "SimpleSkin.gltf",
								   std::filesystem::copy_options::overwrite_existing, ec);
		Asset* model = ec || !unlitShader ? nullptr : AssetDatabase::LoadAsset(Path("project://SimpleSkin.gltf"));
		if (!model) {
			return {};
		}
		const Guid modelGuid = model->assetId;
		AssetDatabase::UnloadAsset(model);
		FileSystem::WriteTextFile(Path((projectRoot / "SkinnedMesh.mat").generic_string()),
								  str("{\n\t\"shader\": \"") + unlitShader->assetId.ToString() + "\"\n}\n");
		auto* material = static_cast<MaterialAsset*>(AssetDatabase::LoadAsset(Path("project://SkinnedMesh.mat")));
		Handle<SceneObject> root = SceneSystem::Instantiate(modelGuid, scene);
		Handle<SkinnedMeshRenderer> renderer = root.IsValid() ? FindSkinnedRenderer(root) : Handle<SkinnedMeshRenderer>{};
		if (!material || !renderer.IsValid()) {
			return {};
		}
		material->SetColor("baseColor", color);
		renderer->SetMaterial(0, Handle<MaterialAsset>(material));
		SceneSystem::EndFrame();
		return renderer;
	}

} // namespace PassProbes
