#include "AnimationEditor.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Math/MathUtil.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/PropertyDrawers.h"

#include "Shalltear/Animation/AnimatorController.h"

#include "Shalltear/Core/Handle.h"

#include <memory>

namespace Shalltear::AnimationEditor {

	namespace {

		using namespace UI;
		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;

		ElementHandle AddLine(Context& context, ElementHandle parent, ColorToken color, MetricToken size, std::string_view text) {
			ElementHandle line = context.CreateElement();
			context.AppendChild(parent, line);
			context.SetText(line, UISystem::GetDefaultFont(), EditorTheme::GetMetric(size),
							EditorTheme::GetColor(color), text);
			return line;
		}

		ElementHandle AddSection(Context& context, ElementHandle parent, std::string_view title) {
			ElementHandle box = context.CreateElement();
			context.AppendChild(parent, box);
			Style style;
			style.flexDirection = FlexDirection::Column;
			style.width = Length::Percent(1.0f);
			style.margin = {8.0f, 6.0f, 0.0f, 8.0f};
			context.SetStyle(box, style);
			AddLine(context, box, ColorToken::WidgetText, MetricToken::FontBody, title);
			return box;
		}

		const char* KindName(AnimatorParameterKind kind) {
			switch (kind) {
			case AnimatorParameterKind::Float:
				return "Float";
			case AnimatorParameterKind::Int:
				return "Int";
			case AnimatorParameterKind::Bool:
				return "Bool";
			case AnimatorParameterKind::Trigger:
				return "Trigger";
			}
			return "?";
		}

		const char* BlendName(AnimatorBlendKind kind) {
			switch (kind) {
			case AnimatorBlendKind::Clip:
				return "Clip";
			case AnimatorBlendKind::Simple1D:
				return "1D";
			case AnimatorBlendKind::FreeformCartesian2D:
				return "2D Freeform Cartesian";
			case AnimatorBlendKind::FreeformDirectional2D:
				return "2D Freeform Directional";
			case AnimatorBlendKind::Direct:
				return "Direct";
			}
			return "?";
		}

		str MotionLabel(const AnimatorController& controller, i32 node) {
			if (node < 0 || static_cast<size_t>(node) >= controller.nodes.size()) {
				return str("<none>");
			}
			const AnimatorController::MotionNode& entry = controller.nodes[static_cast<size_t>(node)];
			if (entry.kind != AnimatorBlendKind::Clip) {
				return str(BlendName(entry.kind));
			}
			const AnimationClip* clip = entry.clip.Get();
			if (!clip) {
				return str("<missing clip>");
			}
			std::string_view path = clip->virtualPath;
			const size_t slash = path.find_last_of('/');
			return str(slash == std::string_view::npos ? path : path.substr(slash + 1));
		}

		struct PreviewSample {
			Vec2 at{};

			Handle<AnimatorStateMachine> live;
			u32 cooldown = 0;

			bool Resolve(const AnimatorController& controller, const AnimatorController::MotionNode& tree, Vec2& out) {
				AnimatorStateMachine* component = live.Get();
				if (component && component->GetController().Get() != &controller) {
					component = nullptr;
					live = {};
				}
				if (!component) {
					if (cooldown > 0) {
						--cooldown;
					} else {
						cooldown = 15;
						component = AnimationEditor::FindPlayingComponent(&controller);
						live = Handle<AnimatorStateMachine>(component);
					}
				}
				if (!component) {
					out = at;
					return false;
				}
				out = Vec2(component->GetFloat(tree.parameterX), component->GetFloat(tree.parameterY));
				return true;
			}
		};

		f32 SpaceExtent(const AnimatorController::MotionNode& tree) {
			f32 extent = 1.0f;
			for (const AnimatorController::MotionChild& child : tree.children) {
				extent = Math::Max(extent, Math::Abs(child.position.x));
				extent = Math::Max(extent, Math::Abs(child.position.y));
			}
			return extent * 1.25f;
		}

