#include "ShadingHarness.h"
#include "TestImages.h"

#include "Shalltear/Core/Graphics/ColorSpace.h"

using namespace Shalltear;
using namespace ShadingTest;

namespace {

	constexpr const char* TINTED_SHADER =
		"#pragma shalltear_template unlit\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 tint; };\n"
		"[[vk::binding(1, 1)]] Texture2D map;\n"
		"[[vk::binding(2, 1)]] SamplerState mapSampler;\n"
		"void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
		"\tsurface.baseColor = map.SampleLevel(mapSampler, float2(0.5, 0.5), 0).rgb * tint.rgb;\n"
		"}\n";

	class ColorSpaceFixture : public MaterialReadbackFixture {
	protected:
		bool Tinted(f32 tint, f32& outTexel) {
			const u8 gray[4] = {128, 128, 128, 255};
			TestImages::WriteTGA(_root / "Gray.tga", gray);
			WriteProjectFile("Tinted.hlsl", TINTED_SHADER);
			Asset* texture = AssetDatabase::LoadAsset(Path("project://Gray.tga"));
			Asset* shader = AssetDatabase::LoadAsset(Path("project://Tinted.hlsl"));
			if (!texture || !shader) {
				return false;
			}
			const str name = std::format("Tinted{}.mat", static_cast<i32>(tint * 100.0f));
			MaterialAsset* material = LoadMaterial(name.c_str(), shader->assetId,
												   std::format("\t\"colors\": {{ \"tint\": [{}, {}, {}, 1] }},\n"
															   "\t\"textures\": {{ \"map\": \"{}\" }}",
															   tint, tint, tint, texture->assetId.ToString()));
			ShadingStage stage;
			if (!BuildStage(material, stage)) {
				return false;
			}
			Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
			Vec2I size{};
			const bool rendered = RenderStage(stage, pixels, size);
			if (rendered) {
				outTexel = CenterTexel(pixels, size).x;
			}
			TearDownStage(stage);
			return rendered;
		}
	};

	class GammaColorSpaceFixture : public ColorSpaceFixture {
	protected:
		str ProjectSettingsDocument() const override { return "[Graphics]\ncolorSpace = Gamma\n"; }
	};

	f32 sRGBTexel(f32 value) {
		return value <= 0.04045f ? value / 12.92f : std::pow((value + 0.055f) / 1.055f, 2.4f);
	}

} // anonymous namespace

TEST_F(ColorSpaceFixture, LinearDecodesTexturesAndEncodesAtPresent) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	EXPECT_EQ(RenderSystem::GetColorSpace(), ColorSpace::Linear);
	f32 full = 0.0f;
	f32 half = 0.0f;
	ASSERT_TRUE(Tinted(1.0f, full));
	ASSERT_TRUE(Tinted(0.5f, half));
	EXPECT_NEAR(full, 128.0f, 1.5f);
	EXPECT_NEAR(half, ExpectedTexel(Vec3(sRGBTexel(128.0f / 255.0f) * 0.5f, 0.0f, 0.0f)).x, 1.5f);
}

TEST_F(GammaColorSpaceFixture, GammaSamplesTexturesAsStoredWithoutEncode) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	EXPECT_EQ(RenderSystem::GetColorSpace(), ColorSpace::Gamma);
	f32 full = 0.0f;
	f32 half = 0.0f;
	ASSERT_TRUE(Tinted(1.0f, full));
	ASSERT_TRUE(Tinted(0.5f, half));
	EXPECT_NEAR(full, 128.0f, 1.5f);
	EXPECT_NEAR(half, 64.0f, 1.5f);
}
