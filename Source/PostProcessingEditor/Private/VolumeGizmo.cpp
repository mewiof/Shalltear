#include "PostProcessingEditor.h"

#include "Shalltear/Core/Scene/SceneObject.h"

#include "Shalltear/Editor/EditorGizmos.h"
#include "Shalltear/Editor/EditorTheme.h"

#include "Shalltear/PostProcessing/PostProcessVolume.h"

namespace Shalltear::PostProcessingEditor {

	namespace {

		void DrawVolumeGizmo(GizmoDrawContext& context, Handle<Component> component) {
			if (!context.selected || !context.lines) {
				return;
			}
			auto* volume = static_cast<PostProcessVolume*>(component.Get());
			if (!volume || volume->isGlobal) {
				return;
			}
			SceneObject* owner = volume->GetOwner().Get();
			if (!owner) {
				return;
			}

			const Vec3 scale = owner->GetLossyScale();
			const Vec3 halfExtents(volume->size.x * scale.x * 0.5f,
								   volume->size.y * scale.y * 0.5f,
								   volume->size.z * scale.z * 0.5f);
			EditorGizmos::AppendOrientedBox(*context.lines,
											owner->GetWorldPosition(),
											owner->GetWorldRotation(),
											halfExtents,
											EditorTheme::GetColor32(EditorTheme::ColorToken::GizmoVolumeWire));
		}

	} // anonymous namespace

	void RegisterVolumeGizmo(const void* owner) {
		EditorGizmos::RegisterComponentGizmo(StringHash("PostProcessVolume"), &DrawVolumeGizmo, owner);
	}

} // namespace Shalltear::PostProcessingEditor