		Vec2 ToPixels(const Rect& area, f32 extent, Vec2 point) {
			const f32 half = Math::Min(area.w, area.h) * 0.5f;
			const f32 centreX = area.x + area.w * 0.5f;
			const f32 centreY = area.y + area.h * 0.5f;
			return Vec2(centreX + point.x / extent * half, centreY - point.y / extent * half);
		}

		Vec2 FromPixels(const Rect& area, f32 extent, Vec2 pixels) {
			const f32 half = Math::Min(area.w, area.h) * 0.5f;
			if (!(half > 0.0f)) {
				return Vec2{};
			}
			const f32 centreX = area.x + area.w * 0.5f;
			const f32 centreY = area.y + area.h * 0.5f;
			return Vec2((pixels.x - centreX) / half * extent, (centreY - pixels.y) / half * extent);
		}

		void EmitDot(PaintScope& paint, Vec2 centre, f32 radius, Color color) {
			if (!(radius > 0.0f)) {
				return;
			}
			paint.EmitQuad({centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f}, color,
						   Radii{radius, radius, radius, radius});
		}

		void EmitRingDot(PaintScope& paint, Vec2 centre, f32 radius, f32 width, Color color) {
			if (!(radius > width)) {
				return;
			}
			paint.EmitRing({centre.x - radius, centre.y - radius, radius * 2.0f, radius * 2.0f},
						   Edges{width, width, width, width}, color, Radii{radius, radius, radius, radius});
		}

		void BuildBlendSpace(Context& context, ElementHandle parent, const AnimatorController& controller, i32 node) {
			const AnimatorController::MotionNode& tree = controller.nodes[static_cast<size_t>(node)];

			ElementHandle box = AddSection(context, parent, MotionLabel(controller, node));
			AddLine(context, box, ColorToken::WidgetTextDim, MetricToken::FontSmall,
					std::string(BlendName(tree.kind)) + "  ·  " + std::string(tree.parameterX) + " / " +
						std::string(tree.parameterY));

			ElementHandle graph = context.CreateElement();
			context.AppendChild(box, graph);
			Style graphStyle;
			graphStyle.width = Length::Pixel(220.0f);
			graphStyle.height = Length::Pixel(220.0f);
			graphStyle.margin = {0.0f, 0.0f, 4.0f, 8.0f};
			context.SetStyle(graph, graphStyle);
			context.AddClass(graph, EditorTheme::Class::Graph);

			auto sample = std::make_shared<PreviewSample>();
			const AnimatorController* asset = &controller;
			const f32 extent = SpaceExtent(tree);

			context.Events(graph).customDraw = [asset, node, sample, extent](Context& c, ElementHandle element,
																			 PaintScope& paint) {
				const Rect area = c.GetLayout(element).content;
				if (area.w <= 4.0f || area.h <= 4.0f) {
					return;
				}
				const AnimatorController::MotionNode& drawn = asset->nodes[static_cast<size_t>(node)];
				Vec2 at{};
				const bool isLive = sample->Resolve(*asset, drawn, at);

				const Color grid = EditorTheme::GetColor(ColorToken::GridLine);
				for (i32 step = -4; step <= 4; ++step) {
					const f32 at = static_cast<f32>(step) * 0.25f * extent;
					const Vec2 horizontal = ToPixels(area, extent, Vec2(0.0f, at));
					const Vec2 vertical = ToPixels(area, extent, Vec2(at, 0.0f));
					paint.EmitQuad({area.x, horizontal.y, area.w, 1.0f}, grid);
					paint.EmitQuad({vertical.x, area.y, 1.0f, area.h}, grid);
				}
				const Vec2 origin = ToPixels(area, extent, Vec2{});
				paint.EmitQuad({area.x, origin.y, area.w, 1.0f}, EditorTheme::GetColor(ColorToken::AxisX));
				paint.EmitQuad({origin.x, area.y, 1.0f, area.h}, EditorTheme::GetColor(ColorToken::AxisY));

				f32 weights[64];
				const u32 count = AnimatorBlend::Weights(*asset, node, at, nullptr, weights, 64);
				const f32 unit = Math::Min(area.w, area.h) * 0.5f;
				for (size_t index = 0; index < drawn.children.size(); ++index) {
					const Vec2 at = ToPixels(area, extent, drawn.children[index].position);
					const f32 share = index < count ? weights[index] : 0.0f;
					EmitRingDot(paint, at, 6.0f, 1.0f, EditorTheme::GetColor(ColorToken::WidgetTextDim));
					EmitDot(paint, at, share * unit * 0.22f, EditorTheme::GetColor(ColorToken::Accent));
					EmitDot(paint, at, 3.0f, EditorTheme::GetColor(ColorToken::WidgetText));
				}

				const Vec2 dot = ToPixels(area, extent, at);
				EmitDot(paint, dot, 5.0f,
						EditorTheme::GetColor(isLive ? ColorToken::GraphNodeActive : ColorToken::StatusError));
			};

			context.Events(graph).onPressDrag = [&context, graph, sample, extent](Vec2 point) {
				const Rect area = context.GetLayout(graph).content;
				sample->at = FromPixels(area, extent, point);
			};

			for (size_t index = 0; index < tree.children.size(); ++index) {
				const AnimatorController::MotionChild& child = tree.children[index];
				AddLine(context, box, ColorToken::WidgetTextDim, MetricToken::FontSmall,
						MotionLabel(controller, child.node) + "   (" + std::to_string(child.position.x) + ", " +
							std::to_string(child.position.y) + ")");
			}
		}

	} // anonymous namespace

