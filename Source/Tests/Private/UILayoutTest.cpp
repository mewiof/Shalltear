#include "TestsPCH.h"

#include "UITestUtil.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	UI::ContextDesc Desc(i32 width = 400, i32 height = 300, f32 scale = 1.0f) {
		UI::ContextDesc desc;
		desc.sizePx = {width, height};
		desc.contentScale = scale;
		return desc;
	}

	void ExpectRect(const UI::Rect& rect, f32 x, f32 y, f32 w, f32 h) {
		EXPECT_NEAR(rect.x, x, 0.01f);
		EXPECT_NEAR(rect.y, y, 0.01f);
		EXPECT_NEAR(rect.w, w, 0.01f);
		EXPECT_NEAR(rect.h, h, 0.01f);
	}

} // anonymous namespace

TEST(UILayoutTest, RootFillsTheViewport) {
	UI::Context ctx(Desc(400, 300), nullptr);
	ctx.Update();
	ExpectRect(ctx.GetLayout(ctx.GetRoot()).borderBox, 0.0f, 0.0f, 400.0f, 300.0f);
}

TEST(UILayoutTest, RowPlacesFixedChildrenInOrder) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle root = ctx.GetRoot();
	UI::Style rowStyle;
	rowStyle.flexDirection = UI::FlexDirection::Row;
	rowStyle.alignItems = UI::Align::Start;
	ctx.SetStyle(root, rowStyle);

	UI::ElementHandle a = MakeChild(ctx, root, FixedBox(50.0f, 40.0f));
	UI::ElementHandle b = MakeChild(ctx, root, FixedBox(70.0f, 30.0f));
	ctx.Update();

	ExpectRect(ctx.GetLayout(a).borderBox, 0.0f, 0.0f, 50.0f, 40.0f);
	ExpectRect(ctx.GetLayout(b).borderBox, 50.0f, 0.0f, 70.0f, 30.0f);
}

TEST(UILayoutTest, ColumnStacksChildren) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle root = ctx.GetRoot();
	UI::Style columnStyle;
	columnStyle.flexDirection = UI::FlexDirection::Column;
	columnStyle.alignItems = UI::Align::Start;
	ctx.SetStyle(root, columnStyle);

	UI::ElementHandle a = MakeChild(ctx, root, FixedBox(50.0f, 40.0f));
	UI::ElementHandle b = MakeChild(ctx, root, FixedBox(70.0f, 30.0f));
	ctx.Update();

	ExpectRect(ctx.GetLayout(a).borderBox, 0.0f, 0.0f, 50.0f, 40.0f);
	ExpectRect(ctx.GetLayout(b).borderBox, 0.0f, 40.0f, 70.0f, 30.0f);
}

TEST(UILayoutTest, FlexGrowDistributesProportionally) {
	UI::Context ctx(Desc(400, 300), nullptr);
	UI::ElementHandle root = ctx.GetRoot();

	auto growBox = [](f32 grow) {
		UI::Style style;
		style.height = UI::Length::Pixel(20.0f);
		style.flexGrow = grow;
		return style;
	};
	UI::ElementHandle a = MakeChild(ctx, root, growBox(1.0f));
	UI::ElementHandle b = MakeChild(ctx, root, growBox(2.0f));
	UI::ElementHandle c = MakeChild(ctx, root, growBox(1.0f));
	ctx.Update();

	EXPECT_NEAR(ctx.GetLayout(a).borderBox.w, 100.0f, 0.5f);
	EXPECT_NEAR(ctx.GetLayout(b).borderBox.w, 200.0f, 0.5f);
	EXPECT_NEAR(ctx.GetLayout(c).borderBox.w, 100.0f, 0.5f);
	EXPECT_NEAR(ctx.GetLayout(b).borderBox.x, 100.0f, 0.5f);
}

TEST(UILayoutTest, JustifyContentOffsetsChildren) {
	auto runCase = [](UI::Align justify) {
		UI::Context ctx(Desc(400, 100), nullptr);
		UI::ElementHandle root = ctx.GetRoot();
		UI::Style rootStyle;
		rootStyle.justifyContent = justify;
		rootStyle.alignItems = UI::Align::Start;
		ctx.SetStyle(root, rootStyle);
		UI::ElementHandle a = MakeChild(ctx, root, FixedBox(50.0f, 20.0f));
		UI::ElementHandle b = MakeChild(ctx, root, FixedBox(50.0f, 20.0f));
		ctx.Update();
		return std::pair{ctx.GetLayout(a).borderBox.x, ctx.GetLayout(b).borderBox.x};
	};

	{
		auto [ax, bx] = runCase(UI::Align::Center);
		EXPECT_NEAR(ax, 150.0f, 0.5f);
		EXPECT_NEAR(bx, 200.0f, 0.5f);
	}
	{
		auto [ax, bx] = runCase(UI::Align::End);
		EXPECT_NEAR(ax, 300.0f, 0.5f);
		EXPECT_NEAR(bx, 350.0f, 0.5f);
	}
	{
		auto [ax, bx] = runCase(UI::Align::SpaceBetween);
		EXPECT_NEAR(ax, 0.0f, 0.5f);
		EXPECT_NEAR(bx, 350.0f, 0.5f);
	}
	{
		auto [ax, bx] = runCase(UI::Align::SpaceAround);
		EXPECT_NEAR(ax, 75.0f, 0.5f);
		EXPECT_NEAR(bx, 275.0f, 0.5f);
	}
}

TEST(UILayoutTest, JustifyPicksLostEnd) {
	auto runCase = [](UI::Align justify, UI::Overflow overflow) {
		UI::Context ctx(Desc(100, 100), nullptr);
		UI::ElementHandle root = ctx.GetRoot();
		UI::Style rootStyle;
		rootStyle.justifyContent = justify;
		rootStyle.alignItems = UI::Align::Start;
		rootStyle.overflow = overflow;
		ctx.SetStyle(root, rootStyle);
		UI::Style child = FixedBox(80.0f, 20.0f);
		child.flexShrink = 0.0f;
		UI::ElementHandle a = MakeChild(ctx, root, child);
		UI::ElementHandle b = MakeChild(ctx, root, child);
		ctx.Update();
		return std::pair{ctx.GetLayout(a).borderBox.x, ctx.GetLayout(b).borderBox.x};
	};

	{
		auto [ax, bx] = runCase(UI::Align::Start, UI::Overflow::Hidden);
		EXPECT_NEAR(ax, 0.0f, 0.5f);
		EXPECT_NEAR(bx, 80.0f, 0.5f);
	}
	{
		auto [ax, bx] = runCase(UI::Align::End, UI::Overflow::Hidden);
		EXPECT_NEAR(ax, -60.0f, 0.5f);
		EXPECT_NEAR(bx, 20.0f, 0.5f);
	}
	{
		auto [ax, bx] = runCase(UI::Align::Center, UI::Overflow::Hidden);
		EXPECT_NEAR(ax, -30.0f, 0.5f);
		EXPECT_NEAR(bx, 50.0f, 0.5f);
	}
	{
		auto [ax, bx] = runCase(UI::Align::SpaceBetween, UI::Overflow::Hidden);
		EXPECT_NEAR(ax, 0.0f, 0.5f);
		EXPECT_NEAR(bx, 80.0f, 0.5f);
	}
	{
		auto [ax, bx] = runCase(UI::Align::SpaceAround, UI::Overflow::Hidden);
		EXPECT_NEAR(ax, -30.0f, 0.5f);
		EXPECT_NEAR(bx, 50.0f, 0.5f);
	}
}

