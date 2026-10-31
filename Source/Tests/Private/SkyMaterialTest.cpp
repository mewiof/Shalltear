#include "TestsPCH.h"

#include "ShadingHarness.h"

#include "Asset/PakReader.h"
#include "Graphics/EnvironmentIBL.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/Graphics/Camera.h"
#include "Shalltear/Core/Graphics/CubeFace.h"
#include "Shalltear/Core/Graphics/Environment.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Math/HalfFloat.h"
#include "Shalltear/Core/Time.h"
#include "Shalltear/Editor/CookOps.h"

#include <cmath>
#include <memory>
#include <string>

using namespace Shalltear;
using ShadingTest::ArmDiagnosticSink;
using ShadingTest::CapturedIrradiance;
using ShadingTest::CenterTexel;
using ShadingTest::ExpectedTexel;
using ShadingTest::MaterialReadbackFixture;
using ShadingTest::MIRROR_BODY;
using ShadingTest::MirrorOf;
using ShadingTest::QuadView;
using ShadingTest::RadianceTexel;
using ShadingTest::ShaderDiagnosticSink;
using ShadingTest::ShadingStage;
using ShadingTest::SkyPixelRay;

namespace {

	constexpr const char* SKY_SOURCE =
		"#pragma shalltear_template sky\n"
		"#pragma shalltear_default tint 1 1 1 1\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial {\n"
		"\tfloat4 tint;\n"
		"\tint mode;\n"
		"};\n"
		"void ShalltearSkyFunction(ShalltearSkyInput input, inout ShalltearSkyOutput sky) {\n"
		"\tif (mode == 0) {\n"
		"\t\tsky.color = (input.directionWS * 0.5 + 0.5) * tint.rgb;\n"
		"\t} else if (mode == 1) {\n"
		"\t\tsky.color = input.sunEnabled ? input.sunDirectionWS * 0.5 + 0.5 : float3(1, 0, 1);\n"
		"\t} else if (mode == 2) {\n"
		"\t\tsky.color = input.sunColor * 0.25;\n"
		"\t} else if (mode == 3) {\n"
		"\t\tsky.color = float3(input.sunAngularDiameter, 0, 0);\n"
		"\t} else if (mode == 4) {\n"
		"\t\tsky.color = float3(frac(input.time.x * 0.1), input.time.w, 0.25);\n"
		"\t} else if (mode == 6) {\n"
		"\t\tsky.color = (0.5 + 0.4 * input.directionWS) * tint.rgb;\n"
		"\t} else if (mode == 7) {\n"
		"\t\tsky.color = input.capturing ? float3(0, 0, 1) : float3(1, 0, 0);\n"
		"\t} else {\n"
		"\t\tsky.color = tint.rgb;\n"
		"\t}\n"
		"}\n";

	enum SkyCase : int {
		ModeDirection = 0,
		ModeSunDirection = 1,
		ModeSunColor = 2,
		ModeSunSize = 3,
		ModeTime = 4,
		ModeTint = 5,
		ModeLinear = 6,
		ModeCapturing = 7,
	};

	Vec3 Texel(const Vector<u8>& pixels, Vec2I size, i32 x, i32 y) {
		const size_t index = (static_cast<size_t>(y) * size.x + x) * 4;
		return Vec3(static_cast<f32>(pixels[index]), static_cast<f32>(pixels[index + 1]), static_cast<f32>(pixels[index + 2]));
	}

	constexpr f32 TEXEL_TOLERANCE = 1.5f;

	void ExpectTexelNear(const Vec3& actual, const Vec3& expected, const char* what) {
		EXPECT_NEAR(actual.x, expected.x, TEXEL_TOLERANCE) << what;
		EXPECT_NEAR(actual.y, expected.y, TEXEL_TOLERANCE) << what;
		EXPECT_NEAR(actual.z, expected.z, TEXEL_TOLERANCE) << what;
	}

	u32 CountOccurrences(const str& haystack, std::string_view needle) {
		u32 count = 0;
		for (size_t at = haystack.find(needle); at != str::npos; at = haystack.find(needle, at + needle.size())) {
			++count;
		}
		return count;
	}

	Guid ShaderGuid(const char* path) {
		Asset* shader = AssetDatabase::LoadAsset(Path(path));
		return shader ? shader->assetId : Guid{};
	}

	class SkyMaterialFixture : public MaterialReadbackFixture {
	protected:
		Guid SkyShaderGuid() {
			WriteProjectFile("TestSky.hlsl", SKY_SOURCE);
			return ShaderGuid("project://TestSky.hlsl");
		}

		MaterialAsset* LoadSky(const char* fileName, int mode, const Vec3& tint = Vec3(1.0f, 1.0f, 1.0f)) {
			return LoadMaterial(fileName, SkyShaderGuid(),
								std::format("\t\"colors\": {{ \"tint\": [{}, {}, {}, 1] }},\n\t\"ints\": {{ \"mode\": {} }}",
											tint.x, tint.y, tint.z, mode));
		}

		bool Render(const ShadingStage& stage, const ViewDesc& desc, Vector<u8>& pixels, Vec2I& size) {
			RenderSystem::SubmitView(stage.target, desc);
			RenderSystem::RenderAll();
			return RenderSystem::ReadbackViewTarget(stage.target, pixels, size);
		}
	};

	constexpr i32 PIXEL_A_X = 48;
	constexpr i32 PIXEL_A_Y = 12;
	constexpr i32 PIXEL_B_X = 9;
	constexpr i32 PIXEL_B_Y = 55;

} // anonymous namespace

