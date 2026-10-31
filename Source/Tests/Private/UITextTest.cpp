#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"

#include <filesystem>

using namespace Shalltear;
using namespace UITest;

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

	FontAsset* LoadEngineFont() {
		std::filesystem::path path = RepoDir("EngineAssets/Fonts/Inter/Inter-Regular.ttf");
		if (path.empty()) {
			return nullptr;
		}
		FontAsset* font = New<FontAsset>(MemTag::Asset);
		if (!font->Import(Path(path.generic_string()))) {
			Delete(font);
			return nullptr;
		}
		return font;
	}

	class UIFontFixture : public ::testing::Test {
	protected:
		FontAsset* _font = nullptr;

		void SetUp() override {
			UISystem::Initialize();
			_font = LoadEngineFont();
			if (!_font) {
				GTEST_SKIP() << "engine font not found";
			}
		}
		void TearDown() override {
			if (_font) {
				Delete(_font);
			}
			UISystem::Terminate();
		}

		UI::TextQuery Query(std::string_view text, f32 wrapWidth = 0.0f, f32 letterSpacing = 0.0f) {
			UI::TextQuery query;
			query.font = Handle<FontAsset>(_font);
			query.sizePx = 16.0f;
			query.utf8 = text;
			query.wrapWidth = wrapWidth;
			query.letterSpacing = letterSpacing;
			return query;
		}

		UI::TextQuery Query(std::string_view text, UI::TextOverflow overflow, f32 wrapWidth = 0.0f) {
			UI::TextQuery query = Query(text, wrapWidth);
			query.overflow = overflow;
			return query;
		}

		f32 MarkWidth() {
			return UISystem::GetFontSystem()->MeasureText(Query("\xE2\x80\xA6")).x;
		}

		static bool WasTruncated(const UI::ShapedRun& run) {
			return run.caretStops.size() == 1;
		}
	};

} // anonymous namespace

TEST(UITextTest, CaretStopsRoundTrip) {
	MockFontSystem fonts;
	UI::ShapedRun run;
	UI::TextQuery query;
	query.utf8 = "abcd";
	ASSERT_TRUE(fonts.ShapeText(query, run));

	EXPECT_NEAR(run.CaretXForByte(0), 0.0f, 0.001f);
	EXPECT_NEAR(run.CaretXForByte(2), 20.0f, 0.001f);
	EXPECT_NEAR(run.CaretXForByte(4), 40.0f, 0.001f);
	EXPECT_NEAR(run.CaretXForByte(99), 40.0f, 0.001f);

	EXPECT_EQ(run.ByteForCaret(-5.0f, 0), 0u);
	EXPECT_EQ(run.ByteForCaret(4.0f, 0), 0u);
	EXPECT_EQ(run.ByteForCaret(6.0f, 0), 1u);
	EXPECT_EQ(run.ByteForCaret(100.0f, 0), 4u);
}

TEST(UITextTest, CaretStopsUseByteOffsets) {
	MockFontSystem fonts;
	UI::ShapedRun run;
	UI::TextQuery query;
	query.utf8 = "\xC3\xA4x";
	ASSERT_TRUE(fonts.ShapeText(query, run));
	ASSERT_EQ(run.caretStops.size(), 3u);
	EXPECT_EQ(run.caretStops[0].byteOffset, 0u);
	EXPECT_EQ(run.caretStops[1].byteOffset, 2u);
	EXPECT_EQ(run.caretStops[2].byteOffset, 3u);
}

