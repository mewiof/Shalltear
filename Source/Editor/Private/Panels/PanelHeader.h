#pragma once

#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorIcons.h"
#include "Shalltear/Editor/EditorTheme.h"

#include <functional>
#include <string_view>

namespace Shalltear::PanelHeader {

	namespace Detail {

		inline Color ChromeRest() { return EditorTheme::GetColor(EditorTheme::ColorToken::TextDim); }
		inline f32 ChromeTextPx() { return EditorTheme::GetMetric(EditorTheme::MetricToken::FontBody); }

		inline Color ChromeChecked() { return EditorTheme::GetColor(EditorTheme::ColorToken::TextStrong); }

	} // namespace Detail

	struct ToolButtonDesc {
		EditorIcons::Icon icon;
		std::string_view fallback;
		std::string_view tooltip;
		std::function<void()> onClick;
		bool checked = false;
	};

	inline EditorIcons::IconButtonParts ToolButton(UI::Context& context, UI::ElementHandle parent,
												   const ToolButtonDesc& desc) {
		EditorIcons::IconButtonParts parts = EditorIcons::IconButton(
			context, parent, desc.icon, desc.fallback,
			desc.tooltip.empty() ? desc.fallback : desc.tooltip, desc.onClick, Detail::ChromeTextPx());
		EditorIcons::SetGlyphOrText(context, parts.face, desc.icon, desc.fallback,
									desc.checked ? Detail::ChromeChecked() : Detail::ChromeRest(),
									Detail::ChromeTextPx(), Detail::ChromeTextPx());
		context.AddClass(parts.button, EditorTheme::Class::ToolButton);
		if (desc.checked) {
			context.AddClass(parts.button, EditorTheme::Class::ToolButtonOn);
		}
		if (context.IsAlive(parts.face)) {
			UI::Style faceStyle = context.GetStyle(parts.face);
			faceStyle.width = UI::Length::Pixel(EditorIcons::LineBoxHeight(Detail::ChromeTextPx()));
			faceStyle.textAlign = UI::Align::Center;
			context.SetStyle(parts.face, faceStyle);
		}
		context.AddClass(parts.button, EditorTheme::Class::IconButton);
		return parts;
	}

	inline UI::ElementHandle BuildStrip(UI::Context& context, UI::ElementHandle parent) {
		UI::ElementHandle strip = context.CreateElement();
		context.AppendChild(parent, strip);
		context.AddClass(strip, EditorTheme::Class::PanelHeader);
		UI::ElementHandle drop = context.CreateElement();
		context.AppendChild(strip, drop);
		context.AddClass(drop, EditorTheme::Class::PanelHeaderDrop);
		context.SetHoverable(drop, false);
		return strip;
	}

	inline void ClearStrip(UI::Context& context, UI::ElementHandle strip) {
		const UI::Element* pod = context.GetElement(strip);
		for (u32 child = pod ? pod->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
			const UI::ElementHandle handle = context.MakeHandle(child);
			child = context.GetElement(handle)->nextSibling;
			if (!context.HasClass(handle, EditorTheme::Class::PanelHeaderDrop)) {
				context.DestroyElement(handle);
			}
		}
	}

	struct Desc {
		std::function<void(UI::Context&, UI::ElementHandle strip)> onBuildLeading;

		EditorIcons::Icon addIcon = EditorIcons::Glyph::Add;
		std::string_view addLabel = "Add";
		std::string_view addTooltip;
		std::function<void()> onAdd;

		bool searchOpen = false;
		std::string_view searchText;
		std::function<void(bool open)> onSearchToggled;
		std::function<void(std::string_view)> onSearchChanged;
		bool focusSearch = false;
	};

	struct Parts {
		UI::ElementHandle root{};
		UI::ElementHandle searchField{};
	};

	inline Parts Build(UI::Context& context, UI::ElementHandle parent, const Desc& desc) {
		Parts parts;
		parts.root = BuildStrip(context, parent);

		if (desc.onBuildLeading) {
			desc.onBuildLeading(context, parts.root);
		}

		if (desc.onAdd) {
			ToolButtonDesc add;
			add.icon = desc.addIcon;
			add.fallback = desc.addLabel;
			add.tooltip = desc.addTooltip.empty() ? desc.addLabel : desc.addTooltip;
			add.onClick = desc.onAdd;
			ToolButton(context, parts.root, add);
		}

		if (!desc.onSearchChanged) {
			return parts;
		}

		if (desc.searchOpen) {
			UI::Widgets::TextFieldDesc field;
			field.text = desc.searchText;
			field.font = UISystem::GetDefaultFont();
			field.fontSizePx = Detail::ChromeTextPx();
			parts.searchField = UI::Widgets::TextField(context, parts.root, field);
			UI::Style fieldStyle = context.GetStyle(parts.searchField);
			fieldStyle.flexGrow = 1.0f;
			fieldStyle.margin = {EditorTheme::GetMetric(EditorTheme::MetricToken::IconGap), 0.0f, 0.0f, 0.0f};
			context.SetStyle(parts.searchField, fieldStyle);

			std::function<void(std::string_view)> changed = desc.onSearchChanged;
			context.Events(parts.searchField).onTextChange = changed;
			std::function<void(bool)> toggled = desc.onSearchToggled;
			context.Events(parts.searchField).onKeyDown = [changed, toggled](Key key, KeyMod) {
				if (key != Key::Escape) {
					return false;
				}
				changed({});
				if (toggled) {
					toggled(false);
				}
				return true;
			};
			if (desc.focusSearch) {
				context.SetFocus(parts.searchField);
			}
		} else {
			UI::Widgets::Spacer(context, parts.root);
		}

		const bool open = desc.searchOpen;
		std::function<void(bool)> toggled = desc.onSearchToggled;
		std::function<void(std::string_view)> changed = desc.onSearchChanged;
		ToolButtonDesc search;
		search.icon = EditorIcons::Glyph::Search;
		search.fallback = "Search";
		search.tooltip = open ? "Close search" : "Search";
		search.onClick = [open, toggled, changed]() {
			if (open) {
				changed({});
			}
			if (toggled) {
				toggled(!open);
			}
		};
		ToolButton(context, parts.root, search);
		return parts;
	}

	inline UI::ElementHandle OverflowButton(UI::Context& context, UI::ElementHandle parent,
											std::function<void(Vector<UI::Widgets::MenuItem>&)> fill) {
		ToolButtonDesc desc;
		desc.icon = EditorIcons::Glyph::More;
		desc.fallback = "More";
		desc.tooltip = "More options";
		UI::ElementHandle button = ToolButton(context, parent, desc).button;
		UI::Context* contextPtr = &context;
		context.Events(button).onClick = [contextPtr, button, fill]() {
			Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
			if (fill) {
				fill(items);
			}
			if (items.empty()) {
				return;
			}
			Vec2 at = contextPtr->GetPointerPosition();
			if (contextPtr->IsAlive(button)) {
				const UI::Rect& box = contextPtr->GetLayout(button).marginBox;
				at = {box.x, box.y + box.h};
			}
			UI::Widgets::OpenMenu(*contextPtr, at, items, UISystem::GetDefaultFont(), Detail::ChromeTextPx());
		};
		return button;
	}

} // namespace Shalltear::PanelHeader
