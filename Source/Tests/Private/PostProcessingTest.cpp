#include "TestsPCH.h"

#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Frame/FrameStages.h"
#include "Shalltear/Core/Math/Geometry.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Module.h"
#include "Shalltear/Core/Module/ModuleAPI.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include "Shalltear/Editor/PackageOps.h"

#include <algorithm>
#include <atomic>
#include <chrono>
#include <cmath>
#include <filesystem>

#ifndef SHALLTEAR_SHARED
#include "Shalltear/PostProcessing/PostProcessPass.h"
#include "Shalltear/PostProcessing/PostProcessProfile.h"
#include "Shalltear/PostProcessing/PostProcessVolume.h"

#include "PostProcessBlend.h"
#include "PostProcessGrade.h"
#include "PostProcessShader.h"
#endif

using namespace Shalltear;

#ifndef SHALLTEAR_SHARED
SLT_DECLARE_LINKED_PACKAGE(PostProcessing)
#endif

namespace {

	constexpr const char* PACKAGE_ID = "dev.shalltear.postprocessing";
	constexpr const char* PACKAGE_TARGET = "PostProcessing";
	constexpr const char* PACKAGE_DISPLAY_NAME = "Post Processing";

	constexpr const char* POST_PROCESS_SHADER_TEXT = "2b91d7c4e6a30f58-8d3f5a217c94be60";
	constexpr const char* POST_PROCESS_SHADER_PATH = "Shaders/PostProcess.hlsl";

	const PackageOps::PackageRow* FindRow(std::span<const PackageOps::PackageRow> rows, std::string_view name) {
		for (const PackageOps::PackageRow& row : rows) {
			if (row.name == name) {
				return &row;
			}
		}
		return nullptr;
	}

	bool Contains(std::span<const str> names, std::string_view name) {
		return std::find(names.begin(), names.end(), str(name)) != names.end();
	}

	std::filesystem::path FindRepoDir(const char* leafName) {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		if (ec) {
			return {};
		}
		for (int depth = 0; depth < 12; ++depth) {
			std::filesystem::path candidate = probe / leafName;
			if (std::filesystem::exists(candidate, ec)) {
				return candidate;
			}
			std::filesystem::path parent = probe.parent_path();
			if (parent == probe) {
				break;
			}
			probe = parent;
		}
		return {};
	}

} // namespace

TEST(PostProcessingManifestTest, CatalogueCarries3rdEngineManifest) {
	Vector<PackageOps::PackageManifest> catalogue;
	Vector<str> diagnostics;
	PackageOps::LoadBuiltInCatalogue(catalogue, diagnostics);

	const PackageOps::PackageManifest* post = PackageOps::FindPackage(catalogue, PACKAGE_ID);
	ASSERT_NE(post, nullptr);

	EXPECT_EQ(post->kind, PackageOps::PackageKind::Engine);
	EXPECT_EQ(post->displayName, PACKAGE_DISPLAY_NAME);
	EXPECT_EQ(post->version, "1.0.0");

	EXPECT_EQ(PackageOps::GetPackageBinaryName(*post), PACKAGE_TARGET);

	EXPECT_TRUE(post->dependencies.empty());
}

TEST(PostProcessingManifestTest, RegistersNoFrameStage) {
	Vector<PackageOps::PackageManifest> catalogue;
	Vector<str> diagnostics;
	PackageOps::LoadBuiltInCatalogue(catalogue, diagnostics);

	const PackageOps::PackageManifest* post = PackageOps::FindPackage(catalogue, PACKAGE_ID);
	ASSERT_NE(post, nullptr);
	EXPECT_FALSE(post->stage.has_value());
}

