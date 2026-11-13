#include "Shalltear/Editor/GradientEditor.h"

#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/UI/RenderList.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorTheme.h"

#include <algorithm>
#include <memory>

namespace Shalltear::GradientEditor {

	namespace {

		using ColorToken = EditorTheme::ColorToken;
		using MetricToken = EditorTheme::MetricToken;

		constexpr f32 MARKER_SIZE = 8.0f;
		constexpr f32 MARKER_PICK = 6.0f;
		constexpr f32 RAMP_HEIGHT = 24.0f;
		constexpr u32 PREVIEW_BANDS = 48;
		constexpr std::string_view Modes[] = {"Blend", "Fixed", "Perceptual Blend"};

		template <typename Key>
		i32 Sort(Vector<Key>& keys, i32 index) {
			if (index < 0 || static_cast<size_t>(index) >= keys.size()) {
				return -1;
			}
			const Key moved = keys[static_cast<size_t>(index)];
			keys.erase(keys.begin() + index);
			const auto at = std::upper_bound(keys.begin(), keys.end(), moved.time, [](f32 time, const Key& key) { return time < key.time; });
			const i32 placed = static_cast<i32>(at - keys.begin());
			keys.insert(at, moved);
			return placed;
		}

		struct State {
			Gradient gradient{GetMemoryResource(MemTag::Editor)};
			UI::ElementHandle alphaStrip;
			UI::ElementHandle colorStrip;
			UI::ElementHandle keyRow;
			bool alpha = false;
			i32 key = -1;
			bool grabbing = false;
			bool editing = false;
			std::function<void(const Gradient&)> onChange;
			std::function<void()> onEditBegin;
			std::function<void()> onEditEnd;
		};

		void Begin(State& state) {
			if (!state.editing) {
				state.editing = true;
				if (state.onEditBegin) {
					state.onEditBegin();
				}
			}
		}

		void End(State& state) {
			if (state.editing) {
				state.editing = false;
				if (state.onEditEnd) {
					state.onEditEnd();
				}
			}
		}

		void Publish(const State& state) {
			if (state.onChange) {
				state.onChange(state.gradient);
			}
		}

		template <typename Edit>
		void Once(State& state, Edit&& edit) {
			Begin(state);
			edit(state.gradient);
			Publish(state);
			End(state);
		}

		f32 TimeAt(UI::Context& context, UI::ElementHandle strip, f32 x) {
			const UI::Rect box = context.GetLayout(strip).borderBox;
			return box.w > 0.0f ? Math::Clamp01((x - box.x) / box.w) : 0.0f;
		}

		i32 Pick(UI::Context& context, const State& state, bool alpha, f32 x) {
			const UI::Rect box = context.GetLayout(alpha ? state.alphaStrip : state.colorStrip).borderBox;
			const f32 reach = MARKER_PICK * (context.GetContentScale() > 0.0f ? context.GetContentScale() : 1.0f);
			i32 best = -1;
			f32 nearest = reach;
			const auto consider = [&](f32 time, size_t index) {
				const f32 distance = Math::Abs(box.x + time * box.w - x);
				if (distance <= nearest) {
					nearest = distance;
					best = static_cast<i32>(index);
				}
			};
			if (alpha) {
				for (size_t i = 0; i < state.gradient.alphaKeys.size(); ++i) {
					consider(state.gradient.alphaKeys[i].time, i);
				}
			} else {
				for (size_t i = 0; i < state.gradient.colorKeys.size(); ++i) {
					consider(state.gradient.colorKeys[i].time, i);
				}
			}
			return best;
		}

