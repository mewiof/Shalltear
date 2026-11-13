#pragma once

#include "Shalltear/Core/Assert.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Input/InputEvent.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/Element.h"
#include "Shalltear/Core/UI/Font.h"
#include "Shalltear/Core/UI/RenderList.h"
#include "Shalltear/Core/UI/Style.h"
#include "Shalltear/Core/UI/UITypes.h"
#include "Shalltear/Core/UI/VectorAsset.h"
#include "Shalltear/Core/UI/VectorIcon.h"
#include "Shalltear/Core/Graphics/TextureAsset.h"

#include "UI/RichText.h"

#include <algorithm>
#include <chrono>
#include <cmath>
#include <limits>
#include <memory>

namespace Shalltear::UI {

	inline constexpr Rect UnknownInk = {0.0f, 0.0f, -1.0f, -1.0f};

	inline constexpr f32 INITIAL_FONT_SIZE = 16.0f;
	inline constexpr Color InitialTextColor = Color::White();
	inline constexpr Color InitialOutlineColor = Color::Black();
	inline constexpr u16 INITIAL_FONT_WEIGHT = 400;

	// hex (#rgb to #rrggbbaa) or a CSS basic color name
	bool ParseStyleColor(std::string_view value, Color& out);

	// name out of name/"name"/url("name")
	std::string_view FontNameOf(std::string_view value);

	bool ResolveFontName(std::string_view value, const StyleVars* vars, Handle<FontAsset>& out);

	[[nodiscard]] inline bool IsKnownInk(const Rect& ink) {
		return ink.w >= 0.0f;
	}

	[[nodiscard]] inline bool InkReachesClip(const Rect& ink, const Rect& clip) {
		if (!IsKnownInk(ink)) {
			return true;
		}
		const f32 x0 = ink.x > clip.x ? ink.x : clip.x;
		const f32 x1 = (ink.x + ink.w) < (clip.x + clip.w) ? (ink.x + ink.w) : (clip.x + clip.w);
		if (x1 <= x0) {
			return false;
		}
		const f32 y0 = ink.y > clip.y ? ink.y : clip.y;
		const f32 y1 = (ink.y + ink.h) < (clip.y + clip.h) ? (ink.y + ink.h) : (clip.y + clip.h);
		return y1 > y0;
	}

	[[nodiscard]] inline Rect RectUnion(const Rect& a, const Rect& b) {
		if (a.w <= 0.0f || a.h <= 0.0f) {
			return b;
		}
		if (b.w <= 0.0f || b.h <= 0.0f) {
			return a;
		}
		f32 x0 = a.x < b.x ? a.x : b.x;
		f32 y0 = a.y < b.y ? a.y : b.y;
		f32 x1 = (a.x + a.w) > (b.x + b.w) ? (a.x + a.w) : (b.x + b.w);
		f32 y1 = (a.y + a.h) > (b.y + b.h) ? (a.y + a.h) : (b.y + b.h);
		return {x0, y0, x1 - x0, y1 - y0};
	}

	// rads clockwise (+y down) about pivot, in parent's layout space
	struct ElementTurn {
		Vec2 pivot{};
		f32 rads = 0.0f;
		f32 cos = 1.0f;
		f32 sin = 0.0f;
		bool turned = false;
	};

	[[nodiscard]] inline Vec2 TurnPoint(const ElementTurn& turn, Vec2 point) {
		const f32 dx = point.x - turn.pivot.x;
		const f32 dy = point.y - turn.pivot.y;
		return {turn.pivot.x + dx * turn.cos - dy * turn.sin, turn.pivot.y + dx * turn.sin + dy * turn.cos};
	}

	[[nodiscard]] inline Vec2 UnturnPoint(const ElementTurn& turn, Vec2 point) {
		const f32 dx = point.x - turn.pivot.x;
		const f32 dy = point.y - turn.pivot.y;
		return {turn.pivot.x + dx * turn.cos + dy * turn.sin, turn.pivot.y - dx * turn.sin + dy * turn.cos};
	}

