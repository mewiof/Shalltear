#include "ConsolePanel.h"

#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/ProjectOps.h"
#include "Shalltear/Editor/ScriptProjectOps.h"

#include "GameModuleHost.h"
#include "PanelEmptyState.h"

#include <algorithm>
#include <format>

namespace Shalltear {

	namespace {

		constexpr size_t ENTRY_CAP = 4096;
		constexpr size_t ROW_CAP = 500;

		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;

		f32 RowFontPx() { return EditorTheme::GetMetric(MetricToken::FontRow); }
		f32 RowPaddingX() { return EditorTheme::GetMetric(MetricToken::RowPaddingX); }
		f32 SmallFontPx() { return EditorTheme::GetMetric(MetricToken::FontSmall); }

		f32 TagWidth() {
			return EditorIcons::IsAvailable(EditorIcons::Glyph::LevelInfo.face) ? 20.0f : 46.0f;
		}

		str CountText(u32 count) {
			return count > 999 ? str("999+") : std::format("{}", count);
		}

	} // anonymous namespace

	ConsolePanel::LogKind ConsolePanel::KindOf(LogLevel level) {
		if (level >= LogLevel::Error) {
			return LogKind::Errors;
		}
		if (level == LogLevel::Warn) {
			return LogKind::Warnings;
		}
		return level == LogLevel::Info ? LogKind::Messages : LogKind::Trace;
	}

	bool& ConsolePanel::ShownFlag(LogKind kind) {
		switch (kind) {
		case LogKind::Trace:
			return showTrace;
		case LogKind::Warnings:
			return showWarnings;
		case LogKind::Errors:
			return showErrors;
		default:
			return showMessages;
		}
	}

	bool ConsolePanel::IsShown(LogKind kind) const {
		return const_cast<ConsolePanel*>(this)->ShownFlag(kind);
	}

	void ConsolePanel::ToggleKind(LogKind kind) {
		bool& shown = ShownFlag(kind);
		shown = !shown;
		_rowsDirty = true;
		if (UI::Context* context = GetPanelContext()) {
			RefreshCounts(*context);
		}
	}

	bool ConsolePanel::PassesFilter(const ConsoleEntry& entry) const {
		if (!IsShown(KindOf(entry.level))) {
			return false;
		}
		if (!textFilter.empty() &&
			entry.message.find(textFilter) == str::npos &&
			entry.category.find(textFilter) == str::npos) {
			return false;
		}
		return true;
	}

