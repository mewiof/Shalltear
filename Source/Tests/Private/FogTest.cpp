#include "TestsPCH.h"

#include "ShadingHarness.h"

#include "Shalltear/Core/Graphics/Environment.h"

#include <cmath>

using namespace Shalltear;
using ShadingTest::CenterTexel;
using ShadingTest::ExpectedTexel;
using ShadingTest::Eye;
using ShadingTest::MaterialReadbackFixture;
using ShadingTest::QuadView;
using ShadingTest::ShadingStage;

namespace {

	const Vec3 SURFACE{0.80f, 0.20f, 0.10f};
	const Vec3 FOG{0.10f, 0.30f, 0.90f};
	const Vec3 OTHER_FOG{0.70f, 0.60f, 0.05f};

	constexpr f32 CENTER_DISTANCE = 3.0f;

	constexpr f32 TEXEL_TOLERANCE = 1.5f;

	void ExpectTexelNear(const Vec3& actual, const Vec3& expected, const char* what) {
		EXPECT_NEAR(actual.x, expected.x, TEXEL_TOLERANCE) << what;
		EXPECT_NEAR(actual.y, expected.y, TEXEL_TOLERANCE) << what;
		EXPECT_NEAR(actual.z, expected.z, TEXEL_TOLERANCE) << what;
	}

	f32 LinearVisibility(f32 start, f32 end, f32 distance) {
		return Math::Clamp((end - distance) / (end - start), 0.0f, 1.0f);
	}
	f32 ExponentialVisibility(f32 density, f32 distance) {
		return std::exp(-density * distance);
	}
	f32 ExponentialSquaredVisibility(f32 density, f32 distance) {
		return std::exp(-(density * distance) * (density * distance));
	}

	Vec3 Fogged(const Vec3& surface, const Vec3& fog, f32 visibility) {
		return surface * visibility + fog * (1.0f - visibility);
	}

	Vec3 Texel(const Vector<u8>& pixels, Vec2I size, i32 x, i32 y) {
		const size_t index = (static_cast<size_t>(y) * size.x + x) * 4;
		return Vec3(static_cast<f32>(pixels[index]), static_cast<f32>(pixels[index + 1]), static_cast<f32>(pixels[index + 2]));
	}

	f32 SRGBToLinearChannel(f32 encoded) {
		const f32 c = encoded / 255.0f;
		return c <= 0.04045f ? c / 12.92f : std::pow((c + 0.055f) / 1.055f, 2.4f);
	}

	f32 DistanceToQuadPlane(const ViewDesc& desc, Vec2I size, i32 x, i32 y) {
		const f32 ndcX = (static_cast<f32>(x) + 0.5f) / static_cast<f32>(size.x) * 2.0f - 1.0f;
		const f32 ndcY = 1.0f - (static_cast<f32>(y) + 0.5f) / static_cast<f32>(size.y) * 2.0f;
		const Mat4 inverse = (desc.projection * desc.view).Inverse();
		const Vec4 farPoint = inverse * Vec4(ndcX, ndcY, 0.0f, 1.0f);
		const Vec3 point(farPoint.x / farPoint.w, farPoint.y / farPoint.w, farPoint.z / farPoint.w);
		const Vec3 ray = (point - desc.cameraPosition).Normalized();
		return -desc.cameraPosition.z / ray.z;
	}

	Guid ShaderGuid(const char* path) {
		Asset* shader = AssetDatabase::LoadAsset(Path(path));
		return shader ? shader->assetId : Guid{};
	}

	Handle<Environment> AddEnvironment(ShadingStage& stage, f32 priority = 0.0f, f32 weight = 1.0f) {
		Handle<SceneObject> object = SceneSystem::CreateObject("Environment", stage.scene);
		Handle<Environment> environment = object->AddComponent<Environment>();
		environment->SetSkyMode(SkyMode::None);
		environment->SetPriority(priority);
		environment->SetWeight(weight);
		SceneSystem::EndFrame();
		return environment;
	}

	void SetFog(Environment& environment, FogMode mode, const Vec3& color) {
		environment.SetFog(true);
		environment.SetFogMode(mode);
		environment.SetFogColor(Color(color.x, color.y, color.z, 1.0f));
	}

	class FogReadbackFixture : public MaterialReadbackFixture {
	protected:
		MaterialAsset* LoadUnlit(const char* fileName, const Vec3& color, f32 alpha = 1.0f, const char* blend = nullptr) {
			str body = std::format("\t\"colors\": {{ \"baseColor\": [{}, {}, {}, {}] }}", color.x, color.y, color.z, alpha);
			if (blend) {
				body += std::format(",\n\t\"blend\": \"{}\"", blend);
			}
			return LoadMaterial(fileName, ShaderGuid("engine://Shaders/Unlit.hlsl"), body);
		}

