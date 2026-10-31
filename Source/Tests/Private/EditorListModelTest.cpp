#include "TestsPCH.h"

#include "Shalltear/Editor/EditorListModel.h"

#include <limits>

using namespace Shalltear;

namespace {

	constexpr u64 ROWS[] = {10, 20, 30, 40, 50};

	ListSelection MakeList(ListSelectionMode mode = ListSelectionMode::Multiple) {
		ListSelection list{mode};
		list.SetRows(ROWS);
		return list;
	}

	void ExpectSelection(const ListSelection& list, std::initializer_list<u64> expected) {
		std::span<const u64> actual = list.GetSelected();
		ASSERT_EQ(actual.size(), expected.size());
		size_t i = 0;
		for (u64 id : expected) {
			EXPECT_EQ(actual[i], id) << i;
			++i;
		}
	}

} // anonymous namespace

TEST(EditorListModelTest, PlainClickReplacesSelectionAndTakesAnchor) {
	ListSelection list = MakeList();

	EXPECT_TRUE(list.ApplyClick(30, KeyMod::None));
	ExpectSelection(list, {30});
	EXPECT_EQ(list.GetActive(), 30u);
	EXPECT_EQ(list.GetCursor(), 30u);

	EXPECT_TRUE(list.ApplyClick(10, KeyMod::None));
	ExpectSelection(list, {10});

	EXPECT_TRUE(list.ApplyClick(30, KeyMod::Shift));
	ExpectSelection(list, {10, 20, 30});
}

TEST(EditorListModelTest, CtrlTogglesRowActiveFollows) {
	ListSelection list = MakeList();
	list.ApplyClick(20, KeyMod::None);

	EXPECT_TRUE(list.ApplyClick(40, KeyMod::Ctrl));
	ExpectSelection(list, {20, 40});
	EXPECT_EQ(list.GetActive(), 40u);
	EXPECT_TRUE(list.IsSelected(20));
	EXPECT_EQ(list.GetCount(), 2u);

	EXPECT_TRUE(list.ApplyClick(40, KeyMod::Ctrl));
	ExpectSelection(list, {20});
	EXPECT_EQ(list.GetActive(), 20u);
	EXPECT_FALSE(list.IsSelected(40));
}

TEST(EditorListModelTest, RangeSpansBothWays) {
	ListSelection list = MakeList();
	list.ApplyClick(20, KeyMod::None);

	EXPECT_TRUE(list.ApplyClick(40, KeyMod::Shift));
	ExpectSelection(list, {20, 30, 40});
	EXPECT_EQ(list.GetActive(), 40u);

	EXPECT_TRUE(list.ApplyClick(10, KeyMod::Shift));
	ExpectSelection(list, {20, 10});
	EXPECT_EQ(list.GetActive(), 10u);
	EXPECT_EQ(list.GetCursor(), 10u);
}

TEST(EditorListModelTest, HiddenAnchorFallsBackToClick) {
	ListSelection list = MakeList();
	list.ApplyClick(20, KeyMod::None);

	const u64 folded[] = {10, 30, 40, 50};
	list.SetRows(folded);
	EXPECT_TRUE(list.ApplyClick(50, KeyMod::Shift));
	ExpectSelection(list, {50});
	EXPECT_EQ(list.GetActive(), 50u);
}

TEST(EditorListModelTest, CtrlBeatsShiftOnAClick) {
	ListSelection list = MakeList();
	list.ApplyClick(10, KeyMod::None);

	EXPECT_TRUE(list.ApplyClick(40, KeyMod::Ctrl | KeyMod::Shift));
	ExpectSelection(list, {10, 40});
}

TEST(EditorListModelTest, HiddenRowClickChangesNothing) {
	ListSelection list = MakeList();
	list.ApplyClick(30, KeyMod::None);

	EXPECT_FALSE(list.ApplyClick(25, KeyMod::None));
	EXPECT_FALSE(list.ApplyClick(0, KeyMod::Ctrl));
	EXPECT_FALSE(list.SelectOnly(25));
	ExpectSelection(list, {30});
	EXPECT_EQ(list.GetCursor(), 30u);
}

TEST(EditorListModelTest, AGestureThatChangesNothingReportsFalse) {
	ListSelection list = MakeList();
	EXPECT_TRUE(list.ApplyClick(30, KeyMod::None));

	EXPECT_FALSE(list.ApplyClick(30, KeyMod::None));
	EXPECT_FALSE(list.SelectOnly(30));
}

