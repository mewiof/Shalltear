#include "TestsPCH.h"

#include "ShadingHarness.h"

#include "Graphics/EnvironmentIBL.h"

#include "Shalltear/Core/Graphics/Environment.h"
#include "Shalltear/Core/Graphics/PrimitiveMesh.h"
#include "Shalltear/Core/Jobs/JobSystem.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include <chrono>
#include <cmath>
#include <filesystem>
#include <format>
#include <fstream>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

using namespace Shalltear;
using ShadingTest::CenterTexel;
using ShadingTest::EnvBRDFApprox;
using ShadingTest::ExpectedTexel;
using ShadingTest::MaterialReadbackFixture;
using ShadingTest::MIN_PERCEPTUAL_ROUGHNESS;
using ShadingTest::ShadingStage;
using ShadingTest::WriteRadianceHDR;
using Shalltear::GraphicsInternal::BuildCubeFromGradient;
using Shalltear::GraphicsInternal::EvaluateIrradianceSH;
using Shalltear::GraphicsInternal::IrradianceSH;
using Shalltear::GraphicsInternal::PrefilterRadianceCube;
using Shalltear::GraphicsInternal::RadianceCube;
using Shalltear::GraphicsInternal::SkyGradient;

namespace {

	constexpr u32 PROCEDURAL_SKY_FACE_SIZE = 64;

	SkyGradient TestGradient() {
		SkyGradient gradient;
		gradient.sky = Vec3(0.10f, 0.20f, 0.80f);
		gradient.equator = Vec3(0.60f, 0.55f, 0.25f);
		gradient.ground = Vec3(0.02f, 0.02f, 0.02f);
		return gradient;
	}

	IrradianceSH GradientIrradiance(const SkyGradient& gradient) {
		RadianceCube cube;
		BuildCubeFromGradient(gradient, PROCEDURAL_SKY_FACE_SIZE, cube);
		IrradianceSH sh;
		PrefilterRadianceCube(cube, sh);
		return sh;
	}

	Handle<Environment> AddEnvironment(ShadingStage& stage) {
		Handle<SceneObject> object = SceneSystem::CreateObject("Environment", stage.scene);
		Handle<Environment> environment = object->AddComponent<Environment>();
		SceneSystem::EndFrame();
		return environment;
	}

	class EnvironmentWarningSink : public ILogSink {
	public:
		static constexpr const char* SENTINEL = "environment-sink-sentinel";

		void ReceiveLog(const LogEvent& event) override {
			std::lock_guard<std::mutex> lock(_mutex);
			if (event.message.find(SENTINEL) != str::npos) {
				_armed = true;
				return;
			}
			if (_armed) {
				_messages.push_back(str(event.message));
			}
		}
		void Flush() override {}

		bool Armed() const {
			std::lock_guard<std::mutex> lock(_mutex);
			return _armed;
		}
		u32 CountMatching(std::string_view needle) const {
			std::lock_guard<std::mutex> lock(_mutex);
			u32 count = 0;
			for (const str& message : _messages) {
				if (message.find(needle) != str::npos) {
					++count;
				}
			}
			return count;
		}
	private:
		mutable std::mutex _mutex;
		std::vector<str> _messages;
		bool _armed = false;
	};

	bool ArmEnvironmentSink(const std::shared_ptr<EnvironmentWarningSink>& sink) {
		SLT_LOG_WARN("Graphics", "{}", EnvironmentWarningSink::SENTINEL);
		const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
		while (std::chrono::steady_clock::now() < deadline) {
			if (sink->Armed()) {
				return true;
			}
			std::this_thread::sleep_for(std::chrono::milliseconds(5));
		}
		return false;
	}

	Guid LitShaderGuid() {
		Asset* shader = AssetDatabase::LoadAsset(Path("engine://Shaders/Lit.hlsl"));
		return shader ? shader->assetId : Guid{};
	}

} // anonymous namespace

class EnvironmentReadbackFixture : public MaterialReadbackFixture {};

