#include "Shalltear/Editor/EditorListView.h"

#include "Shalltear/Core/Assert.h"
#include "Shalltear/Core/UI/UISystem.h"

#include "Shalltear/Editor/AssetBrowserOps.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/EditorIcons.h"

#include <algorithm>

namespace Shalltear {

	namespace {

		using EditorTheme::MetricToken;

		f32 RowFont() { return EditorTheme::GetMetric(MetricToken::FontRow); }
		f32 CellFont() { return EditorTheme::GetMetric(MetricToken::FontSmall); }

		constexpr i32 TO_THE_END = 1 << 24;

		constexpr f32 PING_SECONDS = 1.5f;

	} // anonymous namespace

	EditorListView::~EditorListView() {
		if (_owner) {
			_owner->UnregisterList(this);
		}
	}

	void EditorListView::Build(EditorPanel* owner, UI::Context& context, UI::ElementHandle parent, ListViewDesc desc) {
		if (_owner && _owner != owner) {
			_owner->UnregisterList(this);
		}
		_owner = owner;
		if (_owner) {
			_owner->RegisterList(this);
		}
		_context = &context;
		_desc = std::move(desc);
		_selection = ListSelection(_desc.mode);
		_cells.clear();
		_cellCues.clear();

		UI::Widgets::ScrollViewParts scroll = UI::Widgets::ScrollView(context, parent);
		_root = scroll.root;
		_viewport = scroll.viewport;

		UI::Style rootStyle = context.GetStyle(_root);
		rootStyle.flexGrow = 1.0f;
		context.SetStyle(_root, rootStyle);

		context.SetFocusable(_viewport, true);

		UI::ElementEvents& events = context.Events(_viewport);
		events.onKeyDown = [this](Key key, KeyMod mods) { return OnKey(key, mods); };
		if (_desc.onEmptyRightClick) {
			events.onRightClick = _desc.onEmptyRightClick;
		}
		if (_desc.onEmptyClick) {
			events.onClick = _desc.onEmptyClick;
		}

		AttachBandConsumer();
	}

	void EditorListView::AttachBandConsumer() {
		UI::Context* context = Live();
		if (!context) {
			return;
		}
		if (_desc.layout == ListLayout::Grid) {
			_virtual.Detach();
			if (context->IsAlive(_viewport)) {
				context->Events(_viewport).onScrollChanged =
					[this](const UI::ScrollBand&) { ReflowGrid(); };
				context->Events(_viewport).customDraw = [this](UI::Context& paintContext, UI::ElementHandle,
															   UI::PaintScope& painter) {
					PaintFoldOutBands(paintContext, painter);
				};
			}
			return;
		}
		_virtual.Attach(*context, _viewport, [this](u32 index) -> UI::ElementHandle {
							UI::Context* live = Live();
							return live ? BuildOneRow(*live, index) : UI::ElementHandle{}; }, [this]() { ApplyStyles(); }, [this]() {
							UI::Context* live = Live();
							if (live && _desc.onBuildTrailing) {
								_desc.onBuildTrailing(*live, _viewport);
							} });
	}

	void EditorListView::InContext(const std::function<void(UI::Context&)>& callback) const {
		if (UI::Context* context = Live(); context && callback) {
			callback(*context);
		}
	}

	UI::Context* EditorListView::Live() const {
		if (!_context) {
			return nullptr;
		}
		if (!UISystem::IsInitialized()) {
			return _context;
		}
		return UISystem::IsContextLive(_context) ? _context : nullptr;
	}

