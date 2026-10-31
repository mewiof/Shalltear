#include "TestsPCH.h"

#include "Graphics/GraphicsSettings.h"
#include "Graphics/ShadowCascades.h"

#include "ShadingHarness.h"
#include "ZoneProbe.h"

#include "Shalltear/Core/Graphics/GraphicsProfile.h"
#include "Shalltear/Core/Settings.h"

#include <cmath>

using namespace Shalltear;
using ShadingTest::CenterTexel;
using ShadingTest::Eye;
using ShadingTest::MaterialReadbackFixture;
using ShadingTest::ShadingStage;

namespace {

	constexpr f32 QUAD_YAW_DEG = 40.0f;

	constexpr f32 QUAD_Z = 2.0f;

	constexpr f32 CASCADE_SHADOW_DISTANCE = 40.0f;
	constexpr u32 CASCADE_COUNT = 4;
	constexpr f32 CASCADE_NEAR = 0.1f;

	GraphicsInternal::CascadeSplits ExpectedSplits() {
		const GraphicsProfile profile;
		return GraphicsInternal::SolveCascadeSplits(CASCADE_NEAR, CASCADE_SHADOW_DISTANCE, CASCADE_COUNT,
													profile.shadowFirstCascadeDistance);
	}

	class DebugViewFixture : public MaterialReadbackFixture {
	protected:
		str ProjectSettingsDocument() const override { return "[Graphics]\n"; }

		bool BuildTurnedStage(MaterialAsset* material, ShadingStage& out) {
			if (!BuildStage(material, out)) {
				return false;
			}
			SceneObject* quad = out.quad.Get();
			if (!quad) {
				return false;
			}
			quad->SetLocalRotation(Quat::AngleAxis(QUAD_YAW_DEG, Vec3::Up()));
			quad->SetLocalPosition(Vec3(0.0f, 0.0f, QUAD_Z));
			SceneSystem::EndFrame();
			return true;
		}

		MaterialAsset* UnlitMaterial() {
			Asset* unlitShader = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
			if (!unlitShader) {
				return nullptr;
			}
			return LoadMaterial("Flat.mat", unlitShader->assetId,
								"\t\"colors\": { \"baseColor\": [0.8, 0.2, 0.2, 1] }");
		}

		bool Render(const ShadingStage& stage, DebugView mode, Vector<u8>& pixels, Vec2I& size,
					std::span<const DebugLine> overlay = {}) {
			ViewDesc desc = ShadingTest::QuadView();
			desc.debugView = mode;
			desc.overlayLines = overlay;
			RenderSystem::SubmitView(stage.target, desc);
			RenderSystem::RenderAll();
			return RenderSystem::ReadbackViewTarget(stage.target, pixels, size);
		}

		static Vec3 TexelAt(const Vector<u8>& pixels, Vec2I size, i32 x, i32 y) {
			const size_t index = (static_cast<size_t>(y) * size.x + x) * 4;
			return Vec3(static_cast<f32>(pixels[index]), static_cast<f32>(pixels[index + 1]),
						static_cast<f32>(pixels[index + 2]));
		}

		static Vec3 ExpectedDisplayByte(Vec3 stored) { return stored * 255.0f; }

		bool UseShadowProfile() {
			const Guid profileId =
				AssetDatabase::CreateAsset(Path("project://Cascades.slprofile"), StringHash("GraphicsProfile"));
			if (!profileId.IsValid()) {
				return false;
			}
			auto* profile = static_cast<GraphicsProfile*>(AssetDatabase::LoadAssetByGuid(profileId));
			if (!profile) {
				return false;
			}
			profile->shadowDistance = CASCADE_SHADOW_DISTANCE;
			profile->shadowCascades = static_cast<i32>(CASCADE_COUNT);
			GraphicsSettings* settings =
				Settings::DeclareCategory<GraphicsSettings>("Graphics", SettingScope::Project, true);
			if (!settings) {
				return false;
			}
			settings->qualityProfiles = profileId.ToString();
			Settings::NotifyChanged("Graphics", "qualityProfiles");
			return Graphics::SetQualityLevel(0);
		}

