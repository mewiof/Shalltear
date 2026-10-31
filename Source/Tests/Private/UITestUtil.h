#pragma once

#include "TestsPCH.h"

#include "Shalltear/Core/UI.h"
#include "Shalltear/Core/UI/VectorAsset.h"

namespace UITest {

	using namespace Shalltear;

	class MockFontSystem final : public UI::IFontSystem {
	public:
		static constexpr f32 ADVANCE = 10.0f;
		static constexpr f32 LINE_HEIGHT = 16.0f;
		static constexpr f32 GLYPH_H = 12.0f;

		bool ShapeText(const UI::TextQuery& query, UI::ShapedRun& out) override {
			out.glyphs.clear();
			out.outlineGlyphs.clear();
			out.caretStops.clear();
			out.extent = {};
			out.lineHeight = LINE_HEIGHT;
			out.multiline = false;
			out.truncated = false;
			out.atlas = RHI::TextureHandle{7, 1};

			f32 penX = 0.0f;
			f32 penY = 0.0f;
			f32 maxWidth = 0.0f;
			u32 line = 0;
			out.caretStops.push_back({0, 0.0f, 0});

			const char* text = query.utf8.data();
			u32 length = static_cast<u32>(query.utf8.size());
			u32 i = 0;
			const bool ellipsize = query.overflow == UI::TextOverflow::Ellipsis;
			const bool wrap = query.overflow == UI::TextOverflow::Wrap && query.wrapWidth > 0.0f;
			bool truncated = false;
			u32 kept = 0;
			bool firstOnLine = true;
			while (i < length) {
				u32 byteOffset = i;
				unsigned char c = static_cast<unsigned char>(text[i]);
				u32 extra = c < 0x80 ? 0 : ((c >> 5) == 0x6 ? 1 : ((c >> 4) == 0xE ? 2 : 3));
				i += 1 + extra;
				if (c == '\n') {
					if (ellipsize) {
						truncated = true;
						break;
					}
					penX = 0.0f;
					penY += LINE_HEIGHT;
					out.multiline = true;
					firstOnLine = true;
					out.caretStops.push_back({i, 0.0f, ++line});
					continue;
				}
				f32 step = firstOnLine ? 0.0f : query.letterSpacing;
				if (ellipsize && query.wrapWidth > 0.0f) {
					f32 withMark = penX + step + ADVANCE + query.letterSpacing + ADVANCE;
					if (withMark > query.wrapWidth && kept > 0) {
						truncated = true;
						break;
					}
				}
				if (wrap && penX + step + ADVANCE > query.wrapWidth && !firstOnLine) {
					penX = 0.0f;
					penY += LINE_HEIGHT;
					out.multiline = true;
					firstOnLine = true;
					step = 0.0f;
					out.caretStops.push_back({byteOffset, 0.0f, ++line});
				}
				penX += step;
				UI::ShapedGlyph glyph;
				glyph.rect = {penX + 1.0f, penY + 2.0f, ADVANCE - 2.0f, GLYPH_H};
				glyph.uv = {0.0f, 0.0f, 0.5f, 0.5f};
				out.glyphs.push_back(glyph);
				penX += ADVANCE;
				firstOnLine = false;
				++kept;
				maxWidth = penX > maxWidth ? penX : maxWidth;
				u32 nextByte = i < length ? i : length;
				out.caretStops.push_back({nextByte, penX, line});
			}

			if (truncated) {
				penX += firstOnLine ? 0.0f : query.letterSpacing;
				UI::ShapedGlyph mark;
				mark.rect = {penX + 1.0f, penY + 2.0f, ADVANCE - 2.0f, GLYPH_H};
				mark.uv = {0.0f, 0.0f, 0.5f, 0.5f};
				out.glyphs.push_back(mark);
				penX += ADVANCE;
				maxWidth = penX > maxWidth ? penX : maxWidth;
			}

			out.extent = {maxWidth, penY + LINE_HEIGHT};
			out.truncated = truncated;
			if (truncated) {
				out.caretStops.clear();
				out.caretStops.push_back({0, 0.0f, 0});
			}
			return true;
		}