TEST(EditorListModelTest, SetRowsPrunesKeepsOrder) {
	ListSelection list = MakeList();
	list.ApplyClick(20, KeyMod::None);
	list.ApplyClick(40, KeyMod::Ctrl);
	list.ApplyClick(10, KeyMod::Ctrl);
	ExpectSelection(list, {20, 40, 10});

	const u64 remaining[] = {10, 20, 50};
	list.SetRows(remaining);

	ExpectSelection(list, {20, 10});
	EXPECT_EQ(list.GetActive(), 10u);
	EXPECT_EQ(list.GetCount(), 2u);
	EXPECT_FALSE(list.IsSelected(40));
}

TEST(EditorListModelTest, CursorStaysBesideDeletedRow) {
	ListSelection list = MakeList();
	list.ApplyClick(30, KeyMod::None);
	EXPECT_EQ(list.GetCursor(), 30u);

	const u64 remaining[] = {10, 20, 40, 50};
	list.SetRows(remaining);
	EXPECT_EQ(list.GetCursor(), 40u);

	EXPECT_TRUE(list.MoveCursor(1, KeyMod::None));
	EXPECT_EQ(list.GetCursor(), 50u);
}

TEST(EditorListModelTest, DeletedAnchorNotReplaced) {
	ListSelection list = MakeList();
	list.ApplyClick(30, KeyMod::None);

	const u64 remaining[] = {10, 20, 40, 50};
	list.SetRows(remaining);
	ASSERT_EQ(list.GetCursor(), 40u);

	list.ApplyClick(10, KeyMod::Shift);
	EXPECT_EQ(list.GetCount(), 1u);
	EXPECT_TRUE(list.IsSelected(10));
}

TEST(EditorListModelTest, CursorFallsBackwards) {
	ListSelection list = MakeList();
	list.ApplyClick(50, KeyMod::None);

	const u64 remaining[] = {10, 20, 30};
	list.SetRows(remaining);
	EXPECT_EQ(list.GetCursor(), 30u);
}

TEST(EditorListModelTest, NoSurvivorNoCursor) {
	ListSelection list = MakeList();
	list.ApplyClick(30, KeyMod::None);

	list.SetRows({});
	EXPECT_EQ(list.GetCursor(), 0u);

	const u64 refilled[] = {10, 20, 30};
	list.SetRows(refilled);
	EXPECT_TRUE(list.MoveCursor(1, KeyMod::None));
	EXPECT_EQ(list.GetCursor(), 10u);
}

TEST(EditorListModelTest, IdsSurviveFoldAndReorder) {
	ListSelection list = MakeList();
	list.ApplyClick(50, KeyMod::None);
	list.ApplyClick(20, KeyMod::Ctrl);

	const u64 rebuilt[] = {50, 60, 20, 70, 10};
	list.SetRows(rebuilt);
	ExpectSelection(list, {50, 20});
	EXPECT_EQ(list.GetCursor(), 20u);

	EXPECT_TRUE(list.ApplyClick(10, KeyMod::Shift));
	ExpectSelection(list, {20, 70, 10});
}

TEST(EditorListModelTest, SetRowsDropsZeroId) {
	ListSelection list;
	const u64 withHole[] = {10, 0, 30};
	list.SetRows(withHole);

	EXPECT_FALSE(list.ApplyClick(0, KeyMod::None));
	EXPECT_EQ(list.GetCursor(), 0u);

	list.ApplyClick(10, KeyMod::None);
	EXPECT_TRUE(list.ApplyClick(30, KeyMod::Shift));
	ExpectSelection(list, {10, 30});
}

TEST(EditorListModelTest, SelectAllKeepsActiveRow) {
	ListSelection list = MakeList();
	list.ApplyClick(30, KeyMod::None);

	EXPECT_TRUE(list.SelectAll());
	EXPECT_EQ(list.GetCount(), 5u);
	ExpectSelection(list, {10, 20, 40, 50, 30});
	EXPECT_EQ(list.GetActive(), 30u);

	EXPECT_FALSE(list.SelectAll());
}

TEST(EditorListModelTest, SelectAllCoversDrawnRows) {
	ListSelection list = MakeList();

	const u64 folded[] = {10, 50};
	list.SetRows(folded);
	EXPECT_TRUE(list.SelectAll());
	ExpectSelection(list, {10, 50});
	EXPECT_FALSE(list.IsSelected(30));
}

