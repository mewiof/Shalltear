#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/UI/FontAsset.h"
#include "Shalltear/Core/UI/VectorAsset.h"
#include "UI/UIInternal.h"

#include <algorithm>
#include <bit>
#include <cmath>
#include <cstdlib>
#include <cstring>
#include <format>
#include <memory>

namespace Shalltear::UI::Widgets {

	namespace {

		constexpr const char* WIDGET_SKIN = R"css(
.slt-button { padding: 4 12 4 12; flex-shrink: 0; background-color: var(--slt-control); border-radius: var(--slt-radius); cursor: hand; justify-content: center; align-items: center; box-shadow: inset 0 0 0 1 var(--slt-edge-light), 0 2 0 var(--slt-edge-dark); }
.slt-button:hover { background-color: var(--slt-control-hover); }
.slt-button:active { background-color: var(--slt-control-active); }
.slt-button.slt-button-default { background-color: var(--slt-button-default); }
.slt-button.slt-button-default:hover { background-color: var(--slt-button-default-hover); }
.slt-button.slt-button-default:active { background-color: var(--slt-button-default-active); }
.slt-checkbox { width: 16; height: 16; flex-shrink: 0; background-color: var(--slt-well); border-radius: var(--slt-radius-sm); cursor: hand; justify-content: center; align-items: center; box-shadow: inset 0 0 0 1 var(--slt-edge-light-strong); }
.slt-checkbox:hover { background-color: var(--slt-well-hover); }
.slt-checkbox:active { background-color: var(--slt-well-active); }
.slt-check-mark { width: 8; height: 8; background-color: var(--slt-accent); border-radius: var(--slt-radius-xs); }
.slt-toggle { width: 24; height: 8; flex-shrink: 0; flex-direction: row; align-items: center; justify-content: flex-start; background-color: var(--slt-well); border-radius: var(--slt-radius-pill); cursor: hand; box-shadow: inset 0 0 0 1 var(--slt-edge-dark); }
.slt-toggle.slt-toggle-on { background-color: var(--slt-accent); justify-content: flex-end; }
.slt-toggle-knob { width: 12; height: 12; flex-shrink: 0; background-color: var(--slt-slider-knob); border-radius: var(--slt-radius-pill); cursor: hand; box-shadow: 0 1 0 var(--slt-edge-dark); }
.slt-toggle-knob:hover { background-color: var(--slt-slider-knob-hover); }
.slt-toggle-knob:active { background-color: var(--slt-slider-knob-active); }
.slt-slider { flex-direction: row; align-items: center; flex-grow: 1; min-width: 40; }
.slt-slider-rail { flex-direction: row; align-items: center; flex-grow: 1; flex-shrink: 1; min-width: 24; height: 16; padding: 0 6 0 6; cursor: hand; }
.slt-slider-track { width: 100%; height: 3; flex-shrink: 0; background-color: var(--slt-well); border-radius: var(--slt-radius-pill); box-shadow: inset 0 0 0 1 var(--slt-edge-dark); }
.slt-slider-fill { height: 100%; background-color: var(--slt-accent); border-radius: var(--slt-radius-pill); }
.slt-slider-knob { position: absolute; top: 50%; width: 12; height: 12; margin: -6 0 0 -6; background-color: var(--slt-slider-knob); border-radius: var(--slt-radius-pill); box-shadow: 0 1 0 var(--slt-edge-dark); }
.slt-slider-knob:hover { width: 14; height: 14; margin: -7 0 0 -7; background-color: var(--slt-slider-knob-hover); }
.slt-slider-knob:active { width: 14; height: 14; margin: -7 0 0 -7; background-color: var(--slt-slider-knob-active); }
.slt-textfield { padding: 3 6 3 6; background-color: var(--slt-field); min-width: 40; flex-grow: 1; overflow: hidden; text-overflow: clip; cursor: text; box-shadow: inset 0 1 0 var(--slt-field-top); }
.slt-textfield:focus { box-shadow: inset 0 0 0 1 var(--slt-accent-focus); }
.slt-numberfield { flex-direction: row; align-items: center; flex-grow: 1; min-width: 28; }
.slt-numberfield-labeled { min-width: var(--slt-number-pair-min); }
.slt-numberfield-next { margin: 0 0 0 var(--slt-number-gap); }
.slt-number-input { width: 56; min-width: 28; flex-grow: 1; flex-shrink: 1; }
.slt-number-label { padding: 3 5 3 5; flex-shrink: 0; }
.slt-number-label:hover { background-color: var(--slt-row-hover); border-radius: var(--slt-radius-sm); }
.slt-number-label:active { background-color: var(--slt-row-active); border-radius: var(--slt-radius-sm); }
.slt-slider-value { flex-grow: 0; flex-shrink: 0; width: 48; margin: 0 0 0 6; }
.slt-color-swatch { width: 28; height: 18; flex-shrink: 0; margin: 0 6 0 0; cursor: hand; border-radius: var(--slt-radius-sm); box-shadow: inset 0 0 0 1 var(--slt-edge-bright); }
.slt-color-swatch:active { box-shadow: inset 0 0 0 1 var(--slt-edge-active); }
.slt-color-field { flex-direction: row; align-items: center; }
.slt-color-picker { flex-direction: column; align-items: stretch; padding: 8 8 8 8; background-color: var(--slt-surface); border-radius: var(--slt-radius-lg); box-shadow: inset 0 0 0 1 var(--slt-edge-light-strong), 0 6 16 var(--slt-shadow); }
.slt-color-sv { width: 200; height: 140; flex-shrink: 0; cursor: crosshair; border-radius: var(--slt-radius-sm); }
.slt-color-sv-shade { position: absolute; left: 0; top: 0; width: 100%; height: 100%; border-radius: var(--slt-radius-sm); }
.slt-color-reticle { position: absolute; width: 10; height: 10; margin: -5 0 0 -5; border-radius: var(--slt-radius-pill); box-shadow: inset 0 0 0 1 var(--slt-slider-knob), 0 0 0 1 var(--slt-edge-dark); }
.slt-color-strip { flex-direction: row; align-items: stretch; width: 200; height: 14; flex-shrink: 0; margin: 8 0 0 0; cursor: hand; border-radius: var(--slt-radius-sm); background-color: var(--slt-well); }
.slt-color-strip-band { flex-grow: 1; flex-shrink: 1; height: 100%; }
.slt-color-strip-fill { position: absolute; left: 0; top: 0; width: 100%; height: 100%; }
.slt-color-strip-marker { position: absolute; top: 50%; width: 6; height: 18; margin: -9 0 0 -3; background-color: var(--slt-slider-knob); border-radius: var(--slt-radius-xs); box-shadow: 0 0 0 1 var(--slt-edge-dark); }
.slt-color-hex-row { flex-direction: row; align-items: center; margin: 8 0 0 0; }
.slt-color-hex { flex-grow: 1; }
.slt-dropdown { flex-direction: row; align-items: center; padding: 3 6 3 8; background-color: var(--slt-control); border-radius: var(--slt-radius); cursor: hand; min-width: 60; box-shadow: inset 0 0 0 1 var(--slt-edge-light), 0 2 0 var(--slt-edge-dark); }
.slt-dropdown:hover { background-color: var(--slt-control-hover); }
.slt-dropdown:active { background-color: var(--slt-control-active); }
.slt-dropdown-mark { flex-shrink: 0; margin: 0 0 0 6; }
.slt-scroll-root { flex-direction: row; align-items: stretch; }
.slt-scroll-viewport { flex-grow: 1; flex-direction: column; overflow: scroll; align-items: stretch; }
.slt-scroll-track { width: 10; flex-shrink: 0; background-color: var(--slt-well); cursor: hand; }
.slt-splitter { flex-shrink: 0; z-index: 10; background-color: var(--slt-splitter); }
.slt-tree-row { flex-direction: row; align-items: center; padding: 2 4 2 4; cursor: hand; width: 100%; border-radius: var(--slt-radius-row); }
.slt-tree-row:hover { background-color: var(--slt-row-hover); }
.slt-tree-row:active { background-color: var(--slt-row-active); }
.slt-tree-row:drop { background-color: var(--slt-drop); }
.slt-selected { background-color: var(--slt-selected); }
.slt-tree-row.slt-selected:active { background-color: var(--slt-selected-active); }
.slt-tree-arrow { width: var(--slt-tree-gutter); flex-shrink: 0; text-align: center; text-align-vertical: center; cursor: hand; }
.slt-tree-arrow:active { background-color: var(--slt-row-active); border-radius: var(--slt-radius-sm); }
.slt-tree-indent { flex-shrink: 0; align-self: stretch; }
.slt-menu-bar { flex-direction: row; width: 100%; background-color: var(--slt-well); align-items: center; }
.slt-menu-bar-item { padding: 4 10 4 10; cursor: hand; border-radius: var(--slt-radius); }
.slt-menu-bar-item:hover { background-color: var(--slt-menu-bar-hover); }
.slt-menu-bar-item:active { background-color: var(--slt-menu-bar-active); }
.slt-menu-popup { flex-direction: column; background-color: var(--slt-surface); border-radius: var(--slt-radius-lg); padding: 4 0 4 0; min-width: 160; box-shadow: inset 0 0 0 1 var(--slt-edge-light-strong), 0 6 16 var(--slt-shadow); }
.slt-menu-item { flex-direction: row; align-items: center; padding: 4 12 4 12; cursor: hand; width: 100%; }
.slt-menu-check { width: var(--slt-menu-check-gutter); flex-shrink: 0; justify-content: center; align-items: center; }
.slt-menu-item:hover { background-color: var(--slt-accent-hover); }
.slt-menu-item:active { background-color: var(--slt-accent-active); }
.slt-menu-separator { height: 1; width: 100%; flex-shrink: 0; background-color: var(--slt-edge-dark); margin: 3 0 3 0; box-shadow: 0 1 0 var(--slt-edge-light); }
.slt-separator { height: 1; width: 100%; flex-shrink: 0; background-color: var(--slt-edge-dark); margin: 8 0 8 0; box-shadow: 0 1 0 var(--slt-edge-light); }
.slt-separator-vertical { width: 1; align-self: stretch; flex-shrink: 0; background-color: var(--slt-edge-dark); margin: 0 4 0 4; box-shadow: 1 0 0 var(--slt-edge-light); }
.slt-fill { flex-grow: 1; }
)css";

