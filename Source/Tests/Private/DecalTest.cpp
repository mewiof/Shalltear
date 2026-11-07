#include "TestsPCH.h"

#include "ShadingHarness.h"
#include "TestImages.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Graphics/Decal.h"
#include "Shalltear/Core/Graphics/MeshRenderer.h"
#include "Shalltear/Core/Graphics/RenderSystem.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include "Asset/EngineBuiltins.h"
#include "Graphics/LightCulling.h"

#include <cstring>
#include <format>
#include <vector>

using namespace Shalltear;
using ShadingTest::MaterialReadbackFixture;
using ShadingTest::ShadingStage;
using Shalltear::GraphicsInternal::BinDecals;
using Shalltear::GraphicsInternal::BuildClusterGrid;
using Shalltear::GraphicsInternal::ClusterGrid;
using Shalltear::GraphicsInternal::ClusterIndexOf;
using Shalltear::GraphicsInternal::DecalBound;
using Shalltear::GraphicsInternal::MergeClusterLists;

TEST(DecalBinningTest, FullClusterKeepsTopDecalsInOrder) {
	const ClusterGrid grid = BuildClusterGrid(Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f), 1.0f, 10.0f);
	const Vec3 center(0.0625f * grid.tanHalfX * 4.0f, 0.0f, 4.0f);
	const std::vector<DecalBound> decals(3, DecalBound{center, 0.01f});
	Vector<u32> table{GetMemoryResource(MemTag::Graphics)};
	Vector<u32> indices{GetMemoryResource(MemTag::Graphics)};

	const u32 dropped = BinDecals(grid, Mat4::Identity(), decals, 2, table, indices);

	const u32 cluster = ClusterIndexOf(grid, center);
	ASSERT_EQ(table[cluster * 2 + 1], 2u);
	EXPECT_EQ(indices[table[cluster * 2]], 1u);
	EXPECT_EQ(indices[table[cluster * 2] + 1], 2u);
	EXPECT_GE(dropped, 1u);
}

TEST(DecalBinningTest, MergedHeaderPacksCountsLightsFirst) {
	const u32 lightTable[] = {0, 2, 2, 0};
	const u32 lightIndices[] = {5, 6};
	const u32 decalTable[] = {0, 1, 1, 2};
	const u32 decalIndices[] = {0, 1, 2};
	Vector<u32> header{GetMemoryResource(MemTag::Graphics)};
	Vector<u32> indices{GetMemoryResource(MemTag::Graphics)};

	MergeClusterLists(lightTable, lightIndices, decalTable, decalIndices, header, indices);

	ASSERT_EQ(header.size(), 4u);
	EXPECT_EQ(header[0], 0u);
	EXPECT_EQ(header[1], 2u | (1u << GraphicsInternal::CLUSTER_DECAL_SHIFT));
	EXPECT_EQ(header[2], 3u);
	EXPECT_EQ(header[3], 2u << GraphicsInternal::CLUSTER_DECAL_SHIFT);
	EXPECT_EQ(std::vector<u32>(indices.begin(), indices.end()), (std::vector<u32>{5, 6, 0, 1, 2}));
}

namespace {

	constexpr i32 QUADRANT_SIZE = 8;

	Vec3 TexelAt(const Vector<u8>& pixels, Vec2I size, Vec2I at) {
		const size_t index = (static_cast<size_t>(at.y) * size.x + at.x) * 4;
		return Vec3(static_cast<f32>(pixels[index]), static_cast<f32>(pixels[index + 1]), static_cast<f32>(pixels[index + 2]));
	}

	Vec2I ProjectToPixel(Vec3 worldPosition, Vec2I size) {
		const ViewDesc view = ShadingTest::QuadView();
		const Vec4 clip = (view.projection * view.view) * Vec4(worldPosition, 1.0f);
		return Vec2I{static_cast<i32>((clip.x / clip.w + 1.0f) * 0.5f * static_cast<f32>(size.x)),
					 static_cast<i32>((1.0f - clip.y / clip.w) * 0.5f * static_cast<f32>(size.y))};
	}

	str Rgba(f32 r, f32 g, f32 b, f32 a) {
		return std::format("[{}, {}, {}, {}]", r, g, b, a);
	}

	class DecalFixture : public MaterialReadbackFixture {
	protected:
		u32 _materials = 0;

