#include "TestsPCH.h"

#include "UITestUtil.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	UI::ContextDesc Desc(i32 width = 800, i32 height = 600, f32 scale = 1.0f) {
		UI::ContextDesc desc;
		desc.sizePx = {width, height};
		desc.contentScale = scale;
		return desc;
	}

	u32 WalkToTail(const UI::Context& ctx, UI::ElementHandle parent) {
		const UI::Element* element = ctx.GetElement(parent);
		u32 tail = UI::INVALID_UI_INDEX;
		for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
			tail = child;
			child = ctx.GetElement(ctx.MakeHandle(child))->nextSibling;
		}
		return tail;
	}

	void ExpectTailIsSound(const UI::Context& ctx, UI::ElementHandle parent, const char* after) {
		const UI::Element* element = ctx.GetElement(parent);
		ASSERT_NE(element, nullptr) << after;
		EXPECT_EQ(element->lastChild, WalkToTail(ctx, parent));
		EXPECT_EQ(element->firstChild == UI::INVALID_UI_INDEX, element->lastChild == UI::INVALID_UI_INDEX);
	}

	Vector<UI::ElementHandle> ChildrenInOrder(const UI::Context& ctx, UI::ElementHandle parent) {
		Vector<UI::ElementHandle> out{GetMemoryResource(MemTag::Debug)};
		const UI::Element* element = ctx.GetElement(parent);
		for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = ctx.MakeHandle(child);
			out.push_back(handle);
			child = ctx.GetElement(handle)->nextSibling;
		}
		return out;
	}

} // anonymous namespace

TEST(UIElementTest, CreateDestroyRecyclesGenerations) {
	UI::Context ctx(Desc(), nullptr);
	EXPECT_EQ(ctx.GetLiveCount(), 0u);

	UI::ElementHandle first = ctx.CreateElement();
	ASSERT_TRUE(ctx.IsAlive(first));
	EXPECT_EQ(ctx.GetLiveCount(), 1u);

	ctx.DestroyElement(first);
	EXPECT_FALSE(ctx.IsAlive(first));
	EXPECT_EQ(ctx.GetLiveCount(), 0u);

	UI::ElementHandle second = ctx.CreateElement();
	ASSERT_TRUE(ctx.IsAlive(second));
	EXPECT_EQ(second.index, first.index);
	EXPECT_NE(second.generation, first.generation);
	EXPECT_FALSE(ctx.IsAlive(first));
	EXPECT_EQ(ctx.GetElement(first), nullptr);
}

TEST(UIElementTest, ForeignHandleIsDead) {
	UI::Context first(Desc(), nullptr);
	UI::Context second(Desc(), nullptr);

	for (u32 i = 0; i < 40; ++i) {
		UI::ElementHandle mine = first.CreateElement();
		UI::ElementHandle theirs = second.CreateElement();
		ASSERT_EQ(mine.index, theirs.index);
		ASSERT_EQ(mine.generation, theirs.generation);

		EXPECT_TRUE(first.IsAlive(mine));
		EXPECT_TRUE(second.IsAlive(theirs));
		EXPECT_FALSE(second.IsAlive(mine));
		EXPECT_FALSE(first.IsAlive(theirs));
		EXPECT_EQ(second.GetElement(mine), nullptr);
		EXPECT_EQ(first.GetElement(theirs), nullptr);
	}

	UI::ElementHandle recycledMine = first.CreateElement();
	UI::ElementHandle recycledTheirs = second.CreateElement();
	first.DestroyElement(recycledMine);
	second.DestroyElement(recycledTheirs);
	recycledMine = first.CreateElement();
	recycledTheirs = second.CreateElement();
	ASSERT_EQ(recycledMine.index, recycledTheirs.index);
	ASSERT_EQ(recycledMine.generation, recycledTheirs.generation);
	ASSERT_GT(recycledMine.generation, 1u);
	EXPECT_FALSE(second.IsAlive(recycledMine));
	EXPECT_FALSE(first.IsAlive(recycledTheirs));

	EXPECT_FALSE(first.IsAlive({}));
	EXPECT_FALSE(second.IsAlive({}));
}