TEST_F(SkyMaterialFixture, SkyTexelFollowsRay) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* sky = LoadSky("Direction.mat", ModeDirection, Vec3(1.0f, 0.8f, 0.6f));
	ASSERT_NE(sky, nullptr);
	ASSERT_NE(sky->shader.Get(), nullptr);
	ASSERT_EQ(sky->shader->integrator, ShaderTemplate::Sky);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(sky, stage, environment));

	ViewDesc desc = QuadView();
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, desc, pixels, size));

	const Vec3 tint(1.0f, 0.8f, 0.6f);
	for (const Vec2I pixel : {Vec2I{PIXEL_A_X, PIXEL_A_Y}, Vec2I{PIXEL_B_X, PIXEL_B_Y}}) {
		const Vec3 ray = SkyPixelRay(desc, size, pixel.x, pixel.y);
		const Vec3 linear = (ray * 0.5f + Vec3(0.5f, 0.5f, 0.5f)) * tint;
		ExpectTexelNear(Texel(pixels, size, pixel.x, pixel.y), ExpectedTexel(linear), "the sky is not its function along the pixel's ray");
	}
	EXPECT_GT(std::fabs(Texel(pixels, size, PIXEL_A_X, PIXEL_A_Y).y - Texel(pixels, size, PIXEL_B_X, PIXEL_B_Y).y), 20.0f);

	TearDownStage(stage);
}

TEST_F(SkyMaterialFixture, SkyIntensityScalesMaterialSky) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* sky = LoadSky("Tint.mat", ModeTint, Vec3(0.2f, 0.1f, 0.05f));
	ASSERT_NE(sky, nullptr);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(sky, stage, environment));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	ExpectTexelNear(CenterTexel(pixels, size), ExpectedTexel(Vec3(0.2f, 0.1f, 0.05f)), "the sky at intensity 1");

	environment->SetSkyIntensity(3.0f);
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	ExpectTexelNear(CenterTexel(pixels, size), ExpectedTexel(Vec3(0.6f, 0.3f, 0.15f)), "the intensity did not scale the material sky");

	TearDownStage(stage);
}

TEST_F(SkyMaterialFixture, SunReachesSkyAsLightStatesIt) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* direction = LoadSky("SunDirection.mat", ModeSunDirection);
	MaterialAsset* color = LoadSky("SunColor.mat", ModeSunColor);
	MaterialAsset* disc = LoadSky("SunSize.mat", ModeSunSize);
	ASSERT_NE(direction, nullptr);
	ASSERT_NE(color, nullptr);
	ASSERT_NE(disc, nullptr);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(direction, stage, environment));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	ExpectTexelNear(CenterTexel(pixels, size), ExpectedTexel(stage.lightDirection * 0.5f + Vec3(0.5f, 0.5f, 0.5f)),
					"the sky was not handed the direction toward the light");

	stage.sun->SetColor(Color(1.0f, 0.5f, 0.25f, 1.0f));
	stage.sun->SetIntensity(2.0f);
	environment->SetSkyMaterial(Handle<MaterialAsset>(color));
	SceneSystem::EndFrame();
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	ExpectTexelNear(CenterTexel(pixels, size), ExpectedTexel(Vec3(0.5f, 0.25f, 0.125f)),
					"the sky was not handed the light's color times its intensity");

	environment->SetSunDiscSizeDeg(20.0f);
	environment->SetSkyMaterial(Handle<MaterialAsset>(disc));
	SceneSystem::EndFrame();
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	ExpectTexelNear(CenterTexel(pixels, size), ExpectedTexel(Vec3(20.0f * Math::DEG_TO_RAD, 0.0f, 0.0f)),
					"the sky was not handed the disc row in radians");

	stage.sun->SetEnabled(false);
	environment->SetSkyMaterial(Handle<MaterialAsset>(direction));
	SceneSystem::EndFrame();
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	ExpectTexelNear(CenterTexel(pixels, size), ExpectedTexel(Vec3(1.0f, 0.0f, 1.0f)), "a view with no sun still reported one");

	TearDownStage(stage);
}

TEST_F(SkyMaterialFixture, TimeMovesSkyByViewsClock) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* sky = LoadSky("Time.mat", ModeTime);
	ASSERT_NE(sky, nullptr);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(sky, stage, environment));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ViewDesc desc = QuadView();
	const f64 early = 2.0;
	desc.time = &early;
	ASSERT_TRUE(Render(stage, desc, pixels, size));
	ExpectTexelNear(CenterTexel(pixels, size), ExpectedTexel(Vec3(0.2f, 0.0f, 0.25f)), "the sky at t = 2");

	const f64 late = 5.0;
	desc.time = &late;
	ASSERT_TRUE(Render(stage, desc, pixels, size));
	ExpectTexelNear(CenterTexel(pixels, size), ExpectedTexel(Vec3(0.5f, 0.0f, 0.25f)), "the sky did not move with the clock");

	TearDownStage(stage);
}

TEST_F(SkyMaterialFixture, GeometryCoversTheMaterialSky) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* sky = LoadSky("Red.mat", ModeTint, Vec3(0.9f, 0.1f, 0.1f));
	MaterialAsset* black = LoadMaterial("Black.mat", ShaderGuid("engine://Shaders/Unlit.hlsl"),
										"\t\"colors\": { \"baseColor\": [0, 0, 0, 1] }");
	ASSERT_NE(sky, nullptr);
	ASSERT_NE(black, nullptr);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(sky, stage, environment));
	ASSERT_TRUE(AddQuad(stage, black, 0.0f));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	ExpectTexelNear(Texel(pixels, size, 2, 2), ExpectedTexel(Vec3(0.9f, 0.1f, 0.1f)), "the sky did not fill an uncovered corner");
	ExpectTexelNear(CenterTexel(pixels, size), Vec3(0.0f, 0.0f, 0.0f), "the sky drew over the geometry in front of it");

	TearDownStage(stage);
}