		bool BuildCascadeStage(MaterialAsset* material, ShadingStage& out) {
			if (!BuildStage(material, out)) {
				return false;
			}
			Light* sun = out.sun.Get();
			if (!sun) {
				return false;
			}
			sun->SetCastShadows(true);
			SceneSystem::EndFrame();
			return true;
		}

		void PlaceQuadAtViewDepth(const ShadingStage& stage, f32 viewDepth) {
			SceneObject* quad = stage.quad.Get();
			ASSERT_NE(quad, nullptr);
			quad->SetLocalPosition(Vec3(0.0f, 0.0f, Eye.z - viewDepth));
			quad->SetLocalScale(Vec3(viewDepth * 0.5f, viewDepth * 0.5f, 1.0f));
			SceneSystem::EndFrame();
		}

		bool RenderCascades(const ShadingStage& stage, f32 viewDepth, Vec3& outCenter, Vec3& outCorner) {
			PlaceQuadAtViewDepth(stage, viewDepth);
			Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
			Vec2I size{};
			if (!Render(stage, DebugView::Cascades, pixels, size)) {
				return false;
			}
			outCenter = CenterTexel(pixels, size);
			outCorner = TexelAt(pixels, size, 1, 1);
			return true;
		}

		static bool SameTint(Vec3 a, Vec3 b) {
			return Math::Abs(a.x - b.x) < 16.0f && Math::Abs(a.y - b.y) < 16.0f && Math::Abs(a.z - b.z) < 16.0f;
		}
	};

} // namespace

TEST_F(DebugViewFixture, NormalsViewShowsSurfaceNormalNotClear) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	MaterialAsset* material = UnlitMaterial();
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildTurnedStage(material, stage));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, DebugView::Normals, pixels, size));

	const f32 yaw = QUAD_YAW_DEG * Math::DEG_TO_RAD;
	const Vec3 normal(std::sin(yaw), 0.0f, std::cos(yaw));
	const Vec3 expected = ExpectedDisplayByte(normal * 0.5f + Vec3(0.5f, 0.5f, 0.5f));
	const Vec3 measured = CenterTexel(pixels, size);
	EXPECT_NEAR(measured.x, expected.x, 2.0f);
	EXPECT_NEAR(measured.y, expected.y, 2.0f);
	EXPECT_NEAR(measured.z, expected.z, 2.0f);

	const Vec3 corner = TexelAt(pixels, size, 1, 1);
	const Vec3 clear = ExpectedDisplayByte(Vec3(0.5f, 0.5f, 1.0f));
	EXPECT_NEAR(corner.x, clear.x, 2.0f);
	EXPECT_NEAR(corner.z, clear.z, 2.0f);
	EXPECT_GT(std::abs(measured.x - corner.x), 8.0f);

	TearDownStage(stage);
}

TEST_F(DebugViewFixture, DepthViewShowsReverseZ) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	MaterialAsset* material = UnlitMaterial();
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildTurnedStage(material, stage));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, DebugView::Depth, pixels, size));

	const Mat4 projection = ShadingTest::QuadView().projection;
	const f32 viewZ = Eye.z - QUAD_Z;
	const f32 ndcZ = projection(2, 3) / viewZ + projection(2, 2);
	const Vec3 measured = CenterTexel(pixels, size);
	EXPECT_NEAR(measured.x, ndcZ * 255.0f, 2.0f);
	EXPECT_NEAR(measured.y, measured.x, 1.0f);
	EXPECT_NEAR(measured.z, measured.x, 1.0f);

	const Vec3 corner = TexelAt(pixels, size, 1, 1);
	EXPECT_NEAR(corner.x, 0.0f, 1.0f);
	EXPECT_GT(measured.x, 8.0f);

	TearDownStage(stage);
}

