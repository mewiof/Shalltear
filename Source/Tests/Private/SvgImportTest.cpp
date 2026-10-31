#include "TestsPCH.h"

#include "Shalltear/Core/UI/VectorAsset.h"

#include <algorithm>
#include <filesystem>
#include <fstream>

using namespace Shalltear;

namespace {

	std::filesystem::path RepoDir(const char* relative) {
		std::error_code ec;
		std::filesystem::path probe = std::filesystem::current_path(ec);
		for (int depth = 0; depth < 6 && !probe.empty(); ++depth) {
			std::filesystem::path candidate = probe / relative;
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

	void Parse(std::string_view svg, VectorAsset& out) {
		str error;
		EXPECT_TRUE(out.ParseSvg(svg, error)) << error;
	}

	void ExpectRefused(std::string_view svg, std::string_view mentions) {
		VectorAsset asset;
		str error;
		EXPECT_FALSE(asset.ParseSvg(svg, error)) << "accepted: " << svg;
		EXPECT_NE(error.find(str(mentions)), str::npos) << "message was: " << error;
		EXPECT_TRUE(asset.shapes.empty());
		EXPECT_TRUE(asset.verbs.empty());
	}

	UI::Rect InkBounds(const VectorAsset& asset) {
		f32 minX = 1e30f, minY = 1e30f, maxX = -1e30f, maxY = -1e30f;
		for (const Vec2& point : asset.points) {
			minX = std::min(minX, point.x);
			minY = std::min(minY, point.y);
			maxX = std::max(maxX, point.x);
			maxY = std::max(maxY, point.y);
		}
		return {minX, minY, maxX - minX, maxY - minY};
	}

	u32 CountVerb(const VectorAsset& asset, VectorVerb verb) {
		u32 count = 0;
		for (VectorVerb entry : asset.verbs) {
			if (entry == verb) {
				++count;
			}
		}
		return count;
	}

	void ExpectOneShape(std::string_view svg) {
		VectorAsset asset;
		Parse(svg, asset);
		EXPECT_EQ(asset.shapes.size(), 1u) << svg;
	}

} // anonymous namespace

TEST(SvgImportTest, EngineMarkImportsShapes) {
	std::filesystem::path path = RepoDir("EngineAssets/Icons/Logo.svg");
	if (path.empty()) {
		GTEST_SKIP() << "EngineAssets not found";
	}
	std::ifstream file(path, std::ios::binary);
	ASSERT_TRUE(file.is_open());
	str source((std::istreambuf_iterator<char>(file)), std::istreambuf_iterator<char>());

	VectorAsset asset;
	str error;
	ASSERT_TRUE(asset.ParseSvg(source, error)) << error;

	EXPECT_EQ(asset.viewSize, (Vec2{64.0f, 64.0f}));
	EXPECT_EQ(asset.shapes.size(), 3u);

	ASSERT_EQ(asset.slotDefaults.size(), 1u);
	EXPECT_EQ(asset.slotDefaults[0], Color::White());
	for (const VectorShape& shape : asset.shapes) {
		EXPECT_EQ(shape.slot, 0u);
	}
	std::vector<f32> opacities;
	for (const VectorShape& shape : asset.shapes) {
		opacities.push_back(shape.opacity);
	}
	std::sort(opacities.begin(), opacities.end());
	EXPECT_NEAR(opacities[0], 0.46f, 0.001f);
	EXPECT_NEAR(opacities[1], 0.72f, 0.001f);
	EXPECT_NEAR(opacities[2], 1.00f, 0.001f);

	EXPECT_EQ(asset.masks.size(), 3u);
	for (const VectorShape& shape : asset.shapes) {
		EXPECT_NE(shape.mask, INVALID_VECTOR_INDEX);
	}
	for (const VectorMask& mask : asset.masks) {
		EXPECT_EQ(mask.shapeCount, 2u);
	}

	const UI::Rect ink = InkBounds(asset);
	EXPECT_GE(ink.x, -0.5f);
	EXPECT_GE(ink.y, -0.5f);
	EXPECT_LE(ink.x + ink.w, 64.5f);
	EXPECT_LE(ink.y + ink.h, 64.5f);
}

TEST(SvgImportTest, PathCommandSetLandsWhereCommandsSay) {
	VectorAsset absolute;
	Parse(R"SVG(<svg viewBox="0 0 100 100" fill="#ffffff">
		<path d="M10 10 H 60 V 40 L 10 40 Z"/>
	</svg>)SVG",
		  absolute);
	EXPECT_EQ(absolute.shapes.size(), 1u);
	EXPECT_EQ(InkBounds(absolute), (UI::Rect{10.0f, 10.0f, 50.0f, 30.0f}));

	VectorAsset relative;
	Parse(R"SVG(<svg viewBox="0 0 100 100" fill="#fff">
		<path d="m10 10 h50 v30 l-50 0 z"/>
	</svg>)SVG",
		  relative);
	EXPECT_EQ(InkBounds(relative), (UI::Rect{10.0f, 10.0f, 50.0f, 30.0f}));
}