TEST(PostProcessingManifestTest, BuiltInAndOffByDefault) {
	Vector<PackageOps::PackageManifest> catalogue;
	Vector<str> diagnostics;
	PackageOps::LoadBuiltInCatalogue(catalogue, diagnostics);
	ASSERT_NE(PackageOps::FindPackage(catalogue, PACKAGE_ID), nullptr);

	Vector<PackageOps::ResolvedPackage> resolution;
	PackageOps::ResolveEnabledSet(catalogue, {}, resolution);
	EXPECT_FALSE(PackageOps::IsEnabled(resolution, PACKAGE_ID));

	Vector<PackageOps::PackageRow> rows;
	PackageOps::BuildPackageRows(catalogue, resolution, rows);
	const PackageOps::PackageRow* row = FindRow(rows, PACKAGE_ID);
	ASSERT_NE(row, nullptr);
	EXPECT_EQ(row->kind, PackageOps::PackageKind::Engine);
	EXPECT_EQ(row->displayName, PACKAGE_DISPLAY_NAME);
	EXPECT_FALSE(row->enabled);
	EXPECT_TRUE(row->canToggle);
	EXPECT_TRUE(row->blockedBy.empty());

	Vector<str> stored;
	stored.emplace_back(PACKAGE_ID);
	Vector<PackageOps::ResolvedPackage> enabledResolution;
	PackageOps::ResolveEnabledSet(catalogue, stored, enabledResolution);
	EXPECT_TRUE(PackageOps::IsEnabled(enabledResolution, PACKAGE_ID));

	Vector<str> playerPackages;
	PackageOps::SelectGamePackages(catalogue, enabledResolution, playerPackages);
	EXPECT_TRUE(Contains(playerPackages, PACKAGE_ID));

	Vector<str> defaultPlayerPackages;
	PackageOps::SelectGamePackages(catalogue, resolution, defaultPlayerPackages);
	EXPECT_FALSE(Contains(defaultPlayerPackages, PACKAGE_ID));
}

TEST(PostProcessingShaderTest, CommittedSidecarCarriesGuidPackageResolvesBy) {
	std::filesystem::path engineAssets = FindRepoDir("EngineAssets");
	if (engineAssets.empty()) {
		GTEST_SKIP() << "EngineAssets not found";
	}

	std::filesystem::path source = engineAssets / POST_PROCESS_SHADER_PATH;
	ASSERT_TRUE(std::filesystem::exists(source)) << "EngineAssets/" << POST_PROCESS_SHADER_PATH;

	str text = FileSystem::ReadTextFile(Path((source.generic_string() + ".meta")), MemTag::Asset);
	ASSERT_FALSE(text.empty());
	LinearAllocator arena(text.size() + 64, MemTag::Asset);
	JSONValue root = JSONParse(text, arena);
	const Guid onDisk = Guid::FromString(root["assetId"].AsString());

	const Guid expected = Guid::FromString(POST_PROCESS_SHADER_TEXT);
	ASSERT_TRUE(expected.IsValid());
	EXPECT_EQ(onDisk, expected);

#ifndef SHALLTEAR_SHARED
	EXPECT_EQ(PostProcessingImpl::PostProcessShader, onDisk);
#endif
}

#ifndef SHALLTEAR_SHARED

namespace {

	void MakeGradingProfile(PostProcessProfile& profile, f32 saturation, f32 contrast) {
		profile.colorGrading = true;
		profile.saturation = saturation;
		profile.contrast = contrast;
	}

	void MakeVignetteProfile(PostProcessProfile& profile, f32 intensity, bool rounded = false) {
		profile.vignette = true;
		profile.vignetteIntensity = intensity;
		profile.vignetteRounded = rounded;
	}

} // namespace