	[[nodiscard]] inline Rect TurnBounds(const ElementTurn& turn, const Rect& rect, bool inverse) {
		const Vec2 corners[4] = {{rect.x, rect.y}, {rect.x + rect.w, rect.y}, {rect.x, rect.y + rect.h}, {rect.x + rect.w, rect.y + rect.h}};
		Vec2 low{INFINITY, INFINITY};
		Vec2 high{-INFINITY, -INFINITY};
		for (const Vec2& corner : corners) {
			const Vec2 moved = inverse ? UnturnPoint(turn, corner) : TurnPoint(turn, corner);
			low = {std::min(low.x, moved.x), std::min(low.y, moved.y)};
			high = {std::max(high.x, moved.x), std::max(high.y, moved.y)};
		}
		return {low.x, low.y, high.x - low.x, high.y - low.y};
	}

	struct TextMetrics {
		Handle<FontAsset> font;
		f32 sizePhysical = -1.0f;
		f32 outlineWidth = 0.0f;
		f32 letterSpacing = 0.0f;
		f32 lineHeight = 0.0f;
		f32 lineHeightFactor = 0.0f;
		u16 fontWeight = 400;
		FontStyle fontStyle = FontStyle::Normal;

		void ApplyTo(TextQuery& query) const {
			query.font = font;
			query.sizePx = sizePhysical;
			query.outlineWidth = outlineWidth;
			query.letterSpacing = letterSpacing;
			query.lineHeight = lineHeight;
			query.fontWeight = fontWeight;
			query.fontStyle = fontStyle;
		}

		bool operator==(const TextMetrics&) const = default;
	};

	struct TextComponent {
		// SetText's look, below declared style and above inherited style
		Handle<FontAsset> font;
		f32 sizePx = 16.0f;
		Color color = Color::White();
		bool hinted = false;
		String utf8{GetMemoryResource(MemTag::UI)};

		ShapedRun shaped;
		f32 wrapWidth = 0.0f;
		f32 shapedWrapWidth = -1.0f;
		TextMetrics cachedMetrics;
		TextOverflow cachedOverflow = TextOverflow::Wrap;
		Align cachedAlign = Align::Start;
		bool cachedEditable = false;
		bool richStale = true;
		u32 shapedGeneration = 0;
		Vec2 measuredExtent{};

		struct SizeEntry {
			f32 wrapWidth;
			Vec2 extent;
		};
		static constexpr size_t MAX_SIZE_ENTRIES = 4;
		Vector<SizeEntry> sizeCache{GetMemoryResource(MemTag::UI)};
		TextMetrics memoMetrics;
		TextOverflow memoOverflow = TextOverflow::Wrap;

		bool dirty = true;
		bool inUse = false;
		u32 richSlot = INVALID_UI_INDEX;

		void StoreExtent(f32 width, Vec2 extent) {
			for (SizeEntry& entry : sizeCache) {
				if (std::abs(entry.wrapWidth - width) < 0.01f) {
					entry.extent = extent;
					return;
				}
			}
			if (sizeCache.size() >= MAX_SIZE_ENTRIES) {
				sizeCache.erase(sizeCache.begin());
			}
			sizeCache.push_back({width, extent});
		}

		void ResetCaches() {
			sizeCache.clear();
			shapedWrapWidth = -1.0f;
			dirty = true;
			richStale = true;
		}
	};

	inline constexpr ElementFlags ELEMENT_HAS_PICTURE = ElementFlags::HasIcon | ElementFlags::HasImage;

	struct IconComponent {
		Handle<VectorAsset> asset;
		Handle<TextureAsset> texture;
		ImageDesc image;
		bool fillsContentBox = false;
		f32 sizePx = 16.0f;
		IconPalette palette;

		Vec2 measuredExtent{};
		f32 cachedSizePhysical = -1.0f;
	};

	struct ImageSource {
		const TextureAsset* texture = nullptr;
		Rect texels{};
		SpriteBorder border{};
		f32 pxPerTexel = 1.0f;
	};

	bool ResolveImageSource(const ImageDesc& image, ImageSource& out);

	[[nodiscard]] bool IsValidFillOrigin(ImageFillMethod method, ImageFillOrigin origin);