		Vec2 MeasureText(const UI::TextQuery& query) override {
			UI::ShapedRun scratch;
			ShapeText(query, scratch);
			return scratch.extent;
		}

		f32 GetLineHeight(Handle<FontAsset>, f32) override {
			return LINE_HEIGHT;
		}
	};

	class CountingFont final : public UI::IFontSystem {
	public:
		u32 shapes = 0;
		u32 measures = 0;
		MockFontSystem inner;

		u32 Walks() const { return shapes + measures; }
		void Reset() {
			shapes = 0;
			measures = 0;
		}

		bool ShapeText(const UI::TextQuery& query, UI::ShapedRun& out) override {
			++shapes;
			return inner.ShapeText(query, out);
		}
		Vec2 MeasureText(const UI::TextQuery& query) override {
			++measures;
			return inner.MeasureText(query);
		}
		f32 GetLineHeight(Handle<FontAsset> font, f32 sizePx) override {
			return inner.GetLineHeight(font, sizePx);
		}
	};

	class WrapWidthRecorder final : public UI::IFontSystem {
	public:
		Vector<f32> widths{GetMemoryResource(MemTag::Debug)};
		Vector<f32> shapedWidths{GetMemoryResource(MemTag::Debug)};
		MockFontSystem inner;

		bool ShapeText(const UI::TextQuery& query, UI::ShapedRun& out) override {
			widths.push_back(query.wrapWidth);
			shapedWidths.push_back(query.wrapWidth);
			return inner.ShapeText(query, out);
		}
		Vec2 MeasureText(const UI::TextQuery& query) override {
			widths.push_back(query.wrapWidth);
			return inner.MeasureText(query);
		}
		f32 GetLineHeight(Handle<FontAsset> font, f32 sizePx) override { return inner.GetLineHeight(font, sizePx); }
	};

	class CaptureRenderer final : public UI::IRenderer {
	public:
		UI::RenderList last;
		u32 submitCount = 0;

		void Submit(const UI::RenderList& list, const UI::SubmitInfo&) override {
			last.quads.assign(list.quads.begin(), list.quads.end());
			last.commands.assign(list.commands.begin(), list.commands.end());
			++submitCount;
		}
	};

	UI::ElementHandle FindByClass(UI::Context& context, UI::ElementHandle root, StringHash klass, u32 skip);

	inline u32 CountByClass(UI::Context& context, UI::ElementHandle root, StringHash klass) {
		u32 count = context.HasClass(root, klass) ? 1u : 0u;
		const UI::Element* pod = context.GetElement(root);
		if (!pod) {
			return count;
		}
		for (u32 child = pod->firstChild; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(child);
			count += CountByClass(context, handle, klass);
			child = context.GetElement(handle)->nextSibling;
		}
		return count;
	}

	inline UI::ElementHandle FindByClass(UI::Context& context, UI::ElementHandle root, StringHash klass, u32 skip) {
		if (context.HasClass(root, klass)) {
			if (skip == 0) {
				return root;
			}
			--skip;
		}
		const UI::Element* pod = context.GetElement(root);
		if (!pod) {
			return {};
		}
		for (u32 child = pod->firstChild; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(child);
			UI::ElementHandle found = FindByClass(context, handle, klass, skip);
			if (context.IsAlive(found)) {
				return found;
			}
			skip -= CountByClass(context, handle, klass);
			child = context.GetElement(handle)->nextSibling;
		}
		return {};
	}

	inline UI::ElementHandle NthChildOf(UI::Context& context, UI::ElementHandle parent, u32 n) {
		const UI::Element* pod = context.GetElement(parent);
		if (!pod) {
			return {};
		}
		u32 child = pod->firstChild;
		for (u32 i = 0; i < n && child != UI::INVALID_UI_INDEX; ++i) {
			child = context.GetElement(context.MakeHandle(child))->nextSibling;
		}
		return child != UI::INVALID_UI_INDEX ? context.MakeHandle(child) : UI::ElementHandle{};
	}