TEST(PostProcessBlendTest, TwoVolumesBlendToThePredictedWeight) {
	PostProcessProfile first;
	PostProcessProfile second;
	MakeGradingProfile(first, 40.0f, 20.0f);
	MakeGradingProfile(second, -80.0f, 0.0f);

	PostProcessOps::VolumeSample samples[2];
	samples[0] = {0.0f, 1.0f, &first};
	samples[1] = {1.0f, 0.25f, &second};

	const PostProcessOps::ResolvedPostProcess resolved = PostProcessOps::Blend(samples);

	ASSERT_TRUE(resolved.colorGrading);
	EXPECT_FALSE(resolved.vignette);

	const f32 afterFirst = 0.0f + (40.0f - 0.0f) * 1.0f;
	const f32 afterSecond = afterFirst + (-80.0f - afterFirst) * 0.25f;
	EXPECT_NEAR(resolved.saturation, afterSecond, 1e-4f);
	EXPECT_NEAR(resolved.saturation, 10.0f, 1e-4f);

	EXPECT_NEAR(resolved.contrast, 20.0f + (0.0f - 20.0f) * 0.25f, 1e-4f);
	EXPECT_NEAR(resolved.contrast, 15.0f, 1e-4f);
}

TEST(PostProcessBlendTest, PostExposureBlendsInStops) {
	PostProcessProfile first;
	PostProcessProfile second;
	first.colorGrading = true;
	first.postExposure = 2.0f;
	second.colorGrading = true;
	second.postExposure = -1.0f;
	PostProcessOps::VolumeSample samples[2];
	samples[0] = {0.0f, 1.0f, &first};
	samples[1] = {1.0f, 0.25f, &second};
	EXPECT_NEAR(PostProcessOps::Blend(samples).postExposure, 2.0f + (-1.0f - 2.0f) * 0.25f, 1e-4f);
}

TEST(PostProcessBlendTest, PriorityLayersVolumes) {
	PostProcessProfile low;
	PostProcessProfile high;
	MakeGradingProfile(low, 100.0f, 0.0f);
	MakeGradingProfile(high, -100.0f, 0.0f);

	PostProcessOps::VolumeSample samples[2];
	samples[0] = {10.0f, 1.0f, &high};
	samples[1] = {0.0f, 1.0f, &low};

	const PostProcessOps::ResolvedPostProcess resolved = PostProcessOps::Blend(samples);
	EXPECT_NEAR(resolved.saturation, -100.0f, 1e-4f);
}

TEST(PostProcessBlendTest, DisabledEffectUntouched) {
	PostProcessProfile grading;
	PostProcessProfile vignette;
	MakeGradingProfile(grading, 50.0f, 0.0f);
	MakeVignetteProfile(vignette, 0.8f);

	PostProcessOps::VolumeSample samples[2];
	samples[0] = {0.0f, 1.0f, &grading};
	samples[1] = {1.0f, 1.0f, &vignette};

	const PostProcessOps::ResolvedPostProcess resolved = PostProcessOps::Blend(samples);
	EXPECT_TRUE(resolved.colorGrading);
	EXPECT_TRUE(resolved.vignette);
	EXPECT_NEAR(resolved.saturation, 50.0f, 1e-4f);
	EXPECT_NEAR(resolved.vignetteIntensity, 0.8f, 1e-4f);
}

TEST(PostProcessBlendTest, EmptyStackDrawsNothing) {
	const PostProcessProfile silent;
	PostProcessOps::VolumeSample samples[1];
	samples[0] = {0.0f, 1.0f, &silent};

	EXPECT_FALSE(PostProcessOps::Blend(samples).AnyEnabled());

	PostProcessOps::VolumeSample none[1];
	none[0] = {0.0f, 1.0f, nullptr};
	EXPECT_FALSE(PostProcessOps::Blend(none).AnyEnabled());

	PostProcessProfile loud;
	MakeVignetteProfile(loud, 1.0f);
	PostProcessOps::VolumeSample away[1];
	away[0] = {0.0f, 0.0f, &loud};
	EXPECT_FALSE(PostProcessOps::Blend(away).AnyEnabled());
}

