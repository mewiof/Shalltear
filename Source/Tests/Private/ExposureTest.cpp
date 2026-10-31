#include "TestsPCH.h"

#include "ShadingHarness.h"

#include "Graphics/EnvironmentIBL.h"
#include "Graphics/Exposure.h"
#include "Shalltear/Core/Graphics/Camera.h"
#include "Shalltear/Core/Graphics/Environment.h"
#include "Shalltear/Core/Window/WindowSystem.h"

#include <cmath>

using namespace Shalltear;
using ShadingTest::CapturedIrradiance;
using ShadingTest::CenterTexel;
using ShadingTest::ExpectedTexel;
using ShadingTest::MaterialReadbackFixture;
using ShadingTest::MIRROR_BODY;
using ShadingTest::MirrorOf;
using ShadingTest::QuadView;
using ShadingTest::RadianceTexel;
using ShadingTest::ShadingStage;

namespace {

	namespace GI = Shalltear::GraphicsInternal;

	f32 ExposureAt(f32 ev100) { return 1.0f / (1.2f * std::exp2(ev100)); }

	const Vec3 SURFACE{0.20f, 0.10f, 0.05f};
	const Vec3 SKY{0.06f, 0.12f, 0.24f};

	constexpr f32 TEXEL_TOLERANCE = 1.5f;

	void ExpectTexelNear(const Vec3& actual, const Vec3& expected, const char* what) {
		EXPECT_NEAR(actual.x, expected.x, TEXEL_TOLERANCE) << what;
		EXPECT_NEAR(actual.y, expected.y, TEXEL_TOLERANCE) << what;
		EXPECT_NEAR(actual.z, expected.z, TEXEL_TOLERANCE) << what;
	}

	Vec3 Texel(const Vector<u8>& pixels, Vec2I size, i32 x, i32 y) {
		const size_t index = (static_cast<size_t>(y) * size.x + x) * 4;
		return Vec3(static_cast<f32>(pixels[index]), static_cast<f32>(pixels[index + 1]), static_cast<f32>(pixels[index + 2]));
	}

	Guid ShaderGuid(const char* path) {
		Asset* shader = AssetDatabase::LoadAsset(Path(path));
		return shader ? shader->assetId : Guid{};
	}

	Handle<Environment> AddEnvironment(ShadingStage& stage, f32 priority = 0.0f, f32 weight = 1.0f) {
		Handle<SceneObject> object = SceneSystem::CreateObject("Environment", stage.scene);
		Handle<Environment> environment = object->AddComponent<Environment>();
		environment->SetSkyMode(SkyMode::None);
		environment->SetAmbientMode(AmbientMode::Color);
		environment->SetAmbientColor(Color(0.0f, 0.0f, 0.0f, 1.0f));
		environment->SetReflectionIntensity(0.0f);
		environment->SetPriority(priority);
		environment->SetWeight(weight);
		SceneSystem::EndFrame();
		return environment;
	}

	constexpr const char* ATTENUATION_ONLY_SHADER =
		"#define SHALLTEAR_CUSTOM_LIGHTING\n"
		"#pragma shalltear_template lit\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 baseColor; };\n"
		"float3 ShalltearLightingFunction(ShalltearSurfaceInput input, ShalltearSurface surface, float3 normalWS, ShalltearLightSample light) {\n"
		"\treturn light.color * light.attenuation;\n"
		"}\n"
		"void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
		"\tsurface.baseColor = baseColor.rgb;\n"
		"\tsurface.metallic = 0.0;\n"
		"}\n";

	constexpr const char* UNIFORM_SKY_SHADER =
		"#pragma shalltear_template sky\n"
		"#pragma shalltear_default tint 1 1 1 1\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
		"void ShalltearSkyFunction(ShalltearSkyInput input, inout ShalltearSkyOutput sky) {\n"
		"\tsky.color = tint.rgb;\n"
		"}\n";

	class ExposureReadbackFixture : public MaterialReadbackFixture {
	protected:
		MaterialAsset* LoadUnlit(const char* fileName, const Vec3& color, f32 alpha = 1.0f, const char* blend = nullptr) {
			str body = std::format("\t\"colors\": {{ \"baseColor\": [{}, {}, {}, {}] }}", color.x, color.y, color.z, alpha);
			if (blend) {
				body += std::format(",\n\t\"blend\": \"{}\"", blend);
			}
			return LoadMaterial(fileName, ShaderGuid("engine://Shaders/Unlit.hlsl"), body);
		}

