#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/UITypes.h"
#include "Shalltear/Core/Window/Window.h"

#include <optional>
#include <span>
#include <string_view>

namespace Shalltear::UI {

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

		std::optional<Length> left, top, right, bottom;

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

		Align textAlign = Align::Start;
		Align textAlignVertical = Align::Start;
		TextOverflow textOverflow = TextOverflow::Wrap;
		f32 textOutlineWidth = 0.0f;
		Color textOutlineColor = Color(0.0f, 0.0f, 0.0f, 1.0f);
		f32 letterSpacing = 0.0f;

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
	private:
		struct Entry {
			StringHash name;
			str value;
		};

		Vector<Entry> _entries{GetMemoryResource(MemTag::UI)};

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
