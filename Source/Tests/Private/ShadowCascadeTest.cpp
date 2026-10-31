#include "TestsPCH.h"

#include "Graphics/ShadowCascades.h"

#include "Shalltear/Core/Graphics/GraphicsProfile.h"

#include "Shalltear/Editor/EditorViewportOps.h"

#include <array>
#include <cmath>
#include <limits>

using namespace Shalltear;
using Shalltear::MAX_SHADOW_CASCADES;
using Shalltear::GraphicsInternal::BuildCascadeViews;
using Shalltear::GraphicsInternal::CascadeSplits;
using Shalltear::GraphicsInternal::CascadeView;
using Shalltear::GraphicsInternal::SelectCascade;
using Shalltear::GraphicsInternal::SolveCascadeSplits;

namespace {

	constexpr f32 EDITOR_ASPECT = 16.0f / 9.0f;
	constexpr f32 CASTER_DISTANCE = 50.0f;

	f32 DefaultShadowDistance() {
		const GraphicsProfile profile;
		return profile.shadowDistance;
	}

	f32 DefaultFirstCascade() {
		const GraphicsProfile profile;
		return profile.shadowFirstCascadeDistance;
	}

	CascadeSplits DefaultSplits(f32 nearDepth, f32 shadowDistance, u32 count = 4) {
		return SolveCascadeSplits(nearDepth, shadowDistance, count, DefaultFirstCascade());
	}

	Mat4 EditorProjection() {
		return Mat4::PerspectiveReverseZ(EditorViewportOps::EDITOR_FOV_DEG, EDITOR_ASPECT,
										 EditorViewportOps::EDITOR_NEAR_PLANE, EditorViewportOps::EDITOR_FAR_PLANE);
	}

	Mat4 EditorView(f32 yawDegrees, const Vec3& position = Vec3::Zero()) {
		return Mat4::TRS(position, Quat::FromEuler(0.0f, yawDegrees, 0.0f), Vec3::One()).Inverse();
	}

	std::array<Vec4, 8> SliceCorners(const Mat4& projection, f32 sliceNear, f32 sliceFar) {
		const f32 tanHalfX = 1.0f / std::abs(projection(0, 0));
		const f32 tanHalfY = 1.0f / std::abs(projection(1, 1));
		std::array<Vec4, 8> corners{};
		for (u32 i = 0; i < 8; ++i) {
			const f32 depth = (i & 4) ? sliceFar : sliceNear;
			corners[i] = Vec4(((i & 1) ? 1.0f : -1.0f) * depth * tanHalfX,
							  ((i & 2) ? 1.0f : -1.0f) * depth * tanHalfY, depth, 1.0f);
		}
		return corners;
	}

} // namespace

TEST(ShadowCascadeTest, SplitsSpanRangeAndAscend) {
	for (u32 count = 1; count <= MAX_SHADOW_CASCADES; ++count) {
		const CascadeSplits splits = DefaultSplits(0.05f, DefaultShadowDistance(), count);
		ASSERT_EQ(splits.count, count);
		EXPECT_FLOAT_EQ(splits.depths[0], 0.05f);
		EXPECT_FLOAT_EQ(splits.depths[count], DefaultShadowDistance());
		for (u32 i = 0; i < count; ++i) {
			EXPECT_LT(splits.depths[i], splits.depths[i + 1]) << "cascade " << i << " of " << count;
		}
	}
}

TEST(ShadowCascadeTest, Cascade0EndsAtAuthoredDistance) {
	const f32 distance = DefaultShadowDistance();
	const f32 first = DefaultFirstCascade();
	for (u32 count = 2; count <= MAX_SHADOW_CASCADES; ++count) {
		const CascadeSplits splits = DefaultSplits(0.1f, distance, count);
		ASSERT_EQ(splits.count, count);
		EXPECT_NEAR(splits.depths[1], first, 1e-4f) << count << " cascades";
		const f32 ratio = std::pow(distance / first, 1.0f / static_cast<f32>(count - 1));
		for (u32 i = 1; i < count; ++i) {
			EXPECT_NEAR(splits.depths[i + 1] / splits.depths[i], ratio, 1e-4f) << "cascade " << i << " of " << count;
		}
	}
	EXPECT_FLOAT_EQ(first, 4.0f);
}

