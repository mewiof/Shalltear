#include "TestsPCH.h"

#include "Graphics/ShadowAtlas.h"

#include "Shalltear/Editor/EditorViewportOps.h"

#include <array>
#include <cmath>
#include <set>

using namespace Shalltear;
using Shalltear::GraphicsInternal::BuildPointFaceView;
using Shalltear::GraphicsInternal::BuildSpotView;
using Shalltear::GraphicsInternal::MAX_PUNCTUAL_TILE;
using Shalltear::GraphicsInternal::MAX_SHADOW_MAPS;
using Shalltear::GraphicsInternal::MIN_PUNCTUAL_TILE;
using Shalltear::GraphicsInternal::POINT_SHADOW_FACES;
using Shalltear::GraphicsInternal::PointFaceOf;
using Shalltear::GraphicsInternal::PunctualTileSize;
using Shalltear::GraphicsInternal::PunctualView;
using Shalltear::GraphicsInternal::ShadowAtlasAllocator;
using Shalltear::GraphicsInternal::ShadowImportance;
using Shalltear::GraphicsInternal::ShadowRequest;
using Shalltear::GraphicsInternal::ShadowScreenCoverage;
using Shalltear::GraphicsInternal::SortShadowRequests;

namespace {

	constexpr f32 EDITOR_ASPECT = 16.0f / 9.0f;

	Mat4 EditorProjection() {
		return Mat4::PerspectiveReverseZ(EditorViewportOps::EDITOR_FOV_DEG, EDITOR_ASPECT,
										 EditorViewportOps::EDITOR_NEAR_PLANE, EditorViewportOps::EDITOR_FAR_PLANE);
	}

	bool IsEmpty(const RectI& rect) { return rect.size.x <= 0 || rect.size.y <= 0; }

	Vec3 Project(const Mat4& viewProjection, const Vec3& world) {
		const Vec4 clip = viewProjection * Vec4(world.x, world.y, world.z, 1.0f);
		const f32 w = std::abs(clip.w) > 1e-9f ? clip.w : 1.0f;
		return Vec3(clip.x / w, clip.y / w, clip.z / w);
	}

	bool InsideMap(const Vec3& ndc) {
		return ndc.x >= -1.0f && ndc.x <= 1.0f && ndc.y >= -1.0f && ndc.y <= 1.0f &&
			   ndc.z >= 0.0f && ndc.z <= 1.0f;
	}

	f32 RadialNdc(const Mat4& viewProjection, const Vec3& world) {
		const Vec3 ndc = Project(viewProjection, world);
		return std::sqrt(ndc.x * ndc.x + ndc.y * ndc.y);
	}

	ShadowRequest MakeRequest(u64 key, f32 importance, LightType type = LightType::Spot) {
		ShadowRequest request;
		request.stableKey = key;
		request.importance = importance;
		request.type = type;
		return request;
	}

} // namespace

TEST(ShadowAtlasTest, TileFitsAtlasAndSize) {
	ShadowAtlasAllocator allocator(2048);
	EXPECT_EQ(allocator.AtlasSize(), 2048u);

	for (u32 size : {2048u, 1024u, 512u, 256u, 64u, 1u}) {
		ShadowAtlasAllocator fresh(2048);
		const RectI tile = fresh.Allocate(size);
		ASSERT_FALSE(IsEmpty(tile)) << size << " tile";
		EXPECT_EQ(tile.size.x, static_cast<i32>(size));
		EXPECT_EQ(tile.size.y, static_cast<i32>(size));
		EXPECT_GE(tile.Left(), 0);
		EXPECT_GE(tile.Top(), 0);
		EXPECT_LE(tile.Right(), 2048);
		EXPECT_LE(tile.Bottom(), 2048);
		EXPECT_EQ(fresh.AllocatedCount(), 1u);
	}
}

