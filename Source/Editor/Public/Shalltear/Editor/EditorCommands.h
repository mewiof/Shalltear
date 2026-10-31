#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Math/Quat.h"
#include "Shalltear/Core/Math/Vector.h"
#include "Shalltear/Core/Scene/Prefab.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

#include "Shalltear/Editor/UndoSystem.h"

#include <string_view>

namespace Shalltear {

	class Scene;
	class SceneObject;
	class Component;
	struct TypeInfo;

	struct SHALLTEAR_EDITOR_API CapturedComponent {
		StringHash typeName{};
		str stateJSON;
	};

	struct SHALLTEAR_EDITOR_API CapturedObject {
		str name;
		bool activeSelf = true;
		Vec3 localPosition{};
		Quat localRotation = Quat::Identity();
		Vec3 localScale{1.0f, 1.0f, 1.0f};
		Vector<CapturedComponent> components{GetMemoryResource(MemTag::Editor)};
		Vector<CapturedObject> children{GetMemoryResource(MemTag::Editor)};
	};

	SHALLTEAR_EDITOR_API void CaptureObjectSubtree(Handle<SceneObject> root, CapturedObject& out);
	SHALLTEAR_EDITOR_API Handle<SceneObject> ReviveObjectSubtree(
		const CapturedObject& data, Handle<Scene> scene, Handle<SceneObject> parent, u32 siblingIndex);

	class SHALLTEAR_EDITOR_API CreateObjectCommand final : public IEditorCommand {
	public:
		CreateObjectCommand(str name, Handle<Scene> scene, Handle<SceneObject> parent);
		void Apply() override;
		void Revert() override;
		str GetLabel() const override { return "Create " + _name; }
		Handle<Scene> GetAffectedScene() const override { return _scene; }
		Handle<SceneObject> GetCreated() const { return _created; }
	private:
		str _name;
		Handle<Scene> _scene;
		Handle<SceneObject> _parent;
		Handle<SceneObject> _created;
	};

	class SHALLTEAR_EDITOR_API DuplicateObjectCommand final : public IEditorCommand {
	public:
		DuplicateObjectCommand(Handle<SceneObject> source);
		void Apply() override;
		void Revert() override;
		str GetLabel() const override;
		Handle<Scene> GetAffectedScene() const override { return _scene; }
		Handle<SceneObject> GetCreated() const { return _created; }
	private:
		Handle<SceneObject> _source;
		Handle<Scene> _scene;
		Handle<SceneObject> _created;
	};

	class SHALLTEAR_EDITOR_API InstantiatePrefabCommand final : public IEditorCommand {
	public:
		InstantiatePrefabCommand(const Guid& prefabId, Handle<Scene> scene, Handle<SceneObject> parent);
		InstantiatePrefabCommand(const Guid& prefabId, Handle<Scene> scene, Handle<SceneObject> parent,
								 const Vec3& worldPosition);
		void Apply() override;
		void Revert() override;
		str GetLabel() const override;
		Handle<Scene> GetAffectedScene() const override { return _scene; }
		Handle<SceneObject> GetCreated() const { return _created; }
	private:
		Guid _prefabId;
		Handle<Scene> _scene;
		Handle<SceneObject> _parent;
		Handle<SceneObject> _created;
		Vec3 _worldPosition{};
		bool _placed = false;
	};

	class SHALLTEAR_EDITOR_API DestroyObjectCommand final : public IEditorCommand {
	public:
		DestroyObjectCommand(Handle<SceneObject> object);
		void Apply() override;
		void Revert() override;
		str GetLabel() const override { return "Delete " + _captured.name; }
		Handle<Scene> GetAffectedScene() const override { return _scene; }
	private:
		Handle<SceneObject> _target;
		Handle<Scene> _scene;
		Handle<SceneObject> _parent;
		u32 _siblingIndex = 0;
		CapturedObject _captured;
		bool _capturedOnce = false;
	};

