#include "TestsPCH.h"

#include "UITestUtil.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	struct LayoutCostFixture {
		MockFontSystem font;
		UI::Context ctx;
		UI::Widgets::NumberFieldParts field;
		UI::ElementHandle autoLabel;
		UI::ElementHandle farRow;

		explicit LayoutCostFixture(u32 rowsPerPanel) : ctx(MakeDesc(), &font) {
			UI::Style rootStyle;
			rootStyle.flexDirection = UI::FlexDirection::Row;
			rootStyle.alignItems = UI::Align::Stretch;
			ctx.SetStyle(ctx.GetRoot(), rootStyle);

			for (u32 panel = 0; panel < 4; ++panel) {
				UI::Style panelStyle;
				panelStyle.flexDirection = UI::FlexDirection::Column;
				panelStyle.alignItems = UI::Align::Stretch;
				panelStyle.flexGrow = 1.0f;
				panelStyle.flexShrink = 1.0f;
				panelStyle.height = UI::Length::Percent(1.0f);
				UI::ElementHandle column = MakeChild(ctx, ctx.GetRoot(), panelStyle);

				for (u32 row = 0; row < rowsPerPanel; ++row) {
					UI::Style rowStyle;
					rowStyle.flexDirection = UI::FlexDirection::Row;
					rowStyle.alignItems = UI::Align::Center;
					UI::ElementHandle rowHandle = MakeChild(ctx, column, rowStyle);

					UI::Style captionStyle;
					captionStyle.width = UI::Length::Pixel(90.0f);
					UI::ElementHandle caption = MakeChild(ctx, rowHandle, captionStyle);
					ctx.SetText(caption, {}, 12.0f, Color::White(), "Position X");

					if (panel == 0 && row == 0) {
						UI::Widgets::NumberFieldDesc desc;
						desc.value = 0.0;
						desc.decimals = 3;
						desc.fontSizePx = 12.0f;
						field = UI::Widgets::NumberField(ctx, rowHandle, desc);

						UI::ElementHandle trailing = MakeChild(ctx, rowHandle, UI::Style{});
						ctx.SetText(trailing, {}, 12.0f, Color::White(), "0.000");
						autoLabel = trailing;
					}
					if (panel == 3 && row + 1 == rowsPerPanel) {
						farRow = rowHandle;
					}
				}
			}
			Settle();
		}

		static UI::ContextDesc MakeDesc() {
			UI::ContextDesc desc;
			desc.sizePx = {1600, 900};
			desc.contentScale = 1.0f;
			return desc;
		}

		void Settle() {
			for (int i = 0; i < 4; ++i) {
				ctx.Update();
			}
		}

		UI::LayoutStats CostOfOneFrame() {
			ctx.ResetLayoutStats();
			ctx.Update();
			return ctx.GetLayoutStats();
		}
	};

	struct DepthChainFixture {
		MockFontSystem font;
		UI::Context ctx;

		DepthChainFixture(u32 depth, u32 leaves) : ctx(MakeDesc(), &font) {
			UI::ElementHandle deepest = ctx.GetRoot();
			for (u32 level = 0; level < depth; ++level) {
				UI::Style style;
				style.flexDirection = (level % 2 == 0) ? UI::FlexDirection::Row : UI::FlexDirection::Column;
				style.alignItems = UI::Align::Stretch;
				style.flexGrow = 1.0f;
				style.flexShrink = 1.0f;
				deepest = MakeChild(ctx, deepest, style);
			}
			for (u32 leaf = 0; leaf < leaves; ++leaf) {
				UI::Style rowStyle;
				rowStyle.flexDirection = UI::FlexDirection::Row;
				UI::ElementHandle row = MakeChild(ctx, deepest, rowStyle);
				UI::ElementHandle label = MakeChild(ctx, row, UI::Style{});
				ctx.SetText(label, {}, 12.0f, Color::White(), "Position X");
				UI::Style growStyle;
				growStyle.flexGrow = 1.0f;
				MakeChild(ctx, row, growStyle);
			}
			this->deepest = deepest;
		}

		UI::ElementHandle deepest;

		static UI::ContextDesc MakeDesc() {
			UI::ContextDesc desc;
			desc.sizePx = {2560, 1440};
			desc.contentScale = 1.0f;
			return desc;
		}

		UI::LayoutStats FirstPass() {
			ctx.ResetLayoutStats();
			ctx.Update();
			return ctx.GetLayoutStats();
		}
	};

	TEST(UILayoutCostTest, IdleFrameRunsNoLayoutAtAll) {
		LayoutCostFixture fixture(40);
		UI::LayoutStats stats = fixture.CostOfOneFrame();
		EXPECT_EQ(stats.passes, 0u);
		EXPECT_EQ(stats.measureWork, 0u);
		EXPECT_EQ(stats.arrangeWork, 0u);
	}

	TEST(UILayoutCostTest, RefreshingBoundValueDoesNotRelayoutContext) {
		LayoutCostFixture fixture(40);
		UI::Widgets::SetNumberFieldValue(fixture.ctx, fixture.field, 12.345, 3);
		UI::LayoutStats stats = fixture.CostOfOneFrame();

		EXPECT_LE(stats.measureWork, 2u);
		EXPECT_EQ(stats.arrangeWork, 0u);
	}

	TEST(UILayoutCostTest, RefreshCostIsIndependentOfTreeSize) {
		LayoutCostFixture small(20);
		LayoutCostFixture large(80);

		UI::Widgets::SetNumberFieldValue(small.ctx, small.field, 12.345, 3);
		UI::Widgets::SetNumberFieldValue(large.ctx, large.field, 12.345, 3);
		UI::LayoutStats smallStats = small.CostOfOneFrame();
		UI::LayoutStats largeStats = large.CostOfOneFrame();

		ASSERT_GT(large.ctx.GetLiveCount(), small.ctx.GetLiveCount() * 3);
		EXPECT_EQ(smallStats.measureWork, largeStats.measureWork);
		EXPECT_EQ(smallStats.arrangeWork, largeStats.arrangeWork);
	}

	TEST(UILayoutCostTest, Refreshing9ValuesCosts9NotTree) {
		LayoutCostFixture fixture(40);
		for (int i = 0; i < 9; ++i) {
			UI::Widgets::SetNumberFieldValue(fixture.ctx, fixture.field, 1.0 + i, 3);
		}
		UI::LayoutStats stats = fixture.CostOfOneFrame();
		EXPECT_LE(stats.measureWork, 2u);
		EXPECT_EQ(stats.arrangeWork, 0u);
	}

	TEST(UILayoutCostTest, ContentEditBesideStructuralOneStaysProportional) {
		LayoutCostFixture small(20);
		LayoutCostFixture large(80);

		auto mixedFrame = [](LayoutCostFixture& fixture) {
			UI::Widgets::SetNumberFieldValue(fixture.ctx, fixture.field, 12.345, 3);
			MakeChild(fixture.ctx, fixture.farRow, UI::Style{});
			return fixture.CostOfOneFrame();
		};
		UI::LayoutStats smallStats = mixedFrame(small);
		UI::LayoutStats largeStats = mixedFrame(large);

		ASSERT_GT(large.ctx.GetLiveCount(), small.ctx.GetLiveCount() * 3);
		EXPECT_EQ(smallStats.measureWork, largeStats.measureWork);
		EXPECT_EQ(smallStats.arrangeWork, largeStats.arrangeWork);
	}

	TEST(UILayoutCostTest, ContentSizedRelaysOut) {
		LayoutCostFixture fixture(20);
		f32 before = fixture.ctx.GetLayout(fixture.autoLabel).borderBox.w;

		fixture.ctx.SetTextString(fixture.autoLabel, "0.000000000");
		fixture.ctx.Update();

		f32 after = fixture.ctx.GetLayout(fixture.autoLabel).borderBox.w;
		EXPECT_GT(after, before);
	}

	TEST(UILayoutCostTest, GrownChildStillResizesWhenTextGrows) {
		MockFontSystem font;
		UI::ContextDesc desc;
		desc.sizePx = {600, 200};
		desc.contentScale = 1.0f;
		UI::Context ctx(desc, &font);

		UI::Style rowStyle;
		rowStyle.flexDirection = UI::FlexDirection::Row;
		rowStyle.width = UI::Length::Pixel(300.0f);
		rowStyle.height = UI::Length::Pixel(24.0f);
		UI::ElementHandle row = MakeChild(ctx, ctx.GetRoot(), rowStyle);

		UI::Style growStyle;
		growStyle.flexGrow = 1.0f;
		UI::ElementHandle left = MakeChild(ctx, row, growStyle);
		UI::ElementHandle right = MakeChild(ctx, row, growStyle);
		ctx.SetText(left, {}, 12.0f, Color::White(), "5");
		ctx.SetText(right, {}, 12.0f, Color::White(), "5");

		for (int i = 0; i < 3; ++i) {
			ctx.Update();
		}
		f32 leftBefore = ctx.GetLayout(left).borderBox.w;
		f32 rightBefore = ctx.GetLayout(right).borderBox.w;
		ASSERT_FLOAT_EQ(leftBefore, rightBefore);

		ctx.SetTextString(left, "5555555555");
		ctx.Update();

		EXPECT_GT(ctx.GetLayout(left).borderBox.w, leftBefore);
		EXPECT_LT(ctx.GetLayout(right).borderBox.w, rightBefore);
	}

	TEST(UILayoutCostTest, GrowingTextRelaysOut) {
		MockFontSystem font;
		UI::ContextDesc desc;
		desc.sizePx = {600, 200};
		desc.contentScale = 1.0f;
		UI::Context ctx(desc, &font);

		UI::Style rowStyle;
		rowStyle.flexDirection = UI::FlexDirection::Row;
		UI::ElementHandle panel = MakeChild(ctx, ctx.GetRoot(), rowStyle);
		UI::ElementHandle label = MakeChild(ctx, panel, UI::Style{});
		ctx.SetText(label, {}, 12.0f, Color::White(), "5");
		UI::ElementHandle other = MakeChild(ctx, ctx.GetRoot(), rowStyle);

		for (int i = 0; i < 3; ++i) {
			ctx.Update();
		}
		f32 before = ctx.GetLayout(label).borderBox.w;

		ctx.SetTextString(label, "5555555555");
		MakeChild(ctx, other, UI::Style{});
		ctx.Update();

		EXPECT_GT(ctx.GetLayout(label).borderBox.w, before);
	}

	TEST(UILayoutCostTest, RewrittenLabelsSettleSameFrame) {
		MockFontSystem font;
		UI::ContextDesc desc;
		desc.sizePx = {800, 400};
		desc.contentScale = 1.0f;
		UI::Context ctx(desc, &font);

		constexpr u32 COUNT = 6;
		UI::ElementHandle labels[COUNT];
		UI::Style panelStyle;
		panelStyle.flexDirection = UI::FlexDirection::Row;
		for (u32 i = 0; i < COUNT; ++i) {
			UI::ElementHandle panel = MakeChild(ctx, ctx.GetRoot(), panelStyle);
			labels[i] = MakeChild(ctx, panel, UI::Style{});
			ctx.SetText(labels[i], {}, 12.0f, Color::White(), "5");
		}
		for (int i = 0; i < 4; ++i) {
			ctx.Update();
		}

		for (u32 i = 0; i < COUNT; ++i) {
			ctx.SetTextString(labels[i], "5555555555");
		}
		ctx.Update();

		for (u32 i = 0; i < COUNT; ++i) {
			EXPECT_FLOAT_EQ(ctx.GetLayout(labels[i]).borderBox.w, 100.0f);
		}
	}

	TEST(UILayoutCostTest, RevisitedWidthRelaysSubtree) {
		MockFontSystem font;
		UI::ContextDesc desc;
		desc.sizePx = {400, 200};
		desc.contentScale = 1.0f;
		UI::Context ctx(desc, &font);

		UI::Style panelStyle;
		panelStyle.flexGrow = 1.0f;
		panelStyle.flexShrink = 1.0f;
		panelStyle.alignItems = UI::Align::Stretch;
		UI::ElementHandle panel = MakeChild(ctx, ctx.GetRoot(), panelStyle);

		UI::Style fillStyle;
		fillStyle.width = UI::Length::Percent(1.0f);
		fillStyle.height = UI::Length::Pixel(20.0f);
		UI::ElementHandle fill = MakeChild(ctx, panel, fillStyle);

		auto widthsAt = [&](i32 width) {
			ctx.Resize({width, 200});
			ctx.Update();
			return std::pair{ctx.GetLayout(panel).borderBox.w, ctx.GetLayout(fill).borderBox.w};
		};

		auto [firstPanel, firstFill] = widthsAt(400);
		ASSERT_NEAR(firstPanel, 400.0f, 0.01f);
		ASSERT_NEAR(firstFill, 400.0f, 0.01f);

		auto [wide, wideFill] = widthsAt(600);
		ASSERT_NEAR(wide, 600.0f, 0.01f);
		ASSERT_NEAR(wideFill, 600.0f, 0.01f);

		auto [backPanel, backFill] = widthsAt(400);
		EXPECT_NEAR(backPanel, 400.0f, 0.01f);
		EXPECT_NEAR(backFill, 400.0f, 0.01f);
	}

	TEST(UILayoutCostTest, ARevisitedCrossSizeRestretchesTheSubtree) {
		MockFontSystem font;
		UI::ContextDesc desc;
		desc.sizePx = {400, 200};
		desc.contentScale = 1.0f;
		UI::Context ctx(desc, &font);

		UI::Style rootStyle;
		rootStyle.flexDirection = UI::FlexDirection::Column;
		rootStyle.alignItems = UI::Align::Start;
		ctx.SetStyle(ctx.GetRoot(), rootStyle);

		UI::Style panelStyle;
		panelStyle.flexDirection = UI::FlexDirection::Column;
		panelStyle.alignItems = UI::Align::Stretch;
		UI::ElementHandle panel = MakeChild(ctx, ctx.GetRoot(), panelStyle);

		UI::ElementHandle ruler = MakeChild(ctx, panel, FixedBox(120.0f, 10.0f));
		UI::Style stretchStyle;
		stretchStyle.height = UI::Length::Pixel(20.0f);
		UI::ElementHandle stretched = MakeChild(ctx, panel, stretchStyle);
		UI::ElementHandle inner = MakeChild(ctx, stretched, [] {
			UI::Style style;
			style.width = UI::Length::Percent(1.0f);
			style.height = UI::Length::Pixel(10.0f);
			return style;
		}());

		auto widthsAt = [&](f32 rulerWidth) {
			ctx.SetStyle(ruler, FixedBox(rulerWidth, 10.0f));
			ctx.Update();
			return std::pair{ctx.GetLayout(stretched).borderBox.w, ctx.GetLayout(inner).borderBox.w};
		};

		auto [firstOuter, firstInner] = widthsAt(120.0f);
		ASSERT_NEAR(firstOuter, 120.0f, 0.01f);
		ASSERT_NEAR(firstInner, 120.0f, 0.01f);

		auto [wideOuter, wideInner] = widthsAt(200.0f);
		ASSERT_NEAR(wideOuter, 200.0f, 0.01f);
		ASSERT_NEAR(wideInner, 200.0f, 0.01f);

		auto [backOuter, backInner] = widthsAt(120.0f);
		EXPECT_NEAR(backOuter, 120.0f, 0.01f);
		EXPECT_NEAR(backInner, 120.0f, 0.01f);
	}

	TEST(UILayoutCostTest, QueryPublishesNoWrapWidth) {
		constexpr f32 OFFERED = 2000.0f;
		constexpr f32 WIDEST = 200.0f;

		WrapWidthRecorder font;
		UI::ContextDesc desc;
		desc.sizePx = {static_cast<i32>(OFFERED), 200};
		desc.contentScale = 1.0f;
		UI::Context ctx(desc, &font);

		UI::Style rootStyle;
		rootStyle.flexDirection = UI::FlexDirection::Column;
		rootStyle.alignItems = UI::Align::Start;
		ctx.SetStyle(ctx.GetRoot(), rootStyle);

		UI::Style panelStyle;
		panelStyle.flexDirection = UI::FlexDirection::Column;
		panelStyle.alignItems = UI::Align::Stretch;
		UI::ElementHandle panel = MakeChild(ctx, ctx.GetRoot(), panelStyle);

		UI::ElementHandle ruler = MakeChild(ctx, panel, FixedBox(120.0f, 10.0f));
		UI::ElementHandle label = MakeChild(ctx, panel, UI::Style{});
		ctx.SetText(label, {}, 12.0f, Color::White(), "Pos");

		auto layoutAt = [&](f32 rulerWidth) {
			ctx.SetStyle(ruler, FixedBox(rulerWidth, 10.0f));
			ctx.Update();
		};

		layoutAt(120.0f);
		layoutAt(WIDEST);
		layoutAt(120.0f);

		ctx.AddClass(panel, "perfProbe"_hash);
		ctx.Update();

		ASSERT_NEAR(ctx.GetLayout(label).borderBox.w, 120.0f, 0.01f);

		font.widths.clear();
		UI::RenderList list;
		ctx.BuildRenderList(list);

		for (f32 width : font.widths) {
			EXPECT_LE(width, WIDEST + 0.01f);
		}
	}

	TEST(UILayoutCostTest, HiddenBaseFollowsContent) {
		constexpr const char* SHORT = "0123456789012345678901234567890123456789";
		constexpr const char* LONG = "01234567890123456789012345678901234567890123456789"
									 "0123456789012345678901234567890123456789";

		auto build = [](UI::Context& ctx, const char* text) {
			UI::Style rootStyle;
			rootStyle.flexDirection = UI::FlexDirection::Row;
			rootStyle.alignItems = UI::Align::Start;
			ctx.SetStyle(ctx.GetRoot(), rootStyle);

			UI::Style blockStyle;
			blockStyle.flexDirection = UI::FlexDirection::Column;
			blockStyle.flexShrink = 1.0f;
			blockStyle.alignItems = UI::Align::Start;
			UI::ElementHandle block = MakeChild(ctx, ctx.GetRoot(), blockStyle);

			UI::Style labelStyle;
			labelStyle.height = UI::Length::Pixel(20.0f);
			UI::ElementHandle label = MakeChild(ctx, block, labelStyle);
			ctx.SetText(label, {}, 12.0f, Color::White(), text);

			MakeChild(ctx, ctx.GetRoot(), FixedBox(500.0f, 20.0f));
			return std::pair{block, label};
		};

		MockFontSystem font;
		UI::ContextDesc desc;
		desc.sizePx = {600, 200};
		desc.contentScale = 1.0f;
		UI::Context ctx(desc, &font);
		auto [block, label] = build(ctx, SHORT);
		ctx.Update();

		const UI::Rect labelBefore = ctx.GetLayout(label).borderBox;
		ctx.SetTextString(label, LONG);
		ctx.Update();
		ASSERT_EQ(ctx.GetLayout(label).borderBox, labelBefore);

		ctx.Resize({560, 200});
		ctx.Update();

		MockFontSystem freshFont;
		UI::ContextDesc freshDesc = desc;
		freshDesc.sizePx = {560, 200};
		UI::Context fresh(freshDesc, &freshFont);
		auto [freshBlock, freshLabel] = build(fresh, LONG);
		(void)freshLabel;
		fresh.Update();

		EXPECT_NEAR(ctx.GetLayout(block).borderBox.w, fresh.GetLayout(freshBlock).borderBox.w, 0.01f);
	}

	TEST(UILayoutCostTest, WiderChildStillMovesFollowingSibling) {
		LayoutCostFixture fixture(20);
		UI::ElementHandle sibling = fixture.field.root;
		f32 before = fixture.ctx.GetLayout(sibling).borderBox.x;

		UI::ElementHandle row = fixture.ctx.MakeHandle(
			fixture.ctx.GetElement(fixture.field.root)->parent);
		UI::ElementHandle caption = fixture.ctx.MakeHandle(
			fixture.ctx.GetElement(row)->firstChild);
		UI::Style wider;
		wider.width = UI::Length::Pixel(300.0f);
		fixture.ctx.SetStyle(caption, wider);
		fixture.ctx.Update();

		EXPECT_GT(fixture.ctx.GetLayout(sibling).borderBox.x, before);
	}

	TEST(UILayoutCostTest, LayoutIsStableAcrossRepeatedFrames) {
		LayoutCostFixture fixture(20);
		UI::Rect before = fixture.ctx.GetLayout(fixture.farRow).borderBox;
		for (int i = 0; i < 3; ++i) {
			UI::Widgets::SetNumberFieldValue(fixture.ctx, fixture.field, 5.0 + i, 3);
			fixture.ctx.Update();
		}
		EXPECT_EQ(fixture.ctx.GetLayout(fixture.farRow).borderBox, before);
	}

	TEST(UILayoutCostTest, StructuralEditArrangesSpineNotContext) {
		LayoutCostFixture fixture(40);
		u32 live = fixture.ctx.GetLiveCount();

		UI::Style taller;
		taller.height = UI::Length::Pixel(48.0f);
		fixture.ctx.SetStyle(fixture.farRow, taller);
		UI::LayoutStats stats = fixture.CostOfOneFrame();

		EXPECT_GT(stats.arrangeWork, 0u);
		EXPECT_LT(stats.arrangeWork, live / 3);
	}

	constexpr u64 MEASURE_WORK_PER_ELEMENT_AT_DEPTH12 = 3;

	TEST(UILayoutCostTest, MeasureWorkPerElementDoesNotGrow) {
		DepthChainFixture fixture(12, 40);
		u32 live = fixture.ctx.GetLiveCount();
		UI::LayoutStats stats = fixture.FirstPass();
		u64 perElement = stats.measureWork / live;

		EXPECT_LE(perElement, MEASURE_WORK_PER_ELEMENT_AT_DEPTH12);
	}

	TEST(UILayoutCostTest, MeasureWorkPerLevelDoesNotCompound) {
		DepthChainFixture shallow(10, 40);
		DepthChainFixture deep(12, 40);
		u64 shallowWork = shallow.FirstPass().measureWork;
		u64 deepWork = deep.FirstPass().measureWork;

		ASSERT_GT(shallowWork, 0u);
		f64 perTwoLevels = static_cast<f64>(deepWork) / static_cast<f64>(shallowWork);
		EXPECT_LE(perTwoLevels, 1.05);
	}

	TEST(UILayoutCostTest, ADeepChainStillFillsTheViewport) {
		DepthChainFixture fixture(12, 40);
		fixture.FirstPass();

		const UI::Rect deepest = fixture.ctx.GetLayout(fixture.deepest).borderBox;
		EXPECT_NEAR(deepest.w, 2560.0f, 0.51f);
		EXPECT_NEAR(deepest.h, 1440.0f, 0.51f);

		UI::ElementHandle row = fixture.ctx.MakeHandle(fixture.ctx.GetElement(fixture.deepest)->firstChild);
		EXPECT_NEAR(fixture.ctx.GetLayout(row).borderBox.w, 2560.0f, 0.51f);
	}

} // namespace