		bool Render(const ShadingStage& stage, const ViewDesc& desc, Vector<u8>& pixels, Vec2I& size) {
			RenderSystem::SubmitView(stage.target, desc);
			RenderSystem::RenderAll();
			return RenderSystem::ReadbackViewTarget(stage.target, pixels, size);
		}

		bool RenderCenter(const ShadingStage& stage, const ViewDesc& desc, Vec3& out) {
			Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
			Vec2I size{};
			if (!Render(stage, desc, pixels, size)) {
				return false;
			}
			out = CenterTexel(pixels, size);
			return true;
		}
	};

	class PhysicalExposureFixture : public ExposureReadbackFixture {
	protected:
		str ProjectSettingsDocument() const override { return "[Graphics]\nphysicalLightUnits = true\n"; }
	};

} // anonymous namespace

TEST(ExposureTest, PhysicalCameraIsLensRelationAtDefaults) {
	EXPECT_NEAR(GI::PhysicalCameraEV100(16.0f, 1.0f / 200.0f, 200.0f), std::log2(25600.0f), 1e-4f);

	Camera camera;
	EXPECT_EQ(camera.GetIso(), static_cast<i32>(GI::DEFAULT_CAMERA_ISO));
	EXPECT_FLOAT_EQ(camera.GetShutterSpeed(), GI::DEFAULT_CAMERA_SHUTTER_SPEED);
	EXPECT_FLOAT_EQ(camera.GetAperture(), GI::DEFAULT_CAMERA_APERTURE);

	const f32 base = GI::PhysicalCameraEV100(8.0f, 1.0f / 125.0f, 100.0f);
	EXPECT_NEAR(GI::PhysicalCameraEV100(8.0f, 1.0f / 125.0f, 200.0f), base - 1.0f, 1e-4f);
	EXPECT_NEAR(GI::PhysicalCameraEV100(8.0f, 2.0f / 125.0f, 100.0f), base - 1.0f, 1e-4f);
	EXPECT_NEAR(GI::PhysicalCameraEV100(8.0f / std::sqrt(2.0f), 1.0f / 125.0f, 100.0f), base - 1.0f, 1e-4f);
}

TEST(ExposureTest, AuthoredCameraCannotYieldInfiniteExposure) {
	EXPECT_TRUE(std::isfinite(GI::PhysicalCameraEV100(16.0f, 0.0f, 0.0f)));
	EXPECT_FLOAT_EQ(GI::PhysicalCameraEV100(100.0f, 0.01f, 100.0f), GI::PhysicalCameraEV100(32.0f, 0.01f, 100.0f));
}

TEST(ExposureTest, ViewAtDefaultsIsExposedAtOne) {
	EXPECT_EQ(GI::BlendExposure({}, {}, 1.0f, false, 0.0f), 1.0f);
	const GI::ExposureLayerInput layers[2]{};
	const f32 weights[2]{0.3f, 0.45f};
	EXPECT_EQ(GI::BlendExposure(layers, weights, 0.25f, false, 14.0f), 1.0f);
}

TEST(ExposureTest, ArbitraryUnitsReadCompensationOnly) {
	GI::ExposureLayerInput fixed;
	fixed.fixedEV100 = 3.0f;
	fixed.compensation = 1.5f;
	EXPECT_FLOAT_EQ(GI::ExposureStops(fixed, false, 0.0f), 1.5f);
	GI::ExposureLayerInput physical;
	physical.mode = ExposureMode::PhysicalCamera;
	physical.compensation = -2.0f;
	EXPECT_FLOAT_EQ(GI::ExposureStops(physical, false, 7.0f), -2.0f);
	EXPECT_FLOAT_EQ(GI::ExposureStops(physical, false, 15.0f), -2.0f);

	const f32 one[1]{1.0f};
	EXPECT_FLOAT_EQ(GI::BlendExposure({&fixed, 1}, one, 0.0f, false, 0.0f), std::exp2(1.5f));
}