TEST(UIElementTest, AppendChildKeepsSiblingOrder) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle root = ctx.GetRoot();
	UI::ElementHandle a = ctx.CreateElement();
	UI::ElementHandle b = ctx.CreateElement();
	UI::ElementHandle c = ctx.CreateElement();
	ctx.AppendChild(root, a);
	ctx.AppendChild(root, b);
	ctx.AppendChild(root, c);

	const UI::Element* rootElement = ctx.GetElement(root);
	ASSERT_NE(rootElement, nullptr);
	EXPECT_EQ(rootElement->firstChild, a.index);
	EXPECT_EQ(ctx.GetElement(a)->nextSibling, b.index);
	EXPECT_EQ(ctx.GetElement(b)->nextSibling, c.index);
	EXPECT_EQ(ctx.GetElement(c)->nextSibling, UI::INVALID_UI_INDEX);
	EXPECT_EQ(ctx.GetElement(a)->parent, root.index);
}

TEST(UIElementTest, AppendChildReparentsAndReorders) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle root = ctx.GetRoot();
	UI::ElementHandle a = ctx.CreateElement();
	UI::ElementHandle b = ctx.CreateElement();
	ctx.AppendChild(root, a);
	ctx.AppendChild(root, b);

	ctx.AppendChild(root, a);
	EXPECT_EQ(ctx.GetElement(root)->firstChild, b.index);
	EXPECT_EQ(ctx.GetElement(b)->nextSibling, a.index);

	ctx.AppendChild(b, a);
	EXPECT_EQ(ctx.GetElement(root)->firstChild, b.index);
	EXPECT_EQ(ctx.GetElement(b)->nextSibling, UI::INVALID_UI_INDEX);
	EXPECT_EQ(ctx.GetElement(b)->firstChild, a.index);
	EXPECT_EQ(ctx.GetElement(a)->parent, b.index);
}

TEST(UIElementTest, DestroyElementIsRecursive) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle root = ctx.GetRoot();
	UI::ElementHandle parent = ctx.CreateElement();
	UI::ElementHandle child = ctx.CreateElement();
	UI::ElementHandle grandchild = ctx.CreateElement();
	ctx.AppendChild(root, parent);
	ctx.AppendChild(parent, child);
	ctx.AppendChild(child, grandchild);
	EXPECT_EQ(ctx.GetLiveCount(), 3u);

	ctx.DestroyElement(parent);
	EXPECT_FALSE(ctx.IsAlive(parent));
	EXPECT_FALSE(ctx.IsAlive(child));
	EXPECT_FALSE(ctx.IsAlive(grandchild));
	EXPECT_EQ(ctx.GetLiveCount(), 0u);
	EXPECT_EQ(ctx.GetElement(root)->firstChild, UI::INVALID_UI_INDEX);
}

TEST(UIElementTest, ClearChildrenKeepsTheParent) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle root = ctx.GetRoot();
	UI::ElementHandle parent = ctx.CreateElement();
	ctx.AppendChild(root, parent);
	ctx.AppendChild(parent, ctx.CreateElement());
	ctx.AppendChild(parent, ctx.CreateElement());
	EXPECT_EQ(ctx.GetLiveCount(), 3u);

	ctx.ClearChildren(parent);
	EXPECT_TRUE(ctx.IsAlive(parent));
	EXPECT_EQ(ctx.GetLiveCount(), 1u);
	EXPECT_EQ(ctx.GetElement(parent)->firstChild, UI::INVALID_UI_INDEX);
}

