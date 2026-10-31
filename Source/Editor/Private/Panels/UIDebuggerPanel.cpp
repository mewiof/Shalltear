#include "UIDebuggerPanel.h"

#include "Shalltear/Core/Input/Input.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"
#include "Shalltear/Core/Window/WindowSystem.h"

#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorTheme.h"

#include "PanelHeader.h"

#include <algorithm>
#include <utility>

namespace Shalltear {

	SLT_REGISTER_TYPE(UIDebuggerPanel)

	namespace {

		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;

		Color TextDim() { return EditorTheme::GetColor(ColorToken::TextDim); }
		f32 RowFont() { return EditorTheme::GetMetric(MetricToken::FontRow); }
		f32 SmallFont() { return EditorTheme::GetMetric(MetricToken::FontSmall); }
		f32 RowPadX() { return EditorTheme::GetMetric(MetricToken::RowPaddingX); }

		constexpr std::string_view NoTargetText = "No live UI context";

		constexpr f32 MIN_COLUMN_PX = 120.0f;

		constexpr i32 HIGHLIGHT_Z = 30000;

		Color BandColor(size_t ring) {
			switch (static_cast<UIDebugOps::HighlightRing>(ring)) {
			case UIDebugOps::HighlightRing::Margin:
				return EditorTheme::GetColor(ColorToken::UIDebugMargin);
			case UIDebugOps::HighlightRing::Border:
				return EditorTheme::GetColor(ColorToken::UIDebugBorder);
			case UIDebugOps::HighlightRing::Padding:
				return EditorTheme::GetColor(ColorToken::UIDebugPadding);
			default:
				return EditorTheme::GetColor(ColorToken::UIDebugContent);
			}
		}

		bool SnapshotsMatch(const UIDebugOps::Snapshot& a, const UIDebugOps::Snapshot& b) {
			if (a.rows.size() != b.rows.size()) {
				return false;
			}
			for (size_t i = 0; i < a.rows.size(); ++i) {
				const UIDebugOps::Row& lhs = a.rows[i];
				const UIDebugOps::Row& rhs = b.rows[i];
				if (lhs.element != rhs.element || lhs.key != rhs.key || lhs.depth != rhs.depth || lhs.childCount != rhs.childCount) {
					return false;
				}
			}
			return true;
		}

	} // anonymous namespace

	UIDebugOps::Target UIDebuggerPanel::CurrentTarget() const {
		UIDebugOps::Target target;
		target.kind = static_cast<UIDebugOps::Target::Kind>(_targetKind);
		target.id = _targetId;
		return target;
	}

	UI::Context* UIDebuggerPanel::LiveTarget() const {
		if (_targetId == 0) {
			return nullptr;
		}
		return UIDebugOps::ResolveTarget(CurrentTarget());
	}

	void UIDebuggerPanel::EnsureTarget() {
		if (_targetId != 0) {
			return;
		}
		Vector<UIDebugOps::Target> targets{GetMemoryResource(MemTag::Editor)};
		UIDebugOps::EnumerateTargets(targets);
		if (targets.empty()) {
			return;
		}
		const UI::Context* own = GetPanelContext();
		for (const UIDebugOps::Target& candidate : targets) {
			if (own && UIDebugOps::ResolveTarget(candidate) == own) {
				SetTarget(candidate);
				return;
			}
		}
		SetTarget(targets.front());
	}

	void UIDebuggerPanel::SetTarget(const UIDebugOps::Target& target) {
		if (_targetId != 0 && target.kind == CurrentTarget().kind && target.id == _targetId) {
			return;
		}
		DestroyTextOverlay();
		DestroySelectionHighlight();
		_targetKind = static_cast<u8>(target.kind);
		_targetId = target.id;
		_selectedPath.clear();
		_selectedIdentity = {};
		_hasSelection = false;
		_selectionResolved = false;
		_edits.Clear();
		_focusRow = StringHash{};
		_linkStatus.clear();
		_collapsed.clear();
		_snapshot.rows.clear();
		_lastRevision = 0;
		_forceRebuild = true;
	}

