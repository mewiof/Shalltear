#include "Shalltear/Audio/AudioContainer.h"

#include "AudioComposite.h"

#include "Shalltear/Core/Asset/AssetImporter.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"

namespace Shalltear {

	bool AudioContainer::Import(const Path& sourcePath) {
		return TextSerializer::Deserialize(this, GetType(), FileSystem::ReadTextFile(sourcePath, MemTag::Asset));
	}

	bool AudioContainer::Export(const Path& destPath) const {
		return FileSystem::WriteTextFile(destPath, TextSerializer::Serialize(this, GetType()));
	}

	namespace AudioImpl {

		void RegisterContainerImporters(const void* owner) {
			AssetImporterRegistry::RegisterDeclared(AudioRandomContainer::GetReflectType(), owner);
			AssetImporterRegistry::RegisterDeclared(AudioLayeredContainer::GetReflectType(), owner);
			AssetImporterRegistry::RegisterDeclared(AudioInteractiveContainer::GetReflectType(), owner);
		}

	} // namespace AudioImpl

} // namespace Shalltear
