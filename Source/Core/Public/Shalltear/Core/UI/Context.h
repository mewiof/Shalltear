#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/EnumFlags.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Input/InputEvent.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Element.h"
#include "Shalltear/Core/UI/RenderList.h"
#include "Shalltear/Core/UI/Style.h"
#include "Shalltear/Core/UI/UITypes.h"
#include "Shalltear/Core/UI/VectorIcon.h"
#include "Shalltear/Core/Window/Window.h"

#include <functional>
#include <span>
#include <string_view>

namespace Shalltear {
	class FontAsset;
	class VectorAsset;
	class TextureAsset;
	class SpriteAsset;
	class UISystem;
} // namespace Shalltear

namespace Shalltear::UI {

	class IFontSystem;
	class IVectorIconSystem;
	class PaintScope;
	class Context;
	struct ContextImpl;
	struct DragPayload;
	struct ShapedRun;

	struct ScrollBand {
		Rect shown{};
		Vec2 extent{};

		bool operator==(const ScrollBand&) const = default;
	};

	// no bubbling, callbacks fire on the hit/focused element only
	struct ElementEvents {
		std::function<void()> onMouseDown;
		std::function<void()> onMouseUp;
		std::function<void()> onClick;
		std::function<void()> onRightClick;
		std::function<void()> onDoubleClick;
		std::function<void()> onHoverEnter;
		std::function<void()> onHoverLeave;
		// element's layout space px (rotate undone)
		std::function<void(Vec2)> onPressDrag;
		std::function<void()> onPressEnd;
		std::function<bool(Vec2 notches)> onWheel;
		std::function<void(const ScrollBand&)> onScrollChanged;
		std::function<void(Vec2 deltaPx)> onMiddleDrag;
		std::function<void()> onFocusGained;
		std::function<void()> onFocusLost;
		std::function<bool(Key, KeyMod)> onKeyDown;
		std::function<void(std::string_view)> onTextChange;
		std::function<void(std::string_view)> onTextCommit;
		// user edits only (checkbox 0/1, slider value, dropdown index)
		std::function<void(f64 value)> onValueChange;
		std::function<bool(char32_t)> onTextFilter;
		std::function<void()> onPopupClosed;
		std::function<bool(const DragPayload&)> onDragOver;
		// element's layout space px
		std::function<void(const DragPayload&, Vec2 point)> onDrop;
		std::function<void(Context&, ElementHandle, PaintScope&)> customDraw;
	};

	struct ContextDesc {
		Vec2I sizePx{};
		f32 contentScale = 1.0f;
		f32 doubleClickSeconds = 0.4f;
		f32 doubleClickDistancePx = 4.0f;
		Handle<FontAsset> tooltipFont;
		f32 tooltipFontSizePx = 12.0f;
		f32 tooltipDelaySeconds = 0.5f;
	};

	enum class HitTestMode : u8 {
		Box = 0,
		Precise
	};

	enum class ImageScaleMode : u8 {
		StretchToFill = 0,
		ScaleAndCrop,
		ScaleToFit,
		// sprite's border stays unscaled
		Sliced,
		// at most MAX_IMAGE_TILES tiles (larger tiles past that)
		Tiled
	};

	inline constexpr u32 MAX_IMAGE_TILES = 4096;

	enum class ImageFillMethod : u8 {
		None = 0,
		Horizontal,
		Vertical,
		Radial90,
		Radial180,
		Radial360
	};

	// Horizontal: Left/Right, Vertical: Top/Bottom, Radial90: a corner, Radial180/360: a side
	enum class ImageFillOrigin : u8 {
		Left = 0,
		Right,
		Top,
		Bottom,
		TopLeft,
		TopRight,
		BottomLeft,
		BottomRight
	};

	struct ImageFill {
		ImageFillMethod method = ImageFillMethod::None;
		ImageFillOrigin origin = ImageFillOrigin::Bottom;
		// 0-1
		f32 amount = 1.0f;
		bool clockwise = true;

		bool operator==(const ImageFill&) const = default;
	};