TEST(ShadowAtlasTest, RequestsRoundDownAndCap) {
	ShadowAtlasAllocator allocator(1024);
	EXPECT_EQ(allocator.Allocate(700).size.x, 512);
	EXPECT_EQ(allocator.Allocate(1023).size.x, 512);

	ShadowAtlasAllocator small(1024);
	EXPECT_EQ(small.Allocate(4096).size.x, 1024);
}

TEST(ShadowAtlasTest, TilesNeverOverlap) {
	ShadowAtlasAllocator allocator(2048);
	Vector<RectI> taken{GetMemoryResource(MemTag::Graphics)};

	for (u32 size : {512u, 128u, 1024u, 256u, 128u, 512u, 64u, 256u, 1024u, 64u}) {
		const RectI tile = allocator.Allocate(size);
		if (IsEmpty(tile)) {
			continue;
		}
		for (const RectI& other : taken) {
			EXPECT_FALSE(tile.Intersects(other))
				<< "tile " << tile.Left() << "," << tile.Top() << " " << tile.size.x
				<< " overlaps " << other.Left() << "," << other.Top() << " " << other.size.x;
		}
		taken.push_back(tile);
	}
	EXPECT_EQ(taken.size(), allocator.AllocatedCount());
	EXPECT_GT(taken.size(), 5u);
}

TEST(ShadowAtlasTest, FullAtlasRefusesAndRefusalIsExact) {
	ShadowAtlasAllocator allocator(1024);
	for (u32 i = 0; i < 16; ++i) {
		EXPECT_FALSE(IsEmpty(allocator.Allocate(256))) << "tile " << i;
	}
	EXPECT_TRUE(IsEmpty(allocator.Allocate(256)));
	EXPECT_TRUE(IsEmpty(allocator.Allocate(1024)));
	EXPECT_EQ(allocator.AllocatedCount(), 16u);

	EXPECT_TRUE(IsEmpty(allocator.Allocate(1)));
}

TEST(ShadowAtlasTest, RefusalKeepsAtlasUsable) {
	ShadowAtlasAllocator allocator(1024);
	ASSERT_FALSE(IsEmpty(allocator.Allocate(512)));
	ASSERT_FALSE(IsEmpty(allocator.Allocate(512)));
	ASSERT_FALSE(IsEmpty(allocator.Allocate(512)));
	EXPECT_TRUE(IsEmpty(allocator.Allocate(1024)));
	EXPECT_FALSE(IsEmpty(allocator.Allocate(512)));
	EXPECT_TRUE(IsEmpty(allocator.Allocate(512)));
}

TEST(ShadowAtlasTest, TilingIsDeterministic) {
	auto run = [](Vector<RectI>& out) {
		ShadowAtlasAllocator allocator(2048);
		for (u32 size : {1024u, 256u, 512u, 256u, 128u, 512u, 256u}) {
			out.push_back(allocator.Allocate(size));
		}
	};
	Vector<RectI> first{GetMemoryResource(MemTag::Graphics)};
	Vector<RectI> second{GetMemoryResource(MemTag::Graphics)};
	run(first);
	run(second);
	ASSERT_EQ(first.size(), second.size());
	for (size_t i = 0; i < first.size(); ++i) {
		EXPECT_EQ(first[i], second[i]) << "request " << i;
	}

	ShadowAtlasAllocator reused(2048);
	const RectI before = reused.Allocate(1024);
	reused.Reset(2048);
	EXPECT_EQ(reused.Allocate(1024), before);
	EXPECT_EQ(reused.AllocatedCount(), 1u);
}

TEST(ShadowAtlasTest, MinimumTileBoundsPages) {
	ShadowAtlasAllocator allocator(2048);
	u32 granted = 0;
	while (!IsEmpty(allocator.Allocate(MIN_PUNCTUAL_TILE))) {
		++granted;
		ASSERT_LE(granted, MAX_SHADOW_MAPS + 1);
	}
	EXPECT_EQ(granted, MAX_SHADOW_MAPS);
}