	struct TextInputState {
		i32 cursor = 0;
		i32 anchor = 0;
		bool inUse = false;
		bool multiline = false;
		f32 preferredX = -1.0f;
		String preFocus{GetMemoryResource(MemTag::UI)};
		String completion{GetMemoryResource(MemTag::UI)};
		Color completionColor{};
	};

	constexpr f32 LAYOUT_INF = std::numeric_limits<f32>::max();

	inline constexpr f32 SCROLL_SHOW_MARGIN = 1.0f;

	struct Constraint {
		Vec2 available{LAYOUT_INF, LAYOUT_INF};
		bool definiteX = false;
		bool definiteY = false;
		bool forceX = false;
		bool forceY = false;

		[[nodiscard]] static Constraint Intrinsic() { return Constraint{}; }
		[[nodiscard]] bool IsIntrinsic() const {
			return available.x == LAYOUT_INF && available.y == LAYOUT_INF && !definiteX && !definiteY &&
				   !forceX && !forceY;
		}

		[[nodiscard]] f32 Main(bool isRow) const { return isRow ? available.x : available.y; }
		[[nodiscard]] bool DefiniteMain(bool isRow) const { return isRow ? definiteX : definiteY; }
		[[nodiscard]] bool DefiniteCross(bool isRow) const { return isRow ? definiteY : definiteX; }

		[[nodiscard]] bool operator==(const Constraint& other) const {
			return available.x == other.available.x && available.y == other.available.y &&
				   definiteX == other.definiteX && definiteY == other.definiteY &&
				   forceX == other.forceX && forceY == other.forceY;
		}
	};

	struct MeasureCache {
		struct Entry {
			Constraint key;
			Vec2 desiredSize{};
			bool autoWidth = true;
			bool autoHeight = true;
		};

		static constexpr u8 CAPACITY = 1;
		static constexpr u8 NO_SLOT = 0xFF;

		Vec2 intrinsicSize{};
		bool intrinsicAutoWidth = true;
		bool intrinsicAutoHeight = true;
		bool intrinsicValid = false;

		Entry entries[CAPACITY];
		u8 count = 0;
		u8 next = 0;
		u8 realized = NO_SLOT;
		u8 lastWritten = NO_SLOT;

		void Invalidate() {
			count = 0;
			next = 0;
			realized = NO_SLOT;
			intrinsicValid = false;
		}
	};

	inline constexpr u16 INHERIT_FONT = 1 << 0;
	inline constexpr u16 INHERIT_FONT_SIZE = 1 << 1;
	inline constexpr u16 INHERIT_COLOR = 1 << 2;
	inline constexpr u16 INHERIT_FONT_STYLE = 1 << 3;
	inline constexpr u16 INHERIT_OUTLINE_WIDTH = 1 << 4;
	inline constexpr u16 INHERIT_OUTLINE_COLOR = 1 << 5;
	inline constexpr u16 INHERIT_LETTER_SPACING = 1 << 6;
	inline constexpr u16 INHERIT_LINE_HEIGHT = 1 << 7;
	inline constexpr u16 INHERIT_FONT_WEIGHT = 1 << 8;

	struct TextCascade {
		u16 declared = 0;
		Length fontSize{};
	};

	struct RichSlot {
		RichText text;
		Vector<TextRun> runs{GetMemoryResource(MemTag::UI)};
		TextMetrics runsFor;
		bool styled = false;
		bool resolved = false;
	};

	struct RichView {
		std::string_view utf8;
		std::span<const TextRun> runs;
	};

	inline constexpr u8 TEXT_CHANGED = 1 << 0;
	inline constexpr u8 TEXT_METRICS_CHANGED = 1 << 1;

	struct FlexItem {
		u32 index = INVALID_UI_INDEX;
		f32 mainMargin = 0.0f;
		f32 base = 0.0f;
		f32 target = 0.0f;
		f32 minMain = 0.0f;
		f32 maxMain = 0.0f;
		f32 factor = 0.0f;
		bool frozen = false;

		bool stretchOwns = false;

		Constraint resolved;
	};

	struct ContextImpl {
		Context* owner = nullptr;
		u32 contextId = 0;
		ContextDesc desc;
		IFontSystem* fontSystem = nullptr;
		u32 metricsGeneration = 0;
		IVectorIconSystem* iconSystem = nullptr;

