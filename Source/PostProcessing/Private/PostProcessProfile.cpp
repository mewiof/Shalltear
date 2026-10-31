#include "Shalltear/PostProcessing/PostProcessProfile.h"

#include "Shalltear/Core/Asset/AssetImporter.h"

namespace Shalltear {

	PostProcessProfile::~PostProcessProfile() = default;

	void RegisterPostProcessProfileImporter(const void* owner) {
		AssetImporterRegistry::RegisterDeclared(PostProcessProfile::GetReflectType(), owner);
	}

} // namespace Shalltear