	void UIDebuggerPanel::OpenTargetPicker(UI::Context& context, UI::ElementHandle anchor) {
		Vector<UIDebugOps::Target> targets{GetMemoryResource(MemTag::Editor)};
		UIDebugOps::EnumerateTargets(targets);

		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
		for (const UIDebugOps::Target& target : targets) {
			UI::Widgets::MenuItem item;
			item.label = target.label;
			item.onSelect = [this, target]() { SetTarget(target); };
			items.push_back(std::move(item));
		}
		if (items.empty()) {
			UI::Widgets::MenuItem none;
			none.label = str(NoTargetText);
			none.enabled = false;
			items.push_back(std::move(none));
		}
		Vec2 at = context.GetPointerPosition();
		if (context.IsAlive(anchor)) {
			const UI::Rect& box = context.GetLayout(anchor).marginBox;
			at = {box.x, box.y + box.h};
		}
		UI::Widgets::OpenMenu(context, at, items, UISystem::GetDefaultFont(), RowFont());
	}

	void UIDebuggerPanel::OpenTextOverlayMenu(UI::Context& context, UI::ElementHandle anchor) {
		struct Entry {
			std::string_view label;
			UIDebugOps::TextBoxKinds kind;
		};
		const Entry entries[] = {
			{"Characters", UIDebugOps::TextBoxKinds::Characters},
			{"Words", UIDebugOps::TextBoxKinds::Words},
			{"Lines", UIDebugOps::TextBoxKinds::Lines},
		};
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
		for (const Entry& entry : entries) {
			UI::Widgets::MenuItem item;
			item.label = str(entry.label);
			item.checked = HasFlag(static_cast<UIDebugOps::TextBoxKinds>(_textOverlays), entry.kind);
			item.onSelect = [this, kind = entry.kind]() {
				_textOverlays ^= static_cast<u8>(kind);
				_headerDirty = true;
			};
			items.push_back(std::move(item));
		}
		Vec2 at = context.GetPointerPosition();
		if (context.IsAlive(anchor)) {
			const UI::Rect& box = context.GetLayout(anchor).marginBox;
			at = {box.x, box.y + box.h};
		}
		UI::Widgets::OpenMenu(context, at, items, UISystem::GetDefaultFont(), RowFont());
	}

	void UIDebuggerPanel::OpenAtlasPageMenu(UI::Context& context, UI::ElementHandle anchor) {
		UIDebugOps::BuildAtlasPages(_atlasPages);
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
		for (size_t i = 0; i < _atlasPages.size(); ++i) {
			UI::Widgets::MenuItem item;
			item.label = str(_atlasPages[i].name);
			item.onSelect = [this, index = static_cast<u8>(i)]() {
				_atlasPage = index;
				_inspectorBuilt = false;
			};
			items.push_back(std::move(item));
		}
		if (items.empty()) {
			UI::Widgets::MenuItem none;
			none.label = str(UIDebugOps::NoAtlasText);
			none.enabled = false;
			items.push_back(std::move(none));
		}
		Vec2 at = context.GetPointerPosition();
		if (context.IsAlive(anchor)) {
			const UI::Rect& box = context.GetLayout(anchor).marginBox;
			at = {box.x, box.y + box.h};
		}
		UI::Widgets::OpenMenu(context, at, items, UISystem::GetDefaultFont(), RowFont());
	}

	void UIDebuggerPanel::SyncTextOverlay() {
		UI::Context* target = LiveTarget();
		if (_textOverlays == 0 || !target) {
			DestroyTextOverlay();
			return;
		}
		if (target->IsAlive(_overlayHost)) {
			return;
		}
		_overlayHost = target->CreateElement();
		target->AppendChild(target->GetRoot(), _overlayHost);
		target->AddClass(_overlayHost, StringHash(UIDebugOps::MarkerClass));
		UI::Style style;
		style.position = UI::Position::Absolute;
		style.pointerEvents = false;
		style.left = UI::Length::Pixel(0.0f);
		style.top = UI::Length::Pixel(0.0f);
		style.width = UI::Length::Percent(1.0f);
		style.height = UI::Length::Percent(1.0f);
		style.zIndex = HIGHLIGHT_Z - 1;
		target->SetStyle(_overlayHost, style);
		target->Events(_overlayHost).customDraw = [this](UI::Context& context, UI::ElementHandle, UI::PaintScope& paint) {
			DrawTextOverlay(context, paint);
		};
	}

	void UIDebuggerPanel::DestroyTextOverlay() {
		if (UI::Context* target = LiveTarget(); target && target->IsAlive(_overlayHost)) {
			target->DestroyElement(_overlayHost);
		}
		_overlayHost = {};
		_textBoxes.clear();
		_overlayStats = {};
	}

