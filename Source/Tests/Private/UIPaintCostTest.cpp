#include "TestsPCH.h"

#include "UITestUtil.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	struct DockedEditorFixture {
		CountingFont font;
		UI::Context ctx;
		UI::DockManager dock{ctx, Handle<FontAsset>{}};
		UI::RenderList list;
		u32 rowsPerPanel = 0;

		static constexpr StringHash Panels[4] = {
			"panel.scene"_hash, "panel.game"_hash, "panel.hierarchy"_hash, "panel.console"_hash};

		std::vector<UI::UIQuad> fullWalkQuads;
		UI::PaintStats fullWalk;

		explicit DockedEditorFixture(u32 rows) : ctx(MakeDesc(), &font), rowsPerPanel(rows) {
			for (StringHash panel : Panels) {
				dock.RegisterPanel(panel, "Panel", [this](UI::Context& c, UI::ElementHandle parent) {
					BuildPanelBody(c, parent);
				});
			}
			dock.DockPanel(Panels[0], dock.GetRootNode(), UI::DockZone::Center);
			dock.DockPanel(Panels[1], dock.GetRootNode(), UI::DockZone::Right);
			dock.DockPanel(Panels[2], dock.GetRootNode(), UI::DockZone::Left);
			dock.DockPanel(Panels[3], dock.GetRootNode(), UI::DockZone::Bottom);

			dock.Build(ctx.GetRoot());
			ctx.Update();
			fullWalk = Paint();
			fullWalkQuads.assign(list.quads.begin(), list.quads.end());
		}

		static UI::ContextDesc MakeDesc() {
			UI::ContextDesc desc;
			desc.sizePx = {1600, 900};
			desc.contentScale = 1.0f;
			return desc;
		}

		void BuildPanelBody(UI::Context& c, UI::ElementHandle parent) {
			UI::Widgets::ScrollViewParts scroll = UI::Widgets::ScrollView(c, parent);
			UI::Style scrollStyle = c.GetStyle(scroll.root);
			scrollStyle.flexGrow = 1.0f;
			c.SetStyle(scroll.root, scrollStyle);

			for (u32 row = 0; row < rowsPerPanel; ++row) {
				UI::Style rowStyle;
				rowStyle.flexDirection = UI::FlexDirection::Row;
				rowStyle.width = UI::Length::Percent(1.0f);
				UI::ElementHandle line = MakeChild(c, scroll.viewport, rowStyle);

				UI::Style tagStyle;
				tagStyle.width = UI::Length::Pixel(46.0f);
				c.SetText(MakeChild(c, line, tagStyle), {}, 12.0f, Color::White(), "INFO");

				UI::Style messageStyle;
				messageStyle.flexGrow = 1.0f;
				c.SetText(MakeChild(c, line, messageStyle), {}, 12.0f, Color::White(), "[Editor] a message");
			}
		}

		UI::PaintStats Paint() {
			ctx.ResetPaintStats();
			list.Clear();
			ctx.BuildRenderList(list);
			return ctx.GetPaintStats();
		}
	};

	struct TreeSize {
		u64 visited = 0;
		u64 painted = 0;
	};

	void CountTree(UI::Context& ctx, UI::ElementHandle handle, TreeSize& out) {
		const UI::Element* element = ctx.GetElement(handle);
		if (!element) {
			return;
		}
		++out.visited;
		if (!HasFlag(element->flags, UI::ElementFlags::Visible) ||
			ctx.GetComputedStyle(handle).display == UI::Display::None) {
			return;
		}
		++out.painted;
		for (u32 child = element->firstChild; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle childHandle = ctx.MakeHandle(child);
			CountTree(ctx, childHandle, out);
			child = ctx.GetElement(childHandle)->nextSibling;
		}
	}

	TreeSize CountTree(UI::Context& ctx) {
		TreeSize size;
		CountTree(ctx, ctx.GetRoot(), size);
		return size;
	}

	u32 CountKind(const UI::RenderList& list, UI::UICommandKind kind) {
		u32 count = 0;
		for (const UI::UICommand& command : list.commands) {
			if (command.kind == kind) {
				++count;
			}
		}
		return count;
	}

} // anonymous namespace