TEST(PostProcessBlendTest, NonNumericOverrideSwitchesAtHalfContribution) {
	PostProcessProfile square;
	PostProcessProfile round;
	MakeVignetteProfile(square, 0.5f, false);
	MakeVignetteProfile(round, 0.5f, true);

	PostProcessOps::VolumeSample below[2];
	below[0] = {0.0f, 1.0f, &square};
	below[1] = {1.0f, 0.49f, &round};
	EXPECT_FALSE(PostProcessOps::Blend(below).vignetteRounded);

	PostProcessOps::VolumeSample above[2];
	above[0] = {0.0f, 1.0f, &square};
	above[1] = {1.0f, 0.51f, &round};
	EXPECT_TRUE(PostProcessOps::Blend(above).vignetteRounded);
}

TEST(PostProcessBlendTest, AGlobalVolumeContributesItsWeightAnywhere) {
	const Mat4 identity = Mat4::Identity();
	EXPECT_NEAR(VolumeContribution(true, identity, Vec3(1, 1, 1),
								   0.0f, 0.75f, Vec3(1000, -400, 900)),
				0.75f, 1e-5f);

	EXPECT_NEAR(VolumeContribution(true, identity, Vec3(1, 1, 1), 0.0f, 0.0f, Vec3()), 0.0f, 1e-5f);
}

TEST(PostProcessBlendTest, LocalVolumeFallsOffByCurve) {
	const Mat4 identity = Mat4::Identity();
	const Vec3 size(4, 4, 4);
	constexpr f32 BLEND = 4.0f;
	constexpr f32 WEIGHT = 1.0f;

	EXPECT_NEAR(VolumeContribution(false, identity, size, BLEND, WEIGHT, Vec3(0, 0, 0)), 1.0f, 1e-5f);
	EXPECT_NEAR(VolumeContribution(false, identity, size, BLEND, WEIGHT, Vec3(1.9f, 0, 0)), 1.0f, 1e-5f);

	EXPECT_NEAR(VolumeContribution(false, identity, size, BLEND, WEIGHT, Vec3(3, 0, 0)), 0.9375f, 1e-5f);
	EXPECT_NEAR(VolumeContribution(false, identity, size, BLEND, WEIGHT, Vec3(4, 0, 0)), 0.75f, 1e-5f);

	EXPECT_NEAR(VolumeContribution(false, identity, size, BLEND, WEIGHT, Vec3(6, 0, 0)), 0.0f, 1e-5f);
	EXPECT_NEAR(VolumeContribution(false, identity, size, BLEND, WEIGHT, Vec3(60, 0, 0)), 0.0f, 1e-5f);

	EXPECT_NEAR(VolumeContribution(false, identity, size, BLEND, 0.5f, Vec3(3, 0, 0)),
				0.46875f, 1e-5f);
}

TEST(PostProcessBlendTest, ZeroBlendDistanceIsHardEdge) {
	const Mat4 identity = Mat4::Identity();
	const Vec3 size(4, 4, 4);
	EXPECT_NEAR(VolumeContribution(false, identity, size, 0.0f, 1.0f, Vec3(1.99f, 0, 0)), 1.0f, 1e-5f);
	EXPECT_NEAR(VolumeContribution(false, identity, size, 0.0f, 1.0f, Vec3(2.01f, 0, 0)), 0.0f, 1e-5f);
}