	void UIDebuggerPanel::DrawTextOverlay(UI::Context& target, UI::PaintScope& paint) {
		const auto kinds = static_cast<UIDebugOps::TextBoxKinds>(_textOverlays);
		_overlayStats = UIDebugOps::CollectTextBoxes(target, kinds, _textBoxes);
		const f32 stroke = std::max(1.0f, std::round(target.GetContentScale()));
		const UI::Edges widths{stroke, stroke, stroke, stroke};
		for (const UIDebugOps::TextBox& box : _textBoxes) {
			Color color = EditorTheme::GetColor(ColorToken::UIDebugTextChar);
			if (box.kind == UIDebugOps::TextBoxKinds::Words) {
				color = EditorTheme::GetColor(ColorToken::UIDebugTextWord);
			} else if (box.kind == UIDebugOps::TextBoxKinds::Lines) {
				color = EditorTheme::GetColor(ColorToken::UIDebugTextLine);
			}
			paint.EmitRing(box.box, widths, color);
		}
	}

	void UIDebuggerPanel::OnBuildContent(UI::Context& context, UI::ElementHandle root) {
		UI::Style rootStyle;
		rootStyle.flexDirection = UI::FlexDirection::Column;
		rootStyle.alignItems = UI::Align::Stretch;
		rootStyle.width = UI::Length::Percent(1.0f);
		rootStyle.height = UI::Length::Percent(1.0f);
		context.SetStyle(root, rootStyle);

		_headerHost = context.CreateElement();
		context.AppendChild(root, _headerHost);
		UI::Style hostStyle;
		hostStyle.width = UI::Length::Percent(1.0f);
		hostStyle.flexShrink = 0.0f;
		context.SetStyle(_headerHost, hostStyle);
		EnsureTarget();
		BuildHeader(context);

		UI::Widgets::SplitDesc split;
		split.axis = UI::FlexDirection::Row;
		split.sizing = UI::Widgets::SplitSizing::Ratio;
		split.position = _splitRatio;
		split.minFirstPx = MIN_COLUMN_PX;
		split.minSecondPx = MIN_COLUMN_PX;
		split.onPositionChanged = [this](f32 position) { _splitRatio = position; };
		UI::Widgets::SplitParts columns = UI::Widgets::Split(context, root, split);
		UI::Style columnsStyle = context.GetStyle(columns.root);
		columnsStyle.width = UI::Length::Percent(1.0f);
		columnsStyle.height = UI::Length::Pixel(0.0f);
		columnsStyle.flexGrow = 1.0f;
		context.SetStyle(columns.root, columnsStyle);

		UI::ElementHandle listHost = context.CreateElement();
		context.AppendChild(columns.first, listHost);
		UI::Style listHostStyle;
		listHostStyle.width = UI::Length::Percent(1.0f);
		listHostStyle.height = UI::Length::Percent(1.0f);
		listHostStyle.flexDirection = UI::FlexDirection::Column;
		context.SetStyle(listHost, listHostStyle);
		context.AddClass(listHost, StringHash(UIDebugOps::MarkerClass));

		ListViewDesc list;
		list.layout = ListLayout::Rows;
		list.mode = ListSelectionMode::Single;
		list.onSelectionChanged = [this]() { PublishSelectionFromList(); };
		_list.Build(this, context, listHost, std::move(list));

		UI::ElementHandle inspectorRoot = context.CreateElement();
		context.AppendChild(columns.second, inspectorRoot);
		UI::Style inspectorRootStyle;
		inspectorRootStyle.width = UI::Length::Percent(1.0f);
		inspectorRootStyle.height = UI::Length::Percent(1.0f);
		inspectorRootStyle.flexDirection = UI::FlexDirection::Column;
		context.SetStyle(inspectorRoot, inspectorRootStyle);
		context.AddClass(inspectorRoot, StringHash(UIDebugOps::MarkerClass));
		UI::Widgets::ScrollViewParts scroll = UI::Widgets::ScrollView(context, inspectorRoot);
		UI::Style scrollStyle = context.GetStyle(scroll.root);
		scrollStyle.width = UI::Length::Percent(1.0f);
		scrollStyle.height = UI::Length::Pixel(0.0f);
		scrollStyle.flexGrow = 1.0f;
		context.SetStyle(scroll.root, scrollStyle);
		_inspectorHost = scroll.viewport;
		UI::Style hostColumn = context.GetStyle(_inspectorHost);
		hostColumn.flexDirection = UI::FlexDirection::Column;
		hostColumn.alignItems = UI::Align::Stretch;
		hostColumn.width = UI::Length::Percent(1.0f);
		context.SetStyle(_inspectorHost, hostColumn);
		_inspectorBuilt = false;

		UI::ElementHandle status = context.CreateElement();
		context.AppendChild(root, status);
		UI::Style statusStyle;
		statusStyle.width = UI::Length::Percent(1.0f);
		statusStyle.flexShrink = 0.0f;
		statusStyle.padding = {RowPadX(), 2.0f, RowPadX(), 2.0f};
		context.SetStyle(status, statusStyle);
		_statusLabel = context.CreateElement();
		context.AppendChild(status, _statusLabel);
		UI::Style labelStyle;
		labelStyle.width = UI::Length::Pixel(0.0f);
		labelStyle.flexGrow = 1.0f;
		labelStyle.flexShrink = 1.0f;
		labelStyle.textOverflow = UI::TextOverflow::Ellipsis;
		context.SetStyle(_statusLabel, labelStyle);
		context.SetText(_statusLabel, UISystem::GetDefaultFont(), SmallFont(), TextDim(), "");

		_forceRebuild = true;
	}