TEST_F(SkyMaterialFixture, CaptureSeesMaterialSky) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* sky = LoadSky("Direction.mat", ModeDirection);
	ASSERT_NE(sky, nullptr);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(sky, stage, environment));

	const Vec3 eye(0.0f, 1.0f, 0.0f);
	for (u32 face : {0u, 3u, 5u}) {
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
		for (const Vec2I texel : {Vec2I{10, 20}, Vec2I{50, 41}}) {
			const Vec3 direction = GraphicsInternal::CubeTexelDirection(face, (static_cast<f32>(texel.x) + 0.5f) / static_cast<f32>(size.x),
																		(static_cast<f32>(texel.y) + 0.5f) / static_cast<f32>(size.y));
			const Vec3 expected = direction * 0.5f + Vec3(0.5f, 0.5f, 0.5f);
			const Vec3 kept = RadianceTexel(pixels, size, texel.x, texel.y);
			EXPECT_NEAR(kept.x, expected.x, 0.01f) << "face " << face;
			EXPECT_NEAR(kept.y, expected.y, 0.01f) << "face " << face;
			EXPECT_NEAR(kept.z, expected.z, 0.01f) << "face " << face;
		}
	}

	TearDownStage(stage);
}

namespace {

	Vec3 LinearSky(const Vec3& direction) {
		return Vec3(0.5f, 0.5f, 0.5f) + direction * 0.4f;
	}

} // anonymous namespace

TEST_F(SkyMaterialFixture, MirrorReflectsWhatMaterialSkyDraws) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* sky = LoadSky("Linear.mat", ModeLinear);
	MaterialAsset* mirror = LoadMaterial("Mirror.mat", ShaderGuid("engine://Shaders/Lit.hlsl"), MIRROR_BODY);
	ASSERT_NE(sky, nullptr);
	ASSERT_NE(mirror, nullptr);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(sky, stage, environment));
	ASSERT_TRUE(AddQuad(stage, mirror, 0.0f));
	stage.sun->SetEnabled(false);
	environment->SetAmbientColor(Color(0.0f, 0.0f, 0.0f, 1.0f));

	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	LogManager::SetGlobalLevel(LogLevel::Trace);
	auto sink = std::make_shared<ShaderDiagnosticSink>();
	LogManager::AddSink(sink);
	ASSERT_TRUE(ArmDiagnosticSink(sink));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));

	LogManager::Terminate();
	const str said = sink->Joined();
	sink.reset();
	JobSystem::Terminate();
	EXPECT_EQ(CountOccurrences(said, "VUID"), 0u) << said;

	const Vec3 measured = CenterTexel(pixels, size);
	const Vec3 expected = ExpectedTexel(MirrorOf(LinearSky(Vec3(0.0f, 0.0f, 1.0f))));
	EXPECT_NEAR(measured.x, expected.x, 4.0f);
	EXPECT_NEAR(measured.y, expected.y, 4.0f);
	EXPECT_NEAR(measured.z, expected.z, 4.0f);
	const Vec3 zenith = ExpectedTexel(MirrorOf(LinearSky(Vec3(0.0f, 1.0f, 0.0f))));
	EXPECT_GT(std::fabs(measured.y - zenith.y), 8.0f);

	TearDownStage(stage);
}

TEST_F(SkyMaterialFixture, RoughSurfaceLitBySky) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* sky = LoadSky("Linear.mat", ModeLinear);
	MaterialAsset* rough = LoadMaterial("Rough.mat", ShaderGuid("engine://Shaders/Lit.hlsl"),
										"\t\"floats\": { \"metallic\": 0, \"roughness\": 1 },\n"
										"\t\"colors\": { \"baseColor\": [1, 1, 1, 1] }");
	ASSERT_NE(sky, nullptr);
	ASSERT_NE(rough, nullptr);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(sky, stage, environment));
	ASSERT_TRUE(AddQuad(stage, rough, 0.0f));
	stage.sun->SetEnabled(false);
	environment->SetAmbientColor(Color(0.0f, 0.0f, 0.0f, 1.0f));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	const Vec3 first = CenterTexel(pixels, size);
	for (u32 frame = 0; frame < RHI::FRAMES_IN_FLIGHT + 1; ++frame) {
		ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	}
	const Vec3 settled = CenterTexel(pixels, size);

	const Vec3 irradiance = GraphicsInternal::EvaluateIrradianceSH(CapturedIrradiance(&LinearSky), Vec3(0.0f, 0.0f, 1.0f));
	const Vec2 dfg = ShadingTest::EnvBRDFApprox(1.0f, 1.0f);
	const f32 specularCeiling = 0.9f * (0.04f * dfg.x + dfg.y);
	const Vec3 diffuseOnly = ExpectedTexel(irradiance);
	const Vec3 withCeiling = ExpectedTexel(irradiance + Vec3(specularCeiling, specularCeiling, specularCeiling));
	EXPECT_GE(settled.z, diffuseOnly.z - 3.0f);
	EXPECT_LE(settled.z, withCeiling.z + 3.0f);
	EXPECT_GE(settled.x, diffuseOnly.x - 3.0f);
	EXPECT_LE(settled.x, withCeiling.x + 3.0f);
	EXPECT_GT(settled.z, settled.x + 10.0f);
	EXPECT_LT(first.z + 20.0f, settled.z);

	TearDownStage(stage);
}