TEST(ExposureTest, PhysicalUnitsUseModeEV100) {
	const f32 one[1]{1.0f};
	GI::ExposureLayerInput fixed;
	fixed.fixedEV100 = 12.0f;
	EXPECT_NEAR(GI::BlendExposure({&fixed, 1}, one, 0.0f, true, 0.0f), ExposureAt(12.0f), ExposureAt(12.0f) * 1e-4f);
	fixed.compensation = 1.0f;
	EXPECT_NEAR(GI::BlendExposure({&fixed, 1}, one, 0.0f, true, 0.0f), 2.0f * ExposureAt(12.0f), ExposureAt(12.0f) * 2e-4f);

	GI::ExposureLayerInput camera;
	camera.mode = ExposureMode::PhysicalCamera;
	camera.fixedEV100 = 2.0f;
	EXPECT_NEAR(GI::BlendExposure({&camera, 1}, one, 0.0f, true, 10.0f), ExposureAt(10.0f), ExposureAt(10.0f) * 1e-4f);

	EXPECT_NEAR(GI::BlendExposure({}, {}, 1.0f, true, 0.0f), ExposureAt(16.0f), ExposureAt(16.0f) * 1e-4f);
}

TEST(ExposureTest, LayersBlendInStops) {
	GI::ExposureLayerInput layers[2];
	layers[0].fixedEV100 = 14.0f;
	layers[1].fixedEV100 = 8.0f;
	const f32 halves[2]{0.5f, 0.5f};
	const f32 blended = GI::BlendExposure(layers, halves, 0.0f, true, 0.0f);
	EXPECT_NEAR(blended, ExposureAt(11.0f), ExposureAt(11.0f) * 1e-4f);
	const f32 meanOfMultipliers = 0.5f * (ExposureAt(14.0f) + ExposureAt(8.0f));
	EXPECT_GT(std::fabs(std::log2(meanOfMultipliers / blended)), 2.0f);

	const f32 half[1]{0.5f};
	EXPECT_NEAR(GI::BlendExposure({&layers[1], 1}, half, 0.5f, true, 0.0f), ExposureAt(12.0f), ExposureAt(12.0f) * 1e-4f);
	GI::ExposureLayerInput bright;
	bright.compensation = 2.0f;
	EXPECT_FLOAT_EQ(GI::BlendExposure({&bright, 1}, half, 0.5f, false, 0.0f), 2.0f);
}

TEST(ExposureTest, DefaultExposureFitsSunlight) {
	const f32 radiance = 100000.0f * 0.8f / Math::PI * GI::BlendExposure({}, {}, 1.0f, true, 0.0f);
	EXPECT_GT(radiance, 0.15f);
	EXPECT_LT(radiance, 0.6f);
}

TEST(ExposureTest, BakeIsStoredAtReferenceExposure) {
	EXPECT_EQ(GI::ReferenceExposure(false), 1.0f);
	EXPECT_NEAR(GI::ReferenceExposure(true), ExposureAt(16.0f), ExposureAt(16.0f) * 1e-5f);
}

