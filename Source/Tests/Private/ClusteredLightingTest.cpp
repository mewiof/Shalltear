#include "TestsPCH.h"

#include "ShadingHarness.h"

#include "Graphics/LightCulling.h"
#include "Graphics/Exposure.h"

#include <vector>

using namespace Shalltear;
using ShadingTest::CenterTexel;
using ShadingTest::MaterialReadbackFixture;
using ShadingTest::ShadingStage;
using Shalltear::GraphicsInternal::LightCandidate;
using Shalltear::GraphicsInternal::SelectViewLights;

namespace {

	LightCandidate Punctual(Vec3 position, Vec3 color, u64 key) {
		LightCandidate candidate;
		candidate.position = position;
		candidate.range = 10.0f;
		candidate.color = color;
		candidate.direction = Vec3(0.0f, 0.0f, 1.0f);
		candidate.type = LightType::Point;
		candidate.stableKey = key;
		return candidate;
	}

	LightCandidate Directional(Vec3 color, u64 key) {
		LightCandidate candidate;
		candidate.color = color;
		candidate.direction = Vec3(0.0f, -1.0f, 0.0f);
		candidate.type = LightType::Directional;
		candidate.stableKey = key;
		return candidate;
	}

	Vec3 Grey(f32 value) { return Vec3(value, value, value); }

} // namespace

TEST(LightSelectionTest, DirectionalFirstBrightestLeads) {
	std::vector<LightCandidate> lights{
		Directional(Grey(0.25f), 10),
		Punctual(Vec3(0.0f, 0.0f, 1.0f), Grey(1.0f), 11),
		Directional(Grey(0.9f), 12),
	};

	u32 directionalCount = 0;
	u32 dropped = 0;
	u32 kept = SelectViewLights({lights.data(), lights.size()}, Vec3(0.0f, 0.0f, 0.0f), 256, directionalCount, dropped);

	EXPECT_EQ(kept, 3u);
	EXPECT_EQ(dropped, 0u);
	EXPECT_EQ(directionalCount, 2u);
	EXPECT_EQ(lights[0].stableKey, 12u);
	EXPECT_EQ(lights[1].stableKey, 10u);
	EXPECT_FALSE(lights[2].IsDirectional());
}

TEST(LightSelectionTest, ViewCapKeepsNearestCountsRest) {
	std::vector<LightCandidate> lights;
	for (u32 i = 10; i >= 1; --i) {
		lights.push_back(Punctual(Vec3(0.0f, 0.0f, static_cast<f32>(i)), Grey(1.0f), i));
	}

	u32 directionalCount = 0;
	u32 dropped = 0;
	u32 kept = SelectViewLights({lights.data(), lights.size()}, Vec3(0.0f, 0.0f, 0.0f), 4, directionalCount, dropped);

	EXPECT_EQ(kept, 4u);
	EXPECT_EQ(dropped, 6u);
	EXPECT_EQ(directionalCount, 0u);
	for (u32 i = 0; i < kept; ++i) {
		EXPECT_EQ(lights[i].stableKey, i + 1) << "entry " << i << ' ' << (i + 1) << "th nearest";
	}
}

TEST(LightSelectionTest, DirectionalSurvivesCap) {
	std::vector<LightCandidate> lights{
		Punctual(Vec3(0.0f, 0.0f, 1.0f), Grey(1.0f), 1),
		Punctual(Vec3(0.0f, 0.0f, 2.0f), Grey(1.0f), 2),
		Directional(Grey(0.1f), 3),
	};

	u32 directionalCount = 0;
	u32 dropped = 0;
	u32 kept = SelectViewLights({lights.data(), lights.size()}, Vec3(0.0f, 0.0f, 0.0f), 1, directionalCount, dropped);

	EXPECT_EQ(kept, 1u);
	EXPECT_EQ(dropped, 2u);
	EXPECT_EQ(directionalCount, 1u);
	EXPECT_EQ(lights[0].stableKey, 3u);
}

TEST(LightSelectionTest, EqualDistanceOrderIsStable) {
	auto ordered = [](std::vector<LightCandidate> lights) {
		u32 directionalCount = 0;
		u32 dropped = 0;
		SelectViewLights({lights.data(), lights.size()}, Vec3(0.0f, 0.0f, 0.0f), 256, directionalCount, dropped);
		std::vector<u64> keys;
		for (const LightCandidate& light : lights) {
			keys.push_back(light.stableKey);
		}
		return keys;
	};

	const Vec3 shared(0.0f, 0.0f, 4.0f);
	std::vector<LightCandidate> forward{
		Punctual(shared, Grey(1.0f), 7),
		Punctual(shared, Grey(1.0f), 8),
		Punctual(shared, Grey(1.0f), 9),
	};
	std::vector<LightCandidate> reversed{forward.rbegin(), forward.rend()};

	EXPECT_EQ(ordered(forward), ordered(reversed));
	EXPECT_EQ(ordered(forward), (std::vector<u64>{7, 8, 9}));
}

namespace {

	using Shalltear::GraphicsInternal::SolveSpotCone;

	f32 ConeStrength(f32 outerDeg, f32 innerDeg, f32 angleDeg) {
		f32 scale = 0.0f;
		f32 offset = 1.0f;
		SolveSpotCone(outerDeg, innerDeg, scale, offset);
		return Math::Clamp(std::cos(angleDeg * Math::DEG_TO_RAD) * scale + offset, 0.0f, 1.0f);
	}

} // namespace

