#include "Shalltear/Editor/EditorEvents.h"

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Object.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"

namespace Shalltear {

	EditorPingTarget PingTargetForInstance(u64 instanceId) {
		EditorPingTarget target;
		Object* pointee = instanceId != 0 ? ObjectRegistry::Resolve(instanceId) : nullptr;
		if (!pointee) {
			return target;
		}
		const TypeInfo* assetBase = TypeRegistry::FindType(StringHash("Asset"));
		if (assetBase && TypeRegistry::IsDerivedFrom(pointee->GetType(), assetBase)) {
			target.asset = static_cast<Asset*>(pointee)->assetId;
		} else {
			target.object = instanceId;
		}
		return target;
	}

} // namespace Shalltear
