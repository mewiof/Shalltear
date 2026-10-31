#include "TestsPCH.h"

#include "UITestUtil.h"

#include "Shalltear/Core/Scene.h"

#include "Shalltear/Editor/EditorInsertStrips.h"
#include "Shalltear/Editor/EditorListView.h"
#include "Shalltear/Editor/EditorSceneOps.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PanelManager.h"
#include "Shalltear/Editor/SceneAnnotations.h"
#include "Shalltear/Editor/UndoSystem.h"

using namespace Shalltear;
using namespace UITest;

namespace {

	struct ListRig {
		MockFontSystem fonts;
		UI::Context context;
		UI::ElementHandle root;
		EditorListView list;

		u32 selectionChanges = 0;
		Vector<u64> activated{GetMemoryResource(MemTag::Editor)};
		Vector<u64> expandedToggles{GetMemoryResource(MemTag::Editor)};
		Vector<u8> expandedWanted{GetMemoryResource(MemTag::Editor)};
		Vector<u64> clicked{GetMemoryResource(MemTag::Editor)};
		Vector<UI::ElementHandle> gapHosts{GetMemoryResource(MemTag::Editor)};

		explicit ListRig(ListLayout layout = ListLayout::Rows,
						 ListSelectionMode mode = ListSelectionMode::Multiple,
						 Vec2I size = {400, 600},
						 std::function<void(UI::Context&, UI::ElementHandle)> trailing = {})
			: context(MakeDesc(size), &fonts) {
			EditorTheme::ApplyToContext(context);
			root = context.CreateElement();
			context.AppendChild(context.GetRoot(), root);
			UI::Style rootStyle;
			rootStyle.flexDirection = UI::FlexDirection::Column;
			rootStyle.width = UI::Length::Percent(1.0f);
			rootStyle.height = UI::Length::Percent(1.0f);
			context.SetStyle(root, rootStyle);

			ListViewDesc desc;
			desc.layout = layout;
			desc.mode = mode;
			desc.cellWidthPx = 60.0f;
			desc.cellHeightPx = 60.0f;
			desc.onSelectionChanged = [this]() { ++selectionChanges; };
			desc.onBuildTrailing = std::move(trailing);
			list.Build(nullptr, context, root, std::move(desc));
			context.Update();
		}

		static UI::ContextDesc MakeDesc(Vec2I size) {
			UI::ContextDesc desc;
			desc.sizePx = size;
			return desc;
		}

		void Fill(u32 count, bool selectable = true) {
			Vector<ListRow> rows{GetMemoryResource(MemTag::Editor)};
			for (u32 i = 0; i < count; ++i) {
				rows.push_back(MakeRow(i + 1, selectable));
			}
			list.SetRows(rows);
			context.Update();
		}

		ListRow MakeRow(u64 id, bool selectable = true) {
			ListRow row;
			row.id = id;
			row.selectable = selectable;
			row.label = "Row " + std::to_string(id);
			row.onActivate = [this, id](UI::Context&) { activated.push_back(id); };
			row.onClick = [this, id](UI::Context&) { clicked.push_back(id); };
			row.onToggleExpand = [this, id](bool wanted) {
				expandedToggles.push_back(id);
				expandedWanted.push_back(wanted ? 1u : 0u);
			};
			return row;
		}

		void SetRows(std::span<const ListRow> rows) {
			list.SetRows(rows);
			context.Update();
		}

		UI::ElementHandle Element(u64 id) const { return FindTag(root, id); }

		UI::ElementHandle FindTag(UI::ElementHandle from, u64 tag) const {
			const UI::Element* element = context.GetElement(from);
			if (!element) {
				return {};
			}
			if (element->userTag == tag &&
				(context.HasClass(from, StringHash("slt-tree-row")) ||
				 context.HasClass(from, StringHash("ed-asset-cell")))) {
				return from;
			}
			for (u32 child = element->firstChild; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = context.MakeHandle(child);
				UI::ElementHandle found = FindTag(handle, tag);
				if (found.IsValid()) {
					return found;
				}
				child = context.GetElement(handle)->nextSibling;
			}
			return {};
		}

		Vec2 CenterOf(u64 id) const {
			const UI::Rect& box = context.GetLayout(Element(id)).borderBox;
			return {box.x + box.w * 0.5f, box.y + box.h * 0.5f};
		}

		bool IsSelected(u64 id) const {
			UI::ElementHandle element = Element(id);
			return element.IsValid() && context.HasClass(element, StringHash("slt-selected"));
		}
		u64 SelectedId() const {
			u64 found = 0;
			for (u64 id = 1; id <= 512; ++id) {
				if (Element(id).IsValid() && IsSelected(id)) {
					if (found != 0) {
						return 0;
					}
					found = id;
				}
			}
			return found;
		}

		void ClickRow(u64 id, KeyMod mods = KeyMod::None) {
			Vec2 point = CenterOf(id);
			InputEvent event{};
			event.button = MouseButton::Left;
			event.mousePosPx = {static_cast<i32>(point.x), static_cast<i32>(point.y)};
			event.mods = mods;
			event.type = InputEventType::MouseMove;
			context.InjectInput(event);
			event.type = InputEventType::MouseDown;
			context.InjectInput(event);
			event.type = InputEventType::MouseUp;
			context.InjectInput(event);
			context.Update();
		}

		void Key(Shalltear::Key key, KeyMod mods = KeyMod::None) {
			InputEvent event{};
			event.type = InputEventType::KeyDown;
			event.key = key;
			event.mods = mods;
			context.InjectInput(event);
			context.Update();
		}

		u32 RealizedRows() const { return CountClass(root, StringHash("slt-tree-row")); }

		u32 CountClass(UI::ElementHandle from, StringHash klass) const {
			const UI::Element* element = context.GetElement(from);
			if (!element) {
				return 0;
			}
			u32 count = context.HasClass(from, klass) ? 1u : 0u;
			for (u32 child = element->firstChild; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = context.MakeHandle(child);
				count += CountClass(handle, klass);
				child = context.GetElement(handle)->nextSibling;
			}
			return count;
		}

		UI::ElementHandle Viewport() const { return FindClass(root, StringHash("slt-scroll-viewport")); }

		UI::ElementHandle FindClass(UI::ElementHandle from, StringHash klass) const {
			const UI::Element* element = context.GetElement(from);
			if (!element) {
				return {};
			}
			if (context.HasClass(from, klass)) {
				return from;
			}
			for (u32 child = element->firstChild; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = context.MakeHandle(child);
				UI::ElementHandle found = FindClass(handle, klass);
				if (found.IsValid()) {
					return found;
				}
				child = context.GetElement(handle)->nextSibling;
			}
			return {};
		}

		UI::ElementHandle FindText(UI::ElementHandle from, std::string_view text) const {
			if (context.GetTextString(from) == text) {
				return from;
			}
			const UI::Element* element = context.GetElement(from);
			for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX;
				 child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = context.MakeHandle(child);
				UI::ElementHandle found = FindText(handle, text);
				if (found.IsValid()) {
					return found;
				}
				child = context.GetElement(handle)->nextSibling;
			}
			return {};
		}

		f32 MaxScroll() const {
			const UI::Element* element = context.GetElement(Viewport());
			return element ? element->maxScroll.y : 0.0f;
		}

		void Wheel(f32 notches) {
			InputEvent move{};
			move.type = InputEventType::MouseMove;
			move.mousePosPx = {200, 60};
			context.InjectInput(move);
			InputEvent wheel{};
			wheel.type = InputEventType::MouseWheel;
			wheel.wheel = {0.0f, notches};
			wheel.mousePosPx = {200, 60};
			context.InjectInput(wheel);
			context.Update();
		}
	};

} // anonymous namespace

TEST(EditorListViewTest, PressingRowFocusesListNotClearingFocus) {
	ListRig rig;
	rig.Fill(3);
	ASSERT_FALSE(rig.list.HasFocus());

	rig.ClickRow(2);
	EXPECT_TRUE(rig.list.HasFocus());
	rig.Key(Key::Down);
	EXPECT_EQ(rig.SelectedId(), 3u);
}

TEST(EditorListViewTest, ListsKeepSeparateFocusAndSelection) {
	ListRig first;
	ListRig second;
	first.Fill(4);
	second.Fill(4);

	first.ClickRow(2);
	EXPECT_TRUE(first.list.HasFocus());
	EXPECT_FALSE(second.list.HasFocus());
	EXPECT_TRUE(first.IsSelected(2));
	EXPECT_FALSE(second.IsSelected(2));

	second.ClickRow(1);
	second.Key(Key::Down);
	EXPECT_EQ(second.SelectedId(), 2u);
	EXPECT_EQ(first.SelectedId(), 2u);
}

namespace {

	void DrivesTheWholeClickGesture(ListLayout layout) {
		ListRig rig(layout);
		rig.Fill(5);

		rig.ClickRow(2);
		EXPECT_TRUE(rig.IsSelected(2));
		EXPECT_EQ(rig.list.GetSelected().size(), 1u);

		rig.ClickRow(4, KeyMod::Ctrl);
		EXPECT_TRUE(rig.IsSelected(2));
		EXPECT_TRUE(rig.IsSelected(4));
		EXPECT_EQ(rig.list.GetSelected().back(), 4u);

		rig.ClickRow(4, KeyMod::Ctrl);
		EXPECT_FALSE(rig.IsSelected(4));

		rig.ClickRow(1, KeyMod::Shift);
		EXPECT_TRUE(rig.IsSelected(1));
		EXPECT_TRUE(rig.IsSelected(3));
		EXPECT_EQ(rig.list.GetSelected().size(), 4u);
		EXPECT_EQ(rig.list.GetSelected().back(), 1u);

		rig.ClickRow(5);
		EXPECT_EQ(rig.list.GetSelected().size(), 1u);
		EXPECT_TRUE(rig.IsSelected(5));
	}

	void ReadsTheModifiersThePressHeld(ListLayout layout) {
		ListRig rig(layout);
		rig.Fill(3);
		rig.ClickRow(1);
		ASSERT_TRUE(rig.IsSelected(1));

		Vec2 point = rig.CenterOf(3);
		InputEvent event{};
		event.button = MouseButton::Left;
		event.mousePosPx = {static_cast<i32>(point.x), static_cast<i32>(point.y)};
		event.type = InputEventType::MouseMove;
		event.mods = KeyMod::Ctrl;
		rig.context.InjectInput(event);
		event.type = InputEventType::MouseDown;
		rig.context.InjectInput(event);
		event.type = InputEventType::MouseUp;
		event.mods = KeyMod::None;
		rig.context.InjectInput(event);
		rig.context.Update();

		EXPECT_TRUE(rig.IsSelected(1));
		EXPECT_TRUE(rig.IsSelected(3));
	}

	void RestylesWithoutRebuilding(ListLayout layout) {
		ListRig rig(layout);
		rig.Fill(3);
		UI::ElementHandle row = rig.Element(2);
		ASSERT_TRUE(row.IsValid());

		rig.ClickRow(2);
		EXPECT_TRUE(rig.context.IsAlive(row));
		EXPECT_EQ(rig.Element(2), row);
		EXPECT_TRUE(rig.IsSelected(2));

		rig.ClickRow(1);
		EXPECT_TRUE(rig.context.IsAlive(row));
		EXPECT_FALSE(rig.IsSelected(2));
	}