		MaterialAsset* Surface(f32 r, f32 g, f32 b, f32 a = 1.0f, bool blended = false) {
			const str name = std::format("Surface{}.mat", _materials++);
			str body = "\t\"colors\": { \"baseColor\": " + Rgba(r, g, b, a) + " },\n"
																			  "\t\"floats\": { \"metallic\": 0, \"roughness\": 1, \"reflectance\": 0.5, \"normalScale\": 1, \"occlusionStrength\": 1 }";
			if (blended) {
				body += ",\n\t\"blend\": \"alpha\"";
			}
			return LoadMaterial(name.c_str(), EngineBuiltins::LitShader, body);
		}

		MaterialAsset* DecalMaterial(f32 r, f32 g, f32 b, f32 a = 1.0f, const Guid& texture = {}) {
			const str name = std::format("Decal{}.mat", _materials++);
			str body = "\t\"colors\": { \"baseColor\": " + Rgba(r, g, b, a) + " }";
			if (texture.IsValid()) {
				body += ",\n\t\"textures\": { \"baseColorTexture\": \"" + texture.ToString() + "\" }";
			}
			return LoadMaterial(name.c_str(), EngineBuiltins::DecalShader, body);
		}

		Guid QuadrantTexture() {
			std::vector<u8> rgba(static_cast<size_t>(QUADRANT_SIZE) * QUADRANT_SIZE * 4, 255);
			for (i32 y = 0; y < QUADRANT_SIZE / 2; ++y) {
				for (i32 x = 0; x < QUADRANT_SIZE / 2; ++x) {
					u8* texel = &rgba[(static_cast<size_t>(y) * QUADRANT_SIZE + x) * 4];
					texel[0] = texel[1] = texel[2] = 0;
				}
			}
			TestImages::WriteTga(_root / "Quadrants.tga", QUADRANT_SIZE, QUADRANT_SIZE, rgba.data());
			const Path path("project://Quadrants.tga");
			const Guid guid = AssetDatabase::EnsureAssetIdentity(path);
			JSONValue settings = JSONValue::MakeObject();
			settings.AddMember("sRGB", JSONValue::MakeBool(false));
			settings.AddMember("compression", JSONValue::MakeString("none"));
			settings.AddMember("generateMips", JSONValue::MakeBool(false));
			settings.AddMember("filter", JSONValue::MakeString("point"));
			if (!AssetDatabase::WriteImporterSettings(path, settings) || !AssetDatabase::ReimportAsset(path)) {
				return {};
			}
			return guid;
		}

		Handle<Decal> AddDecal(ShadingStage& stage, MaterialAsset* material, Vec3 position, Vec3 size) {
			Handle<SceneObject> object = SceneSystem::CreateObject("Decal", stage.scene);
			object->SetLocalPosition(position);
			object->SetLocalRotation(Quat::AngleAxis(180.0f, Vec3::Up()));
			Handle<Decal> decal = object->AddComponent<Decal>();
			decal->SetMaterial(Handle<MaterialAsset>(material));
			decal->SetSize(size);
			decal->SetPivot(Vec3(0.0f, 0.0f, 0.0f));
			SceneSystem::EndFrame();
			return decal;
		}

		void SetSurface(ShadingStage& stage, MaterialAsset* material) {
			stage.quad->GetComponent<MeshRenderer>()->SetMaterial(0, Handle<MaterialAsset>(material));
		}

		bool Render(const ShadingStage& stage, Vector<u8>& pixels, Vec2I& size) {
			SceneSystem::EndFrame();
			return RenderStage(stage, pixels, size);
		}

		bool Ready() {
			return RenderSystem::IsRenderingAvailable() && CompilerAvailable();
		}
	};

} // namespace

TEST_F(DecalFixture, DecalTexelEqualsSurfaceOfItsColor) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or dxcompiler.dll";
	}
	MaterialAsset* white = Surface(1.0f, 1.0f, 1.0f);
	MaterialAsset* red = Surface(1.0f, 0.0f, 0.0f);
	MaterialAsset* redDecal = DecalMaterial(1.0f, 0.0f, 0.0f);
	ASSERT_NE(redDecal, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(white, stage));
	stage.quad->SetLocalScale(Vec3(3.0f, 3.0f, 1.0f));
	const Vec3 center(0.6f, 0.3f, 0.0f);
	Handle<Decal> decal = AddDecal(stage, redDecal, center, Vec3(0.3f, 0.3f, 0.2f));

	Vector<u8> decaled{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> plain{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> painted{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, decaled, size));
	decal->SetEnabled(false);
	ASSERT_TRUE(Render(stage, plain, size));
	SetSurface(stage, red);
	ASSERT_TRUE(Render(stage, painted, size));

	const Vec2I inside = ProjectToPixel(center, size);
	const Vec2I outside = ProjectToPixel(center + Vec3(-0.5f, 0.0f, 0.0f), size);
	EXPECT_EQ(TexelAt(decaled, size, inside), TexelAt(painted, size, inside));
	EXPECT_NE(TexelAt(plain, size, inside), TexelAt(painted, size, inside));
	EXPECT_EQ(TexelAt(decaled, size, outside), TexelAt(plain, size, outside));

	TearDownStage(stage);
}

