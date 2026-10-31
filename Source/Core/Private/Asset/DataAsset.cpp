#include "Shalltear/Core/Asset/DataAsset.h"

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"

#include <cstdint>

namespace Shalltear {

	DataAsset::~DataAsset() = default;

	StringHash DataAsset::GetAssetTypeId() const {
		return GetType()->name;
	}

	bool DataAsset::Import(const Path& sourcePath) {
		return TextSerializer::Deserialize(this, GetType(), FileSystem::ReadTextFile(sourcePath, MemTag::Asset));
	}

	bool DataAsset::Export(const Path& destPath) const {
		return FileSystem::WriteTextFile(destPath, TextSerializer::Serialize(this, GetType()));
	}

	DeclaredAssetImporter::DeclaredAssetImporter(TypeFn type, CreateFn create, std::string_view menuLabel,
												 std::string_view extension, std::string_view defaultFileName) noexcept
		: _type(type), _create(create), _menuLabel(menuLabel), _extension(extension), _defaultFileName(defaultFileName) {}

	StringHash DeclaredAssetImporter::GetAssetType() const {
		return _type()->name;
	}

	Asset* DeclaredAssetImporter::Import(const Path& sourcePath) {
		Asset* asset = _create();
		if (!asset->Import(sourcePath)) {
			Delete(asset);
			return nullptr;
		}
		return asset;
	}

	void DeclaredAssetImporter::Declare(ReflectBuilder& builder, DeclaredAssetImporter* importer) {
		builder.AddAttribute(StringHash("DeclaredImporter"), Variant(static_cast<u64>(reinterpret_cast<std::uintptr_t>(importer))));
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::DataAsset)
