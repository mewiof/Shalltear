#include "Shalltear/Editor/EditorCommands.h"

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Reflection.h"
#include "Shalltear/Core/Scene/Scene.h"
#include "Shalltear/Core/Scene/SceneObject.h"
#include "Shalltear/Core/Scene/SceneSystem.h"
#include "Shalltear/Core/Serialization/HandleResolver.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"
#include "Shalltear/Editor/AuthoringDefaults.h"

#include <algorithm>
#include <cstring>
#include <format>

namespace Shalltear {

	namespace {

		str TypeNameOf(const TypeInfo* type) {
			if (type && type->displayName) {
				return type->displayName;
			}
			return type ? std::format("{:#x}", type->name.GetValue()) : str("?");
		}

		void ResyncEnabledAfterDeserialize(Component* component) {
			if (!component || component->IsEnabledSelf()) {
				return;
			}
			Variant live(true);
			SetField(component, component->GetType(), "_enabledSelf"_hash, &live);
			component->SetEnabled(false);
		}

	} // anonymous namespace

	void CaptureObjectSubtree(Handle<SceneObject> root, CapturedObject& out) {
		SceneObject* object = root.Get();
		if (!object) {
			return;
		}
		out.name = object->name;
		out.activeSelf = object->IsActiveSelf();
		out.localPosition = object->GetLocalPosition();
		out.localRotation = object->GetLocalRotation();
		out.localScale = object->GetLocalScale();

		for (u32 i = 0; i < object->GetComponentCount(); ++i) {
			Component* component = object->GetComponentAt(i).Get();
			if (!component || component->IsPendingDestroy()) {
				continue;
			}
			const TypeInfo* type = component->GetType();
			CapturedComponent captured;
			captured.typeName = type->name;
			captured.stateJSON = TextSerializer::Serialize(component, type);
			out.components.push_back(std::move(captured));
		}

		for (u32 i = 0; i < object->GetChildCount(); ++i) {
			Handle<SceneObject> child = object->GetChild(i);
			if (child.Get() && !child.Get()->IsPendingDestroy()) {
				CapturedObject childData;
				CaptureObjectSubtree(child, childData);
				out.children.push_back(std::move(childData));
			}
		}
	}

	Handle<SceneObject> ReviveObjectSubtree(
		const CapturedObject& data, Handle<Scene> scene, Handle<SceneObject> parent, u32 siblingIndex) {
		Handle<SceneObject> revived = SceneSystem::CreateObject(data.name, scene, parent);
		SceneObject* object = revived.Get();
		if (!object) {
			return {};
		}
		object->SetLocalPosition(data.localPosition);
		object->SetLocalRotation(data.localRotation);
		object->SetLocalScale(data.localScale);
		object->SetSiblingIndex(siblingIndex);

		for (const CapturedComponent& captured : data.components) {
			const TypeInfo* type = TypeRegistry::FindType(captured.typeName);
			if (!type) {
				SLT_LOG_WARN("Editor", "revive: component type {:#x} no longer exists. Dropped", captured.typeName.GetValue());
				continue;
			}
			Handle<Component> component = object->AddComponent(type);
			if (Component* resolved = component.Get()) {
				TextSerializer::Deserialize(resolved, type, captured.stateJSON);
				ResyncEnabledAfterDeserialize(resolved);
			}
		}

		for (u32 i = 0; i < data.children.size(); ++i) {
			ReviveObjectSubtree(data.children[i], scene, revived, i);
		}

		object->SetActive(data.activeSelf);
		return revived;
	}

	CreateObjectCommand::CreateObjectCommand(str name, Handle<Scene> scene, Handle<SceneObject> parent)
		: _name(std::move(name)), _scene(scene), _parent(parent) {}

	void CreateObjectCommand::Apply() {
		_created = SceneSystem::CreateObject(_name, _scene, _parent);
	}

	void CreateObjectCommand::Revert() {
		if (_created.IsValid()) {
			SceneSystem::Destroy(_created);
		}
	}

	namespace {

