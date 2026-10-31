#include "Shalltear/Editor/EditorVirtualRows.h"

#include "Shalltear/Core/UI/RenderList.h"
#include "Shalltear/Core/UI/UISystem.h"

#include "Shalltear/Editor/EditorTheme.h"

#include <algorithm>

namespace Shalltear {

	namespace {

		constexpr f32 OVERSCAN_WINDOWS = 0.5f;

		constexpr u32 BOOTSTRAP_ROWS = 64;

	} // anonymous namespace

	void EditorVirtualRows::Attach(UI::Context& context, UI::ElementHandle viewport, BuildRow build,
								   OnRealized realized, BuildTail buildTail) {
		_context = &context;
		_viewport = viewport;
		_build = std::move(build);
		_realized = std::move(realized);
		_buildTail = std::move(buildTail);
		_window.clear();
		_first = 0;
		_leadSpacer = {};
		_tailSpacer = {};
		_band = {};
		context.Events(viewport).onScrollChanged = [this](const UI::ScrollBand& band) { OnBand(band.shown); };
		context.Events(viewport).customDraw = [this](UI::Context& paintContext, UI::ElementHandle self,
													 UI::PaintScope& painter) {
			PaintStripes(paintContext, self, painter);
		};
	}

	void EditorVirtualRows::Detach() {
		UI::Context* context = Live();
		if (context && context->IsAlive(_viewport)) {
			context->Events(_viewport).onScrollChanged = {};
			context->Events(_viewport).customDraw = {};
		}
		_context = nullptr;
		_viewport = {};
		_window.clear();
		_first = 0;
		_leadSpacer = {};
		_tailSpacer = {};
		_band = {};
	}

	UI::Context* EditorVirtualRows::Live() const {
		if (!_context) {
			return nullptr;
		}
		if (!UISystem::IsInitialized()) {
			return _context;
		}
		return UISystem::IsContextLive(_context) ? _context : nullptr;
	}

	void EditorVirtualRows::Reset(u32 count) {
		UI::Context* context = Live();
		if (!context || !context->IsAlive(_viewport)) {
			return;
		}
		_heights.assign(count, 0.0f);
		_first = 0;
		_pendingReveal = NO_PENDING_REVEAL;
		u32 first = 0;
		u32 last = 0;
		RowRangeFor(first, last, _band.h * OVERSCAN_WINDOWS);
		RealizeWindow(first, last);
	}

	void EditorVirtualRows::Appended(u32 dropped, u32 appended) {
		UI::Context* context = Live();
		if (!context || !context->IsAlive(_viewport)) {
			return;
		}
		const u32 oldCount = GetCount();
		if (dropped > oldCount) {
			dropped = oldCount;
		}
		const bool windowWasAtTail = !_window.empty() && _first + GetRealizedCount() == oldCount;

		if (dropped > 0) {
			_heights.erase(_heights.begin(), _heights.begin() + static_cast<ptrdiff_t>(dropped));
		}
		_heights.insert(_heights.end(), appended, 0.0f);
		_stripeOrigin = (_stripeOrigin + dropped) & 1u;

		if (dropped > _first) {
			const u32 lost = std::min(dropped - _first, GetRealizedCount());
			if (lost > 0 && !HeadIsClean()) {
				Reset(GetCount());
				return;
			}
			for (u32 i = 0; i < lost; ++i) {
				DestroyRealizedRow(0);
			}
			_first = 0;
		} else {
			_first -= dropped;
		}

		if (windowWasAtTail && appended > 0) {
			ExtendTailTo(GetCount());
			TrimHeadAbove(_band.y - _band.h * OVERSCAN_WINDOWS);
		}
		Realize();
		UpdateSpacers();
	}

	void EditorVirtualRows::OnBand(UI::Rect shown) {
		const bool heightsMoved = HarvestHeights();
		_band = shown;
		Realize();
		if (_pendingReveal != NO_PENDING_REVEAL) {
			const u32 index = _pendingReveal;
			_pendingReveal = NO_PENDING_REVEAL;
			if (ApplyScrollIntoView(index)) {
				_pendingReveal = index;
				UI::Context* context = Live();
				if (const UI::Element* viewport = context ? context->GetElement(_viewport) : nullptr) {
					_band.y = viewport->scrollOffset.y;
					Realize();
				}
			}
		}
		if (heightsMoved) {
			UpdateSpacers();
		}
	}

	void EditorVirtualRows::Realize() {
		if (GetCount() == 0) {
			UI::Context* context = Live();
			if (context && context->IsAlive(_tailSpacer)) {
				return;
			}
		}
		if (!_window.empty() && GetCount() > 0) {
			u32 visibleFirst = 0;
			u32 visibleLast = 0;
			RowRangeFor(visibleFirst, visibleLast, 0.0f);
			const u32 builtLast = _first + GetRealizedCount();
			if (visibleFirst >= _first && visibleLast <= builtLast) {
				return;
			}
		}
		u32 first = 0;
		u32 last = 0;
		RowRangeFor(first, last, _band.h * OVERSCAN_WINDOWS);
		RealizeWindow(first, last);
	}

