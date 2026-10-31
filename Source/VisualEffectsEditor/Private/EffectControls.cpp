#include "EffectControls.h"

#include "EffectInstance.h"
#include "EffectPass.h"

#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"
#include "Shalltear/Editor/EditorTheme.h"

#include "Shalltear/VisualEffects/VisualEffect.h"

#include <algorithm>
#include <cmath>
#include <format>
#include <memory>

namespace Shalltear::VisualEffectsEditor::EffectControls {

	using namespace VisualEffectsImpl;
	using EditorTheme::MetricToken;

	namespace {

		constexpr f32 MIN_PERCENT = 1.0f;
		constexpr f32 MAX_PERCENT = 4000.0f;
		constexpr f32 SLIDER_POWER = 10.0f;
		constexpr i32 PRESETS[] = {1, 10, 50, 100, 200, 500, 1000, 4000};

		EffectInstance* Instance(const VisualEffect& effect) { return EffectComponentAccess::Instance(const_cast<VisualEffect&>(effect)); }

		Vector<Handle<VisualEffect>> Gather(const Targets& targets) {
			Vector<Handle<VisualEffect>> out{GetMemoryResource(MemTag::Editor)};
			if (targets) {
				targets(out);
			}
			std::erase_if(out, [](const Handle<VisualEffect>& handle) { return handle.Get() == nullptr; });
			return out;
		}

	} // anonymous namespace

	void Stop(VisualEffect& effect) {
		effect.Reinit();
		if (EffectInstance* instance = Instance(effect)) {
			instance->SetPaused(true);
		}
	}

	void PlayPause(VisualEffect& effect) {
		if (EffectInstance* instance = Instance(effect)) {
			instance->SetPaused(!instance->IsPaused());
		}
	}

	void Step(VisualEffect& effect) {
		if (EffectInstance* instance = Instance(effect)) {
			instance->Step();
		}
	}

	void Restart(VisualEffect& effect) {
		effect.Reinit();
		if (EffectInstance* instance = Instance(effect)) {
			instance->SetPaused(false);
		}
	}

	bool IsPaused(const VisualEffect& effect) {
		const EffectInstance* instance = Instance(effect);
		return instance && instance->IsPaused();
	}

	void SetRate(VisualEffect& effect, f32 rate) {
		if (EffectInstance* instance = Instance(effect)) {
			instance->SetControlRate(std::clamp(rate, MIN_PERCENT / 100.0f, MAX_PERCENT / 100.0f));
		}
	}

	f32 GetRate(const VisualEffect& effect) {
		const EffectInstance* instance = Instance(effect);
		return instance ? instance->GetControlRate() : 1.0f;
	}

	void Attach(VisualEffect& effect) {
		if (EffectInstance* instance = Instance(effect)) {
			instance->WatchCounts(true);
		}
	}

	void Detach(VisualEffect& effect) {
		if (EffectInstance* instance = Instance(effect)) {
			instance->WatchCounts(false);
			instance->SetControlRate(1.0f);
			instance->SetPaused(false);
		}
	}

	u32 SendToEvery(std::string_view name) {
		Vector<Handle<VisualEffect>> effects{GetMemoryResource(MemTag::Editor)};
		GetEffects(effects);
		for (const Handle<VisualEffect>& handle : effects) {
			handle.Get()->SendEvent(name);
		}
		return static_cast<u32>(effects.size());
	}

	f32 SliderOfRate(f32 rate) { return std::pow(std::clamp(rate * 100.0f, MIN_PERCENT, MAX_PERCENT), 1.0f / SLIDER_POWER); }

	f32 RateOfSlider(f32 position) { return std::clamp(std::pow(position, SLIDER_POWER), MIN_PERCENT, MAX_PERCENT) / 100.0f; }

	std::span<const i32> RatePresets() { return PRESETS; }

	bool WorldBounds(const VisualEffect& effect, AABB& out) {
		const EffectInstance* instance = Instance(effect);
		if (!instance || instance->GetSystemCount() == 0) {
			return false;
		}
		const SceneObject* owner = effect.GetOwner().Get();
		out = instance->GetWorldBounds(owner ? owner->GetWorldMatrix() : Mat4::Identity());
		return true;
	}

