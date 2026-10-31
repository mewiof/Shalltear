#pragma once

#include "Shalltear/Core/Asset.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include <string_view>
#include <vector>

namespace ProductLookup {

	inline Shalltear::Guid Find(const Shalltear::Path& sourceVirtualPath, std::string_view key) {
		using namespace Shalltear;
		str text = FileSystem::ReadTextFile(FileSystem::Resolve(AssetDatabase::GetMetaPath(sourceVirtualPath)), MemTag::Core);
		if (text.empty()) {
			return Guid{};
		}
		LinearAllocator arena(text.size() + 64, MemTag::Core);
		JSONValue root = JSONParse(text, arena);
		const JSONValue& products = root["products"];
		if (!products.IsArray()) {
			return Guid{};
		}
		for (size_t i = 0; i < products.Size(); ++i) {
			if (products[i]["key"].AsString() == key) {
				return Guid::FromString(products[i]["guid"].AsString());
			}
		}
		return Guid{};
	}

	inline std::vector<Shalltear::str> Keys(const Shalltear::Path& sourceVirtualPath) {
		using namespace Shalltear;
		std::vector<str> keys;
		str text = FileSystem::ReadTextFile(FileSystem::Resolve(AssetDatabase::GetMetaPath(sourceVirtualPath)), MemTag::Core);
		if (text.empty()) {
			return keys;
		}
		LinearAllocator arena(text.size() + 64, MemTag::Core);
		JSONValue root = JSONParse(text, arena);
		const JSONValue& products = root["products"];
		if (!products.IsArray()) {
			return keys;
		}
		for (size_t i = 0; i < products.Size(); ++i) {
			keys.emplace_back(products[i]["key"].AsString());
		}
		return keys;
	}

	inline Shalltear::Asset* Load(const Shalltear::Path& sourceVirtualPath, std::string_view key) {
		using namespace Shalltear;
		Guid guid = Find(sourceVirtualPath, key);
		return guid.IsValid() ? AssetDatabase::LoadAssetByGuid(guid) : nullptr;
	}

} // namespace ProductLookup