	void MakesEnterAndDoubleClickTheSameGesture(ListLayout layout) {
		ListRig rig(layout);
		rig.Fill(3);

		rig.ClickRow(2);
		rig.ClickRow(2);
		ASSERT_EQ(rig.activated.size(), 1u);
		EXPECT_EQ(rig.activated[0], 2u);

		rig.Key(Key::Enter);
		ASSERT_EQ(rig.activated.size(), 2u);
		EXPECT_EQ(rig.activated[1], 2u);
	}

} // anonymous namespace

TEST(EditorListViewTest, RowsDriveTheWholeClickGesture) { DrivesTheWholeClickGesture(ListLayout::Rows); }
TEST(EditorListViewTest, GridDrivesTheWholeClickGesture) { DrivesTheWholeClickGesture(ListLayout::Grid); }
TEST(EditorListViewTest, RowsReadTheModifiersThePressHeld) { ReadsTheModifiersThePressHeld(ListLayout::Rows); }
TEST(EditorListViewTest, GridReadsTheModifiersThePressHeld) { ReadsTheModifiersThePressHeld(ListLayout::Grid); }
TEST(EditorListViewTest, RowsRestyleWithoutRebuild) { RestylesWithoutRebuilding(ListLayout::Rows); }
TEST(EditorListViewTest, GridRestylesWithoutRebuild) { RestylesWithoutRebuilding(ListLayout::Grid); }
TEST(EditorListViewTest, RowEnterEqualsDoubleClick) {
	MakesEnterAndDoubleClickTheSameGesture(ListLayout::Rows);
}
TEST(EditorListViewTest, GridEnterEqualsDoubleClick) {
	MakesEnterAndDoubleClickTheSameGesture(ListLayout::Grid);
}

TEST(EditorListViewTest, GridFlowsCellsAcrossViewportWidth) {
	ListRig rig(ListLayout::Grid, ListSelectionMode::Multiple, {400, 600});
	rig.Fill(8);

	const UI::Rect& first = rig.context.GetLayout(rig.Element(1)).borderBox;
	const UI::Rect& second = rig.context.GetLayout(rig.Element(2)).borderBox;
	EXPECT_GT(second.x, first.x);
	EXPECT_FLOAT_EQ(second.y, first.y);

	bool wrapped = false;
	for (u64 id = 2; id <= 8; ++id) {
		if (rig.context.GetLayout(rig.Element(id)).borderBox.y > first.y) {
			wrapped = true;
			break;
		}
	}
	EXPECT_TRUE(wrapped);
}

TEST(EditorListViewTest, CellContentAboveLabel) {
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 600};
	UI::Context context{desc, &fonts};
	EditorTheme::ApplyToContext(context);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);
	UI::Style rootStyle;
	rootStyle.flexDirection = UI::FlexDirection::Column;
	rootStyle.width = UI::Length::Percent(1.0f);
	rootStyle.height = UI::Length::Percent(1.0f);
	context.SetStyle(root, rootStyle);

	EditorListView list;
	UI::ElementHandle picture;
	ListViewDesc listDesc;
	listDesc.layout = ListLayout::Grid;
	listDesc.cellWidthPx = 60.0f;
	listDesc.cellHeightPx = 60.0f;
	listDesc.onBuildCell = [&context, &picture](UI::Context&, u64, UI::ElementHandle cell) {
		picture = context.CreateElement();
		context.AppendChild(cell, picture);
		UI::Style style;
		style.width = UI::Length::Pixel(30.0f);
		style.height = UI::Length::Pixel(30.0f);
		context.SetStyle(picture, style);
	};
	list.Build(nullptr, context, root, std::move(listDesc));

	Vector<ListRow> rows{GetMemoryResource(MemTag::Editor)};
	ListRow only;
	only.id = 1;
	only.label = "Caption";
	rows.push_back(std::move(only));
	list.SetRows(rows);
	context.Update();

	ASSERT_TRUE(context.IsAlive(picture));
	const UI::ElementHandle square = context.MakeHandle(context.GetElement(picture)->parent);
	ASSERT_TRUE(context.HasClass(square, StringHash("ed-asset-cell-well")));
	UI::ElementHandle label;
	for (UI::ElementHandle walk = square; context.IsAlive(walk);) {
		const UI::Element* element = context.GetElement(walk);
		if (!element || element->nextSibling == UI::INVALID_UI_INDEX) {
			break;
		}
		walk = context.MakeHandle(element->nextSibling);
		if (context.HasClass(walk, StringHash("ed-asset-cell-label"))) {
			label = walk;
			break;
		}
	}
	ASSERT_TRUE(context.IsAlive(label));
	EXPECT_GT(context.GetLayout(label).borderBox.y, context.GetLayout(picture).borderBox.y);
}

namespace {

	void FillWithFoldOut(ListRig& rig, u32 before, u32 children, u32 after, bool expanded) {
		Vector<ListRow> rows{GetMemoryResource(MemTag::Editor)};
		u64 id = 1;
		for (u32 i = 0; i < before; ++i) {
			rows.push_back(rig.MakeRow(id++));
		}
		ListRow source = rig.MakeRow(id++);
		source.hasChildren = true;
		source.expanded = expanded;
		rows.push_back(std::move(source));
		for (u32 i = 0; expanded && i < children; ++i) {
			ListRow child = rig.MakeRow(id++);
			child.depth = 1;
			rows.push_back(std::move(child));
		}
		for (u32 i = 0; i < after; ++i) {
			rows.push_back(rig.MakeRow(id++));
		}
		rig.SetRows(rows);
		rig.context.Update();
	}

	UI::ElementHandle FindFoldButton(UI::Context& context, UI::ElementHandle from) {
		return UITest::FindByClass(context, from, StringHash("ed-asset-fold-button"), 0);
	}

	void PressAt(UI::Context& context, Vec2 point) {
		InputEvent event{};
		event.button = MouseButton::Left;
		event.mousePosPx = {static_cast<i32>(point.x), static_cast<i32>(point.y)};
		event.type = InputEventType::MouseMove;
		context.InjectInput(event);
		event.type = InputEventType::MouseDown;
		context.InjectInput(event);
		event.type = InputEventType::MouseUp;
		context.InjectInput(event);
		context.Update();
	}

} // anonymous namespace

TEST(EditorListViewTest, GridFoldsOutFromRightEdge) {
	ListRig rig(ListLayout::Grid, ListSelectionMode::Multiple, {400, 600});
	FillWithFoldOut(rig, 0, 0, 3, false);

	UI::ElementHandle button = FindFoldButton(rig.context, rig.root);
	ASSERT_TRUE(rig.context.IsAlive(button));
	const UI::Rect source = rig.context.GetLayout(rig.Element(1)).borderBox;
	const UI::Rect box = rig.context.GetLayout(button).borderBox;
	ASSERT_LT(box.x, source.x + source.w);
	ASSERT_GT(box.x + box.w, source.x + source.w);

	PressAt(rig.context, {source.x + source.w + (box.x + box.w - source.x - source.w) * 0.5f, box.y + box.h * 0.5f});
	ASSERT_EQ(rig.expandedToggles.size(), 1u);
	EXPECT_EQ(rig.expandedToggles[0], 1u);
	EXPECT_EQ(rig.expandedWanted[0], 1u);
	EXPECT_TRUE(rig.list.GetSelected().empty());

	FillWithFoldOut(rig, 0, 2, 3, true);
	button = FindFoldButton(rig.context, rig.root);
	ASSERT_TRUE(rig.context.IsAlive(button));
	const UI::Rect again = rig.context.GetLayout(button).borderBox;
	PressAt(rig.context, {again.x + again.w * 0.5f, again.y + again.h * 0.5f});
	ASSERT_EQ(rig.expandedToggles.size(), 2u);
	EXPECT_EQ(rig.expandedWanted[1], 0u);
}

TEST(EditorListViewTest, BandOnePiecePerRow) {
	ListRig rig(ListLayout::Grid, ListSelectionMode::Multiple, {400, 600});
	FillWithFoldOut(rig, 1, 9, 2, true);

	auto center = [&rig](u64 id) {
		const UI::Rect& box = rig.context.GetLayout(rig.Element(id)).borderBox;
		return Vec2{box.x + box.w * 0.5f, box.y + box.h * 0.5f};
	};
	Vector<EditorListView::FoldOutBand> bands{GetMemoryResource(MemTag::Editor)};
	rig.list.GetFoldOutBands(bands);
	ASSERT_FALSE(bands.empty());

	Vector<f32> childRowsY{GetMemoryResource(MemTag::Editor)};
	for (u64 id = 1; id <= 13; ++id) {
		const bool child = id >= 3 && id <= 11;
		u32 holding = 0;
		for (const EditorListView::FoldOutBand& band : bands) {
			holding += band.rect.Contains(center(id)) ? 1u : 0u;
		}
		EXPECT_EQ(holding, child ? 1u : 0u) << "cell " << id << (child ? " is a child and" : " is not a child and") << " lies under " << holding;
		const f32 y = rig.context.GetLayout(rig.Element(id)).borderBox.y;
		if (child && std::find(childRowsY.begin(), childRowsY.end(), y) == childRowsY.end()) {
			childRowsY.push_back(y);
		}
	}
	ASSERT_GT(childRowsY.size(), 1u);
	EXPECT_EQ(bands.size(), childRowsY.size());
	for (size_t i = 0; i < bands.size(); ++i) {
		EXPECT_EQ(bands[i].continuesBelow, i + 1 < bands.size()) << "piece " << i;
		EXPECT_EQ(bands[i].fromAbove, i > 0) << "piece " << i;
	}

	FillWithFoldOut(rig, 1, 9, 2, false);
	rig.list.GetFoldOutBands(bands);
	EXPECT_TRUE(bands.empty());
}

TEST(EditorListViewTest, LabelHookGetsCaptions) {
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 600};
	UI::Context context{desc, &fonts};
	EditorTheme::ApplyToContext(context);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);

	EditorListView list;
	Vector<std::pair<u64, str>> seen{GetMemoryResource(MemTag::Editor)};
	ListViewDesc listDesc;
	listDesc.layout = ListLayout::Grid;
	listDesc.cellWidthPx = 60.0f;
	listDesc.cellHeightPx = 60.0f;
	listDesc.onDecorateCellLabel = [&seen](UI::Context& ctx, u64 id, UI::ElementHandle label) {
		EXPECT_TRUE(ctx.HasClass(label, StringHash("ed-asset-cell-label")));
		seen.emplace_back(id, ctx.GetTextString(label));
	};
	list.Build(nullptr, context, root, std::move(listDesc));

	Vector<ListRow> rows{GetMemoryResource(MemTag::Editor)};
	for (u64 id = 1; id <= 3; ++id) {
		ListRow row;
		row.id = id;
		row.label = "Cell " + std::to_string(id);
		rows.push_back(std::move(row));
	}
	list.SetRows(rows);
	context.Update();

	ASSERT_EQ(seen.size(), 3u);
	for (u64 id = 1; id <= 3; ++id) {
		EXPECT_EQ(seen[id - 1].first, id);
		EXPECT_EQ(seen[id - 1].second, "Cell " + std::to_string(id));
	}
}

