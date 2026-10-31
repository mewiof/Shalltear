#pragma once

#include "Shalltear/Core/Math/Color.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"
#include "Shalltear/Core/UI/Context.h"
#include "Shalltear/Core/UI/Style.h"

#include "Shalltear/Editor/EditorAPI.h"

#include <functional>
#include <string_view>

// semantic names, plain CSS values (no var())
#define SLT_THEME_COLOR_TOKENS(X)                                                \
	X(Surface, "--slt-surface", "#2b2c2f")                                       \
	X(Well, "--slt-well", "#141516")                                             \
	X(WellHover, "--slt-well-hover", "#1c1c1f")                                  \
	X(WellActive, "--slt-well-active", "#26262a")                                \
	X(Field, "--slt-field", "#1a1b1c")                                           \
	X(FieldTop, "--slt-field-top", "#111212")                                    \
	X(Control, "--slt-control", "#3e3e41")                                       \
	X(ControlHover, "--slt-control-hover", "#47474a")                            \
	X(ControlActive, "--slt-control-active", "#333336")                          \
	X(ButtonDefault, "--slt-button-default", "#0078d7")                          \
	X(ButtonDefaultHover, "--slt-button-default-hover", "#1a8ae9")               \
	X(ButtonDefaultActive, "--slt-button-default-active", "#0063b1")             \
	X(RowHover, "--slt-row-hover", "#313236")                                    \
	X(RowActive, "--slt-row-active", "#3a3b40")                                  \
	X(TabHover, "--slt-tab-hover", "#232327")                                    \
	X(TabPressed, "--slt-tab-pressed", "#1b1b1f")                                \
	X(SliderKnob, "--slt-slider-knob", "#b0b0b8")                                \
	X(SliderKnobHover, "--slt-slider-knob-hover", "#d2d2da")                     \
	X(SliderKnobActive, "--slt-slider-knob-active", "#8f8f98")                   \
	X(ScrollThumb, "--slt-scroll-thumb", "#6b6b7a")                              \
	X(ScrollThumbHover, "--slt-scroll-thumb-hover", "#8a8a99")                   \
	X(ScrollThumbActive, "--slt-scroll-thumb-active", "#a3a3b2")                 \
	X(MenuBarHover, "--slt-menu-bar-hover", "#ffffff14")                         \
	X(MenuBarActive, "--slt-menu-bar-active", "#ffffff24")                       \
	X(Splitter, "--slt-splitter", "#000000")                                     \
	X(Tooltip, "--slt-tooltip", "#3a3a40")                                       \
	X(Accent, "--slt-accent", "#0078d7")                                         \
	X(AccentHover, "--slt-accent-hover", "#135c9e")                              \
	X(AccentActive, "--slt-accent-active", "#1b6ab3")                            \
	X(AccentFocus, "--slt-accent-focus", "#0078d7c0")                            \
	X(Selected, "--slt-selected", "#274b6d")                                     \
	X(SelectedActive, "--slt-selected-active", "#305a83")                        \
	X(Drop, "--slt-drop", "#12436f")                                             \
	X(EdgeLight, "--slt-edge-light", "#ffffff08")                                \
	X(EdgeLightStrong, "--slt-edge-light-strong", "#ffffff10")                   \
	X(EdgeBright, "--slt-edge-bright", "#ffffff20")                              \
	X(EdgeActive, "--slt-edge-active", "#ffffff40")                              \
	X(EdgeDark, "--slt-edge-dark", "#00000050")                                  \
	X(Shadow, "--slt-shadow", "#00000080")                                       \
	X(TreeGuide, "--slt-tree-guide", "#ffffff14")                                \
	X(WidgetText, "--slt-text", "#c3c3cb")                                       \
	X(WidgetTextDim, "--slt-text-dim", "#9e9ea8")                                \
	X(TabText, "--slt-tab-text", "#c3c3cb")                                      \
	X(MenuText, "--slt-menu-text", "#9e9ea8")                                    \
	X(ButtonDefaultText, "--slt-button-default-text", "#ffffff")                 \
	X(ZoneFill, "--slt-zone-fill", "#0078d64d")                                  \
	X(ZoneBorder, "--slt-zone-border", "#409ef2e6")                              \
	X(ZoneTriggerActive, "--slt-zone-trigger-active", "#4db3ffe6")               \
	X(ZoneTrigger, "--slt-zone-trigger", "#26262bcc")                            \
	X(ZoneTriggerBorder, "--slt-zone-trigger-border", "#80808080")               \
	X(TabGhost, "--slt-tab-ghost", "#333340f2")                                  \
	X(GhostBorder, "--slt-ghost-border", "#666673ff")                            \
	X(PayloadGhost, "--slt-payload-ghost", "#292933f2")                          \
	X(PayloadGhostText, "--slt-payload-ghost-text", "#f2f2f2")                   \
	X(ChromeBar, "--ed-chrome-bar", "#000000")                                   \
	X(ChromeStatus, "--ed-chrome-status", "#000000")                             \
	X(ChromeGutter, "--ed-chrome-gutter", "#000000")                             \
	X(WindowButtonHover, "--ed-window-button-hover", "#ffffff14")                \
	X(WindowButtonActive, "--ed-window-button-active", "#ffffff1f")              \
	X(WindowCloseHover, "--ed-window-close-hover", "#c42b1c")                    \
	X(WindowCloseActive, "--ed-window-close-active", "#d9402f")                  \
	X(ToolButtonOn, "--ed-tool-button-on", "#ffffff26")                          \
	X(ToolButtonOnHover, "--ed-tool-button-on-hover", "#ffffff33")               \
	X(ToolButtonOnActive, "--ed-tool-button-on-active", "#ffffff40")             \
	X(SwitchGroup, "--ed-switch-group", "#00000033")                             \
	X(CardEdge, "--ed-card-edge", "#ffffff0f")                                   \
	X(CardShadow, "--ed-card-shadow", "#00000099")                               \
	X(SectionFill, "--ed-section-fill", "#ffffff06")                             \
	X(SectionShadow, "--ed-section-shadow", "#0000004c")                         \
	X(HeaderFill, "--ed-header-fill", "#ffffff0e")                               \
	X(HeaderShadow, "--ed-header-shadow", "#00000038")                           \
	X(RowStripe, "--ed-row-stripe", "#00000020")                                 \
	X(AboutCard, "--ed-about-card", "#24242a")                                   \
	X(AboutCardBorder, "--ed-about-card-border", "#525261")                      \
	X(TextStroke, "--ed-text-stroke", "#0000004d")                               \
	X(TextStrong, "--ed-text-strong", "#f0f0f5")                                 \
	X(Text, "--ed-text", "#c3c3cb")                                              \
	X(TextDim, "--ed-text-dim", "#9e9ea8")                                       \
	X(TextFaint, "--ed-text-faint", "#737380")                                   \
	X(StatusOk, "--ed-status-ok", "#59bf66")                                     \
	X(StatusWarn, "--ed-status-warn", "#f2c740")                                 \
	X(StatusError, "--ed-status-error", "#f2594c")                               \
	X(StatusErrorSoft, "--ed-status-error-soft", "#f26b61")                      \
	X(StatusFatal, "--ed-status-fatal", "#ff3380")                               \
	X(LogDebug, "--ed-log-debug", "#8c8c9e")                                     \
	X(LogInfo, "--ed-log-info", "#c7c7d1")                                       \
	X(PlayTint, "--ed-play-tint", "#4a86ff1a")                                   \
	X(SyntaxPunct, "--ed-syntax-punct", "#7b7b88")                               \
	X(SyntaxTag, "--ed-syntax-tag", "#7fb2e5")                                   \
	X(SyntaxAttr, "--ed-syntax-attr", "#c6a97a")                                 \
	X(SyntaxValue, "--ed-syntax-value", "#93c78e")                               \
	X(SyntaxComment, "--ed-syntax-comment", "#6f7a86")                           \
	X(AssetFolder, "--ed-asset-folder", "#ccb270")                               \
	X(AssetShader, "--ed-asset-shader", "#b88cd9")                               \
	X(AssetTexture, "--ed-asset-texture", "#8cc78c")                             \
	X(AssetMesh, "--ed-asset-mesh", "#d9a66b")                                   \
	X(AssetMaterial, "--ed-asset-material", "#73bfbf")                           \
	X(AssetFont, "--ed-asset-font", "#a6a6cc")                                   \
	X(AssetScene, "--ed-asset-scene", "#6ba8e6")                                 \
	X(AssetPrefab, "--ed-asset-prefab", "#80cce6")                               \
	X(AssetUI, "--ed-asset-ui", "#d98cb0")                                       \
	X(AssetProfile, "--ed-asset-profile", "#e0917f")                             \
	X(AssetAnimation, "--ed-asset-animation", "#9b82e8")                         \
	X(AssetAudio, "--ed-asset-audio", "#e8a24a")                                 \
	X(BrowserFoldBand, "--ed-browser-fold-band", "#36373c")                      \
	X(BrowserFoldButton, "--ed-browser-fold-button", "#2c2d31")                  \
	X(BrowserFoldButtonHover, "--ed-browser-fold-button-hover", "#3e3f44")       \
	X(BrowserFoldButtonActive, "--ed-browser-fold-button-active", "#4a4b51")     \
	X(BrowserTileShadow, "--ed-browser-tile-shadow", "#00000080")                \
	X(OverrideMark, "--ed-override-mark", "#3f8cff")                             \
	X(LabelRed, "--ed-label-red", "#c8464666")                                   \
	X(LabelOrange, "--ed-label-orange", "#cc7a3866")                             \
	X(LabelYellow, "--ed-label-yellow", "#ccb04066")                             \
	X(LabelGreen, "--ed-label-green", "#52a35266")                               \
	X(LabelTeal, "--ed-label-teal", "#3aa39c66")                                 \
	X(LabelBlue, "--ed-label-blue", "#4a7fd966")                                 \
	X(LabelPurple, "--ed-label-purple", "#8257c266")                             \
	X(LabelPink, "--ed-label-pink", "#c2559666")                                 \
	X(GraphBackground, "--ed-graph-background", "#121216")                       \
	X(GraphBudget, "--ed-graph-budget", "#ffffff40")                             \
	X(GraphGrid, "--ed-graph-grid", "#24242b")                                   \
	X(GraphGridMajor, "--ed-graph-grid-major", "#2e2f38")                        \
	X(GraphNode, "--ed-graph-node", "#3a3b45")                                   \
	X(GraphNodeMuted, "--ed-graph-node-muted", "#33343d")                        \
	X(GraphNodeActive, "--ed-graph-node-active", "#3fbf6f")                      \
	X(GraphNodeProgress, "--ed-graph-node-progress", "#2f7f52")                  \
	X(GraphPortBoolean, "--ed-graph-port-boolean", "#9b84e0")                    \
	X(GraphPortInteger, "--ed-graph-port-integer", "#4fa3b4")                    \
	X(GraphPortFloat, "--ed-graph-port-float", "#7fd3e0")                        \
	X(GraphPortVector2, "--ed-graph-port-vector2", "#8fd88a")                    \
	X(GraphPortVector3, "--ed-graph-port-vector3", "#e0e08a")                    \
	X(GraphPortVector4, "--ed-graph-port-vector4", "#e7b3dd")                    \
	X(GraphPortMatrix, "--ed-graph-port-matrix", "#8ab3d6")                      \
	X(GraphPortTexture, "--ed-graph-port-texture", "#e58787")                    \
	X(GraphPortOther, "--ed-graph-port-other", "#a8a8b0")                        \
	X(GraphSearchMatch, "--ed-graph-search-match", "#ffcd62")                    \
	X(GraphSearchCompletion, "--ed-graph-search-completion", "#828282")          \
	X(GraphMarquee, "--ed-graph-marquee", "#ffaa2818")                           \
	X(GraphMarqueeEdge, "--ed-graph-marquee-edge", "#ffaa2899")                  \
	X(GraphGroup, "--ed-graph-group", "#ffffff0d")                               \
	X(GraphGroupEdge, "--ed-graph-group-edge", "#ffffff2e")                      \
	X(GraphNoteLight, "--ed-graph-note-light", "#e6dca0")                        \
	X(GraphNoteLightText, "--ed-graph-note-light-text", "#26261f")               \
	X(GraphNoteDark, "--ed-graph-note-dark", "#2b2c33")                          \
	X(GraphNoteDarkText, "--ed-graph-note-dark-text", "#d6d6dc")                 \
	X(GraphInlineField, "--ed-graph-inline-field", "#2a2b33")                    \
	X(GraphInlineFieldEdge, "--ed-graph-inline-field-edge", "#4a4b56")           \
	X(GraphPreviewWell, "--ed-graph-preview-well", "#222329")                    \
	X(CurveLine, "--ed-curve-line", "#62c95a")                                   \
	X(GraphBlock, "--ed-graph-block", "#464753")                                 \
	X(GraphFlow, "--ed-graph-flow", "#c9cad2")                                   \
	X(GraphOutline, "--ed-graph-outline", "#ffffff40")                           \
	X(SheetKey, "--ed-sheet-key", "#c8c8d4")                                     \
	X(SheetRow, "--ed-sheet-row", "#ffffff10")                                   \
	X(ViewportClear, "--ed-viewport-clear", "#171a1f")                           \
	X(ViewportPlaceholder, "--ed-viewport-placeholder", "#0f0f13")               \
	X(GridLine, "--ed-grid-line", "#3a3a42")                                     \
	X(GridMajor, "--ed-grid-major", "#4e4e58")                                   \
	X(AxisX, "--ed-axis-x", "#c84646")                                           \
	X(AxisY, "--ed-axis-y", "#5ac85a")                                           \
	X(AxisZ, "--ed-axis-z", "#4678dc")                                           \
	X(SelectionWire, "--ed-selection-wire", "#ffaa28")                           \
	X(SelectionOutline, "--ed-selection-outline", "#ff6600")                     \
	X(SelectionChildOutline, "--ed-selection-child-outline", "#5e779b")          \
	X(SceneWireframe, "--ed-scene-wireframe", "#b4bcc8")                         \
	X(SceneWireframeOverlay, "--ed-scene-wireframe-overlay", "#141619")          \
	X(GizmoHighlight, "--ed-gizmo-highlight", "#ffeb78")                         \
	X(GizmoHighlightActive, "--ed-gizmo-highlight-active", "#fff6c2")            \
	X(GizmoScreen, "--ed-gizmo-screen", "#d2d2dc")                               \
	X(GizmoCameraWire, "--ed-gizmo-camera-wire", "#8ac6f0")                      \
	X(GizmoLightWire, "--ed-gizmo-light-wire", "#f0d68a")                        \
	X(GizmoColliderWire, "--ed-gizmo-collider-wire", "#6ee06e")                  \
	X(GizmoVolumeWire, "--ed-gizmo-volume-wire", "#5ad2c8")                      \
	X(GizmoAudioRange, "--ed-gizmo-audio-range", "#a5b4ff")                      \
	X(GizmoEffectBounds, "--ed-gizmo-effect-bounds", "#c8c8d2")                  \
	X(GizmoEffectRecordedBounds, "--ed-gizmo-effect-recorded-bounds", "#e65050") \
	X(GizmoEffectShape, "--ed-gizmo-effect-shape", "#ececf0")                    \
	X(AudioWaveform, "--ed-audio-waveform", "#f0a848")                           \
	X(AudioCurveVolume, "--ed-audio-curve-volume", "#e64d33")                    \
	X(AudioCurveSpatialBlend, "--ed-audio-curve-spatial-blend", "#40b333")       \
	X(AudioCurveSpread, "--ed-audio-curve-spread", "#408cf2")                    \
	X(AudioCurveLowPass, "--ed-audio-curve-low-pass", "#cc40e6")                 \
	X(AudioCurveReverbZoneMix, "--ed-audio-curve-reverb-zone-mix", "#b3b333")    \
	X(AudioListenerDistance, "--ed-audio-listener-distance", "#ff00004d")        \
	X(AudioMeterWell, "--ed-audio-meter-well", "#121216")                        \
	X(AudioMeterLow, "--ed-audio-meter-low", "#3fbf6f")                          \
	X(AudioMeterHigh, "--ed-audio-meter-high", "#e6c247")                        \
	X(AudioMeterClip, "--ed-audio-meter-clip", "#f2594c")                        \
	X(AudioMeterPeak, "--ed-audio-meter-peak", "#e0e0e6")                        \
	X(AudioFaderTrack, "--ed-audio-fader-track", "#4a4b57")                      \
	X(AudioFaderHandle, "--ed-audio-fader-handle", "#6b6c78")                    \
	X(AudioSolo, "--ed-audio-solo", "#e6c247")                                   \
	X(AudioMute, "--ed-audio-mute", "#3f8fd6")                                   \
	X(AudioBypass, "--ed-audio-bypass", "#e0822d")                               \
	X(AudioToggleLitText, "--ed-audio-toggle-lit-text", "#1b1c1e")               \
	X(AudioEffectSlot, "--ed-audio-effect-slot", "#67a000")                      \
	X(AudioSendSlot, "--ed-audio-send-slot", "#0093aa")                          \
	X(AudioSlotRest, "--ed-audio-slot-rest", "#00000066")                        \
	X(AudioEditingLive, "--ed-audio-editing-live", "#d9534f")                    \
	X(AudioGroupYellow, "--ed-audio-group-yellow", "#ffd000")                    \
	X(AudioGroupOrange, "--ed-audio-group-orange", "#f59c04")                    \
	X(AudioGroupRed, "--ed-audio-group-red", "#ff4b3a")                          \
	X(AudioGroupMagenta, "--ed-audio-group-magenta", "#ff619c")                  \
	X(AudioGroupViolet, "--ed-audio-group-violet", "#a872b7")                    \
	X(AudioGroupBlue, "--ed-audio-group-blue", "#0d9cd2")                        \
	X(AudioGroupCyan, "--ed-audio-group-cyan", "#00bec8")                        \
	X(AudioGroupGreen, "--ed-audio-group-green", "#8ac001")                      \
	X(GizmoVertexSnap, "--ed-gizmo-vertex-snap", "#78dcff")                      \
	X(GizmoShadowFit, "--ed-gizmo-shadow-fit", "#ff78d2")                        \
	X(GizmoShadowFitCamera, "--ed-gizmo-shadow-fit-camera", "#78e0ff")           \
	X(UIDebugMargin, "--ed-ui-debug-margin", "#e0904dcc")                        \
	X(UIDebugBorder, "--ed-ui-debug-border", "#e0c466cc")                        \
	X(UIDebugPadding, "--ed-ui-debug-padding", "#7fbf6bcc")                      \
	X(UIDebugContent, "--ed-ui-debug-content", "#5b9bd54d")                      \
	X(UIDebugTextChar, "--ed-ui-debug-text-char", "#d76ad0cc")                   \
	X(UIDebugTextWord, "--ed-ui-debug-text-word", "#66c8c4cc")                   \
	X(UIDebugTextLine, "--ed-ui-debug-text-line", "#e0c466cc")