		bool RenderCenter(const ShadingStage& stage, const ViewDesc& desc, Vec3& out) {
			Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
			Vec2I size{};
			RenderSystem::SubmitView(stage.target, desc);
			RenderSystem::RenderAll();
			if (!RenderSystem::ReadbackViewTarget(stage.target, pixels, size)) {
				return false;
			}
			out = CenterTexel(pixels, size);
			return true;
		}
	};

} // anonymous namespace

TEST_F(FogReadbackFixture, EachModeLetsThroughWhatCurveSays) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* material = LoadUnlit("Surface.mat", SURFACE);
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	Handle<Environment> environment = AddEnvironment(stage);

	Vec3 texel;
	ASSERT_TRUE(RenderCenter(stage, QuadView(), texel));
	ExpectTexelNear(texel, ExpectedTexel(SURFACE), "fog off by default, quad keeps its color");

	SetFog(*environment, FogMode::Linear, FOG);
	environment->SetFogStart(1.0f);
	environment->SetFogEnd(5.0f);
	ASSERT_TRUE(RenderCenter(stage, QuadView(), texel));
	ExpectTexelNear(texel, ExpectedTexel(Fogged(SURFACE, FOG, LinearVisibility(1.0f, 5.0f, CENTER_DISTANCE))), "Linear");

	environment->SetFogMode(FogMode::Exponential);
	environment->SetFogDensity(0.25f);
	ASSERT_TRUE(RenderCenter(stage, QuadView(), texel));
	ExpectTexelNear(texel, ExpectedTexel(Fogged(SURFACE, FOG, ExponentialVisibility(0.25f, CENTER_DISTANCE))), "Exponential");

	environment->SetFogMode(FogMode::ExponentialSquared);
	ASSERT_TRUE(RenderCenter(stage, QuadView(), texel));
	ExpectTexelNear(texel, ExpectedTexel(Fogged(SURFACE, FOG, ExponentialSquaredVisibility(0.25f, CENTER_DISTANCE))),
					"Exponential Squared");

	TearDownStage(stage);
}

TEST_F(FogReadbackFixture, RangelessLinearFogHasHardEdge) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* material = LoadUnlit("Surface.mat", SURFACE);
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	Handle<Environment> environment = AddEnvironment(stage);
	SetFog(*environment, FogMode::Linear, FOG);

	environment->SetFogStart(3.5f);
	environment->SetFogEnd(3.5f);
	Vec3 texel;
	ASSERT_TRUE(RenderCenter(stage, QuadView(), texel));
	ExpectTexelNear(texel, ExpectedTexel(SURFACE), "a surface nearer than the edge is clear");

	environment->SetFogStart(4.0f);
	environment->SetFogEnd(2.5f);
	ASSERT_TRUE(RenderCenter(stage, QuadView(), texel));
	ExpectTexelNear(texel, ExpectedTexel(FOG), "a surface past an End before its Start is the fog's color");

	TearDownStage(stage);
}

TEST_F(FogReadbackFixture, FogMeasuresAlongRay) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* material = LoadUnlit("Surface.mat", SURFACE);
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.quad->SetLocalScale(Vec3(8.0f, 8.0f, 1.0f));
	Handle<Environment> environment = AddEnvironment(stage);
	SetFog(*environment, FogMode::ExponentialSquared, FOG);
	environment->SetFogDensity(0.3f);

	const ViewDesc desc = QuadView();
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	RenderSystem::SubmitView(stage.target, desc);
	RenderSystem::RenderAll();
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(stage.target, pixels, size));

	for (const Vec2I pixel : {Vec2I{size.x / 2, size.y / 2}, Vec2I{3, 4}, Vec2I{size.x - 5, size.y - 3}}) {
		const f32 distance = DistanceToQuadPlane(desc, size, pixel.x, pixel.y);
		ExpectTexelNear(Texel(pixels, size, pixel.x, pixel.y),
						ExpectedTexel(Fogged(SURFACE, FOG, ExponentialSquaredVisibility(0.3f, distance))),
						std::format("pixel ({}, {}) at {} along its ray", pixel.x, pixel.y, distance).c_str());
	}
	EXPECT_GT(DistanceToQuadPlane(desc, size, 3, 4), CENTER_DISTANCE * 1.2f);

	TearDownStage(stage);
}