TEST_F(EnvironmentReadbackFixture, MirrorReflectsCubeFaceNormalPointsAt) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* material = LoadMaterial("Mirror.mat", LitShaderGuid(),
										   "\t\"floats\": { \"metallic\": 1, \"roughness\": 0 },\n"
										   "\t\"colors\": { \"baseColor\": [1, 1, 1, 1] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.sun->SetEnabled(false);

	const SkyGradient gradient = TestGradient();
	Handle<Environment> environment = AddEnvironment(stage);
	ASSERT_TRUE(environment.IsValid());
	environment->SetSkyMode(SkyMode::Gradient);
	environment->SetSkyColor(Color(gradient.sky.x, gradient.sky.y, gradient.sky.z, 1.0f));
	environment->SetEquatorColor(Color(gradient.equator.x, gradient.equator.y, gradient.equator.z, 1.0f));
	environment->SetGroundColor(Color(gradient.ground.x, gradient.ground.y, gradient.ground.z, 1.0f));
	environment->SetSunDiscSizeDeg(0.0f);
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));

	const Vec3 reflected = Shalltear::GraphicsInternal::EvaluateSkyGradient(gradient, Vec3(0.0f, 0.0f, 1.0f));
	const Vec2 dfg = EnvBRDFApprox(MIN_PERCEPTUAL_ROUGHNESS, 1.0f);
	const Vec3 expectedLinear = reflected * (1.0f * dfg.x + dfg.y);
	const Vec3 expected = ExpectedTexel(expectedLinear);
	const Vec3 measured = CenterTexel(pixels, size);
	if (measured.x + measured.y + measured.z == 0.0f) {
		TearDownStage(stage);
		FAIL() << "quad didn't draw";
	}
	EXPECT_NEAR(measured.x, expected.x, 4.0f);
	EXPECT_NEAR(measured.y, expected.y, 4.0f);
	EXPECT_NEAR(measured.z, expected.z, 4.0f);
	const Vec3 zenith = ExpectedTexel(Shalltear::GraphicsInternal::EvaluateSkyGradient(gradient, Vec3(0.0f, 1.0f, 0.0f)) *
									  (dfg.x + dfg.y));
	EXPECT_GT(std::fabs(measured.z - zenith.z), 8.0f);

	TearDownStage(stage);
}

TEST_F(EnvironmentReadbackFixture, RoughSurfaceReadsIrradianceProjectionProduced) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	const Vec3 baseColor(0.8f, 0.6f, 0.4f);
	MaterialAsset* material = LoadMaterial("Rough.mat", LitShaderGuid(),
										   "\t\"floats\": { \"metallic\": 0, \"roughness\": 1 },\n"
										   "\t\"colors\": { \"baseColor\": [0.8, 0.6, 0.4, 1] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.sun->SetEnabled(false);

	const SkyGradient gradient = TestGradient();
	Handle<Environment> environment = AddEnvironment(stage);
	ASSERT_TRUE(environment.IsValid());
	environment->SetSkyMode(SkyMode::Gradient);
	environment->SetSkyColor(Color(gradient.sky.x, gradient.sky.y, gradient.sky.z, 1.0f));
	environment->SetEquatorColor(Color(gradient.equator.x, gradient.equator.y, gradient.equator.z, 1.0f));
	environment->SetGroundColor(Color(gradient.ground.x, gradient.ground.y, gradient.ground.z, 1.0f));
	environment->SetSunDiscSizeDeg(0.0f);
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));

	const IrradianceSH sh = GradientIrradiance(gradient);
	const Vec3 irradiance = EvaluateIrradianceSH(sh, Vec3(0.0f, 0.0f, 1.0f));
	const Vec2 dfg = EnvBRDFApprox(1.0f, 1.0f);
	const f32 f0 = 0.16f * 0.5f * 0.5f;
	const Vec3 specular = Shalltear::GraphicsInternal::EvaluateSkyGradient(gradient, Vec3(0.0f, 0.0f, 1.0f)) * 0.0f;
	(void)specular;
	const Vec3 measured = CenterTexel(pixels, size);
	if (measured.x + measured.y + measured.z == 0.0f) {
		TearDownStage(stage);
		FAIL() << "quad didn't draw";
	}
	const f32 specularCeiling = 0.80f * (f0 * dfg.x + dfg.y);
	const Vec3 diffuseOnly = ExpectedTexel(baseColor * irradiance);
	const Vec3 withCeiling = ExpectedTexel(baseColor * irradiance + Vec3(specularCeiling, specularCeiling, specularCeiling));
	EXPECT_GE(measured.x, diffuseOnly.x - 3.0f);
	EXPECT_LE(measured.x, withCeiling.x + 3.0f);
	EXPECT_GE(measured.z, diffuseOnly.z - 3.0f);
	EXPECT_LE(measured.z, withCeiling.z + 3.0f);
	EXPECT_GT(measured.x, measured.z + 4.0f);

	TearDownStage(stage);
}

