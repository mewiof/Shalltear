#include "AudioEditor.h"

#include "AudioPlacement.h"
#include "AudioScenePass.h"
#include "AudioSettings.h"

#include "Shalltear/Audio/AudioFilters.h"
#include "Shalltear/Audio/AudioSource.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math/AnimationCurve.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Reflection/Walk.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/AnimationCurveOps.h"
#include "Shalltear/Editor/EditorCommands.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PropertyDrawers.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <array>
#include <cmath>
#include <format>
#include <functional>
#include <memory>

namespace Shalltear::AudioEditor {

	namespace {

		using AnimationCurveOps::KeySide;
		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;

		enum class GraphCurve : u8 {
			Volume,
			SpatialBlend,
			Spread,
			LowPass,
			ReverbZoneMix,
		};
		constexpr u32 CURVE_COUNT = 5;

		struct CurveFacts {
			std::string_view label;
			ColorToken color;
			f32 top;
		};
		constexpr std::array<CurveFacts, CURVE_COUNT> CURVES{{
			{"Volume", ColorToken::AudioCurveVolume, 1.0f},
			{"Spatial Blend", ColorToken::AudioCurveSpatialBlend, 1.0f},
			{"Spread", ColorToken::AudioCurveSpread, 1.0f},
			{"Low-Pass", ColorToken::AudioCurveLowPass, 1.0f},
			{"Reverb Zone Mix", ColorToken::AudioCurveReverbZoneMix, 1.1f},
		}};

		constexpr f32 VALUE_TOP = 1.1f;
		constexpr f32 CUTOFF_LOW = 10.0f;
		constexpr f32 CUTOFF_HIGH = 22000.0f;

		constexpr f32 KEY_PICK = 7.0f;
		constexpr f32 HANDLE_PICK = 7.0f;
		constexpr f32 CURVE_PICK = 6.0f;
		constexpr f32 HANDLE_LENGTH = 32.0f;
		constexpr f32 KEY_SIZE = 7.0f;
		constexpr f32 MARGIN_LEFT = 28.0f;
		constexpr f32 MARGIN_RIGHT = 10.0f;
		constexpr f32 MARGIN_TOP = 18.0f;
		constexpr f32 MARGIN_BOTTOM = 18.0f;
		constexpr f32 LEGEND_CELL = 75.0f;

		AudioSource* SourceOf(const EditTarget& target) {
			Object* object = target.Resolve();
			return object && TypeRegistry::IsDerivedFrom(object->GetType(), AudioSource::GetReflectType()) ? static_cast<AudioSource*>(object) : nullptr;
		}

		AudioLowPassFilter* FilterOf(const AudioSource& source) {
			const SceneObject* owner = source.GetOwner().Get();
			return owner ? owner->GetComponent<AudioLowPassFilter>().Get() : nullptr;
		}

		const AnimationCurve* CurveField(Object& object, std::string_view name) {
			const FieldInfo* field = FindField(object.GetType(), StringHash(name));
			if (!field || field->fieldType != AnimationCurve::GetReflectType() || !field->addrFn) {
				return nullptr;
			}
			return static_cast<const AnimationCurve*>(field->addrFn(&object));
		}

		void ValueCurve(Object& object, std::string_view name, f32 value, AnimationCurve& out) {
			const AnimationCurve* curve = CurveField(object, name);
			if (curve && curve->keys.size() >= 2) {
				out = *curve;
				return;
			}
			out.keys.resize(1);
			out.keys[0] = Keyframe{.time = 0.0f, .value = value};
			out.preWrapMode = AnimationCurveWrapMode::Clamp;
			out.postWrapMode = AnimationCurveWrapMode::Clamp;
		}

		void GeneratedRolloff(const AudioSource& source, AnimationCurve& out) {
			const f32 max = source.GetMaxDistance() > 0.0f ? source.GetMaxDistance() : 1.0f;
			const f32 min = source.GetMinDistance() > 0.0f ? source.GetMinDistance() : 0.0f;
			const f32 start = Math::Clamp(min / max, 0.0001f, 1.0f);
			const AudioRolloffMode mode = source.GetRolloffMode();
			const f32 scale = AudioImpl::GetSettings().volumeRolloffScale;
			const auto at = [&](f32 share) { return AudioImpl::ComputeDistanceAttenuation(mode, share * max, min, max, scale); };
			out.keys.clear();
			out.preWrapMode = AnimationCurveWrapMode::Clamp;
			out.postWrapMode = AnimationCurveWrapMode::Clamp;
			if (mode == AudioRolloffMode::Linear || !(start < 1.0f)) {
				const f32 slope = start < 1.0f ? -1.0f / (1.0f - start) : 0.0f;
				out.keys.push_back(Keyframe{.time = start, .value = 1.0f, .inTangent = slope, .outTangent = slope});
				if (start < 1.0f) {
					out.keys.push_back(Keyframe{.time = 1.0f, .value = 0.0f, .inTangent = slope, .outTangent = slope});
				}
				return;
			}
			const auto add = [&](f32 share) {
				const f32 step = share / 50.0f;
				const f32 slope = (at(share + step) - at(share - step)) / (step * 2.0f);
				out.keys.push_back(Keyframe{.time = share, .value = at(share), .inTangent = slope, .outTangent = slope});
			};
			for (f32 share = start; share < 1.0f; share *= 2.0f) {
				add(share);
			}
			add(1.0f);
		}

