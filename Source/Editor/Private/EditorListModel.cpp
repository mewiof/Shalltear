#include "Shalltear/Editor/EditorListModel.h"

#include "Shalltear/Editor/EditorSelectionOps.h"

#include <algorithm>

namespace Shalltear {

	ListSelection::ListSelection(ListSelectionMode mode)
		: _mode(mode),
		  _rows(GetMemoryResource(MemTag::Editor)),
		  _selected(GetMemoryResource(MemTag::Editor)),
		  _selectedSet(GetMemoryResource(MemTag::Editor)) {}

	u32 ListSelection::IndexOfRow(u64 row) const {
		if (row == 0) {
			return MISSING;
		}
		for (u32 i = 0; i < _rows.size(); ++i) {
			if (_rows[i] == row) {
				return i;
			}
		}
		return MISSING;
	}

	bool ListSelection::SetSelectionTo(std::span<const u64> ids) {
		if (_selected.size() == ids.size() && std::equal(_selected.begin(), _selected.end(), ids.begin())) {
			return false;
		}
		_selected.assign(ids.begin(), ids.end());
		_selectedSet.clear();
		for (u64 id : _selected) {
			_selectedSet.insert(id);
		}
		return true;
	}

	void ListSelection::SetRows(std::span<const u64> orderedSelectableRows) {
		Vector<u64> previous{GetMemoryResource(MemTag::Editor)};
		previous.swap(_rows);
		const u32 cursorWas = [&]() {
			for (u32 i = 0; i < previous.size(); ++i) {
				if (previous[i] == _cursor) {
					return i;
				}
			}
			return MISSING;
		}();

		_rows.clear();
		_rows.reserve(orderedSelectableRows.size());
		for (u64 id : orderedSelectableRows) {
			if (id != 0) {
				_rows.push_back(id);
			}
		}

		HashSet<u64> present{GetMemoryResource(MemTag::Editor)};
		present.reserve(_rows.size());
		for (u64 id : _rows) {
			present.insert(id);
		}

		size_t kept = 0;
		for (u64 id : _selected) {
			if (present.contains(id)) {
				_selected[kept++] = id;
			}
		}
		if (kept != _selected.size()) {
			_selected.resize(kept);
			_selectedSet.clear();
			for (u64 id : _selected) {
				_selectedSet.insert(id);
			}
		}

		if (!present.contains(_anchor)) {
			_anchor = 0;
		}
		if (!present.contains(_cursor)) {
			_cursor = 0;
			if (cursorWas != MISSING) {
				for (u32 i = cursorWas + 1; i < previous.size() && _cursor == 0; ++i) {
					if (present.contains(previous[i])) {
						_cursor = previous[i];
					}
				}
				for (u32 i = cursorWas; i-- > 0 && _cursor == 0;) {
					if (present.contains(previous[i])) {
						_cursor = previous[i];
					}
				}
			}
		}
	}

	bool ListSelection::ExtendTo(u64 row) {
		u32 first = 0;
		u32 last = 0;
		if (_anchor == 0 || !EditorSelectionOps::ResolveRange(_rows, _anchor, row, first, last)) {
			return SelectOnly(row);
		}

		Vector<u64> run{GetMemoryResource(MemTag::Editor)};
		run.reserve(static_cast<size_t>(last - first) + 1);
		for (u32 i = first; i <= last; ++i) {
			if (_rows[i] != row) {
				run.push_back(_rows[i]);
			}
		}
		run.push_back(row);

		bool changed = SetSelectionTo(run);
		changed |= _cursor != row;
		_cursor = row;
		return changed;
	}

	bool ListSelection::ApplyClick(u64 row, KeyMod mods) {
		if (IndexOfRow(row) == MISSING) {
			return false;
		}
		if (_mode == ListSelectionMode::Single) {
			return SelectOnly(row);
		}

		const bool ctrl = (mods & KeyMod::Ctrl) != KeyMod::None;
		const bool shift = (mods & KeyMod::Shift) != KeyMod::None;

		if (ctrl) {
			Vector<u64> next{GetMemoryResource(MemTag::Editor)};
			next.reserve(_selected.size() + 1);
			for (u64 id : _selected) {
				if (id != row) {
					next.push_back(id);
				}
			}
			if (!_selectedSet.contains(row)) {
				next.push_back(row);
			}
			bool changed = SetSelectionTo(next);
			changed |= _cursor != row;
			_anchor = row;
			_cursor = row;
			return changed;
		}

		if (shift) {
			return ExtendTo(row);
		}

		return SelectOnly(row);
	}