TEST_F(EnvironmentReadbackFixture, ColorAmbientReplacesDiffuseOnly) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* material = LoadMaterial("Diffuse.mat", LitShaderGuid(),
										   "\t\"floats\": { \"metallic\": 0, \"roughness\": 1 },\n"
										   "\t\"colors\": { \"baseColor\": [1, 1, 1, 1] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.sun->SetEnabled(false);

	const SkyGradient gradient = TestGradient();
	Handle<Environment> environment = AddEnvironment(stage);
	environment->SetSkyMode(SkyMode::Gradient);
	environment->SetSkyColor(Color(gradient.sky.x, gradient.sky.y, gradient.sky.z, 1.0f));
	environment->SetEquatorColor(Color(gradient.equator.x, gradient.equator.y, gradient.equator.z, 1.0f));
	environment->SetGroundColor(Color(gradient.ground.x, gradient.ground.y, gradient.ground.z, 1.0f));
	environment->SetSunDiscSizeDeg(0.0f);
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 fromSky = CenterTexel(pixels, size);

	environment->SetAmbientMode(AmbientMode::Color);
	environment->SetAmbientColor(Color(0.0f, 1.0f, 0.0f, 1.0f));
	SceneSystem::EndFrame();
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 fromColor = CenterTexel(pixels, size);

	EXPECT_GT(fromColor.y, fromSky.y + 8.0f);
	EXPECT_LT(fromColor.x, fromSky.x - 8.0f);
}

TEST_F(EnvironmentReadbackFixture, SecondEnvironmentIgnored) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* material = LoadMaterial("Diffuse.mat", LitShaderGuid(),
										   "\t\"floats\": { \"metallic\": 0, \"roughness\": 1 },\n"
										   "\t\"colors\": { \"baseColor\": [1, 1, 1, 1] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.sun->SetEnabled(false);

	Handle<Environment> first = AddEnvironment(stage);
	first->SetSkyMode(SkyMode::Color);
	first->SetAmbientMode(AmbientMode::Color);
	first->SetAmbientColor(Color(0.5f, 0.0f, 0.0f, 1.0f));
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 alone = CenterTexel(pixels, size);
	EXPECT_GT(alone.x, alone.y + 20.0f);

	Handle<Environment> second = AddEnvironment(stage);
	second->SetSkyMode(SkyMode::Color);
	second->SetAmbientMode(AmbientMode::Color);
	second->SetAmbientColor(Color(0.0f, 0.0f, 0.5f, 1.0f));
	SceneSystem::EndFrame();
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 withSecond = CenterTexel(pixels, size);
	EXPECT_NEAR(withSecond.x, alone.x, 1.5f);
	EXPECT_NEAR(withSecond.z, alone.z, 1.5f);

	first->SetEnabled(false);
	SceneSystem::EndFrame();
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 afterHandover = CenterTexel(pixels, size);
	EXPECT_GT(afterHandover.z, afterHandover.x + 20.0f);

	TearDownStage(stage);
}

TEST_F(EnvironmentReadbackFixture, SecondEnvironmentReportedOnce) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* material = LoadMaterial("Diffuse.mat", LitShaderGuid(),
										   "\t\"floats\": { \"metallic\": 0, \"roughness\": 1 },\n"
										   "\t\"colors\": { \"baseColor\": [1, 1, 1, 1] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));

	JobSystem::Initialize();
	LogManager::Initialize({false, false});
	LogManager::SetGlobalLevel(LogLevel::Trace);
	auto sink = std::make_shared<EnvironmentWarningSink>();
	LogManager::AddSink(sink);
	ASSERT_TRUE(ArmEnvironmentSink(sink));

	AddEnvironment(stage);
	Handle<Environment> second = AddEnvironment(stage);
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	for (i32 frame = 0; frame < 5; ++frame) {
		ASSERT_TRUE(RenderStage(stage, pixels, size));
	}

	second->SetEnabled(false);
	SceneSystem::EndFrame();
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	second->SetEnabled(true);
	SceneSystem::EndFrame();
	for (i32 frame = 0; frame < 5; ++frame) {
		ASSERT_TRUE(RenderStage(stage, pixels, size));
	}

	LogManager::Terminate();
	ASSERT_GT(sink->CountMatching(""), 0u);
	EXPECT_EQ(sink->CountMatching("a 2nd Environment"), 2u);
	sink.reset();
	JobSystem::Terminate();

	TearDownStage(stage);
}