TEST_F(SkyMaterialFixture, CaptureIsSkyDrawnWithCapturingSet) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* sky = LoadSky("Capturing.mat", ModeCapturing);
	MaterialAsset* mirror = LoadMaterial("Mirror.mat", ShaderGuid("engine://Shaders/Lit.hlsl"), MIRROR_BODY);
	ASSERT_NE(sky, nullptr);
	ASSERT_NE(mirror, nullptr);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(sky, stage, environment));
	ASSERT_TRUE(AddQuad(stage, mirror, 0.0f));
	stage.sun->SetEnabled(false);
	environment->SetAmbientColor(Color(0.0f, 0.0f, 0.0f, 1.0f));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	ExpectTexelNear(Texel(pixels, size, 2, 2), ExpectedTexel(Vec3(1.0f, 0.0f, 0.0f)), "the visible sky is not the one drawn without capturing");
	ExpectTexelNear(CenterTexel(pixels, size), ExpectedTexel(MirrorOf(Vec3(0.0f, 0.0f, 1.0f))), "the reflection is not the capture");

	TearDownStage(stage);
}

TEST_F(SkyMaterialFixture, EachUpdateModeCapturesWhenItSays) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* sky = LoadSky("Clock.mat", ModeTime);
	MaterialAsset* mirror = LoadMaterial("Mirror.mat", ShaderGuid("engine://Shaders/Lit.hlsl"), MIRROR_BODY);
	ASSERT_NE(sky, nullptr);
	ASSERT_NE(mirror, nullptr);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(sky, stage, environment));
	ASSERT_TRUE(AddQuad(stage, mirror, 0.0f));
	stage.sun->SetEnabled(false);
	environment->SetAmbientColor(Color(0.0f, 0.0f, 0.0f, 1.0f));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	auto renderAt = [&](f64 seconds, f32& mirrorRed, f32& skyRed) {
		FrameTime frame = Time::GetFrame();
		frame.time = seconds;
		frame.deltaTime = 0.0f;
		Time::FrameScope scope(frame);
		const bool rendered = Render(stage, QuadView(), pixels, size);
		mirrorRed = CenterTexel(pixels, size).x;
		skyRed = Texel(pixels, size, 2, 2).x;
		return rendered;
	};
	auto mirrorAt = [](f32 clockRed) { return ExpectedTexel(MirrorOf(Vec3(clockRed, 0.0f, 0.25f))).x; };
	f32 mirrorRed = 0.0f;
	f32 skyRed = 0.0f;

	ASSERT_EQ(environment->GetSkyUpdateMode(), SkyUpdateMode::OnChanged);
	ASSERT_TRUE(renderAt(2.0, mirrorRed, skyRed));
	EXPECT_NEAR(mirrorRed, mirrorAt(0.2f), 3.0f);
	ASSERT_TRUE(renderAt(5.0, mirrorRed, skyRed));
	EXPECT_NEAR(skyRed, ExpectedTexel(Vec3(0.5f, 0.0f, 0.25f)).x, 3.0f);
	EXPECT_NEAR(mirrorRed, mirrorAt(0.2f), 3.0f);

	environment->SetSkyUpdateMode(SkyUpdateMode::Realtime);
	ASSERT_TRUE(renderAt(5.0, mirrorRed, skyRed));
	EXPECT_NEAR(mirrorRed, mirrorAt(0.5f), 3.0f);

	environment->SetSkyUpdateMode(SkyUpdateMode::OnDemand);
	ASSERT_TRUE(renderAt(7.0, mirrorRed, skyRed));
	EXPECT_NEAR(mirrorRed, mirrorAt(0.5f), 3.0f);
	environment->RequestSkyUpdate();
	ASSERT_TRUE(renderAt(7.0, mirrorRed, skyRed));
	EXPECT_NEAR(mirrorRed, mirrorAt(0.7f), 3.0f);
	ASSERT_TRUE(renderAt(8.0, mirrorRed, skyRed));
	EXPECT_NEAR(mirrorRed, mirrorAt(0.7f), 3.0f);

	TearDownStage(stage);
}

TEST_F(SkyMaterialFixture, CaptureUsesViewClock) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* sky = LoadSky("Clock.mat", ModeTime);
	MaterialAsset* mirror = LoadMaterial("Mirror.mat", ShaderGuid("engine://Shaders/Lit.hlsl"), MIRROR_BODY);
	ASSERT_NE(sky, nullptr);
	ASSERT_NE(mirror, nullptr);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(sky, stage, environment));
	ASSERT_TRUE(AddQuad(stage, mirror, 0.0f));
	stage.sun->SetEnabled(false);
	environment->SetAmbientColor(Color(0.0f, 0.0f, 0.0f, 1.0f));
	environment->SetSkyUpdateMode(SkyUpdateMode::Realtime);

	FrameTime frame = Time::GetFrame();
	frame.time = 7.0;
	frame.deltaTime = 0.0f;
	Time::FrameScope scope(frame);
	ViewDesc desc = QuadView();
	const f64 viewClock = 3.0;
	desc.time = &viewClock;
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, desc, pixels, size));
	EXPECT_NEAR(CenterTexel(pixels, size).x, ExpectedTexel(MirrorOf(Vec3(0.3f, 0.0f, 0.25f))).x, 3.0f);

	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	EXPECT_NEAR(CenterTexel(pixels, size).x, ExpectedTexel(MirrorOf(Vec3(0.7f, 0.0f, 0.25f))).x, 3.0f);

	TearDownStage(stage);
}

