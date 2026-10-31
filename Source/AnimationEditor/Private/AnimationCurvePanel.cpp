#include "AnimationEditor.h"

#include "Shalltear/Core/Asset/AssetCues.h"
#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorDragData.h"
#include "Shalltear/Editor/EditorEvents.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/EditorSelection.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/ObjectField.h"
#include "Shalltear/Editor/PropertyRows.h"

#include "Shalltear/Animation/AnimationClip.h"

#include "AnimationClipOps.h"
#include "Binder.h"

#include <cmath>
#include <cstdio>
#include <span>
#include <utility>

namespace Shalltear {

	namespace AnimationEditorImpl {

		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;

		constexpr f32 RULER_HEIGHT = 22.0f;
		constexpr f32 ROW_HEIGHT = 18.0f;
		constexpr f32 KEY_RADIUS = 4.0f;
		constexpr f32 KEY_PICK_RADIUS = 7.0f;
		constexpr f32 TRACK_LIST_WIDTH = 236.0f;
		constexpr f32 MIN_PIXELS_PER_SECOND = 8.0f;
		constexpr f32 MAX_PIXELS_PER_SECOND = 6000.0f;
		constexpr f32 DEFAULT_PIXELS_PER_SECOND = 180.0f;
		constexpr f32 MIN_GRID_SPACING = 9.0f;

		struct Grid {
			f32 minor = 1.0f;
			f32 major = 5.0f;
			bool frames = false;
			f32 rate = 0.0f;
		};

		constexpr f32 HANDLE_LENGTH = 44.0f;
		constexpr f32 HANDLE_PICK_RADIUS = 8.0f;

		enum class SheetMode : u8 {
			Dopesheet,
			Curves,
		};

		enum class CurveGrab : u8 {
			None,
			Key,
			InTangent,
			OutTangent,
		};

		class AnimationCurvePanel : public EditorPanel {
		public:
			SLT_REFLECT_CLASS(AnimationCurvePanel)
			SLT_REFLECT_PARENT(EditorPanel)
			SLT_REFLECT_CREATABLE()
			SLT_EDITOR_PANEL_NAME("Animation")
			SLT_EDITOR_PANEL_CATEGORY("Animation")
			SLT_END_REFLECT()

			AnimationCurvePanel() = default;

			void OnBuildContent(UI::Context& context, UI::ElementHandle root) override;
			void OnTick(f32 deltaSeconds) override;
			bool OnCommand(PanelCommand command) override;
			void OnClose() override;
		private:
			Guid _guid{};
			Handle<AnimationClip> _clip;
			bool _dirty = false;

			Handle<SceneObject> _target;

			f32 _timeOrigin = 0.0f;
			f32 _pixelsPerSecond = DEFAULT_PIXELS_PER_SECOND;
			f32 _playhead = 0.0f;

			f32 _valueLow = -1.0f;
			f32 _valueHigh = 1.0f;
			bool _needsValueFit = true;
			SheetMode _mode = SheetMode::Dopesheet;
			CurveGrab _grab = CurveGrab::None;

			i32 _pickTrack = -1;
			i32 _pickKey = -1;

			bool _draggingKey = false;
			bool _scrubbing = false;
			bool _panning = false;
			Vec2 _panAnchor{};

			UI::ElementHandle _title;
			UI::ElementHandle _targetLabel;
			UI::ElementHandle _sheet;
			UI::ElementHandle _rulerHost;
			UI::ElementHandle _trackHost;
			UI::ElementHandle _properties;
			UI::ElementHandle _viewButtons[2];

			bool _needsTrackRebuild = false;
			bool _needsRulerRebuild = false;
			bool _needsPropertyRebuild = false;

			AnimationClip* Clip() const { return _clip.Get(); }
			void FollowSelection();
			void Save();
			void MarkEdited(bool structural);
			void Select(i32 track, i32 key);

			f32 TimeToLocal(f32 time) const { return (time - _timeOrigin) * _pixelsPerSecond; }
			f32 LocalToTime(f32 x) const { return _timeOrigin + x / _pixelsPerSecond; }
			f32 RowTop(i32 track) const { return RULER_HEIGHT + static_cast<f32>(track) * ROW_HEIGHT; }
			i32 TrackAtLocal(f32 y) const;
			Grid GridFor() const;
			bool PickKey(Vec2 local, i32& track, i32& key) const;

			f32 ValueToLocal(f32 value, f32 heightLocal) const;
			f32 LocalToValue(f32 y, f32 heightLocal) const;
			f32 SheetHeight(UI::Context& context) const;
			void FitValueRange();
			Vec2 HandleOffset(f32 slope, bool outgoing, f32 heightLocal) const;
			CurveGrab PickCurve(Vec2 local, f32 heightLocal, i32& track, i32& key) const;
			void PaintCurves(UI::Context& context, UI::PaintScope& paint, const UI::Rect& content, f32 scale,
							 f32 widthLocal, f32 heightLocal);
			bool PointerLocal(UI::Context& context, Vec2& out) const;

			void BuildToolbar(UI::Context& context, UI::ElementHandle parent);
			void BuildBody(UI::Context& context, UI::ElementHandle parent);
			void RebuildTracks(UI::Context& context);
			void RebuildRuler(UI::Context& context);
			void RebuildProperties(UI::Context& context);
			void RefreshTitle(UI::Context& context);

			void PaintSheet(UI::Context& context, UI::ElementHandle element, UI::PaintScope& paint);

			void AddPropertyMenu(UI::Context& context);
			void TrackMenu(UI::Context& context, i32 track);
			void DeleteSelection();
			bool _needsFrame = false;
			bool FrameAll(UI::Context& context);
		};

		namespace {

			UI::ElementHandle AddText(UI::Context& context, UI::ElementHandle parent, ColorToken color,
									  MetricToken size, std::string_view text) {
				UI::ElementHandle line = context.CreateElement();
				context.AppendChild(parent, line);
				context.SetText(line, UISystem::GetDefaultFont(), EditorTheme::GetMetric(size),
								EditorTheme::GetColor(color), text);
				return line;
			}

			str TrackLabel(const AnimationClip::Track& track) {
				str label;
				if (!track.objectPath.empty()) {
					label = track.objectPath;
					label += " . ";
				}
				label += track.name.size() > 1 && track.name[0] == '_' ? std::string_view(track.name).substr(1)
																	   : std::string_view(track.name);
				if (track.component != ANIMATION_WHOLE_VALUE) {
					constexpr const char* CHANNEL[] = {".x", ".y", ".z", ".w"};
					label += track.component < 4 ? CHANNEL[track.component] : ".?";
				}
				return label;
			}

			f32 LadderStep(f32 wanted) {
				if (!(wanted > 0.0f)) {
					return 1.0f;
				}
				const f32 decade = std::pow(10.0f, std::floor(std::log10(wanted)));
				const f32 normalized = wanted / decade;
				if (normalized <= 1.0f) {
					return decade;
				}
				if (normalized <= 2.0f) {
					return 2.0f * decade;
				}
				if (normalized <= 5.0f) {
					return 5.0f * decade;
				}
				return 10.0f * decade;
			}

			f32 FrameLadderStep(f32 wantedFrames) {
				constexpr f32 STEPS[] = {1.0f, 2.0f, 5.0f, 10.0f, 25.0f, 50.0f, 100.0f, 250.0f, 500.0f, 1000.0f};
				for (f32 step : STEPS) {
					if (step >= wantedFrames) {
						return step;
					}
				}
				return LadderStep(wantedFrames);
			}

			std::string_view FieldName(const FieldInfo& field) {
				return field.displayName ? std::string_view(field.displayName) : std::string_view("<field>");
			}

			str UniqueName(const AnimationClip& clip, AnimationTrackKind kind, StringHash componentType,
						   std::string_view base) {
				for (u32 attempt = 0; attempt < 64; ++attempt) {
					const str candidate = attempt == 0 ? str(base) : std::format("{} {}", base, attempt);
					AnimationClipOps::TrackAddress address;
					address.kind = kind;
					address.objectPath = "";
					address.componentType = componentType;
					address.name = candidate;
					address.component = ANIMATION_WHOLE_VALUE;
					if (AnimationClipOps::FindTrack(clip, address) < 0) {
						return candidate;
					}
				}
				return str(base);
			}

			std::string_view TypeLabel(const TypeInfo* type) {
				return type && type->displayName ? std::string_view(type->displayName) : std::string_view("<type>");
			}