#define SLT_THEME_LABEL_CLASSES(X) \
	X(Red, "ed-label-red")         \
	X(Orange, "ed-label-orange")   \
	X(Yellow, "ed-label-yellow")   \
	X(Green, "ed-label-green")     \
	X(Teal, "ed-label-teal")       \
	X(Blue, "ed-label-blue")       \
	X(Purple, "ed-label-purple")   \
	X(Pink, "ed-label-pink")

#define SLT_THEME_METRIC_TOKENS(X)                                      \
	X(RadiusXs, "--slt-radius-xs", "2")                                 \
	X(RadiusSm, "--slt-radius-sm", "3")                                 \
	X(Radius, "--slt-radius", "4")                                      \
	X(RadiusTab, "--slt-radius-tab", "4")                               \
	X(RadiusLg, "--slt-radius-lg", "4")                                 \
	X(RadiusPill, "--slt-radius-pill", "7")                             \
	X(RadiusRow, "--slt-radius-row", "0")                               \
	X(TabFontSize, "--slt-tab-font-size", "11.5")                       \
	X(TabIconGap, "--slt-tab-icon-gap", "5")                            \
	X(TreeGutter, "--slt-tree-gutter", "14")                            \
	X(NumberGap, "--slt-number-gap", "6")                               \
	X(NumberPairMin, "--slt-number-pair-min", "52")                     \
	X(MenuCheckGutter, "--slt-menu-check-gutter", "16")                 \
	X(TooltipOffset, "--slt-tooltip-offset", "18")                      \
	X(FontTitle, "--ed-font-title", "11.5")                             \
	X(FontHeading, "--ed-font-heading", "11.5")                         \
	X(Font, "--ed-font", "11.5")                                        \
	X(FontBody, "--ed-font-body", "11.5")                               \
	X(FontRow, "--ed-font-row", "11.5")                                 \
	X(FontSmall, "--ed-font-small", "11.5")                             \
	X(FontTiny, "--ed-font-tiny", "11.5")                               \
	X(TextStrokeWidth, "--ed-text-stroke-width", "1")                   \
	X(GraphNoteSmall, "--ed-graph-note-small", "11.5")                  \
	X(GraphNoteMedium, "--ed-graph-note-medium", "14")                  \
	X(GraphNoteLarge, "--ed-graph-note-large", "20")                    \
	X(GraphNoteHuge, "--ed-graph-note-huge", "32")                      \
	X(IconGutter, "--ed-icon-gutter", "11")                             \
	X(IconBody, "--ed-icon-body", "12")                                 \
	X(IconTab, "--ed-icon-tab", "11")                                   \
	X(IconGap, "--ed-icon-gap", "4")                                    \
	X(IconEmptyState, "--ed-icon-empty-state", "32")                    \
	X(MarkTitle, "--ed-mark-title", "18")                               \
	X(MarkComponent, "--ed-mark-component", "16")                       \
	X(MarkSceneIcon, "--ed-mark-scene-icon", "24")                      \
	X(MarkOrientation, "--ed-mark-orientation", "84")                   \
	X(RowPaddingX, "--ed-row-padding-x", "6")                           \
	X(InlineListHeight, "--ed-inline-list-height", "160")               \
	X(PreviewPaneHeight, "--ed-preview-pane-height", "180")             \
	X(AudioGraphHeight, "--ed-audio-graph-height", "170")               \
	X(CurveEditorWidth, "--ed-curve-editor-width", "360")               \
	X(CurveEditorHeight, "--ed-curve-editor-height", "200")             \
	X(AudioStripWidth, "--ed-audio-strip-width", "84")                  \
	X(AudioMeterHeight, "--ed-audio-meter-height", "190")               \
	X(AudioMixerColumn, "--ed-audio-mixer-column", "190")               \
	X(AudioChainColumn, "--ed-audio-chain-column", "280")               \
	X(AudioMixerSnapshots, "--ed-audio-mixer-snapshots", "96")          \
	X(AudioMixerViews, "--ed-audio-mixer-views", "72")                  \
	X(AudioMixerMixers, "--ed-audio-mixer-mixers", "90")                \
	X(PreviewBadgeSize, "--ed-preview-badge-size", "44")                \
	X(PreviewRenderSize, "--ed-preview-render-size", "256")             \
	X(BrowserCellMin, "--ed-browser-cell-min", "52")                    \
	X(BrowserCellMax, "--ed-browser-cell-max", "124")                   \
	X(BrowserCellLabel, "--ed-browser-cell-label", "28")                \
	X(BrowserCellPad, "--ed-browser-cell-pad", "5")                     \
	X(BrowserFoldButtonSmall, "--ed-browser-fold-button-small", "14")   \
	X(BrowserFoldButtonMedium, "--ed-browser-fold-button-medium", "20") \
	X(BrowserFoldButtonLarge, "--ed-browser-fold-button-large", "26")   \
	X(BrowserFoldGlyph, "--ed-browser-fold-glyph", "0.5")               \
	X(BrowserFoldCap, "--ed-browser-fold-cap", "0.12")                  \
	X(BrowserFoldRadius, "--ed-browser-fold-radius", "6")               \
	X(BrowserFoldInset, "--ed-browser-fold-inset", "2")                 \
	X(BrowserSliderWidth, "--ed-browser-slider-width", "84")            \
	X(CaptionControlsWidth, "--ed-caption-controls-width", "120")       \
	X(CaptionStripHeight, "--ed-caption-strip-height", "30")            \
	X(SelectionOutlineWidth, "--ed-selection-outline-width", "2")