		Vector<Element> elements{GetMemoryResource(MemTag::UI)};
		Vector<Style> inlineStyles{GetMemoryResource(MemTag::UI)};
		Vector<Style> computedStyles{GetMemoryResource(MemTag::UI)};
		Vector<LayoutData> layouts{GetMemoryResource(MemTag::UI)};
		Vector<MeasureCache> measureCaches{GetMemoryResource(MemTag::UI)};
		Vector<Rect> subtreeInk{GetMemoryResource(MemTag::UI)};
		Vector<TextCascade> cascades{GetMemoryResource(MemTag::UI)};
		Vector<u32> freeList{GetMemoryResource(MemTag::UI)};
		u32 root = 0;

		StyleSheet styleSheet;
		StyleVars styleVars;

		Vector<TextComponent> texts{GetMemoryResource(MemTag::UI)};
		Vector<u32> textFreeList{GetMemoryResource(MemTag::UI)};
		Vector<RichSlot> richSlots{GetMemoryResource(MemTag::UI)};
		Vector<u32> richFreeList{GetMemoryResource(MemTag::UI)};
		Vector<TextInputState> textInputs{GetMemoryResource(MemTag::UI)};
		Vector<u32> textInputFreeList{GetMemoryResource(MemTag::UI)};
		HashMap<u32, ElementEvents> events{GetMemoryResource(MemTag::UI)};
		struct ElementModel {
			const void* type = nullptr;
			std::shared_ptr<void> value;
		};
		HashMap<u32, Vector<ElementModel>> models{GetMemoryResource(MemTag::UI)};
		static void SetModel(Context& context, ElementHandle handle, const void* type, std::shared_ptr<void> model);
		static std::shared_ptr<void> GetModel(const Context& context, ElementHandle handle, const void* type);
		HashMap<u32, IconComponent> icons{GetMemoryResource(MemTag::UI)};

		struct TextKeyBinding {
			str key;
			str fallback;
		};
		HashMap<u32, TextKeyBinding> textKeys{GetMemoryResource(MemTag::UI)};
		u32 textKeyGeneration = 0;
		void WriteTextString(u32 index, std::string_view utf8);
		void ResolveTextKey(u32 index, const TextKeyBinding& binding);
		void RefreshTextKeys();

		HashMap<u32, ScrollBand> scrollShown{GetMemoryResource(MemTag::UI)};
		Vector<ElementHandle> scrollMoved{GetMemoryResource(MemTag::UI)};

		Vector<InputEvent> pendingInput{GetMemoryResource(MemTag::UI)};
		Vec2 lastMousePos{};
		bool mouseInside = false;
		bool hoverSuppressed = false;
		u32 pointerClaim = 0;
		u32 nextPointerClaim = 1;
		ElementHandle pointerClaimOwner{};
		CursorMode pointerClaimMode = CursorMode::Normal;
		u32 hovered = INVALID_UI_INDEX;
		u32 pressed = INVALID_UI_INDEX;
		u32 pressedRight = INVALID_UI_INDEX;
		u32 pressedMiddle = INVALID_UI_INDEX;
		Vec2 middleDragLast{};
		u32 focused = INVALID_UI_INDEX;
		i32 pressCaret = 0;
		bool pressSelectedAll = false;
		f64 caretBlinkStart = 0.0;
		SystemCursor desiredCursor = SystemCursor::Arrow;

		u32 lastClickIndex = INVALID_UI_INDEX;
		Vec2 lastClickPos{};
		std::chrono::steady_clock::time_point lastClickTime{};
		u32 clickStreak = 0;

		enum class PopupSide : u8 {
			None,
			Right,
			Below
		};
		struct PopupEntry {
			u32 index;
			PopupFlags flags;
			PopupSide side = PopupSide::None;
			Rect exclude{};
			Vec2 anchor{};
			ElementHandle focusBefore{};
			bool focusDroppedInside = false;
		};
		Vector<PopupEntry> popups{GetMemoryResource(MemTag::UI)};
		HashMap<u32, u32> popupDefaultButtons{GetMemoryResource(MemTag::UI)};
		u32 popupScrim = INVALID_UI_INDEX;