TEST(ShadowCascadeTest, TheNearPlaneMovesNoSplit) {
	const CascadeSplits editor = DefaultSplits(EditorViewportOps::EDITOR_NEAR_PLANE, DefaultShadowDistance());
	const CascadeSplits camera = DefaultSplits(0.1f, DefaultShadowDistance());
	const CascadeSplits close = DefaultSplits(0.01f, DefaultShadowDistance());
	for (u32 i = 1; i <= camera.count; ++i) {
		EXPECT_FLOAT_EQ(editor.depths[i], camera.depths[i]) << "split " << i;
		EXPECT_FLOAT_EQ(close.depths[i], camera.depths[i]) << "split " << i;
	}
}

TEST(ShadowCascadeTest, BadFirstDistanceStillAscends) {
	const f32 distance = DefaultShadowDistance();
	const f32 typed[] = {0.0f, -3.0f, 0.1f, distance, distance * 4.0f, std::numeric_limits<f32>::quiet_NaN(),
						 std::numeric_limits<f32>::infinity()};
	for (f32 first : typed) {
		const CascadeSplits splits = SolveCascadeSplits(0.1f, distance, 4, first);
		ASSERT_EQ(splits.count, 4u);
		EXPECT_FLOAT_EQ(splits.depths[0], 0.1f);
		EXPECT_FLOAT_EQ(splits.depths[4], distance);
		for (u32 i = 0; i < splits.count; ++i) {
			EXPECT_LT(splits.depths[i], splits.depths[i + 1]) << "first " << first << ", cascade " << i;
		}
	}
	EXPECT_GT(SolveCascadeSplits(0.1f, distance, 4, distance * 4.0f).depths[1], distance * 0.95f);
}

TEST(ShadowCascadeTest, DegenerateInputsClamped) {
	EXPECT_EQ(DefaultSplits(0.05f, DefaultShadowDistance(), 0).count, 1u);
	EXPECT_EQ(DefaultSplits(0.05f, DefaultShadowDistance(), 99).count, MAX_SHADOW_CASCADES);

	const CascadeSplits behind = DefaultSplits(10.0f, 1.0f);
	for (u32 i = 0; i < behind.count; ++i) {
		EXPECT_LT(behind.depths[i], behind.depths[i + 1]);
	}
}

TEST(ShadowCascadeTest, ClipRangeSplitWastesMaps) {
	const CascadeSplits overClipRange =
		DefaultSplits(EditorViewportOps::EDITOR_NEAR_PLANE, EditorViewportOps::EDITOR_FAR_PLANE, 4);
	const CascadeSplits overShadowDistance =
		DefaultSplits(EditorViewportOps::EDITOR_NEAR_PLANE, DefaultShadowDistance(), 4);

	const f32 clipLast = overClipRange.depths[4] - overClipRange.depths[3];
	EXPECT_GT(clipLast / EditorViewportOps::EDITOR_FAR_PLANE, 0.5f);

	EXPECT_FLOAT_EQ(overClipRange.depths[1], overShadowDistance.depths[1]);
	for (u32 i = 2; i <= 3; ++i) {
		EXPECT_GT(overClipRange.depths[i] / overShadowDistance.depths[i], 4.0f) << "split " << i;
	}

	for (u32 i = 0; i < 4; ++i) {
		const f32 span = overShadowDistance.depths[i + 1] - overShadowDistance.depths[i];
		EXPECT_GT(span / DefaultShadowDistance(), 0.02f) << "cascade " << i;
	}
	EXPECT_LT(overShadowDistance.depths[4] - overShadowDistance.depths[3], DefaultShadowDistance() * 0.75f);
}

TEST(ShadowCascadeTest, SelectCascadeMatchesSplits) {
	const CascadeSplits splits = DefaultSplits(EditorViewportOps::EDITOR_NEAR_PLANE, DefaultShadowDistance(), 4);
	for (u32 i = 0; i < splits.count; ++i) {
		const f32 boundary = splits.depths[i + 1];
		EXPECT_EQ(SelectCascade(splits, boundary * 0.999f), i) << i;
		if (i + 1 < splits.count) {
			EXPECT_EQ(SelectCascade(splits, boundary * 1.001f), i + 1) << i;
		}
	}
	EXPECT_EQ(SelectCascade(splits, 0.0f), 0u);
	EXPECT_EQ(SelectCascade(splits, DefaultShadowDistance() * 1.01f), splits.count);
}