TEST(EditorListModelTest, ClearDropsKeyboardPlace) {
	ListSelection list = MakeList();
	list.ApplyClick(30, KeyMod::None);

	EXPECT_TRUE(list.Clear());
	EXPECT_EQ(list.GetCount(), 0u);
	EXPECT_EQ(list.GetActive(), 0u);
	EXPECT_EQ(list.GetCursor(), 0u);
	EXPECT_FALSE(list.Clear());

	EXPECT_TRUE(list.MoveCursor(1, KeyMod::None));
	ExpectSelection(list, {10});
	EXPECT_TRUE(list.MoveCursor(1, KeyMod::Shift));
	ExpectSelection(list, {10, 20});
}

TEST(EditorListModelTest, ExternalClearDropsKeyboardPlace) {
	ListSelection list = MakeList();
	list.ApplyClick(30, KeyMod::None);

	EXPECT_TRUE(list.SetSelection({}));
	EXPECT_EQ(list.GetCursor(), 0u);
	EXPECT_TRUE(list.MoveCursor(1, KeyMod::None));
	ExpectSelection(list, {10});

	const u64 handed[] = {40};
	EXPECT_TRUE(list.SetSelection(handed));
	EXPECT_EQ(list.GetCursor(), 40u);
}

TEST(EditorListModelTest, SingleRefusesToGrowUnderEitherModifier) {
	ListSelection list = MakeList(ListSelectionMode::Single);
	list.ApplyClick(20, KeyMod::None);

	EXPECT_TRUE(list.ApplyClick(40, KeyMod::Ctrl));
	ExpectSelection(list, {40});
	EXPECT_TRUE(list.ApplyClick(10, KeyMod::Shift));
	ExpectSelection(list, {10});

	EXPECT_FALSE(list.ApplyClick(10, KeyMod::Ctrl));
	ExpectSelection(list, {10});

	EXPECT_FALSE(list.SelectAll());
	ExpectSelection(list, {10});
}

TEST(EditorListModelTest, SingleArrowsMoveTheChoiceItself) {
	ListSelection list = MakeList(ListSelectionMode::Single);
	list.ApplyClick(20, KeyMod::None);

	EXPECT_TRUE(list.MoveCursor(1, KeyMod::Ctrl));
	ExpectSelection(list, {30});
	EXPECT_TRUE(list.MoveCursor(1, KeyMod::Shift));
	ExpectSelection(list, {40});
	EXPECT_EQ(list.GetCursor(), 40u);
}

TEST(EditorListModelTest, ArrowLandsOnFirstRow) {
	ListSelection down = MakeList();
	EXPECT_TRUE(down.MoveCursor(1, KeyMod::None));
	ExpectSelection(down, {10});

	ListSelection up = MakeList();
	EXPECT_TRUE(up.MoveCursor(-1, KeyMod::None));
	ExpectSelection(up, {10});

	ListSelection end = MakeList();
	EXPECT_TRUE(end.MoveCursor(std::numeric_limits<i32>::max(), KeyMod::None));
	ExpectSelection(end, {50});
}

TEST(EditorListModelTest, CursorSaturatesAtBothEndsNotWrapping) {
	ListSelection list = MakeList();
	list.ApplyClick(30, KeyMod::None);

	EXPECT_TRUE(list.MoveCursor(-1000, KeyMod::None));
	EXPECT_EQ(list.GetCursor(), 10u);
	EXPECT_FALSE(list.MoveCursor(-1, KeyMod::None));
	EXPECT_TRUE(list.MoveCursor(1000, KeyMod::None));
	EXPECT_EQ(list.GetCursor(), 50u);
	EXPECT_FALSE(list.MoveCursor(1, KeyMod::None));
	ExpectSelection(list, {50});

	EXPECT_TRUE(list.MoveCursor(std::numeric_limits<i32>::min(), KeyMod::None));
	EXPECT_EQ(list.GetCursor(), 10u);
	EXPECT_TRUE(list.MoveCursor(std::numeric_limits<i32>::max(), KeyMod::None));
	EXPECT_EQ(list.GetCursor(), 50u);
}