	struct ImageDesc {
		Handle<TextureAsset> texture;
		// used over texture when set
		Handle<SpriteAsset> sprite;
		ImageScaleMode scaleMode = ImageScaleMode::ScaleToFit;
		ImageFill fill;
		// border + tile size multiplier (Sliced/Tiled only)
		f32 sliceScale = 1.0f;

		bool operator==(const ImageDesc&) const = default;
	};

	enum class PopupFlags : u8 {
		None = 0,
		NoOutsideDismiss = 1 << 0,
		Center = 1 << 1,
		Scrim = 1 << 2,
	};
	SLT_ENUM_CLASS_FLAGS(PopupFlags)

	class SHALLTEAR_CORE_API Context {
	public:
		Context(const ContextDesc& desc, IFontSystem* fontSystem, IVectorIconSystem* iconSystem = nullptr);
		~Context();
		Context(const Context&) = delete;
		Context& operator=(const Context&) = delete;

		ElementHandle GetRoot() const;
		ElementHandle CreateElement();
		void DestroyElement(ElementHandle handle);
		// only safe way to destroy a subtree from its callback
		void DestroyElementNextUpdate(ElementHandle handle);
		void ClearChildren(ElementHandle parent);
		void AppendChild(ElementHandle parent, ElementHandle child);
		bool IsAlive(ElementHandle handle) const;
		u32 GetLiveCount() const;

		const Element* GetElement(ElementHandle handle) const;
		ElementHandle MakeHandle(u32 elementIndex) const;

		u32 GetTreeRevision() const;

		void SetVisible(ElementHandle handle, bool visible);
		void SetHoverable(ElementHandle handle, bool hoverable);
		void SetInheritsPointerState(ElementHandle handle, bool inherits);
		void SetFocusable(ElementHandle handle, bool focusable);
		void SetTint(ElementHandle handle, Color tint);
		void SetUserTag(ElementHandle handle, u64 tag);
		void SetScrollOffset(ElementHandle handle, Vec2 offset);
		void ScrollIntoView(ElementHandle handle);
		void ScrollRangeIntoView(ElementHandle scroller, f32 startY, f32 endY);

		void SetStyle(ElementHandle handle, const Style& style);
		const Style& GetStyle(ElementHandle handle) const;
		const Style& GetComputedStyle(ElementHandle handle) const;

		void SetStyleVar(StringHash name, std::string_view value);
		void SetStyleVars(std::span<const StyleVarDesc> descs);
		// name used by font-family values and <font> tags
		void SetStyleFont(std::string_view name, Handle<FontAsset> font);
		[[nodiscard]] const StyleVars& GetStyleVars() const;
		[[nodiscard]] Color GetStyleVarColor(StringHash name, Color fallback) const;
		[[nodiscard]] f32 GetStyleVarFloat(StringHash name, f32 fallback) const;

		void LoadStyleSheet(std::string_view cssSource);
		bool LoadStyleSheetOnce(StringHash tag, std::string_view cssSource);
		void ClearStyleSheets();
		[[nodiscard]] std::span<const StringHash> GetLoadedSheetTags() const;

		void AddClass(ElementHandle handle, StringHash klass);
		void RemoveClass(ElementHandle handle, StringHash klass);
		bool HasClass(ElementHandle handle, StringHash klass) const;
		void SetId(ElementHandle handle, StringHash id);
		void SetTypeTag(ElementHandle handle, StringHash tag);

		// SRGB color. font/size/color used over inherited style (not declared style)
		void SetText(ElementHandle handle, Handle<FontAsset> font, f32 sizePx, Color color, std::string_view utf8);
		// no prior SetText = font/size/color from style
		void SetTextString(ElementHandle handle, std::string_view utf8);
		// tags like <b>/<color=#f00> parsed (never in a text input)
		void SetRichText(ElementHandle handle, bool enabled);
		str GetTextString(ElementHandle handle) const;
		void ClearText(ElementHandle handle);

		void SetTextInput(ElementHandle handle, bool enabled);
		bool HasTextInput(ElementHandle handle) const;
		void SetTextInputMultiline(ElementHandle handle, bool multiline);
		void SetTextInputCompletion(ElementHandle handle, std::string_view suffix, Color color);