			// must match the binder (IsCallableFromClip)
			void GatherCallableMethods(const TypeInfo* type, Vector<std::string_view>& out) {
				for (const TypeInfo* walk = type; walk; walk = walk->parent) {
					for (u32 m = 0; m < walk->methodCount; ++m) {
						const MethodInfo& method = walk->methods[m];
						if (!method.displayName || !AnimationImpl::IsCallableFromClip(type, method.name)) {
							continue;
						}
						const std::string_view name{method.displayName};
						bool seen = false;
						for (std::string_view had : out) {
							if (had == name) {
								seen = true;
								break;
							}
						}
						if (!seen) {
							out.push_back(name);
						}
					}
				}
			}

			const TypeInfo* MethodOwnerType(StringHash componentType) {
				return componentType.GetValue() == 0 ? SceneObject::GetReflectType()
													 : TypeRegistry::FindType(componentType);
			}

			constexpr std::string_view SelfPath = "";

		} // anonymous namespace

		void AnimationCurvePanel::FollowSelection() {
			if (Object* active = EditorSelection::GetActive().Get()) {
				if (TypeRegistry::IsDerivedFrom(active->GetType(), SceneObject::GetReflectType())) {
					Handle<SceneObject> selected(static_cast<SceneObject*>(active));
					if (selected != _target) {
						_target = selected;
						_needsPropertyRebuild = true;
					}
				}
			}

			const Guid& selected = EditorSelection::GetSelectedAsset();
			if (!selected.IsValid() || selected == _guid) {
				return;
			}
			Asset* resolved = AssetDatabase::LoadAssetByGuid(selected);
			if (!resolved || resolved->GetAssetTypeId() != AnimationClip::GetReflectType()->name) {
				return;
			}
			if (AnimationClip* previous = Clip()) {
				AssetDatabase::UnloadAsset(previous);
			}
			_guid = selected;
			_clip = Handle<AnimationClip>(static_cast<AnimationClip*>(resolved));
			_dirty = false;
			_timeOrigin = 0.0f;
			_pixelsPerSecond = DEFAULT_PIXELS_PER_SECOND;
			_playhead = 0.0f;
			Select(-1, -1);
			_needsTrackRebuild = true;
			_needsRulerRebuild = true;
			_needsFrame = true;
		}

		void AnimationCurvePanel::OnClose() {
			if (AnimationClip* held = Clip()) {
				AssetDatabase::UnloadAsset(held);
			}
			_clip = {};
			_guid = {};
		}

		void AnimationCurvePanel::Save() {
			AnimationClip* clip = Clip();
			if (!clip || !_guid.IsValid()) {
				return;
			}
			Path path = AssetDatabase::LookupPath(_guid);
			if (path.ToString().empty() || path.GetExtension() != ".anim") {
				SLT_LOG_WARN("Animation", "Animation: '{}' is a model's clip with no file (edits aren't saved)",
							 path.ToString().empty() ? str("<unnamed>") : path.ToString());
				return;
			}
			if (clip->Export(FileSystem::Resolve(path))) {
				_dirty = false;
				SLT_LOG_INFO("Animation", "Animation: saved '{}'", path.ToString());
			} else {
				SLT_LOG_WARN("Animation", "Animation: failed to write '{}'", path.ToString());
			}
		}

		void AnimationCurvePanel::MarkEdited(bool structural) {
			_dirty = true;
			if (structural) {
				_needsTrackRebuild = true;
			}
			if (AnimationClip* clip = Clip()) {
				AnimationClipOps::RecomputeLength(*clip);
			}
			_needsRulerRebuild = true;
			_needsValueFit = true;
		}

		void AnimationCurvePanel::Select(i32 track, i32 key) {
			_pickTrack = track;
			_pickKey = key;
			_needsPropertyRebuild = true;
		}

		i32 AnimationCurvePanel::TrackAtLocal(f32 y) const {
			const AnimationClip* clip = Clip();
			if (!clip || y < RULER_HEIGHT) {
				return -1;
			}
			const i32 row = static_cast<i32>((y - RULER_HEIGHT) / ROW_HEIGHT);
			return row >= 0 && static_cast<size_t>(row) < clip->tracks.size() ? row : -1;
		}

		Grid AnimationCurvePanel::GridFor() const {
			Grid grid;
			const AnimationClip* clip = Clip();
			const f32 rate = clip ? clip->frameRate : 0.0f;
			if (rate > 0.0f) {
				grid.frames = true;
				grid.rate = rate;
				const f32 wanted = MIN_GRID_SPACING * rate / _pixelsPerSecond;
				const f32 step = FrameLadderStep(wanted);
				grid.minor = step / rate;
				grid.major = grid.minor * 5.0f;
				return grid;
			}
			grid.minor = LadderStep(MIN_GRID_SPACING / _pixelsPerSecond);
			grid.major = grid.minor * 5.0f;
			return grid;
		}

		bool AnimationCurvePanel::PickKey(Vec2 local, i32& track, i32& key) const {
			const AnimationClip* clip = Clip();
			const i32 row = TrackAtLocal(local.y);
			if (!clip || row < 0) {
				return false;
			}
			const AnimationClip::Track& curve = clip->tracks[static_cast<size_t>(row)];
			const size_t count = AnimationImpl::KeyCount(curve);
			f32 best = KEY_PICK_RADIUS;
			i32 found = -1;
			for (size_t k = 0; k < count; ++k) {
				const f32 distance = Math::Abs(TimeToLocal(curve.times[k]) - local.x);
				if (distance <= best) {
					best = distance;
					found = static_cast<i32>(k);
				}
			}
			if (found < 0) {
				return false;
			}
			track = row;
			key = found;
			return true;
		}

		f32 AnimationCurvePanel::ValueToLocal(f32 value, f32 heightLocal) const {
			const f32 span = _valueHigh - _valueLow;
			const f32 usable = heightLocal - RULER_HEIGHT;
			if (!(span > 0.0f) || !(usable > 0.0f)) {
				return RULER_HEIGHT + usable * 0.5f;
			}
			return RULER_HEIGHT + usable * (1.0f - (value - _valueLow) / span);
		}

		f32 AnimationCurvePanel::LocalToValue(f32 y, f32 heightLocal) const {
			const f32 span = _valueHigh - _valueLow;
			const f32 usable = heightLocal - RULER_HEIGHT;
			if (!(usable > 0.0f)) {
				return _valueLow;
			}
			return _valueLow + span * (1.0f - (y - RULER_HEIGHT) / usable);
		}

		f32 AnimationCurvePanel::SheetHeight(UI::Context& context) const {
			if (!context.IsAlive(_sheet)) {
				return 0.0f;
			}
			const f32 scale = context.GetContentScale();
			return context.GetLayout(_sheet).content.h / (scale > 0.0f ? scale : 1.0f);
		}

		void AnimationCurvePanel::FitValueRange() {
			_needsValueFit = false;
			const AnimationClip* clip = Clip();
			if (!clip) {
				return;
			}
			f32 low = 0.0f;
			f32 high = 0.0f;
			bool any = false;
			for (const AnimationClip::Track& track : clip->tracks) {
				const size_t count = AnimationImpl::KeyCount(track);
				for (size_t k = 0; k < count; ++k) {
					const f32 v = AnimationImpl::KeyValue(track, k).x;
					low = any ? Math::Min(low, v) : v;
					high = any ? Math::Max(high, v) : v;
					any = true;
				}
			}
			if (!any) {
				_valueLow = -1.0f;
				_valueHigh = 1.0f;
				return;
			}
			const f32 span = Math::Max(high - low, 1e-3f);
			_valueLow = low - span * 0.15f;
			_valueHigh = high + span * 0.15f;
		}

		Vec2 AnimationCurvePanel::HandleOffset(f32 slope, bool outgoing, f32 heightLocal) const {
			const f32 valueSpan = _valueHigh - _valueLow;
			const f32 usable = heightLocal - RULER_HEIGHT;
			const f32 pixelsPerValue = valueSpan > 0.0f && usable > 0.0f ? usable / valueSpan : 1.0f;
			Vec2 direction(1.0f, -slope * pixelsPerValue / _pixelsPerSecond);
			const f32 length = direction.Length();
			if (!(length > 0.0f)) {
				direction = Vec2(1.0f, 0.0f);
			} else {
				direction = direction * (1.0f / length);
			}
			const f32 scaled = HANDLE_LENGTH * (outgoing ? 1.0f : -1.0f);
			return Vec2(direction.x * scaled, direction.y * scaled);
		}

