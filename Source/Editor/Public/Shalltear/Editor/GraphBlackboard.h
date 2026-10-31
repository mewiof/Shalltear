#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"

#include "Shalltear/Editor/EditorAPI.h"
#include "Shalltear/Editor/EditorInsertStrips.h"
#include "Shalltear/Editor/EditorListView.h"

#include <functional>
#include <span>
#include <string_view>

namespace Shalltear {

	class EditorPanel;

	struct GraphBlackboardItem {
		u64 id = 0;
		str name;
		std::function<void(UI::Context&, UI::ElementHandle row)> onBuildControls;
	};

	struct GraphBlackboardDesc {
		str title;
		Vector<str> kinds{GetMemoryResource(MemTag::Editor)};

		std::function<u64(u32 kind)> onAdd;
		std::function<u64(u64 id, std::string_view name)> onRename;
		std::function<u64(u64 id, u32 index)> onMove;
		std::function<void(std::span<const u64> ids)> onDelete;
		std::function<void(std::span<const u64> ids)> onSelectionChanged;
		std::function<void(UI::Context&, UI::ElementHandle host)> onBuildSubtitle;
	};

	class SHALLTEAR_EDITOR_API GraphBlackboard {
	public:
		GraphBlackboard() = default;
		GraphBlackboard(const GraphBlackboard&) = delete;
		GraphBlackboard& operator=(const GraphBlackboard&) = delete;

		void Build(EditorPanel* owner, UI::Context& context, UI::ElementHandle parent, GraphBlackboardDesc desc);

		void SetItems(std::span<const GraphBlackboardItem> items);

		bool BeginRename(u64 id);

		[[nodiscard]] bool HasFocus() const;

		[[nodiscard]] bool OwnsPayload(const UI::DragPayload& payload, u64& item) const;

		void Tick(f32 deltaSeconds);
	private:
		void Rebuild();
		void OpenAddMenu(UI::Context& context);
		void OpenRowMenu(UI::Context& context, u64 id);
		[[nodiscard]] EditorInsertStripDesc BandBefore(u32 before);
		[[nodiscard]] bool MovesAnything(const UI::DragPayload& payload, u32 before) const;
		void Drop(const UI::DragPayload& payload, u32 before);
		void CommitRename(u64 id, std::string_view name);
		void Arrive(u64 id, bool rename);
		[[nodiscard]] i32 IndexOf(u64 id) const;

		EditorListView _list;
		EditorInsertStrips _gaps;
		EditorInsertStrips _top;
		GraphBlackboardDesc _desc;
		Vector<GraphBlackboardItem> _items{GetMemoryResource(MemTag::Editor)};
		UI::Context* _context = nullptr;
		u64 _boardId = 0;
		u64 _renaming = 0;
		bool _renameNeedsFocus = false;
		u64 _arriving = 0;
		bool _arrivingRenames = false;
		bool _dirty = false;
		bool _refocus = false;
	};

} // namespace Shalltear
