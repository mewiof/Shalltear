#include "TestsPCH.h"

#include "ShadingHarness.h"

#include "Shalltear/Core/Graphics/CubeFace.h"
#include "Shalltear/Core/Graphics/Environment.h"

#include <cmath>
#include <format>

using namespace Shalltear;
using ShadingTest::CapturedIrradiance;
using ShadingTest::MaterialReadbackFixture;
using ShadingTest::QuadView;
using ShadingTest::RadianceTexel;
using ShadingTest::ShadingStage;

namespace {

	struct SkyRows {
		Vec3 zenith{0.2f, 0.38f, 0.75f};
		Vec3 horizon{0.62f, 0.72f, 0.85f};
		Vec3 ground{0.16f, 0.15f, 0.14f};
		Vec3 sunset{1.0f, 0.45f, 0.15f};
		f32 haze = 0.35f;
		f32 glow = 0.25f;
		f32 glowFocus = 0.8f;
		f32 night = 0.05f;
	};

	struct SkySun {
		Vec3 direction{0.0f, 1.0f, 0.0f};
		Vec3 color{};
		f32 discDeg = 1.0f;
	};

	f32 Smoothstep(f32 edge0, f32 edge1, f32 x) {
		const f32 t = Math::Clamp((x - edge0) / (edge1 - edge0), 0.0f, 1.0f);
		return t * t * (3.0f - 2.0f * t);
	}

	Vec3 Lerp(const Vec3& a, const Vec3& b, f32 t) { return a + (b - a) * t; }

	f32 Peak(const Vec3& color) { return Math::Max(color.x, Math::Max(color.y, color.z)); }

	Vec3 AboveHorizon(const SkyRows& rows, const Vec3& direction, const Vec3& sun, f32 daylight, f32 dusk) {
		const f32 band = std::exp(-Math::Max(direction.y, 0.0f) / Math::Max(rows.haze, 0.001f));
		Vec3 color = Lerp(rows.zenith, rows.horizon, band);
		const f32 lengths = std::sqrt((direction.x * direction.x + direction.z * direction.z) * (sun.x * sun.x + sun.z * sun.z));
		const f32 facing = lengths > 1.0e-6f ? (direction.x * sun.x + direction.z * sun.z) / lengths * 0.5f + 0.5f : 0.5f;
		color = Lerp(color, rows.sunset, Math::Clamp(dusk * band * facing * facing, 0.0f, 1.0f));
		return color * (rows.night + (1.0f - rows.night) * daylight);
	}

	Vec3 Glow(const SkyRows& rows, const SkySun& sun, const Vec3& direction) {
		const f32 peak = Peak(sun.color);
		if (peak <= 0.0f) {
			return {};
		}
		const f32 focus = Math::Clamp(rows.glowFocus, 0.0f, 0.98f);
		const f32 spread = Math::Max(1.0f + focus * focus - 2.0f * focus * Vec3::Dot(direction, sun.direction), 1.0e-4f);
		const f32 lobe = (1.0f - focus * focus) / (spread * std::sqrt(spread)) * 0.0795775f;
		return sun.color / peak * (rows.glow * lobe * Smoothstep(-0.1f, 0.05f, sun.direction.y));
	}

	Vec3 Disc(const SkySun& sun, const Vec3& direction) {
		if (sun.discDeg <= 0.0f || Peak(sun.color) <= 0.0f) {
			return {};
		}
		const f32 cosSize = std::cos(Math::DEG_TO_RAD * sun.discDeg * 0.5f);
		const f32 feather = Math::Max((1.0f - cosSize) * 0.15f, 1.0e-5f);
		return sun.color * Smoothstep(cosSize - feather, cosSize + feather, Vec3::Dot(direction, sun.direction));
	}