		HashMap<u32, String> tooltips{GetMemoryResource(MemTag::UI)};
		u32 tooltipSource = INVALID_UI_INDEX;
		f64 tooltipHoverStart = 0.0;
		u32 tooltipElement = INVALID_UI_INDEX;
		Vec2 tooltipAnchor{};
		bool tooltipShown = false;
		bool tooltipResolved = false;

		u32 dragTarget = INVALID_UI_INDEX;
		KeyMod lastMouseMods = KeyMod::None;
		KeyMod pressMods = KeyMod::None;
		Vector<StringHash> loadedSheetTags{GetMemoryResource(MemTag::UI)};

		Vector<ElementHandle> pendingDestroy{GetMemoryResource(MemTag::UI)};

		Vector<FlexItem> flexScratch{GetMemoryResource(MemTag::UI)};

		bool layoutDirty = true;

		Vector<ElementHandle> contentDirty{GetMemoryResource(MemTag::UI)};

		LayoutStats layoutStats;

		PaintStats paintStats;

		TreeStats treeStats;

		StyleStats styleStats;

		u32 treeRevision = 1;
		void BumpTreeRevision() { ++treeRevision; }

		u32 Allocate();
		void Free(u32 index);
		bool IsAliveHandle(ElementHandle handle) const;
		u32 ResolveIndex(ElementHandle handle) const;
		ElementHandle HandleFor(u32 index) const;
		void DetachFromParent(u32 index);
		void DestroyRecursive(u32 index);
		void MarkLayoutDirty(u32 index);
		void MarkLayoutDirtyUpwards(u32 index);
		void RemeasureAll();
		void MarkContentDirty(u32 index);
		void ResolveContentDirty();
		bool NotifyScrollMoved();
		bool FullPassWillRemeasure(u32 index) const;
		void InvalidatePaintInkUpwards(u32 index);

		void RecalcStyle(u32 index);
		void RecalcPointerState(u32 index);
		void RecalcAllStyles();
		void DrainStyle(u32 index);
		u8 ComposeStyle(u32 index);

		Vector<ElementHandle> inheritPending{GetMemoryResource(MemTag::UI)};
		Vector<u32> inheritScratch{GetMemoryResource(MemTag::UI)};
		bool flushingInherit = false;
		void QueueInherit(u32 index);
		// never call during a layout pass (layout gets marked dirty)
		void FlushInherit();
		void FillInherited(u32 index);
		u8 ResolveInherited(u32 index);
		void PropagateInherited(u32 index);
		u8 CurrentPseudo(const Element& element) const;

		TextComponent& AcquireText(u32 elementIndex);
		void ReleaseText(u32 elementIndex);
		RichView TextFor(u32 elementIndex, TextComponent& text, const TextMetrics& metrics);
		std::string_view ShownText(u32 elementIndex, const TextComponent& text) const;
		TextComponent* GetText(u32 elementIndex);
		const TextComponent* GetText(u32 elementIndex) const;
		Vec2 TextOriginPx(u32 elementIndex) const;
		TextInputState* GetTextInput(u32 elementIndex);
		TextOverflow EffectiveTextOverflow(u32 elementIndex) const;
		Align EffectiveTextAlign(u32 elementIndex) const;
		TextMetrics TextMetricsFor(u32 elementIndex);
		void EnsureShaped(u32 elementIndex);
		Vec2 MeasureTextExtent(u32 elementIndex, f32 wrapWidth, bool commit = true);
		void OnTextEdited(u32 elementIndex);
		const ShapedRun* ShapeCompletion(u32 elementIndex);
		ShapedRun completionRun;
		struct CompletionKey {
			u32 element = INVALID_UI_INDEX;
			u32 generation = 0;
			TextMetrics metrics;
			u32 atlasGeneration = 0;

			bool operator==(const CompletionKey&) const = default;
		};
		CompletionKey completionKey;
		String completionShaped{GetMemoryResource(MemTag::UI)};

		IconComponent* GetIcon(u32 elementIndex);
		const IconComponent* GetIcon(u32 elementIndex) const;
		IconComponent* GetPicture(u32 elementIndex);
		const IconComponent* GetPicture(u32 elementIndex) const;
		Vec2 MeasureIconExtent(u32 elementIndex);

