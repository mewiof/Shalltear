#pragma once

#include "Shalltear/Core/Asset/Asset.h"
#include "Shalltear/Core/Containers.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Types.h"

#include "Asset/AssetSource.h"

#include <mutex>
#include <vector>

namespace Shalltear {

	struct AssetDatabaseData {
		HashMap<Guid, Asset*> guidToAsset{GetMemoryResource(MemTag::Asset)};

		HashMap<u64, Guid> instanceToGuid{GetMemoryResource(MemTag::Asset)};

		HashMap<str, Guid> pathToGuid{GetMemoryResource(MemTag::Asset)};
		HashMap<Guid, str> guidToPath{GetMemoryResource(MemTag::Asset)};

		struct ProductIndexEntry {
			Guid source;
			str key;
			str name;
		};
		HashMap<Guid, ProductIndexEntry> productIndex{GetMemoryResource(MemTag::Asset)};

		Vector<str> importsInFlight{GetMemoryResource(MemTag::Asset)};

		HashSet<Asset*> parked{GetMemoryResource(MemTag::Asset)};

		struct UnresolvedAsset {
			u64 typeNameHash = 0;
			int refCount = 0;
			AssetLoadState loadState = AssetLoadState::Unloaded;
			bool wasParked = false;
			std::vector<u8> blob;
			std::vector<Guid> bound;
		};
		HashMap<Guid, UnresolvedAsset> unresolved{GetMemoryResource(MemTag::Asset)};

		// lock order: data.mutex -> GenerateGuid's mutex/the registries
		std::recursive_mutex mutex;
		bool initialized = false;

		PackageAssetSource package;
	};

	namespace AssetInternal {

		inline AssetDatabaseData* _boundData = nullptr;

		inline AssetDatabaseData& GetData() noexcept {
			if (_boundData)
				return *_boundData;
			static AssetDatabaseData data;
			return data;
		}

	} // namespace AssetInternal
} // namespace Shalltear