		void PaintMarkers(UI::Context& context, UI::PaintScope& paint, const State& state, bool alpha) {
			const UI::Rect box = context.GetLayout(alpha ? state.alphaStrip : state.colorStrip).borderBox;
			const f32 scale = context.GetContentScale() > 0.0f ? context.GetContentScale() : 1.0f;
			const f32 size = MARKER_SIZE * scale;
			const size_t count = alpha ? state.gradient.alphaKeys.size() : state.gradient.colorKeys.size();
			for (size_t i = 0; i < count; ++i) {
				const f32 time = alpha ? state.gradient.alphaKeys[i].time : state.gradient.colorKeys[i].time;
				Color fill = Color::White();
				if (alpha) {
					const f32 value = Math::Clamp01(state.gradient.alphaKeys[i].alpha);
					fill = Color(value, value, value, 1.0f);
				} else {
					fill = state.gradient.colorKeys[i].color;
					fill.a = 1.0f;
				}
				const UI::Rect marker{box.x + time * box.w - size * 0.5f, box.y + (box.h - size) * 0.5f, size, size};
				const bool selected = state.alpha == alpha && state.key == static_cast<i32>(i);
				paint.EmitQuad(marker, fill);
				paint.EmitRing(marker, UI::Edges::All(scale), EditorTheme::GetColor(selected ? ColorToken::Accent : ColorToken::TextStrong));
			}
		}

		UI::ElementHandle AddLabel(UI::Context& context, UI::ElementHandle parent, std::string_view text) {
			UI::ElementHandle label = context.CreateElement();
			context.AppendChild(parent, label);
			context.SetText(label, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontSmall), EditorTheme::GetColor(ColorToken::TextDim),
							text);
			context.SetHoverable(label, false);
			return label;
		}

		void BuildKeyRow(UI::Context& context, const std::shared_ptr<State>& state) {
			UI::ElementHandle row = state->keyRow;
			context.ClearChildren(row);
			const bool alpha = state->alpha;
			const size_t count = alpha ? state->gradient.alphaKeys.size() : state->gradient.colorKeys.size();
			if (state->key < 0 || static_cast<size_t>(state->key) >= count) {
				return;
			}
			const size_t key = static_cast<size_t>(state->key);
			const f32 fontSize = EditorTheme::GetMetric(MetricToken::FontSmall);
			AddLabel(context, row, alpha ? "Alpha" : "Color");
			if (alpha) {
				UI::Widgets::NumberFieldDesc value;
				value.value = state->gradient.alphaKeys[key].alpha;
				value.minValue = 0.0;
				value.maxValue = 1.0;
				value.dragScale = 0.01;
				value.font = UISystem::GetDefaultFont();
				value.fontSizePx = fontSize;
				value.onChange = [state](f64 next) {
					if (state->key >= 0 && static_cast<size_t>(state->key) < state->gradient.alphaKeys.size()) {
						Once(*state, [&](Gradient& g) { g.alphaKeys[static_cast<size_t>(state->key)].alpha = static_cast<f32>(next); });
					}
				};
				UI::Widgets::NumberField(context, row, value);
			} else {
				UI::Widgets::ColorSwatchField(
					context, row, state->gradient.colorKeys[key].color, UISystem::GetDefaultFont(), fontSize,
					[state](Color next) {
						if (state->key >= 0 && static_cast<size_t>(state->key) < state->gradient.colorKeys.size()) {
							Begin(*state);
							next.a = 1.0f;
							state->gradient.colorKeys[static_cast<size_t>(state->key)].color = next;
							Publish(*state);
						}
					},
					[state]() { Begin(*state); }, [state]() { End(*state); });
			}
			AddLabel(context, row, "Location");
			UI::Widgets::NumberFieldDesc location;
			location.value = (alpha ? state->gradient.alphaKeys[key].time : state->gradient.colorKeys[key].time) * 100.0f;
			location.minValue = 0.0;
			location.maxValue = 100.0;
			location.dragScale = 0.5;
			location.decimals = 1;
			location.font = UISystem::GetDefaultFont();
			location.fontSizePx = fontSize;
			location.onChange = [state](f64 percent) {
				Once(*state, [&](Gradient& g) { state->key = MoveKey(g, state->alpha, state->key, static_cast<f32>(percent / 100.0)); });
			};
			UI::Widgets::NumberField(context, row, location);
		}

		void Select(UI::Context& context, const std::shared_ptr<State>& state, bool alpha, i32 key) {
			state->alpha = alpha;
			state->key = key;
			BuildKeyRow(context, state);
		}