		str DuplicateName(const SceneObject& source) {
			std::string_view base = source.name;
			const size_t open = base.rfind(" (");
			if (open != std::string_view::npos && base.size() > open + 3 && base.back() == ')' &&
				std::all_of(base.begin() + static_cast<std::ptrdiff_t>(open) + 2, base.end() - 1,
							[](char c) { return c >= '0' && c <= '9'; })) {
				base = base.substr(0, open);
			}
			HashSet<str> taken{GetMemoryResource(MemTag::Editor)};
			auto note = [&taken](Handle<SceneObject> sibling) {
				if (SceneObject* object = sibling.Get()) {
					taken.insert(object->name);
				}
			};
			if (SceneObject* parent = source.GetParent().Get()) {
				for (u32 i = 0; i < parent->GetChildCount(); ++i) {
					note(parent->GetChild(i));
				}
			} else if (Scene* scene = source.GetScene().Get()) {
				for (u32 i = 0; i < scene->GetRootCount(); ++i) {
					note(scene->GetRoot(i));
				}
			}
			for (u32 n = 1;; ++n) {
				str candidate = std::format("{} ({})", base, n);
				if (!taken.contains(candidate)) {
					return candidate;
				}
			}
		}

	} // namespace

	DuplicateObjectCommand::DuplicateObjectCommand(Handle<SceneObject> source)
		: _source(source) {
		if (SceneObject* resolved = source.Get()) {
			_scene = resolved->GetScene();
		}
	}

	void DuplicateObjectCommand::Apply() {
		SceneObject* source = _source.Get();
		if (!source) {
			SLT_LOG_WARN("Editor", "duplicate: source no longer exists");
			return;
		}
		str name = DuplicateName(*source);
		_created = SceneSystem::Instantiate(_source, source->GetParent());
		if (SceneObject* clone = _created.Get()) {
			clone->name = std::move(name);
			clone->SetSiblingIndex(source->GetSiblingIndex() + 1);
		}
	}

	void DuplicateObjectCommand::Revert() {
		if (_created.IsValid()) {
			SceneSystem::Destroy(_created);
		}
	}

	str DuplicateObjectCommand::GetLabel() const {
		SceneObject* source = _source.Get();
		return "Duplicate " + (source ? source->name : str("Object"));
	}

	InstantiatePrefabCommand::InstantiatePrefabCommand(const Guid& prefabId, Handle<Scene> scene,
													   Handle<SceneObject> parent)
		: _prefabId(prefabId), _scene(scene), _parent(parent) {
	}

	InstantiatePrefabCommand::InstantiatePrefabCommand(const Guid& prefabId, Handle<Scene> scene,
													   Handle<SceneObject> parent, const Vec3& worldPosition)
		: _prefabId(prefabId), _scene(scene), _parent(parent), _worldPosition(worldPosition), _placed(true) {
	}

	void InstantiatePrefabCommand::Apply() {
		_created = SceneSystem::Instantiate(_prefabId, _scene, _parent);
		if (!_created.IsValid()) {
			SLT_LOG_WARN("Editor", "instantiate: prefab is unavailable");
			return;
		}
		if (_placed) {
			_created.Get()->SetWorldPosition(_worldPosition);
		}
	}

	void InstantiatePrefabCommand::Revert() {
		if (_created.IsValid()) {
			SceneSystem::Destroy(_created);
		}
	}

	str InstantiatePrefabCommand::GetLabel() const {
		str file = AssetDatabase::LookupPath(_prefabId).GetFilename();
		return "Instantiate " + (file.empty() ? str("Prefab") : file);
	}

	DestroyObjectCommand::DestroyObjectCommand(Handle<SceneObject> object)
		: _target(object) {
		if (SceneObject* resolved = object.Get()) {
			_scene = resolved->GetScene();
			_parent = resolved->GetParent();
			_siblingIndex = resolved->GetSiblingIndex();
		}
	}

	void DestroyObjectCommand::Apply() {
		if (!_capturedOnce) {
			CaptureObjectSubtree(_target, _captured);
			_capturedOnce = true;
		}
		if (_target.IsValid()) {
			SceneSystem::Destroy(_target);
		}
	}

	void DestroyObjectCommand::Revert() {
		_target = ReviveObjectSubtree(_captured, _scene, _parent, _siblingIndex);
	}