TEST(EditorListViewTest, OnlyViewportRowsVisible) {
	ListRig rig(ListLayout::Rows, ListSelectionMode::Multiple, {400, 200});
	rig.Fill(60);
	rig.ClickRow(1);

	Vector<u64> visible{GetMemoryResource(MemTag::Editor)};
	rig.list.GetVisibleRows(visible);
	ASSERT_FALSE(visible.empty());
	EXPECT_LT(visible.size(), 60u);
	EXPECT_EQ(visible.front(), 1u);

	rig.Key(Key::End);
	Vector<u64> afterEnd{GetMemoryResource(MemTag::Editor)};
	rig.list.GetVisibleRows(afterEnd);
	ASSERT_FALSE(afterEnd.empty());
	EXPECT_EQ(afterEnd.back(), 60u);
	EXPECT_NE(afterEnd.front(), visible.front());
}

namespace {

	u32 GridColumnsOnScreen(const ListRig& rig, u32 rowCount) {
		f32 topY = 0.0f;
		bool haveTop = false;
		u32 columns = 0;
		for (u32 id = 1; id <= rowCount; ++id) {
			UI::ElementHandle cell = rig.Element(id);
			if (!rig.context.IsAlive(cell)) {
				continue;
			}
			const UI::Rect& box = rig.context.GetLayout(cell).borderBox;
			if (!haveTop) {
				topY = box.y;
				haveTop = true;
			}
			if (std::abs(box.y - topY) < 0.5f) {
				++columns;
			}
		}
		return columns;
	}

} // anonymous namespace

TEST(EditorListViewTest, GridDrawsColumnsImmediately) {
	ListRig rig(ListLayout::Grid, ListSelectionMode::Multiple, {400, 600});
	rig.Fill(12);
	const u32 wide = GridColumnsOnScreen(rig, 12);
	ASSERT_GT(wide, 2u);

	rig.context.ClearChildren(rig.root);
	ListViewDesc desc;
	desc.layout = ListLayout::Grid;
	desc.cellWidthPx = 60.0f;
	desc.cellHeightPx = 60.0f;
	rig.list.Build(nullptr, rig.context, rig.root, std::move(desc));
	rig.Fill(12);

	EXPECT_EQ(GridColumnsOnScreen(rig, 12), wide);
}

TEST(EditorListViewTest, GridReflowsInSettle) {
	ListRig rig(ListLayout::Grid, ListSelectionMode::Multiple, {400, 600});
	rig.Fill(12);
	const u32 wide = GridColumnsOnScreen(rig, 12);
	ASSERT_GT(wide, 2u);

	rig.context.Resize({160, 600});
	rig.context.Update();
	const u32 narrow = GridColumnsOnScreen(rig, 12);
	EXPECT_LT(narrow, wide);

	rig.context.Resize({400, 600});
	rig.context.Update();
	EXPECT_EQ(GridColumnsOnScreen(rig, 12), wide);
}

TEST(EditorListViewTest, PressedGridDefersReflow) {
	ListRig rig(ListLayout::Grid, ListSelectionMode::Multiple, {400, 600});
	rig.Fill(12);
	const u32 wide = GridColumnsOnScreen(rig, 12);
	ASSERT_GT(wide, 2u);

	Vec2 point = rig.CenterOf(3);
	InputEvent event{};
	event.button = MouseButton::Left;
	event.mousePosPx = {static_cast<i32>(point.x), static_cast<i32>(point.y)};
	event.type = InputEventType::MouseMove;
	rig.context.InjectInput(event);
	event.type = InputEventType::MouseDown;
	rig.context.InjectInput(event);
	rig.context.Update();

	rig.context.Resize({160, 600});
	rig.context.Update();
	EXPECT_EQ(GridColumnsOnScreen(rig, 12), wide);

	event.type = InputEventType::MouseUp;
	rig.context.InjectInput(event);
	rig.context.Update();
	EXPECT_EQ(GridColumnsOnScreen(rig, 12), wide);

	rig.list.Tick(0.016f);
	rig.context.Update();
	EXPECT_LT(GridColumnsOnScreen(rig, 12), wide);
}

TEST(EditorListViewTest, LayoutSwitchHandsOverBand) {
	ListRig rig(ListLayout::Grid, ListSelectionMode::Multiple, {400, 600});
	rig.Fill(12);
	ASSERT_GT(GridColumnsOnScreen(rig, 12), 2u);

	rig.list.SetLayout(ListLayout::Rows, 60.0f, 60.0f, 0.0f);
	rig.Fill(12);
	EXPECT_TRUE(rig.context.IsAlive(rig.Element(1)));

	rig.list.SetLayout(ListLayout::Grid, 60.0f, 60.0f, 0.0f);
	rig.Fill(12);
	EXPECT_GT(GridColumnsOnScreen(rig, 12), 2u);
}

TEST(EditorListViewTest, ArrowsWalkShiftExtends) {
	ListRig rig;
	rig.Fill(6);
	rig.ClickRow(2);

	rig.Key(Key::Down);
	EXPECT_EQ(rig.SelectedId(), 3u);
	EXPECT_TRUE(rig.IsSelected(3));
	EXPECT_FALSE(rig.IsSelected(2));

	rig.Key(Key::Down, KeyMod::Shift);
	EXPECT_TRUE(rig.IsSelected(3));
	EXPECT_TRUE(rig.IsSelected(4));
	EXPECT_EQ(rig.list.GetSelected().size(), 2u);

	rig.Key(Key::Up);
	EXPECT_EQ(rig.SelectedId(), 3u);
	EXPECT_EQ(rig.list.GetSelected().size(), 1u);
}

TEST(EditorListViewTest, CtrlArrowExtendsLikeShift) {
	ListRig rig;
	rig.Fill(5);
	rig.ClickRow(1);
	ASSERT_TRUE(rig.IsSelected(1));

	rig.Key(Key::Down, KeyMod::Ctrl);
	rig.Key(Key::Down, KeyMod::Ctrl);
	EXPECT_TRUE(rig.IsSelected(1));
	EXPECT_TRUE(rig.IsSelected(2));
	EXPECT_TRUE(rig.IsSelected(3));
	EXPECT_EQ(rig.list.GetSelected().size(), 3u);

	rig.Key(Key::Down, KeyMod::Ctrl | KeyMod::Shift);
	EXPECT_EQ(rig.list.GetSelected().size(), 4u);
}

TEST(EditorListViewTest, CursorThatMovesAloneIsNotPublished) {
	ListRig rig;
	rig.Fill(5);
	rig.ClickRow(1);
	rig.ClickRow(2, KeyMod::Ctrl);
	rig.ClickRow(2, KeyMod::Ctrl);
	ASSERT_EQ(rig.SelectedId(), 1u);
	const u32 afterClicks = rig.selectionChanges;
	ASSERT_GT(afterClicks, 0u);

	rig.Key(Key::Up);
	EXPECT_EQ(rig.SelectedId(), 1u);
	EXPECT_EQ(rig.selectionChanges, afterClicks);

	rig.Key(Key::Down);
	EXPECT_EQ(rig.selectionChanges, afterClicks + 1);
}

TEST(EditorListViewTest, EmptiedSelectionSendsNextArrowToTop) {
	ListRig rig;
	rig.Fill(5);
	rig.ClickRow(3);
	rig.list.SetSelection({});
	rig.context.Update();
	ASSERT_TRUE(rig.list.GetSelected().empty());

	rig.Key(Key::Down);
	EXPECT_EQ(rig.SelectedId(), 1u);

	rig.ClickRow(3);
	rig.list.SetSelection({});
	rig.context.Update();
	rig.Key(Key::Up);
	EXPECT_EQ(rig.SelectedId(), 1u);
}

TEST(EditorListViewTest, DeselectedRowLooksUntouched) {
	ListRig rig;
	rig.Fill(5);
	rig.ClickRow(3);
	rig.list.SetSelection({});
	InputEvent away{};
	away.type = InputEventType::MouseMove;
	away.mousePosPx = {390, 590};
	rig.context.InjectInput(away);
	rig.context.Update();

	ASSERT_TRUE(rig.Element(3).IsValid() && rig.Element(1).IsValid());
	ASSERT_FALSE(rig.IsSelected(3));
	EXPECT_TRUE(rig.context.GetComputedStyle(rig.Element(3)) == rig.context.GetComputedStyle(rig.Element(1)));

	rig.ClickRow(1);
	rig.ClickRow(2, KeyMod::Ctrl);
	rig.ClickRow(2, KeyMod::Ctrl);
	rig.context.InjectInput(away);
	rig.context.Update();
	ASSERT_FALSE(rig.IsSelected(2));
	EXPECT_TRUE(rig.context.GetComputedStyle(rig.Element(2)) == rig.context.GetComputedStyle(rig.Element(4)));
}

TEST(EditorListViewTest, DeselectedCellLooksUntouched) {
	ListRig rig(ListLayout::Grid, ListSelectionMode::Multiple, {400, 600});
	rig.Fill(6);
	rig.ClickRow(3);
	rig.list.SetSelection({});
	InputEvent away{};
	away.type = InputEventType::MouseMove;
	away.mousePosPx = {390, 590};
	rig.context.InjectInput(away);
	rig.context.Update();

	const StringHash square("ed-asset-cell-well");
	UI::ElementHandle touched = FindByClass(rig.context, rig.Element(3), square, 0);
	UI::ElementHandle untouched = FindByClass(rig.context, rig.Element(1), square, 0);
	ASSERT_TRUE(rig.context.IsAlive(touched) && rig.context.IsAlive(untouched));
	ASSERT_FALSE(rig.IsSelected(3));
	EXPECT_TRUE(rig.context.GetComputedStyle(touched) == rig.context.GetComputedStyle(untouched));
}

TEST(EditorListViewTest, CellShowsPressKeepsSelection) {
	ListRig rig(ListLayout::Grid, ListSelectionMode::Multiple, {400, 600});
	rig.Fill(6);
	rig.ClickRow(3);
	ASSERT_TRUE(rig.IsSelected(3));
	const StringHash squareClass("ed-asset-cell-well");
	auto squareFill = [&](u64 id) {
		return rig.context.GetComputedStyle(FindByClass(rig.context, rig.Element(id), squareClass, 0)).background;
	};
	auto send = [&](InputEventType type, Vec2 point) {
		InputEvent event{};
		event.type = type;
		event.button = MouseButton::Left;
		event.mousePosPx = {static_cast<i32>(point.x), static_cast<i32>(point.y)};
		rig.context.InjectInput(event);
		rig.context.Update();
	};
	auto pressedFill = [&](u64 id) {
		send(InputEventType::MouseMove, {390.0f, 590.0f});
		const UI::Brush resting = squareFill(id);
		send(InputEventType::MouseMove, rig.CenterOf(id));
		const UI::Brush hovered = squareFill(id);
		send(InputEventType::MouseDown, rig.CenterOf(id));
		const UI::Brush pressed = squareFill(id);
		send(InputEventType::MouseUp, {-5000.0f, -5000.0f});
		EXPECT_FALSE(SameBrush(pressed, resting)) << "cell " << id;
		EXPECT_FALSE(SameBrush(pressed, hovered)) << "cell " << id;
		return pressed;
	};

	const UI::Brush plain = pressedFill(1);
	const UI::Brush selected = pressedFill(3);
	EXPECT_TRUE(rig.IsSelected(3));
	EXPECT_FALSE(SameBrush(plain, selected));
}

