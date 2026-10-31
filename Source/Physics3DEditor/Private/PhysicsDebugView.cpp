#include "Physics3DEditor.h"

#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/UI/UISystem.h"
#include "Shalltear/Core/UI/Widgets.h"

#include "Shalltear/Editor/EditorGizmos.h"
#include "Shalltear/Editor/EditorPanel.h"
#include "Shalltear/Editor/EditorTheme.h"

#include "Shalltear/Physics3D/Colliders.h"
#include "Shalltear/Physics3D/Physics.h"
#include "Shalltear/Physics3D/Rigidbody.h"

#include "PhysicsBackend.h"
#include "PhysicsWorld.h"

#include <format>

namespace Shalltear {

	namespace Physics3DEditorImpl {

		using EditorTheme::ColorToken;
		using EditorTheme::MetricToken;

		struct DebugViewState {
			bool enabled = false;
			PhysicsBackend::DebugDrawOptions options;

			bool mouseSelect = false;

			bool hasHit = false;
			Handle<Collider> hitCollider;
			Handle<SceneObject> hitObject;
			Handle<Rigidbody> hitBody;
			f32 hitDistance = 0.0f;
		};

		DebugViewState& State() {
			static DebugViewState state;
			return state;
		}

		namespace {

			struct LineSink {
				Vector<DebugLine>* lines = nullptr;
			};

			Vector<DebugLine>& LinesOf(void* user) { return *static_cast<LineSink*>(user)->lines; }

			Color32 FromRGB(u32 rgb) {
				return Color32(static_cast<u8>((rgb >> 16) & 0xFFu),
							   static_cast<u8>((rgb >> 8) & 0xFFu),
							   static_cast<u8>(rgb & 0xFFu));
			}

			void SinkLine(void* user, const Vec3& a, const Vec3& b, u32 rgb) {
				LinesOf(user).push_back({a, b, FromRGB(rgb)});
			}

			void SinkSphere(void* user, const Vec3& center, f32 radius, u32 rgb) {
				Vector<DebugLine>& lines = LinesOf(user);
				const Color32 color = FromRGB(rgb);
				EditorGizmos::AppendCircle(lines, center, Vec3::Right(), radius, color);
				EditorGizmos::AppendCircle(lines, center, Vec3::Up(), radius, color);
				EditorGizmos::AppendCircle(lines, center, Vec3::Forward(), radius, color);
			}

			void SinkCapsule(void* user, const Vec3& pointA, const Vec3& pointB, f32 radius, u32 rgb) {
				EditorGizmos::AppendCapsule(LinesOf(user), pointA, pointB, radius, FromRGB(rgb));
			}

			void SinkPoint(void* user, const Vec3& position, f32 size, u32 rgb) {
				Vector<DebugLine>& lines = LinesOf(user);
				const Color32 color = FromRGB(rgb);
				const f32 arm = size * 0.1f;
				lines.push_back({position - Vec3::Right() * arm, position + Vec3::Right() * arm, color});
				lines.push_back({position - Vec3::Up() * arm, position + Vec3::Up() * arm, color});
				lines.push_back({position - Vec3::Forward() * arm, position + Vec3::Forward() * arm, color});
			}

			constexpr f32 MARKER_RADIUS = 0.12f;
			constexpr f32 MARKER_NORMAL_LENGTH = 0.5f;

			void UpdateMouseSelect(GizmoDrawContext& context) {
				DebugViewState& state = State();
				if (!state.mouseSelect || !context.cursorInView) {
					state.hasHit = false;
					return;
				}

				RaycastHit hit;
				state.hasHit = Physics::Raycast(context.cursorRay, hit);
				if (!state.hasHit) {
					return;
				}
				state.hitCollider = hit.collider;
				state.hitObject = hit.object;
				state.hitBody = hit.rigidbody;
				state.hitDistance = hit.distance;

				if (!context.overlayLines) {
					return;
				}
				const Color32 color = EditorTheme::GetColor32(ColorToken::Accent);
				EditorGizmos::AppendCircle(*context.overlayLines, hit.point, hit.normal, MARKER_RADIUS, color);
				context.overlayLines->push_back({hit.point, hit.point + hit.normal * MARKER_NORMAL_LENGTH, color});
			}

			void DrawPhysicsDebug(GizmoDrawContext& context) {
				DebugViewState& state = State();
				PhysicsWorld& world = GetPhysicsWorld();
				if (!world.IsUp()) {
					return;
				}

				UpdateMouseSelect(context);

				if (!state.enabled || !context.lines) {
					return;
				}
				LineSink target;
				target.lines = context.lines;

				PhysicsBackend::DebugDrawSink sink;
				sink.user = &target;
				sink.line = &SinkLine;
				sink.sphere = &SinkSphere;
				sink.capsule = &SinkCapsule;
				sink.point = &SinkPoint;
				world.DrawDebug(state.options, sink);
			}

		} // anonymous namespace

		class PhysicsDebugPanel : public EditorPanel {
		public:
			SLT_REFLECT_CLASS(PhysicsDebugPanel)
			SLT_REFLECT_PARENT(EditorPanel)
			SLT_REFLECT_CREATABLE()
			SLT_EDITOR_PANEL_NAME("Physics Debug")
			SLT_EDITOR_PANEL_CATEGORY("Physics")
			SLT_END_REFLECT()

			PhysicsDebugPanel() = default;

			void OnBuildContent(UI::Context& context, UI::ElementHandle root) override;
			void OnTick(f32 deltaSeconds) override;
		private:
			UI::ElementHandle _status;
			UI::ElementHandle _pick;
			f32 _statusAccum = 0.0f;