	void UIDebuggerPanel::BuildHeader(UI::Context& context) {
		context.ClearChildren(_headerHost);
		_targetButton = {};
		_overlayButton = {};
		_atlasButton = {};

		PanelHeader::Desc desc;
		desc.onBuildLeading = [this](UI::Context& context, UI::ElementHandle strip) {
			UIDebugOps::Target target = CurrentTarget();
			str label{NoTargetText};
			if (_targetId != 0) {
				Vector<UIDebugOps::Target> targets{GetMemoryResource(MemTag::Editor)};
				UIDebugOps::EnumerateTargets(targets);
				label = "(gone)";
				for (const UIDebugOps::Target& candidate : targets) {
					if (candidate.kind == target.kind && candidate.id == target.id) {
						label = candidate.label;
						break;
					}
				}
			}
			_targetButton = context.CreateElement();
			context.AppendChild(strip, _targetButton);
			context.AddClass(_targetButton, StringHash("slt-button"));
			context.AddClass(_targetButton, EditorTheme::Class::ToolButton);
			context.AddClass(_targetButton, EditorTheme::Class::RowText);
			context.SetText(_targetButton, UISystem::GetDefaultFont(),
							EditorTheme::GetMetric(MetricToken::FontBody),
							EditorTheme::GetColor(ColorToken::Text), label);
			context.Events(_targetButton).onClick = [this]() {
				if (UI::Context* live = GetPanelContext()) {
					OpenTargetPicker(*live, _targetButton);
				}
			};
			UI::Style style = context.GetStyle(_targetButton);
			style.width = UI::Length::Pixel(0.0f);
			style.flexGrow = 1.0f;
			style.flexShrink = 1.0f;
			context.SetStyle(_targetButton, style);

			PanelHeader::ToolButtonDesc pick;
			pick.icon = EditorIcons::Glyph::Picker;
			pick.fallback = "Pick";
			pick.tooltip = "Pick Element: click an element in any window, Escape cancels";
			pick.checked = IsPicking();
			pick.onClick = [this]() { ArmPick(); };
			_pickButton = PanelHeader::ToolButton(context, strip, pick).button;

			PanelHeader::ToolButtonDesc overlays;
			overlays.icon = EditorIcons::Glyph::TextOverlay;
			overlays.fallback = "Text";
			overlays.tooltip = "Text overlays: box the characters/words/lines of each run";
			overlays.checked = _textOverlays != 0;
			overlays.onClick = [this]() {
				if (UI::Context* live = GetPanelContext()) {
					OpenTextOverlayMenu(*live, _overlayButton);
				}
			};
			_overlayButton = PanelHeader::ToolButton(context, strip, overlays).button;

			PanelHeader::ToolButtonDesc atlas;
			atlas.icon = EditorIcons::Glyph::AssetTexture;
			atlas.fallback = "Atlas";
			atlas.tooltip = "Texture Atlas Viewer: the pages this toolkit rasterizes into";
			atlas.checked = _atlasOpen;
			atlas.onClick = [this]() {
				_atlasOpen = !_atlasOpen;
				_headerDirty = true;
				_inspectorBuilt = false;
			};
			_atlasButton = PanelHeader::ToolButton(context, strip, atlas).button;
		};
		desc.searchOpen = _searchOpen;
		desc.searchText = _filter;
		desc.focusSearch = _focusSearch;
		desc.onSearchToggled = [this](bool open) {
			_searchOpen = open;
			_focusSearch = open;
			if (UI::Context* live = GetPanelContext()) {
				BuildHeader(*live);
			}
			_forceRebuild = true;
		};
		desc.onSearchChanged = [this](std::string_view text) {
			if (_filter == text) {
				return;
			}
			_filter = str(text);
			_forceRebuild = true;
		};
		PanelHeader::Build(context, _headerHost, desc);
		_focusSearch = false;
	}