TEST(UIElementTest, TailLinkSurvivesEveryWayChildLeaves) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle parent = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), parent);

	UI::ElementHandle a = ctx.CreateElement();
	UI::ElementHandle b = ctx.CreateElement();
	UI::ElementHandle c = ctx.CreateElement();
	ctx.AppendChild(parent, a);
	ExpectTailIsSound(ctx, parent, "the first append");
	ctx.AppendChild(parent, b);
	ctx.AppendChild(parent, c);
	ExpectTailIsSound(ctx, parent, "three appends");
	EXPECT_EQ(ctx.GetElement(parent)->lastChild, c.index);

	ctx.DestroyElement(c);
	ExpectTailIsSound(ctx, parent, "destroying the tail");
	EXPECT_EQ(ctx.GetElement(parent)->lastChild, b.index);

	ctx.DestroyElement(a);
	ExpectTailIsSound(ctx, parent, "destroying the head");
	EXPECT_EQ(ctx.GetElement(parent)->firstChild, b.index);
	EXPECT_EQ(ctx.GetElement(parent)->lastChild, b.index);

	ctx.DestroyElement(b);
	ExpectTailIsSound(ctx, parent, "destroying the only child");

	UI::ElementHandle d = ctx.CreateElement();
	UI::ElementHandle e = ctx.CreateElement();
	UI::ElementHandle f = ctx.CreateElement();
	ctx.AppendChild(parent, d);
	ctx.AppendChild(parent, e);
	ctx.AppendChild(parent, f);
	UI::ElementHandle elsewhere = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), elsewhere);
	ctx.AppendChild(elsewhere, e);
	ExpectTailIsSound(ctx, parent, "reparenting a middle child away");
	ExpectTailIsSound(ctx, elsewhere, "reparenting a middle child in");
	EXPECT_EQ(ctx.GetElement(parent)->lastChild, f.index);

	ctx.AppendChild(parent, f);
	ExpectTailIsSound(ctx, parent, "re-appending the tail to its parent");
	EXPECT_EQ(ctx.GetElement(parent)->firstChild, d.index);
	EXPECT_EQ(ctx.GetElement(parent)->lastChild, f.index);

	ctx.DestroyElement(f);
	ctx.AppendChild(parent, d);
	ExpectTailIsSound(ctx, parent, "re-appending the only child to its parent");
	EXPECT_EQ(ctx.GetElement(parent)->firstChild, d.index);
	EXPECT_EQ(ctx.GetElement(parent)->lastChild, d.index);

	ctx.ClearChildren(parent);
	ExpectTailIsSound(ctx, parent, "ClearChildren");
	EXPECT_EQ(ctx.GetElement(parent)->lastChild, UI::INVALID_UI_INDEX);
}

TEST(UIElementTest, LongListKeepsAppendOrder) {
	constexpr u32 COUNT = 400;
	constexpr f32 ROW_HEIGHT = 3.0f;

	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle column = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), column);
	UI::Style columnStyle;
	columnStyle.flexDirection = UI::FlexDirection::Column;
	columnStyle.alignItems = UI::Align::Stretch;
	ctx.SetStyle(column, columnStyle);

	Vector<UI::ElementHandle> appended{GetMemoryResource(MemTag::Debug)};
	for (u32 i = 0; i < COUNT; ++i) {
		UI::ElementHandle row = ctx.CreateElement();
		ctx.AppendChild(column, row);
		UI::Style rowStyle;
		rowStyle.height = UI::Length::Pixel(ROW_HEIGHT);
		rowStyle.flexShrink = 0.0f;
		ctx.SetStyle(row, rowStyle);
		appended.push_back(row);
	}
	ExpectTailIsSound(ctx, column, "400 appends");

	Vector<UI::ElementHandle> walked = ChildrenInOrder(ctx, column);
	ASSERT_EQ(walked.size(), appended.size()) << walked.size() << " children after " << appended.size() << " appends";
	for (size_t i = 0; i < walked.size(); ++i) {
		ASSERT_EQ(walked[i].index, appended[i].index) << "sibling " << i;
		ASSERT_EQ(walked[i].generation, appended[i].generation) << "sibling " << i;
	}

	ctx.Update();
	f32 top = ctx.GetLayout(column).content.y;
	for (u32 i = 0; i < COUNT; ++i) {
		EXPECT_NEAR(ctx.GetLayout(appended[i]).borderBox.y, top + static_cast<f32>(i) * ROW_HEIGHT, 0.01f);
	}
}

