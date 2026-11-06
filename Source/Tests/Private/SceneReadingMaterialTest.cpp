#include "TestsPCH.h"

#include "ShadingHarness.h"
#include "ZoneProbe.h"

#include "Graphics/GraphicsSettings.h"

#include "Shalltear/Core/Graphics.h"
#include "Shalltear/Core/Math/HalfFloat.h"
#include "Shalltear/Core/Settings.h"
#include "Shalltear/Core/Window/WindowSystem.h"

#include <filesystem>

using namespace Shalltear;

namespace {

	using namespace ShadingTest;

	constexpr const char* FADE_SHADER =
		"#pragma shalltear_template unlit\n"
		"#pragma shalltear_blend alpha\n"
		"#pragma shalltear_depth test_only\n"
		"#pragma shalltear_cull off\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; float fadeDistance; };\n"
		"[[vk::binding(0, 2)]] Texture2D sltSceneDepth;\n"
		"void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
		"\tfloat raw = sltSceneDepth.Load(int3(int2(input.positionSS), 0)).r;\n"
		"\tfloat4 scene = mul(ShalltearInverseProjectionMatrix(), float4(0.0, 0.0, raw, 1.0));\n"
		"\tfloat sceneEye = abs(scene.z / scene.w);\n"
		"\tfloat eye = abs(mul(ShalltearViewMatrix(), float4(input.positionWS, 1.0)).z);\n"
		"\tsurface.baseColor = tint.rgb;\n"
		"\tsurface.alpha = saturate((sceneEye - eye) / fadeDistance);\n"
		"}\n";

	constexpr const char* COLOR_SHADER =
		"#pragma shalltear_template unlit\n"
		"#pragma shalltear_blend alpha\n"
		"#pragma shalltear_depth test_only\n"
		"#pragma shalltear_cull off\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
		"[[vk::binding(0, 2)]] Texture2D sltSceneColor;\n"
		"void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
		"\tfloat3 scene = sltSceneColor.Load(int3(int2(input.positionSS), 0)).rgb;\n"
		"\tsurface.baseColor = scene.gbr * tint.rgb;\n"
		"\tsurface.alpha = 1.0;\n"
		"}\n";

	constexpr const char* EVERY_IMAGE_SHADER =
		"#pragma shalltear_template unlit\n"
		"#pragma shalltear_blend alpha\n"
		"#pragma shalltear_depth test_only\n"
		"#pragma shalltear_cull off\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
		"[[vk::binding(0, 2)]] Texture2D sltSceneColor;\n"
		"[[vk::binding(1, 2)]] Texture2D sltSceneDepth;\n"
		"[[vk::binding(2, 2)]] Texture2D sltSceneNormals;\n"
		"void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
		"\tint3 texel = int3(int2(input.positionSS), 0);\n"
		"\tsurface.baseColor = sltSceneNormals.Load(texel).rgb;\n"
		"\tif (tint.a < 0.0) {\n"
		"\t\tsurface.baseColor = sltSceneColor.Load(texel).rgb + sltSceneDepth.Load(texel).rrr;\n"
		"\t}\n"
		"\tsurface.alpha = 1.0;\n"
		"}\n";

	constexpr const char* FADE_BODY = "\t\"floats\": { \"fadeDistance\": 1 },\n\t\"colors\": { \"tint\": [1, 1, 1, 1] }";
	constexpr const char* COLOR_BODY = "\t\"colors\": { \"tint\": [1, 1, 1, 1] }";

	class SceneReadingMaterialFixture : public MaterialReadbackFixture {
	protected:
		str ProjectSettingsDocument() const override { return "[Graphics]\n"; }

		bool Ready() { return RenderSystem::IsRenderingAvailable() && CompilerAvailable(); }

		MaterialAsset* Unlit(const char* file, const Color& color, bool blended = false) {
			Asset* unlit = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
			if (!unlit) {
				return nullptr;
			}
			str body = blended ? "\t\"blend\": \"alpha\",\n" : "";
			body += "\t\"colors\": { \"baseColor\": [" + std::to_string(color.r) + ", " + std::to_string(color.g) + ", " +
					std::to_string(color.b) + ", " + std::to_string(color.a) + "] }";
			return LoadMaterial(file, unlit->assetId, body);
		}

		MaterialAsset* Reader(const char* materialFile, const char* shaderFile, const char* source, const str& body) {
			WriteProjectFile(shaderFile, source);
			Asset* shader = AssetDatabase::LoadAsset(Path(str("project://") + shaderFile));
			return shader ? LoadMaterial(materialFile, shader->assetId, body) : nullptr;
		}

