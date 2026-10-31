#include "Shalltear/Editor/CurveEditor.h"

#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/AnimationCurveOps.h"
#include "Shalltear/Editor/EditorTheme.h"

#include <algorithm>
#include <limits>
#include <memory>

namespace Shalltear::CurveEditor {

	namespace {

		using AnimationCurveOps::KeySide;
		using ColorToken = EditorTheme::ColorToken;
		using MetricToken = EditorTheme::MetricToken;

		constexpr f32 KEY_SIZE = 7.0f;
		constexpr f32 KEY_PICK = 7.0f;
		constexpr f32 HANDLE_LENGTH = 36.0f;
		constexpr f32 HANDLE_PICK = 6.0f;
		constexpr f32 CURVE_PICK = 6.0f;
		constexpr u32 FRAME_SAMPLES = 64;
		constexpr f32 FRAME_PADDING = 0.1f;
		constexpr std::string_view WRAP_MODES[] = {"Clamp", "Loop", "Ping Pong"};

		enum class Grab : u8 {
			None,
			Key,
			In,
			Out,
		};

		struct Plot {
			UI::Rect box;
			View view;
			f32 scale = 1.0f;

			f32 X(f32 time) const { return box.x + (time - view.timeMin) / (view.timeMax - view.timeMin) * box.w; }
			f32 Y(f32 value) const { return box.y + (1.0f - (value - view.valueMin) / (view.valueMax - view.valueMin)) * box.h; }
			f32 Time(f32 px) const { return view.timeMin + (px - box.x) / box.w * (view.timeMax - view.timeMin); }
			f32 Value(f32 py) const { return view.valueMin + (1.0f - (py - box.y) / box.h) * (view.valueMax - view.valueMin); }
			Vec2 At(const Keyframe& key) const { return Vec2(X(key.time), Y(key.value)); }
			f32 Across() const { return box.w / (view.timeMax - view.timeMin); }
			f32 Up() const { return box.h / (view.valueMax - view.valueMin); }

			Vec2 Handle(f32 slope, bool outgoing) const {
				Vec2 direction(1.0f, -slope * Up() / Across());
				const f32 length = direction.Length();
				direction = length > 0.0f ? direction * (1.0f / length) : Vec2(1.0f, 0.0f);
				const f32 reach = HANDLE_LENGTH * scale * (outgoing ? 1.0f : -1.0f);
				return Vec2(direction.x * reach, direction.y * reach);
			}
		};

		struct State {
			AnimationCurve curve{GetMemoryResource(MemTag::Editor)};
			View view;
			UI::ElementHandle plot;
			i32 key = -1;
			Grab grab = Grab::None;
			bool editing = false;
			std::function<void(const AnimationCurve&)> onChange;
			std::function<void()> onEditBegin;
			std::function<void()> onEditEnd;
		};