TEST(ShadowCascadeTest, CascadeMapsEncloseSlices) {
	const Mat4 projection = EditorProjection();
	const Mat4 view = EditorView(35.0f, Vec3(4.0f, 3.0f, -6.0f));
	const CascadeSplits splits = DefaultSplits(EditorViewportOps::EDITOR_NEAR_PLANE, DefaultShadowDistance(), 4);
	const Vec3 lightDirection = Vec3(0.4f, -1.0f, 0.3f).Normalized();

	std::array<CascadeView, MAX_SHADOW_CASCADES> views{};
	ASSERT_EQ(BuildCascadeViews(view, projection, lightDirection, splits, 2048, CASTER_DISTANCE, views), 4u);

	const Mat4 inverseView = view.Inverse();
	for (u32 i = 0; i < 4; ++i) {
		for (const Vec4& cornerView : SliceCorners(projection, splits.depths[i], splits.depths[i + 1])) {
			const Vec4 world = inverseView * cornerView;
			const Vec4 clip = views[i].viewProjection * Vec4(world.x, world.y, world.z, 1.0f);
			EXPECT_GE(clip.x, -1.0f) << "cascade " << i;
			EXPECT_LE(clip.x, 1.0f) << "cascade " << i;
			EXPECT_GE(clip.y, -1.0f) << "cascade " << i;
			EXPECT_LE(clip.y, 1.0f) << "cascade " << i;
			EXPECT_GE(clip.z, 0.0f) << "cascade " << i;
			EXPECT_LE(clip.z, 1.0f) << "cascade " << i;
		}
	}
}

TEST(ShadowCascadeTest, TurningCameraDoesNotResizeCascadesMap) {
	const Mat4 projection = EditorProjection();
	const CascadeSplits splits = DefaultSplits(EditorViewportOps::EDITOR_NEAR_PLANE, DefaultShadowDistance(), 4);
	const Vec3 lightDirection = Vec3(0.4f, -1.0f, 0.3f).Normalized();

	std::array<CascadeView, MAX_SHADOW_CASCADES> reference{};
	BuildCascadeViews(EditorView(0.0f), projection, lightDirection, splits, 2048, CASTER_DISTANCE, reference);

	for (f32 yaw : {17.0f, 45.0f, 90.0f, 133.0f, 271.0f}) {
		std::array<CascadeView, MAX_SHADOW_CASCADES> turned{};
		BuildCascadeViews(EditorView(yaw), projection, lightDirection, splits, 2048, CASTER_DISTANCE, turned);
		for (u32 i = 0; i < 4; ++i) {
			EXPECT_NEAR(turned[i].worldTexelSize, reference[i].worldTexelSize, reference[i].worldTexelSize * 1e-4f) << "cascade " << i << ' ' << yaw;
		}
	}
}

TEST(ShadowCascadeTest, SubTexelSlideKeepsGrid) {
	const Mat4 projection = EditorProjection();
	const CascadeSplits splits = DefaultSplits(EditorViewportOps::EDITOR_NEAR_PLANE, DefaultShadowDistance(), 4);
	const Vec3 lightDirection = Vec3(0.3f, -1.0f, 0.15f).Normalized();

	std::array<CascadeView, MAX_SHADOW_CASCADES> before{};
	BuildCascadeViews(EditorView(0.0f), projection, lightDirection, splits, 2048, CASTER_DISTANCE, before);

	const Vec4 probe(3.0f, 0.5f, 12.0f, 1.0f);
	const Vec4 mapped = before[0].viewProjection * probe;

	const f32 nudge = before[0].worldTexelSize * 0.25f;
	std::array<CascadeView, MAX_SHADOW_CASCADES> after{};
	BuildCascadeViews(EditorView(0.0f, Vec3(nudge, 0.0f, 0.0f)), projection, lightDirection, splits, 2048,
					  CASTER_DISTANCE, after);
	const Vec4 mappedAfter = after[0].viewProjection * probe;

	EXPECT_NEAR(mapped.x, mappedAfter.x, 1e-5f);
	EXPECT_NEAR(mapped.y, mappedAfter.y, 1e-5f);
}

TEST(ShadowCascadeTest, UnnormalizedDirectionMatches) {
	const Mat4 projection = EditorProjection();
	const CascadeSplits splits = DefaultSplits(EditorViewportOps::EDITOR_NEAR_PLANE, DefaultShadowDistance(), 4);
	const Vec3 authored(0.02f, -0.5f, 0.03f);

	std::array<CascadeView, MAX_SHADOW_CASCADES> asAuthored{};
	std::array<CascadeView, MAX_SHADOW_CASCADES> asNormalized{};
	BuildCascadeViews(EditorView(0.0f), projection, authored, splits, 2048, CASTER_DISTANCE, asAuthored);
	BuildCascadeViews(EditorView(0.0f), projection, authored.Normalized(), splits, 2048, CASTER_DISTANCE,
					  asNormalized);

	for (u32 i = 0; i < 4; ++i) {
		for (u32 row = 0; row < 4; ++row) {
			for (u32 column = 0; column < 4; ++column) {
				EXPECT_NEAR(asAuthored[i].viewProjection(row, column), asNormalized[i].viewProjection(row, column),
							1e-5f)
					<< "cascade " << i << " element (" << row << ", " << column << ")";
			}
		}
	}
}

