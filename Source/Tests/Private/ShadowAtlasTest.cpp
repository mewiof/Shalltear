#include "TestsPCH.h"

#include "Graphics/ShadowAtlas.h"

#include "Shalltear/Editor/EditorViewportOps.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <set>

using namespace Shalltear;
using Shalltear::GraphicsInternal::BuildPointFaceView;
using Shalltear::GraphicsInternal::BuildSpotView;
using Shalltear::GraphicsInternal::FavorKeptShadows;
using Shalltear::GraphicsInternal::FitShadowRequests;
using Shalltear::GraphicsInternal::MAX_OBJECT_SHADOWS;
using Shalltear::GraphicsInternal::MAX_PUNCTUAL_TILE;
using Shalltear::GraphicsInternal::MAX_SHADOW_MAPS;
using Shalltear::GraphicsInternal::MIN_PUNCTUAL_TILE;
using Shalltear::GraphicsInternal::ObjectShadowTileSize;
using Shalltear::GraphicsInternal::POINT_SHADOW_FACES;
using Shalltear::GraphicsInternal::PointFaceOf;
using Shalltear::GraphicsInternal::PunctualTileSize;
using Shalltear::GraphicsInternal::PunctualView;
using Shalltear::GraphicsInternal::SHADOW_KEEP_MARGIN;
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

namespace {

	ShadowRequest Sized(u64 key, f32 importance, LightType type, u32 tile) {
		ShadowRequest request = MakeRequest(key, importance, type);
		request.tileSize = tile;
		return request;
	}

	bool PacksInOrder(std::span<const ShadowRequest> kept, u32 atlas) {
		ShadowAtlasAllocator allocator(atlas);
		Vector<RectI> taken{GetMemoryResource(MemTag::Graphics)};
		for (const ShadowRequest& request : kept) {
			for (u32 map = 0; map < request.MapCount(); ++map) {
				const RectI tile = allocator.Allocate(request.tileSize);
				if (IsEmpty(tile) || tile.size.x != static_cast<i32>(request.tileSize)) {
					return false;
				}
				for (const RectI& other : taken) {
					if (tile.Intersects(other)) {
						return false;
					}
				}
				taken.push_back(tile);
			}
		}
		return true;
	}

	std::array<u64, 2> KeptPair(f32 x, f32 y, std::span<const u64> keptLastFrame) {
		std::array<ShadowRequest, 3> requests{
			Sized(1, 4.0f, LightType::Point, 256), Sized(2, x, LightType::Point, 256), Sized(3, y, LightType::Point, 256)};
		FavorKeptShadows({requests.data(), requests.size()}, keptLastFrame);
		const u32 kept = FitShadowRequests({requests.data(), requests.size()}, 1024, MAX_SHADOW_MAPS);
		EXPECT_EQ(kept, 2u);
		std::array<u64, 2> keys{requests[0].stableKey, requests[1].stableKey};
		std::sort(keys.begin(), keys.end());
		return keys;
	}

} // namespace

TEST(ShadowAtlasTest, CrowdedAtlasShrinksTilesInsteadOfDropping) {
	const u32 ceiling = PunctualTileSize(1.0f, 4096, POINT_SHADOW_FACES);
	ASSERT_EQ(ceiling, 1024u);
	std::array<ShadowRequest, 3> requests{
		Sized(1, 1.5f, LightType::Point, ceiling), Sized(2, 1.4f, LightType::Point, ceiling),
		Sized(3, 1.2f, LightType::Point, ceiling)};
	EXPECT_EQ(FitShadowRequests({requests.data(), requests.size()}, 4096, MAX_SHADOW_MAPS), 3u);
	for (const ShadowRequest& request : requests) {
		EXPECT_GE(request.tileSize, MIN_PUNCTUAL_TILE);
		EXPECT_LT(request.tileSize, ceiling);
		EXPECT_EQ(request.tileSize, requests[0].tileSize);
	}
	EXPECT_TRUE(PacksInOrder({requests.data(), requests.size()}, 4096));
}

TEST(ShadowAtlasTest, FittedTilesIgnoreImportance) {
	auto fit = [](std::array<f32, 5> importance) {
		std::array<ShadowRequest, 5> requests{
			Sized(11, importance[0], LightType::Point, 1024), Sized(12, importance[1], LightType::Point, 1024),
			Sized(13, importance[2], LightType::Point, 1024), Sized(14, importance[3], LightType::Point, 512),
			Sized(15, importance[4], LightType::Spot, 1024)};
		EXPECT_EQ(FitShadowRequests({requests.data(), requests.size()}, 4096, MAX_SHADOW_MAPS), 5u);
		return requests;
	};
	const std::array<ShadowRequest, 5> calm = fit({1.0f, 0.9f, 0.8f, 0.7f, 0.6f});
	const std::array<ShadowRequest, 5> flicker = fit({0.6f, 1.3f, 0.7f, 1.1f, 0.9f});
	EXPECT_LT(calm[0].tileSize, 1024u);
	for (size_t i = 0; i < calm.size(); ++i) {
		EXPECT_EQ(calm[i].stableKey, flicker[i].stableKey);
		EXPECT_EQ(calm[i].tileSize, flicker[i].tileSize);
	}
}