TEST_F(SkyMaterialFixture, EachEnvironmentLightsViewsWithCapture) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* red = LoadSky("Red.mat", ModeTint, Vec3(0.8f, 0.05f, 0.05f));
	MaterialAsset* blue = LoadSky("Blue.mat", ModeTint, Vec3(0.05f, 0.05f, 0.8f));
	MaterialAsset* mirror = LoadMaterial("Mirror.mat", ShaderGuid("engine://Shaders/Lit.hlsl"), MIRROR_BODY);
	ASSERT_NE(red, nullptr);
	ASSERT_NE(blue, nullptr);
	ASSERT_NE(mirror, nullptr);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(red, stage, environment));
	ASSERT_TRUE(AddQuad(stage, mirror, 0.0f));
	stage.sun->SetEnabled(false);

	Handle<Scene> isolated = SceneSystem::CreateIsolatedScene("Preview");
	Asset* quadMesh = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Quad)));
	ASSERT_NE(quadMesh, nullptr);
	Handle<SceneObject> quad = SceneSystem::CreateObject("Quad", isolated);
	Handle<MeshRenderer> renderer = quad->AddComponent<MeshRenderer>();
	renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(quadMesh)));
	renderer->SetMaterial(0, Handle<MaterialAsset>(mirror));
	Handle<SceneObject> previewEnvironmentObject = SceneSystem::CreateObject("Environment", isolated);
	Handle<Environment> previewEnvironment = previewEnvironmentObject->AddComponent<Environment>();
	previewEnvironment->SetSkyMode(SkyMode::Material);
	previewEnvironment->SetSkyMaterial(Handle<MaterialAsset>(blue));
	SceneSystem::EndFrame();

	const ViewTargetId previewTarget = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(previewTarget, INVALID_VIEW_TARGET);
	ViewDesc previewDesc = QuadView();
	Handle<Scene> only[] = {isolated};
	previewDesc.scenes = {only, 1};
	RenderSystem::SubmitView(stage.target, QuadView());
	RenderSystem::SubmitView(previewTarget, previewDesc);
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(stage.target, pixels, size));
	const Vec3 world = CenterTexel(pixels, size);
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(previewTarget, pixels, size));
	const Vec3 preview = CenterTexel(pixels, size);
	ExpectTexelNear(world, ExpectedTexel(MirrorOf(Vec3(0.8f, 0.05f, 0.05f))), "the world's mirror did not reflect the world's sky");
	ExpectTexelNear(preview, ExpectedTexel(MirrorOf(Vec3(0.05f, 0.05f, 0.8f))), "the preview's mirror did not reflect the preview's sky");

	RenderSystem::DestroyViewTarget(previewTarget);
	SceneSystem::UnloadScene(isolated);
	SceneSystem::EndFrame();
	TearDownStage(stage);
}

TEST_F(SkyMaterialFixture, EnvironmentThatGoesAwayGivesCaptureBack) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* sky = LoadSky("Tint.mat", ModeTint, Vec3(0.6f, 0.3f, 0.1f));
	MaterialAsset* mirror = LoadMaterial("Mirror.mat", ShaderGuid("engine://Shaders/Lit.hlsl"), MIRROR_BODY);
	ASSERT_NE(sky, nullptr);
	ASSERT_NE(mirror, nullptr);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(sky, stage, environment));
	ASSERT_TRUE(AddQuad(stage, mirror, 0.0f));
	stage.sun->SetEnabled(false);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	auto settle = [&]() {
		for (u32 frame = 0; frame < RHI::FRAMES_IN_FLIGHT + 2; ++frame) {
			EXPECT_TRUE(Render(stage, QuadView(), pixels, size));
		}
	};
	settle();
	const GraphicsStats with = RenderSystem::GetStats();
	const Vec3 lit = CenterTexel(pixels, size);

	environment->SetEnabled(false);
	SceneSystem::EndFrame();
	settle();
	const GraphicsStats without = RenderSystem::GetStats();
	EXPECT_EQ(with.liveTextures - without.liveTextures, 2u);
	EXPECT_EQ(with.liveBuffers - without.liveBuffers, 1u);

	environment->SetEnabled(true);
	SceneSystem::EndFrame();
	settle();
	const GraphicsStats again = RenderSystem::GetStats();
	EXPECT_EQ(again.liveTextures, with.liveTextures);
	EXPECT_EQ(again.liveBuffers, with.liveBuffers);
	EXPECT_EQ(CenterTexel(pixels, size), lit);

	TearDownStage(stage);
}

namespace {

	Handle<Environment> AddLayer(ShadingStage& stage, MaterialAsset* sky, f32 priority, f32 weight) {
		Handle<SceneObject> object = SceneSystem::CreateObject("Layer", stage.scene);
		Handle<Environment> layer = object->AddComponent<Environment>();
		layer->SetSkyMode(SkyMode::Material);
		layer->SetSkyMaterial(Handle<MaterialAsset>(sky));
		layer->SetPriority(priority);
		layer->SetWeight(weight);
		layer->SetAmbientColor(Color(0.0f, 0.0f, 0.0f, 1.0f));
		SceneSystem::EndFrame();
		return layer;
	}

	Vec3 Lerp3(const Vec3& a, const Vec3& b, f32 t) {
		return a * (1.0f - t) + b * t;
	}

} // anonymous namespace

TEST_F(SkyMaterialFixture, GlobalEnvironmentsMixByWeight) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	const Vec3 red(0.8f, 0.05f, 0.05f);
	const Vec3 blue(0.05f, 0.05f, 0.8f);
	MaterialAsset* redSky = LoadSky("Red.mat", ModeTint, red);
	MaterialAsset* blueSky = LoadSky("Blue.mat", ModeTint, blue);
	MaterialAsset* mirror = LoadMaterial("Mirror.mat", ShaderGuid("engine://Shaders/Lit.hlsl"), MIRROR_BODY);
	ASSERT_NE(redSky, nullptr);
	ASSERT_NE(blueSky, nullptr);
	ASSERT_NE(mirror, nullptr);
	ShadingStage stage;
	Handle<Environment> lower;
	ASSERT_TRUE(BuildSkyStage(redSky, stage, lower));
	ASSERT_TRUE(AddQuad(stage, mirror, 0.0f));
	stage.sun->SetEnabled(false);
	lower->SetAmbientColor(Color(0.0f, 0.0f, 0.0f, 1.0f));
	Handle<Environment> upper = AddLayer(stage, blueSky, 1.0f, 0.4f);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	const Vec3 mixed = Lerp3(red, blue, 0.4f);
	ExpectTexelNear(Texel(pixels, size, 2, 2), ExpectedTexel(mixed), "the sky is not 40% of the upper over the lower");
	ExpectTexelNear(CenterTexel(pixels, size), ExpectedTexel(MirrorOf(mixed)), "the reflection did not mix by the same share");

	upper->SetWeight(1.0f);
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	ExpectTexelNear(Texel(pixels, size, 2, 2), ExpectedTexel(blue), "a full upper weight is not the upper alone");
	upper->SetWeight(0.0f);
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	ExpectTexelNear(Texel(pixels, size, 2, 2), ExpectedTexel(red), "0 upper weight isn't the lower alone");

	TearDownStage(stage);
}

