#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/UI/RenderList.h"

#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/EditorVirtualRows.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	struct RowsRig {
		static constexpr f32 ROW_HEIGHT = 20.0f;
		static constexpr u64 FOOTER_TAG = 999999;

		MockFontSystem fonts;
		UI::Context context;
		UI::ElementHandle viewport;
		EditorVirtualRows window;
		Vector<u32> built{GetMemoryResource(MemTag::Editor)};

		UI::ElementHandle footer;
		bool wantsFooter = false;

		explicit RowsRig(Vec2I size = {400, 200}, f32 headerPx = 0.0f) : context(MakeDesc(size), &fonts) {
			UI::ElementHandle root = context.CreateElement();
			context.AppendChild(context.GetRoot(), root);
			UI::Style rootStyle;
			rootStyle.flexDirection = UI::FlexDirection::Column;
			rootStyle.width = UI::Length::Percent(1.0f);
			rootStyle.height = UI::Length::Percent(1.0f);
			context.SetStyle(root, rootStyle);
			if (headerPx > 0.0f) {
				UI::Style headerStyle;
				headerStyle.height = UI::Length::Pixel(headerPx);
				headerStyle.flexShrink = 0.0f;
				MakeChild(context, root, headerStyle);
			}

			UI::Widgets::ScrollViewParts scroll = UI::Widgets::ScrollView(context, root);
			UI::Style scrollStyle = context.GetStyle(scroll.root);
			scrollStyle.flexGrow = 1.0f;
			context.SetStyle(scroll.root, scrollStyle);
			viewport = scroll.viewport;

			window.Attach(context, viewport, [this](u32 index) { return Build(index); }, {}, [this]() {
				if (!wantsFooter) {
					return;
				}
				footer = context.CreateElement();
				context.AppendChild(viewport, footer);
				UI::Style style;
				style.height = UI::Length::Pixel(7.0f);
				style.flexShrink = 0.0f;
				context.SetStyle(footer, style);
				context.SetUserTag(footer, FOOTER_TAG); });
			context.Update();
		}

		static UI::ContextDesc MakeDesc(Vec2I size) {
			UI::ContextDesc desc;
			desc.sizePx = size;
			desc.contentScale = 1.0f;
			return desc;
		}

		bool unequalRows = false;
		u32 tallRow = UINT32_MAX;
		f32 HeightFor(u32 index) const {
			return (unequalRows && index % 7 == 0) || index == tallRow ? ROW_HEIGHT * 2.0f : ROW_HEIGHT;
		}

		Vector<u64> originOfElement{GetMemoryResource(MemTag::Editor)};
		Vector<UI::ElementHandle> elementOfOrigin{GetMemoryResource(MemTag::Editor)};

		u32 OriginOf(UI::ElementHandle element) const {
			for (size_t i = 0; i < elementOfOrigin.size(); ++i) {
				if (elementOfOrigin[i] == element) {
					return static_cast<u32>(originOfElement[i]);
				}
			}
			return UINT32_MAX;
		}

		UI::ElementHandle Build(u32 index) {
			built.push_back(index);
			UI::ElementHandle row = context.CreateElement();
			context.AppendChild(viewport, row);
			originOfElement.push_back(index);
			elementOfOrigin.push_back(row);
			UI::Style style;
			style.height = UI::Length::Pixel(HeightFor(index));
			style.width = UI::Length::Percent(1.0f);
			style.flexShrink = 0.0f;
			context.SetStyle(row, style);
			context.SetUserTag(row, index + 1);
			return row;
		}

		void Settle(u32 frames = 3) {
			for (u32 frame = 0; frame < frames; ++frame) {
				context.Update();
			}
		}

		void ScrollTo(f32 y) {
			context.SetScrollOffset(viewport, {0.0f, y});
			Settle();
		}

		u32 Realized() const { return window.GetRealizedCount(); }

		f32 MaxScrollY() const {
			const UI::Element* element = context.GetElement(viewport);
			return element ? element->maxScroll.y : -1.0f;
		}

		void VisibleIndices(Vector<u32>& out) const {
			out.clear();
			const UI::Rect& view = context.GetLayout(viewport).content;
			for (u32 index = 0; index < window.GetCount(); ++index) {
				UI::ElementHandle element = window.GetElement(index);
				if (!element.IsValid() || !context.IsAlive(element)) {
					continue;
				}
				const UI::Rect& box = context.GetLayout(element).borderBox;
				if (box.h > 0.0f && box.y + box.h > view.y && box.y < view.y + view.h) {
					out.push_back(index);
				}
			}
		}

		u32 ChildrenOfViewport() const {
			u32 count = 0;
			const UI::Element* element = context.GetElement(viewport);
			for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX;
				 child != UI::INVALID_UI_INDEX;) {
				++count;
				child = context.GetElement(context.MakeHandle(child))->nextSibling;
			}
			return count;
		}

		u64 TagOfChild(u32 nth) const {
			const UI::Element* element = context.GetElement(viewport);
			u32 index = 0;
			for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX;
				 child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = context.MakeHandle(child);
				if (index++ == nth) {
					return context.GetElement(handle)->userTag;
				}
				child = context.GetElement(handle)->nextSibling;
			}
			return 0;
		}
	};

} // namespace

