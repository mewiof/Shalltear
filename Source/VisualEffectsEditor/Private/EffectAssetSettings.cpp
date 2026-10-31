#include "EffectAssetSettings.h"

#include "EffectGraphEditOps.h"
#include "GraphEffect.h"

#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/GraphDocument.h"
#include "Shalltear/Editor/PropertyRows.h"

#include <algorithm>
#include <iterator>

namespace Shalltear::VisualEffectsEditor::EffectAssetSettings {

	namespace {

		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;
		using GraphWindowParts::AddField;
		using GraphWindowParts::AddText;
		using GraphWindowParts::FontRow;
		namespace Ops = EffectGraphEditOps;

	} // anonymous namespace

	void Build(UI::Context& context, UI::ElementHandle parent, const GraphDocument& document, const GraphWindowParts::Editor& editor) {
		const GraphEffectTiming timing = GraphEffectTarget::Timing(document);
		AddText(context, parent, ColorToken::TextStrong, MetricToken::FontBody, "Update mode");
		const auto toggle = [&](std::string_view label, std::string_view key, bool value, bool offered) {
			const UI::Widgets::CheckboxParts box =
				UI::Widgets::Checkbox(context, AddField(context, parent, label), value, [editor, key = str(key), offered](bool on) {
					if (offered) {
						editor.edit("Update Mode", [&](GraphDocument& edited) { return Ops::SetUpdateMode(edited, key, on); });
					}
				});
			if (!offered) {
				PropertyRows::MakeDisabled(context, box.box);
			}
		};
		toggle("Fixed Delta Time", GRAPH_EFFECT_FIXED_DELTA_TIME, timing.fixedDeltaTime, true);
		toggle("Exact Fixed Time", GRAPH_EFFECT_EXACT_FIXED_TIME, timing.exactFixedTime, timing.fixedDeltaTime);
		toggle("Ignore Time Scale", GRAPH_EFFECT_IGNORE_TIME_SCALE, timing.ignoreTimeScale, true);

		static constexpr std::string_view CULLING_OPTIONS[] = {"Simulate and update bounds only while visible",
															   "Always update bounds, simulate only while visible",
															   "Always simulate and update bounds"};
		static constexpr u32 CULLING_FLAGS[] = {GRAPH_EFFECT_CULL_DEFAULT, GRAPH_EFFECT_CULL_SIMULATION, 0};
		UI::Widgets::DropdownDesc culling;
		culling.items = CULLING_OPTIONS;
		culling.selected = static_cast<u32>(std::size(CULLING_FLAGS));
		for (u32 i = 0; i < std::size(CULLING_FLAGS); ++i) {
			culling.selected = CULLING_FLAGS[i] == timing.cullingFlags ? i : culling.selected;
		}
		culling.font = UISystem::GetDefaultFont();
		culling.fontSizePx = FontRow();
		const bool forced = GraphEffectTarget::HasAutomaticBounds(document);
		culling.onSelect = [editor, forced](u32 index) {
			if (!forced && index < std::size(CULLING_FLAGS)) {
				editor.edit("Culling Flags", [&](GraphDocument& edited) { return Ops::SetCullingFlags(edited, CULLING_FLAGS[index]); });
			}
		};
		const UI::Widgets::DropdownParts popup = UI::Widgets::Dropdown(context, AddField(context, parent, "Culling Flags"), culling);
		if (forced) {
			PropertyRows::MakeDisabled(context, popup.root);
		}

		AddText(context, parent, ColorToken::TextStrong, MetricToken::FontBody, "Initial state");
		const auto number = [&](std::string_view label, Ops::PrewarmField field, f64 value, bool whole) {
			UI::Widgets::NumberFieldDesc desc;
			desc.value = value;
			desc.minValue = 0.0;
			desc.maxValue = whole ? static_cast<f64>(GRAPH_EFFECT_PREWARM_MAX_STEPS) : 1.0e300;
			desc.decimals = whole ? 0 : UI::Widgets::DEFAULT_DECIMALS;
			desc.dragScale = whole ? 1.0 : 0.01;
			desc.font = UISystem::GetDefaultFont();
			desc.fontSizePx = FontRow();
			desc.onChange = [editor, field](f64 changed) {
				editor.edit("PreWarm", [&](GraphDocument& edited) { return Ops::SetPrewarm(edited, field, changed); });
			};
			desc.onScrubBegin = [editor]() { editor.begin("PreWarm"); };
			desc.onScrubEnd = [editor]() { editor.end(); };
			UI::Widgets::NumberField(context, AddField(context, parent, label), desc);
		};
		number("PreWarm Total Time", Ops::PrewarmField::TotalTime, static_cast<f64>(timing.prewarmDeltaTime) * timing.prewarmStepCount, false);
		number("PreWarm Step Count", Ops::PrewarmField::StepCount, timing.prewarmStepCount, true);
		number("PreWarm Delta Time", Ops::PrewarmField::DeltaTime, timing.prewarmDeltaTime, false);

		const GraphEffectInstancing instancing = GraphEffectTarget::Instancing(document);
		AddText(context, parent, ColorToken::TextStrong, MetricToken::FontBody, "Instancing");
		if (instancing.refused != 0) {
			AddText(context, parent, ColorToken::WidgetTextDim, MetricToken::FontSmall, "Instancing is off for this effect:");
			if ((instancing.refused & GRAPH_EFFECT_INSTANCING_GPU_EVENT) != 0) {
				AddText(context, parent, ColorToken::WidgetTextDim, MetricToken::FontSmall, "- it uses GPU events");
			}
		}
		static constexpr std::string_view INSTANCING_OPTIONS[] = {"Disabled", "Automatic batch capacity", "Custom batch capacity"};
		static constexpr i32 INSTANCING_MODES[] = {GRAPH_EFFECT_INSTANCING_DISABLED, GRAPH_EFFECT_INSTANCING_AUTOMATIC, GRAPH_EFFECT_INSTANCING_CUSTOM};
		const bool refused = instancing.refused != 0;
		const i32 shown = refused ? GRAPH_EFFECT_INSTANCING_DISABLED : instancing.mode;
		UI::Widgets::DropdownDesc mode;
		mode.items = INSTANCING_OPTIONS;
		mode.selected = 0;
		for (u32 i = 0; i < std::size(INSTANCING_MODES); ++i) {
			mode.selected = INSTANCING_MODES[i] == shown ? i : mode.selected;
		}
		mode.font = UISystem::GetDefaultFont();
		mode.fontSizePx = FontRow();
		mode.onSelect = [editor, refused](u32 index) {
			if (!refused && index < std::size(INSTANCING_MODES)) {
				editor.edit("Instancing Mode", [&](GraphDocument& edited) { return Ops::SetInstancingMode(edited, INSTANCING_MODES[index]); });
			}
		};
		const UI::Widgets::DropdownParts modePopup = UI::Widgets::Dropdown(context, AddField(context, parent, "Instancing Mode"), mode);
		if (refused) {
			PropertyRows::MakeDisabled(context, modePopup.root);
		}
		if (shown == GRAPH_EFFECT_INSTANCING_CUSTOM) {
			UI::Widgets::NumberFieldDesc capacity;
			capacity.value = instancing.capacity;
			capacity.minValue = 1.0;
			capacity.maxValue = static_cast<f64>(GRAPH_EFFECT_INSTANCING_MAX_CAPACITY);
			capacity.decimals = 0;
			capacity.dragScale = 1.0;
			capacity.font = UISystem::GetDefaultFont();
			capacity.fontSizePx = FontRow();
			capacity.onChange = [editor](f64 changed) {
				const u32 whole = static_cast<u32>(std::clamp(changed, 1.0, static_cast<f64>(GRAPH_EFFECT_INSTANCING_MAX_CAPACITY)));
				editor.edit("Max Batch Capacity", [&](GraphDocument& edited) { return Ops::SetInstancingCapacity(edited, whole); });
			};
			capacity.onScrubBegin = [editor]() { editor.begin("Max Batch Capacity"); };
			capacity.onScrubEnd = [editor]() { editor.end(); };
			UI::Widgets::NumberField(context, AddField(context, parent, "Max Batch Capacity"), capacity);
		}
	}

} // namespace Shalltear::VisualEffectsEditor::EffectAssetSettings