TEST(UIPaintCostTest, PaintVisitsOncePerElement) {
	DockedEditorFixture fixture{40};

	TreeSize tree = CountTree(fixture.ctx);
	ASSERT_GT(tree.painted, 500u);

	UI::PaintStats stats = fixture.Paint();
	EXPECT_LE(stats.elementsVisited, tree.visited);
	EXPECT_LE(stats.elementsPainted, tree.painted);

	EXPECT_EQ(fixture.fullWalk.elementsPainted, tree.painted);
}

TEST(UIPaintCostTest, HiddenSubtreeCostsOneVisit) {
	DockedEditorFixture fixture{16};
	ASSERT_EQ(fixture.Paint().elementsPainted, CountTree(fixture.ctx).painted);

	auto costOfHiding = [&fixture](UI::ElementHandle target, auto&& hide, auto&& show) {
		TreeSize whole = CountTree(fixture.ctx);
		UI::PaintStats before = fixture.Paint();

		hide(target);
		fixture.ctx.Update();
		const u64 subtree = whole.painted - CountTree(fixture.ctx).painted;
		UI::PaintStats after = fixture.Paint();

		show(target);
		fixture.ctx.Update();
		return std::tuple{before, after, subtree};
	};

	UI::ElementHandle leaf = FindByClass(fixture.ctx, fixture.ctx.GetRoot(), "dock-leaf"_hash, 0);
	ASSERT_TRUE(fixture.ctx.IsAlive(leaf));
	auto [beforeFlag, afterFlag, flagSubtree] = costOfHiding(
		leaf,
		[&](UI::ElementHandle e) { fixture.ctx.SetVisible(e, false); },
		[&](UI::ElementHandle e) { fixture.ctx.SetVisible(e, true); });

	ASSERT_GT(flagSubtree, 20u);
	EXPECT_EQ(afterFlag.elementsVisited, beforeFlag.elementsVisited - (flagSubtree - 1));
	EXPECT_EQ(afterFlag.elementsPainted, beforeFlag.elementsPainted - flagSubtree);

	UI::ElementHandle scrollView = FindByClass(fixture.ctx, fixture.ctx.GetRoot(), "slt-scroll-viewport"_hash, 0);
	ASSERT_TRUE(fixture.ctx.IsAlive(scrollView));
	UI::ElementHandle row = fixture.ctx.MakeHandle(fixture.ctx.GetElement(scrollView)->firstChild);
	ASSERT_TRUE(fixture.ctx.IsAlive(row));

	const UI::Style shown = fixture.ctx.GetStyle(row);
	auto [beforeStyle, afterStyle, styleSubtree] = costOfHiding(
		row,
		[&](UI::ElementHandle e) {
			UI::Style hidden = shown;
			hidden.display = UI::Display::None;
			fixture.ctx.SetStyle(e, hidden);
		},
		[&](UI::ElementHandle e) { fixture.ctx.SetStyle(e, shown); });

	ASSERT_EQ(styleSubtree, 3u);
	EXPECT_EQ(afterStyle.elementsVisited, beforeStyle.elementsVisited - (styleSubtree - 1));
	EXPECT_EQ(afterStyle.elementsPainted, beforeStyle.elementsPainted - styleSubtree);
}

TEST(UIPaintCostTest, RepaintingUnchangedTreeCostsSameEveryFrame) {
	DockedEditorFixture fixture{40};

	UI::PaintStats first = fixture.Paint();
	const size_t firstQuads = fixture.list.quads.size();
	const size_t firstCommands = fixture.list.commands.size();

	for (int frame = 0; frame < 20; ++frame) {
		UI::PaintStats repeat = fixture.Paint();
		ASSERT_EQ(repeat.elementsVisited, first.elementsVisited);
		ASSERT_EQ(repeat.elementsPainted, first.elementsPainted);
		ASSERT_EQ(fixture.list.quads.size(), firstQuads);
		ASSERT_EQ(fixture.list.commands.size(), firstCommands);
	}
}