TEST_F(DebugViewFixture, CascadeViewPartitionsDepthWhereSplitsDo) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	ASSERT_TRUE(UseShadowProfile());
	MaterialAsset* material = UnlitMaterial();
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildCascadeStage(material, stage));

	const GraphicsInternal::CascadeSplits splits = ExpectedSplits();
	ASSERT_EQ(splits.count, CASCADE_COUNT);

	Vector<f32> depths{GetMemoryResource(MemTag::Graphics)};
	for (u32 i = 0; i < splits.count; ++i) {
		depths.push_back((splits.depths[i] + splits.depths[i + 1]) * 0.5f);
	}
	for (u32 boundary = 1; boundary < splits.count; ++boundary) {
		depths.push_back(splits.depths[boundary] * 0.97f);
		depths.push_back(splits.depths[boundary] * 1.03f);
	}

	Vector<Vec3> tints{GetMemoryResource(MemTag::Graphics)};
	Vec3 background{};
	for (f32 depth : depths) {
		Vec3 center{};
		Vec3 corner{};
		ASSERT_TRUE(RenderCascades(stage, depth, center, corner)) << depth;
		tints.push_back(center);
		background = corner;
	}

	EXPECT_FALSE(SameTint(tints[0], background));

	for (size_t i = 0; i < depths.size(); ++i) {
		for (size_t j = i + 1; j < depths.size(); ++j) {
			const u32 cascadeI = GraphicsInternal::SelectCascade(splits, depths[i]);
			const u32 cascadeJ = GraphicsInternal::SelectCascade(splits, depths[j]);
			const bool expectedSame = cascadeI == cascadeJ;
			EXPECT_EQ(SameTint(tints[i], tints[j]), expectedSame) << "view depths " << depths[i] << " and " << depths[j] << ' ' << cascadeI << " and " << cascadeJ << ' ' << (expectedSame ? "split them" : "merged them");
		}
	}

	TearDownStage(stage);
}

TEST_F(DebugViewFixture, PastShadowDistanceNotClamped) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	ASSERT_TRUE(UseShadowProfile());
	MaterialAsset* material = UnlitMaterial();
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildCascadeStage(material, stage));

	const GraphicsInternal::CascadeSplits splits = ExpectedSplits();
	ASSERT_EQ(splits.count, CASCADE_COUNT);

	Vec3 lastCascade{};
	Vec3 unusedCorner{};
	const f32 lastMidpoint = (splits.depths[splits.count - 1] + splits.depths[splits.count]) * 0.5f;
	ASSERT_TRUE(RenderCascades(stage, lastMidpoint, lastCascade, unusedCorner));

	Vec3 beyond{};
	Vec3 background{};
	ASSERT_TRUE(RenderCascades(stage, CASCADE_SHADOW_DISTANCE * 1.5f, beyond, background));

	EXPECT_FALSE(SameTint(beyond, lastCascade));
	EXPECT_TRUE(SameTint(beyond, background));

	TearDownStage(stage);
}