	inline str TooltipAt(UI::Context& context, Vec2 point) {
		InputEvent move{};
		move.type = InputEventType::MouseMove;
		move.mousePosPx = {static_cast<i32>(point.x), static_cast<i32>(point.y)};
		context.InjectInput(move);
		context.Update();
		UI::ElementHandle card = FindByClass(context, context.GetRoot(), StringHash("slt-tooltip"), 0);
		const UI::Element* pod = context.GetElement(card);
		if (!pod || !HasFlag(pod->flags, UI::ElementFlags::Visible)) {
			return {};
		}
		return str(context.GetTextString(card));
	}

	inline u32 ChildCountOf(UI::Context& context, UI::ElementHandle parent) {
		const UI::Element* pod = context.GetElement(parent);
		if (!pod) {
			return 0;
		}
		u32 count = 0;
		for (u32 child = pod->firstChild; child != UI::INVALID_UI_INDEX; ++count) {
			child = context.GetElement(context.MakeHandle(child))->nextSibling;
		}
		return count;
	}

	inline UI::ElementHandle FindByIcon(UI::Context& context, UI::ElementHandle root,
										Handle<VectorAsset> asset) {
		if (!asset.IsValid()) {
			return {};
		}
		if (context.GetIconAsset(root) == asset) {
			return root;
		}
		const UI::Element* pod = context.GetElement(root);
		if (!pod) {
			return {};
		}
		for (u32 child = pod->firstChild; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(child);
			UI::ElementHandle found = FindByIcon(context, handle, asset);
			if (context.IsAlive(found)) {
				return found;
			}
			child = context.GetElement(handle)->nextSibling;
		}
		return {};
	}

	inline UI::ElementHandle ControlWearing(UI::Context& context, UI::ElementHandle root,
											Handle<VectorAsset> asset, StringHash controlClass) {
		UI::ElementHandle face = FindByIcon(context, root, asset);
		for (UI::ElementHandle walk = face; context.IsAlive(walk);) {
			if (context.HasClass(walk, controlClass)) {
				return walk;
			}
			const UI::Element* pod = context.GetElement(walk);
			if (!pod || pod->parent == UI::INVALID_UI_INDEX) {
				break;
			}
			walk = context.MakeHandle(pod->parent);
		}
		return {};
	}

	inline UI::ElementHandle FindByText(UI::Context& context, UI::ElementHandle root, std::string_view text) {
		if (context.GetTextString(root) == text) {
			return root;
		}
		const UI::Element* pod = context.GetElement(root);
		if (!pod) {
			return {};
		}
		for (u32 child = pod->firstChild; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(child);
			UI::ElementHandle found = FindByText(context, handle, text);
			if (context.IsAlive(found)) {
				return found;
			}
			child = context.GetElement(handle)->nextSibling;
		}
		return {};
	}

	inline void RightClickAt(UI::Context& context, i32 x, i32 y) {
		InputEvent move{};
		move.type = InputEventType::MouseMove;
		move.mousePosPx = {x, y};
		context.InjectInput(move);
		InputEvent down{};
		down.type = InputEventType::MouseDown;
		down.button = MouseButton::Right;
		down.mousePosPx = {x, y};
		context.InjectInput(down);
		InputEvent up{};
		up.type = InputEventType::MouseUp;
		up.button = MouseButton::Right;
		up.mousePosPx = {x, y};
		context.InjectInput(up);
		context.Update();
	}

	inline void RightClickElement(UI::Context& context, UI::ElementHandle element) {
		const UI::Rect rect = context.GetLayout(element).borderBox;
		RightClickAt(context, static_cast<i32>(rect.x + rect.w * 0.5f), static_cast<i32>(rect.y + rect.h * 0.5f));
	}

	inline void DragElementBy(UI::Context& context, UI::ElementHandle element, i32 dx, i32 dy) {
		const UI::Rect rect = context.GetLayout(element).borderBox;
		const i32 x = static_cast<i32>(rect.x + rect.w * 0.5f);
		const i32 y = static_cast<i32>(rect.y + rect.h * 0.5f);
		InputEvent move{};
		move.type = InputEventType::MouseMove;
		move.mousePosPx = {x, y};
		context.InjectInput(move);
		InputEvent down{};
		down.type = InputEventType::MouseDown;
		down.button = MouseButton::Left;
		down.mousePosPx = {x, y};
		context.InjectInput(down);
		context.Update();
		for (i32 step = 1; step <= 2; ++step) {
			InputEvent travel{};
			travel.type = InputEventType::MouseMove;
			travel.mousePosPx = {x + dx * step / 2, y + dy * step / 2};
			context.InjectInput(travel);
			context.Update();
		}
		InputEvent up{};
		up.type = InputEventType::MouseUp;
		up.button = MouseButton::Left;
		up.mousePosPx = {x + dx, y + dy};
		context.InjectInput(up);
		context.Update();
	}

