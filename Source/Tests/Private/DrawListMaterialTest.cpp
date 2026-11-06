#include "TestsPCH.h"

#include "ShadingHarness.h"

#include "Shalltear/Core/Graphics/MaterialAsset.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Graphics/ShaderAsset.h"

#include <format>

using namespace Shalltear;

namespace {

	using namespace ShadingTest;

	constexpr const char* TINT_SURFACE =
		"#pragma shalltear_template unlit\n"
		"#pragma shalltear_blend opaque\n"
		"#pragma shalltear_depth test_write\n"
		"#pragma shalltear_cull back\n"
		"#pragma shalltear_default tint 1 1 1 1\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
		"void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
		"\tsurface.baseColor = tint.rgb;\n"
		"}\n";

	constexpr u32 COLUMNS = 8;
	constexpr u32 ROWS = 5;
	constexpr u32 MATERIALS = COLUMNS * ROWS;

	Vec3 TintOf(u32 index) {
		const f32 t = static_cast<f32>(index) / static_cast<f32>(MATERIALS - 1);
		return Vec3(t, 1.0f - t, index % 2 == 0 ? 0.25f : 0.75f);
	}

} // namespace

TEST_F(MaterialReadbackFixture, MaterialsDrawAsAssigned) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no device, or no compiler";
	}
	WriteProjectFile("Tint.hlsl", TINT_SURFACE);
	auto* shader = static_cast<ShaderAsset*>(AssetDatabase::LoadAsset(Path("project://Tint.hlsl")));
	ASSERT_NE(shader, nullptr);
	Vector<MaterialAsset*> materials{GetMemoryResource(MemTag::Graphics)};
	for (u32 i = 0; i < MATERIALS; ++i) {
		const Vec3 tint = TintOf(i);
		MaterialAsset* material = LoadMaterial(std::format("Tint{}.mat", i).c_str(), shader->assetId,
											   std::format("\t\"colors\": {{ \"tint\": [{}, {}, {}, 1] }}", tint.x, tint.y, tint.z));
		ASSERT_NE(material, nullptr);
		materials.push_back(material);
	}
	MaterialAsset* missing = LoadMaterial("Missing.mat", Guid{}, {});
	ASSERT_NE(missing, nullptr);

	const Handle<MeshAsset> cube(static_cast<MeshAsset*>(AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Cube)))));
	ASSERT_TRUE(cube.IsValid());
	Handle<Scene> scene = SceneSystem::CreateScene("Materials");
	auto place = [&](u32 column, u32 row) {
		return Vec3((static_cast<f32>(column) - 3.5f) * 1.2f, (static_cast<f32>(row) - 2.0f) * 1.2f, 0.0f);
	};
	for (u32 k = 0; k < MATERIALS; ++k) {
		Handle<SceneObject> object = SceneSystem::CreateObject("Cube", scene);
		object->SetLocalPosition(place(k % COLUMNS, k / COLUMNS));
		object->SetLocalScale(Vec3(0.8f, 0.8f, 0.8f));
		Handle<MeshRenderer> renderer = object->AddComponent<MeshRenderer>();
		renderer->SetMesh(cube);
		if (k == 3) {
			renderer->SetMaterial(0, Handle<MaterialAsset>(missing));
		} else if (k != 11) {
			renderer->SetMaterial(0, Handle<MaterialAsset>(materials[(k * 7) % MATERIALS]));
		}
	}
	SceneSystem::EndFrame();

	const ViewTargetId target = RenderSystem::CreateViewTarget({256, 256});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	ViewDesc desc;
	const Vec3 eye(0.0f, 0.0f, 10.0f);
	desc.view = Mat4::LookAt(eye, Vec3::Zero());
	desc.projection = Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f);
	desc.cameraPosition = eye;
	desc.clearColor = Color::Black();
	desc.tonemap = false;
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));

	for (u32 k = 0; k < MATERIALS; ++k) {
		const Vec4 clip = (desc.projection * desc.view) * Vec4(place(k % COLUMNS, k / COLUMNS) + Vec3(0.0f, 0.0f, 0.4f), 1.0f);
		const i32 x = static_cast<i32>((clip.x / clip.w + 1.0f) * 0.5f * static_cast<f32>(size.x));
		const i32 y = static_cast<i32>((1.0f - clip.y / clip.w) * 0.5f * static_cast<f32>(size.y));
		const size_t at = (static_cast<size_t>(y) * size.x + x) * 4;
		const Vec3 texel(static_cast<f32>(pixels[at]), static_cast<f32>(pixels[at + 1]), static_cast<f32>(pixels[at + 2]));
		if (k == 3 || k == 11) {
			EXPECT_GT(texel.x, 20.0f) << k;
			EXPECT_LT(texel.y, 5.0f) << k;
			EXPECT_NEAR(texel.x, texel.z, 3.0f) << k;
			continue;
		}
		const Vec3 expected = ExpectedTexel(TintOf((k * 7) % MATERIALS));
		EXPECT_NEAR(texel.x, expected.x, 3.0f) << k;
		EXPECT_NEAR(texel.y, expected.y, 3.0f) << k;
		EXPECT_NEAR(texel.z, expected.z, 3.0f) << k;
	}

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
}
