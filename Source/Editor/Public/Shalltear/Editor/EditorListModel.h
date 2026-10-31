#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Input/InputCodes.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <span>

namespace Shalltear {

	enum class ListSelectionMode : u8 {
		Single,
		Multiple
	};

	class SHALLTEAR_EDITOR_API ListSelection {
	public:
		explicit ListSelection(ListSelectionMode mode = ListSelectionMode::Multiple);

		void SetRows(std::span<const u64> orderedSelectableRows);

		bool ApplyClick(u64 row, KeyMod mods);

		bool SelectOnly(u64 row);
		bool SelectAll();
		bool Clear();

		bool SetSelection(std::span<const u64> ids);

		bool MoveCursor(i32 delta, KeyMod mods);

		std::span<const u64> GetSelected() const { return _selected; }
		u64 GetActive() const { return _selected.empty() ? 0 : _selected.back(); }
		u64 GetCursor() const { return _cursor; }
		bool IsSelected(u64 row) const { return _selectedSet.contains(row); }
		u32 GetCount() const { return static_cast<u32>(_selected.size()); }
	private:
		static constexpr u32 MISSING = ~0u;

		u32 IndexOfRow(u64 row) const;

		bool SetSelectionTo(std::span<const u64> ids);

		bool ExtendTo(u64 row);

		ListSelectionMode _mode = ListSelectionMode::Multiple;

		Vector<u64> _rows;
		Vector<u64> _selected;
		HashSet<u64> _selectedSet;

		u64 _anchor = 0;
		u64 _cursor = 0;
	};

	enum class ListNavigation : u8 {
		None,
		Prev,
		Next,
		First,
		Last,
		PageUp,
		PageDown,
		Expand,
		Collapse,
		Activate
	};

	SHALLTEAR_EDITOR_API ListNavigation TranslateKey(Key key, KeyMod mods);

} // namespace Shalltear
