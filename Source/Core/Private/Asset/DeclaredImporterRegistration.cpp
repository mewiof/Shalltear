#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/Asset/DataAsset.h"

#include <cstdint>

namespace Shalltear {

	DeclaredAssetImporter* DeclaredAssetImporter::FindDeclared(const TypeInfo* type) {
		const AttributeInfo* attribute = type ? FindTypeAttribute(type, StringHash("DeclaredImporter")) : nullptr;
		if (!attribute || !attribute->value.Is<u64>()) {
			return nullptr;
		}
		return reinterpret_cast<DeclaredAssetImporter*>(static_cast<std::uintptr_t>(attribute->value.As<u64>()));
	}

	bool AssetImporterRegistry::RegisterDeclared(const TypeInfo* type, const void* owner) {
		DeclaredAssetImporter* importer = DeclaredAssetImporter::FindDeclared(type);
		if (!importer) {
			return false;
		}

		IAssetImporter* owning = Find(type->name);
		if (owning && owning != importer) {
			return false;
		}
		IAssetImporter* byExtension = FindByExtension(importer->GetExtension());
		if (byExtension && byExtension != importer) {
			return false;
		}
		Register(type->name, importer, {importer->GetExtension()}, owner);
		return true;
	}

} // namespace Shalltear