		bool Shown(AudioSource& source, AudioLowPassFilter* filter, GraphCurve which, AnimationCurve& out) {
			switch (which) {
			case GraphCurve::Volume:
				if (source.GetRolloffMode() == AudioRolloffMode::Custom) {
					const AnimationCurve* custom = CurveField(source, "_customRolloffCurve");
					if (!custom) {
						return false;
					}
					out = *custom;
					return true;
				}
				GeneratedRolloff(source, out);
				return true;
			case GraphCurve::SpatialBlend:
				ValueCurve(source, "_spatialBlendCurve", source.GetSpatialBlend(), out);
				return true;
			case GraphCurve::Spread:
				ValueCurve(source, "_spreadCurve", source.GetSpread() / 360.0f, out);
				return true;
			case GraphCurve::LowPass:
				if (!filter) {
					return false;
				}
				ValueCurve(*filter, "_cutoffCurve", (filter->GetCutoffFrequency() - CUTOFF_LOW) / (CUTOFF_HIGH - CUTOFF_LOW), out);
				return true;
			case GraphCurve::ReverbZoneMix:
				ValueCurve(source, "_reverbZoneMixCurve", source.GetReverbZoneMix(), out);
				return true;
			}
			return false;
		}

		template <typename Mutate>
		void Write(const EditTarget& target, Object& object, std::initializer_list<std::string_view> names, Mutate&& mutate) {
			std::array<str, 2> before;
			size_t index = 0;
			for (std::string_view name : names) {
				before[index++] = CaptureFieldState(object, StringHash(name));
			}
			mutate();
			index = 0;
			for (std::string_view name : names) {
				str after = CaptureFieldState(object, StringHash(name));
				if (!before[index].empty() && after != before[index]) {
					UndoSystem::Push(MakeUnique<SetFieldStateCommand>(target, StringHash(name), std::move(before[index]), std::move(after), "Edit Curve"));
				}
				++index;
			}
		}

		bool Commit(const EditTarget& target, GraphCurve which, const AnimationCurve& edited) {
			AudioSource* source = SourceOf(target);
			if (!source) {
				return false;
			}
			switch (which) {
			case GraphCurve::Volume:
				Write(target, *source, {"_customRolloffCurve", "_rolloffMode"}, [&]() {
					source->SetCustomCurve(AudioSourceCurveType::CustomRolloff, edited);
					source->SetRolloffMode(AudioRolloffMode::Custom);
				});
				return true;
			case GraphCurve::SpatialBlend:
				Write(target, *source, {"_spatialBlendCurve", "_spatialBlend"},
					  [&]() { source->SetCustomCurve(AudioSourceCurveType::SpatialBlend, edited); });
				return true;
			case GraphCurve::Spread:
				Write(target, *source, {"_spreadCurve", "_spread"}, [&]() { source->SetCustomCurve(AudioSourceCurveType::Spread, edited); });
				return true;
			case GraphCurve::ReverbZoneMix:
				Write(target, *source, {"_reverbZoneMixCurve", "_reverbZoneMix"},
					  [&]() { source->SetCustomCurve(AudioSourceCurveType::ReverbZoneMix, edited); });
				return true;
			case GraphCurve::LowPass: {
				AudioLowPassFilter* filter = FilterOf(*source);
				if (!filter || target.IsPrefabRow()) {
					return false;
				}
				Write(EditTarget(Handle<AudioLowPassFilter>(filter)), *filter, {"_cutoffCurve", "_cutoffFrequency"},
					  [&]() { filter->SetCustomCutoffCurve(edited); });
				return true;
			}
			}
			return false;
		}

		enum class Grab : u8 {
			None,
			Key,
			In,
			Out,
		};

		struct Plot {
			f32 x = 0.0f;
			f32 y = 0.0f;
			f32 w = 1.0f;
			f32 h = 1.0f;
			f32 scale = 1.0f;

			f32 X(f32 share) const { return x + share * w; }
			f32 Y(f32 value) const { return y + (1.0f - value / VALUE_TOP) * h; }
			f32 Share(f32 px) const { return (px - x) / w; }
			f32 Value(f32 py) const { return (1.0f - (py - y) / h) * VALUE_TOP; }
			Vec2 At(const Keyframe& key) const { return Vec2(X(key.time), Y(key.value)); }

			Vec2 Handle(f32 slope, bool outgoing) const {
				Vec2 direction(1.0f, -slope * (h / VALUE_TOP) / w);
				const f32 length = direction.Length();
				direction = length > 0.0f ? direction * (1.0f / length) : Vec2(1.0f, 0.0f);
				const f32 reach = HANDLE_LENGTH * scale * (outgoing ? 1.0f : -1.0f);
				return Vec2(direction.x * reach, direction.y * reach);
			}
		};

		struct Graph {
			EditTarget target;
			const void* owner = nullptr;
			UI::ElementHandle plot;
			UI::ElementHandle area;
			UI::ElementHandle listenerLabel;
			std::array<UI::ElementHandle, 3> axis{};
			std::array<UI::ElementHandle, CURVE_COUNT> legend{};
			std::array<UI::ElementHandle, CURVE_COUNT> swatches{};
			u32 hidden = 0;
			i32 curve = -1;
			i32 key = -1;
			Grab grab = Grab::None;
			bool grouped = false;
			std::array<AnimationCurve, CURVE_COUNT> shown{AnimationCurve(GetMemoryResource(MemTag::Editor)),
														  AnimationCurve(GetMemoryResource(MemTag::Editor)),
														  AnimationCurve(GetMemoryResource(MemTag::Editor)),
														  AnimationCurve(GetMemoryResource(MemTag::Editor)),
														  AnimationCurve(GetMemoryResource(MemTag::Editor))};
			std::array<bool, CURVE_COUNT> present{};
			AnimationCurve working{GetMemoryResource(MemTag::Editor)};

