#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/DragDrop.h"

#include <functional>
#include <span>
#include <string_view>

namespace Shalltear {
	class FontAsset;
	class VectorAsset;
} // namespace Shalltear

namespace Shalltear::UI::Widgets {

	SHALLTEAR_CORE_API void EnsureSkin(Context& context);
	SHALLTEAR_CORE_API std::string_view GetDefaultSkinSource();

	struct ButtonDesc {
		std::string_view label;
		Handle<FontAsset> font;
		f32 fontSizePx = 13.0f;
		std::function<void()> onClick;
		ElementHandle defaultForPopup;
	};
	SHALLTEAR_CORE_API ElementHandle Button(Context& context, ElementHandle parent, const ButtonDesc& desc);

	struct CheckboxParts {
		ElementHandle box;
		ElementHandle mark;
	};
	SHALLTEAR_CORE_API CheckboxParts Checkbox(Context& context, ElementHandle parent, bool value, std::function<void(bool)> onChange);
	SHALLTEAR_CORE_API void SetCheckboxValue(Context& context, const CheckboxParts& parts, bool value);

	struct ToggleParts {
		ElementHandle track;
		ElementHandle knob;
	};
	SHALLTEAR_CORE_API ToggleParts Toggle(Context& context, ElementHandle parent, bool value, std::function<void(bool)> onChange);
	SHALLTEAR_CORE_API void SetToggleValue(Context& context, const ToggleParts& parts, bool value);
	SHALLTEAR_CORE_API bool GetToggleValue(const Context& context, const ToggleParts& parts);

	SHALLTEAR_CORE_API ElementHandle Separator(Context& context, ElementHandle parent);
	SHALLTEAR_CORE_API ElementHandle Spacer(Context& context, ElementHandle parent, f32 grow = 1.0f);

	struct TextFieldDesc {
		std::string_view text;
		Handle<FontAsset> font;
		f32 fontSizePx = 13.0f;
		std::function<void(std::string_view)> onCommit;
		std::function<bool(char32_t)> filter;
		bool multiline = false;
	};
	SHALLTEAR_CORE_API ElementHandle TextField(Context& context, ElementHandle parent, const TextFieldDesc& desc);

	inline constexpr i32 DEFAULT_DECIMALS = 5;

	struct NumberFieldDesc {
		f64 value = 0.0;
		f64 minValue = -1.0e300;
		f64 maxValue = 1.0e300;
		f64 dragScale = 0.1;
		i32 decimals = DEFAULT_DECIMALS;
		std::string_view label;
		Handle<FontAsset> font;
		f32 fontSizePx = 13.0f;
		std::function<void(f64)> onChange;
		std::function<void()> onScrubBegin;
		std::function<void()> onScrubEnd;
	};
	struct NumberFieldParts {
		ElementHandle root;
		ElementHandle labelElement;
		ElementHandle field;
	};
	SHALLTEAR_CORE_API NumberFieldParts NumberField(Context& context, ElementHandle parent, const NumberFieldDesc& desc);
	SHALLTEAR_CORE_API void SetNumberFieldValue(Context& context, const NumberFieldParts& parts, f64 value, i32 decimals = 3);
	SHALLTEAR_CORE_API f64 GetNumberFieldValue(const Context& context, const NumberFieldParts& parts);

	struct SliderDesc {
		f32 minValue = 0.0f;
		f32 maxValue = 1.0f;
		f32 value = 0.0f;
		bool showValueField = false;
		i32 decimals = 2;
		Handle<FontAsset> font;
		f32 fontSizePx = 13.0f;
		std::function<void(f32)> onChange;
		std::function<void()> onDragBegin;
		std::function<void()> onDragEnd;
	};
	struct SliderParts {
		ElementHandle root;
		ElementHandle track;
		ElementHandle fill;
		ElementHandle knob;
		NumberFieldParts value;
		i32 decimals = 2;
	};
	SHALLTEAR_CORE_API SliderParts Slider(Context& context, ElementHandle parent, const SliderDesc& desc);
	SHALLTEAR_CORE_API void SetSliderValue(Context& context, const SliderParts& parts, f32 minValue, f32 maxValue, f32 value);

	struct VectorFieldDesc {
		u32 componentCount = 3;
		f64 values[4] = {};
		const char* labels[4] = {"X", "Y", "Z", "W"};
		f64 minValue = -1.0e300;
		f64 maxValue = 1.0e300;
		f64 dragScale = 0.1;
		i32 decimals = DEFAULT_DECIMALS;
		Handle<FontAsset> font;
		f32 fontSizePx = 13.0f;
		std::function<void(u32, f64)> onChange;
		std::function<void()> onEditBegin;
		std::function<void()> onEditEnd;
	};
	struct VectorFieldParts {
		ElementHandle root;
		NumberFieldParts components[4];
	};
	SHALLTEAR_CORE_API VectorFieldParts VectorField(Context& context, ElementHandle parent, const VectorFieldDesc& desc);
	SHALLTEAR_CORE_API void SetVectorFieldValues(Context& context, const VectorFieldParts& parts, u32 componentCount, const f64* values, i32 decimals = 3);

