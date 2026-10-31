#include "TestsPCH.h"

#include "UI/VectorRasterizer.h"

using namespace Shalltear;

namespace {

	constexpr f32 U8 = 1.0f / 255.0f;

	struct ImageBuilder {
		VectorAsset asset;

		explicit ImageBuilder(f32 viewWidth = 16.0f, f32 viewHeight = 16.0f) {
			asset.viewSize = {viewWidth, viewHeight};
		}

		ImageBuilder& Polygon(std::initializer_list<Vec2> points, Color color = Color::White(), f32 opacity = 1.0f) {
			VectorShape shape;
			shape.firstVerb = static_cast<u32>(asset.verbs.size());
			shape.firstPoint = static_cast<u32>(asset.points.size());
			bool first = true;
			for (Vec2 point : points) {
				asset.verbs.push_back(first ? VectorVerb::Move : VectorVerb::Line);
				asset.points.push_back(point);
				first = false;
			}
			asset.verbs.push_back(VectorVerb::Close);
			shape.verbCount = static_cast<u32>(asset.verbs.size()) - shape.firstVerb;
			shape.pointCount = static_cast<u32>(asset.points.size()) - shape.firstPoint;
			shape.opacity = opacity;
			shape.slot = static_cast<u32>(asset.slotDefaults.size());
			asset.slotDefaults.push_back(color);
			asset.shapes.push_back(shape);
			return *this;
		}

		ImageBuilder& Rect(f32 x, f32 y, f32 w, f32 h, Color color = Color::White(), f32 opacity = 1.0f) {
			return Polygon({{x, y}, {x + w, y}, {x + w, y + h}, {x, y + h}}, color, opacity);
		}
	};

	struct Raster {
		Vector<u8> pixels{GetMemoryResource(MemTag::UI)};
		Vec2I size{};

		[[nodiscard]] f32 Alpha(i32 x, i32 y) const {
			return static_cast<f32>(pixels[(static_cast<size_t>(y) * size.x + x) * 4 + 3]) * U8;
		}
		[[nodiscard]] f32 Channel(i32 x, i32 y, i32 component) const {
			const f32 alpha = Alpha(x, y);
			if (alpha <= 0.0f) {
				return 0.0f;
			}
			return static_cast<f32>(pixels[(static_cast<size_t>(y) * size.x + x) * 4 + component]) * U8 / alpha;
		}
		[[nodiscard]] f32 TotalAlpha() const {
			f32 total = 0.0f;
			for (size_t i = 3; i < pixels.size(); i += 4) {
				total += static_cast<f32>(pixels[i]) * U8;
			}
			return total;
		}
	};

	Raster Rasterize(const VectorAsset& asset, f32 sizePx, const UI::IconPalette& palette = {}) {
		UI::VectorRasterizer rasterizer;
		Raster out;
		EXPECT_TRUE(rasterizer.Rasterize(asset, sizePx, palette, out.pixels, out.size));
		return out;
	}

} // anonymous namespace

TEST(VectorRasterizerTest, AlignedRectIsExact) {
	ImageBuilder image(16.0f, 16.0f);
	image.Rect(4.0f, 4.0f, 8.0f, 8.0f);
	Raster raster = Rasterize(image.asset, 16.0f);
	ASSERT_EQ(raster.size, (Vec2I{16, 16}));

	EXPECT_NEAR(raster.Alpha(8, 8), 1.0f, U8);
	EXPECT_NEAR(raster.Alpha(4, 4), 1.0f, U8);
	EXPECT_NEAR(raster.Alpha(11, 11), 1.0f, U8);
	EXPECT_NEAR(raster.Alpha(3, 8), 0.0f, U8);
	EXPECT_NEAR(raster.Alpha(12, 8), 0.0f, U8);
	EXPECT_NEAR(raster.Alpha(0, 0), 0.0f, U8);
	EXPECT_NEAR(raster.TotalAlpha(), 64.0f, 0.5f);
}

TEST(VectorRasterizerTest, FractionalEdgeCoversPixelByFraction) {
	ImageBuilder image(16.0f, 16.0f);
	image.Rect(4.25f, 4.0f, 7.5f, 8.0f);
	Raster raster = Rasterize(image.asset, 16.0f);

	EXPECT_NEAR(raster.Alpha(4, 8), 0.75f, 2.0f * U8);
	EXPECT_NEAR(raster.Alpha(11, 8), 0.75f, 2.0f * U8);
	EXPECT_NEAR(raster.Alpha(5, 8), 1.0f, U8);
	EXPECT_NEAR(raster.Alpha(3, 8), 0.0f, U8);
	EXPECT_NEAR(raster.TotalAlpha(), 7.5f * 8.0f, 0.5f);
}