	void EditorListView::ComputeGuideMasks() {
		_guideMasks.assign(_rows.size(), 0u);
		bool openAtLevel[MAX_GUIDE_LEVELS] = {};
		for (size_t index = _rows.size(); index > 0; --index) {
			const u32 depth = _rows[index - 1].depth;
			u32 mask = 0;
			const u32 levels = depth < MAX_GUIDE_LEVELS ? depth : MAX_GUIDE_LEVELS;
			for (u32 level = 0; level < levels; ++level) {
				if (openAtLevel[level]) {
					mask |= 1u << level;
				}
			}
			_guideMasks[index - 1] = mask;
			if (depth > 0 && depth <= MAX_GUIDE_LEVELS) {
				openAtLevel[depth - 1] = true;
			}
			for (u32 level = depth; level < MAX_GUIDE_LEVELS; ++level) {
				openAtLevel[level] = false;
			}
		}
	}

	void EditorListView::SetRows(std::span<const ListRow> rows) {
		UI::Context* context = Live();
		if (!context || !context->IsAlive(_viewport)) {
			return;
		}
		_rows.assign(rows.begin(), rows.end());
		ComputeGuideMasks();

		Vector<u64> selectable{GetMemoryResource(MemTag::Editor)};
		selectable.reserve(_rows.size());
		for (const ListRow& row : _rows) {
			if (row.selectable) {
				SLT_APP_ASSERT_MSG(row.id != 0, "a selectable list row must not use id 0 ('no row')");
				selectable.push_back(row.id);
			}
		}
		_selection.SetRows(selectable);
		_published.assign(_selection.GetSelected().begin(), _selection.GetSelected().end());

		UI::Context* live = Live();
		if (!live) {
			return;
		}
		_pressed = false;
		if (_desc.layout == ListLayout::Grid) {
			live->ClearChildren(_viewport);
			_cells.clear();
			_cellCues.clear();
			BuildGrid(*live, _rows);
			ApplyStyles();
			return;
		}
		_cells.clear();
		_cellCues.clear();
		_gridColumns = 0;
		_virtual.Reset(static_cast<u32>(_rows.size()));
	}

	const ListRow* EditorListView::FindModelRow(u64 id) const {
		if (id == 0) {
			return nullptr;
		}
		for (const ListRow& row : _rows) {
			if (row.id == id) {
				return &row;
			}
		}
		return nullptr;
	}

	void EditorListView::SetLayout(ListLayout layout, f32 cellWidthPx, f32 cellHeightPx, f32 cellPaddingPx) {
		const ListLayout previous = _desc.layout;
		_desc.layout = layout;
		_desc.cellWidthPx = cellWidthPx;
		_desc.cellHeightPx = cellHeightPx;
		_desc.cellPaddingPx = cellPaddingPx;
		if (_desc.layout != previous) {
			AttachBandConsumer();
		}
	}

	UI::ElementHandle EditorListView::BuildOneRow(UI::Context& context, u32 index) {
		Handle<FontAsset> font = UISystem::GetDefaultFont();
		{
			const ListRow& row = _rows[index];
			if (row.onBuildLeading) {
				row.onBuildLeading(context, _viewport);
			}

			UI::Widgets::TreeRowDesc desc;
			desc.depth = row.depth;
			desc.guideMask = index < _guideMasks.size() ? _guideMasks[index] : 0u;
			desc.hasChildren = row.hasChildren;
			desc.expanded = row.expanded;
			desc.label = row.label;
			desc.font = font;
			desc.fontSizePx = RowFont();
			desc.userTag = row.id;
			if (row.selectable) {
				desc.onSelect = [this, id = row.id](KeyMod mods) { Click(id, mods); };
			} else if (row.onClick) {
				desc.onSelect = [this, click = row.onClick](KeyMod) { InContext(click); };
			}
			if (row.hasChildren && row.onToggleExpand) {
				desc.onToggleExpand = [toggle = row.onToggleExpand, expanded = row.expanded]() {
					toggle(!expanded);
				};
			}
			if (row.onActivate) {
				desc.onDoubleClick = [this, activate = row.onActivate]() { InContext(activate); };
			}
			if (row.onRightClick) {
				desc.onRightClick = [this, rightClick = row.onRightClick]() { InContext(rightClick); };
			}
			desc.onDragRow = row.onDragRow;
			desc.onDragOver = row.onDragOver;
			desc.onDrop = row.onDrop;

			UI::Widgets::TreeRowParts parts = UI::Widgets::TreeRow(context, _viewport, desc);
			TrackPress(context, parts.row);
			if (row.onDecorate) {
				row.onDecorate(context, parts);
			}
			if (_pingRow != 0 && row.id == _pingRow) {
				context.AddClass(parts.row, EditorTheme::Class::RowPing);
				_pingElement = parts.row;
			}
			return parts.row;
		}
	}