TEST(PostProcessBlendTest, BalanceAndTonesBlendByContribution) {
	PostProcessProfile first;
	first.whiteBalance = true;
	first.temperature = 40.0f;
	first.shadowsMidtonesHighlights = true;
	first.shadows = Color(0.5f, 1.0f, 0.5f);
	first.highlightsEnd = 0.8f;
	PostProcessProfile second;
	second.whiteBalance = true;
	second.tint = -20.0f;

	PostProcessOps::VolumeSample samples[2];
	samples[0] = {0.0f, 1.0f, &first};
	samples[1] = {1.0f, 0.25f, &second};
	const PostProcessOps::ResolvedPostProcess resolved = PostProcessOps::Blend(samples);

	EXPECT_TRUE(resolved.AnyEnabled());
	EXPECT_FALSE(resolved.colorGrading);
	ASSERT_TRUE(resolved.whiteBalance);
	ASSERT_TRUE(resolved.shadowsMidtonesHighlights);
	EXPECT_NEAR(resolved.temperature, 30.0f, 1e-4f);
	EXPECT_NEAR(resolved.tint, -5.0f, 1e-4f);
	EXPECT_NEAR(resolved.shadows.r, 0.5f, 1e-4f);
	EXPECT_NEAR(resolved.shadows.g, 1.0f, 1e-4f);
	EXPECT_NEAR(resolved.highlightsEnd, 0.8f, 1e-4f);
	EXPECT_NEAR(resolved.shadowsEnd, 0.3f, 1e-4f);
}

namespace {

	f64 DaylightLocusY(f64 x) {
		return 2.870 * x - 3.000 * x * x - 0.275;
	}

	Vec3 Rec709OfWhite(f64 x, f64 y) {
		const f64 tristimulusX = x / y;
		const f64 tristimulusZ = (1.0 - x - y) / y;
		return Vec3(static_cast<f32>(3.2406 * tristimulusX - 1.5372 - 0.4986 * tristimulusZ),
					static_cast<f32>(-0.9689 * tristimulusX + 1.8758 + 0.0415 * tristimulusZ),
					static_cast<f32>(0.0557 * tristimulusX - 0.2040 + 1.0570 * tristimulusZ));
	}

} // namespace

TEST(PostProcessGradeTest, ZeroBalanceIsIdentity) {
	const Mat3 balance = PostProcessOps::WhiteBalanceMatrix(0.0f, 0.0f);
	for (u32 row = 0; row < 3; ++row) {
		for (u32 column = 0; column < 3; ++column) {
			EXPECT_NEAR(balance(row, column), row == column ? 1.0f : 0.0f, 1e-6f) << row << ',' << column;
		}
	}
}

TEST(PostProcessGradeTest, ChosenWhiteBecomesNeutral) {
	const Vec3 neutral = Rec709OfWhite(0.31271, DaylightLocusY(0.31271));
	const Vec2 cases[] = {Vec2(60.0f, 0.0f), Vec2(-80.0f, 0.0f), Vec2(0.0f, 45.0f), Vec2(-30.0f, -70.0f), Vec2(100.0f, 100.0f)};
	for (const Vec2& slider : cases) {
		const f64 warmth = slider.x / 65.0;
		const f64 x = 0.31271 - warmth * (warmth < 0.0 ? 0.1 : 0.05);
		const f64 y = DaylightLocusY(x) + slider.y / 65.0 * 0.05;
		const Vec3 adapted = PostProcessOps::WhiteBalanceMatrix(slider.x, slider.y) * Rec709OfWhite(x, y);
		EXPECT_NEAR(adapted.x, neutral.x, 2e-3f) << slider.x << ' ' << slider.y;
		EXPECT_NEAR(adapted.y, neutral.y, 2e-3f) << slider.x << ' ' << slider.y;
		EXPECT_NEAR(adapted.z, neutral.z, 2e-3f) << slider.x << ' ' << slider.y;
	}
	EXPECT_NEAR(neutral.x, 1.0f, 2e-3f);
	EXPECT_NEAR(neutral.y, 1.0f, 2e-3f);
	EXPECT_NEAR(neutral.z, 1.0f, 2e-3f);
}