TEST(ShadowAtlasTest, CoverageFollowsDistanceAndRange) {
	const Mat4 projection = EditorProjection();
	const Vec3 eye(-3.0f, 4.5f, -11.0f);
	const Vec3 light(2.7f, 1.3f, 6.1f);

	const f32 near = ShadowScreenCoverage(projection, eye, light, 8.0f);
	const f32 far = ShadowScreenCoverage(projection, eye, light * 4.0f, 8.0f);
	const f32 bigger = ShadowScreenCoverage(projection, eye, light, 24.0f);

	EXPECT_GT(near, 0.0f);
	EXPECT_LE(near, 1.0f);
	EXPECT_LT(far, near);
	EXPECT_GT(bigger, near);
	EXPECT_LE(bigger, 1.0f);

	EXPECT_FLOAT_EQ(ShadowScreenCoverage(projection, eye, eye, 10.0f), 1.0f);
	EXPECT_EQ(ShadowScreenCoverage(projection, eye, light, 0.0f), 0.0f);
}

TEST(ShadowAtlasTest, ImportanceIsCoverageTimesIntensity) {
	const f32 closeDim = ShadowImportance(0.6f, Vec3(0.5f, 0.5f, 0.5f));
	const f32 farBright = ShadowImportance(0.1f, Vec3(9.0f, 9.0f, 9.0f));
	EXPECT_GT(farBright, closeDim);

	EXPECT_FLOAT_EQ(ShadowImportance(0.5f, Vec3(4.0f, 0.0f, 0.0f)),
					ShadowImportance(0.5f, Vec3(4.0f, 4.0f, 4.0f)));
	EXPECT_EQ(ShadowImportance(0.0f, Vec3(100.0f, 100.0f, 100.0f)), 0.0f);
}

TEST(ShadowAtlasTest, TileSizeTracksCoverage) {
	EXPECT_EQ(PunctualTileSize(1.0f, 2048, 1), MAX_PUNCTUAL_TILE);
	EXPECT_EQ(PunctualTileSize(0.0f, 2048, 1), MIN_PUNCTUAL_TILE);
	EXPECT_EQ(PunctualTileSize(0.001f, 2048, 1), MIN_PUNCTUAL_TILE);

	u32 previous = 0;
	for (u32 step = 0; step <= 20; ++step) {
		const u32 size = PunctualTileSize(static_cast<f32>(step) / 20.0f, 2048, 1);
		EXPECT_GE(size, MIN_PUNCTUAL_TILE);
		EXPECT_LE(size, MAX_PUNCTUAL_TILE);
		EXPECT_EQ(size & (size - 1), 0u) << size;
		EXPECT_GE(size, previous);
		previous = size;
	}

	EXPECT_EQ(PunctualTileSize(1.0f, 1, 1), 1u);
	EXPECT_EQ(PunctualTileSize(0.0f, 1, 6), 1u);
}

TEST(ShadowAtlasTest, TileCeilingLeavesHalfAtlas) {
	for (u32 atlas : {512u, 1024u, 2048u, 4096u}) {
		for (u32 maps : {1u, POINT_SHADOW_FACES}) {
			const u32 tile = PunctualTileSize(1.0f, atlas, maps);
			const u64 claimed = static_cast<u64>(maps) * tile * tile;
			const u64 half = static_cast<u64>(atlas) * atlas / 2;
			EXPECT_LE(claimed, half) << atlas << "-atlas, " << maps << " maps at " << tile;

			ShadowAtlasAllocator allocator(atlas);
			for (u32 map = 0; map < maps; ++map) {
				EXPECT_FALSE(IsEmpty(allocator.Allocate(tile))) << "map " << map << " of " << maps << ' ' << atlas << ' ' << tile;
			}
		}
	}
	EXPECT_EQ(PunctualTileSize(1.0f, 2048, 1), 1024u);
	EXPECT_EQ(PunctualTileSize(1.0f, 2048, POINT_SHADOW_FACES), 512u);
}