	ReparentCommand::ReparentCommand(Handle<SceneObject> object, Handle<SceneObject> newParent, u32 newSiblingIndex, bool worldStays)
		: _object(object), _newParent(newParent), _newIndex(newSiblingIndex), _worldStays(worldStays) {
		if (SceneObject* resolved = object.Get()) {
			_scene = resolved->GetScene();
			_oldParent = resolved->GetParent();
			_oldIndex = resolved->GetSiblingIndex();
		}
	}

	void ReparentCommand::Move(Handle<SceneObject> parent, u32 siblingIndex) {
		SceneObject* object = _object.Get();
		if (!object) {
			SLT_LOG_WARN("Editor", "reparent: object no longer exists");
			return;
		}
		object->SetParent(parent, _worldStays);
		object->SetSiblingIndex(siblingIndex);
	}

	void ReparentCommand::Apply() {
		Move(_newParent, _newIndex);
	}

	void ReparentCommand::Revert() {
		Move(_oldParent, _oldIndex);
	}

	SetSiblingIndexCommand::SetSiblingIndexCommand(Handle<SceneObject> object, u32 newIndex)
		: _object(object), _newIndex(newIndex) {
		if (SceneObject* resolved = object.Get()) {
			_scene = resolved->GetScene();
			_oldIndex = resolved->GetSiblingIndex();
		}
	}

	void SetSiblingIndexCommand::Apply() {
		if (SceneObject* object = _object.Get()) {
			object->SetSiblingIndex(_newIndex);
		}
	}

	void SetSiblingIndexCommand::Revert() {
		if (SceneObject* object = _object.Get()) {
			object->SetSiblingIndex(_oldIndex);
		}
	}

	namespace {

		SceneObject* AsSceneObject(const EditTarget& target) {
			Object* object = target.Resolve();
			return object && TypeRegistry::IsDerivedFrom(object->GetType(), SceneObject::GetReflectType())
					   ? static_cast<SceneObject*>(object)
					   : nullptr;
		}

		Component* AsComponent(const EditTarget& target) {
			Object* object = target.Resolve();
			return object && TypeRegistry::IsDerivedFrom(object->GetType(), Component::GetReflectType())
					   ? static_cast<Component*>(object)
					   : nullptr;
		}

	} // anonymous namespace

	SetActiveCommand::SetActiveCommand(EditTarget object, bool active)
		: _object(object), _active(active) {
		if (SceneObject* resolved = AsSceneObject(_object)) {
			_scene = resolved->GetScene();
			_wasActive = resolved->IsActiveSelf();
		}
	}

	void SetActiveCommand::Apply() {
		if (SceneObject* object = AsSceneObject(_object)) {
			object->SetActive(_active);
			_object.NotifyWritten();
		}
	}

	void SetActiveCommand::Revert() {
		if (SceneObject* object = AsSceneObject(_object)) {
			object->SetActive(_wasActive);
			_object.NotifyWritten();
		}
	}

	SetEnabledCommand::SetEnabledCommand(EditTarget component, bool enabled)
		: _component(component), _enabled(enabled) {
		if (Component* resolved = AsComponent(_component)) {
			_scene = resolved->GetScene();
			_wasEnabled = resolved->IsEnabledSelf();
		}
	}

	void SetEnabledCommand::Apply() {
		if (Component* component = AsComponent(_component)) {
			component->SetEnabled(_enabled);
			_component.NotifyWritten();
		}
	}

	void SetEnabledCommand::Revert() {
		if (Component* component = AsComponent(_component)) {
			component->SetEnabled(_wasEnabled);
			_component.NotifyWritten();
		}
	}

	AddComponentCommand::AddComponentCommand(Handle<SceneObject> object, StringHash componentType, str stateJSON)
		: _object(object), _typeName(componentType), _stateJSON(std::move(stateJSON)) {
		if (SceneObject* resolved = object.Get()) {
			_scene = resolved->GetScene();
		}
		if (_stateJSON.empty()) {
			_defaults = AuthoringDefaults::StateFor(TypeRegistry::FindType(componentType));
		}
	}