TEST(VectorRasterizerTest, ADiagonalEdgeCarriesExactHalfCoverage) {
	ImageBuilder image(16.0f, 16.0f);
	image.Polygon({{0.0f, 0.0f}, {16.0f, 16.0f}, {0.0f, 16.0f}});
	Raster raster = Rasterize(image.asset, 16.0f);

	for (i32 i : {2, 7, 13}) {
		EXPECT_NEAR(raster.Alpha(i, i), 0.5f, 2.0f * U8) << "diagonal pixel " << i;
	}
	EXPECT_NEAR(raster.Alpha(1, 12), 1.0f, U8);
	EXPECT_NEAR(raster.Alpha(12, 1), 0.0f, U8);
	EXPECT_NEAR(raster.TotalAlpha(), 128.0f, 1.0f);
}

TEST(VectorRasterizerTest, CircleMatchesPiRSquaredWithinPerimeter) {
	VectorAsset asset;
	asset.viewSize = {32.0f, 32.0f};
	constexpr f32 KAPPA = 0.5522847498307933f;
	constexpr f32 R = 8.0f;
	constexpr f32 O = R * KAPPA;
	constexpr f32 C = 16.0f;
	auto cubic = [&](Vec2 c0, Vec2 c1, Vec2 to) {
		asset.verbs.push_back(VectorVerb::Cubic);
		asset.points.push_back(c0);
		asset.points.push_back(c1);
		asset.points.push_back(to);
	};
	asset.verbs.push_back(VectorVerb::Move);
	asset.points.push_back({C + R, C});
	cubic({C + R, C + O}, {C + O, C + R}, {C, C + R});
	cubic({C - O, C + R}, {C - R, C + O}, {C - R, C});
	cubic({C - R, C - O}, {C - O, C - R}, {C, C - R});
	cubic({C + O, C - R}, {C + R, C - O}, {C + R, C});
	asset.verbs.push_back(VectorVerb::Close);

	VectorShape shape;
	shape.verbCount = static_cast<u32>(asset.verbs.size());
	shape.pointCount = static_cast<u32>(asset.points.size());
	asset.shapes.push_back(shape);
	asset.slotDefaults.push_back(Color::White());

	Raster raster = Rasterize(asset, 32.0f);
	const f32 expected = 3.14159265f * R * R;
	EXPECT_NEAR(raster.TotalAlpha(), expected, 2.0f * 3.14159265f * R * 0.05f + 1.0f);
	EXPECT_NEAR(raster.Alpha(16, 16), 1.0f, U8);
	EXPECT_NEAR(raster.Alpha(0, 0), 0.0f, U8);
}

TEST(VectorRasterizerTest, OverlappingContoursSaturateInsteadOfAccumulating) {
	ImageBuilder image(16.0f, 16.0f);
	VectorShape shape;
	shape.firstVerb = 0;
	shape.firstPoint = 0;
	auto box = [&](f32 x, f32 y, f32 w, f32 h) {
		image.asset.verbs.push_back(VectorVerb::Move);
		image.asset.points.push_back({x, y});
		for (Vec2 point : {Vec2{x + w, y}, Vec2{x + w, y + h}, Vec2{x, y + h}}) {
			image.asset.verbs.push_back(VectorVerb::Line);
			image.asset.points.push_back(point);
		}
		image.asset.verbs.push_back(VectorVerb::Close);
	};
	box(2.0f, 2.0f, 8.0f, 8.0f);
	box(6.0f, 6.0f, 8.0f, 8.0f);
	shape.verbCount = static_cast<u32>(image.asset.verbs.size());
	shape.pointCount = static_cast<u32>(image.asset.points.size());
	image.asset.shapes.push_back(shape);
	image.asset.slotDefaults.push_back(Color::White());

	Raster raster = Rasterize(image.asset, 16.0f);
	EXPECT_NEAR(raster.Alpha(8, 8), 1.0f, U8);
	EXPECT_NEAR(raster.TotalAlpha(), 112.0f, 1.0f);
}

TEST(VectorRasterizerTest, OversizedShapeDoesNotLeak) {
	ImageBuilder image(16.0f, 16.0f);
	image.Rect(-20.0f, -20.0f, 56.0f, 56.0f);
	Raster raster = Rasterize(image.asset, 16.0f);

	for (i32 y = 0; y < 16; ++y) {
		for (i32 x = 0; x < 16; ++x) {
			ASSERT_NEAR(raster.Alpha(x, y), 1.0f, U8) << "at " << x << "," << y;
		}
	}
	EXPECT_NEAR(raster.TotalAlpha(), 256.0f, 0.5f);
}