TEST(UITextTest, WrappedCaretStopsNameLine) {
	MockFontSystem fonts;
	UI::ShapedRun run;
	UI::TextQuery query;
	query.utf8 = "ab cd";
	query.wrapWidth = 30.0f;
	ASSERT_TRUE(fonts.ShapeText(query, run));
	ASSERT_TRUE(run.multiline);
	EXPECT_EQ(run.LineCount(), 2u);
	const UI::CaretStop wrapped = run.StopForByte(3);
	EXPECT_EQ(wrapped.line, 1u);
	EXPECT_NEAR(wrapped.x, 0.0f, 0.001f);
	EXPECT_EQ(run.StopForByte(5).line, 1u);
	EXPECT_NEAR(run.StopForByte(5).x, 20.0f, 0.001f);
	EXPECT_EQ(run.ByteForCaret(100.0f, 0), 2u);
	EXPECT_EQ(run.ByteForCaret(12.0f, 1), 4u);
	EXPECT_EQ(run.ByteForCaret(0.0f, 7), 3u);

	query.utf8 = "ab\ncd";
	query.wrapWidth = 0.0f;
	ASSERT_TRUE(fonts.ShapeText(query, run));
	EXPECT_EQ(run.StopForByte(2).line, 0u);
	EXPECT_NEAR(run.StopForByte(2).x, 20.0f, 0.001f);
	EXPECT_EQ(run.StopForByte(3).line, 1u);
	query.utf8 = "ab\n";
	ASSERT_TRUE(fonts.ShapeText(query, run));
	EXPECT_EQ(run.LineCount(), 2u);
	EXPECT_EQ(run.StopForByte(3).line, 1u);
}

TEST_F(UIFontFixture, ShapesTextWithRealMetrics) {
	UI::IFontSystem* fonts = UISystem::GetFontSystem();
	ASSERT_NE(fonts, nullptr);

	UI::ShapedRun run;
	ASSERT_TRUE(fonts->ShapeText(Query("Hello"), run));
	EXPECT_GT(run.extent.x, 0.0f);
	EXPECT_GT(run.extent.y, 0.0f);
	EXPECT_GT(run.lineHeight, 0.0f);
	EXPECT_FALSE(run.multiline);
	ASSERT_EQ(run.caretStops.size(), 6u);
	EXPECT_EQ(run.caretStops.back().byteOffset, 5u);
	for (size_t i = 1; i < run.caretStops.size(); ++i) {
		EXPECT_GE(run.caretStops[i].x, run.caretStops[i - 1].x);
	}
}

TEST_F(UIFontFixture, LongerTextMeasuresWider) {
	UI::IFontSystem* fonts = UISystem::GetFontSystem();
	Vec2 shortExtent = fonts->MeasureText(Query("Hi"));
	Vec2 longExtent = fonts->MeasureText(Query("Hi there, much longer"));
	EXPECT_GT(longExtent.x, shortExtent.x);
	EXPECT_NEAR(shortExtent.y, longExtent.y, 0.5f);
}

TEST_F(UIFontFixture, WrappingBreaksLines) {
	UI::IFontSystem* fonts = UISystem::GetFontSystem();
	Vec2 unwrapped = fonts->MeasureText(Query("several words that will wrap"));
	UI::ShapedRun run;
	ASSERT_TRUE(fonts->ShapeText(Query("several words that will wrap", unwrapped.x * 0.4f), run));
	EXPECT_TRUE(run.multiline);
	EXPECT_LT(run.extent.x, unwrapped.x);
	EXPECT_GT(run.extent.y, unwrapped.y * 1.5f);
}

TEST_F(UIFontFixture, ExplicitNewlinesBreakLines) {
	UI::IFontSystem* fonts = UISystem::GetFontSystem();
	UI::ShapedRun run;
	ASSERT_TRUE(fonts->ShapeText(Query("one\ntwo\nthree"), run));
	EXPECT_TRUE(run.multiline);
	EXPECT_GE(run.extent.y, run.lineHeight * 2.5f);
}