TEST(EditorListViewTest, HomeEndReachEnds) {
	ListRig rig;
	rig.Fill(5);
	rig.ClickRow(3);

	rig.Key(Key::Home);
	EXPECT_EQ(rig.SelectedId(), 1u);
	rig.Key(Key::Up);
	EXPECT_EQ(rig.SelectedId(), 1u);

	rig.Key(Key::End);
	EXPECT_EQ(rig.SelectedId(), 5u);
	rig.Key(Key::Down);
	EXPECT_EQ(rig.SelectedId(), 5u);
}

TEST(EditorListViewTest, OnlyClaimedKeysConsumed) {
	ListRig rig;
	rig.Fill(3);
	rig.ClickRow(3);

	UI::ElementEvents& events = rig.context.Events(rig.context.GetFocused());
	ASSERT_TRUE(static_cast<bool>(events.onKeyDown));
	EXPECT_TRUE(events.onKeyDown(Key::Down, KeyMod::None));
	EXPECT_TRUE(events.onKeyDown(Key::Enter, KeyMod::None));
	EXPECT_FALSE(events.onKeyDown(Key::D, KeyMod::Ctrl));
	EXPECT_FALSE(events.onKeyDown(Key::Delete, KeyMod::None));
	EXPECT_FALSE(events.onKeyDown(Key::Down, KeyMod::Alt));
}

TEST(EditorListViewTest, LeftRightFoldCursorRow) {
	ListRig rig;
	Vector<ListRow> rows{GetMemoryResource(MemTag::Editor)};
	rows.push_back(rig.MakeRow(1));
	ListRow parent = rig.MakeRow(2);
	parent.hasChildren = true;
	parent.expanded = false;
	rows.push_back(std::move(parent));
	rig.SetRows(rows);

	rig.ClickRow(2);
	rig.Key(Key::Right);
	ASSERT_EQ(rig.expandedToggles.size(), 1u);
	EXPECT_EQ(rig.expandedToggles[0], 2u);
	EXPECT_TRUE(rig.expandedWanted[0]);

	rig.Key(Key::Left);
	EXPECT_EQ(rig.expandedToggles.size(), 1u);

	rig.ClickRow(1);
	rig.Key(Key::Right);
	EXPECT_EQ(rig.expandedToggles.size(), 1u);
}

TEST(EditorListViewTest, ViewFollowsCursorBelowFold) {
	ListRig rig(ListLayout::Rows, ListSelectionMode::Multiple, {400, 120});
	rig.Fill(40);
	rig.ClickRow(1);

	const UI::Rect& window = rig.context.GetLayout(rig.Element(1)).borderBox;
	f32 rowHeight = window.h;
	ASSERT_GT(rowHeight, 0.0f);

	auto insideTheWindow = [&rig](u64 id) {
		UI::ElementHandle element = rig.Element(id);
		if (!element.IsValid()) {
			return false;
		}
		const UI::Rect& box = rig.context.GetLayout(element).borderBox;
		return box.y >= -0.5f && box.y + box.h <= 120.5f;
	};
	ASSERT_FALSE(insideTheWindow(30));

	for (u32 step = 0; step < 29; ++step) {
		rig.Key(Key::Down);
	}
	ASSERT_EQ(rig.SelectedId(), 30u);
	EXPECT_TRUE(insideTheWindow(30));

	for (u32 step = 0; step < 29; ++step) {
		rig.Key(Key::Up);
	}
	ASSERT_EQ(rig.SelectedId(), 1u);
	EXPECT_TRUE(insideTheWindow(1));
}

TEST(EditorListViewTest, RevealRowBringsUnrealizedRowOntoScreen) {
	ListRig rig(ListLayout::Rows, ListSelectionMode::Single, {400, 120});
	rig.Fill(40);

	auto insideTheWindow = [&rig](u64 id) {
		UI::ElementHandle element = rig.Element(id);
		if (!element.IsValid()) {
			return false;
		}
		const UI::Rect& box = rig.context.GetLayout(element).borderBox;
		return box.y >= -0.5f && box.y + box.h <= 120.5f;
	};
	ASSERT_FALSE(insideTheWindow(30));

	u64 ids[1] = {30};
	rig.list.SetSelection(ids);
	rig.context.Update();
	EXPECT_FALSE(insideTheWindow(30));

	rig.list.RevealRow(30);
	rig.context.Update();
	EXPECT_TRUE(insideTheWindow(30));

	const f32 settled = rig.context.GetLayout(rig.Element(30)).borderBox.y;
	rig.list.RevealRow(9999);
	rig.context.Update();
	EXPECT_FLOAT_EQ(rig.context.GetLayout(rig.Element(30)).borderBox.y, settled);
}

TEST(EditorListViewTest, RevealSurvivesModelReplace) {
	ListRig rig(ListLayout::Rows, ListSelectionMode::Single, {400, 120});
	rig.Fill(4);
	rig.context.Update();

	Vector<ListRow> grown{GetMemoryResource(MemTag::Editor)};
	for (u32 i = 0; i < 200; ++i) {
		grown.push_back(rig.MakeRow(i + 1));
	}
	rig.list.SetRows(grown);
	rig.list.RevealRow(190);
	rig.context.Update();

	UI::ElementHandle element = rig.Element(190);
	ASSERT_TRUE(element.IsValid());
	const UI::Rect& box = rig.context.GetLayout(element).borderBox;
	EXPECT_GE(box.y, -0.5f);
	EXPECT_LE(box.y + box.h, 120.5f);
}

TEST(EditorListViewTest, PageDownMovesOnePage) {
	ListRig rig(ListLayout::Rows, ListSelectionMode::Multiple, {400, 120});
	rig.Fill(60);
	rig.ClickRow(1);

	rig.Key(Key::PageDown);
	u64 afterOnePage = rig.SelectedId();
	EXPECT_GT(afterOnePage, 2u);
	EXPECT_LT(afterOnePage, 60u);

	rig.Key(Key::PageUp);
	EXPECT_EQ(rig.SelectedId(), 1u);
}

namespace {

	ListRig& FillMixed(ListRig& rig) {
		Vector<ListRow> rows{GetMemoryResource(MemTag::Editor)};
		for (u64 id = 1; id <= 5; ++id) {
			rows.push_back(rig.MakeRow(id, id != 2 && id != 4));
		}
		rig.SetRows(rows);
		return rig;
	}

} // anonymous namespace

TEST(EditorListViewTest, ArrowSkipsUnselectableRows) {
	ListRig rig;
	FillMixed(rig);
	rig.ClickRow(1);

	rig.Key(Key::Down);
	EXPECT_EQ(rig.SelectedId(), 3u);
	rig.Key(Key::Down);
	EXPECT_EQ(rig.SelectedId(), 5u);
}

TEST(EditorListViewTest, UnselectableRowRunsAction) {
	ListRig rig;
	FillMixed(rig);
	rig.ClickRow(1);
	ASSERT_TRUE(rig.IsSelected(1));

	rig.ClickRow(2);
	EXPECT_FALSE(rig.IsSelected(2));
	EXPECT_TRUE(rig.IsSelected(1));
	ASSERT_EQ(rig.clicked.size(), 1u);
	EXPECT_EQ(rig.clicked[0], 2u);
}

TEST(EditorListViewTest, SelectAllTakesSelectableRows) {
	ListRig rig;
	FillMixed(rig);

	EXPECT_TRUE(rig.list.SelectAll());
	EXPECT_EQ(rig.list.GetSelected().size(), 3u);
	EXPECT_TRUE(rig.IsSelected(1));
	EXPECT_TRUE(rig.IsSelected(3));
	EXPECT_TRUE(rig.IsSelected(5));
	EXPECT_FALSE(rig.IsSelected(2));
	EXPECT_FALSE(rig.IsSelected(4));
}

TEST(EditorListViewTest, SelectAllCoversCurrentRows) {
	ListRig rig;
	rig.Fill(5);
	rig.Fill(3);

	EXPECT_TRUE(rig.list.SelectAll());
	EXPECT_EQ(rig.list.GetSelected().size(), 3u);
}

TEST(EditorListViewTest, SingleSelectionRefusesSelectAll) {
	ListRig rig(ListLayout::Rows, ListSelectionMode::Single);
	rig.Fill(4);
	rig.ClickRow(2);

	EXPECT_FALSE(rig.list.SelectAll());
	EXPECT_EQ(rig.list.GetSelected().size(), 1u);
}

TEST(EditorListViewTest, HandedSelectionNotPublished) {
	ListRig rig;
	rig.Fill(5);
	u32 before = rig.selectionChanges;

	const u64 ids[] = {2, 4};
	rig.list.SetSelection(ids);
	rig.context.Update();

	EXPECT_TRUE(rig.IsSelected(2));
	EXPECT_TRUE(rig.IsSelected(4));
	EXPECT_EQ(rig.selectionChanges, before);
}

TEST(EditorListViewTest, CursorFollowsSelectionHandedInFromOutside) {
	ListRig rig;
	rig.Fill(5);
	rig.ClickRow(1);

	const u64 ids[] = {2, 4};
	rig.list.SetSelection(ids);
	rig.context.Update();

	rig.Key(Key::Down);
	EXPECT_EQ(rig.SelectedId(), 5u);
}

TEST(EditorListViewTest, HandedSelectionKeepsOwnRows) {
	ListRig rig;
	rig.Fill(3);

	const u64 ids[] = {2, 99};
	rig.list.SetSelection(ids);
	rig.context.Update();

	EXPECT_TRUE(rig.IsSelected(2));
	EXPECT_EQ(rig.list.GetSelected().size(), 1u);
}

TEST(EditorListViewTest, RowsLeavingTheScreenPublishNothing) {
	ListRig rig;
	rig.Fill(5);
	rig.ClickRow(4);
	u32 afterClick = rig.selectionChanges;

	rig.Fill(2);
	EXPECT_EQ(rig.selectionChanges, afterClick);
	EXPECT_TRUE(rig.list.GetSelected().empty());
}

TEST(EditorListViewTest, CursorLandsBesideDeletedRow) {
	ListRig rig;
	rig.Fill(5);
	rig.ClickRow(3);
	ASSERT_EQ(rig.SelectedId(), 3u);

	Vector<ListRow> rows{GetMemoryResource(MemTag::Editor)};
	for (u64 id : {1ull, 2ull, 4ull, 5ull}) {
		rows.push_back(rig.MakeRow(id));
	}
	rig.SetRows(rows);

	rig.Key(Key::Down);
	EXPECT_EQ(rig.SelectedId(), 5u);
}

