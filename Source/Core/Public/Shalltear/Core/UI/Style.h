#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/UITypes.h"
#include "Shalltear/Core/Window/Window.h"

#include <bit>
#include <optional>
#include <span>
#include <string_view>

namespace Shalltear {
	class FontAsset;
}

namespace Shalltear::UI {

	template <typename T>
	struct StyleUnset;

	template <>
	struct StyleUnset<f32> {
		static constexpr u32 BITS = 0x7FC0FEEDu;
		static constexpr f32 Value() { return std::bit_cast<f32>(BITS); }
		static constexpr bool Is(f32 value) { return std::bit_cast<u32>(value) == BITS; }
		static constexpr bool Same(f32 a, f32 b) { return std::bit_cast<u32>(a) == std::bit_cast<u32>(b); }
	};

	template <>
	struct StyleUnset<Color> {
		static constexpr Color Value() { return Color(StyleUnset<f32>::Value(), 0.0f, 0.0f, 0.0f); }
		static constexpr bool Is(const Color& value) { return StyleUnset<f32>::Is(value.r); }
		static constexpr bool Same(const Color& a, const Color& b) {
			return StyleUnset<f32>::Same(a.r, b.r) && StyleUnset<f32>::Same(a.g, b.g) && StyleUnset<f32>::Same(a.b, b.b) &&
				   StyleUnset<f32>::Same(a.a, b.a);
		}
	};

	template <>
	struct StyleUnset<Length> {
		static constexpr Length Value() { return {static_cast<Length::Unit>(0xFF), 0.0f}; }
		static constexpr bool Is(const Length& value) { return value.unit == static_cast<Length::Unit>(0xFF); }
		static constexpr bool Same(const Length& a, const Length& b) { return a.unit == b.unit && StyleUnset<f32>::Same(a.value, b.value); }
	};

	template <>
	struct StyleUnset<u16> {
		static constexpr u16 Value() { return 0; }
		static constexpr bool Is(u16 value) { return value == 0; }
		static constexpr bool Same(u16 a, u16 b) { return a == b; }
	};

	template <>
	struct StyleUnset<FontStyle> {
		static constexpr FontStyle Value() { return static_cast<FontStyle>(0xFF); }
		static constexpr bool Is(FontStyle value) { return value == Value(); }
		static constexpr bool Same(FontStyle a, FontStyle b) { return a == b; }
	};

	template <typename T>
	struct StyleUnset<Handle<T>> {
		static Handle<T> Value() { return std::bit_cast<Handle<T>>(~u64{0}); }
		static bool Is(const Handle<T>& value) { return value.GetInstanceId() == ~u64{0}; }
		static bool Same(const Handle<T>& a, const Handle<T>& b) { return a == b; }
	};

	// std::optional without bool. Unset = StyleUnset<T> (never a real value)
	template <typename T>
	class StyleValue {
	public:
		StyleValue() = default;
		StyleValue(std::nullopt_t) {}
		StyleValue(const T& value) : _value(value) {}

		StyleValue& operator=(const T& value) {
			_value = value;
			return *this;
		}
		StyleValue& operator=(std::nullopt_t) {
			reset();
			return *this;
		}

		[[nodiscard]] bool has_value() const { return !StyleUnset<T>::Is(_value); }
		explicit operator bool() const { return has_value(); }
		const T& operator*() const { return _value; }
		const T* operator->() const { return &_value; }
		[[nodiscard]] T value_or(const T& fallback) const { return has_value() ? _value : fallback; }
		void reset() { _value = StyleUnset<T>::Value(); }

		bool operator==(const StyleValue& other) const { return StyleUnset<T>::Same(_value, other._value); }
		friend bool operator==(const StyleValue& a, const T& b) { return a.has_value() && a._value == b; }
	private:
		T _value = StyleUnset<T>::Value();
	};

	// colors in SRGB
	struct Style {
		Display display = Display::Flex;
		Position position = Position::Relative;
		FlexDirection flexDirection = FlexDirection::Row;
		Align justifyContent = Align::Start;
		Align alignItems = Align::Stretch;
		Align alignSelf = Align::Auto;
		Overflow overflow = Overflow::Visible;

		Length width = Length::Auto();
		Length height = Length::Auto();
		Length minWidth = Length::Auto();
		Length minHeight = Length::Auto();
		Length maxWidth = Length::Auto();
		Length maxHeight = Length::Auto();
		Edges margin{};
		Edges padding{};
		f32 flexGrow = 0.0f;
		f32 flexShrink = 1.0f;

		StyleValue<Length> left, top, right, bottom;

		Brush background{};
		Color borderColor = Color(0.0f, 0.0f, 0.0f, 1.0f);
		Edges borderWidth{};
		Radii borderRadius{};

		static constexpr u32 MAX_BOX_SHADOWS = 6;
		BoxShadow boxShadows[MAX_BOX_SHADOWS]{};
		u8 boxShadowCount = 0;
		DropShadow dropShadow{};

		f32 opacity = 1.0f;
		i32 zIndex = 0;