	Vec3 GradientSky(const SkyRows& rows, const SkySun& sun, const Vec3& direction, bool capturing) {
		const f32 daylight = Smoothstep(-0.15f, 0.2f, sun.direction.y);
		const f32 dusk = (1.0f - Smoothstep(0.0f, 0.4f, sun.direction.y)) * Smoothstep(-0.2f, 0.0f, sun.direction.y);
		if (direction.y >= 0.0f) {
			const Vec3 color = AboveHorizon(rows, direction, sun.direction, daylight, dusk) + Glow(rows, sun, direction);
			return capturing ? color : color + Disc(sun, direction);
		}
		const f32 across = std::sqrt(direction.x * direction.x + direction.z * direction.z);
		const Vec3 level = across > 1.0e-6f ? Vec3(direction.x / across, 0.0f, direction.z / across) : Vec3(0.0f, 0.0f, 1.0f);
		const Vec3 edge = AboveHorizon(rows, level, sun.direction, daylight, dusk) + Glow(rows, sun, level);
		const Vec3 below = rows.ground * (rows.night + (1.0f - rows.night) * daylight);
		return Lerp(below, edge, Smoothstep(-0.03f, 0.0f, direction.y));
	}

	Vec3 TexelDirection(u32 face, Vec2I texel, Vec2I size) {
		return GraphicsInternal::CubeTexelDirection(face, (static_cast<f32>(texel.x) + 0.5f) / static_cast<f32>(size.x),
													(static_cast<f32>(texel.y) + 0.5f) / static_cast<f32>(size.y));
	}

	Vec2I TexelToward(u32 face, const Vec3& direction, Vec2I size) {
		Vec2I best{};
		f32 bestDot = -2.0f;
		for (i32 y = 0; y < size.y; ++y) {
			for (i32 x = 0; x < size.x; ++x) {
				const f32 dot = Vec3::Dot(TexelDirection(face, Vec2I{x, y}, size), direction);
				if (dot > bestDot) {
					bestDot = dot;
					best = Vec2I{x, y};
				}
			}
		}
		return best;
	}

	Vec3 BesideTheSun(const Vec3& sun, f32 degrees) {
		const Vec3 side = Vec3(sun.z, 0.0f, -sun.x).Normalized();
		const f32 angle = degrees * Math::DEG_TO_RAD;
		return (sun * std::cos(angle) + side * std::sin(angle)).Normalized();
	}

	void ExpectRadianceNear(const Vec3& actual, const Vec3& expected, const std::string& what) {
		for (u32 i = 0; i < 3; ++i) {
			EXPECT_NEAR(actual[i], expected[i], 0.004f + 0.01f * std::fabs(expected[i])) << what << ", channel " << i;
		}
	}

	class DefaultSkyFixture : public MaterialReadbackFixture {
	protected:
		MaterialAsset* LoadDefaultSky() {
			return static_cast<MaterialAsset*>(AssetDatabase::LoadAsset(Path("engine://Materials/DefaultSky.mat")));
		}

		Guid GradientSkyGuid() {
			Asset* shader = AssetDatabase::LoadAsset(Path("engine://Shaders/GradientSky.hlsl"));
			return shader ? shader->assetId : Guid{};
		}

		Vec3 AimSun(ShadingStage& stage, f32 elevation, f32 azimuth) {
			Handle<SceneObject> object = stage.sun->GetOwner();
			object->SetLocalRotation(Quat::FromEuler(Vec3(elevation, azimuth + 180.0f, 0.0f)));
			SceneSystem::EndFrame();
			return -object->TransformDirection(Vec3::Forward()).Normalized();
		}

		bool CaptureFace(const ShadingStage& stage, u32 face, Vector<u8>& pixels, Vec2I& size) {
			ViewDesc desc;
			desc.view = CubeFaceView(face, Vec3::Zero());
			desc.projection = Mat4::PerspectiveReverseZ(90.0f, 1.0f, 0.05f, 100.0f);
			desc.cameraPosition = Vec3::Zero();
			desc.radianceCapture = true;
			RenderSystem::SubmitView(stage.target, desc);
			RenderSystem::RenderAll();
			return RenderSystem::ReadbackViewTargetRadiance(stage.target, pixels, size);
		}

