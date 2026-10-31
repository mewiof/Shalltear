#pragma once

#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Types.h"

#include <string_view>

namespace Shalltear {

	class SceneObject;
	class Behavior;
	class Asset;
	class SceneAsset;
	struct PrefabInstanceRecord;

	enum class SceneState : u8 {
		Loading,
		Loaded,
		Unloading,
	};

	enum class SceneKind : u8 {
		World = 0,
		Isolated,
	};

	class SHALLTEAR_CORE_API Scene : public Object {
	public:
		~Scene() override;

		SLT_REFLECT_CLASS(Scene)
		SLT_REFLECT_PARENT(Object)
		SLT_REFLECT_FIELD(name)
		SLT_END_REFLECT()

		str name;

		SceneState GetState() const { return _state; }
		SceneKind GetKind() const { return _kind; }
		Guid GetSourceGuid() const { return _sourceGuid; }
		u32 GetRootCount() const;
		Handle<SceneObject> GetRoot(u32 index) const;
		Handle<SceneObject> FindObjectByName(std::string_view objectName) const;
		u32 GetObjectCount() const;
		void ForEachObject(void (*visitor)(SceneObject&, void*), void* user) const;
	private:
		friend class SceneObject;
		friend class SceneSystem;
		friend struct SceneInternalAccess;

		Scene();

		Vector<SceneObject*> _roots{GetMemoryResource(MemTag::Scene)};
		Vector<SceneObject*> _objects{GetMemoryResource(MemTag::Scene)};
		Vector<Behavior*> _tickLists[3]{
			Vector<Behavior*>{GetMemoryResource(MemTag::Scene)},
			Vector<Behavior*>{GetMemoryResource(MemTag::Scene)},
			Vector<Behavior*>{GetMemoryResource(MemTag::Scene)}};
		Vector<PrefabInstanceRecord*> _instances{GetMemoryResource(MemTag::Scene)};
		Vector<Handle<Asset>> _retainedAssets{GetMemoryResource(MemTag::Scene)};
		u64 _nextLocalId = 1;
		Guid _sourceGuid{};
		Handle<SceneAsset> _sourceAsset;
		SceneState _state = SceneState::Loaded;
		SceneKind _kind = SceneKind::World;
		bool _activationAllowed = true;
	};

} // namespace Shalltear