TEST(ShadowAtlasTest, RequestsSortByImportance) {
	std::array<ShadowRequest, 5> requests{
		MakeRequest(40, 0.25f), MakeRequest(10, 0.90f), MakeRequest(30, 0.25f),
		MakeRequest(20, 0.10f), MakeRequest(50, 0.90f)};
	SortShadowRequests({requests.data(), requests.size()});

	EXPECT_EQ(requests[0].stableKey, 10u);
	EXPECT_EQ(requests[1].stableKey, 50u);
	EXPECT_EQ(requests[2].stableKey, 30u);
	EXPECT_EQ(requests[3].stableKey, 40u);
	EXPECT_EQ(requests[4].stableKey, 20u);

	std::array<ShadowRequest, 5> shuffled{
		MakeRequest(50, 0.90f), MakeRequest(30, 0.25f), MakeRequest(20, 0.10f),
		MakeRequest(10, 0.90f), MakeRequest(40, 0.25f)};
	SortShadowRequests({shuffled.data(), shuffled.size()});
	for (size_t i = 0; i < requests.size(); ++i) {
		EXPECT_EQ(shuffled[i].stableKey, requests[i].stableKey) << "position " << i;
	}
}

TEST(ShadowAtlasTest, PointAsks6SpotAsks1) {
	EXPECT_EQ(MakeRequest(1, 1.0f, LightType::Spot).MapCount(), 1u);
	EXPECT_EQ(MakeRequest(1, 1.0f, LightType::Point).MapCount(), POINT_SHADOW_FACES);
}

TEST(ShadowAtlasTest, SpotMapEnclosesCone) {
	const Vec3 position(2.3f, 6.1f, -4.7f);
	const Vec3 direction = Vec3(0.41f, -0.83f, 0.37f).Normalized();
	constexpr f32 OUTER = 50.0f;
	constexpr f32 RANGE = 30.0f;
	const PunctualView view = BuildSpotView(position, direction, OUTER, RANGE, 512);

	const Vec3 onAxis = position + direction * (RANGE * 0.5f);
	const Vec3 center = Project(view.viewProjection, onAxis);
	EXPECT_TRUE(InsideMap(center));
	EXPECT_NEAR(center.x, 0.0f, 1e-4f);
	EXPECT_NEAR(center.y, 0.0f, 1e-4f);

	const Vec3 sideways = Vec3::Cross(direction, Vec3(0.19f, 0.57f, -0.79f).Normalized()).Normalized();
	const f32 reach = RANGE * 0.5f;
	const f32 insideRadius = reach * std::tan(OUTER * 0.5f * Math::DEG_TO_RAD) * 0.85f;
	const f32 outsideRadius = reach * std::tan(OUTER * 0.5f * Math::DEG_TO_RAD) * 1.6f;
	EXPECT_TRUE(InsideMap(Project(view.viewProjection, onAxis + sideways * insideRadius)));
	EXPECT_FALSE(InsideMap(Project(view.viewProjection, onAxis + sideways * outsideRadius)));

	EXPECT_FALSE(InsideMap(Project(view.viewProjection, position - direction * 5.0f)));
	EXPECT_FALSE(InsideMap(Project(view.viewProjection, position + direction * (RANGE * 1.4f))));
}

TEST(ShadowAtlasTest, APunctualMapIsReverseZ) {
	const Vec3 position(-1.7f, 3.9f, 2.2f);
	const Vec3 direction = Vec3(0.6f, -0.5f, 0.62f).Normalized();
	const PunctualView spot = BuildSpotView(position, direction, 60.0f, 40.0f, 512);
	const f32 near = Project(spot.viewProjection, position + direction * 1.0f).z;
	const f32 far = Project(spot.viewProjection, position + direction * 39.0f).z;
	EXPECT_GT(near, far);
	EXPECT_LE(near, 1.0f);
	EXPECT_GE(far, 0.0f);

	const PunctualView face = BuildPointFaceView(position, 4, 40.0f, 512, 3);
	const f32 faceNear = Project(face.viewProjection, position + Vec3(0.0f, 0.0f, 1.0f)).z;
	const f32 faceFar = Project(face.viewProjection, position + Vec3(0.0f, 0.0f, 39.0f)).z;
	EXPECT_GT(faceNear, faceFar);
}