TEST(ShadowCascadeTest, CascadeProjectionIsReverseZ) {
	const Mat4 projection = EditorProjection();
	const CascadeSplits splits = DefaultSplits(EditorViewportOps::EDITOR_NEAR_PLANE, DefaultShadowDistance(), 1);
	const Vec3 lightDirection = Vec3(0.0f, -1.0f, 0.0f);

	std::array<CascadeView, 1> views{};
	ASSERT_EQ(BuildCascadeViews(EditorView(0.0f), projection, lightDirection, splits, 1024, CASTER_DISTANCE, views), 1u);

	const Vec4 nearer = views[0].viewProjection * Vec4(0.0f, 60.0f, 40.0f, 1.0f);
	const Vec4 farther = views[0].viewProjection * Vec4(0.0f, -60.0f, 40.0f, 1.0f);
	EXPECT_GT(nearer.z, farther.z);
	EXPECT_LE(nearer.z, 1.0f);
	EXPECT_GE(farther.z, 0.0f);
}

TEST(ShadowCascadeTest, CascadeTilesCoverAtlas) {
	constexpr u32 ATLAS = 2048;
	for (u32 count = 1; count <= MAX_SHADOW_CASCADES; ++count) {
		const u32 perAxis = GraphicsInternal::CascadeTilesPerAxis(count);
		const i32 expected = static_cast<i32>(ATLAS / perAxis);
		std::array<RectI, MAX_SHADOW_CASCADES> tiles{};
		for (u32 i = 0; i < count; ++i) {
			tiles[i] = GraphicsInternal::CascadeAtlasRect(i, ATLAS, count);
			EXPECT_EQ(tiles[i].size.x, expected) << "count " << count << " cascade " << i;
			EXPECT_EQ(tiles[i].size.y, expected);
			EXPECT_GE(tiles[i].Left(), 0);
			EXPECT_GE(tiles[i].Top(), 0);
			EXPECT_LE(tiles[i].Right(), static_cast<i32>(ATLAS));
			EXPECT_LE(tiles[i].Bottom(), static_cast<i32>(ATLAS));
		}
		for (u32 a = 0; a < count; ++a) {
			for (u32 b = a + 1; b < count; ++b) {
				EXPECT_FALSE(tiles[a].Intersects(tiles[b])) << "cascades " << a << " and " << b << ' ' << count;
			}
		}
	}
	EXPECT_EQ(GraphicsInternal::CascadeAtlasRect(0, ATLAS, 1).size.x, static_cast<i32>(ATLAS));
}

TEST(ShadowCascadeTest, AtlasMatrixMapsTopLeft) {
	constexpr u32 ATLAS = 1024;
	constexpr u32 COUNT = 4;
	const RectI tile = GraphicsInternal::CascadeAtlasRect(3, ATLAS, COUNT);
	ASSERT_EQ(tile.Left(), 512);
	ASSERT_EQ(tile.Top(), 512);

	const Mat4 atlasMatrix = GraphicsInternal::CascadeAtlasMatrix(Mat4::Identity(), tile, ATLAS);
	auto uvOf = [&](f32 clipX, f32 clipY) {
		const Vec4 uv = atlasMatrix * Vec4(clipX, clipY, 0.5f, 1.0f);
		return Vec2(uv.x, uv.y);
	};

	const Vec2 topLeft = uvOf(-1.0f, 1.0f);
	EXPECT_NEAR(topLeft.x, 0.5f, 1e-5f);
	EXPECT_NEAR(topLeft.y, 0.5f, 1e-5f);
	const Vec2 bottomRight = uvOf(1.0f, -1.0f);
	EXPECT_NEAR(bottomRight.x, 1.0f, 1e-5f);
	EXPECT_NEAR(bottomRight.y, 1.0f, 1e-5f);
	const Vec2 center = uvOf(0.0f, 0.0f);
	EXPECT_NEAR(center.x, 0.75f, 1e-5f);
	EXPECT_NEAR(center.y, 0.75f, 1e-5f);

	const Vec4 projected = atlasMatrix * Vec4(0.25f, -0.5f, 0.375f, 1.0f);
	EXPECT_NEAR(projected.z, 0.375f, 1e-6f);
	EXPECT_NEAR(projected.w, 1.0f, 1e-6f);
}