			bool Read() {
				AudioSource* source = SourceOf(target);
				if (!source) {
					return false;
				}
				AudioLowPassFilter* filter = FilterOf(*source);
				for (u32 index = 0; index < CURVE_COUNT; ++index) {
					present[index] = Shown(*source, filter, static_cast<GraphCurve>(index), shown[index]);
				}
				if (curve >= 0 && (!present[static_cast<size_t>(curve)] || key >= static_cast<i32>(shown[static_cast<size_t>(curve)].keys.size()))) {
					curve = -1;
					key = -1;
				}
				return true;
			}

			bool Visible(u32 index) const { return present[index] && (hidden & (1u << index)) == 0; }
		};

		struct GraphView {
			EditTarget target;
			u32 hidden = 0;
			i32 curve = -1;
			i32 key = -1;
			bool focused = false;
		};
		GraphView* _view = nullptr;

		void Remember(UI::Context& context, const Graph& graph) {
			if (!_view || !(_view->target == graph.target)) {
				return;
			}
			_view->hidden = graph.hidden;
			_view->curve = graph.curve;
			_view->key = graph.key;
			_view->focused = context.IsAlive(graph.plot) && context.GetFocused() == graph.plot;
		}

		bool Recall(Graph& graph) {
			if (!_view) {
				_view = New<GraphView>(MemTag::Editor);
			}
			if (!(_view->target == graph.target)) {
				*_view = GraphView{.target = graph.target};
			}
			graph.hidden = _view->hidden;
			graph.curve = _view->curve;
			graph.key = _view->key;
			return _view->focused;
		}

		Plot PlotOf(UI::Context& context, const Graph& graph) {
			Plot plot;
			if (!context.IsAlive(graph.area)) {
				return plot;
			}
			const UI::Rect content = context.GetLayout(graph.area).borderBox;
			plot.scale = context.GetContentScale() > 0.0f ? context.GetContentScale() : 1.0f;
			plot.x = content.x;
			plot.y = content.y;
			plot.w = Math::Max(1.0f, content.w);
			plot.h = Math::Max(1.0f, content.h);
			return plot;
		}

		f32 ListenerDistance(const AudioSource& source) {
			const SceneObject* owner = source.GetOwner().Get();
			if (!owner) {
				return -1.0f;
			}
			Vector<AudioImpl::AudioListenerPose> listeners{GetMemoryResource(MemTag::Editor)};
			AudioImpl::GatherListenerPoses(listeners);
			const i32 nearest = AudioImpl::FindNearestListener(listeners, owner->GetWorldPosition());
			return nearest < 0 ? -1.0f : (listeners[static_cast<size_t>(nearest)].position - owner->GetWorldPosition()).Length();
		}

		Grab Pick(const Graph& graph, const Plot& plot, Vec2 point, i32& curve, i32& key) {
			if (graph.curve >= 0 && graph.Visible(static_cast<u32>(graph.curve))) {
				const AnimationCurve& selected = graph.shown[static_cast<size_t>(graph.curve)];
				if (graph.key >= 0 && static_cast<size_t>(graph.key) < selected.keys.size()) {
					const Keyframe& k = selected.keys[static_cast<size_t>(graph.key)];
					const Vec2 at = plot.At(k);
					if (k.inTangentMode != KeyframeTangentMode::Constant && (point - (at + plot.Handle(k.inTangent, false))).Length() <= HANDLE_PICK * plot.scale) {
						curve = graph.curve;
						key = graph.key;
						return Grab::In;
					}
					if (k.outTangentMode != KeyframeTangentMode::Constant && (point - (at + plot.Handle(k.outTangent, true))).Length() <= HANDLE_PICK * plot.scale) {
						curve = graph.curve;
						key = graph.key;
						return Grab::Out;
					}
				}
			}
			f32 best = KEY_PICK * plot.scale;
			Grab found = Grab::None;
			for (u32 index = 0; index < CURVE_COUNT; ++index) {
				if (!graph.Visible(index)) {
					continue;
				}
				const AnimationCurve& shown = graph.shown[index];
				for (size_t k = 0; k < shown.keys.size(); ++k) {
					const f32 distance = (point - plot.At(shown.keys[k])).Length();
					if (distance <= best) {
						best = distance;
						curve = static_cast<i32>(index);
						key = static_cast<i32>(k);
						found = Grab::Key;
					}
				}
			}
			return found;
		}

		i32 CurveAt(const Graph& graph, const Plot& plot, Vec2 point) {
			const f32 share = Math::Clamp(plot.Share(point.x), 0.0f, 1.0f);
			f32 best = CURVE_PICK * plot.scale;
			i32 found = -1;
			for (u32 index = 0; index < CURVE_COUNT; ++index) {
				if (!graph.Visible(index)) {
					continue;
				}
				const f32 distance = Math::Abs(plot.Y(graph.shown[index].Evaluate(share)) - point.y);
				if (distance <= best) {
					best = distance;
					found = static_cast<i32>(index);
				}
			}
			return found;
		}

