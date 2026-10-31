#pragma once

#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorTheme.h"

#include <string_view>

namespace Shalltear::UIBuilderChrome {

	using EditorTheme::ColorToken;
	using EditorTheme::MetricToken;

	inline Color TextDim() { return EditorTheme::GetColor(ColorToken::TextDim); }
	inline Color Text() { return EditorTheme::GetColor(ColorToken::Text); }
	inline Color Accent() { return EditorTheme::GetColor(ColorToken::Accent); }
	inline Color TextFaint() { return EditorTheme::GetColor(ColorToken::TextFaint); }
	inline f32 RowFont() { return EditorTheme::GetMetric(MetricToken::FontSmall); }

	inline UI::ElementHandle PaneHeader(UI::Context& context, UI::ElementHandle parent, std::string_view title,
										Handle<FontAsset> font) {
		UI::ElementHandle header = context.CreateElement();
		context.AppendChild(parent, header);
		context.AddClass(header, EditorTheme::Class::SectionHeader);
		UI::Style style;
		style.padding = {6.0f, 3.0f, 6.0f, 3.0f};
		context.SetStyle(header, style);
		context.SetText(header, font, RowFont(), TextDim(), title);
		return header;
	}

	inline UI::ElementHandle SectionTitle(UI::Context& context, UI::ElementHandle parent, std::string_view title,
										  Handle<FontAsset> font) {
		UI::ElementHandle header = context.CreateElement();
		context.AppendChild(parent, header);
		context.AddClass(header, EditorTheme::Class::SectionHeader);
		UI::Style style;
		style.padding = {8.0f, 4.0f, 8.0f, 4.0f};
		style.margin = {0.0f, 4.0f, 0.0f, 0.0f};
		context.SetStyle(header, style);
		context.SetText(header, font, RowFont(), Text(), title);
		return header;
	}

	inline UI::ElementHandle RowShell(UI::Context& context, UI::ElementHandle parent, std::string_view label,
									  Handle<FontAsset> font, UI::ElementHandle& outValueParent) {
		UI::ElementHandle row = context.CreateElement();
		context.AppendChild(parent, row);
		UI::Style rowStyle;
		rowStyle.flexDirection = UI::FlexDirection::Row;
		rowStyle.alignItems = UI::Align::Center;
		rowStyle.padding = {8.0f, 1.5f, 8.0f, 1.5f};
		rowStyle.width = UI::Length::Percent(1.0f);
		context.SetStyle(row, rowStyle);

		UI::ElementHandle labelElement = context.CreateElement();
		context.AppendChild(row, labelElement);
		context.AddClass(labelElement, EditorTheme::Class::FieldLabel);
		UI::Style labelStyle;
		labelStyle.width = UI::Length::Percent(0.5f);
		context.SetStyle(labelElement, labelStyle);
		context.SetHoverable(labelElement, false);
		context.SetText(labelElement, font, RowFont(), TextDim(), label);

		UI::ElementHandle valueHost = context.CreateElement();
		context.AppendChild(row, valueHost);
		UI::Style valueStyle;
		valueStyle.width = UI::Length::Percent(0.5f);
		context.SetStyle(valueHost, valueStyle);
		outValueParent = valueHost;
		return row;
	}

	inline UI::ElementHandle LabeledRow(UI::Context& context, UI::ElementHandle parent, std::string_view label,
										std::string_view value, Handle<FontAsset> font) {
		UI::ElementHandle valueHost;
		UI::ElementHandle row = RowShell(context, parent, label, font, valueHost);
		context.AddClass(valueHost, EditorTheme::Class::RowText);
		context.SetHoverable(valueHost, false);
		context.SetText(valueHost, font, RowFont(), Text(), value);
		return row;
	}

	inline UI::ElementHandle FieldRow(UI::Context& context, UI::ElementHandle parent, std::string_view label,
									  std::string_view value, Handle<FontAsset> font,
									  std::function<void(std::string_view)> onCommit) {
		UI::ElementHandle valueHost;
		UI::ElementHandle row = RowShell(context, parent, label, font, valueHost);
		UI::Widgets::TextFieldDesc desc;
		desc.text = value;
		desc.font = font;
		desc.fontSizePx = RowFont();
		desc.onCommit = std::move(onCommit);
		UI::Widgets::TextField(context, valueHost, desc);
		return row;
	}

	inline void MarkRowOverridden(UI::Context& context, UI::ElementHandle row, Handle<FontAsset> font,
								  std::string_view label) {
		const UI::Element* element = context.GetElement(row);
		if (!element || element->firstChild == UI::INVALID_UI_INDEX) {
			return;
		}
		context.SetText(context.MakeHandle(element->firstChild), font, RowFont(), Text(), label);
	}

} // namespace Shalltear::UIBuilderChrome
