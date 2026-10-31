#include "Shalltear/Editor/GraphBlackboard.h"

#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorDragData.h"
#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorTheme.h"

#include <algorithm>

namespace Shalltear {

	using EditorTheme::ColorToken;
	using EditorTheme::MetricToken;

	namespace {

		constexpr StringHash PAYLOAD = "GraphBlackboardItem"_hash;

		u64 g_nextBoard = 0;

		bool PayloadItem(const UI::DragPayload& payload, u64 board, u64& outItem) {
			if (payload.type != PAYLOAD || !payload.data.Is<GraphBlackboardDragData>()) {
				return false;
			}
			const GraphBlackboardDragData& data = payload.data.As<GraphBlackboardDragData>();
			outItem = data.item;
			return data.board == board;
		}

	} // anonymous namespace

	void GraphBlackboard::Build(EditorPanel* owner, UI::Context& context, UI::ElementHandle parent, GraphBlackboardDesc desc) {
		if (_boardId == 0) {
			_boardId = ++g_nextBoard;
		}
		_desc = std::move(desc);
		_context = &context;
		_renaming = 0;
		_dirty = false;

		UI::ElementHandle bar = context.CreateElement();
		context.AppendChild(parent, bar);
		UI::Style barStyle;
		barStyle.flexDirection = UI::FlexDirection::Row;
		barStyle.alignItems = UI::Align::Center;
		context.SetStyle(bar, barStyle);

		UI::ElementHandle title = context.CreateElement();
		context.AppendChild(bar, title);
		context.SetText(title, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontBody),
						EditorTheme::GetColor(ColorToken::TextStrong), _desc.title);
		UI::Style titleStyle;
		titleStyle.flexGrow = 1.0f;
		context.SetStyle(title, titleStyle);

		if (_desc.onAdd && !_desc.kinds.empty()) {
			EditorIcons::IconButton(context, bar, EditorIcons::Glyph::Add, "+", "Add", [this]() {
				if (_desc.kinds.size() == 1) {
					Arrive(_desc.onAdd(0), true);
				} else if (_context) {
					OpenAddMenu(*_context);
				}
			});
		}

		if (_desc.onBuildSubtitle) {
			UI::ElementHandle subtitle = context.CreateElement();
			context.AppendChild(parent, subtitle);
			UI::Style subtitleStyle;
			subtitleStyle.flexDirection = UI::FlexDirection::Row;
			subtitleStyle.width = UI::Length::Percent(1.0f);
			context.SetStyle(subtitle, subtitleStyle);
			_desc.onBuildSubtitle(context, subtitle);
		}

		_top.BeginBuild(context);
		if (_desc.onMove) {
			_top.PendStrip(BandBefore(0));
			_top.DrainInto(context, parent);
		}