TEST(UIPaintCostTest, EditorFrameQuadsBounded) {
	DockedEditorFixture fixture{40};

	UI::PaintStats stats = fixture.Paint();
	ASSERT_GT(CountTree(fixture.ctx).painted, 500u);
	ASSERT_GT(stats.elementsPainted, 100u);

	u32 glyphQuads = 0;
	for (const UI::UIQuad& quad : fixture.list.quads) {
		if (quad.flags & UI::QUAD_FLAG_GLYPH) {
			++glyphQuads;
		}
	}
	ASSERT_GT(glyphQuads, 0u);

	const u64 boxQuads = fixture.list.quads.size() - glyphQuads;
	EXPECT_LE(boxQuads, 60u);

	EXPECT_LE(fixture.list.commands.size(), 40u) << fixture.list.quads.size() << ' ' << fixture.list.commands.size();
}

TEST(UIPaintCostTest, WalkCostIndependentOfSize) {
	DockedEditorFixture few{160};
	DockedEditorFixture many{640};

	const u64 fewElements = CountTree(few.ctx).painted;
	const u64 manyElements = CountTree(many.ctx).painted;
	ASSERT_GT(manyElements, fewElements * 3);

	UI::PaintStats fewStats = few.Paint();
	UI::PaintStats manyStats = many.Paint();

	EXPECT_EQ(manyStats.elementsVisited, fewStats.elementsVisited);
	EXPECT_EQ(many.list.quads.size(), few.list.quads.size());

	EXPECT_LT(manyStats.elementsVisited, manyElements) << manyElements << " elements";
}

TEST(UIPaintCostTest, SkipDropsOnlyClipped) {
	DockedEditorFixture fixture{40};

	UI::PaintStats cheap = fixture.Paint();
	ASSERT_LT(cheap.elementsVisited, fixture.fullWalk.elementsVisited);
	ASSERT_FALSE(fixture.fullWalkQuads.empty());

	auto sameQuad = [](const UI::UIQuad& a, const UI::UIQuad& b) {
		return a.rect == b.rect && a.clipRect == b.clipRect && a.uv == b.uv &&
			   a.flags == b.flags && a.color0 == b.color0;
	};

	size_t kept = 0;
	u64 dropped = 0;
	for (const UI::UIQuad& full : fixture.fullWalkQuads) {
		if (kept < fixture.list.quads.size() && sameQuad(full, fixture.list.quads[kept])) {
			++kept;
			continue;
		}
		const UI::Rect visible = UI::RectIntersect(full.rect, full.clipRect);
		++dropped;
		ASSERT_TRUE(visible.w <= 0.0f || visible.h <= 0.0f);
	}
	EXPECT_EQ(kept, fixture.list.quads.size());
	EXPECT_GT(dropped, 0u);
}

TEST(UIPaintCostTest, ScrollingBringsBackWhatWasSkipped) {
	DockedEditorFixture fixture{160};

	UI::ElementHandle viewport = FindByClass(fixture.ctx, fixture.ctx.GetRoot(), "slt-scroll-viewport"_hash, 0);
	ASSERT_TRUE(fixture.ctx.IsAlive(viewport));
	const UI::Element* viewportElement = fixture.ctx.GetElement(viewport);
	ASSERT_NE(viewportElement->firstChild, UI::INVALID_UI_INDEX);

	UI::ElementHandle deepRow = fixture.ctx.MakeHandle(viewportElement->firstChild);
	for (int step = 0; step < 100; ++step) {
		deepRow = fixture.ctx.MakeHandle(fixture.ctx.GetElement(deepRow)->nextSibling);
		ASSERT_TRUE(fixture.ctx.IsAlive(deepRow));
	}
	const UI::Rect rowBox = fixture.ctx.GetLayout(deepRow).borderBox;
	ASSERT_GT(rowBox.h, 0.0f);

	auto glyphsInside = [&fixture](const UI::Rect& box) {
		u32 count = 0;
		for (const UI::UIQuad& quad : fixture.list.quads) {
			if ((quad.flags & UI::QUAD_FLAG_GLYPH) == 0) {
				continue;
			}
			if (quad.rect.x >= box.x && quad.rect.y >= box.y &&
				quad.rect.x + quad.rect.w <= box.x + box.w &&
				quad.rect.y + quad.rect.h <= box.y + box.h) {
				++count;
			}
		}
		return count;
	};

	fixture.Paint();
	ASSERT_EQ(glyphsInside(rowBox), 0u);

	fixture.ctx.SetScrollOffset(viewport, {0.0f, rowBox.y - fixture.ctx.GetLayout(viewport).borderBox.y});
	fixture.ctx.Update();
	fixture.Paint();
	const UI::Rect scrolledBox = fixture.ctx.GetLayout(deepRow).borderBox;

	EXPECT_GT(glyphsInside(scrolledBox), 0u);
}