TEST_F(SkyMaterialFixture, LocalEnvironmentFades) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	const Vec3 red(0.8f, 0.05f, 0.05f);
	const Vec3 blue(0.05f, 0.05f, 0.8f);
	MaterialAsset* redSky = LoadSky("Red.mat", ModeTint, red);
	MaterialAsset* blueSky = LoadSky("Blue.mat", ModeTint, blue);
	ASSERT_NE(redSky, nullptr);
	ASSERT_NE(blueSky, nullptr);
	ShadingStage stage;
	Handle<Environment> world;
	ASSERT_TRUE(BuildSkyStage(redSky, stage, world));
	stage.sun->SetEnabled(false);
	Handle<Environment> zone = AddLayer(stage, blueSky, 1.0f, 1.0f);
	zone->SetIsGlobal(false);
	zone->SetSize(Vec3(2.0f, 2.0f, 2.0f));
	zone->SetBlendDistance(4.0f);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	ExpectTexelNear(Texel(pixels, size, 2, 2), ExpectedTexel(Lerp3(red, blue, 0.75f)), "half way through the fade is not three quarters of the zone");

	zone->SetBlendDistance(1.5f);
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	const Vector<u8> outOfReach = pixels;
	zone->SetEnabled(false);
	SceneSystem::EndFrame();
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	EXPECT_EQ(std::memcmp(outOfReach.data(), pixels.data(), pixels.size()), 0);
	ExpectTexelNear(Texel(pixels, size, 2, 2), ExpectedTexel(red), "the world's sky alone is not red");

	TearDownStage(stage);
}

TEST_F(SkyMaterialFixture, UnviewedSkyCapturesNothing) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	const Vec3 red(0.8f, 0.05f, 0.05f);
	MaterialAsset* redSky = LoadSky("Red.mat", ModeTint, red);
	MaterialAsset* clockSky = LoadSky("Clock.mat", ModeTime);
	MaterialAsset* mirror = LoadMaterial("Mirror.mat", ShaderGuid("engine://Shaders/Lit.hlsl"), MIRROR_BODY);
	ASSERT_NE(redSky, nullptr);
	ASSERT_NE(clockSky, nullptr);
	ASSERT_NE(mirror, nullptr);
	ShadingStage stage;
	Handle<Environment> world;
	ASSERT_TRUE(BuildSkyStage(redSky, stage, world));
	ASSERT_TRUE(AddQuad(stage, mirror, 0.0f));
	stage.sun->SetEnabled(false);
	world->SetAmbientColor(Color(0.0f, 0.0f, 0.0f, 1.0f));
	Handle<Environment> zone = AddLayer(stage, clockSky, 1.0f, 1.0f);
	zone->SetIsGlobal(false);
	zone->SetSize(Vec3(2.0f, 2.0f, 2.0f));
	zone->SetSkyUpdateMode(SkyUpdateMode::OnDemand);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	auto renderAt = [&](f64 seconds) {
		FrameTime frame = Time::GetFrame();
		frame.time = seconds;
		frame.deltaTime = 0.0f;
		Time::FrameScope scope(frame);
		return Render(stage, QuadView(), pixels, size);
	};
	ASSERT_TRUE(renderAt(2.0));
	ASSERT_TRUE(renderAt(2.0));
	ExpectTexelNear(CenterTexel(pixels, size), ExpectedTexel(MirrorOf(red)), "out of reach, the mirror is not the world's alone");
	const GraphicsStats outside = RenderSystem::GetStats();

	zone->SetSize(Vec3(10.0f, 10.0f, 10.0f));
	ASSERT_TRUE(renderAt(7.0));
	EXPECT_NEAR(CenterTexel(pixels, size).x, ExpectedTexel(MirrorOf(Vec3(0.7f, 0.0f, 0.25f))).x, 3.0f);
	const GraphicsStats inside = RenderSystem::GetStats();
	EXPECT_EQ(inside.liveTextures - outside.liveTextures, 2u);
	EXPECT_EQ(inside.liveBuffers - outside.liveBuffers, 1u);

	TearDownStage(stage);
}