TEST_F(EnvironmentReadbackFixture, NoEnvironmentKeepsFlatAmbient) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* material = LoadMaterial("Diffuse.mat", LitShaderGuid(),
										   "\t\"floats\": { \"metallic\": 0, \"roughness\": 1 },\n"
										   "\t\"colors\": { \"baseColor\": [1, 1, 1, 1] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.sun->SetEnabled(false);
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));

	const Vec3 diffuseOnly = ExpectedTexel(Vec3(ShadingTest::VIEW_AMBIENT, ShadingTest::VIEW_AMBIENT, ShadingTest::VIEW_AMBIENT));
	const Vec3 measured = CenterTexel(pixels, size);
	EXPECT_GE(measured.x, diffuseOnly.x - 2.0f);
	EXPECT_LE(measured.x, diffuseOnly.x + 8.0f);
	EXPECT_NEAR(measured.x, measured.y, 1.5f);

	const size_t corner = (2u * static_cast<size_t>(size.x) + 2u) * 4u;
	EXPECT_EQ(pixels[corner], 0u);
	EXPECT_EQ(pixels[corner + 1], 0u);
	EXPECT_EQ(pixels[corner + 2], 0u);

	TearDownStage(stage);
}

TEST_F(EnvironmentReadbackFixture, SkyFillsUncoveredPixels) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* material = LoadMaterial("Black.mat", LitShaderGuid(),
										   "\t\"floats\": { \"metallic\": 0, \"roughness\": 1 },\n"
										   "\t\"colors\": { \"baseColor\": [0, 0, 0, 1] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.sun->SetEnabled(false);

	Handle<Environment> environment = AddEnvironment(stage);
	environment->SetSkyMode(SkyMode::Color);
	environment->SetSkyColor(Color(0.9f, 0.1f, 0.1f, 1.0f));
	environment->SetAmbientMode(AmbientMode::Color);
	environment->SetAmbientColor(Color(0.0f, 0.0f, 0.0f, 1.0f));
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));

	const size_t corner = (2u * static_cast<size_t>(size.x) + 2u) * 4u;
	EXPECT_GT(pixels[corner], 150u);
	EXPECT_LT(pixels[corner + 1], 120u);

	const Vec3 center = CenterTexel(pixels, size);
	EXPECT_LT(center.x, 40.0f);

	TearDownStage(stage);
}

TEST_F(EnvironmentReadbackFixture, BlendedSurfaceCompositesOverSky) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* material = LoadMaterial("HalfBlack.mat", LitShaderGuid(),
										   "\t\"blend\": \"alpha\",\n"
										   "\t\"floats\": { \"metallic\": 0, \"roughness\": 1 },\n"
										   "\t\"colors\": { \"baseColor\": [0, 0, 0, 0.5] }");
	ASSERT_NE(material, nullptr);
	ASSERT_TRUE(material->blend.has_value());

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.sun->SetEnabled(false);

	Handle<Environment> environment = AddEnvironment(stage);
	environment->SetSkyMode(SkyMode::Color);
	environment->SetSkyColor(Color(0.9f, 0.1f, 0.1f, 1.0f));
	environment->SetAmbientMode(AmbientMode::Color);
	environment->SetAmbientColor(Color(0.0f, 0.0f, 0.0f, 1.0f));
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));

	const size_t corner = (2u * static_cast<size_t>(size.x) + 2u) * 4u;
	const f32 skyRed = static_cast<f32>(pixels[corner]);
	const Vec3 center = CenterTexel(pixels, size);
	ASSERT_GT(skyRed, 150.0f);
	EXPECT_GT(center.x, 60.0f);
	EXPECT_LT(center.x, skyRed * 0.85f);

	TearDownStage(stage);
}