TEST_F(UIFontFixture, WrappedCaretStopsOrdered) {
	UI::IFontSystem* fonts = UISystem::GetFontSystem();
	const char* text = "several words that will wrap";
	Vec2 unwrapped = fonts->MeasureText(Query(text));
	UI::ShapedRun run;
	ASSERT_TRUE(fonts->ShapeText(Query(text, unwrapped.x * 0.4f), run));
	ASSERT_TRUE(run.multiline);
	ASSERT_GT(run.caretStops.size(), 2u);
	u32 line = 0;
	for (size_t i = 1; i < run.caretStops.size(); ++i) {
		const UI::CaretStop& stop = run.caretStops[i];
		EXPECT_GE(stop.byteOffset, run.caretStops[i - 1].byteOffset) << i;
		if (stop.line != line) {
			EXPECT_EQ(stop.line, line + 1);
			EXPECT_FLOAT_EQ(stop.x, 0.0f) << "line " << stop.line;
			line = stop.line;
		}
	}
	EXPECT_EQ(run.caretStops.back().byteOffset, static_cast<u32>(std::strlen(text)));
	EXPECT_EQ(run.LineCount(), static_cast<u32>(std::lround(run.extent.y / run.lineHeight)));

	ASSERT_TRUE(fonts->ShapeText(Query("one\ntwo\n"), run));
	EXPECT_EQ(run.LineCount(), 3u);
	EXPECT_EQ(run.StopForByte(3).line, 0u);
	EXPECT_EQ(run.StopForByte(4).line, 1u);
	EXPECT_EQ(run.StopForByte(8).line, 2u);
	EXPECT_NEAR(run.extent.y, 3.0f * run.lineHeight, 0.5f);
}

TEST_F(UIFontFixture, EllipsisFitsOneLine) {
	UI::IFontSystem* fonts = UISystem::GetFontSystem();
	constexpr std::string_view TEXT = "New Object (Copy) (Copy)";
	Vec2 full = fonts->MeasureText(Query(TEXT));

	const f32 budget = full.x * 0.5f;
	UI::ShapedRun run;
	ASSERT_TRUE(fonts->ShapeText(Query(TEXT, UI::TextOverflow::Ellipsis, budget), run));

	EXPECT_FALSE(run.multiline);
	EXPECT_FLOAT_EQ(run.extent.y, full.y);
	EXPECT_LE(run.extent.x, budget);
	EXPECT_TRUE(WasTruncated(run));
	EXPECT_GT(run.extent.x, budget * 0.8f);
}

TEST_F(UIFontFixture, BudgetTooNarrowForMarkIsMark) {
	UI::IFontSystem* fonts = UISystem::GetFontSystem();
	const f32 mark = MarkWidth();
	ASSERT_GT(mark, 0.0f);

	UI::ShapedRun run;
	ASSERT_TRUE(fonts->ShapeText(Query("New Object (Copy)", UI::TextOverflow::Ellipsis, mark * 0.5f), run));
	EXPECT_FLOAT_EQ(run.extent.x, mark);
	EXPECT_TRUE(WasTruncated(run));
}

TEST_F(UIFontFixture, TextThatAlreadyFitsIsNotTruncated) {
	UI::IFontSystem* fonts = UISystem::GetFontSystem();
	constexpr std::string_view TEXT = "New Object";
	Vec2 full = fonts->MeasureText(Query(TEXT));

	UI::ShapedRun run;
	ASSERT_TRUE(fonts->ShapeText(Query(TEXT, UI::TextOverflow::Ellipsis, full.x), run));
	EXPECT_FLOAT_EQ(run.extent.x, full.x);
	EXPECT_FALSE(WasTruncated(run));
}

TEST_F(UIFontFixture, ClipIgnoresTheWidthEntirely) {
	UI::IFontSystem* fonts = UISystem::GetFontSystem();
	constexpr std::string_view TEXT = "several words that would wrap";
	UI::ShapedRun unbounded;
	ASSERT_TRUE(fonts->ShapeText(Query(TEXT), unbounded));

	UI::ShapedRun clipped;
	ASSERT_TRUE(fonts->ShapeText(Query(TEXT, UI::TextOverflow::Clip, 20.0f), clipped));

	EXPECT_FALSE(clipped.multiline);
	EXPECT_FLOAT_EQ(clipped.extent.x, unbounded.extent.x);
	EXPECT_EQ(clipped.glyphs.size(), unbounded.glyphs.size());
	EXPECT_EQ(clipped.caretStops.size(), unbounded.caretStops.size());
}