TEST(SpotConeTest, ZeroInnerAngleIsSingleCone) {
	const f32 outer = 45.0f;
	const f32 cosOuter = std::cos(outer * 0.5f * Math::DEG_TO_RAD);
	for (f32 angle : {0.0f, 5.0f, 11.25f, 20.0f, 22.5f, 30.0f}) {
		const f32 legacy = Math::Clamp((std::cos(angle * Math::DEG_TO_RAD) - cosOuter) / (1.0f - cosOuter), 0.0f, 1.0f);
		EXPECT_NEAR(ConeStrength(outer, 0.0f, angle), legacy, 1e-5f) << "at " << angle;
	}
}

TEST(SpotConeTest, WiderInnerHoldsFullStrength) {
	const f32 outer = 45.0f;
	EXPECT_FLOAT_EQ(ConeStrength(outer, 30.0f, 10.0f), 1.0f);
	EXPECT_LT(ConeStrength(outer, 0.0f, 10.0f), 1.0f);
	EXPECT_FLOAT_EQ(ConeStrength(outer, 30.0f, 22.5f), 0.0f);
	EXPECT_FLOAT_EQ(ConeStrength(outer, 0.0f, 22.5f), 0.0f);
	EXPECT_FLOAT_EQ(ConeStrength(outer, 30.0f, 40.0f), 0.0f);
}

TEST(SpotConeTest, InnerPastOuterHardensEdge) {
	const f32 outer = 45.0f;
	EXPECT_FLOAT_EQ(ConeStrength(outer, 90.0f, 0.0f), 1.0f);
	EXPECT_FLOAT_EQ(ConeStrength(outer, 90.0f, 22.4f), 1.0f);
	EXPECT_FLOAT_EQ(ConeStrength(outer, 90.0f, 22.6f), 0.0f);
	EXPECT_FLOAT_EQ(ConeStrength(outer, 90.0f, 80.0f), 0.0f);
}

TEST(SpotConeTest, ConelessLightUntouched) {
	LightCandidate point = Punctual(Vec3(0.0f, 0.0f, 1.0f), Grey(1.0f), 1);
	EXPECT_FLOAT_EQ(point.spotScale, 0.0f);
	EXPECT_FLOAT_EQ(point.spotOffset, 1.0f);
	for (f32 cosAngle : {-1.0f, 0.0f, 0.5f, 1.0f}) {
		EXPECT_FLOAT_EQ(Math::Clamp(cosAngle * point.spotScale + point.spotOffset, 0.0f, 1.0f), 1.0f);
	}
}

namespace {

	using Shalltear::GraphicsInternal::DEFAULT_PHYSICAL_EV100;
	using Shalltear::GraphicsInternal::SolveLightIntensity;

	f32 Exposure() { return 1.0f / (1.2f * std::exp2(DEFAULT_PHYSICAL_EV100)); }

} // namespace

TEST(LightUnitsTest, ArbitraryUnitsPassEveryIntensityThroughUntouched) {
	for (LightType type : {LightType::Directional, LightType::Point, LightType::Spot}) {
		for (f32 intensity : {0.0f, 0.5f, 1.0f, 1000.0f}) {
			EXPECT_FLOAT_EQ(SolveLightIntensity(type, intensity, false), intensity);
		}
	}
}

TEST(LightUnitsTest, PhysicalUnitsPerLightType) {
	EXPECT_FLOAT_EQ(SolveLightIntensity(LightType::Directional, 100000.0f, true), 100000.0f);
	EXPECT_FLOAT_EQ(SolveLightIntensity(LightType::Point, 1000.0f, true), 1000.0f / (4.0f * Math::PI));
	EXPECT_FLOAT_EQ(SolveLightIntensity(LightType::Spot, 1000.0f, true), 1000.0f / Math::PI);

	EXPECT_GT(SolveLightIntensity(LightType::Spot, 1000.0f, true), SolveLightIntensity(LightType::Point, 1000.0f, true));
}

namespace {

	using Shalltear::GraphicsInternal::BinLights;
	using Shalltear::GraphicsInternal::BuildClusterGrid;
	using Shalltear::GraphicsInternal::ClusterGrid;
	using Shalltear::GraphicsInternal::ClusterIndexOf;
	using Shalltear::GraphicsInternal::PunctualDepthSpan;

	ClusterGrid TestGrid(f32 lightsNearDepth = 0.1f, f32 lightsFarDepth = 100.0f) {
		return BuildClusterGrid(Mat4::PerspectiveReverseZ(60.0f, 1.0f, 0.1f, 100.0f), lightsNearDepth, lightsFarDepth);
	}

	struct Binned {
		Vector<u32> table{GetMemoryResource(MemTag::Graphics)};
		Vector<u32> indices{GetMemoryResource(MemTag::Graphics)};
		Vector<f32> keys{GetMemoryResource(MemTag::Graphics)};
		u32 dropped = 0;