	void UIDebuggerPanel::RebuildRows(UI::Context& context) {
		_rows.clear();
		_view.clear();

		if (UI::Context* target = LiveTarget()) {
			UIDebugOps::TreeViewDesc view;
			view.query = _filter;
			view.collapsed = _collapsed;
			UIDebugOps::BuildTreeView(*target, _snapshot, _names, view, _view);
		}

		for (const UIDebugOps::TreeRow& row : _view) {
			ListRow desc;
			desc.id = row.key;
			desc.depth = row.depth;
			desc.hasChildren = row.hasChildren;
			desc.expanded = row.expanded;
			desc.label = row.label;
			if (row.hasChildren) {
				desc.onToggleExpand = [this, key = row.key](bool) { ToggleExpanded(key); };
			}
			const bool answer = _filter.empty() || row.matched;
			desc.onDecorate = [hasChildren = row.hasChildren, expanded = row.expanded,
							   answer](UI::Context& rowContext, const UI::Widgets::TreeRowParts& parts) {
				EditorIcons::SetRowGutter(rowContext, parts, hasChildren, expanded, TextDim(),
										  EditorIcons::Glyph::UIElement, TextDim());
				if (!answer) {
					rowContext.AddClass(parts.labelElement, EditorTheme::Class::RowOff);
				}
			};
			_rows.push_back(std::move(desc));
		}
		_list.SetRows(_rows);
		PushSelectionToList();
		UpdateStatus(context);

		if (UI::Context* target = LiveTarget()) {
			_lastRevision = target->GetTreeRevision();
		}
	}

	void UIDebuggerPanel::UpdateStatus(UI::Context& context) {
		if (!context.IsAlive(_statusLabel)) {
			return;
		}
		str text;
		if (IsPicking()) {
			text = "Pick Element: click an element in any window, Escape cancels";
		} else if (!_linkStatus.empty()) {
			text = _linkStatus;
		} else if (_targetId == 0) {
			text = NoTargetText;
		} else if (!LiveTarget()) {
			text = "Target is gone. Showing the last snapshot";
		} else {
			text = std::to_string(_snapshot.rows.size()) + " elements";
			if (!_filter.empty()) {
				text += ", " + std::to_string(_view.size()) + " shown";
			}
			if (_textOverlays != 0) {
				text += ", " + std::to_string(_overlayStats.runs) + " runs, " + std::to_string(_overlayStats.boxes) + " text boxes";
				if (_overlayStats.wordlessRuns != 0) {
					text += " (" + std::to_string(_overlayStats.wordlessRuns) + " wrapped runs have no word spans)";
				}
			}
			if (_hasSelection && !_selectionResolved) {
				text += ". Selected element is gone";
			} else if (_hasSelection && !_selectionDrawn) {
				text += _filter.empty() ? ". Selected element is folded away"
										: ". Selected element is filtered out";
			}
		}
		context.SetTextString(_statusLabel, text);
	}

	void UIDebuggerPanel::PublishSelectionFromList() {
		std::span<const u64> selected = _list.GetSelected();
		UI::Context* target = LiveTarget();
		UI::ElementHandle element;
		if (!selected.empty() && target) {
			const u64 key = selected.back();
			for (const UIDebugOps::TreeRow& row : _view) {
				if (row.key == key) {
					element = row.element;
					break;
				}
			}
		}
		SelectElement(target, element);
	}

	void UIDebuggerPanel::SelectElement(const UI::Context* target, UI::ElementHandle element) {
		_edits.Clear();
		_focusRow = StringHash{};
		_linkStatus.clear();
		if (!target || !target->IsAlive(element)) {
			_hasSelection = false;
			_selectionResolved = false;
			_selectedPath.clear();
			_selectedIdentity = {};
			return;
		}
		UIDebugOps::PathTo(*target, element, _selectedPath);
		_selectedIdentity = UIDebugOps::IdentityOf(*target, element);
		_hasSelection = true;
		_selectionResolved = true;
	}