TEST(UILayoutTest, OverflowingScrollContainerKeepsStartEdge) {
	UI::Context ctx(Desc(100, 100), nullptr);
	UI::ElementHandle root = ctx.GetRoot();
	UI::Style rootStyle;
	rootStyle.justifyContent = UI::Align::End;
	rootStyle.alignItems = UI::Align::Start;
	rootStyle.overflow = UI::Overflow::Scroll;
	ctx.SetStyle(root, rootStyle);
	UI::Style child = FixedBox(80.0f, 20.0f);
	child.flexShrink = 0.0f;
	UI::ElementHandle a = MakeChild(ctx, root, child);
	UI::ElementHandle b = MakeChild(ctx, root, child);
	ctx.Update();

	EXPECT_NEAR(ctx.GetLayout(a).borderBox.x, 0.0f, 0.5f);
	EXPECT_NEAR(ctx.GetLayout(b).borderBox.x, 80.0f, 0.5f);
	EXPECT_NEAR(ctx.GetElement(root)->maxScroll.x, 60.0f, 0.5f);
}

TEST(UILayoutTest, ShrunkContentUsesClampedOffset) {
	UI::Context ctx(Desc(400, 300), nullptr);
	UI::Style scrollStyle;
	scrollStyle.flexDirection = UI::FlexDirection::Column;
	scrollStyle.overflow = UI::Overflow::Scroll;
	scrollStyle.width = UI::Length::Pixel(200.0f);
	scrollStyle.height = UI::Length::Pixel(100.0f);
	UI::ElementHandle scroller = MakeChild(ctx, ctx.GetRoot(), scrollStyle);
	Vector<UI::ElementHandle> rows{GetMemoryResource(MemTag::Debug)};
	for (int i = 0; i < 10; ++i) {
		rows.push_back(MakeChild(ctx, scroller, FixedBox(200.0f, 40.0f)));
	}
	ctx.Update();
	ctx.SetScrollOffset(scroller, {0.0f, 300.0f});
	ctx.Update();
	ASSERT_NEAR(ctx.GetElement(scroller)->scrollOffset.y, 300.0f, 0.01f);
	const UI::Rect view = ctx.GetLayout(scroller).content;

	for (int i = 9; i >= 5; --i) {
		ctx.DestroyElement(rows[i]);
	}
	ctx.Update();
	ASSERT_NEAR(ctx.GetElement(scroller)->scrollOffset.y, 100.0f, 0.01f);
	const UI::Rect last = ctx.GetLayout(rows[4]).borderBox;
	EXPECT_NEAR(last.y + last.h, view.y + view.h, 0.5f);

	for (int i = 4; i >= 2; --i) {
		ctx.DestroyElement(rows[i]);
	}
	ctx.Update();
	ASSERT_NEAR(ctx.GetElement(scroller)->scrollOffset.y, 0.0f, 0.01f);
	EXPECT_NEAR(ctx.GetLayout(rows[0]).borderBox.y, view.y, 0.5f);
}

TEST(UILayoutTest, AlignItemsAndAlignSelf) {
	UI::Context ctx(Desc(400, 100), nullptr);
	UI::ElementHandle root = ctx.GetRoot();
	UI::Style rootStyle;
	rootStyle.alignItems = UI::Align::Center;
	ctx.SetStyle(root, rootStyle);

	UI::ElementHandle centered = MakeChild(ctx, root, FixedBox(40.0f, 20.0f));
	UI::Style endStyle = FixedBox(40.0f, 20.0f);
	endStyle.alignSelf = UI::Align::End;
	UI::ElementHandle endAligned = MakeChild(ctx, root, endStyle);
	UI::Style stretchStyle;
	stretchStyle.width = UI::Length::Pixel(40.0f);
	stretchStyle.alignSelf = UI::Align::Stretch;
	UI::ElementHandle stretched = MakeChild(ctx, root, stretchStyle);
	ctx.Update();

	EXPECT_NEAR(ctx.GetLayout(centered).borderBox.y, 40.0f, 0.5f);
	EXPECT_NEAR(ctx.GetLayout(endAligned).borderBox.y, 80.0f, 0.5f);
	EXPECT_NEAR(ctx.GetLayout(stretched).borderBox.h, 100.0f, 0.5f);
}

TEST(UILayoutTest, PercentResolvesAgainstTheParent) {
	UI::Context ctx(Desc(400, 300), nullptr);
	UI::ElementHandle root = ctx.GetRoot();
	UI::Style halfStyle;
	halfStyle.width = UI::Length::Percent(0.5f);
	halfStyle.height = UI::Length::Percent(0.25f);
	UI::ElementHandle half = MakeChild(ctx, root, halfStyle);
	ctx.Update();
	ExpectRect(ctx.GetLayout(half).borderBox, 0.0f, 0.0f, 200.0f, 75.0f);
}

TEST(UILayoutTest, PercentChildDoesNotSizeAutoParent) {
	UI::Context ctx(Desc(400, 300), nullptr);
	UI::Style boxStyle;
	boxStyle.flexDirection = UI::FlexDirection::Column;
	boxStyle.alignItems = UI::Align::Stretch;
	UI::ElementHandle box = MakeChild(ctx, ctx.GetRoot(), boxStyle);

	UI::Style rowStyle;
	rowStyle.width = UI::Length::Percent(1.0f);
	UI::ElementHandle row = MakeChild(ctx, box, rowStyle);
	MakeChild(ctx, row, FixedBox(60.0f, 20.0f));
	ctx.Update();

	EXPECT_NEAR(ctx.GetLayout(box).borderBox.w, 60.0f, 0.01f);
	EXPECT_NEAR(ctx.GetLayout(row).borderBox.w, 60.0f, 0.01f);
}

TEST(UILayoutTest, GrowChildrenDoNotInflateAutoContainer) {
	UI::Context ctx(Desc(400, 300), nullptr);
	UI::Style rowStyle;
	rowStyle.flexDirection = UI::FlexDirection::Row;
	rowStyle.width = UI::Length::Percent(1.0f);
	rowStyle.height = UI::Length::Pixel(20.0f);
	UI::ElementHandle row = MakeChild(ctx, ctx.GetRoot(), rowStyle);

	MakeChild(ctx, row, FixedBox(110.0f, 20.0f));

	UI::Style vectorStyle;
	vectorStyle.flexDirection = UI::FlexDirection::Row;
	vectorStyle.flexGrow = 1.0f;
	UI::ElementHandle vector = MakeChild(ctx, row, vectorStyle);

	UI::ElementHandle components[3]{};
	for (u32 i = 0; i < 3; ++i) {
		UI::Style componentStyle;
		componentStyle.flexGrow = 1.0f;
		componentStyle.minWidth = UI::Length::Pixel(40.0f);
		componentStyle.height = UI::Length::Pixel(20.0f);
		components[i] = MakeChild(ctx, vector, componentStyle);
	}
	ctx.Update();

	ExpectRect(ctx.GetLayout(vector).borderBox, 110.0f, 0.0f, 290.0f, 20.0f);
	for (u32 i = 0; i < 3; ++i) {
		const UI::Rect& box = ctx.GetLayout(components[i]).borderBox;
		EXPECT_NEAR(box.w, 290.0f / 3.0f, 0.51f);
		EXPECT_LE(box.x + box.w, 400.01f);
	}
}