		struct ShapedTextView {
			const ShapedRun* run = nullptr;
			Vec2 originPx{};
			// tags removed (indexed by CaretStop::byteOffset), valid until next text change
			std::string_view text;
		};
		[[nodiscard]] ShapedTextView GetShapedText(ElementHandle handle) const;

		void SetIcon(ElementHandle handle, Handle<VectorAsset> asset, f32 sizePx,
					 const IconPalette& palette = {});
		void SetIconPalette(ElementHandle handle, const IconPalette& palette);
		void ClearIcon(ElementHandle handle);
		[[nodiscard]] bool HasIcon(ElementHandle handle) const;
		[[nodiscard]] Handle<VectorAsset> GetIconAsset(ElementHandle handle) const;

		void SetImage(ElementHandle handle, Handle<TextureAsset> asset, f32 sizePx);
		// native size, drawn into content box by scaleMode
		void SetImage(ElementHandle handle, const ImageDesc& image);
		// null unless SetImage(handle, ImageDesc) set it
		[[nodiscard]] const ImageDesc* GetImage(ElementHandle handle) const;
		void ClearImage(ElementHandle handle);
		[[nodiscard]] bool HasImage(ElementHandle handle) const;
		[[nodiscard]] Handle<TextureAsset> GetImageAsset(ElementHandle handle) const;

		void SetTooltip(ElementHandle handle, std::string_view text);

		ElementEvents& Events(ElementHandle handle);
		[[nodiscard]] bool HasEvents(ElementHandle handle) const;

		// physical px
		void OpenPopup(ElementHandle content, Vec2 anchorPx, PopupFlags flags = PopupFlags::None, Rect excludePx = {});
		void ClosePopup(ElementHandle content);
		void CloseAllPopups();
		bool IsPopupOpen(ElementHandle content) const;
		u32 GetOpenPopupCount() const;
		ElementHandle GetOpenPopup(u32 index) const;

		void SetPopupDefaultButton(ElementHandle content, ElementHandle button);

		void InjectInput(const InputEvent& event);
		ElementHandle GetFocused() const;
		ElementHandle GetHovered() const;
		void ClearFocus();
		void SetFocus(ElementHandle handle);
		void SetHoverSuppressed(bool suppressed);
		bool IsHoverSuppressed() const;
		u32 ClaimPointer(ElementHandle owner, CursorMode mode);
		void ReleasePointer(u32 claim);
		CursorMode GetPointerClaimMode() const;

		KeyMod GetPointerMods() const;
		KeyMod GetPressMods() const;
		Vec2 GetPointerPosition() const;
		// element's layout space px (rotate undone)
		Vec2 GetPointerPosition(ElementHandle element) const;
		bool WantsMouse() const;
		bool WantsKeyboard() const;
		SystemCursor GetDesiredCursor() const;

		void Update();
		void BuildRenderList(RenderList& out);

		ElementHandle HitTest(Vec2 point, HitTestMode mode = HitTestMode::Box) const;

		ElementHandle UpdateDragTarget(const DragPayload& payload, Vec2 point);
		void ClearDragTarget();
		bool DeliverDrop(const DragPayload& payload, Vec2 point);

		void Resize(Vec2I sizePx);
		void SetContentScale(f32 scale);
		Vec2I GetSize() const;
		f32 GetContentScale() const;

		const LayoutData& GetLayout(ElementHandle handle) const;

		const LayoutStats& GetLayoutStats() const;
		void ResetLayoutStats();

		const PaintStats& GetPaintStats() const;
		void ResetPaintStats();

		const TreeStats& GetTreeStats() const;
		void ResetTreeStats();

		const StyleStats& GetStyleStats() const;
		void ResetStyleStats();

		IFontSystem* GetFontSystem() const;
	private:
		friend class LayoutSystem;
		friend class Painter;
		friend struct ContextImpl;
		friend class Shalltear::UISystem;

		void RouteInput();
		void SettleLayout();

		ContextImpl* _impl = nullptr;
	};

} // namespace Shalltear::UI
