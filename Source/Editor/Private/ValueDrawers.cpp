#include "ValueDrawers.h"

#include "Shalltear/Core/Math/AnimationCurve.h"
#include "Shalltear/Core/Math/Gradient.h"
#include "Shalltear/Core/Reflection/TypeInfo.h"
#include "Shalltear/Core/UI/RenderList.h"

#include "Shalltear/Editor/CurveEditor.h"
#include "Shalltear/Editor/EditorCommands.h"
#include "Shalltear/Editor/EditorTheme.h"
#include "Shalltear/Editor/GradientEditor.h"
#include "Shalltear/Editor/PropertyDrawers.h"
#include "Shalltear/Editor/PropertyRows.h"
#include "Shalltear/Editor/UndoSystem.h"

#include <memory>

namespace Shalltear::ValueDrawers {

	namespace {

		using MetricToken = EditorTheme::MetricToken;

		template <typename T>
		T* ValueOf(const EditTarget& target, FieldRef field) {
			Object* object = target.Resolve();
			const FieldInfo* info = field.Resolve();
			if (!object || !info || !info->addrFn || !info->fieldType || info->fieldType->name != T::GetReflectType()->name) {
				return nullptr;
			}
			return static_cast<T*>(info->addrFn(object));
		}

		struct Edit {
			EditTarget target;
			FieldRef field;
			str before;
		};

		template <typename T>
		void Write(const Edit& edit, const T& value) {
			if (T* live = ValueOf<T>(edit.target, edit.field)) {
				*live = value;
				edit.target.NotifyWritten();
			}
		}

		void BeginEdit(Edit& edit) {
			Object* object = edit.target.Resolve();
			const FieldInfo* info = edit.field.Resolve();
			edit.before = object && info ? CaptureFieldState(*object, info->name) : str{};
		}

		void EndEdit(Edit& edit) {
			Object* object = edit.target.Resolve();
			const FieldInfo* info = edit.field.Resolve();
			if (!object || !info || edit.before.empty()) {
				return;
			}
			str after = CaptureFieldState(*object, info->name);
			if (after != edit.before) {
				UndoSystem::Push(MakeUnique<SetFieldStateCommand>(edit.target, info->name, std::move(edit.before), std::move(after),
																  str("Edit ") + PropertyRows::FieldLabel(*info)));
			}
			edit.before.clear();
		}

		template <typename T, typename Paint, typename OpenEditor>
		void DrawPreview(UI::Context& context, UI::ElementHandle row, FieldRef field, EditTarget target, Paint&& paint, OpenEditor&& open) {
			UI::ElementHandle preview = context.CreateElement();
			context.AppendChild(row, preview);
			context.AddClass(preview, EditorTheme::Class::CurveEditorPlot);
			context.AddClass(preview, EditorTheme::Class::Clickable);
			UI::Style style;
			style.flexGrow = 1.0f;
			style.alignSelf = UI::Align::Stretch;
			style.minHeight = UI::Length::Pixel(EditorTheme::GetMetric(MetricToken::FontSmall));
			context.SetStyle(preview, style);
			context.Events(preview).customDraw = [target, field, paint](UI::Context& c, UI::ElementHandle self, UI::PaintScope& scope) {
				if (const T* value = ValueOf<T>(target, field)) {
					paint(c, self, scope, *value);
				}
			};
			UI::Context* contextPtr = &context;
			context.Events(preview).onClick = [contextPtr, preview, target, field, open]() {
				const T* value = ValueOf<T>(target, field);
				if (!value) {
					return;
				}
				auto edit = std::make_shared<Edit>(Edit{target, field, {}});
				open(*contextPtr, preview, *value, [edit](const T& next) { Write(*edit, next); }, [edit]() { BeginEdit(*edit); }, [edit]() { EndEdit(*edit); });
			};
		}

	} // anonymous namespace

	void RegisterBuiltIns() {
		PropertyDrawers::RegisterFieldDrawer(AnimationCurve::GetReflectType()->name, [](UI::Context& context, UI::ElementHandle row, FieldRef field, EditTarget target) {
			DrawPreview<AnimationCurve>(
				context, row, field, target,
				[](UI::Context& c, UI::ElementHandle self, UI::PaintScope& scope, const AnimationCurve& curve) { CurveEditor::PaintPreview(c, self, scope, curve); },
				[](UI::Context& c, UI::ElementHandle anchor, const AnimationCurve& curve, auto&& change, auto&& begin, auto&& end) {
					CurveEditor::Open(c, anchor, curve, change, begin, end);
				});
		});
		PropertyDrawers::RegisterFieldDrawer(Gradient::GetReflectType()->name, [](UI::Context& context, UI::ElementHandle row, FieldRef field, EditTarget target) {
			DrawPreview<Gradient>(
				context, row, field, target,
				[](UI::Context& c, UI::ElementHandle self, UI::PaintScope& scope, const Gradient& gradient) { GradientEditor::PaintPreview(c, self, scope, gradient); },
				[](UI::Context& c, UI::ElementHandle anchor, const Gradient& gradient, auto&& change, auto&& begin, auto&& end) {
					GradientEditor::Open(c, anchor, gradient, change, begin, end);
				});
		});
	}

} // namespace Shalltear::ValueDrawers
