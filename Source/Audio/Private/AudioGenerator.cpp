#include "Shalltear/Audio/AudioGenerator.h"

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/TextSerializer.h"

namespace Shalltear {

	bool AudioGenerator::Import(const Path& sourcePath) {
		return TextSerializer::Deserialize(this, GetType(), FileSystem::ReadTextFile(sourcePath, MemTag::Asset));
	}

	bool AudioGenerator::Export(const Path& destPath) const {
		return FileSystem::WriteTextFile(destPath, TextSerializer::Serialize(this, GetType()));
	}

	void AudioGeneratorControl::SetParameter(u32 index, f32 value) const {
		Audio::SetGeneratorParameter(_voice, index, value);
	}

} // namespace Shalltear