	void EditorListView::ReflowGrid() {
		if (_desc.layout != ListLayout::Grid) {
			return;
		}
		if (_pressed) {
			_reflowPending = true;
			return;
		}
		UI::Context* context = Live();
		if (!context || !context->IsAlive(_viewport)) {
			return;
		}
		f32 scale = std::max(0.001f, context->GetContentScale());
		f32 paneWidthLogical = context->GetLayout(_viewport).content.w / scale;
		if (paneWidthLogical <= 0.0f) {
			return;
		}
		if (AssetBrowserOps::GridColumnsFor(paneWidthLogical, _desc.cellWidthPx) == _gridColumns) {
			return;
		}
		_reflowPending = false;
		context->ClearChildren(_viewport);
		_cells.clear();
		_cellCues.clear();
		BuildGrid(*context, _rows);
		ApplyStyles();
	}

	void EditorListView::Tick(f32 deltaSeconds) {
		if (_reflowPending && !_pressed) {
			ReflowGrid();
		}

		if (_pingRevealPending) {
			if (_pingRevealWaits) {
				_pingRevealWaits = false;
			} else if (IndexOfRow(_pingRow) < _rows.size()) {
				_pingRevealPending = false;
				ScrollRowIntoView(_pingRow);
			}
		}
		if (_pingSecondsLeft > 0.0f) {
			_pingSecondsLeft -= deltaSeconds;
			if (_pingSecondsLeft <= 0.0f) {
				UI::Context* context = Live();
				if (context && context->IsAlive(_pingElement)) {
					context->RemoveClass(_pingElement, EditorTheme::Class::RowPing);
				}
				if (context && context->IsAlive(_pingCue)) {
					context->RemoveClass(_pingCue, EditorTheme::Class::RowPing);
				}
				_pingElement = {};
				_pingCue = {};
				_pingRow = 0;
				_pingSecondsLeft = 0.0f;
				_pingRevealPending = false;
			}
		}
	}

	void EditorListView::PingRow(u64 id) {
		UI::Context* context = Live();
		if (context && _pingElement.IsValid() && context->IsAlive(_pingElement)) {
			context->RemoveClass(_pingElement, EditorTheme::Class::RowPing);
		}
		if (context && _pingCue.IsValid() && context->IsAlive(_pingCue)) {
			context->RemoveClass(_pingCue, EditorTheme::Class::RowPing);
		}
		_pingElement = {};
		_pingCue = {};
		_pingRow = id;
		_pingSecondsLeft = id != 0 ? PING_SECONDS : 0.0f;
		_pingRevealPending = id != 0;
		_pingRevealWaits = id != 0;
	}