		ListViewDesc list;
		if (_desc.onDelete) {
			list.onDelete = [this]() {
				Vector<u64> ids{_list.GetSelected().begin(), _list.GetSelected().end(), GetMemoryResource(MemTag::Editor)};
				if (!ids.empty()) {
					_desc.onDelete(ids);
				}
			};
		}
		if (_desc.onRename) {
			list.onRename = [this](u64 row) { return BeginRename(row); };
		}
		list.onBuildTrailing = [this](UI::Context& context, UI::ElementHandle host) { _gaps.DrainInto(context, host); };
		if (_desc.onSelectionChanged) {
			list.onSelectionChanged = [this]() { _desc.onSelectionChanged(_list.GetSelected()); };
		}
		_list.Build(owner, context, parent, std::move(list));
		Rebuild();
	}

	void GraphBlackboard::SetItems(std::span<const GraphBlackboardItem> items) {
		_items.assign(items.begin(), items.end());
		if (_renaming != 0 && IndexOf(_renaming) < 0) {
			_renaming = 0;
		}
		Rebuild();
	}

	bool GraphBlackboard::BeginRename(u64 id) {
		if (!_desc.onRename || IndexOf(id) < 0) {
			return false;
		}
		_renaming = id;
		_renameNeedsFocus = true;
		_dirty = true;
		return true;
	}

	bool GraphBlackboard::HasFocus() const {
		return _list.HasFocus();
	}

	bool GraphBlackboard::OwnsPayload(const UI::DragPayload& payload, u64& item) const {
		return _boardId != 0 && PayloadItem(payload, _boardId, item);
	}

	void GraphBlackboard::Tick(f32 deltaSeconds) {
		if (_dirty) {
			Rebuild();
		}
		if (_context) {
			_top.SyncHits(*_context);
			_gaps.SyncHits(*_context);
		}
		_list.Tick(deltaSeconds);
	}

	void GraphBlackboard::Rebuild() {
		_dirty = false;
		if (!_context) {
			return;
		}
		u64 arrived = 0;
		if (_arriving != 0 && IndexOf(_arriving) >= 0) {
			arrived = _arriving;
			if (_arrivingRenames && _desc.onRename) {
				_renaming = arrived;
				_renameNeedsFocus = true;
			}
			_arriving = 0;
		}

		UI::Context& context = *_context;
		_gaps.BeginBuild(context);
		Vector<ListRow> rows{GetMemoryResource(MemTag::Editor)};
		rows.reserve(_items.size());
		for (u32 index = 0; index < static_cast<u32>(_items.size()); ++index) {
			const GraphBlackboardItem& item = _items[index];
			const u64 id = item.id;
			if (index > 0 && _desc.onMove) {
				_gaps.PendStrip(BandBefore(index));
			}

			if (id == _renaming) {
				_gaps.PendInFlow([this, id, name = item.name](UI::Context& context, UI::ElementHandle host) {
					UI::Widgets::TextFieldDesc field;
					field.text = name;
					field.font = UISystem::GetDefaultFont();
					field.fontSizePx = EditorTheme::GetMetric(MetricToken::FontRow);
					field.onCommit = [this, id](std::string_view text) { CommitRename(id, text); };
					const UI::ElementHandle element = UI::Widgets::TextField(context, host, field);
					if (_renameNeedsFocus) {
						_renameNeedsFocus = false;
						context.SetFocus(element);
					}
					auto chained = context.Events(element).onFocusLost;
					context.Events(element).onFocusLost = [this, id, chained]() {
						if (chained) {
							chained();
						}
						if (_renaming == id) {
							_renaming = 0;
							_refocus = true;
							_dirty = true;
						}
					};
				});
				continue;
			}

			ListRow row;
			row.id = id;
			row.label = item.name;
			row.onBuildLeading = _gaps.Take();
			row.onActivate = [this, id](UI::Context&) { BeginRename(id); };
			row.onRightClick = [this, id](UI::Context& context) { OpenRowMenu(context, id); };
			if (_desc.onMove) {
				row.onDragRow = [this, id, name = item.name](Vec2) {
					if (UISystem::IsDragPayloadActive()) {
						return;
					}
					UI::DragPayload payload;
					payload.type = PAYLOAD;
					payload.data = Variant(GraphBlackboardDragData{_boardId, id});
					payload.display = name;
					UISystem::BeginDragPayload(payload);
				};
			}
			row.onDecorate = [this, index](UI::Context& context, const UI::Widgets::TreeRowParts& parts) {
				UI::Style labelStyle = context.GetStyle(parts.labelElement);
				labelStyle.flexGrow = 1.0f;
				labelStyle.flexShrink = 1.0f;
				labelStyle.textOverflow = UI::TextOverflow::Ellipsis;
				context.SetStyle(parts.labelElement, labelStyle);
				if (index < _items.size() && _items[index].onBuildControls) {
					_items[index].onBuildControls(context, parts.row);
				}
			};
			rows.push_back(std::move(row));
		}
		if (!_items.empty() && _desc.onMove) {
			_gaps.PendStrip(BandBefore(static_cast<u32>(_items.size())));
		}
		_list.SetRows(rows);
		if (arrived != 0) {
			_list.SetSelection(std::span<const u64>(&arrived, 1));
			if (_desc.onSelectionChanged) {
				_desc.onSelectionChanged(_list.GetSelected());
			}
		}
		if (_refocus && _renaming == 0) {
			_refocus = false;
			if (!context.IsAlive(context.GetFocused())) {
				_list.TakeFocus();
			}
		}
	}

	void GraphBlackboard::OpenAddMenu(UI::Context& context) {
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
		for (u32 kind = 0; kind < static_cast<u32>(_desc.kinds.size()); ++kind) {
			UI::Widgets::MenuItem item;
			item.label = _desc.kinds[kind];
			item.onSelect = [this, kind]() {
				if (_desc.onAdd) {
					Arrive(_desc.onAdd(kind), true);
				}
			};
			items.push_back(std::move(item));
		}
		UI::Widgets::OpenMenu(context, context.GetPointerPosition(), items, UISystem::GetDefaultFont(),
							  EditorTheme::GetMetric(MetricToken::FontSmall));
	}

	void GraphBlackboard::OpenRowMenu(UI::Context& context, u64 id) {
		Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
		if (_desc.onRename) {
			UI::Widgets::MenuItem rename;
			rename.label = "Rename";
			rename.shortcut = "F2";
			rename.onSelect = [this, id]() { BeginRename(id); };
			items.push_back(std::move(rename));
		}
		if (_desc.onDelete) {
			UI::Widgets::MenuItem remove;
			remove.label = "Delete";
			remove.shortcut = "Del";
			remove.onSelect = [this, id]() {
				std::span<const u64> selected = _list.GetSelected();
				Vector<u64> ids{GetMemoryResource(MemTag::Editor)};
				if (std::find(selected.begin(), selected.end(), id) != selected.end()) {
					ids.assign(selected.begin(), selected.end());
				} else {
					ids.push_back(id);
				}
				_desc.onDelete(ids);
			};
			items.push_back(std::move(remove));
		}
		if (items.empty()) {
			return;
		}
		UI::Widgets::OpenMenu(context, context.GetPointerPosition(), items, UISystem::GetDefaultFont(),
							  EditorTheme::GetMetric(MetricToken::FontSmall));
	}

	EditorInsertStripDesc GraphBlackboard::BandBefore(u32 before) {
		EditorInsertStripDesc desc;
		desc.onDragOver = [this, before](const UI::DragPayload& payload) { return MovesAnything(payload, before); };
		desc.onDrop = [this, before](const UI::DragPayload& payload, Vec2) { Drop(payload, before); };
		return desc;
	}

	bool GraphBlackboard::MovesAnything(const UI::DragPayload& payload, u32 before) const {
		u64 item = 0;
		if (!PayloadItem(payload, _boardId, item)) {
			return false;
		}
		const i32 from = IndexOf(item);
		return from >= 0 && before != static_cast<u32>(from) && before != static_cast<u32>(from) + 1;
	}

	void GraphBlackboard::Drop(const UI::DragPayload& payload, u32 before) {
		if (!MovesAnything(payload, before) || !_desc.onMove) {
			return;
		}
		u64 item = 0;
		PayloadItem(payload, _boardId, item);
		const u32 from = static_cast<u32>(IndexOf(item));
		const u32 index = before > from ? before - 1 : before;
		Arrive(_desc.onMove(item, index), false);
	}

	void GraphBlackboard::CommitRename(u64 id, std::string_view name) {
		if (_renaming != id) {
			return;
		}
		_renaming = 0;
		_refocus = true;
		_dirty = true;
		const i32 index = IndexOf(id);
		if (index < 0) {
			return;
		}
		u64 after = id;
		if (!name.empty() && name != _items[static_cast<size_t>(index)].name) {
			if (const u64 renamed = _desc.onRename(id, name)) {
				after = renamed;
			}
		}
		Arrive(after, false);
	}

	void GraphBlackboard::Arrive(u64 id, bool rename) {
		if (id == 0) {
			return;
		}
		_arriving = id;
		_arrivingRenames = rename;
		_dirty = true;
	}

	i32 GraphBlackboard::IndexOf(u64 id) const {
		for (size_t index = 0; index < _items.size(); ++index) {
			if (_items[index].id == id) {
				return static_cast<i32>(index);
			}
		}
		return -1;
	}

} // namespace Shalltear