		CurveGrab AnimationCurvePanel::PickCurve(Vec2 local, f32 heightLocal, i32& track, i32& key) const {
			const AnimationClip* clip = Clip();
			if (!clip || clip->tracks.empty()) {
				return CurveGrab::None;
			}
			if (_pickTrack >= 0 && static_cast<size_t>(_pickTrack) < clip->tracks.size() && _pickKey >= 0) {
				const AnimationClip::Track& curve = clip->tracks[static_cast<size_t>(_pickTrack)];
				if (static_cast<size_t>(_pickKey) < AnimationImpl::KeyCount(curve) &&
					AnimationValuesPerKey(curve.interp) == 3u) {
					const size_t k = static_cast<size_t>(_pickKey);
					const Vec2 at(TimeToLocal(curve.times[k]),
								  ValueToLocal(AnimationImpl::KeyValue(curve, k).x, heightLocal));
					const Vec2 in = at + HandleOffset(AnimationImpl::KeyInTangent(curve, k).x, false, heightLocal);
					const Vec2 out = at + HandleOffset(AnimationImpl::KeyOutTangent(curve, k).x, true, heightLocal);
					if ((local - in).Length() <= HANDLE_PICK_RADIUS) {
						track = _pickTrack;
						key = _pickKey;
						return CurveGrab::InTangent;
					}
					if ((local - out).Length() <= HANDLE_PICK_RADIUS) {
						track = _pickTrack;
						key = _pickKey;
						return CurveGrab::OutTangent;
					}
				}
			}

			f32 best = KEY_PICK_RADIUS;
			CurveGrab result = CurveGrab::None;
			for (size_t t = 0; t < clip->tracks.size(); ++t) {
				const AnimationClip::Track& curve = clip->tracks[t];
				const size_t count = AnimationImpl::KeyCount(curve);
				for (size_t k = 0; k < count; ++k) {
					const Vec2 at(TimeToLocal(curve.times[k]),
								  ValueToLocal(AnimationImpl::KeyValue(curve, k).x, heightLocal));
					const f32 distance = (local - at).Length();
					if (distance <= best) {
						best = distance;
						track = static_cast<i32>(t);
						key = static_cast<i32>(k);
						result = CurveGrab::Key;
					}
				}
			}
			return result;
		}

		bool AnimationCurvePanel::PointerLocal(UI::Context& context, Vec2& out) const {
			if (!context.IsAlive(_sheet)) {
				return false;
			}
			const UI::Rect content = context.GetLayout(_sheet).content;
			const f32 scale = context.GetContentScale();
			const Vec2 pointer = context.GetPointerPosition();
			out = Vec2((pointer.x - content.x) / scale, (pointer.y - content.y) / scale);
			return true;
		}

		void AnimationCurvePanel::OnBuildContent(UI::Context& context, UI::ElementHandle root) {
			UI::Style rootStyle;
			rootStyle.flexDirection = UI::FlexDirection::Column;
			rootStyle.width = UI::Length::Percent(1.0f);
			rootStyle.height = UI::Length::Percent(1.0f);
			context.SetStyle(root, rootStyle);

			BuildToolbar(context, root);
			BuildBody(context, root);

			RebuildTracks(context);
			RebuildProperties(context);
			RefreshTitle(context);
		}

		void AnimationCurvePanel::BuildToolbar(UI::Context& context, UI::ElementHandle parent) {
			UI::ElementHandle bar = context.CreateElement();
			context.AppendChild(parent, bar);
			UI::Style style;
			style.flexDirection = UI::FlexDirection::Row;
			style.alignItems = UI::Align::Center;
			style.width = UI::Length::Percent(1.0f);
			style.flexShrink = 0.0f;
			style.padding = {6.0f, 4.0f, 6.0f, 4.0f};
			style.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::HeaderFill));
			context.SetStyle(bar, style);

			_title = AddText(context, bar, ColorToken::TextStrong, MetricToken::FontBody, "no clip selected");

			UI::Widgets::ButtonDesc add;
			add.label = "Add Property";
			add.font = UISystem::GetDefaultFont();
			add.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
			add.onClick = [this]() {
				if (UI::Context* c = GetPanelContext()) {
					AddPropertyMenu(*c);
				}
			};
			UI::Widgets::Button(context, bar, add);

			_targetLabel = AddText(context, bar, ColorToken::WidgetTextDim, MetricToken::FontSmall, "");

			UI::Widgets::Spacer(context, bar);

			for (u32 index = 0; index < 2; ++index) {
				const SheetMode mode = index == 0 ? SheetMode::Dopesheet : SheetMode::Curves;
				UI::Widgets::ButtonDesc view;
				view.label = index == 0 ? "Dopesheet" : "Curves";
				view.font = UISystem::GetDefaultFont();
				view.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
				view.onClick = [this, mode]() {
					if (_mode == mode) {
						return;
					}
					_mode = mode;
					_needsValueFit = true;
					_needsPropertyRebuild = true;
					if (UI::Context* c = GetPanelContext()) {
						for (u32 which = 0; which < 2; ++which) {
							if (!c->IsAlive(_viewButtons[which])) {
								continue;
							}
							const bool live = (which == 0 ? SheetMode::Dopesheet : SheetMode::Curves) == _mode;
							if (live) {
								c->AddClass(_viewButtons[which], EditorTheme::Class::ToolButtonOn);
							} else {
								c->RemoveClass(_viewButtons[which], EditorTheme::Class::ToolButtonOn);
							}
						}
					}
				};
				UI::ElementHandle button = UI::Widgets::Button(context, bar, view);
				context.AddClass(button, EditorTheme::Class::ToolButton);
				if (_mode == mode) {
					context.AddClass(button, EditorTheme::Class::ToolButtonOn);
				}
				_viewButtons[index] = button;
			}

			UI::Widgets::ButtonDesc frame;
			frame.label = "Frame";
			frame.font = UISystem::GetDefaultFont();
			frame.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
			frame.onClick = [this]() { _needsFrame = true; };
			UI::Widgets::Button(context, bar, frame);