TEST(ShadowAtlasTest, MapLimitDropsLeastImportant) {
	Vector<ShadowRequest> requests{GetMemoryResource(MemTag::Graphics)};
	for (u64 key = 1; key <= 11; ++key) {
		requests.push_back(Sized(key, 1.0f + static_cast<f32>(key), LightType::Point, 256));
	}
	requests.push_back(Sized(99, 0.5f, LightType::Spot, 256));
	const u32 kept = FitShadowRequests({requests.data(), requests.size()}, 4096, MAX_SHADOW_MAPS);
	ASSERT_EQ(kept, 11u);
	EXPECT_EQ(requests.back().stableKey, 1u);
	EXPECT_EQ(requests.back().tileSize, 0u);
	EXPECT_EQ(requests[kept - 1].stableKey, 99u);
	EXPECT_TRUE(PacksInOrder({requests.data(), kept}, 4096));
}

TEST(ShadowAtlasTest, FloorAreaDropsLeastImportant) {
	std::array<ShadowRequest, 4> requests{
		Sized(1, 3.0f, LightType::Point, 256), Sized(2, 2.0f, LightType::Point, 256),
		Sized(3, 1.0f, LightType::Point, 256), Sized(4, 0.5f, LightType::Spot, 256)};
	ASSERT_EQ(FitShadowRequests({requests.data(), requests.size()}, 1024, MAX_SHADOW_MAPS), 3u);
	EXPECT_EQ(requests[3].stableKey, 3u);
	EXPECT_EQ(requests[3].tileSize, 0u);
	EXPECT_TRUE(PacksInOrder({requests.data(), 3}, 1024));
}

TEST(ShadowAtlasTest, FittedRequestsAlwaysPack) {
	u32 seed = 12345u;
	auto next = [&seed]() {
		seed = seed * 1664525u + 1013904223u;
		return seed >> 8;
	};
	u32 shrunk = 0;
	u32 dropped = 0;
	for (u32 trial = 0; trial < 400; ++trial) {
		const u32 atlas = 512u << (next() % 4);
		const u32 freeMaps = MAX_SHADOW_MAPS - next() % 5;
		Vector<ShadowRequest> requests{GetMemoryResource(MemTag::Graphics)};
		Vector<u32> wanted{GetMemoryResource(MemTag::Graphics)};
		const u32 count = 1 + next() % 24;
		for (u32 i = 0; i < count; ++i) {
			const LightType type = next() % 3 == 0 ? LightType::Point : LightType::Spot;
			ShadowRequest request = MakeRequest(i, static_cast<f32>(next() % 1000) / 100.0f, type);
			request.tileSize = PunctualTileSize(static_cast<f32>(next() % 1001) / 1000.0f, atlas, request.MapCount());
			wanted.push_back(request.tileSize);
			requests.push_back(request);
		}
		const u32 kept = FitShadowRequests({requests.data(), requests.size()}, atlas, freeMaps);
		u32 maps = 0;
		u64 area = 0;
		for (u32 i = 0; i < kept; ++i) {
			const ShadowRequest& request = requests[i];
			ASSERT_GT(request.tileSize, 0u);
			EXPECT_LE(request.tileSize, wanted[request.stableKey]);
			shrunk += request.tileSize < wanted[request.stableKey] ? 1u : 0u;
			maps += request.MapCount();
			area += static_cast<u64>(request.MapCount()) * request.tileSize * request.tileSize;
		}
		for (size_t i = kept; i < requests.size(); ++i) {
			EXPECT_EQ(requests[i].tileSize, 0u);
			++dropped;
		}
		EXPECT_LE(maps, freeMaps);
		EXPECT_LE(area, static_cast<u64>(atlas) * atlas);
		EXPECT_TRUE(PacksInOrder({requests.data(), kept}, atlas)) << trial;
	}
	EXPECT_GT(shrunk, 0u);
	EXPECT_GT(dropped, 0u);
}

TEST(ShadowAtlasTest, ObjectTilesFillAtMostHalfAtlas) {
	EXPECT_EQ(ObjectShadowTileSize(2048, 1, 1024), 1024u);
	EXPECT_EQ(ObjectShadowTileSize(2048, 2, 1024), 1024u);
	EXPECT_EQ(ObjectShadowTileSize(2048, 4, 1024), 512u);
	EXPECT_EQ(ObjectShadowTileSize(2048, 2, 4096), 1024u);
	EXPECT_EQ(ObjectShadowTileSize(1024, 16, 1024), 128u);
	EXPECT_EQ(ObjectShadowTileSize(2048, 1, 300), 256u);
	EXPECT_EQ(ObjectShadowTileSize(2048, 0, 1024), 0u);
}