TEST_F(FogReadbackFixture, OrthographicFogMeasuresFromCamerasPlane) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* material = LoadUnlit("Surface.mat", SURFACE);
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.quad->SetLocalScale(Vec3(8.0f, 8.0f, 1.0f));
	Handle<Environment> environment = AddEnvironment(stage);
	SetFog(*environment, FogMode::ExponentialSquared, FOG);
	environment->SetFogDensity(0.3f);

	ViewDesc desc = QuadView();
	desc.projection = Mat4::OrthographicReverseZ(-3.0f, 3.0f, -3.0f, 3.0f, 0.1f, 100.0f);
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	RenderSystem::SubmitView(stage.target, desc);
	RenderSystem::RenderAll();
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(stage.target, pixels, size));

	const Vec3 expected = ExpectedTexel(Fogged(SURFACE, FOG, ExponentialSquaredVisibility(0.3f, CENTER_DISTANCE)));
	for (const Vec2I pixel : {Vec2I{size.x / 2, size.y / 2}, Vec2I{2, 2}, Vec2I{size.x - 3, size.y - 3}}) {
		ExpectTexelNear(Texel(pixels, size, pixel.x, pixel.y), expected, std::format("pixel ({}, {})", pixel.x, pixel.y).c_str());
	}

	TearDownStage(stage);
}

TEST_F(FogReadbackFixture, MixedModeZonesComposite) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* material = LoadUnlit("Surface.mat", SURFACE);
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	Handle<Environment> lower = AddEnvironment(stage, 0.0f, 1.0f);
	SetFog(*lower, FogMode::Linear, FOG);
	lower->SetFogStart(0.0f);
	lower->SetFogEnd(4.0f);
	Handle<Environment> upper = AddEnvironment(stage, 1.0f, 0.5f);
	SetFog(*upper, FogMode::ExponentialSquared, OTHER_FOG);
	upper->SetFogDensity(0.4f);

	const f32 lowerTaken = 0.5f * (1.0f - LinearVisibility(0.0f, 4.0f, CENTER_DISTANCE));
	const f32 upperTaken = 0.5f * (1.0f - ExponentialSquaredVisibility(0.4f, CENTER_DISTANCE));
	const Vec3 composite = SURFACE * (1.0f - lowerTaken - upperTaken) + FOG * lowerTaken + OTHER_FOG * upperTaken;
	Vec3 texel;
	ASSERT_TRUE(RenderCenter(stage, QuadView(), texel));
	ExpectTexelNear(texel, ExpectedTexel(composite), "both zones' fogs by their shares");

	upper->SetFog(false);
	ASSERT_TRUE(RenderCenter(stage, QuadView(), texel));
	ExpectTexelNear(texel, ExpectedTexel(SURFACE * (1.0f - lowerTaken) + FOG * lowerTaken), "the lower zone's fog at half its share");

	TearDownStage(stage);
}

TEST_F(FogReadbackFixture, LocalZonesFogFadesOverBlendDistance) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* material = LoadUnlit("Surface.mat", SURFACE);
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	Handle<Environment> zone = AddEnvironment(stage);
	zone->SetIsGlobal(false);
	zone->SetSize(Vec3(2.0f, 2.0f, 2.0f));
	zone->SetBlendDistance(4.0f);
	SetFog(*zone, FogMode::Exponential, FOG);
	zone->SetFogDensity(0.3f);
	ASSERT_FLOAT_EQ(Eye.z - 1.0f, 2.0f);

	const f32 share = 1.0f - 0.5f * 0.5f;
	const f32 taken = share * (1.0f - ExponentialVisibility(0.3f, CENTER_DISTANCE));
	Vec3 texel;
	ASSERT_TRUE(RenderCenter(stage, QuadView(), texel));
	ExpectTexelNear(texel, ExpectedTexel(SURFACE * (1.0f - taken) + FOG * taken), "three quarters of the zone's fog");

	zone->SetBlendDistance(1.5f);
	ASSERT_TRUE(RenderCenter(stage, QuadView(), texel));
	ExpectTexelNear(texel, ExpectedTexel(SURFACE), "a camera past the blend distance sees through no fog");

	TearDownStage(stage);
}

