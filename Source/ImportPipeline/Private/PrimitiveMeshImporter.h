#pragma once

#include "Shalltear/Core/Asset/AssetImporter.h"

namespace Shalltear {

	class PrimitiveMeshImporter : public IAssetImporter {
	public:
		StringHash GetAssetType() const override { return StringHash("MeshAsset"); }
		// bump when the generated geometry changes
		u32 GetImporterVersion() const override { return 2; }
		Asset* Import(const Path& sourcePath) override;
	};

} // namespace Shalltear