			UI::Widgets::ButtonDesc save;
			save.label = "Save";
			save.font = UISystem::GetDefaultFont();
			save.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
			save.onClick = [this]() {
				Save();
				if (UI::Context* c = GetPanelContext()) {
					RefreshTitle(*c);
				}
			};
			UI::Widgets::Button(context, bar, save);
		}

		void AnimationCurvePanel::BuildBody(UI::Context& context, UI::ElementHandle parent) {
			UI::ElementHandle body = context.CreateElement();
			context.AppendChild(parent, body);
			UI::Style bodyStyle;
			bodyStyle.flexDirection = UI::FlexDirection::Row;
			bodyStyle.width = UI::Length::Percent(1.0f);
			bodyStyle.flexGrow = 1.0f;
			bodyStyle.minHeight = UI::Length::Pixel(0.0f);
			context.SetStyle(body, bodyStyle);

			UI::ElementHandle list = context.CreateElement();
			context.AppendChild(body, list);
			UI::Style listStyle;
			listStyle.flexDirection = UI::FlexDirection::Column;
			listStyle.width = UI::Length::Pixel(TRACK_LIST_WIDTH);
			listStyle.flexShrink = 0.0f;
			listStyle.height = UI::Length::Percent(1.0f);
			listStyle.overflow = UI::Overflow::Hidden;
			listStyle.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::SectionFill));
			context.SetStyle(list, listStyle);

			UI::ElementHandle rulerGap = context.CreateElement();
			context.AppendChild(list, rulerGap);
			UI::Style gapStyle;
			gapStyle.height = UI::Length::Pixel(RULER_HEIGHT);
			gapStyle.flexShrink = 0.0f;
			gapStyle.width = UI::Length::Percent(1.0f);
			context.SetStyle(rulerGap, gapStyle);

			_trackHost = context.CreateElement();
			context.AppendChild(list, _trackHost);
			UI::Style hostStyle;
			hostStyle.flexDirection = UI::FlexDirection::Column;
			hostStyle.width = UI::Length::Percent(1.0f);
			context.SetStyle(_trackHost, hostStyle);

			_sheet = context.CreateElement();
			context.AppendChild(body, _sheet);
			UI::Style sheetStyle;
			sheetStyle.flexGrow = 1.0f;
			sheetStyle.height = UI::Length::Percent(1.0f);
			sheetStyle.minWidth = UI::Length::Pixel(0.0f);
			sheetStyle.overflow = UI::Overflow::Hidden;
			context.SetStyle(_sheet, sheetStyle);
			context.AddClass(_sheet, EditorTheme::Class::Graph);
			context.SetFocusable(_sheet, true);

			_rulerHost = context.CreateElement();
			context.AppendChild(_sheet, _rulerHost);
			UI::Style rulerStyle;
			rulerStyle.position = UI::Position::Absolute;
			rulerStyle.left = UI::Length::Pixel(0.0f);
			rulerStyle.top = UI::Length::Pixel(0.0f);
			rulerStyle.width = UI::Length::Pixel(0.0f);
			rulerStyle.height = UI::Length::Pixel(0.0f);
			context.SetStyle(_rulerHost, rulerStyle);

			context.Events(_sheet).customDraw = [this](UI::Context& c, UI::ElementHandle element, UI::PaintScope& paint) {
				PaintSheet(c, element, paint);
			};

			context.Events(_sheet).onClick = [this]() {
				UI::Context* c = GetPanelContext();
				Vec2 local{};
				if (!c || !PointerLocal(*c, local)) {
					return;
				}
				i32 track = -1;
				i32 key = -1;
				const bool hit = _mode == SheetMode::Curves
									 ? PickCurve(local, SheetHeight(*c), track, key) != CurveGrab::None
									 : PickKey(local, track, key);
				if (hit) {
					Select(track, key);
					return;
				}
				const AnimationClip* clip = Clip();
				_playhead = Math::Max(0.0f, clip ? AnimationClipOps::SnapTime(*clip, LocalToTime(local.x))
												 : LocalToTime(local.x));
				Select(-1, -1);
			};

			context.Events(_sheet).onDoubleClick = [this]() {
				UI::Context* c = GetPanelContext();
				AnimationClip* clip = Clip();
				Vec2 local{};
				if (!c || !clip || !PointerLocal(*c, local)) {
					return;
				}
				const i32 track = _mode == SheetMode::Curves ? _pickTrack : TrackAtLocal(local.y);
				if (track < 0 || static_cast<size_t>(track) >= clip->tracks.size()) {
					return;
				}
				const f32 time = Math::Max(0.0f, AnimationClipOps::SnapTime(*clip, LocalToTime(local.x)));
				const i32 key = AnimationClipOps::AddKey(*clip, track, time);
				if (key < 0) {
					return;
				}
				_playhead = time;
				MarkEdited(false);
				Select(track, key);
			};

			context.Events(_sheet).onRightClick = [this]() {
				UI::Context* c = GetPanelContext();
				Vec2 local{};
				if (!c || !PointerLocal(*c, local)) {
					return;
				}
				TrackMenu(*c, TrackAtLocal(local.y));
			};

			context.Events(_sheet).onMouseDown = [this]() {
				UI::Context* c = GetPanelContext();
				Vec2 local{};
				if (!c || !PointerLocal(*c, local)) {
					return;
				}
				_draggingKey = false;
				_scrubbing = false;
				_panning = false;
				_grab = CurveGrab::None;
				i32 track = -1;
				i32 key = -1;
				if (_mode == SheetMode::Curves) {
					const f32 heightLocal = SheetHeight(*c);
					const CurveGrab grabbed = PickCurve(local, heightLocal, track, key);
					if (grabbed != CurveGrab::None) {
						_grab = grabbed;
						if (grabbed == CurveGrab::Key) {
							Select(track, key);
						}
						return;
					}
					if (local.y < RULER_HEIGHT) {
						_scrubbing = true;
					} else {
						_panning = true;
						_panAnchor = local;
					}
					return;
				}
				if (Clip() && PickKey(local, track, key)) {
					_draggingKey = true;
					Select(track, key);
				} else if (local.y < RULER_HEIGHT) {
					_scrubbing = true;
				} else {
					_panning = true;
					_panAnchor = local;
				}
			};
			context.Events(_sheet).onPressDrag = [this](Vec2 point) {
				UI::Context* c = GetPanelContext();
				AnimationClip* clip = Clip();
				if (!c || !c->IsAlive(_sheet)) {
					return;
				}
				const UI::Rect content = c->GetLayout(_sheet).content;
				const f32 scale = c->GetContentScale();
				const Vec2 local((point.x - content.x) / scale, (point.y - content.y) / scale);

				if (_grab != CurveGrab::None && clip && _pickTrack >= 0) {
					const f32 heightLocal = SheetHeight(*c);
					if (_grab == CurveGrab::Key) {
						const f32 time = Math::Max(0.0f, AnimationClipOps::SnapTime(*clip, LocalToTime(local.x)));
						const i32 moved = AnimationClipOps::MoveKey(*clip, _pickTrack, _pickKey, time);
						if (moved >= 0) {
							_pickKey = moved;
							Vec4 edited = AnimationImpl::KeyValue(clip->tracks[static_cast<size_t>(_pickTrack)],
																  static_cast<size_t>(_pickKey));
							edited.x = LocalToValue(local.y, heightLocal);
							AnimationClipOps::SetKeyValue(*clip, _pickTrack, _pickKey, edited);
							_playhead = time;
							MarkEdited(false);
							_needsPropertyRebuild = true;
						}
						return;
					}
					const AnimationClip::Track& curve = clip->tracks[static_cast<size_t>(_pickTrack)];
					if (static_cast<size_t>(_pickKey) >= AnimationImpl::KeyCount(curve)) {
						return;
					}
					const size_t k = static_cast<size_t>(_pickKey);
					const Vec2 at(TimeToLocal(curve.times[k]),
								  ValueToLocal(AnimationImpl::KeyValue(curve, k).x, heightLocal));
					const Vec2 delta = local - at;
					const f32 valueSpan = _valueHigh - _valueLow;
					const f32 usable = heightLocal - RULER_HEIGHT;
					const f32 pixelsPerValue = valueSpan > 0.0f && usable > 0.0f ? usable / valueSpan : 1.0f;
					const f32 run = _grab == CurveGrab::OutTangent ? delta.x : -delta.x;
					if (!(Math::Abs(run) > 1.0f)) {
						return;
					}
					const f32 rise = _grab == CurveGrab::OutTangent ? -delta.y : delta.y;
					const f32 slope = (rise / pixelsPerValue) / (run / _pixelsPerSecond);
					Vec4 in = AnimationImpl::KeyInTangent(curve, k);
					Vec4 out = AnimationImpl::KeyOutTangent(curve, k);
					in.x = slope;
					out.x = slope;
					AnimationClipOps::SetKeyTangents(*clip, _pickTrack, _pickKey, in, out);
					MarkEdited(false);
					_needsPropertyRebuild = true;
					return;
				}

				if (_draggingKey && clip && _pickTrack >= 0) {
					const f32 time = Math::Max(0.0f, AnimationClipOps::SnapTime(*clip, LocalToTime(local.x)));
					const i32 moved = AnimationClipOps::MoveKey(*clip, _pickTrack, _pickKey, time);
					if (moved >= 0) {
						_pickKey = moved;
						_playhead = time;
						MarkEdited(false);
						_needsPropertyRebuild = true;
					}
					return;
				}
				if (_scrubbing) {
					_playhead = Math::Max(0.0f, clip ? AnimationClipOps::SnapTime(*clip, LocalToTime(local.x))
													 : LocalToTime(local.x));
					return;
				}
				if (_panning) {
					_timeOrigin -= (local.x - _panAnchor.x) / _pixelsPerSecond;
					_panAnchor = local;
					_needsRulerRebuild = true;
				}
			};
			context.Events(_sheet).onPressEnd = [this]() {
				_draggingKey = false;
				_scrubbing = false;
				_panning = false;
				_grab = CurveGrab::None;
			};
			context.Events(_sheet).onMiddleDrag = [this](Vec2 delta) {
				UI::Context* c = GetPanelContext();
				if (!c) {
					return;
				}
				_timeOrigin -= delta.x / (c->GetContentScale() * _pixelsPerSecond);
				_needsRulerRebuild = true;
			};

			context.Events(_sheet).onWheel = [this](Vec2 notches) {
				UI::Context* c = GetPanelContext();
				Vec2 local{};
				if (!c || notches.y == 0.0f || !PointerLocal(*c, local)) {
					return false;
				}
				const f32 anchor = LocalToTime(local.x);
				_pixelsPerSecond = Math::Clamp(_pixelsPerSecond * std::pow(1.15f, notches.y),
											   MIN_PIXELS_PER_SECOND, MAX_PIXELS_PER_SECOND);
				_timeOrigin = anchor - local.x / _pixelsPerSecond;
				_needsRulerRebuild = true;
				return true;
			};

			_properties = context.CreateElement();
			context.AppendChild(body, _properties);
			UI::Style column;
			column.flexDirection = UI::FlexDirection::Column;
			column.width = UI::Length::Pixel(226.0f);
			column.flexShrink = 0.0f;
			column.height = UI::Length::Percent(1.0f);
			column.overflow = UI::Overflow::Scroll;
			column.padding = {8.0f, 8.0f, 8.0f, 8.0f};
			column.background = UI::Brush::SolidColor(EditorTheme::GetColor(ColorToken::SectionFill));
			context.SetStyle(_properties, column);
		}

		void AnimationCurvePanel::RebuildTracks(UI::Context& context) {
			_needsTrackRebuild = false;
			if (!context.IsAlive(_trackHost)) {
				return;
			}
			context.ClearChildren(_trackHost);
			const AnimationClip* clip = Clip();
			if (!clip) {
				return;
			}
			for (size_t i = 0; i < clip->tracks.size(); ++i) {
				UI::ElementHandle row = context.CreateElement();
				context.AppendChild(_trackHost, row);
				UI::Style rowStyle;
				rowStyle.flexDirection = UI::FlexDirection::Row;
				rowStyle.alignItems = UI::Align::Center;
				rowStyle.width = UI::Length::Percent(1.0f);
				rowStyle.height = UI::Length::Pixel(ROW_HEIGHT);
				rowStyle.flexShrink = 0.0f;
				rowStyle.padding = {8.0f, 0.0f, 4.0f, 0.0f};
				context.SetStyle(row, rowStyle);
				context.AddClass(row, EditorTheme::Class::Clickable);

				const i32 index = static_cast<i32>(i);
				context.Events(row).onClick = [this, index]() { Select(index, -1); };
				context.Events(row).onRightClick = [this, index]() {
					if (UI::Context* c = GetPanelContext()) {
						TrackMenu(*c, index);
					}
				};

				UI::ElementHandle text = AddText(context, row, _pickTrack == index ? ColorToken::TextStrong : ColorToken::WidgetText,
												 MetricToken::FontSmall, TrackLabel(clip->tracks[i]));
				context.AddClass(text, EditorTheme::Class::FieldLabel);
				UI::Style inert;
				inert.pointerEvents = false;
				context.SetStyle(text, inert);
			}
		}

		void AnimationCurvePanel::RebuildRuler(UI::Context& context) {
			_needsRulerRebuild = false;
			if (!context.IsAlive(_rulerHost) || !context.IsAlive(_sheet)) {
				return;
			}
			context.ClearChildren(_rulerHost);
			const AnimationClip* clip = Clip();
			if (!clip) {
				return;
			}
			const f32 scale = context.GetContentScale();
			const f32 width = context.GetLayout(_sheet).content.w / (scale > 0.0f ? scale : 1.0f);
			const Grid grid = GridFor();
			if (!(grid.major > 0.0f)) {
				return;
			}
			const f32 first = std::floor(_timeOrigin / grid.major) * grid.major;
			constexpr u32 MAX_LABELS = 64;
			for (u32 n = 0; n < MAX_LABELS; ++n) {
				const f32 time = first + static_cast<f32>(n) * grid.major;
				const f32 x = TimeToLocal(time);
				if (x > width) {
					break;
				}
				if (x < -40.0f || time < 0.0f) {
					continue;
				}
				char text[32];
				if (grid.frames) {
					std::snprintf(text, sizeof(text), "%d", static_cast<i32>(std::lround(time * grid.rate)));
				} else {
					std::snprintf(text, sizeof(text), "%.3g", static_cast<f64>(time));
				}
				UI::ElementHandle label = AddText(context, _rulerHost, ColorToken::WidgetTextDim,
												  MetricToken::FontTiny, text);
				UI::Style style;
				style.position = UI::Position::Absolute;
				style.left = UI::Length::Pixel(x + 3.0f);
				style.top = UI::Length::Pixel(3.0f);
				context.SetStyle(label, style);
			}
		}

		void AnimationCurvePanel::RebuildProperties(UI::Context& context) {
			_needsPropertyRebuild = false;
			if (!context.IsAlive(_properties)) {
				return;
			}
			context.ClearChildren(_properties);
			AnimationClip* clip = Clip();
			if (!clip) {
				AddText(context, _properties, ColorToken::WidgetTextDim, MetricToken::FontSmall,
						"Select an Animation Clip.");
				return;
			}

			AddText(context, _properties, ColorToken::TextStrong, MetricToken::FontSmall, "Clip");

			UI::Widgets::NumberFieldDesc rate;
			rate.value = static_cast<f64>(clip->frameRate);
			rate.label = "Frame Rate";
			rate.font = UISystem::GetDefaultFont();
			rate.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
			rate.onChange = [this](f64 value) {
				AnimationClip* live = Clip();
				if (!live) {
					return;
				}
				live->frameRate = Math::Clamp(static_cast<f32>(value), 0.0f, 1000.0f);
				_dirty = true;
				_needsRulerRebuild = true;
			};
			UI::Widgets::NumberField(context, _properties, rate);

			char summary[64];
			std::snprintf(summary, sizeof(summary), "Length  %.3f s", static_cast<f64>(clip->length));
			AddText(context, _properties, ColorToken::WidgetTextDim, MetricToken::FontSmall, summary);

			if (_pickTrack < 0 || static_cast<size_t>(_pickTrack) >= clip->tracks.size()) {
				return;
			}
			const AnimationClip::Track& track = clip->tracks[static_cast<size_t>(_pickTrack)];
			UI::ElementHandle heading = AddText(context, _properties, ColorToken::TextStrong, MetricToken::FontSmall,
												TrackLabel(track));
			UI::Style headingStyle;
			headingStyle.margin = {0.0f, 12.0f, 0.0f, 0.0f};
			context.SetStyle(heading, headingStyle);

			if (track.kind == AnimationTrackKind::Method) {
				const TypeInfo* owner = MethodOwnerType(track.componentType);
				Vector<std::string_view> callable{GetMemoryResource(MemTag::Editor)};
				GatherCallableMethods(owner, callable);

				u32 selected = 0;
				bool known = false;
				for (u32 i = 0; i < callable.size(); ++i) {
					if (callable[i] == track.name) {
						selected = i;
						known = true;
						break;
					}
				}
				if (!known) {
					callable.insert(callable.begin(), std::string_view(track.name));
					selected = 0;
				}

				UI::Widgets::DropdownDesc pick;
				pick.items = std::span<const std::string_view>(callable.data(), callable.size());
				pick.selected = selected;
				pick.font = UISystem::GetDefaultFont();
				pick.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
				pick.onSelect = [this, names = callable](u32 index) {
					AnimationClip* live = Clip();
					if (!live || index >= names.size()) {
						return;
					}
					if (AnimationClipOps::SetTrackName(*live, _pickTrack, names[index])) {
						MarkEdited(true);
					}
					_needsPropertyRebuild = true;
				};
				UI::Widgets::Dropdown(context, _properties, pick);

				if (!known) {
					AddText(context, _properties, ColorToken::WidgetTextDim, MetricToken::FontSmall,
							"Nothing on this object reflects that method.");
				}

				AddText(context, _properties, ColorToken::WidgetTextDim, MetricToken::FontSmall,
						"Called with no arguments.");
			}

			const bool signalTrack = track.kind == AnimationTrackKind::Event || track.kind == AnimationTrackKind::Audio;
			if (signalTrack) {
				UI::Widgets::TextFieldDesc name;
				name.text = track.name;
				name.font = UISystem::GetDefaultFont();
				name.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
				name.onCommit = [this](std::string_view typed) {
					AnimationClip* live = Clip();
					if (!live) {
						return;
					}
					if (AnimationClipOps::SetTrackName(*live, _pickTrack, typed)) {
						MarkEdited(true);
					}
					_needsPropertyRebuild = true;
				};
				UI::Widgets::TextField(context, _properties, name);
			}

			{
				constexpr std::string_view InterpLabels[] = {"Step", "Linear", "Cubic"};
				UI::Widgets::DropdownDesc interp;
				interp.items = std::span<const std::string_view>(InterpLabels, 3);
				interp.selected = static_cast<u32>(track.interp) < 3u ? static_cast<u32>(track.interp) : 1u;
				interp.font = UISystem::GetDefaultFont();
				interp.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
				interp.onSelect = [this](u32 index) {
					AnimationClip* live = Clip();
					if (!live) {
						return;
					}
					if (AnimationClipOps::SetInterpolation(*live, _pickTrack,
														   static_cast<AnimationInterpolation>(index))) {
						MarkEdited(false);
						_needsPropertyRebuild = true;
					}
				};
				UI::Widgets::Dropdown(context, _properties, interp);
			}

			if (_pickKey < 0 || static_cast<size_t>(_pickKey) >= AnimationImpl::KeyCount(track)) {
				AddText(context, _properties, ColorToken::WidgetTextDim, MetricToken::FontSmall,
						"No key selected.");
				return;
			}

			UI::Widgets::NumberFieldDesc time;
			time.value = static_cast<f64>(track.times[static_cast<size_t>(_pickKey)]);
			time.label = "Time";
			time.font = UISystem::GetDefaultFont();
			time.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
			time.onChange = [this](f64 value) {
				AnimationClip* live = Clip();
				if (!live) {
					return;
				}
				const i32 moved = AnimationClipOps::MoveKey(*live, _pickTrack, _pickKey,
															Math::Max(0.0f, static_cast<f32>(value)));
				if (moved >= 0) {
					_pickKey = moved;
					MarkEdited(false);
				}
			};
			UI::Widgets::NumberField(context, _properties, time);

			if (track.kind == AnimationTrackKind::Method) {
				return;
			}

			if (track.kind == AnimationTrackKind::Audio) {
				const size_t key = static_cast<size_t>(_pickKey);
				const Guid sound = key < track.assets.size() ? track.assets[key] : Guid{};
				str caption = "None (drop a sound here)";
				if (sound.IsValid()) {
					const str path = AssetDatabase::LookupPath(sound).ToString();
					const size_t slash = path.find_last_of('/');
					caption = path.empty() ? sound.ToString() : path.substr(slash == str::npos ? 0 : slash + 1);
				}
				ObjectField::Desc box;
				box.caption = caption;
				box.accepts = [](const UI::DragPayload& payload) {
					const AssetDragData* drag = payload.type == StringHash("Asset") ? payload.data.TryGet<AssetDragData>() : nullptr;
					return drag && AssetCues::CanCue(TypeRegistry::FindType(StringHash(drag->assetTypeHash)));
				};
				box.onDrop = [this](const UI::DragPayload& payload, Vec2) {
					const AssetDragData* drag = payload.data.TryGet<AssetDragData>();
					AnimationClip* live = Clip();
					if (!drag || !live) {
						return;
					}
					if (AnimationClipOps::SetKeyAsset(*live, _pickTrack, _pickKey, drag->guid)) {
						MarkEdited(false);
						_needsPropertyRebuild = true;
					}
				};
				if (sound.IsValid()) {
					box.onClear = [this]() {
						AnimationClip* live = Clip();
						if (live && AnimationClipOps::SetKeyAsset(*live, _pickTrack, _pickKey, Guid{})) {
							MarkEdited(false);
							_needsPropertyRebuild = true;
						}
					};
				}
				box.resolveTarget = [sound]() {
					EditorPingTarget target;
					target.asset = sound;
					return target;
				};
				ObjectField::Build(context, _properties, box);

				UI::Widgets::NumberFieldDesc volume;
				volume.value = static_cast<f64>(AnimationImpl::KeyValue(track, key).x);
				volume.label = "Volume";
				volume.font = UISystem::GetDefaultFont();
				volume.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
				volume.onChange = [this](f64 typed) {
					AnimationClip* live = Clip();
					if (!live || _pickTrack < 0 || static_cast<size_t>(_pickTrack) >= live->tracks.size()) {
						return;
					}
					Vec4 edited = AnimationImpl::KeyValue(live->tracks[static_cast<size_t>(_pickTrack)], static_cast<size_t>(_pickKey));
					edited.x = Math::Max(0.0f, static_cast<f32>(typed));
					if (AnimationClipOps::SetKeyValue(*live, _pickTrack, _pickKey, edited)) {
						MarkEdited(false);
					}
				};
				UI::Widgets::NumberField(context, _properties, volume);
				return;
			}

			const Vec4 value = AnimationImpl::KeyValue(track, static_cast<size_t>(_pickKey));
			const u32 channels = track.component == ANIMATION_WHOLE_VALUE ? 4u : 1u;
			constexpr std::string_view ChannelLabels[] = {"X", "Y", "Z", "W"};
			for (u32 c = 0; c < channels; ++c) {
				UI::Widgets::NumberFieldDesc field;
				field.value = static_cast<f64>(channels == 1 ? value.x : (&value.x)[c]);
				field.label = channels == 1 ? std::string_view("Value") : ChannelLabels[c];
				field.font = UISystem::GetDefaultFont();
				field.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
				field.onChange = [this, c, channels](f64 typed) {
					AnimationClip* live = Clip();
					if (!live || _pickTrack < 0 || static_cast<size_t>(_pickTrack) >= live->tracks.size()) {
						return;
					}
					Vec4 edited = AnimationImpl::KeyValue(live->tracks[static_cast<size_t>(_pickTrack)],
														  static_cast<size_t>(_pickKey));
					(&edited.x)[channels == 1 ? 0u : c] = static_cast<f32>(typed);
					if (AnimationClipOps::SetKeyValue(*live, _pickTrack, _pickKey, edited)) {
						MarkEdited(false);
					}
				};
				UI::Widgets::NumberField(context, _properties, field);
			}

			if (AnimationValuesPerKey(track.interp) == 3u) {
				UI::Widgets::NumberFieldDesc tangent;
				tangent.value = static_cast<f64>(AnimationImpl::KeyOutTangent(track, static_cast<size_t>(_pickKey)).x);
				tangent.label = "Tangent";
				tangent.font = UISystem::GetDefaultFont();
				tangent.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
				tangent.onChange = [this](f64 typed) {
					AnimationClip* live = Clip();
					if (!live) {
						return;
					}
					const Vec4 slope{static_cast<f32>(typed), 0.0f, 0.0f, 0.0f};
					if (AnimationClipOps::SetKeyTangents(*live, _pickTrack, _pickKey, slope, slope)) {
						MarkEdited(false);
					}
				};
				UI::Widgets::NumberField(context, _properties, tangent);

				UI::Widgets::ButtonDesc autoTangent;
				autoTangent.label = "Auto";
				autoTangent.font = UISystem::GetDefaultFont();
				autoTangent.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
				autoTangent.onClick = [this]() {
					AnimationClip* live = Clip();
					if (!live || _pickTrack < 0 || static_cast<size_t>(_pickTrack) >= live->tracks.size()) {
						return;
					}
					const Vec4 slope = AnimationClipOps::AutoTangent(live->tracks[static_cast<size_t>(_pickTrack)],
																	 _pickKey);
					if (AnimationClipOps::SetKeyTangents(*live, _pickTrack, _pickKey, slope, slope)) {
						MarkEdited(false);
						_needsPropertyRebuild = true;
					}
				};
				UI::Widgets::Button(context, _properties, autoTangent);
			}
		}

		void AnimationCurvePanel::RefreshTitle(UI::Context& context) {
			if (!context.IsAlive(_title)) {
				return;
			}
			const AnimationClip* clip = Clip();
			str label = str("no clip selected");
			if (clip) {
				std::string_view path = clip->virtualPath;
				const size_t slash = path.find_last_of('/');
				label = str(slash == std::string_view::npos ? path : path.substr(slash + 1));
				if (_dirty) {
					label += " *";
				}
			}
			context.SetText(_title, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontBody),
							EditorTheme::GetColor(ColorToken::TextStrong), label);

			if (context.IsAlive(_targetLabel)) {
				const SceneObject* target = _target.Get();
				const str targetText = target ? std::format("target: {}", target->name)
											  : str("target: select an object");
				context.SetText(_targetLabel, UISystem::GetDefaultFont(),
								EditorTheme::GetMetric(MetricToken::FontSmall),
								EditorTheme::GetColor(ColorToken::WidgetTextDim), targetText);
			}
		}

		void AnimationCurvePanel::PaintSheet(UI::Context& context, UI::ElementHandle element, UI::PaintScope& paint) {
			const UI::Rect content = context.GetLayout(element).content;
			const f32 scale = context.GetContentScale();
			const auto toPaintX = [&](f32 localX) { return content.x + localX * scale; };
			const auto toPaintY = [&](f32 localY) { return content.y + localY * scale; };
			const f32 widthLocal = scale > 0.0f ? content.w / scale : content.w;
			const f32 heightLocal = scale > 0.0f ? content.h / scale : content.h;

			const AnimationClip* clip = Clip();
			if (!clip) {
				return;
			}

			const Grid grid = GridFor();
			if (grid.minor > 0.0f && grid.minor * _pixelsPerSecond >= 1.0f) {
				const f32 first = std::floor(_timeOrigin / grid.minor) * grid.minor;
				constexpr u32 MAX_RULES = 512;
				for (u32 n = 0; n < MAX_RULES; ++n) {
					const f32 time = first + static_cast<f32>(n) * grid.minor;
					const f32 x = TimeToLocal(time);
					if (x > widthLocal) {
						break;
					}
					if (x < 0.0f) {
						continue;
					}
					const f32 fromMajor = std::fabs(time / grid.major - std::round(time / grid.major));
					const bool major = fromMajor < 1e-3f;
					paint.EmitLine(Vec2(toPaintX(x), toPaintY(0.0f)), Vec2(toPaintX(x), toPaintY(heightLocal)),
								   1.0f, EditorTheme::GetColor(major ? ColorToken::GraphGridMajor : ColorToken::GraphGrid));
				}
			}

			if (_mode == SheetMode::Curves) {
				PaintCurves(context, paint, content, scale, widthLocal, heightLocal);
				const f32 headCurve = TimeToLocal(_playhead);
				if (headCurve >= 0.0f && headCurve <= widthLocal) {
					paint.EmitLine(Vec2(toPaintX(headCurve), toPaintY(0.0f)),
								   Vec2(toPaintX(headCurve), toPaintY(heightLocal)), 1.0f * scale,
								   EditorTheme::GetColor(ColorToken::Accent));
				}
				return;
			}

			for (size_t i = 0; i < clip->tracks.size(); ++i) {
				const f32 top = RowTop(static_cast<i32>(i));
				if (top > heightLocal) {
					break;
				}
				if (static_cast<i32>(i) != _pickTrack) {
					continue;
				}
				paint.EmitQuad(UI::Rect{content.x, toPaintY(top), content.w, ROW_HEIGHT * scale},
							   EditorTheme::GetColor(ColorToken::SheetRow));
			}

			const Color keyColor = EditorTheme::GetColor(ColorToken::SheetKey);
			const Color pickColor = EditorTheme::GetColor(ColorToken::SelectionWire);
			const f32 radius = KEY_RADIUS * scale;
			for (size_t i = 0; i < clip->tracks.size(); ++i) {
				const f32 centreLocal = RowTop(static_cast<i32>(i)) + ROW_HEIGHT * 0.5f;
				if (centreLocal - KEY_RADIUS > heightLocal) {
					break;
				}
				const AnimationClip::Track& track = clip->tracks[i];
				const size_t count = AnimationImpl::KeyCount(track);
				for (size_t k = 0; k < count; ++k) {
					const f32 x = TimeToLocal(track.times[k]);
					if (x < -KEY_RADIUS || x > widthLocal + KEY_RADIUS) {
						continue;
					}
					const UI::Rect box{toPaintX(x) - radius, toPaintY(centreLocal) - radius, radius * 2.0f,
									   radius * 2.0f};
					const bool signalKey = track.kind == AnimationTrackKind::Event ||
										   track.kind == AnimationTrackKind::Method ||
										   track.kind == AnimationTrackKind::Audio;
					const UI::Radii round = signalKey ? UI::Radii{} : UI::Radii{radius, radius, radius, radius};
					paint.EmitQuad(box, keyColor, round);
					if (static_cast<i32>(i) == _pickTrack && static_cast<i32>(k) == _pickKey) {
						const UI::Rect ring{box.x - 2.0f * scale, box.y - 2.0f * scale, box.w + 4.0f * scale,
											box.h + 4.0f * scale};
						paint.EmitRing(ring, UI::Edges{1.5f * scale, 1.5f * scale, 1.5f * scale, 1.5f * scale},
									   pickColor,
									   UI::Radii{ring.w * 0.5f, ring.w * 0.5f, ring.w * 0.5f, ring.w * 0.5f});
					}
				}
			}

			const f32 head = TimeToLocal(_playhead);
			if (head >= 0.0f && head <= widthLocal) {
				paint.EmitLine(Vec2(toPaintX(head), toPaintY(0.0f)), Vec2(toPaintX(head), toPaintY(heightLocal)),
							   1.0f * scale, EditorTheme::GetColor(ColorToken::Accent));
			}
		}

		void AnimationCurvePanel::PaintCurves(UI::Context& context, UI::PaintScope& paint, const UI::Rect& content,
											  f32 scale, f32 widthLocal, f32 heightLocal) {
			(void)context;
			const AnimationClip* clip = Clip();
			if (!clip) {
				return;
			}
			const auto toPaint = [&](Vec2 localPoint) {
				return Vec2(content.x + localPoint.x * scale, content.y + localPoint.y * scale);
			};
			const Color dim = EditorTheme::GetColor(ColorToken::GraphGridMajor);
			const Color bright = EditorTheme::GetColor(ColorToken::SheetKey);
			const Color pick = EditorTheme::GetColor(ColorToken::SelectionWire);
			constexpr f32 STEP = 2.0f;

			for (size_t t = 0; t < clip->tracks.size(); ++t) {
				const AnimationClip::Track& curve = clip->tracks[t];
				const size_t count = AnimationImpl::KeyCount(curve);
				if (count == 0) {
					continue;
				}
				const bool selected = static_cast<i32>(t) == _pickTrack;
				const Color ink = selected ? bright : dim;
				Vec2 previous{};
				bool has = false;
				for (f32 x = 0.0f; x <= widthLocal; x += STEP) {
					const f32 time = LocalToTime(x);
					const f32 value = AnimationImpl::SampleTrack(curve, time).x;
					const Vec2 point(x, ValueToLocal(value, heightLocal));
					if (has) {
						paint.EmitLine(toPaint(previous), toPaint(point), (selected ? 1.8f : 1.0f) * scale, ink,
									   UI::LineCap::Round);
					}
					previous = point;
					has = true;
				}

				for (size_t k = 0; k < count; ++k) {
					const f32 keyX = TimeToLocal(curve.times[k]);
					if (keyX < -KEY_RADIUS || keyX > widthLocal + KEY_RADIUS) {
						continue;
					}
					const Vec2 at(keyX, ValueToLocal(AnimationImpl::KeyValue(curve, k).x, heightLocal));
					const f32 radius = KEY_RADIUS * scale;
					const Vec2 centre = toPaint(at);
					const UI::Rect box{centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f};
					paint.EmitQuad(box, ink, UI::Radii{radius, radius, radius, radius});
					if (!selected || static_cast<i32>(k) != _pickKey) {
						continue;
					}
					const UI::Rect ring{box.x - 2.0f * scale, box.y - 2.0f * scale, box.w + 4.0f * scale,
										box.h + 4.0f * scale};
					paint.EmitRing(ring, UI::Edges{1.5f * scale, 1.5f * scale, 1.5f * scale, 1.5f * scale}, pick,
								   UI::Radii{ring.w * 0.5f, ring.w * 0.5f, ring.w * 0.5f, ring.w * 0.5f});

					if (AnimationValuesPerKey(curve.interp) != 3u) {
						continue;
					}
					const Vec2 inAt = at + HandleOffset(AnimationImpl::KeyInTangent(curve, k).x, false, heightLocal);
					const Vec2 outAt = at + HandleOffset(AnimationImpl::KeyOutTangent(curve, k).x, true, heightLocal);
					for (const Vec2& handle : {inAt, outAt}) {
						paint.EmitLine(centre, toPaint(handle), 1.0f * scale, pick);
						const Vec2 knob = toPaint(handle);
						const f32 knobRadius = 3.0f * scale;
						paint.EmitQuad(UI::Rect{knob.x - knobRadius, knob.y - knobRadius, knobRadius * 2.0f,
												knobRadius * 2.0f},
									   pick, UI::Radii{knobRadius, knobRadius, knobRadius, knobRadius});
					}
				}
			}
		}

		void AnimationCurvePanel::AddPropertyMenu(UI::Context& context) {
			AnimationClip* clip = Clip();
			SceneObject* target = _target.Get();
			Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};
			if (!clip) {
				return;
			}
			if (!target) {
				UI::Widgets::MenuItem none;
				none.label = "Select an object in the Hierarchy";
				none.enabled = false;
				items.push_back(std::move(none));
				UI::Widgets::OpenMenu(context, context.GetPointerPosition(), items, UISystem::GetDefaultFont(),
									  EditorTheme::GetMetric(MetricToken::FontSmall));
				return;
			}

			const auto fill = [this](const TypeInfo* type, StringHash componentType,
									 Vector<UI::Widgets::MenuItem>& into) {
				for (const TypeInfo* walk = type; walk; walk = walk->parent) {
					for (u32 f = 0; f < walk->fieldCount; ++f) {
						const FieldInfo& field = walk->fields[f];
						const u32 scalars = AnimationImpl::ScalarCount(field.fieldType);
						if (scalars == 0 || (field.flags & FieldFlagEditorVisible) == 0) {
							continue;
						}
						UI::Widgets::MenuItem row;
						row.label = PropertyRows::FieldLabel(field);
						const str name = str(FieldName(field));
						row.onSelect = [this, componentType, name, scalars]() {
							AnimationClip* live = Clip();
							if (!live) {
								return;
							}
							i32 first = -1;
							for (u32 c = 0; c < scalars; ++c) {
								AnimationClipOps::TrackAddress address;
								address.kind = AnimationTrackKind::Property;
								address.objectPath = SelfPath;
								address.componentType = componentType;
								address.name = name;
								address.component = static_cast<u8>(c);
								const i32 track = AnimationClipOps::AddTrack(*live, address,
																			 AnimationInterpolation::Linear);
								if (first < 0) {
									first = track;
								}
							}
							MarkEdited(true);
							Select(first, -1);
						};
						into.push_back(std::move(row));
					}
				}
			};

			UI::Widgets::MenuItem event;
			event.label = "Event";
			event.onSelect = [this]() {
				AnimationClip* live = Clip();
				if (!live) {
					return;
				}
				AnimationClipOps::TrackAddress address;
				address.kind = AnimationTrackKind::Event;
				address.objectPath = SelfPath;
				const str minted = UniqueName(*live, AnimationTrackKind::Event, StringHash(), "New Event");
				address.name = minted;
				address.component = ANIMATION_WHOLE_VALUE;
				const i32 track = AnimationClipOps::AddTrack(*live, address, AnimationInterpolation::Step);
				MarkEdited(true);
				Select(track, -1);
			};
			items.push_back(std::move(event));

			UI::Widgets::MenuItem audio;
			audio.label = "Audio";
			audio.onSelect = [this]() {
				AnimationClip* live = Clip();
				if (!live) {
					return;
				}
				AnimationClipOps::TrackAddress address;
				address.kind = AnimationTrackKind::Audio;
				address.objectPath = SelfPath;
				const str minted = UniqueName(*live, AnimationTrackKind::Audio, StringHash(), "Audio");
				address.name = minted;
				address.component = ANIMATION_WHOLE_VALUE;
				const i32 track = AnimationClipOps::AddTrack(*live, address, AnimationInterpolation::Step);
				MarkEdited(true);
				Select(track, -1);
			};
			items.push_back(std::move(audio));

			UI::Widgets::MenuItem method;
			method.label = "Method";
			{
				const auto addMethodRow = [this](const TypeInfo* type, StringHash componentType,
												 Vector<UI::Widgets::MenuItem>& into) {
					Vector<std::string_view> callable{GetMemoryResource(MemTag::Editor)};
					GatherCallableMethods(type, callable);

					UI::Widgets::MenuItem row;
					row.label = str(TypeLabel(type));
					for (std::string_view methodName : callable) {
						UI::Widgets::MenuItem pick;
						pick.label = str(methodName);
						pick.onSelect = [this, componentType, name = str(methodName)]() {
							AnimationClip* live = Clip();
							if (!live) {
								return;
							}
							AnimationClipOps::TrackAddress address;
							address.kind = AnimationTrackKind::Method;
							address.objectPath = SelfPath;
							address.componentType = componentType;
							address.name = name;
							address.component = ANIMATION_WHOLE_VALUE;
							const i32 track = AnimationClipOps::AddTrack(*live, address,
																		 AnimationInterpolation::Step);
							MarkEdited(true);
							Select(track, -1);
						};
						row.children.push_back(std::move(pick));
					}
					if (!row.children.empty()) {
						into.push_back(std::move(row));
					}
				};
				addMethodRow(SceneObject::GetReflectType(), StringHash(), method.children);
				for (u32 c = 0; c < target->GetComponentCount(); ++c) {
					Component* component = target->GetComponentAt(c).Get();
					if (component && component->GetType()) {
						addMethodRow(component->GetType(), component->GetType()->name, method.children);
					}
				}
				if (method.children.empty()) {
					UI::Widgets::MenuItem none;
					none.label = "No component reflects a callable method";
					none.enabled = false;
					method.children.push_back(std::move(none));
				}
			}
			items.push_back(std::move(method));

			UI::Widgets::MenuItem self;
			self.label = "Transform";
			fill(SceneObject::GetReflectType(), StringHash(), self.children);
			if (!self.children.empty()) {
				items.push_back(std::move(self));
			}

			for (u32 c = 0; c < target->GetComponentCount(); ++c) {
				Component* component = target->GetComponentAt(c).Get();
				if (!component) {
					continue;
				}
				const TypeInfo* type = component->GetType();
				if (!type) {
					continue;
				}
				UI::Widgets::MenuItem entry;
				entry.label = str(TypeLabel(type));
				fill(type, type->name, entry.children);
				if (!entry.children.empty()) {
					items.push_back(std::move(entry));
				}
			}

			if (items.empty()) {
				UI::Widgets::MenuItem none;
				none.label = "Nothing on this object is animatable";
				none.enabled = false;
				items.push_back(std::move(none));
			}
			UI::Widgets::OpenMenu(context, context.GetPointerPosition(), items, UISystem::GetDefaultFont(),
								  EditorTheme::GetMetric(MetricToken::FontSmall));
		}

		void AnimationCurvePanel::TrackMenu(UI::Context& context, i32 track) {
			AnimationClip* clip = Clip();
			if (!clip || track < 0 || static_cast<size_t>(track) >= clip->tracks.size()) {
				return;
			}
			Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::Editor)};

			UI::Widgets::MenuItem addKey;
			addKey.label = "Add Key";
			const f32 at = _playhead;
			addKey.onSelect = [this, track, at]() {
				AnimationClip* live = Clip();
				if (!live) {
					return;
				}
				const i32 key = AnimationClipOps::AddKey(*live, track, at);
				if (key >= 0) {
					MarkEdited(false);
					Select(track, key);
				}
			};
			items.push_back(std::move(addKey));

			UI::Widgets::MenuItem remove;
			remove.label = "Remove Property";
			remove.onSelect = [this, track]() {
				AnimationClip* live = Clip();
				if (!live || !AnimationClipOps::RemoveTrack(*live, track)) {
					return;
				}
				Select(-1, -1);
				MarkEdited(true);
			};
			items.push_back(std::move(remove));

			UI::Widgets::OpenMenu(context, context.GetPointerPosition(), items, UISystem::GetDefaultFont(),
								  EditorTheme::GetMetric(MetricToken::FontSmall));
		}

		void AnimationCurvePanel::DeleteSelection() {
			AnimationClip* clip = Clip();
			if (!clip || _pickTrack < 0) {
				return;
			}
			if (_pickKey >= 0) {
				if (AnimationClipOps::RemoveKey(*clip, _pickTrack, _pickKey)) {
					Select(_pickTrack, -1);
					MarkEdited(false);
				}
				return;
			}
			if (AnimationClipOps::RemoveTrack(*clip, _pickTrack)) {
				Select(-1, -1);
				MarkEdited(true);
			}
		}

		bool AnimationCurvePanel::OnCommand(PanelCommand command) {
			if (command != PanelCommand::Delete) {
				return EditorPanel::OnCommand(command);
			}
			if (_pickTrack < 0) {
				return false;
			}
			DeleteSelection();
			return true;
		}

		bool AnimationCurvePanel::FrameAll(UI::Context& context) {
			const AnimationClip* clip = Clip();
			if (!clip || !context.IsAlive(_sheet)) {
				return false;
			}
			const f32 scale = context.GetContentScale();
			const f32 width = context.GetLayout(_sheet).content.w / (scale > 0.0f ? scale : 1.0f);
			if (!(width > 1.0f)) {
				return false;
			}
			const f32 span = Math::Max(clip->length, 1.0f);
			_pixelsPerSecond = Math::Clamp(width * 0.9f / span, MIN_PIXELS_PER_SECOND, MAX_PIXELS_PER_SECOND);
			_timeOrigin = -width * 0.05f / _pixelsPerSecond;
			_needsRulerRebuild = true;
			return true;
		}

		void AnimationCurvePanel::OnTick(f32 deltaSeconds) {
			(void)deltaSeconds;
			FollowSelection();

			UI::Context* context = GetPanelContext();
			if (!context) {
				return;
			}
			if (_needsTrackRebuild) {
				RebuildTracks(*context);
				_needsPropertyRebuild = true;
			}
			if (_needsFrame && FrameAll(*context)) {
				_needsFrame = false;
			}
			if (_needsValueFit) {
				FitValueRange();
			}
			if (_needsRulerRebuild) {
				RebuildRuler(*context);
			}
			if (_needsPropertyRebuild) {
				RebuildProperties(*context);
			}
			RefreshTitle(*context);
		}

	} // namespace AnimationEditorImpl

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::AnimationEditorImpl::AnimationCurvePanel)