	void EditorListView::BuildGrid(UI::Context& context, std::span<const ListRow> rows) {
		Handle<FontAsset> font = UISystem::GetDefaultFont();
		f32 scale = std::max(0.001f, context.GetContentScale());
		f32 paneWidthLogical = context.GetLayout(_viewport).content.w / scale;
		u32 columns = AssetBrowserOps::GridColumnsFor(paneWidthLogical, _desc.cellWidthPx);
		_gridColumns = columns;

		Vector<u32> pendingButtons{GetMemoryResource(MemTag::Editor)};
		auto flushButtons = [&](UI::ElementHandle into) {
			for (u32 slot : pendingButtons) {
				BuildFoldButton(context, into, slot % columns, rows[slot]);
			}
			pendingButtons.clear();
		};

		UI::ElementHandle flow;
		for (u32 slot = 0; slot < rows.size(); ++slot) {
			if (slot % columns == 0) {
				if (context.IsAlive(flow)) {
					flushButtons(flow);
				}
				flow = context.CreateElement();
				context.AppendChild(_viewport, flow);
				context.AddClass(flow, StringHash("ed-asset-grid-row"));
				context.SetHoverable(flow, false);
			}

			const ListRow& row = rows[slot];
			UI::ElementHandle cell = context.CreateElement();
			context.AppendChild(flow, cell);
			context.AddClass(cell, StringHash("ed-asset-cell"));
			UI::Style cellStyle;
			cellStyle.width = UI::Length::Pixel(_desc.cellWidthPx);
			cellStyle.height = UI::Length::Pixel(_desc.cellHeightPx);
			cellStyle.cursor = SystemCursor::Hand;
			context.SetStyle(cell, cellStyle);
			context.SetUserTag(cell, row.id);

			UI::ElementHandle well = context.CreateElement();
			context.AppendChild(cell, well);
			context.AddClass(well, StringHash("ed-asset-cell-well"));
			context.SetHoverable(well, false);
			context.SetInheritsPointerState(well, true);
			UI::Style wellStyle;
			wellStyle.width = UI::Length::Pixel(_desc.cellWidthPx);
			wellStyle.height = UI::Length::Pixel(_desc.cellWidthPx);
			wellStyle.padding = UI::Edges::All(_desc.cellPaddingPx);
			wellStyle.flexShrink = 0.0f;
			context.SetStyle(well, wellStyle);

			if (_desc.onBuildCell) {
				_desc.onBuildCell(context, row.id, well);
			}
			UI::ElementHandle label;
			if (!row.label.empty()) {
				label = context.CreateElement();
				context.AppendChild(cell, label);
				context.AddClass(label, StringHash("ed-asset-cell-label"));
				context.SetHoverable(label, false);
				context.SetText(label, font, CellFont(),
								EditorTheme::GetColor(EditorTheme::ColorToken::Text), row.label);
				if (_desc.onDecorateCellLabel) {
					_desc.onDecorateCellLabel(context, row.id, label);
				}
			}

			if (row.hasChildren && row.onToggleExpand) {
				pendingButtons.push_back(slot);
			}

			TrackPress(context, cell);
			UI::Context* contextPtr = &context;
			UI::ElementEvents& events = context.Events(cell);
			if (row.selectable) {
				events.onClick = [this, contextPtr, id = row.id]() { Click(id, contextPtr->GetPressMods()); };
			} else if (row.onClick) {
				events.onClick = [this, click = row.onClick]() { InContext(click); };
			}
			if (row.onActivate) {
				events.onDoubleClick = [this, activate = row.onActivate]() { InContext(activate); };
			}
			if (row.onRightClick) {
				events.onRightClick = [this, rightClick = row.onRightClick]() { InContext(rightClick); };
			}
			events.onPressDrag = row.onDragRow;
			events.onDragOver = row.onDragOver;
			events.onDrop = row.onDrop;
			if (_pingRow != 0 && row.id == _pingRow) {
				context.AddClass(cell, EditorTheme::Class::RowPing);
				context.AddClass(well, EditorTheme::Class::RowPing);
				_pingElement = cell;
				_pingCue = well;
			}
			_cells.push_back(cell);
			_cellCues.push_back({well, label});
		}
		if (context.IsAlive(flow)) {
			flushButtons(flow);
		}
	}