TEST_F(EnvironmentReadbackFixture, SunDiscInSkyNotIrradiance) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* material = LoadMaterial("Black.mat", LitShaderGuid(),
										   "\t\"colors\": { \"baseColor\": [0, 0, 0, 1] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.sun->GetOwner()->SetLocalRotation(Quat::Identity());
	stage.sun->SetIntensity(4.0f);
	stage.sun->SetColor(Color(0.0f, 1.0f, 0.0f, 1.0f));

	Handle<Environment> environment = AddEnvironment(stage);
	environment->SetSkyMode(SkyMode::Color);
	environment->SetSkyColor(Color(0.0f, 0.0f, 0.05f, 1.0f));
	environment->SetSunDiscSizeDeg(60.0f);
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const size_t above = (4u * static_cast<size_t>(size.x) + static_cast<size_t>(size.x) / 2u) * 4u;
	const u8 withDisc = pixels[above + 1];

	environment->SetSunDiscSizeDeg(0.0f);
	SceneSystem::EndFrame();
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const u8 withoutDisc = pixels[above + 1];

	EXPECT_GT(withDisc, withoutDisc + 20u);

	TearDownStage(stage);
}

namespace {

	Vec3 ForwardGreenBackRed(f32 u, f32 v) {
		(void)v;
		return (u >= 0.25f && u < 0.75f) ? Vec3(0.0f, 0.7f, 0.0f) : Vec3(0.7f, 0.0f, 0.0f);
	}

} // anonymous namespace

TEST_F(EnvironmentReadbackFixture, EquirectImportsAsConvolvedCube) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	WriteRadianceHDR(_root / "Sky.hdr", 256, 128, ForwardGreenBackRed);

	Asset* asset = AssetDatabase::LoadAsset(Path("project://Sky.hdr"));
	ASSERT_NE(asset, nullptr);
	TextureAsset* cubemap = static_cast<TextureAsset*>(asset);

	EXPECT_TRUE(cubemap->isCube);
	EXPECT_EQ(cubemap->format, RHI::Format::BC6HUFloat);
	EXPECT_EQ(cubemap->size.x, 64);
	EXPECT_EQ(cubemap->size.y, 64);
	EXPECT_EQ(cubemap->storedMipLevels, Shalltear::GraphicsInternal::RadianceCubeMipCount(64));
	ASSERT_EQ(cubemap->irradianceSH.size(), 9u);
	u64 chainBytes = 0;
	for (u32 mip = 0; mip < cubemap->storedMipLevels; ++mip) {
		const i32 edge = std::max(1, 64 >> mip);
		chainBytes += RHI::FormatLevelBytes(RHI::Format::BC6HUFloat, {edge, edge}) * 6;
	}
	EXPECT_EQ(cubemap->pixels.size(), chainBytes);
	EXPECT_LT(chainBytes * 7, Shalltear::GraphicsInternal::RadianceCubeBytes(64, cubemap->storedMipLevels));

	MaterialAsset* material = LoadMaterial("CubeMirror.mat", LitShaderGuid(),
										   "\t\"floats\": { \"metallic\": 1, \"roughness\": 0 },\n"
										   "\t\"colors\": { \"baseColor\": [1, 1, 1, 1] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.sun->SetEnabled(false);

	Handle<Environment> environment = AddEnvironment(stage);
	environment->SetSkyMode(SkyMode::Cubemap);
	environment->SetSkyCubemap(Handle<TextureAsset>(cubemap));
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));

	const Vec3 measured = CenterTexel(pixels, size);
	EXPECT_GT(measured.y, 60.0f);
	EXPECT_GT(measured.y, measured.x + 40.0f);

	TearDownStage(stage);
}

namespace {

	Vec3 StructuredSky(f32 u, f32 v) {
		const f32 stripes = 0.6f + 0.4f * (0.5f + 0.5f * std::sin(u * 48.0f));
		const Vec3 sky = (Vec3(0.2f, 0.35f, 0.8f) * (1.0f - v) + Vec3(0.6f, 0.5f, 0.3f) * v) * stripes;
		const f32 du = u - 0.53f;
		const f32 dv = v - 0.47f;
		const f32 sun = 40.0f * std::exp(-(du * du + dv * dv) * 3000.0f);
		return sky + Vec3(sun, sun * 0.9f, sun * 0.7f);
	}

} // anonymous namespace

