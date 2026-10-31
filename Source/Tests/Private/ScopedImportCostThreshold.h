#pragma once

#include "Asset/ImportArtifactStore.h"

#include <cstdint>

namespace Shalltear {

	class ScopedImportCostThreshold {
	public:
		static constexpr u64 CacheEverything = 0;
		static constexpr u64 CacheNothing = UINT64_MAX;

		explicit ScopedImportCostThreshold(u64 micros)
			: _previous(ImportArtifactStore::GetCostThresholdMicros()) {
			ImportArtifactStore::SetCostThresholdMicros(micros);
		}

		~ScopedImportCostThreshold() { ImportArtifactStore::SetCostThresholdMicros(_previous); }

		ScopedImportCostThreshold(const ScopedImportCostThreshold&) = delete;
		ScopedImportCostThreshold& operator=(const ScopedImportCostThreshold&) = delete;
	private:
		u64 _previous;
	};

} // namespace Shalltear