	void EditorListView::BuildFoldButton(UI::Context& context, UI::ElementHandle flow, u32 column,
										 const ListRow& row) {
		using EditorTheme::MetricToken;
		const f32 cell = _desc.cellWidthPx;
		const f32 pad = _desc.cellPaddingPx;
		const f32 well = std::max(0.0f, cell - pad * 2.0f);
		const f32 diameter = AssetBrowserOps::FoldButtonSizeFor(
			cell, EditorTheme::GetMetric(MetricToken::BrowserCellMax),
			EditorTheme::GetMetric(MetricToken::BrowserFoldButtonSmall),
			EditorTheme::GetMetric(MetricToken::BrowserFoldButtonMedium),
			EditorTheme::GetMetric(MetricToken::BrowserFoldButtonLarge));
		const f32 glyphPx = std::round(diameter * EditorTheme::GetMetric(MetricToken::BrowserFoldGlyph));

		UI::ElementHandle button = context.CreateElement();
		context.AppendChild(flow, button);
		context.AddClass(button, StringHash("ed-asset-fold-button"));
		EditorIcons::SetGlyph(context, button,
							  row.expanded ? EditorIcons::Glyph::FoldOutClose : EditorIcons::Glyph::FoldOutOpen,
							  EditorTheme::GetColor(EditorTheme::ColorToken::Text), glyphPx);
		UI::Style style = context.GetStyle(button);
		style.position = UI::Position::Absolute;
		style.left = UI::Length::Pixel(static_cast<f32>(column + 1) * cell - diameter * 0.5f);
		style.top = UI::Length::Pixel(pad + (well - diameter) * 0.5f);
		style.width = UI::Length::Pixel(diameter);
		style.height = UI::Length::Pixel(diameter);
		style.borderRadius = UI::Radii::All(diameter * 0.5f);
		style.textAlign = UI::Align::Center;
		style.textAlignVertical = UI::Align::Center;
		style.flexShrink = 0.0f;
		style.cursor = SystemCursor::Hand;
		context.SetStyle(button, style);
		context.Events(button).onClick = [toggle = row.onToggleExpand, expanded = row.expanded]() {
			toggle(!expanded);
		};
	}

	void EditorListView::GetFoldOutBands(Vector<FoldOutBand>& out) const {
		out.clear();
		UI::Context* context = Live();
		if (!context || _desc.layout != ListLayout::Grid || _gridColumns == 0 || _cells.size() != _rows.size()) {
			return;
		}
		Vector<u32> depths{GetMemoryResource(MemTag::Editor)};
		depths.reserve(_rows.size());
		for (const ListRow& row : _rows) {
			depths.push_back(row.depth);
		}
		Vector<AssetBrowserOps::FoldOutSegment> segments{GetMemoryResource(MemTag::Editor)};
		AssetBrowserOps::FoldOutBandSegments(depths, _gridColumns, segments);
		for (const AssetBrowserOps::FoldOutSegment& segment : segments) {
			if (!context->IsAlive(_cells[segment.first]) || !context->IsAlive(_cells[segment.last])) {
				continue;
			}
			const UI::Rect& first = context->GetLayout(_cells[segment.first]).borderBox;
			const UI::Rect& last = context->GetLayout(_cells[segment.last]).borderBox;
			if (first.w <= 0.0f || last.w <= 0.0f) {
				continue;
			}
			FoldOutBand band;
			band.rect = {first.x, first.y, last.x + last.w - first.x, first.h};
			band.fromAbove = segment.fromAbove;
			band.continuesBelow = segment.continuesBelow;
			out.push_back(band);
		}
	}

