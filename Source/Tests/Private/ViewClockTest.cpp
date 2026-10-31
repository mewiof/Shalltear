#include "TestsPCH.h"

#include "ShadingHarness.h"

#include "Shalltear/Core/Graphics/ShaderAsset.h"
#include "Shalltear/Core/Time.h"

#include <cmath>

using namespace Shalltear;
using ShadingTest::CenterTexel;
using ShadingTest::ExpectedTexel;
using ShadingTest::MaterialReadbackFixture;
using ShadingTest::ShadingStage;

namespace {

	constexpr const char* CLOCK_SHADER = "#pragma shalltear_template unlit\n"
										 "[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 unused; };\n"
										 "void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
										 "\tfloat4 t = ShalltearTimeParameters();\n"
										 "\tsurface.baseColor = float3(t.x, t.y, t.w + 0.5);\n"
										 "\tsurface.alpha = 1.0;\n"
										 "}\n";

	class ViewClockFixture : public MaterialReadbackFixture {
	protected:
		Vec3 Render(const ShadingStage& stage, const f64* time) {
			ViewDesc desc = ShadingTest::QuadView();
			desc.time = time;
			RenderSystem::SubmitView(stage.target, desc);
			RenderSystem::RenderAll();
			Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
			Vec2I size{};
			if (!RenderSystem::ReadbackViewTarget(stage.target, pixels, size)) {
				return Vec3(-1.0f, -1.0f, -1.0f);
			}
			return CenterTexel(pixels, size);
		}

		static f32 Encoded(f64 linear) { return ExpectedTexel(Vec3(static_cast<f32>(linear), 0.0f, 0.0f)).x; }
	};

} // anonymous namespace

TEST_F(ViewClockFixture, ViewsClockIsTimeSurfacesRead) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteProjectFile("Clock.hlsl", CLOCK_SHADER);
	Asset* shader = AssetDatabase::LoadAsset(Path("project://Clock.hlsl"));
	ASSERT_NE(shader, nullptr);
	MaterialAsset* material = LoadMaterial("Clock.mat", shader->assetId, {});
	ASSERT_NE(material, nullptr);
	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));

	for (const f64 seconds : {0.3, 0.6}) {
		const Vec3 texel = Render(stage, &seconds);
		ASSERT_GE(texel.x, 0.0f);
		EXPECT_NEAR(texel.x, Encoded(seconds), 2.0f) << seconds;
		EXPECT_NEAR(texel.y, Encoded(std::sin(seconds)), 2.0f) << seconds;
		EXPECT_NEAR(texel.z, Encoded(0.5), 2.0f);
	}

	const Vec3 frame = Render(stage, nullptr);
	const f64 now = Time::GetTime();
	EXPECT_NEAR(frame.x, Encoded(now > 1.0 ? 1.0 : now), 2.0f);

	TearDownStage(stage);
}