namespace Shalltear::EditorTheme {

	enum class ColorToken : u32 {
#define SLT_THEME_X(name, cssName, value) name,
		SLT_THEME_COLOR_TOKENS(SLT_THEME_X)
#undef SLT_THEME_X
			Count
	};

	enum class MetricToken : u32 {
#define SLT_THEME_X(name, cssName, value) name,
		SLT_THEME_METRIC_TOKENS(SLT_THEME_X)
#undef SLT_THEME_X
			Count
	};

	inline constexpr u32 COLOR_TOKEN_COUNT = static_cast<u32>(ColorToken::Count);
	inline constexpr u32 METRIC_TOKEN_COUNT = static_cast<u32>(MetricToken::Count);

	SHALLTEAR_EDITOR_API Color GetColor(ColorToken token);
	SHALLTEAR_EDITOR_API f32 GetMetric(MetricToken token);
	SHALLTEAR_EDITOR_API Color32 GetColor32(ColorToken token);

	SHALLTEAR_EDITOR_API std::string_view GetVarName(ColorToken token);
	SHALLTEAR_EDITOR_API std::string_view GetVarName(MetricToken token);

	struct ThemeDesc {
		str colors[COLOR_TOKEN_COUNT];
		str metrics[METRIC_TOKEN_COUNT];