TEST(VectorRasterizerTest, OverriddenSlotReplacesHueAndLeavesOthers) {
	ImageBuilder image(16.0f, 16.0f);
	image.Rect(0.0f, 0.0f, 8.0f, 16.0f, Color(1.0f, 0.0f, 0.0f, 1.0f));
	image.Rect(8.0f, 0.0f, 8.0f, 16.0f, Color(0.0f, 0.0f, 1.0f, 1.0f));
	ASSERT_EQ(image.asset.slotDefaults.size(), 2u);

	Raster authored = Rasterize(image.asset, 16.0f);
	EXPECT_NEAR(authored.Channel(2, 8, 0), 1.0f, 2.0f * U8);
	EXPECT_NEAR(authored.Channel(12, 8, 2), 1.0f, 2.0f * U8);

	UI::IconPalette palette;
	palette.Set(0, Color(0.0f, 1.0f, 0.0f, 1.0f));
	Raster retinted = Rasterize(image.asset, 16.0f, palette);
	EXPECT_NEAR(retinted.Channel(2, 8, 1), 1.0f, 2.0f * U8);
	EXPECT_NEAR(retinted.Channel(2, 8, 0), 0.0f, 2.0f * U8);
	EXPECT_NEAR(retinted.Channel(12, 8, 2), 1.0f, 2.0f * U8);
}

TEST(VectorRasterizerTest, AnOverrideKeepsTheAuthoredShapeOpacity) {
	ImageBuilder image(16.0f, 16.0f);
	image.Rect(0.0f, 0.0f, 16.0f, 16.0f, Color::White(), 0.5f);

	UI::IconPalette palette;
	palette.Set(0, Color(1.0f, 0.0f, 0.0f, 1.0f));
	Raster raster = Rasterize(image.asset, 16.0f, palette);
	EXPECT_NEAR(raster.Alpha(8, 8), 0.5f, 2.0f * U8);
	EXPECT_NEAR(raster.Channel(8, 8, 0), 1.0f, 3.0f * U8);
}

TEST(VectorRasterizerTest, LuminanceMaskCutsHole) {
	ImageBuilder image(16.0f, 16.0f);
	image.Rect(0.0f, 0.0f, 16.0f, 16.0f);

	auto maskBox = [&](f32 x, f32 y, f32 w, f32 h, Color color) {
		VectorMaskShape shape;
		shape.firstVerb = static_cast<u32>(image.asset.maskVerbs.size());
		shape.firstPoint = static_cast<u32>(image.asset.maskPoints.size());
		image.asset.maskVerbs.push_back(VectorVerb::Move);
		image.asset.maskPoints.push_back({x, y});
		for (Vec2 point : {Vec2{x + w, y}, Vec2{x + w, y + h}, Vec2{x, y + h}}) {
			image.asset.maskVerbs.push_back(VectorVerb::Line);
			image.asset.maskPoints.push_back(point);
		}
		image.asset.maskVerbs.push_back(VectorVerb::Close);
		shape.verbCount = static_cast<u32>(image.asset.maskVerbs.size()) - shape.firstVerb;
		shape.pointCount = static_cast<u32>(image.asset.maskPoints.size()) - shape.firstPoint;
		shape.color = color;
		image.asset.maskShapes.push_back(shape);
	};
	VectorMask mask;
	mask.firstShape = 0;
	maskBox(0.0f, 0.0f, 16.0f, 16.0f, Color::White());
	maskBox(4.0f, 4.0f, 8.0f, 8.0f, Color(0.0f, 0.0f, 0.0f));
	mask.shapeCount = 2;
	image.asset.masks.push_back(mask);
	image.asset.shapes[0].mask = 0;

	Raster raster = Rasterize(image.asset, 16.0f);
	EXPECT_NEAR(raster.Alpha(0, 0), 1.0f, U8);
	EXPECT_NEAR(raster.Alpha(8, 8), 0.0f, U8);
	EXPECT_NEAR(raster.Alpha(15, 15), 1.0f, U8);
	EXPECT_NEAR(raster.TotalAlpha(), 192.0f, 1.0f);
}

TEST(VectorRasterizerTest, FittedBoxPreservesAspectAtEverySize) {
	ImageBuilder image(32.0f, 8.0f);
	image.Rect(0.0f, 0.0f, 32.0f, 8.0f);

	for (f32 size : {16.0f, 24.0f, 64.0f}) {
		Raster raster = Rasterize(image.asset, size);
		EXPECT_EQ(raster.size.x, static_cast<i32>(size)) << "at " << size;
		EXPECT_EQ(raster.size.y, static_cast<i32>(size) / 4) << "at " << size;
		EXPECT_NEAR(raster.TotalAlpha(), static_cast<f32>(raster.size.x * raster.size.y), 1.0f);
	}
	EXPECT_EQ(image.asset.FitInto(64.0f), (Vec2{64.0f, 16.0f}));
}

TEST(VectorRasterizerTest, EmptyAssetRasterizesNothingAndSaysSo) {
	UI::VectorRasterizer rasterizer;
	Vector<u8> pixels{GetMemoryResource(MemTag::UI)};
	Vec2I size{};
	VectorAsset empty;
	EXPECT_FALSE(rasterizer.Rasterize(empty, 16.0f, {}, pixels, size));

	ImageBuilder image;
	image.Rect(0.0f, 0.0f, 16.0f, 16.0f);
	EXPECT_FALSE(rasterizer.Rasterize(image.asset, 0.4f, {}, pixels, size));
}