TEST(EditorVirtualRowsTest, ModelOfThousandsRealizesBandViewportShows) {
	RowsRig rig;
	rig.window.Reset(2000);
	rig.Settle();

	EXPECT_LT(rig.Realized(), 200u);
	EXPECT_GT(rig.Realized(), 5u);

	Vector<u32> visible{GetMemoryResource(MemTag::Editor)};
	rig.VisibleIndices(visible);
	EXPECT_GE(visible.size(), 5u) << visible.size();
}

TEST(EditorVirtualRowsTest, RealizedCountIsFlat) {
	RowsRig small;
	small.window.Reset(200);
	small.Settle();

	RowsRig large;
	large.window.Reset(5000);
	large.Settle();

	EXPECT_EQ(large.Realized(), small.Realized());
}

TEST(EditorVirtualRowsTest, ScrollExtentIsWholeModel) {
	RowsRig rig;
	rig.window.Reset(2000);
	rig.Settle();

	const f32 viewportH = rig.context.GetLayout(rig.viewport).content.h;
	const f32 wanted = 2000 * RowsRig::ROW_HEIGHT - viewportH;
	EXPECT_NEAR(rig.MaxScrollY(), wanted, RowsRig::ROW_HEIGHT) << rig.MaxScrollY() << ' ' << RowsRig::ROW_HEIGHT << " px need " << wanted;

	rig.ScrollTo(rig.MaxScrollY());
	EXPECT_TRUE(rig.window.IsRealized(1999));
}

TEST(EditorVirtualRowsTest, AppendRealizesWithoutBandReport) {
	RowsRig rig;
	rig.window.Reset(0);
	rig.Settle();
	ASSERT_EQ(rig.Realized(), 0u);

	rig.window.Appended(0, 700);
	rig.context.Update();

	EXPECT_GT(rig.Realized(), 0u);
}

TEST(EditorVirtualRowsTest, AppendSlidesKeepsElements) {
	RowsRig rig;
	rig.window.Reset(100);
	rig.Settle();
	rig.ScrollTo(rig.MaxScrollY());
	ASSERT_GT(rig.Realized(), 5u);
	ASSERT_TRUE(rig.window.IsRealized(99));

	const u32 watchedIndex = rig.window.GetFirstRealized() + 2;
	UI::ElementHandle watched = rig.window.GetElement(watchedIndex);
	ASSERT_TRUE(watched.IsValid());

	rig.built.clear();
	rig.window.Appended(0, 1);
	rig.Settle();

	EXPECT_TRUE(rig.context.IsAlive(watched));
	EXPECT_LT(rig.built.size(), 5u);
	EXPECT_GT(rig.built.size(), 0u);
}

