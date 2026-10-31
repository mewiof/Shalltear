#include "Shalltear/Core/Asset/TextAsset.h"

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"

namespace Shalltear {

	TextAsset::~TextAsset() = default;

	bool TextAsset::Import(const Path& sourcePath) {
		if (!FileSystem::Exists(sourcePath)) {
			SLT_LOG_ERROR("Asset", "TextAsset::Import: source '{}' doesn't exist", sourcePath.ToString());
			return false;
		}

		content = FileSystem::ReadTextFile(sourcePath, MemTag::Asset);
		return true;
	}

	bool TextAsset::Export(const Path& destPath) const {
		return FileSystem::WriteTextFile(destPath, content);
	}

	Asset* TextAssetImporter::Import(const Path& sourcePath) {
		TextAsset* asset = New<TextAsset>(MemTag::Asset);
		if (!asset->Import(sourcePath)) {
			Delete(asset);
			return nullptr;
		}
		return asset;
	}

	Asset* TextAssetImporter::CreateAsset() {
		return New<TextAsset>(MemTag::Asset);
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::TextAsset)
