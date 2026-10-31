#include "TestsPCH.h"

#include "Shalltear/Editor/EditorEvents.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorSelectionOps.h"

using namespace Shalltear;

namespace {

	class SelectionListener : public Object {
	public:
		i32 events = 0;

		void OnChanged(const EditorSelectionChangedEvent&) { ++events; }

		void SubscribeSelf() {
			EditorEvents::SelectionChanged.Subscribe(
				EventBus::GetGlobal(), Handle<SelectionListener>(this), &SelectionListener::OnChanged);
		}
		~SelectionListener() override {
			EventBus::GetGlobal().UnsubscribeAll(GetInstanceId());
		}
	};

	class EditorSelectionTest : public ::testing::Test {
	protected:
		void SetUp() override {
			EditorSelection::Initialize();
			_listener.SubscribeSelf();
		}
		void TearDown() override {
			EditorSelection::Terminate();
		}

		SelectionListener _listener;
	};

} // anonymous namespace

TEST_F(EditorSelectionTest, SelectReplacesAndSetsActive) {
	Object a, b;
	EditorSelection::Select(Handle<Object>(&a));
	EditorSelection::Select(Handle<Object>(&b));

	auto selection = EditorSelection::GetSelection();
	ASSERT_EQ(selection.size(), 1u);
	EXPECT_EQ(selection[0].GetInstanceId(), b.GetInstanceId());
	EXPECT_EQ(EditorSelection::GetActive().GetInstanceId(), b.GetInstanceId());
	EXPECT_FALSE(EditorSelection::IsSelected(Handle<Object>(&a)));
	EXPECT_EQ(_listener.events, 2);
}

TEST_F(EditorSelectionTest, SelectSameObjectPublishesOnce) {
	Object a;
	EditorSelection::Select(Handle<Object>(&a));
	EditorSelection::Select(Handle<Object>(&a));
	EXPECT_EQ(_listener.events, 1);
}

TEST_F(EditorSelectionTest, AddToSelectionAppendsAndPromotesActive) {
	Object a, b;
	EditorSelection::Select(Handle<Object>(&a));
	EditorSelection::AddToSelection(Handle<Object>(&b));

	auto selection = EditorSelection::GetSelection();
	ASSERT_EQ(selection.size(), 2u);
	EXPECT_EQ(selection[0].GetInstanceId(), a.GetInstanceId());
	EXPECT_EQ(selection[1].GetInstanceId(), b.GetInstanceId());
	EXPECT_EQ(EditorSelection::GetActive().GetInstanceId(), b.GetInstanceId());

	i32 before = _listener.events;
	EditorSelection::AddToSelection(Handle<Object>(&a));
	EXPECT_EQ(EditorSelection::GetActive().GetInstanceId(), a.GetInstanceId());
	EXPECT_EQ(_listener.events, before);
}

TEST_F(EditorSelectionTest, RemoveOfActivePromotesLastRemaining) {
	Object a, b, c;
	EditorSelection::Select(Handle<Object>(&a));
	EditorSelection::AddToSelection(Handle<Object>(&b));
	EditorSelection::AddToSelection(Handle<Object>(&c));

	EditorSelection::RemoveFromSelection(Handle<Object>(&c));
	EXPECT_EQ(EditorSelection::GetActive().GetInstanceId(), b.GetInstanceId());
	EXPECT_EQ(EditorSelection::GetSelectionCount(), 2u);

	i32 before = _listener.events;
	EditorSelection::RemoveFromSelection(Handle<Object>(&c));
	EXPECT_EQ(_listener.events, before);
}

TEST_F(EditorSelectionTest, ToggleAddsThenRemoves) {
	Object a;
	EditorSelection::ToggleInSelection(Handle<Object>(&a));
	EXPECT_TRUE(EditorSelection::IsSelected(Handle<Object>(&a)));
	EditorSelection::ToggleInSelection(Handle<Object>(&a));
	EXPECT_FALSE(EditorSelection::IsSelected(Handle<Object>(&a)));
	EXPECT_EQ(_listener.events, 2);
}

TEST_F(EditorSelectionTest, ClearEmptiesEverythingAndPublishesOnce) {
	Object a;
	EditorSelection::Select(Handle<Object>(&a));
	EditorSelection::Clear();
	EXPECT_EQ(EditorSelection::GetSelectionCount(), 0u);
	EXPECT_FALSE(EditorSelection::GetActive().IsValid());
	EXPECT_EQ(_listener.events, 2);

	EditorSelection::Clear();
	EXPECT_EQ(_listener.events, 2);
}

TEST_F(EditorSelectionTest, DeadHandlesCompactOnRead) {
	Object a;
	{
		Object dying;
		EditorSelection::Select(Handle<Object>(&a));
		EditorSelection::AddToSelection(Handle<Object>(&dying));
		EXPECT_EQ(EditorSelection::GetSelectionCount(), 2u);
	}
	i32 before = _listener.events;
	auto selection = EditorSelection::GetSelection();
	ASSERT_EQ(selection.size(), 1u);
	EXPECT_EQ(selection[0].GetInstanceId(), a.GetInstanceId());
	EXPECT_EQ(EditorSelection::GetActive().GetInstanceId(), a.GetInstanceId());
	EXPECT_EQ(_listener.events, before);
}