TEST_F(SkyMaterialFixture, WindowCameraReachesSkyMirrorReflects) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	const Vec3 tint(0.6f, 0.3f, 0.1f);
	MaterialAsset* sky = LoadSky("Tint.mat", ModeTint, tint);
	MaterialAsset* mirror = LoadMaterial("Mirror.mat", ShaderGuid("engine://Shaders/Lit.hlsl"), MIRROR_BODY);
	ASSERT_NE(sky, nullptr);
	ASSERT_NE(mirror, nullptr);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(sky, stage, environment));
	ASSERT_TRUE(AddQuad(stage, mirror, 0.0f));
	stage.sun->SetEnabled(false);
	environment->SetAmbientColor(Color(0.0f, 0.0f, 0.0f, 1.0f));

	WindowDesc windowDesc;
	windowDesc.title = "SkyMaterialTest";
	windowDesc.clientSize = {64, 64};
	windowDesc.flags = WindowFlags::Hidden;
	Handle<Window> window = WindowSystem::Create(windowDesc);
	ASSERT_TRUE(window.IsValid());
	Handle<SceneObject> cameraObject = SceneSystem::CreateObject("Camera", stage.scene);
	cameraObject->SetLocalPosition(ShadingTest::Eye);
	cameraObject->SetLocalRotation(Quat::AngleAxis(180.0f, Vec3::Up()));
	Handle<Camera> camera = cameraObject->AddComponent<Camera>();
	camera->SetTargetWindow(window);
	camera->SetClearColor(Color::Black());
	for (u32 frame = 0; frame < 2; ++frame) {
		WindowSystem::PumpEvents();
		SceneSystem::EndFrame();
		RenderSystem::RenderAll();
	}

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackWindowSceneColor(window, pixels, size));
	const Vec3 centre = RadianceTexel(pixels, size, size.x / 2, size.y / 2);
	const Vec3 expected = MirrorOf(tint);
	EXPECT_NEAR(centre.x, expected.x, 0.02f);
	EXPECT_NEAR(centre.y, expected.y, 0.02f);
	EXPECT_NEAR(centre.z, expected.z, 0.02f);

	TearDownStage(stage);
}

TEST_F(SkyMaterialFixture, BlendTintsSunEveryLayerSees) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* sunSky = LoadSky("SunColor.mat", ModeSunColor);
	ASSERT_NE(sunSky, nullptr);
	ShadingStage stage;
	Handle<Environment> world;
	ASSERT_TRUE(BuildSkyStage(sunSky, stage, world));
	Handle<Environment> zone = AddLayer(stage, sunSky, 1.0f, 0.5f);
	zone->SetSunTint(Color(1.0f, 0.0f, 0.0f, 1.0f));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	ExpectTexelNear(CenterTexel(pixels, size), ExpectedTexel(Vec3(0.25f, 0.125f, 0.125f)), "the sun was not tinted by the blend");

	zone->SetSunIntensityMultiplier(3.0f);
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	ExpectTexelNear(CenterTexel(pixels, size), ExpectedTexel(Vec3(2.0f, 0.5f, 0.5f) * 0.25f), "the zone's intensity multiplier was not blended");

	TearDownStage(stage);
}

TEST_F(SkyMaterialFixture, ProceduralSkiesBlendBakes) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	const Vec3 red(0.8f, 0.05f, 0.05f);
	const Vec3 blue(0.05f, 0.05f, 0.8f);
	MaterialAsset* unused = LoadSky("Unused.mat", ModeTint);
	ASSERT_NE(unused, nullptr);
	ShadingStage stage;
	Handle<Environment> world;
	ASSERT_TRUE(BuildSkyStage(unused, stage, world));
	stage.sun->SetEnabled(false);
	world->SetSkyMode(SkyMode::Color);
	world->SetSkyColor(Color(red.x, red.y, red.z, 1.0f));
	Handle<Environment> upper = AddLayer(stage, unused, 1.0f, 0.5f);
	upper->SetSkyMode(SkyMode::Color);
	upper->SetSkyColor(Color(blue.x, blue.y, blue.z, 1.0f));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	ASSERT_TRUE(Render(stage, QuadView(), pixels, size));
	ExpectTexelNear(Texel(pixels, size, 2, 2), ExpectedTexel(Lerp3(red, blue, 0.5f)), "the 2 skies didn't each draw their color");

	TearDownStage(stage);
}

TEST_F(SkyMaterialFixture, SkyMaterialOnMeshIsError) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* sky = LoadSky("OnAMesh.mat", ModeTint, Vec3(0.0f, 0.0f, 0.0f));
	ASSERT_NE(sky, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(sky, stage));

	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	LogManager::SetGlobalLevel(LogLevel::Trace);
	auto sink = std::make_shared<ShaderDiagnosticSink>();
	LogManager::AddSink(sink);
	ASSERT_TRUE(ArmDiagnosticSink(sink));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	for (i32 frame = 0; frame < 3; ++frame) {
		ASSERT_TRUE(RenderStage(stage, pixels, size));
	}
	const Vec3 center = CenterTexel(pixels, size);

	LogManager::Terminate();
	const str said = sink->Joined();
	sink.reset();
	JobSystem::Terminate();

	EXPECT_GT(center.x, center.y + 80.0f);
	EXPECT_NEAR(center.x, center.z, 2.0f);
	EXPECT_EQ(CountOccurrences(said, "is a sky (drawn behind"), 1u) << said;

	TearDownStage(stage);
}

TEST_F(SkyMaterialFixture, SurfaceMaterialInSkyRowRefused) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* first = LoadMaterial("First.mat", ShaderGuid("engine://Shaders/Unlit.hlsl"),
										"\t\"colors\": { \"baseColor\": [1, 1, 1, 1] }");
	MaterialAsset* second = LoadMaterial("Second.mat", ShaderGuid("engine://Shaders/Unlit.hlsl"),
										 "\t\"colors\": { \"baseColor\": [1, 0, 0, 1] }");
	ASSERT_NE(first, nullptr);
	ASSERT_NE(second, nullptr);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(first, stage, environment));

	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	LogManager::SetGlobalLevel(LogLevel::Trace);
	auto sink = std::make_shared<ShaderDiagnosticSink>();
	LogManager::AddSink(sink);
	ASSERT_TRUE(ArmDiagnosticSink(sink));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ViewDesc desc = QuadView();
	desc.clearColor = Color(0.0f, 0.5f, 0.0f, 1.0f);
	for (i32 frame = 0; frame < 3; ++frame) {
		ASSERT_TRUE(Render(stage, desc, pixels, size));
	}
	const Vec3 corner = Texel(pixels, size, 2, 2);
	environment->SetSkyMaterial(Handle<MaterialAsset>(second));
	SceneSystem::EndFrame();
	for (i32 frame = 0; frame < 3; ++frame) {
		ASSERT_TRUE(Render(stage, desc, pixels, size));
	}

	LogManager::Terminate();
	const str said = sink->Joined();
	sink.reset();
	JobSystem::Terminate();

	ExpectTexelNear(corner, ExpectedTexel(Vec3(0.0f, 0.5f, 0.0f)), "a surface material drew a sky over the clear color");
	EXPECT_EQ(CountOccurrences(said, "isn't a sky"), 2u) << said;

	TearDownStage(stage);
}

