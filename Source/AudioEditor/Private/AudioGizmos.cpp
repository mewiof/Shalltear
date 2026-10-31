#include "AudioEditor.h"

#include "Shalltear/Audio/AudioReverbZone.h"
#include "Shalltear/Audio/AudioSource.h"

#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Scene/SceneObject.h"

#include "Shalltear/Editor/EditorGizmos.h"
#include "Shalltear/Editor/EditorTheme.h"

namespace Shalltear::AudioEditor {

	namespace {

		void AppendSphere(Vector<DebugLine>& lines, const Vec3& center, f32 radius, Color32 color) {
			EditorGizmos::AppendCircle(lines, center, Vec3::Right(), radius, color);
			EditorGizmos::AppendCircle(lines, center, Vec3::Up(), radius, color);
			EditorGizmos::AppendCircle(lines, center, Vec3::Forward(), radius, color);
		}

		Color32 RangeColor() { return EditorTheme::GetColor32(EditorTheme::ColorToken::GizmoAudioRange); }

		template <typename Ranged>
		void DrawRanges(GizmoDrawContext& context, Handle<Component> component) {
			const Ranged* ranged = static_cast<const Ranged*>(component.Get());
			const SceneObject* owner = ranged ? ranged->GetOwner().Get() : nullptr;
			if (!owner || !context.selected) {
				return;
			}
			const Vec3 center = owner->GetWorldPosition();
			for (const f32 radius : {ranged->GetMinDistance(), ranged->GetMaxDistance()}) {
				if (radius > 0.0f) {
					AppendSphere(*context.lines, center, radius, RangeColor());
				}
			}
		}

		template <typename Ranged>
		void OfferRanges(Handle<Component> component, Vector<RadiusHandle>& out) {
			const Ranged* ranged = static_cast<const Ranged*>(component.Get());
			const SceneObject* owner = ranged ? ranged->GetOwner().Get() : nullptr;
			if (!owner) {
				return;
			}
			RadiusHandle radius;
			radius.center = owner->GetWorldPosition();
			radius.color = RangeColor();
			radius.radius = ranged->GetMinDistance();
			radius.field = StringHash("_minDistance");
			radius.label = "Min Distance";
			out.push_back(radius);
			radius.radius = ranged->GetMaxDistance();
			radius.field = StringHash("_maxDistance");
			radius.label = "Max Distance";
			out.push_back(radius);
		}

	} // anonymous namespace

	void RegisterAudioGizmos(const void* owner) {
		bool registered = EditorGizmos::RegisterComponentGizmo(StringHash("AudioSource"), &DrawRanges<AudioSource>, owner);
		registered &= EditorGizmos::RegisterComponentGizmo(StringHash("AudioReverbZone"), &DrawRanges<AudioReverbZone>, owner);
		registered &= EditorGizmos::RegisterRadiusHandles(StringHash("AudioSource"), &OfferRanges<AudioSource>, owner);
		registered &= EditorGizmos::RegisterRadiusHandles(StringHash("AudioReverbZone"), &OfferRanges<AudioReverbZone>, owner);
		if (!registered) {
			SLT_LOG_ERROR("Audio", "couldn't register the audio gizmos");
		}
	}

} // namespace Shalltear::AudioEditor