	void EditorListView::PaintFoldOutBands(UI::Context& context, UI::PaintScope& painter) const {
		using EditorTheme::MetricToken;
		const Color color = EditorTheme::GetColor(EditorTheme::ColorToken::BrowserFoldBand);
		if (color.a <= 0.0f) {
			return;
		}
		Vector<FoldOutBand> bands{GetMemoryResource(MemTag::Editor)};
		GetFoldOutBands(bands);
		if (bands.empty()) {
			return;
		}
		const f32 scale = std::max(0.001f, context.GetContentScale());
		const f32 inset = EditorTheme::GetMetric(MetricToken::BrowserFoldInset) * scale;
		const f32 radius = EditorTheme::GetMetric(MetricToken::BrowserFoldRadius) * scale;
		const f32 capShare = EditorTheme::GetMetric(MetricToken::BrowserFoldCap);

		for (const FoldOutBand& band : bands) {
			const f32 top = band.rect.y + inset;
			const f32 bottom = band.rect.y + band.rect.h - inset;
			const f32 height = bottom - top;
			if (height <= 0.0f) {
				continue;
			}
			const f32 half = height * 0.5f;
			const f32 mid = top + half;
			const f32 x0 = band.rect.x;
			const f32 x1 = band.rect.x + band.rect.w;
			const f32 cap = std::min(height * capShare, band.rect.w * 0.25f);
			const f32 slant = std::sqrt(cap * cap + half * half);
			if (slant <= 0.0f) {
				continue;
			}
			const f32 reach = cap * half / slant;

			const UI::Radii closed = band.continuesBelow ? UI::Radii{} : UI::Radii{0.0f, radius, radius, 0.0f};
			painter.EmitQuad({x0 + cap, top, x1 - x0 - cap, height}, color, closed);

			const f32 skip = cap * cap / slant;
			const f32 sliver = skip * half / slant;
			for (f32 side : {-1.0f, 1.0f}) {
				const f32 edgeY = side < 0.0f ? top : bottom;
				const Vec2 along{cap / slant, -side * half / slant};
				const Vec2 inward{half / slant, side * cap / slant};
				const Vec2 start{x0 + along.x * skip, edgeY + along.y * skip};
				const Vec2 end{x0 + cap, mid};
				painter.EmitLine({start.x + inward.x * reach * 0.5f, start.y + inward.y * reach * 0.5f},
								 {end.x + inward.x * reach * 0.5f, end.y + inward.y * reach * 0.5f}, reach, color);
				const f32 fillX = x0 + cap * sliver / half;
				painter.EmitQuad({fillX, side < 0.0f ? top : bottom - sliver, x0 + cap - fillX, sliver}, color);
			}

			if (band.continuesBelow) {
				const Vec2 tip{x1 + cap, mid};
				for (f32 side : {-1.0f, 1.0f}) {
					const Vec2 from{x1, side < 0.0f ? top : bottom};
					const Vec2 inward{-half / slant, -side * cap / slant};
					painter.EmitLine({from.x + inward.x * reach * 0.5f, from.y + inward.y * reach * 0.5f},
									 {tip.x + inward.x * reach * 0.5f, tip.y + inward.y * reach * 0.5f}, reach, color);
				}
			}
		}
	}

	void EditorListView::TrackPress(UI::Context& context, UI::ElementHandle element) {
		UI::ElementEvents& events = context.Events(element);
		events.onMouseDown = [this]() { _pressed = true; };
		events.onPressEnd = [this]() { _pressed = false; };
	}

	UI::ElementHandle EditorListView::ElementForRow(u32 index) const {
		if (index >= _rows.size()) {
			return {};
		}
		if (_desc.layout == ListLayout::Grid) {
			return index < _cells.size() ? _cells[index] : UI::ElementHandle{};
		}
		return _virtual.GetElement(index);
	}

	u32 EditorListView::IndexOfRow(u64 id) const {
		const u32 count = static_cast<u32>(_rows.size());
		if (id == 0) {
			return count;
		}
		for (u32 index = 0; index < count; ++index) {
			if (_rows[index].id == id) {
				return index;
			}
		}
		return count;
	}

	void EditorListView::Click(u64 row, KeyMod mods) {
		AfterMutation(_selection.ApplyClick(row, mods), false);
	}