TEST(EditorListModelTest, CtrlArrowExtendsLikeShift) {
	ListSelection list = MakeList();
	list.ApplyClick(20, KeyMod::None);

	EXPECT_TRUE(list.MoveCursor(2, KeyMod::Ctrl));
	ExpectSelection(list, {20, 30, 40});
	EXPECT_EQ(list.GetActive(), 40u);
	EXPECT_EQ(list.GetCursor(), 40u);

	ListSelection shifted = MakeList();
	shifted.ApplyClick(20, KeyMod::None);
	EXPECT_TRUE(shifted.MoveCursor(2, KeyMod::Ctrl | KeyMod::Shift));
	ExpectSelection(shifted, {20, 30, 40});
}

TEST(EditorListModelTest, ShiftExtendsFromAnchor) {
	ListSelection list = MakeList();
	list.ApplyClick(20, KeyMod::None);

	EXPECT_TRUE(list.MoveCursor(1, KeyMod::Shift));
	ExpectSelection(list, {20, 30});
	EXPECT_TRUE(list.MoveCursor(1, KeyMod::Shift));
	ExpectSelection(list, {20, 30, 40});
	EXPECT_EQ(list.GetActive(), 40u);

	EXPECT_TRUE(list.MoveCursor(-3, KeyMod::Shift));
	ExpectSelection(list, {20, 10});
	EXPECT_EQ(list.GetActive(), 10u);
	EXPECT_EQ(list.GetCursor(), 10u);
}

TEST(EditorListModelTest, ShiftAdoptsCursorRowAsAnchor) {
	ListSelection list = MakeList();
	list.ApplyClick(30, KeyMod::None);
	constexpr u64 WITHOUT_30[] = {10, 20, 40, 50};
	list.SetRows(WITHOUT_30);
	ASSERT_EQ(list.GetCursor(), 40u);

	EXPECT_TRUE(list.MoveCursor(1, KeyMod::Shift));
	ExpectSelection(list, {40, 50});
}

TEST(EditorListModelTest, KeyTableHas10Entries) {
	struct Entry {
		Key key;
		ListNavigation nav;
	};
	constexpr Entry TABLE[] = {
		{Key::Up, ListNavigation::Prev},
		{Key::Down, ListNavigation::Next},
		{Key::Home, ListNavigation::First},
		{Key::End, ListNavigation::Last},
		{Key::PageUp, ListNavigation::PageUp},
		{Key::PageDown, ListNavigation::PageDown},
		{Key::Right, ListNavigation::Expand},
		{Key::Left, ListNavigation::Collapse},
		{Key::Enter, ListNavigation::Activate},
		{Key::KpEnter, ListNavigation::Activate},
	};

	for (const Entry& entry : TABLE) {
		EXPECT_EQ(TranslateKey(entry.key, KeyMod::None), entry.nav) << "key " << ToString(entry.key);
	}

	for (u16 raw = 0; raw < static_cast<u16>(Key::Count); ++raw) {
		const Key key = static_cast<Key>(raw);
		bool mapped = false;
		for (const Entry& entry : TABLE) {
			mapped = mapped || entry.key == key;
		}
		if (!mapped) {
			EXPECT_EQ(TranslateKey(key, KeyMod::None), ListNavigation::None) << "key " << ToString(key);
		}
	}
	EXPECT_EQ(TranslateKey(Key::Delete, KeyMod::None), ListNavigation::None);
	EXPECT_EQ(TranslateKey(Key::A, KeyMod::Ctrl), ListNavigation::None);
}

TEST(EditorListModelTest, CtrlAndShiftRideThroughTableUntouched) {
	EXPECT_EQ(TranslateKey(Key::Down, KeyMod::Shift), ListNavigation::Next);
	EXPECT_EQ(TranslateKey(Key::Down, KeyMod::Ctrl), ListNavigation::Next);
	EXPECT_EQ(TranslateKey(Key::Home, KeyMod::Ctrl | KeyMod::Shift), ListNavigation::First);
}

TEST(EditorListModelTest, AltAndSuperRefuseEveryNavigationKey) {
	constexpr Key NAV[] = {Key::Up, Key::Down, Key::Home, Key::End, Key::PageUp, Key::PageDown, Key::Left, Key::Right, Key::Enter, Key::KpEnter};
	for (Key key : NAV) {
		EXPECT_EQ(TranslateKey(key, KeyMod::Alt), ListNavigation::None) << "key " << ToString(key);
		EXPECT_EQ(TranslateKey(key, KeyMod::Super), ListNavigation::None) << "key " << ToString(key);
		EXPECT_EQ(TranslateKey(key, KeyMod::Alt | KeyMod::Shift), ListNavigation::None) << "key " << ToString(key);
	}
}