		u32 CountAt(u32 cluster) const { return table[static_cast<size_t>(cluster) * 2 + 1]; }
		bool Lists(u32 cluster, u32 lightIndex) const {
			const u32 offset = table[static_cast<size_t>(cluster) * 2];
			for (u32 i = 0; i < CountAt(cluster); ++i) {
				if (indices[offset + i] == lightIndex) {
					return true;
				}
			}
			return false;
		}
	};

	void Bin(const ClusterGrid& grid, std::span<const LightCandidate> lights, u32 lightsPerCluster, Binned& out) {
		out.dropped = BinLights(grid, Mat4::Identity(), lights, 0, lightsPerCluster, out.table, out.indices, out.keys);
	}

	LightCandidate Ranged(Vec3 viewPosition, f32 range, u64 key) {
		LightCandidate candidate = Punctual(viewPosition, Grey(1.0f), key);
		candidate.range = range;
		return candidate;
	}

} // namespace

TEST(LightClusterTest, LightListedInCentreCluster) {
	const ClusterGrid grid = TestGrid();
	const Vec3 position(0.6f, 0.4f, 4.0f);
	const LightCandidate lights[] = {Ranged(position, 0.2f, 1)};

	Binned binned;
	Bin(grid, lights, 32, binned);

	const u32 own = ClusterIndexOf(grid, position);
	EXPECT_TRUE(binned.Lists(own, 0));
	EXPECT_EQ(binned.dropped, 0u);

	const u32 elsewhere = ClusterIndexOf(grid, Vec3(-0.6f, -0.4f, 40.0f));
	ASSERT_NE(elsewhere, own);
	EXPECT_FALSE(binned.Lists(elsewhere, 0));
}

TEST(LightClusterTest, LightAboveAxisBinsUpper) {
	const ClusterGrid grid = TestGrid();
	const LightCandidate lights[] = {Ranged(Vec3(0.0f, 1.2f, 4.0f), 0.3f, 1)};

	Binned binned;
	Bin(grid, lights, 32, binned);

	u32 listedIn = 0;
	for (u32 z = 0; z < grid.dimZ; ++z) {
		for (u32 y = 0; y < grid.dimY; ++y) {
			for (u32 x = 0; x < grid.dimX; ++x) {
				const u32 cluster = (z * grid.dimY + y) * grid.dimX + x;
				if (!binned.Lists(cluster, 0)) {
					continue;
				}
				++listedIn;
				EXPECT_LT(y, grid.dimY / 2);
			}
		}
	}
	EXPECT_GT(listedIn, 0u);
}

TEST(LightClusterTest, WiderLightReachesMoreClusters) {
	const ClusterGrid grid = TestGrid();
	auto reach = [&](f32 range) {
		const LightCandidate lights[] = {Ranged(Vec3(0.0f, 0.0f, 6.0f), range, 1)};
		Binned binned;
		Bin(grid, lights, 32, binned);
		u32 clusters = 0;
		for (u32 cluster = 0; cluster < grid.ClusterCount(); ++cluster) {
			clusters += binned.CountAt(cluster);
		}
		return clusters;
	};
	EXPECT_GT(reach(2.0f), reach(0.25f));
	EXPECT_GT(reach(0.25f), 0u);
}

TEST(LightClusterTest, LightBehindViewNotBinned) {
	const ClusterGrid grid = TestGrid();
	const LightCandidate lights[] = {Ranged(Vec3(0.0f, 0.0f, -8.0f), 1.0f, 1)};

	Binned binned;
	Bin(grid, lights, 32, binned);

	u32 total = 0;
	for (u32 cluster = 0; cluster < grid.ClusterCount(); ++cluster) {
		total += binned.CountAt(cluster);
	}
	EXPECT_EQ(total, 0u);
	EXPECT_EQ(binned.dropped, 0u);
}

TEST(LightClusterTest, FullClusterKeepsNearest) {
	const ClusterGrid grid = TestGrid();
	std::vector<LightCandidate> lights;
	for (u32 i = 0; i < 6; ++i) {
		lights.push_back(Ranged(Vec3(0.0f, 0.0f, 5.0f), 0.3f, i));
	}

	Binned binned;
	Bin(grid, {lights.data(), lights.size()}, 2, binned);

	const u32 shared = ClusterIndexOf(grid, Vec3(0.0f, 0.0f, 5.0f));
	EXPECT_EQ(binned.CountAt(shared), 2u);
	EXPECT_TRUE(binned.Lists(shared, 0));
	EXPECT_TRUE(binned.Lists(shared, 1));
	EXPECT_FALSE(binned.Lists(shared, 5));
	EXPECT_GT(binned.dropped, 0u);
}

TEST(LightClusterTest, FullClusterKeepsNearestToItself) {
	const ClusterGrid grid = TestGrid();
	const Vec3 inside(0.35f, 0.0f, 8.75f);
	const u32 target = ClusterIndexOf(grid, inside);

	std::vector<LightCandidate> lights;
	lights.push_back(Ranged(Vec3(0.2f, 0.0f, 4.5f), 3.5f, 1));
	lights.push_back(Ranged(Vec3(0.5f, 0.0f, 4.5f), 3.5f, 2));
	lights.push_back(Ranged(Vec3(0.2f, -0.2f, 8.6f), 0.5f, 3));
	lights.push_back(Ranged(Vec3(0.5f, 0.2f, 8.9f), 0.5f, 4));
	ASSERT_EQ(ClusterIndexOf(grid, lights[2].position), target);
	ASSERT_EQ(ClusterIndexOf(grid, lights[3].position), target);

	Binned binned;
	Bin(grid, {lights.data(), lights.size()}, 2, binned);

	EXPECT_EQ(binned.CountAt(target), 2u);
	EXPECT_TRUE(binned.Lists(target, 2));
	EXPECT_TRUE(binned.Lists(target, 3));
	EXPECT_FALSE(binned.Lists(target, 0));
	EXPECT_FALSE(binned.Lists(target, 1));
	EXPECT_GT(binned.dropped, 0u);
}

