#include "Shalltear/Editor/FontInspectorOps.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"
#include "Shalltear/Core/UI/FontAsset.h"

#include "Shalltear/Editor/EditorAssetOps.h"

#include <algorithm>
#include <deque>

namespace Shalltear::FontInspectorOps {

	bool ApplyFallbacks(const Path& fontPath, std::span<const Guid> fallbacks) {
		const Guid self = AssetDatabase::LookupGuid(fontPath);
		Vector<Guid> kept{GetMemoryResource(MemTag::Editor)};
		for (const Guid& fallback : fallbacks) {
			if (fallback.IsValid() && fallback != self && std::find(kept.begin(), kept.end(), fallback) == kept.end()) {
				kept.push_back(fallback);
			}
		}

		const str text = FileSystem::ReadTextFile(FileSystem::Resolve(AssetDatabase::GetMetaPath(fontPath)), MemTag::Editor);
		LinearAllocator arena(text.size() + 64, MemTag::Editor);
		const JSONValue existing = text.empty() ? JSONValue() : JSONParse(text, arena);

		std::deque<str> pool;
		JSONValue list = JSONValue::MakeArray();
		for (const Guid& fallback : kept) {
			list.Push(JSONValue::MakeString(pool.emplace_back(fallback.ToString())));
		}
		JSONValue rebuilt = JSONValue::MakeObject();
		const JSONValue& importer = existing["importer"];
		const JSONValue* previous = importer.IsObject() ? importer.Find("settings") : nullptr;
		if (previous && previous->IsObject()) {
			for (size_t i = 0; i < previous->MemberCount(); ++i) {
				if (previous->KeyAt(i) != "fallbacks") {
					rebuilt.AddMember(previous->KeyAt(i), previous->ChildAt(i));
				}
			}
		}
		rebuilt.AddMember("fallbacks", std::move(list));

		if (!AssetDatabase::WriteImporterSettings(fontPath, rebuilt)) {
			return false;
		}
		if (!EditorAssetOps::Reimport(fontPath)) {
			SLT_LOG_WARN("Editor", "'{}': fallbacks saved, the font couldn't be re-imported. Close what uses it and re-import",
						 fontPath.ToString());
			return false;
		}
		return true;
	}

	void CollectCandidates(const Guid& font, std::span<const Guid> listed, Vector<AssetBrowserOps::AssignableAsset>& out) {
		AssetBrowserOps::CollectAssignableAssets(FontAsset::GetReflectType()->name, out);
		std::erase_if(out, [&](const AssetBrowserOps::AssignableAsset& candidate) {
			return candidate.guid == font || std::find(listed.begin(), listed.end(), candidate.guid) != listed.end();
		});
	}

} // namespace Shalltear::FontInspectorOps
