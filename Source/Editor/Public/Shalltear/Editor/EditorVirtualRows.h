#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <functional>

namespace Shalltear {

	class SHALLTEAR_EDITOR_API EditorVirtualRows {
	public:
		using BuildRow = std::function<UI::ElementHandle(u32 index)>;
		using OnRealized = std::function<void()>;
		using BuildTail = std::function<void()>;

		EditorVirtualRows() = default;
		EditorVirtualRows(const EditorVirtualRows&) = delete;
		EditorVirtualRows& operator=(const EditorVirtualRows&) = delete;

		void Attach(UI::Context& context, UI::ElementHandle viewport, BuildRow build, OnRealized realized = {},
					BuildTail buildTail = {});
		void Detach();

		void Reset(u32 count);
		void Appended(u32 dropped, u32 appended);

		u32 GetCount() const { return static_cast<u32>(_heights.size()); }
		u32 GetFirstRealized() const { return _first; }
		u32 GetRealizedCount() const { return static_cast<u32>(_window.size()); }
		bool IsRealized(u32 index) const { return index >= _first && index < _first + GetRealizedCount(); }
		UI::ElementHandle GetElement(u32 index) const;

		f32 HeightOf(u32 index) const;
		f32 ContentTopOf(u32 index) const;
		f32 ContentHeight() const;

		void ScrollIntoView(u32 index);

		const UI::Rect& GetBand() const { return _band; }
	private:
		UI::Context* Live() const;
		void OnBand(UI::Rect shown);
		void PaintStripes(UI::Context& context, UI::ElementHandle viewport, UI::PaintScope& painter) const;
		bool IsStriped(u32 index) const { return ((index + _stripeOrigin) & 1u) != 0; }
		void Realize();
		void RealizeWindow(u32 first, u32 last);
		void RowRangeFor(u32& first, u32& last, f32 marginPx) const;
		bool HarvestHeights();
		void UpdateSpacers();
		bool ApplyScrollIntoView(u32 index);
		void SetSpacerHeight(UI::ElementHandle spacer, f32 height);
		UI::ElementHandle CreateSpacer(f32 height);

		bool TailIsClean() const;
		bool HeadIsClean() const;
		void ExtendTailTo(u32 last);
		void TrimHeadAbove(f32 keepAbovePx);
		void DestroyRealizedRow(size_t slot);

		UI::Context* _context = nullptr;
		UI::ElementHandle _viewport;
		BuildRow _build;
		OnRealized _realized;
		BuildTail _buildTail;

		Vector<f32> _heights{GetMemoryResource(MemTag::Editor)};
		f32 _hint = 0.0f;
		Vector<f32> _measured{GetMemoryResource(MemTag::Editor)};
		Vector<UI::ElementHandle> _window{GetMemoryResource(MemTag::Editor)};
		u32 _first = 0;
		UI::Rect _band{};
		static constexpr u32 NO_PENDING_REVEAL = 0xFFFFFFFFu;
		u32 _pendingReveal = NO_PENDING_REVEAL;
		UI::ElementHandle _leadSpacer;
		UI::ElementHandle _tailSpacer;
		u32 _stripeOrigin = 0;
	};

} // namespace Shalltear