TEST(UILayoutTest, FlexBaseIsChildsMaxContentSize) {
	MockFontSystem font;
	UI::Context ctx(Desc(300, 100), &font);
	UI::Style rowStyle;
	rowStyle.flexDirection = UI::FlexDirection::Row;
	rowStyle.alignItems = UI::Align::Start;
	ctx.SetStyle(ctx.GetRoot(), rowStyle);

	UI::Style textStyle;
	textStyle.height = UI::Length::Pixel(20.0f);
	UI::ElementHandle text = MakeChild(ctx, ctx.GetRoot(), textStyle);
	ctx.SetText(text, {}, 12.0f, Color::White(), Shalltear::String(50, '0', GetMemoryResource(MemTag::Debug)));

	UI::ElementHandle box = MakeChild(ctx, ctx.GetRoot(), FixedBox(100.0f, 20.0f));
	ctx.Update();

	EXPECT_NEAR(ctx.GetLayout(text).borderBox.w, 250.0f, 0.01f);
	EXPECT_NEAR(ctx.GetLayout(box).borderBox.w, 50.0f, 0.01f);
}

TEST(UILayoutTest, PercentUsesGrownSize) {
	auto measureFill = [](f32 fraction) {
		UI::Context ctx(Desc(400, 300), nullptr);
		UI::Style rowStyle;
		rowStyle.flexDirection = UI::FlexDirection::Row;
		ctx.SetStyle(ctx.GetRoot(), rowStyle);

		MakeChild(ctx, ctx.GetRoot(), FixedBox(100.0f, 20.0f));

		UI::Style trackStyle;
		trackStyle.flexGrow = 1.0f;
		trackStyle.height = UI::Length::Pixel(14.0f);
		UI::ElementHandle track = MakeChild(ctx, ctx.GetRoot(), trackStyle);

		UI::Style fillStyle;
		fillStyle.width = UI::Length::Percent(fraction);
		fillStyle.height = UI::Length::Percent(1.0f);
		UI::ElementHandle fill = MakeChild(ctx, track, fillStyle);
		ctx.Update();
		return std::pair{ctx.GetLayout(track).borderBox.w, ctx.GetLayout(fill).borderBox.w};
	};

	for (f32 fraction : {0.0f, 0.25f, 0.5f, 0.65f, 0.9f, 1.0f}) {
		auto [trackWidth, fillWidth] = measureFill(fraction);
		EXPECT_NEAR(trackWidth, 300.0f, 0.01f) << "fraction " << fraction;
		EXPECT_NEAR(fillWidth, 300.0f * fraction, 0.01f) << "fraction " << fraction;
	}
}

TEST(UILayoutTest, StretchedChildKeepsResolvedMainSize) {
	UI::Context ctx(Desc(600, 200), nullptr);

	UI::Style rowStyle;
	rowStyle.flexDirection = UI::FlexDirection::Row;
	rowStyle.width = UI::Length::Pixel(300.0f);
	rowStyle.alignSelf = UI::Align::Start;
	UI::ElementHandle row = MakeChild(ctx, ctx.GetRoot(), rowStyle);

	UI::Style growStyle;
	growStyle.flexGrow = 1.0f;
	UI::ElementHandle grower = MakeChild(ctx, row, growStyle);
	UI::ElementHandle inner = MakeChild(ctx, grower, FixedBox(10.0f, 20.0f));

	ctx.Update();

	ASSERT_NEAR(ctx.GetLayout(row).borderBox.w, 300.0f, 0.01f);
	ASSERT_NEAR(ctx.GetLayout(row).borderBox.h, 20.0f, 0.01f);
	EXPECT_NEAR(ctx.GetLayout(grower).borderBox.w, 300.0f, 0.01f);
	EXPECT_NEAR(ctx.GetLayout(grower).borderBox.h, 20.0f, 0.01f);
	EXPECT_NEAR(ctx.GetLayout(inner).borderBox.w, 10.0f, 0.01f);
}

TEST(UILayoutTest, StretchedChildKeepsMainSize) {
	UI::Context ctx(Desc(600, 400), nullptr);

	UI::Style columnStyle;
	columnStyle.flexDirection = UI::FlexDirection::Column;
	columnStyle.height = UI::Length::Pixel(300.0f);
	columnStyle.alignSelf = UI::Align::Start;
	UI::ElementHandle column = MakeChild(ctx, ctx.GetRoot(), columnStyle);

	UI::Style growStyle;
	growStyle.flexGrow = 1.0f;
	UI::ElementHandle grower = MakeChild(ctx, column, growStyle);
	MakeChild(ctx, grower, FixedBox(20.0f, 10.0f));

	ctx.Update();

	ASSERT_NEAR(ctx.GetLayout(column).borderBox.w, 20.0f, 0.01f);
	EXPECT_NEAR(ctx.GetLayout(grower).borderBox.h, 300.0f, 0.01f);
	EXPECT_NEAR(ctx.GetLayout(grower).borderBox.w, 20.0f, 0.01f);
}

TEST(UILayoutTest, FixedSiblingKeepsSpaceBesideGrowViewport) {
	UI::Context ctx(Desc(400, 300), nullptr);
	UI::Style columnStyle;
	columnStyle.flexDirection = UI::FlexDirection::Column;
	columnStyle.alignItems = UI::Align::Stretch;
	ctx.SetStyle(ctx.GetRoot(), columnStyle);

	UI::Style scrollRootStyle;
	scrollRootStyle.flexDirection = UI::FlexDirection::Row;
	scrollRootStyle.height = UI::Length::Pixel(200.0f);
	UI::ElementHandle scrollRoot = MakeChild(ctx, ctx.GetRoot(), scrollRootStyle);

	UI::Style viewportStyle;
	viewportStyle.flexDirection = UI::FlexDirection::Column;
	viewportStyle.overflow = UI::Overflow::Scroll;
	viewportStyle.flexGrow = 1.0f;
	UI::ElementHandle viewport = MakeChild(ctx, scrollRoot, viewportStyle);

	UI::Style trackStyle;
	trackStyle.width = UI::Length::Pixel(10.0f);
	UI::ElementHandle track = MakeChild(ctx, scrollRoot, trackStyle);

	UI::Style rowStyle;
	rowStyle.width = UI::Length::Percent(1.0f);
	rowStyle.height = UI::Length::Pixel(30.0f);
	UI::ElementHandle contentRow = MakeChild(ctx, viewport, rowStyle);
	ctx.Update();

	EXPECT_NEAR(ctx.GetLayout(viewport).borderBox.w, 390.0f, 0.01f);
	EXPECT_NEAR(ctx.GetLayout(contentRow).borderBox.w, 390.0f, 0.01f);
	ExpectRect(ctx.GetLayout(track).borderBox, 390.0f, 0.0f, 10.0f, 200.0f);
}

TEST(UILayoutTest, OverflowShrinksItemsInProportionToSize) {
	UI::Context ctx(Desc(400, 300), nullptr);
	UI::Style rowStyle;
	rowStyle.flexDirection = UI::FlexDirection::Row;
	ctx.SetStyle(ctx.GetRoot(), rowStyle);

	UI::ElementHandle wide = MakeChild(ctx, ctx.GetRoot(), FixedBox(400.0f, 20.0f));
	UI::ElementHandle narrow = MakeChild(ctx, ctx.GetRoot(), FixedBox(200.0f, 20.0f));
	ctx.Update();

	EXPECT_NEAR(ctx.GetLayout(wide).borderBox.w, 400.0f - 400.0f / 3.0f, 0.6f);
	EXPECT_NEAR(ctx.GetLayout(narrow).borderBox.w, 200.0f - 200.0f / 3.0f, 0.6f);
	EXPECT_NEAR(ctx.GetLayout(narrow).borderBox.x + ctx.GetLayout(narrow).borderBox.w, 400.0f, 0.6f);
}