	void EditorVirtualRows::RealizeWindow(u32 first, u32 last) {
		UI::Context* context = Live();
		if (!context || !context->IsAlive(_viewport) || !_build) {
			return;
		}
		context->ClearChildren(_viewport);
		_window.clear();
		_first = first;

		_leadSpacer = CreateSpacer(ContentTopOf(first));
		for (u32 index = first; index < last && index < GetCount(); ++index) {
			_window.push_back(_build(index));
		}
		_tailSpacer = CreateSpacer(ContentHeight() - ContentTopOf(last));
		if (_buildTail) {
			_buildTail();
		}

		if (_realized) {
			_realized();
		}
	}

	bool EditorVirtualRows::TailIsClean() const {
		UI::Context* context = Live();
		if (!context || _window.empty() || !context->IsAlive(_viewport) || !context->IsAlive(_tailSpacer)) {
			return false;
		}
		const UI::Element* viewport = context->GetElement(_viewport);
		if (!viewport || viewport->lastChild == UI::INVALID_UI_INDEX ||
			context->MakeHandle(viewport->lastChild) != _tailSpacer) {
			return false;
		}
		const UI::Element* last = context->GetElement(_window.back());
		return last && last->nextSibling != UI::INVALID_UI_INDEX &&
			   context->MakeHandle(last->nextSibling) == _tailSpacer;
	}

	bool EditorVirtualRows::HeadIsClean() const {
		UI::Context* context = Live();
		if (!context || _window.empty() || !context->IsAlive(_leadSpacer)) {
			return false;
		}
		const UI::Element* lead = context->GetElement(_leadSpacer);
		return lead && lead->nextSibling != UI::INVALID_UI_INDEX &&
			   context->MakeHandle(lead->nextSibling) == _window.front();
	}

	void EditorVirtualRows::ExtendTailTo(u32 last) {
		UI::Context* context = Live();
		if (!context || !_build) {
			return;
		}
		const u32 builtLast = _first + GetRealizedCount();
		if (last <= builtLast) {
			return;
		}
		if (!TailIsClean()) {
			RealizeWindow(_first, last);
			return;
		}
		context->DestroyElement(_tailSpacer);
		_tailSpacer = {};
		for (u32 index = builtLast; index < last && index < GetCount(); ++index) {
			_window.push_back(_build(index));
		}
		_tailSpacer = CreateSpacer(ContentHeight() - ContentTopOf(_first + GetRealizedCount()));
		if (_realized) {
			_realized();
		}
	}

	void EditorVirtualRows::TrimHeadAbove(f32 keepAbovePx) {
		if (!HeadIsClean()) {
			return;
		}
		while (GetRealizedCount() > 1) {
			if (ContentTopOf(_first) + HeightOf(_first) >= keepAbovePx) {
				break;
			}
			DestroyRealizedRow(0);
			++_first;
			if (!HeadIsClean()) {
				break;
			}
		}
	}

	void EditorVirtualRows::DestroyRealizedRow(size_t slot) {
		UI::Context* context = Live();
		if (!context || slot >= _window.size()) {
			return;
		}
		if (context->IsAlive(_window[slot])) {
			context->DestroyElement(_window[slot]);
		}
		_window.erase(_window.begin() + static_cast<ptrdiff_t>(slot));
	}

	void EditorVirtualRows::PaintStripes(UI::Context& context, UI::ElementHandle viewport,
										 UI::PaintScope& painter) const {
		const Color color = EditorTheme::GetColor(EditorTheme::ColorToken::RowStripe);
		if (color.a <= 0.0f || _window.empty()) {
			return;
		}
		const UI::Rect& view = context.GetLayout(viewport).borderBox;
		if (view.w <= 0.0f || view.h <= 0.0f) {
			return;
		}
		for (size_t slot = 0; slot < _window.size(); ++slot) {
			if (!IsStriped(_first + static_cast<u32>(slot)) || !context.IsAlive(_window[slot])) {
				continue;
			}
			const UI::Rect& row = context.GetLayout(_window[slot]).borderBox;
			const f32 top = std::max(row.y, view.y);
			const f32 bottom = std::min(row.y + row.h, view.y + view.h);
			if (bottom <= top) {
				continue;
			}
			painter.EmitQuad({view.x, top, view.w, bottom - top}, color);
		}
	}