TEST(EditorVirtualRowsTest, HeadDropShiftsRows) {
	RowsRig rig;
	rig.unequalRows = true;
	rig.window.Reset(40);
	rig.Settle();
	rig.ScrollTo(rig.MaxScrollY());

	const u32 lastIndex = rig.window.GetCount() - 1;
	UI::ElementHandle lastRow = rig.window.GetElement(lastIndex);
	ASSERT_TRUE(lastRow.IsValid());
	ASSERT_TRUE(rig.window.IsRealized(35));
	ASSERT_FLOAT_EQ(rig.window.HeightOf(35), RowsRig::ROW_HEIGHT * 2.0f);

	rig.window.Appended(10, 10);
	rig.Settle();

	EXPECT_EQ(rig.window.GetCount(), 40u);
	EXPECT_EQ(rig.window.GetElement(lastIndex - 10), lastRow);
	EXPECT_FLOAT_EQ(rig.window.HeightOf(25), RowsRig::ROW_HEIGHT * 2.0f);

	for (u32 index = rig.window.GetFirstRealized(); index < rig.window.GetFirstRealized() + rig.Realized();
		 ++index) {
		UI::ElementHandle element = rig.window.GetElement(index);
		ASSERT_TRUE(element.IsValid());
		const u32 origin = rig.OriginOf(element);
		EXPECT_TRUE(origin == index || origin == index + 10);
	}
}

TEST(EditorVirtualRowsTest, CappedLogRealizesNewest) {
	RowsRig rig;
	rig.window.Reset(300);
	rig.Settle();
	rig.ScrollTo(rig.MaxScrollY());
	ASSERT_EQ(rig.window.GetFirstRealized() + rig.Realized(), rig.window.GetCount());

	rig.built.clear();
	for (u32 tick = 0; tick < 20; ++tick) {
		rig.window.Appended(1, 1);
		rig.Settle(1);
	}

	EXPECT_EQ(rig.window.GetCount(), 300u);
	const u32 newest = rig.window.GetCount() - 1;
	EXPECT_TRUE(rig.window.IsRealized(newest));

	EXPECT_EQ(rig.built.size(), 20u);
	ASSERT_FALSE(rig.built.empty());

	UI::ElementHandle element = rig.window.GetElement(newest);
	ASSERT_TRUE(element.IsValid());
	EXPECT_EQ(rig.OriginOf(element), rig.built.back());
}

TEST(EditorVirtualRowsTest, AppendRebuildsBeforeTrailing) {
	RowsRig rig;
	rig.wantsFooter = true;
	rig.window.Reset(100);
	rig.Settle();
	rig.ScrollTo(rig.MaxScrollY());
	ASSERT_TRUE(rig.window.IsRealized(99));
	ASSERT_TRUE(rig.context.IsAlive(rig.footer));

	rig.window.Appended(0, 1);
	rig.Settle();

	ASSERT_TRUE(rig.window.IsRealized(100));
	const u32 children = rig.ChildrenOfViewport();
	u32 footerAt = children;
	u32 appendedAt = children;
	for (u32 nth = 0; nth < children; ++nth) {
		const u64 tag = rig.TagOfChild(nth);
		if (tag == RowsRig::FOOTER_TAG) {
			footerAt = nth;
		} else if (tag == 101) {
			appendedAt = nth;
		}
	}
	ASSERT_LT(appendedAt, children);
	ASSERT_LT(footerAt, children);
	EXPECT_LT(appendedAt, footerAt);
}

TEST(EditorVirtualRowsTest, TallLastRowSettles) {
	RowsRig rig;
	rig.tallRow = 199;
	rig.window.Reset(200);
	rig.Settle();
	rig.ScrollTo(rig.MaxScrollY());
	rig.Settle();
	ASSERT_TRUE(rig.window.IsRealized(199));
	UI::ElementHandle tall = rig.window.GetElement(199);

	rig.built.clear();
	rig.Settle(6);

	EXPECT_EQ(rig.built.size(), 0u);
	EXPECT_TRUE(rig.context.IsAlive(tall));
	EXPECT_TRUE(rig.window.IsRealized(199));
}