TEST(UILayoutTest, ShrinkFreezesAtMinAndRedistributes) {
	UI::Context ctx(Desc(400, 300), nullptr);
	UI::Style rowStyle;
	rowStyle.flexDirection = UI::FlexDirection::Row;
	ctx.SetStyle(ctx.GetRoot(), rowStyle);

	UI::Style flooredStyle = FixedBox(300.0f, 20.0f);
	flooredStyle.minWidth = UI::Length::Pixel(250.0f);
	UI::ElementHandle floored = MakeChild(ctx, ctx.GetRoot(), flooredStyle);
	UI::ElementHandle free = MakeChild(ctx, ctx.GetRoot(), FixedBox(300.0f, 20.0f));
	ctx.Update();

	EXPECT_NEAR(ctx.GetLayout(floored).borderBox.w, 250.0f, 0.6f);
	EXPECT_NEAR(ctx.GetLayout(free).borderBox.w, 150.0f, 0.6f);
}

TEST(UILayoutTest, ShrinkZeroPinsAndUnfittableContentOverflows) {
	UI::Context ctx(Desc(400, 300), nullptr);
	UI::Style rowStyle;
	rowStyle.flexDirection = UI::FlexDirection::Row;
	ctx.SetStyle(ctx.GetRoot(), rowStyle);

	UI::Style pinnedStyle = FixedBox(300.0f, 20.0f);
	pinnedStyle.flexShrink = 0.0f;
	UI::ElementHandle pinned = MakeChild(ctx, ctx.GetRoot(), pinnedStyle);
	UI::Style flooredStyle = FixedBox(300.0f, 20.0f);
	flooredStyle.minWidth = UI::Length::Pixel(250.0f);
	UI::ElementHandle floored = MakeChild(ctx, ctx.GetRoot(), flooredStyle);
	ctx.Update();

	EXPECT_NEAR(ctx.GetLayout(pinned).borderBox.w, 300.0f, 0.01f);
	EXPECT_NEAR(ctx.GetLayout(floored).borderBox.w, 250.0f, 0.01f);
}

TEST(UILayoutTest, SameRowGrowsAndShrinksWithContainer) {
	struct Widths {
		f32 label = 0.0f;
		f32 fields = 0.0f;
		f32 right = 0.0f;
	};
	auto measure = [](i32 contextWidth, bool pinLabel) {
		UI::Context ctx(Desc(contextWidth, 200), nullptr);
		UI::Style rowStyle;
		rowStyle.flexDirection = UI::FlexDirection::Row;
		ctx.SetStyle(ctx.GetRoot(), rowStyle);

		UI::Style labelStyle = FixedBox(110.0f, 20.0f);
		labelStyle.flexShrink = pinLabel ? 0.0f : 1.0f;
		UI::ElementHandle label = MakeChild(ctx, ctx.GetRoot(), labelStyle);

		UI::ElementHandle fields[3]{};
		for (u32 i = 0; i < 3; ++i) {
			UI::Style fieldStyle = FixedBox(80.0f, 20.0f);
			fieldStyle.flexGrow = 1.0f;
			fieldStyle.minWidth = UI::Length::Pixel(40.0f);
			fields[i] = MakeChild(ctx, ctx.GetRoot(), fieldStyle);
		}
		ctx.Update();

		Widths out;
		out.label = ctx.GetLayout(label).borderBox.w;
		for (UI::ElementHandle field : fields) {
			out.fields += ctx.GetLayout(field).borderBox.w;
		}
		out.right = ctx.GetLayout(fields[2]).borderBox.x + ctx.GetLayout(fields[2]).borderBox.w;
		return out;
	};

	Widths roomy = measure(500, false);
	EXPECT_NEAR(roomy.label, 110.0f, 0.6f);
	EXPECT_NEAR(roomy.fields, 390.0f, 0.6f);
	EXPECT_NEAR(roomy.right, 500.0f, 0.6f);

	Widths tight = measure(300, false);
	EXPECT_NEAR(tight.label, 110.0f - 50.0f * 110.0f / 350.0f, 0.6f);
	EXPECT_NEAR(tight.fields, 240.0f - 50.0f * 240.0f / 350.0f, 0.6f);
	EXPECT_NEAR(tight.right, 300.0f, 0.6f);

	Widths pinned = measure(300, true);
	EXPECT_NEAR(pinned.label, 110.0f, 0.6f);
	EXPECT_NEAR(pinned.fields, 190.0f, 0.6f);
	EXPECT_NEAR(pinned.right, 300.0f, 0.6f);
}

TEST(UILayoutTest, MinMaxClampSizes) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle root = ctx.GetRoot();
	UI::Style clamped = FixedBox(500.0f, 10.0f);
	clamped.maxWidth = UI::Length::Pixel(120.0f);
	clamped.minHeight = UI::Length::Pixel(60.0f);
	UI::ElementHandle box = MakeChild(ctx, root, clamped);
	ctx.Update();
	EXPECT_NEAR(ctx.GetLayout(box).borderBox.w, 120.0f, 0.01f);
	EXPECT_NEAR(ctx.GetLayout(box).borderBox.h, 60.0f, 0.01f);
}

TEST(UILayoutTest, BoxModelNesting) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle root = ctx.GetRoot();
	UI::Style boxStyle = FixedBox(200.0f, 100.0f);
	boxStyle.margin = UI::Edges::All(10.0f);
	boxStyle.borderWidth = UI::Edges::All(2.0f);
	boxStyle.padding = UI::Edges::All(8.0f);
	UI::ElementHandle box = MakeChild(ctx, root, boxStyle);
	ctx.Update();

	const UI::LayoutData& layout = ctx.GetLayout(box);
	ExpectRect(layout.borderBox, 10.0f, 10.0f, 200.0f, 100.0f);
	ExpectRect(layout.marginBox, 0.0f, 0.0f, 220.0f, 120.0f);
	ExpectRect(layout.paddingBox, 12.0f, 12.0f, 196.0f, 96.0f);
	ExpectRect(layout.content, 20.0f, 20.0f, 180.0f, 80.0f);
}

TEST(UILayoutTest, AbsolutePositioning) {
	UI::Context ctx(Desc(400, 300), nullptr);
	UI::ElementHandle root = ctx.GetRoot();

	UI::Style corner = FixedBox(50.0f, 40.0f);
	corner.position = UI::Position::Absolute;
	corner.left = UI::Length::Pixel(30.0f);
	corner.top = UI::Length::Pixel(20.0f);
	UI::ElementHandle topLeft = MakeChild(ctx, root, corner);

	UI::Style opposite = FixedBox(50.0f, 40.0f);
	opposite.position = UI::Position::Absolute;
	opposite.right = UI::Length::Pixel(10.0f);
	opposite.bottom = UI::Length::Pixel(10.0f);
	UI::ElementHandle bottomRight = MakeChild(ctx, root, opposite);

	UI::Style stretch;
	stretch.position = UI::Position::Absolute;
	stretch.left = UI::Length::Pixel(100.0f);
	stretch.right = UI::Length::Pixel(100.0f);
	stretch.top = UI::Length::Pixel(0.0f);
	stretch.height = UI::Length::Pixel(10.0f);
	UI::ElementHandle stretched = MakeChild(ctx, root, stretch);
	ctx.Update();

	ExpectRect(ctx.GetLayout(topLeft).borderBox, 30.0f, 20.0f, 50.0f, 40.0f);
	ExpectRect(ctx.GetLayout(bottomRight).borderBox, 340.0f, 250.0f, 50.0f, 40.0f);
	ExpectRect(ctx.GetLayout(stretched).borderBox, 100.0f, 0.0f, 200.0f, 10.0f);
}