TEST_F(DecalFixture, DecalTextureFacesProjector) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or dxcompiler.dll";
	}
	const Guid quadrants = QuadrantTexture();
	ASSERT_TRUE(quadrants.IsValid());
	MaterialAsset* white = Surface(1.0f, 1.0f, 1.0f);
	MaterialAsset* black = Surface(0.0f, 0.0f, 0.0f);
	MaterialAsset* decalMaterial = DecalMaterial(1.0f, 1.0f, 1.0f, 1.0f, quadrants);
	ASSERT_NE(decalMaterial, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(white, stage));
	stage.quad->SetLocalScale(Vec3(3.0f, 3.0f, 1.0f));
	const Vec3 center(-0.2f, 0.1f, 0.0f);
	Handle<Decal> decal = AddDecal(stage, decalMaterial, center, Vec3(1.0f, 1.0f, 0.5f));

	Vector<u8> decaled{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> plain{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> dark{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, decaled, size));
	decal->SetEnabled(false);
	ASSERT_TRUE(Render(stage, plain, size));
	SetSurface(stage, black);
	ASSERT_TRUE(Render(stage, dark, size));

	const Vec2I darkQuadrant = ProjectToPixel(center + Vec3(0.25f, 0.25f, 0.0f), size);
	EXPECT_EQ(TexelAt(decaled, size, darkQuadrant), TexelAt(dark, size, darkQuadrant));
	for (const Vec3& lit : {Vec3(-0.25f, 0.25f, 0.0f), Vec3(0.25f, -0.25f, 0.0f), Vec3(-0.25f, -0.25f, 0.0f)}) {
		const Vec2I at = ProjectToPixel(center + lit, size);
		EXPECT_EQ(TexelAt(decaled, size, at), TexelAt(plain, size, at)) << lit.x << ", " << lit.y;
	}

	TearDownStage(stage);
}

TEST_F(DecalFixture, HigherPriorityDecalDrawsOver) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or dxcompiler.dll";
	}
	MaterialAsset* white = Surface(1.0f, 1.0f, 1.0f);
	MaterialAsset* red = Surface(1.0f, 0.0f, 0.0f);
	MaterialAsset* green = Surface(0.0f, 1.0f, 0.0f);
	MaterialAsset* redDecal = DecalMaterial(1.0f, 0.0f, 0.0f);
	MaterialAsset* greenDecal = DecalMaterial(0.0f, 1.0f, 0.0f);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(white, stage));
	stage.quad->SetLocalScale(Vec3(3.0f, 3.0f, 1.0f));
	Handle<Decal> first = AddDecal(stage, redDecal, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.8f, 0.8f, 0.5f));
	Handle<Decal> second = AddDecal(stage, greenDecal, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.8f, 0.8f, 0.5f));
	first->SetPriority(1);

	Vector<u8> firstOnTop{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> secondOnTop{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> paintedRed{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> paintedGreen{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, firstOnTop, size));
	first->SetPriority(-1);
	ASSERT_TRUE(Render(stage, secondOnTop, size));
	first->SetEnabled(false);
	second->SetEnabled(false);
	SetSurface(stage, red);
	ASSERT_TRUE(Render(stage, paintedRed, size));
	SetSurface(stage, green);
	ASSERT_TRUE(Render(stage, paintedGreen, size));

	const Vec2I center = ProjectToPixel(Vec3(), size);
	EXPECT_EQ(TexelAt(firstOnTop, size, center), TexelAt(paintedRed, size, center));
	EXPECT_EQ(TexelAt(secondOnTop, size, center), TexelAt(paintedGreen, size, center));

	TearDownStage(stage);
}

TEST_F(DecalFixture, TransparentSurfaceReceivesDecal) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or dxcompiler.dll";
	}
	MaterialAsset* glass = Surface(1.0f, 1.0f, 1.0f, 0.5f, true);
	MaterialAsset* redGlass = Surface(1.0f, 0.0f, 0.0f, 0.5f, true);
	MaterialAsset* redDecal = DecalMaterial(1.0f, 0.0f, 0.0f);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(glass, stage));
	stage.quad->SetLocalScale(Vec3(3.0f, 3.0f, 1.0f));
	Handle<Decal> decal = AddDecal(stage, redDecal, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.8f, 0.8f, 0.5f));

	Vector<u8> decaled{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> plain{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> painted{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, decaled, size));
	decal->SetEnabled(false);
	ASSERT_TRUE(Render(stage, plain, size));
	SetSurface(stage, redGlass);
	ASSERT_TRUE(Render(stage, painted, size));

	const Vec2I center = ProjectToPixel(Vec3(), size);
	EXPECT_EQ(TexelAt(decaled, size, center), TexelAt(painted, size, center));
	EXPECT_NE(TexelAt(plain, size, center), TexelAt(painted, size, center));

	TearDownStage(stage);
}