TEST_F(FogReadbackFixture, EachBlendIsFoggedAsItComposites) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	const Vec3 behind{0.30f, 0.40f, 0.20f};
	const f32 alpha = 0.6f;
	const f32 visibility = LinearVisibility(1.0f, 5.0f, CENTER_DISTANCE);
	const f32 taken = 1.0f - visibility;
	struct BlendCase {
		const char* blend;
		Vec3 expected;
	};
	const BlendCase cases[] = {
		{"alpha", Fogged(SURFACE, FOG, visibility) * alpha + behind * (1.0f - alpha)},
		{"additive", behind + SURFACE * (visibility * alpha)},
		{"premultiplied", SURFACE * visibility + FOG * (taken * alpha) + behind * (1.0f - alpha)},
		{"multiply", Vec3(behind.x * (SURFACE.x * visibility + taken), behind.y * (SURFACE.y * visibility + taken),
						  behind.z * (SURFACE.z * visibility + taken))},
	};
	for (const BlendCase& blendCase : cases) {
		SCOPED_TRACE(blendCase.blend);
		MaterialAsset* material = LoadUnlit(std::format("Surface_{}.mat", blendCase.blend).c_str(), SURFACE, alpha, blendCase.blend);
		ASSERT_NE(material, nullptr);
		ASSERT_TRUE(material->blend.has_value());
		ShadingStage stage;
		ASSERT_TRUE(BuildStage(material, stage));
		Handle<Environment> environment = AddEnvironment(stage);
		SetFog(*environment, FogMode::Linear, FOG);
		environment->SetFogStart(1.0f);
		environment->SetFogEnd(5.0f);

		ViewDesc desc = QuadView();
		desc.clearColor = Color(behind.x, behind.y, behind.z, 1.0f);
		Vec3 texel;
		ASSERT_TRUE(RenderCenter(stage, desc, texel));
		ExpectTexelNear(texel, ExpectedTexel(blendCase.expected), blendCase.blend);
		if (str(blendCase.blend) == "additive") {
			const Vec3 towardFog = behind + Fogged(SURFACE, FOG, visibility) * alpha;
			EXPECT_GT(Math::Abs(ExpectedTexel(towardFog).z - texel.z), 10.0f);
		}
		TearDownStage(stage);
	}
}

TEST_F(FogReadbackFixture, SkyUnfoggedSurfaceFogged) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* material = LoadUnlit("Surface.mat", SURFACE);
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	const Vec3 sky{0.05f, 0.60f, 0.25f};
	Handle<Environment> environment = AddEnvironment(stage);
	environment->SetSkyMode(SkyMode::Color);
	environment->SetSkyColor(Color(sky.x, sky.y, sky.z, 1.0f));
	SetFog(*environment, FogMode::ExponentialSquared, FOG);
	environment->SetFogDensity(0.25f);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	RenderSystem::SubmitView(stage.target, QuadView());
	RenderSystem::RenderAll();
	ASSERT_TRUE(RenderSystem::ReadbackViewTarget(stage.target, pixels, size));
	ExpectTexelNear(Texel(pixels, size, 2, 2), ExpectedTexel(sky), "the sky took the fog");
	ExpectTexelNear(CenterTexel(pixels, size), ExpectedTexel(Fogged(SURFACE, FOG, ExponentialSquaredVisibility(0.25f, CENTER_DISTANCE))),
					"the quad in front of the sky");

	TearDownStage(stage);
}

TEST_F(FogReadbackFixture, LitSurfaceIsFoggedOverShading) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* material =
		LoadMaterial("Lit.mat", ShaderGuid("engine://Shaders/Lit.hlsl"),
					 "\t\"floats\": { \"metallic\": 0, \"roughness\": 0.6 },\n\t\"colors\": { \"baseColor\": [0.7, 0.5, 0.3, 1] }");
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	Handle<Environment> environment = AddEnvironment(stage);

	Vec3 unfogged;
	ASSERT_TRUE(RenderCenter(stage, QuadView(), unfogged));
	ASSERT_GT(unfogged.x, 40.0f);
	ASSERT_LT(Math::Max(unfogged.x, Math::Max(unfogged.y, unfogged.z)), 250.0f);
	const Vec3 lit(SRGBToLinearChannel(unfogged.x), SRGBToLinearChannel(unfogged.y), SRGBToLinearChannel(unfogged.z));

	SetFog(*environment, FogMode::Linear, FOG);
	environment->SetFogStart(1.0f);
	environment->SetFogEnd(5.0f);
	Vec3 texel;
	ASSERT_TRUE(RenderCenter(stage, QuadView(), texel));
	ExpectTexelNear(texel, ExpectedTexel(Fogged(lit, FOG, LinearVisibility(1.0f, 5.0f, CENTER_DISTANCE))), "half fogged");

	environment->SetFogStart(0.5f);
	environment->SetFogEnd(1.0f);
	ASSERT_TRUE(RenderCenter(stage, QuadView(), texel));
	ExpectTexelNear(texel, ExpectedTexel(FOG), "wholly fogged");

	TearDownStage(stage);
}