TEST(EditorVirtualRowsTest, ResetRebuildsEvenWhenRangeIsIdentical) {
	RowsRig rig;
	rig.window.Reset(300);
	rig.Settle();
	const u32 firstBefore = rig.window.GetFirstRealized();
	UI::ElementHandle rowBefore = rig.window.GetElement(firstBefore);
	ASSERT_TRUE(rowBefore.IsValid());

	rig.built.clear();
	rig.window.Reset(300);
	rig.Settle();

	EXPECT_FALSE(rig.context.IsAlive(rowBefore));
	EXPECT_GT(rig.built.size(), 0u);
}

TEST(EditorVirtualRowsTest, RevealLandsInFirstSettle) {
	RowsRig rig({400, 200}, 40.0f);
	rig.window.Reset(20);
	rig.Settle();
	rig.ScrollTo(rig.MaxScrollY());
	ASSERT_GT(rig.MaxScrollY(), 0.0f);
	auto scrollY = [&rig]() { return rig.context.GetElement(rig.viewport)->scrollOffset.y; };

	rig.window.Reset(2000);
	rig.window.ScrollIntoView(1999);
	rig.context.Update();

	ASSERT_TRUE(rig.window.IsRealized(1999));
	const UI::Rect view = rig.context.GetLayout(rig.viewport).content;
	const UI::Rect row = rig.context.GetLayout(rig.window.GetElement(1999)).borderBox;
	ASSERT_GT(row.h, 0.0f);
	EXPECT_LE(row.y + row.h, view.y + view.h + 0.5f);
	EXPECT_GE(row.y, view.y - 0.5f);
	EXPECT_FLOAT_EQ(scrollY(), rig.MaxScrollY());

	const f32 landed = scrollY();
	rig.Settle();
	EXPECT_FLOAT_EQ(scrollY(), landed);
}

namespace {

	i64 StripeFor(RowsRig& rig, const UI::RenderList& list, UI::ElementHandle element) {
		const UI::Rect& view = rig.context.GetLayout(rig.viewport).borderBox;
		const UI::Rect& row = rig.context.GetLayout(element).borderBox;
		for (size_t i = 0; i < list.quads.size(); ++i) {
			const UI::Rect& quad = list.quads[i].rect;
			if (std::abs(quad.x - view.x) < 0.5f && std::abs(quad.w - view.w) < 0.5f &&
				std::abs(quad.y - row.y) < 0.5f && std::abs(quad.h - row.h) < 0.5f) {
				return static_cast<i64>(i);
			}
		}
		return -1;
	}

	u32 CountStripes(RowsRig& rig, const UI::RenderList& list) {
		u32 count = 0;
		for (u32 index = rig.window.GetFirstRealized(); index < rig.window.GetFirstRealized() + rig.Realized(); ++index) {
			if (StripeFor(rig, list, rig.window.GetElement(index)) >= 0) {
				++count;
			}
		}
		return count;
	}

} // anonymous namespace

TEST(EditorVirtualRowsTest, AlternateRowsStriped) {
	RowsRig rig;
	rig.window.Reset(8);
	rig.Settle();
	ASSERT_EQ(rig.Realized(), 8u);

	UI::ElementHandle filled = rig.window.GetElement(3);
	UI::Style style = rig.context.GetStyle(filled);
	style.background = UI::Brush::SolidColor(Color::White());
	rig.context.SetStyle(filled, style);
	rig.Settle();

	UI::RenderList list;
	rig.context.BuildRenderList(list);
	for (u32 index = 0; index < 8; ++index) {
		EXPECT_EQ(StripeFor(rig, list, rig.window.GetElement(index)) >= 0, index % 2 == 1)
			<< "row " << index << " is " << (index % 2 == 1 ? "not " : "") << "striped";
	}

	const i64 stripe = StripeFor(rig, list, filled);
	ASSERT_GE(stripe, 0);
	const UI::Rect& box = rig.context.GetLayout(filled).borderBox;
	i64 fill = -1;
	for (size_t i = 0; i < list.quads.size(); ++i) {
		const UI::Rect& quad = list.quads[i].rect;
		if (static_cast<i64>(i) != stripe && std::abs(quad.y - box.y) < 0.5f && std::abs(quad.h - box.h) < 0.5f) {
			fill = static_cast<i64>(i);
		}
	}
	ASSERT_GE(fill, 0);
	EXPECT_GT(fill, stripe);
}

