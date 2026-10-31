#include "UI/UIInternal.h"

#include "Shalltear/Core/Time.h"
#include "Shalltear/Core/Window/WindowSystem.h"

#include <algorithm>

namespace Shalltear::UI {

	namespace {

		bool IsUTF8Start(char c) {
			return (c & 0xC0) != 0x80;
		}

		u32 UTF8Prev(const String& text, u32 i) {
			if (i == 0) {
				return 0;
			}
			--i;
			while (i > 0 && !IsUTF8Start(text[i])) {
				--i;
			}
			return i;
		}

		u32 UTF8Next(const String& text, u32 i) {
			if (i >= text.size()) {
				return static_cast<u32>(text.size());
			}
			++i;
			while (i < text.size() && !IsUTF8Start(text[i])) {
				++i;
			}
			return i;
		}

		void UTF8Append(String& out, char32_t c) {
			if (c < 0x80) {
				out += static_cast<char>(c);
			} else if (c < 0x800) {
				out += static_cast<char>(0xC0 | (c >> 6));
				out += static_cast<char>(0x80 | (c & 0x3F));
			} else if (c < 0x10000) {
				out += static_cast<char>(0xE0 | (c >> 12));
				out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (c & 0x3F));
			} else {
				out += static_cast<char>(0xF0 | (c >> 18));
				out += static_cast<char>(0x80 | ((c >> 12) & 0x3F));
				out += static_cast<char>(0x80 | ((c >> 6) & 0x3F));
				out += static_cast<char>(0x80 | (c & 0x3F));
			}
		}

		constexpr f32 SCROLL_SHOW_MARGIN = 1.0f;