	class SHALLTEAR_EDITOR_API ReparentCommand final : public IEditorCommand {
	public:
		ReparentCommand(Handle<SceneObject> object, Handle<SceneObject> newParent, u32 newSiblingIndex, bool worldStays = true);
		void Apply() override;
		void Revert() override;
		str GetLabel() const override { return "Reparent"; }
		Handle<Scene> GetAffectedScene() const override { return _scene; }
	private:
		void Move(Handle<SceneObject> parent, u32 siblingIndex);

		Handle<SceneObject> _object;
		Handle<Scene> _scene;
		Handle<SceneObject> _oldParent;
		Handle<SceneObject> _newParent;
		u32 _oldIndex = 0;
		u32 _newIndex = 0;
		bool _worldStays = true;
	};

	class SHALLTEAR_EDITOR_API SetSiblingIndexCommand final : public IEditorCommand {
	public:
		SetSiblingIndexCommand(Handle<SceneObject> object, u32 newIndex);
		void Apply() override;
		void Revert() override;
		str GetLabel() const override { return "Reorder"; }
		Handle<Scene> GetAffectedScene() const override { return _scene; }
	private:
		Handle<SceneObject> _object;
		Handle<Scene> _scene;
		u32 _oldIndex = 0;
		u32 _newIndex = 0;
	};

	class SHALLTEAR_EDITOR_API SetActiveCommand final : public IEditorCommand {
	public:
		SetActiveCommand(EditTarget object, bool active);
		void Apply() override;
		void Revert() override;
		str GetLabel() const override { return _active ? "Activate" : "Deactivate"; }
		Handle<Scene> GetAffectedScene() const override { return _scene; }
	private:
		EditTarget _object;
		Handle<Scene> _scene;
		bool _active = true;
		bool _wasActive = true;
	};

	class SHALLTEAR_EDITOR_API SetEnabledCommand final : public IEditorCommand {
	public:
		SetEnabledCommand(EditTarget component, bool enabled);
		void Apply() override;
		void Revert() override;
		str GetLabel() const override { return _enabled ? "Enable" : "Disable"; }
		Handle<Scene> GetAffectedScene() const override { return _scene; }
	private:
		EditTarget _component;
		Handle<Scene> _scene;
		bool _enabled = true;
		bool _wasEnabled = true;
	};

	class SHALLTEAR_EDITOR_API AddComponentCommand final : public IEditorCommand {
	public:
		AddComponentCommand(Handle<SceneObject> object, StringHash componentType, str stateJSON = {});
		void Apply() override;
		void Revert() override;
		str GetLabel() const override;
		Handle<Scene> GetAffectedScene() const override { return _scene; }
		Handle<Component> GetCreated() const { return _created; }
	private:
		Handle<SceneObject> _object;
		Handle<Scene> _scene;
		StringHash _typeName{};
		str _stateJSON;
		str _defaults;
		Handle<Component> _created;
	};

	class SHALLTEAR_EDITOR_API SetRawFieldCommand final : public IEditorCommand {
	public:
		SetRawFieldCommand(EditTarget target, StringHash field, u64 beforeBits, u64 afterBits, u32 byteSize, std::string_view label = {});
		void Apply() override;
		void Revert() override;
		str GetLabel() const override;
		Handle<Scene> GetAffectedScene() const override;
	private:
		void Write(u64 bits);

		EditTarget _target;
		StringHash _field{};
		u64 _before = 0;
		u64 _after = 0;
		u32 _byteSize = 0;
		str _label;
	};

	class SHALLTEAR_EDITOR_API SetContainerElementCommand final : public IEditorCommand {
	public:
		SetContainerElementCommand(EditTarget target, StringHash field, u32 index,
								   u64 beforeBits, u64 afterBits, u32 beforeCount, std::string_view label = {});
		void Apply() override;
		void Revert() override;
		str GetLabel() const override;
		Handle<Scene> GetAffectedScene() const override;
	private:
		void Write(u64 bits, u32 count);