	bool EditorVirtualRows::HarvestHeights() {
		UI::Context* context = Live();
		if (!context) {
			return false;
		}
		bool moved = false;
		_measured.clear();
		for (size_t slot = 0; slot < _window.size(); ++slot) {
			const u32 index = _first + static_cast<u32>(slot);
			if (index >= _heights.size() || !context->IsAlive(_window[slot])) {
				continue;
			}
			f32 height = context->GetLayout(_window[slot]).borderBox.h;
			if (height <= 0.0f) {
				continue;
			}
			moved = moved || _heights[index] != height;
			_heights[index] = height;
			_measured.push_back(height);
		}
		if (!_measured.empty()) {
			auto middle = _measured.begin() + static_cast<ptrdiff_t>(_measured.size() / 2);
			std::nth_element(_measured.begin(), middle, _measured.end());
			_hint = *middle;
		}
		return moved;
	}

	f32 EditorVirtualRows::HeightOf(u32 index) const {
		if (index >= _heights.size()) {
			return _hint;
		}
		return _heights[index] > 0.0f ? _heights[index] : _hint;
	}

	f32 EditorVirtualRows::ContentTopOf(u32 index) const {
		f32 top = 0.0f;
		for (u32 i = 0; i < index && i < _heights.size(); ++i) {
			top += HeightOf(i);
		}
		return top;
	}

	f32 EditorVirtualRows::ContentHeight() const {
		return ContentTopOf(static_cast<u32>(_heights.size()));
	}

	UI::ElementHandle EditorVirtualRows::GetElement(u32 index) const {
		if (!IsRealized(index)) {
			return {};
		}
		return _window[index - _first];
	}

	UI::ElementHandle EditorVirtualRows::CreateSpacer(f32 height) {
		UI::Context* context = Live();
		if (!context) {
			return {};
		}
		UI::ElementHandle element = context->CreateElement();
		context->AppendChild(_viewport, element);
		UI::Style style;
		style.height = UI::Length::Pixel(std::max(0.0f, height) / std::max(0.001f, context->GetContentScale()));
		style.flexShrink = 0.0f;
		context->SetStyle(element, style);
		return element;
	}

	void EditorVirtualRows::UpdateSpacers() {
		SetSpacerHeight(_leadSpacer, ContentTopOf(_first));
		SetSpacerHeight(_tailSpacer, ContentHeight() - ContentTopOf(_first + GetRealizedCount()));
	}

	void EditorVirtualRows::SetSpacerHeight(UI::ElementHandle spacer, f32 height) {
		UI::Context* context = Live();
		if (!context || !context->IsAlive(spacer)) {
			return;
		}
		UI::Style style = context->GetStyle(spacer);
		UI::Length wanted = UI::Length::Pixel(std::max(0.0f, height) / std::max(0.001f, context->GetContentScale()));
		if (style.height == wanted) {
			return;
		}
		style.height = wanted;
		context->SetStyle(spacer, style);
	}

	void EditorVirtualRows::RowRangeFor(u32& first, u32& last, f32 marginPx) const {
		const u32 count = GetCount();
		first = 0;
		last = count;
		if (_hint <= 0.0f || _band.h <= 0.0f) {
			last = std::min(count, BOOTSTRAP_ROWS);
			return;
		}

		const f32 wantTop = _band.y - marginPx;
		const f32 wantBottom = _band.y + _band.h + marginPx;
		f32 cursor = 0.0f;
		first = count;
		last = 0;
		for (u32 i = 0; i < count; ++i) {
			const f32 height = HeightOf(i);
			if (cursor + height > wantTop && cursor < wantBottom) {
				first = std::min(first, i);
				last = i + 1;
			}
			cursor += height;
		}
		if (first > last) {
			first = count > BOOTSTRAP_ROWS ? count - BOOTSTRAP_ROWS : 0;
			last = count;
		}
	}

	void EditorVirtualRows::ScrollIntoView(u32 index) {
		if (!Live() || index >= GetCount()) {
			return;
		}
		ApplyScrollIntoView(index);
		_pendingReveal = index;
	}

	bool EditorVirtualRows::ApplyScrollIntoView(u32 index) {
		UI::Context* context = Live();
		if (!context || index >= GetCount() || !context->IsAlive(_viewport)) {
			return false;
		}
		const UI::Element* viewport = context->GetElement(_viewport);
		if (!viewport) {
			return false;
		}
		const f32 before = viewport->scrollOffset.y;
		UI::ElementHandle element = GetElement(index);
		if (element.IsValid() && context->IsAlive(element) && context->GetLayout(element).borderBox.h > 0.0f) {
			context->ScrollIntoView(element);
		} else {
			const f32 top = ContentTopOf(index);
			context->ScrollRangeIntoView(_viewport, top, top + HeightOf(index));
		}
		const UI::Element* after = context->GetElement(_viewport);
		return after && after->scrollOffset.y != before;
	}

} // namespace Shalltear