TEST_F(ExposureReadbackFixture, CompensationScalesEveryWriterAndNotLines) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* material = LoadUnlit("Surface.mat", SURFACE);
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	Handle<Environment> environment = AddEnvironment(stage);
	environment->SetSkyMode(SkyMode::Color);
	environment->SetSkyColor(Color(SKY.x, SKY.y, SKY.z, 1.0f));

	const DebugLine line{Vec3(-2.0f, 1.2f, 0.0f), Vec3(2.0f, 1.2f, 0.0f), Color32(250, 30, 30, 255)};
	ViewDesc desc = QuadView();
	desc.lines = {&line, 1};
	auto brightestRedRow = [](const Vector<u8>& pixels, Vec2I size, i32& row) {
		f32 best = -1.0f;
		for (i32 y = 0; y < size.y / 3; ++y) {
			const f32 red = Texel(pixels, size, size.x / 2, y).x;
			if (red > best) {
				best = red;
				row = y;
			}
		}
	};

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	struct Arm {
		f32 stops;
		const char* what;
	};
	i32 lineRow = -1;
	Vec3 lineTexel{};
	for (const Arm arm : {Arm{0.0f, "at compensation 0"}, Arm{1.0f, "a stop over"}, Arm{-1.0f, "a stop under"}}) {
		SCOPED_TRACE(arm.what);
		environment->SetExposureCompensation(arm.stops);
		ASSERT_TRUE(Render(stage, desc, pixels, size));
		const f32 scale = std::exp2(arm.stops);
		ExpectTexelNear(CenterTexel(pixels, size), ExpectedTexel(SURFACE * scale), "the unlit surface");
		ExpectTexelNear(Texel(pixels, size, 2, size.y - 3), ExpectedTexel(SKY * scale), "the sky");
		i32 row = -1;
		brightestRedRow(pixels, size, row);
		ASSERT_GE(row, 0);
		const Vec3 drawn = Texel(pixels, size, size.x / 2, row);
		EXPECT_GT(drawn.x, 200.0f);
		if (lineRow < 0) {
			lineRow = row;
			lineTexel = drawn;
		} else {
			EXPECT_EQ(row, lineRow);
			EXPECT_EQ(drawn.x, lineTexel.x);
			EXPECT_EQ(drawn.y, lineTexel.y);
		}
	}

	TearDownStage(stage);
}

TEST_F(ExposureReadbackFixture, MultiplyIsFilterAndIsNotExposed) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	const Vec3 behind{0.50f, 0.40f, 0.30f};
	const Vec3 surface{0.50f, 0.60f, 0.70f};
	const f32 alpha = 0.6f;
	struct BlendCase {
		const char* blend;
		Vec3 expected;
	};
	const BlendCase cases[] = {
		{"multiply", Vec3(behind.x * surface.x, behind.y * surface.y, behind.z * surface.z)},
		{"alpha", surface * (2.0f * alpha) + behind * (1.0f - alpha)},
	};
	for (const BlendCase& blendCase : cases) {
		SCOPED_TRACE(blendCase.blend);
		MaterialAsset* material = LoadUnlit(std::format("Surface_{}.mat", blendCase.blend).c_str(), surface, alpha, blendCase.blend);
		ASSERT_NE(material, nullptr);
		ASSERT_TRUE(material->blend.has_value());
		ShadingStage stage;
		ASSERT_TRUE(BuildStage(material, stage));
		AddEnvironment(stage)->SetExposureCompensation(1.0f);
		ViewDesc desc = QuadView();
		desc.clearColor = Color(behind.x, behind.y, behind.z, 1.0f);
		Vec3 texel;
		ASSERT_TRUE(RenderCenter(stage, desc, texel));
		ExpectTexelNear(texel, ExpectedTexel(blendCase.expected), blendCase.blend);
		TearDownStage(stage);
	}
}

TEST_F(ExposureReadbackFixture, ValueViewsNotExposed) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* material = LoadUnlit("Surface.mat", SURFACE);
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	AddEnvironment(stage)->SetExposureCompensation(1.0f);

	Vec3 texel;
	ASSERT_TRUE(RenderCenter(stage, QuadView(), texel));
	ExpectTexelNear(texel, ExpectedTexel(SURFACE * 2.0f), "the ordinary view is a stop over");

	ViewDesc values = QuadView();
	values.tonemap = false;
	ASSERT_TRUE(RenderCenter(stage, values, texel));
	ExpectTexelNear(texel, ExpectedTexel(SURFACE), "a view of values was exposed");

	ViewDesc capture = QuadView();
	capture.radianceCapture = true;
	RenderSystem::SubmitView(stage.target, capture);
	RenderSystem::RenderAll();
	Vector<u8> radiance{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTargetRadiance(stage.target, radiance, size));
	const Vec3 captured = RadianceTexel(radiance, size, size.x / 2, size.y / 2);
	EXPECT_NEAR(captured.x, SURFACE.x, 1e-3f);
	EXPECT_NEAR(captured.y, SURFACE.y, 1e-3f);
	EXPECT_NEAR(captured.z, SURFACE.z, 1e-3f);

	TearDownStage(stage);
}