		bool RenderQuadRadiance(const ShadingStage& stage, Vec3& out) {
			ViewDesc desc = QuadView();
			desc.radianceCapture = true;
			RenderSystem::SubmitView(stage.target, desc);
			RenderSystem::RenderAll();
			Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
			Vec2I size{};
			if (!RenderSystem::ReadbackViewTargetRadiance(stage.target, pixels, size)) {
				return false;
			}
			out = RadianceTexel(pixels, size, size.x / 2, size.y / 2);
			return true;
		}

		MaterialAsset* LoadAmbientOnly() {
			WriteProjectFile("AmbientOnly.hlsl",
							 "#define SHALLTEAR_CUSTOM_LIGHTING\n"
							 "#pragma shalltear_template lit\n"
							 "float3 ShalltearLightingFunction(ShalltearSurfaceInput input, ShalltearSurface surface, float3 normalWS, ShalltearLightSample light) {\n"
							 "\treturn float3(0.0, 0.0, 0.0);\n"
							 "}\n"
							 "void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
							 "\tsurface.baseColor = float3(1.0, 1.0, 1.0);\n"
							 "\tsurface.metallic = 0.0;\n"
							 "\tsurface.roughness = 1.0;\n"
							 "\tsurface.reflectance = 0.0;\n"
							 "}\n");
			Asset* shader = AssetDatabase::LoadAsset(Path("project://AmbientOnly.hlsl"));
			return shader ? LoadMaterial("AmbientOnly.mat", shader->assetId, {}) : nullptr;
		}
	};

	Vec3 PredictedAmbient(const SkySun& sun, const Vec3& normal, bool withDisc) {
		const SkyRows rows;
		const GraphicsInternal::IrradianceSH sh =
			CapturedIrradiance([&](const Vec3& ray) { return GradientSky(rows, sun, ray, !withDisc); });
		return GraphicsInternal::EvaluateIrradianceSH(sh, normal);
	}

} // anonymous namespace

TEST_F(DefaultSkyFixture, SkyMatchesModelThroughDay) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* sky = LoadDefaultSky();
	ASSERT_NE(sky, nullptr);
	ASSERT_NE(sky->shader.Get(), nullptr);
	ASSERT_EQ(sky->shader->integrator, ShaderTemplate::Sky);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(sky, stage, environment));
	stage.sun->SetColor(Color(1.0f, 0.9f, 0.7f, 1.0f));

	const SkyRows rows;
	Vec3 zeniths[3];
	u32 elevationIndex = 0;
	for (const f32 elevation : {60.0f, 15.0f, 2.0f}) {
		SkySun sun;
		sun.direction = AimSun(stage, elevation, 0.0f);
		sun.color = Vec3(1.0f, 0.9f, 0.7f);
		ASSERT_NEAR(sun.direction.y, std::sin(elevation * Math::DEG_TO_RAD), 1.0e-4f) << elevation;
		const std::string at = " with the sun " + std::to_string(static_cast<int>(elevation)) + " degrees up";

		Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
		Vec2I size{};
		ASSERT_TRUE(CaptureFace(stage, 2, pixels, size));
		const Vec2I middle{size.x / 2, size.y / 2};
		const Vec3 zenith = RadianceTexel(pixels, size, middle.x, middle.y);
		ExpectRadianceNear(zenith, GradientSky(rows, sun, TexelDirection(2, middle, size), false), "the zenith" + at);
		zeniths[elevationIndex++] = zenith;
		ASSERT_TRUE(CaptureFace(stage, 3, pixels, size));
		ExpectRadianceNear(RadianceTexel(pixels, size, middle.x, middle.y), GradientSky(rows, sun, TexelDirection(3, middle, size), false),
						   "the ground" + at);

		for (const u32 face : {4u, 5u}) {
			ASSERT_TRUE(CaptureFace(stage, face, pixels, size));
			for (const i32 row : {middle.y - 1, middle.y, middle.y + 2}) {
				const Vec2I texel{middle.x + 5, row};
				ExpectRadianceNear(RadianceTexel(pixels, size, texel.x, texel.y), GradientSky(rows, sun, TexelDirection(face, texel, size), false),
								   "the horizon on face " + std::to_string(face) + " at row " + std::to_string(row) + at);
			}
		}
		const Vec3 besideSun = BesideTheSun(sun.direction, 8.0f);
		const u32 glowFace = besideSun.y > 0.7071f ? 2u : 4u;
		ASSERT_TRUE(CaptureFace(stage, glowFace, pixels, size));
		const Vec2I glow = TexelToward(glowFace, besideSun, size);
		const Vec3 glowRay = TexelDirection(glowFace, glow, size);
		ASSERT_GT(Vec3::Dot(glowRay, sun.direction), std::cos(12.0f * Math::DEG_TO_RAD)) << at;
		ASSERT_GT(glowRay.y, 0.0f) << at;
		ExpectRadianceNear(RadianceTexel(pixels, size, glow.x, glow.y), GradientSky(rows, sun, glowRay, false), "the sun's glow" + at);
	}
	EXPECT_GT(zeniths[0].z, zeniths[2].z * 1.5f);

	TearDownStage(stage);
}

