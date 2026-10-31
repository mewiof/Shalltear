#include "TestsPCH.h"

#include "UITestUtil.h"

#include <chrono>
#include <format>
#include <iostream>

using namespace Shalltear;
using namespace UITest;

namespace {

	struct TextRowFixture {
		CountingFont font;
		UI::Context ctx;
		UI::Style rootStyle;
		UI::ElementHandle message;

		TextRowFixture() : ctx(MakeDesc(), &font) {
			rootStyle.flexDirection = UI::FlexDirection::Column;
			ctx.SetStyle(ctx.GetRoot(), rootStyle);

			UI::Style rowStyle;
			rowStyle.flexDirection = UI::FlexDirection::Row;
			rowStyle.width = UI::Length::Percent(1.0f);
			UI::ElementHandle row = MakeChild(ctx, ctx.GetRoot(), rowStyle);

			UI::Style tagStyle;
			tagStyle.width = UI::Length::Pixel(46.0f);
			UI::ElementHandle tag = MakeChild(ctx, row, tagStyle);
			ctx.SetText(tag, {}, 12.5f, Color::White(), "INFO");

			UI::Style messageStyle;
			messageStyle.flexGrow = 1.0f;
			message = MakeChild(ctx, row, messageStyle);
			ctx.SetText(message, {}, 12.5f, Color::White(), "[Editor] a console message of some length");

			ctx.Update();
		}

		static UI::ContextDesc MakeDesc() {
			UI::ContextDesc desc;
			desc.sizePx = {800, 600};
			desc.contentScale = 1.0f;
			return desc;
		}

		void DirtyLayoutWithoutChangingGeometry() {
			if (ctx.HasClass(ctx.GetRoot(), "perfProbe"_hash)) {
				ctx.RemoveClass(ctx.GetRoot(), "perfProbe"_hash);
			} else {
				ctx.AddClass(ctx.GetRoot(), "perfProbe"_hash);
			}
		}
	};

} // anonymous namespace

TEST(UITextCacheTest, RelayoutWithUnchangedTextDoesNotReshape) {
	TextRowFixture fixture;

	u32 before = fixture.font.Walks();
	for (int frame = 0; frame < 20; ++frame) {
		fixture.DirtyLayoutWithoutChangingGeometry();
		fixture.ctx.Update();
	}
	EXPECT_EQ(fixture.font.Walks(), before);
}

TEST(UITextCacheTest, ResizeReshapesBoundedWorkPerFrame) {
	TextRowFixture fixture;

	u32 before = fixture.font.Walks();
	constexpr int FRAMES = 100;
	for (int frame = 0; frame < FRAMES; ++frame) {
		fixture.ctx.Resize({700 + frame, 600});
		fixture.ctx.Update();
	}
	u32 shaped = fixture.font.Walks() - before;
	EXPECT_LE(shaped, static_cast<u32>(FRAMES) * 2)
		<< "resize re-shaped " << shaped << " times over " << FRAMES << " frames";
}

TEST(UITextCacheTest, CostReturnsToZeroAfterResizeStops) {
	TextRowFixture fixture;

	for (int frame = 0; frame < 200; ++frame) {
		fixture.ctx.Resize({700 + frame, 600});
		fixture.ctx.Update();
	}

	u32 afterDrag = fixture.font.Walks();
	for (int frame = 0; frame < 20; ++frame) {
		fixture.DirtyLayoutWithoutChangingGeometry();
		fixture.ctx.Update();
	}
	EXPECT_EQ(fixture.font.Walks(), afterDrag);
}

TEST(UITextCacheTest, OverflowRowsFixedFontCalls) {
	constexpr u32 ROWS = 500;
	u32 walksAtShortest = 0;
	for (u32 chars : {u32(10), u32(100), u32(800)}) {
		CountingFont font;
		UI::ContextDesc desc;
		desc.sizePx = {1600, 300};
		desc.contentScale = 1.0f;
		UI::Context ctx{desc, &font};
		ctx.LoadStyleSheet(".msg { text-overflow: ellipsis; }");

		UI::Style columnStyle;
		columnStyle.flexDirection = UI::FlexDirection::Column;
		columnStyle.width = UI::Length::Percent(1.0f);
		UI::ElementHandle column = MakeChild(ctx, ctx.GetRoot(), columnStyle);

		const str line(chars, 'x');
		for (u32 i = 0; i < ROWS; ++i) {
			UI::Style rowStyle;
			rowStyle.flexDirection = UI::FlexDirection::Row;
			rowStyle.width = UI::Length::Percent(1.0f);
			UI::ElementHandle row = MakeChild(ctx, column, rowStyle);

			UI::Style tagStyle;
			tagStyle.width = UI::Length::Pixel(60.0f);
			tagStyle.flexShrink = 0.0f;
			MakeChild(ctx, row, tagStyle);

			UI::Style messageStyle;
			messageStyle.flexGrow = 1.0f;
			UI::ElementHandle message = MakeChild(ctx, row, messageStyle);
			ctx.AddClass(message, StringHash("msg"));
			ctx.SetText(message, {}, 12.0f, Color::White(), line);
		}

		font.Reset();
		auto begin = std::chrono::steady_clock::now();
		ctx.Update();
		UI::RenderList list;
		ctx.BuildRenderList(list);
		f64 ms = std::chrono::duration<f64, std::milli>(std::chrono::steady_clock::now() - begin).count();

		std::cout << std::format("[M41] {} rows x {:<4} chars: {} walks ({} measures, {} shapes) | {:.3f} ms",
								 ROWS, chars, font.Walks(), font.measures, font.shapes, ms)
				  << std::endl;

		ASSERT_GT(font.Walks(), 0u);
		if (chars == 10) {
			walksAtShortest = font.Walks();
		} else {
			EXPECT_EQ(font.Walks(), walksAtShortest);
		}
	}
}

TEST(UITextCacheTest, PaintAloneShapesText) {
	TextRowFixture fixture;
	fixture.font.Reset();

	fixture.ctx.SetTextString(fixture.message, "[Editor] a different console message entirely");
	fixture.ctx.Update();

	EXPECT_GT(fixture.font.measures, 0u);
	EXPECT_EQ(fixture.font.shapes, 0u) << fixture.font.shapes;

	UI::RenderList list;
	fixture.ctx.BuildRenderList(list);
	EXPECT_GT(fixture.font.shapes, 0u);
}

TEST(UITextCacheTest, AMeasureLeavesThePaintedRunAlone) {
	TextRowFixture fixture;
	UI::RenderList list;
	fixture.ctx.BuildRenderList(list);
	fixture.font.Reset();

	fixture.ctx.Update();
	fixture.ctx.BuildRenderList(list);
	EXPECT_EQ(fixture.font.shapes, 0u);
}

TEST(UITextCacheTest, RewritingAnIdenticalStyleDoesNotRelayout) {
	TextRowFixture fixture;
	UI::Style unchanged = fixture.ctx.GetStyle(fixture.ctx.GetRoot());

	u32 before = fixture.font.Walks();
	for (int frame = 0; frame < 20; ++frame) {
		fixture.ctx.SetStyle(fixture.ctx.GetRoot(), unchanged);
		fixture.ctx.Update();
	}
	EXPECT_EQ(fixture.font.Walks(), before);

	UI::Style widened = unchanged;
	widened.padding = UI::Edges::All(12.0f);
	fixture.ctx.SetStyle(fixture.ctx.GetRoot(), widened);
	fixture.ctx.Update();
	EXPECT_GT(fixture.font.Walks(), before);
}