	void ConsolePanel::OnBuildContent(UI::Context& context, UI::ElementHandle root) {
		Handle<FontAsset> font = UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{};

		UI::Style rootStyle;
		rootStyle.flexDirection = UI::FlexDirection::Column;
		rootStyle.alignItems = UI::Align::Stretch;
		rootStyle.width = UI::Length::Percent(1.0f);
		rootStyle.height = UI::Length::Percent(1.0f);
		context.SetStyle(root, rootStyle);

		UI::ElementHandle toolbar = context.CreateElement();
		context.AppendChild(root, toolbar);
		UI::Style toolbarStyle;
		toolbarStyle.flexDirection = UI::FlexDirection::Row;
		toolbarStyle.alignItems = UI::Align::Center;
		toolbarStyle.padding = {6.0f, 4.0f, 6.0f, 4.0f};
		toolbarStyle.flexShrink = 0.0f;
		context.SetStyle(toolbar, toolbarStyle);

		UI::Widgets::TextFieldDesc filterField;
		filterField.text = textFilter;
		filterField.font = font;
		filterField.fontSizePx = EditorTheme::GetMetric(MetricToken::FontBody);
		filterField.onCommit = [this](std::string_view text) {
			textFilter = str(text);
			_rowsDirty = true;
		};
		UI::ElementHandle filter = UI::Widgets::TextField(context, toolbar, filterField);
		UI::Style filterStyle = context.GetStyle(filter);
		filterStyle.width = UI::Length::Pixel(160.0f);
		filterStyle.margin = {0.0f, 0.0f, 8.0f, 0.0f};
		context.SetStyle(filter, filterStyle);

		UI::Widgets::ButtonDesc clearButton;
		clearButton.label = "Clear";
		clearButton.font = font;
		clearButton.fontSizePx = EditorTheme::GetMetric(MetricToken::FontBody);
		clearButton.onClick = [this]() {
			EditorConsole::Clear();
			_entries.clear();
			_expanded.clear();
			_rowsDirty = true;
		};
		UI::ElementHandle clear = UI::Widgets::Button(context, toolbar, clearButton);
		UI::Style clearStyle = context.GetStyle(clear);
		clearStyle.margin = {0.0f, 0.0f, 8.0f, 0.0f};
		context.SetStyle(clear, clearStyle);

		_autoscrollBox = UI::Widgets::Checkbox(context, toolbar, autoscroll, [this](bool value) {
			autoscroll = value;
			_tailHeldOff = !value;
			_pinnedScrollY = -1.0f;
			FollowTailThisFrame();
		});
		UI::ElementHandle autoLabel = context.CreateElement();
		context.AppendChild(toolbar, autoLabel);
		UI::Style autoLabelStyle;
		autoLabelStyle.margin = {2.0f, 0.0f, 0.0f, 0.0f};
		autoLabelStyle.flexShrink = 0.0f;
		context.SetStyle(autoLabel, autoLabelStyle);
		context.SetText(autoLabel, font, SmallFontPx(), EditorTheme::GetColor(ColorToken::TextDim), "autoscroll");

		UI::Widgets::Spacer(context, toolbar);

		_droppedLabel = context.CreateElement();
		context.AppendChild(toolbar, _droppedLabel);
		UI::Style droppedStyle;
		droppedStyle.flexShrink = 0.0f;
		droppedStyle.margin = {0.0f, 0.0f, 8.0f, 0.0f};
		context.SetStyle(_droppedLabel, droppedStyle);
		context.SetText(_droppedLabel, font, SmallFontPx(), EditorTheme::GetColor(ColorToken::TextDim), "");

		UI::ElementHandle switches = context.CreateElement();
		context.AppendChild(toolbar, switches);
		context.AddClass(switches, EditorTheme::Class::SwitchGroup);
		UI::Style switchesStyle;
		switchesStyle.flexDirection = UI::FlexDirection::Row;
		switchesStyle.alignItems = UI::Align::Center;
		switchesStyle.flexShrink = 0.0f;
		context.SetStyle(switches, switchesStyle);
		const char* names[KIND_COUNT] = {"Trace and Debug", "Messages", "Warnings", "Errors"};
		for (u32 index = 0; index < KIND_COUNT; ++index) {
			const LogKind kind = static_cast<LogKind>(index);
			KindSwitch& parts = _switches[index];
			parts = KindSwitch{};

			UI::Widgets::ButtonDesc desc;
			desc.label = "0";
			desc.font = font;
			desc.fontSizePx = SmallFontPx();
			desc.onClick = [this, kind]() { ToggleKind(kind); };
			parts.button = UI::Widgets::Button(context, switches, desc);
			context.AddClass(parts.button, EditorTheme::Class::ToolButton);
			context.SetTooltip(parts.button, names[index]);
			UI::Style buttonStyle = context.GetStyle(parts.button);
			buttonStyle.flexShrink = 0.0f;
			buttonStyle.margin = {index == 0 ? 0.0f : 2.0f, 0.0f, 0.0f, 0.0f};
			context.SetStyle(parts.button, buttonStyle);

			const UI::Element* pod = context.GetElement(parts.button);
			parts.count = pod ? context.MakeHandle(pod->firstChild) : UI::ElementHandle{};
			parts.icon = context.CreateElement();
			context.AppendChild(parts.button, parts.icon);
			context.SetHoverable(parts.icon, false);
			UI::Style iconStyle;
			iconStyle.flexShrink = 0.0f;
			iconStyle.margin = {0.0f, 0.0f, EditorTheme::GetMetric(MetricToken::IconGap), 0.0f};
			context.SetStyle(parts.icon, iconStyle);
			if (context.IsAlive(parts.count)) {
				context.AppendChild(parts.button, parts.count);
			}
		}

		UI::Widgets::ScrollViewParts scroll = UI::Widgets::ScrollView(context, root);
		UI::Style scrollStyle = context.GetStyle(scroll.root);
		scrollStyle.flexGrow = 1.0f;
		context.SetStyle(scroll.root, scrollStyle);
		_viewport = scroll.viewport;
		_scrollRoot = scroll.root;
		_emptyState = PanelEmptyState::Build(context, root);
		_pinnedScrollY = -1.0f;
		_lastScrollY = -1.0f;

		_window.Attach(context, _viewport,
					   [this](u32 index) -> UI::ElementHandle {
						   UI::Context* live = GetPanelContext();
						   return live ? BuildRowBlock(*live, index) : UI::ElementHandle{};
					   });
		_visibleTail = 0;
		DrainStream();
		RefreshModel(context, true);
		RefreshCounts(context);
		RefreshEmptyState(context);
	}