		// deg clockwise (+y down) about transformOrigin, layout untouched
		f32 rotate = 0.0f;
		TransformOrigin transformOrigin{};

		// unset = parent's value (never unset in a computed style)
		StyleValue<Handle<FontAsset>> font;
		// Percent = fraction of parent's font size. Computed = Pixel
		StyleValue<Length> fontSize;
		StyleValue<Color> color;
		StyleValue<Color> textOutlineColor;
		// Percent = fraction of font size (not of parent)
		StyleValue<Length> lineHeight;
		StyleValue<f32> letterSpacing;
		StyleValue<f32> textOutlineWidth;
		StyleValue<u16> fontWeight;
		StyleValue<FontStyle> fontStyle;

		Align textAlign = Align::Start;
		Align textAlignVertical = Align::Start;
		TextOverflow textOverflow = TextOverflow::Wrap;

		bool pointerEvents = true;
		SystemCursor cursor = SystemCursor::Arrow;

		void AddBoxShadow(const BoxShadow& shadow) {
			if (boxShadowCount < MAX_BOX_SHADOWS) {
				boxShadows[boxShadowCount++] = shadow;
			}
		}

		bool operator==(const Style&) const = default;
	};

	SHALLTEAR_CORE_API std::span<const std::string_view> GetStylePropertyNames();

	SHALLTEAR_CORE_API bool IsInheritedStyleProperty(StringHash name);

	inline constexpr size_t MAX_STYLE_VALUE_TEXT = 256;

	SHALLTEAR_CORE_API std::string_view ReadStyleProperty(const Style& style, StringHash name,
														  char* buffer, size_t capacity);

	class StyleVars;

	SHALLTEAR_CORE_API bool ApplyStyleDeclaration(Style& style, StringHash name, std::string_view value,
												  const StyleVars* vars);

	struct StyleVarDesc {
		const char* name;
		const char* value;
	};

	SHALLTEAR_CORE_API std::span<const StyleVarDesc> GetDefaultStyleVars();

	class SHALLTEAR_CORE_API StyleVars {
	public:
		static constexpr size_t MAX_EXPANSION = 512;
		static constexpr u32 MAX_DEPTH = 4;

		void Set(StringHash name, std::string_view value);
		void Set(std::span<const StyleVarDesc> descs);
		void Clear();

		[[nodiscard]] bool Has(StringHash name) const;
		[[nodiscard]] std::string_view Get(StringHash name) const;
		[[nodiscard]] u32 GetCount() const;

		[[nodiscard]] std::string_view Expand(std::string_view value, char* buffer, size_t capacity) const;

		[[nodiscard]] Color GetColor(StringHash name, Color fallback) const;
		[[nodiscard]] f32 GetFloat(StringHash name, f32 fallback) const;

		void SetFont(std::string_view name, Handle<FontAsset> font);
		[[nodiscard]] bool FindFont(std::string_view name, Handle<FontAsset>& out) const;
	private:
		struct Entry {
			StringHash name;
			str value;
		};
		struct FontEntry {
			StringHash name;
			Handle<FontAsset> font;
		};

		Vector<Entry> _entries{GetMemoryResource(MemTag::UI)};
		Vector<FontEntry> _fonts{GetMemoryResource(MemTag::UI)};

		void ExpandInto(std::string_view value, char* buffer, size_t capacity, size_t& length, bool& overflow, u32 depth) const;
	};

	inline constexpr u8 PSEUDO_NONE = 0;
	inline constexpr u8 PSEUDO_HOVER = 1;
	inline constexpr u8 PSEUDO_ACTIVE = 2;
	inline constexpr u8 PSEUDO_FOCUS = 3;
	inline constexpr u8 PSEUDO_DROP = 4;

	struct StyleProperty {
		StringHash name;
		str value;
		bool hasVar = false;
	};

	// simple selectors only (no descendant combinators)
	struct StyleRule {
		StringHash targetTag;
		StringHash targetClass;
		StringHash targetId;
		u8 pseudoState = PSEUDO_NONE;
		i32 specificity = 0;

		Vector<StyleProperty> properties{GetMemoryResource(MemTag::UI)};
	};

	struct StyleDeclarationSource {
		std::string_view name;
		std::string_view value;
		std::string_view text;
	};

	struct StyleRuleSource {
		std::string_view selector;
		std::string_view body;
		Vector<StyleDeclarationSource> declarations{GetMemoryResource(MemTag::UI)};
	};

	class SHALLTEAR_CORE_API StyleSheet {
	public:
		void Parse(std::string_view source);
		void Clear();

		static void SplitSource(std::string_view source, Vector<StyleRuleSource>& out);
		static void SplitDeclarations(std::string_view declarations, Vector<StyleDeclarationSource>& out);

		[[nodiscard]] const Vector<StyleRule>& GetRules() const { return _rules; }

		static void ApplyRule(Style& style, const StyleRule& rule, const StyleVars* vars = nullptr);
	private:
		Vector<StyleRule> _rules{GetMemoryResource(MemTag::UI)};
	};

} // namespace Shalltear::UI