		EditTarget _target;
		StringHash _field{};
		u32 _index = 0;
		u64 _before = 0;
		u64 _after = 0;
		u32 _beforeCount = 0;
		str _label;
	};

	class SHALLTEAR_EDITOR_API RemoveContainerElementCommand final : public IEditorCommand {
	public:
		RemoveContainerElementCommand(EditTarget target, StringHash field, u32 index, std::string_view label = {});
		void Apply() override;
		void Revert() override;
		str GetLabel() const override;
		Handle<Scene> GetAffectedScene() const override;
	private:
		EditTarget _target;
		StringHash _field{};
		u32 _index = 0;
		Vector<u64> _before{GetMemoryResource(MemTag::Editor)};
		str _label;
	};

	class SHALLTEAR_EDITOR_API SetFieldStateCommand final : public IEditorCommand {
	public:
		SetFieldStateCommand(EditTarget target, StringHash field, str beforeState, str afterState, std::string_view label = {});
		void Apply() override;
		void Revert() override;
		str GetLabel() const override;
		bool TryCoalesce(const IEditorCommand& next) override;
		const SetFieldStateCommand* AsFieldState() const override { return this; }
		Handle<Scene> GetAffectedScene() const override;
	private:
		void Write(const str& state);

		EditTarget _target;
		StringHash _field{};
		str _before;
		str _after;
		str _label;
	};

	SHALLTEAR_EDITOR_API str CaptureFieldState(Object& object, StringHash field);
	SHALLTEAR_EDITOR_API str CaptureFieldState(const void* instance, const TypeInfo* type, StringHash field);

	class SHALLTEAR_EDITOR_API ScopedLiveHandles {
	public:
		ScopedLiveHandles();
		~ScopedLiveHandles();
		ScopedLiveHandles(const ScopedLiveHandles&) = delete;
		ScopedLiveHandles& operator=(const ScopedLiveHandles&) = delete;
	};

	class SHALLTEAR_EDITOR_API RemoveComponentCommand final : public IEditorCommand {
	public:
		RemoveComponentCommand(Handle<Component> component);
		void Apply() override;
		void Revert() override;
		str GetLabel() const override;
		Handle<Scene> GetAffectedScene() const override { return _scene; }
	private:
		Handle<Component> _component;
		Handle<SceneObject> _owner;
		Handle<Scene> _scene;
		StringHash _typeName{};
		str _stateJSON;
		bool _capturedOnce = false;
	};

	class SHALLTEAR_EDITOR_API RevertRemovedComponentCommand final : public IEditorCommand {
	public:
		RevertRemovedComponentCommand(Handle<SceneObject> instanceRoot, Handle<SceneObject> owner,
									  const PrefabTargetPath& row, StringHash typeName);
		void Apply() override;
		void Revert() override;
		str GetLabel() const override;
		Handle<Scene> GetAffectedScene() const override { return _scene; }
		Handle<Component> GetRestored() const { return _restored; }
	private:
		Handle<SceneObject> _root;
		Handle<SceneObject> _owner;
		Handle<Scene> _scene;
		PrefabTargetPath _row;
		StringHash _typeName{};
		Handle<Component> _restored;
	};

	class SHALLTEAR_EDITOR_API SetComponentIndexCommand final : public IEditorCommand {
	public:
		SetComponentIndexCommand(Handle<Component> component, u32 newIndex);
		void Apply() override;
		void Revert() override;
		str GetLabel() const override { return "Reorder Component"; }
		Handle<Scene> GetAffectedScene() const override { return _scene; }
	private:
		Handle<Component> _component;
		Handle<SceneObject> _owner;
		Handle<Scene> _scene;
		u32 _oldIndex = 0;
		u32 _newIndex = 0;
	};

} // namespace Shalltear