		void Set(ColorToken token, std::string_view cssValue) { colors[static_cast<u32>(token)] = str(cssValue); }
		void Set(MetricToken token, std::string_view cssValue) { metrics[static_cast<u32>(token)] = str(cssValue); }
	};

	SHALLTEAR_EDITOR_API ThemeDesc MakeDefaultDesc();

	inline constexpr StringHash DarkTheme = StringHash("Dark");

	SHALLTEAR_EDITOR_API void Register(StringHash name, const ThemeDesc& desc);
	SHALLTEAR_EDITOR_API bool SetActive(StringHash name);
	SHALLTEAR_EDITOR_API StringHash GetActive();
	SHALLTEAR_EDITOR_API void ForEachTheme(const std::function<void(StringHash)>& visit);
	SHALLTEAR_EDITOR_API void Shutdown();

	SHALLTEAR_EDITOR_API void ApplyToContext(UI::Context& context);
	SHALLTEAR_EDITOR_API std::string_view GetSheetSource();

	namespace Class {
		inline constexpr StringHash ChromeBar = StringHash("ed-chrome-bar");
		inline constexpr StringHash TitleBar = StringHash("ed-title-bar");
		inline constexpr StringHash TitleZone = StringHash("ed-title-zone");
		inline constexpr StringHash TitleMenu = StringHash("ed-title-menu");
		inline constexpr StringHash ToolButton = StringHash("ed-tool-button");
		inline constexpr StringHash CaptionClose = StringHash("ed-caption-close");
		inline constexpr StringHash IconButton = StringHash("ed-icon-button");
		inline constexpr StringHash ToolButtonOn = StringHash("ed-tool-button-on");
		inline constexpr StringHash SwitchGroup = StringHash("ed-switch-group");
		inline constexpr StringHash PathCrumb = StringHash("ed-path-crumb");
		inline constexpr StringHash FieldRefused = StringHash("ed-field-refused");
		inline constexpr StringHash PanelHeader = StringHash("ed-panel-header");
		inline constexpr StringHash PanelHeaderDrop = StringHash("ed-panel-header-drop");
		inline constexpr StringHash LayoutBar = StringHash("ed-layout-bar");
		inline constexpr StringHash StatusBar = StringHash("ed-status-bar");
		inline constexpr StringHash StatusNotice = StringHash("ed-status-notice");
		inline constexpr StringHash DockGutter = StringHash("ed-dock-gutter");
		inline constexpr StringHash PopupCard = StringHash("ed-popup-card");
		inline constexpr StringHash PopupCardCompact = StringHash("ed-popup-card-compact");
		inline constexpr StringHash AboutCard = StringHash("ed-about-card");
		inline constexpr StringHash SectionCard = StringHash("ed-section-card");
		inline constexpr StringHash HubBody = StringHash("ed-hub-body");
		inline constexpr StringHash HubColumns = StringHash("ed-hub-columns");
		inline constexpr StringHash HubRow = StringHash("ed-hub-row");
		inline constexpr StringHash Clickable = StringHash("ed-clickable");
		inline constexpr StringHash DragHandle = StringHash("ed-drag-handle");
		inline constexpr StringHash RowPing = StringHash("ed-row-ping");
		inline constexpr StringHash SectionHeader = StringHash("ed-section-header");
		inline constexpr StringHash ComponentCard = StringHash("ed-component-card");
		inline constexpr StringHash StageBreadcrumb = StringHash("ed-stage-breadcrumb");
		inline constexpr StringHash InsertStrip = StringHash("ed-insert-strip");
		inline constexpr StringHash FieldLabel = StringHash("ed-field-label");
		inline constexpr StringHash RowText = StringHash("ed-row-text");
		inline constexpr StringHash LogRow = StringHash("ed-log-row");
		inline constexpr StringHash OverrideMark = StringHash("ed-override-mark");
		inline constexpr StringHash ObjectField = StringHash("ed-object-field");
		inline constexpr StringHash ObjectFieldButton = StringHash("ed-object-field-button");
		inline constexpr StringHash RowReveal = StringHash("ed-row-reveal");
		inline constexpr StringHash RowOff = StringHash("ed-row-off");
		SHALLTEAR_EDITOR_API StringHash ForLabel(u8 labelColor);
		SHALLTEAR_EDITOR_API u32 GetLabelClassCount();
		inline constexpr StringHash Disabled = StringHash("ed-disabled");
		inline constexpr StringHash Graph = StringHash("ed-graph");
		inline constexpr StringHash GraphPreviewWell = StringHash("ed-graph-preview-well");
		inline constexpr StringHash GraphPreviewStrip = StringHash("ed-graph-preview-strip");
		inline constexpr StringHash GraphInlineField = StringHash("ed-graph-inline-field");
		inline constexpr StringHash GraphBlock = StringHash("ed-graph-block");
		inline constexpr StringHash GraphFlowPort = StringHash("ed-graph-flow-port");
		inline constexpr StringHash GraphSettingAction = StringHash("ed-graph-setting-action");
		inline constexpr StringHash GraphSpaceButton = StringHash("ed-graph-space-button");
		inline constexpr StringHash GraphBoard = StringHash("ed-graph-board");
		inline constexpr StringHash CurveEditor = StringHash("ed-curve-editor");
		inline constexpr StringHash CurveEditorPlot = StringHash("ed-curve-editor-plot");
		inline constexpr StringHash ViewportPlaceholder = StringHash("ed-viewport-placeholder");
		inline constexpr StringHash SceneFollow = StringHash("ed-scene-follow");
		inline constexpr StringHash SceneOrientation = StringHash("ed-scene-orientation");
		inline constexpr StringHash SceneOrientationCube = StringHash("ed-scene-orientation-cube");
		inline constexpr StringHash SceneOverlay = StringHash("ed-scene-overlay");
		inline constexpr StringHash SceneOverlayCard = StringHash("ed-scene-overlay-card");
		inline constexpr StringHash PlayTint = StringHash("ed-play-tint");
		inline constexpr StringHash UIDebugBand = StringHash("ed-ui-debug-band");
		inline constexpr StringHash UIDebugValue = StringHash("ed-ui-debug-value");
		inline constexpr StringHash UIDebugLink = StringHash("ed-ui-debug-link");
	} // namespace Class

} // namespace Shalltear::EditorTheme