	void RegisterAnimatorControllerInspector(const void* owner) {
		const bool registered = PropertyDrawers::RegisterAssetInspector(
			StringHash("AnimatorController"),
			[](Context& context, ElementHandle parent, const Guid& guid, Asset& asset) {
				(void)guid;
				const AnimatorController& controller = static_cast<const AnimatorController&>(asset);

				AddLine(context, parent, ColorToken::WidgetText, MetricToken::FontBody, "Animator Controller");

				ElementHandle parameters = AddSection(context, parent, "Parameters");
				for (const AnimatorController::Parameter& parameter : controller.parameters) {
					AddLine(context, parameters, ColorToken::WidgetTextDim, MetricToken::FontSmall,
							std::string(parameter.name) + "   " + KindName(parameter.kind));
				}

				ElementHandle layers = AddSection(context, parent, "Layers");
				for (const AnimatorController::Layer& layer : controller.layers) {
					AddLine(context, layers, ColorToken::WidgetTextDim, MetricToken::FontSmall,
							std::string(layer.name) + "   weight " + std::to_string(layer.weight) +
								(layer.blend == AnimationBlendMode::Additive ? "   additive" : "   override") +
								(layer.mask.empty() ? "" : "   masked"));
				}

				ElementHandle states = AddSection(context, parent, "States");
				for (const AnimatorController::State& state : controller.states) {
					const str machine = state.machine >= 0 && static_cast<size_t>(state.machine) < controller.machines.size()
											? controller.machines[static_cast<size_t>(state.machine)].name
											: str("<none>");
					AddLine(context, states, ColorToken::WidgetTextDim, MetricToken::FontSmall,
							std::string(machine) + " / " + std::string(state.name) + "   " +
								std::string(MotionLabel(controller, state.motion)) + "   " +
								std::to_string(state.transitions.size()) + " transition(s)");
				}

				for (size_t node = 0; node < controller.nodes.size(); ++node) {
					const AnimatorBlendKind kind = controller.nodes[node].kind;
					if (kind == AnimatorBlendKind::FreeformCartesian2D || kind == AnimatorBlendKind::FreeformDirectional2D) {
						BuildBlendSpace(context, parent, controller, static_cast<i32>(node));
					}
				}
			},
			owner);

		if (!registered) {
			SLT_LOG_WARN("Animation", "couldn't register the AnimatorController inspector");
		}
	}

} // namespace Shalltear::AnimationEditor