TEST_F(DefaultSkyFixture, EveryRowOfMaterialReachesSky) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	const Guid shader = GradientSkyGuid();
	ASSERT_TRUE(shader.IsValid());
	SkyRows rows;
	rows.zenith = Vec3(0.05f, 0.3f, 0.6f);
	rows.horizon = Vec3(0.5f, 0.6f, 0.4f);
	rows.ground = Vec3(0.4f, 0.1f, 0.05f);
	rows.sunset = Vec3(0.9f, 0.2f, 0.6f);
	rows.haze = 0.6f;
	rows.glow = 1.5f;
	rows.glowFocus = 0.6f;
	rows.night = 0.3f;
	const auto document = std::format("\t\"floats\": {{ \"haze\": {}, \"glow\": {}, \"glowFocus\": {}, \"night\": {} }},\n"
									  "\t\"colors\": {{ \"zenith\": [{}, {}, {}, 1], \"horizon\": [{}, {}, {}, 1], \"ground\": [{}, {}, {}, 1], "
									  "\"sunset\": [{}, {}, {}, 1] }}",
									  rows.haze, rows.glow, rows.glowFocus, rows.night, rows.zenith.x, rows.zenith.y, rows.zenith.z, rows.horizon.x,
									  rows.horizon.y, rows.horizon.z, rows.ground.x, rows.ground.y, rows.ground.z, rows.sunset.x, rows.sunset.y,
									  rows.sunset.z);
	MaterialAsset* material = LoadMaterial("Rows.mat", shader, document);
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(material, stage, environment));
	SkySun sun;
	sun.direction = AimSun(stage, 5.0f, 0.0f);
	sun.color = Vec3(1.0f, 1.0f, 1.0f);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(CaptureFace(stage, 4, pixels, size));
	for (const Vec2I texel : {TexelToward(4, BesideTheSun(sun.direction, 3.0f), size), TexelToward(4, BesideTheSun(sun.direction, 12.0f), size),
							  Vec2I{2, size.y / 2 - 2}, Vec2I{size.x / 2, size.y - 3}}) {
		ExpectRadianceNear(RadianceTexel(pixels, size, texel.x, texel.y), GradientSky(rows, sun, TexelDirection(4, texel, size), false),
						   "the sky toward the sun at (" + std::to_string(texel.x) + ", " + std::to_string(texel.y) + ")");
	}
	for (const u32 face : {2u, 3u, 5u}) {
		ASSERT_TRUE(CaptureFace(stage, face, pixels, size));
		const Vec2I texel{size.x / 2, size.y / 2 - 3};
		ExpectRadianceNear(RadianceTexel(pixels, size, texel.x, texel.y), GradientSky(rows, sun, TexelDirection(face, texel, size), false),
						   "the sky on face " + std::to_string(face));
	}
	SkyRows noGlow = rows;
	noGlow.glow = 0.0f;
	const Vec3 besideSun = BesideTheSun(sun.direction, 3.0f);
	EXPECT_GT(GradientSky(rows, sun, besideSun, false).x, GradientSky(noGlow, sun, besideSun, false).x + 0.5f);

	TearDownStage(stage);
}