TEST(ShadowAtlasTest, EveryDirectionProjectsInsideFaceRuleSelects) {
	const Vec3 position(4.2f, -1.9f, 3.3f);
	constexpr f32 RANGE = 50.0f;
	std::array<PunctualView, POINT_SHADOW_FACES> faces{};
	for (u32 face = 0; face < POINT_SHADOW_FACES; ++face) {
		faces[face] = BuildPointFaceView(position, face, RANGE, 512, 3);
	}

	u32 checked = 0;
	std::set<u32> seen;
	for (u32 i = 0; i < 11; ++i) {
		for (u32 j = 0; j < 13; ++j) {
			const f32 theta = (static_cast<f32>(i) + 0.37f) / 11.0f * Math::PI;
			const f32 phi = (static_cast<f32>(j) + 0.19f) / 13.0f * 2.0f * Math::PI;
			const Vec3 unit(std::sin(theta) * std::cos(phi), std::cos(theta), std::sin(theta) * std::sin(phi));
			const Vec3 world = position + unit * (RANGE * 0.4f);

			const u32 face = PointFaceOf(unit);
			ASSERT_LT(face, POINT_SHADOW_FACES);
			seen.insert(face);
			const Vec3 ndc = Project(faces[face].viewProjection, world);
			EXPECT_TRUE(InsideMap(ndc)) << "direction " << unit.x << "," << unit.y << "," << unit.z << " selected face " << face << ' ' << ndc.x << "," << ndc.y << "," << ndc.z;
			++checked;
		}
	}
	EXPECT_EQ(checked, 143u);
	EXPECT_EQ(seen.size(), POINT_SHADOW_FACES);
}

TEST(ShadowAtlasTest, FaceRuleCoversDiagonals) {
	const std::array<Vec3, 10> diagonals{
		Vec3(1.0f, 1.0f, 0.0f), Vec3(1.0f, 0.0f, 1.0f), Vec3(0.0f, 1.0f, 1.0f),
		Vec3(1.0f, 1.0f, 1.0f), Vec3(-1.0f, 1.0f, 1.0f), Vec3(1.0f, -1.0f, 1.0f),
		Vec3(1.0f, 1.0f, -1.0f), Vec3(-1.0f, -1.0f, -1.0f), Vec3(-1.0f, 1.0f, -1.0f),
		Vec3(0.0f, 0.0f, 0.0f)};
	for (const Vec3& diagonal : diagonals) {
		EXPECT_LT(PointFaceOf(diagonal), POINT_SHADOW_FACES) << diagonal.x << "," << diagonal.y << "," << diagonal.z;
	}
	EXPECT_EQ(PointFaceOf(Vec3(1.0f, 0.0f, 0.0f)), 0u);
	EXPECT_EQ(PointFaceOf(Vec3(-1.0f, 0.0f, 0.0f)), 1u);
	EXPECT_EQ(PointFaceOf(Vec3(0.0f, 1.0f, 0.0f)), 2u);
	EXPECT_EQ(PointFaceOf(Vec3(0.0f, -1.0f, 0.0f)), 3u);
	EXPECT_EQ(PointFaceOf(Vec3(0.0f, 0.0f, 1.0f)), 4u);
	EXPECT_EQ(PointFaceOf(Vec3(0.0f, 0.0f, -1.0f)), 5u);
}

