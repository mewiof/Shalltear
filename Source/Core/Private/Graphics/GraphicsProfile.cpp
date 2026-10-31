#include "Shalltear/Core/Graphics/GraphicsProfile.h"

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"

SLT_REGISTER_TYPE(Shalltear::GraphicsProfile)

namespace Shalltear {

	bool GraphicsProfile::Import(const Path& sourcePath) {
		if (!FileSystem::Exists(sourcePath)) {
			SLT_LOG_ERROR("Asset", "GraphicsProfile::Import: source '{}' doesn't exist", sourcePath.ToString());
			return false;
		}
		return TextSerializer::Deserialize(this, GetReflectType(), FileSystem::ReadTextFile(sourcePath, MemTag::Asset));
	}

	bool GraphicsProfile::Export(const Path& destPath) const {
		return FileSystem::WriteTextFile(destPath, TextSerializer::Serialize(this, GetReflectType()));
	}

	Asset* GraphicsProfileImporter::Import(const Path& sourcePath) {
		GraphicsProfile* asset = New<GraphicsProfile>(MemTag::Asset);
		if (!asset->Import(sourcePath)) {
			Delete(asset);
			return nullptr;
		}
		return asset;
	}

	Asset* GraphicsProfileImporter::CreateAsset() {
		return New<GraphicsProfile>(MemTag::Asset);
	}

} // namespace Shalltear
