#include "Shalltear/Editor/TypeNameOps.h"

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Reflection/TypeRegistry.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include <charconv>
#include <deque>
#include <format>
#include <map>

namespace Shalltear::TypeNameOps {

	namespace {

		Path CacheDir() {
			Path resolved = FileSystem::Resolve(Path("project://Library/Editor"));
			if (resolved.ToString().find("://") == str::npos) {
				return resolved;
			}
			return Path("Library/Editor");
		}

		std::map<u64, str>& Names() {
			static std::map<u64, str> names;
			return names;
		}

	} // namespace

	void Refresh() {
		Path file = CacheDir() / "TypeNames.json";

		Names().clear();
		str text = FileSystem::ReadTextFile(file, MemTag::Editor);
		if (!text.empty()) {
			LinearAllocator arena(text.size() + 64, MemTag::Editor);
			JSONValue root = JSONParse(text, arena);
			for (size_t i = 0; i < root.MemberCount(); ++i) {
				std::string_view key = root.KeyAt(i);
				u64 hash = 0;
				auto result = std::from_chars(key.data(), key.data() + key.size(), hash, 16);
				if (result.ec != std::errc{} || result.ptr != key.data() + key.size()) {
					SLT_LOG_WARN("Editor", "type-name cache key '{}' isn't a hash. Skipping it", key);
					continue;
				}
				Names()[hash] = str(root.ChildAt(i).AsString());
			}
		}

		struct Merge {
			std::map<u64, str>* names;
			u32 learned;
		} merge{&Names(), 0};

		TypeRegistry::ForEachType(
			+[](const TypeInfo* type, void* user) {
				auto* m = static_cast<Merge*>(user);
				if (!type || !type->displayName) {
					return;
				}
				str& stored = (*m->names)[type->name.GetValue()];
				if (stored != type->displayName) {
					stored = type->displayName;
					++m->learned;
				}
			},
			&merge);

		if (merge.learned == 0) {
			return;
		}

		std::deque<str> keys;
		JSONValue root = JSONValue::MakeObject();
		for (const auto& [hash, name] : Names()) {
			keys.push_back(std::format("{:016x}", hash));
			root.AddMember(keys.back(), JSONValue::MakeString(name));
		}

		FileSystem::CreateDirectory(CacheDir());
		if (!FileSystem::WriteTextFile(file, JSONSerialize(root))) {
			SLT_LOG_WARN("Editor", "couldn't write the type-name cache to {}", file.ToString());
		}
	}

	str Lookup(StringHash typeName) {
		auto it = Names().find(typeName.GetValue());
		return it != Names().end() ? it->second : str{};
	}

	str MissingTypeLabel(StringHash typeName) {
		str name = Lookup(typeName);
		return name.empty() ? std::format("{:#018x}", typeName.GetValue()) : name;
	}

} // namespace Shalltear::TypeNameOps
