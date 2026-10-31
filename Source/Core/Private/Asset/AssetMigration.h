#pragma once

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Types.h"

#include <vector>

namespace Shalltear::AssetInternal {

	struct AssetMigrationEnvelope {
		Guid assetId;
		u64 instanceId = 0;
		u64 typeNameHash = 0;
		int refCount = 0;
		AssetLoadState loadState = AssetLoadState::Unloaded;
		bool wasParked = false;
		std::vector<u8> blob;

		std::vector<Guid> bound;
	};

	struct AssetMigrationSnapshot {
		Vector<AssetMigrationEnvelope> envelopes{GetMemoryResource(MemTag::Asset)};
	};

	// capture -> demolition while the image is still mapped
	void CaptureAndDemolishModuleAssets(const void* moduleImage, AssetMigrationSnapshot& out);

	void RestoreModuleAssets(AssetMigrationSnapshot& snapshot);

	u32 RebuildPreservedAssets();

} // namespace Shalltear::AssetInternal