TEST(EditorListViewTest, CursorWalksBackwardsWhenLastRowsGo) {
	ListRig rig;
	rig.Fill(5);
	rig.ClickRow(5);

	Vector<ListRow> rows{GetMemoryResource(MemTag::Editor)};
	for (u64 id : {1ull, 2ull, 3ull}) {
		rows.push_back(rig.MakeRow(id));
	}
	rig.SetRows(rows);

	rig.Key(Key::Up);
	EXPECT_EQ(rig.SelectedId(), 2u);
}

TEST(EditorListViewTest, PanelsElementsLandBetweenRowsInOrder) {
	ListRig rig;
	Vector<ListRow> rows{GetMemoryResource(MemTag::Editor)};
	for (u64 id = 1; id <= 3; ++id) {
		ListRow row = rig.MakeRow(id);
		row.onBuildLeading = [&rig, id](UI::Context&, UI::ElementHandle host) {
			UI::ElementHandle marker = rig.context.CreateElement();
			rig.context.AppendChild(host, marker);
			rig.context.AddClass(marker, StringHash("test-gap"));
			rig.context.SetUserTag(marker, 1000 + id);
			UI::Style style;
			style.height = UI::Length::Pixel(3.0f);
			style.width = UI::Length::Percent(1.0f);
			rig.context.SetStyle(marker, style);
		};
		rows.push_back(std::move(row));
	}
	rig.SetRows(rows);

	const UI::Element* firstRow = rig.context.GetElement(rig.Element(1));
	ASSERT_NE(firstRow, nullptr);
	ASSERT_NE(firstRow->parent, UI::INVALID_UI_INDEX);
	UI::ElementHandle host = rig.context.MakeHandle(firstRow->parent);

	Vector<u64> order{GetMemoryResource(MemTag::Editor)};
	for (u32 child = rig.context.GetElement(host)->firstChild; child != UI::INVALID_UI_INDEX;) {
		UI::ElementHandle handle = rig.context.MakeHandle(child);
		u64 tag = rig.context.GetElement(handle)->userTag;
		if (tag != 0) {
			order.push_back(tag);
		}
		child = rig.context.GetElement(handle)->nextSibling;
	}
	const u64 expected[] = {1001, 1, 1002, 2, 1003, 3};
	ASSERT_EQ(order.size(), 6u);
	for (u32 i = 0; i < 6; ++i) {
		EXPECT_EQ(order[i], expected[i]) << "position " << i;
	}
}

namespace {

	struct CommandLog {
		u32 deletes = 0;
		u32 duplicates = 0;
		Vector<u64> renamed{GetMemoryResource(MemTag::Editor)};
		bool renameAnswers = true;
	};

	CommandLog _leftLog;
	CommandLog _rightLog;

	UI::ElementHandle FindRowTagged(const UI::Context& context, UI::ElementHandle from, u64 tag) {
		const UI::Element* element = context.GetElement(from);
		if (!element) {
			return {};
		}
		if (element->userTag == tag && context.HasClass(from, StringHash("slt-tree-row"))) {
			return from;
		}
		for (u32 child = element->firstChild; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(child);
			UI::ElementHandle found = FindRowTagged(context, handle, tag);
			if (found.IsValid()) {
				return found;
			}
			child = context.GetElement(handle)->nextSibling;
		}
		return {};
	}

	class TwoListPanel : public EditorPanel {
	public:
		SLT_REFLECT_CLASS(TwoListPanel)
		SLT_REFLECT_PARENT(EditorPanel)
		SLT_REFLECT_CREATABLE()
		SLT_EDITOR_PANEL_NAME("Two Lists")
		SLT_END_REFLECT()

		TwoListPanel() = default;

		void OnBuildContent(UI::Context& context, UI::ElementHandle root) override {
			UI::Style rootStyle;
			rootStyle.flexDirection = UI::FlexDirection::Column;
			rootStyle.width = UI::Length::Percent(1.0f);
			rootStyle.height = UI::Length::Percent(1.0f);
			context.SetStyle(root, rootStyle);

			ListViewDesc left;
			left.onDelete = []() { ++_leftLog.deletes; };
			left.onDuplicate = []() { ++_leftLog.duplicates; };
			left.onRename = [](u64 row) {
				_leftLog.renamed.push_back(row);
				return _leftLog.renameAnswers;
			};
			_left.Build(this, context, root, std::move(left));

			ListViewDesc right;
			right.onDelete = []() { ++_rightLog.deletes; };
			right.onRename = [](u64 row) {
				_rightLog.renamed.push_back(row);
				return _rightLog.renameAnswers;
			};
			_right.Build(this, context, root, std::move(right));

			Fill(_left, 100);
			Fill(_right, 200);
		}

		static void Fill(EditorListView& list, u64 base) {
			Vector<ListRow> rows{GetMemoryResource(MemTag::Editor)};
			for (u64 i = 1; i <= 3; ++i) {
				ListRow row;
				row.id = base + i;
				row.label = "Row " + std::to_string(base + i);
				rows.push_back(std::move(row));
			}
			list.SetRows(rows);
		}

		EditorListView _left;
		EditorListView _right;
	};

	class PanelCommandTest : public ::testing::Test {
	protected:
		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			TwoListPanel::GetReflectType();
			EditorSelection::Initialize();
			PanelManager::Initialize();
			_leftLog = CommandLog{};
			_rightLog = CommandLog{};
		}
		void TearDown() override {
			PanelManager::Terminate();
			EditorSelection::Terminate();
		}
	};

	struct TwoListRig {
		MockFontSystem fonts;
		UI::Context context;
		UI::ElementHandle root;
		Handle<EditorPanel> panel;

		TwoListRig()
			: context(Desc(), &fonts) {
			EditorTheme::ApplyToContext(context);
			panel = PanelManager::OpenPanel(TwoListPanel::GetReflectType());
			root = context.CreateElement();
			context.AppendChild(context.GetRoot(), root);
			panel.Get()->BuildContent(context, root);
			context.Update();
		}

		static UI::ContextDesc Desc() {
			UI::ContextDesc desc;
			desc.sizePx = {400, 600};
			return desc;
		}

		TwoListPanel& Panel() const { return *static_cast<TwoListPanel*>(panel.Get()); }

		void ClickRow(u64 id, KeyMod mods = KeyMod::None) {
			UI::ElementHandle element = FindRowTagged(context, root, id);
			ASSERT_TRUE(element.IsValid()) << "no row " << id << " on screen";
			const UI::Rect& box = context.GetLayout(element).borderBox;
			InputEvent event{};
			event.button = MouseButton::Left;
			event.mousePosPx = {static_cast<i32>(box.x + box.w * 0.5f), static_cast<i32>(box.y + box.h * 0.5f)};
			event.mods = mods;
			event.type = InputEventType::MouseMove;
			context.InjectInput(event);
			event.type = InputEventType::MouseDown;
			context.InjectInput(event);
			event.type = InputEventType::MouseUp;
			context.InjectInput(event);
			context.Update();
		}
	};

} // anonymous namespace

TEST_F(PanelCommandTest, CommandReachesFocusedListAndNotSibling) {
	TwoListRig rig;
	rig.ClickRow(101);
	ASSERT_TRUE(rig.Panel()._left.HasFocus());
	ASSERT_FALSE(rig.Panel()._right.HasFocus());

	EXPECT_TRUE(rig.panel.Get()->OnCommand(PanelCommand::Delete));
	EXPECT_EQ(_leftLog.deletes, 1u);
	EXPECT_EQ(_rightLog.deletes, 0u);

	rig.ClickRow(202);
	ASSERT_TRUE(rig.Panel()._right.HasFocus());
	EXPECT_TRUE(rig.panel.Get()->OnCommand(PanelCommand::Delete));
	EXPECT_EQ(_rightLog.deletes, 1u);
	EXPECT_EQ(_leftLog.deletes, 1u);
}

TEST_F(PanelCommandTest, UnfocusedPanelAnswersNothing) {
	TwoListRig rig;
	ASSERT_FALSE(rig.Panel()._left.HasFocus());
	ASSERT_FALSE(rig.Panel()._right.HasFocus());

	EXPECT_FALSE(rig.panel.Get()->OnCommand(PanelCommand::Delete));
	EXPECT_EQ(_leftLog.deletes, 0u);
	EXPECT_EQ(_rightLog.deletes, 0u);

	rig.ClickRow(101);
	EXPECT_TRUE(rig.panel.Get()->OnCommand(PanelCommand::Delete));
	EXPECT_EQ(_leftLog.deletes, 1u);
}

TEST_F(PanelCommandTest, NullCallbackRefusesCommandAndSaysSo) {
	TwoListRig rig;
	rig.ClickRow(201);
	ASSERT_TRUE(rig.Panel()._right.HasFocus());

	EXPECT_FALSE(rig.panel.Get()->OnCommand(PanelCommand::Duplicate));
	EXPECT_EQ(_rightLog.duplicates, 0u);
	EXPECT_EQ(_leftLog.duplicates, 0u);

	rig.ClickRow(101);
	EXPECT_TRUE(rig.panel.Get()->OnCommand(PanelCommand::Duplicate));
	EXPECT_EQ(_leftLog.duplicates, 1u);
}

TEST_F(PanelCommandTest, ListAnswersSelectAll) {
	TwoListRig rig;
	rig.ClickRow(101);
	ASSERT_EQ(rig.Panel()._left.GetSelected().size(), 1u);

	EXPECT_TRUE(rig.panel.Get()->OnCommand(PanelCommand::SelectAll));
	EXPECT_EQ(rig.Panel()._left.GetSelected().size(), 3u);
	EXPECT_EQ(rig.Panel()._right.GetSelected().size(), 0u);
}

TEST_F(PanelCommandTest, RenameNeedsSingleRow) {
	TwoListRig rig;
	rig.ClickRow(102);
	EXPECT_TRUE(rig.panel.Get()->OnCommand(PanelCommand::Rename));
	ASSERT_EQ(_leftLog.renamed.size(), 1u);
	EXPECT_EQ(_leftLog.renamed[0], 102u);

	rig.ClickRow(103, KeyMod::Ctrl);
	ASSERT_EQ(rig.Panel()._left.GetSelected().size(), 2u);
	EXPECT_FALSE(rig.panel.Get()->OnCommand(PanelCommand::Rename));
	EXPECT_EQ(_leftLog.renamed.size(), 1u);

	rig.Panel()._left.SetSelection({});
	EXPECT_FALSE(rig.panel.Get()->OnCommand(PanelCommand::Rename));
	EXPECT_EQ(_leftLog.renamed.size(), 1u);
}

TEST_F(PanelCommandTest, PanelRefusingRenameIsAnswerDispatchGets) {
	TwoListRig rig;
	_leftLog.renameAnswers = false;
	rig.ClickRow(101);

	EXPECT_FALSE(rig.panel.Get()->OnCommand(PanelCommand::Rename));
	EXPECT_EQ(_leftLog.renamed.size(), 1u);
}