TEST_F(EnvironmentReadbackFixture, BC6HCubeMatchesHalfFloat) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	WriteRadianceHDR(_root / "Sky.hdr", 256, 128, StructuredSky);
	WriteRadianceHDR(_root / "SkyRaw.hdr", 256, 128, StructuredSky);
	const Path rawPath("project://SkyRaw.hdr");
	ASSERT_TRUE(AssetDatabase::EnsureAssetIdentity(rawPath).IsValid());
	JSONValue uncompressed = JSONValue::MakeObject();
	uncompressed.AddMember("compression", JSONValue::MakeString("none"));
	ASSERT_TRUE(AssetDatabase::WriteImporterSettings(rawPath, uncompressed));
	Asset* compressedAsset = AssetDatabase::LoadAsset(Path("project://Sky.hdr"));
	Asset* rawAsset = AssetDatabase::LoadAsset(rawPath);
	ASSERT_NE(compressedAsset, nullptr);
	ASSERT_NE(rawAsset, nullptr);
	TextureAsset* compressed = static_cast<TextureAsset*>(compressedAsset);
	TextureAsset* raw = static_cast<TextureAsset*>(rawAsset);
	ASSERT_TRUE(compressed->isCube && raw->isCube);
	ASSERT_EQ(compressed->format, RHI::Format::BC6HUFloat);
	ASSERT_EQ(raw->format, RHI::Format::RGBA16Float);
	ASSERT_EQ(compressed->storedMipLevels, raw->storedMipLevels);
	ASSERT_GT(compressed->storedMipLevels, 1u);
	ASSERT_TRUE(compressed->texture.IsValid());

	auto render = [&](MaterialAsset* material, TextureAsset* cube, Vector<u8>& pixels) {
		ShadingStage stage;
		if (!BuildStage(material, stage)) {
			return false;
		}
		stage.sun->SetEnabled(false);
		Handle<Environment> environment = AddEnvironment(stage);
		environment->SetSkyMode(cube ? SkyMode::Cubemap : SkyMode::None);
		if (cube) {
			environment->SetSkyCubemap(Handle<TextureAsset>(cube));
		}
		SceneSystem::EndFrame();
		Vec2I size{};
		const bool rendered = RenderStage(stage, pixels, size);
		TearDownStage(stage);
		return rendered && size == Vec2I(64, 64);
	};

	const f32 roughnesses[] = {0.0f, 0.5f};
	for (f32 roughness : roughnesses) {
		const str name = std::format("Metal{}.mat", static_cast<i32>(roughness * 10.0f));
		MaterialAsset* material = LoadMaterial(name.c_str(), LitShaderGuid(),
											   std::format("\t\"floats\": {{ \"metallic\": 1, \"roughness\": {} }},\n"
														   "\t\"colors\": {{ \"baseColor\": [1, 1, 1, 1] }}",
														   roughness));
		ASSERT_NE(material, nullptr);
		Vector<u8> fromBlocks{GetMemoryResource(MemTag::Graphics)};
		Vector<u8> fromHalves{GetMemoryResource(MemTag::Graphics)};
		Vector<u8> unlit{GetMemoryResource(MemTag::Graphics)};
		ASSERT_TRUE(render(material, compressed, fromBlocks));
		ASSERT_TRUE(render(material, raw, fromHalves));
		ASSERT_TRUE(render(material, nullptr, unlit));

		i32 worst = 0;
		i32 reflected = 0;
		u64 total = 0;
		u32 covered = 0;
		for (size_t i = 0; i < fromHalves.size(); i += 4) {
			const bool lit = fromHalves[i] != 0 || fromHalves[i + 1] != 0 || fromHalves[i + 2] != 0;
			covered += lit ? 1u : 0u;
			for (u32 c = 0; c < 3; ++c) {
				const i32 difference = std::abs(static_cast<i32>(fromBlocks[i + c]) - static_cast<i32>(fromHalves[i + c]));
				worst = std::max(worst, difference);
				total += lit ? static_cast<u64>(difference) : 0u;
				reflected = std::max(reflected, std::abs(static_cast<i32>(fromHalves[i + c]) - static_cast<i32>(unlit[i + c])));
			}
		}
		ASSERT_GE(covered, 200u);
		const f64 mean = static_cast<f64>(total) / static_cast<f64>(covered * 3);
		EXPECT_LE(worst, 6) << "roughness " << roughness << ' ' << worst << " of 255";
		EXPECT_LT(mean, 1.0) << "roughness " << roughness << ' ' << mean;
		EXPECT_GE(reflected, 40);
		AssetDatabase::UnloadAsset(material);
	}
	AssetDatabase::UnloadAsset(compressedAsset);
	AssetDatabase::UnloadAsset(rawAsset);
}