		Plot PlotOf(UI::Context& context, const State& state) {
			Plot plot;
			plot.view = state.view;
			plot.scale = context.GetContentScale() > 0.0f ? context.GetContentScale() : 1.0f;
			if (context.IsAlive(state.plot)) {
				plot.box = context.GetLayout(state.plot).borderBox;
			}
			plot.box.w = Math::Max(1.0f, plot.box.w);
			plot.box.h = Math::Max(1.0f, plot.box.h);
			return plot;
		}

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
				state.onChange(state.curve);
			}
		}

		template <typename Edit>
		void Once(State& state, Edit&& edit) {
			Begin(state);
			edit(state.curve);
			Publish(state);
			End(state);
		}

		Grab Pick(const State& state, const Plot& plot, Vec2 point, i32& key) {
			const auto& keys = state.curve.keys;
			if (state.key >= 0 && static_cast<size_t>(state.key) < keys.size()) {
				const Keyframe& selected = keys[static_cast<size_t>(state.key)];
				const Vec2 at = plot.At(selected);
				if (selected.inTangentMode != KeyframeTangentMode::Constant && (point - (at + plot.Handle(selected.inTangent, false))).Length() <= HANDLE_PICK * plot.scale) {
					key = state.key;
					return Grab::In;
				}
				if (selected.outTangentMode != KeyframeTangentMode::Constant && (point - (at + plot.Handle(selected.outTangent, true))).Length() <= HANDLE_PICK * plot.scale) {
					key = state.key;
					return Grab::Out;
				}
			}
			f32 best = KEY_PICK * plot.scale;
			Grab found = Grab::None;
			for (size_t k = 0; k < keys.size(); ++k) {
				const f32 distance = (point - plot.At(keys[k])).Length();
				if (distance <= best) {
					best = distance;
					key = static_cast<i32>(k);
					found = Grab::Key;
				}
			}
			return found;
		}

		bool OnCurve(const State& state, const Plot& plot, Vec2 point) {
			return Math::Abs(plot.Y(state.curve.Evaluate(plot.Time(point.x))) - point.y) <= CURVE_PICK * plot.scale;
		}

		void AddKeyAt(State& state, const Plot& plot, Vec2 point) {
			if (!OnCurve(state, plot, point)) {
				return;
			}
			const f32 time = plot.Time(point.x);
			Once(state, [&](AnimationCurve& curve) { state.key = AnimationCurveOps::AddKey(curve, time, curve.Evaluate(time)); });
		}

		void DeleteSelected(State& state) {
			if (state.key < 0) {
				return;
			}
			const i32 key = state.key;
			Once(state, [key](AnimationCurve& curve) { AnimationCurveOps::RemoveKey(curve, key); });
			state.key = -1;
		}

		void OpenKeyMenu(UI::Context& context, const std::shared_ptr<State>& shared, Vec2 point) {
			State& state = *shared;
			const Plot plot = PlotOf(context, state);
			i32 key = -1;
			const bool onKey = Pick(state, plot, point, key) == Grab::Key;
			const std::weak_ptr<State> weak = shared;
			Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::UI)};
			if (!onKey) {
				UI::Widgets::MenuItem add;
				add.label = "Add Key";
				add.enabled = OnCurve(state, plot, point);
				add.onSelect = [weak, plot, point]() {
					if (std::shared_ptr<State> live = weak.lock()) {
						AddKeyAt(*live, plot, point);
					}
				};
				items.push_back(std::move(add));
			} else {
				state.key = key;
				const auto item = [&](std::string_view label, bool checked, std::function<void(AnimationCurve&, i32)> edit, bool separator = false) {
					UI::Widgets::MenuItem row;
					row.label = str(label);
					row.checked = checked;
					row.separatorBefore = separator;
					row.onSelect = [weak, edit, key]() {
						if (std::shared_ptr<State> live = weak.lock()) {
							live->key = key;
							Once(*live, [&edit, key](AnimationCurve& c) { edit(c, key); });
						}
					};
					return row;
				};
				UI::Widgets::MenuItem remove;
				remove.label = "Delete Key";
				remove.onSelect = [weak, key]() {
					if (std::shared_ptr<State> live = weak.lock()) {
						live->key = key;
						DeleteSelected(*live);
					}
				};
				items.push_back(std::move(remove));
				const Keyframe& k = state.curve.keys[static_cast<size_t>(key)];
				const auto both = [&k](KeyframeTangentMode mode) { return k.inTangentMode == mode && k.outTangentMode == mode; };
				const bool freeSmooth = !k.broken && both(KeyframeTangentMode::Free);
				items.push_back(item("Clamped Auto", both(KeyframeTangentMode::ClampedAuto), [](AnimationCurve& c, i32 i) { AnimationCurveOps::SetSmooth(c, i, KeyframeTangentMode::ClampedAuto); }, true));
				items.push_back(item("Auto", both(KeyframeTangentMode::Auto), [](AnimationCurve& c, i32 i) { AnimationCurveOps::SetSmooth(c, i, KeyframeTangentMode::Auto); }));
				items.push_back(item("Free Smooth", freeSmooth, [](AnimationCurve& c, i32 i) { AnimationCurveOps::SetSmooth(c, i, KeyframeTangentMode::Free); }));
				items.push_back(item("Flat", freeSmooth && k.inTangent == 0.0f && k.outTangent == 0.0f, [](AnimationCurve& c, i32 i) { AnimationCurveOps::Flatten(c, i); }));
				items.push_back(item("Broken", k.broken, [](AnimationCurve& c, i32 i) { AnimationCurveOps::SetBroken(c, i, true); }));
				const auto side = [&](std::string_view label, KeySide which, bool separator) {
					UI::Widgets::MenuItem parent;
					parent.label = str(label);
					parent.separatorBefore = separator;
					const auto on = [&k, which](KeyframeTangentMode mode) {
						return k.broken && (which == KeySide::Out || k.inTangentMode == mode) && (which == KeySide::In || k.outTangentMode == mode);
					};
					for (const KeyframeTangentMode mode : {KeyframeTangentMode::Free, KeyframeTangentMode::Linear, KeyframeTangentMode::Constant}) {
						const std::string_view name = mode == KeyframeTangentMode::Free ? "Free" : mode == KeyframeTangentMode::Linear ? "Linear"
																																	   : "Constant";
						parent.children.push_back(item(name, on(mode), [which, mode](AnimationCurve& c, i32 i) { AnimationCurveOps::SetSideMode(c, i, which, mode); }));
					}
					return parent;
				};
				items.push_back(side("Left Tangent", KeySide::In, true));
				items.push_back(side("Right Tangent", KeySide::Out, false));
				items.push_back(side("Both Tangents", KeySide::Both, false));
			}
			UI::Widgets::OpenMenu(context, point, items, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::Font));
		}

		void Paint(UI::Context& context, UI::PaintScope& paint, const State& state) {
			const Plot plot = PlotOf(context, state);
			const f32 line = Math::Max(1.0f, plot.scale);
			const Color grid = EditorTheme::GetColor(ColorToken::GraphGrid);
			const Color major = EditorTheme::GetColor(ColorToken::GraphGridMajor);
			for (u32 step = 1; step < 4; ++step) {
				const f32 x = plot.box.x + plot.box.w * static_cast<f32>(step) * 0.25f;
				const f32 y = plot.box.y + plot.box.h * static_cast<f32>(step) * 0.25f;
				paint.EmitLine(Vec2(x, plot.box.y), Vec2(x, plot.box.y + plot.box.h), line, grid);
				paint.EmitLine(Vec2(plot.box.x, y), Vec2(plot.box.x + plot.box.w, y), line, grid);
			}
			const auto& keys = state.curve.keys;
			if (!keys.empty()) {
				for (const f32 time : {keys.front().time, keys.back().time}) {
					const f32 x = plot.X(time);
					paint.EmitLine(Vec2(x, plot.box.y), Vec2(x, plot.box.y + plot.box.h), line, major);
				}
			}
			const Color ink = EditorTheme::GetColor(ColorToken::CurveLine);
			const f32 stride = 2.0f * plot.scale;
			Vec2 previous(plot.box.x, plot.Y(state.curve.Evaluate(plot.Time(plot.box.x))));
			for (f32 x = plot.box.x + stride; x <= plot.box.x + plot.box.w + 0.5f * stride; x += stride) {
				const f32 clamped = Math::Min(x, plot.box.x + plot.box.w);
				const Vec2 next(clamped, plot.Y(state.curve.Evaluate(plot.Time(clamped))));
				paint.EmitLine(previous, next, line * 1.5f, ink, UI::LineCap::Round);
				previous = next;
			}
			const f32 half = KEY_SIZE * 0.5f * plot.scale;
			for (const Keyframe& key : keys) {
				const Vec2 at = plot.At(key);
				paint.EmitQuad({at.x - half, at.y - half, half * 2.0f, half * 2.0f}, ink, UI::Radii::All(half));
			}
			if (state.key >= 0 && static_cast<size_t>(state.key) < keys.size()) {
				const Keyframe& key = keys[static_cast<size_t>(state.key)];
				const Vec2 at = plot.At(key);
				const Color text = EditorTheme::GetColor(ColorToken::Text);
				const f32 ring = KEY_SIZE * plot.scale;
				paint.EmitRing({at.x - ring, at.y - ring, ring * 2.0f, ring * 2.0f}, UI::Edges::All(line), text, UI::Radii::All(ring));
				const f32 dot = KEY_SIZE * 0.4f * plot.scale;
				const auto handle = [&](f32 slope, bool outgoing) {
					const Vec2 end = at + plot.Handle(slope, outgoing);
					paint.EmitLine(at, end, line, text);
					paint.EmitQuad({end.x - dot, end.y - dot, dot * 2.0f, dot * 2.0f}, text, UI::Radii::All(dot));
				};
				if (key.inTangentMode != KeyframeTangentMode::Constant) {
					handle(key.inTangent, false);
				}
				if (key.outTangentMode != KeyframeTangentMode::Constant) {
					handle(key.outTangent, true);
				}
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

	} // anonymous namespace

	View Frame(const AnimationCurve& curve) {
		View view;
		if (curve.keys.empty()) {
			return view;
		}
		view.timeMin = curve.keys.front().time;
		view.timeMax = curve.keys.back().time;
		if (!(view.timeMax - view.timeMin > 1e-4f)) {
			view.timeMin -= 0.5f;
			view.timeMax += 0.5f;
		}
		view.valueMin = std::numeric_limits<f32>::max();
		view.valueMax = std::numeric_limits<f32>::lowest();
		for (u32 sample = 0; sample <= FRAME_SAMPLES; ++sample) {
			const f32 value = curve.Evaluate(view.timeMin + (view.timeMax - view.timeMin) * static_cast<f32>(sample) / static_cast<f32>(FRAME_SAMPLES));
			view.valueMin = Math::Min(view.valueMin, value);
			view.valueMax = Math::Max(view.valueMax, value);
		}
		for (const Keyframe& key : curve.keys) {
			view.valueMin = Math::Min(view.valueMin, key.value);
			view.valueMax = Math::Max(view.valueMax, key.value);
		}
		if (!(view.valueMax - view.valueMin > 1e-4f)) {
			view.valueMin -= 0.5f;
			view.valueMax += 0.5f;
		}
		const f32 across = (view.timeMax - view.timeMin) * FRAME_PADDING;
		const f32 up = (view.valueMax - view.valueMin) * FRAME_PADDING;
		view.timeMin -= across;
		view.timeMax += across;
		view.valueMin -= up;
		view.valueMax += up;
		return view;
	}

	UI::ElementHandle Open(UI::Context& context, UI::ElementHandle anchor, const AnimationCurve& curve, std::function<void(const AnimationCurve&)> onChange,
						   std::function<void()> onEditBegin, std::function<void()> onEditEnd) {
		UI::Context* contextPtr = &context;
		auto state = std::make_shared<State>();
		state->curve = curve;
		state->view = Frame(curve);
		state->onChange = std::move(onChange);
		state->onEditBegin = std::move(onEditBegin);
		state->onEditEnd = std::move(onEditEnd);

		UI::ElementHandle content = context.CreateElement();
		context.AddClass(content, EditorTheme::Class::CurveEditor);
		context.Events(content).onPopupClosed = [contextPtr, content, state]() {
			End(*state);
			contextPtr->DestroyElementNextUpdate(content);
		};

		UI::ElementHandle plot = context.CreateElement();
		context.AppendChild(content, plot);
		context.AddClass(plot, EditorTheme::Class::CurveEditorPlot);
		context.AddClass(plot, EditorTheme::Class::Clickable);
		context.SetFocusable(plot, true);
		UI::Style plotStyle;
		plotStyle.width = UI::Length::Pixel(EditorTheme::GetMetric(MetricToken::CurveEditorWidth));
		plotStyle.height = UI::Length::Pixel(EditorTheme::GetMetric(MetricToken::CurveEditorHeight));
		plotStyle.overflow = UI::Overflow::Hidden;
		context.SetStyle(plot, plotStyle);
		state->plot = plot;
		context.Events(plot).customDraw = [state](UI::Context& c, UI::ElementHandle, UI::PaintScope& paint) { Paint(c, paint, *state); };

		context.Events(plot).onMouseDown = [state, plot, contextPtr]() {
			contextPtr->SetFocus(plot);
			i32 key = -1;
			state->grab = Pick(*state, PlotOf(*contextPtr, *state), contextPtr->GetPointerPosition(), key);
			state->key = state->grab == Grab::None ? -1 : key;
		};
		context.Events(plot).onPressDrag = [state, contextPtr](Vec2 point) {
			if (state->grab == Grab::None || state->key < 0 || static_cast<size_t>(state->key) >= state->curve.keys.size()) {
				return;
			}
			const Plot area = PlotOf(*contextPtr, *state);
			if (state->grab == Grab::Key) {
				Begin(*state);
				state->key = AnimationCurveOps::MoveKey(state->curve, state->key, area.Time(point.x), area.Value(point.y));
			} else {
				const Vec2 delta = point - area.At(state->curve.keys[static_cast<size_t>(state->key)]);
				const bool out = state->grab == Grab::Out;
				const f32 run = out ? delta.x : -delta.x;
				if (!(Math::Abs(run) > area.scale)) {
					return;
				}
				const f32 rise = out ? -delta.y : delta.y;
				Begin(*state);
				AnimationCurveOps::SetTangent(state->curve, state->key, out ? KeySide::Out : KeySide::In, (rise / area.Up()) / (run / area.Across()));
			}
			Publish(*state);
		};
		context.Events(plot).onPressEnd = [state]() {
			state->grab = Grab::None;
			End(*state);
		};
		context.Events(plot).onDoubleClick = [state, contextPtr]() { AddKeyAt(*state, PlotOf(*contextPtr, *state), contextPtr->GetPointerPosition()); };
		context.Events(plot).onRightClick = [state, contextPtr]() { OpenKeyMenu(*contextPtr, state, contextPtr->GetPointerPosition()); };
		context.Events(plot).onKeyDown = [state](Key key, KeyMod mods) {
			if ((key == Key::Delete || key == Key::Backspace) && mods == KeyMod::None && state->key >= 0) {
				DeleteSelected(*state);
				return true;
			}
			if (key == Key::F && mods == KeyMod::None) {
				state->view = Frame(state->curve);
				return true;
			}
			return false;
		};

		UI::ElementHandle wraps = context.CreateElement();
		context.AppendChild(content, wraps);
		UI::Style wrapsStyle;
		wrapsStyle.flexDirection = UI::FlexDirection::Row;
		wrapsStyle.alignItems = UI::Align::Center;
		wrapsStyle.justifyContent = UI::Align::SpaceBetween;
		wrapsStyle.margin = {0.0f, 0.0f, 4.0f, 0.0f};
		context.SetStyle(wraps, wrapsStyle);
		for (const bool post : {false, true}) {
			UI::ElementHandle side = context.CreateElement();
			context.AppendChild(wraps, side);
			UI::Style sideStyle;
			sideStyle.flexDirection = UI::FlexDirection::Row;
			sideStyle.alignItems = UI::Align::Center;
			context.SetStyle(side, sideStyle);
			AddLabel(context, side, post ? "Post Wrap" : "Pre Wrap");
			UI::Widgets::DropdownDesc desc;
			desc.items = WRAP_MODES;
			desc.selected = static_cast<u32>(post ? curve.postWrapMode : curve.preWrapMode);
			desc.font = UISystem::GetDefaultFont();
			desc.fontSizePx = EditorTheme::GetMetric(MetricToken::FontSmall);
			desc.onSelect = [state, post](u32 index) {
				const AnimationCurveWrapMode mode = static_cast<AnimationCurveWrapMode>(Math::Min<u32>(index, 2u));
				Once(*state, [post, mode](AnimationCurve& c) { (post ? c.postWrapMode : c.preWrapMode) = mode; });
			};
			UI::Widgets::Dropdown(context, side, desc);
		}

		const UI::Rect& box = context.GetLayout(anchor).borderBox;
		context.OpenPopup(content, {box.x, box.y + box.h});
		return content;
	}

} // namespace Shalltear::CurveEditor