TEST(ShadowAtlasTest, ReservedAreaShrinksPunctualTiles) {
	auto fit = [](u64 reserved) {
		std::array<ShadowRequest, 4> requests{Sized(1, 1.0f, LightType::Spot, 1024), Sized(2, 0.9f, LightType::Spot, 1024),
											  Sized(3, 0.8f, LightType::Spot, 1024), Sized(4, 0.7f, LightType::Spot, 1024)};
		const u32 kept = FitShadowRequests({requests.data(), requests.size()}, 2048, MAX_SHADOW_MAPS, reserved);
		return kept == 4 ? requests[3].tileSize : 0u;
	};
	EXPECT_EQ(fit(0), 1024u);
	EXPECT_EQ(fit(2ull * 1024 * 1024), 512u);
	EXPECT_EQ(fit(~0ull), 0u);
}

TEST(ShadowAtlasTest, ObjectTilesPackBetweenFittedRequests) {
	u32 seed = 777u;
	auto next = [&seed]() {
		seed = seed * 1664525u + 1013904223u;
		return seed >> 8;
	};
	for (u32 trial = 0; trial < 400; ++trial) {
		const u32 atlas = 512u << (next() % 4);
		const u32 budget = 1 + next() % MAX_OBJECT_SHADOWS;
		const u32 objects = 1 + next() % budget;
		const u32 tile = ObjectShadowTileSize(atlas, budget, 64u << (next() % 7));
		ASSERT_GT(tile, 0u);
		Vector<ShadowRequest> requests{GetMemoryResource(MemTag::Graphics)};
		const u32 count = next() % 24;
		for (u32 i = 0; i < count; ++i) {
			const LightType type = next() % 3 == 0 ? LightType::Point : LightType::Spot;
			ShadowRequest request = MakeRequest(i, static_cast<f32>(next() % 1000) / 100.0f, type);
			request.tileSize = PunctualTileSize(static_cast<f32>(next() % 1001) / 1000.0f, atlas, request.MapCount());
			requests.push_back(request);
		}
		const u32 kept = FitShadowRequests({requests.data(), requests.size()}, atlas, MAX_SHADOW_MAPS - objects,
										   static_cast<u64>(objects) * tile * tile);
		ShadowAtlasAllocator allocator(atlas);
		bool placed = false;
		auto place = [&] {
			placed = true;
			for (u32 object = 0; object < objects; ++object) {
				EXPECT_EQ(allocator.Allocate(tile).size.x, static_cast<i32>(tile)) << trial;
			}
		};
		for (u32 i = 0; i < kept; ++i) {
			if (!placed && requests[i].tileSize < tile) {
				place();
			}
			for (u32 map = 0; map < requests[i].MapCount(); ++map) {
				EXPECT_EQ(allocator.Allocate(requests[i].tileSize).size.x, static_cast<i32>(requests[i].tileSize)) << trial;
			}
		}
		if (!placed) {
			place();
		}
	}
}

TEST(ShadowAtlasTest, NearPlaneAdmitsCloseCaster) {
	const Vec3 light(1.0f, 2.0f, 3.0f);
	const Vec3 down(0.0f, -1.0f, 0.0f);
	const Vec3 close = light + down * 0.05f;
	EXPECT_FALSE(InsideMap(Project(BuildSpotView(light, down, 60.0f, 8.0f, 512).viewProjection, close)));
	EXPECT_TRUE(InsideMap(Project(BuildSpotView(light, down, 60.0f, 8.0f, 512, 0.02f).viewProjection, close)));

	const u32 face = PointFaceOf(down);
	EXPECT_FALSE(InsideMap(Project(BuildPointFaceView(light, face, 8.0f, 512, 9).viewProjection, close)));
	EXPECT_TRUE(InsideMap(Project(BuildPointFaceView(light, face, 8.0f, 512, 9, 0.02f).viewProjection, close)));

	const Vec3 touching = light + down * (Light::MIN_SHADOW_NEAR_PLANE * 0.5f);
	EXPECT_FALSE(InsideMap(Project(BuildSpotView(light, down, 60.0f, 8.0f, 512, 0.0f).viewProjection, touching)));
	const Vec3 far = light + down * 7.0f;
	EXPECT_TRUE(InsideMap(Project(BuildSpotView(light, down, 60.0f, 8.0f, 512, 100.0f).viewProjection, far)));
}

TEST(ShadowAtlasTest, KeptShadowSurvivesSmallImportanceSwap) {
	std::array<u64, 2> kept = KeptPair(1.1f, 1.0f, {});
	ASSERT_EQ(kept, (std::array<u64, 2>{1, 2}));
	for (u32 frame = 0; frame < 6; ++frame) {
		const bool odd = frame % 2 == 1;
		kept = KeptPair(odd ? 1.1f : 1.0f, odd ? 1.0f : 1.1f, {kept.data(), kept.size()});
		EXPECT_EQ(kept, (std::array<u64, 2>{1, 2}));
	}
	EXPECT_EQ(KeptPair(1.0f, 1.1f, {}), (std::array<u64, 2>{1, 3}));

	kept = KeptPair(1.0f, 1.1f * SHADOW_KEEP_MARGIN, {kept.data(), kept.size()});
	EXPECT_EQ(kept, (std::array<u64, 2>{1, 3}));
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