		UI::ElementHandle AddStrip(UI::Context& context, UI::ElementHandle parent, const std::shared_ptr<State>& state, bool alpha) {
			UI::Context* contextPtr = &context;
			UI::ElementHandle strip = context.CreateElement();
			context.AppendChild(parent, strip);
			context.AddClass(strip, EditorTheme::Class::Clickable);
			context.SetFocusable(strip, true);
			UI::Style style;
			style.width = UI::Length::Pixel(EditorTheme::GetMetric(MetricToken::CurveEditorWidth));
			style.height = UI::Length::Pixel(MARKER_SIZE * 2.0f);
			context.SetStyle(strip, style);
			context.Events(strip).customDraw = [state, alpha](UI::Context& c, UI::ElementHandle, UI::PaintScope& paint) { PaintMarkers(c, paint, *state, alpha); };
			context.Events(strip).onMouseDown = [state, strip, alpha, contextPtr]() {
				contextPtr->SetFocus(strip);
				const f32 x = contextPtr->GetPointerPosition().x;
				i32 key = Pick(*contextPtr, *state, alpha, x);
				if (key < 0) {
					Once(*state, [&](Gradient& g) { key = AddKey(g, alpha, TimeAt(*contextPtr, strip, x)); });
				}
				state->grabbing = key >= 0;
				Select(*contextPtr, state, alpha, key);
			};
			context.Events(strip).onPressDrag = [state, strip, alpha, contextPtr](Vec2 point) {
				if (!state->grabbing || state->alpha != alpha || state->key < 0) {
					return;
				}
				Begin(*state);
				state->key = MoveKey(state->gradient, alpha, state->key, TimeAt(*contextPtr, strip, point.x));
				Publish(*state);
			};
			context.Events(strip).onPressEnd = [state, contextPtr]() {
				if (state->grabbing) {
					state->grabbing = false;
					End(*state);
					BuildKeyRow(*contextPtr, state);
				}
			};
			context.Events(strip).onKeyDown = [state, alpha, contextPtr](Key key, KeyMod mods) {
				if ((key != Key::Delete && key != Key::Backspace) || mods != KeyMod::None || state->alpha != alpha || state->key < 0) {
					return false;
				}
				bool removed = false;
				Once(*state, [&](Gradient& g) { removed = RemoveKey(g, alpha, state->key); });
				if (removed) {
					Select(*contextPtr, state, alpha, -1);
				}
				return true;
			};
			return strip;
		}

	} // anonymous namespace

	i32 AddKey(Gradient& gradient, bool alpha, f32 time) {
		const f32 at = Math::Clamp01(time);
		const Color sample = gradient.Evaluate(at);
		if (alpha) {
			if (gradient.alphaKeys.size() >= Gradient::MAX_KEYS) {
				return -1;
			}
			gradient.alphaKeys.push_back({sample.a, at});
			return Sort(gradient.alphaKeys, static_cast<i32>(gradient.alphaKeys.size()) - 1);
		}
		if (gradient.colorKeys.size() >= Gradient::MAX_KEYS) {
			return -1;
		}
		gradient.colorKeys.push_back({Color(sample.r, sample.g, sample.b, 1.0f), at});
		return Sort(gradient.colorKeys, static_cast<i32>(gradient.colorKeys.size()) - 1);
	}

	i32 MoveKey(Gradient& gradient, bool alpha, i32 index, f32 time) {
		const f32 at = Math::Clamp01(time);
		if (alpha) {
			if (index < 0 || static_cast<size_t>(index) >= gradient.alphaKeys.size()) {
				return -1;
			}
			gradient.alphaKeys[static_cast<size_t>(index)].time = at;
			return Sort(gradient.alphaKeys, index);
		}
		if (index < 0 || static_cast<size_t>(index) >= gradient.colorKeys.size()) {
			return -1;
		}
		gradient.colorKeys[static_cast<size_t>(index)].time = at;
		return Sort(gradient.colorKeys, index);
	}

	bool RemoveKey(Gradient& gradient, bool alpha, i32 index) {
		const size_t count = alpha ? gradient.alphaKeys.size() : gradient.colorKeys.size();
		if (count <= 1 || index < 0 || static_cast<size_t>(index) >= count) {
			return false;
		}
		if (alpha) {
			gradient.alphaKeys.erase(gradient.alphaKeys.begin() + index);
		} else {
			gradient.colorKeys.erase(gradient.colorKeys.begin() + index);
		}
		return true;
	}

	void PaintPreview(UI::Context& context, UI::ElementHandle part, UI::PaintScope& paint, const Gradient& gradient) {
		const UI::Rect box = context.GetLayout(part).borderBox;
		if (!(box.w > 0.0f) || !(box.h > 0.0f)) {
			return;
		}
		const f32 band = box.w / static_cast<f32>(PREVIEW_BANDS);
		for (u32 i = 0; i < PREVIEW_BANDS; ++i) {
			const Color color = gradient.Evaluate((static_cast<f32>(i) + 0.5f) / static_cast<f32>(PREVIEW_BANDS));
			paint.EmitQuad({box.x + band * static_cast<f32>(i), box.y, band + 0.5f, box.h}, color);
		}
	}

	UI::ElementHandle Open(UI::Context& context, UI::ElementHandle anchor, const Gradient& gradient, std::function<void(const Gradient&)> onChange,
						   std::function<void()> onEditBegin, std::function<void()> onEditEnd) {
		UI::Context* contextPtr = &context;
		auto state = std::make_shared<State>();
		state->gradient = gradient;
		state->onChange = std::move(onChange);
		state->onEditBegin = std::move(onEditBegin);
		state->onEditEnd = std::move(onEditEnd);

		UI::ElementHandle content = context.CreateElement();
		context.AddClass(content, EditorTheme::Class::CurveEditor);
		context.Events(content).onPopupClosed = [contextPtr, content, state]() {
			End(*state);
			contextPtr->DestroyElementNextUpdate(content);
		};

		state->alphaStrip = AddStrip(context, content, state, true);
		UI::ElementHandle ramp = context.CreateElement();
		context.AppendChild(content, ramp);
		context.AddClass(ramp, EditorTheme::Class::CurveEditorPlot);
		UI::Style rampStyle;
		rampStyle.width = UI::Length::Pixel(EditorTheme::GetMetric(MetricToken::CurveEditorWidth));
		rampStyle.height = UI::Length::Pixel(RAMP_HEIGHT);
		context.SetStyle(ramp, rampStyle);
		context.SetHoverable(ramp, false);
		context.Events(ramp).customDraw = [state](UI::Context& c, UI::ElementHandle self, UI::PaintScope& paint) { PaintPreview(c, self, paint, state->gradient); };
		state->colorStrip = AddStrip(context, content, state, false);

		state->keyRow = context.CreateElement();
		context.AppendChild(content, state->keyRow);
		UI::Style rowStyle;
		rowStyle.flexDirection = UI::FlexDirection::Row;
		rowStyle.alignItems = UI::Align::Center;
		context.SetStyle(state->keyRow, rowStyle);

		UI::ElementHandle modes = context.CreateElement();
		context.AppendChild(content, modes);
		context.SetStyle(modes, rowStyle);
		AddLabel(context, modes, "Mode");
		UI::Widgets::DropdownDesc desc;
		desc.items = Modes;
		desc.selected = static_cast<u32>(gradient.mode);
		desc.font = UISystem::GetDefaultFont();
		desc.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
		desc.onSelect = [state](u32 index) {
			Once(*state, [index](Gradient& g) { g.mode = static_cast<GradientMode>(Math::Min<u32>(index, 2u)); });
		};
		UI::Widgets::Dropdown(context, modes, desc);

		const UI::Rect& box = context.GetLayout(anchor).borderBox;
		context.OpenPopup(content, {box.x, box.y + box.h});
		return content;
	}

} // namespace Shalltear::GradientEditor