TEST(PostProcessGradeTest, SlidersShiftGrayTheirWay) {
	const Vec3 gray(0.5f, 0.5f, 0.5f);
	const Vec3 warm = PostProcessOps::WhiteBalanceMatrix(50.0f, 0.0f) * gray;
	EXPECT_GT(warm.x, gray.x);
	EXPECT_LT(warm.z, gray.z);
	const Vec3 cold = PostProcessOps::WhiteBalanceMatrix(-50.0f, 0.0f) * gray;
	EXPECT_LT(cold.x, gray.x);
	EXPECT_GT(cold.z, gray.z);
	const Vec3 magenta = PostProcessOps::WhiteBalanceMatrix(0.0f, 50.0f) * gray;
	EXPECT_LT(magenta.y, magenta.x);
	EXPECT_LT(magenta.y, magenta.z);
	const Vec3 green = PostProcessOps::WhiteBalanceMatrix(0.0f, -50.0f) * gray;
	EXPECT_GT(green.y, green.x);
	EXPECT_GT(green.y, green.z);
	const Vec3 clamped = PostProcessOps::WhiteBalanceMatrix(400.0f, 0.0f) * gray;
	const Vec3 limit = PostProcessOps::WhiteBalanceMatrix(100.0f, 0.0f) * gray;
	EXPECT_NEAR(clamped.x, limit.x, 1e-6f);
	EXPECT_NEAR(clamped.z, limit.z, 1e-6f);
}

TEST(PostProcessGradeTest, ToneOffsetRaisesAndLowers) {
	const Vec3 lifted = PostProcessOps::ToneMultiplier(Color::White(), 0.25f);
	EXPECT_NEAR(lifted.x, 2.0f, 1e-6f);
	EXPECT_NEAR(lifted.z, 2.0f, 1e-6f);
	const Vec3 lowered = PostProcessOps::ToneMultiplier(Color::White(), -0.5f);
	EXPECT_NEAR(lowered.y, 0.5f, 1e-6f);
	const Vec3 floored = PostProcessOps::ToneMultiplier(Color(0.2f, 0.4f, 1.0f), -0.5f);
	EXPECT_EQ(floored.x, 0.0f);
	EXPECT_EQ(floored.y, 0.0f);
	EXPECT_NEAR(floored.z, 0.5f, 1e-6f);
	const Vec3 tinted = PostProcessOps::ToneMultiplier(Color(0.5f, 1.0f, 0.5f), 0.0f);
	EXPECT_NEAR(tinted.x, 0.5f, 1e-6f);
	EXPECT_NEAR(tinted.y, 1.0f, 1e-6f);
	const Vec3 capped = PostProcessOps::ToneMultiplier(Color::White(), 3.0f);
	EXPECT_NEAR(capped.y, 5.0f, 1e-6f);
}

TEST(PostProcessBlendTest, FalloffInWorldUnits) {
	Mat4 stretched = Mat4::Identity();
	stretched = Mat4::Scale(Vec3(10.0f, 1.0f, 1.0f));
	const Vec3 size(4, 4, 4);

	EXPECT_NEAR(VolumeContribution(false, stretched, size, 4.0f, 1.0f, Vec3(19.0f, 0, 0)), 1.0f, 1e-4f);
	EXPECT_NEAR(VolumeContribution(false, stretched, size, 4.0f, 1.0f, Vec3(22.0f, 0, 0)), 0.75f, 1e-4f);
	EXPECT_NEAR(VolumeContribution(false, stretched, size, 4.0f, 1.0f, Vec3(25.0f, 0, 0)), 0.0f, 1e-4f);
	EXPECT_NEAR(VolumeContribution(false, stretched, size, 4.0f, 1.0f, Vec3(0, 4.0f, 0)), 0.75f, 1e-4f);
}

namespace {

	class PostProcessVolumeLifetimeFixture : public ::testing::Test {
	protected:
		Handle<Scene> _scene;

		void SetUp() override {
			SceneSystem::Initialize();
			_scene = SceneSystem::CreateScene("PostProcessVolumeLifetime");
		}

		void TearDown() override { SceneSystem::Terminate(); }
	};

} // namespace