		void Paint(UI::Context& context, UI::PaintScope& paint, Graph& graph) {
			if (!graph.Read()) {
				return;
			}
			const Plot plot = PlotOf(context, graph);
			const f32 line = Math::Max(1.0f, plot.scale);
			const Color grid = EditorTheme::GetColor(ColorToken::GraphGrid);
			const Color major = EditorTheme::GetColor(ColorToken::GraphGridMajor);
			for (u32 step = 0; step <= 4; ++step) {
				const f32 x = plot.X(static_cast<f32>(step) * 0.25f);
				paint.EmitLine(Vec2(x, plot.y), Vec2(x, plot.y + plot.h), line, step == 0 || step == 4 ? major : grid);
			}
			for (u32 step = 0; step <= 11; ++step) {
				const f32 value = static_cast<f32>(step) * 0.1f;
				const f32 y = plot.Y(value);
				paint.EmitLine(Vec2(plot.x, y), Vec2(plot.x + plot.w, y), line, step == 0 || step == 10 ? major : grid);
			}

			if (const AudioSource* source = SourceOf(graph.target)) {
				const f32 distance = ListenerDistance(*source);
				const f32 max = source->GetMaxDistance();
				if (distance >= 0.0f && max > 0.0f && distance <= max) {
					const f32 x = plot.X(distance / max);
					paint.EmitLine(Vec2(x, plot.y), Vec2(x, plot.y + plot.h), line * 2.0f, EditorTheme::GetColor(ColorToken::AudioListenerDistance));
				}
			}

			const f32 stride = 2.0f * plot.scale;
			const auto drawCurve = [&](u32 index) {
				const AnimationCurve& curve = graph.shown[index];
				const Color color = EditorTheme::GetColor(CURVES[index].color);
				Vec2 previous(plot.x, plot.Y(curve.Evaluate(0.0f)));
				for (f32 x = plot.x + stride; x <= plot.x + plot.w + 0.5f * stride; x += stride) {
					const f32 clamped = Math::Min(x, plot.x + plot.w);
					const Vec2 next(clamped, plot.Y(curve.Evaluate(plot.Share(clamped))));
					paint.EmitLine(previous, next, line * 1.5f, color, UI::LineCap::Round);
					previous = next;
				}
				const f32 half = KEY_SIZE * 0.5f * plot.scale;
				for (const Keyframe& key : curve.keys) {
					const Vec2 at = plot.At(key);
					paint.EmitQuad({at.x - half, at.y - half, half * 2.0f, half * 2.0f}, color, UI::Radii::All(half));
				}
			};
			for (u32 index = 0; index < CURVE_COUNT; ++index) {
				if (graph.Visible(index) && static_cast<i32>(index) != graph.curve) {
					drawCurve(index);
				}
			}
			if (graph.curve >= 0 && graph.Visible(static_cast<u32>(graph.curve))) {
				drawCurve(static_cast<u32>(graph.curve));
				const AnimationCurve& curve = graph.shown[static_cast<size_t>(graph.curve)];
				if (graph.key >= 0 && static_cast<size_t>(graph.key) < curve.keys.size()) {
					const Keyframe& key = curve.keys[static_cast<size_t>(graph.key)];
					const Vec2 at = plot.At(key);
					const Color ink = EditorTheme::GetColor(ColorToken::Text);
					const f32 ring = KEY_SIZE * plot.scale;
					paint.EmitRing({at.x - ring, at.y - ring, ring * 2.0f, ring * 2.0f}, UI::Edges::All(line), ink, UI::Radii::All(ring));
					const f32 half = KEY_SIZE * 0.4f * plot.scale;
					const auto handle = [&](f32 slope, bool outgoing) {
						const Vec2 end = at + plot.Handle(slope, outgoing);
						paint.EmitLine(at, end, line, ink);
						paint.EmitQuad({end.x - half, end.y - half, half * 2.0f, half * 2.0f}, ink, UI::Radii::All(half));
					};
					if (key.inTangentMode != KeyframeTangentMode::Constant) {
						handle(key.inTangent, false);
					}
					if (key.outTangentMode != KeyframeTangentMode::Constant) {
						handle(key.outTangent, true);
					}
				}
			}
		}

		Vec2 Pointer(UI::Context& context) { return context.GetPointerPosition(); }

		void Open(Graph& graph) {
			if (!graph.grouped) {
				UndoSystem::BeginGroup("Edit Curve");
				graph.grouped = true;
			}
		}

		void Close(Graph& graph) {
			if (graph.grouped) {
				UndoSystem::EndGroup();
				graph.grouped = false;
			}
		}

		template <typename Edit>
		void EditSelected(Graph& graph, Edit&& edit) {
			if (graph.curve < 0 || !graph.Read()) {
				return;
			}
			graph.working = graph.shown[static_cast<size_t>(graph.curve)];
			edit(graph.working);
			UndoSystem::BeginGroup("Edit Curve");
			Commit(graph.target, static_cast<GraphCurve>(graph.curve), graph.working);
			UndoSystem::EndGroup();
		}

		void AddKeyAt(Graph& graph, const Plot& plot, Vec2 point) {
			if (!graph.Read()) {
				return;
			}
			const i32 curve = CurveAt(graph, plot, point);
			if (curve < 0) {
				return;
			}
			graph.curve = curve;
			const f32 share = Math::Clamp(plot.Share(point.x), 0.0f, 1.0f);
			graph.working = graph.shown[static_cast<size_t>(curve)];
			graph.key = AnimationCurveOps::AddKey(graph.working, share, graph.working.Evaluate(share));
			UndoSystem::BeginGroup("Add Key");
			Commit(graph.target, static_cast<GraphCurve>(curve), graph.working);
			UndoSystem::EndGroup();
		}

		void DeleteSelected(Graph& graph) {
			if (graph.key < 0) {
				return;
			}
			const i32 key = graph.key;
			EditSelected(graph, [key](AnimationCurve& curve) { AnimationCurveOps::RemoveKey(curve, key); });
			graph.key = -1;
		}