	void EditorListView::SetSelection(std::span<const u64> ids) {
		if (!_selection.SetSelection(ids)) {
			return;
		}
		_published.assign(_selection.GetSelected().begin(), _selection.GetSelected().end());
		ApplyStyles();
	}

	void EditorListView::RevealRow(u64 id) {
		ScrollRowIntoView(id);
	}

	bool EditorListView::SelectAll() {
		bool changed = _selection.SelectAll();
		AfterMutation(changed, true);
		return changed;
	}

	std::span<const u64> EditorListView::GetSelected() const {
		return _selection.GetSelected();
	}

	void EditorListView::AfterMutation(bool changed, bool scroll) {
		if (!changed) {
			return;
		}
		ApplyStyles();

		if (scroll) {
			ScrollRowIntoView(_selection.GetCursor());
		}

		std::span<const u64> selected = _selection.GetSelected();
		if (_published.size() == selected.size() &&
			std::equal(_published.begin(), _published.end(), selected.begin())) {
			return;
		}
		_published.assign(selected.begin(), selected.end());
		if (_desc.onSelectionChanged) {
			_desc.onSelectionChanged();
		}
	}

	void EditorListView::ScrollRowIntoView(u64 id) {
		UI::Context* context = Live();
		if (!context || id == 0) {
			return;
		}
		const u32 index = IndexOfRow(id);
		if (index >= _rows.size()) {
			return;
		}
		if (_desc.layout == ListLayout::Grid) {
			UI::ElementHandle cell = ElementForRow(index);
			if (cell.IsValid() && context->IsAlive(cell)) {
				context->ScrollIntoView(cell);
			}
			return;
		}
		_virtual.ScrollIntoView(index);
	}

	void EditorListView::SetClass(UI::Context& context, UI::ElementHandle element, StringHash klass, bool on) {
		if (!context.IsAlive(element) || on == context.HasClass(element, klass)) {
			return;
		}
		if (on) {
			context.AddClass(element, klass);
		} else {
			context.RemoveClass(element, klass);
		}
	}

	void EditorListView::ApplyStyles() {
		UI::Context* context = Live();
		if (!context) {
			return;
		}
		for (u32 index = 0; index < _rows.size(); ++index) {
			UI::ElementHandle element = ElementForRow(index);
			if (!element.IsValid() || !context->IsAlive(element)) {
				continue;
			}
			const ListRow& row = _rows[index];
			const bool wanted = row.selectable && _selection.IsSelected(row.id);
			SetClass(*context, element, StringHash("slt-selected"), wanted);
			if (_desc.layout == ListLayout::Grid && index < _cellCues.size()) {
				const CellCue& cue = _cellCues[index];
				SetClass(*context, cue.well, StringHash("slt-selected"), wanted);
				SetClass(*context, cue.label, StringHash("slt-selected"), wanted);
			}
		}
	}

	bool EditorListView::Command(PanelCommand command) {
		switch (command) {
		case PanelCommand::SelectAll:
			return SelectAll();
		case PanelCommand::Delete:
			if (!_desc.onDelete) {
				return false;
			}
			_desc.onDelete();
			return true;
		case PanelCommand::Duplicate:
			if (!_desc.onDuplicate) {
				return false;
			}
			_desc.onDuplicate();
			return true;
		case PanelCommand::Rename: {
			std::span<const u64> selected = _selection.GetSelected();
			if (!_desc.onRename || selected.size() != 1) {
				return false;
			}
			return _desc.onRename(selected.back());
		}
		case PanelCommand::Count:
			break;
		}
		return false;
	}

	void EditorListView::TakeFocus() {
		UI::Context* context = Live();
		if (context && context->IsAlive(_viewport)) {
			context->SetFocus(_viewport);
		}
	}