TEST(LightClusterTest, AdjacentFroxelsAgreeAtBorder) {
	const ClusterGrid grid = TestGrid();
	std::vector<LightCandidate> lights;
	for (u32 iz = 0; iz < 10; ++iz) {
		for (u32 ix = 0; ix < 20; ++ix) {
			lights.push_back(Ranged(Vec3(-9.5f + static_cast<f32>(ix), -2.5f, 3.5f + static_cast<f32>(iz)), 3.5f, iz * 20 + ix));
		}
	}

	Binned binned;
	Bin(grid, {lights.data(), lights.size()}, 32, binned);
	ASSERT_GT(binned.dropped, 0u);

	auto attenuation = [](const Vec3& point, const LightCandidate& light) {
		const f32 distanceSquared = (point - light.position).LengthSquared();
		const f32 ratio = distanceSquared / (light.range * light.range);
		const f32 window = Math::Clamp(1.0f - ratio * ratio, 0.0f, 1.0f);
		return window * window / Math::Max(distanceSquared, 1e-4f);
	};
	auto delivered = [&](u32 cluster, const Vec3& point) {
		f32 total = 0.0f;
		const u32 offset = binned.table[static_cast<size_t>(cluster) * 2];
		for (u32 i = 0; i < binned.CountAt(cluster); ++i) {
			total += attenuation(point, lights[binned.indices[offset + i]]);
		}
		return total;
	};

	f32 worstStep = 0.0f;
	u32 seams = 0;
	for (u32 z = 0; z < grid.dimZ; ++z) {
		const f32 depth = std::exp2((static_cast<f32>(z) + 0.5f - grid.sliceBias) / grid.sliceScale);
		for (u32 y = 0; y + 1 < grid.dimY; ++y) {
			for (u32 x = 0; x + 1 < grid.dimX; ++x) {
				const u32 here = (z * grid.dimY + y) * grid.dimX + x;
				const u32 right = (z * grid.dimY + y) * grid.dimX + (x + 1);
				const u32 below = (z * grid.dimY + (y + 1)) * grid.dimX + x;
				if (binned.CountAt(here) == 0 && binned.CountAt(right) == 0 && binned.CountAt(below) == 0) {
					continue;
				}
				const f32 ndcXCenter = -1.0f + 2.0f * (static_cast<f32>(x) + 0.5f) / static_cast<f32>(grid.dimX);
				const f32 ndcXSeam = -1.0f + 2.0f * static_cast<f32>(x + 1) / static_cast<f32>(grid.dimX);
				const f32 ndcYCenter = 1.0f - 2.0f * (static_cast<f32>(y) + 0.5f) / static_cast<f32>(grid.dimY);
				const f32 ndcYSeam = 1.0f - 2.0f * static_cast<f32>(y + 1) / static_cast<f32>(grid.dimY);

				const Vec3 acrossX(ndcXSeam * grid.tanHalfX * depth, ndcYCenter * grid.tanHalfY * depth, depth);
				const Vec3 acrossY(ndcXCenter * grid.tanHalfX * depth, ndcYSeam * grid.tanHalfY * depth, depth);
				for (auto [a, b, point] : {std::tuple{here, right, acrossX}, std::tuple{here, below, acrossY}}) {
					f32 truth = 0.0f;
					for (const LightCandidate& light : lights) {
						truth += attenuation(point, light);
					}
					if (truth < 1e-3f) {
						continue;
					}
					++seams;
					const f32 step = std::abs(delivered(a, point) - delivered(b, point)) / truth;
					worstStep = Math::Max(worstStep, step);
				}
			}
		}
	}

	ASSERT_GT(seams, 1000u);
	EXPECT_LT(worstStep, 0.10f);
}

TEST(LightClusterTest, SlicesSpanLightsNotCameraClipRange) {
	const Mat4 projection = Mat4::PerspectiveReverseZ(60.0f, 1.92f, 0.05f, 5000.0f);
	const ClusterGrid grid = BuildClusterGrid(projection, 16.24f, 43.47f);

	EXPECT_LE(grid.zNear, 16.24f);
	EXPECT_GE(grid.zFar, 43.47f);
	EXPECT_NEAR(grid.zCameraNear, 0.05f, 1e-3f);

	const f32 ratio = std::exp2(1.0f / grid.sliceScale);
	const f32 tileAngle = Math::Max(2.0f * grid.tanHalfX / static_cast<f32>(grid.dimX),
									2.0f * grid.tanHalfY / static_cast<f32>(grid.dimY));
	EXPECT_LE(ratio, 1.0f + tileAngle + 1e-3f);

	const ClusterGrid straddled = BuildClusterGrid(projection, 0.05f, 43.47f);
	EXPECT_LE(std::exp2(1.0f / straddled.sliceScale), 1.0f + tileAngle + 1e-3f);
	EXPECT_GT(straddled.zNear, 0.05f);
	EXPECT_NEAR(straddled.zCameraNear, 0.05f, 1e-3f);
}