TEST_F(EnvironmentReadbackFixture, IsolatedSceneUsesOwnEnvironment) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* material = LoadMaterial("Diffuse.mat", LitShaderGuid(),
										   "\t\"floats\": { \"metallic\": 0, \"roughness\": 1 },\n"
										   "\t\"colors\": { \"baseColor\": [1, 1, 1, 1] }");
	ASSERT_NE(material, nullptr);

	Handle<Scene> world = SceneSystem::CreateScene("World");
	Handle<SceneObject> worldEnvironmentObject = SceneSystem::CreateObject("Environment", world);
	Handle<Environment> worldEnvironment = worldEnvironmentObject->AddComponent<Environment>();
	worldEnvironment->SetSkyMode(SkyMode::None);
	worldEnvironment->SetAmbientMode(AmbientMode::Color);
	worldEnvironment->SetAmbientColor(Color(1.0f, 0.0f, 0.0f, 1.0f));

	Handle<Scene> isolated = SceneSystem::CreateIsolatedScene("Preview");
	Asset* quadMesh = AssetDatabase::LoadAsset(Path(GetPrimitiveMeshPath(PrimitiveShape::Quad)));
	ASSERT_NE(quadMesh, nullptr);
	Handle<SceneObject> quad = SceneSystem::CreateObject("Quad", isolated);
	Handle<MeshRenderer> renderer = quad->AddComponent<MeshRenderer>();
	renderer->SetMesh(Handle<MeshAsset>(static_cast<MeshAsset*>(quadMesh)));
	renderer->SetMaterial(0, Handle<MaterialAsset>(material));
	Handle<SceneObject> isolatedEnvironmentObject = SceneSystem::CreateObject("Environment", isolated);
	Handle<Environment> isolatedEnvironment = isolatedEnvironmentObject->AddComponent<Environment>();
	isolatedEnvironment->SetSkyMode(SkyMode::None);
	isolatedEnvironment->SetAmbientMode(AmbientMode::Color);
	isolatedEnvironment->SetAmbientColor(Color(0.0f, 0.0f, 1.0f, 1.0f));
	SceneSystem::EndFrame();

	ViewTargetId target = RenderSystem::CreateViewTarget({64, 64});
	ASSERT_NE(target, INVALID_VIEW_TARGET);
	ViewDesc desc = ShadingTest::QuadView();
	Handle<Scene> only[] = {isolated};
	desc.scenes = {only, 1};
	RenderSystem::SubmitView(target, desc);
	RenderSystem::RenderAll();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(target, pixels, size));
	const Vec3 measured = CenterTexel(pixels, size);
	EXPECT_GT(measured.z, measured.x + 40.0f);

	RenderSystem::DestroyViewTarget(target);
	SceneSystem::UnloadScene(isolated);
	SceneSystem::UnloadScene(world);
	SceneSystem::EndFrame();
}

TEST_F(EnvironmentReadbackFixture, SkyNoneLightsAndKeepsClear) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* material = LoadMaterial("Chrome.mat", LitShaderGuid(),
										   "\t\"floats\": { \"metallic\": 1, \"roughness\": 0 },\n"
										   "\t\"colors\": { \"baseColor\": [1, 1, 1, 1] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.sun->SetEnabled(false);

	Handle<Environment> environment = AddEnvironment(stage);
	environment->SetSkyMode(SkyMode::None);
	environment->SetAmbientMode(AmbientMode::Color);
	environment->SetAmbientColor(Color(0.4f, 0.4f, 0.4f, 1.0f));
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size, Color(0.0f, 0.25f, 0.0f, 1.0f)));

	const Vec2 dfg = EnvBRDFApprox(MIN_PERCEPTUAL_ROUGHNESS, 1.0f);
	const Vec3 expected = ExpectedTexel(Vec3(0.4f, 0.4f, 0.4f) * (dfg.x + dfg.y));
	const Vec3 measured = CenterTexel(pixels, size);
	EXPECT_NEAR(measured.x, expected.x, 4.0f);

	const size_t corner = (2u * static_cast<size_t>(size.x) + 2u) * 4u;
	EXPECT_LT(pixels[corner], 20u);
	EXPECT_GT(pixels[corner + 1], 100u);

	TearDownStage(stage);
}
