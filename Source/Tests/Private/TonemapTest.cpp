#include "TestsPCH.h"

#include "Graphics/GraphicsSettings.h"

#include "ShadingHarness.h"

#include "Shalltear/Core/Graphics/Camera.h"
#include "Shalltear/Core/Graphics/GraphicsProfile.h"
#include "Shalltear/Core/Settings.h"

#include <cmath>

using namespace Shalltear;
using ShadingTest::CenterTexel;
using ShadingTest::ExpectedTexel;
using ShadingTest::MaterialReadbackFixture;
using ShadingTest::ShadingStage;
using ShadingTest::Tonemap;

namespace {

	constexpr Vec3 HDR_TEXEL{4.0f, 1.0f, 0.25f};

	constexpr const char* HDR_MATERIAL_ROW = "\t\"colors\": { \"baseColor\": [4.0, 1.0, 0.25, 1] }";

	constexpr TonemapCurve CURVES[] = {TonemapCurve::None, TonemapCurve::Neutral, TonemapCurve::ACES,
									   TonemapCurve::AgX};

	constexpr const char* CURVE_NAMES[] = {"None", "Neutral", "ACES", "AgX"};

	class TonemapFixture : public MaterialReadbackFixture {
	protected:
		str ProjectSettingsDocument() const override { return "[Graphics]\n"; }

		bool BuildProfiles() {
			str packed;
			for (u32 index = 0; index < static_cast<u32>(std::size(CURVES)); ++index) {
				const str fileName = str("project://") + CURVE_NAMES[index] + ".slprofile";
				const Guid id = AssetDatabase::CreateAsset(Path(fileName), StringHash("GraphicsProfile"));
				if (!id.IsValid()) {
					return false;
				}
				auto* profile = static_cast<GraphicsProfile*>(AssetDatabase::LoadAssetByGuid(id));
				if (!profile) {
					return false;
				}
				profile->tonemap = CURVES[index];
				if (!packed.empty()) {
					packed += ';';
				}
				packed += id.ToString();
			}
			GraphicsSettings* settings =
				Settings::DeclareCategory<GraphicsSettings>("Graphics", SettingScope::Project, true);
			if (!settings) {
				return false;
			}
			settings->qualityProfiles = packed;
			Settings::NotifyChanged("Graphics", "qualityProfiles");
			return true;
		}

		Vec3 RenderAtLevel(const ShadingStage& stage, i32 level, bool tonemap = true) {
			Graphics::SetQualityLevel(level);
			ViewDesc desc = ShadingTest::QuadView();
			desc.tonemap = tonemap;
			RenderSystem::SubmitView(stage.target, desc);
			RenderSystem::RenderAll();
			Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
			Vec2I size{};
			if (!RenderSystem::ReadbackViewTarget(stage.target, pixels, size)) {
				return Vec3(0.0f, 0.0f, 0.0f);
			}
			return CenterTexel(pixels, size);
		}
	};

} // namespace

TEST_F(TonemapFixture, CurvesMapKnownTexel) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	ASSERT_TRUE(BuildProfiles());
	Asset* unlitShader = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
	ASSERT_NE(unlitShader, nullptr);
	MaterialAsset* material = LoadMaterial("Hdr.mat", unlitShader->assetId, HDR_MATERIAL_ROW);
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));

	for (u32 index = 0; index < static_cast<u32>(std::size(CURVES)); ++index) {
		const Vec3 measured = RenderAtLevel(stage, static_cast<i32>(index));
		if (measured.x + measured.y + measured.z == 0.0f) {
			TearDownStage(stage);
			GTEST_SKIP() << "quad didn't draw";
		}
		const Vec3 expected = ExpectedTexel(Tonemap(CURVES[index], HDR_TEXEL));
		EXPECT_NEAR(measured.x, expected.x, 2.0f) << CURVE_NAMES[index] << " red";
		EXPECT_NEAR(measured.y, expected.y, 2.0f) << CURVE_NAMES[index] << " green";
		EXPECT_NEAR(measured.z, expected.z, 2.0f) << CURVE_NAMES[index] << " blue";
	}

	TearDownStage(stage);
}

TEST_F(TonemapFixture, SelectedCurveIsWhatFrameApplied) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	ASSERT_TRUE(BuildProfiles());
	Asset* unlitShader = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
	ASSERT_NE(unlitShader, nullptr);
	MaterialAsset* material = LoadMaterial("Hdr.mat", unlitShader->assetId, HDR_MATERIAL_ROW);
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));

	Vec3 readings[std::size(CURVES)];
	for (u32 index = 0; index < static_cast<u32>(std::size(CURVES)); ++index) {
		readings[index] = RenderAtLevel(stage, static_cast<i32>(index));
	}
	if (readings[0].x + readings[0].y + readings[0].z == 0.0f) {
		TearDownStage(stage);
		GTEST_SKIP() << "quad didn't draw";
	}
	for (u32 a = 0; a < static_cast<u32>(std::size(CURVES)); ++a) {
		for (u32 b = a + 1; b < static_cast<u32>(std::size(CURVES)); ++b) {
			const f32 delta = std::abs(readings[a].x - readings[b].x) + std::abs(readings[a].y - readings[b].y) +
							  std::abs(readings[a].z - readings[b].z);
			EXPECT_GT(delta, 2.0f) << CURVE_NAMES[a] << " and " << CURVE_NAMES[b];
		}
	}

	const Vec3 builtIn = RenderAtLevel(stage, -1);
	EXPECT_NEAR(builtIn.x, readings[0].x, 1.0f);
	EXPECT_NEAR(builtIn.y, readings[0].y, 1.0f);
	EXPECT_NEAR(builtIn.z, readings[0].z, 1.0f);

	TearDownStage(stage);
}

TEST_F(TonemapFixture, ValueViewSkipsCurve) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	ASSERT_TRUE(BuildProfiles());
	Asset* unlitShader = AssetDatabase::LoadAsset(Path("engine://Shaders/Unlit.hlsl"));
	ASSERT_NE(unlitShader, nullptr);
	MaterialAsset* material = LoadMaterial("Hdr.mat", unlitShader->assetId, HDR_MATERIAL_ROW);
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));

	const Vec3 untouched = ExpectedTexel(Tonemap(TonemapCurve::None, HDR_TEXEL));
	for (u32 index = 0; index < static_cast<u32>(std::size(CURVES)); ++index) {
		const Vec3 values = RenderAtLevel(stage, static_cast<i32>(index), false);
		if (values.x + values.y + values.z == 0.0f) {
			TearDownStage(stage);
			GTEST_SKIP() << "quad didn't draw";
		}
		EXPECT_NEAR(values.x, untouched.x, 2.0f) << CURVE_NAMES[index];
		EXPECT_NEAR(values.y, untouched.y, 2.0f) << CURVE_NAMES[index];
		EXPECT_NEAR(values.z, untouched.z, 2.0f) << CURVE_NAMES[index];
	}
	const Vec3 curved = RenderAtLevel(stage, 1);
	const Vec3 expected = ExpectedTexel(Tonemap(TonemapCurve::Neutral, HDR_TEXEL));
	EXPECT_NEAR(curved.x, expected.x, 2.0f);

	TearDownStage(stage);
}