TEST(UILayoutTest, ContentScaleMultipliesEveryLength) {
	UI::Context ctx(Desc(400, 300, 2.0f), nullptr);
	UI::ElementHandle root = ctx.GetRoot();
	UI::Style boxStyle = FixedBox(50.0f, 40.0f);
	boxStyle.margin = UI::Edges::All(5.0f);
	UI::ElementHandle box = MakeChild(ctx, root, boxStyle);
	ctx.Update();

	ExpectRect(ctx.GetLayout(box).borderBox, 10.0f, 10.0f, 100.0f, 80.0f);

	ctx.SetContentScale(1.0f);
	ctx.Update();
	ExpectRect(ctx.GetLayout(box).borderBox, 5.0f, 5.0f, 50.0f, 40.0f);
}

TEST(UILayoutTest, DecorationEdgesResolveToWholePhysicalPixels) {
	UI::Context ctx(Desc(400, 300, 1.25f), nullptr);
	UI::Style boxStyle = FixedBox(100.0f, 50.0f);
	boxStyle.padding = UI::Edges::All(1.0f);
	boxStyle.borderWidth = UI::Edges::All(3.0f);
	boxStyle.margin = {2.0f, 6.0f, 2.0f, 6.0f};
	UI::ElementHandle box = MakeChild(ctx, ctx.GetRoot(), boxStyle);
	ctx.Update();

	const UI::LayoutData& layout = ctx.GetLayout(box);
	ExpectRect(layout.borderBox, 3.0f, 8.0f, 125.0f, 62.5f);
	ExpectRect(layout.paddingBox, 7.0f, 12.0f, 117.0f, 54.5f);
	ExpectRect(layout.content, 8.0f, 13.0f, 115.0f, 52.5f);
}

TEST(UILayoutTest, InsetSurvivesSnapping) {
	for (i32 step = 0; step < 20; ++step) {
		const f32 offset = 0.05f * static_cast<f32>(step);
		UI::Context ctx(Desc(400, 300, 1.25f), nullptr);

		UI::Style outerStyle = FixedBox(200.0f, 40.0f);
		outerStyle.position = UI::Position::Absolute;
		outerStyle.padding = UI::Edges::All(1.0f);
		UI::ElementHandle outer = MakeChild(ctx, ctx.GetRoot(), outerStyle);

		UI::Style nudged = ctx.GetStyle(outer);
		nudged.left = UI::Length::Pixel(10.0f + offset);
		ctx.SetStyle(outer, nudged);
		UI::ElementHandle inner = MakeChild(ctx, outer, FixedBox(50.0f, 20.0f));
		ctx.Update();

		const UI::Rect parentBox = UI::SnapRectToPixels(ctx.GetLayout(outer).borderBox);
		const UI::Rect childBox = UI::SnapRectToPixels(ctx.GetLayout(inner).borderBox);
		EXPECT_EQ(childBox.x - parentBox.x, 1.0f) << offset;
		EXPECT_EQ(childBox.y - parentBox.y, 1.0f) << offset;
	}
}

TEST(UILayoutTest, ScrollContainerComputesMaxScroll) {
	UI::Context ctx(Desc(200, 100), nullptr);
	UI::ElementHandle root = ctx.GetRoot();
	UI::Style scrollStyle;
	scrollStyle.flexDirection = UI::FlexDirection::Column;
	scrollStyle.overflow = UI::Overflow::Scroll;
	scrollStyle.width = UI::Length::Pixel(200.0f);
	scrollStyle.height = UI::Length::Pixel(100.0f);
	UI::ElementHandle scroller = MakeChild(ctx, root, scrollStyle);
	for (int i = 0; i < 5; ++i) {
		MakeChild(ctx, scroller, FixedBox(50.0f, 40.0f));
	}
	ctx.Update();

	EXPECT_NEAR(ctx.GetElement(scroller)->maxScroll.y, 100.0f, 0.5f);

	ctx.SetScrollOffset(scroller, {0.0f, 500.0f});
	EXPECT_NEAR(ctx.GetElement(scroller)->scrollOffset.y, 100.0f, 0.5f);
	ctx.Update();
	const UI::Element* element = ctx.GetElement(scroller);
	UI::ElementHandle firstChild = ctx.MakeHandle(element->firstChild);
	EXPECT_NEAR(ctx.GetLayout(firstChild).borderBox.y, -100.0f, 0.5f);
}

namespace {

	UI::ElementHandle MakeScroller(UI::Context& ctx, Vector<UI::ElementHandle>& rows) {
		UI::Style scrollStyle;
		scrollStyle.flexDirection = UI::FlexDirection::Column;
		scrollStyle.overflow = UI::Overflow::Scroll;
		scrollStyle.width = UI::Length::Pixel(200.0f);
		scrollStyle.height = UI::Length::Pixel(100.0f);
		UI::ElementHandle scroller = MakeChild(ctx, ctx.GetRoot(), scrollStyle);
		for (int i = 0; i < 6; ++i) {
			rows.push_back(MakeChild(ctx, scroller, FixedBox(200.0f, 40.0f)));
		}
		ctx.Update();
		return scroller;
	}

	bool FullyInside(UI::Context& ctx, UI::ElementHandle scroller, UI::ElementHandle row) {
		const UI::Rect& window = ctx.GetLayout(scroller).content;
		const UI::Rect& box = ctx.GetLayout(row).borderBox;
		return box.y >= window.y - 0.01f && box.y + box.h <= window.y + window.h + 0.01f;
	}

} // anonymous namespace

TEST(UILayoutTest, ScrollIntoViewRevealsRow) {
	UI::Context ctx(Desc(400, 300), nullptr);
	Vector<UI::ElementHandle> rows{GetMemoryResource(MemTag::Debug)};
	UI::ElementHandle scroller = MakeScroller(ctx, rows);
	ASSERT_FALSE(FullyInside(ctx, scroller, rows[4]));

	ctx.ScrollIntoView(rows[4]);
	ctx.Update();
	EXPECT_TRUE(FullyInside(ctx, scroller, rows[4]));

	ctx.ScrollIntoView(rows[0]);
	ctx.Update();
	EXPECT_TRUE(FullyInside(ctx, scroller, rows[0]));
}

TEST(UILayoutTest, ScrollIntoViewKeepsVisible) {
	UI::Context ctx(Desc(400, 300), nullptr);
	Vector<UI::ElementHandle> rows{GetMemoryResource(MemTag::Debug)};
	UI::ElementHandle scroller = MakeScroller(ctx, rows);
	ctx.SetScrollOffset(scroller, {0.0f, 80.0f});
	ctx.Update();
	ASSERT_TRUE(FullyInside(ctx, scroller, rows[2]));

	ctx.ScrollIntoView(rows[2]);
	ctx.Update();
	EXPECT_NEAR(ctx.GetElement(scroller)->scrollOffset.y, 80.0f, 0.01f);

	UI::ElementHandle loose = MakeChild(ctx, ctx.GetRoot(), FixedBox(20.0f, 20.0f));
	ctx.Update();
	ctx.ScrollIntoView(loose);
	EXPECT_NEAR(ctx.GetElement(scroller)->scrollOffset.y, 80.0f, 0.01f);

	ctx.ScrollIntoView(rows[5]);
	ctx.Update();
	EXPECT_GT(ctx.GetElement(scroller)->scrollOffset.y, 80.0f);
}

