#pragma once

#include "Shalltear/Core/CoreAPI.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Types.h"

#include <span>
#include <string_view>

namespace Shalltear {

	class Asset;
	struct FileAccessRecording;

	struct ImportArtifactRequest {
		Guid guid;
		const str* virtualKey = nullptr;
		Path physSource;
		u64 settingsHash = 0;
		u32 importerVersion = 1;
	};

	struct ImportArtifactProduct {
		std::string_view key;
		const Asset* asset = nullptr;
	};

	enum class ImportArtifactProductResult {
		Miss,
		Loaded,
		NotProduced,
	};

	namespace ImportArtifactStore {

		SHALLTEAR_CORE_API u64 GetCostThresholdMicros();
		SHALLTEAR_CORE_API void SetCostThresholdMicros(u64 micros);

		Asset* TryLoad(const ImportArtifactRequest& request);

		ImportArtifactProductResult TryLoadProduct(const ImportArtifactRequest& request, std::string_view key,
												   const Guid& productGuid, const str& displayPath, Asset*& out);

		void Fill(const ImportArtifactRequest& request, const Asset& asset, std::span<const ImportArtifactProduct> products,
				  const FileAccessRecording& accesses, u64 importMicros);

	} // namespace ImportArtifactStore

} // namespace Shalltear