	void AddComponentCommand::Apply() {
		SceneObject* object = _object.Get();
		const TypeInfo* type = TypeRegistry::FindType(_typeName);
		if (!object || !type) {
			SLT_LOG_WARN("Editor", "add component: target or type no longer exists");
			return;
		}
		_created = object->AddComponent(type);
		Component* component = _created.Get();
		if (component && !_stateJSON.empty()) {
			TextSerializer::Deserialize(component, type, _stateJSON);
			ResyncEnabledAfterDeserialize(component);
		} else if (component && !_defaults.empty()) {
			AuthoringDefaults::Write(*component, _defaults);
		}
	}

	void AddComponentCommand::Revert() {
		if (_created.IsValid()) {
			SceneSystem::Destroy(_created);
		}
	}

	str AddComponentCommand::GetLabel() const {
		return "Add " + TypeNameOf(TypeRegistry::FindType(_typeName));
	}

	SetRawFieldCommand::SetRawFieldCommand(EditTarget target, StringHash field, u64 beforeBits, u64 afterBits, u32 byteSize, std::string_view label)
		: _target(target), _field(field), _before(beforeBits), _after(afterBits),
		  _byteSize(byteSize <= 8 ? byteSize : 8), _label(label) {}

	void SetRawFieldCommand::Write(u64 bits) {
		Object* object = _target.Resolve();
		if (!object) {
			SLT_LOG_WARN("Editor", "raw field command target no longer exists. Skipping");
			return;
		}
		const TypeInfo* type = object->GetType();
		const FieldInfo* field = FindField(type, _field);
		bool rawWritable = field &&
						   (field->flags & (FieldFlagTriviallyCopyable | FieldFlagHandle | FieldFlagEnum)) != 0;
		if (!field || !field->addrFn || !rawWritable || field->size > 8) {
			SLT_LOG_WARN("Editor", "raw field command: field {:#x} isn't raw-writable", _field.GetValue());
			return;
		}
		std::memcpy(field->addrFn(object), &bits, _byteSize < field->size ? _byteSize : static_cast<u32>(field->size));
		_target.NotifyWritten();
	}

	void SetRawFieldCommand::Apply() {
		Write(_after);
	}

	void SetRawFieldCommand::Revert() {
		Write(_before);
	}

	str SetRawFieldCommand::GetLabel() const {
		return _label.empty() ? str("Edit Field") : _label;
	}

	Handle<Scene> SetRawFieldCommand::GetAffectedScene() const {
		Object* object = _target.Resolve();
		if (!object) {
			return {};
		}
		const TypeInfo* type = object->GetType();
		if (TypeRegistry::IsDerivedFrom(type, SceneObject::GetReflectType())) {
			return static_cast<SceneObject*>(object)->GetScene();
		}
		if (TypeRegistry::IsDerivedFrom(type, Component::GetReflectType())) {
			return static_cast<Component*>(object)->GetScene();
		}
		return {};
	}

	SetContainerElementCommand::SetContainerElementCommand(EditTarget target, StringHash field, u32 index,
														   u64 beforeBits, u64 afterBits, u32 beforeCount, std::string_view label)
		: _target(target), _field(field), _index(index), _before(beforeBits), _after(afterBits),
		  _beforeCount(beforeCount), _label(label) {}

	void SetContainerElementCommand::Write(u64 bits, u32 count) {
		Object* object = _target.Resolve();
		if (!object) {
			SLT_LOG_WARN("Editor", "container element command target no longer exists. Skipping");
			return;
		}
		const FieldInfo* field = FindField(object->GetType(), _field);
		if (!field || !field->addrFn || !(field->flags & FieldFlagContainer) || !field->container) {
			SLT_LOG_WARN("Editor", "container element command: field {:#x} isn't a container", _field.GetValue());
			return;
		}
		const ContainerInfo* container = field->container;
		if (!container->resizeFn || !container->atFn || container->elementSize > 8) {
			return;
		}

		void* containerPtr = field->addrFn(object);
		u32 required = _index + 1 > count ? _index + 1 : count;
		container->resizeFn(containerPtr, required);
		std::memcpy(container->atFn(containerPtr, _index), &bits, container->elementSize);
		if (count < required) {
			container->resizeFn(containerPtr, count);
		}
		_target.NotifyWritten();
	}