		Handle<SceneObject> AddQuadAt(Handle<Scene> scene, MaterialAsset* material, const Vec3& position) {
			Asset* quadMesh = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Quad)));
			if (!quadMesh || !material) {
				return {};
			}
			Handle<SceneObject> object = SceneSystem::CreateObject("Quad", scene);
			object->SetLocalPosition(position);
			Handle<MeshRenderer> renderer = object->AddComponent<MeshRenderer>();
			renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(quadMesh)));
			renderer->SetMaterial(0, Handle<MaterialAsset>(material));
			SceneSystem::EndFrame();
			return object;
		}

		bool UseSamples(i32 samples) {
			std::filesystem::create_directories(_root / "Assets");
			const Guid profileId = AssetDatabase::CreateAsset(Path("project://Assets/Msaa.slprofile"), StringHash("GraphicsProfile"));
			auto* profile = static_cast<GraphicsProfile*>(AssetDatabase::LoadAssetByGuid(profileId));
			GraphicsSettings* settings = Settings::DeclareCategory<GraphicsSettings>("Graphics", SettingScope::Project, true);
			if (!profile || !settings) {
				return false;
			}
			profile->msaa = samples;
			settings->qualityProfiles = profileId.ToString();
			settings->qualityLevel = 0;
			Settings::NotifyChanged("Graphics", "qualityLevel");
			RenderSystem::RenderAll();
			return RenderSystem::GetSampleCount() == static_cast<u32>(samples);
		}

		Vec3 Center(const ShadingStage& stage) {
			Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
			Vec2I size{};
			if (!RenderStage(stage, pixels, size) || pixels.empty()) {
				ADD_FAILURE() << "frame readback failed";
				return {};
			}
			return CenterTexel(pixels, size);
		}

		u32 DrawCalls(const ShadingStage& stage) {
			Center(stage);
			const GraphicsStats stats = RenderSystem::GetStats();
			return stats.drawCalls + stats.drawCallsSaved;
		}

		void ExpectOpaqueImageTurned(const ShadingStage& stage) {
			const Vec3 texel = Center(stage);
			EXPECT_LT(texel.x, 3.0f);
			EXPECT_LT(texel.y, 3.0f);
			EXPECT_GT(texel.z, 252.0f);
		}

		void BuildTurnStage(ShadingStage& stage) {
			ASSERT_TRUE(BuildStage(Unlit("Wall.mat", Color(1.0f, 0.0f, 0.0f, 1.0f)), stage));
			ASSERT_TRUE(AddQuadAt(stage.scene, Unlit("Veil.mat", Color(0.0f, 1.0f, 0.0f, 0.5f), true), Vec3(0.0f, 0.0f, 0.25f)).IsValid());
			ASSERT_TRUE(AddQuadAt(stage.scene, Reader("Turn.mat", "Turn.hlsl", COLOR_SHADER, COLOR_BODY), Vec3(0.0f, 0.0f, 0.5f)).IsValid());
		}
	};

} // anonymous namespace

TEST_F(SceneReadingMaterialFixture, DepthFadeReadsGapToWall) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(Unlit("Wall.mat", Color(0.0f, 0.0f, 0.0f, 1.0f)), stage));
	MaterialAsset* fade = Reader("Fade.mat", "Fade.hlsl", FADE_SHADER, FADE_BODY);
	ASSERT_NE(fade, nullptr);
	Handle<SceneObject> reader = AddQuadAt(stage.scene, fade, Vec3(0.0f, 0.0f, 0.25f));
	ASSERT_TRUE(reader.IsValid());

	for (const f32 gap : {0.25f, 0.5f}) {
		reader->SetLocalPosition(Vec3(0.0f, 0.0f, gap));
		const Vec3 texel = Center(stage);
		const f32 expected = ExpectedTexel(Vec3(gap, gap, gap)).x;
		EXPECT_NEAR(texel.x, expected, 2.0f) << gap;
		EXPECT_NEAR(texel.y, expected, 2.0f) << gap;
	}
	TearDownStage(stage);
}

TEST_F(SceneReadingMaterialFixture, SceneColorIsOpaqueImage) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	ShadingStage stage;
	BuildTurnStage(stage);
	ExpectOpaqueImageTurned(stage);
	TearDownStage(stage);
}

TEST_F(SceneReadingMaterialFixture, SceneColorAt4xIsResolved) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	if (!UseSamples(4)) {
		GTEST_SKIP() << "device offers no 4x MSAA";
	}
	ShadingStage stage;
	BuildTurnStage(stage);
	ExpectOpaqueImageTurned(stage);

	MaterialAsset* fade = Reader("Fade.mat", "Fade.hlsl", FADE_SHADER, FADE_BODY);
	ASSERT_TRUE(AddQuadAt(stage.scene, fade, Vec3(0.0f, 0.0f, 0.75f)).IsValid());
	const Vec3 texel = Center(stage);
	const f32 expected = ExpectedTexel(Vec3(0.75f, 0.75f, 0.75f)).x;
	EXPECT_NEAR(texel.x, expected, 2.0f);
	EXPECT_NEAR(texel.y, expected, 2.0f);
	EXPECT_NEAR(texel.z, ExpectedTexel(Vec3(1.0f, 1.0f, 1.0f)).x, 2.0f);
	TearDownStage(stage);
}

