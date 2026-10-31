#include "TestsPCH.h"

#include "Graphics/GraphicsSettings.h"

#include "ShadingHarness.h"

#include "Shalltear/Core/Graphics/GraphicsProfile.h"
#include "Shalltear/Core/Settings.h"

#include <functional>

using namespace Shalltear;
using ShadingTest::MaterialReadbackFixture;
using ShadingTest::ShadingStage;

namespace {

	constexpr const char* UV_RANGE_PROBE =
		"#pragma shalltear_template unlit\n"
		"void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
		"\tbool outside = any(input.uv < -1e-4) || any(input.uv > 1.0 + 1e-4);\n"
		"\tsurface.baseColor = outside ? float3(1, 0, 0) : float3(0, 1, 0);\n"
		"\tsurface.alpha = 1.0;\n"
		"}\n";

	constexpr i32 SAMPLE_COUNTS[] = {1, 4};
	constexpr const char* PROFILE_NAMES[] = {"NoMsaa", "Msaa4x"};

	struct UVVerdict {
		i32 outside = 0;
		i32 covered = 0;
	};

	class VaryingInterpolationFixture : public MaterialReadbackFixture {
	protected:
		str ProjectSettingsDocument() const override { return "[Graphics]\n"; }

		bool BuildProfiles() {
			str packed;
			for (u32 index = 0; index < static_cast<u32>(std::size(SAMPLE_COUNTS)); ++index) {
				const str fileName = str("project://") + PROFILE_NAMES[index] + ".slprofile";
				const Guid id = AssetDatabase::CreateAsset(Path(fileName), StringHash("GraphicsProfile"));
				if (!id.IsValid()) {
					return false;
				}
				auto* profile = static_cast<GraphicsProfile*>(AssetDatabase::LoadAssetByGuid(id));
				if (!profile) {
					return false;
				}
				profile->msaa = SAMPLE_COUNTS[index];
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

		void PoseFacing(const ShadingStage& stage) {
			stage.quad->SetLocalRotation(Quat::AngleAxis(20.0f, Vec3(0.0f, 0.0f, 1.0f)));
			SceneSystem::EndFrame();
		}

		void PoseGrazing(const ShadingStage& stage) {
			stage.quad->SetLocalScale(Vec3(8.0f, 1.0f, 1.0f));
			stage.quad->SetLocalRotation(Quat::AngleAxis(80.0f, Vec3(0.0f, 1.0f, 0.0f)));
			SceneSystem::EndFrame();
		}

		using Pose = std::function<void(const ShadingStage&)>;

		void ExpectUVInRange(const Pose& pose) {
			if (!RenderSystem::IsRenderingAvailable()) {
				GTEST_SKIP() << "no Vulkan device available";
			}
			if (!CompilerAvailable()) {
				GTEST_SKIP() << "dxcompiler.dll unavailable";
			}
			WriteProjectFile("UVRangeProbe.hlsl", UV_RANGE_PROBE);
			Asset* shaderAsset = AssetDatabase::LoadAsset(Path("project://UVRangeProbe.hlsl"));
			ASSERT_NE(shaderAsset, nullptr);
			MaterialAsset* material = LoadMaterial("UVRange.mat", shaderAsset->assetId, str());
			ASSERT_NE(material, nullptr);

			ASSERT_TRUE(BuildProfiles());
			ShadingStage stage;
			ASSERT_TRUE(BuildStage(material, stage));
			pose(stage);

			const UVVerdict single = MeasureAtLevel(stage, 0);
			ASSERT_EQ(RenderSystem::GetSampleCount(), 1u);
			EXPECT_GT(single.covered, 200);
			EXPECT_EQ(single.outside, 0);

			const UVVerdict multi = MeasureAtLevel(stage, 1);
			if (RenderSystem::GetSampleCount() == 1u) {
				TearDownStage(stage);
				GTEST_SKIP() << "adapter offers no 4x MSAA on the scene chain";
			}
			EXPECT_GT(multi.covered, 200);
			EXPECT_EQ(multi.outside, 0);

			TearDownStage(stage);
		}

		UVVerdict MeasureAtLevel(const ShadingStage& stage, i32 level) {
			Graphics::SetQualityLevel(level);
			ViewDesc desc = ShadingTest::QuadView();
			RenderSystem::SubmitView(stage.target, desc);
			RenderSystem::RenderAll();
			Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
			Vec2I size{};
			UVVerdict verdict;
			if (!RenderSystem::ReadbackViewTarget(stage.target, pixels, size)) {
				return verdict;
			}
			for (i32 y = 0; y < size.y; ++y) {
				for (i32 x = 0; x < size.x; ++x) {
					const size_t index = (static_cast<size_t>(y) * size.x + x) * 4;
					if (pixels[index] > 0) {
						++verdict.outside;
					}
					if (pixels[index + 1] > 0) {
						++verdict.covered;
					}
				}
			}
			return verdict;
		}
	};

} // namespace

TEST_F(VaryingInterpolationFixture, FacingUVStaysInside) {
	ExpectUVInRange([this](const ShadingStage& stage) { PoseFacing(stage); });
}

TEST_F(VaryingInterpolationFixture, GrazingUVStaysInside) {
	ExpectUVInRange([this](const ShadingStage& stage) { PoseGrazing(stage); });
}