TEST(UILayoutTest, ScrollIntoViewClampsAtBothEnds) {
	UI::Context ctx(Desc(400, 300), nullptr);
	Vector<UI::ElementHandle> rows{GetMemoryResource(MemTag::Debug)};
	UI::ElementHandle scroller = MakeScroller(ctx, rows);
	const f32 maxScroll = ctx.GetElement(scroller)->maxScroll.y;
	ASSERT_GT(maxScroll, 0.0f);

	ctx.ScrollIntoView(rows[5]);
	ctx.Update();
	EXPECT_NEAR(ctx.GetElement(scroller)->scrollOffset.y, maxScroll, 0.01f);
	EXPECT_TRUE(FullyInside(ctx, scroller, rows[5]));

	ctx.ScrollIntoView(rows[0]);
	ctx.Update();
	EXPECT_NEAR(ctx.GetElement(scroller)->scrollOffset.y, 0.0f, 0.01f);
}

TEST(UILayoutTest, HiddenAndDisplayNoneTakeNoSpace) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle root = ctx.GetRoot();
	UI::Style rootStyle;
	rootStyle.alignItems = UI::Align::Start;
	ctx.SetStyle(root, rootStyle);

	UI::ElementHandle a = MakeChild(ctx, root, FixedBox(50.0f, 20.0f));
	UI::Style noneStyle = FixedBox(50.0f, 20.0f);
	noneStyle.display = UI::Display::None;
	MakeChild(ctx, root, noneStyle);
	UI::ElementHandle c = MakeChild(ctx, root, FixedBox(50.0f, 20.0f));
	ctx.Update();

	EXPECT_NEAR(ctx.GetLayout(a).borderBox.x, 0.0f, 0.01f);
	EXPECT_NEAR(ctx.GetLayout(c).borderBox.x, 50.0f, 0.01f);
}

TEST(UILayoutTest, TextMeasuresThroughTheFontSystem) {
	MockFontSystem fonts;
	UI::Context ctx(Desc(400, 300), &fonts);
	UI::ElementHandle root = ctx.GetRoot();
	UI::Style columnStyle;
	columnStyle.flexDirection = UI::FlexDirection::Column;
	columnStyle.alignItems = UI::Align::Start;
	ctx.SetStyle(root, columnStyle);

	UI::ElementHandle label = ctx.CreateElement();
	ctx.AppendChild(root, label);
	ctx.SetText(label, Handle<FontAsset>{}, 14.0f, Color::White(), "hello");
	ctx.Update();

	EXPECT_NEAR(ctx.GetLayout(label).borderBox.w, 50.0f, 0.01f);
	EXPECT_NEAR(ctx.GetLayout(label).borderBox.h, MockFontSystem::LINE_HEIGHT, 0.01f);

	UI::ElementHandle wrapped = ctx.CreateElement();
	ctx.AppendChild(root, wrapped);
	UI::Style wrapStyle;
	wrapStyle.width = UI::Length::Pixel(80.0f);
	ctx.SetStyle(wrapped, wrapStyle);
	ctx.SetText(wrapped, Handle<FontAsset>{}, 14.0f, Color::White(), "abcdefghijkl");
	ctx.Update();
	EXPECT_NEAR(ctx.GetLayout(wrapped).borderBox.h, 2.0f * MockFontSystem::LINE_HEIGHT, 0.01f);
}

TEST(UILayoutTest, EllipsizedLabelStaysOneLine) {
	MockFontSystem fonts;
	UI::Context ctx(Desc(400, 300), &fonts);
	UI::ElementHandle root = ctx.GetRoot();
	UI::Style columnStyle;
	columnStyle.flexDirection = UI::FlexDirection::Column;
	columnStyle.alignItems = UI::Align::Start;
	ctx.SetStyle(root, columnStyle);

	auto makeRow = [&](UI::TextOverflow overflow) {
		UI::ElementHandle row = ctx.CreateElement();
		UI::Style rowStyle;
		rowStyle.flexDirection = UI::FlexDirection::Row;
		rowStyle.width = UI::Length::Pixel(100.0f);
		ctx.SetStyle(row, rowStyle);
		ctx.AppendChild(root, row);

		UI::ElementHandle label = ctx.CreateElement();
		UI::Style labelStyle;
		labelStyle.textOverflow = overflow;
		ctx.SetStyle(label, labelStyle);
		ctx.AppendChild(row, label);
		ctx.SetText(label, Handle<FontAsset>{}, 14.0f, Color::White(), "New Object (Copy) (Copy)");
		return label;
	};
	UI::ElementHandle wrapping = makeRow(UI::TextOverflow::Wrap);
	UI::ElementHandle ellipsized = makeRow(UI::TextOverflow::Ellipsis);
	ctx.Update();

	EXPECT_NEAR(ctx.GetLayout(wrapping).borderBox.h, 3.0f * MockFontSystem::LINE_HEIGHT, 0.01f);
	EXPECT_NEAR(ctx.GetLayout(ellipsized).borderBox.h, MockFontSystem::LINE_HEIGHT, 0.01f);
	EXPECT_LE(ctx.GetLayout(ellipsized).borderBox.w, 100.0f);
}

TEST(UILayoutTest, EllipsizedLabelStillAsksForWholeWidth) {
	MockFontSystem fonts;
	UI::Context ctx(Desc(400, 300), &fonts);
	UI::ElementHandle root = ctx.GetRoot();
	UI::Style columnStyle;
	columnStyle.flexDirection = UI::FlexDirection::Column;
	columnStyle.alignItems = UI::Align::Start;
	ctx.SetStyle(root, columnStyle);

	UI::ElementHandle label = ctx.CreateElement();
	UI::Style labelStyle;
	labelStyle.textOverflow = UI::TextOverflow::Ellipsis;
	ctx.SetStyle(label, labelStyle);
	ctx.AppendChild(root, label);
	ctx.SetText(label, Handle<FontAsset>{}, 14.0f, Color::White(), "abcdefghij");
	ctx.Update();

	EXPECT_NEAR(ctx.GetLayout(label).borderBox.w, 100.0f, 0.01f);
}

TEST(UILayoutTest, OverflowChangeReshapes) {
	MockFontSystem fonts;
	UI::Context ctx(Desc(400, 300), &fonts);
	UI::ElementHandle root = ctx.GetRoot();
	UI::Style columnStyle;
	columnStyle.flexDirection = UI::FlexDirection::Column;
	columnStyle.alignItems = UI::Align::Start;
	ctx.SetStyle(root, columnStyle);

	UI::ElementHandle label = ctx.CreateElement();
	UI::Style style;
	style.width = UI::Length::Pixel(100.0f);
	ctx.SetStyle(label, style);
	ctx.AppendChild(root, label);
	ctx.SetText(label, Handle<FontAsset>{}, 14.0f, Color::White(), "abcdefghijklmnopqrst");
	ctx.Update();
	EXPECT_NEAR(ctx.GetLayout(label).borderBox.h, 2.0f * MockFontSystem::LINE_HEIGHT, 0.01f);

	style.textOverflow = UI::TextOverflow::Ellipsis;
	ctx.SetStyle(label, style);
	ctx.Update();
	EXPECT_NEAR(ctx.GetLayout(label).borderBox.h, MockFontSystem::LINE_HEIGHT, 0.01f);

	style.textOverflow = UI::TextOverflow::Wrap;
	ctx.SetStyle(label, style);
	ctx.Update();
	EXPECT_NEAR(ctx.GetLayout(label).borderBox.h, 2.0f * MockFontSystem::LINE_HEIGHT, 0.01f);
}