TEST_F(SceneReadingMaterialFixture, ReadersShareOneCopy) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(Unlit("Wall.mat", Color(1.0f, 0.0f, 0.0f, 1.0f)), stage));
	MaterialAsset* veil = Unlit("Veil.mat", Color(0.0f, 1.0f, 0.0f, 0.5f), true);
	MaterialAsset* turn = Reader("Turn.mat", "Turn.hlsl", COLOR_SHADER, COLOR_BODY);
	ASSERT_NE(turn, nullptr);
	Handle<SceneObject> first = AddQuadAt(stage.scene, veil, Vec3(0.0f, 0.0f, 0.25f));
	Handle<SceneObject> second = AddQuadAt(stage.scene, veil, Vec3(0.0f, 0.0f, 0.5f));
	ASSERT_TRUE(first.IsValid() && second.IsValid());

	const u32 plain = DrawCalls(stage);
	first->GetComponent<MeshRenderer>()->SetMaterial(0, Handle<MaterialAsset>(turn));
	EXPECT_EQ(DrawCalls(stage), plain + 1);
	second->GetComponent<MeshRenderer>()->SetMaterial(0, Handle<MaterialAsset>(turn));
	EXPECT_EQ(DrawCalls(stage), plain + 1);
	ExpectOpaqueImageTurned(stage);
	first->GetComponent<MeshRenderer>()->SetMaterial(0, Handle<MaterialAsset>(veil));
	second->GetComponent<MeshRenderer>()->SetMaterial(0, Handle<MaterialAsset>(veil));
	EXPECT_EQ(DrawCalls(stage), plain);
	TearDownStage(stage);
}

TEST_F(SceneReadingMaterialFixture, CulledReaderTakesNoCopy) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(Unlit("Wall.mat", Color(1.0f, 0.0f, 0.0f, 1.0f)), stage));
	MaterialAsset* veil = Unlit("Veil.mat", Color(0.0f, 1.0f, 0.0f, 0.5f), true);
	MaterialAsset* turn = Reader("Turn.mat", "Turn.hlsl", COLOR_SHADER, COLOR_BODY);
	ASSERT_NE(turn, nullptr);
	Handle<SceneObject> quad = AddQuadAt(stage.scene, veil, Vec3(50.0f, 0.0f, 0.5f));
	ASSERT_TRUE(quad.IsValid());

	const u32 plain = DrawCalls(stage);
	quad->GetComponent<MeshRenderer>()->SetMaterial(0, Handle<MaterialAsset>(turn));
	EXPECT_EQ(DrawCalls(stage), plain);
	quad->SetLocalPosition(Vec3(0.0f, 0.0f, 0.5f));
	EXPECT_EQ(DrawCalls(stage), plain + 2);
	TearDownStage(stage);
}

TEST_F(SceneReadingMaterialFixture, DepthReaderRunsPrepass) {
#if defined(SHALLTEAR_CONFIG_RELEASE)
	GTEST_SKIP() << "zones compiled out in Release";
#else
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(Unlit("Wall.mat", Color(0.0f, 0.0f, 0.0f, 1.0f)), stage));
	MaterialAsset* veil = Unlit("Veil.mat", Color(0.0f, 1.0f, 0.0f, 0.5f), true);
	MaterialAsset* fade = Reader("Fade.mat", "Fade.hlsl", FADE_SHADER, FADE_BODY);
	ASSERT_NE(fade, nullptr);
	Handle<SceneObject> quad = AddQuadAt(stage.scene, veil, Vec3(0.0f, 0.0f, 0.5f));
	ASSERT_TRUE(quad.IsValid());

	const auto prepasses = [&] {
		Center(stage);
		ZoneProbe::ZoneScope zones;
		Center(stage);
		Profiler::NextFrame();
		return ZoneProbe::ZoneCalls("Render.View.Prepass");
	};
	EXPECT_EQ(prepasses(), 0u);
	quad->GetComponent<MeshRenderer>()->SetMaterial(0, Handle<MaterialAsset>(fade));
	EXPECT_EQ(prepasses(), 1u);
	quad->SetLocalPosition(Vec3(50.0f, 0.0f, 0.5f));
	EXPECT_EQ(prepasses(), 0u);
	quad->SetLocalPosition(Vec3(0.0f, 0.0f, 0.5f));
	EXPECT_EQ(prepasses(), 1u);
	quad->GetComponent<MeshRenderer>()->SetMaterial(0, Handle<MaterialAsset>(veil));
	EXPECT_EQ(prepasses(), 0u);
	TearDownStage(stage);