	void SetContainerElementCommand::Apply() {
		Write(_after, _index + 1 > _beforeCount ? _index + 1 : _beforeCount);
	}

	void SetContainerElementCommand::Revert() {
		Write(_before, _beforeCount);
	}

	str SetContainerElementCommand::GetLabel() const {
		return _label.empty() ? str("Edit Element") : _label;
	}

	Handle<Scene> SetContainerElementCommand::GetAffectedScene() const {
		Object* object = _target.Resolve();
		if (!object) {
			return {};
		}
		const TypeInfo* type = object->GetType();
		if (TypeRegistry::IsDerivedFrom(type, SceneObject::GetReflectType())) {
			return static_cast<SceneObject*>(object)->GetScene();
		}
		if (TypeRegistry::IsDerivedFrom(type, Component::GetReflectType())) {
			return static_cast<Component*>(object)->GetScene();
		}
		return {};
	}

	namespace {

		const ContainerInfo* ResolveContainer(EditTarget& target, StringHash field, void** outPtr, u32* outCount) {
			Object* object = target.Resolve();
			if (!object) {
				return nullptr;
			}
			const FieldInfo* info = FindField(object->GetType(), field);
			if (!info || !info->addrFn || !(info->flags & FieldFlagContainer) || !info->container) {
				return nullptr;
			}
			const ContainerInfo* container = info->container;
			if (!container->resizeFn || !container->atFn || !container->sizeFn || container->elementSize > 8) {
				return nullptr;
			}
			*outPtr = info->addrFn(object);
			*outCount = static_cast<u32>(container->sizeFn(*outPtr));
			return container;
		}

	} // anonymous namespace

	RemoveContainerElementCommand::RemoveContainerElementCommand(EditTarget target, StringHash field, u32 index,
																 std::string_view label)
		: _target(target), _field(field), _index(index), _label(label) {
		void* containerPtr = nullptr;
		u32 count = 0;
		const ContainerInfo* container = ResolveContainer(_target, _field, &containerPtr, &count);
		if (!container) {
			return;
		}
		_before.reserve(count);
		for (u32 element = 0; element < count; ++element) {
			u64 bits = 0;
			std::memcpy(&bits, container->atFn(containerPtr, element), container->elementSize);
			_before.push_back(bits);
		}
	}

	void RemoveContainerElementCommand::Apply() {
		void* containerPtr = nullptr;
		u32 count = 0;
		const ContainerInfo* container = ResolveContainer(_target, _field, &containerPtr, &count);
		if (!container || _index >= count) {
			SLT_LOG_WARN("Editor", "remove element command: index {} is past a container of {}", _index, count);
			return;
		}
		for (u32 element = _index; element + 1 < count; ++element) {
			u64 bits = 0;
			std::memcpy(&bits, container->atFn(containerPtr, element + 1), container->elementSize);
			std::memcpy(container->atFn(containerPtr, element), &bits, container->elementSize);
		}
		container->resizeFn(containerPtr, count - 1);
		_target.NotifyWritten();
	}

	void RemoveContainerElementCommand::Revert() {
		void* containerPtr = nullptr;
		u32 count = 0;
		const ContainerInfo* container = ResolveContainer(_target, _field, &containerPtr, &count);
		if (!container) {
			return;
		}
		container->resizeFn(containerPtr, _before.size());
		for (size_t element = 0; element < _before.size(); ++element) {
			std::memcpy(container->atFn(containerPtr, element), &_before[element], container->elementSize);
		}
		_target.NotifyWritten();
	}

	str RemoveContainerElementCommand::GetLabel() const {
		return _label.empty() ? str("Remove Element") : _label;
	}

	Handle<Scene> RemoveContainerElementCommand::GetAffectedScene() const {
		Object* object = _target.Resolve();
		if (!object) {
			return {};
		}
		const TypeInfo* type = object->GetType();
		if (TypeRegistry::IsDerivedFrom(type, SceneObject::GetReflectType())) {
			return static_cast<SceneObject*>(object)->GetScene();
		}
		if (TypeRegistry::IsDerivedFrom(type, Component::GetReflectType())) {
			return static_cast<Component*>(object)->GetScene();
		}
		return {};
	}