TEST_F(DefaultSkyFixture, AmbientFollowsTurnedSunWithinCapturesFrames) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* sky = LoadDefaultSky();
	MaterialAsset* surface = LoadAmbientOnly();
	ASSERT_NE(sky, nullptr);
	ASSERT_NE(surface, nullptr);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(sky, stage, environment));
	ASSERT_TRUE(AddQuad(stage, surface, 0.0f));
	ASSERT_EQ(environment->GetSkyUpdateMode(), SkyUpdateMode::OnChanged);
	const Vec3 normal(0.0f, 0.0f, 1.0f);

	SkySun morning;
	morning.direction = AimSun(stage, 2.0f, 0.0f);
	morning.color = Vec3(1.0f, 1.0f, 1.0f);
	Vec3 texel;
	for (u32 frame = 0; frame < RHI::FRAMES_IN_FLIGHT + 2; ++frame) {
		ASSERT_TRUE(RenderQuadRadiance(stage, texel));
	}
	const Vec3 morningAmbient = PredictedAmbient(morning, normal, false);
	ExpectRadianceNear(texel, morningAmbient, "the morning's ambient");

	SkySun noon;
	noon.direction = AimSun(stage, 70.0f, 0.0f);
	noon.color = morning.color;
	const Vec3 noonAmbient = PredictedAmbient(noon, normal, false);
	ASSERT_GT(noonAmbient.z - morningAmbient.z, 0.3f * morningAmbient.z);
	for (u32 frame = 0; frame < RHI::FRAMES_IN_FLIGHT + 1; ++frame) {
		ASSERT_TRUE(RenderQuadRadiance(stage, texel));
	}
	ExpectRadianceNear(texel, noonAmbient, "the noon's ambient, FRAMES_IN_FLIGHT frames after the turn");

	TearDownStage(stage);
}

TEST_F(DefaultSkyFixture, SkyLightExcludesSun) {
	if (!RenderSystem::IsRenderingAvailable() || !CompilerAvailable()) {
		GTEST_SKIP() << "no Vulkan device or no shader compiler";
	}
	MaterialAsset* sky = LoadDefaultSky();
	MaterialAsset* surface = LoadAmbientOnly();
	ASSERT_NE(sky, nullptr);
	ASSERT_NE(surface, nullptr);
	ShadingStage stage;
	Handle<Environment> environment;
	ASSERT_TRUE(BuildSkyStage(sky, stage, environment));
	ASSERT_TRUE(AddQuad(stage, surface, 0.0f));
	environment->SetSunDiscSizeDeg(30.0f);
	SceneSystem::EndFrame();
	const Vec3 normal(0.0f, 0.0f, 1.0f);

	SkySun sun;
	sun.direction = AimSun(stage, 8.0f, 0.0f);
	sun.color = Vec3(1.0f, 1.0f, 1.0f);
	sun.discDeg = 30.0f;
	Vec3 texel;
	for (u32 frame = 0; frame < RHI::FRAMES_IN_FLIGHT + 2; ++frame) {
		ASSERT_TRUE(RenderQuadRadiance(stage, texel));
	}
	const Vec3 without = PredictedAmbient(sun, normal, false);
	const Vec3 with = PredictedAmbient(sun, normal, true);
	ASSERT_GT(with.x - without.x, 0.03f * without.x + 0.012f);
	ExpectRadianceNear(texel, without, "the sky's light without its sun");

	TearDownStage(stage);
}