		void OpenKeyMenu(UI::Context& context, const std::shared_ptr<Graph>& shared, Vec2 point) {
			Graph& graph = *shared;
			if (!graph.Read()) {
				return;
			}
			const Plot plot = PlotOf(context, graph);
			i32 curve = -1;
			i32 key = -1;
			const bool onKey = Pick(graph, plot, point, curve, key) == Grab::Key;
			Vector<UI::Widgets::MenuItem> items{GetMemoryResource(MemTag::UI)};
			const std::weak_ptr<Graph> weak = shared;
			UI::Context* const contextPtr = &context;
			if (!onKey) {
				UI::Widgets::MenuItem add;
				add.label = "Add Key";
				add.enabled = CurveAt(graph, plot, point) >= 0;
				add.onSelect = [weak, plot, point, contextPtr]() {
					if (std::shared_ptr<Graph> live = weak.lock()) {
						AddKeyAt(*live, plot, point);
						Remember(*contextPtr, *live);
					}
				};
				items.push_back(std::move(add));
			} else {
				graph.curve = curve;
				graph.key = key;
				const auto item = [&](std::string_view label, bool checked, std::function<void(AnimationCurve&, i32)> edit, bool separator = false) {
					UI::Widgets::MenuItem row;
					row.label = str(label);
					row.checked = checked;
					row.separatorBefore = separator;
					row.onSelect = [weak, edit, curve, key, contextPtr]() {
						if (std::shared_ptr<Graph> live = weak.lock()) {
							live->curve = curve;
							live->key = key;
							EditSelected(*live, [&edit, key](AnimationCurve& c) { edit(c, key); });
							Remember(*contextPtr, *live);
						}
					};
					return row;
				};
				UI::Widgets::MenuItem remove;
				remove.label = "Delete Key";
				remove.onSelect = [weak, curve, key, contextPtr]() {
					if (std::shared_ptr<Graph> live = weak.lock()) {
						live->curve = curve;
						live->key = key;
						DeleteSelected(*live);
						Remember(*contextPtr, *live);
					}
				};
				items.push_back(std::move(remove));
				const Keyframe& k = graph.shown[static_cast<size_t>(curve)].keys[static_cast<size_t>(key)];
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

		UI::ElementHandle AddText(UI::Context& context, UI::ElementHandle parent, std::string_view text, ColorToken color) {
			UI::ElementHandle element = context.CreateElement();
			context.AppendChild(parent, element);
			context.SetText(element, UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontSmall), EditorTheme::GetColor(color), text);
			context.SetHoverable(element, false);
			return element;
		}

		UI::ElementHandle AddPinned(UI::Context& context, UI::ElementHandle parent, std::string_view text, f32 left, f32 top) {
			UI::ElementHandle element = AddText(context, parent, text, ColorToken::TextDim);
			UI::Style style;
			style.position = UI::Position::Absolute;
			style.left = UI::Length::Pixel(left);
			style.top = UI::Length::Pixel(top);
			context.SetStyle(element, style);
			return element;
		}

		str Distance(f32 value) { return std::format("{:g}", std::round(value * 10.0f) / 10.0f); }

		void PlaceAt(UI::Context& context, const Graph& graph, UI::ElementHandle label, f32 share, bool shown) {
			if (!context.IsAlive(label) || !context.IsAlive(graph.plot)) {
				return;
			}
			const f32 scale = context.GetContentScale() > 0.0f ? context.GetContentScale() : 1.0f;
			const f32 total = context.GetLayout(graph.plot).content.w / scale;
			const f32 width = context.GetLayout(label).borderBox.w / scale;
			const f32 at = MARGIN_LEFT + (total - MARGIN_LEFT - MARGIN_RIGHT) * share - width * 0.5f;
			UI::Style style = context.GetStyle(label);
			style.display = shown ? UI::Display::Flex : UI::Display::None;
			style.left = UI::Length::Pixel(Math::Clamp(at, 0.0f, Math::Max(0.0f, total - width)));
			context.SetStyle(label, style);
		}

		void Refresh(UI::Context& context, Graph& graph) {
			if (!graph.Read()) {
				return;
			}
			for (u32 index = 0; index < CURVE_COUNT; ++index) {
				if (!context.IsAlive(graph.legend[index])) {
					continue;
				}
				UI::Style style = context.GetStyle(graph.legend[index]);
				const UI::Display display = graph.present[index] ? UI::Display::Flex : UI::Display::None;
				if (style.display != display) {
					style.display = display;
					context.SetStyle(graph.legend[index], style);
				}
				UI::Style swatch = context.GetStyle(graph.swatches[index]);
				const bool off = (graph.hidden & (1u << index)) != 0;
				const Color fill = EditorTheme::GetColor(off ? ColorToken::GridLine : CURVES[index].color);
				if (swatch.background.type != UI::BrushType::Solid || !(swatch.background.solid == fill)) {
					swatch.background = UI::Brush::SolidColor(fill);
					context.SetStyle(graph.swatches[index], swatch);
				}
			}
			const AudioSource* source = SourceOf(graph.target);
			const f32 max = source ? source->GetMaxDistance() : 0.0f;
			const std::array<str, 3> numbers{str("0"), Distance(max * 0.5f), Distance(max)};
			for (size_t index = 0; index < graph.axis.size(); ++index) {
				if (context.IsAlive(graph.axis[index]) && context.GetTextString(graph.axis[index]) != numbers[index]) {
					context.SetText(graph.axis[index], UISystem::GetDefaultFont(), EditorTheme::GetMetric(MetricToken::FontSmall),
									EditorTheme::GetColor(ColorToken::TextDim), numbers[index]);
				}
				PlaceAt(context, graph, graph.axis[index], static_cast<f32>(index) * 0.5f, true);
			}
			const f32 distance = source ? ListenerDistance(*source) : -1.0f;
			const bool listening = distance >= 0.0f && max > 0.0f && distance <= max;
			PlaceAt(context, graph, graph.listenerLabel, listening ? distance / max : 0.0f, listening);
			Remember(context, graph);
		}

		void DrawGraph(UI::Context& context, UI::ElementHandle row, EditTarget target, const void* owner) {
			const UI::Element* pod = context.GetElement(row);
			for (u32 child = pod ? pod->firstChild : UI::INVALID_UI_INDEX; child != UI::INVALID_UI_INDEX;) {
				const UI::ElementHandle handle = context.MakeHandle(child);
				UI::Style hidden = context.GetStyle(handle);
				hidden.display = UI::Display::None;
				context.SetStyle(handle, hidden);
				child = context.GetElement(handle)->nextSibling;
			}
			UI::Style rowStyle = context.GetStyle(row);
			rowStyle.flexDirection = UI::FlexDirection::Column;
			rowStyle.alignItems = UI::Align::Stretch;
			context.SetStyle(row, rowStyle);

			auto graph = std::make_shared<Graph>();
			graph->target = target;
			graph->owner = owner;
			const bool focused = Recall(*graph);

			UI::ElementHandle plot = context.CreateElement();
			context.AppendChild(row, plot);
			context.AddClass(plot, EditorTheme::Class::Graph);
			context.AddClass(plot, EditorTheme::Class::Clickable);
			context.SetFocusable(plot, true);
			UI::Style plotStyle;
			plotStyle.width = UI::Length::Percent(1.0f);
			plotStyle.height = UI::Length::Pixel(EditorTheme::GetMetric(MetricToken::AudioGraphHeight));
			plotStyle.overflow = UI::Overflow::Hidden;
			context.SetStyle(plot, plotStyle);
			graph->plot = plot;
			if (focused) {
				context.SetFocus(plot);
			}
			graph->area = context.CreateElement();
			context.AppendChild(plot, graph->area);
			UI::Style areaStyle;
			areaStyle.position = UI::Position::Absolute;
			areaStyle.left = UI::Length::Pixel(MARGIN_LEFT);
			areaStyle.top = UI::Length::Pixel(MARGIN_TOP);
			areaStyle.right = UI::Length::Pixel(MARGIN_RIGHT);
			areaStyle.bottom = UI::Length::Pixel(MARGIN_BOTTOM);
			context.SetStyle(graph->area, areaStyle);
			context.SetHoverable(graph->area, false);

			AddPinned(context, plot, "1", 6.0f, MARGIN_TOP - 7.0f);
			AddPinned(context, plot, "0", 6.0f, EditorTheme::GetMetric(MetricToken::AudioGraphHeight) - MARGIN_BOTTOM - 7.0f);
			const f32 bottom = EditorTheme::GetMetric(MetricToken::AudioGraphHeight) - MARGIN_BOTTOM + 2.0f;
			graph->axis[0] = AddPinned(context, plot, "0", MARGIN_LEFT - 3.0f, bottom);
			graph->axis[1] = AddPinned(context, plot, "", MARGIN_LEFT, bottom);
			graph->axis[2] = AddPinned(context, plot, "", MARGIN_LEFT, bottom);
			graph->listenerLabel = AddPinned(context, plot, "Listener", MARGIN_LEFT, 1.0f);

			std::shared_ptr<Graph> shared = graph;
			UI::Context* contextPtr = &context;
			context.Events(plot).customDraw = [graph](UI::Context& c, UI::ElementHandle, UI::PaintScope& paint) { Paint(c, paint, *graph); };

			context.Events(plot).onMouseDown = [graph, plot, contextPtr]() {
				UI::Context* c = contextPtr;
				if (!c || !graph->Read()) {
					return;
				}
				c->SetFocus(plot);
				const Plot area = PlotOf(*c, *graph);
				i32 curve = -1;
				i32 key = -1;
				graph->grab = Pick(*graph, area, Pointer(*c), curve, key);
				graph->curve = graph->grab == Grab::None ? -1 : curve;
				graph->key = graph->grab == Grab::None ? -1 : key;
				if (graph->grab != Grab::None) {
					graph->working = graph->shown[static_cast<size_t>(curve)];
				}
				Remember(*c, *graph);
			};
			context.Events(plot).onPressDrag = [graph, contextPtr](Vec2 point) {
				UI::Context* c = contextPtr;
				if (!c || graph->grab == Grab::None || graph->curve < 0) {
					return;
				}
				const Plot area = PlotOf(*c, *graph);
				const u32 index = static_cast<u32>(graph->curve);
				if (graph->grab == Grab::Key) {
					const f32 share = Math::Clamp(area.Share(point.x), 0.0f, 1.0f);
					const f32 value = Math::Clamp(area.Value(point.y), 0.0f, CURVES[index].top);
					graph->key = AnimationCurveOps::MoveKey(graph->working, graph->key, share, value);
				} else {
					if (graph->key < 0 || static_cast<size_t>(graph->key) >= graph->working.keys.size()) {
						return;
					}
					const Vec2 at = area.At(graph->working.keys[static_cast<size_t>(graph->key)]);
					const Vec2 delta = point - at;
					const bool out = graph->grab == Grab::Out;
					const f32 run = out ? delta.x : -delta.x;
					if (!(Math::Abs(run) > area.scale)) {
						return;
					}
					const f32 rise = out ? -delta.y : delta.y;
					const f32 slope = (rise / (area.h / VALUE_TOP)) / (run / area.w);
					AnimationCurveOps::SetTangent(graph->working, graph->key, out ? KeySide::Out : KeySide::In, slope);
				}
				Open(*graph);
				Commit(graph->target, static_cast<GraphCurve>(index), graph->working);
				Remember(*c, *graph);
			};
			context.Events(plot).onPressEnd = [graph]() {
				graph->grab = Grab::None;
				Close(*graph);
			};
			context.Events(plot).onDoubleClick = [graph, contextPtr]() {
				UI::Context* c = contextPtr;
				if (c) {
					AddKeyAt(*graph, PlotOf(*c, *graph), Pointer(*c));
					Remember(*c, *graph);
				}
			};
			context.Events(plot).onRightClick = [shared, contextPtr]() {
				UI::Context* c = contextPtr;
				if (c) {
					OpenKeyMenu(*c, shared, Pointer(*c));
					Remember(*c, *shared);
				}
			};
			context.Events(plot).onKeyDown = [graph, contextPtr](Key key, KeyMod mods) {
				if ((key == Key::Delete || key == Key::Backspace) && mods == KeyMod::None && graph->key >= 0) {
					DeleteSelected(*graph);
					Remember(*contextPtr, *graph);
					return true;
				}
				return false;
			};

			UI::ElementHandle legend = context.CreateElement();
			context.AppendChild(row, legend);
			UI::Style legendStyle;
			legendStyle.flexDirection = UI::FlexDirection::Row;
			legendStyle.padding = {4.0f, 4.0f, 4.0f, 2.0f};
			context.SetStyle(legend, legendStyle);
			for (u32 index = 0; index < CURVE_COUNT; ++index) {
				UI::ElementHandle entry = context.CreateElement();
				context.AppendChild(legend, entry);
				context.AddClass(entry, EditorTheme::Class::Clickable);
				UI::Style entryStyle;
				entryStyle.flexDirection = UI::FlexDirection::Column;
				entryStyle.width = UI::Length::Pixel(0.0f);
				entryStyle.maxWidth = UI::Length::Pixel(LEGEND_CELL);
				entryStyle.flexGrow = 1.0f;
				context.SetStyle(entry, entryStyle);
				UI::ElementHandle swatch = context.CreateElement();
				context.AppendChild(entry, swatch);
				UI::Style swatchStyle;
				swatchStyle.width = UI::Length::Pixel(KEY_SIZE + 2.0f);
				swatchStyle.height = UI::Length::Pixel(KEY_SIZE + 2.0f);
				swatchStyle.alignSelf = UI::Align::Center;
				swatchStyle.margin = {0.0f, 0.0f, 0.0f, 3.0f};
				swatchStyle.background = UI::Brush::SolidColor(EditorTheme::GetColor(CURVES[index].color));
				context.SetStyle(swatch, swatchStyle);
				context.SetHoverable(swatch, false);
				UI::ElementHandle name = AddText(context, entry, CURVES[index].label, ColorToken::Text);
				UI::Style nameStyle;
				nameStyle.textAlign = UI::Align::Center;
				context.SetStyle(name, nameStyle);
				graph->legend[index] = entry;
				graph->swatches[index] = swatch;
				context.Events(entry).onClick = [graph, index, contextPtr]() {
					UI::Context* c = contextPtr;
					const u32 bit = 1u << index;
					const u32 every = (1u << CURVE_COUNT) - 1u;
					if (c && (c->GetPointerMods() & KeyMod::Ctrl) != KeyMod::None) {
						graph->hidden ^= bit;
					} else {
						graph->hidden = graph->hidden == (every & ~bit) ? 0u : (every & ~bit);
					}
					if ((graph->hidden & every) == every) {
						graph->hidden = 0;
					}
					if (c) {
						Refresh(*c, *graph);
					}
				};
			}

			Refresh(context, *graph);
			PropertyDrawers::SetRowRefresh(context, row, [graph](UI::Context& c) { Refresh(c, *graph); }, owner);
		}

		struct ValueRow {
			EditTarget target;
			str field;
			f32 min = 0.0f;
			f32 max = 1.0f;
			bool slider = true;
			std::function<bool(Object&)> held;
			UI::ElementHandle holder;
			bool shownHeld = false;
			UI::Widgets::SliderParts sliderParts;
			UI::Widgets::NumberFieldParts numberParts;
		};

		f32* ValueAt(Object& object, std::string_view name) {
			const FieldInfo* field = FindField(object.GetType(), StringHash(name));
			if (!field || field->fieldType != GetStaticType<f32>() || !field->addrFn) {
				return nullptr;
			}
			return static_cast<f32*>(field->addrFn(&object));
		}

		void BuildValue(UI::Context& context, const std::shared_ptr<ValueRow>& shared) {
			ValueRow& row = *shared;
			context.ClearChildren(row.holder);
			Object* object = row.target.Resolve();
			const f32* value = object ? ValueAt(*object, row.field) : nullptr;
			row.shownHeld = object && row.held(*object);
			if (!value) {
				return;
			}
			if (row.shownHeld) {
				const UI::ElementHandle held = AddText(context, row.holder, "Controlled by curve", ColorToken::Text);
				context.AddClass(held, EditorTheme::Class::Disabled);
				return;
			}
			const std::weak_ptr<ValueRow> weak = shared;
			const auto write = [weak](f32 next) {
				std::shared_ptr<ValueRow> live = weak.lock();
				Object* target = live ? live->target.Resolve() : nullptr;
				f32* at = target ? ValueAt(*target, live->field) : nullptr;
				if (at) {
					Write(live->target, *target, {live->field}, [at, next]() { *at = next; });
				}
			};
			if (row.slider) {
				UI::Widgets::SliderDesc desc;
				desc.minValue = row.min;
				desc.maxValue = row.max;
				desc.value = *value;
				desc.showValueField = true;
				desc.font = UISystem::GetDefaultFont();
				desc.fontSizePx = EditorTheme::GetMetric(MetricToken::FontBody);
				desc.onChange = write;
				desc.onDragBegin = []() { UndoSystem::BeginGroup("Set Value"); };
				desc.onDragEnd = []() { UndoSystem::EndGroup(); };
				row.sliderParts = UI::Widgets::Slider(context, row.holder, desc);
				UI::Style grow = context.GetStyle(row.sliderParts.root);
				grow.flexGrow = 1.0f;
				context.SetStyle(row.sliderParts.root, grow);
				return;
			}
			UI::Widgets::NumberFieldDesc desc;
			desc.value = *value;
			desc.minValue = row.min;
			desc.font = UISystem::GetDefaultFont();
			desc.fontSizePx = EditorTheme::GetMetric(MetricToken::FontBody);
			desc.onChange = [write](f64 next) { write(static_cast<f32>(next)); };
			desc.onScrubBegin = []() { UndoSystem::BeginGroup("Set Value"); };
			desc.onScrubEnd = []() { UndoSystem::EndGroup(); };
			row.numberParts = UI::Widgets::NumberField(context, row.holder, desc);
			UI::Style grow = context.GetStyle(row.numberParts.root);
			grow.flexGrow = 1.0f;
			context.SetStyle(row.numberParts.root, grow);
		}

		void RefreshValue(UI::Context& context, const std::shared_ptr<ValueRow>& shared) {
			ValueRow& row = *shared;
			Object* object = row.target.Resolve();
			if (!object || !context.IsAlive(row.holder)) {
				return;
			}
			if (row.held(*object) != row.shownHeld) {
				BuildValue(context, shared);
				return;
			}
			const f32* value = ValueAt(*object, row.field);
			if (!value || row.shownHeld) {
				return;
			}
			if (row.slider) {
				UI::Widgets::SetSliderValue(context, row.sliderParts, row.min, row.max, *value);
			} else if (context.IsAlive(row.numberParts.field) && UI::Widgets::GetNumberFieldValue(context, row.numberParts) != static_cast<f64>(*value)) {
				UI::Widgets::SetNumberFieldValue(context, row.numberParts, *value);
			}
		}

		void DrawValue(UI::Context& context, UI::ElementHandle row, EditTarget target, std::string_view field, f32 min, f32 max, bool slider,
					   std::function<bool(Object&)> held, const void* owner) {
			auto shared = std::make_shared<ValueRow>();
			shared->target = target;
			shared->field = str(field);
			shared->min = min;
			shared->max = max;
			shared->slider = slider;
			shared->held = std::move(held);
			shared->holder = context.CreateElement();
			context.AppendChild(row, shared->holder);
			UI::Style style;
			style.flexDirection = UI::FlexDirection::Row;
			style.alignItems = UI::Align::Center;
			style.flexGrow = 1.0f;
			context.SetStyle(shared->holder, style);
			BuildValue(context, shared);
			PropertyDrawers::SetRowRefresh(context, row, [shared](UI::Context& c) { RefreshValue(c, shared); }, owner);
		}

		std::function<bool(Object&)> HeldBy(std::string_view curveField) {
			return [name = str(curveField)](Object& object) {
				const AnimationCurve* curve = CurveField(object, name);
				return curve && curve->keys.size() >= 2;
			};
		}

	} // anonymous namespace