TEST(UIElementTest, AppendLeavesExistingAlone) {
	constexpr u32 COUNT = 400;
	constexpr f32 ROW_HEIGHT = 3.0f;

	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle column = ctx.CreateElement();
	ctx.AppendChild(ctx.GetRoot(), column);
	UI::Style columnStyle;
	columnStyle.flexDirection = UI::FlexDirection::Column;
	columnStyle.alignItems = UI::Align::Stretch;
	ctx.SetStyle(column, columnStyle);

	UI::Style rowStyle;
	rowStyle.height = UI::Length::Pixel(ROW_HEIGHT);
	rowStyle.flexShrink = 0.0f;

	Vector<UI::ElementHandle> rows{GetMemoryResource(MemTag::Debug)};
	for (u32 i = 0; i < COUNT; ++i) {
		UI::ElementHandle row = ctx.CreateElement();
		ctx.AppendChild(column, row);
		ctx.SetStyle(row, rowStyle);
		rows.push_back(row);
	}
	ctx.Update();

	Vector<UI::Rect> before{GetMemoryResource(MemTag::Debug)};
	for (UI::ElementHandle row : rows) {
		before.push_back(ctx.GetLayout(row).borderBox);
	}

	UI::ElementHandle arrival = ctx.CreateElement();
	ctx.AppendChild(column, arrival);
	ctx.SetStyle(arrival, rowStyle);
	ctx.Update();

	for (u32 i = 0; i < COUNT; ++i) {
		ASSERT_TRUE(ctx.IsAlive(rows[i])) << "row " << i;
		EXPECT_EQ(ctx.GetLayout(rows[i]).borderBox, before[i]) << "row " << i;
	}
	EXPECT_EQ(ctx.GetElement(column)->lastChild, arrival.index);
	EXPECT_NEAR(ctx.GetLayout(arrival).borderBox.y, before.back().y + ROW_HEIGHT, 0.01f);
}

TEST(UIElementTest, ClassSetAddsRemovesAndCaps) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle element = ctx.CreateElement();

	ctx.AddClass(element, "one"_hash);
	ctx.AddClass(element, "two"_hash);
	ctx.AddClass(element, "one"_hash);
	EXPECT_TRUE(ctx.HasClass(element, "one"_hash));
	EXPECT_TRUE(ctx.HasClass(element, "two"_hash));
	EXPECT_EQ(ctx.GetElement(element)->classCount, 2u);

	ctx.RemoveClass(element, "one"_hash);
	EXPECT_FALSE(ctx.HasClass(element, "one"_hash));
	EXPECT_EQ(ctx.GetElement(element)->classCount, 1u);

	ctx.AddClass(element, "a"_hash);
	ctx.AddClass(element, "b"_hash);
	ctx.AddClass(element, "c"_hash);
	ctx.AddClass(element, "overflow"_hash);
	EXPECT_EQ(ctx.GetElement(element)->classCount, UI::MAX_ELEMENT_CLASSES);
	EXPECT_FALSE(ctx.HasClass(element, "overflow"_hash));
}

TEST(UIElementTest, UserTagAndTintPersist) {
	UI::Context ctx(Desc(), nullptr);
	UI::ElementHandle element = ctx.CreateElement();
	ctx.SetUserTag(element, 0xC0FFEEu);
	ctx.SetTint(element, Color(0.5f, 0.25f, 0.125f, 1.0f));
	EXPECT_EQ(ctx.GetElement(element)->userTag, 0xC0FFEEu);
	EXPECT_EQ(ctx.GetElement(element)->tint, Color(0.5f, 0.25f, 0.125f, 1.0f));
}
