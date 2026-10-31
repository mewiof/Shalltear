#pragma once

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/Handle.h"
#include "Shalltear/Core/Reflection/Variant.h"
#include "Shalltear/Core/StringHash.h"
#include "Shalltear/Core/Types.h"

namespace Shalltear {

	class SceneObject;
	class Component;
	struct SceneDocument;

	struct PrefabFileRef {
		Guid file{};
		u64 localId = 0;

		bool operator==(const PrefabFileRef& o) const { return file == o.file && localId == o.localId; }
	};

	struct PrefabTargetPath {
		Vector<PrefabFileRef> hops{GetMemoryResource(MemTag::Scene)};
		PrefabFileRef leaf{};
	};

	SHALLTEAR_CORE_API u64 FoldRef(u64 seed, const Guid& file, u64 localId);

	struct OverrideValue {
		Variant direct;
		Guid handleGuid{};
		StringHash handlePointee;
		bool isHandle = false;
		Handle<Object> liveTarget;
	};

	struct PropertyOverrideOp {
		PrefabTargetPath target;
		Vector<StringHash> fieldPath{GetMemoryResource(MemTag::Scene)};
		OverrideValue value;
	};

	struct AddedObjectOp {
		PrefabTargetPath parent;
		u32 insertIndex = 0;
		u64 rootRowId = 0;
		Handle<SceneObject> liveRoot;
	};

	struct RemovedObjectOp {
		PrefabTargetPath target;
	};

	struct AddedComponentOp {
		PrefabTargetPath owner;
		u64 componentRowId = 0;
		Handle<Component> liveComponent;
	};

	struct RemovedComponentOp {
		PrefabTargetPath target;
	};

	struct OverrideLayer {
		Vector<PropertyOverrideOp> properties{GetMemoryResource(MemTag::Scene)};
		Vector<AddedObjectOp> addedObjects{GetMemoryResource(MemTag::Scene)};
		Vector<RemovedObjectOp> removedObjects{GetMemoryResource(MemTag::Scene)};
		Vector<AddedComponentOp> addedComponents{GetMemoryResource(MemTag::Scene)};
		Vector<RemovedComponentOp> removedComponents{GetMemoryResource(MemTag::Scene)};

		bool IsEmpty() const {
			return properties.empty() && addedObjects.empty() && removedObjects.empty() &&
				   addedComponents.empty() && removedComponents.empty();
		}
	};

	enum class PrefabKind : u8 {
		Regular = 0,
		Model,
	};

	class SHALLTEAR_CORE_API PrefabAsset : public Asset {
	public:
		SLT_REFLECT_CLASS(PrefabAsset)
		SLT_REFLECT_PARENT(Asset)
		SLT_REFLECT_CREATABLE()
		SLT_END_REFLECT()

		PrefabAsset();
		~PrefabAsset() override;

		bool Import(const Path& sourcePath) override;
		bool Export(const Path& destPath) const override;
		StringHash GetAssetTypeId() const override { return PrefabAsset::GetReflectType()->name; }

		bool Cook(CookWriter& descriptor, CookWriter& payload) const override;
		bool LoadCooked(CookReader& descriptor, CookReader& payload) override;

		bool CookImportState(CookWriter& importState) const override;

		Guid GetBasePrefab() const;
		const OverrideLayer& GetOverrides() const;
		const SceneDocument* GetFragment() const { return _fragment; }
		PrefabKind GetKind() const { return _kind; }
	protected:
		void OnLoad() override;
	private:
		friend class SceneSystem;
		friend struct SceneInternalAccess;

		SceneDocument* _fragment = nullptr;
		PrefabKind _kind = PrefabKind::Regular;
	};

	struct PrefabInstanceRecord {
		Guid prefabGuid{};
		Vector<Guid> chainGuids{GetMemoryResource(MemTag::Scene)};
		OverrideLayer liveOverrides;
		HashMap<u64, Object*> correspondence{GetMemoryResource(MemTag::Scene)};
		HashMap<u64, PrefabTargetPath> keyPaths{GetMemoryResource(MemTag::Scene)};
		HashMap<u64, u64> keyTypes{GetMemoryResource(MemTag::Scene)};
		Handle<SceneObject> root;
		PrefabAsset* asset = nullptr;
		u64 sceneRowId = 0;
	};

	struct PrefabInstanceDiff {
		struct Property {
			Handle<Object> target;
			PrefabTargetPath row;
			Vector<StringHash> fieldPath{GetMemoryResource(MemTag::Scene)};
			Variant base;
			u64 baseHandleId = 0;
			bool isHandle = false;
		};
		struct RemovedComponent {
			Handle<SceneObject> owner;
			PrefabTargetPath row;
			StringHash type;
			u32 index = 0;
		};
		struct RemovedObject {
			Handle<SceneObject> parent;
			PrefabTargetPath row;
			str name;
		};

		Vector<Property> properties{GetMemoryResource(MemTag::Scene)};
		Vector<Handle<Component>> addedComponents{GetMemoryResource(MemTag::Scene)};
		Vector<RemovedComponent> removedComponents{GetMemoryResource(MemTag::Scene)};
		Vector<Handle<SceneObject>> addedObjects{GetMemoryResource(MemTag::Scene)};
		Vector<RemovedObject> removedObjects{GetMemoryResource(MemTag::Scene)};

		bool IsEmpty() const {
			return properties.empty() && addedComponents.empty() && removedComponents.empty() &&
				   addedObjects.empty() && removedObjects.empty();
		}
	};

} // namespace Shalltear