TEST_F(EditorSelectionTest, AssetSelectionIsExclusiveWithObjects) {
	Object a;
	EditorSelection::Select(Handle<Object>(&a));

	Guid asset{0x1234, 0x5678};
	EditorSelection::SelectAsset(asset);
	EXPECT_TRUE(EditorSelection::HasAssetSelection());
	EXPECT_EQ(EditorSelection::GetSelectedAsset(), asset);
	EXPECT_EQ(EditorSelection::GetAssetSelectionCount(), 1u);
	EXPECT_EQ(EditorSelection::GetSelectionCount(), 0u);

	EditorSelection::Select(Handle<Object>(&a));
	EXPECT_FALSE(EditorSelection::HasAssetSelection());
	EXPECT_EQ(EditorSelection::GetAssetSelectionCount(), 0u);
	EXPECT_EQ(_listener.events, 3);
}

TEST_F(EditorSelectionTest, SelectAssetsLastIsActive) {
	const Guid run[] = {{1, 0}, {2, 0}, {3, 0}};
	EditorSelection::SelectAssets(run);

	EXPECT_EQ(EditorSelection::GetAssetSelectionCount(), 3u);
	EXPECT_TRUE(EditorSelection::HasAssetSelection());
	EXPECT_EQ(EditorSelection::GetSelectedAsset(), run[2]);
	EXPECT_EQ(_listener.events, 1);
}

TEST_F(EditorSelectionTest, SelectAssetsDropsNullsAndDuplicates) {
	const Guid a{1, 0};
	const Guid b{2, 0};
	const Guid run[] = {a, {}, b, a};
	EditorSelection::SelectAssets(run);

	EXPECT_EQ(EditorSelection::GetAssetSelectionCount(), 2u);
	EXPECT_EQ(EditorSelection::GetSelectedAsset(), b);
}

TEST_F(EditorSelectionTest, EmptySelectAssetsClears) {
	EditorSelection::SelectAsset(Guid{1, 0});
	i32 before = _listener.events;

	const Guid nulls[] = {{}, {}};
	EditorSelection::SelectAssets(nulls);
	EXPECT_FALSE(EditorSelection::HasAssetSelection());
	EXPECT_EQ(EditorSelection::GetAssetSelectionCount(), 0u);
	EXPECT_EQ(EditorSelection::GetSelectedAsset(), Guid{});
	EXPECT_EQ(_listener.events, before + 1);
}

TEST_F(EditorSelectionTest, ReselectingTheSameAssetRunPublishesOnce) {
	const Guid run[] = {{1, 0}, {2, 0}};
	EditorSelection::SelectAssets(run);
	EditorSelection::SelectAssets(run);
	EXPECT_EQ(_listener.events, 1);

	const Guid other[] = {{1, 0}, {3, 0}};
	EditorSelection::SelectAssets(other);
	EXPECT_EQ(_listener.events, 2);
}

TEST_F(EditorSelectionTest, SelectAssetNarrowsMultiSelectionToOne) {
	const Guid run[] = {{1, 0}, {2, 0}, {3, 0}};
	EditorSelection::SelectAssets(run);
	EditorSelection::SelectAsset(run[0]);

	EXPECT_EQ(EditorSelection::GetAssetSelectionCount(), 1u);
	EXPECT_EQ(EditorSelection::GetSelectedAsset(), run[0]);
}

TEST_F(EditorSelectionTest, ObjectSelectionClearsAssets) {
	Object a;
	const Guid run[] = {{1, 0}, {2, 0}, {3, 0}};
	EditorSelection::SelectAssets(run);

	EditorSelection::Select(Handle<Object>(&a));
	EXPECT_EQ(EditorSelection::GetAssetSelectionCount(), 0u);
	EXPECT_FALSE(EditorSelection::HasAssetSelection());
}

TEST(EditorSelectionOpsTest, RangeIncludesBothEnds) {
	const u64 rows[] = {10, 20, 30, 40, 50};
	u32 first = 99;
	u32 last = 99;

	ASSERT_TRUE(EditorSelectionOps::ResolveRange(rows, 20, 40, first, last));
	EXPECT_EQ(first, 1u);
	EXPECT_EQ(last, 3u);

	ASSERT_TRUE(EditorSelectionOps::ResolveRange(rows, 40, 20, first, last));
	EXPECT_EQ(first, 1u);
	EXPECT_EQ(last, 3u);
}

TEST(EditorSelectionOpsTest, SelfRangeIsOneRow) {
	const u64 rows[] = {10, 20, 30};
	u32 first = 99;
	u32 last = 99;
	ASSERT_TRUE(EditorSelectionOps::ResolveRange(rows, 20, 20, first, last));
	EXPECT_EQ(first, 1u);
	EXPECT_EQ(last, 1u);
}

TEST(EditorSelectionOpsTest, AnAbsentEndIsNotARange) {
	const u64 rows[] = {10, 20, 30};
	u32 first = 99;
	u32 last = 99;

	EXPECT_FALSE(EditorSelectionOps::ResolveRange(rows, 15, 30, first, last));
	EXPECT_FALSE(EditorSelectionOps::ResolveRange(rows, 10, 35, first, last));
	EXPECT_FALSE(EditorSelectionOps::ResolveRange({}, 10, 10, first, last));
	EXPECT_EQ(first, 99u);
	EXPECT_EQ(last, 99u);
}

TEST_F(EditorSelectionTest, AddToSelectionClearsAssets) {
	Object a;
	const Guid run[] = {{1, 0}, {2, 0}};
	EditorSelection::SelectAssets(run);
	i32 before = _listener.events;

	EditorSelection::AddToSelection(Handle<Object>(&a));
	EXPECT_EQ(EditorSelection::GetAssetSelectionCount(), 0u);
	EXPECT_EQ(EditorSelection::GetSelectionCount(), 1u);
	EXPECT_EQ(_listener.events, before + 1);
}