TEST(PostProcessRowTest, RowDeclaresPostPointAndSceneColor) {
	PostProcessPass pass;
	EXPECT_EQ(pass.point, PassPoint::Post);
	EXPECT_EQ(pass.position, FrameStagePosition::Normal);
	EXPECT_EQ(pass.GetRequests(), PassRequests::SceneColor);

	EXPECT_EQ(pass.GetAssetTypeId(), StringHash("PostProcessPass"));
}

TEST(PostProcessRowTest, AProfileRoundTripsThroughItsDocument) {
	std::filesystem::path dir = std::filesystem::temp_directory_path() / "shalltear_postprocess_roundtrip";
	std::filesystem::create_directories(dir);
	const Path file((dir / "profile.slpp").generic_string());

	PostProcessProfile written;
	written.colorGrading = true;
	written.postExposure = -0.75f;
	written.contrast = 12.5f;
	written.saturation = -37.5f;
	written.hueShift = 90.0f;
	written.colorFilter = Color(0.25f, 0.5f, 0.75f, 1.0f);
	written.vignette = true;
	written.vignetteIntensity = 0.6f;
	written.vignetteSmoothness = 0.4f;
	written.vignetteCenter = Vec2(0.4f, 0.6f);
	written.vignetteRounded = true;
	written.whiteBalance = true;
	written.temperature = -35.0f;
	written.tint = 12.0f;
	written.shadowsMidtonesHighlights = true;
	written.shadows = Color(0.5f, 1.0f, 0.75f, 1.0f);
	written.shadowsOffset = 0.2f;
	written.midtonesOffset = -0.4f;
	written.highlights = Color(1.0f, 0.9f, 0.6f, 1.0f);
	written.highlightsStart = 0.6f;
	written.highlightsEnd = 1.5f;
	ASSERT_TRUE(written.Export(file));

	PostProcessProfile read;
	ASSERT_TRUE(read.Import(file));
	EXPECT_TRUE(read.colorGrading);
	EXPECT_NEAR(read.postExposure, -0.75f, 1e-4f);
	EXPECT_NEAR(read.contrast, 12.5f, 1e-4f);
	EXPECT_NEAR(read.saturation, -37.5f, 1e-4f);
	EXPECT_NEAR(read.hueShift, 90.0f, 1e-4f);
	EXPECT_NEAR(read.colorFilter.g, 0.5f, 1e-4f);
	EXPECT_TRUE(read.vignette);
	EXPECT_NEAR(read.vignetteIntensity, 0.6f, 1e-4f);
	EXPECT_NEAR(read.vignetteSmoothness, 0.4f, 1e-4f);
	EXPECT_NEAR(read.vignetteCenter.y, 0.6f, 1e-4f);
	EXPECT_TRUE(read.vignetteRounded);
	EXPECT_TRUE(read.whiteBalance);
	EXPECT_NEAR(read.temperature, -35.0f, 1e-4f);
	EXPECT_NEAR(read.tint, 12.0f, 1e-4f);
	EXPECT_TRUE(read.shadowsMidtonesHighlights);
	EXPECT_NEAR(read.shadows.b, 0.75f, 1e-4f);
	EXPECT_NEAR(read.shadowsOffset, 0.2f, 1e-4f);
	EXPECT_NEAR(read.midtonesOffset, -0.4f, 1e-4f);
	EXPECT_NEAR(read.highlights.b, 0.6f, 1e-4f);
	EXPECT_NEAR(read.highlightsStart, 0.6f, 1e-4f);
	EXPECT_NEAR(read.highlightsEnd, 1.5f, 1e-4f);

	std::error_code ec;
	std::filesystem::remove_all(dir, ec);
}