	void RegisterSourceGraph(const void* owner) {
		const StringHash source = AudioSource::GetReflectType()->name;
		const StringHash filter = AudioLowPassFilter::GetReflectType()->name;
		bool registered = PropertyDrawers::RegisterFieldDrawerFor(
			source, StringHash("_customRolloffCurve"),
			[owner](UI::Context& context, UI::ElementHandle row, FieldRef, EditTarget target) { DrawGraph(context, row, target, owner); }, owner);

		const auto value = [&](StringHash type, std::string_view field, std::string_view curve, f32 min, f32 max) {
			registered &= PropertyDrawers::RegisterFieldDrawerFor(
				type, StringHash(field),
				[owner, field, curve, min, max](UI::Context& context, UI::ElementHandle row, FieldRef, EditTarget target) {
					DrawValue(context, row, target, field, min, max, true, HeldBy(curve), owner);
				},
				owner);
		};
		value(source, "_spatialBlend", "_spatialBlendCurve", 0.0f, 1.0f);
		value(source, "_reverbZoneMix", "_reverbZoneMixCurve", 0.0f, 1.1f);
		value(source, "_spread", "_spreadCurve", 0.0f, 360.0f);
		value(filter, "_cutoffFrequency", "_cutoffCurve", CUTOFF_LOW, CUTOFF_HIGH);

		registered &= PropertyDrawers::RegisterFieldDrawerFor(
			source, StringHash("_minDistance"),
			[owner](UI::Context& context, UI::ElementHandle row, FieldRef, EditTarget target) {
				DrawValue(context, row, target, "_minDistance", 0.0f, 0.0f, false, [](Object& object) { return TypeRegistry::IsDerivedFrom(object.GetType(), AudioSource::GetReflectType()) &&
																											   static_cast<AudioSource&>(object).GetRolloffMode() == AudioRolloffMode::Custom; }, owner);
			},
			owner);
		if (!registered) {
			SLT_LOG_ERROR("Audio", "couldn't register the source's distance graph");
		}
	}

	void ReleaseSourceGraph() {
		if (_view) {
			Delete(_view);
			_view = nullptr;
		}
	}

} // namespace Shalltear::AudioEditor