TEST_F(UIFontFixture, EllipsizedRunStopsAtFirstNewline) {
	UI::IFontSystem* fonts = UISystem::GetFontSystem();
	UI::ShapedRun run;
	ASSERT_TRUE(fonts->ShapeText(Query("one\ntwo\nthree", UI::TextOverflow::Ellipsis), run));

	EXPECT_FALSE(run.multiline);
	EXPECT_FLOAT_EQ(run.extent.y, run.lineHeight);
	EXPECT_TRUE(WasTruncated(run));
	EXPECT_NEAR(run.extent.x, fonts->MeasureText(Query("one")).x + MarkWidth(), 2.0f);
}

TEST_F(UIFontFixture, AnUnboundedEllipsisIsPlainMaxContent) {
	UI::IFontSystem* fonts = UISystem::GetFontSystem();
	constexpr std::string_view TEXT = "New Object (Copy) (Copy)";
	UI::ShapedRun wrapped;
	ASSERT_TRUE(fonts->ShapeText(Query(TEXT), wrapped));

	UI::ShapedRun unbounded;
	ASSERT_TRUE(fonts->ShapeText(Query(TEXT, UI::TextOverflow::Ellipsis, 0.0f), unbounded));

	EXPECT_FLOAT_EQ(unbounded.extent.x, wrapped.extent.x);
	EXPECT_EQ(unbounded.caretStops.size(), wrapped.caretStops.size());
	EXPECT_FALSE(WasTruncated(unbounded));
}

TEST_F(UIFontFixture, LineHeightIsPositiveAndScales) {
	UI::IFontSystem* fonts = UISystem::GetFontSystem();
	Handle<FontAsset> handle(_font);
	f32 small = fonts->GetLineHeight(handle, 12.0f);
	f32 large = fonts->GetLineHeight(handle, 24.0f);
	EXPECT_GT(small, 0.0f);
	EXPECT_GT(large, small);
}

TEST_F(UIFontFixture, LetterSpacingBetweenGlyphsOnly) {
	UI::IFontSystem* fonts = UISystem::GetFontSystem();
	constexpr f32 SPACING = 3.0f;

	UI::ShapedRun plain;
	ASSERT_TRUE(fonts->ShapeText(Query("Hello"), plain));
	UI::ShapedRun spaced;
	ASSERT_TRUE(fonts->ShapeText(Query("Hello", 0.0f, SPACING), spaced));

	ASSERT_EQ(plain.caretStops.size(), 6u);
	EXPECT_NEAR(spaced.extent.x, plain.extent.x + 4.0f * SPACING, 0.01f);
	EXPECT_NEAR(spaced.extent.y, plain.extent.y, 0.01f);

	ASSERT_EQ(spaced.caretStops.size(), plain.caretStops.size());
	for (size_t k = 1; k < spaced.caretStops.size(); ++k) {
		EXPECT_NEAR(spaced.caretStops[k].x,
					plain.caretStops[k].x + static_cast<f32>(k - 1) * SPACING, 0.01f)
			<< "caret boundary " << k;
	}
}

TEST_F(UIFontFixture, SingleGlyphIgnoresSpacing) {
	UI::IFontSystem* fonts = UISystem::GetFontSystem();
	UI::ShapedRun plain;
	ASSERT_TRUE(fonts->ShapeText(Query("M"), plain));
	UI::ShapedRun spaced;
	ASSERT_TRUE(fonts->ShapeText(Query("M", 0.0f, 8.0f), spaced));

	ASSERT_EQ(plain.caretStops.size(), 2u);
	ASSERT_EQ(spaced.caretStops.size(), 2u);
	EXPECT_NEAR(spaced.extent.x, plain.extent.x, 0.001f);
	EXPECT_NEAR(spaced.caretStops[1].x, plain.caretStops[1].x, 0.001f);
}