TEST(UIPaintCostTest, CustomDrawInstalledAfterFactStillPaints) {
	DockedEditorFixture fixture{160};

	UI::ElementHandle viewport = FindByClass(fixture.ctx, fixture.ctx.GetRoot(), "slt-scroll-viewport"_hash, 0);
	ASSERT_TRUE(fixture.ctx.IsAlive(viewport));
	UI::ElementHandle row = fixture.ctx.MakeHandle(fixture.ctx.GetElement(viewport)->firstChild);
	for (int step = 0; step < 100; ++step) {
		row = fixture.ctx.MakeHandle(fixture.ctx.GetElement(row)->nextSibling);
		ASSERT_TRUE(fixture.ctx.IsAlive(row));
	}

	const UI::Rect viewportBox = fixture.ctx.GetLayout(viewport).borderBox;
	const UI::Rect rowBox = fixture.ctx.GetLayout(row).borderBox;
	ASSERT_GT(rowBox.y, viewportBox.y + viewportBox.h);

	const UI::Rect mark{viewportBox.x + 3.0f, viewportBox.y + 3.0f, 7.0f, 7.0f};
	bool ran = false;
	fixture.ctx.Events(row).customDraw = [&](UI::Context&, UI::ElementHandle, UI::PaintScope& scope) {
		ran = true;
		scope.EmitQuad(mark, Color::Red());
	};

	fixture.Paint();
	EXPECT_TRUE(ran);
	bool marked = false;
	for (const UI::UIQuad& quad : fixture.list.quads) {
		const UI::Rect hit = UI::RectIntersect(quad.rect, mark);
		marked = marked || (quad.color0.r == 1.0f && quad.color0.g == 0.0f && quad.color0.b == 0.0f &&
							hit.w > 0.0f && hit.h > 0.0f);
	}
	EXPECT_TRUE(marked);
}

TEST(UIPaintCostTest, AUniformZTreeReordersNoScope) {
	UI::ContextDesc desc;
	desc.sizePx = {800, 600};
	desc.contentScale = 1.0f;
	MockFontSystem font;
	UI::Context ctx(desc, &font);

	UI::Style boxStyle;
	boxStyle.width = UI::Length::Percent(1.0f);
	boxStyle.height = UI::Length::Pixel(20.0f);
	boxStyle.background = UI::Brush::SolidColor(Color::White());
	for (u32 i = 0; i < 20; ++i) {
		UI::ElementHandle box = MakeChild(ctx, ctx.GetRoot(), boxStyle);
		UI::Style nested = boxStyle;
		nested.opacity = 0.5f;
		MakeChild(ctx, MakeChild(ctx, box, nested), boxStyle);
	}
	ctx.Update();

	UI::RenderList list;
	ctx.ResetPaintStats();
	ctx.BuildRenderList(list);

	EXPECT_GT(ctx.GetPaintStats().scopesWalked, 1u);
	EXPECT_EQ(ctx.GetPaintStats().scopesSorted, 0u);

	UI::Style lifted = boxStyle;
	lifted.zIndex = 10;
	ctx.SetStyle(ctx.MakeHandle(ctx.GetElement(ctx.GetRoot())->firstChild), lifted);
	ctx.Update();
	ctx.ResetPaintStats();
	ctx.BuildRenderList(list);
	EXPECT_GT(ctx.GetPaintStats().scopesSorted, 0u);
}

TEST(UIPaintCostTest, OnlyScopeHoldingDockSplittersSorts) {
	DockedEditorFixture fixture{40};

	UI::PaintStats stats = fixture.Paint();
	EXPECT_GT(stats.scopesWalked, 4u);
	EXPECT_LE(stats.scopesSorted, 1u);

	EXPECT_GT(CountKind(fixture.list, UI::UICommandKind::PushLayer), 0u);
}