	UI::ElementHandle ConsolePanel::BuildRowBlock(UI::Context& context, u32 index) {
		const ConsoleEntry& entry = _entries[_visible[index]];
		Handle<FontAsset> font = UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{};
		const f32 tagWidth = TagWidth();
		const EditorIcons::LogLevelFace face = EditorIcons::FaceForLogLevel(entry.level);

		UI::ElementHandle block = context.CreateElement();
		context.AppendChild(_viewport, block);
		UI::Style blockStyle;
		blockStyle.flexDirection = UI::FlexDirection::Column;
		blockStyle.width = UI::Length::Percent(1.0f);
		context.SetStyle(block, blockStyle);

		UI::ElementHandle row = context.CreateElement();
		context.AppendChild(block, row);
		context.AddClass(row, EditorTheme::Class::LogRow);
		UI::Style rowStyle;
		rowStyle.flexDirection = UI::FlexDirection::Row;
		rowStyle.padding = {RowPaddingX(), 1.5f, RowPaddingX(), 1.5f};
		rowStyle.width = UI::Length::Percent(1.0f);
		context.SetStyle(row, rowStyle);
		u64 sequence = entry.sequence;
		context.Events(row).onClick = [this, sequence]() {
			SetTail(false);
			SetExpanded(sequence, !IsExpanded(sequence));
		};
		if (entry.clickable) {
			str originFile = entry.file;
			u32 originLine = entry.line;
			u32 originColumn = entry.column;
			GameModuleHost* moduleHost = GameModuleHost::GetActive();
			str editorCommand = moduleHost ? moduleHost->GetExternalEditorCommand() : str{};
			context.Events(row).onDoubleClick = [this, sequence, originFile, originLine, originColumn, editorCommand]() {
				SetExpanded(sequence, true);
				ScriptProjectOps::OpenInExternalEditor(ProjectOps::GetMountedProjectPath(), editorCommand,
													   originFile, originLine, originColumn);
			};
		}

		UI::ElementHandle tag = context.CreateElement();
		context.AppendChild(row, tag);
		context.SetHoverable(tag, false);
		UI::Style tagStyle;
		tagStyle.width = UI::Length::Pixel(tagWidth);
		tagStyle.flexShrink = 0.0f;
		context.SetStyle(tag, tagStyle);
		EditorIcons::SetGlyphOrText(context, tag, face.icon, face.tag, face.color, RowFontPx(), RowFontPx());

		UI::ElementHandle message = context.CreateElement();
		context.AppendChild(row, message);
		context.SetHoverable(message, false);
		UI::Style messageStyle;
		messageStyle.flexGrow = 1.0f;
		context.SetStyle(message, messageStyle);
		context.SetText(message, font, RowFontPx(), EditorTheme::GetColor(ColorToken::Text),
						"[" + entry.category + "] " + entry.message);

		ShowExpansion(context, block, entry);
		return block;
	}