TEST(LightClusterTest, FragmentKeepsReachingLights) {
	const Mat4 projection = Mat4::PerspectiveReverseZ(60.0f, 1.92f, 0.05f, 5000.0f);

	std::vector<LightCandidate> lights;
	for (u32 iz = 0; iz < 10; ++iz) {
		for (u32 ix = 0; ix < 20; ++ix) {
			lights.push_back(Ranged(Vec3(-9.5f + static_cast<f32>(ix), -2.5f,
										 16.5f + static_cast<f32>(iz) * 3.0f),
									3.5f, iz * 20 + ix));
		}
	}

	f32 lightsNear = 0.0f;
	f32 lightsFar = 0.0f;
	ASSERT_TRUE(PunctualDepthSpan(Mat4::Identity(), {lights.data(), lights.size()}, lightsNear, lightsFar));
	const ClusterGrid grid = BuildClusterGrid(projection, lightsNear, lightsFar);

	Binned binned;
	Bin(grid, {lights.data(), lights.size()}, 32, binned);
	ASSERT_GT(binned.dropped, 0u);

	auto attenuation = [](const Vec3& point, const LightCandidate& light) {
		const f32 distanceSquared = (point - light.position).LengthSquared();
		const f32 ratio = distanceSquared / (light.range * light.range);
		const f32 window = Math::Clamp(1.0f - ratio * ratio, 0.0f, 1.0f);
		return window * window / Math::Max(distanceSquared, 1e-4f);
	};

	f32 worstMissing = 0.0f;
	u32 samples = 0;
	for (u32 iz = 0; iz < 120; ++iz) {
		for (u32 ix = 0; ix < 120; ++ix) {
			const Vec3 point(-10.0f + 20.0f * static_cast<f32>(ix) / 119.0f, -4.2f,
							 17.0f + 26.0f * static_cast<f32>(iz) / 119.0f);
			f32 truth = 0.0f;
			for (const LightCandidate& light : lights) {
				truth += attenuation(point, light);
			}
			if (truth < 1e-3f) {
				continue;
			}
			++samples;
			const u32 cluster = ClusterIndexOf(grid, point);
			f32 delivered = 0.0f;
			const u32 offset = binned.table[static_cast<size_t>(cluster) * 2];
			for (u32 i = 0; i < binned.CountAt(cluster); ++i) {
				delivered += attenuation(point, lights[binned.indices[offset + i]]);
			}
			worstMissing = Math::Max(worstMissing, 1.0f - delivered / truth);
		}
	}

	ASSERT_GT(samples, 5000u);
	EXPECT_LT(worstMissing, 0.20f);
}

namespace {

	Handle<Light> AddPointLight(const ShadingStage& stage, f32 z, Color color, f32 range) {
		Handle<SceneObject> object = SceneSystem::CreateObject("Point", stage.scene);
		object->SetLocalPosition(Vec3(0.0f, 0.0f, z));
		Handle<Light> light = object->AddComponent<Light>();
		light->SetLightType(LightType::Point);
		light->SetColor(color);
		light->SetIntensity(1.0f);
		light->SetRange(range);
		return light;
	}

} // namespace

TEST_F(MaterialReadbackFixture, ANinthLightStillLightsATexel) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	Asset* litShader = AssetDatabase::LoadAsset(Path("engine://Shaders/Lit.hlsl"));
	ASSERT_NE(litShader, nullptr);

	MaterialAsset* material = LoadMaterial("Ninth.mat", litShader->assetId,
										   "\t\"colors\": { \"baseColor\": [1, 1, 1, 1] },\n"
										   "\t\"floats\": { \"metallic\": 0, \"roughness\": 1, \"reflectance\": 0.5, \"normalScale\": 1, \"occlusionStrength\": 1 },\n"
										   "\t\"vec3s\": { \"emissive\": [0, 0, 0] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));

	Vector<Handle<Light>> fillers{GetMemoryResource(MemTag::Graphics)};
	for (u32 i = 0; i < 8; ++i) {
		fillers.push_back(AddPointLight(stage, 1.5f, Color(0.0f, 0.0f, 0.0f, 1.0f), 1.0f));
	}
	Handle<Light> ninth = AddPointLight(stage, 0.5f, Color(1.0f, 1.0f, 1.0f, 1.0f), 2.0f);
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 asNinth = CenterTexel(pixels, size);
	EXPECT_EQ(RenderSystem::GetStats().lightsDropped, 0u);

	for (Handle<Light> filler : fillers) {
		filler->SetEnabled(false);
	}
	SceneSystem::EndFrame();
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 asFirst = CenterTexel(pixels, size);

	ninth->SetEnabled(false);
	SceneSystem::EndFrame();
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 unlit = CenterTexel(pixels, size);

	EXPECT_GT(asFirst.x, unlit.x + 8.0f);
	EXPECT_NEAR(asNinth.x, asFirst.x, 1.5f);
	EXPECT_NEAR(asNinth.y, asFirst.y, 1.5f);
	EXPECT_NEAR(asNinth.z, asFirst.z, 1.5f);

	TearDownStage(stage);
}