	Refresh BuildControls(UI::Context& context, UI::ElementHandle parent, Targets targets) {
		const Handle<FontAsset> font = UISystem::GetDefaultFont();
		const f32 size = EditorTheme::GetMetric(MetricToken::FontSmall);
		auto row = [&]() {
			UI::ElementHandle line = context.CreateElement();
			context.AppendChild(parent, line);
			UI::Style style;
			style.flexDirection = UI::FlexDirection::Row;
			style.alignItems = UI::Align::Center;
			context.SetStyle(line, style);
			return line;
		};
		auto button = [&](UI::ElementHandle line, std::string_view label, std::function<void(VisualEffect&)> act) {
			UI::Widgets::ButtonDesc desc;
			desc.label = label;
			desc.font = font;
			desc.fontSizePx = size;
			desc.onClick = [targets, act = std::move(act)]() {
				for (const Handle<VisualEffect>& handle : Gather(targets)) {
					act(*handle.Get());
				}
			};
			return UI::Widgets::Button(context, line, desc);
		};

		UI::ElementHandle buttons = row();
		button(buttons, "Stop", &Stop);
		const UI::ElementHandle play = button(buttons, "Play", &PlayPause);
		const UI::ElementHandle pause = button(buttons, "Pause", &PlayPause);
		button(buttons, "Step", &Step);
		button(buttons, "Restart", &Restart);

		UI::ElementHandle rate = row();
		auto applyRate = [targets](f32 value) {
			for (const Handle<VisualEffect>& handle : Gather(targets)) {
				SetRate(*handle.Get(), value);
			}
		};
		UI::Widgets::SliderDesc slider;
		slider.minValue = SliderOfRate(MIN_PERCENT / 100.0f);
		slider.maxValue = SliderOfRate(MAX_PERCENT / 100.0f);
		slider.value = SliderOfRate(1.0f);
		slider.font = font;
		slider.fontSizePx = size;
		slider.onChange = [applyRate](f32 position) { applyRate(RateOfSlider(position)); };
		const UI::Widgets::SliderParts track = UI::Widgets::Slider(context, rate, slider);
		UI::Style grow = context.GetStyle(track.root);
		grow.flexGrow = 1.0f;
		context.SetStyle(track.root, grow);
		UI::Widgets::NumberFieldDesc percent;
		percent.value = 100.0;
		percent.minValue = MIN_PERCENT;
		percent.maxValue = MAX_PERCENT;
		percent.decimals = 0;
		percent.dragScale = 1.0;
		percent.font = font;
		percent.fontSizePx = size;
		percent.onChange = [applyRate](f64 value) { applyRate(static_cast<f32>(std::round(value)) / 100.0f); };
		const UI::Widgets::NumberFieldParts field = UI::Widgets::NumberField(context, rate, percent);
		UI::Widgets::ButtonDesc set;
		set.label = "Set";
		set.font = font;
		set.fontSizePx = size;
		UI::Context* where = &context;
		set.onClick = [where, applyRate, font, size]() {
			Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::UI)};
			for (i32 preset : PRESETS) {
				items.push_back({std::format("{}%", preset), "", true, false, [applyRate, preset]() { applyRate(static_cast<f32>(preset) / 100.0f); }});
			}
			UI::Widgets::OpenMenu(*where, where->GetPointerPosition(), items, font, size);
		};
		UI::Widgets::Button(context, rate, set);

		auto shown = std::make_shared<f32>(-1.0f);
		return [targets, play, pause, track, field, shown](UI::Context& refreshed) {
			const Vector<Handle<VisualEffect>> live = Gather(targets);
			const VisualEffect* first = live.empty() ? nullptr : live[0].Get();
			const bool paused = first && IsPaused(*first);
			for (auto [element, visible] : {std::pair{play, paused}, std::pair{pause, !paused}}) {
				if (refreshed.IsAlive(element)) {
					UI::Style style = refreshed.GetStyle(element);
					style.display = visible ? UI::Display::Flex : UI::Display::None;
					refreshed.SetStyle(element, style);
				}
			}
			const f32 rate = first ? GetRate(*first) : 1.0f;
			if (rate != *shown) {
				*shown = rate;
				UI::Widgets::SetSliderValue(refreshed, track, SliderOfRate(MIN_PERCENT / 100.0f), SliderOfRate(MAX_PERCENT / 100.0f), SliderOfRate(rate));
				if (refreshed.GetFocused() != field.field) {
					UI::Widgets::SetNumberFieldValue(refreshed, field, std::round(rate * 100.0f), 0);
				}
			}
		};
	}

} // namespace Shalltear::VisualEffectsEditor::EffectControls