TEST_F(UIFontFixture, NegativeLetterSpacingTightensTheRun) {
	UI::IFontSystem* fonts = UISystem::GetFontSystem();
	constexpr f32 SPACING = -1.5f;

	UI::ShapedRun plain;
	ASSERT_TRUE(fonts->ShapeText(Query("Hello"), plain));
	UI::ShapedRun tight;
	ASSERT_TRUE(fonts->ShapeText(Query("Hello", 0.0f, SPACING), tight));

	EXPECT_NEAR(tight.extent.x, plain.extent.x + 4.0f * SPACING, 0.01f);
	EXPECT_GT(tight.extent.x, 0.0f);
}

TEST_F(UIFontFixture, LetterSpacingIsPartOfWrapDecision) {
	UI::IFontSystem* fonts = UISystem::GetFontSystem();
	constexpr char TEXT[] = "several words that will wrap";
	f32 box = fonts->MeasureText(Query(TEXT)).x + 1.0f;

	UI::ShapedRun fits;
	ASSERT_TRUE(fonts->ShapeText(Query(TEXT, box), fits));
	ASSERT_FALSE(fits.multiline);

	UI::ShapedRun spaced;
	ASSERT_TRUE(fonts->ShapeText(Query(TEXT, box, 6.0f), spaced));
	EXPECT_TRUE(spaced.multiline);
	EXPECT_LE(spaced.extent.x, box + 0.01f);
}

TEST_F(UIFontFixture, MissingFontFailsCleanly) {
	UI::IFontSystem* fonts = UISystem::GetFontSystem();
	UI::TextQuery query;
	query.font = Handle<FontAsset>{};
	query.sizePx = 16.0f;
	query.utf8 = "text";
	UI::ShapedRun run;
	EXPECT_FALSE(fonts->ShapeText(query, run));
	EXPECT_TRUE(run.glyphs.empty());
}

TEST(UITextTest, FontAssetImportReadsBytes) {
	FontAsset* font = LoadEngineFont();
	if (!font) {
		GTEST_SKIP() << "engine font not found";
	}
	EXPECT_GT(font->data.size(), 1024u);
	bool truetype = font->data[0] == 0x00 && font->data[1] == 0x01;
	bool otto = font->data[0] == 'O' && font->data[1] == 'T';
	EXPECT_TRUE(truetype || otto);
	Delete(font);
}

TEST_F(UIFontFixture, EllipsizedRunResolvesVisible) {
	constexpr u32 CHARS = 4000;
	constexpr f32 BUDGET = 120.0f;
	const str text(CHARS, 'x');
	UI::IFontSystem* fonts = UISystem::GetFontSystem();

	fonts->ResetTextStats();
	UI::ShapedRun whole;
	ASSERT_TRUE(fonts->ShapeText(Query(text, UI::TextOverflow::Clip, 0.0f), whole));
	const u64 resolvedWhole = fonts->GetTextStats().glyphsResolved;
	ASSERT_EQ(resolvedWhole, CHARS);

	fonts->ResetTextStats();
	UI::ShapedRun clipped;
	ASSERT_TRUE(fonts->ShapeText(Query(text, UI::TextOverflow::Ellipsis, BUDGET), clipped));
	const UI::TextStats budgeted = fonts->GetTextStats();

	ASSERT_TRUE(clipped.truncated);
	EXPECT_EQ(budgeted.walks, 1u);
	EXPECT_EQ(budgeted.runsBuilt, 1u);

	const f32 glyph = fonts->MeasureText(Query("x")).x;
	ASSERT_GT(glyph, 1.0f);
	const u64 fits = static_cast<u64>(BUDGET / glyph) + 2;
	ASSERT_LT(fits, CHARS / 4);
	EXPECT_LE(budgeted.glyphsResolved, fits);
}