TEST(SvgImportTest, ARepeatedMovetoBecomesALineto) {
	VectorAsset asset;
	Parse(R"SVG(<svg viewBox="0 0 100 100" fill="#fff">
		<path d="M10 10 50 10 30 40 Z"/>
	</svg>)SVG",
		  asset);
	EXPECT_EQ(CountVerb(asset, VectorVerb::Move), 1u);
	EXPECT_EQ(CountVerb(asset, VectorVerb::Line), 2u);
	EXPECT_EQ(InkBounds(asset), (UI::Rect{10.0f, 10.0f, 40.0f, 30.0f}));
}

TEST(SvgImportTest, ArcFollowsFlags) {
	VectorAsset up;
	Parse(R"SVG(<svg viewBox="0 0 100 100" fill="#fff">
		<path d="M10 50 A 40 40 0 0 1 90 50 Z"/>
	</svg>)SVG",
		  up);
	const UI::Rect ink = InkBounds(up);
	EXPECT_NEAR(ink.x, 10.0f, 0.01f);
	EXPECT_NEAR(ink.x + ink.w, 90.0f, 0.01f);
	EXPECT_LT(ink.y, 12.0f);
	EXPECT_GT(ink.y, 4.0f);
	EXPECT_NEAR(ink.y + ink.h, 50.0f, 0.01f);

	VectorAsset down;
	Parse(R"SVG(<svg viewBox="0 0 100 100" fill="#fff">
		<path d="M10 50a40 40 0 0080 0z"/>
	</svg>)SVG",
		  down);
	const UI::Rect downInk = InkBounds(down);
	EXPECT_GT(downInk.y + downInk.h, 85.0f);
	EXPECT_NEAR(downInk.y, 50.0f, 0.01f);
}

TEST(SvgImportTest, NestedTransformsComposeParentThenChild) {
	VectorAsset nested;
	Parse(R"SVG(<svg viewBox="0 0 100 100" fill="#fff">
		<g transform="translate(10 10)"><g transform="scale(2)">
			<rect x="0" y="0" width="1" height="1"/>
		</g></g>
	</svg>)SVG",
		  nested);
	EXPECT_EQ(InkBounds(nested), (UI::Rect{10.0f, 10.0f, 2.0f, 2.0f}));

	VectorAsset chained;
	Parse(R"SVG(<svg viewBox="0 0 100 100" fill="#fff">
		<rect transform="translate(10 10) scale(2)" x="0" y="0" width="1" height="1"/>
	</svg>)SVG",
		  chained);
	EXPECT_EQ(InkBounds(chained), (UI::Rect{10.0f, 10.0f, 2.0f, 2.0f}));
}