TEST_F(MaterialReadbackFixture, SecondDirectionalLightKept) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	Asset* litShader = AssetDatabase::LoadAsset(Path("engine://Shaders/Lit.hlsl"));
	ASSERT_NE(litShader, nullptr);

	MaterialAsset* material = LoadMaterial("Fill.mat", litShader->assetId,
										   "\t\"colors\": { \"baseColor\": [1, 1, 1, 1] },\n"
										   "\t\"floats\": { \"metallic\": 0, \"roughness\": 1, \"reflectance\": 0.5, \"normalScale\": 1, \"occlusionStrength\": 1 },\n"
										   "\t\"vec3s\": { \"emissive\": [0, 0, 0] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 keyOnly = CenterTexel(pixels, size);

	Handle<SceneObject> fillObject = SceneSystem::CreateObject("Fill", stage.scene);
	fillObject->SetLocalRotation(Quat::AngleAxis(180.0f, Vec3::Up()));
	Handle<Light> fill = fillObject->AddComponent<Light>();
	fill->SetLightType(LightType::Directional);
	fill->SetColor(Color(0.4f, 0.4f, 0.4f, 1.0f));
	fill->SetIntensity(1.0f);
	SceneSystem::EndFrame();

	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 keyAndFill = CenterTexel(pixels, size);

	EXPECT_GT(keyAndFill.x, keyOnly.x + 8.0f);
	EXPECT_EQ(RenderSystem::GetStats().lightsBinned, 2u);

	TearDownStage(stage);
}

namespace {

	Vec3 TexelAt(const Vector<u8>& pixels, Vec2I size, i32 x, i32 y) {
		x = Math::Clamp(x, 0, size.x - 1);
		y = Math::Clamp(y, 0, size.y - 1);
		const size_t index = (static_cast<size_t>(y) * size.x + x) * 4;
		return Vec3(static_cast<f32>(pixels[index]), static_cast<f32>(pixels[index + 1]), static_cast<f32>(pixels[index + 2]));
	}

	Vec2I ProjectToPixel(Vec3 worldPosition, Vec2I size) {
		const ViewDesc view = ShadingTest::QuadView();
		const Vec4 clip = (view.projection * view.view) * Vec4(worldPosition, 1.0f);
		const f32 ndcX = clip.x / clip.w;
		const f32 ndcY = clip.y / clip.w;
		return Vec2I{static_cast<i32>((ndcX + 1.0f) * 0.5f * static_cast<f32>(size.x)),
					 static_cast<i32>((1.0f - ndcY) * 0.5f * static_cast<f32>(size.y))};
	}

} // namespace

TEST_F(MaterialReadbackFixture, ClusteredLightLightsItsPosition) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	Asset* litShader = AssetDatabase::LoadAsset(Path("engine://Shaders/Lit.hlsl"));
	ASSERT_NE(litShader, nullptr);

	MaterialAsset* material = LoadMaterial("Quadrant.mat", litShader->assetId,
										   "\t\"colors\": { \"baseColor\": [1, 1, 1, 1] },\n"
										   "\t\"floats\": { \"metallic\": 0, \"roughness\": 1, \"reflectance\": 0.5, \"normalScale\": 1, \"occlusionStrength\": 1 },\n"
										   "\t\"vec3s\": { \"emissive\": [0, 0, 0] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.quad->SetLocalScale(Vec3(3.0f, 3.0f, 1.0f));
	stage.sun->SetEnabled(false);

	const Vec3 lightPosition(0.9f, 0.6f, 0.15f);
	Handle<Light> light = AddPointLight(stage, lightPosition.z, Color(1.0f, 1.0f, 1.0f, 1.0f), 0.35f);
	light->GetOwner()->SetLocalPosition(lightPosition);
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));

	const Vec2I lit = ProjectToPixel(Vec3(lightPosition.x, lightPosition.y, 0.0f), size);
	const Vec2I mirroredX = ProjectToPixel(Vec3(-lightPosition.x, lightPosition.y, 0.0f), size);
	const Vec2I mirroredY = ProjectToPixel(Vec3(lightPosition.x, -lightPosition.y, 0.0f), size);
	const Vec2I mirroredBoth = ProjectToPixel(Vec3(-lightPosition.x, -lightPosition.y, 0.0f), size);

	const f32 under = TexelAt(pixels, size, lit.x, lit.y).x;
	EXPECT_GT(under, 8.0f);
	EXPECT_GT(under, TexelAt(pixels, size, mirroredX.x, mirroredX.y).x + 8.0f);
	EXPECT_GT(under, TexelAt(pixels, size, mirroredY.x, mirroredY.y).x + 8.0f);
	EXPECT_GT(under, TexelAt(pixels, size, mirroredBoth.x, mirroredBoth.y).x + 8.0f);

	TearDownStage(stage);
}

