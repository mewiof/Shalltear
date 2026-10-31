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

#include <chrono>
#include <cmath>
#include <limits>

namespace Shalltear::UI {

	inline constexpr Rect UnknownInk = {0.0f, 0.0f, -1.0f, -1.0f};

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

	struct TextComponent {
		Handle<FontAsset> font;
		f32 sizePx = 16.0f;
		Color color = Color::White();
		String utf8{GetMemoryResource(MemTag::UI)};

		ShapedRun shaped;
		f32 wrapWidth = 0.0f;
		f32 shapedWrapWidth = -1.0f;
		f32 cachedOutlineWidth = 0.0f;
		f32 cachedLetterSpacing = 0.0f;
		f32 cachedSizePhysical = 0.0f;
		TextOverflow cachedOverflow = TextOverflow::Wrap;
		u32 shapedGeneration = 0;
		Vec2 measuredExtent{};

		struct SizeEntry {
			f32 wrapWidth;
			Vec2 extent;
		};
		static constexpr size_t MAX_SIZE_ENTRIES = 4;
		Vector<SizeEntry> sizeCache{GetMemoryResource(MemTag::UI)};
		f32 memoSizePhysical = -1.0f;
		f32 memoOutlineWidth = -1.0f;
		f32 memoLetterSpacing = 0.0f;
		TextOverflow memoOverflow = TextOverflow::Wrap;

		bool dirty = true;
		bool inUse = false;

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
		}
	};

	inline constexpr ElementFlags ELEMENT_HAS_PICTURE = ElementFlags::HasIcon | ElementFlags::HasImage;

	struct IconComponent {
		Handle<VectorAsset> asset;
		Handle<TextureAsset> texture;
		f32 sizePx = 16.0f;
		IconPalette palette;

		Vec2 measuredExtent{};
		f32 cachedSizePhysical = -1.0f;
	};

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
		IVectorIconSystem* iconSystem = nullptr;

		Vector<Element> elements{GetMemoryResource(MemTag::UI)};
		Vector<Style> inlineStyles{GetMemoryResource(MemTag::UI)};
		Vector<Style> computedStyles{GetMemoryResource(MemTag::UI)};
		Vector<LayoutData> layouts{GetMemoryResource(MemTag::UI)};
		Vector<MeasureCache> measureCaches{GetMemoryResource(MemTag::UI)};
		Vector<Rect> subtreeInk{GetMemoryResource(MemTag::UI)};
		Vector<u32> freeList{GetMemoryResource(MemTag::UI)};
		u32 root = 0;

		StyleSheet styleSheet;
		StyleVars styleVars;

		Vector<TextComponent> texts{GetMemoryResource(MemTag::UI)};
		Vector<u32> textFreeList{GetMemoryResource(MemTag::UI)};
		Vector<TextInputState> textInputs{GetMemoryResource(MemTag::UI)};
		Vector<u32> textInputFreeList{GetMemoryResource(MemTag::UI)};
		HashMap<u32, ElementEvents> events{GetMemoryResource(MemTag::UI)};
		HashMap<u32, IconComponent> icons{GetMemoryResource(MemTag::UI)};

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
		void MarkContentDirty(u32 index);
		void ResolveContentDirty();
		bool NotifyScrollMoved();
		bool FullPassWillRemeasure(u32 index) const;
		void InvalidatePaintInkUpwards(u32 index);

		void RecalcStyle(u32 index);
		void RecalcPointerState(u32 index);
		void RecalcAllStyles();
		void DrainStyle(u32 index);
		void ComposeStyle(u32 index);
		u8 CurrentPseudo(const Element& element) const;

		TextComponent* GetText(u32 elementIndex);
		const TextComponent* GetText(u32 elementIndex) const;
		Vec2 TextOriginPx(u32 elementIndex) const;
		TextInputState* GetTextInput(u32 elementIndex);
		TextOverflow EffectiveTextOverflow(u32 elementIndex) const;
		void EnsureShaped(u32 elementIndex);
		Vec2 MeasureTextExtent(u32 elementIndex, f32 wrapWidth, bool commit = true);
		void OnTextEdited(u32 elementIndex);
		const ShapedRun* ShapeCompletion(u32 elementIndex);
		ShapedRun completionRun;
		struct CompletionKey {
			u32 element = INVALID_UI_INDEX;
			u32 generation = 0;
			f32 sizePhysical = -1.0f;
			f32 letterSpacing = 0.0f;
			u32 atlasGeneration = 0;
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
			DrainStyle(index);
			return computedStyles[index];
		}
		const Style& ComputedS(u32 index) const {
			const_cast<ContextImpl*>(this)->DrainStyle(index);
			return computedStyles[index];
		}
		LayoutData& L(u32 index) { return layouts[index]; }
		const LayoutData& L(u32 index) const { return layouts[index]; }
		MeasureCache& M(u32 index) { return measureCaches[index]; }
		Rect& Ink(u32 index) { return subtreeInk[index]; }
		const Rect& Ink(u32 index) const { return subtreeInk[index]; }
	};

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