	struct ColorFieldParts {
		ElementHandle root;
		ElementHandle swatch;
		VectorFieldParts vector;
	};
	SHALLTEAR_CORE_API ColorFieldParts ColorSwatchField(Context& context, ElementHandle parent, Color value,
														Handle<FontAsset> font, f32 fontSizePx,
														std::function<void(Color)> onChange,
														std::function<void()> onEditBegin = {}, std::function<void()> onEditEnd = {});
	// never write the components through SetVectorFieldValues
	SHALLTEAR_CORE_API void SetColorSwatchFieldValue(Context& context, const ColorFieldParts& parts, Color value);

	SHALLTEAR_CORE_API ElementHandle OpenColorPicker(Context& context, ElementHandle anchor, Color value,
													 Handle<FontAsset> font, f32 fontSizePx,
													 std::function<void(Color)> onChange,
													 std::function<void()> onEditBegin = {},
													 std::function<void()> onEditEnd = {});

	struct DropdownParts {
		ElementHandle root;
		ElementHandle label;
	};

	struct DropdownDesc {
		std::span<const std::string_view> items;
		u32 selected = 0;
		Handle<FontAsset> font;
		f32 fontSizePx = 13.0f;
		std::function<void(u32)> onSelect;
	};
	SHALLTEAR_CORE_API DropdownParts Dropdown(Context& context, ElementHandle parent, const DropdownDesc& desc);

	SHALLTEAR_CORE_API void SetDropdownMark(Handle<VectorAsset> mark, f32 sizeScale);
	SHALLTEAR_CORE_API void SetDropdownSelection(Context& context, const DropdownParts& parts,
												 std::span<const std::string_view> items, u32 selected);

	// bit i selects items[i], max 64 items
	struct MaskDropdownDesc {
		std::span<const std::string_view> items;
		u64 mask = 0;
		Handle<FontAsset> font;
		f32 fontSizePx = 13.0f;
		std::function<void(u64)> onChange;
	};
	SHALLTEAR_CORE_API DropdownParts MaskDropdown(Context& context, ElementHandle parent, const MaskDropdownDesc& desc);

	SHALLTEAR_CORE_API void SetMaskDropdownValue(Context& context, const DropdownParts& parts,
												 std::span<const std::string_view> items, u64 mask);

	inline constexpr std::string_view EdgeControlClass = "slt-edge-control";

	struct ScrollViewParts {
		ElementHandle root;
		ElementHandle viewport;
		ElementHandle track;
	};
	SHALLTEAR_CORE_API ScrollViewParts ScrollView(Context& context, ElementHandle parent);

	enum class SplitSizing : u8 {
		Ratio,
		FirstFixed,
		SecondFixed,
	};

	struct SplitDesc {
		FlexDirection axis = FlexDirection::Row;
		SplitSizing sizing = SplitSizing::Ratio;
		f32 position = 0.5f;
		f32 minFirstPx = 0.0f;
		f32 minSecondPx = 0.0f;
		std::function<void(f32 position)> onPositionChanged;
	};

	struct SplitParts {
		ElementHandle root;
		ElementHandle first;
		ElementHandle second;
		ElementHandle splitter;
	};
	SHALLTEAR_CORE_API SplitParts Split(Context& context, ElementHandle parent, const SplitDesc& desc);

	SHALLTEAR_CORE_API f32 GetSplitterThickness();

	struct TreeRowDesc {
		u32 depth = 0;
		// bit L = ancestor at lvl L has a later child
		u32 guideMask = 0;
		bool hasChildren = false;
		bool expanded = false;
		bool selected = false;
		std::string_view label;
		Handle<FontAsset> font;
		f32 fontSizePx = 13.0f;
		u64 userTag = 0;
		std::function<void(KeyMod)> onSelect;
		std::function<void()> onToggleExpand;
		std::function<void()> onDoubleClick;
		std::function<void()> onRightClick;
		std::function<void(Vec2)> onDragRow;
		std::function<bool(const DragPayload&)> onDragOver;
		std::function<void(const DragPayload&, Vec2 point)> onDrop;
	};
	struct TreeRowParts {
		ElementHandle row;
		ElementHandle arrow;
		ElementHandle labelElement;
	};
	SHALLTEAR_CORE_API TreeRowParts TreeRow(Context& context, ElementHandle parent, const TreeRowDesc& desc);

	SHALLTEAR_CORE_API void SetTreeSelection(Context& context, ElementHandle rowsParent, std::span<const u64> selectedTags);

	struct MenuItem {
		str label;
		str shortcut;
		bool enabled = true;
		bool separatorBefore = false;
		std::function<void()> onSelect;
		bool checked = false;
		Vector<MenuItem> children{GetMemoryResource(MemTag::UI)};
	};

	SHALLTEAR_CORE_API ElementHandle OpenMenu(Context& context, Vec2 anchorPx, std::span<const MenuItem> items,
											  Handle<FontAsset> font, f32 fontSizePx);

	struct MenuBarItemDesc {
		str label;
		std::function<void(Vector<MenuItem>&)> fill;
	};
	// bar + popups must agree about fontSizePx
	SHALLTEAR_CORE_API ElementHandle MenuBar(Context& context, ElementHandle parent,
											 std::span<const MenuBarItemDesc> menus,
											 Handle<FontAsset> font, f32 fontSizePx);

} // namespace Shalltear::UI::Widgets
