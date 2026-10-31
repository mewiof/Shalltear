#include "Shalltear/Editor/EditorTheme.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/UI/Docking.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include <charconv>

namespace Shalltear::EditorTheme {

	namespace {

#define SLT_LABEL_RULE(name, cls)                                                                 \
	"." cls " { background: linear-gradient(var(--" cls "), transparent); }\n"                    \
	".slt-tree-row." cls ":hover { background: linear-gradient(var(--" cls "), transparent); }\n" \
	".slt-tree-row." cls ":active { background: linear-gradient(var(--" cls "), var(--slt-row-active)); }\n"

		constexpr const char* EDITOR_SKIN = R"css(
* { text-outline-width: var(--ed-text-stroke-width); text-outline-color: var(--ed-text-stroke); }

.ed-chrome-bar { flex-direction: row; align-items: center; background-color: var(--ed-chrome-bar); }
.ed-status-bar { flex-direction: row; align-items: center; padding: 3 8 3 8; background-color: var(--ed-chrome-status); flex-shrink: 0; }
.ed-status-notice { padding: 0 4 0 4; border-radius: var(--slt-radius-sm); cursor: hand; }
.ed-status-notice:hover { background-color: var(--ed-window-button-hover); }
.ed-status-notice:active { background-color: var(--ed-window-button-active); }
.ed-panel-header { flex-direction: row; align-items: center; width: 100%; flex-shrink: 0; min-height: 24; padding: 0 3 0 3; z-index: 1; box-shadow: inset 0 -1 0 var(--slt-edge-dark); }
.ed-panel-header-drop { position: absolute; left: 0; right: 0; top: 100%; height: 3; background: linear-gradient(var(--ed-header-shadow), transparent, to bottom); pointer-events: none; }
.ed-dock-gutter { background-color: var(--ed-chrome-gutter); }
.ed-layout-bar { flex-direction: row; align-items: center; flex-shrink: 0; padding: 4 2 4 2; background-color: var(--ed-chrome-bar); box-shadow: inset 0 -1 0 var(--slt-edge-light); }

.ed-title-bar { flex-direction: row; align-items: center; background-color: var(--ed-chrome-bar); box-shadow: inset 0 -1 0 var(--slt-edge-light); flex-shrink: 0; }
.ed-title-zone { flex-direction: row; align-items: center; width: 0; flex-grow: 1; overflow: hidden; }
.ed-title-menu { background-color: transparent; width: auto; flex-shrink: 0; }
.ed-tool-button { background-color: transparent; box-shadow: none; flex-shrink: 0; }
.ed-tool-button:hover { background-color: var(--ed-window-button-hover); }
.ed-tool-button:active { background-color: var(--ed-window-button-active); }
.ed-tool-button.ed-tool-button-on { background-color: var(--ed-tool-button-on); }
.ed-tool-button.ed-tool-button-on:hover { background-color: var(--ed-tool-button-on-hover); }
.ed-tool-button.ed-tool-button-on:active { background-color: var(--ed-tool-button-on-active); }
.ed-switch-group { padding: 1 1 1 1; border-radius: var(--slt-radius); background-color: var(--ed-switch-group); }
.ed-path-crumb { padding: 1 4 1 4; border-radius: var(--slt-radius); cursor: hand; }
.ed-path-crumb:hover { background-color: var(--ed-window-button-hover); }
.ed-path-crumb:active { background-color: var(--ed-window-button-active); }
.slt-textfield.ed-field-refused { box-shadow: inset 0 0 0 1 var(--ed-status-error); }
.slt-textfield.ed-field-refused:focus { box-shadow: inset 0 0 0 1 var(--ed-status-error); }
.ed-caption-close:hover { background-color: var(--ed-window-close-hover); }
.ed-caption-close:active { background-color: var(--ed-window-close-active); }
.ed-icon-button { padding: 4 4 4 4; }
.ed-caption-strip { padding: 0 var(--ed-caption-controls-width) 0 0; min-height: var(--ed-caption-strip-height); }

.ed-popup-card { flex-direction: column; padding: 14; background-color: var(--slt-surface); border-radius: var(--slt-radius-lg); box-shadow: inset 0 0 0 1 var(--ed-card-edge), 0 8 24 var(--ed-card-shadow); }
.ed-popup-card-compact { flex-direction: column; padding: 8; background-color: var(--slt-surface); border-radius: var(--slt-radius-lg); box-shadow: inset 0 0 0 1 var(--ed-card-edge), 0 8 24 var(--ed-card-shadow); }
.ed-about-card { flex-direction: column; padding: 16; background-color: var(--ed-about-card); border-radius: var(--slt-radius-lg); border-width: 1; border-color: var(--ed-about-card-border); }

.ed-hub-body { flex-direction: column; align-items: stretch; background-color: var(--slt-surface); }
.ed-hub-columns { flex-direction: row; align-items: center; flex-shrink: 0; padding: 2 6 6 6; box-shadow: inset 0 -1 0 var(--slt-edge-dark); }
.ed-hub-row { padding: 5 6 5 6; }

.ed-section-card { background-color: var(--ed-section-fill); box-shadow: inset 0 -1 0 var(--ed-section-shadow); }
.ed-section-header { background-color: var(--ed-header-fill); }
.ed-stage-breadcrumb { background-color: var(--ed-header-fill); }
.ed-graph { background-color: var(--ed-graph-background); }
.ed-graph-board { padding: 6 8 6 8; border-radius: var(--slt-radius-sm); background-color: var(--slt-surface); box-shadow: inset 0 0 0 1 var(--ed-card-edge); }
.ed-curve-editor { flex-direction: column; padding: 6 6 6 6; border-radius: var(--slt-radius-sm); background-color: var(--slt-surface); box-shadow: inset 0 0 0 1 var(--ed-card-edge); }
.ed-curve-editor-plot { background-color: var(--ed-graph-background); }
.ed-viewport-placeholder { background-color: var(--ed-viewport-placeholder); }
.ed-scene-follow { position: absolute; left: 8; top: 8; padding: 3 8 3 8; border-radius: var(--slt-radius-sm); background-color: var(--slt-surface); box-shadow: inset 0 0 0 1 var(--ed-card-edge); }
.ed-scene-orientation { position: absolute; left: 10; bottom: 10; }
.ed-scene-orientation-cube { cursor: hand; }
.ed-scene-overlay { position: absolute; right: 10; bottom: 10; flex-direction: column; align-items: stretch; }
.ed-scene-overlay-card { flex-direction: column; padding: 6 8 6 8; margin-top: 6; border-radius: var(--slt-radius-sm); background-color: var(--slt-surface); box-shadow: inset 0 0 0 1 var(--ed-card-edge); }
.ed-clickable { cursor: hand; }
.ed-clickable:active { background-color: var(--slt-row-active); }
.ed-drag-handle { cursor: hand; }
.ed-drag-handle:hover { background-color: var(--slt-row-hover); border-radius: var(--slt-radius-sm); }
.ed-drag-handle:active { background-color: var(--slt-row-active); border-radius: var(--slt-radius-sm); }
.ed-ui-debug-band { box-shadow: inset 0 0 0 1 var(--slt-edge-light); border-radius: var(--slt-radius-xs); }
.ed-ui-debug-value { flex-grow: 1.4; min-width: 24; }
.ed-ui-debug-link { cursor: hand; }
.ed-ui-debug-link:hover { background-color: var(--slt-row-hover); }
.ed-ui-debug-link:active { background-color: var(--slt-row-active); }

.ed-insert-strip:drop { background-color: var(--slt-accent); }

.ed-row-reveal { opacity: 0; }
.ed-row-reveal:hover { opacity: 1; }
.ed-row-reveal:active { opacity: 1; }

.ed-row-off { opacity: 0.5; }

.ed-row-ping { box-shadow: inset 0 0 0 2 var(--slt-accent); }

.slt-tree-label { text-overflow: ellipsis; }
.dock-tab-label { text-overflow: ellipsis; }
.ed-field-label { text-overflow: ellipsis; overflow: hidden; min-width: 48; }
.ed-row-text { text-overflow: ellipsis; }
.ed-log-row:hover { background-color: var(--slt-row-hover); }
.ed-log-row:active { background-color: var(--slt-row-active); }
.ed-override-mark { background-color: var(--ed-override-mark); border-radius: var(--slt-radius-xs); }

.ed-object-field { flex-direction: row; align-items: center; flex-grow: 1; flex-shrink: 1; min-width: 40; min-height: 20; overflow: hidden; padding: 0 0 0 6; background-color: var(--slt-field); border-radius: var(--slt-radius); box-shadow: inset 0 1 0 var(--slt-field-top); cursor: hand; }
.ed-object-field:hover { box-shadow: inset 0 1 0 var(--slt-field-top), inset 0 0 0 1 var(--slt-edge-bright); }
.ed-object-field:active { box-shadow: inset 0 1 0 var(--slt-field-top), inset 0 0 0 1 var(--slt-edge-active); }
.ed-object-field:drop { background-color: var(--slt-drop); }
.ed-object-field-button { align-self: stretch; flex-shrink: 0; padding: 0 5 0 5; border-radius: 0; box-shadow: none; }

.ed-disabled { opacity: 0.45; }

.ed-asset-cell { flex-direction: column; align-items: center; justify-content: flex-start; overflow: hidden; flex-shrink: 0; }
.ed-asset-cell-well { border-radius: var(--slt-radius-sm); }
.ed-asset-cell-well:hover { background-color: var(--slt-row-hover); }
.ed-asset-cell-well:active { background-color: var(--slt-row-active); }
.ed-asset-cell-label { text-overflow: ellipsis; max-width: 100%; text-align: center; padding: 0 3 0 3; border-radius: var(--slt-radius-sm); }
.ed-asset-cell-picture { filter: drop-shadow(0 4 12 var(--ed-browser-tile-shadow)); }
.ed-asset-fold-button { background-color: var(--ed-browser-fold-button); box-shadow: inset 0 0 0 1 var(--slt-edge-bright); }
.ed-asset-fold-button:hover { background-color: var(--ed-browser-fold-button-hover); }
.ed-asset-fold-button:active { background-color: var(--ed-browser-fold-button-active); }
.ed-asset-cell-variant { z-index: 10; background-color: var(--slt-surface); border-radius: 2; }
.ed-asset-grid-row { flex-direction: row; align-items: flex-start; width: 100%; flex-shrink: 0; }
.ed-panel-footer { flex-direction: row; align-items: center; width: 100%; flex-shrink: 0; min-height: 22; padding: 0 3 0 3; box-shadow: inset 0 1 0 var(--slt-edge-dark); }
.ed-browser-slider { flex-direction: row; align-items: center; flex-grow: 0; flex-shrink: 0; }
)css" SLT_THEME_LABEL_CLASSES(SLT_LABEL_RULE)
			R"css(
.slt-tree-row.slt-selected { background-color: var(--slt-selected); }
.slt-tree-row.slt-selected:hover { background-color: var(--slt-selected); }
.slt-tree-row.slt-selected:active { background-color: var(--slt-selected-active); }
.slt-tree-row.slt-selected:drop { background-color: var(--slt-drop); }
.slt-selected.ed-asset-cell { background-color: transparent; box-shadow: none; }
.slt-selected.ed-asset-cell:hover { background-color: transparent; }
.slt-selected.ed-asset-cell:active { background-color: transparent; }
.slt-selected.ed-asset-cell:drop { background-color: var(--slt-drop); }
.ed-play-tint { position: absolute; left: 0; top: 0; width: 100%; height: 100%; z-index: 20000; pointer-events: none; background-color: var(--ed-play-tint); }
)css";

		struct RegisteredTheme {
			StringHash name;
			ThemeDesc desc;
		};

		struct ThemeState {
			Vector<RegisteredTheme> themes{GetMemoryResource(MemTag::Editor)};
			StringHash active = DarkTheme;

			Color colors[COLOR_TOKEN_COUNT]{};
			f32 metrics[METRIC_TOKEN_COUNT]{};
			ThemeDesc activeDesc;
			Vector<UI::StyleVarDesc> varDescs{GetMemoryResource(MemTag::Editor)};

			bool built = false;
		};

		ThemeState& State() {
			static ThemeState state;
			return state;
		}

		constexpr const char* COLOR_VAR_NAMES[] = {
#define SLT_THEME_X(name, cssName, value) cssName,
			SLT_THEME_COLOR_TOKENS(SLT_THEME_X)
#undef SLT_THEME_X
		};

		constexpr const char* COLOR_DEFAULTS[] = {
#define SLT_THEME_X(name, cssName, value) value,
			SLT_THEME_COLOR_TOKENS(SLT_THEME_X)
#undef SLT_THEME_X
		};

		constexpr const char* METRIC_VAR_NAMES[] = {
#define SLT_THEME_X(name, cssName, value) cssName,
			SLT_THEME_METRIC_TOKENS(SLT_THEME_X)
#undef SLT_THEME_X
		};

		constexpr const char* METRIC_DEFAULTS[] = {
#define SLT_THEME_X(name, cssName, value) value,
			SLT_THEME_METRIC_TOKENS(SLT_THEME_X)
#undef SLT_THEME_X
		};

		static_assert(std::size(COLOR_VAR_NAMES) == COLOR_TOKEN_COUNT);
		static_assert(std::size(METRIC_VAR_NAMES) == METRIC_TOKEN_COUNT);

		f32 ParseMetric(std::string_view text) {
			f32 value = 0.0f;
			std::from_chars(text.data(), text.data() + text.size(), value);
			return value;
		}

		void RebuildCaches(ThemeState& state) {
			state.varDescs.clear();
			state.varDescs.reserve(COLOR_TOKEN_COUNT + METRIC_TOKEN_COUNT);

			for (u32 i = 0; i < COLOR_TOKEN_COUNT; ++i) {
				const str& value = state.activeDesc.colors[i];
				Color parsed = Color::Magenta();
				if (!Color::TryParseHex(value, parsed)) {
					SLT_LOG_WARN("Editor", "theme: '{}' isn't a color ('{}')", COLOR_VAR_NAMES[i], value);
				}
				state.colors[i] = parsed;
				state.varDescs.push_back({COLOR_VAR_NAMES[i], value.c_str()});
			}
			for (u32 i = 0; i < METRIC_TOKEN_COUNT; ++i) {
				const str& value = state.activeDesc.metrics[i];
				state.metrics[i] = ParseMetric(value);
				state.varDescs.push_back({METRIC_VAR_NAMES[i], value.c_str()});
			}
			state.built = true;
		}

		RegisteredTheme* Find(ThemeState& state, StringHash name) {
			for (RegisteredTheme& theme : state.themes) {
				if (theme.name == name) {
					return &theme;
				}
			}
			return nullptr;
		}

		ThemeState& Ready() {
			ThemeState& state = State();
			if (!state.built) {
				state.activeDesc = MakeDefaultDesc();
				state.themes.push_back({DarkTheme, state.activeDesc});
				state.active = DarkTheme;
				RebuildCaches(state);
			}
			return state;
		}

	} // anonymous namespace

	Color GetColor(ColorToken token) {
		return Ready().colors[static_cast<u32>(token)];
	}

	Color32 GetColor32(ColorToken token) {
		return GetColor(token).ToColor32();
	}

	f32 GetMetric(MetricToken token) {
		return Ready().metrics[static_cast<u32>(token)];
	}

	std::string_view GetVarName(ColorToken token) {
		return COLOR_VAR_NAMES[static_cast<u32>(token)];
	}

	std::string_view GetVarName(MetricToken token) {
		return METRIC_VAR_NAMES[static_cast<u32>(token)];
	}

	ThemeDesc MakeDefaultDesc() {
		ThemeDesc desc;
		for (u32 i = 0; i < COLOR_TOKEN_COUNT; ++i) {
			desc.colors[i] = COLOR_DEFAULTS[i];
		}
		for (u32 i = 0; i < METRIC_TOKEN_COUNT; ++i) {
			desc.metrics[i] = METRIC_DEFAULTS[i];
		}
		return desc;
	}

	void Register(StringHash name, const ThemeDesc& desc) {
		ThemeState& state = Ready();
		if (RegisteredTheme* existing = Find(state, name)) {
			existing->desc = desc;
		} else {
			state.themes.push_back({name, desc});
		}
		if (state.active == name) {
			state.activeDesc = desc;
			RebuildCaches(state);
			UISystem::ForEachContext([](Handle<Window>, UI::Context& context) { ApplyToContext(context); });
		}
	}

	bool SetActive(StringHash name) {
		ThemeState& state = Ready();
		RegisteredTheme* theme = Find(state, name);
		if (!theme) {
			SLT_LOG_WARN("Editor", "theme: no theme registered as hash {:x}", name.GetValue());
			return false;
		}
		state.active = name;
		state.activeDesc = theme->desc;
		RebuildCaches(state);
		UISystem::ForEachContext([](Handle<Window>, UI::Context& context) { ApplyToContext(context); });
		return true;
	}

	StringHash GetActive() {
		return Ready().active;
	}

	void ForEachTheme(const std::function<void(StringHash)>& visit) {
		if (!visit) {
			return;
		}
		ThemeState& state = Ready();
		for (const RegisteredTheme& theme : state.themes) {
			visit(theme.name);
		}
	}

	void Shutdown() {
		ThemeState& state = State();
		state.themes.clear();
		state.themes.shrink_to_fit();
		state.varDescs.clear();
		state.varDescs.shrink_to_fit();
		state.activeDesc = ThemeDesc{};
		state.active = DarkTheme;
		state.built = false;
	}

	void ApplyToContext(UI::Context& context) {
		ThemeState& state = Ready();
		context.SetStyleVars(state.varDescs);
		UI::Widgets::EnsureSkin(context);
		UI::DockManager::EnsureSkin(context);
		context.LoadStyleSheetOnce(StringHash("ed-skin"), EDITOR_SKIN);
	}

	std::string_view GetSheetSource() {
		return EDITOR_SKIN;
	}

	namespace Class {

		namespace {
			// must match the swatch ordinals
			enum class LabelIndex : u8 {
				None = 0,
#define SLT_THEME_X(name, cls) name,
				SLT_THEME_LABEL_CLASSES(SLT_THEME_X)
#undef SLT_THEME_X
					Count
			};
		} // anonymous namespace

		u32 GetLabelClassCount() {
			return static_cast<u32>(LabelIndex::Count) - 1;
		}

		StringHash ForLabel(u8 labelColor) {
			switch (labelColor) {
#define SLT_THEME_X(name, cls)              \
	case static_cast<u8>(LabelIndex::name): \
		return StringHash(cls);
				SLT_THEME_LABEL_CLASSES(SLT_THEME_X)
#undef SLT_THEME_X
			default:
				return StringHash();
			}
		}

	} // namespace Class

} // namespace Shalltear::EditorTheme