	namespace {

		bool LiveToGuid(void*, u64 instanceId, Guid* outGuid, StringHash* outPointeeType) {
			Object* object = ObjectRegistry::Resolve(instanceId);
			if (!object) {
				return false;
			}
			if (TypeRegistry::IsDerivedFrom(object->GetType(), Asset::GetReflectType()) &&
				static_cast<Asset*>(object)->assetId.IsValid()) {
				return false;
			}
			*outGuid = Guid{instanceId, SLT_LOCAL_SENTINEL};
			*outPointeeType = object->GetType()->name;
			return true;
		}

		bool LiveToHandle(void*, const Guid& guid, StringHash, u64* outId) {
			if (guid.high != SLT_LOCAL_SENTINEL) {
				return false;
			}
			*outId = guid.low;
			return true;
		}

		constexpr HandleResolverScope LIVE_SCOPE{&LiveToGuid, &LiveToHandle, nullptr};

		bool KeyNames(std::string_view key, const FieldInfo& field) {
			return StringHash(key) == field.name || key == std::format("{:016x}", field.name.GetValue());
		}

	} // anonymous namespace

	ScopedLiveHandles::ScopedLiveHandles() {
		HandleResolver::PushScope(LIVE_SCOPE);
	}

	ScopedLiveHandles::~ScopedLiveHandles() {
		HandleResolver::PopScope();
	}

	str CaptureFieldState(Object& object, StringHash field) {
		return CaptureFieldState(&object, object.GetType(), field);
	}

	str CaptureFieldState(const void* instance, const TypeInfo* type, StringHash field) {
		const FieldInfo* info = instance && type ? FindField(type, field) : nullptr;
		if (!info || (info->flags & FieldFlagTransient)) {
			return {};
		}
		str whole;
		{
			ScopedLiveHandles live;
			whole = TextSerializer::Serialize(instance, type);
		}
		LinearAllocator arena(whole.size() + 64, MemTag::Editor);
		JSONValue document = JSONParse(whole, arena);
		for (size_t i = 0; i < document.MemberCount(); ++i) {
			if (!KeyNames(document.KeyAt(i), *info)) {
				continue;
			}
			JSONValue state = JSONValue::MakeObject();
			state.AddMember(document.KeyAt(i), document.ChildAt(i));
			return JSONSerialize(state, false);
		}
		return {};
	}

	SetFieldStateCommand::SetFieldStateCommand(EditTarget target, StringHash field, str beforeState, str afterState, std::string_view label)
		: _target(target), _field(field), _before(std::move(beforeState)), _after(std::move(afterState)), _label(label) {}

	void SetFieldStateCommand::Write(const str& state) {
		Object* object = _target.Resolve();
		if (!object) {
			SLT_LOG_WARN("Editor", "field state command target no longer exists. Skipping");
			return;
		}
		if (state.empty() || !FindField(object->GetType(), _field)) {
			SLT_LOG_WARN("Editor", "field state command: field {:#x} isn't on its target", _field.GetValue());
			return;
		}
		{
			ScopedLiveHandles live;
			TextSerializer::Deserialize(object, object->GetType(), state);
		}
		_target.NotifyWritten();
	}

	void SetFieldStateCommand::Apply() {
		Write(_after);
	}

	void SetFieldStateCommand::Revert() {
		Write(_before);
	}

	str SetFieldStateCommand::GetLabel() const {
		return _label.empty() ? str("Edit Property") : _label;
	}

	bool SetFieldStateCommand::TryCoalesce(const IEditorCommand& next) {
		const SetFieldStateCommand* incoming = next.AsFieldState();
		if (!incoming || !(incoming->_target == _target) || incoming->_field != _field) {
			return false;
		}
		_after = incoming->_after;
		return true;
	}