			void RefreshStatus(UI::Context& context);
			str DescribePick() const;
		};

		namespace {

			constexpr f32 STATUS_REFRESH_SECONDS = 0.25f;

			UI::ElementHandle AddLabel(UI::Context& context, UI::ElementHandle parent, Handle<FontAsset> font,
									   MetricToken size, ColorToken color, std::string_view text) {
				UI::ElementHandle line = context.CreateElement();
				context.AppendChild(parent, line);
				context.SetText(line, font, EditorTheme::GetMetric(size), EditorTheme::GetColor(color), text);
				return line;
			}

			void AddOptionRow(UI::Context& context, UI::ElementHandle parent, Handle<FontAsset> font,
							  std::string_view label, bool* field) {
				UI::ElementHandle row = context.CreateElement();
				context.AppendChild(parent, row);
				UI::Style style;
				style.flexDirection = UI::FlexDirection::Row;
				style.alignItems = UI::Align::Center;
				style.margin = {0.0f, 0.0f, 0.0f, 2.0f};
				context.SetStyle(row, style);
				UI::Widgets::Checkbox(context, row, *field, [field](bool value) { *field = value; });
				AddLabel(context, row, font, MetricToken::Font, ColorToken::Text, label);
			}

		} // anonymous namespace

		void PhysicsDebugPanel::OnBuildContent(UI::Context& context, UI::ElementHandle root) {
			Handle<FontAsset> font = UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{};
			DebugViewState& state = State();

			UI::Style rootStyle;
			rootStyle.flexDirection = UI::FlexDirection::Column;
			rootStyle.alignItems = UI::Align::Stretch;
			rootStyle.width = UI::Length::Percent(1.0f);
			rootStyle.height = UI::Length::Percent(1.0f);
			rootStyle.padding = UI::Edges::All(8.0f);
			rootStyle.overflow = UI::Overflow::Scroll;
			context.SetStyle(root, rootStyle);

			AddOptionRow(context, root, font, "Draw physics debug", &state.enabled);
			AddOptionRow(context, root, font, "Mouse select", &state.mouseSelect);
			UI::Widgets::Separator(context, root);

			AddOptionRow(context, root, font, "Collision geometry", &state.options.shapes);
			AddOptionRow(context, root, font, "Broadphase bounds", &state.options.bounds);
			AddOptionRow(context, root, font, "Contact points", &state.options.contacts);
			AddOptionRow(context, root, font, "Contact normals", &state.options.contactNormals);
			AddOptionRow(context, root, font, "Contact impulses", &state.options.contactImpulses);
			AddOptionRow(context, root, font, "Center of mass", &state.options.mass);
			AddOptionRow(context, root, font, "Sleep state", &state.options.sleep);
			AddOptionRow(context, root, font, "Solver islands", &state.options.islands);
			AddOptionRow(context, root, font, "Joint frames", &state.options.joints);

			UI::Widgets::Separator(context, root);
			_status = AddLabel(context, root, font, MetricToken::FontSmall, ColorToken::TextDim, "");
			_pick = AddLabel(context, root, font, MetricToken::FontSmall, ColorToken::TextDim, "");
			RefreshStatus(context);
		}

		void PhysicsDebugPanel::RefreshStatus(UI::Context& context) {
			if (!context.IsAlive(_status)) {
				return;
			}
			const PhysicsWorld& world = GetPhysicsWorld();
			const str text = std::format("backend {}: {} island(s), {} rigidbody(s), {} static body(s), {} collider(s)",
										 PhysicsBackend::Name(), world.GetIslandCount(), world.GetBodyCount(),
										 world.GetStaticBodyCount(), world.GetColliderCount());
			context.SetText(_status, UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{},
							EditorTheme::GetMetric(MetricToken::FontSmall), EditorTheme::GetColor(ColorToken::TextDim), text);

			if (!context.IsAlive(_pick)) {
				return;
			}
			context.SetText(_pick, UISystem::IsInitialized() ? UISystem::GetDefaultFont() : Handle<FontAsset>{},
							EditorTheme::GetMetric(MetricToken::FontSmall), EditorTheme::GetColor(ColorToken::TextDim),
							DescribePick());
		}

		str PhysicsDebugPanel::DescribePick() const {
			const DebugViewState& state = State();
			if (!state.mouseSelect) {
				return "mouse select is off";
			}
			if (!state.hasHit) {
				return "under cursor: nothing";
			}

			const SceneObject* object = state.hitObject.Get();
			const Collider* collider = state.hitCollider.Get();
			if (!object || !collider) {
				return "under cursor: <destroyed>";
			}
			const Rigidbody* body = state.hitBody.Get();
			const TypeInfo* type = collider->GetType();
			return std::format("under cursor: {} ({}), {}, {:.2f} m", object->name,
							   type && type->displayName ? type->displayName : "Collider",
							   body ? "dynamic body" : "static geometry", state.hitDistance);
		}

		void PhysicsDebugPanel::OnTick(f32 deltaSeconds) {
			UI::Context* context = GetPanelContext();
			if (!context) {
				return;
			}
			_statusAccum += deltaSeconds;
			if (_statusAccum >= STATUS_REFRESH_SECONDS) {
				_statusAccum = 0.0f;
				RefreshStatus(*context);
			}
		}

	} // namespace Physics3DEditorImpl

	namespace Physics3DEditor {

		void RegisterPhysicsDebugView(const void* owner) {
			EditorGizmos::RegisterSceneGizmo(StringHash("PhysicsDebug"),
											 &Physics3DEditorImpl::DrawPhysicsDebug, owner);
		}

	} // namespace Physics3DEditor

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::Physics3DEditorImpl::PhysicsDebugPanel)
