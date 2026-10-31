#pragma once

#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/UISystem.h"

#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorTheme.h"

#include <string_view>

namespace Shalltear::PanelEmptyState {

	struct Desc {
		EditorIcons::Icon icon;
		std::string_view title;
		std::string_view hint;
	};

	inline UI::ElementHandle Build(UI::Context& context, UI::ElementHandle parent) {
		UI::ElementHandle host = context.CreateElement();
		context.AppendChild(parent, host);
		UI::Style style;
		style.display = UI::Display::None;
		style.flexDirection = UI::FlexDirection::Column;
		style.alignItems = UI::Align::Center;
		style.justifyContent = UI::Align::Center;
		style.flexGrow = 1.0f;
		style.padding = UI::Edges::All(16.0f);
		context.SetStyle(host, style);
		context.SetHoverable(host, false);
		return host;
	}

	namespace Detail {

		inline void SetDisplay(UI::Context& context, UI::ElementHandle element, UI::Display display) {
			if (!context.IsAlive(element)) {
				return;
			}
			UI::Style style = context.GetStyle(element);
			if (style.display == display) {
				return;
			}
			style.display = display;
			context.SetStyle(element, style);
		}

		inline UI::ElementHandle Line(UI::Context& context, UI::ElementHandle host, std::string_view text,
									  EditorTheme::ColorToken color, EditorTheme::MetricToken size) {
			UI::ElementHandle line = context.CreateElement();
			context.AppendChild(host, line);
			context.SetHoverable(line, false);
			UI::Style style;
			style.textAlign = UI::Align::Center;
			context.SetStyle(line, style);
			context.SetText(line, UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{},
							EditorTheme::GetMetric(size), EditorTheme::GetColor(color), text);
			return line;
		}

		inline bool Shows(UI::Context& context, UI::ElementHandle host, const Desc& desc) {
			const UI::Element* pod = context.GetElement(host);
			if (!pod || pod->firstChild == UI::INVALID_UI_INDEX) {
				return false;
			}
			UI::ElementHandle icon = context.MakeHandle(pod->firstChild);
			const UI::Element* iconPod = context.GetElement(icon);
			UI::ElementHandle title = iconPod ? context.MakeHandle(iconPod->nextSibling) : UI::ElementHandle{};
			if (!context.IsAlive(title) || context.GetTextString(title) != desc.title) {
				return false;
			}
			const UI::Element* titlePod = context.GetElement(title);
			const bool hasHint = titlePod && titlePod->nextSibling != UI::INVALID_UI_INDEX;
			if (!hasHint) {
				return desc.hint.empty();
			}
			return context.GetTextString(context.MakeHandle(titlePod->nextSibling)) == desc.hint;
		}

	} // namespace Detail

	inline void Show(UI::Context& context, UI::ElementHandle host, UI::ElementHandle content, const Desc& desc) {
		if (!context.IsAlive(host)) {
			return;
		}
		Detail::SetDisplay(context, content, UI::Display::None);
		Detail::SetDisplay(context, host, UI::Display::Flex);
		if (Detail::Shows(context, host, desc)) {
			return;
		}
		context.ClearChildren(host);

		UI::ElementHandle icon = EditorIcons::Add(context, host, desc.icon,
												  EditorTheme::GetColor(EditorTheme::ColorToken::TextFaint),
												  EditorTheme::GetMetric(EditorTheme::MetricToken::IconEmptyState), 0.0f);
		UI::Style iconStyle = context.GetStyle(icon);
		iconStyle.margin = {0.0f, 0.0f, 0.0f, 10.0f};
		context.SetStyle(icon, iconStyle);

		Detail::Line(context, host, desc.title, EditorTheme::ColorToken::TextDim, EditorTheme::MetricToken::FontBody);
		if (!desc.hint.empty()) {
			UI::ElementHandle hint = Detail::Line(context, host, desc.hint, EditorTheme::ColorToken::TextFaint,
												  EditorTheme::MetricToken::FontSmall);
			UI::Style hintStyle = context.GetStyle(hint);
			hintStyle.margin = {0.0f, 4.0f, 0.0f, 0.0f};
			context.SetStyle(hint, hintStyle);
		}
	}

	inline void Hide(UI::Context& context, UI::ElementHandle host, UI::ElementHandle content) {
		Detail::SetDisplay(context, host, UI::Display::None);
		Detail::SetDisplay(context, content, UI::Display::Flex);
	}

} // namespace Shalltear::PanelEmptyState
