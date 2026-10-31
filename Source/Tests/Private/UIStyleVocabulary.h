#pragma once

#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Style.h"
#include "Shalltear/Core/Window/Window.h"

namespace Shalltear::StyleVocabulary {

	struct Entry {
		const char* field;
		const char* declaration;
		void (*mutate)(UI::Style&);
	};

	inline constexpr Entry All[] = {
		{"display", "display: none", [](UI::Style& s) { s.display = UI::Display::None; }},
		{"position", "position: absolute", [](UI::Style& s) { s.position = UI::Position::Absolute; }},
		{"flexDirection", "flex-direction: column", [](UI::Style& s) { s.flexDirection = UI::FlexDirection::Column; }},
		{"justifyContent", "justify-content: space-between", [](UI::Style& s) { s.justifyContent = UI::Align::SpaceBetween; }},
		{"alignItems", "align-items: center", [](UI::Style& s) { s.alignItems = UI::Align::Center; }},
		{"alignSelf", "align-self: flex-end", [](UI::Style& s) { s.alignSelf = UI::Align::End; }},
		{"overflow", "overflow: scroll", [](UI::Style& s) { s.overflow = UI::Overflow::Scroll; }},
		{"width", "width: 120px", [](UI::Style& s) { s.width = UI::Length::Pixel(120.0f); }},
		{"height", "height: 50%", [](UI::Style& s) { s.height = UI::Length::Percent(0.5f); }},
		{"minWidth", "min-width: 5px", [](UI::Style& s) { s.minWidth = UI::Length::Pixel(5.0f); }},
		{"minHeight", "min-height: 6px", [](UI::Style& s) { s.minHeight = UI::Length::Pixel(6.0f); }},
		{"maxWidth", "max-width: 7px", [](UI::Style& s) { s.maxWidth = UI::Length::Pixel(7.0f); }},
		{"maxHeight", "max-height: 8px", [](UI::Style& s) { s.maxHeight = UI::Length::Pixel(8.0f); }},
		{"margin", "margin: 1px 2px 3px 4px", [](UI::Style& s) { s.margin = UI::Edges{4.0f, 1.0f, 2.0f, 3.0f}; }},
		{"padding", "padding: 5px 6px 7px 8px", [](UI::Style& s) { s.padding = UI::Edges{8.0f, 5.0f, 6.0f, 7.0f}; }},
		{"flexGrow", "flex-grow: 2", [](UI::Style& s) { s.flexGrow = 2.0f; }},
		{"flexShrink", "flex-shrink: 0", [](UI::Style& s) { s.flexShrink = 0.0f; }},
		{"left", "left: 3px", [](UI::Style& s) { s.left = UI::Length::Pixel(3.0f); }},
		{"top", "top: 4px", [](UI::Style& s) { s.top = UI::Length::Pixel(4.0f); }},
		{"right", "right: 5px", [](UI::Style& s) { s.right = UI::Length::Pixel(5.0f); }},
		{"bottom", "bottom: 6px", [](UI::Style& s) { s.bottom = UI::Length::Pixel(6.0f); }},

		{"background", "background: #123456", [](UI::Style& s) { s.background = UI::Brush::SolidColor(Color::FromHex("#123456")); }},
		{"borderColor", "border-color: #654321", [](UI::Style& s) { s.borderColor = Color::FromHex("#654321"); }},
		{"borderWidth", "border-width: 2px", [](UI::Style& s) { s.borderWidth = UI::Edges::All(2.0f); }},
		{"borderRadius", "border-radius: 4px", [](UI::Style& s) { s.borderRadius = UI::Radii::All(4.0f); }},
		{"boxShadows", "box-shadow: 1px 2px 3px 4px #00000080", [](UI::Style& s) { s.AddBoxShadow(UI::BoxShadow{1.0f, 2.0f, 3.0f, 4.0f, Color::FromHex("#00000080"), false}); }},
		{"dropShadow", "filter: drop-shadow(1px 2px 3px #00000080)", [](UI::Style& s) { s.dropShadow = UI::DropShadow{1.0f, 2.0f, 3.0f, Color::FromHex("#00000080")}; }},
		{"opacity", "opacity: 0.5", [](UI::Style& s) { s.opacity = 0.5f; }},
		{"zIndex", "z-index: 3", [](UI::Style& s) { s.zIndex = 3; }},

		{"textAlign", "text-align: center", [](UI::Style& s) { s.textAlign = UI::Align::Center; }},
		{"textAlignVertical", "text-align-vertical: center", [](UI::Style& s) { s.textAlignVertical = UI::Align::Center; }},
		{"textOverflow", "text-overflow: ellipsis", [](UI::Style& s) { s.textOverflow = UI::TextOverflow::Ellipsis; }},
		{"textOutlineWidth", "text-outline-width: 1.5", [](UI::Style& s) { s.textOutlineWidth = 1.5f; }},
		{"textOutlineColor", "text-outline-color: #abcdef", [](UI::Style& s) { s.textOutlineColor = Color::FromHex("#abcdef"); }},
		{"letterSpacing", "letter-spacing: 1.5", [](UI::Style& s) { s.letterSpacing = 1.5f; }},

		{"pointerEvents", "pointer-events: none", [](UI::Style& s) { s.pointerEvents = false; }},
		{"cursor", "cursor: pointer", [](UI::Style& s) { s.cursor = SystemCursor::Hand; }},
	};

	inline str EveryDeclaration() {
		str fragment;
		for (const Entry& entry : All) {
			if (!fragment.empty()) {
				fragment += "; ";
			}
			fragment += entry.declaration;
		}
		return fragment;
	}

	inline UI::Style EveryDeclarationAsStyle() {
		UI::Style style;
		for (const Entry& entry : All) {
			entry.mutate(style);
		}
		return style;
	}

} // namespace Shalltear::StyleVocabulary