	void ConsolePanel::ShowExpansion(UI::Context& context, UI::ElementHandle block, const ConsoleEntry& entry) {
		const UI::Element* blockPod = context.GetElement(block);
		if (!blockPod || blockPod->firstChild == UI::INVALID_UI_INDEX) {
			return;
		}
		const UI::Element* rowPod = context.GetElement(context.MakeHandle(blockPod->firstChild));
		const UI::ElementHandle message = rowPod ? context.MakeHandle(rowPod->lastChild) : UI::ElementHandle{};
		UI::ElementHandle source = blockPod->lastChild != blockPod->firstChild ? context.MakeHandle(blockPod->lastChild)
																			   : UI::ElementHandle{};
		const bool expanded = IsExpanded(entry.sequence);

		if (context.IsAlive(message)) {
			if (expanded) {
				context.RemoveClass(message, EditorTheme::Class::RowText);
			} else {
				context.AddClass(message, EditorTheme::Class::RowText);
			}
		}

		if (!expanded || entry.file.empty()) {
			if (context.IsAlive(source)) {
				context.DestroyElement(source);
			}
			return;
		}
		if (context.IsAlive(source)) {
			return;
		}
		Handle<FontAsset> font = UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{};
		source = context.CreateElement();
		context.AppendChild(block, source);
		UI::Style sourceStyle;
		sourceStyle.padding = {RowPaddingX() + TagWidth() + 6.0f, 0.0f, RowPaddingX(), 3.0f};
		context.SetStyle(source, sourceStyle);
		str sourceText = !entry.clickable ? std::format("{}:{} ({})", entry.file, entry.line, entry.function)
						 : entry.column != 0
							 ? std::format("{}:{}:{}   (double-click to open)", entry.file, entry.line, entry.column)
							 : std::format("{}:{}   (double-click to open)", entry.file, entry.line);
		context.SetText(source, font, SmallFontPx(), EditorTheme::GetColor(ColorToken::LogDebug), sourceText);
	}

	bool ConsolePanel::IsExpanded(u64 sequence) const {
		return std::find(_expanded.begin(), _expanded.end(), sequence) != _expanded.end();
	}

	void ConsolePanel::SetExpanded(u64 sequence, bool expanded) {
		if (IsExpanded(sequence) == expanded) {
			return;
		}
		if (expanded) {
			_expanded.push_back(sequence);
		} else {
			_expanded.erase(std::find(_expanded.begin(), _expanded.end(), sequence));
		}
		UI::Context* context = GetPanelContext();
		u32 index = 0;
		if (!context || !context->IsAlive(_viewport) || !FindVisible(sequence, index)) {
			return;
		}
		UI::ElementHandle block = _window.GetElement(index);
		if (context->IsAlive(block)) {
			ShowExpansion(*context, block, _entries[_visible[index]]);
		}
	}

	bool ConsolePanel::FindVisible(u64 sequence, u32& index) const {
		if (_visible.empty() || _visible.back() >= _entries.size()) {
			return false;
		}
		auto found = std::lower_bound(_visible.begin(), _visible.end(), sequence,
									  [this](u32 entry, u64 wanted) { return _entries[entry].sequence < wanted; });
		if (found == _visible.end() || _entries[*found].sequence != sequence) {
			return false;
		}
		index = static_cast<u32>(found - _visible.begin());
		return true;
	}

	void ConsolePanel::FollowTailThisFrame() {
		if (autoscroll && _window.GetCount() > 0) {
			_window.ScrollIntoView(_window.GetCount() - 1);
		}
	}