TEST_F(PanelCommandTest, RebuiltListLeavesOldRegistry) {
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 600};
	UI::Context context(desc, &fonts);
	EditorTheme::ApplyToContext(context);
	UI::ElementHandle root = context.CreateElement();
	context.AppendChild(context.GetRoot(), root);

	Handle<EditorPanel> first = PanelManager::OpenPanel(TwoListPanel::GetReflectType());
	Handle<EditorPanel> second = PanelManager::OpenPanel(TwoListPanel::GetReflectType());
	ASSERT_TRUE(first.IsValid());
	ASSERT_TRUE(second.IsValid());

	EditorListView moving;
	ListViewDesc movingDesc;
	movingDesc.onDelete = []() { ++_rightLog.duplicates; };
	moving.Build(first.Get(), context, root, movingDesc);

	Vector<ListRow> rows{GetMemoryResource(MemTag::Editor)};
	ListRow only;
	only.id = 301;
	only.label = "Moving";
	rows.push_back(std::move(only));
	moving.SetRows(rows);
	context.Update();

	UI::ElementHandle row = FindRowTagged(context, root, 301);
	ASSERT_TRUE(row.IsValid());
	const UI::Rect& box = context.GetLayout(row).borderBox;
	InputEvent event{};
	event.button = MouseButton::Left;
	event.mousePosPx = {static_cast<i32>(box.x + box.w * 0.5f), static_cast<i32>(box.y + box.h * 0.5f)};
	event.type = InputEventType::MouseMove;
	context.InjectInput(event);
	event.type = InputEventType::MouseDown;
	context.InjectInput(event);
	event.type = InputEventType::MouseUp;
	context.InjectInput(event);
	context.Update();

	ASSERT_TRUE(moving.HasFocus());
	ASSERT_TRUE(first.Get()->OnCommand(PanelCommand::Delete));
	ASSERT_EQ(_rightLog.duplicates, 1u);
	ASSERT_FALSE(second.Get()->OnCommand(PanelCommand::Delete));

	context.ClearChildren(root);
	moving.Build(second.Get(), context, root, std::move(movingDesc));
	moving.SetRows(rows);
	context.Update();
	row = FindRowTagged(context, root, 301);
	ASSERT_TRUE(row.IsValid());
	const UI::Rect& moved = context.GetLayout(row).borderBox;
	event.mousePosPx = {static_cast<i32>(moved.x + moved.w * 0.5f), static_cast<i32>(moved.y + moved.h * 0.5f)};
	event.type = InputEventType::MouseMove;
	context.InjectInput(event);
	event.type = InputEventType::MouseDown;
	context.InjectInput(event);
	event.type = InputEventType::MouseUp;
	context.InjectInput(event);
	context.Update();

	EXPECT_TRUE(second.Get()->OnCommand(PanelCommand::Delete));
	EXPECT_EQ(_rightLog.duplicates, 2u);
	EXPECT_FALSE(first.Get()->OnCommand(PanelCommand::Delete));
	EXPECT_EQ(_rightLog.duplicates, 2u);
}

namespace {

	class HierarchyListTest : public ::testing::Test {
	protected:
		void SetUp() override {
			TypeRegistry::InitializePrimitives();
			RegisterBuiltInPanels();
			SceneSystem::Initialize();
			EditorSceneOps::Initialize();
			EditorSelection::Initialize();
			UndoSystem::Initialize();
			SceneAnnotations::Initialize();
			PanelManager::Initialize();
			_scene = SceneSystem::CreateScene("Fixture");
		}
		void TearDown() override {
			PanelManager::Terminate();
			SceneAnnotations::Terminate();
			UndoSystem::Terminate();
			EditorSelection::Terminate();
			EditorSceneOps::Terminate();
			SceneSystem::Terminate();
		}
		Handle<Scene> _scene;
	};

	struct HierarchyRig {
		MockFontSystem fonts;
		UI::Context context;
		UI::ElementHandle root;
		Handle<EditorPanel> panel;

		HierarchyRig()
			: context(Desc(), &fonts) {
			EditorTheme::ApplyToContext(context);
			panel = PanelManager::OpenPanel(TypeRegistry::FindType(StringHash("HierarchyPanel")));
			root = context.CreateElement();
			context.AppendChild(context.GetRoot(), root);
			panel.Get()->BuildContent(context, root);
			context.Update();
		}

		static UI::ContextDesc Desc() {
			UI::ContextDesc desc;
			desc.sizePx = {600, 800};
			return desc;
		}

		UI::ElementHandle RowNamed(std::string_view label) const { return Search(root, label); }

		bool RowBefore(std::string_view row, std::string_view between) const {
			f32 rowY = context.GetLayout(RowNamed(row)).borderBox.y;
			f32 betweenY = context.GetLayout(RowNamed(between)).borderBox.y;
			return betweenY < rowY;
		}

		UI::ElementHandle Search(UI::ElementHandle from, std::string_view label) const {
			if (context.HasClass(from, StringHash("slt-tree-row")) && SaysAnywhere(from, label)) {
				return from;
			}
			const UI::Element* element = context.GetElement(from);
			for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = context.MakeHandle(child);
				UI::ElementHandle found = Search(handle, label);
				if (found.IsValid()) {
					return found;
				}
				child = context.GetElement(handle)->nextSibling;
			}
			return {};
		}

		bool SaysAnywhere(UI::ElementHandle from, std::string_view text) const {
			if (context.GetTextString(from) == text) {
				return true;
			}
			const UI::Element* element = context.GetElement(from);
			for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = context.MakeHandle(child);
				if (SaysAnywhere(handle, text)) {
					return true;
				}
				child = context.GetElement(handle)->nextSibling;
			}
			return false;
		}

		u32 SelectedRowCount() const { return CountSelected(root); }

		u32 CountSelected(UI::ElementHandle from) const {
			u32 total = 0;
			if (context.HasClass(from, StringHash("slt-tree-row")) &&
				context.HasClass(from, StringHash("slt-selected"))) {
				++total;
			}
			const UI::Element* element = context.GetElement(from);
			for (u32 child = element ? element->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = context.MakeHandle(child);
				total += CountSelected(handle);
				child = context.GetElement(handle)->nextSibling;
			}
			return total;
		}

		void ClickRow(std::string_view label, KeyMod mods = KeyMod::None) {
			UI::ElementHandle row = RowNamed(label);
			const UI::Rect& box = context.GetLayout(row).borderBox;
			Vec2 point{box.x + box.w * 0.5f, box.y + box.h * 0.5f};
			InputEvent event{};
			event.button = MouseButton::Left;
			event.mousePosPx = {static_cast<i32>(point.x), static_cast<i32>(point.y)};
			event.mods = mods;
			event.type = InputEventType::MouseMove;
			context.InjectInput(event);
			event.type = InputEventType::MouseDown;
			context.InjectInput(event);
			event.type = InputEventType::MouseUp;
			context.InjectInput(event);
			context.Update();
			panel.Get()->OnTick(0.016f);
			context.Update();
		}

		void Key(Shalltear::Key key, KeyMod mods = KeyMod::None) {
			InputEvent event{};
			event.type = InputEventType::KeyDown;
			event.key = key;
			event.mods = mods;
			context.InjectInput(event);
			context.Update();
			panel.Get()->OnTick(0.016f);
			context.Update();
		}

		void ToggleCaret(std::string_view label) {
			UI::ElementHandle row = RowNamed(label);
			ASSERT_TRUE(row.IsValid());
			UI::ElementHandle caret;
			for (u32 walk = context.GetElement(row)->firstChild; walk != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = context.MakeHandle(walk);
				if (context.HasClass(handle, StringHash("slt-tree-arrow"))) {
					caret = handle;
					break;
				}
				walk = context.GetElement(handle)->nextSibling;
			}
			ASSERT_TRUE(caret.IsValid());
			const UI::Rect& box = context.GetLayout(caret).borderBox;
			InputEvent event{};
			event.button = MouseButton::Left;
			event.mousePosPx = {static_cast<i32>(box.x + box.w * 0.5f), static_cast<i32>(box.y + box.h * 0.5f)};
			event.type = InputEventType::MouseDown;
			context.InjectInput(event);
			event.type = InputEventType::MouseUp;
			context.InjectInput(event);
			context.Update();
			panel.Get()->OnTick(0.016f);
			context.Update();
		}
	};

} // anonymous namespace

TEST_F(HierarchyListTest, ArrowingThroughTreeStepsOverFolderRow) {
	Handle<SceneObject> a = SceneSystem::CreateObject("Alpha", _scene);
	Handle<SceneObject> b = SceneSystem::CreateObject("Beta", _scene);
	Handle<SceneObject> first[] = {a};
	Handle<SceneObject> second[] = {b};
	SceneAnnotations::CreateFolder(_scene, "GroupOne", FolderParent::Roots(), first);
	SceneAnnotations::CreateFolder(_scene, "GroupTwo", FolderParent::Roots(), second);

	HierarchyRig rig;
	ASSERT_TRUE(rig.RowNamed("GroupTwo").IsValid());
	ASSERT_TRUE(rig.RowBefore("Beta", "GroupTwo"));

	rig.ClickRow("Alpha");
	ASSERT_TRUE(EditorSelection::IsSelected(Handle<Object>(a.Get())));

	rig.Key(Key::Down);
	EXPECT_TRUE(EditorSelection::IsSelected(Handle<Object>(b.Get())));
	EXPECT_EQ(EditorSelection::GetSelectionCount(), 1u);

	rig.Key(Key::Up);
	EXPECT_TRUE(EditorSelection::IsSelected(Handle<Object>(a.Get())));
}

TEST_F(HierarchyListTest, ArrowingThroughTreeStepsOverSceneRow) {
	Handle<Scene> second = SceneSystem::CreateScene("Second");
	Handle<SceneObject> a = SceneSystem::CreateObject("Alpha", _scene);
	Handle<SceneObject> b = SceneSystem::CreateObject("Beta", second);

	HierarchyRig rig;
	ASSERT_TRUE(rig.RowNamed("Second").IsValid());
	ASSERT_TRUE(rig.RowBefore("Beta", "Second"));

	rig.ClickRow("Alpha");
	rig.Key(Key::Down);
	EXPECT_TRUE(EditorSelection::IsSelected(Handle<Object>(b.Get())));
	EXPECT_EQ(EditorSelection::GetSelectionCount(), 1u);

	rig.Key(Key::Up);
	EXPECT_TRUE(EditorSelection::IsSelected(Handle<Object>(a.Get())));
}

TEST_F(HierarchyListTest, ArrowingPublishesSelectionToRestOfEditor) {
	SceneSystem::CreateObject("Alpha", _scene);
	Handle<SceneObject> b = SceneSystem::CreateObject("Beta", _scene);

	HierarchyRig rig;
	rig.ClickRow("Alpha");
	rig.Key(Key::Down);

	EXPECT_EQ(EditorSelection::GetActive().GetInstanceId(), b.GetInstanceId());
}

