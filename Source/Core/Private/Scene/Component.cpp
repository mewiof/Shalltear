#include "Shalltear/Core/Scene/Component.h"

#include "SceneInternal.h"
#include "Shalltear/Core/Scene/Behavior.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"

namespace Shalltear {

	using namespace SceneInternal;

	Component::~Component() = default;

	Handle<SceneObject> Component::GetOwner() const {
		return Handle<SceneObject>(_owner);
	}

	Handle<Scene> Component::GetScene() const {
		return _owner ? _owner->GetScene() : Handle<Scene>();
	}

	bool Component::IsEffectivelyEnabled() const {
		return _enabledSelf && _owner && _owner->IsActiveInHierarchy() && !_pendingDestroy;
	}

	void Component::SetEnabled(bool enabled) {
		SLT_SCENE_ASSERT_MAIN_THREAD();
		SLT_CORE_ASSERT_MSG(IsStructuralMutationLegal(), "SetEnabled is illegal inside lifecycle hooks");
		SLT_CORE_ASSERT_MSG(!_pendingDestroy, "SetEnabled on a doomed component");
		if (!IsStructuralMutationLegal() || _pendingDestroy) {
			return;
		}
		if (_enabledSelf == enabled) {
			return;
		}

		bool wasEffective = IsEffectivelyEnabled();
		_enabledSelf = enabled;
		bool nowEffective = IsEffectivelyEnabled();
		if (wasEffective == nowEffective) {
			return;
		}

		const TypeInfo* slotType = nullptr;
		for (const auto& slot : SceneInternalAccess::ComponentsOf(*_owner)) {
			if (slot.ptr == this) {
				slotType = slot.type;
				break;
			}
		}
		if (slotType && TypeRegistry::IsDerivedFrom(slotType, Behavior::GetReflectType())) {
			Behavior& behavior = *static_cast<Behavior*>(this);
			if (nowEffective) {
				RegisterTicks(behavior);
			} else {
				UnregisterTicks(behavior);
			}
		}

		HookGuard guard;
		if (nowEffective) {
			SceneInternalAccess::OnEnable(*this);
		} else {
			SceneInternalAccess::OnDisable(*this);
		}
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::Component)