	void ConsolePanel::SetTail(bool on) {
		if (autoscroll == on) {
			return;
		}
		autoscroll = on;
		_tailHeldOff = false;
		if (UI::Context* context = GetPanelContext()) {
			UI::Widgets::SetCheckboxValue(*context, _autoscrollBox, on);
		}
		_pinnedScrollY = -1.0f;
	}

	void ConsolePanel::RefreshModel(UI::Context& context, bool structural) {
		if (!context.IsAlive(_viewport)) {
			return;
		}
		const size_t previousCount = _visible.size();
		const u64 previousTail = _visibleTail;

		_visible.clear();
		for (u32& count : _counts) {
			count = 0;
		}
		for (u32 index = 0; index < _entries.size(); ++index) {
			++_counts[static_cast<u32>(KindOf(_entries[index].level))];
			if (PassesFilter(_entries[index])) {
				_visible.push_back(index);
			}
		}
		if (_visible.size() > ROW_CAP) {
			_visible.erase(_visible.begin(), _visible.begin() + static_cast<ptrdiff_t>(_visible.size() - ROW_CAP));
		}
		_visibleTail = _visible.empty() ? 0 : _entries[_visible.back()].sequence;
		_realizedThrough = _entries.empty() ? 0 : _entries.back().sequence;

		if (structural) {
			ResetWindow();
			return;
		}

		u32 appended = 0;
		for (size_t i = _visible.size(); i > 0; --i) {
			if (_entries[_visible[i - 1]].sequence <= previousTail) {
				break;
			}
			++appended;
		}
		const i64 dropped = static_cast<i64>(previousCount) + appended - static_cast<i64>(_visible.size());
		if (dropped < 0) {
			ResetWindow();
			return;
		}
		_window.Appended(static_cast<u32>(dropped), appended);
	}

	void ConsolePanel::ResetWindow() {
		_window.Reset(static_cast<u32>(_visible.size()));
		FollowTailThisFrame();
	}

	void ConsolePanel::RefreshCounts(UI::Context& context) {
		if (context.IsAlive(_droppedLabel)) {
			u64 dropped = EditorConsole::GetDroppedCount();
			str text = dropped > 0 ? std::format("{} dropped", dropped) : str();
			if (context.GetTextString(_droppedLabel) != text) {
				context.SetTextString(_droppedLabel, text);
			}
		}

		const LogLevel faces[KIND_COUNT] = {LogLevel::Trace, LogLevel::Info, LogLevel::Warn, LogLevel::Error};
		for (u32 index = 0; index < KIND_COUNT; ++index) {
			KindSwitch& parts = _switches[index];
			if (!context.IsAlive(parts.button)) {
				continue;
			}
			const u32 count = _counts[index];
			const bool shown = IsShown(static_cast<LogKind>(index));
			if (count == parts.drawnCount && shown == parts.drawnShown) {
				continue;
			}
			if (shown) {
				context.AddClass(parts.button, EditorTheme::Class::ToolButtonOn);
			} else {
				context.RemoveClass(parts.button, EditorTheme::Class::ToolButtonOn);
			}
			const bool wasEmpty = parts.drawnCount == 0;
			if (parts.drawnCount == 0xFFFFFFFFu || wasEmpty != (count == 0)) {
				const EditorIcons::LogLevelFace face = EditorIcons::FaceForLogLevel(faces[index]);
				EditorIcons::SetGlyphOrText(context, parts.icon, face.icon, face.tag,
											count > 0 ? face.color : EditorTheme::GetColor(ColorToken::TextFaint),
											RowFontPx(), SmallFontPx());
			}
			if (context.IsAlive(parts.count)) {
				Handle<FontAsset> font = UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{};
				context.SetText(parts.count, font, SmallFontPx(),
								EditorTheme::GetColor(shown ? ColorToken::Text : ColorToken::TextFaint), CountText(count));
			}
			parts.drawnCount = count;
			parts.drawnShown = shown;
		}
	}