TEST_F(ExposureReadbackFixture, TwoLayersBlendInStops) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* material = LoadUnlit("Surface.mat", SURFACE);
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	AddEnvironment(stage, 0.0f);
	Handle<Environment> upper = AddEnvironment(stage, 1.0f, 0.5f);
	upper->SetExposureCompensation(2.0f);

	Vec3 texel;
	ASSERT_TRUE(RenderCenter(stage, QuadView(), texel));
	ExpectTexelNear(texel, ExpectedTexel(SURFACE * 2.0f), "half a share two stops over");
	EXPECT_GT(std::fabs(texel.x - ExpectedTexel(SURFACE * 2.5f).x), 6.0f);

	upper->SetWeight(1.0f);
	ASSERT_TRUE(RenderCenter(stage, QuadView(), texel));
	ExpectTexelNear(texel, ExpectedTexel(SURFACE * 4.0f), "the whole share");

	TearDownStage(stage);
}

TEST_F(ExposureReadbackFixture, CameraExposedByItsEnvironment) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* material = LoadUnlit("Surface.mat", SURFACE);
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	AddEnvironment(stage)->SetExposureCompensation(-1.0f);

	WindowDesc windowDesc;
	windowDesc.title = "ExposureTest";
	windowDesc.clientSize = {64, 64};
	windowDesc.flags = WindowFlags::Hidden;
	Handle<Window> window = WindowSystem::Create(windowDesc);
	ASSERT_TRUE(window.IsValid());
	Handle<SceneObject> cameraObject = SceneSystem::CreateObject("Camera", stage.scene);
	cameraObject->SetLocalPosition(ShadingTest::Eye);
	cameraObject->SetLocalRotation(Quat::AngleAxis(180.0f, Vec3::Up()));
	Handle<Camera> camera = cameraObject->AddComponent<Camera>();
	camera->SetTargetWindow(window);
	for (u32 frame = 0; frame < 2; ++frame) {
		WindowSystem::PumpEvents();
		SceneSystem::EndFrame();
		RenderSystem::RenderAll();
	}

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackWindowSceneColor(window, pixels, size));
	const Vec3 centre = RadianceTexel(pixels, size, size.x / 2, size.y / 2);
	EXPECT_NEAR(centre.x, SURFACE.x * 0.5f, 1e-3f);
	EXPECT_NEAR(centre.y, SURFACE.y * 0.5f, 1e-3f);

	TearDownStage(stage);
}

TEST_F(PhysicalExposureFixture, LuxReadsRadianceAtEV100) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	WriteProjectFile("Units.hlsl", ATTENUATION_ONLY_SHADER);
	Asset* shader = AssetDatabase::LoadAsset(Path("project://Units.hlsl"));
	ASSERT_NE(shader, nullptr);
	MaterialAsset* material = LoadMaterial("Units.mat", shader->assetId, "\t\"colors\": { \"baseColor\": [0, 0, 0, 1] }");
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	constexpr f32 LUX = 2000.0f;
	stage.sun->SetIntensity(LUX);
	Handle<Environment> environment = AddEnvironment(stage);
	environment->SetFixedExposure(12.0f);

	Vec3 texel;
	ASSERT_TRUE(RenderCenter(stage, QuadView(), texel));
	EXPECT_NEAR(texel.x, ExpectedTexel(Vec3(1.0f, 1.0f, 1.0f) * (LUX * ExposureAt(12.0f))).x, TEXEL_TOLERANCE);
	environment->SetFixedExposure(13.0f);
	ASSERT_TRUE(RenderCenter(stage, QuadView(), texel));
	EXPECT_NEAR(texel.x, ExpectedTexel(Vec3(1.0f, 1.0f, 1.0f) * (LUX * ExposureAt(13.0f))).x, TEXEL_TOLERANCE);

	environment->SetExposureMode(ExposureMode::PhysicalCamera);
	ASSERT_TRUE(RenderCenter(stage, QuadView(), texel));
	const f32 defaultCamera = std::log2(16.0f * 16.0f * 200.0f * 100.0f / 200.0f);
	EXPECT_NEAR(texel.x, ExpectedTexel(Vec3(1.0f, 1.0f, 1.0f) * (LUX * ExposureAt(defaultCamera))).x, TEXEL_TOLERANCE);

	TearDownStage(stage);
}