TEST_F(UIFontFixture, MeasuringTextWalksItWithoutBuildingRun) {
	const str text = "the quick brown fox jumps over the lazy dog";
	UI::IFontSystem* fonts = UISystem::GetFontSystem();

	fonts->ResetTextStats();
	UI::ShapedRun run;
	ASSERT_TRUE(fonts->ShapeText(Query(text, UI::TextOverflow::Clip, 0.0f), run));
	const UI::TextStats shaped = fonts->GetTextStats();

	fonts->ResetTextStats();
	const Vec2 extent = fonts->MeasureText(Query(text, UI::TextOverflow::Clip, 0.0f));
	const UI::TextStats measured = fonts->GetTextStats();

	ASSERT_GT(shaped.glyphsResolved, 0u);
	EXPECT_EQ(measured.glyphsResolved, shaped.glyphsResolved);
	EXPECT_EQ(measured.walks, 1u);
	EXPECT_EQ(measured.runsBuilt, 0u);
	EXPECT_EQ(shaped.runsBuilt, 1u);
	EXPECT_FLOAT_EQ(extent.x, run.extent.x);
}

TEST_F(UIFontFixture, EllipsizedRunFillsBudget) {
	constexpr f32 BUDGET = 120.0f;
	UI::IFontSystem* fonts = UISystem::GetFontSystem();
	const f32 glyph = fonts->MeasureText(Query("x")).x;
	ASSERT_GT(glyph, 1.0f);

	UI::ShapedRun shortTail;
	UI::ShapedRun longTail;
	fonts->ResetTextStats();
	ASSERT_TRUE(fonts->ShapeText(Query(str(40, 'x'), UI::TextOverflow::Ellipsis, BUDGET), shortTail));
	const u64 shortWalk = fonts->GetTextStats().glyphsResolved;
	fonts->ResetTextStats();
	ASSERT_TRUE(fonts->ShapeText(Query(str(4000, 'x'), UI::TextOverflow::Ellipsis, BUDGET), longTail));
	const u64 longWalk = fonts->GetTextStats().glyphsResolved;
	ASSERT_TRUE(shortTail.truncated);

	EXPECT_LE(shortTail.extent.x, BUDGET + 0.01f);
	EXPECT_GT(shortTail.extent.x, BUDGET - glyph - 1.0f);

	EXPECT_FLOAT_EQ(longTail.extent.x, shortTail.extent.x);
	EXPECT_FLOAT_EQ(longTail.extent.y, shortTail.extent.y);
	EXPECT_EQ(longTail.truncated, shortTail.truncated);
	EXPECT_EQ(longWalk, shortWalk) << longWalk << " glyphs against " << shortWalk;
}

TEST_F(UIFontFixture, MeasureMatchesShape) {
	UI::IFontSystem* fonts = UISystem::GetFontSystem();
	const str prefix = "the quick brown fox jumps over the lazy dog";
	for (UI::TextOverflow overflow : {UI::TextOverflow::Clip, UI::TextOverflow::Wrap, UI::TextOverflow::Ellipsis}) {
		for (f32 width : {0.0f, 120.0f, 4000.0f}) {
			UI::ShapedRun run;
			ASSERT_TRUE(fonts->ShapeText(Query(prefix, overflow, width), run));
			const Vec2 measured = fonts->MeasureText(Query(prefix, overflow, width));
			EXPECT_FLOAT_EQ(measured.x, run.extent.x) << "overflow " << static_cast<int>(overflow) << " at " << width;
			EXPECT_FLOAT_EQ(measured.y, run.extent.y) << "overflow " << static_cast<int>(overflow) << " at " << width;
		}
	}
}