TEST(UILayoutTest, LetterSpacingWidensText) {
	MockFontSystem fonts;
	UI::Context ctx(Desc(400, 300), &fonts);
	UI::Style columnStyle;
	columnStyle.flexDirection = UI::FlexDirection::Column;
	columnStyle.alignItems = UI::Align::Start;
	ctx.SetStyle(ctx.GetRoot(), columnStyle);

	UI::ElementHandle label = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), label);
	ctx.SetText(label, Handle<FontAsset>{}, 14.0f, Color::White(), "hello");
	ctx.Update();
	EXPECT_NEAR(ctx.GetLayout(label).borderBox.w, 50.0f, 0.01f);

	UI::Style spaced;
	spaced.letterSpacing = 3.0f;
	ctx.SetStyle(label, spaced);
	ctx.Update();
	EXPECT_NEAR(ctx.GetLayout(label).borderBox.w, 62.0f, 0.01f);

	ctx.SetStyle(label, UI::Style{});
	ctx.Update();
	EXPECT_NEAR(ctx.GetLayout(label).borderBox.w, 50.0f, 0.01f);
}

TEST(UILayoutTest, LetterSpacingScalesWithContentScale) {
	MockFontSystem fonts;
	UI::Context ctx(Desc(400, 300, 2.0f), &fonts);
	UI::Style columnStyle;
	columnStyle.flexDirection = UI::FlexDirection::Column;
	columnStyle.alignItems = UI::Align::Start;
	ctx.SetStyle(ctx.GetRoot(), columnStyle);

	UI::ElementHandle label = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), label);
	UI::Style spaced;
	spaced.letterSpacing = 3.0f;
	ctx.SetStyle(label, spaced);
	ctx.SetText(label, Handle<FontAsset>{}, 14.0f, Color::White(), "hello");
	ctx.Update();

	EXPECT_NEAR(ctx.GetLayout(label).borderBox.w, 50.0f + 4.0f * 6.0f, 0.01f);
}

TEST(UILayoutTest, IncrementalRelayoutTracksChanges) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle root = ctx.GetRoot();
	UI::ElementHandle a = MakeChild(ctx, root, FixedBox(50.0f, 20.0f));
	UI::ElementHandle b = MakeChild(ctx, root, FixedBox(50.0f, 20.0f));
	ctx.Update();
	EXPECT_NEAR(ctx.GetLayout(b).borderBox.x, 50.0f, 0.01f);

	ctx.SetStyle(a, FixedBox(80.0f, 20.0f));
	ctx.Update();
	EXPECT_NEAR(ctx.GetLayout(a).borderBox.w, 80.0f, 0.01f);
	EXPECT_NEAR(ctx.GetLayout(b).borderBox.x, 80.0f, 0.01f);
}

TEST(UILayoutTest, HiddenChildYieldsItsFlexSpace) {
	UI::Context ctx(Desc(400, 300), nullptr);
	UI::Style columnStyle = FixedBox(400.0f, 300.0f);
	columnStyle.flexDirection = UI::FlexDirection::Column;
	columnStyle.alignItems = UI::Align::Stretch;
	UI::ElementHandle column = MakeChild(ctx, ctx.GetRoot(), columnStyle);

	UI::Style growerStyle;
	growerStyle.height = UI::Length::Pixel(0.0f);
	growerStyle.flexGrow = 1.0f;
	UI::ElementHandle grower = MakeChild(ctx, column, growerStyle);

	UI::Style paneStyle;
	paneStyle.height = UI::Length::Percent(0.3f);
	UI::ElementHandle pane = MakeChild(ctx, column, paneStyle);
	ctx.Update();

	EXPECT_NEAR(ctx.GetLayout(pane).borderBox.h, 90.0f, 0.5f);
	EXPECT_NEAR(ctx.GetLayout(grower).borderBox.h, 210.0f, 0.5f);

	ctx.SetVisible(pane, false);
	ctx.Update();
	EXPECT_NEAR(ctx.GetLayout(grower).borderBox.h, 300.0f, 0.5f);

	ctx.SetVisible(pane, true);
	ctx.Update();
	EXPECT_NEAR(ctx.GetLayout(grower).borderBox.h, 210.0f, 0.5f);
}

TEST(UILayoutTest, HiddenChildYieldsFlexSpaceInRow) {
	UI::Context ctx(Desc(400, 300), nullptr);
	UI::Style rowStyle = FixedBox(400.0f, 300.0f);
	rowStyle.flexDirection = UI::FlexDirection::Row;
	rowStyle.alignItems = UI::Align::Stretch;
	UI::ElementHandle row = MakeChild(ctx, ctx.GetRoot(), rowStyle);

	UI::Style growerStyle;
	growerStyle.width = UI::Length::Pixel(0.0f);
	growerStyle.flexGrow = 1.0f;
	UI::ElementHandle grower = MakeChild(ctx, row, growerStyle);

	UI::Style paneStyle;
	paneStyle.width = UI::Length::Pixel(120.0f);
	UI::ElementHandle pane = MakeChild(ctx, row, paneStyle);
	ctx.Update();
	EXPECT_NEAR(ctx.GetLayout(grower).borderBox.w, 280.0f, 0.5f);

	ctx.SetVisible(pane, false);
	ctx.Update();
	EXPECT_NEAR(ctx.GetLayout(grower).borderBox.w, 400.0f, 0.5f);
}

TEST(UILayoutTest, DisplayNoneStillYieldsItsFlexSpace) {
	UI::Context ctx(Desc(400, 300), nullptr);
	UI::Style columnStyle = FixedBox(400.0f, 300.0f);
	columnStyle.flexDirection = UI::FlexDirection::Column;
	UI::ElementHandle column = MakeChild(ctx, ctx.GetRoot(), columnStyle);

	UI::Style growerStyle;
	growerStyle.height = UI::Length::Pixel(0.0f);
	growerStyle.flexGrow = 1.0f;
	UI::ElementHandle grower = MakeChild(ctx, column, growerStyle);

	UI::Style paneStyle;
	paneStyle.height = UI::Length::Percent(0.3f);
	paneStyle.display = UI::Display::None;
	MakeChild(ctx, column, paneStyle);
	ctx.Update();

	EXPECT_NEAR(ctx.GetLayout(grower).borderBox.h, 300.0f, 0.5f);
}

namespace {

	struct BandWatcher {
		Vector<UI::Rect> bands{GetMemoryResource(MemTag::Debug)};

		void Watch(UI::Context& ctx, UI::ElementHandle scroller) {
			ctx.Events(scroller).onScrollChanged = [this](const UI::ScrollBand& band) { bands.push_back(band.shown); };
		}
		size_t Count() const { return bands.size(); }
		const UI::Rect& Last() const { return bands.back(); }
	};