TEST_F(DebugViewFixture, ADebugViewIsNotTonemapped) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	const Guid profileId =
		AssetDatabase::CreateAsset(Path("project://Aces.slprofile"), StringHash("GraphicsProfile"));
	ASSERT_TRUE(profileId.IsValid());
	auto* profile = static_cast<GraphicsProfile*>(AssetDatabase::LoadAssetByGuid(profileId));
	ASSERT_NE(profile, nullptr);
	profile->tonemap = TonemapCurve::ACES;
	GraphicsSettings* settings =
		Settings::DeclareCategory<GraphicsSettings>("Graphics", SettingScope::Project, true);
	ASSERT_NE(settings, nullptr);
	settings->qualityProfiles = profileId.ToString();
	Settings::NotifyChanged("Graphics", "qualityProfiles");
	ASSERT_TRUE(Graphics::SetQualityLevel(0));

	MaterialAsset* material = UnlitMaterial();
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildTurnedStage(material, stage));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, DebugView::Normals, pixels, size));

	const f32 yaw = QUAD_YAW_DEG * Math::DEG_TO_RAD;
	const Vec3 normal(std::sin(yaw), 0.0f, std::cos(yaw));
	const Vec3 expected = ExpectedDisplayByte(normal * 0.5f + Vec3(0.5f, 0.5f, 0.5f));
	const Vec3 measured = CenterTexel(pixels, size);
	EXPECT_NEAR(measured.x, expected.x, 2.0f);
	EXPECT_NEAR(measured.y, expected.y, 2.0f);
	EXPECT_NEAR(measured.z, expected.z, 2.0f);

	TearDownStage(stage);
}

TEST_F(DebugViewFixture, AnOverlayLineSurvivesADebugView) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	MaterialAsset* material = UnlitMaterial();
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildTurnedStage(material, stage));

	Vector<u8> bare{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(Render(stage, DebugView::Normals, bare, size));

	const DebugLine line{Vec3(-2.0f, 0.0f, QUAD_Z - 0.5f), Vec3(2.0f, 0.0f, QUAD_Z - 0.5f), Color32(255, 0, 255, 255)};
	Vector<u8> withLine{GetMemoryResource(MemTag::Graphics)};
	Vec2I lineSize{};
	ASSERT_TRUE(Render(stage, DebugView::Normals, withLine, lineSize, {&line, 1}));
	ASSERT_EQ(size.x, lineSize.x);
	ASSERT_EQ(size.y, lineSize.y);

	u32 moved = 0;
	for (i32 y = 0; y < size.y; ++y) {
		for (i32 x = 0; x < size.x; ++x) {
			if (TexelAt(bare, size, x, y).x != TexelAt(withLine, size, x, y).x) {
				++moved;
			}
		}
	}
	EXPECT_GT(moved, 0u);

	TearDownStage(stage);
}

TEST_F(DebugViewFixture, TheModeIsWhatDeclaresThePrepass) {
#ifdef SHALLTEAR_CONFIG_RELEASE
	GTEST_SKIP() << "profiler zones compiled out in Release";
#else
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	MaterialAsset* material = UnlitMaterial();
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildTurnedStage(material, stage));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	{
		ZoneProbe::ZoneScope zones;
		ASSERT_TRUE(Render(stage, DebugView::None, pixels, size));
		Profiler::NextFrame();
		EXPECT_GT(ZoneProbe::ZoneCalls("Render.View.Scene"), 0u);
		EXPECT_EQ(ZoneProbe::ZoneCalls("Render.View.Prepass"), 0u);
		EXPECT_EQ(ZoneProbe::ZoneCalls("Render.View.DebugView"), 0u);
	}
	{
		ZoneProbe::ZoneScope zones;
		ASSERT_TRUE(Render(stage, DebugView::Normals, pixels, size));
		Profiler::NextFrame();
		EXPECT_GT(ZoneProbe::ZoneCalls("Render.View.Scene"), 0u);
		EXPECT_GT(ZoneProbe::ZoneCalls("Render.View.Prepass"), 0u);
		EXPECT_GT(ZoneProbe::ZoneCalls("Render.View.DebugView"), 0u);
	}
	{
		ZoneProbe::ZoneScope zones;
		ASSERT_TRUE(Render(stage, DebugView::Cascades, pixels, size));
		Profiler::NextFrame();
		EXPECT_GT(ZoneProbe::ZoneCalls("Render.View.Scene"), 0u);
		EXPECT_GT(ZoneProbe::ZoneCalls("Render.View.Prepass"), 0u);
		EXPECT_GT(ZoneProbe::ZoneCalls("Render.View.DebugView"), 0u);
	}

	TearDownStage(stage);
#endif
}