TEST_F(HierarchyListTest, ExternalSelectionSurvivesRebuild) {
	SceneSystem::CreateObject("Alpha", _scene);
	Handle<SceneObject> b = SceneSystem::CreateObject("Beta", _scene);

	HierarchyRig rig;
	EditorSelection::Select(Handle<Object>(b.Get()));
	rig.panel.Get()->OnTick(0.016f);
	rig.context.Update();
	EXPECT_TRUE(rig.context.HasClass(rig.RowNamed("Beta"), StringHash("slt-selected")));

	SceneSystem::CreateObject("Gamma", _scene);
	rig.panel.Get()->OnTick(0.016f);
	rig.context.Update();
	EXPECT_TRUE(rig.context.HasClass(rig.RowNamed("Beta"), StringHash("slt-selected")));
	EXPECT_TRUE(EditorSelection::IsSelected(Handle<Object>(b.Get())));
}

TEST_F(HierarchyListTest, CollapseKeepsChildSelected) {
	SceneSystem::CreateObject("Parent", _scene);
	Handle<SceneObject> child = SceneSystem::CreateObject("Child", _scene,
														  SceneSystem::GetPrimaryScene().Get()->GetRoot(0));

	HierarchyRig rig;
	rig.ToggleCaret("Parent");
	ASSERT_TRUE(rig.RowNamed("Child").IsValid());

	rig.ClickRow("Child");
	ASSERT_TRUE(EditorSelection::IsSelected(Handle<Object>(child.Get())));

	rig.ToggleCaret("Parent");
	ASSERT_FALSE(rig.RowNamed("Child").IsValid());
	EXPECT_TRUE(EditorSelection::IsSelected(Handle<Object>(child.Get())));

	rig.ToggleCaret("Parent");
	ASSERT_TRUE(rig.RowNamed("Child").IsValid());
	EXPECT_TRUE(rig.context.HasClass(rig.RowNamed("Child"), StringHash("slt-selected")));
}

TEST_F(HierarchyListTest, DeleteReachesFoldedObject) {
	SceneSystem::CreateObject("Parent", _scene);
	Handle<SceneObject> child = SceneSystem::CreateObject("Child", _scene,
														  SceneSystem::GetPrimaryScene().Get()->GetRoot(0));
	u64 childId = child.GetInstanceId();

	HierarchyRig rig;
	rig.ToggleCaret("Parent");
	ASSERT_TRUE(rig.RowNamed("Child").IsValid());
	rig.ClickRow("Child");
	ASSERT_TRUE(EditorSelection::IsSelected(Handle<Object>(child.Get())));

	rig.ToggleCaret("Parent");
	ASSERT_FALSE(rig.RowNamed("Child").IsValid());
	ASSERT_TRUE(rig.context.GetFocused().IsValid());
	ASSERT_EQ(rig.SelectedRowCount(), 0u);
	ASSERT_EQ(EditorSelection::GetSelectionCount(), 1u);

	EXPECT_TRUE(rig.panel.Get()->OnCommand(PanelCommand::Delete));
	SceneSystem::EndFrame();
	rig.panel.Get()->OnTick(0.016f);
	rig.context.Update();

	EXPECT_EQ(ObjectRegistry::Resolve(childId), nullptr);
	EXPECT_EQ(EditorSelection::GetSelectionCount(), 0u);
}

TEST_F(HierarchyListTest, DeleteThroughCommandRoadDestroysWholeSelection) {
	Handle<SceneObject> a = SceneSystem::CreateObject("Alpha", _scene);
	Handle<SceneObject> b = SceneSystem::CreateObject("Beta", _scene);
	Handle<SceneObject> c = SceneSystem::CreateObject("Gamma", _scene);
	u64 aId = a.GetInstanceId();
	u64 bId = b.GetInstanceId();
	u64 cId = c.GetInstanceId();

	HierarchyRig rig;
	rig.ClickRow("Alpha");
	rig.ClickRow("Beta", KeyMod::Ctrl);
	ASSERT_EQ(EditorSelection::GetSelectionCount(), 2u);

	EXPECT_TRUE(rig.panel.Get()->OnCommand(PanelCommand::Delete));
	SceneSystem::EndFrame();
	rig.panel.Get()->OnTick(0.016f);
	rig.context.Update();

	EXPECT_EQ(ObjectRegistry::Resolve(aId), nullptr);
	EXPECT_EQ(ObjectRegistry::Resolve(bId), nullptr);
	EXPECT_NE(ObjectRegistry::Resolve(cId), nullptr);

	UndoSystem::Undo();
	SceneSystem::EndFrame();
	EXPECT_EQ(_scene.Get()->GetRootCount(), 3u);
}

TEST_F(HierarchyListTest, ArrowAfterDelContinuesBesideDeletedRow) {
	SceneSystem::CreateObject("Alpha", _scene);
	Handle<SceneObject> b = SceneSystem::CreateObject("Beta", _scene);
	SceneSystem::CreateObject("Gamma", _scene);
	Handle<SceneObject> d = SceneSystem::CreateObject("Delta", _scene);
	u64 bId = b.GetInstanceId();

	HierarchyRig rig;
	rig.ClickRow("Beta");
	ASSERT_TRUE(EditorSelection::IsSelected(Handle<Object>(b.Get())));

	EXPECT_TRUE(rig.panel.Get()->OnCommand(PanelCommand::Delete));
	rig.panel.Get()->OnTick(0.016f);
	rig.context.Update();
	SceneSystem::EndFrame();
	rig.panel.Get()->OnTick(0.016f);
	rig.context.Update();
	ASSERT_EQ(ObjectRegistry::Resolve(bId), nullptr);
	ASSERT_EQ(EditorSelection::GetSelectionCount(), 0u);

	rig.Key(Key::Down);
	EXPECT_TRUE(EditorSelection::IsSelected(Handle<Object>(d.Get())));
	EXPECT_EQ(EditorSelection::GetSelectionCount(), 1u);
}

TEST_F(HierarchyListTest, SelectAllPublishesObjects) {
	Handle<SceneObject> a = SceneSystem::CreateObject("Alpha", _scene);
	SceneSystem::CreateObject("Beta", _scene);
	Handle<SceneObject> hidden = SceneSystem::CreateObject("Hidden", _scene, a);

	HierarchyRig rig;
	rig.ClickRow("Alpha");
	ASSERT_FALSE(rig.RowNamed("Hidden").IsValid());

	EXPECT_TRUE(rig.panel.Get()->OnCommand(PanelCommand::SelectAll));
	rig.panel.Get()->OnTick(0.016f);
	rig.context.Update();

	EXPECT_EQ(EditorSelection::GetSelectionCount(), 2u);
	EXPECT_FALSE(EditorSelection::IsSelected(Handle<Object>(hidden.Get())));
}

TEST_F(HierarchyListTest, RenameThroughCommandRoadArmsSelectedRow) {
	SceneSystem::CreateObject("Alpha", _scene);
	SceneSystem::CreateObject("Beta", _scene);

	HierarchyRig rig;
	rig.ClickRow("Beta");
	EXPECT_TRUE(rig.panel.Get()->OnCommand(PanelCommand::Rename));
	rig.panel.Get()->OnTick(0.016f);
	rig.context.Update();

	EXPECT_FALSE(rig.RowNamed("Beta").IsValid());
	EXPECT_TRUE(rig.RowNamed("Alpha").IsValid());

	rig.panel.Get()->OnTick(0.016f);
	rig.ClickRow("Alpha");
	rig.ClickRow("Beta", KeyMod::Ctrl);
	ASSERT_EQ(EditorSelection::GetSelectionCount(), 2u);
	EXPECT_FALSE(rig.panel.Get()->OnCommand(PanelCommand::Rename));
}

TEST_F(HierarchyListTest, DuplicateThroughCommandRoadCopiesWholeSelection) {
	SceneSystem::CreateObject("Alpha", _scene);
	SceneSystem::CreateObject("Beta", _scene);

	HierarchyRig rig;
	rig.ClickRow("Alpha");
	rig.ClickRow("Beta", KeyMod::Ctrl);
	ASSERT_EQ(_scene.Get()->GetRootCount(), 2u);

	EXPECT_TRUE(rig.panel.Get()->OnCommand(PanelCommand::Duplicate));
	SceneSystem::EndFrame();
	rig.panel.Get()->OnTick(0.016f);
	rig.context.Update();
	EXPECT_EQ(_scene.Get()->GetRootCount(), 4u);

	UndoSystem::Undo();
	SceneSystem::EndFrame();
	EXPECT_EQ(_scene.Get()->GetRootCount(), 2u);
}

TEST_F(HierarchyListTest, DuplicatesLandBelowSelected) {
	SceneSystem::CreateObject("Alpha", _scene);
	SceneSystem::CreateObject("Beta", _scene);
	SceneSystem::CreateObject("Gamma", _scene);

	HierarchyRig rig;
	rig.ClickRow("Alpha");
	rig.ClickRow("Beta", KeyMod::Ctrl);

	EXPECT_TRUE(rig.panel.Get()->OnCommand(PanelCommand::Duplicate));
	rig.panel.Get()->OnTick(0.016f);
	rig.context.Update();

	ASSERT_TRUE(rig.RowNamed("Alpha (1)").IsValid());
	ASSERT_TRUE(rig.RowNamed("Beta (1)").IsValid());
	EXPECT_TRUE(rig.RowBefore("Alpha (1)", "Alpha"));
	EXPECT_TRUE(rig.RowBefore("Beta", "Alpha (1)"));
	EXPECT_TRUE(rig.RowBefore("Beta (1)", "Beta"));
	EXPECT_TRUE(rig.RowBefore("Gamma", "Beta (1)"));
	EXPECT_EQ(rig.SelectedRowCount(), 2u);
	EXPECT_TRUE(rig.context.HasClass(rig.RowNamed("Alpha (1)"), StringHash("slt-selected")));
	EXPECT_TRUE(rig.context.HasClass(rig.RowNamed("Beta (1)"), StringHash("slt-selected")));
}

TEST_F(HierarchyListTest, CommandNeedsListFocus) {
	Handle<SceneObject> a = SceneSystem::CreateObject("Alpha", _scene);
	u64 aId = a.GetInstanceId();

	HierarchyRig rig;
	rig.ClickRow("Alpha");
	ASSERT_TRUE(EditorSelection::IsSelected(Handle<Object>(a.Get())));

	InputEvent event{};
	event.button = MouseButton::Left;
	event.mousePosPx = {2, 2};
	event.type = InputEventType::MouseMove;
	rig.context.InjectInput(event);
	event.type = InputEventType::MouseDown;
	rig.context.InjectInput(event);
	event.type = InputEventType::MouseUp;
	rig.context.InjectInput(event);
	rig.context.Update();

	ASSERT_FALSE(rig.context.GetFocused().IsValid());
	EXPECT_FALSE(rig.panel.Get()->OnCommand(PanelCommand::Delete));
	EXPECT_NE(ObjectRegistry::Resolve(aId), nullptr);
}