	inline void ClickElement(UI::Context& context, UI::ElementHandle element) {
		const UI::Rect rect = context.GetLayout(element).borderBox;
		const i32 x = static_cast<i32>(rect.x + rect.w * 0.5f);
		const i32 y = static_cast<i32>(rect.y + rect.h * 0.5f);
		InputEvent move{};
		move.type = InputEventType::MouseMove;
		move.mousePosPx = {x, y};
		context.InjectInput(move);
		InputEvent down{};
		down.type = InputEventType::MouseDown;
		down.button = MouseButton::Left;
		down.mousePosPx = {x, y};
		context.InjectInput(down);
		InputEvent up{};
		up.type = InputEventType::MouseUp;
		up.button = MouseButton::Left;
		up.mousePosPx = {x, y};
		context.InjectInput(up);
		context.Update();
	}

	inline void DoubleClickElement(UI::Context& context, UI::ElementHandle element) {
		ClickElement(context, element);
		ClickElement(context, element);
	}

	inline UI::ElementHandle FindByTextContaining(UI::Context& context, UI::ElementHandle root, std::string_view needle) {
		if (context.GetTextString(root).find(needle) != std::string_view::npos) {
			return root;
		}
		const UI::Element* pod = context.GetElement(root);
		if (!pod) {
			return {};
		}
		for (u32 child = pod->firstChild; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(child);
			UI::ElementHandle found = FindByTextContaining(context, handle, needle);
			if (context.IsAlive(found)) {
				return found;
			}
			child = context.GetElement(handle)->nextSibling;
		}
		return {};
	}

	inline UI::Style FixedBox(f32 width, f32 height) {
		UI::Style style;
		style.width = UI::Length::Pixel(width);
		style.height = UI::Length::Pixel(height);
		return style;
	}

	inline UI::ElementHandle MakeChild(UI::Context& ctx, UI::ElementHandle parent, const UI::Style& style) {
		UI::ElementHandle handle = ctx.CreateElement();
		ctx.SetStyle(handle, style);
		ctx.AppendChild(parent, handle);
		return handle;
	}

	inline bool IsCanvasSurface(UI::Context& context, UI::ElementHandle element) {
		const UI::Element* pod = context.GetElement(element);
		if (!pod) {
			return false;
		}
		for (u8 i = 0; i < pod->classCount; ++i) {
			if (pod->classes[i] == StringHash("ed-viewport-placeholder")) {
				return true;
			}
		}
		return false;
	}

	inline bool IsScrollBackground(UI::Context& context, UI::ElementHandle element) {
		return context.HasClass(element, StringHash("slt-scroll-viewport"));
	}

	inline bool TakesAPress(UI::Context& context, UI::ElementHandle element) {
		if (context.HasTextInput(element)) {
			return true;
		}
		if (!context.HasEvents(element)) {
			return false;
		}
		const UI::ElementEvents& events = context.Events(element);
		return static_cast<bool>(events.onClick) || static_cast<bool>(events.onMouseDown) ||
			   static_cast<bool>(events.onDoubleClick) || static_cast<bool>(events.onPressDrag);
	}

	inline void CollectCursorOffenders(UI::Context& context, UI::ElementHandle root, Vector<str>& out) {
		const UI::Element* pod = context.GetElement(root);
		if (!pod) {
			return;
		}
		const bool reachable = HasFlag(pod->flags, UI::ElementFlags::Hoverable) &&
							   context.GetComputedStyle(root).pointerEvents;
		if (reachable && TakesAPress(context, root) && !IsCanvasSurface(context, root) && !IsScrollBackground(context, root) &&
			context.GetComputedStyle(root).cursor == SystemCursor::Arrow) {
			str names;
			for (u8 i = 0; i < pod->classCount; ++i) {
				names += (names.empty() ? "" : ".");
				names += std::format("{:016x}", pod->classes[i].GetValue());
			}
			str text = str(context.GetTextString(root));
			out.push_back(std::format("element[classes {}]{}",
									  names.empty() ? str("(none)") : names,
									  text.empty() ? str() : " text=\"" + text + "\""));
		}
		for (u32 child = pod->firstChild; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(child);
			CollectCursorOffenders(context, handle, out);
			child = context.GetElement(handle)->nextSibling;
		}
	}