TEST_F(SkyMaterialFixture, SkyImportsOneProgramWithTemplatesState) {
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "no shader compiler";
	}
	WriteProjectFile("Blended.hlsl", str("#pragma shalltear_blend alpha\n#pragma shalltear_cull back\n") + SKY_SOURCE);

	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	LogManager::SetGlobalLevel(LogLevel::Trace);
	auto sink = std::make_shared<ShaderDiagnosticSink>();
	LogManager::AddSink(sink);
	ASSERT_TRUE(ArmDiagnosticSink(sink));

	ShaderAsset shader;
	const bool imported = shader.Import(FileSystem::Resolve(Path("project://Blended.hlsl")));
	WriteProjectFile("Plain.hlsl", SKY_SOURCE);
	ShaderAsset plain;
	const bool plainImported = plain.Import(FileSystem::Resolve(Path("project://Plain.hlsl")));
	const bool reimported = plain.Import(FileSystem::Resolve(Path("project://Plain.hlsl")));

	LogManager::Terminate();
	const str said = sink->Joined();
	sink.reset();
	JobSystem::Terminate();

	ASSERT_TRUE(imported) << said;
	ASSERT_TRUE(plainImported) << said;
	ASSERT_TRUE(reimported) << said;
	EXPECT_EQ(shader.integrator, ShaderTemplate::Sky);
	const ShaderVariant* base = shader.GetVariant(SHADER_BASE_VARIANT);
	ASSERT_NE(base, nullptr);
	EXPECT_TRUE(base->HasProgram(ShaderProgramKind::Forward));
	EXPECT_FALSE(base->HasProgram(ShaderProgramKind::Depth));
	EXPECT_EQ(CountOccurrences(said, "doesn't build"), 0u) << said;
	EXPECT_TRUE(base->depthValuePixelSpirv.empty());
	for (const str& keyword : shader.keywords) {
		EXPECT_NE(keyword, ShaderAlphaClipKeyword);
	}
	EXPECT_EQ(shader.renderState.blend, RHI::BlendMode::Opaque);
	EXPECT_EQ(shader.renderState.depth, RHI::DepthMode::TestOnly);
	EXPECT_EQ(shader.renderState.cull, RHI::CullMode::Off);
	EXPECT_EQ(CountOccurrences(said, "pragmas ignored"), 1u) << said;
}

TEST_F(SkyMaterialFixture, ACookedSkyMaterialDrawsItsSky) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* sky = LoadSky("Shipped.mat", ModeTint, Vec3(0.9f, 0.1f, 0.1f));
	ASSERT_NE(sky, nullptr);
	ASSERT_NE(sky->shader.Get(), nullptr);
	const Guid shaderGuid = sky->shader->assetId;

	Vector<Guid> roots{GetMemoryResource(MemTag::Editor)};
	roots.push_back(sky->assetId);
	CookOps::CookClosure closure;
	str error;
	ASSERT_TRUE(CookOps::ComputeClosure(roots, closure, error)) << error;
	Vector<u8> package{GetMemoryResource(MemTag::Editor)};
	ASSERT_TRUE(CookOps::CookPackage(closure, CookOps::PakWriteDesc{}, package, error)) << error;
	const std::filesystem::path file = _root / "Shipped.slpak";
	Buffer bytes(package.size(), MemTag::Editor);
	std::memcpy(bytes.data, package.data(), package.size());
	ASSERT_TRUE(FileSystem::WriteFile(Path(file.generic_string()), bytes));

	PakReader reader;
	ASSERT_TRUE(reader.Mount(Path(file.generic_string()), error)) << error;
	const u32 index = reader.FindAsset(shaderGuid);
	ASSERT_NE(PAK_INVALID_INDEX, index);
	const PakAssetEntry* entry = reader.GetAsset(index);
	ASSERT_NE(entry, nullptr);
	Buffer payloadBytes;
	ASSERT_TRUE(reader.ReadRegion(entry->regionIndex, payloadBytes, error)) << error;
	CookReader descriptor = reader.GetDescriptor(index);
	CookReader payload(payloadBytes.data, payloadBytes.size);
	ShaderAsset* shipped = New<ShaderAsset>(MemTag::Asset);
	ASSERT_TRUE(shipped->LoadCooked(descriptor, payload));
	EXPECT_EQ(shipped->integrator, ShaderTemplate::Sky);

	const Handle<ShaderAsset> imported = sky->shader;
	sky->shader = Handle<ShaderAsset>(shipped);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(sky, stage, environment));
	ViewDesc desc = QuadView();
	desc.clearColor = Color(0.0f, 0.5f, 0.0f, 1.0f);
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, desc, pixels, size));
	ExpectTexelNear(CenterTexel(pixels, size), ExpectedTexel(Vec3(0.9f, 0.1f, 0.1f)), "a cooked sky material drew no sky");

	TearDownStage(stage);
	sky->shader = imported;
	Delete(shipped);
}