	bool ListSelection::SelectOnly(u64 row) {
		if (IndexOfRow(row) == MISSING) {
			return false;
		}
		const u64 one[] = {row};
		bool changed = SetSelectionTo(one);
		changed |= _cursor != row;
		_anchor = row;
		_cursor = row;
		return changed;
	}

	bool ListSelection::SelectAll() {
		if (_mode == ListSelectionMode::Single || _rows.empty()) {
			return false;
		}

		const u64 active = GetActive();
		const bool keepActive = IndexOfRow(active) != MISSING;
		Vector<u64> all{GetMemoryResource(MemTag::Editor)};
		all.reserve(_rows.size());
		for (u64 id : _rows) {
			if (!keepActive || id != active) {
				all.push_back(id);
			}
		}
		if (keepActive) {
			all.push_back(active);
		}

		bool changed = SetSelectionTo(all);
		if (_cursor == 0) {
			_cursor = GetActive();
			changed = true;
		}
		return changed;
	}

	bool ListSelection::Clear() {
		if (_selected.empty()) {
			return false;
		}
		_selected.clear();
		_selectedSet.clear();
		_anchor = 0;
		_cursor = 0;
		return true;
	}

	bool ListSelection::SetSelection(std::span<const u64> ids) {
		HashSet<u64> present{GetMemoryResource(MemTag::Editor)};
		present.reserve(_rows.size());
		for (u64 id : _rows) {
			present.insert(id);
		}

		Vector<u64> kept{GetMemoryResource(MemTag::Editor)};
		kept.reserve(ids.size());
		HashSet<u64> seen{GetMemoryResource(MemTag::Editor)};
		for (u64 id : ids) {
			if (present.contains(id) && seen.insert(id).second) {
				kept.push_back(id);
			}
		}
		if (_mode == ListSelectionMode::Single && kept.size() > 1) {
			const u64 active = kept.back();
			kept.clear();
			kept.push_back(active);
		}

		if (!SetSelectionTo(kept)) {
			return false;
		}
		_anchor = GetActive();
		_cursor = GetActive();
		return true;
	}

	bool ListSelection::MoveCursor(i32 delta, KeyMod mods) {
		if (_rows.empty() || delta == 0) {
			return false;
		}

		const i64 count = static_cast<i64>(_rows.size());
		const u32 cursorIndex = IndexOfRow(_cursor);
		i64 from = cursorIndex == MISSING ? -1 : static_cast<i64>(cursorIndex);
		i64 target = std::clamp<i64>(from + delta, 0, count - 1);
		const u64 row = _rows[static_cast<size_t>(target)];

		if (_mode == ListSelectionMode::Single) {
			return SelectOnly(row);
		}

		if ((mods & (KeyMod::Ctrl | KeyMod::Shift)) != KeyMod::None) {
			if (_anchor == 0) {
				_anchor = cursorIndex == MISSING ? row : _cursor;
			}
			return ExtendTo(row);
		}

		return SelectOnly(row);
	}

	ListNavigation TranslateKey(Key key, KeyMod mods) {
		if ((mods & (KeyMod::Alt | KeyMod::Super)) != KeyMod::None) {
			return ListNavigation::None;
		}

		switch (key) {
		case Key::Up:
			return ListNavigation::Prev;
		case Key::Down:
			return ListNavigation::Next;
		case Key::Home:
			return ListNavigation::First;
		case Key::End:
			return ListNavigation::Last;
		case Key::PageUp:
			return ListNavigation::PageUp;
		case Key::PageDown:
			return ListNavigation::PageDown;
		case Key::Right:
			return ListNavigation::Expand;
		case Key::Left:
			return ListNavigation::Collapse;
		case Key::Enter:
		case Key::KpEnter:
			return ListNavigation::Activate;
		default:
			return ListNavigation::None;
		}
	}

} // namespace Shalltear
