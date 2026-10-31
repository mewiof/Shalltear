#include "Shalltear/VisualEffects/VisualEffectBinder.h"

#include "EffectPass.h"

#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Scene/SceneObject.h"

#include "Shalltear/VisualEffects/VisualEffect.h"

SLT_REGISTER_TYPE(Shalltear::VisualEffectBinder)
SLT_REGISTER_TYPE(Shalltear::VisualEffectPositionBinder)
SLT_REGISTER_TYPE(Shalltear::VisualEffectPreviousPositionBinder)
SLT_REGISTER_TYPE(Shalltear::VisualEffectVelocityBinder)
SLT_REGISTER_TYPE(Shalltear::VisualEffectTransformBinder)
SLT_REGISTER_TYPE(Shalltear::VisualEffectEnabledBinder)

namespace Shalltear {

	using namespace VisualEffectsImpl;

	namespace {

		bool ToLocal(const VisualEffect& effect, VisualEffectBinderSpace space, Mat4& out) {
			const SceneObject* owner = effect.GetOwner().Get();
			return space == VisualEffectBinderSpace::Local && owner && owner->GetWorldMatrix().TryInvert(out);
		}

		Vec3 InSpace(const VisualEffect& effect, VisualEffectBinderSpace space, const Vec3& world) {
			Mat4 toLocal;
			return ToLocal(effect, space, toLocal) ? toLocal.TransformPoint(world) : world;
		}

	} // anonymous namespace

	void VisualEffectBinder::OnEnable() {
		Component::OnEnable();
		_sinceUpdate = 0.0f;
		RegisterBinder(*this);
	}

	void VisualEffectBinder::OnDisable() {
		UnregisterBinder(*this);
		Component::OnDisable();
	}

	bool VisualEffectPositionBinder::IsValid(const VisualEffect&) const { return target.Get() != nullptr; }

	void VisualEffectPositionBinder::UpdateBinding(VisualEffect& effect, f32) {
		effect.SetVector3(property, InSpace(effect, space, target.Get()->GetWorldPosition()));
	}

	void VisualEffectPreviousPositionBinder::OnEnable() {
		VisualEffectBinder::OnEnable();
		_hasPrevious = false;
	}

	bool VisualEffectPreviousPositionBinder::IsValid(const VisualEffect&) const { return target.Get() != nullptr; }

	void VisualEffectPreviousPositionBinder::UpdateBinding(VisualEffect& effect, f32) {
		const Vec3 current = InSpace(effect, space, target.Get()->GetWorldPosition());
		effect.SetVector3(property, _hasPrevious ? _previous : current);
		_previous = current;
		_hasPrevious = true;
	}

	void VisualEffectVelocityBinder::OnEnable() {
		VisualEffectBinder::OnEnable();
		_hasPrevious = false;
	}

	bool VisualEffectVelocityBinder::IsValid(const VisualEffect&) const { return target.Get() != nullptr; }

	void VisualEffectVelocityBinder::UpdateBinding(VisualEffect& effect, f32 deltaTime) {
		const Vec3 current = InSpace(effect, space, target.Get()->GetWorldPosition());
		Vec3 velocity{};
		if (_hasPrevious && deltaTime > 0.0f) {
			const Vec3 moved = current - _previous;
			if (moved.LengthSquared() > 0.0f) {
				velocity = moved / deltaTime;
			}
		}
		effect.SetVector3(property, velocity);
		_previous = current;
		_hasPrevious = true;
	}

	bool VisualEffectTransformBinder::IsValid(const VisualEffect&) const { return target.Get() != nullptr; }

	void VisualEffectTransformBinder::UpdateBinding(VisualEffect& effect, f32) {
		Mat4 transform = target.Get()->GetWorldMatrix();
		Mat4 toLocal;
		if (ToLocal(effect, space, toLocal)) {
			transform = toLocal * transform;
		}
		Vec3 position;
		Quat rotation;
		Vec3 scale;
		static_cast<void>(Mat4::DecomposeTRS(transform, position, rotation, scale));
		effect.SetVector3(property + "_position", position);
		effect.SetVector3(property + "_angles", rotation.ToEuler());
		effect.SetVector3(property + "_scale", scale);
	}

	bool VisualEffectEnabledBinder::IsValid(const VisualEffect&) const { return target.Get() != nullptr; }

	void VisualEffectEnabledBinder::UpdateBinding(VisualEffect& effect, f32) {
		const SceneObject& read = *target.Get();
		effect.SetBool(property, check == VisualEffectEnabledCheck::ActiveSelf ? read.IsActiveSelf() : read.IsActiveInHierarchy());
	}

} // namespace Shalltear
