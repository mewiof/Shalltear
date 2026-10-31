#include "TestsPCH.h"

#include "ShadingHarness.h"

#include "Graphics/EnvironmentIBL.h"
#include "Shalltear/Core/Graphics/CubeFace.h"
#include "Shalltear/Core/Graphics/GraphicsProfile.h"
#include "Shalltear/Core/Math/HalfFloat.h"

#include "Graphics/GraphicsSettings.h"

using namespace Shalltear;
using ShadingTest::CenterTexel;
using ShadingTest::MaterialReadbackFixture;
using ShadingTest::QuadView;
using ShadingTest::RadianceTexel;
using ShadingTest::ShadingStage;

namespace {

	Guid UnlitShaderGuid() {
		Asset* shader = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
		return shader ? shader->assetId : Guid{};
	}

	class RadianceCaptureFixture : public MaterialReadbackFixture {
	protected:
		str ProjectSettingsDocument() const override { return "[Graphics]\n"; }

		MaterialAsset* LoadUnlit(const char* fileName, const Vec3& color) {
			return LoadMaterial(fileName, UnlitShaderGuid(),
								std::format("\t\"colors\": {{ \"baseColor\": [{}, {}, {}, 1] }}", color.x, color.y, color.z));
		}

		bool AddMarker(ShadingStage& stage, MaterialAsset* material, const Vec3& position, f32 scale) {
			Asset* quadMesh = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Quad)));
			if (!quadMesh || !material) {
				return false;
			}
			Handle<SceneObject> object = SceneSystem::CreateObject("Marker", stage.scene);
			object->SetLocalPosition(position);
			object->SetLocalScale(Vec3(scale, scale, scale));
			Handle<MeshRenderer> renderer = object->AddComponent<MeshRenderer>();
			renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(quadMesh)));
			renderer->SetMaterial(0, Handle<MaterialAsset>(material));
			SceneSystem::EndFrame();
			return true;
		}
	};

} // anonymous namespace

TEST_F(RadianceCaptureFixture, CaptureKeepsLightDisplayedResultClamps) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* material = LoadUnlit("Bright.mat", Vec3(3.0f, 0.5f, 0.25f));
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	EXPECT_FALSE(RenderSystem::ReadbackViewTargetRadiance(stage.target, pixels, size));

	ViewDesc desc = QuadView();
	desc.radianceCapture = true;
	RenderSystem::SubmitView(stage.target, desc);
	RenderSystem::RenderAll();
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(stage.target, pixels, size));
	EXPECT_EQ(CenterTexel(pixels, size).x, 255.0f);

	ASSERT_TRUE(RenderSystem::ReadbackViewTargetRadiance(stage.target, pixels, size));
	ASSERT_EQ(size, (Vec2I{64, 64}));
	const Vec3 kept = RadianceTexel(pixels, size, 32, 32);
	EXPECT_NEAR(kept.x, 3.0f, 0.01f);
	EXPECT_NEAR(kept.y, 0.5f, 0.01f);
	EXPECT_NEAR(kept.z, 0.25f, 0.01f);

	TearDownStage(stage);
}

TEST_F(RadianceCaptureFixture, CaptureIgnoresRenderScale) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	const Guid id = AssetDatabase::CreateAsset(Path("project://Half.slprofile"), StringHash("GraphicsProfile"));
	ASSERT_TRUE(id.IsValid());
	auto* profile = static_cast<GraphicsProfile*>(AssetDatabase::LoadAssetByGuid(id));
	ASSERT_NE(profile, nullptr);
	profile->renderScale = 0.5f;
	GraphicsSettings* settings = Settings::DeclareCategory<GraphicsSettings>("Graphics", SettingScope::Project, true);
	ASSERT_NE(settings, nullptr);
	settings->qualityProfiles = id.ToString();
	settings->qualityLevel = 0;
	Settings::NotifyChanged("Graphics", "qualityLevel");

	MaterialAsset* material = LoadUnlit("Flat.mat", Vec3(0.5f, 0.5f, 0.5f));
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));

	ViewDesc desc = QuadView();
	desc.radianceCapture = true;
	RenderSystem::SubmitView(stage.target, desc);
	RenderSystem::RenderAll();
	ASSERT_FLOAT_EQ(RenderSystem::GetActiveProfile()->renderScale, 0.5f);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTargetRadiance(stage.target, pixels, size));
	EXPECT_EQ(size, (Vec2I{64, 64}));
	EXPECT_NEAR(RadianceTexel(pixels, size, 32, 32).x, 0.5f, 0.01f);

	TearDownStage(stage);
	AssetDatabase::UnloadAsset(profile);
}

TEST_F(RadianceCaptureFixture, FaceViewMatchesStorage) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* back = LoadUnlit("Back.mat", Vec3(0.0f, 0.0f, 1.0f));
	MaterialAsset* mark = LoadUnlit("Mark.mat", Vec3(1.0f, 0.0f, 0.0f));
	ASSERT_NE(back, nullptr);
	ASSERT_NE(mark, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(back, stage));
	stage.sun->SetEnabled(false);
	const Vec3 markAt(0.25f, 0.25f, 0.01f);
	ASSERT_TRUE(AddMarker(stage, mark, markAt, 0.1f));

	constexpr u32 face = 5;
	const Vec3 eye(0.0f, 0.0f, 1.0f);
	ViewDesc desc;
	desc.view = CubeFaceView(face, eye);
	desc.projection = Mat4::PerspectiveReverseZ(90.0f, 1.0f, 0.05f, 100.0f);
	desc.cameraPosition = eye;
	desc.radianceCapture = true;
	RenderSystem::SubmitView(stage.target, desc);
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTargetRadiance(stage.target, pixels, size));

	f32 sumX = 0.0f;
	f32 sumY = 0.0f;
	u32 count = 0;
	for (i32 y = 0; y < size.y; ++y) {
		for (i32 x = 0; x < size.x; ++x) {
			const Vec3 texel = RadianceTexel(pixels, size, x, y);
			if (texel.x > 0.5f && texel.z < 0.5f) {
				sumX += static_cast<f32>(x) + 0.5f;
				sumY += static_cast<f32>(y) + 0.5f;
				++count;
			}
		}
	}
	ASSERT_GT(count, 0u);
	const f32 u = sumX / static_cast<f32>(count) / static_cast<f32>(size.x);
	const f32 v = sumY / static_cast<f32>(count) / static_cast<f32>(size.y);
	const Vec3 sampled = GraphicsInternal::CubeTexelDirection(face, u, v);
	const Vec3 toMark = (markAt - eye).Normalized();
	EXPECT_GT(Vec3::Dot(sampled, toMark), 0.999f);
	const Vec3 mirrored = RadianceTexel(pixels, size, static_cast<i32>((1.0f - u) * size.x), static_cast<i32>(v * size.y));
	EXPECT_GT(mirrored.z, 0.5f);

	TearDownStage(stage);
}