TEST_F(PostProcessVolumeLifetimeFixture, VolumeLeavesLiveList) {
	const u32 before = PostProcessVolume::GetEnabledCount();

	Handle<SceneObject> object = SceneSystem::CreateObject("Volume", _scene);
	Handle<PostProcessVolume> volume = object->AddComponent<PostProcessVolume>();
	ASSERT_TRUE(volume.Get());
	EXPECT_EQ(PostProcessVolume::GetEnabledCount(), before + 1);

	volume->SetEnabled(false);
	EXPECT_EQ(PostProcessVolume::GetEnabledCount(), before);
	volume->SetEnabled(true);
	EXPECT_EQ(PostProcessVolume::GetEnabledCount(), before + 1);

	object->SetActive(false);
	EXPECT_EQ(PostProcessVolume::GetEnabledCount(), before);
	object->SetActive(true);
	EXPECT_EQ(PostProcessVolume::GetEnabledCount(), before + 1);

	SceneSystem::Destroy(object);
	EXPECT_EQ(PostProcessVolume::GetEnabledCount(), before);
}

#endif

namespace {

	class PostProcessDisableCycleFixture : public ::testing::Test {
	protected:
#ifdef SHALLTEAR_SHARED
		ModuleHandle _module;
#else
		char _token = 0;
#endif

		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			ModuleSystem::Initialize();
		}

		void TearDown() override {
			ModuleSystem::Terminate();
		}

		bool StandUp() {
#ifdef SHALLTEAR_SHARED
			_module = ModuleSystem::Load(Path(str(PACKAGE_TARGET) + SLT_TEST_DLL_SUFFIX));
			return ModuleSystem::IsLoaded(_module);
#else
			SLT_CALL_LINKED_PACKAGE(PostProcessing, &_token);
			return true;
#endif
		}

		const void* OwnerToken() const {
#ifdef SHALLTEAR_SHARED
			return ModuleSystem::GetNativeHandle(_module);
#else
			return &_token;
#endif
		}
	};

} // namespace

TEST_F(PostProcessDisableCycleFixture, RegistersTypesAndImporter) {
	ASSERT_TRUE(StandUp());

	const void* owner = OwnerToken();
	ASSERT_NE(owner, nullptr);

	EXPECT_NE(TypeRegistry::FindType(StringHash("PostProcessVolume")), nullptr);
	EXPECT_NE(TypeRegistry::FindType(StringHash("PostProcessProfile")), nullptr);
	EXPECT_NE(TypeRegistry::FindType(StringHash("PostProcessPass")), nullptr);
	EXPECT_NE(TypeRegistry::FindType(StringHash("AmbientOcclusionPass")), nullptr);
	EXPECT_NE(TypeRegistry::FindType(StringHash("LensFlare")), nullptr);
	EXPECT_NE(TypeRegistry::FindType(StringHash("LensFlareAsset")), nullptr);
	EXPECT_NE(TypeRegistry::FindType(StringHash("LensFlarePass")), nullptr);

	EXPECT_EQ(FrameStages::UnregisterOwnedBy(owner), 0u);

	EXPECT_EQ(AssetImporterRegistry::UnregisterOwnedBy(owner), 2u);
}

#ifdef SHALLTEAR_SHARED
TEST_F(PostProcessDisableCycleFixture, TypesSweptWithImage) {
	ASSERT_TRUE(StandUp());
	ASSERT_NE(TypeRegistry::FindType(StringHash("PostProcessVolume")), nullptr);

	ModuleSystem::Unload(_module);
	EXPECT_FALSE(ModuleSystem::IsLoaded(_module));

	EXPECT_EQ(TypeRegistry::FindType(StringHash("PostProcessVolume")), nullptr);
	EXPECT_EQ(TypeRegistry::FindType(StringHash("PostProcessProfile")), nullptr);
	EXPECT_EQ(TypeRegistry::FindType(StringHash("PostProcessPass")), nullptr);
	EXPECT_EQ(TypeRegistry::FindType(StringHash("LensFlare")), nullptr);
	EXPECT_EQ(TypeRegistry::FindType(StringHash("LensFlareAsset")), nullptr);
	EXPECT_EQ(TypeRegistry::FindType(StringHash("LensFlarePass")), nullptr);
}
#endif