	inline str ReportCursorOffenders(const Vector<str>& offenders) {
		str message = std::format("{} interactive element(s) show the plain arrow:", offenders.size());
		for (const str& one : offenders) {
			message += "\n  " + one;
		}
		return message;
	}

	inline bool SameBrush(const UI::Brush& a, const UI::Brush& b) {
		if (a.type != b.type) {
			return false;
		}
		switch (a.type) {
		case UI::BrushType::None:
			return true;
		case UI::BrushType::Solid:
			return a.solid == b.solid;
		case UI::BrushType::LinearGradient:
			return a.gradient0 == b.gradient0 && a.gradient1 == b.gradient1 && a.gradientDirection == b.gradientDirection;
		default:
			return a == b;
		}
	}

	inline bool SamePaint(const UI::Style& a, const UI::Style& b) {
		if (!SameBrush(a.background, b.background) || !(a.borderColor == b.borderColor) || !(a.borderWidth == b.borderWidth) ||
			a.boxShadowCount != b.boxShadowCount || !(a.dropShadow == b.dropShadow) || a.opacity != b.opacity ||
			!(a.width == b.width) || !(a.height == b.height) || !(a.margin == b.margin)) {
			return false;
		}
		for (u8 i = 0; i < a.boxShadowCount; ++i) {
			if (!(a.boxShadows[i] == b.boxShadows[i])) {
				return false;
			}
		}
		return true;
	}

	using PaintSnapshot = Vector<std::pair<u32, UI::Style>>;

	inline void SnapshotPaint(UI::Context& context, UI::ElementHandle root, PaintSnapshot& out) {
		const UI::Element* pod = context.GetElement(root);
		if (!pod) {
			return;
		}
		out.push_back({root.index, context.GetComputedStyle(root)});
		for (u32 child = pod->firstChild; child != UI::INVALID_UI_INDEX;) {
			UI::ElementHandle handle = context.MakeHandle(child);
			SnapshotPaint(context, handle, out);
			child = context.GetElement(handle)->nextSibling;
		}
	}

	inline bool PaintDiffers(const PaintSnapshot& a, const PaintSnapshot& b) {
		if (a.size() != b.size()) {
			return true;
		}
		for (size_t i = 0; i < a.size(); ++i) {
			if (a[i].first != b[i].first || !SamePaint(a[i].second, b[i].second)) {
				return true;
			}
		}
		return false;
	}

	inline bool AnswersAClick(UI::Context& context, UI::ElementHandle element) {
		if (!context.HasEvents(element)) {
			return false;
		}
		const UI::ElementEvents& events = context.Events(element);
		return static_cast<bool>(events.onClick) || static_cast<bool>(events.onDoubleClick);
	}

	inline bool PaintsItself(UI::Context& context, UI::ElementHandle element) {
		return context.HasEvents(element) && static_cast<bool>(context.Events(element).customDraw);
	}

	inline str DescribeElement(UI::Context& context, UI::ElementHandle element) {
		const UI::Element* pod = context.GetElement(element);
		str names;
		for (u8 i = 0; pod && i < pod->classCount; ++i) {
			names += (names.empty() ? "" : ".");
			names += std::format("{:016x}", pod->classes[i].GetValue());
		}
		str text = str(context.GetTextString(element));
		return std::format("element[classes {}]{}", names.empty() ? str("(none)") : names,
						   text.empty() ? str() : " text=\"" + text + "\"");
	}

	struct PressWalk {
		u32 inspected = 0;
		u32 unreachable = 0;
		Vector<StringHash> inspectedClasses{GetMemoryResource(MemTag::Core)};
		Vector<str> offenders{GetMemoryResource(MemTag::Core)};

