#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	class SceneObject;
	class Scene;

	class SHALLTEAR_CORE_API Component : public Object {
	public:
		~Component() override;

		SLT_REFLECT_COMPONENT_ABSTRACT(Component, Object)
		SLT_REFLECT_FIELD(_enabledSelf, SLT_HIDDEN)
		SLT_END_REFLECT()

		Handle<SceneObject> GetOwner() const;
		Handle<Scene> GetScene() const;
		void SetEnabled(bool enabled);
		bool IsEnabledSelf() const { return _enabledSelf; }
		bool IsEffectivelyEnabled() const;
		bool IsPendingDestroy() const { return _pendingDestroy; }

		u64 GetLocalId() const { return _localId; }
		u64 GetPrefabKey() const { return _prefabKey; }
	protected:
		Component() = default;
		virtual void OnAttach() {}
		virtual void OnDetach() {}
		virtual void OnEnable() {}
		virtual void OnDisable() {}
		virtual void OnDestroy() {}
	private:
		friend class Behavior;
		friend class SceneObject;
		friend class Scene;
		friend class SceneSystem;
		friend struct SceneInternalAccess;

		SceneObject* _owner = nullptr;
		u64 _localId = 0;
		u64 _prefabKey = 0;
		bool _enabledSelf = true;
		bool _pendingDestroy = false;
		bool _isScript = false;
	};

} // namespace Shalltear