	void UIDebuggerPanel::PushSelectionToList() {
		const bool reveal = std::exchange(_revealSelection, false);
		_selectionDrawn = false;
		if (!_hasSelection) {
			_selectionResolved = false;
			_list.SetSelection({});
			return;
		}
		UI::Context* target = LiveTarget();
		UI::ElementHandle element =
			target ? UIDebugOps::ResolveSame(*target, _selectedPath, _selectedIdentity) : UI::ElementHandle{};
		_selectionResolved = element.IsValid();
		if (!_selectionResolved) {
			_list.SetSelection({});
			return;
		}
		const u64 key = UIDebugOps::PathKey(_selectedPath);
		u64 ids[1] = {key};
		_list.SetSelection(ids);
		_selectionDrawn = std::any_of(_view.begin(), _view.end(),
									  [key](const UIDebugOps::TreeRow& row) { return row.key == key; });
		if (reveal && _selectionDrawn) {
			_list.RevealRow(key);
		}
	}

	void UIDebuggerPanel::ToggleExpanded(u64 key) {
		auto at = std::find(_collapsed.begin(), _collapsed.end(), key);
		if (at == _collapsed.end()) {
			_collapsed.push_back(key);
		} else {
			_collapsed.erase(at);
		}
		_forceRebuild = true;
	}

	void UIDebuggerPanel::CreateHighlightBands(UI::Context& context,
											   UI::ElementHandle (&bands)[UIDebugOps::HIGHLIGHT_BAND_COUNT],
											   i32 zIndex) {
		for (size_t i = 0; i < UIDebugOps::HIGHLIGHT_BAND_COUNT; ++i) {
			UI::ElementHandle band = context.CreateElement();
			context.AppendChild(context.GetRoot(), band);
			context.AddClass(band, StringHash(UIDebugOps::MarkerClass));
			UI::Style style;
			style.position = UI::Position::Absolute;
			style.pointerEvents = false;
			style.zIndex = zIndex;
			context.SetStyle(band, style);
			context.SetVisible(band, false);
			bands[i] = band;
		}
	}

	void UIDebuggerPanel::DestroyHighlightBands(UI::Context& context,
												UI::ElementHandle (&bands)[UIDebugOps::HIGHLIGHT_BAND_COUNT]) {
		for (UI::ElementHandle& band : bands) {
			if (context.IsAlive(band)) {
				context.DestroyElement(band);
			}
			band = {};
		}
	}

	UIDebuggerPanel::PickTarget UIDebuggerPanel::OpenPickTarget(const UIDebugOps::Target& target, UI::Context& context) {
		PickTarget entry;
		entry.target = target;
		CreateHighlightBands(context, entry.bands, HIGHLIGHT_Z);
		entry.claim = context.ClaimPointer(entry.bands[0], CursorMode::Normal);
		return entry;
	}

	void UIDebuggerPanel::ClosePickTarget(UI::Context& context, PickTarget& entry) {
		context.ReleasePointer(entry.claim);
		entry.claim = 0;
		DestroyHighlightBands(context, entry.bands);
	}

	void UIDebuggerPanel::SyncSelectionHighlight() {
		UI::Context* target = LiveTarget();
		if (!target || IsPicking() || !_hasSelection) {
			DestroySelectionHighlight();
			return;
		}
		UI::ElementHandle element = UIDebugOps::ResolveSame(*target, _selectedPath, _selectedIdentity);
		if (!target->IsAlive(element)) {
			DestroySelectionHighlight();
			return;
		}
		if (!target->IsAlive(_selectionBands[0])) {
			CreateHighlightBands(*target, _selectionBands, HIGHLIGHT_Z - 2);
		}
		DrawHighlight(*target, _selectionBands, element);
	}

	void UIDebuggerPanel::DestroySelectionHighlight() {
		if (UI::Context* target = LiveTarget()) {
			DestroyHighlightBands(*target, _selectionBands);
		}
		for (UI::ElementHandle& band : _selectionBands) {
			band = {};
		}
	}

	bool UIDebuggerPanel::IsPicking() const { return !_pickTargets.empty(); }

	void UIDebuggerPanel::ArmPick() {
		if (IsPicking()) {
			return;
		}
		SyncPickTargets();
		_pickPressed = false;
		_headerDirty = true;
	}