		Color TextNormal(const Context& context) {
			return context.GetStyleVarColor("--slt-text"_hash, Color(0.88f, 0.88f, 0.9f));
		}

		Color TextDim(const Context& context) {
			return context.GetStyleVarColor("--slt-text-dim"_hash, Color(0.55f, 0.55f, 0.6f));
		}

		Color DefaultButtonText(const Context& context) {
			return context.GetStyleVarColor("--slt-button-default-text"_hash, Color(1.0f, 1.0f, 1.0f));
		}

		Color MenuText(const Context& context) {
			return context.GetStyleVarColor("--slt-menu-text"_hash, Color(0.62f, 0.62f, 0.66f));
		}

		Color ScrollThumbColor(const Context& context, ElementHandle track, const Rect& thumb) {
			const Element* element = context.GetElement(track);
			if (element && HasFlag(element->flags, ElementFlags::Pressed)) {
				return context.GetStyleVarColor("--slt-scroll-thumb-active"_hash, Color(0.64f, 0.64f, 0.7f));
			}
			Vec2 pointer = context.GetPointerPosition();
			if (element && HasFlag(element->flags, ElementFlags::Hovered) && pointer.y >= thumb.y &&
				pointer.y <= thumb.y + thumb.h) {
				return context.GetStyleVarColor("--slt-scroll-thumb-hover"_hash, Color(0.54f, 0.54f, 0.6f));
			}
			return context.GetStyleVarColor("--slt-scroll-thumb"_hash, Color(0.42f, 0.42f, 0.48f));
		}

		f32 IndentStep(const Context& context) {
			return context.GetStyleVarFloat("--slt-tree-indent"_hash, 14.0f);
		}

		Color TreeGuideColor(const Context& context) {
			return context.GetStyleVarColor("--slt-tree-guide"_hash, Color(1.0f, 1.0f, 1.0f, 0.078f));
		}

		ElementHandle MakeLabel(Context& ctx, ElementHandle parent, Handle<FontAsset> font,
								f32 sizePx, Color color, std::string_view text) {
			ElementHandle label = ctx.CreateElement();
			ctx.SetText(label, font, sizePx, color, text);
			ctx.SetHoverable(label, false);
			ctx.AppendChild(parent, label);
			return label;
		}

		str FormatNumber(f64 value, i32 decimals) {
			constexpr f64 FIXED_NOTATION_CEILING = 9007199254740992.0;
			if (std::isfinite(value) && std::abs(value) >= FIXED_NOTATION_CEILING) {
				return str{std::format("{}", value)};
			}

			str text = std::format("{:.{}f}", value, std::max(0, decimals));
			if (text.find('.') == str::npos) {
				return text;
			}
			text.erase(text.find_last_not_of('0') + 1);
			if (!text.empty() && text.back() == '.') {
				text.pop_back();
			}
			return text == "-0" ? "0" : text;
		}

		f64 ReadNumber(const Context& ctx, ElementHandle field) {
			const Element* element = ctx.GetElement(field);
			return element ? std::bit_cast<f64>(element->userTag) : 0.0;
		}

		bool NumberFilter(char32_t c) {
			return (c >= '0' && c <= '9') || c == '-' || c == '+' || c == '.' || c == 'e' || c == 'E';
		}

		constexpr f64 CHANNEL_MAX = 255.0;

		f64 ToChannelByte(f32 channel) {
			return std::round(std::clamp(static_cast<f64>(channel), 0.0, 1.0) * CHANNEL_MAX);
		}

		f32 FromChannelByte(f64 level) {
			return static_cast<f32>(std::clamp(level, 0.0, CHANNEL_MAX) / CHANNEL_MAX);
		}

		Color ReadChannels(const Context& ctx, const VectorFieldParts& components) {
			return Color(FromChannelByte(GetNumberFieldValue(ctx, components.components[0])),
						 FromChannelByte(GetNumberFieldValue(ctx, components.components[1])),
						 FromChannelByte(GetNumberFieldValue(ctx, components.components[2])),
						 FromChannelByte(GetNumberFieldValue(ctx, components.components[3])));
		}

		struct MenuState {
			ElementHandle openRow;
			ElementHandle openChild;
		};