TEST(EditorVirtualRowsTest, RowKeepsStripeWhenRowsLeaveFront) {
	RowsRig rig;
	rig.window.Reset(300);
	rig.Settle();
	rig.ScrollTo(rig.MaxScrollY());

	UI::RenderList list;
	rig.context.BuildRenderList(list);
	Vector<UI::ElementHandle> rows{GetMemoryResource(MemTag::Editor)};
	Vector<u8> striped{GetMemoryResource(MemTag::Editor)};
	for (u32 index = rig.window.GetFirstRealized(); index < rig.window.GetFirstRealized() + rig.Realized(); ++index) {
		UI::ElementHandle element = rig.window.GetElement(index);
		rows.push_back(element);
		striped.push_back(StripeFor(rig, list, element) >= 0 ? 1 : 0);
	}
	ASSERT_GT(CountStripes(rig, list), 0u);

	for (u32 tick = 0; tick < 3; ++tick) {
		rig.window.Appended(1, 1);
		rig.Settle(1);
	}
	rig.Settle();
	list.Clear();
	rig.context.BuildRenderList(list);

	u32 compared = 0;
	for (size_t i = 0; i < rows.size(); ++i) {
		if (!rig.context.IsAlive(rows[i]) || rig.context.GetLayout(rows[i]).borderBox.h <= 0.0f) {
			continue;
		}
		const UI::Rect& view = rig.context.GetLayout(rig.viewport).borderBox;
		const UI::Rect& box = rig.context.GetLayout(rows[i]).borderBox;
		if (box.y < view.y || box.y + box.h > view.y + view.h) {
			continue;
		}
		EXPECT_EQ(StripeFor(rig, list, rows[i]) >= 0, striped[i] != 0);
		++compared;
	}
	EXPECT_GT(compared, 3u);

	bool previous = false;
	bool first = true;
	for (u32 index = rig.window.GetFirstRealized(); index < rig.window.GetFirstRealized() + rig.Realized(); ++index) {
		const UI::Rect& view = rig.context.GetLayout(rig.viewport).borderBox;
		const UI::Rect& box = rig.context.GetLayout(rig.window.GetElement(index)).borderBox;
		if (box.y < view.y || box.y + box.h > view.y + view.h) {
			continue;
		}
		const bool now = StripeFor(rig, list, rig.window.GetElement(index)) >= 0;
		EXPECT_TRUE(first || now != previous) << "rows " << index - 1 << " and " << index;
		previous = now;
		first = false;
	}
}

TEST(EditorVirtualRowsTest, StripeFollowsTheme) {
	RowsRig rig;
	rig.window.Reset(8);
	rig.Settle();
	UI::RenderList list;
	rig.context.BuildRenderList(list);
	ASSERT_EQ(CountStripes(rig, list), 4u);

	EditorTheme::ThemeDesc bare = EditorTheme::MakeDefaultDesc();
	bare.Set(EditorTheme::ColorToken::RowStripe, "#00000000");
	constexpr StringHash BARE("StripelessTest");
	EditorTheme::Register(BARE, bare);
	ASSERT_TRUE(EditorTheme::SetActive(BARE));
	list.Clear();
	rig.context.BuildRenderList(list);
	EXPECT_EQ(CountStripes(rig, list), 0u);

	ASSERT_TRUE(EditorTheme::SetActive(EditorTheme::DarkTheme));
	list.Clear();
	rig.context.BuildRenderList(list);
	EXPECT_EQ(CountStripes(rig, list), 4u);
	EditorTheme::Shutdown();
}