	void ConsolePanel::RefreshEmptyState(UI::Context& context) {
		if (!_visible.empty()) {
			PanelEmptyState::Hide(context, _emptyState, _scrollRoot);
			return;
		}
		if (_entries.empty()) {
			PanelEmptyState::Show(context, _emptyState, _scrollRoot, {EditorIcons::Glyph::PanelConsole, "No logs", {}});
		} else if (!textFilter.empty()) {
			const str title = std::format("No results for \"{}\"", textFilter);
			PanelEmptyState::Show(context, _emptyState, _scrollRoot, {EditorIcons::Glyph::Search, title, {}});
		} else {
			PanelEmptyState::Show(context, _emptyState, _scrollRoot,
								  {EditorIcons::Glyph::EyeSlash, "All logs are hidden",
								   "Turn on Trace, Messages, Warnings or Errors in the toolbar."});
		}
	}

	void ConsolePanel::FollowUserScroll(UI::Context& context) {
		const UI::Element* viewport = context.GetElement(_viewport);
		if (!viewport) {
			return;
		}
		const f32 offset = viewport->scrollOffset.y;
		const f32 end = viewport->maxScroll.y;
		if (!autoscroll) {
			if (_lastScrollY < 0.0f || offset < end) {
				return;
			}
			const bool movedDown = offset > _lastScrollY;
			const bool arrived = !_wasAtEnd;
			if (movedDown || (arrived && !_tailHeldOff)) {
				SetTail(true);
			}
			return;
		}
		if (_pinnedScrollY < 0.0f || offset >= end || offset >= _pinnedScrollY) {
			return;
		}
		SetTail(false);
	}

	void ConsolePanel::PinTail(UI::Context& context) {
		if (!autoscroll || !context.IsAlive(_viewport)) {
			_pinnedScrollY = -1.0f;
			return;
		}
		context.SetScrollOffset(_viewport, {0.0f, 1.0e9f});
		if (const UI::Element* viewport = context.GetElement(_viewport)) {
			_pinnedScrollY = viewport->scrollOffset.y;
		}
	}

	bool ConsolePanel::DrainStream() {
		Vector<ConsoleEntry> fresh{GetMemoryResource(MemTag::Editor)};
		_lastSequence = EditorConsole::ReadSince(_lastSequence, fresh);
		if (fresh.empty()) {
			return false;
		}
		for (ConsoleEntry& entry : fresh) {
			_entries.push_back(std::move(entry));
		}
		if (_entries.size() > ENTRY_CAP) {
			_entries.erase(_entries.begin(), _entries.begin() + static_cast<ptrdiff_t>(_entries.size() - ENTRY_CAP));
			const u64 oldest = _entries.front().sequence;
			std::erase_if(_expanded, [oldest](u64 sequence) { return sequence < oldest; });
		}
		return true;
	}

	void ConsolePanel::OnTick(f32) {
		const bool appended = DrainStream();

		UI::Context* context = GetPanelContext();
		if (!context) {
			return;
		}
		if (!context->IsAlive(_viewport)) {
			return;
		}

		FollowUserScroll(*context);

		if (_rowsDirty || appended) {
			const bool structural = _rowsDirty;
			_rowsDirty = false;
			RefreshModel(*context, structural);
			RefreshCounts(*context);
			RefreshEmptyState(*context);
		}

		EditorConsole::MarkSeenThrough(_realizedThrough);

		PinTail(*context);
		if (const UI::Element* viewport = context->GetElement(_viewport)) {
			_lastScrollY = viewport->scrollOffset.y;
			_wasAtEnd = viewport->scrollOffset.y >= viewport->maxScroll.y;
		}
	}

	EditorIcons::Icon ConsolePanel::GetTabIcon() const {
		return EditorIcons::Glyph::PanelConsole;
	}

} // namespace Shalltear