		String TooltipTextFor(u32 elementIndex);
		bool ResolveTooltip();
		bool PlaceTooltip();

		void ProcessInput();
		void ResolveHoverFromCachedPointer();
		void ResolveDesiredCursor();
		u32 CaptureOwner() const;
		void ClearPointerClaim();
		void ExpireDeadPointerClaim();
		u32 HitTestIndex(Vec2 point, HitTestMode mode) const;
		u32 HitTestRecursive(u32 index, Vec2 point, HitTestMode mode, const Rect* clip) const;
		ElementTurn TurnOf(u32 index) const;
		// context px -> index's layout space (own rotate undone too)
		Vec2 ToLayoutSpace(u32 index, Vec2 point) const;
		void SetHovered(u32 index);
		void SetFocused(u32 index);
		struct CaretMark {
			u32 element = INVALID_UI_INDEX;
			i32 cursor = 0;
			i32 anchor = 0;
			size_t length = 0;
		};
		CaretMark MarkCaret();
		void RestartBlinkIfCaretMoved(const CaretMark& before);
		void FollowCaret();
		void ScrollIntoView(u32 index);
		void ScrollRangeIntoView(u32 index, f32 startY, f32 endY);
		static f32 ScrollAxisToShow(f32 offset, f32 windowSize, f32 targetMin, f32 targetMax, f32 maxOffset);
		i32 CaretIndexFromMouse(u32 elementIndex, Vec2 mouse);
		bool HandlePopupPress(Vec2 point);
		bool HandlePopupWheel(Vec2 point);
		bool CloseDismissibleTop();

		void ClosePopupsFrom(size_t firstClosed);
		bool IsPopupIndex(u32 index) const;
		size_t PopupContaining(u32 index) const;
		u32 FocusToHandBack(const PopupEntry& entry) const;
		bool PressPopupDefaultButton();
		bool PositionPopups();
		void UpdatePopupScrim();
		size_t LowestScrimmedPopup() const;
		bool IsBehindPopupScrim(u32 index) const;

		void SetDragTarget(u32 index);

		// idx must be a live slot
		Element& E(u32 index) { return elements[index]; }
		const Element& E(u32 index) const { return elements[index]; }
		// only access to a computed style (composition is deferred)
		Style& ComputedS(u32 index) {
			if (HasAnyFlag(elements[index].flags, ElementFlags::DirtyStyle | ElementFlags::DirtyInherit) || !inheritPending.empty()) {
				DrainStyle(index);
			}
			return computedStyles[index];
		}
		const Style& ComputedS(u32 index) const {
			return const_cast<ContextImpl*>(this)->ComputedS(index);
		}
		LayoutData& L(u32 index) { return layouts[index]; }
		const LayoutData& L(u32 index) const { return layouts[index]; }
		MeasureCache& M(u32 index) { return measureCaches[index]; }
		Rect& Ink(u32 index) { return subtreeInk[index]; }
		const Rect& Ink(u32 index) const { return subtreeInk[index]; }
	};

	template <typename T>
	inline constexpr char ElementModelType = 0;

	// one model per type T, released with element
	template <typename T>
	void SetElementModel(Context& context, ElementHandle handle, std::shared_ptr<T> model) {
		ContextImpl::SetModel(context, handle, &ElementModelType<T>, std::move(model));
	}

	template <typename T>
	std::shared_ptr<T> GetElementModel(const Context& context, ElementHandle handle) {
		return std::static_pointer_cast<T>(ContextImpl::GetModel(context, handle, &ElementModelType<T>));
	}

	namespace Layout {
		void UpdateLayout(ContextImpl& ctx);

		bool RemeasureInPlace(ContextImpl& ctx, u32 index);
	} // namespace Layout

	namespace Paint {
		void BuildRenderList(ContextImpl& ctx, RenderList& out);
	}

	inline f32 Snap(f32 v) {
		return SnapToPixel(v);
	}
	inline Rect SnapRect(const Rect& r) {
		return SnapRectToPixels(r);
	}

} // namespace Shalltear::UI
