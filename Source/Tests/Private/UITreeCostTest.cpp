#include "TestsPCH.h"

#include "UITestUtil.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	UI::ContextDesc Desc() {
		UI::ContextDesc desc;
		desc.sizePx = {800, 600};
		desc.contentScale = 1.0f;
		return desc;
	}

	struct ListFixture {
		UI::Context ctx{Desc(), nullptr};
		UI::ElementHandle parent;
		Vector<UI::ElementHandle> children{GetMemoryResource(MemTag::Debug)};

		explicit ListFixture(u32 count) {
			parent = ctx.CreateElement();
			ctx.AppendChild(ctx.GetRoot(), parent);
			ctx.ResetTreeStats();
			for (u32 i = 0; i < count; ++i) {
				UI::ElementHandle child = ctx.CreateElement();
				ctx.AppendChild(parent, child);
				children.push_back(child);
			}
		}
	};

} // anonymous namespace

TEST(UITreeCostTest, BuildingASiblingListSearchesForNothing) {
	constexpr u32 COUNT = 400;
	ListFixture fixture(COUNT);
	UI::TreeStats stats = fixture.ctx.GetTreeStats();

	EXPECT_EQ(stats.linkOps, COUNT);
	EXPECT_EQ(stats.linkWork, 0u);
}

TEST(UITreeCostTest, ListBuildCostIsFlat) {
	ListFixture small(400);
	ListFixture large(1600);

	UI::TreeStats smallStats = small.ctx.GetTreeStats();
	UI::TreeStats largeStats = large.ctx.GetTreeStats();

	ASSERT_EQ(largeStats.linkOps, smallStats.linkOps * 4);
	EXPECT_EQ(smallStats.linkWork, largeStats.linkWork);
	EXPECT_EQ(largeStats.linkWork / largeStats.linkOps, 0u);
}

TEST(UITreeCostTest, FrontEvictionSearchesNothing) {
	constexpr u32 COUNT = 400;
	constexpr u32 EVICTED = 100;
	ListFixture fixture(COUNT);

	fixture.ctx.ResetTreeStats();
	for (u32 i = 0; i < EVICTED; ++i) {
		fixture.ctx.DestroyElement(fixture.children[i]);
	}
	UI::TreeStats stats = fixture.ctx.GetTreeStats();

	EXPECT_EQ(stats.linkOps, EVICTED) << "evicting " << EVICTED << " rows took " << stats.linkOps;
	EXPECT_EQ(stats.linkWork, 0u);
}

TEST(UITreeCostTest, ClearingALongListSearchesForNothing) {
	constexpr u32 COUNT = 400;
	ListFixture fixture(COUNT);

	fixture.ctx.ResetTreeStats();
	fixture.ctx.ClearChildren(fixture.parent);
	UI::TreeStats stats = fixture.ctx.GetTreeStats();

	EXPECT_EQ(fixture.ctx.GetElement(fixture.parent)->firstChild, UI::INVALID_UI_INDEX);
	EXPECT_EQ(stats.linkWork, 0u);
}

TEST(UITreeCostTest, RemovingFromMiddleCostsAtMostPosition) {
	constexpr u32 COUNT = 400;
	constexpr u32 POSITION = 100;
	ListFixture fixture(COUNT);

	fixture.ctx.ResetTreeStats();
	fixture.ctx.DestroyElement(fixture.children[POSITION]);
	UI::TreeStats stats = fixture.ctx.GetTreeStats();

	EXPECT_LE(stats.linkWork, static_cast<u64>(POSITION));
}