TEST_F(PhysicalExposureFixture, SkyNitsReadExposed) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* mirror = LoadMaterial("Mirror.mat", ShaderGuid("engine://Shaders/Lit.hlsl"), MIRROR_BODY);
	ASSERT_NE(mirror, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(mirror, stage));
	stage.sun->SetEnabled(false);
	Handle<Environment> environment = AddEnvironment(stage);
	environment->SetSkyMode(SkyMode::Color);
	environment->SetSkyColor(Color(1.0f, 1.0f, 1.0f, 1.0f));
	environment->SetSkyIntensity(5000.0f);
	environment->SetReflectionIntensity(1.0f);
	environment->SetFixedExposure(13.0f);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	const f32 exposed = 5000.0f * ExposureAt(13.0f);
	ExpectTexelNear(Texel(pixels, size, 2, size.y - 3), ExpectedTexel(Vec3(exposed, exposed, exposed)), "the sky");
	ExpectTexelNear(CenterTexel(pixels, size), ExpectedTexel(MirrorOf(Vec3(exposed, exposed, exposed))), "its reflection");

	TearDownStage(stage);
}

TEST_F(PhysicalExposureFixture, MaterialSkyLightInNits) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	WriteProjectFile("Uniform.hlsl", UNIFORM_SKY_SHADER);
	constexpr f32 TINT = 0.002f;
	constexpr f32 INTENSITY = 2.5e6f;
	MaterialAsset* sky = LoadMaterial("Uniform.mat", ShaderGuid("project://Uniform.hlsl"),
									  std::format("\t\"colors\": {{ \"tint\": [{}, {}, {}, 1] }}", TINT, TINT, TINT));
	MaterialAsset* mirror = LoadMaterial("Mirror.mat", ShaderGuid("engine://Shaders/Lit.hlsl"), MIRROR_BODY);
	MaterialAsset* rough = LoadMaterial("Rough.mat", ShaderGuid("engine://Shaders/Lit.hlsl"),
										"\t\"floats\": { \"metallic\": 0, \"roughness\": 1 },\n"
										"\t\"colors\": { \"baseColor\": [1, 1, 1, 1] }");
	ASSERT_NE(sky, nullptr);
	ASSERT_NE(mirror, nullptr);
	ASSERT_NE(rough, nullptr);
	const f32 exposed = TINT * INTENSITY * ExposureAt(13.0f);

	for (MaterialAsset* surface : {mirror, rough}) {
		SCOPED_TRACE(surface == mirror ? "mirror" : "rough quad");
		ShadingStage stage;
		Handle<Environment> environment;
		ASSERT_TRUE(BuildSkyStage(sky, stage, environment));
		ASSERT_TRUE(AddQuad(stage, surface, 0.0f));
		stage.sun->SetEnabled(false);
		environment->SetSkyIntensity(INTENSITY);
		environment->SetFixedExposure(13.0f);
		environment->SetAmbientColor(Color(0.0f, 0.0f, 0.0f, 1.0f));

		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		for (u32 frame = 0; frame < RHI::FRAMES_IN_FLIGHT + 2; ++frame) {
			ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
		}
		ExpectTexelNear(Texel(pixels, size, 2, size.y - 3), ExpectedTexel(Vec3(exposed, exposed, exposed)), "the sky drawn");
		if (surface == mirror) {
			ExpectTexelNear(CenterTexel(pixels, size), ExpectedTexel(MirrorOf(Vec3(exposed, exposed, exposed))), "the reflection");
		} else {
			const f32 irradiance = GraphicsInternal::EvaluateIrradianceSH(
									   CapturedIrradiance([](const Vec3&) { return Vec3(1.0f, 1.0f, 1.0f); }), Vec3(0.0f, 0.0f, 1.0f))
									   .x;
			const Vec2 dfg = ShadingTest::EnvBRDFApprox(1.0f, 1.0f);
			const f32 predicted = exposed * (irradiance + 0.04f * dfg.x + dfg.y);
			EXPECT_NEAR(CenterTexel(pixels, size).x, ExpectedTexel(Vec3(predicted, predicted, predicted)).x, 3.0f);
		}
		TearDownStage(stage);
	}
}