TEST(SvgImportTest, ViewBoxOriginIsFoldedIntoPoints) {
	VectorAsset asset;
	Parse(R"SVG(<svg viewBox="20 30 40 40" fill="#fff">
		<rect x="20" y="30" width="10" height="10"/>
	</svg>)SVG",
		  asset);
	EXPECT_EQ(asset.viewSize, (Vec2{40.0f, 40.0f}));
	EXPECT_EQ(InkBounds(asset), (UI::Rect{0.0f, 0.0f, 10.0f, 10.0f}));
}

TEST(SvgImportTest, TheBasicShapesBecomePaths) {
	VectorAsset circle;
	Parse(R"SVG(<svg viewBox="0 0 100 100" fill="#fff"><circle cx="50" cy="50" r="20"/></svg>)SVG", circle);
	EXPECT_EQ(InkBounds(circle), (UI::Rect{30.0f, 30.0f, 40.0f, 40.0f}));

	VectorAsset ellipse;
	Parse(R"SVG(<svg viewBox="0 0 100 100" fill="#fff"><ellipse cx="50" cy="50" rx="30" ry="10"/></svg>)SVG", ellipse);
	EXPECT_EQ(InkBounds(ellipse), (UI::Rect{20.0f, 40.0f, 60.0f, 20.0f}));

	VectorAsset polygon;
	Parse(R"SVG(<svg viewBox="0 0 100 100" fill="#fff"><polygon points="10,10 90,10 50,90"/></svg>)SVG", polygon);
	EXPECT_EQ(InkBounds(polygon), (UI::Rect{10.0f, 10.0f, 80.0f, 80.0f}));

	VectorAsset rounded;
	Parse(R"SVG(<svg viewBox="0 0 100 100" fill="#fff"><rect x="10" y="10" width="80" height="60" rx="8"/></svg>)SVG", rounded);
	EXPECT_EQ(InkBounds(rounded), (UI::Rect{10.0f, 10.0f, 80.0f, 60.0f}));
	EXPECT_EQ(CountVerb(rounded, VectorVerb::Cubic), 4u);

	VectorAsset square;
	Parse(R"SVG(<svg viewBox="0 0 100 100" fill="#fff"><rect x="10" y="10" width="80" height="60"/></svg>)SVG", square);
	EXPECT_EQ(CountVerb(square, VectorVerb::Cubic), 0u);
}

TEST(SvgImportTest, FillsBecomeSlotsInOrder) {
	VectorAsset asset;
	Parse(R"SVG(<svg viewBox="0 0 100 100">
		<rect fill="#ff0000" x="0" y="0" width="10" height="10"/>
		<rect fill="#0000ff" x="10" y="0" width="10" height="10"/>
		<rect fill="#ff0000" x="20" y="0" width="10" height="10"/>
	</svg>)SVG",
		  asset);
	ASSERT_EQ(asset.slotDefaults.size(), 2u);
	EXPECT_EQ(asset.slotDefaults[0], Color(1.0f, 0.0f, 0.0f, 1.0f));
	EXPECT_EQ(asset.slotDefaults[1], Color(0.0f, 0.0f, 1.0f, 1.0f));
	ASSERT_EQ(asset.shapes.size(), 3u);
	EXPECT_EQ(asset.shapes[0].slot, 0u);
	EXPECT_EQ(asset.shapes[1].slot, 1u);
	EXPECT_EQ(asset.shapes[2].slot, 0u);
}

TEST(SvgImportTest, EveryTransparencySpellingFoldsIntoShapeOpacity) {
	VectorAsset asset;
	Parse(R"SVG(<svg viewBox="0 0 100 100" fill="#ffffff">
		<g opacity="0.5">
			<rect fill-opacity="0.5" x="0" y="0" width="10" height="10"/>
			<rect fill="#ffffff80" x="10" y="0" width="10" height="10"/>
		</g>
	</svg>)SVG",
		  asset);
	ASSERT_EQ(asset.shapes.size(), 2u);
	EXPECT_NEAR(asset.shapes[0].opacity, 0.25f, 0.01f);
	EXPECT_NEAR(asset.shapes[1].opacity, 0.25f, 0.01f);
	EXPECT_EQ(asset.slotDefaults.size(), 1u);
}

TEST(SvgImportTest, FillNonePaintsNothing) {
	VectorAsset asset;
	Parse(R"SVG(<svg viewBox="0 0 100 100" fill="#fff">
		<rect fill="none" x="0" y="0" width="10" height="10"/>
		<rect x="10" y="0" width="10" height="10"/>
	</svg>)SVG",
		  asset);
	EXPECT_EQ(asset.shapes.size(), 1u);
	EXPECT_EQ(InkBounds(asset), (UI::Rect{10.0f, 0.0f, 10.0f, 10.0f}));
}

TEST(SvgImportTest, CurrentColorTakesSlotDefaultingToWhite) {
	VectorAsset asset;
	Parse(R"SVG(<svg viewBox="0 0 100 100">
		<rect fill="currentColor" x="0" y="0" width="10" height="10"/>
		<rect fill="#ff0000" x="10" y="0" width="10" height="10"/>
	</svg>)SVG",
		  asset);
	ASSERT_EQ(asset.slotDefaults.size(), 2u);
	EXPECT_EQ(asset.slotDefaults[0], Color::White());
	EXPECT_EQ(asset.shapes[0].slot, 0u);
	EXPECT_EQ(asset.shapes[1].slot, 1u);
}

TEST(SvgImportTest, UnsupportedConstructsRefused) {
	ExpectRefused(R"SVG(<svg viewBox="0 0 10 10"><path stroke="#000" stroke-width="2" d="M0 0 L10 10"/></svg>)SVG", "stroke");
	ExpectRefused(R"SVG(<svg viewBox="0 0 10 10"><rect style="fill:#f00" x="0" y="0" width="1" height="1"/></svg>)SVG", "style");
	ExpectRefused(R"SVG(<svg viewBox="0 0 10 10"><rect fill="url(#g)" x="0" y="0" width="1" height="1"/></svg>)SVG", "gradients");
	ExpectRefused(R"SVG(<svg viewBox="0 0 10 10"><path fill-rule="evenodd" fill="#fff" d="M0 0 L10 0 L10 10 Z"/></svg>)SVG", "nonzero");
	ExpectRefused(R"SVG(<svg viewBox="0 0 10 10"><use href="#x"/></svg>)SVG", "isn't in the subset");
	ExpectRefused(R"SVG(<svg viewBox="0 0 10 10"><text x="0" y="0">hi</text></svg>)SVG", "isn't in the subset");
	ExpectRefused(R"SVG(<svg viewBox="0 0 10 10"><line x1="0" y1="0" x2="10" y2="10"/></svg>)SVG", "no fill");
	ExpectRefused(R"SVG(<svg viewBox="0 0 10 10"><rect clip-path="url(#c)" x="0" y="0" width="1" height="1"/></svg>)SVG", "clip-path");
	ExpectRefused(R"SVG(<svg viewBox="0 0 10 10"><rect fill="rebeccapurple" x="0" y="0" width="1" height="1"/></svg>)SVG", "unrecognized color");
	ExpectRefused(R"SVG(<svg viewBox="0 0 10 10"><rect transform="wobble(2)" x="0" y="0" width="1" height="1"/></svg>)SVG", "unrecognized transform");
	ExpectRefused(R"SVG(<svg width="1cm" height="1cm"><rect fill="#fff" x="0" y="0" width="1" height="1"/></svg>)SVG", "viewBox");
	ExpectRefused(R"SVG(<rect x="0" y="0" width="1" height="1"/>)SVG", "isn't <svg>");
	ExpectRefused(R"SVG(<svg viewBox="0 0 10 10"><mask id="m"><rect x="0" y="0" width="1" height="1" fill="#fff"/></mask><rect mask="url(#m)" fill="#fff" x="0" y="0" width="1" height="1"/></svg>)SVG", "userSpaceOnUse");
	ExpectRefused(R"SVG(<svg viewBox="0 0 10 10"><rect mask="url(#nope)" fill="#fff" x="0" y="0" width="1" height="1"/></svg>)SVG", "no <mask> defines");
}

TEST(SvgImportTest, AcceptedSiblingOfEveryRefusalStillParses) {
	ExpectOneShape(R"SVG(<svg viewBox="0 0 10 10"><rect stroke="none" fill="#fff" x="0" y="0" width="1" height="1"/></svg>)SVG");
	ExpectOneShape(R"SVG(<svg viewBox="0 0 10 10"><rect fill-rule="nonzero" fill="#fff" x="0" y="0" width="1" height="1"/></svg>)SVG");
	ExpectOneShape(R"SVG(<svg viewBox="0 0 10 10"><rect fill="teal" x="0" y="0" width="1" height="1"/></svg>)SVG");
	ExpectOneShape(R"SVG(<svg viewBox="0 0 10 10"><rect transform="rotate(45 5 5)" fill="#fff" x="0" y="0" width="1" height="1"/></svg>)SVG");
	VectorAsset masked;
	Parse(R"SVG(<svg viewBox="0 0 10 10"><mask id="m" maskUnits="userSpaceOnUse"><rect x="0" y="0" width="10" height="10" fill="#fff"/></mask><rect mask="url(#m)" fill="#fff" x="0" y="0" width="1" height="1"/></svg>)SVG",
		  masked);
	EXPECT_EQ(masked.masks.size(), 1u);
	EXPECT_NE(masked.shapes[0].mask, INVALID_VECTOR_INDEX);

	VectorAsset sized;
	Parse(R"SVG(<svg width="24" height="12"><rect fill="#fff" x="0" y="0" width="1" height="1"/></svg>)SVG", sized);
	EXPECT_EQ(sized.viewSize, (Vec2{24.0f, 12.0f}));
}

TEST(SvgImportTest, PaletteCapIs4) {
	VectorAsset four;
	Parse(R"SVG(<svg viewBox="0 0 10 10">
		<rect fill="#100000" x="0" y="0" width="1" height="1"/>
		<rect fill="#200000" x="0" y="0" width="1" height="1"/>
		<rect fill="#300000" x="0" y="0" width="1" height="1"/>
		<rect fill="#400000" x="0" y="0" width="1" height="1"/>
	</svg>)SVG",
		  four);
	EXPECT_EQ(four.slotDefaults.size(), 4u);

	ExpectRefused(R"SVG(<svg viewBox="0 0 10 10">
		<rect fill="#100000" x="0" y="0" width="1" height="1"/>
		<rect fill="#200000" x="0" y="0" width="1" height="1"/>
		<rect fill="#300000" x="0" y="0" width="1" height="1"/>
		<rect fill="#400000" x="0" y="0" width="1" height="1"/>
		<rect fill="#500000" x="0" y="0" width="1" height="1"/>
	</svg>)SVG",
				  "palette");
}

TEST(SvgImportTest, UnknownAttributesAreIgnored) {
	VectorAsset asset;
	Parse(R"SVG(<svg xmlns="http://www.w3.org/2000/svg" viewBox="0 0 10 10" role="img" aria-labelledby="t" fill="#fff">
		<title id="t">A Mark</title>
		<desc>ignored</desc>
		<rect id="body" data-thing="x" x="0" y="0" width="10" height="10"/>
	</svg>)SVG",
		  asset);
	EXPECT_EQ(asset.shapes.size(), 1u);
	EXPECT_EQ(InkBounds(asset), (UI::Rect{0.0f, 0.0f, 10.0f, 10.0f}));
}

TEST(SvgImportTest, AnSvgWithNothingPaintableIsRefused) {
	ExpectRefused(R"SVG(<svg viewBox="0 0 10 10"><title>only a title</title></svg>)SVG", "nothing paintable");
}