TEST_F(MaterialReadbackFixture, FalloffIsWindowedInverseSquare) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteProjectFile("Falloff.hlsl",
					 "#define SHALLTEAR_CUSTOM_LIGHTING\n"
					 "#pragma shalltear_template lit\n"
					 "[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 baseColor; };\n"
					 "float3 ShalltearLightingFunction(ShalltearSurfaceInput input, ShalltearSurface surface, float3 normalWS, ShalltearLightSample light) {\n"
					 "\treturn light.color * light.attenuation;\n"
					 "}\n"
					 "void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
					 "\tsurface.baseColor = baseColor.rgb;\n"
					 "\tsurface.metallic = 0.0;\n"
					 "}\n");
	Asset* falloffShader = AssetDatabase::LoadAsset(Path("project://Falloff.hlsl"));
	ASSERT_NE(falloffShader, nullptr);

	MaterialAsset* material = LoadMaterial("Falloff.mat", falloffShader->assetId,
										   "\t\"colors\": { \"baseColor\": [0, 0, 0, 1] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.sun->SetEnabled(false);

	const f32 range = 4.0f;
	Handle<Light> light = AddPointLight(stage, 1.0f, Color(1.0f, 1.0f, 1.0f, 1.0f), range);

	auto expectedAttenuation = [&](f32 distance) {
		const f32 distanceSquared = distance * distance;
		const f32 ratio = distanceSquared / (range * range);
		const f32 window = Math::Clamp(1.0f - ratio * ratio, 0.0f, 1.0f);
		return window * window / Math::Max(distanceSquared, 1e-4f);
	};

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	for (f32 distance : {1.0f, 1.5f, 2.0f, 3.0f}) {
		light->GetOwner()->SetLocalPosition(Vec3(0.0f, 0.0f, distance));
		SceneSystem::EndFrame();
		ASSERT_TRUE(RenderStage(stage, pixels, size));

		const Vec3 expected = ShadingTest::ExpectedTexel(Vec3(1.0f, 1.0f, 1.0f) * expectedAttenuation(distance));
		const Vec3 measured = CenterTexel(pixels, size);
		EXPECT_NEAR(measured.x, expected.x, 1.5f) << "at " << distance;
	}

	light->GetOwner()->SetLocalPosition(Vec3(0.0f, 0.0f, range));
	SceneSystem::EndFrame();
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 atRange = CenterTexel(pixels, size);

	light->SetEnabled(false);
	SceneSystem::EndFrame();
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 unlit = CenterTexel(pixels, size);
	EXPECT_FLOAT_EQ(atRange.x, unlit.x);
	light->SetEnabled(true);
	light->GetOwner()->SetLocalPosition(Vec3(0.0f, 0.0f, range * 0.5f));
	SceneSystem::EndFrame();
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	EXPECT_GT(CenterTexel(pixels, size).x, unlit.x + 1.0f);

	TearDownStage(stage);
}

TEST_F(MaterialReadbackFixture, WiderInnerBrightensEdge) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	Asset* litShader = AssetDatabase::LoadAsset(Path("engine://Shaders/Lit.hlsl"));
	ASSERT_NE(litShader, nullptr);

	MaterialAsset* material = LoadMaterial("Cone.mat", litShader->assetId,
										   "\t\"colors\": { \"baseColor\": [1, 1, 1, 1] },\n"
										   "\t\"floats\": { \"metallic\": 0, \"roughness\": 1, \"reflectance\": 0.5, \"normalScale\": 1, \"occlusionStrength\": 1 },\n"
										   "\t\"vec3s\": { \"emissive\": [0, 0, 0] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.quad->SetLocalScale(Vec3(3.0f, 3.0f, 1.0f));
	stage.sun->SetEnabled(false);

	Handle<SceneObject> spotObject = SceneSystem::CreateObject("Spot", stage.scene);
	spotObject->SetLocalPosition(Vec3(0.0f, 0.0f, 1.0f));
	spotObject->SetLocalRotation(Quat::AngleAxis(180.0f, Vec3::Up()));
	Handle<Light> spot = spotObject->AddComponent<Light>();
	spot->SetLightType(LightType::Spot);
	spot->SetColor(Color(1.0f, 1.0f, 1.0f, 1.0f));
	spot->SetIntensity(1.0f);
	spot->SetRange(3.0f);
	spot->SetSpotAngleDeg(60.0f);
	SceneSystem::EndFrame();

	const Vec3 edgePoint(std::tan(20.0f * Math::DEG_TO_RAD), 0.0f, 0.0f);

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec2I axis = ProjectToPixel(Vec3(0.0f, 0.0f, 0.0f), size);
	const Vec2I edge = ProjectToPixel(edgePoint, size);
	const f32 narrowAxis = TexelAt(pixels, size, axis.x, axis.y).x;
	const f32 narrowEdge = TexelAt(pixels, size, edge.x, edge.y).x;

	spot->SetInnerSpotAngleDeg(40.0f);
	SceneSystem::EndFrame();
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const f32 wideAxis = TexelAt(pixels, size, axis.x, axis.y).x;
	const f32 wideEdge = TexelAt(pixels, size, edge.x, edge.y).x;

	EXPECT_GT(narrowAxis, 8.0f);
	EXPECT_NEAR(wideAxis, narrowAxis, 1.5f);
	EXPECT_GT(wideEdge, narrowEdge + 8.0f);

	TearDownStage(stage);
}