		bool Inspected(StringHash klass) const {
			return std::find(inspectedClasses.begin(), inspectedClasses.end(), klass) != inspectedClasses.end();
		}
		str Report() const {
			str message = std::format("{} control(s) show nothing new under the press:", offenders.size());
			for (const str& one : offenders) {
				message += "\n  " + one;
			}
			return message;
		}
	};

	inline void WalkThePress(UI::Context& context, UI::ElementHandle root, PressWalk& walk,
							 const std::function<void()>& frame = {}) {
		auto advance = [&]() {
			if (frame) {
				frame();
			} else {
				context.Update();
			}
		};
		auto send = [&](InputEventType type, Vec2 point) {
			InputEvent event{};
			event.type = type;
			event.button = MouseButton::Left;
			event.mousePosPx = {static_cast<i32>(point.x), static_cast<i32>(point.y)};
			context.InjectInput(event);
			advance();
		};
		constexpr Vec2 FAR_AWAY{-5000.0f, -5000.0f};

		Vector<UI::ElementHandle> candidates{GetMemoryResource(MemTag::Core)};
		std::function<void(UI::ElementHandle)> collect = [&](UI::ElementHandle element) {
			const UI::Element* pod = context.GetElement(element);
			if (!pod) {
				return;
			}
			const bool reachable = HasFlag(pod->flags, UI::ElementFlags::Hoverable) &&
								   context.GetComputedStyle(element).pointerEvents;
			if (reachable && TakesAPress(context, element) && !IsCanvasSurface(context, element) &&
				!IsScrollBackground(context, element) && !PaintsItself(context, element)) {
				candidates.push_back(element);
			}
			for (u32 child = pod->firstChild; child != UI::INVALID_UI_INDEX;) {
				UI::ElementHandle handle = context.MakeHandle(child);
				collect(handle);
				child = context.GetElement(handle)->nextSibling;
			}
		};
		collect(root);

		for (UI::ElementHandle element : candidates) {
			if (!context.IsAlive(element)) {
				continue;
			}
			const UI::Rect box = context.GetLayout(element).borderBox;
			Vec2 point{};
			bool found = false;
			for (f32 fy : {0.5f, 0.25f, 0.75f}) {
				for (f32 fx : {0.5f, 0.2f, 0.8f, 0.05f, 0.95f}) {
					const Vec2 probe{box.x + box.w * fx, box.y + box.h * fy};
					if (!found && context.HitTest(probe) == element) {
						point = probe;
						found = true;
					}
				}
			}
			if (!found) {
				++walk.unreachable;
				continue;
			}
			const str name = DescribeElement(context, element);
			const bool clicks = AnswersAClick(context, element);
			const UI::Element* pod = context.GetElement(element);
			for (u8 i = 0; i < pod->classCount; ++i) {
				walk.inspectedClasses.push_back(pod->classes[i]);
			}
			++walk.inspected;

			PaintSnapshot resting{GetMemoryResource(MemTag::Core)};
			PaintSnapshot hovered{GetMemoryResource(MemTag::Core)};
			PaintSnapshot pressed{GetMemoryResource(MemTag::Core)};
			send(InputEventType::MouseMove, FAR_AWAY);
			SnapshotPaint(context, element, resting);
			send(InputEventType::MouseMove, point);
			SnapshotPaint(context, element, hovered);
			send(InputEventType::MouseDown, point);
			const bool died = !context.IsAlive(element);
			if (!died) {
				SnapshotPaint(context, element, pressed);
			}
			send(InputEventType::MouseUp, FAR_AWAY);
			context.CloseAllPopups();
			advance();
			if (died) {
				continue;
			}

			if (clicks && !PaintDiffers(resting, pressed)) {
				walk.offenders.push_back(name + " - pressed looks like resting");
			} else if (clicks && !PaintDiffers(hovered, pressed)) {
				walk.offenders.push_back(name + " - pressed looks like hovered");
			} else if (!clicks && PaintDiffers(resting, hovered) && !PaintDiffers(resting, pressed)) {
				walk.offenders.push_back(name + " - its hover drops back to rest under the press");
			}
		}
	}

} // namespace UITest
