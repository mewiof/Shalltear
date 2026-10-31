#include "TestsPCH.h"

#include "Shalltear/Core/Graphics/Camera.h"
#include "Shalltear/Core/Graphics/ObjectPass.h"

#include "ShadingHarness.h"

using namespace Shalltear;
using namespace ShadingTest;

namespace {

	constexpr u32 QUAD_LAYER = 1u << 1;
	constexpr u32 OTHER_LAYER = 1u << 2;

	class ObjectPassFixture : public MaterialReadbackFixture {
	protected:
		Vec3 RenderWithRow(const ShadingStage& stage, const Handle<RenderPass>& row) {
			Handle<RenderPass> rows[] = {row};
			ViewDesc desc = QuadView();
			desc.passes = rows;
			RenderSystem::SubmitView(stage.target, desc);
			RenderSystem::RenderAll();

			Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
			Vec2I size{};
			EXPECT_TRUE(RenderSystem::ReadbackViewTarget(stage.target, pixels, size));
			return CenterTexel(pixels, size);
		}
	};

} // anonymous namespace

TEST_F(ObjectPassFixture, MaskSelectsOverridePaints) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	Asset* unlitShader = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
	ASSERT_NE(unlitShader, nullptr);

	MaterialAsset* white = LoadMaterial("PassWhite.mat", unlitShader->assetId,
										"\t\"colors\": { \"baseColor\": [1, 1, 1, 1] }");
	MaterialAsset* red = LoadMaterial("PassRed.mat", unlitShader->assetId,
									  "\t\"colors\": { \"baseColor\": [1, 0, 0, 1] }");
	ASSERT_NE(white, nullptr);
	ASSERT_NE(red, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(white, stage));
	Handle<MeshRenderer> renderer = stage.quad->GetComponent<MeshRenderer>();
	ASSERT_TRUE(renderer.IsValid());
	renderer->SetRenderingLayerMask(QUAD_LAYER);

	ObjectPass* pass = New<ObjectPass>(MemTag::Graphics);
	ASSERT_NE(pass, nullptr);
	pass->point = PassPoint::AfterOpaque;
	pass->overrideMaterial = Handle<MaterialAsset>(red);
	Handle<RenderPass> row(static_cast<RenderPass*>(pass));

	pass->renderingLayerMask = QUAD_LAYER;
	const Vec3 selected = RenderWithRow(stage, row);

	pass->renderingLayerMask = OTHER_LAYER;
	const Vec3 filtered = RenderWithRow(stage, row);

	EXPECT_GT(selected.x, 200.0f);
	EXPECT_LT(selected.y, 60.0f);
	EXPECT_LT(selected.z, 60.0f);

	EXPECT_GT(filtered.y, 200.0f);
	EXPECT_GT(filtered.z, 200.0f);

	EXPECT_GT(filtered.y - selected.y, 150.0f);

	Delete(pass);
	TearDownStage(stage);
}

TEST_F(ObjectPassFixture, OverridelessRowUsesMaterials) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	Asset* unlitShader = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
	ASSERT_NE(unlitShader, nullptr);

	MaterialAsset* green = LoadMaterial("PassGreen.mat", unlitShader->assetId,
										"\t\"colors\": { \"baseColor\": [0, 1, 0, 1] }");
	ASSERT_NE(green, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(green, stage));
	Handle<MeshRenderer> renderer = stage.quad->GetComponent<MeshRenderer>();
	ASSERT_TRUE(renderer.IsValid());
	renderer->SetRenderingLayerMask(QUAD_LAYER);

	ObjectPass* pass = New<ObjectPass>(MemTag::Graphics);
	ASSERT_NE(pass, nullptr);
	pass->point = PassPoint::AfterOpaque;
	pass->renderingLayerMask = QUAD_LAYER;
	Handle<RenderPass> row(static_cast<RenderPass*>(pass));

	const Vec3 texel = RenderWithRow(stage, row);
	EXPECT_GT(texel.y, 200.0f);
	EXPECT_LT(texel.x, 60.0f);

	Delete(pass);
	TearDownStage(stage);
}

namespace {

	Handle<SceneObject> AddLayeredQuad(ShadingStage& stage, MaterialAsset* material, f32 z, u32 layer) {
		Asset* quadMesh = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Quad)));
		if (!quadMesh || !material) {
			return {};
		}
		Handle<SceneObject> object = SceneSystem::CreateObject("LayeredQuad", stage.scene);
		object->SetLocalPosition(Vec3(0.0f, 0.0f, z));
		Handle<MeshRenderer> renderer = object->AddComponent<MeshRenderer>();
		renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(quadMesh)));
		renderer->SetMaterial(0, Handle<MaterialAsset>(material));
		renderer->SetRenderingLayerMask(layer);
		SceneSystem::EndFrame();
		return object;
	}

	bool RenderForCamera(const ShadingStage& stage, Handle<Camera> camera, std::span<const Handle<RenderPass>> rows,
						 Vector<u8>& pixels, Vec2I& size) {
		ViewDesc desc = QuadView();
		desc.camera = camera;
		desc.passes = rows;
		RenderSystem::SubmitView(stage.target, desc);
		RenderSystem::RenderAll();
		return RenderSystem::ReadbackViewTarget(stage.target, pixels, size);
	}

	i32 CenterRowWidth(const Vector<u8>& pixels, Vec2I size) {
		i32 width = 0;
		const i32 row = size.y / 2;
		for (i32 x = 0; x < size.x; ++x) {
			if (pixels[(static_cast<size_t>(row) * size.x + x) * 4] > 128) {
				++width;
			}
		}
		return width;
	}

} // anonymous namespace