	Handle<Scene> SetFieldStateCommand::GetAffectedScene() const {
		Object* object = _target.Resolve();
		if (!object) {
			return {};
		}
		const TypeInfo* type = object->GetType();
		if (TypeRegistry::IsDerivedFrom(type, SceneObject::GetReflectType())) {
			return static_cast<SceneObject*>(object)->GetScene();
		}
		if (TypeRegistry::IsDerivedFrom(type, Component::GetReflectType())) {
			return static_cast<Component*>(object)->GetScene();
		}
		return {};
	}

	RemoveComponentCommand::RemoveComponentCommand(Handle<Component> component)
		: _component(component) {
		if (Component* resolved = component.Get()) {
			_scene = resolved->GetScene();
			_owner = resolved->GetOwner();
			_typeName = resolved->GetType()->name;
		}
	}

	void RemoveComponentCommand::Apply() {
		Component* component = _component.Get();
		if (component && !_capturedOnce) {
			_stateJSON = TextSerializer::Serialize(component, component->GetType());
			_capturedOnce = true;
		}
		if (component) {
			SceneSystem::Destroy(_component);
		}
	}

	void RemoveComponentCommand::Revert() {
		SceneObject* owner = _owner.Get();
		const TypeInfo* type = TypeRegistry::FindType(_typeName);
		if (!owner || !type) {
			SLT_LOG_WARN("Editor", "remove component undo: owner or type no longer exists");
			return;
		}
		_component = owner->AddComponent(type);
		if (Component* revived = _component.Get()) {
			TextSerializer::Deserialize(revived, type, _stateJSON);
			ResyncEnabledAfterDeserialize(revived);
		}
	}

	str RemoveComponentCommand::GetLabel() const {
		return "Remove " + TypeNameOf(TypeRegistry::FindType(_typeName));
	}

	RevertRemovedComponentCommand::RevertRemovedComponentCommand(Handle<SceneObject> instanceRoot, Handle<SceneObject> owner,
																 const PrefabTargetPath& row, StringHash typeName)
		: _root(instanceRoot), _owner(owner), _typeName(typeName) {
		_row.leaf = row.leaf;
		for (const PrefabFileRef& hop : row.hops) {
			_row.hops.push_back(hop);
		}
		if (SceneObject* resolved = instanceRoot.Get()) {
			_scene = resolved->GetScene();
		}
	}

	void RevertRemovedComponentCommand::Apply() {
		const PrefabTargetPath rows[] = {_row};
		if (!SceneSystem::RevertInstanceOverrides(_root, rows)) {
			SLT_LOG_WARN("Editor", "revert removed component: the instance root is gone or isn't one");
			return;
		}
		_restored = {};
		SceneObject* owner = _owner.Get();
		for (u32 i = 0; owner && i < owner->GetComponentCount(); ++i) {
			Handle<Component> candidate = owner->GetComponentAt(i);
			Component* resolved = candidate.Get();
			if (resolved && !resolved->IsPendingDestroy() && resolved->GetLocalId() == _row.leaf.localId &&
				resolved->GetType()->name == _typeName) {
				_restored = candidate;
				break;
			}
		}
	}

	void RevertRemovedComponentCommand::Revert() {
		if (_restored.IsValid()) {
			SceneSystem::Destroy(_restored);
		}
	}

	str RevertRemovedComponentCommand::GetLabel() const {
		return "Revert Removed " + TypeNameOf(TypeRegistry::FindType(_typeName));
	}

	SetComponentIndexCommand::SetComponentIndexCommand(Handle<Component> component, u32 newIndex)
		: _component(component), _newIndex(newIndex) {
		Component* resolved = component.Get();
		if (!resolved) {
			return;
		}
		_owner = resolved->GetOwner();
		if (SceneObject* owner = _owner.Get()) {
			_scene = owner->GetScene();
			for (u32 i = 0; i < owner->GetComponentCount(); ++i) {
				if (owner->GetComponentAt(i) == component) {
					_oldIndex = i;
					break;
				}
			}
		}
	}

	void SetComponentIndexCommand::Apply() {
		if (SceneObject* owner = _owner.Get()) {
			owner->SetComponentIndex(_component, _newIndex);
		}
	}

	void SetComponentIndexCommand::Revert() {
		if (SceneObject* owner = _owner.Get()) {
			owner->SetComponentIndex(_component, _oldIndex);
		}
	}

} // namespace Shalltear
