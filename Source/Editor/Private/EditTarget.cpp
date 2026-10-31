#include "Shalltear/Editor/EditTarget.h"

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"

#include "Shalltear/Editor/EditorAssetOps.h"
#include "Shalltear/Editor/EditorSceneOps.h"

namespace Shalltear {

	EditTarget EditTarget::PrefabRow(const Guid& prefabId, u64 rowKey) {
		EditTarget target;
		if (!prefabId.IsValid() || rowKey == 0) {
			return target;
		}
		target._prefabId = prefabId;
		target._rowKey = rowKey;
		return target;
	}

	Object* EditTarget::Resolve() const {
		if (_rowKey != 0) {
			return EditorSceneOps::ResolvePrefabRow(_prefabId, _rowKey);
		}
		return _liveId != 0 ? ObjectRegistry::Resolve(_liveId) : nullptr;
	}

	void EditTarget::NotifyWritten() const {
		if (_rowKey != 0) {
			EditorSceneOps::MarkPrefabPreviewEdited(_prefabId);
			return;
		}
		Object* object = Resolve();
		if (object && TypeRegistry::IsDerivedFrom(object->GetType(), Asset::GetReflectType())) {
			EditorAssetOps::MarkDirty(Handle<Asset>(static_cast<Asset*>(object)));
		}
	}

	bool EditTarget::operator==(const EditTarget& other) const {
		return _liveId == other._liveId && _rowKey == other._rowKey && _prefabId == other._prefabId;
	}

} // namespace Shalltear