TEST_F(ObjectPassFixture, CullingMaskSkipsUnnamedLayers) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	Asset* unlitShader = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
	ASSERT_NE(unlitShader, nullptr);
	MaterialAsset* white = LoadMaterial("MaskWhite.mat", unlitShader->assetId, "\t\"colors\": { \"baseColor\": [1, 1, 1, 1] }");
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(white, stage));
	stage.quad->GetComponent<MeshRenderer>()->SetRenderingLayerMask(QUAD_LAYER);
	Handle<Camera> camera = SceneSystem::CreateObject("Camera", stage.scene)->AddComponent<Camera>();
	ASSERT_TRUE(camera.IsValid());
	EXPECT_EQ(camera->GetCullingMask(), ~0u);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	camera->SetCullingMask(~QUAD_LAYER);
	ASSERT_TRUE(RenderForCamera(stage, camera, {}, pixels, size));
	const Vec3 culled = CenterTexel(pixels, size);
	camera->SetCullingMask(~0u);
	ASSERT_TRUE(RenderForCamera(stage, camera, {}, pixels, size));
	const Vec3 drawn = CenterTexel(pixels, size);

	EXPECT_LT(culled.x + culled.y + culled.z, 30.0f);
	EXPECT_GT(drawn.x, 200.0f);
	TearDownStage(stage);
}

TEST_F(ObjectPassFixture, CameraOverrideUsesFieldOfView) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	Asset* unlitShader = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
	ASSERT_NE(unlitShader, nullptr);
	MaterialAsset* white = LoadMaterial("FovWhite.mat", unlitShader->assetId, "\t\"colors\": { \"baseColor\": [1, 1, 1, 1] }");
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(white, stage));
	stage.quad->GetComponent<MeshRenderer>()->SetRenderingLayerMask(QUAD_LAYER);
	Handle<Camera> camera = SceneSystem::CreateObject("Camera", stage.scene)->AddComponent<Camera>();
	camera->SetCullingMask(~QUAD_LAYER);

	ObjectPass* pass = New<ObjectPass>(MemTag::Graphics);
	pass->point = PassPoint::AfterOpaque;
	pass->renderingLayerMask = QUAD_LAYER;
	pass->overrideCamera = true;
	Handle<RenderPass> rows[] = {Handle<RenderPass>(static_cast<RenderPass*>(pass))};

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	pass->fieldOfViewDeg = 60.0f;
	ASSERT_TRUE(RenderForCamera(stage, camera, rows, pixels, size));
	const i32 own = CenterRowWidth(pixels, size);
	pass->fieldOfViewDeg = 30.0f;
	ASSERT_TRUE(RenderForCamera(stage, camera, rows, pixels, size));
	const i32 narrow = CenterRowWidth(pixels, size);

	EXPECT_NEAR(static_cast<f32>(own), 18.5f, 1.5f);
	ASSERT_GT(own, 0);
	EXPECT_NEAR(static_cast<f32>(narrow) / static_cast<f32>(own), 2.155f, 0.17f);
	Delete(pass);
	TearDownStage(stage);
}

TEST_F(ObjectPassFixture, NearDepthRowDrawsOverWorld) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	Asset* unlitShader = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
	ASSERT_NE(unlitShader, nullptr);
	MaterialAsset* red = LoadMaterial("HandRed.mat", unlitShader->assetId, "\t\"colors\": { \"baseColor\": [1, 0, 0, 1] }");
	MaterialAsset* green = LoadMaterial("HandGreen.mat", unlitShader->assetId, "\t\"colors\": { \"baseColor\": [0, 1, 0, 1] }");
	MaterialAsset* blue = LoadMaterial("WallBlue.mat", unlitShader->assetId, "\t\"colors\": { \"baseColor\": [0, 0, 1, 1] }");
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(red, stage));
	stage.quad->GetComponent<MeshRenderer>()->SetRenderingLayerMask(QUAD_LAYER);
	ASSERT_TRUE(AddLayeredQuad(stage, green, -0.3f, QUAD_LAYER).IsValid());
	ASSERT_TRUE(AddLayeredQuad(stage, blue, 0.5f, 1u).IsValid());
	Handle<Camera> camera = SceneSystem::CreateObject("Camera", stage.scene)->AddComponent<Camera>();
	camera->SetCullingMask(~QUAD_LAYER);

	ObjectPass* pass = New<ObjectPass>(MemTag::Graphics);
	pass->point = PassPoint::AfterOpaque;
	pass->renderingLayerMask = QUAD_LAYER;
	pass->overrideCamera = true;
	pass->fieldOfViewDeg = 60.0f;
	Handle<RenderPass> rows[] = {Handle<RenderPass>(static_cast<RenderPass*>(pass))};

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	pass->depthScale = 1.0f;
	ASSERT_TRUE(RenderForCamera(stage, camera, rows, pixels, size));
	const Vec3 own = CenterTexel(pixels, size);
	pass->depthScale = 0.01f;
	ASSERT_TRUE(RenderForCamera(stage, camera, rows, pixels, size));
	const Vec3 squeezed = CenterTexel(pixels, size);

	EXPECT_GT(own.z, 200.0f);
	EXPECT_LT(own.x, 60.0f);
	EXPECT_GT(squeezed.x, 200.0f);
	EXPECT_LT(squeezed.y, 60.0f);
	EXPECT_LT(squeezed.z, 60.0f);
	Delete(pass);
	TearDownStage(stage);
}