	bool EditorListView::HasFocus() const {
		UI::Context* context = Live();
		if (!context || !context->IsAlive(_viewport)) {
			return false;
		}
		UI::ElementHandle walk = context->GetFocused();
		for (u32 step = 0; walk.IsValid() && step < 64; ++step) {
			if (walk == _viewport) {
				return true;
			}
			const UI::Element* element = context->GetElement(walk);
			if (!element || element->parent == UI::INVALID_UI_INDEX) {
				return false;
			}
			walk = context->MakeHandle(element->parent);
		}
		return false;
	}

	void EditorListView::GetVisibleRows(Vector<u64>& out) const {
		out.clear();
		UI::Context* context = Live();
		if (!context || !context->IsAlive(_viewport)) {
			return;
		}
		const UI::Rect& window = context->GetLayout(_viewport).content;
		if (window.h <= 0.0f) {
			return;
		}
		for (u32 index = 0; index < _rows.size(); ++index) {
			UI::ElementHandle element = ElementForRow(index);
			if (!element.IsValid() || !context->IsAlive(element)) {
				continue;
			}
			const UI::Rect& box = context->GetLayout(element).borderBox;
			if (box.h > 0.0f && box.y + box.h > window.y && box.y < window.y + window.h) {
				out.push_back(_rows[index].id);
			}
		}
	}

	i32 EditorListView::PageStep() const {
		UI::Context* context = Live();
		if (!context || _rows.empty()) {
			return 1;
		}
		const u32 cursorIndex = IndexOfRow(_selection.GetCursor());
		f32 rowHeight = 0.0f;
		UI::ElementHandle onCursor = ElementForRow(cursorIndex);
		if (onCursor.IsValid() && context->IsAlive(onCursor)) {
			rowHeight = context->GetLayout(onCursor).borderBox.h;
		}
		if (rowHeight <= 0.0f && _desc.layout != ListLayout::Grid) {
			rowHeight = _virtual.HeightOf(cursorIndex);
		}
		for (u32 index = 0; rowHeight <= 0.0f && index < _rows.size(); ++index) {
			UI::ElementHandle any = ElementForRow(index);
			if (any.IsValid() && context->IsAlive(any)) {
				rowHeight = context->GetLayout(any).borderBox.h;
			}
		}
		if (rowHeight <= 0.0f) {
			return 1;
		}
		f32 window = context->GetLayout(_viewport).content.h;
		return std::max(1, static_cast<i32>(window / rowHeight));
	}

	bool EditorListView::OnKey(Key key, KeyMod mods) {
		ListNavigation nav = TranslateKey(key, mods);
		if (nav == ListNavigation::None) {
			return false;
		}

		switch (nav) {
		case ListNavigation::Prev:
			AfterMutation(_selection.MoveCursor(-1, mods), true);
			break;
		case ListNavigation::Next:
			AfterMutation(_selection.MoveCursor(1, mods), true);
			break;
		case ListNavigation::First:
			AfterMutation(_selection.MoveCursor(-TO_THE_END, mods), true);
			break;
		case ListNavigation::Last:
			AfterMutation(_selection.MoveCursor(TO_THE_END, mods), true);
			break;
		case ListNavigation::PageUp:
			AfterMutation(_selection.MoveCursor(-PageStep(), mods), true);
			break;
		case ListNavigation::PageDown:
			AfterMutation(_selection.MoveCursor(PageStep(), mods), true);
			break;
		case ListNavigation::Expand:
		case ListNavigation::Collapse: {
			const bool wantExpanded = (nav == ListNavigation::Expand);
			const ListRow* row = FindModelRow(_selection.GetCursor());
			if (row && row->hasChildren && row->expanded != wantExpanded && row->onToggleExpand) {
				row->onToggleExpand(wantExpanded);
			}
			break;
		}
		case ListNavigation::Activate: {
			const ListRow* row = FindModelRow(_selection.GetCursor());
			if (row && row->onActivate) {
				InContext(row->onActivate);
			}
			break;
		}
		case ListNavigation::None:
			break;
		}
		return true;
	}

} // namespace Shalltear
