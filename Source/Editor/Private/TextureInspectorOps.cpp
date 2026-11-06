#include "Shalltear/Editor/TextureInspectorOps.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"

namespace Shalltear::TextureInspectorOps {

	TextureAsset::Settings ReadImportSettings(const Path& texturePath) {
		TextureAsset::Settings settings;
		const str text = FileSystem::ReadTextFile(FileSystem::Resolve(AssetDatabase::GetMetaPath(texturePath)), MemTag::Editor);
		if (!text.empty()) {
			LinearAllocator arena(text.size() + 64, MemTag::Editor);
			const JSONValue root = JSONParse(text, arena);
			TextureAssetImporter::ReadSettings(root["importer"]["settings"], settings);
		}
		return settings;
	}

	bool ApplyImportSettings(const Path& texturePath, const TextureAsset::Settings& settings) {
		const str text = FileSystem::ReadTextFile(FileSystem::Resolve(AssetDatabase::GetMetaPath(texturePath)), MemTag::Editor);
		LinearAllocator arena(text.size() + 64, MemTag::Editor);
		const JSONValue existing = text.empty() ? JSONValue() : JSONParse(text, arena);
		JSONValue merged = JSONValue::MakeObject();
		TextureAssetImporter::WriteSettings(settings, merged);
		const JSONValue& previous = existing["importer"]["settings"];
		for (size_t i = 0; i < previous.MemberCount(); ++i) {
			if (!merged.HasKey(previous.KeyAt(i))) {
				merged.AddMember(previous.KeyAt(i), previous.ChildAt(i));
			}
		}
		if (!AssetDatabase::WriteImporterSettings(texturePath, merged)) {
			return false;
		}
		if (!AssetDatabase::ReimportAsset(texturePath)) {
			SLT_LOG_WARN("Editor", "'{}': settings saved, the texture couldn't be re-imported. Close what uses it and re-import", texturePath.ToString());
			return false;
		}
		return true;
	}

} // namespace Shalltear::TextureInspectorOps