TEST(EditorListViewTest, LongListRealizesBandAndNotModel) {
	ListRig rig(ListLayout::Rows, ListSelectionMode::Multiple, {400, 120});
	rig.Fill(2000);

	const u32 realized = rig.RealizedRows();
	EXPECT_LT(realized, 200u);
	EXPECT_GT(realized, 5u);

	Vector<u64> visible{GetMemoryResource(MemTag::Editor)};
	rig.list.GetVisibleRows(visible);
	EXPECT_GE(visible.size(), 4u) << visible.size();
	for (u64 id : visible) {
		EXPECT_TRUE(rig.Element(id).IsValid()) << "row " << id;
	}
}

TEST(EditorListViewTest, ScrollExtentIsWholeList) {
	ListRig rig(ListLayout::Rows, ListSelectionMode::Multiple, {400, 120});
	rig.Fill(2000);

	const f32 rowHeight = rig.context.GetLayout(rig.Element(1)).borderBox.h;
	ASSERT_GT(rowHeight, 0.0f);
	const f32 wholeList = rowHeight * 2000.0f - 120.0f;
	EXPECT_GT(rig.MaxScroll(), wholeList * 0.9f);
}

TEST(EditorListViewTest, CtrlASelectsUnrealizedRows) {
	ListRig rig(ListLayout::Rows, ListSelectionMode::Multiple, {400, 120});
	rig.Fill(2000);
	ASSERT_LT(rig.RealizedRows(), 2000u);

	ASSERT_TRUE(rig.list.SelectAll());
	EXPECT_EQ(rig.list.GetSelected().size(), 2000u);
}

TEST(EditorListViewTest, CursorReachesUnrealizedRow) {
	ListRig rig(ListLayout::Rows, ListSelectionMode::Multiple, {400, 120});
	rig.Fill(2000);
	rig.ClickRow(1);
	ASSERT_FALSE(rig.Element(2000).IsValid());

	rig.Key(Key::End);

	ASSERT_TRUE(rig.Element(2000).IsValid());
	EXPECT_TRUE(rig.IsSelected(2000));
	const UI::Rect& box = rig.context.GetLayout(rig.Element(2000)).borderBox;
	EXPECT_GE(box.y, -0.5f);
	EXPECT_LE(box.y + box.h, 120.5f);
}

TEST(EditorListViewTest, ScrolledRowKeepsSelectionCue) {
	ListRig rig(ListLayout::Rows, ListSelectionMode::Multiple, {400, 120});
	rig.Fill(2000);
	rig.ClickRow(1);
	ASSERT_TRUE(rig.IsSelected(1));

	for (u32 step = 0; step < 40; ++step) {
		rig.Wheel(-1.0f);
	}
	ASSERT_FALSE(rig.Element(1).IsValid());
	for (u32 step = 0; step < 60; ++step) {
		rig.Wheel(1.0f);
	}

	ASSERT_TRUE(rig.Element(1).IsValid());
	EXPECT_TRUE(rig.IsSelected(1));
}

TEST(EditorListViewTest, WheelNotchInsideMarginRebuildsNothing) {
	ListRig rig(ListLayout::Rows, ListSelectionMode::Multiple, {400, 400});
	rig.Fill(2000);
	UI::ElementHandle before = rig.Element(2);
	ASSERT_TRUE(before.IsValid());

	rig.Wheel(-1.0f);

	EXPECT_EQ(rig.Element(2), before);

	for (u32 step = 0; step < 40; ++step) {
		rig.Wheel(-1.0f);
	}
	EXPECT_FALSE(rig.Element(2).IsValid());
}

TEST(EditorListViewTest, TrailingElementsFollowAllRows) {
	UI::ElementHandle marker;
	ListRig rig(ListLayout::Rows, ListSelectionMode::Multiple, {400, 120},
				[&marker](UI::Context& context, UI::ElementHandle host) {
					marker = context.CreateElement();
					context.AppendChild(host, marker);
					UI::Style style;
					style.height = UI::Length::Pixel(7.0f);
					style.flexShrink = 0.0f;
					context.SetStyle(marker, style);
				});
	rig.Fill(2000);

	UI::ElementHandle viewport = rig.Viewport();
	ASSERT_TRUE(viewport.IsValid());
	ASSERT_TRUE(marker.IsValid());
	const UI::Element* element = rig.context.GetElement(viewport);
	ASSERT_GT(element->maxScroll.y, 1000.0f);

	const UI::Rect& view = rig.context.GetLayout(viewport).content;
	const f32 contentY = rig.context.GetLayout(marker).borderBox.y - view.y + element->scrollOffset.y;
	EXPECT_GE(contentY, element->maxScroll.y);
}

TEST(EditorListViewTest, EmptyListKeepsTrailingElements) {
	u32 builds = 0;
	UI::ElementHandle built;
	ListRig rig(ListLayout::Rows, ListSelectionMode::Multiple, {400, 600},
				[&](UI::Context& context, UI::ElementHandle host) {
					++builds;
					built = context.CreateElement();
					context.AppendChild(host, built);
					context.SetStyle(built, FixedBox(200.0f, 40.0f));
				});
	rig.SetRows(std::span<const ListRow>{});
	ASSERT_TRUE(rig.context.IsAlive(built));
	const UI::ElementHandle first = built;
	const u32 afterReset = builds;

	UI::Style style = rig.context.GetStyle(rig.root);
	style.height = UI::Length::Pixel(300.0f);
	rig.context.SetStyle(rig.root, style);
	rig.context.Update();
	rig.context.Update();
	EXPECT_TRUE(rig.context.IsAlive(first));
	EXPECT_EQ(builds, afterReset);
}

TEST(EditorListViewTest, TailBandSurvivesRealize) {
	EditorInsertStrips gaps;
	ListRig rig(ListLayout::Rows, ListSelectionMode::Multiple, {400, 120},
				[&gaps](UI::Context& context, UI::ElementHandle host) { gaps.DrainInto(context, host); });
	gaps.BeginBuild(rig.context);
	gaps.PendStrip(EditorInsertStripDesc{});
	rig.Fill(2000);
	ASSERT_EQ(CountByClass(rig.context, rig.root, EditorTheme::Class::InsertStrip), 1u);
	rig.list.RevealRow(2000);
	for (int i = 0; i < 4; ++i) {
		rig.context.Update();
	}
	EXPECT_EQ(CountByClass(rig.context, rig.root, EditorTheme::Class::InsertStrip), 1u);
}

TEST(EditorListViewTest, RelabeledRowsRedraw) {
	ListRig rig(ListLayout::Rows, ListSelectionMode::Multiple, {400, 120});
	rig.Fill(2000);
	ASSERT_TRUE(rig.Element(1).IsValid());

	Vector<ListRow> rows{GetMemoryResource(MemTag::Editor)};
	for (u32 i = 0; i < 2000; ++i) {
		ListRow row = rig.MakeRow(i + 1);
		row.label = "Renamed " + std::to_string(i + 1);
		rows.push_back(std::move(row));
	}
	rig.SetRows(rows);

	UI::ElementHandle row = rig.Element(1);
	ASSERT_TRUE(row.IsValid());
	EXPECT_TRUE(rig.FindText(row, "Renamed 1").IsValid());
}

TEST(EditorListViewTest, RowsKnowAncestorContinuations) {
	ListRig rig(ListLayout::Rows, ListSelectionMode::Multiple, {400, 400});
	Vector<ListRow> rows{GetMemoryResource(MemTag::Editor)};
	const u32 depths[] = {0, 1, 1, 2};
	for (u32 i = 0; i < 4; ++i) {
		ListRow row = rig.MakeRow(i + 1);
		row.depth = depths[i];
		row.hasChildren = depths[i] < 2;
		row.expanded = true;
		rows.push_back(std::move(row));
	}
	rig.SetRows(rows);

	UI::RenderList list;
	rig.context.BuildRenderList(list);
	auto stemsIn = [&](u64 id) {
		const UI::ElementHandle element = rig.Element(id);
		EXPECT_TRUE(element.IsValid()) << "row " << id;
		const UI::Rect& box = rig.context.GetLayout(element).borderBox;
		Vector<UI::Rect> found{GetMemoryResource(MemTag::Editor)};
		for (const UI::UIQuad& quad : list.quads) {
			if (quad.rect.w == 1.0f && quad.rect.y >= box.y && quad.rect.y < box.y + box.h) {
				found.push_back(quad.rect);
			}
		}
		return found;
	};

	const UI::Rect& rowA = rig.context.GetLayout(rig.Element(1)).borderBox;
	EXPECT_TRUE(stemsIn(1).empty());

	Vector<UI::Rect> b = stemsIn(2);
	ASSERT_EQ(b.size(), 1u);
	EXPECT_FLOAT_EQ(b[0].h, rowA.h);

	Vector<UI::Rect> c = stemsIn(3);
	ASSERT_EQ(c.size(), 1u);
	EXPECT_FLOAT_EQ(c[0].x, b[0].x);
	EXPECT_LT(c[0].h, b[0].h);

	Vector<UI::Rect> d = stemsIn(4);
	ASSERT_EQ(d.size(), 1u);
	EXPECT_GT(d[0].x, c[0].x);
}

TEST(EditorListViewTest, RowsDecorateInTheirContext) {
	MockFontSystem fonts;
	UI::ContextDesc desc;
	desc.sizePx = {400, 600};
	UI::Context first(desc, &fonts);
	UI::Context second(desc, &fonts);
	EditorTheme::ApplyToContext(first);
	EditorTheme::ApplyToContext(second);

	Vector<UI::Context*> seen{GetMemoryResource(MemTag::Editor)};
	UI::ElementHandle lastRow;

	auto host = [](UI::Context& context) {
		UI::ElementHandle root = context.CreateElement();
		context.AppendChild(context.GetRoot(), root);
		UI::Style style;
		style.flexDirection = UI::FlexDirection::Column;
		style.width = UI::Length::Percent(1.0f);
		style.height = UI::Length::Percent(1.0f);
		context.SetStyle(root, style);
		return root;
	};

	EditorListView list;
	ListViewDesc listDesc;
	listDesc.layout = ListLayout::Rows;
	list.Build(nullptr, first, host(first), std::move(listDesc));

	Vector<ListRow> rows{GetMemoryResource(MemTag::Editor)};
	for (u64 id = 1; id <= 4; ++id) {
		ListRow row;
		row.id = id;
		row.label = "Row " + std::to_string(id);
		row.onDecorate = [&seen, &lastRow](UI::Context& context, const UI::Widgets::TreeRowParts& parts) {
			seen.push_back(&context);
			lastRow = parts.row;
			context.AddClass(parts.row, StringHash("probe-decorated"));
		};
		rows.push_back(std::move(row));
	}
	list.SetRows(rows);
	first.Update();
	ASSERT_FALSE(seen.empty());
	EXPECT_EQ(seen.back(), &first);

	seen.clear();
	ListViewDesc rebuilt;
	rebuilt.layout = ListLayout::Rows;
	list.Build(nullptr, second, host(second), std::move(rebuilt));
	second.Update();

	ASSERT_FALSE(seen.empty());
	for (UI::Context* context : seen) {
		EXPECT_EQ(context, &second);
	}
	ASSERT_TRUE(second.IsAlive(lastRow));
	EXPECT_TRUE(second.HasClass(lastRow, StringHash("probe-decorated")));
}