		ElementHandle BuildMenuContent(Context& ctx, std::span<const MenuItem> items,
									   Handle<FontAsset> font, f32 fontSizePx, ElementHandle chainRoot) {
			ElementHandle content = ctx.CreateElement();
			ctx.AddClass(content, StringHash("slt-menu-popup"));
			ElementHandle root = chainRoot.IsValid() ? chainRoot : content;

			auto state = std::make_shared<MenuState>();
			Context* menuContext = &ctx;
			auto closeOpenChild = [menuContext, state]() {
				if (state->openChild.IsValid() && menuContext->IsPopupOpen(state->openChild)) {
					menuContext->ClosePopup(state->openChild);
				}
				state->openRow = {};
				state->openChild = {};
			};
			ctx.Events(content).onHoverEnter = closeOpenChild;

			for (const MenuItem& item : items) {
				if (item.separatorBefore) {
					ElementHandle separator = ctx.CreateElement();
					ctx.AddClass(separator, StringHash("slt-menu-separator"));
					ctx.SetHoverable(separator, false);
					ctx.AppendChild(content, separator);
				}

				ElementHandle row = ctx.CreateElement();
				ctx.AddClass(row, StringHash("slt-menu-item"));
				ctx.AppendChild(content, row);

				ElementHandle gutter = ctx.CreateElement();
				ctx.AddClass(gutter, StringHash("slt-menu-check"));
				ctx.AppendChild(row, gutter);
				ctx.SetHoverable(gutter, false);
				if (item.checked) {
					ElementHandle tick = ctx.CreateElement();
					ctx.AddClass(tick, StringHash("slt-check-mark"));
					ctx.AppendChild(gutter, tick);
					ctx.SetHoverable(tick, false);
				}

				Color labelColor = item.enabled ? TextNormal(ctx) : TextDim(ctx);
				MakeLabel(ctx, row, font, fontSizePx, labelColor, item.label);
				Spacer(ctx, row);
				bool hasChildren = !item.children.empty();
				if (hasChildren) {
					MakeLabel(ctx, row, font, fontSizePx, TextDim(ctx), "\xE2\x80\xBA");
				} else if (!item.shortcut.empty()) {
					MakeLabel(ctx, row, font, fontSizePx, TextDim(ctx), item.shortcut);
				}

				if (!item.enabled) {
					ctx.SetHoverable(row, false);
					continue;
				}

				if (hasChildren) {
					Context* context = &ctx;
					Vector<MenuItem> children = item.children;
					auto openChild = [context, state, closeOpenChild, row, children, font, fontSizePx, root]() {
						if (state->openRow == row && state->openChild.IsValid() &&
							context->IsPopupOpen(state->openChild)) {
							return;
						}
						closeOpenChild();
						ElementHandle child = BuildMenuContent(
							*context, {children.data(), children.size()}, font, fontSizePx, root);
						auto baseClosed = context->Events(child).onPopupClosed;
						context->Events(child).onPopupClosed = [state, child, baseClosed]() {
							if (state->openChild == child) {
								state->openRow = {};
								state->openChild = {};
							}
							if (baseClosed) {
								baseClosed();
							}
						};
						const Rect& box = context->GetLayout(row).borderBox;
						context->OpenPopup(child, {box.x + box.w, box.y}, PopupFlags::None, box);
						state->openRow = row;
						state->openChild = child;
					};
					ctx.Events(row).onClick = openChild;
					ctx.Events(row).onHoverEnter = openChild;
					continue;
				}

				Context* context = &ctx;
				std::function<void()> select = item.onSelect;
				ctx.Events(row).onHoverEnter = closeOpenChild;
				ctx.Events(row).onClick = [context, root, select]() {
					context->ClosePopup(root);
					if (select) {
						select();
					}
				};
			}

			Context* context = &ctx;
			ctx.Events(content).onPopupClosed = [context, content]() {
				context->DestroyElementNextUpdate(content);
			};
			return content;
		}

	} // anonymous namespace

	void EnsureSkin(Context& context) {
		context.LoadStyleSheetOnce(StringHash("slt-widgets"), WIDGET_SKIN);
	}

	std::string_view GetDefaultSkinSource() {
		return WIDGET_SKIN;
	}

	ElementHandle Button(Context& ctx, ElementHandle parent, const ButtonDesc& desc) {
		EnsureSkin(ctx);
		ElementHandle button = ctx.CreateElement();
		ctx.AddClass(button, StringHash("slt-button"));
		ctx.AppendChild(parent, button);
		const bool isDefault = desc.defaultForPopup.IsValid();
		if (isDefault) {
			ctx.AddClass(button, StringHash("slt-button-default"));
			ctx.SetPopupDefaultButton(desc.defaultForPopup, button);
		}
		MakeLabel(ctx, button, desc.font, desc.fontSizePx,
				  isDefault ? DefaultButtonText(ctx) : TextNormal(ctx), desc.label);
		if (desc.onClick) {
			ctx.Events(button).onClick = desc.onClick;
		}
		return button;
	}

	CheckboxParts Checkbox(Context& ctx, ElementHandle parent, bool value, std::function<void(bool)> onChange) {
		EnsureSkin(ctx);
		CheckboxParts parts;
		parts.box = ctx.CreateElement();
		ctx.AddClass(parts.box, StringHash("slt-checkbox"));
		ctx.AppendChild(parent, parts.box);

		parts.mark = ctx.CreateElement();
		ctx.AddClass(parts.mark, StringHash("slt-check-mark"));
		ctx.SetHoverable(parts.mark, false);
		ctx.SetVisible(parts.mark, value);
		ctx.AppendChild(parts.box, parts.mark);

		Context* context = &ctx;
		ElementHandle mark = parts.mark;
		ctx.Events(parts.box).onClick = [context, mark, onChange]() {
			const Element* element = context->GetElement(mark);
			if (!element) {
				return;
			}
			bool next = !HasFlag(element->flags, ElementFlags::Visible);
			context->SetVisible(mark, next);
			if (onChange) {
				onChange(next);
			}
		};
		return parts;
	}

	void SetCheckboxValue(Context& ctx, const CheckboxParts& parts, bool value) {
		ctx.SetVisible(parts.mark, value);
	}

	ToggleParts Toggle(Context& ctx, ElementHandle parent, bool value, std::function<void(bool)> onChange) {
		EnsureSkin(ctx);
		ToggleParts parts;
		parts.track = ctx.CreateElement();
		ctx.AddClass(parts.track, StringHash("slt-toggle"));
		ctx.AppendChild(parent, parts.track);

		parts.knob = ctx.CreateElement();
		ctx.AddClass(parts.knob, StringHash("slt-toggle-knob"));
		ctx.SetInheritsPointerState(parts.knob, true);
		ctx.AppendChild(parts.track, parts.knob);
		SetToggleValue(ctx, parts, value);

		Context* context = &ctx;
		ToggleParts held = parts;
		std::function<void()> flip = [context, held, onChange]() {
			bool next = !context->HasClass(held.track, StringHash("slt-toggle-on"));
			SetToggleValue(*context, held, next);
			if (onChange) {
				onChange(next);
			}
		};
		ctx.Events(parts.track).onClick = flip;
		ctx.Events(parts.knob).onClick = flip;
		return parts;
	}

	void SetToggleValue(Context& ctx, const ToggleParts& parts, bool value) {
		if (value) {
			ctx.AddClass(parts.track, StringHash("slt-toggle-on"));
		} else {
			ctx.RemoveClass(parts.track, StringHash("slt-toggle-on"));
		}
	}

	bool GetToggleValue(const Context& ctx, const ToggleParts& parts) {
		return ctx.HasClass(parts.track, StringHash("slt-toggle-on"));
	}

	namespace {

		f32 SliderFraction(f32 minValue, f32 maxValue, f32 value) {
			f32 range = maxValue - minValue;
			return range != 0.0f ? std::clamp((value - minValue) / range, 0.0f, 1.0f) : 0.0f;
		}

		void PlaceSliderAt(Context& ctx, const SliderParts& parts, f32 t) {
			Style fillStyle = ctx.GetStyle(parts.fill);
			fillStyle.width = Length::Percent(t);
			ctx.SetStyle(parts.fill, fillStyle);
			Style knobStyle = ctx.GetStyle(parts.knob);
			knobStyle.left = Length::Percent(t);
			ctx.SetStyle(parts.knob, knobStyle);
		}

	} // anonymous namespace

	SliderParts Slider(Context& ctx, ElementHandle parent, const SliderDesc& desc) {
		EnsureSkin(ctx);
		SliderParts parts;
		parts.decimals = desc.decimals;
		parts.root = ctx.CreateElement();
		ctx.AddClass(parts.root, StringHash("slt-slider"));
		ctx.AppendChild(parent, parts.root);

		ElementHandle rail = ctx.CreateElement();
		ctx.AddClass(rail, StringHash("slt-slider-rail"));
		ctx.AppendChild(parts.root, rail);

		parts.track = ctx.CreateElement();
		ctx.AddClass(parts.track, StringHash("slt-slider-track"));
		ctx.SetHoverable(parts.track, false);
		ctx.SetInheritsPointerState(parts.track, true);
		ctx.AppendChild(rail, parts.track);

		parts.fill = ctx.CreateElement();
		ctx.AddClass(parts.fill, StringHash("slt-slider-fill"));
		ctx.SetHoverable(parts.fill, false);
		ctx.AppendChild(parts.track, parts.fill);

		parts.knob = ctx.CreateElement();
		ctx.AddClass(parts.knob, StringHash("slt-slider-knob"));
		ctx.SetHoverable(parts.knob, false);
		ctx.SetInheritsPointerState(parts.knob, true);
		ctx.AppendChild(parts.track, parts.knob);

		Context* context = &ctx;
		SliderParts placement = parts;
		f32 minValue = desc.minValue;
		f32 maxValue = desc.maxValue;

		if (desc.showValueField) {
			NumberFieldDesc field;
			field.value = std::clamp(desc.value, desc.minValue, desc.maxValue);
			field.minValue = desc.minValue;
			field.maxValue = desc.maxValue;
			field.decimals = desc.decimals;
			field.font = desc.font;
			field.fontSizePx = desc.fontSizePx;
			field.onChange = [context, placement, minValue, maxValue, onChange = desc.onChange](f64 typed) {
				PlaceSliderAt(*context, placement, SliderFraction(minValue, maxValue, static_cast<f32>(typed)));
				if (onChange) {
					onChange(static_cast<f32>(typed));
				}
			};
			parts.value = NumberField(ctx, parts.root, field);
			ctx.AddClass(parts.value.root, StringHash("slt-slider-value"));
			placement.value = parts.value;
		}

		SetSliderValue(ctx, parts, desc.minValue, desc.maxValue, desc.value);

		ElementHandle track = parts.track;
		auto apply = [context, track, placement, minValue, maxValue, onChange = desc.onChange](Vec2 mouse) {
			const Rect& content = context->GetLayout(track).content;
			if (content.w <= 0.0f) {
				return;
			}
			f32 t = std::clamp((mouse.x - content.x) / content.w, 0.0f, 1.0f);
			f32 value = minValue + t * (maxValue - minValue);
			PlaceSliderAt(*context, placement, t);
			if (placement.value.field.IsValid()) {
				SetNumberFieldValue(*context, placement.value, value, placement.decimals);
			}
			if (onChange) {
				onChange(value);
			}
		};
		ctx.Events(rail).onMouseDown = [context, apply, onDragBegin = desc.onDragBegin]() {
			if (onDragBegin) {
				onDragBegin();
			}
			apply(context->GetPointerPosition());
		};
		ctx.Events(rail).onPressDrag = [apply](Vec2 mouse) {
			apply(mouse);
		};
		ctx.Events(rail).onPressEnd = desc.onDragEnd;
		return parts;
	}

	void SetSliderValue(Context& ctx, const SliderParts& parts, f32 minValue, f32 maxValue, f32 value) {
		PlaceSliderAt(ctx, parts, SliderFraction(minValue, maxValue, value));
		if (parts.value.field.IsValid()) {
			SetNumberFieldValue(ctx, parts.value, std::clamp(value, minValue, maxValue), parts.decimals);
		}
	}

	ElementHandle Separator(Context& ctx, ElementHandle parent) {
		EnsureSkin(ctx);
		ElementHandle separator = ctx.CreateElement();
		bool inRow = ctx.GetComputedStyle(parent).flexDirection == FlexDirection::Row;
		ctx.AddClass(separator, inRow ? StringHash("slt-separator-vertical") : StringHash("slt-separator"));
		ctx.SetHoverable(separator, false);
		ctx.AppendChild(parent, separator);
		return separator;
	}

	ElementHandle Spacer(Context& ctx, ElementHandle parent, f32 grow) {
		ElementHandle spacer = ctx.CreateElement();
		Style style;
		style.flexGrow = grow;
		ctx.SetStyle(spacer, style);
		ctx.SetHoverable(spacer, false);
		ctx.AppendChild(parent, spacer);
		return spacer;
	}

	ElementHandle TextField(Context& ctx, ElementHandle parent, const TextFieldDesc& desc) {
		EnsureSkin(ctx);
		ElementHandle field = ctx.CreateElement();
		ctx.AddClass(field, StringHash("slt-textfield"));
		ctx.SetText(field, desc.font, desc.fontSizePx, TextNormal(ctx), desc.text);
		ctx.SetTextInput(field, true);
		ctx.SetTextInputMultiline(field, desc.multiline);
		ctx.AppendChild(parent, field);

		auto lastCommitted = std::make_shared<str>(desc.text);
		Context* context = &ctx;
		ElementEvents& events = ctx.Events(field);
		events.onTextFilter = desc.filter;
		events.onTextCommit = [lastCommitted, onCommit = desc.onCommit](std::string_view text) {
			*lastCommitted = str(text);
			if (onCommit) {
				onCommit(text);
			}
		};
		events.onFocusLost = [context, field, lastCommitted, onCommit = desc.onCommit]() {
			str now = context->GetTextString(field);
			if (now != *lastCommitted) {
				*lastCommitted = now;
				if (onCommit) {
					onCommit(now);
				}
			}
		};
		return field;
	}

	NumberFieldParts NumberField(Context& ctx, ElementHandle parent, const NumberFieldDesc& desc) {
		EnsureSkin(ctx);
		NumberFieldParts parts;
		parts.root = ctx.CreateElement();
		ctx.AddClass(parts.root, StringHash("slt-numberfield"));
		ctx.AppendChild(parent, parts.root);

		Context* context = &ctx;
		f64 initial = std::clamp(desc.value, desc.minValue, desc.maxValue);

		parts.field = ctx.CreateElement();
		ctx.AddClass(parts.field, StringHash("slt-textfield"));
		ctx.AddClass(parts.field, StringHash("slt-number-input"));
		ctx.SetText(parts.field, desc.font, desc.fontSizePx, TextNormal(ctx), FormatNumber(initial, desc.decimals));
		ctx.SetTextInput(parts.field, true);
		ctx.SetUserTag(parts.field, std::bit_cast<u64>(initial));

		ElementHandle field = parts.field;
		auto commitValue = [context, field,
							minValue = desc.minValue, maxValue = desc.maxValue, decimals = desc.decimals,
							onChange = desc.onChange](f64 value, bool rewriteText) {
			value = std::clamp(value, minValue, maxValue);
			bool changed = value != ReadNumber(*context, field);
			context->SetUserTag(field, std::bit_cast<u64>(value));
			if (rewriteText) {
				context->SetTextString(field, FormatNumber(value, decimals));
			}
			if (changed && onChange) {
				onChange(value);
			}
		};

		auto parseAndCommit = [context, field, commitValue](std::string_view text) {
			str buffer(text);
			char* end = nullptr;
			f64 parsed = std::strtod(buffer.c_str(), &end);
			if (end == buffer.c_str()) {
				parsed = ReadNumber(*context, field);
			}
			commitValue(parsed, true);
		};

		ElementEvents& fieldEvents = ctx.Events(parts.field);
		fieldEvents.onTextFilter = NumberFilter;
		fieldEvents.onTextCommit = parseAndCommit;
		fieldEvents.onFocusLost = [context, field, parseAndCommit]() {
			parseAndCommit(context->GetTextString(field));
		};

		if (!desc.label.empty()) {
			parts.labelElement = ctx.CreateElement();
			ctx.AddClass(parts.labelElement, StringHash("slt-number-label"));
			ctx.AddClass(parts.root, StringHash("slt-numberfield-labeled"));
			Style labelStyle = ctx.GetStyle(parts.labelElement);
			labelStyle.cursor = SystemCursor::ResizeEW;
			ctx.SetStyle(parts.labelElement, labelStyle);
			ctx.SetText(parts.labelElement, desc.font, desc.fontSizePx, TextDim(ctx), desc.label);
			ctx.AppendChild(parts.root, parts.labelElement);

			struct ScrubState {
				f64 pressValue = 0.0;
				f32 pressX = 0.0f;
				bool active = false;
				u32 claim = 0;
			};
			auto scrub = std::make_shared<ScrubState>();
			ctx.Events(parts.labelElement).onMouseDown = [context, field, scrub]() {
				scrub->pressValue = ReadNumber(*context, field);
				scrub->pressX = context->GetPointerPosition().x;
				scrub->active = false;
			};
			ctx.Events(parts.labelElement).onPressDrag =
				[context, scrub, label = parts.labelElement, commitValue,
				 dragScale = desc.dragScale, onScrubBegin = desc.onScrubBegin](Vec2 mouse) {
					if (!scrub->active) {
						scrub->active = true;
						scrub->claim = context->ClaimPointer(label, CursorMode::Wrapped);
						if (onScrubBegin) {
							onScrubBegin();
						}
					}
					commitValue(scrub->pressValue + static_cast<f64>(mouse.x - scrub->pressX) * dragScale, true);
				};
			ctx.Events(parts.labelElement).onPressEnd = [context, scrub, onScrubEnd = desc.onScrubEnd]() {
				if (scrub->active) {
					scrub->active = false;
					context->ReleasePointer(scrub->claim);
					scrub->claim = 0;
					if (onScrubEnd) {
						onScrubEnd();
					}
				}
			};
		}

		ctx.AppendChild(parts.root, parts.field);
		return parts;
	}

	void SetNumberFieldValue(Context& ctx, const NumberFieldParts& parts, f64 value, i32 decimals) {
		ctx.SetUserTag(parts.field, std::bit_cast<u64>(value));
		ctx.SetTextString(parts.field, FormatNumber(value, decimals));
	}

	f64 GetNumberFieldValue(const Context& ctx, const NumberFieldParts& parts) {
		return ReadNumber(ctx, parts.field);
	}

	VectorFieldParts VectorField(Context& ctx, ElementHandle parent, const VectorFieldDesc& desc) {
		EnsureSkin(ctx);
		VectorFieldParts parts;
		parts.root = ctx.CreateElement();
		Style rowStyle;
		rowStyle.flexDirection = FlexDirection::Row;
		rowStyle.alignItems = Align::Center;
		rowStyle.flexGrow = 1.0f;
		ctx.SetStyle(parts.root, rowStyle);
		ctx.AppendChild(parent, parts.root);

		u32 count = std::clamp(desc.componentCount, 1u, 4u);
		const f32 pairMin = ctx.GetStyleVarFloat(StringHash("--slt-number-pair-min"), 52.0f);
		const f32 componentGap = ctx.GetStyleVarFloat(StringHash("--slt-number-gap"), 6.0f);
		rowStyle.minWidth = Length::Pixel(static_cast<f32>(count) * pairMin +
										  static_cast<f32>(count - 1) * componentGap);
		ctx.SetStyle(parts.root, rowStyle);
		for (u32 i = 0; i < count; ++i) {
			NumberFieldDesc component;
			component.value = desc.values[i];
			component.minValue = desc.minValue;
			component.maxValue = desc.maxValue;
			component.dragScale = desc.dragScale;
			component.decimals = desc.decimals;
			component.label = desc.labels[i] ? std::string_view(desc.labels[i]) : std::string_view{};
			component.font = desc.font;
			component.fontSizePx = desc.fontSizePx;
			component.onScrubBegin = desc.onEditBegin;
			component.onScrubEnd = desc.onEditEnd;
			if (desc.onChange) {
				component.onChange = [i, onChange = desc.onChange](f64 value) {
					onChange(i, value);
				};
			}
			parts.components[i] = NumberField(ctx, parts.root, component);
			if (i > 0) {
				ctx.AddClass(parts.components[i].root, StringHash("slt-numberfield-next"));
			}
		}
		return parts;
	}

	void SetVectorFieldValues(Context& ctx, const VectorFieldParts& parts, u32 componentCount, const f64* values, i32 decimals) {
		u32 count = std::clamp(componentCount, 1u, 4u);
		for (u32 i = 0; i < count; ++i) {
			SetNumberFieldValue(ctx, parts.components[i], values[i], decimals);
		}
	}

	namespace {

		struct HSV {
			f32 h = 0.0f;
			f32 s = 0.0f;
			f32 v = 0.0f;
		};

		Color HSVToRGB(const HSV& hsv, f32 alpha) {
			f32 h = hsv.h - std::floor(hsv.h);
			f32 s = std::clamp(hsv.s, 0.0f, 1.0f);
			f32 v = std::clamp(hsv.v, 0.0f, 1.0f);
			f32 sector = h * 6.0f;
			i32 index = static_cast<i32>(std::floor(sector)) % 6;
			f32 fraction = sector - std::floor(sector);
			f32 p = v * (1.0f - s);
			f32 q = v * (1.0f - s * fraction);
			f32 t = v * (1.0f - s * (1.0f - fraction));
			switch (index) {
			case 0:
				return Color(v, t, p, alpha);
			case 1:
				return Color(q, v, p, alpha);
			case 2:
				return Color(p, v, t, alpha);
			case 3:
				return Color(p, q, v, alpha);
			case 4:
				return Color(t, p, v, alpha);
			default:
				return Color(v, p, q, alpha);
			}
		}

		HSV RGBToHSV(const Color& color) {
			f32 r = std::clamp(color.r, 0.0f, 1.0f);
			f32 g = std::clamp(color.g, 0.0f, 1.0f);
			f32 b = std::clamp(color.b, 0.0f, 1.0f);
			f32 max = std::max({r, g, b});
			f32 min = std::min({r, g, b});
			f32 span = max - min;
			HSV out;
			out.v = max;
			out.s = max > 0.0f ? span / max : 0.0f;
			if (span <= 0.0f) {
				return out;
			}
			if (max == r) {
				out.h = (g - b) / span / 6.0f;
			} else if (max == g) {
				out.h = (2.0f + (b - r) / span) / 6.0f;
			} else {
				out.h = (4.0f + (r - g) / span) / 6.0f;
			}
			out.h -= std::floor(out.h);
			return out;
		}

		f32 FractionAcross(const Rect& box, f32 x) {
			return box.w <= 0.0f ? 0.0f : std::clamp((x - box.x) / box.w, 0.0f, 1.0f);
		}
		f32 FractionDown(const Rect& box, f32 y) {
			return box.h <= 0.0f ? 0.0f : std::clamp((y - box.y) / box.h, 0.0f, 1.0f);
		}

		struct PickerState {
			HSV hsv;
			f32 alpha = 1.0f;
			ElementHandle square;
			ElementHandle shade;
			ElementHandle reticle;
			ElementHandle hueStrip;
			ElementHandle hueMarker;
			ElementHandle alphaStrip;
			ElementHandle alphaFill;
			ElementHandle alphaMarker;
			ElementHandle hexField;
			bool editing = false;
			bool changed = false;
		};
		using PickerRef = std::shared_ptr<PickerState>;

		void SetBackground(Context& ctx, ElementHandle element, const Brush& brush) {
			Style style = ctx.GetStyle(element);
			style.background = brush;
			ctx.SetStyle(element, style);
		}

		void PlaceAt(Context& ctx, ElementHandle element, f32 across, f32 down) {
			Style style = ctx.GetStyle(element);
			style.left = Length::Percent(across);
			style.top = Length::Percent(down);
			ctx.SetStyle(element, style);
		}

		void SyncPicker(Context& ctx, const PickerRef& state) {
			Color rgb = HSVToRGB(state->hsv, state->alpha);
			Color opaqueHue = HSVToRGB(HSV{state->hsv.h, 1.0f, 1.0f}, 1.0f);
			if (ctx.IsAlive(state->square)) {
				SetBackground(ctx, state->square, Brush::Gradient(Color::White(), opaqueHue, {1.0f, 0.0f}));
			}
			if (ctx.IsAlive(state->reticle)) {
				PlaceAt(ctx, state->reticle, state->hsv.s, 1.0f - state->hsv.v);
			}
			if (ctx.IsAlive(state->hueMarker)) {
				PlaceAt(ctx, state->hueMarker, state->hsv.h, 0.5f);
			}
			if (ctx.IsAlive(state->alphaFill)) {
				Color clear = rgb;
				clear.a = 0.0f;
				Color solid = rgb;
				solid.a = 1.0f;
				SetBackground(ctx, state->alphaFill, Brush::Gradient(clear, solid, {1.0f, 0.0f}));
			}
			if (ctx.IsAlive(state->alphaMarker)) {
				PlaceAt(ctx, state->alphaMarker, state->alpha, 0.5f);
			}
			if (ctx.IsAlive(state->hexField)) {
				ctx.SetTextString(state->hexField, rgb.ToHex());
			}
		}

		ElementHandle AddOverlay(Context& ctx, ElementHandle parent, StringHash klass, const Brush& brush) {
			ElementHandle overlay = ctx.CreateElement();
			ctx.AppendChild(parent, overlay);
			ctx.AddClass(overlay, klass);
			ctx.SetHoverable(overlay, false);
			SetBackground(ctx, overlay, brush);
			return overlay;
		}

		void InstallStripGesture(Context& ctx, ElementHandle strip, const PickerRef& state,
								 std::function<void(f32)> apply,
								 const std::function<void()>& onEditBegin,
								 const std::function<void()>& onEditEnd) {
			Context* context = &ctx;
			auto place = [context, strip, apply](f32 x) {
				apply(FractionAcross(context->GetLayout(strip).content, x));
			};
			ctx.Events(strip).onMouseDown = [context, state, place, onEditBegin]() {
				if (!state->editing) {
					state->editing = true;
					if (onEditBegin) {
						onEditBegin();
					}
				}
				place(context->GetPointerPosition().x);
			};
			ctx.Events(strip).onPressDrag = [place](Vec2 mouse) { place(mouse.x); };
			ctx.Events(strip).onPressEnd = [state, onEditEnd]() {
				if (state->editing) {
					state->editing = false;
					if (onEditEnd) {
						onEditEnd();
					}
				}
			};
		}

	} // anonymous namespace

	ElementHandle OpenColorPicker(Context& ctx, ElementHandle anchor, Color value,
								  Handle<FontAsset> font, f32 fontSizePx,
								  std::function<void(Color)> onChange,
								  std::function<void()> onEditBegin, std::function<void()> onEditEnd) {
		EnsureSkin(ctx);
		Context* context = &ctx;
		auto state = std::make_shared<PickerState>();
		state->hsv = RGBToHSV(value);
		state->alpha = std::clamp(value.a, 0.0f, 1.0f);

		ElementHandle content = ctx.CreateElement();
		ctx.AddClass(content, StringHash("slt-color-picker"));
		ctx.Events(content).onPopupClosed = [context, content]() {
			context->DestroyElementNextUpdate(content);
		};

		auto publish = [context, state, onChange]() {
			SyncPicker(*context, state);
			state->changed = true;
			if (onChange) {
				onChange(HSVToRGB(state->hsv, state->alpha));
			}
		};

		state->square = ctx.CreateElement();
		ctx.AppendChild(content, state->square);
		ctx.AddClass(state->square, StringHash("slt-color-sv"));
		state->shade = AddOverlay(ctx, state->square, StringHash("slt-color-sv-shade"),
								  Brush::Gradient(Color(0.0f, 0.0f, 0.0f, 0.0f), Color::Black(), {0.0f, 1.0f}));
		state->reticle = ctx.CreateElement();
		ctx.AppendChild(state->square, state->reticle);
		ctx.AddClass(state->reticle, StringHash("slt-color-reticle"));
		ctx.SetHoverable(state->reticle, false);

		ElementHandle square = state->square;
		auto placeSv = [context, state, square, publish](f32 x, f32 y) {
			const Rect& box = context->GetLayout(square).content;
			state->hsv.s = FractionAcross(box, x);
			state->hsv.v = 1.0f - FractionDown(box, y);
			publish();
		};
		ctx.Events(state->square).onMouseDown = [context, state, placeSv, onEditBegin]() {
			if (!state->editing) {
				state->editing = true;
				if (onEditBegin) {
					onEditBegin();
				}
			}
			Vec2 pointer = context->GetPointerPosition();
			placeSv(pointer.x, pointer.y);
		};
		ctx.Events(state->square).onPressDrag = [placeSv](Vec2 mouse) { placeSv(mouse.x, mouse.y); };
		ctx.Events(state->square).onPressEnd = [state, onEditEnd]() {
			if (state->editing) {
				state->editing = false;
				if (onEditEnd) {
					onEditEnd();
				}
			}
		};

		state->hueStrip = ctx.CreateElement();
		ctx.AppendChild(content, state->hueStrip);
		ctx.AddClass(state->hueStrip, StringHash("slt-color-strip"));
		for (u32 band = 0; band < 6; ++band) {
			ElementHandle segment = ctx.CreateElement();
			ctx.AppendChild(state->hueStrip, segment);
			ctx.AddClass(segment, StringHash("slt-color-strip-band"));
			ctx.SetHoverable(segment, false);
			SetBackground(ctx, segment,
						  Brush::Gradient(HSVToRGB(HSV{static_cast<f32>(band) / 6.0f, 1.0f, 1.0f}, 1.0f),
										  HSVToRGB(HSV{static_cast<f32>(band + 1) / 6.0f, 1.0f, 1.0f}, 1.0f),
										  {1.0f, 0.0f}));
		}
		state->hueMarker = ctx.CreateElement();
		ctx.AppendChild(state->hueStrip, state->hueMarker);
		ctx.AddClass(state->hueMarker, StringHash("slt-color-strip-marker"));
		ctx.SetHoverable(state->hueMarker, false);
		InstallStripGesture(ctx, state->hueStrip, state, [state, publish](f32 fraction) {
			state->hsv.h = fraction;
			publish(); }, onEditBegin, onEditEnd);

		state->alphaStrip = ctx.CreateElement();
		ctx.AppendChild(content, state->alphaStrip);
		ctx.AddClass(state->alphaStrip, StringHash("slt-color-strip"));
		state->alphaFill = AddOverlay(ctx, state->alphaStrip, StringHash("slt-color-strip-fill"), Brush{});
		state->alphaMarker = ctx.CreateElement();
		ctx.AppendChild(state->alphaStrip, state->alphaMarker);
		ctx.AddClass(state->alphaMarker, StringHash("slt-color-strip-marker"));
		ctx.SetHoverable(state->alphaMarker, false);
		InstallStripGesture(ctx, state->alphaStrip, state, [state, publish](f32 fraction) {
			state->alpha = fraction;
			publish(); }, onEditBegin, onEditEnd);

		ElementHandle hexRow = ctx.CreateElement();
		ctx.AppendChild(content, hexRow);
		ctx.AddClass(hexRow, StringHash("slt-color-hex-row"));
		const str hexText = value.ToHex();
		TextFieldDesc hex;
		hex.text = hexText;
		hex.font = font;
		hex.fontSizePx = fontSizePx;
		hex.onCommit = [context, state, publish, onEditBegin, onEditEnd](std::string_view text) {
			Color parsed;
			if (!Color::TryParseHex(text, parsed)) {
				SyncPicker(*context, state);
				return;
			}
			state->hsv = RGBToHSV(parsed);
			state->alpha = std::clamp(parsed.a, 0.0f, 1.0f);
			if (onEditBegin) {
				onEditBegin();
			}
			publish();
			if (onEditEnd) {
				onEditEnd();
			}
		};
		state->hexField = TextField(ctx, hexRow, hex);
		ctx.AddClass(state->hexField, StringHash("slt-color-hex"));

		const Color original = value;
		auto cancel = [context, state, content, original, onChange, onEditBegin, onEditEnd]() {
			context->SetFocus(content);
			if (state->changed) {
				state->hsv = RGBToHSV(original);
				state->alpha = std::clamp(original.a, 0.0f, 1.0f);
				SyncPicker(*context, state);
				if (onEditBegin) {
					onEditBegin();
				}
				if (onChange) {
					onChange(original);
				}
				if (onEditEnd) {
					onEditEnd();
				}
			}
			context->ClosePopup(content);
		};
		ctx.SetFocusable(content, true);
		ctx.Events(content).onKeyDown = [context, content, cancel](Key key, KeyMod) {
			if (key == Key::Escape) {
				cancel();
				return true;
			}
			if (key == Key::Enter) {
				context->ClosePopup(content);
				return true;
			}
			return false;
		};
		ctx.Events(state->hexField).onKeyDown = [cancel](Key key, KeyMod) {
			if (key == Key::Escape) {
				cancel();
				return true;
			}
			return false;
		};

		SyncPicker(ctx, state);
		const Rect& box = ctx.GetLayout(anchor).borderBox;
		ctx.OpenPopup(content, {box.x, box.y + box.h});
		ctx.SetFocus(content);
		return content;
	}

	ColorFieldParts ColorSwatchField(Context& ctx, ElementHandle parent, Color value,
									 Handle<FontAsset> font, f32 fontSizePx,
									 std::function<void(Color)> onChange,
									 std::function<void()> onEditBegin, std::function<void()> onEditEnd) {
		EnsureSkin(ctx);
		ColorFieldParts parts;
		parts.root = ctx.CreateElement();
		ctx.AddClass(parts.root, StringHash("slt-color-field"));
		ctx.AppendChild(parent, parts.root);

		parts.swatch = ctx.CreateElement();
		ctx.AddClass(parts.swatch, StringHash("slt-color-swatch"));
		Style swatchStyle = ctx.GetStyle(parts.swatch);
		swatchStyle.background = Brush::SolidColor(value);
		ctx.SetStyle(parts.swatch, swatchStyle);
		ctx.AppendChild(parts.root, parts.swatch);

		Context* context = &ctx;
		ElementHandle swatch = parts.swatch;
		auto components = std::make_shared<VectorFieldParts>();

		VectorFieldDesc vector;
		vector.componentCount = 4;
		vector.values[0] = ToChannelByte(value.r);
		vector.values[1] = ToChannelByte(value.g);
		vector.values[2] = ToChannelByte(value.b);
		vector.values[3] = ToChannelByte(value.a);
		static constexpr const char* CHANNELS[4] = {"R", "G", "B", "A"};
		for (u32 i = 0; i < 4; ++i) {
			vector.labels[i] = CHANNELS[i];
		}
		vector.minValue = 0.0;
		vector.maxValue = CHANNEL_MAX;
		vector.dragScale = 1.0;
		vector.decimals = 0;
		vector.font = font;
		vector.fontSizePx = fontSizePx;
		vector.onEditBegin = onEditBegin;
		vector.onEditEnd = onEditEnd;
		vector.onChange = [context, swatch, components, onChange](u32, f64) {
			Color next = ReadChannels(*context, *components);
			Style style = context->GetStyle(swatch);
			style.background = Brush::SolidColor(next);
			context->SetStyle(swatch, style);
			if (onChange) {
				onChange(next);
			}
		};
		parts.vector = VectorField(ctx, parts.root, vector);
		*components = parts.vector;

		const Style swatchBox = ctx.GetComputedStyle(parts.swatch);
		const Style channelStyle = ctx.GetStyle(parts.vector.root);
		if (channelStyle.minWidth.unit == Length::Unit::Pixel) {
			Style rootStyle = ctx.GetStyle(parts.root);
			const f32 swatchWidth = swatchBox.width.unit == Length::Unit::Pixel ? swatchBox.width.value : 0.0f;
			rootStyle.minWidth = Length::Pixel(swatchWidth + swatchBox.margin.right + channelStyle.minWidth.value);
			ctx.SetStyle(parts.root, rootStyle);
		}

		ColorFieldParts built = parts;
		ctx.Events(parts.swatch).onClick = [context, built, components, font, fontSizePx,
											onChange, onEditBegin, onEditEnd]() {
			auto picker = std::make_shared<ElementHandle>();
			const auto end = [context, built, picker, onEditEnd]() {
				if (onEditEnd) {
					onEditEnd();
				}
				if (!context->IsAlive(built.swatch)) {
					context->ClosePopup(*picker);
				}
			};
			*picker = OpenColorPicker(*context, built.swatch, ReadChannels(*context, *components), font, fontSizePx, [context, built, onChange](Color picked) {
								if (!context->IsAlive(built.swatch)) {
									return;
								}
								SetColorSwatchFieldValue(*context, built, picked);
								if (onChange) {
									onChange(picked);
								} }, onEditBegin, end);
		};
		return parts;
	}

	void SetColorSwatchFieldValue(Context& ctx, const ColorFieldParts& parts, Color value) {
		f64 channels[4] = {ToChannelByte(value.r), ToChannelByte(value.g),
						   ToChannelByte(value.b), ToChannelByte(value.a)};
		SetVectorFieldValues(ctx, parts.vector, 4, channels, 0);
		Style style = ctx.GetStyle(parts.swatch);
		style.background = Brush::SolidColor(value);
		ctx.SetStyle(parts.swatch, style);
	}

	namespace {

		constexpr size_t MAX_MASK_ITEMS = 64;

		using OwnedItems = std::shared_ptr<Vector<str>>;

		OwnedItems CopyItems(std::span<const std::string_view> items, size_t cap) {
			auto owned = std::make_shared<Vector<str>>(GetMemoryResource(MemTag::UI));
			size_t count = items.size();
			if (count > cap) {
				SLT_LOG_WARN("UI", "MaskDropdown: {} items exceeds the {} a mask can address (rest dropped)",
							 count, cap);
				count = cap;
			}
			owned->reserve(count);
			for (size_t i = 0; i < count; ++i) {
				owned->emplace_back(items[i].begin(), items[i].end());
			}
			return owned;
		}

		u64 AllBits(size_t count) {
			return count >= MAX_MASK_ITEMS ? ~0ULL : ((1ULL << count) - 1ULL);
		}

		template <typename Items>
		str MaskLabel(const Items& items, u64 mask) {
			if (items.empty()) {
				return str("Nothing");
			}
			u64 all = AllBits(items.size());
			if ((mask & all) == 0) {
				return str("Nothing");
			}
			if ((mask & all) == all) {
				return str("Everything");
			}
			str text;
			for (size_t i = 0; i < items.size(); ++i) {
				if ((mask & (1ULL << i)) == 0) {
					continue;
				}
				if (!text.empty()) {
					text += ", ";
				}
				text += items[i];
			}
			return text;
		}

		constexpr std::string_view DefaultDropdownMark = "\xE2\x96\xBC";
		constexpr f32 DEFAULT_DROPDOWN_MARK_SCALE = 0.62f;
		Handle<VectorAsset> _markIcon;
		f32 _markScale = DEFAULT_DROPDOWN_MARK_SCALE;

		DropdownParts MakeDropdownButton(Context& ctx, ElementHandle parent, Handle<FontAsset> font,
										 f32 fontSizePx, std::string_view initialLabel) {
			EnsureSkin(ctx);
			DropdownParts parts;
			parts.root = ctx.CreateElement();
			ctx.AddClass(parts.root, StringHash("slt-dropdown"));
			ctx.AppendChild(parent, parts.root);

			parts.label = MakeLabel(ctx, parts.root, font, fontSizePx, TextNormal(ctx), initialLabel);
			Style labelStyle = ctx.GetStyle(parts.label);
			labelStyle.overflow = Overflow::Hidden;
			labelStyle.textOverflow = TextOverflow::Ellipsis;
			labelStyle.flexShrink = 1.0f;
			ctx.SetStyle(parts.label, labelStyle);

			Spacer(ctx, parts.root);
			ElementHandle mark;
			if (_markIcon.IsValid()) {
				mark = ctx.CreateElement();
				ctx.AppendChild(parts.root, mark);
				ctx.SetHoverable(mark, false);
				ctx.SetIcon(mark, _markIcon, fontSizePx * _markScale, IconPalette::Tint(TextDim(ctx)));
			} else {
				mark = MakeLabel(ctx, parts.root, font, fontSizePx * DEFAULT_DROPDOWN_MARK_SCALE,
								 TextDim(ctx), DefaultDropdownMark);
			}
			ctx.AddClass(mark, StringHash("slt-dropdown-mark"));
			return parts;
		}

		ElementHandle OpenDropdownPopup(Context& ctx, ElementHandle button) {
			ElementHandle content = ctx.CreateElement();
			ctx.AddClass(content, StringHash("slt-menu-popup"));
			const Rect& box = ctx.GetLayout(button).borderBox;
			Style style = ctx.GetStyle(content);
			style.minWidth = Length::Pixel(box.w / std::max(0.001f, ctx.GetContentScale()));
			ctx.SetStyle(content, style);
			ctx.Events(content).onPopupClosed = [&ctx, content]() {
				ctx.DestroyElementNextUpdate(content);
			};
			return content;
		}

		struct PopupRow {
			ElementHandle row;
			ElementHandle tick;
		};

		void SetRowTick(Context& ctx, const PopupRow& row, bool on) {
			if (!ctx.IsAlive(row.tick)) {
				return;
			}
			Style style = ctx.GetStyle(row.tick);
			style.display = on ? Display::Flex : Display::None;
			ctx.SetStyle(row.tick, style);
		}

		PopupRow MakePopupRow(Context& ctx, ElementHandle content, Handle<FontAsset> font,
							  f32 fontSizePx, std::string_view text, bool marked) {
			PopupRow parts;
			parts.row = ctx.CreateElement();
			ctx.AddClass(parts.row, StringHash("slt-menu-item"));
			ctx.AppendChild(content, parts.row);

			ElementHandle gutter = ctx.CreateElement();
			ctx.AddClass(gutter, StringHash("slt-menu-check"));
			ctx.AppendChild(parts.row, gutter);
			ctx.SetHoverable(gutter, false);

			parts.tick = ctx.CreateElement();
			ctx.AddClass(parts.tick, StringHash("slt-check-mark"));
			ctx.AppendChild(gutter, parts.tick);
			ctx.SetHoverable(parts.tick, false);
			SetRowTick(ctx, parts, marked);

			MakeLabel(ctx, parts.row, font, fontSizePx, TextNormal(ctx), text);
			return parts;
		}

		struct MaskRow {
			PopupRow row;
			u64 bits = 0;
			bool exact = false;

			bool IsOn(u64 mask) const {
				return exact ? mask == bits : (mask & bits) != 0;
			}
		};

	} // namespace

	void SetDropdownMark(Handle<VectorAsset> mark, f32 sizeScale) {
		if (!mark.IsValid()) {
			_markIcon = {};
			_markScale = DEFAULT_DROPDOWN_MARK_SCALE;
			return;
		}
		_markIcon = mark;
		_markScale = sizeScale > 0.0f ? sizeScale : DEFAULT_DROPDOWN_MARK_SCALE;
	}

	DropdownParts Dropdown(Context& ctx, ElementHandle parent, const DropdownDesc& desc) {
		OwnedItems items = CopyItems(desc.items, desc.items.size());
		std::string_view current = desc.selected < items->size()
									   ? std::string_view((*items)[desc.selected])
									   : std::string_view{};
		DropdownParts parts = MakeDropdownButton(ctx, parent, desc.font, desc.fontSizePx, current);
		ctx.SetUserTag(parts.root, desc.selected);

		Context* context = &ctx;
		ctx.Events(parts.root).onClick = [context, parts, items,
										  font = desc.font, fontSizePx = desc.fontSizePx,
										  onSelect = desc.onSelect]() {
			const Element* button = context->GetElement(parts.root);
			u64 selected = button ? button->userTag : 0;

			ElementHandle content = OpenDropdownPopup(*context, parts.root);
			for (u32 i = 0; i < items->size(); ++i) {
				PopupRow row = MakePopupRow(*context, content, font, fontSizePx, (*items)[i], i == selected);
				context->Events(row.row).onClick = [context, content, parts, items, i, onSelect]() {
					context->SetUserTag(parts.root, i);
					if (context->IsAlive(parts.label)) {
						context->SetTextString(parts.label, (*items)[i]);
					}
					context->ClosePopup(content);
					if (onSelect) {
						onSelect(i);
					}
				};
			}
			const Rect& box = context->GetLayout(parts.root).borderBox;
			context->OpenPopup(content, {box.x, box.y + box.h});
		};
		return parts;
	}

	void SetDropdownSelection(Context& ctx, const DropdownParts& parts,
							  std::span<const std::string_view> items, u32 selected) {
		if (!ctx.IsAlive(parts.root)) {
			return;
		}
		ctx.SetUserTag(parts.root, selected);
		if (ctx.IsAlive(parts.label)) {
			ctx.SetTextString(parts.label,
							  selected < items.size() ? items[selected] : std::string_view{});
		}
	}

	DropdownParts MaskDropdown(Context& ctx, ElementHandle parent, const MaskDropdownDesc& desc) {
		OwnedItems items = CopyItems(desc.items, MAX_MASK_ITEMS);
		DropdownParts parts = MakeDropdownButton(ctx, parent, desc.font, desc.fontSizePx,
												 MaskLabel(*items, desc.mask));
		ctx.SetUserTag(parts.root, desc.mask);

		Context* context = &ctx;
		ctx.Events(parts.root).onClick = [context, parts, items,
										  font = desc.font, fontSizePx = desc.fontSizePx,
										  onChange = desc.onChange]() {
			auto currentMask = [context, parts]() -> u64 {
				const Element* button = context->GetElement(parts.root);
				return button ? button->userTag : 0;
			};
			auto apply = [context, parts, items, onChange](u64 next) {
				context->SetUserTag(parts.root, next);
				if (context->IsAlive(parts.label)) {
					context->SetTextString(parts.label, MaskLabel(*items, next));
				}
				if (onChange) {
					onChange(next);
				}
			};

			ElementHandle content = OpenDropdownPopup(*context, parts.root);
			const u64 all = AllBits(items->size());

			auto rows = std::make_shared<Vector<MaskRow>>(GetMemoryResource(MemTag::UI));
			auto retick = [context, rows](u64 mask) {
				for (const MaskRow& entry : *rows) {
					SetRowTick(*context, entry.row, entry.IsOn(mask));
				}
			};
			auto pick = [apply, retick](u64 next) {
				apply(next);
				retick(next);
			};

			u64 mask = currentMask();
			MaskRow nothing{MakePopupRow(*context, content, font, fontSizePx, "Nothing", false), 0, true};
			context->Events(nothing.row.row).onClick = [pick]() { pick(0); };
			rows->push_back(nothing);

			MaskRow everything{MakePopupRow(*context, content, font, fontSizePx, "Everything", false), all, true};
			context->Events(everything.row.row).onClick = [pick, all]() { pick(all); };
			rows->push_back(everything);

			ElementHandle divider = context->CreateElement();
			context->AddClass(divider, StringHash("slt-menu-separator"));
			context->AppendChild(content, divider);

			for (u32 i = 0; i < items->size(); ++i) {
				const u64 bit = 1ULL << i;
				MaskRow entry{MakePopupRow(*context, content, font, fontSizePx, (*items)[i], false), bit, false};
				context->Events(entry.row.row).onClick = [pick, currentMask, bit]() {
					pick(currentMask() ^ bit);
				};
				rows->push_back(entry);
			}
			retick(mask);

			const Rect& box = context->GetLayout(parts.root).borderBox;
			context->OpenPopup(content, {box.x, box.y + box.h});
		};
		return parts;
	}

	void SetMaskDropdownValue(Context& ctx, const DropdownParts& parts,
							  std::span<const std::string_view> items, u64 mask) {
		if (!ctx.IsAlive(parts.root)) {
			return;
		}
		ctx.SetUserTag(parts.root, mask);
		if (ctx.IsAlive(parts.label)) {
			ctx.SetTextString(parts.label, MaskLabel(items, mask));
		}
	}

	ScrollViewParts ScrollView(Context& ctx, ElementHandle parent) {
		EnsureSkin(ctx);
		ScrollViewParts parts;
		parts.root = ctx.CreateElement();
		ctx.AddClass(parts.root, StringHash("slt-scroll-root"));
		ctx.AppendChild(parent, parts.root);

		parts.viewport = ctx.CreateElement();
		ctx.AddClass(parts.viewport, StringHash("slt-scroll-viewport"));
		ctx.AppendChild(parts.root, parts.viewport);

		parts.track = ctx.CreateElement();
		ctx.AddClass(parts.track, StringHash("slt-scroll-track"));
		ctx.AddClass(parts.track, StringHash(EdgeControlClass));
		ctx.AppendChild(parts.root, parts.track);

		Context* context = &ctx;
		ElementHandle viewport = parts.viewport;
		auto thumbRect = [context, viewport](const Rect& track) -> Rect {
			const Element* view = context->GetElement(viewport);
			if (!view || view->maxScroll.y <= 0.0f) {
				return {};
			}
			f32 viewH = context->GetLayout(viewport).content.h;
			f32 totalH = viewH + view->maxScroll.y;
			if (viewH <= 0.0f || totalH <= 0.0f) {
				return {};
			}
			f32 thumbH = std::max(20.0f, track.h * (viewH / totalH));
			f32 t = view->scrollOffset.y / view->maxScroll.y;
			f32 thumbY = track.y + (track.h - thumbH) * std::clamp(t, 0.0f, 1.0f);
			return {track.x + 2.0f, thumbY, track.w - 4.0f, thumbH};
		};

		ctx.Events(parts.track).customDraw =
			[thumbRect](Context& paintCtx, ElementHandle self, PaintScope& painter) {
				Rect thumb = thumbRect(paintCtx.GetLayout(self).content);
				if (thumb.h > 0.0f) {
					painter.EmitQuad(thumb, ScrollThumbColor(paintCtx, self, thumb), Radii::All(3.0f));
				}
			};

		ElementHandle track = parts.track;
		auto grab = std::make_shared<f32>(0.0f);
		auto scrollToThumbTop = [context, viewport, track, thumbRect](f32 thumbTop) {
			const Element* view = context->GetElement(viewport);
			if (!view || view->maxScroll.y <= 0.0f) {
				return;
			}
			const Rect& trackRect = context->GetLayout(track).content;
			Rect thumb = thumbRect(trackRect);
			f32 movable = trackRect.h - thumb.h;
			if (movable <= 0.0f) {
				return;
			}
			f32 t = std::clamp((thumbTop - trackRect.y) / movable, 0.0f, 1.0f);
			context->SetScrollOffset(viewport, {view->scrollOffset.x, t * view->maxScroll.y});
		};
		ctx.Events(parts.track).onMouseDown = [context, track, thumbRect, grab, scrollToThumbTop]() {
			f32 mouseY = context->GetPointerPosition().y;
			Rect thumb = thumbRect(context->GetLayout(track).content);
			if (thumb.h > 0.0f && mouseY >= thumb.y && mouseY <= thumb.y + thumb.h) {
				*grab = mouseY - thumb.y;
				return;
			}
			*grab = thumb.h * 0.5f;
			scrollToThumbTop(mouseY - *grab);
		};
		ctx.Events(parts.track).onPressDrag = [grab, scrollToThumbTop](Vec2 mouse) {
			scrollToThumbTop(mouse.y - *grab);
		};
		return parts;
	}

	namespace {

		constexpr f32 SPLITTER_THICKNESS_PX = 4.0f;

		bool IsRow(FlexDirection axis) {
			return axis == FlexDirection::Row;
		}

		f32 MainOf(const Rect& rect, FlexDirection axis) {
			return IsRow(axis) ? rect.w : rect.h;
		}

		f32 MainOf(Vec2 point, FlexDirection axis) {
			return IsRow(axis) ? point.x : point.y;
		}

		void SetMainSize(Style& style, FlexDirection axis, Length size) {
			if (IsRow(axis)) {
				style.width = size;
			} else {
				style.height = size;
			}
		}

		void ApplySplitPosition(Context& ctx, const SplitParts& parts, FlexDirection axis, SplitSizing sizing, f32 position) {
			Style first = ctx.GetStyle(parts.first);
			Style second = ctx.GetStyle(parts.second);
			switch (sizing) {
			case SplitSizing::Ratio:
				SetMainSize(first, axis, Length::Pixel(0.0f));
				SetMainSize(second, axis, Length::Pixel(0.0f));
				first.flexGrow = position;
				second.flexGrow = 1.0f - position;
				break;
			case SplitSizing::FirstFixed:
				SetMainSize(first, axis, Length::Pixel(position));
				SetMainSize(second, axis, Length::Pixel(0.0f));
				first.flexGrow = 0.0f;
				second.flexGrow = 1.0f;
				break;
			case SplitSizing::SecondFixed:
				SetMainSize(first, axis, Length::Pixel(0.0f));
				SetMainSize(second, axis, Length::Pixel(position));
				first.flexGrow = 1.0f;
				second.flexGrow = 0.0f;
				break;
			}
			ctx.SetStyle(parts.first, first);
			ctx.SetStyle(parts.second, second);
		}

		f32 ClampBetween(f32 value, f32 lo, f32 hi) {
			return std::clamp(value, lo, std::max(lo, hi));
		}

	} // namespace

	f32 GetSplitterThickness() {
		return SPLITTER_THICKNESS_PX;
	}

	SplitParts Split(Context& ctx, ElementHandle parent, const SplitDesc& desc) {
		EnsureSkin(ctx);
		const FlexDirection axis = desc.axis;
		const SplitSizing sizing = desc.sizing;

		SplitParts parts;
		parts.root = ctx.CreateElement();
		Style rootStyle;
		rootStyle.flexDirection = axis;
		rootStyle.alignItems = Align::Stretch;
		rootStyle.width = Length::Percent(1.0f);
		rootStyle.height = Length::Percent(1.0f);
		rootStyle.flexGrow = 1.0f;
		rootStyle.overflow = Overflow::Hidden;
		ctx.SetStyle(parts.root, rootStyle);
		ctx.AppendChild(parent, parts.root);

		auto makePane = [&]() {
			ElementHandle pane = ctx.CreateElement();
			Style style;
			style.flexDirection = FlexDirection::Column;
			style.alignItems = Align::Stretch;
			if (IsRow(axis)) {
				style.height = Length::Percent(1.0f);
			} else {
				style.width = Length::Percent(1.0f);
			}
			style.overflow = Overflow::Hidden;
			ctx.SetStyle(pane, style);
			ctx.AppendChild(parts.root, pane);
			return pane;
		};

		parts.first = makePane();

		parts.splitter = ctx.CreateElement();
		Style bandStyle;
		SetMainSize(bandStyle, axis, Length::Pixel(SPLITTER_THICKNESS_PX));
		if (IsRow(axis)) {
			bandStyle.height = Length::Percent(1.0f);
			bandStyle.cursor = SystemCursor::ResizeEW;
		} else {
			bandStyle.width = Length::Percent(1.0f);
			bandStyle.cursor = SystemCursor::ResizeNS;
		}
		ctx.SetStyle(parts.splitter, bandStyle);
		ctx.AddClass(parts.splitter, StringHash("slt-splitter"));
		ctx.AppendChild(parts.root, parts.splitter);

		parts.second = makePane();
		ApplySplitPosition(ctx, parts, axis, sizing, desc.position);

		struct SplitDrag {
			f32 startMouseMain = 0.0f;
			f32 startMain = 0.0f;
		};
		auto drag = std::make_shared<SplitDrag>();
		Context* context = &ctx;
		SplitParts handles = parts;
		f32 minFirst = desc.minFirstPx;
		f32 minSecond = desc.minSecondPx;
		std::function<void(f32)> report = desc.onPositionChanged;

		ctx.Events(parts.splitter).onMouseDown = [context, handles, axis, sizing, drag]() {
			drag->startMouseMain = MainOf(context->GetPointerPosition(), axis);
			const Rect& firstBox = context->GetLayout(handles.first).borderBox;
			const Rect& secondBox = context->GetLayout(handles.second).borderBox;
			drag->startMain = MainOf(sizing == SplitSizing::SecondFixed ? secondBox : firstBox, axis);
		};

		ctx.Events(parts.splitter).onPressDrag = [context, handles, axis, sizing, drag, minFirst, minSecond, report](Vec2 mousePos) {
			if (!context->IsAlive(handles.root)) {
				return;
			}
			const f32 scale = context->GetContentScale();
			const f32 available = MainOf(context->GetLayout(handles.root).content, axis) - SPLITTER_THICKNESS_PX * scale;
			if (available <= 0.0f) {
				return;
			}
			const f32 delta = MainOf(mousePos, axis) - drag->startMouseMain;
			const f32 minFirstPhys = minFirst * scale;
			const f32 minSecondPhys = minSecond * scale;

			f32 position = 0.0f;
			if (sizing == SplitSizing::Ratio) {
				f32 firstPhys = ClampBetween(drag->startMain + delta, minFirstPhys, available - minSecondPhys);
				position = std::round(firstPhys) / available;
			} else if (sizing == SplitSizing::FirstFixed) {
				f32 firstPhys = ClampBetween(drag->startMain + delta, minFirstPhys, available - minSecondPhys);
				position = std::round(firstPhys) / scale;
			} else {
				f32 secondPhys = ClampBetween(drag->startMain - delta, minSecondPhys, available - minFirstPhys);
				position = std::round(secondPhys) / scale;
			}

			ApplySplitPosition(*context, handles, axis, sizing, position);
			if (report) {
				report(position);
			}
		};
		return parts;
	}

	TreeRowParts TreeRow(Context& ctx, ElementHandle parent, const TreeRowDesc& desc) {
		EnsureSkin(ctx);
		TreeRowParts parts;
		parts.row = ctx.CreateElement();
		ctx.AddClass(parts.row, StringHash("slt-tree-row"));
		if (desc.selected) {
			ctx.AddClass(parts.row, StringHash("slt-selected"));
		}
		ctx.SetUserTag(parts.row, desc.userTag);
		ctx.AppendChild(parent, parts.row);

		if (desc.depth > 0) {
			ElementHandle indent = ctx.CreateElement();
			ctx.AddClass(indent, StringHash("slt-tree-indent"));
			Style indentStyle;
			indentStyle.width = Length::Pixel(static_cast<f32>(desc.depth) * IndentStep(ctx));
			ctx.SetStyle(indent, indentStyle);
			ctx.SetHoverable(indent, false);
			ctx.AppendChild(parts.row, indent);

			ctx.Events(indent).customDraw = [depth = desc.depth, mask = desc.guideMask](Context& paintCtx, ElementHandle self, PaintScope& painter) {
				const Element* element = paintCtx.GetElement(self);
				if (!element || element->parent == INVALID_UI_INDEX) {
					return;
				}
				const Rect& column = paintCtx.GetLayout(self).borderBox;
				const Rect& box = paintCtx.GetLayout(paintCtx.MakeHandle(element->parent)).borderBox;
				if (box.h <= 0.0f) {
					return;
				}
				const f32 scale = paintCtx.GetContentScale();
				const f32 step = IndentStep(paintCtx) * scale;
				const f32 width = std::max(1.0f, std::floor(scale));
				const Color color = TreeGuideColor(paintCtx);
				if (color.a <= 0.0f) {
					return;
				}
				const auto stem = [&](f32 x, f32 top, f32 height) {
					painter.EmitQuad({std::round(x - width * 0.5f), top, width, height}, color);
				};
				const f32 middle = std::round(box.y + box.h * 0.5f);
				for (u32 level = 0; level + 1 < depth; ++level) {
					if ((mask & (1u << level)) != 0) {
						stem(column.x + (static_cast<f32>(level) + 0.5f) * step, box.y, box.h);
					}
				}

				const f32 own = column.x + (static_cast<f32>(depth) - 0.5f) * step;
				const bool continues = (mask & (1u << (depth - 1))) != 0;
				stem(own, box.y, continues ? box.h : middle - box.y + width);
				const f32 armLeft = std::round(own - width * 0.5f);
				const f32 armRight = column.x + column.w;
				if (armRight > armLeft) {
					painter.EmitQuad({armLeft, middle, armRight - armLeft, width}, color);
				}
			};
		}

		parts.arrow = ctx.CreateElement();
		ctx.AddClass(parts.arrow, StringHash("slt-tree-arrow"));
		ctx.SetText(parts.arrow, desc.font, desc.fontSizePx, TextDim(ctx),
					desc.hasChildren ? (desc.expanded ? "v" : ">") : " ");
		ctx.SetHoverable(parts.arrow, desc.hasChildren);
		if (desc.hasChildren && desc.onToggleExpand) {
			ctx.Events(parts.arrow).onClick = desc.onToggleExpand;
		}
		ctx.AppendChild(parts.row, parts.arrow);

		parts.labelElement = MakeLabel(ctx, parts.row, desc.font, desc.fontSizePx, TextNormal(ctx), desc.label);
		ctx.AddClass(parts.labelElement, StringHash("slt-tree-label"));

		Context* context = &ctx;
		ElementEvents& events = ctx.Events(parts.row);
		if (desc.onSelect) {
			events.onClick = [context, onSelect = desc.onSelect]() {
				onSelect(context->GetPressMods());
			};
		}
		if (desc.onDoubleClick) {
			events.onDoubleClick = desc.onDoubleClick;
		}
		if (desc.onRightClick) {
			events.onRightClick = desc.onRightClick;
		}
		if (desc.onDragRow) {
			events.onPressDrag = desc.onDragRow;
		}
		events.onDragOver = desc.onDragOver;
		events.onDrop = desc.onDrop;
		return parts;
	}

	void SetTreeSelection(Context& ctx, ElementHandle rowsParent, std::span<const u64> selectedTags) {
		if (!ctx.IsAlive(rowsParent)) {
			return;
		}
		if (ctx.HasClass(rowsParent, StringHash("slt-tree-row"))) {
			const Element* element = ctx.GetElement(rowsParent);
			bool wanted = element != nullptr &&
						  std::find(selectedTags.begin(), selectedTags.end(), element->userTag) != selectedTags.end();
			if (wanted != ctx.HasClass(rowsParent, StringHash("slt-selected"))) {
				if (wanted) {
					ctx.AddClass(rowsParent, StringHash("slt-selected"));
				} else {
					ctx.RemoveClass(rowsParent, StringHash("slt-selected"));
				}
			}
		}
		const Element* parent = ctx.GetElement(rowsParent);
		for (u32 child = parent ? parent->firstChild : INVALID_UI_INDEX; child != INVALID_UI_INDEX;) {
			ElementHandle handle = ctx.MakeHandle(child);
			SetTreeSelection(ctx, handle, selectedTags);
			const Element* childElement = ctx.GetElement(handle);
			child = childElement ? childElement->nextSibling : INVALID_UI_INDEX;
		}
	}

	ElementHandle OpenMenu(Context& ctx, Vec2 anchorPx, std::span<const MenuItem> items,
						   Handle<FontAsset> font, f32 fontSizePx) {
		EnsureSkin(ctx);
		ElementHandle content = BuildMenuContent(ctx, items, font, fontSizePx, {});
		ctx.OpenPopup(content, anchorPx);
		return content;
	}

	ElementHandle MenuBar(Context& ctx, ElementHandle parent,
						  std::span<const MenuBarItemDesc> menus,
						  Handle<FontAsset> font, f32 fontSizePx) {
		EnsureSkin(ctx);
		ElementHandle bar = ctx.CreateElement();
		ctx.AddClass(bar, StringHash("slt-menu-bar"));
		ctx.AppendChild(parent, bar);

		struct BarState {
			i32 openIndex = -1;
			ElementHandle openContent;
		};
		auto state = std::make_shared<BarState>();

		auto owned = std::make_shared<Vector<MenuBarItemDesc>>(GetMemoryResource(MemTag::UI));
		owned->reserve(menus.size());
		for (const MenuBarItemDesc& menu : menus) {
			owned->push_back(menu);
		}

		Context* context = &ctx;
		for (i32 i = 0; i < static_cast<i32>(owned->size()); ++i) {
			ElementHandle item = ctx.CreateElement();
			ctx.AddClass(item, StringHash("slt-menu-bar-item"));
			ctx.SetText(item, font, fontSizePx, MenuText(ctx), (*owned)[static_cast<size_t>(i)].label);
			ctx.AppendChild(bar, item);

			auto open = [context, state, owned, item, i, font, fontSizePx]() {
				if (state->openContent.IsValid() && context->IsPopupOpen(state->openContent)) {
					context->ClosePopup(state->openContent);
				}
				Vector<MenuItem> items{GetMemoryResource(MemTag::UI)};
				(*owned)[static_cast<size_t>(i)].fill(items);
				ElementHandle content = BuildMenuContent(*context, {items.data(), items.size()}, font, fontSizePx, {});

				auto baseClosed = context->Events(content).onPopupClosed;
				context->Events(content).onPopupClosed = [context, state, content, baseClosed]() {
					if (state->openContent == content) {
						state->openIndex = -1;
						state->openContent = {};
					}
					if (baseClosed) {
						baseClosed();
					}
				};

				const Rect& box = context->GetLayout(item).borderBox;
				context->OpenPopup(content, {box.x, box.y + box.h});
				state->openIndex = i;
				state->openContent = content;
			};

			ctx.Events(item).onClick = [state, i, open]() {
				if (state->openIndex == i) {
					return;
				}
				open();
			};
			ctx.Events(item).onHoverEnter = [state, i, open]() {
				if (state->openIndex >= 0 && state->openIndex != i) {
					open();
				}
			};
		}
		return bar;
	}

} // namespace Shalltear::UI::Widgets