#endif
}

TEST_F(SceneReadingMaterialFixture, ReaderOfEveryImageSeesNormals) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(Unlit("Wall.mat", Color(1.0f, 0.0f, 0.0f, 1.0f)), stage));
	ASSERT_TRUE(AddQuadAt(stage.scene, Reader("Every.mat", "Every.hlsl", EVERY_IMAGE_SHADER, COLOR_BODY), Vec3(0.0f, 0.0f, 0.5f)).IsValid());

	const Vec3 normal = stage.quad->TransformDirection(Vec3(0.0f, 0.0f, 1.0f)).Normalized();
	const Vec3 expected = ExpectedTexel(normal * 0.5f + Vec3(0.5f, 0.5f, 0.5f));
	const Vec3 texel = Center(stage);
	EXPECT_NEAR(texel.x, expected.x, 3.0f);
	EXPECT_NEAR(texel.y, expected.y, 3.0f);
	EXPECT_NEAR(texel.z, expected.z, 3.0f);
	TearDownStage(stage);
}

TEST_F(SceneReadingMaterialFixture, WindowCameraReaderSeesOpaqueImage) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	WindowDesc windowDesc;
	windowDesc.title = "SceneReadingMaterial";
	windowDesc.clientSize = {64, 64};
	windowDesc.flags = WindowFlags::Hidden;
	Handle<Window> window = WindowSystem::Create(windowDesc);
	ASSERT_TRUE(window.IsValid());

	Handle<Scene> scene = SceneSystem::CreateScene("WindowReader");
	ASSERT_TRUE(AddQuadAt(scene, Unlit("Wall.mat", Color(1.0f, 0.0f, 0.0f, 1.0f)), Vec3(0.0f, 0.0f, 0.0f)).IsValid());
	ASSERT_TRUE(AddQuadAt(scene, Unlit("Veil.mat", Color(0.0f, 1.0f, 0.0f, 0.5f), true), Vec3(0.0f, 0.0f, 0.25f)).IsValid());
	ASSERT_TRUE(AddQuadAt(scene, Reader("Turn.mat", "Turn.hlsl", COLOR_SHADER, COLOR_BODY), Vec3(0.0f, 0.0f, 0.5f)).IsValid());
	Handle<SceneObject> cameraObject = SceneSystem::CreateObject("Camera", scene);
	cameraObject->SetLocalPosition(Vec3(0.0f, 0.0f, 3.0f));
	cameraObject->SetLocalRotation(Quat::AngleAxis(180.0f, Vec3::Up()));
	Handle<Camera> camera = cameraObject->AddComponent<Camera>();
	camera->SetTargetWindow(window);
	camera->SetClearColor(Color::Black());
	SceneSystem::EndFrame();

	for (u32 frame = 0; frame < 3; ++frame) {
		WindowSystem::PumpEvents();
		RenderSystem::RenderAll();
	}
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackWindowSceneColor(window, pixels, size));
	ASSERT_GT(size.x, 0);
	const u16* halves = reinterpret_cast<const u16*>(pixels.data());
	const size_t center = (static_cast<size_t>(size.y / 2) * size.x + size.x / 2) * 4;
	EXPECT_NEAR(Math::HalfToFloat(halves[center]), 0.0f, 0.01f);
	EXPECT_NEAR(Math::HalfToFloat(halves[center + 1]), 0.0f, 0.01f);
	EXPECT_NEAR(Math::HalfToFloat(halves[center + 2]), 1.0f, 0.01f);

	SceneSystem::UnloadScene(scene);
	SceneSystem::EndFrame();
}

TEST_F(SceneReadingMaterialFixture, ClippedReaderIsPicked) {
	if (!Ready()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(Unlit("Wall.mat", Color(0.0f, 0.0f, 0.0f, 1.0f)), stage));
	MaterialAsset* fade = Reader("Fade.mat", "Fade.hlsl", FADE_SHADER, str(FADE_BODY) + ",\n\t\"alphaClip\": 0.1");
	ASSERT_NE(fade, nullptr);
	Handle<SceneObject> reader = AddQuadAt(stage.scene, fade, Vec3(0.0f, 0.0f, 0.5f));
	ASSERT_TRUE(reader.IsValid());

	const ViewPick pick{Vec2I(32, 32), 7};
	ViewDesc desc = QuadView();
	desc.pick = &pick;
	RenderSystem::SubmitView(stage.target, desc);
	RenderSystem::RenderAll();
	PickAnswer answer;
	for (u32 frames = 0; !RenderSystem::TakePickAnswer(stage.target, answer) && frames < 2 * RHI::FRAMES_IN_FLIGHT; ++frames) {
		Center(stage);
	}
	EXPECT_EQ(answer.tag, 7u);
	EXPECT_EQ(answer.object, reader);
	TearDownStage(stage);
}