TEST_F(DecalFixture, DecalSkipsOtherLayers) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or dxcompiler.dll";
	}
	MaterialAsset* white = Surface(1.0f, 1.0f, 1.0f);
	MaterialAsset* redDecal = DecalMaterial(1.0f, 0.0f, 0.0f);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(white, stage));
	stage.quad->SetLocalScale(Vec3(3.0f, 3.0f, 1.0f));
	MeshRenderer* renderer = stage.quad->GetComponent<MeshRenderer>().Get();
	Handle<Decal> decal = AddDecal(stage, redDecal, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.8f, 0.8f, 0.5f));
	decal->SetRenderingLayerMask(1u << 1);

	Vector<u8> otherLayer{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> sameLayer{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> plain{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	renderer->SetRenderingLayerMask(1u << 2);
	ASSERT_TRUE(Render(stage, otherLayer, size));
	renderer->SetRenderingLayerMask(1u << 1);
	ASSERT_TRUE(Render(stage, sameLayer, size));
	decal->SetEnabled(false);
	ASSERT_TRUE(Render(stage, plain, size));

	const Vec2I center = ProjectToPixel(Vec3(), size);
	EXPECT_EQ(TexelAt(otherLayer, size, center), TexelAt(plain, size, center));
	EXPECT_NE(TexelAt(sameLayer, size, center), TexelAt(plain, size, center));

	TearDownStage(stage);
}

TEST_F(DecalFixture, AngleFadeAtPredictedFactor) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or dxcompiler.dll";
	}
	MaterialAsset* white = Surface(1.0f, 1.0f, 1.0f);
	MaterialAsset* half = Surface(1.0f, 0.5f, 0.5f);
	MaterialAsset* red = Surface(1.0f, 0.0f, 0.0f);
	MaterialAsset* redDecal = DecalMaterial(1.0f, 0.0f, 0.0f);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(white, stage));
	stage.quad->SetLocalRotation(Quat::AngleAxis(60.0f, Vec3::Up()));
	Handle<Decal> decal = AddDecal(stage, redDecal, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.6f, 0.6f, 2.0f));

	Vector<u8> faded{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> unfaded{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> gone{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> paintedHalf{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> paintedRed{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> plain{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	decal->SetAngleFade(Vec2(40.0f, 80.0f));
	ASSERT_TRUE(Render(stage, faded, size));
	decal->SetAngleFade(Vec2(70.0f, 80.0f));
	ASSERT_TRUE(Render(stage, unfaded, size));
	decal->SetAngleFade(Vec2(20.0f, 40.0f));
	ASSERT_TRUE(Render(stage, gone, size));
	decal->SetEnabled(false);
	ASSERT_TRUE(Render(stage, plain, size));
	SetSurface(stage, half);
	ASSERT_TRUE(Render(stage, paintedHalf, size));
	SetSurface(stage, red);
	ASSERT_TRUE(Render(stage, paintedRed, size));

	const Vec2I center = ProjectToPixel(Vec3(), size);
	const Vec3 measured = TexelAt(faded, size, center);
	const Vec3 expected = TexelAt(paintedHalf, size, center);
	EXPECT_NEAR(measured.x, expected.x, 1.0f);
	EXPECT_NEAR(measured.y, expected.y, 1.0f);
	EXPECT_NEAR(measured.z, expected.z, 1.0f);
	EXPECT_GT(TexelAt(plain, size, center).y, expected.y + 8.0f);
	EXPECT_GT(expected.y, TexelAt(paintedRed, size, center).y + 8.0f);
	EXPECT_EQ(TexelAt(unfaded, size, center), TexelAt(paintedRed, size, center));
	EXPECT_EQ(TexelAt(gone, size, center), TexelAt(plain, size, center));

	TearDownStage(stage);
}

TEST_F(DecalFixture, DecalBinnedToItsCluster) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or dxcompiler.dll";
	}
	MaterialAsset* white = Surface(1.0f, 1.0f, 1.0f);
	MaterialAsset* black = Surface(0.0f, 0.0f, 0.0f);
	MaterialAsset* blackDecal = DecalMaterial(0.0f, 0.0f, 0.0f);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(white, stage));
	stage.quad->SetLocalScale(Vec3(3.0f, 3.0f, 1.0f));
	const ViewTargetId large = RenderSystem::CreateViewTarget({256, 256});
	ASSERT_NE(large, INVALID_VIEW_TARGET);
	const f32 tileWorld = 2.0f * 3.0f * std::tan(30.0f * Math::DEG_TO_RAD) / 16.0f;
	const Vec3 center(-0.5f * tileWorld, 0.0f, 0.0f);
	Handle<Decal> decal = AddDecal(stage, blackDecal, center, Vec3(0.08f, 0.08f, 0.04f));

	auto render = [&](Vector<u8>& pixels, Vec2I& size) {
		SceneSystem::EndFrame();
		RenderSystem::SubmitView(large, ShadingTest::QuadView());
		RenderSystem::RenderAll();
		return RenderSystem::ReadbackViewTarget(large, pixels, size);
	};
	Vector<u8> decaled{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> plain{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> painted{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(render(decaled, size));
	decal->SetEnabled(false);
	ASSERT_TRUE(render(plain, size));
	SetSurface(stage, black);
	ASSERT_TRUE(render(painted, size));

	const Vec2I inside = ProjectToPixel(center, size);
	EXPECT_EQ(TexelAt(decaled, size, inside), TexelAt(painted, size, inside));
	EXPECT_GT(TexelAt(plain, size, inside).x, TexelAt(painted, size, inside).x + 8.0f);
	const Vec2I neighbor = ProjectToPixel(center + Vec3(tileWorld, 0.0f, 0.0f), size);
	EXPECT_EQ(TexelAt(decaled, size, neighbor), TexelAt(plain, size, neighbor));

	RenderSystem::DestroyViewTarget(large);
	TearDownStage(stage);
}

TEST_F(DecalFixture, DecaledFramesReadIdentical) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or dxcompiler.dll";
	}
	const Guid quadrants = QuadrantTexture();
	MaterialAsset* white = Surface(1.0f, 1.0f, 1.0f);
	MaterialAsset* decalMaterial = DecalMaterial(1.0f, 0.5f, 0.0f, 0.75f, quadrants);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(white, stage));
	stage.quad->SetLocalScale(Vec3(3.0f, 3.0f, 1.0f));
	AddDecal(stage, decalMaterial, Vec3(0.2f, -0.1f, 0.0f), Vec3(1.0f, 0.7f, 0.5f));
	AddDecal(stage, decalMaterial, Vec3(-0.3f, 0.2f, 0.0f), Vec3(0.6f, 0.6f, 0.5f));

	Vector<u8> first{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> second{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, first, size));
	ASSERT_TRUE(Render(stage, second, size));
	ASSERT_EQ(first.size(), second.size());
	EXPECT_EQ(std::memcmp(first.data(), second.data(), first.size()), 0);

	TearDownStage(stage);
}

TEST_F(DecalFixture, DeadDecalMaterialLeavesSurface) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or dxcompiler.dll";
	}
	MaterialAsset* white = Surface(1.0f, 1.0f, 1.0f);
	MaterialAsset* redDecal = DecalMaterial(1.0f, 0.0f, 0.0f);
	MaterialAsset* lit = Surface(0.0f, 0.0f, 1.0f);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(white, stage));
	stage.quad->SetLocalScale(Vec3(3.0f, 3.0f, 1.0f));
	Handle<Decal> decal = AddDecal(stage, redDecal, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.8f, 0.8f, 0.5f));
	Handle<Decal> wrongShader = AddDecal(stage, lit, Vec3(0.0f, 0.0f, 0.0f), Vec3(0.8f, 0.8f, 0.5f));

	Vector<u8> decaled{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> after{GetMemoryResource(MemTag::Graphics)};
	Vector<u8> plain{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, decaled, size));
	AssetDatabase::UnloadAsset(redDecal);
	AssetDatabase::CollectGarbage();
	EXPECT_FALSE(decal->GetMaterial().IsValid());
	ASSERT_TRUE(Render(stage, after, size));
	decal->SetEnabled(false);
	wrongShader->SetEnabled(false);
	ASSERT_TRUE(Render(stage, plain, size));

	EXPECT_NE(std::memcmp(decaled.data(), plain.data(), plain.size()), 0);
	EXPECT_EQ(std::memcmp(after.data(), plain.data(), plain.size()), 0);

	TearDownStage(stage);
}
