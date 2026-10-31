#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/MathReflect.h"
#include "Shalltear/Core/Math/Matrix.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Scene/Component.h"
#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear {

	class Scene;

	class SHALLTEAR_CORE_API SceneObject : public Object {
	public:
		~SceneObject() override;

		SLT_REFLECT_CLASS(SceneObject)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_CREATABLE()
		SLT_REFLECT_FIELD(name)
		SLT_REFLECT_FIELD(_activeSelf, SLT_HIDDEN)
		SLT_REFLECT_FIELD(_localPosition)
		SLT_REFLECT_FIELD(_localRotation)
		SLT_REFLECT_FIELD(_localScale)
		SLT_END_REFLECT()

		str name;

		Handle<Scene> GetScene() const;
		u64 GetLocalId() const { return _localId; }
		u64 GetPrefabKey() const { return _prefabKey; }
		bool IsPendingDestroy() const { return _pendingDestroy; }

		void SetActive(bool active);
		bool IsActiveSelf() const { return _activeSelf; }
		bool IsActiveInHierarchy() const { return _activeInHierarchy; }

		Handle<SceneObject> GetParent() const;
		void SetParent(Handle<SceneObject> parent, bool worldStays = true);
		u32 GetChildCount() const { return static_cast<u32>(_children.size()); }
		Handle<SceneObject> GetChild(u32 index) const;
		u32 GetSiblingIndex() const;
		void SetSiblingIndex(u32 index);
		Handle<SceneObject> FindChildByName(std::string_view childName) const;

		Vec3 GetLocalPosition() const { return _localPosition; }
		void SetLocalPosition(const Vec3& position);
		Quat GetLocalRotation() const { return _localRotation; }
		void SetLocalRotation(const Quat& rotation);
		Vec3 GetLocalScale() const { return _localScale; }
		void SetLocalScale(const Vec3& scale);

		const Mat4& GetWorldMatrix() const;
		Vec3 GetWorldPosition() const;
		void SetWorldPosition(const Vec3& position);
		Quat GetWorldRotation() const;
		void SetWorldRotation(const Quat& rotation);
		Vec3 GetLossyScale() const;
		Vec3 TransformPoint(const Vec3& point) const;
		Vec3 TransformDirection(const Vec3& direction) const;
		Vec3 InverseTransformPoint(const Vec3& point) const;

		template <typename T>
		Handle<T> AddComponent() {
			Handle<Component> handle = AddComponent(GetStaticType<T>());
			return Handle<T>(static_cast<T*>(handle.Get()));
		}
		Handle<Component> AddComponent(const TypeInfo* type);

		template <typename T>
		Handle<T> GetComponent() const {
			const TypeInfo* want = GetStaticType<T>();
			for (const ComponentSlot& slot : _components) {
				if (!slot.ptr->IsPendingDestroy() && TypeRegistry::IsDerivedFrom(slot.type, want)) {
					return Handle<T>(static_cast<T*>(slot.ptr));
				}
			}
			return Handle<T>();
		}

		template <typename T>
		void GetComponents(Vector<Handle<T>>& out) const {
			const TypeInfo* want = GetStaticType<T>();
			for (const ComponentSlot& slot : _components) {
				if (!slot.ptr->IsPendingDestroy() && TypeRegistry::IsDerivedFrom(slot.type, want)) {
					out.push_back(Handle<T>(static_cast<T*>(slot.ptr)));
				}
			}
		}

		template <typename T>
		Handle<T> GetComponentInChildren(bool includeInactive = false) const {
			if (_pendingDestroy || (!includeInactive && !_activeInHierarchy)) {
				return Handle<T>();
			}
			if (Handle<T> own = GetComponent<T>()) {
				return own;
			}
			for (SceneObject* child : _children) {
				if (child->_pendingDestroy) {
					continue;
				}
				if (Handle<T> found = child->GetComponentInChildren<T>(includeInactive)) {
					return found;
				}
			}
			return Handle<T>();
		}

		template <typename T>
		Handle<T> GetComponentInParent() const {
			for (const SceneObject* node = this; node; node = node->_parent) {
				if (node->_pendingDestroy) {
					continue;
				}
				if (Handle<T> found = node->GetComponent<T>()) {
					return found;
				}
			}
			return Handle<T>();
		}

		u32 GetComponentCount() const { return static_cast<u32>(_components.size()); }
		Handle<Component> GetComponentAt(u32 index) const;
		void SetComponentIndex(Handle<Component> component, u32 index);

		// required after a raw reflected write to the local TRS
		void NotifyLocalTransformWritten();
	private:
		friend class Scene;
		friend class SceneSystem;
		friend struct SceneInternalAccess;

		SceneObject();

		void MarkWorldDirtyRecursive() const;
		void DeriveScaleKind();

		enum class ScaleKind : u8 {
			Identity,
			Uniform,
			NonUniform,
			Negative,
		};

		struct ComponentSlot {
			const TypeInfo* type;
			Component* ptr;
		};

		Scene* _scene = nullptr;
		SceneObject* _parent = nullptr;
		Vector<SceneObject*> _children{GetMemoryResource(MemTag::Scene)};
		Vector<ComponentSlot> _components{GetMemoryResource(MemTag::Scene)};
		u64 _localId = 0;
		u64 _prefabKey = 0;
		u32 _prefabRecord = 0;
		Vec3 _localPosition{};
		Quat _localRotation = Quat::Identity();
		Vec3 _localScale{1.0f, 1.0f, 1.0f};
		mutable Mat4 _worldMatrix; // valid only when not _worldDirty
		mutable bool _worldDirty = true;
		ScaleKind _scaleKind = ScaleKind::Identity;
		bool _activeSelf = true;
		bool _activeInHierarchy = false;
		bool _pendingDestroy = false;
	};

} // namespace Shalltear
