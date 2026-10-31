#include "Shalltear/Core/UI/Style.h"

namespace Shalltear::UI {

	std::span<const StyleVarDesc> GetDefaultStyleVars() {
		static const StyleVarDesc vars[] = {
			{"--slt-surface", "#2b2c2f"},
			{"--slt-well", "#141516"},
			{"--slt-well-hover", "#1c1c1f"},
			{"--slt-well-active", "#26262a"},
			{"--slt-field", "#1a1b1c"},
			{"--slt-field-top", "#111212"},
			{"--slt-control", "#3e3e41"},
			{"--slt-control-hover", "#47474a"},
			{"--slt-control-active", "#333336"},
			{"--slt-button-default", "#0078d7"},
			{"--slt-button-default-hover", "#1a8ae9"},
			{"--slt-button-default-active", "#0063b1"},
			{"--slt-row-hover", "#313236"},
			{"--slt-row-active", "#3a3b40"},
			{"--slt-tab-hover", "#232327"},
			{"--slt-tab-pressed", "#1b1b1f"},
			{"--slt-slider-knob", "#b0b0b8"},
			{"--slt-slider-knob-hover", "#d2d2da"},
			{"--slt-slider-knob-active", "#8f8f98"},
			{"--slt-scroll-thumb", "#6b6b7a"},
			{"--slt-scroll-thumb-hover", "#8a8a99"},
			{"--slt-scroll-thumb-active", "#a3a3b2"},
			{"--slt-menu-bar-hover", "#ffffff14"},
			{"--slt-menu-bar-active", "#ffffff24"},
			{"--slt-splitter", "#000000"},

			{"--slt-accent", "#0078d7"},
			{"--slt-accent-hover", "#135c9e"},
			{"--slt-accent-active", "#1b6ab3"},
			{"--slt-accent-focus", "#0078d7c0"},
			{"--slt-selected", "#274b6d"},
			{"--slt-selected-active", "#305a83"},
			{"--slt-drop", "#12436f"},

			{"--slt-edge-light", "#ffffff08"},
			{"--slt-edge-light-strong", "#ffffff10"},
			{"--slt-edge-bright", "#ffffff20"},
			{"--slt-edge-active", "#ffffff40"},
			{"--slt-edge-dark", "#00000050"},
			{"--slt-shadow", "#00000080"},
			{"--slt-popup-scrim", "#00000073"},
			{"--slt-tooltip", "#3a3a40"},

			{"--slt-text", "#c3c3cb"},
			{"--slt-text-dim", "#9e9ea8"},
			{"--slt-button-default-text", "#ffffff"},
			{"--slt-tab-text", "#c3c3cb"},
			{"--slt-menu-text", "#9e9ea8"},

			{"--slt-zone-fill", "#0078d64d"},
			{"--slt-zone-border", "#409ef2e6"},
			{"--slt-zone-trigger-active", "#4db3ffe6"},
			{"--slt-zone-trigger", "#26262bcc"},
			{"--slt-zone-trigger-border", "#80808080"},
			{"--slt-tab-ghost", "#333340f2"},
			{"--slt-ghost-border", "#666673ff"},
			{"--slt-payload-ghost", "#292933f2"},
			{"--slt-payload-ghost-text", "#f2f2f2"},

			{"--slt-radius-xs", "2"},
			{"--slt-radius-sm", "3"},
			{"--slt-radius", "4"},
			// tab's arc must match its bar's
			{"--slt-radius-tab", "4"},
			{"--slt-radius-lg", "4"},
			{"--slt-radius-pill", "7"},
			{"--slt-radius-row", "0"},

			{"--slt-tab-font-size", "12"},
			{"--slt-tab-icon-gap", "5"},
			{"--slt-number-gap", "6"},
			{"--slt-number-pair-min", "48"},
			{"--slt-tree-gutter", "14"},
			{"--slt-tree-indent", "14"},
			{"--slt-tree-guide", "#ffffff14"},
			{"--slt-tooltip-offset", "18"},
			{"--slt-menu-check-gutter", "16"},
		};
		return vars;
	}

} // namespace Shalltear::UI