	void UIDebuggerPanel::DisarmPick() {
		if (_pickTargets.empty()) {
			return;
		}
		ReleasePickTargets();
		_pickPressed = false;
		_pickSpent = false;
		_headerDirty = true;
	}

	void UIDebuggerPanel::ReleasePickTargets() {
		for (PickTarget& entry : _pickTargets) {
			if (UI::Context* context = UIDebugOps::ResolveTarget(entry.target)) {
				ClosePickTarget(*context, entry);
			}
		}
		_pickTargets.clear();
	}

	void UIDebuggerPanel::SyncPickTargets() {
		for (size_t i = _pickTargets.size(); i-- > 0;) {
			if (!UIDebugOps::ResolveTarget(_pickTargets[i].target)) {
				_pickTargets.erase(_pickTargets.begin() + static_cast<i64>(i));
			}
		}
		Vector<UIDebugOps::Target> targets{GetMemoryResource(MemTag::Editor)};
		UIDebugOps::EnumerateTargets(targets);
		for (const UIDebugOps::Target& target : targets) {
			bool held = false;
			for (const PickTarget& existing : _pickTargets) {
				if (existing.target.kind == target.kind && existing.target.id == target.id) {
					held = true;
					break;
				}
			}
			UI::Context* context = held ? nullptr : UIDebugOps::ResolveTarget(target);
			if (!context) {
				continue;
			}
			PickTarget entry = OpenPickTarget(target, *context);
			if (entry.claim == 0) {
				ClosePickTarget(*context, entry);
				continue;
			}
			_pickTargets.push_back(entry);
		}
	}

	void UIDebuggerPanel::UpdatePick() {
		if (_pickSpent) {
			DisarmPick();
			return;
		}
		SyncPickTargets();

		Handle<Window> under = WindowSystem::GetWindowAtPoint(Input::GetMousePosition());
		UI::Context* hovered = under.IsValid() ? UISystem::GetContext(under) : nullptr;
		UI::ElementHandle picked;
		if (hovered) {
			const Vec2I client = Input::GetMousePosition(under);
			picked = UIDebugOps::PickAt(*hovered, {static_cast<f32>(client.x), static_cast<f32>(client.y)});
		}
		for (PickTarget& entry : _pickTargets) {
			UI::Context* context = UIDebugOps::ResolveTarget(entry.target);
			if (!context) {
				continue;
			}
			DrawHighlight(*context, entry.bands, context == hovered ? picked : UI::ElementHandle{});
		}

		if (Input::WasKeyPressed(Key::Escape)) {
			DisarmPick();
			return;
		}

		if (Input::WasMouseButtonPressed(MouseButton::Left)) {
			_pickPressed = true;
		}
		if (!_pickPressed || !Input::WasMouseButtonReleased(MouseButton::Left)) {
			return;
		}
		if (hovered && picked.IsValid() && !(hovered == GetPanelContext() && picked == _pickButton)) {
			UIDebugOps::Target landed;
			landed.kind = UIDebugOps::Target::Kind::Window;
			landed.id = under.GetInstanceId();
			CommitPick(landed, *hovered, picked);
		}
		_pickSpent = true;
	}

	void UIDebuggerPanel::DrawHighlight(UI::Context& context,
										UI::ElementHandle (&bands)[UIDebugOps::HIGHLIGHT_BAND_COUNT],
										UI::ElementHandle element) {
		UIDebugOps::Highlight highlight;
		if (context.IsAlive(element)) {
			UIDebugOps::BuildHighlight(context, element, highlight);
		}
		for (size_t i = 0; i < UIDebugOps::HIGHLIGHT_BAND_COUNT; ++i) {
			UI::ElementHandle band = bands[i];
			if (!context.IsAlive(band)) {
				continue;
			}
			const UIDebugOps::HighlightBand& source = highlight.bands[i];
			const bool content = i == static_cast<size_t>(UIDebugOps::HighlightRing::Content);
			const f32 ringInk = source.ring.left + source.ring.top + source.ring.right + source.ring.bottom;
			if (source.box.w <= 0.0f || source.box.h <= 0.0f || (!content && ringInk <= 0.0f)) {
				context.SetVisible(band, false);
				continue;
			}
			UI::Style style = context.GetStyle(band);
			style.left = UI::Length::Pixel(source.box.x);
			style.top = UI::Length::Pixel(source.box.y);
			style.width = UI::Length::Pixel(source.box.w);
			style.height = UI::Length::Pixel(source.box.h);
			if (content) {
				style.background = UI::Brush::SolidColor(BandColor(i));
				style.borderWidth = {};
			} else {
				style.background = {};
				style.borderColor = BandColor(i);
				style.borderWidth = source.ring;
			}
			context.SetStyle(band, style);
			context.SetVisible(band, true);
		}
	}

