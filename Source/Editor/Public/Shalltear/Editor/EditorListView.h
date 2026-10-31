#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Input/InputCodes.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorAPI.h"
#include "Shalltear/Editor/EditorListModel.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/EditorVirtualRows.h"

#include <functional>
#include <span>

namespace Shalltear {

	enum class ListLayout : u8 {
		Rows,
		Grid
	};

	struct ListViewDesc {
		ListLayout layout = ListLayout::Rows;
		ListSelectionMode mode = ListSelectionMode::Multiple;

		f32 cellWidthPx = 72.0f;
		f32 cellHeightPx = 88.0f;
		f32 cellPaddingPx = 0.0f;

		std::function<void()> onSelectionChanged;

		std::function<void()> onDelete;
		std::function<void()> onDuplicate;
		std::function<bool(u64 row)> onRename;

		std::function<void(UI::Context&, u64, UI::ElementHandle well)> onBuildCell;
		std::function<void(UI::Context&, u64, UI::ElementHandle label)> onDecorateCellLabel;

		std::function<void()> onEmptyRightClick;
		std::function<void()> onEmptyClick;

		std::function<void(UI::Context&, UI::ElementHandle host)> onBuildTrailing;
	};

	struct ListRow {
		u64 id = 0;
		bool selectable = true;
		u32 depth = 0;
		bool hasChildren = false;
		bool expanded = false;
		str label;

		std::function<void(UI::Context&, const UI::Widgets::TreeRowParts&)> onDecorate;
		std::function<void(bool expanded)> onToggleExpand;
		std::function<void(UI::Context&)> onActivate;
		std::function<void(UI::Context&)> onClick;
		std::function<void(UI::Context&)> onRightClick;
		std::function<void(Vec2)> onDragRow;
		std::function<bool(const UI::DragPayload&)> onDragOver;
		std::function<void(const UI::DragPayload&, Vec2)> onDrop;
		std::function<void(UI::Context&, UI::ElementHandle host)> onBuildLeading;
	};

	class SHALLTEAR_EDITOR_API EditorListView {
	public:
		EditorListView() = default;
		~EditorListView();

		EditorListView(const EditorListView&) = delete;
		EditorListView& operator=(const EditorListView&) = delete;

		void Build(EditorPanel* owner, UI::Context& context, UI::ElementHandle parent, ListViewDesc desc);

		void SetRows(std::span<const ListRow> rows);

		void SetLayout(ListLayout layout, f32 cellWidthPx, f32 cellHeightPx, f32 cellPaddingPx);

		void SetSelection(std::span<const u64> ids);

		void RevealRow(u64 id);

		std::span<const u64> GetSelected() const;
		bool SelectAll();

		bool HasFocus() const;

		// use EditorPanel::FocusList, never directly
		void TakeFocus();

		void GetVisibleRows(Vector<u64>& out) const;

		struct FoldOutBand {
			UI::Rect rect;
			bool fromAbove = false;
			bool continuesBelow = false;
		};
		void GetFoldOutBands(Vector<FoldOutBand>& out) const;

		void PingRow(u64 id);

		void Tick(f32 deltaSeconds);

		bool Command(PanelCommand command);
	private:
		static constexpr u32 MAX_GUIDE_LEVELS = 32;

		UI::Context* Live() const;
		void InContext(const std::function<void(UI::Context&)>& callback) const;

		void ComputeGuideMasks();

		UI::ElementHandle BuildOneRow(UI::Context& context, u32 index);
		void AttachBandConsumer();
		void BuildGrid(UI::Context& context, std::span<const ListRow> rows);
		void BuildFoldButton(UI::Context& context, UI::ElementHandle flow, u32 column, const ListRow& row);
		void PaintFoldOutBands(UI::Context& context, UI::PaintScope& painter) const;
		void ReflowGrid();
		UI::ElementHandle ElementForRow(u32 index) const;
		u32 IndexOfRow(u64 id) const;
		void ScrollRowIntoView(u64 id);
		const ListRow* FindModelRow(u64 id) const;
		void TrackPress(UI::Context& context, UI::ElementHandle element);

		void Click(u64 row, KeyMod mods);
		void ApplyStyles();
		static void SetClass(UI::Context& context, UI::ElementHandle element, StringHash klass, bool on);
		void AfterMutation(bool changed, bool scroll);
		bool OnKey(Key key, KeyMod mods);
		i32 PageStep() const;

		UI::Context* _context = nullptr;
		EditorPanel* _owner = nullptr;
		UI::ElementHandle _root;
		UI::ElementHandle _viewport;
		ListViewDesc _desc;
		ListSelection _selection;
		Vector<ListRow> _rows{GetMemoryResource(MemTag::Editor)};
		EditorVirtualRows _virtual;
		Vector<UI::ElementHandle> _cells{GetMemoryResource(MemTag::Editor)};
		struct CellCue {
			UI::ElementHandle well;
			UI::ElementHandle label;
		};
		Vector<CellCue> _cellCues{GetMemoryResource(MemTag::Editor)};
		Vector<u32> _guideMasks{GetMemoryResource(MemTag::Editor)};
		Vector<u64> _published{GetMemoryResource(MemTag::Editor)};
		u32 _gridColumns = 0;
		// list must never rebuild under a press
		bool _pressed = false;
		bool _reflowPending = false;

		u64 _pingRow = 0;
		f32 _pingSecondsLeft = 0.0f;
		UI::ElementHandle _pingElement;
		UI::ElementHandle _pingCue;
		bool _pingRevealPending = false;
		bool _pingRevealWaits = false;
	};

} // namespace Shalltear