	UI::ElementHandle MakeBandScroller(UI::Context& ctx, f32 viewportH = 100.0f) {
		UI::Style scrollStyle;
		scrollStyle.flexDirection = UI::FlexDirection::Column;
		scrollStyle.overflow = UI::Overflow::Scroll;
		scrollStyle.width = UI::Length::Pixel(200.0f);
		scrollStyle.height = UI::Length::Pixel(viewportH);
		UI::ElementHandle scroller = MakeChild(ctx, ctx.GetRoot(), scrollStyle);
		for (int i = 0; i < 5; ++i) {
			MakeChild(ctx, scroller, FixedBox(200.0f, 40.0f));
		}
		return scroller;
	}

} // anonymous namespace

TEST(UILayoutTest, ScrollableReportsOnce) {
	UI::Context ctx(Desc(400, 300), nullptr);
	UI::ElementHandle scroller = MakeBandScroller(ctx);
	BandWatcher watcher;
	watcher.Watch(ctx, scroller);

	ctx.Update();
	ASSERT_EQ(watcher.Count(), 1u);
	EXPECT_NEAR(watcher.Last().x, 0.0f, 0.01f);
	EXPECT_NEAR(watcher.Last().y, 0.0f, 0.01f);
	EXPECT_NEAR(watcher.Last().w, 200.0f, 0.5f);
	EXPECT_NEAR(watcher.Last().h, 100.0f, 0.5f);

	for (int frame = 0; frame < 5; ++frame) {
		UI::Style repainted = ctx.GetStyle(scroller);
		repainted.background = UI::Brush::SolidColor(Color(0.1f * static_cast<f32>(frame + 1), 0.0f, 0.0f, 1.0f));
		ctx.SetStyle(scroller, repainted);
		ctx.Update();
	}
	EXPECT_EQ(watcher.Count(), 1u);
}

TEST(UILayoutTest, NotchReportsNewBand) {
	UI::Context ctx(Desc(400, 300), nullptr);
	UI::ElementHandle scroller = MakeBandScroller(ctx);
	BandWatcher watcher;
	watcher.Watch(ctx, scroller);
	ctx.Update();
	ASSERT_EQ(watcher.Count(), 1u);

	InputEvent move{};
	move.type = InputEventType::MouseMove;
	move.mousePosPx = {100, 50};
	ctx.InjectInput(move);
	InputEvent wheel{};
	wheel.type = InputEventType::MouseWheel;
	wheel.wheel = {0.0f, -1.0f};
	wheel.mousePosPx = {100, 50};
	ctx.InjectInput(wheel);
	ctx.Update();

	ASSERT_EQ(watcher.Count(), 2u);
	EXPECT_GT(watcher.Last().y, 0.0f);
	EXPECT_NEAR(watcher.Last().y, ctx.GetElement(scroller)->scrollOffset.y, 0.01f);
}

TEST(UILayoutTest, GrownViewportReportsBand) {
	UI::Context ctx(Desc(400, 300), nullptr);
	UI::ElementHandle scroller = MakeBandScroller(ctx, 100.0f);
	BandWatcher watcher;
	watcher.Watch(ctx, scroller);
	ctx.Update();
	ASSERT_EQ(watcher.Count(), 1u);
	const f32 offsetBefore = ctx.GetElement(scroller)->scrollOffset.y;

	UI::Style taller = ctx.GetStyle(scroller);
	taller.height = UI::Length::Pixel(160.0f);
	ctx.SetStyle(scroller, taller);
	ctx.Update();

	EXPECT_NEAR(ctx.GetElement(scroller)->scrollOffset.y, offsetBefore, 0.01f);
	ASSERT_EQ(watcher.Count(), 2u);
	EXPECT_NEAR(watcher.Last().h, 160.0f, 0.5f);
}

TEST(UILayoutTest, BandRowsLaidOutSameSettle) {
	UI::Context ctx(Desc(400, 300), nullptr);
	UI::ElementHandle scroller = MakeBandScroller(ctx);
	UI::ElementHandle built;
	ctx.Events(scroller).onScrollChanged = [&ctx, scroller, &built](const UI::ScrollBand&) {
		if (ctx.IsAlive(built)) {
			return;
		}
		built = MakeChild(ctx, scroller, FixedBox(120.0f, 24.0f));
	};

	ctx.Update();

	ASSERT_TRUE(ctx.IsAlive(built));
	EXPECT_NEAR(ctx.GetLayout(built).borderBox.w, 120.0f, 0.5f);
	EXPECT_NEAR(ctx.GetLayout(built).borderBox.h, 24.0f, 0.5f);
}

TEST(UILayoutTest, ScrollBackDoesNotSpin) {
	UI::Context ctx(Desc(400, 300), nullptr);
	UI::ElementHandle scroller = MakeBandScroller(ctx);
	u32 reports = 0;
	ctx.Events(scroller).onScrollChanged = [&ctx, scroller, &reports](const UI::ScrollBand& band) {
		++reports;
		ctx.SetScrollOffset(scroller, {0.0f, band.shown.y + 20.0f});
	};

	ctx.Update();
	EXPECT_LE(reports, 2u);
	EXPECT_GE(reports, 1u);
}

TEST(UILayoutTest, RecycledSlotDropsBand) {
	UI::Context ctx(Desc(400, 300), nullptr);
	UI::ElementHandle first = MakeBandScroller(ctx);
	BandWatcher ignored;
	ignored.Watch(ctx, first);
	ctx.Update();
	ASSERT_EQ(ignored.Count(), 1u);

	ctx.DestroyElement(first);
	ctx.Update();

	UI::ElementHandle second = MakeBandScroller(ctx);
	BandWatcher watcher;
	watcher.Watch(ctx, second);
	ctx.Update();
	EXPECT_EQ(watcher.Count(), 1u);
}

TEST(UILayoutTest, ScrollableDestroyedByEarlierHandlerReportsNothing) {
	UI::Context ctx(Desc(400, 300), nullptr);
	UI::ElementHandle first = MakeBandScroller(ctx);
	UI::ElementHandle second = MakeBandScroller(ctx);
	u32 secondReports = 0;
	ctx.Events(second).onScrollChanged = [&secondReports](const UI::ScrollBand&) { ++secondReports; };
	ctx.Events(first).onScrollChanged = [&ctx, second](const UI::ScrollBand&) {
		if (ctx.IsAlive(second)) {
			ctx.DestroyElement(second);
		}
	};

	ctx.Update();
	ctx.Update();

	EXPECT_FALSE(ctx.IsAlive(second));
	EXPECT_EQ(secondReports, 0u);

	UI::ElementHandle third = MakeBandScroller(ctx);
	BandWatcher watcher;
	watcher.Watch(ctx, third);
	ctx.Update();
	EXPECT_EQ(watcher.Count(), 1u);
}

TEST(UILayoutTest, StaticElementReportsNoBand) {
	UI::Context ctx(Desc(400, 300), nullptr);
	UI::ElementHandle plain = MakeChild(ctx, ctx.GetRoot(), FixedBox(200.0f, 100.0f));
	MakeChild(ctx, plain, FixedBox(200.0f, 400.0f));
	BandWatcher watcher;
	watcher.Watch(ctx, plain);

	for (int frame = 0; frame < 3; ++frame) {
		ctx.Update();
	}
	EXPECT_EQ(watcher.Count(), 0u) << watcher.Count() << " bands";

	UI::Style scrolls = ctx.GetStyle(plain);
	scrolls.overflow = UI::Overflow::Scroll;
	ctx.SetStyle(plain, scrolls);
	ctx.Update();
	EXPECT_EQ(watcher.Count(), 1u);
}