	void UIDebuggerPanel::CommitPick(const UIDebugOps::Target& target, UI::Context& context, UI::ElementHandle element) {
		if (target.kind != CurrentTarget().kind || target.id != _targetId) {
			SetTarget(target);
		}
		SelectElement(&context, element);
		UIDebugOps::ExpandAncestors(_selectedPath, _collapsed);
		_revealSelection = true;
		_forceRebuild = true;
		RaisePanelWindow();
	}

	void UIDebuggerPanel::RaisePanelWindow() {
		UI::Context* context = GetPanelContext();
		if (!context) {
			return;
		}
		Handle<Window> own;
		UISystem::ForEachContext([&own, context](Handle<Window> window, UI::Context& candidate) {
			if (!own.IsValid() && &candidate == context) {
				own = window;
			}
		});
		if (Window* window = own.Get()) {
			window->Focus();
		}
	}

	void UIDebuggerPanel::OnTick(f32) {
		UI::Context* context = GetPanelContext();
		if (!context) {
			return;
		}
		EnsureTarget();

		if (IsPicking()) {
			UpdatePick();
		}
		if (_headerDirty) {
			_headerDirty = false;
			BuildHeader(*context);
			UpdateStatus(*context);
		}

		UI::Context* target = LiveTarget();
		const bool alive = target != nullptr;
		if (alive != _targetWasAlive) {
			_targetWasAlive = alive;
			BuildHeader(*context);
			UpdateStatus(*context);
			if (!alive) {
				return;
			}
		}
		if (!alive) {
			return;
		}

		SyncTextOverlay();

		const u32 revision = target->GetTreeRevision();
		if (revision != _lastRevision) {
			UIDebugOps::BuildSnapshot(*target, _pending);
			const bool moved = !SnapshotsMatch(_snapshot, _pending);
			_snapshot.rows.swap(_pending.rows);
			_snapshot.revision = _pending.revision;
			if (_namedTargetId != _targetId) {
				FeedProjectNames();
				_names.Rebuild(*target);
				_namedTargetId = _targetId;
				BuildHeader(*context);
				_inspectorBuilt = false;
			}
			if (moved || _forceRebuild) {
				_forceRebuild = false;
				RebuildRows(*context);
			} else {
				_lastRevision = target->GetTreeRevision();
			}
		} else if (_forceRebuild) {
			_forceRebuild = false;
			RebuildRows(*context);
		}

		if (!_editGesture) {
			if (InspectorKey key = ReadInspectorKey(); !_inspectorBuilt || key != _inspectorKey) {
				BuildInspector(*context);
			}
		}

		SyncSelectionHighlight();

		if (_textOverlays != 0) {
			UpdateStatus(*context);
		}
	}

	void UIDebuggerPanel::OnClose() {
		DisarmPick();
		_pickTargets.shrink_to_fit();
		DestroyTextOverlay();
		DestroySelectionHighlight();
		_textBoxes.shrink_to_fit();
		_atlasPages.clear();
		_atlasPages.shrink_to_fit();
		_snapshot.rows.clear();
		_snapshot.rows.shrink_to_fit();
		_pending.rows.clear();
		_pending.rows.shrink_to_fit();
		_view.clear();
		_view.shrink_to_fit();
		_rows.clear();
		_rows.shrink_to_fit();
		_collapsed.clear();
		_collapsed.shrink_to_fit();
		_selectedPath.clear();
		_selectedPath.shrink_to_fit();
		_selectedIdentity = {};
		_attributes.clear();
		_attributes.shrink_to_fit();
		_properties.clear();
		_properties.shrink_to_fit();
		_matches.clear();
		_matches.shrink_to_fit();
		_sheets.clear();
		_sheets.shrink_to_fit();
		_edits.Clear();
		_edits.declarations.shrink_to_fit();
		_linkStatus.clear();
		_linkStatus.shrink_to_fit();
		_names = UIDebugOps::NameDictionary();
	}

	EditorIcons::Icon UIDebuggerPanel::GetTabIcon() const {
		return EditorIcons::Glyph::UIElement;
	}

} // namespace Shalltear