TEST(ShadowAtlasTest, PointFaceHasFilterBorder) {
	const Vec3 position(0.5f, 2.5f, -3.5f);
	constexpr u32 RESOLUTION = 512;
	const PunctualView wide = BuildPointFaceView(position, 0, 40.0f, RESOLUTION, 5);
	const PunctualView narrow = BuildPointFaceView(position, 0, 40.0f, RESOLUTION, 1);

	const Vec3 corner = position + Vec3(10.0f, 10.0f, 0.0f);
	EXPECT_TRUE(InsideMap(Project(wide.viewProjection, corner)));
	EXPECT_TRUE(InsideMap(Project(narrow.viewProjection, corner)));

	const f32 wideEdge = RadialNdc(wide.viewProjection, corner);
	const f32 narrowEdge = RadialNdc(narrow.viewProjection, corner);
	EXPECT_LT(wideEdge, narrowEdge);
	EXPECT_NEAR(narrowEdge, 1.0f, 0.02f);

	for (u32 width : {1u, 3u, 5u, 7u}) {
		const PunctualView face = BuildPointFaceView(position, 0, 40.0f, RESOLUTION, width);
		const f32 footprint = (static_cast<f32>((width + 1) / 2) + 0.5f) * 2.0f / static_cast<f32>(RESOLUTION);
		EXPECT_LE(RadialNdc(face.viewProjection, corner) + footprint, 1.0f) << "width " << width;
	}

	const PunctualView big = BuildPointFaceView(position, 0, 40.0f, 1024, 5);
	EXPECT_GT(RadialNdc(big.viewProjection, corner), wideEdge);
}

TEST(ShadowAtlasTest, TexelRateFollowsResolutionAndCone) {
	const Vec3 position(1.0f, 2.0f, 3.0f);
	const Vec3 direction = Vec3(0.3f, -0.9f, 0.31f).Normalized();

	const PunctualView small = BuildSpotView(position, direction, 60.0f, 20.0f, 256);
	const PunctualView large = BuildSpotView(position, direction, 60.0f, 20.0f, 1024);
	EXPECT_NEAR(small.worldTexelSizePerUnit, large.worldTexelSizePerUnit * 4.0f, 1e-6f);

	const PunctualView wide = BuildSpotView(position, direction, 120.0f, 20.0f, 256);
	EXPECT_GT(wide.worldTexelSizePerUnit, small.worldTexelSizePerUnit);

	const f32 texelAtFive = small.worldTexelSizePerUnit * 5.0f;
	const Vec3 near = position + direction * 5.0f;
	const Vec3 sideways = Vec3::Cross(direction, Vec3(0.11f, 0.63f, -0.77f).Normalized()).Normalized();
	ASSERT_NEAR(RadialNdc(small.viewProjection, near), 0.0f, 1e-4f);
	EXPECT_NEAR(RadialNdc(small.viewProjection, near + sideways * texelAtFive), 2.0f / 256.0f, 1e-4f);
	const Vec3 farPoint = position + direction * 10.0f;
	EXPECT_NEAR(RadialNdc(small.viewProjection, farPoint + sideways * (small.worldTexelSizePerUnit * 10.0f)),
				2.0f / 256.0f, 1e-4f);
}

TEST(ShadowAtlasTest, DegenerateRangesAndAnglesProduceUsableMap) {
	const Vec3 position(0.7f, 1.3f, -2.1f);
	const Vec3 direction = Vec3(0.5f, -0.6f, 0.62f).Normalized();

	for (f32 range : {0.0f, 1e-5f, 0.05f}) {
		const PunctualView view = BuildSpotView(position, direction, 45.0f, range, 256);
		EXPECT_TRUE(std::isfinite(view.viewProjection(0, 0)));
		EXPECT_TRUE(std::isfinite(view.viewProjection(2, 3)));
		EXPECT_GT(view.worldTexelSizePerUnit, 0.0f);
	}
	for (f32 angle : {0.0f, 0.5f, 180.0f, 400.0f}) {
		const PunctualView view = BuildSpotView(position, direction, angle, 10.0f, 256);
		EXPECT_TRUE(std::isfinite(view.viewProjection(1, 1)));
		EXPECT_GT(view.worldTexelSizePerUnit, 0.0f);
	}
	EXPECT_TRUE(std::isfinite(BuildPointFaceView(position, 2, 10.0f, 0, 3).worldTexelSizePerUnit));
	EXPECT_TRUE(std::isfinite(BuildPointFaceView(position, 99, 10.0f, 256, 3).viewProjection(0, 0)));
}