TEST_F(MaterialReadbackFixture, OverSubscribedClusterIsReportedByFrame) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	Asset* litShader = AssetDatabase::LoadAsset(Path("engine://Shaders/Lit.hlsl"));
	ASSERT_NE(litShader, nullptr);

	MaterialAsset* material = LoadMaterial("Crowd.mat", litShader->assetId,
										   "\t\"colors\": { \"baseColor\": [1, 1, 1, 1] },\n"
										   "\t\"floats\": { \"metallic\": 0, \"roughness\": 1, \"reflectance\": 0.5, \"normalScale\": 1, \"occlusionStrength\": 1 },\n"
										   "\t\"vec3s\": { \"emissive\": [0, 0, 0] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));

	for (u32 i = 0; i < 8; ++i) {
		AddPointLight(stage, 0.5f, Color(0.1f, 0.1f, 0.1f, 1.0f), 1.0f);
	}
	SceneSystem::EndFrame();
	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	EXPECT_EQ(RenderSystem::GetStats().clusterLightsDropped, 0u);

	for (u32 i = 0; i < 32; ++i) {
		AddPointLight(stage, 0.5f, Color(0.1f, 0.1f, 0.1f, 1.0f), 1.0f);
	}
	SceneSystem::EndFrame();
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	EXPECT_GT(RenderSystem::GetStats().clusterLightsDropped, 0u);
	EXPECT_EQ(RenderSystem::GetStats().lightsDropped, 0u);

	TearDownStage(stage);
}

class PhysicalLightUnitsFixture : public MaterialReadbackFixture {
protected:
	str ProjectSettingsDocument() const override { return "[Graphics]\nphysicalLightUnits = true\n"; }
};

namespace {

	constexpr const char* ATTENUATION_ONLY_SHADER =
		"#define SHALLTEAR_CUSTOM_LIGHTING\n"
		"#pragma shalltear_template lit\n"
		"[[vk::binding(0, 1)]] cbuffer ShalltearMaterial { float4 baseColor; };\n"
		"float3 ShalltearLightingFunction(ShalltearSurfaceInput input, ShalltearSurface surface, float3 normalWS, ShalltearLightSample light) {\n"
		"\treturn light.color * light.attenuation;\n"
		"}\n"
		"void ShalltearSurfaceFunction(ShalltearSurfaceInput input, inout ShalltearSurface surface) {\n"
		"\tsurface.baseColor = baseColor.rgb;\n"
		"\tsurface.metallic = 0.0;\n"
		"}\n";

	constexpr f32 PHYSICAL_LUX = 20000.0f;

	f32 ExposedLux() { return PHYSICAL_LUX * Exposure(); }

} // namespace

TEST_F(PhysicalLightUnitsFixture, LuxValueOnDirectionalYieldsPredictedRadiance) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteProjectFile("Units.hlsl", ATTENUATION_ONLY_SHADER);
	Asset* unitsShader = AssetDatabase::LoadAsset(Path("project://Units.hlsl"));
	ASSERT_NE(unitsShader, nullptr);

	MaterialAsset* material = LoadMaterial("Units.mat", unitsShader->assetId,
										   "\t\"colors\": { \"baseColor\": [0, 0, 0, 1] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.sun->SetIntensity(PHYSICAL_LUX);
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));

	const Vec3 expected = ShadingTest::ExpectedTexel(Vec3(1.0f, 1.0f, 1.0f) * ExposedLux());
	EXPECT_NEAR(CenterTexel(pixels, size).x, expected.x, 1.5f);

	stage.sun->SetIntensity(PHYSICAL_LUX * 2.0f);
	SceneSystem::EndFrame();
	ASSERT_TRUE(RenderStage(stage, pixels, size));
	const Vec3 expectedDouble = ShadingTest::ExpectedTexel(Vec3(1.0f, 1.0f, 1.0f) * ExposedLux() * 2.0f);
	EXPECT_NEAR(CenterTexel(pixels, size).x, expectedDouble.x, 1.5f);

	TearDownStage(stage);
}

TEST_F(MaterialReadbackFixture, ArbitraryUnitsLeaveIntensityAsPlainMultiplier) {
	if (!RenderSystem::IsRenderingAvailable()) {
		GTEST_SKIP() << "no Vulkan device available";
	}
	if (!CompilerAvailable()) {
		GTEST_SKIP() << "dxcompiler.dll unavailable";
	}
	WriteProjectFile("Plain.hlsl", ATTENUATION_ONLY_SHADER);
	Asset* plainShader = AssetDatabase::LoadAsset(Path("project://Plain.hlsl"));
	ASSERT_NE(plainShader, nullptr);

	MaterialAsset* material = LoadMaterial("Plain.mat", plainShader->assetId,
										   "\t\"colors\": { \"baseColor\": [0, 0, 0, 1] }");
	ASSERT_NE(material, nullptr);

	ShadingStage stage;
	ASSERT_TRUE(BuildStage(material, stage));
	stage.sun->SetIntensity(ExposedLux());
	SceneSystem::EndFrame();

	Vector<u8> pixels{GetMemoryResource(MemTag::Graphics)};
	Vec2I size{};
	ASSERT_TRUE(RenderStage(stage, pixels, size));

	const Vec3 expected = ShadingTest::ExpectedTexel(Vec3(1.0f, 1.0f, 1.0f) * ExposedLux());
	EXPECT_NEAR(CenterTexel(pixels, size).x, expected.x, 1.5f);

	TearDownStage(stage);
}