		bool PointInRoundedRect(Vec2 p, const Rect& r, const Radii& radii) {
			if (radii.IsZero()) {
				return r.Contains(p);
			}
			if (!r.Contains(p)) {
				return false;
			}
			auto inCorner = [&](f32 cx, f32 cy, f32 radius) {
				if (radius <= 0.0f) {
					return true;
				}
				f32 dx = p.x - cx;
				f32 dy = p.y - cy;
				return dx * dx + dy * dy <= radius * radius;
			};
			if (p.x < r.x + radii.topLeft && p.y < r.y + radii.topLeft) {
				return inCorner(r.x + radii.topLeft, r.y + radii.topLeft, radii.topLeft);
			}
			if (p.x > r.x + r.w - radii.topRight && p.y < r.y + radii.topRight) {
				return inCorner(r.x + r.w - radii.topRight, r.y + radii.topRight, radii.topRight);
			}
			if (p.x > r.x + r.w - radii.bottomRight && p.y > r.y + r.h - radii.bottomRight) {
				return inCorner(r.x + r.w - radii.bottomRight, r.y + r.h - radii.bottomRight, radii.bottomRight);
			}
			if (p.x < r.x + radii.bottomLeft && p.y > r.y + r.h - radii.bottomLeft) {
				return inCorner(r.x + radii.bottomLeft, r.y + r.h - radii.bottomLeft, radii.bottomLeft);
			}
			return true;
		}

	} // anonymous namespace

	u32 ContextImpl::HitTestRecursive(u32 index, Vec2 point, HitTestMode mode, const Rect* clip) const {
		const Element& element = E(index);
		const Style& style = ComputedS(index);
		if (!HasFlag(element.flags, ElementFlags::Visible) || style.display == Display::None) {
			return INVALID_UI_INDEX;
		}

		const Rect& bounds = L(index).borderBox;

		Rect narrowed{};
		const Rect* effectiveClip = clip;
		if (style.overflow != Overflow::Visible) {
			narrowed = clip ? RectIntersect(*clip, bounds) : bounds;
			if (narrowed.w <= 0.0f || narrowed.h <= 0.0f) {
				return INVALID_UI_INDEX;
			}
			effectiveClip = &narrowed;
		}

		bool selfHittable;
		if (clip) {
			selfHittable = RectIntersect(*clip, bounds).Contains(point);
		} else {
			selfHittable = bounds.Contains(point);
		}

		u32 found = INVALID_UI_INDEX;
		for (u32 child = element.firstChild; child != INVALID_UI_INDEX; child = E(child).nextSibling) {
			u32 hit = HitTestRecursive(child, point, mode, effectiveClip);
			if (hit != INVALID_UI_INDEX) {
				found = hit;
			}
		}
		if (found != INVALID_UI_INDEX) {
			return found;
		}
		if (!selfHittable) {
			return INVALID_UI_INDEX;
		}

		if (style.pointerEvents && HasFlag(element.flags, ElementFlags::Hoverable)) {
			bool inside;
			if (mode == HitTestMode::Box) {
				inside = bounds.Contains(point);
			} else {
				f32 scale = std::max(0.0f, desc.contentScale);
				Radii physical = {
					style.borderRadius.topLeft * scale,
					style.borderRadius.topRight * scale,
					style.borderRadius.bottomRight * scale,
					style.borderRadius.bottomLeft * scale};
				inside = PointInRoundedRect(point, bounds, physical);
			}
			if (inside) {
				return index;
			}
		}
		return INVALID_UI_INDEX;
	}

	u32 ContextImpl::HitTestIndex(Vec2 point, HitTestMode mode) const {
		for (size_t i = popups.size(); i-- > 0;) {
			u32 hit = HitTestRecursive(popups[i].index, point, mode, nullptr);
			if (hit != INVALID_UI_INDEX) {
				return hit;
			}
		}
		if (popupScrim != INVALID_UI_INDEX && HasFlag(E(popupScrim).flags, ElementFlags::Visible) &&
			L(popupScrim).borderBox.Contains(point)) {
			return popupScrim;
		}
		return HitTestRecursive(root, point, mode, nullptr);
	}

	u32 ContextImpl::CaptureOwner() const {
		if (pressed == INVALID_UI_INDEX) {
			return INVALID_UI_INDEX;
		}
		auto eventsIt = events.find(pressed);
		if (eventsIt == events.end() || !eventsIt->second.onPressDrag) {
			return INVALID_UI_INDEX;
		}
		return pressed;
	}

	void ContextImpl::ClearPointerClaim() {
		pointerClaim = 0;
		pointerClaimOwner = {};
		pointerClaimMode = CursorMode::Normal;
	}

	void ContextImpl::ExpireDeadPointerClaim() {
		if (pointerClaim != 0 && !IsAliveHandle(pointerClaimOwner)) {
			ClearPointerClaim();
		}
	}

	void ContextImpl::SetHovered(u32 index) {
		if (hoverSuppressed || pointerClaim != 0) {
			index = INVALID_UI_INDEX;
		}
		u32 capture = CaptureOwner();
		if (capture != INVALID_UI_INDEX && index != capture) {
			index = INVALID_UI_INDEX;
		}
		if (index == hovered) {
			return;
		}
		u32 previous = hovered;
		hovered = index;
		if (previous != INVALID_UI_INDEX) {
			E(previous).flags &= ~ElementFlags::Hovered;
			RecalcPointerState(previous);
			auto eventsIt = events.find(previous);
			if (eventsIt != events.end() && eventsIt->second.onHoverLeave) {
				eventsIt->second.onHoverLeave();
			}
		}
		if (index != INVALID_UI_INDEX) {
			E(index).flags |= ElementFlags::Hovered;
			RecalcPointerState(index);
			auto eventsIt = events.find(index);
			if (eventsIt != events.end() && eventsIt->second.onHoverEnter) {
				eventsIt->second.onHoverEnter();
			}
		}
	}

	void ContextImpl::SetFocused(u32 index) {
		if (index == focused) {
			return;
		}
		u32 previous = focused;
		focused = index;
		if (previous != INVALID_UI_INDEX) {
			E(previous).flags &= ~ElementFlags::Focused;
			RecalcStyle(previous);
			if (E(previous).textInputIndex != INVALID_UI_INDEX) {
				E(previous).scrollOffset.x = 0.0f;
			}
			auto eventsIt = events.find(previous);
			if (eventsIt != events.end() && eventsIt->second.onFocusLost) {
				eventsIt->second.onFocusLost();
			}
		}
		if (index != INVALID_UI_INDEX) {
			E(index).flags |= ElementFlags::Focused;
			RecalcStyle(index);

			if (E(index).textInputIndex != INVALID_UI_INDEX) {
				TextInputState* input = GetTextInput(index);
				const TextComponent* text = GetText(index);
				if (input && text) {
					input->preFocus = text->utf8;
					input->anchor = 0;
					input->cursor = static_cast<i32>(text->utf8.size());
				}
				caretBlinkStart = Time::GetRealtimeSinceStartup();
			}
			auto eventsIt = events.find(index);
			if (eventsIt != events.end() && eventsIt->second.onFocusGained) {
				eventsIt->second.onFocusGained();
			}
		}
	}

	i32 ContextImpl::CaretIndexFromMouse(u32 elementIndex, Vec2 mouse) {
		TextComponent* text = GetText(elementIndex);
		if (!text || !fontSystem) {
			return 0;
		}
		EnsureShaped(elementIndex);
		const Vec2 origin = TextOriginPx(elementIndex);
		const ShapedRun& run = text->shaped;
		const f32 lineHeight = run.lineHeight > 0.0f ? run.lineHeight : 1.0f;
		const f32 below = (mouse.y - origin.y) / lineHeight;
		const u32 line = below > 0.0f ? static_cast<u32>(below) : 0u;
		return static_cast<i32>(run.ByteForCaret(mouse.x - origin.x, line));
	}

	ContextImpl::CaretMark ContextImpl::MarkCaret() {
		CaretMark mark;
		if (focused >= elements.size() || E(focused).textInputIndex == INVALID_UI_INDEX) {
			return mark;
		}
		const TextInputState* input = GetTextInput(focused);
		const TextComponent* text = GetText(focused);
		if (!input || !text) {
			return mark;
		}
		mark.element = focused;
		mark.cursor = input->cursor;
		mark.anchor = input->anchor;
		mark.length = text->utf8.size();
		return mark;
	}

	void ContextImpl::RestartBlinkIfCaretMoved(const CaretMark& before) {
		const CaretMark after = MarkCaret();
		if (after.element == INVALID_UI_INDEX) {
			return;
		}
		if (after.element != before.element || after.cursor != before.cursor || after.anchor != before.anchor ||
			after.length != before.length) {
			caretBlinkStart = Time::GetRealtimeSinceStartup();
		}
	}

	f32 ContextImpl::ScrollAxisToShow(f32 offset, f32 windowSize, f32 targetMin, f32 targetMax, f32 maxOffset) {
		if (windowSize <= 0.0f) {
			return offset;
		}
		const f32 visibleEnd = offset + windowSize - SCROLL_SHOW_MARGIN;
		if (targetMin < offset) {
			offset = targetMin;
		} else if (targetMax > visibleEnd) {
			offset = targetMax - windowSize + SCROLL_SHOW_MARGIN;
		}
		return std::clamp(offset, 0.0f, std::max(0.0f, maxOffset));
	}

	void ContextImpl::ScrollIntoView(u32 index) {
		if (index == INVALID_UI_INDEX) {
			return;
		}
		for (u32 scroller = E(index).parent; scroller != INVALID_UI_INDEX; scroller = E(scroller).parent) {
			if (ComputedS(scroller).overflow != Overflow::Scroll) {
				continue;
			}
			Element& element = E(scroller);
			const Rect& window = L(scroller).content;
			const Rect& target = L(index).borderBox;
			const f32 targetX = target.x - window.x + element.scrollOffset.x;
			const f32 targetY = target.y - window.y + element.scrollOffset.y;
			Vec2 wanted = {
				ScrollAxisToShow(element.scrollOffset.x, window.w, targetX, targetX + target.w, element.maxScroll.x),
				ScrollAxisToShow(element.scrollOffset.y, window.h, targetY, targetY + target.h, element.maxScroll.y)};
			if (wanted.x != element.scrollOffset.x || wanted.y != element.scrollOffset.y) {
				element.scrollOffset = wanted;
				MarkLayoutDirtyUpwards(scroller);
			}
			return;
		}
	}

	void ContextImpl::ScrollRangeIntoView(u32 index, f32 startY, f32 endY) {
		if (index == INVALID_UI_INDEX || ComputedS(index).overflow != Overflow::Scroll) {
			return;
		}
		Element& element = E(index);
		const f32 wanted =
			ScrollAxisToShow(element.scrollOffset.y, L(index).content.h, startY, endY, element.maxScroll.y);
		if (wanted != element.scrollOffset.y) {
			element.scrollOffset.y = wanted;
			MarkLayoutDirtyUpwards(index);
		}
	}

	void ContextImpl::FollowCaret() {
		if (focused == INVALID_UI_INDEX) {
			return;
		}
		Element& element = E(focused);
		if (element.textInputIndex == INVALID_UI_INDEX || element.textIndex == INVALID_UI_INDEX) {
			return;
		}
		TextInputState* input = GetTextInput(focused);
		TextComponent* text = GetText(focused);
		if (!input || !text || !fontSystem) {
			return;
		}
		EnsureShaped(focused);
		const Rect& content = L(focused).content;
		if (input->multiline) {
			const ShapedRun& run = text->shaped;
			const f32 top = static_cast<f32>(run.StopForByte(static_cast<u32>(input->cursor)).line) * run.lineHeight;
			element.scrollOffset.x = 0.0f;
			element.scrollOffset.y = ScrollAxisToShow(element.scrollOffset.y, content.h, top,
													  top + run.lineHeight - SCROLL_SHOW_MARGIN, run.extent.y - content.h);
			return;
		}
		f32 caretX = text->shaped.CaretXForByte(static_cast<u32>(input->cursor));
		element.scrollOffset.x = ScrollAxisToShow(element.scrollOffset.x, content.w, caretX, caretX,
												  text->shaped.extent.x - content.w + SCROLL_SHOW_MARGIN);
	}

	bool ContextImpl::HandlePopupPress(Vec2 point) {
		if (popups.empty()) {
			return false;
		}
		for (size_t i = popups.size(); i-- > 0;) {
			if (L(popups[i].index).borderBox.Contains(point)) {
				ClosePopupsFrom(i + 1);
				return false;
			}
		}
		return CloseDismissibleTop();
	}

	bool ContextImpl::HandlePopupWheel(Vec2 point) {
		for (const PopupEntry& popup : popups) {
			if (L(popup.index).borderBox.Contains(point)) {
				return false;
			}
		}
		return CloseDismissibleTop();
	}

	bool ContextImpl::CloseDismissibleTop() {
		size_t firstClosed = popups.size();
		while (firstClosed > 0 && !HasFlag(popups[firstClosed - 1].flags, PopupFlags::NoOutsideDismiss)) {
			--firstClosed;
		}
		if (firstClosed == popups.size()) {
			return false;
		}
		ClosePopupsFrom(firstClosed);
		return true;
	}

	bool ContextImpl::PressPopupDefaultButton() {
		if (popups.empty()) {
			return false;
		}
		auto defaultIt = popupDefaultButtons.find(popups.back().index);
		if (defaultIt == popupDefaultButtons.end()) {
			return false;
		}
		const u32 button = defaultIt->second;
		if (button >= elements.size() || !HasFlag(E(button).flags, ElementFlags::Visible)) {
			return false;
		}
		auto eventsIt = events.find(button);
		if (eventsIt == events.end() || !eventsIt->second.onClick) {
			return false;
		}
		std::function<void()> click = eventsIt->second.onClick;
		click();
		return true;
	}

	void ContextImpl::ResolveHoverFromCachedPointer() {
		if (!mouseInside) {
			return;
		}
		SetHovered(HitTestIndex(lastMousePos, HitTestMode::Box));
	}

	void ContextImpl::ResolveDesiredCursor() {
		if (pressed != INVALID_UI_INDEX) {
			desiredCursor = ComputedS(pressed).cursor;
		} else if (hovered != INVALID_UI_INDEX) {
			desiredCursor = ComputedS(hovered).cursor;
		} else {
			desiredCursor = SystemCursor::Arrow;
		}
	}

	void ContextImpl::ProcessInput() {
		for (const InputEvent& event : pendingInput) {
			const CaretMark caretBefore = MarkCaret();
			switch (event.type) {
			case InputEventType::MouseMove: {
				lastMousePos = {static_cast<f32>(event.mousePosPx.x), static_cast<f32>(event.mousePosPx.y)};
				lastMouseMods = event.mods;
				mouseInside = true;

				if (pressed != INVALID_UI_INDEX && pressed == focused &&
					E(pressed).textInputIndex != INVALID_UI_INDEX) {
					if (TextInputState* input = GetTextInput(pressed)) {
						i32 caret = CaretIndexFromMouse(pressed, lastMousePos);
						if (!pressSelectedAll || caret != pressCaret) {
							if (pressSelectedAll) {
								input->anchor = pressCaret;
								pressSelectedAll = false;
							}
							input->cursor = caret;
						}
					}
				}
				if (pressed != INVALID_UI_INDEX) {
					auto eventsIt = events.find(pressed);
					if (eventsIt != events.end() && eventsIt->second.onPressDrag) {
						eventsIt->second.onPressDrag(lastMousePos);
					}
				}
				if (pressedMiddle != INVALID_UI_INDEX) {
					const Vec2 delta{lastMousePos.x - middleDragLast.x, lastMousePos.y - middleDragLast.y};
					middleDragLast = lastMousePos;
					auto eventsIt = events.find(pressedMiddle);
					if (eventsIt != events.end() && eventsIt->second.onMiddleDrag) {
						eventsIt->second.onMiddleDrag(delta);
					}
				}
				SetHovered(HitTestIndex(lastMousePos, HitTestMode::Box));
				break;
			}
			case InputEventType::MouseLeave: {
				mouseInside = false;
				SetHovered(INVALID_UI_INDEX);
				break;
			}
			case InputEventType::MouseEnter: {
				mouseInside = true;
				break;
			}
			case InputEventType::MouseDown: {
				lastMousePos = {static_cast<f32>(event.mousePosPx.x), static_cast<f32>(event.mousePosPx.y)};
				lastMouseMods = event.mods;
				if (CaptureOwner() != INVALID_UI_INDEX || pointerClaim != 0) {
					break;
				}
				if (HandlePopupPress(lastMousePos)) {
					break;
				}
				if (event.button == MouseButton::Right) {
					pressedRight = HitTestIndex(lastMousePos, HitTestMode::Box);
					break;
				}
				pressSelectedAll = false;
				if (event.button == MouseButton::Middle) {
					pressedMiddle = HitTestIndex(lastMousePos, HitTestMode::Box);
					middleDragLast = lastMousePos;
					break;
				}
				if (event.button != MouseButton::Left) {
					break;
				}
				pressMods = event.mods;
				u32 hit = HitTestIndex(lastMousePos, HitTestMode::Box);
				if (hit != INVALID_UI_INDEX && hit == popupScrim) {
					break;
				}

				auto now = std::chrono::steady_clock::now();
				f32 clickDx = lastMousePos.x - lastClickPos.x;
				f32 clickDy = lastMousePos.y - lastClickPos.y;
				f32 maxDist = desc.doubleClickDistancePx * std::max(1.0f, desc.contentScale);
				bool streak = hit != INVALID_UI_INDEX && hit == lastClickIndex &&
							  std::chrono::duration<f32>(now - lastClickTime).count() <= desc.doubleClickSeconds &&
							  clickDx * clickDx + clickDy * clickDy <= maxDist * maxDist;
				clickStreak = streak ? clickStreak + 1 : 1;
				lastClickIndex = hit;
				lastClickPos = lastMousePos;
				lastClickTime = now;

				if (hit != INVALID_UI_INDEX) {
					pressed = hit;
					E(hit).flags |= ElementFlags::Pressed;
					RecalcPointerState(hit);

					auto eventsIt = events.find(hit);
					if (eventsIt != events.end() && eventsIt->second.onMouseDown) {
						eventsIt->second.onMouseDown();
					}
				}

				u32 focusTarget = INVALID_UI_INDEX;
				for (u32 walk = hit; walk != INVALID_UI_INDEX; walk = E(walk).parent) {
					if (HasFlag(E(walk).flags, ElementFlags::Focusable)) {
						focusTarget = walk;
						break;
					}
				}
				if (focusTarget != INVALID_UI_INDEX) {
					const bool alreadyFocused = focused == focusTarget;
					SetFocused(focusTarget);
					if (E(focusTarget).textInputIndex != INVALID_UI_INDEX) {
						if (TextInputState* input = GetTextInput(focusTarget)) {
							i32 caret = CaretIndexFromMouse(focusTarget, lastMousePos);
							input->preferredX = -1.0f;
							pressCaret = caret;
							pressSelectedAll = !alreadyFocused && !HasFlag(event.mods, KeyMod::Shift);
							if (!pressSelectedAll) {
								input->cursor = caret;
								if (!HasFlag(event.mods, KeyMod::Shift)) {
									input->anchor = caret;
								}
							}
						}
					}
				} else {
					SetFocused(INVALID_UI_INDEX);
					const size_t holder = PopupContaining(hit);
					if (holder < popups.size()) {
						popups[holder].focusDroppedInside = true;
					} else {
						for (PopupEntry& popup : popups) {
							popup.focusDroppedInside = false;
						}
					}
				}
				break;
			}
			case InputEventType::MouseUp: {
				u32 capture = CaptureOwner();
				if (event.button == MouseButton::Right) {
					if (capture != INVALID_UI_INDEX) {
						break;
					}
					lastMousePos = {static_cast<f32>(event.mousePosPx.x), static_cast<f32>(event.mousePosPx.y)};
					u32 hit = HitTestIndex(lastMousePos, HitTestMode::Box);
					if (hit != INVALID_UI_INDEX && hit == pressedRight) {
						auto eventsIt = events.find(hit);
						if (eventsIt != events.end() && eventsIt->second.onRightClick) {
							eventsIt->second.onRightClick();
						}
					}
					pressedRight = INVALID_UI_INDEX;
					break;
				}
				if (event.button == MouseButton::Middle) {
					pressedMiddle = INVALID_UI_INDEX;
					break;
				}
				if (event.button != MouseButton::Left) {
					break;
				}
				lastMousePos = {static_cast<f32>(event.mousePosPx.x), static_cast<f32>(event.mousePosPx.y)};
				lastMouseMods = event.mods;
				u32 hit = HitTestIndex(lastMousePos, HitTestMode::Box);
				u32 deliverTo = (capture != INVALID_UI_INDEX && hit != capture) ? INVALID_UI_INDEX : hit;
				if (deliverTo != INVALID_UI_INDEX) {
					auto eventsIt = events.find(deliverTo);
					if (eventsIt != events.end()) {
						if (eventsIt->second.onMouseUp) {
							eventsIt->second.onMouseUp();
						}
						if (deliverTo == pressed) {
							if (eventsIt->second.onClick) {
								eventsIt->second.onClick();
							}
							if (clickStreak == 2 && eventsIt->second.onDoubleClick) {
								eventsIt->second.onDoubleClick();
								clickStreak = 0;
								lastClickIndex = INVALID_UI_INDEX;
							}
						}
					}
				}
				if (pressed != INVALID_UI_INDEX) {
					auto pressedIt = events.find(pressed);
					if (pressedIt != events.end() && pressedIt->second.onPressEnd) {
						pressedIt->second.onPressEnd();
					}
					if (pressed != INVALID_UI_INDEX) {
						E(pressed).flags &= ~ElementFlags::Pressed;
						RecalcPointerState(pressed);
						pressed = INVALID_UI_INDEX;
					}
				}
				SetHovered(hit);
				break;
			}
			case InputEventType::MouseWheel: {
				if (!mouseInside || pointerClaim != 0) {
					break;
				}
				if (CaptureOwner() == INVALID_UI_INDEX && HandlePopupWheel(lastMousePos)) {
					break;
				}
				u32 capture = CaptureOwner();
				u32 target = capture != INVALID_UI_INDEX ? capture : HitTestIndex(lastMousePos, HitTestMode::Box);
				constexpr f32 SCROLL_SPEED = 48.0f;
				while (target != INVALID_UI_INDEX) {
					Element& element = E(target);
					{
						auto eventsIt = events.find(target);
						if (eventsIt != events.end() && eventsIt->second.onWheel &&
							eventsIt->second.onWheel(event.wheel)) {
							break;
						}
					}
					if (ComputedS(target).overflow == Overflow::Scroll) {
						f32 wheelX = event.wheel.x;
						f32 wheelY = event.wheel.y;
						if (wheelX == 0.0f && element.maxScroll.x > 0.0f && element.maxScroll.y <= 0.0f) {
							wheelX = wheelY;
							wheelY = 0.0f;
						}
						element.scrollOffset.x = std::clamp(element.scrollOffset.x - wheelX * SCROLL_SPEED, 0.0f, element.maxScroll.x);
						element.scrollOffset.y = std::clamp(element.scrollOffset.y - wheelY * SCROLL_SPEED, 0.0f, element.maxScroll.y);
						MarkLayoutDirtyUpwards(target);
						break;
					}
					target = element.parent;
				}
				break;
			}
			case InputEventType::KeyDown: {
				if (focused == INVALID_UI_INDEX || IsBehindPopupScrim(focused)) {
					if (event.key == Key::Escape && !popups.empty()) {
						ClosePopupsFrom(popups.size() - 1);
					} else if (event.key == Key::Enter) {
						PressPopupDefaultButton();
					}
					break;
				}
				{
					auto eventsIt = events.find(focused);
					if (eventsIt != events.end() && eventsIt->second.onKeyDown &&
						eventsIt->second.onKeyDown(event.key, event.mods)) {
						break;
					}
				}
				if (event.key == Key::Escape) {
					Element& focusedElement = E(focused);
					if (focusedElement.textInputIndex != INVALID_UI_INDEX &&
						focusedElement.textIndex != INVALID_UI_INDEX) {
						TextInputState* input = GetTextInput(focused);
						TextComponent* text = GetText(focused);
						if (input && text && text->utf8 != input->preFocus) {
							text->utf8 = input->preFocus;
							input->cursor = input->anchor = static_cast<i32>(text->utf8.size());
							OnTextEdited(focused);
						}
						SetFocused(INVALID_UI_INDEX);
					} else if (!popups.empty()) {
						ClosePopupsFrom(popups.size() - 1);
					} else {
						SetFocused(INVALID_UI_INDEX);
					}
					break;
				}
				Element& element = E(focused);
				if (element.textInputIndex == INVALID_UI_INDEX || element.textIndex == INVALID_UI_INDEX) {
					if (event.key == Key::Enter) {
						PressPopupDefaultButton();
					}
					break;
				}
				TextInputState* input = GetTextInput(focused);
				TextComponent* text = GetText(focused);
				if (!input || !text) {
					break;
				}

				bool shift = HasFlag(event.mods, KeyMod::Shift);
				bool ctrl = HasFlag(event.mods, KeyMod::Ctrl);
				bool changed = false;
				const bool vertical = input->multiline && !ctrl && (event.key == Key::Up || event.key == Key::Down);
				if (!vertical) {
					input->preferredX = -1.0f;
				}
				auto moveCaret = [&](i32 to) {
					input->cursor = to;
					if (!shift) {
						input->anchor = to;
					}
				};
				auto lineEnd = [&](bool end) -> i32 {
					EnsureShaped(focused);
					const ShapedRun& run = GetText(focused)->shaped;
					const u32 line = run.StopForByte(static_cast<u32>(input->cursor)).line;
					return static_cast<i32>(run.ByteForCaret(end ? run.extent.x + 1.0f : -1.0f, line));
				};

				auto deleteSelection = [&]() -> bool {
					if (input->cursor == input->anchor) {
						return false;
					}
					i32 begin = std::min(input->cursor, input->anchor);
					i32 end = std::max(input->cursor, input->anchor);
					text->utf8.erase(static_cast<size_t>(begin), static_cast<size_t>(end - begin));
					input->cursor = input->anchor = begin;
					return true;
				};
				auto selectedText = [&]() -> str {
					i32 begin = std::min(input->cursor, input->anchor);
					i32 end = std::max(input->cursor, input->anchor);
					return str(text->utf8.data() + begin, static_cast<size_t>(end - begin));
				};

				if (ctrl) {
					switch (event.key) {
					case Key::A:
						input->anchor = 0;
						input->cursor = static_cast<i32>(text->utf8.size());
						break;
					case Key::Home:
						moveCaret(0);
						break;
					case Key::End:
						moveCaret(static_cast<i32>(text->utf8.size()));
						break;
					case Key::C:
						if (input->cursor != input->anchor && WindowSystem::IsInitialized()) {
							WindowSystem::SetClipboardText(selectedText());
						}
						break;
					case Key::X:
						if (input->cursor != input->anchor && WindowSystem::IsInitialized()) {
							WindowSystem::SetClipboardText(selectedText());
							changed = deleteSelection();
						}
						break;
					case Key::V: {
						if (!WindowSystem::IsInitialized()) {
							break;
						}
						str pasted = WindowSystem::GetClipboardText();
						if (pasted.empty()) {
							break;
						}
						deleteSelection();
						const bool keepBreaks = input->multiline;
						auto eventsIt = events.find(focused);
						String insert{GetMemoryResource(MemTag::UI)};
						size_t i = 0;
						while (i < pasted.size()) {
							unsigned char c = static_cast<unsigned char>(pasted[i]);
							char32_t code = 0;
							u32 extra = 0;
							if (c < 0x80) {
								code = c;
							} else if ((c >> 5) == 0x6) {
								code = c & 0x1F;
								extra = 1;
							} else if ((c >> 4) == 0xE) {
								code = c & 0x0F;
								extra = 2;
							} else if ((c >> 3) == 0x1E) {
								code = c & 0x07;
								extra = 3;
							} else {
								++i;
								continue;
							}
							++i;
							for (u32 k = 0; k < extra && i < pasted.size(); ++k, ++i) {
								code = (code << 6) | (static_cast<unsigned char>(pasted[i]) & 0x3F);
							}
							if (code < 32 && !(code == '\n' && keepBreaks)) {
								continue;
							}
							if (eventsIt != events.end() && eventsIt->second.onTextFilter && !eventsIt->second.onTextFilter(code)) {
								continue;
							}
							UTF8Append(insert, code);
						}
						if (focused >= elements.size()) {
							break;
						}
						input = GetTextInput(focused);
						text = GetText(focused);
						if (!input || !text) {
							break;
						}
						if (!insert.empty()) {
							text->utf8.insert(static_cast<size_t>(input->cursor), insert);
							input->cursor += static_cast<i32>(insert.size());
							input->anchor = input->cursor;
							changed = true;
						}
						break;
					}
					default:
						break;
					}
				} else {
					switch (event.key) {
					case Key::Left:
						if (input->cursor > 0) {
							input->cursor = static_cast<i32>(UTF8Prev(text->utf8, static_cast<u32>(input->cursor)));
						}
						if (!shift) {
							input->anchor = input->cursor;
						}
						break;
					case Key::Right:
						if (input->cursor < static_cast<i32>(text->utf8.size())) {
							input->cursor = static_cast<i32>(UTF8Next(text->utf8, static_cast<u32>(input->cursor)));
						}
						if (!shift) {
							input->anchor = input->cursor;
						}
						break;
					case Key::Home:
						moveCaret(input->multiline ? lineEnd(false) : 0);
						break;
					case Key::End:
						moveCaret(input->multiline ? lineEnd(true) : static_cast<i32>(text->utf8.size()));
						break;
					case Key::Up:
					case Key::Down: {
						if (!input->multiline) {
							break;
						}
						EnsureShaped(focused);
						const ShapedRun& run = GetText(focused)->shaped;
						const CaretStop at = run.StopForByte(static_cast<u32>(input->cursor));
						const f32 column = input->preferredX >= 0.0f ? input->preferredX : at.x;
						const bool up = event.key == Key::Up;
						if (up && at.line == 0) {
							moveCaret(0);
						} else if (!up && at.line + 1 >= run.LineCount()) {
							moveCaret(static_cast<i32>(text->utf8.size()));
						} else {
							moveCaret(static_cast<i32>(run.ByteForCaret(column, up ? at.line - 1 : at.line + 1)));
						}
						input->preferredX = column;
						break;
					}
					case Key::Tab: {
						const i32 end = static_cast<i32>(text->utf8.size());
						if (!shift && !input->completion.empty() && input->cursor == end && input->anchor == end) {
							text->utf8.append(input->completion.data(), input->completion.size());
							input->cursor = input->anchor = static_cast<i32>(text->utf8.size());
							changed = true;
						}
						break;
					}
					case Key::Backspace:
						if (deleteSelection()) {
							changed = true;
						} else if (input->cursor > 0) {
							u32 previous = UTF8Prev(text->utf8, static_cast<u32>(input->cursor));
							text->utf8.erase(previous, static_cast<u32>(input->cursor) - previous);
							input->cursor = input->anchor = static_cast<i32>(previous);
							changed = true;
						}
						break;
					case Key::Delete:
						if (deleteSelection()) {
							changed = true;
						} else if (input->cursor < static_cast<i32>(text->utf8.size())) {
							u32 next = UTF8Next(text->utf8, static_cast<u32>(input->cursor));
							text->utf8.erase(static_cast<size_t>(input->cursor), next - static_cast<u32>(input->cursor));
							changed = true;
						}
						break;
					case Key::Enter: {
						if (input->multiline) {
							deleteSelection();
							text->utf8.insert(static_cast<size_t>(input->cursor), 1, '\n');
							input->cursor = input->anchor = input->cursor + 1;
							changed = true;
							break;
						}
						auto eventsIt = events.find(focused);
						if (eventsIt != events.end() && eventsIt->second.onTextCommit) {
							String committed = text->utf8;
							const u32 generation = E(focused).generation;
							eventsIt->second.onTextCommit(std::string_view(committed));
							if (focused >= elements.size() || elements[focused].generation != generation) {
								break;
							}
							input = GetTextInput(focused);
							text = GetText(focused);
							if (!input || !text) {
								break;
							}
						}
						input->preFocus = text->utf8;
						PressPopupDefaultButton();
						break;
					}
					default:
						break;
					}
				}

				if (changed) {
					OnTextEdited(focused);
				}
				break;
			}
			case InputEventType::Text: {
				if (focused == INVALID_UI_INDEX || event.codepoint < 32 || IsBehindPopupScrim(focused)) {
					break;
				}
				Element& element = E(focused);
				if (element.textInputIndex == INVALID_UI_INDEX || element.textIndex == INVALID_UI_INDEX) {
					break;
				}
				auto eventsIt = events.find(focused);
				if (eventsIt != events.end() && eventsIt->second.onTextFilter &&
					!eventsIt->second.onTextFilter(event.codepoint)) {
					break;
				}
				TextInputState* input = GetTextInput(focused);
				TextComponent* text = GetText(focused);
				if (!input || !text) {
					break;
				}
				if (input->cursor != input->anchor) {
					i32 begin = std::min(input->cursor, input->anchor);
					i32 end = std::max(input->cursor, input->anchor);
					text->utf8.erase(static_cast<size_t>(begin), static_cast<size_t>(end - begin));
					input->cursor = input->anchor = begin;
				}
				input->preferredX = -1.0f;
				String encoded{GetMemoryResource(MemTag::UI)};
				UTF8Append(encoded, event.codepoint);
				text->utf8.insert(static_cast<size_t>(input->cursor), encoded);
				input->cursor += static_cast<i32>(encoded.size());
				input->anchor = input->cursor;
				OnTextEdited(focused);
				break;
			}
			default:
				break;
			}
			RestartBlinkIfCaretMoved(caretBefore);
		}
		pendingInput.clear();
	}

} // namespace Shalltear::UI
