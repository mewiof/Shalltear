#include "UI/UIInternal.h"

#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Profiler.h"
#include "Shalltear/Core/Time.h"
#include "Shalltear/Core/UI/DragDrop.h"
#include "Shalltear/Core/UI/FontAsset.h"
#include "Shalltear/Core/UI/VectorAsset.h"

#include <algorithm>

namespace Shalltear::UI {

	namespace {
		constexpr i32 POPUP_Z_BASE = 10000;
		constexpr i32 TOOLTIP_Z = POPUP_Z_BASE * 2;

		constexpr u32 TOOLTIP_SEARCH_LIMIT = 64;

		constexpr const char* TOOLTIP_SKIN = R"css(
.slt-tooltip { padding: 4 7 4 7; max-width: 420; background-color: var(--slt-tooltip); border-radius: var(--slt-radius); box-shadow: inset 0 0 0 1 var(--slt-edge-light-strong), 0 4 12 var(--slt-shadow); }
)css";

		u32 NextContextId() {
			static u32 next = 0;
			return ++next;
		}
	} // anonymous namespace

	u32 ContextImpl::Allocate() {
		u32 index;
		if (!freeList.empty()) {
			index = freeList.back();
			freeList.pop_back();
			const u32 generation = elements[index].generation;
			elements[index] = Element{};
			elements[index].generation = generation;
			inlineStyles[index] = Style{};
			computedStyles[index] = Style{};
			layouts[index] = LayoutData{};
			measureCaches[index] = MeasureCache{};
			subtreeInk[index] = UnknownInk;
		} else {
			index = static_cast<u32>(elements.size());
			elements.emplace_back();
			inlineStyles.emplace_back();
			computedStyles.emplace_back();
			layouts.emplace_back();
			measureCaches.emplace_back();
			subtreeInk.push_back(UnknownInk);
		}
		RecalcStyle(index);
		BumpTreeRevision();
		return index;
	}

	void ContextImpl::Free(u32 index) {
		BumpTreeRevision();
		Element& element = elements[index];
		if (element.textIndex != INVALID_UI_INDEX) {
			texts[element.textIndex] = TextComponent{};
			textFreeList.push_back(element.textIndex);
		}
		if (element.textInputIndex != INVALID_UI_INDEX) {
			textInputs[element.textInputIndex] = TextInputState{};
			textInputFreeList.push_back(element.textInputIndex);
		}
		events.erase(index);
		icons.erase(index);
		scrollShown.erase(index);

		if (hovered == index) {
			hovered = INVALID_UI_INDEX;
		}
		if (pressed == index) {
			pressed = INVALID_UI_INDEX;
		}
		if (pressedRight == index) {
			pressedRight = INVALID_UI_INDEX;
		}
		if (pressedMiddle == index) {
			pressedMiddle = INVALID_UI_INDEX;
		}
		if (focused == index) {
			focused = INVALID_UI_INDEX;
		}
		if (lastClickIndex == index) {
			lastClickIndex = INVALID_UI_INDEX;
		}
		if (dragTarget == index) {
			dragTarget = INVALID_UI_INDEX;
		}
		if (popupScrim == index) {
			popupScrim = INVALID_UI_INDEX;
		}
		if (tooltipElement == index) {
			tooltipElement = INVALID_UI_INDEX;
			tooltipShown = false;
		}
		if (tooltipSource == index) {
			tooltipSource = INVALID_UI_INDEX;
			tooltipResolved = false;
		}
		tooltips.erase(index);
		popupDefaultButtons.erase(index);
		for (auto it = popupDefaultButtons.begin(); it != popupDefaultButtons.end();) {
			it = it->second == index ? popupDefaultButtons.erase(it) : std::next(it);
		}
		for (size_t i = 0; i < popups.size(); ++i) {
			if (popups[i].index == index) {
				popups.erase(popups.begin() + static_cast<i64>(i));
				break;
			}
		}

		++element.generation;
		element.parent = element.firstChild = element.lastChild = element.nextSibling = INVALID_UI_INDEX;
		element.textIndex = element.textInputIndex = INVALID_UI_INDEX;
		element.flags &= ~ElementFlags::Visible;
		freeList.push_back(index);
	}

	bool ContextImpl::IsAliveHandle(ElementHandle handle) const {
		return handle.context == contextId && handle.IsValid() &&
			   handle.index < elements.size() &&
			   elements[handle.index].generation == handle.generation;
	}

	u32 ContextImpl::ResolveIndex(ElementHandle handle) const {
		return IsAliveHandle(handle) ? handle.index : INVALID_UI_INDEX;
	}

	ElementHandle ContextImpl::HandleFor(u32 index) const {
		if (index == INVALID_UI_INDEX || index >= elements.size()) {
			return {};
		}
		return ElementHandle{index, elements[index].generation, contextId};
	}

	void ContextImpl::DetachFromParent(u32 index) {
		if (index == root) {
			return;
		}
		Element& element = elements[index];
		if (element.parent == INVALID_UI_INDEX) {
			return;
		}
		u32 oldParent = element.parent;
		Element& parent = elements[oldParent];
		++treeStats.linkOps;
		if (parent.firstChild == index) {
			parent.firstChild = element.nextSibling;
			if (parent.lastChild == index) {
				parent.lastChild = INVALID_UI_INDEX;
			}
		} else {
			u32 sibling = parent.firstChild;
			while (sibling != INVALID_UI_INDEX) {
				++treeStats.linkWork;
				u32 next = elements[sibling].nextSibling;
				if (next == index) {
					elements[sibling].nextSibling = element.nextSibling;
					if (parent.lastChild == index) {
						parent.lastChild = sibling;
					}
					break;
				}
				sibling = next;
			}
		}
		element.parent = INVALID_UI_INDEX;
		element.nextSibling = INVALID_UI_INDEX;
		BumpTreeRevision();
		MarkLayoutDirtyUpwards(oldParent);
	}

	void ContextImpl::DestroyRecursive(u32 index) {
		u32 child = elements[index].firstChild;
		while (child != INVALID_UI_INDEX) {
			u32 next = elements[child].nextSibling;
			DestroyRecursive(child);
			child = next;
		}
		Free(index);
	}

	void ContextImpl::MarkLayoutDirty(u32 index) {
		elements[index].flags |= ElementFlags::DirtyLayout;
		measureCaches[index].Invalidate();
	}

	void ContextImpl::MarkLayoutDirtyUpwards(u32 index) {
		while (index != INVALID_UI_INDEX) {
			MarkLayoutDirty(index);
			index = elements[index].parent;
		}
		layoutDirty = true;
	}

	void ContextImpl::InvalidatePaintInkUpwards(u32 index) {
		while (index != INVALID_UI_INDEX) {
			subtreeInk[index] = UnknownInk;
			index = elements[index].parent;
		}
	}

	void ContextImpl::MarkContentDirty(u32 index) {
		Element& element = elements[index];
		if (HasFlag(element.flags, ElementFlags::ContentDirty)) {
			return;
		}
		element.flags |= ElementFlags::ContentDirty;
		MarkLayoutDirty(index);
		contentDirty.push_back(HandleFor(index));

		for (u32 ancestor = element.parent; ancestor != INVALID_UI_INDEX; ancestor = elements[ancestor].parent) {
			measureCaches[ancestor].intrinsicValid = false;
		}
	}

	bool ContextImpl::FullPassWillRemeasure(u32 index) const {
		if (!layoutDirty) {
			return false;
		}
		for (u32 ancestor = elements[index].parent; ancestor != INVALID_UI_INDEX; ancestor = elements[ancestor].parent) {
			if (!HasFlag(elements[ancestor].flags, ElementFlags::DirtyLayout)) {
				return false;
			}
		}
		return true;
	}

	void ContextImpl::ResolveContentDirty() {
		if (contentDirty.empty()) {
			return;
		}
		Vector<ElementHandle> pending{GetMemoryResource(MemTag::UI)};
		pending.swap(contentDirty);

		for (ElementHandle handle : pending) {
			u32 index = ResolveIndex(handle);
			if (index == INVALID_UI_INDEX) {
				continue;
			}
			Element& element = elements[index];
			element.flags &= ~ElementFlags::ContentDirty;

			if (FullPassWillRemeasure(index)) {
				continue;
			}

			if (Layout::RemeasureInPlace(*this, index)) {
				MarkLayoutDirtyUpwards(element.parent);
				if (element.parent == INVALID_UI_INDEX) {
					layoutDirty = true;
				}
				continue;
			}
			element.flags &= ~ElementFlags::DirtyLayout;
		}
	}

	bool ContextImpl::NotifyScrollMoved() {
		if (scrollMoved.empty()) {
			return false;
		}
		Vector<ElementHandle> pending{GetMemoryResource(MemTag::UI)};
		pending.swap(scrollMoved);

		for (ElementHandle handle : pending) {
			const u32 index = ResolveIndex(handle);
			if (index == INVALID_UI_INDEX || ComputedS(index).overflow != Overflow::Scroll) {
				continue;
			}
			const Element& element = elements[index];
			const ScrollBand shown = {
				{element.scrollOffset.x, element.scrollOffset.y, L(index).content.w, L(index).content.h},
				element.maxScroll};
			auto told = scrollShown.find(index);
			if (told != scrollShown.end() && told->second == shown) {
				continue;
			}
			scrollShown[index] = shown;

			auto entry = events.find(index);
			if (entry == events.end() || !entry->second.onScrollChanged) {
				continue;
			}
			std::function<void(const ScrollBand&)> handler = entry->second.onScrollChanged;
			handler(shown);
		}
		return layoutDirty;
	}

	bool ContextImpl::IsPopupIndex(u32 index) const {
		for (const PopupEntry& popup : popups) {
			if (popup.index == index) {
				return true;
			}
		}
		return false;
	}

	size_t ContextImpl::PopupContaining(u32 index) const {
		for (u32 walk = index; walk != INVALID_UI_INDEX; walk = elements[walk].parent) {
			for (size_t i = 0; i < popups.size(); ++i) {
				if (popups[i].index == walk) {
					return i;
				}
			}
		}
		return popups.size();
	}

	u32 ContextImpl::FocusToHandBack(const PopupEntry& entry) const {
		const u32 target = ResolveIndex(entry.focusBefore);
		if (target == INVALID_UI_INDEX || !HasFlag(elements[target].flags, ElementFlags::Focusable)) {
			return INVALID_UI_INDEX;
		}
		for (u32 walk = target; walk != root; walk = elements[walk].parent) {
			if (walk == entry.index || elements[walk].parent == INVALID_UI_INDEX) {
				return INVALID_UI_INDEX;
			}
		}
		return target;
	}

	void ContextImpl::ClosePopupsFrom(size_t firstClosed) {
		while (popups.size() > firstClosed) {
			const PopupEntry entry = popups.back();
			const u32 index = entry.index;
			popups.pop_back();

			auto insideSubtree = [&](u32 candidate) {
				for (u32 walk = candidate; walk != INVALID_UI_INDEX; walk = E(walk).parent) {
					if (walk == index) {
						return true;
					}
				}
				return false;
			};
			if (hovered != INVALID_UI_INDEX && insideSubtree(hovered)) {
				SetHovered(INVALID_UI_INDEX);
			}
			if (pressed != INVALID_UI_INDEX && insideSubtree(pressed)) {
				E(pressed).flags &= ~ElementFlags::Pressed;
				RecalcPointerState(pressed);
				pressed = INVALID_UI_INDEX;
			}
			if (pressedRight != INVALID_UI_INDEX && insideSubtree(pressedRight)) {
				pressedRight = INVALID_UI_INDEX;
			}
			if (pressedMiddle != INVALID_UI_INDEX && insideSubtree(pressedMiddle)) {
				pressedMiddle = INVALID_UI_INDEX;
			}
			const bool heldInside = focused != INVALID_UI_INDEX && insideSubtree(focused);
			const bool droppedInside = focused == INVALID_UI_INDEX && entry.focusDroppedInside;
			if (heldInside || droppedInside) {
				SetFocused(FocusToHandBack(entry));
			}

			DetachFromParent(index);

			// after detach (destroying callback sees it gone)
			auto eventsIt = events.find(index);
			if (eventsIt != events.end() && eventsIt->second.onPopupClosed) {
				eventsIt->second.onPopupClosed();
			}
		}
		UpdatePopupScrim();
	}

	void ContextImpl::SetDragTarget(u32 index) {
		if (index == dragTarget) {
			return;
		}
		u32 previous = dragTarget;
		dragTarget = index;
		if (previous != INVALID_UI_INDEX) {
			E(previous).flags &= ~ElementFlags::DropHover;
			RecalcStyle(previous);
		}
		if (index != INVALID_UI_INDEX) {
			E(index).flags |= ElementFlags::DropHover;
			RecalcStyle(index);
		}
	}

	bool ContextImpl::PositionPopups() {
		bool dirtied = false;
		f32 scale = std::max(0.001f, desc.contentScale);
		Vec2 size = {static_cast<f32>(desc.sizePx.x), static_cast<f32>(desc.sizePx.y)};
		for (const PopupEntry& popup : popups) {
			const Rect& box = L(popup.index).borderBox;
			f32 wantX = HasFlag(popup.flags, PopupFlags::Center) ? (size.x - box.w) * 0.5f : popup.anchor.x;
			f32 wantY = HasFlag(popup.flags, PopupFlags::Center) ? (size.y - box.h) * 0.5f : popup.anchor.y;
			f32 placedX = std::clamp(wantX, 0.0f, std::max(0.0f, size.x - box.w));
			f32 placedY = std::clamp(wantY, 0.0f, std::max(0.0f, size.y - box.h));
			const Rect& ex = popup.exclude;
			if (popup.side != PopupSide::None && placedX < ex.x + ex.w && ex.x < placedX + box.w && placedY < ex.y + ex.h &&
				ex.y < placedY + box.h) {
				if (popup.side == PopupSide::Right && ex.x >= box.w) {
					placedX = ex.x - box.w;
				} else if (popup.side == PopupSide::Below && ex.y >= box.h) {
					placedY = ex.y - box.h;
				}
			}
			if (placedX != box.x || placedY != box.y) {
				Style style = inlineStyles[popup.index];
				style.left = Length::Pixel(placedX / scale);
				style.top = Length::Pixel(placedY / scale);
				owner->SetStyle(HandleFor(popup.index), style);
				dirtied = true;
			}
		}
		return dirtied;
	}

	size_t ContextImpl::LowestScrimmedPopup() const {
		for (size_t i = 0; i < popups.size(); ++i) {
			if (HasFlag(popups[i].flags, PopupFlags::Scrim)) {
				return i;
			}
		}
		return popups.size();
	}

	bool ContextImpl::IsBehindPopupScrim(u32 index) const {
		const size_t lowest = LowestScrimmedPopup();
		if (lowest == popups.size() || index == INVALID_UI_INDEX) {
			return false;
		}
		for (u32 walk = index; walk != INVALID_UI_INDEX; walk = elements[walk].parent) {
			for (size_t i = lowest; i < popups.size(); ++i) {
				if (popups[i].index == walk) {
					return false;
				}
			}
		}
		return true;
	}

	void ContextImpl::UpdatePopupScrim() {
		size_t lowest = LowestScrimmedPopup();
		if (lowest == popups.size()) {
			if (popupScrim != INVALID_UI_INDEX) {
				owner->SetVisible(HandleFor(popupScrim), false);
			}
			return;
		}

		if (popupScrim == INVALID_UI_INDEX) {
			popupScrim = ResolveIndex(owner->CreateElement());
		}
		Style style;
		style.position = Position::Absolute;
		style.left = Length::Pixel(0.0f);
		style.top = Length::Pixel(0.0f);
		style.right = Length::Pixel(0.0f);
		style.bottom = Length::Pixel(0.0f);
		style.background = Brush::SolidColor(
			owner->GetStyleVarColor("--slt-popup-scrim"_hash, Color(0.0f, 0.0f, 0.0f, 0.45f)));
		style.zIndex = POPUP_Z_BASE + static_cast<i32>(lowest) * 10 - 5;
		ElementHandle handle = HandleFor(popupScrim);
		owner->SetStyle(handle, style);
		owner->SetVisible(handle, true);
		owner->AppendChild(owner->GetRoot(), handle);
	}

	String ContextImpl::TooltipTextFor(u32 elementIndex) {
		for (u32 walk = elementIndex; walk != INVALID_UI_INDEX; walk = elements[walk].parent) {
			auto it = tooltips.find(walk);
			if (it != tooltips.end() && !it->second.empty()) {
				return String(it->second, GetMemoryResource(MemTag::UI));
			}
		}

		u32 stack[TOOLTIP_SEARCH_LIMIT];
		u32 depth = 0;
		u32 visited = 0;
		stack[depth++] = elementIndex;
		while (depth > 0 && visited < TOOLTIP_SEARCH_LIMIT) {
			u32 index = stack[--depth];
			++visited;
			const Element& element = elements[index];
			if (!HasFlag(element.flags, ElementFlags::Visible)) {
				continue;
			}
			if (element.textIndex != INVALID_UI_INDEX) {
				EnsureShaped(index);
				const TextComponent& text = texts[element.textIndex];
				if (text.shaped.truncated && !text.utf8.empty()) {
					return String(text.utf8, GetMemoryResource(MemTag::UI));
				}
			}
			for (u32 child = element.firstChild; child != INVALID_UI_INDEX && depth < TOOLTIP_SEARCH_LIMIT;
				 child = elements[child].nextSibling) {
				if (ComputedS(child).pointerEvents && HasFlag(elements[child].flags, ElementFlags::Hoverable)) {
					continue;
				}
				stack[depth++] = child;
			}
		}
		return String(GetMemoryResource(MemTag::UI));
	}

	bool ContextImpl::ResolveTooltip() {
		u32 candidate = hovered;
		if (hoverSuppressed || pointerClaim != 0 || !mouseInside ||
			pressed != INVALID_UI_INDEX || pressedRight != INVALID_UI_INDEX ||
			pressedMiddle != INVALID_UI_INDEX ||
			!desc.tooltipFont.IsValid() || candidate == root) {
			candidate = INVALID_UI_INDEX;
		}

		bool dirtied = false;
		if (candidate != tooltipSource) {
			tooltipSource = candidate;
			tooltipHoverStart = Time::GetRealtimeSinceStartup();
			tooltipResolved = false;
			if (tooltipShown) {
				owner->SetVisible(HandleFor(tooltipElement), false);
				tooltipShown = false;
				dirtied = true;
			}
		}
		if (tooltipSource == INVALID_UI_INDEX || tooltipShown || tooltipResolved) {
			return dirtied;
		}
		if (Time::GetRealtimeSinceStartup() - tooltipHoverStart < static_cast<f64>(desc.tooltipDelaySeconds)) {
			return dirtied;
		}
		tooltipResolved = true;

		String text = TooltipTextFor(tooltipSource);
		if (text.empty()) {
			return dirtied;
		}

		if (tooltipElement == INVALID_UI_INDEX) {
			owner->LoadStyleSheetOnce(StringHash("slt-tooltip-skin"), TOOLTIP_SKIN);
			tooltipElement = ResolveIndex(owner->CreateElement());
			owner->AddClass(HandleFor(tooltipElement), StringHash("slt-tooltip"));
			owner->SetHoverable(HandleFor(tooltipElement), false);
		}
		ElementHandle handle = HandleFor(tooltipElement);
		Style style = inlineStyles[tooltipElement];
		style.position = Position::Absolute;
		style.zIndex = TOOLTIP_Z;
		owner->SetStyle(handle, style);
		owner->SetText(handle, desc.tooltipFont, desc.tooltipFontSizePx,
					   owner->GetStyleVarColor(StringHash("--slt-text"), Color(0.88f, 0.88f, 0.9f)), text);
		owner->SetVisible(handle, true);
		owner->AppendChild(owner->GetRoot(), handle);

		tooltipAnchor = lastMousePos;
		tooltipShown = true;
		return true;
	}

	bool ContextImpl::PlaceTooltip() {
		if (!tooltipShown || tooltipElement == INVALID_UI_INDEX) {
			return false;
		}
		f32 scale = std::max(0.001f, desc.contentScale);
		f32 offset = owner->GetStyleVarFloat(StringHash("--slt-tooltip-offset"), 18.0f) * scale;
		Vec2 size = {static_cast<f32>(desc.sizePx.x), static_cast<f32>(desc.sizePx.y)};
		const Rect& box = L(tooltipElement).borderBox;

		f32 placedX = std::clamp(tooltipAnchor.x, 0.0f, std::max(0.0f, size.x - box.w));
		f32 wantY = tooltipAnchor.y + offset;
		if (wantY + box.h > size.y) {
			wantY = tooltipAnchor.y - offset - box.h;
		}
		f32 placedY = std::clamp(wantY, 0.0f, std::max(0.0f, size.y - box.h));
		if (placedX == box.x && placedY == box.y) {
			return false;
		}
		Style style = inlineStyles[tooltipElement];
		style.left = Length::Pixel(placedX / scale);
		style.top = Length::Pixel(placedY / scale);
		owner->SetStyle(HandleFor(tooltipElement), style);
		return true;
	}

	u8 ContextImpl::CurrentPseudo(const Element& element) const {
		if (HasFlag(element.flags, ElementFlags::DropHover)) {
			return PSEUDO_DROP;
		}
		if (HasFlag(element.flags, ElementFlags::Focused)) {
			return PSEUDO_FOCUS;
		}
		ElementFlags pointer = element.flags;
		for (const Element* walk = &element;
			 HasFlag(walk->flags, ElementFlags::InheritsPointerState) && walk->parent != INVALID_UI_INDEX;) {
			walk = &elements[walk->parent];
			pointer |= walk->flags;
		}
		if (HasFlag(pointer, ElementFlags::Pressed)) {
			return PSEUDO_ACTIVE;
		}
		if (HasFlag(pointer, ElementFlags::Hovered)) {
			return PSEUDO_HOVER;
		}
		return PSEUDO_NONE;
	}

	void ContextImpl::RecalcStyle(u32 index) {
		elements[index].flags |= ElementFlags::DirtyStyle;
		MarkLayoutDirtyUpwards(index);
	}

	void ContextImpl::RecalcPointerState(u32 index) {
		RecalcStyle(index);
		for (u32 child = elements[index].firstChild; child != INVALID_UI_INDEX; child = elements[child].nextSibling) {
			if (HasFlag(elements[child].flags, ElementFlags::InheritsPointerState)) {
				RecalcPointerState(child);
			}
		}
	}

	void ContextImpl::DrainStyle(u32 index) {
		if (index >= elements.size()) {
			return;
		}
		if (HasFlag(elements[index].flags, ElementFlags::DirtyStyle)) {
			ComposeStyle(index);
		}
	}

	void ContextImpl::ComposeStyle(u32 index) {
		Element& element = elements[index];

		++styleStats.recalcs;
		styleStats.ruleTests += styleSheet.GetRules().size();

		Style composed = inlineStyles[index];

		u8 pseudo = CurrentPseudo(element);
		for (const StyleRule& rule : styleSheet.GetRules()) {
			if (rule.targetTag.GetValue() != 0 && rule.targetTag != element.typeTag) {
				continue;
			}
			if (rule.targetId.GetValue() != 0 && rule.targetId != element.id) {
				continue;
			}
			if (rule.pseudoState != PSEUDO_NONE && rule.pseudoState != pseudo) {
				continue;
			}
			if (rule.targetClass.GetValue() != 0) {
				bool found = false;
				for (u8 i = 0; i < element.classCount; ++i) {
					if (element.classes[i] == rule.targetClass) {
						found = true;
						break;
					}
				}
				if (!found) {
					continue;
				}
			}
			StyleSheet::ApplyRule(composed, rule, &styleVars);
		}

		computedStyles[index] = composed;
		element.flags &= ~ElementFlags::DirtyStyle;
	}

	void ContextImpl::RecalcAllStyles() {
		SLT_PROFILE_ZONE("UI.RecalcAll");
		for (u32 i = 0; i < elements.size(); ++i) {
			if (std::find(freeList.begin(), freeList.end(), i) == freeList.end()) {
				RecalcStyle(i);
			}
		}
	}

	TextComponent* ContextImpl::GetText(u32 elementIndex) {
		u32 slot = elements[elementIndex].textIndex;
		return slot != INVALID_UI_INDEX ? &texts[slot] : nullptr;
	}

	const TextComponent* ContextImpl::GetText(u32 elementIndex) const {
		u32 slot = elements[elementIndex].textIndex;
		return slot != INVALID_UI_INDEX ? &texts[slot] : nullptr;
	}

	IconComponent* ContextImpl::GetIcon(u32 elementIndex) {
		if (!HasFlag(elements[elementIndex].flags, ElementFlags::HasIcon)) {
			return nullptr;
		}
		auto it = icons.find(elementIndex);
		return it != icons.end() ? &it->second : nullptr;
	}

	const IconComponent* ContextImpl::GetIcon(u32 elementIndex) const {
		if (!HasFlag(elements[elementIndex].flags, ElementFlags::HasIcon)) {
			return nullptr;
		}
		auto it = icons.find(elementIndex);
		return it != icons.end() ? &it->second : nullptr;
	}

	IconComponent* ContextImpl::GetPicture(u32 elementIndex) {
		if (!HasAnyFlag(elements[elementIndex].flags, ELEMENT_HAS_PICTURE)) {
			return nullptr;
		}
		auto it = icons.find(elementIndex);
		return it != icons.end() ? &it->second : nullptr;
	}

	const IconComponent* ContextImpl::GetPicture(u32 elementIndex) const {
		if (!HasAnyFlag(elements[elementIndex].flags, ELEMENT_HAS_PICTURE)) {
			return nullptr;
		}
		auto it = icons.find(elementIndex);
		return it != icons.end() ? &it->second : nullptr;
	}

	Vec2 FitTextureInto(const TextureAsset* image, f32 sizePx) {
		if (!image || image->size.x <= 0 || image->size.y <= 0 || sizePx <= 0.0f) {
			return {};
		}
		const f32 w = static_cast<f32>(image->size.x);
		const f32 h = static_cast<f32>(image->size.y);
		const f32 scale = sizePx / std::max(w, h);
		return {w * scale, h * scale};
	}

	Vec2 ContextImpl::MeasureIconExtent(u32 elementIndex) {
		IconComponent* icon = GetPicture(elementIndex);
		if (!icon) {
			return {};
		}
		const f32 sizePhysical = icon->sizePx * desc.contentScale;
		if (icon->cachedSizePhysical == sizePhysical) {
			return icon->measuredExtent;
		}
		if (HasFlag(elements[elementIndex].flags, ElementFlags::HasImage)) {
			const TextureAsset* image = icon->texture.Get();
			icon->measuredExtent = FitTextureInto(image, std::min(sizePhysical, MAX_ICON_PX));
			icon->cachedSizePhysical = sizePhysical;
			return icon->measuredExtent;
		}
		if (iconSystem) {
			icon->measuredExtent = iconSystem->MeasureIcon(icon->asset, sizePhysical);
		} else {
			const VectorAsset* image = icon->asset.Get();
			icon->measuredExtent = image && !image->IsEmpty()
									   ? image->FitInto(std::min(sizePhysical, MAX_ICON_PX))
									   : Vec2{};
		}
		icon->cachedSizePhysical = sizePhysical;
		return icon->measuredExtent;
	}

	TextInputState* ContextImpl::GetTextInput(u32 elementIndex) {
		u32 slot = elements[elementIndex].textInputIndex;
		return slot != INVALID_UI_INDEX ? &textInputs[slot] : nullptr;
	}

	Vec2 ContextImpl::TextOriginPx(u32 elementIndex) const {
		const LayoutData& box = layouts[elementIndex];
		const TextComponent* text = GetText(elementIndex);
		if (!text) {
			return {box.content.x, box.content.y};
		}
		const Element& element = elements[elementIndex];
		const Style& style = ComputedS(elementIndex);
		const Vec2 extent = text->shaped.extent;

		f32 dx = box.content.x;
		f32 dy = box.content.y;
		if (element.textInputIndex != INVALID_UI_INDEX) {
			dx -= element.scrollOffset.x;
			if (textInputs[element.textInputIndex].multiline) {
				return {Snap(dx), Snap(dy - element.scrollOffset.y)};
			}
		} else {
			f32 freeX = std::max(0.0f, box.content.w - extent.x);
			if (style.textAlign == Align::Center) {
				dx += freeX * 0.5f;
			} else if (style.textAlign == Align::End) {
				dx += freeX;
			}
		}
		f32 freeY = std::max(0.0f, box.content.h - extent.y);
		if (style.textAlignVertical == Align::Center) {
			dy += freeY * 0.5f;
		} else if (style.textAlignVertical == Align::End) {
			dy += freeY;
		}
		return {Snap(dx), Snap(dy)};
	}

	TextOverflow ContextImpl::EffectiveTextOverflow(u32 elementIndex) const {
		const u32 input = elements[elementIndex].textInputIndex;
		if (input != INVALID_UI_INDEX) {
			return textInputs[input].multiline ? TextOverflow::Wrap : TextOverflow::Clip;
		}
		return ComputedS(elementIndex).textOverflow;
	}

	void ContextImpl::EnsureShaped(u32 elementIndex) {
		TextComponent* text = GetText(elementIndex);
		if (!text || !fontSystem) {
			return;
		}
		const Style& style = ComputedS(elementIndex);
		f32 scale = desc.contentScale > 0.0f ? desc.contentScale : 1.0f;
		f32 physicalSize = text->sizePx * scale;
		f32 physicalOutline = style.textOutlineWidth * scale;
		f32 physicalSpacing = style.letterSpacing * scale;
		TextOverflow overflow = EffectiveTextOverflow(elementIndex);

		const u32 atlasGeneration = fontSystem->GetAtlasGeneration();
		if (!text->dirty &&
			text->shapedWrapWidth == text->wrapWidth &&
			text->cachedSizePhysical == physicalSize &&
			text->cachedOutlineWidth == physicalOutline &&
			text->cachedLetterSpacing == physicalSpacing &&
			text->cachedOverflow == overflow &&
			text->shapedGeneration == atlasGeneration) {
			return;
		}

		TextQuery query;
		query.font = text->font;
		query.sizePx = physicalSize;
		query.utf8 = std::string_view(text->utf8.data(), text->utf8.size());
		query.wrapWidth = text->wrapWidth;
		query.overflow = overflow;
		query.outlineWidth = physicalOutline;
		query.letterSpacing = physicalSpacing;
		fontSystem->ShapeText(query, text->shaped);

		text->measuredExtent = text->shaped.extent;
		text->cachedSizePhysical = physicalSize;
		text->cachedOutlineWidth = physicalOutline;
		text->cachedLetterSpacing = physicalSpacing;
		text->cachedOverflow = overflow;
		text->shapedGeneration = atlasGeneration;
		text->shapedWrapWidth = text->wrapWidth;
		text->dirty = false;
		text->StoreExtent(text->wrapWidth, text->measuredExtent);
	}

	Vec2 ContextImpl::MeasureTextExtent(u32 elementIndex, f32 wrapWidth, bool commit) {
		TextComponent* text = GetText(elementIndex);
		if (!text) {
			return {};
		}
		if (!fontSystem) {
			return {};
		}

		const Style& style = ComputedS(elementIndex);
		f32 scale = desc.contentScale > 0.0f ? desc.contentScale : 1.0f;
		f32 physicalSize = text->sizePx * scale;
		f32 physicalOutline = style.textOutlineWidth * scale;
		f32 physicalSpacing = style.letterSpacing * scale;

		TextOverflow overflow = EffectiveTextOverflow(elementIndex);
		if (text->memoSizePhysical != physicalSize || text->memoOutlineWidth != physicalOutline ||
			text->memoLetterSpacing != physicalSpacing || text->memoOverflow != overflow) {
			text->sizeCache.clear();
			text->memoSizePhysical = physicalSize;
			text->memoOutlineWidth = physicalOutline;
			text->memoLetterSpacing = physicalSpacing;
			text->memoOverflow = overflow;
		}

		if (!text->dirty) {
			for (const TextComponent::SizeEntry& entry : text->sizeCache) {
				if (std::abs(entry.wrapWidth - wrapWidth) < 0.01f) {
					if (commit) {
						text->wrapWidth = wrapWidth;
						text->measuredExtent = entry.extent;
					}
					return entry.extent;
				}
			}
		}

		TextQuery query;
		query.font = text->font;
		query.sizePx = physicalSize;
		query.utf8 = std::string_view(text->utf8.data(), text->utf8.size());
		query.wrapWidth = wrapWidth;
		query.overflow = overflow;
		query.outlineWidth = physicalOutline;
		query.letterSpacing = physicalSpacing;
		const Vec2 extent = fontSystem->MeasureText(query);
		text->dirty = false;
		text->StoreExtent(wrapWidth, extent);
		if (commit) {
			text->wrapWidth = wrapWidth;
			text->measuredExtent = extent;
		}
		return extent;
	}

	void ContextImpl::OnTextEdited(u32 elementIndex) {
		TextComponent* text = GetText(elementIndex);
		if (!text) {
			return;
		}
		text->ResetCaches();
		MarkLayoutDirtyUpwards(elementIndex);
		if (TextInputState* input = GetTextInput(elementIndex)) {
			input->completion.clear();
		}
		auto eventsIt = events.find(elementIndex);
		if (eventsIt != events.end() && eventsIt->second.onTextChange) {
			eventsIt->second.onTextChange(std::string_view(text->utf8.data(), text->utf8.size()));
		}
	}

	const ShapedRun* ContextImpl::ShapeCompletion(u32 elementIndex) {
		TextInputState* input = GetTextInput(elementIndex);
		TextComponent* text = GetText(elementIndex);
		if (!input || !text || input->completion.empty() || !fontSystem) {
			return nullptr;
		}
		const f32 scale = desc.contentScale > 0.0f ? desc.contentScale : 1.0f;
		CompletionKey key;
		key.element = elementIndex;
		key.generation = E(elementIndex).generation;
		key.sizePhysical = text->sizePx * scale;
		key.letterSpacing = ComputedS(elementIndex).letterSpacing * scale;
		key.atlasGeneration = fontSystem->GetAtlasGeneration();
		if (key.element != completionKey.element || key.generation != completionKey.generation ||
			key.sizePhysical != completionKey.sizePhysical || key.letterSpacing != completionKey.letterSpacing ||
			key.atlasGeneration != completionKey.atlasGeneration || completionShaped != input->completion) {
			TextQuery query;
			query.font = text->font;
			query.sizePx = key.sizePhysical;
			query.utf8 = std::string_view(input->completion.data(), input->completion.size());
			query.overflow = TextOverflow::Clip;
			query.letterSpacing = key.letterSpacing;
			fontSystem->ShapeText(query, completionRun);
			completionKey = key;
			completionShaped = input->completion;
		}
		return &completionRun;
	}

	Context::Context(const ContextDesc& desc, IFontSystem* fontSystem, IVectorIconSystem* iconSystem) {
		_impl = New<ContextImpl>(MemTag::UI);
		_impl->owner = this;
		_impl->contextId = NextContextId();
		_impl->desc = desc;
		_impl->fontSystem = fontSystem;
		_impl->iconSystem = iconSystem;

		_impl->elements.reserve(256);
		_impl->inlineStyles.reserve(256);
		_impl->computedStyles.reserve(256);
		_impl->layouts.reserve(256);

		_impl->lastMousePos = {-1.0f, -1.0f};

		_impl->styleVars.Set(GetDefaultStyleVars());

		_impl->root = _impl->Allocate();
	}

	Context::~Context() {
		Delete(_impl);
		_impl = nullptr;
	}

	ElementHandle Context::GetRoot() const {
		return _impl->HandleFor(_impl->root);
	}

	ElementHandle Context::CreateElement() {
		u32 index = _impl->Allocate();
		return _impl->HandleFor(index);
	}

	void Context::DestroyElement(ElementHandle handle) {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX) {
			return;
		}
		SLT_CORE_ASSERT_MSG(index != _impl->root, "the root element can't be destroyed");
		_impl->DetachFromParent(index);
		_impl->DestroyRecursive(index);
	}

	void Context::DestroyElementNextUpdate(ElementHandle handle) {
		if (_impl->IsAliveHandle(handle)) {
			_impl->pendingDestroy.push_back(handle);
		}
	}

	void Context::ClearChildren(ElementHandle parent) {
		u32 index = _impl->ResolveIndex(parent);
		if (index == INVALID_UI_INDEX) {
			return;
		}
		Element& element = _impl->E(index);
		while (element.firstChild != INVALID_UI_INDEX) {
			u32 child = element.firstChild;
			_impl->DetachFromParent(child);
			_impl->DestroyRecursive(child);
		}
	}

	void Context::AppendChild(ElementHandle parent, ElementHandle child) {
		u32 parentIndex = _impl->ResolveIndex(parent);
		u32 childIndex = _impl->ResolveIndex(child);
		if (parentIndex == INVALID_UI_INDEX || childIndex == INVALID_UI_INDEX || parentIndex == childIndex) {
			return;
		}
#if !defined(SHALLTEAR_CONFIG_RELEASE)
		for (u32 walk = parentIndex; walk != INVALID_UI_INDEX; walk = _impl->E(walk).parent) {
			SLT_CORE_ASSERT_MSG(walk != childIndex, "AppendChild would create a cycle");
		}
#endif
		_impl->DetachFromParent(childIndex);
		Element& parentElement = _impl->E(parentIndex);
		Element& childElement = _impl->E(childIndex);
		childElement.parent = parentIndex;
		childElement.nextSibling = INVALID_UI_INDEX;
		++_impl->treeStats.linkOps;
		if (parentElement.lastChild == INVALID_UI_INDEX) {
			parentElement.firstChild = childIndex;
		} else {
			_impl->E(parentElement.lastChild).nextSibling = childIndex;
		}
		parentElement.lastChild = childIndex;
		_impl->BumpTreeRevision();
		_impl->MarkLayoutDirtyUpwards(parentIndex);
		if (HasFlag(childElement.flags, ElementFlags::InheritsPointerState)) {
			_impl->RecalcPointerState(childIndex);
		}
	}

	bool Context::IsAlive(ElementHandle handle) const {
		return _impl->IsAliveHandle(handle);
	}

	u32 Context::GetLiveCount() const {
		return static_cast<u32>(_impl->elements.size() - _impl->freeList.size()) - 1;
	}

	const Element* Context::GetElement(ElementHandle handle) const {
		u32 index = _impl->ResolveIndex(handle);
		return index != INVALID_UI_INDEX ? &_impl->E(index) : nullptr;
	}

	ElementHandle Context::MakeHandle(u32 elementIndex) const {
		return _impl->HandleFor(elementIndex);
	}

	u32 Context::GetTreeRevision() const {
		return _impl->treeRevision;
	}

	namespace {

		void SetElementFlag(ContextImpl& impl, ElementHandle handle, ElementFlags flag, bool value, bool affectsLayout) {
			u32 index = impl.ResolveIndex(handle);
			if (index == INVALID_UI_INDEX) {
				return;
			}
			Element& element = impl.E(index);
			bool current = HasFlag(element.flags, flag);
			if (current == value) {
				return;
			}
			if (value) {
				element.flags |= flag;
			} else {
				element.flags &= ~flag;
			}
			if (affectsLayout) {
				impl.MarkLayoutDirtyUpwards(index);
			}
		}

	} // anonymous namespace

	void Context::SetVisible(ElementHandle handle, bool visible) {
		SetElementFlag(*_impl, handle, ElementFlags::Visible, visible, true);
	}

	void Context::SetHoverable(ElementHandle handle, bool hoverable) {
		SetElementFlag(*_impl, handle, ElementFlags::Hoverable, hoverable, false);
	}

	void Context::SetInheritsPointerState(ElementHandle handle, bool inherits) {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX || HasFlag(_impl->E(index).flags, ElementFlags::InheritsPointerState) == inherits) {
			return;
		}
		SetElementFlag(*_impl, handle, ElementFlags::InheritsPointerState, inherits, false);
		_impl->RecalcPointerState(index);
	}

	void Context::SetFocusable(ElementHandle handle, bool focusable) {
		SetElementFlag(*_impl, handle, ElementFlags::Focusable, focusable, false);
	}

	void Context::SetTint(ElementHandle handle, Color tint) {
		u32 index = _impl->ResolveIndex(handle);
		if (index != INVALID_UI_INDEX) {
			_impl->E(index).tint = tint;
		}
	}

	void Context::SetUserTag(ElementHandle handle, u64 tag) {
		u32 index = _impl->ResolveIndex(handle);
		if (index != INVALID_UI_INDEX) {
			_impl->E(index).userTag = tag;
		}
	}

	void Context::SetScrollOffset(ElementHandle handle, Vec2 offset) {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX) {
			return;
		}
		Element& element = _impl->E(index);
		Vec2 clamped = {
			std::clamp(offset.x, 0.0f, element.maxScroll.x),
			std::clamp(offset.y, 0.0f, element.maxScroll.y)};
		if (clamped.x != element.scrollOffset.x || clamped.y != element.scrollOffset.y) {
			element.scrollOffset = clamped;
			_impl->MarkLayoutDirtyUpwards(index);
		}
	}

	void Context::ScrollIntoView(ElementHandle handle) {
		_impl->ScrollIntoView(_impl->ResolveIndex(handle));
	}

	void Context::ScrollRangeIntoView(ElementHandle scroller, f32 startY, f32 endY) {
		_impl->ScrollRangeIntoView(_impl->ResolveIndex(scroller), startY, endY);
	}

	void Context::SetStyle(ElementHandle handle, const Style& style) {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX) {
			return;
		}
		if (_impl->inlineStyles[index] == style) {
			return;
		}
		_impl->inlineStyles[index] = style;
		_impl->RecalcStyle(index);
	}

	const Style& Context::GetStyle(ElementHandle handle) const {
		u32 index = _impl->ResolveIndex(handle);
		SLT_CORE_ASSERT_MSG(index != INVALID_UI_INDEX, "GetStyle on a dead element");
		return _impl->inlineStyles[index];
	}

	const Style& Context::GetComputedStyle(ElementHandle handle) const {
		u32 index = _impl->ResolveIndex(handle);
		SLT_CORE_ASSERT_MSG(index != INVALID_UI_INDEX, "GetComputedStyle on a dead element");
		return _impl->ComputedS(index);
	}

	void Context::SetStyleVar(StringHash name, std::string_view value) {
		_impl->styleVars.Set(name, value);
		_impl->RecalcAllStyles();
	}

	void Context::SetStyleVars(std::span<const StyleVarDesc> descs) {
		_impl->styleVars.Set(descs);
		_impl->RecalcAllStyles();
	}

	const StyleVars& Context::GetStyleVars() const {
		return _impl->styleVars;
	}

	Color Context::GetStyleVarColor(StringHash name, Color fallback) const {
		return _impl->styleVars.GetColor(name, fallback);
	}

	f32 Context::GetStyleVarFloat(StringHash name, f32 fallback) const {
		return _impl->styleVars.GetFloat(name, fallback);
	}

	void Context::LoadStyleSheet(std::string_view cssSource) {
		_impl->styleSheet.Parse(cssSource);
		_impl->RecalcAllStyles();
	}

	bool Context::LoadStyleSheetOnce(StringHash tag, std::string_view cssSource) {
		for (StringHash loaded : _impl->loadedSheetTags) {
			if (loaded == tag) {
				return false;
			}
		}
		_impl->loadedSheetTags.push_back(tag);
		LoadStyleSheet(cssSource);
		return true;
	}

	void Context::ClearStyleSheets() {
		_impl->styleSheet.Clear();
		_impl->loadedSheetTags.clear();
		_impl->RecalcAllStyles();
	}

	std::span<const StringHash> Context::GetLoadedSheetTags() const {
		return _impl->loadedSheetTags;
	}

	void Context::AddClass(ElementHandle handle, StringHash klass) {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX) {
			return;
		}
		Element& element = _impl->E(index);
		for (u8 i = 0; i < element.classCount; ++i) {
			if (element.classes[i] == klass) {
				return;
			}
		}
		if (element.classCount >= MAX_ELEMENT_CLASSES) {
			SLT_LOG_WARN("UI", "AddClass: element exceeds {} classes. Class dropped", MAX_ELEMENT_CLASSES);
			return;
		}
		element.classes[element.classCount++] = klass;
		_impl->BumpTreeRevision();
		_impl->RecalcStyle(index);
	}

	void Context::RemoveClass(ElementHandle handle, StringHash klass) {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX) {
			return;
		}
		Element& element = _impl->E(index);
		for (u8 i = 0; i < element.classCount; ++i) {
			if (element.classes[i] == klass) {
				element.classes[i] = element.classes[element.classCount - 1];
				element.classes[--element.classCount] = StringHash{};
				_impl->BumpTreeRevision();
				_impl->RecalcStyle(index);
				return;
			}
		}
	}

	bool Context::HasClass(ElementHandle handle, StringHash klass) const {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX) {
			return false;
		}
		const Element& element = _impl->E(index);
		for (u8 i = 0; i < element.classCount; ++i) {
			if (element.classes[i] == klass) {
				return true;
			}
		}
		return false;
	}

	void Context::SetId(ElementHandle handle, StringHash id) {
		u32 index = _impl->ResolveIndex(handle);
		if (index != INVALID_UI_INDEX) {
			_impl->E(index).id = id;
			_impl->BumpTreeRevision();
			_impl->RecalcStyle(index);
		}
	}

	void Context::SetTypeTag(ElementHandle handle, StringHash tag) {
		u32 index = _impl->ResolveIndex(handle);
		if (index != INVALID_UI_INDEX) {
			_impl->E(index).typeTag = tag;
			_impl->BumpTreeRevision();
			_impl->RecalcStyle(index);
		}
	}

	void Context::SetText(ElementHandle handle, Handle<FontAsset> font, f32 sizePx, Color color, std::string_view utf8) {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX) {
			return;
		}
		Element& element = _impl->E(index);
		if (element.textIndex == INVALID_UI_INDEX) {
			u32 slot;
			if (!_impl->textFreeList.empty()) {
				slot = _impl->textFreeList.back();
				_impl->textFreeList.pop_back();
				_impl->texts[slot] = TextComponent{};
			} else {
				slot = static_cast<u32>(_impl->texts.size());
				_impl->texts.emplace_back();
			}
			_impl->texts[slot].inUse = true;
			element.textIndex = slot;
		}
		TextComponent& text = _impl->texts[element.textIndex];
		std::string_view current(text.utf8.data(), text.utf8.size());
		bool changed = text.font != font || text.sizePx != sizePx || text.color != color || current != utf8;
		if (!changed) {
			return;
		}
		text.font = font;
		text.sizePx = sizePx;
		text.color = color;
		text.utf8.assign(utf8.begin(), utf8.end());
		text.ResetCaches();
		_impl->MarkLayoutDirtyUpwards(index);
	}

	void Context::SetTextString(ElementHandle handle, std::string_view utf8) {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX) {
			return;
		}
		TextComponent* text = _impl->GetText(index);
		if (!text) {
			SLT_LOG_WARN("UI", "SetTextString: element has no text component (SetText first)");
			return;
		}
		if (std::string_view(text->utf8.data(), text->utf8.size()) == utf8) {
			return;
		}
		text->utf8.assign(utf8.begin(), utf8.end());
		text->ResetCaches();
		if (TextInputState* input = _impl->GetTextInput(index)) {
			input->cursor = std::min(input->cursor, static_cast<i32>(text->utf8.size()));
			input->anchor = std::min(input->anchor, static_cast<i32>(text->utf8.size()));
			input->completion.clear();
		}
		_impl->MarkContentDirty(index);
	}

	str Context::GetTextString(ElementHandle handle) const {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX) {
			return {};
		}
		const TextComponent* text = _impl->GetText(index);
		return text ? str(text->utf8.data(), text->utf8.size()) : str();
	}

	void Context::ClearText(ElementHandle handle) {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX) {
			return;
		}
		Element& element = _impl->E(index);
		if (element.textIndex == INVALID_UI_INDEX) {
			return;
		}
		_impl->texts[element.textIndex] = TextComponent{};
		_impl->textFreeList.push_back(element.textIndex);
		element.textIndex = INVALID_UI_INDEX;
		_impl->MarkLayoutDirtyUpwards(index);
	}

	void Context::SetIcon(ElementHandle handle, Handle<VectorAsset> asset, f32 sizePx, const IconPalette& palette) {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX) {
			return;
		}
		if (!asset.IsValid()) {
			ClearIcon(handle);
			return;
		}
		Element& element = _impl->E(index);
		IconComponent& icon = _impl->icons[index];
		if (HasFlag(element.flags, ElementFlags::HasIcon) &&
			icon.asset == asset && icon.sizePx == sizePx && icon.palette == palette) {
			return;
		}
		icon.asset = asset;
		icon.sizePx = sizePx;
		icon.palette = palette;
		icon.cachedSizePhysical = -1.0f;
		icon.texture = {};
		element.flags &= ~ElementFlags::HasImage;
		element.flags |= ElementFlags::HasIcon;
		_impl->MarkLayoutDirtyUpwards(index);
	}

	void Context::SetIconPalette(ElementHandle handle, const IconPalette& palette) {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX) {
			return;
		}
		IconComponent* icon = _impl->GetIcon(index);
		if (!icon) {
			SLT_LOG_WARN("UI", "SetIconPalette: element has no icon (SetIcon first)");
			return;
		}
		if (icon->palette == palette) {
			return;
		}
		icon->palette = palette;
	}

	void Context::ClearIcon(ElementHandle handle) {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX) {
			return;
		}
		Element& element = _impl->E(index);
		if (!HasFlag(element.flags, ElementFlags::HasIcon)) {
			return;
		}
		_impl->icons.erase(index);
		element.flags &= ~ElementFlags::HasIcon;
		_impl->MarkLayoutDirtyUpwards(index);
	}

	bool Context::HasIcon(ElementHandle handle) const {
		u32 index = _impl->ResolveIndex(handle);
		return index != INVALID_UI_INDEX && _impl->GetIcon(index) != nullptr;
	}

	Handle<VectorAsset> Context::GetIconAsset(ElementHandle handle) const {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX) {
			return {};
		}
		const IconComponent* icon = _impl->GetIcon(index);
		return icon ? icon->asset : Handle<VectorAsset>{};
	}

	void Context::SetImage(ElementHandle handle, Handle<TextureAsset> asset, f32 sizePx) {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX) {
			return;
		}
		if (!asset.IsValid()) {
			ClearImage(handle);
			return;
		}
		Element& element = _impl->E(index);
		IconComponent& picture = _impl->icons[index];
		if (HasFlag(element.flags, ElementFlags::HasImage) && picture.texture == asset && picture.sizePx == sizePx) {
			return;
		}
		picture.texture = asset;
		picture.sizePx = sizePx;
		picture.cachedSizePhysical = -1.0f;
		picture.asset = {};
		picture.palette = {};
		element.flags &= ~ElementFlags::HasIcon;
		element.flags |= ElementFlags::HasImage;
		_impl->MarkLayoutDirtyUpwards(index);
	}

	void Context::ClearImage(ElementHandle handle) {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX) {
			return;
		}
		Element& element = _impl->E(index);
		if (!HasFlag(element.flags, ElementFlags::HasImage)) {
			return;
		}
		_impl->icons.erase(index);
		element.flags &= ~ElementFlags::HasImage;
		_impl->MarkLayoutDirtyUpwards(index);
	}

	bool Context::HasImage(ElementHandle handle) const {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX) {
			return false;
		}
		return HasFlag(_impl->E(index).flags, ElementFlags::HasImage) && _impl->GetPicture(index) != nullptr;
	}

	Handle<TextureAsset> Context::GetImageAsset(ElementHandle handle) const {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX || !HasFlag(_impl->E(index).flags, ElementFlags::HasImage)) {
			return {};
		}
		const IconComponent* picture = _impl->GetPicture(index);
		return picture ? picture->texture : Handle<TextureAsset>{};
	}

	void Context::SetTextInput(ElementHandle handle, bool enabled) {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX) {
			return;
		}
		Element& element = _impl->E(index);
		if (enabled) {
			if (element.textInputIndex != INVALID_UI_INDEX) {
				return;
			}
			u32 slot;
			if (!_impl->textInputFreeList.empty()) {
				slot = _impl->textInputFreeList.back();
				_impl->textInputFreeList.pop_back();
				_impl->textInputs[slot] = TextInputState{};
			} else {
				slot = static_cast<u32>(_impl->textInputs.size());
				_impl->textInputs.emplace_back();
			}
			_impl->textInputs[slot].inUse = true;
			element.textInputIndex = slot;
			element.flags |= ElementFlags::Focusable;
		} else {
			if (element.textInputIndex == INVALID_UI_INDEX) {
				return;
			}
			_impl->textInputs[element.textInputIndex] = TextInputState{};
			_impl->textInputFreeList.push_back(element.textInputIndex);
			element.textInputIndex = INVALID_UI_INDEX;
		}
	}

	bool Context::HasTextInput(ElementHandle handle) const {
		u32 index = _impl->ResolveIndex(handle);
		return index != INVALID_UI_INDEX && _impl->E(index).textInputIndex != INVALID_UI_INDEX;
	}

	void Context::SetTextInputMultiline(ElementHandle handle, bool multiline) {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX || _impl->E(index).textInputIndex == INVALID_UI_INDEX) {
			return;
		}
		TextInputState& input = _impl->textInputs[_impl->E(index).textInputIndex];
		if (input.multiline == multiline) {
			return;
		}
		input.multiline = multiline;
		_impl->E(index).scrollOffset = {};
		_impl->MarkLayoutDirtyUpwards(index);
	}

	void Context::SetTextInputCompletion(ElementHandle handle, std::string_view suffix, Color color) {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX || _impl->E(index).textInputIndex == INVALID_UI_INDEX) {
			return;
		}
		TextInputState& input = _impl->textInputs[_impl->E(index).textInputIndex];
		input.completion.assign(suffix.begin(), suffix.end());
		input.completionColor = color;
	}

	Context::ShapedTextView Context::GetShapedText(ElementHandle handle) const {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX) {
			return {};
		}
		const TextComponent* text = _impl->GetText(index);
		if (!text || text->shapedWrapWidth < 0.0f) {
			return {};
		}
		ShapedTextView view;
		view.run = &text->shaped;
		view.originPx = _impl->TextOriginPx(index);
		return view;
	}

	void Context::SetTooltip(ElementHandle handle, std::string_view text) {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX) {
			return;
		}
		if (text.empty()) {
			_impl->tooltips.erase(index);
		} else {
			_impl->tooltips[index] = String(text, GetMemoryResource(MemTag::UI));
		}
		if (_impl->tooltipSource == index) {
			_impl->tooltipResolved = false;
		}
	}

	bool Context::HasEvents(ElementHandle handle) const {
		u32 index = _impl->ResolveIndex(handle);
		return index != INVALID_UI_INDEX && _impl->events.contains(index);
	}

	ElementEvents& Context::Events(ElementHandle handle) {
		u32 index = _impl->ResolveIndex(handle);
		SLT_CORE_ASSERT_MSG(index != INVALID_UI_INDEX, "Events on a dead element");
		_impl->InvalidatePaintInkUpwards(index);
		return _impl->events[index];
	}

	void Context::OpenPopup(ElementHandle content, Vec2 anchorPx, PopupFlags flags, Rect excludePx) {
		u32 index = _impl->ResolveIndex(content);
		if (index == INVALID_UI_INDEX || index == _impl->root) {
			return;
		}
		if (_impl->IsPopupIndex(index)) {
			return;
		}
		f32 scale = std::max(0.001f, _impl->desc.contentScale);
		Style style = _impl->inlineStyles[index];
		style.position = Position::Absolute;
		style.left = Length::Pixel(anchorPx.x / scale);
		style.top = Length::Pixel(anchorPx.y / scale);
		style.zIndex = POPUP_Z_BASE + static_cast<i32>(_impl->popups.size()) * 10;
		SetStyle(content, style);
		AppendChild(GetRoot(), content);
		ContextImpl::PopupEntry entry{index, flags};
		entry.anchor = anchorPx;
		entry.focusBefore = _impl->HandleFor(_impl->focused);
		if (excludePx.w > 0.0f && excludePx.h > 0.0f) {
			entry.exclude = excludePx;
			entry.side = anchorPx.x >= excludePx.x + excludePx.w   ? ContextImpl::PopupSide::Right
						 : anchorPx.y >= excludePx.y + excludePx.h ? ContextImpl::PopupSide::Below
																   : ContextImpl::PopupSide::None;
		}
		_impl->popups.push_back(entry);
		_impl->UpdatePopupScrim();
	}

	void Context::ClosePopup(ElementHandle content) {
		u32 index = _impl->ResolveIndex(content);
		for (size_t i = 0; i < _impl->popups.size(); ++i) {
			if (_impl->popups[i].index == index) {
				_impl->ClosePopupsFrom(i);
				return;
			}
		}
	}

	void Context::CloseAllPopups() {
		_impl->ClosePopupsFrom(0);
	}

	bool Context::IsPopupOpen(ElementHandle content) const {
		u32 index = _impl->ResolveIndex(content);
		return index != INVALID_UI_INDEX && _impl->IsPopupIndex(index);
	}

	u32 Context::GetOpenPopupCount() const {
		return static_cast<u32>(_impl->popups.size());
	}

	ElementHandle Context::GetOpenPopup(u32 index) const {
		if (index >= _impl->popups.size()) {
			return {};
		}
		return _impl->HandleFor(_impl->popups[index].index);
	}

	void Context::SetPopupDefaultButton(ElementHandle content, ElementHandle button) {
		u32 contentIndex = _impl->ResolveIndex(content);
		if (contentIndex == INVALID_UI_INDEX) {
			return;
		}
		u32 buttonIndex = _impl->ResolveIndex(button);
		if (buttonIndex == INVALID_UI_INDEX) {
			_impl->popupDefaultButtons.erase(contentIndex);
			return;
		}
		_impl->popupDefaultButtons[contentIndex] = buttonIndex;
	}

	void Context::InjectInput(const InputEvent& event) {
		if (event.type == InputEventType::MouseMove && !_impl->pendingInput.empty() &&
			_impl->pendingInput.back().type == InputEventType::MouseMove) {
			_impl->pendingInput.back() = event;
			return;
		}
		_impl->pendingInput.push_back(event);
	}

	ElementHandle Context::GetFocused() const {
		return _impl->HandleFor(_impl->focused);
	}

	ElementHandle Context::GetHovered() const {
		return _impl->HandleFor(_impl->hovered);
	}

	void Context::ClearFocus() {
		_impl->SetFocused(INVALID_UI_INDEX);
	}

	void Context::SetFocus(ElementHandle handle) {
		u32 index = _impl->ResolveIndex(handle);
		if (index == INVALID_UI_INDEX) {
			_impl->SetFocused(INVALID_UI_INDEX);
			return;
		}
		if (!HasFlag(_impl->E(index).flags, ElementFlags::Focusable)) {
			return;
		}
		_impl->SetFocused(index);
	}

	u32 Context::ClaimPointer(ElementHandle owner, CursorMode mode) {
		if (_impl->pointerClaim != 0) {
			return 0;
		}
		if (!_impl->IsAliveHandle(owner)) {
			return 0;
		}
		_impl->pointerClaim = _impl->nextPointerClaim++;
		_impl->pointerClaimOwner = owner;
		_impl->pointerClaimMode = mode;
		_impl->SetHovered(INVALID_UI_INDEX);
		return _impl->pointerClaim;
	}

	void Context::ReleasePointer(u32 claim) {
		if (claim == 0 || claim != _impl->pointerClaim) {
			return;
		}
		_impl->ClearPointerClaim();
	}

	CursorMode Context::GetPointerClaimMode() const {
		return _impl->pointerClaimMode;
	}

	void Context::SetHoverSuppressed(bool suppressed) {
		if (_impl->hoverSuppressed == suppressed) {
			return;
		}
		_impl->hoverSuppressed = suppressed;
		if (suppressed) {
			_impl->SetHovered(INVALID_UI_INDEX);
		}
	}

	bool Context::IsHoverSuppressed() const {
		return _impl->hoverSuppressed;
	}

	KeyMod Context::GetPointerMods() const {
		return _impl->lastMouseMods;
	}

	KeyMod Context::GetPressMods() const {
		return _impl->pressMods;
	}

	Vec2 Context::GetPointerPosition() const {
		return _impl->lastMousePos;
	}

	bool Context::WantsMouse() const {
		return (_impl->hovered != INVALID_UI_INDEX && _impl->hovered != _impl->root) ||
			   _impl->pressed != INVALID_UI_INDEX;
	}

	bool Context::WantsKeyboard() const {
		return _impl->focused != INVALID_UI_INDEX &&
			   _impl->E(_impl->focused).textInputIndex != INVALID_UI_INDEX;
	}

	SystemCursor Context::GetDesiredCursor() const {
		return _impl->desiredCursor;
	}

	void Context::Update() {
		RouteInput();
		SettleLayout();
	}

	void Context::RouteInput() {
		SLT_PROFILE_ZONE("UI.Route");
		{
			SLT_PROFILE_ZONE("UI.Drain");
			if (!_impl->pendingDestroy.empty()) {
				Vector<ElementHandle> doomed{GetMemoryResource(MemTag::UI)};
				doomed.swap(_impl->pendingDestroy);
				for (ElementHandle handle : doomed) {
					DestroyElement(handle);
				}
			}
			_impl->ExpireDeadPointerClaim();
		}
		{
			SLT_PROFILE_ZONE("UI.Input");
			_impl->ProcessInput();
		}
	}

	void Context::SettleLayout() {
		SLT_PROFILE_ZONE("UI.Settle");
		{
			SLT_PROFILE_ZONE("UI.ContentDirty");
			_impl->ResolveContentDirty();
		}
		bool boxesMoved = false;
		if (_impl->layoutDirty) {
			Layout::UpdateLayout(*_impl);
			_impl->layoutDirty = false;
			boxesMoved = true;
		}
		{
			SLT_PROFILE_ZONE("UI.ScrollBand");
			for (u32 round = 0; round < 2 && _impl->NotifyScrollMoved(); ++round) {
				Layout::UpdateLayout(*_impl);
				_impl->layoutDirty = false;
				boxesMoved = true;
			}
		}
		bool popupsMoved;
		{
			SLT_PROFILE_ZONE("UI.Popups");
			popupsMoved = _impl->PositionPopups();
		}
		if (popupsMoved) {
			Layout::UpdateLayout(*_impl);
			_impl->layoutDirty = false;
			boxesMoved = true;
		}
		if (boxesMoved) {
			{
				SLT_PROFILE_ZONE("UI.Hover");
				_impl->ResolveHoverFromCachedPointer();
			}
			if (_impl->layoutDirty) {
				Layout::UpdateLayout(*_impl);
				_impl->layoutDirty = false;
			}
		}
		{
			SLT_PROFILE_ZONE("UI.SettleTail");
			_impl->ResolveDesiredCursor();
			if (_impl->ResolveTooltip()) {
				Layout::UpdateLayout(*_impl);
				_impl->layoutDirty = false;
			}
			if (_impl->PlaceTooltip()) {
				Layout::UpdateLayout(*_impl);
				_impl->layoutDirty = false;
			}
			_impl->FollowCaret();
		}
	}

	void Context::BuildRenderList(RenderList& out) {
		Paint::BuildRenderList(*_impl, out);
	}

	ElementHandle Context::HitTest(Vec2 point, HitTestMode mode) const {
		u32 found = _impl->HitTestIndex(point, mode);
		return _impl->HandleFor(found);
	}

	ElementHandle Context::UpdateDragTarget(const DragPayload& payload, Vec2 point) {
		u32 hit = _impl->HitTestIndex(point, HitTestMode::Box);
		u32 accepted = INVALID_UI_INDEX;
		for (u32 walk = hit; walk != INVALID_UI_INDEX; walk = _impl->E(walk).parent) {
			auto eventsIt = _impl->events.find(walk);
			if (eventsIt != _impl->events.end() && eventsIt->second.onDragOver) {
				if (eventsIt->second.onDragOver(payload)) {
					accepted = walk;
				}
				break;
			}
		}
		_impl->SetDragTarget(accepted);
		return _impl->HandleFor(accepted);
	}

	void Context::ClearDragTarget() {
		_impl->SetDragTarget(INVALID_UI_INDEX);
	}

	bool Context::DeliverDrop(const DragPayload& payload, Vec2 point) {
		ElementHandle target = UpdateDragTarget(payload, point);
		_impl->SetDragTarget(INVALID_UI_INDEX);
		u32 index = _impl->ResolveIndex(target);
		if (index == INVALID_UI_INDEX) {
			return false;
		}
		auto eventsIt = _impl->events.find(index);
		if (eventsIt != _impl->events.end() && eventsIt->second.onDrop) {
			eventsIt->second.onDrop(payload, point);
			return true;
		}
		return false;
	}

	void Context::Resize(Vec2I sizePx) {
		if (_impl->desc.sizePx == sizePx) {
			return;
		}
		_impl->desc.sizePx = sizePx;
		_impl->MarkLayoutDirtyUpwards(_impl->root);
	}

	void Context::SetContentScale(f32 scale) {
		if (_impl->desc.contentScale == scale) {
			return;
		}
		_impl->desc.contentScale = scale;
		for (TextComponent& text : _impl->texts) {
			if (text.inUse) {
				text.ResetCaches();
			}
		}
		for (u32 index = 0; index < _impl->elements.size(); ++index) {
			_impl->MarkLayoutDirty(index);
		}
		_impl->layoutDirty = true;
	}

	Vec2I Context::GetSize() const {
		return _impl->desc.sizePx;
	}

	f32 Context::GetContentScale() const {
		return _impl->desc.contentScale;
	}

	const LayoutData& Context::GetLayout(ElementHandle handle) const {
		u32 index = _impl->ResolveIndex(handle);
		SLT_CORE_ASSERT_MSG(index != INVALID_UI_INDEX, "GetLayout on a dead element");
		return _impl->layouts[index];
	}

	const LayoutStats& Context::GetLayoutStats() const {
		return _impl->layoutStats;
	}

	void Context::ResetLayoutStats() {
		_impl->layoutStats = {};
	}

	const PaintStats& Context::GetPaintStats() const {
		return _impl->paintStats;
	}

	void Context::ResetPaintStats() {
		_impl->paintStats = {};
	}

	const TreeStats& Context::GetTreeStats() const {
		return _impl->treeStats;
	}

	void Context::ResetTreeStats() {
		_impl->treeStats = {};
	}

	const StyleStats& Context::GetStyleStats() const {
		return _impl->styleStats;
	}

	void Context::ResetStyleStats() {
		_impl->styleStats = {};
	}

	IFontSystem* Context::GetFontSystem() const {
		return _impl->fontSystem;
	}

} // namespace Shalltear::UI
