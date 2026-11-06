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

	bool Apply(const Path& fontPath, std::span<const Guid> faces, std::span<const Guid> fallbacks) {
		const Guid self = AssetDatabase::LookupGuid(fontPath);
		std::deque<str> pool;
		auto idList = [&](std::span<const Guid> ids) {
			Vector<Guid> kept{GetMemoryResource(MemTag::Editor)};
			JSONValue list = JSONValue::MakeArray();
			for (const Guid& id : ids) {
				if (id.IsValid() && id != self && std::find(kept.begin(), kept.end(), id) == kept.end()) {
					kept.push_back(id);
					list.Push(JSONValue::MakeString(pool.emplace_back(id.ToString())));
				}
			}
			return list;
		};

		const str text = FileSystem::ReadTextFile(FileSystem::Resolve(AssetDatabase::GetMetaPath(fontPath)), MemTag::Editor);
		LinearAllocator arena(text.size() + 64, MemTag::Editor);
		const JSONValue existing = text.empty() ? JSONValue() : JSONParse(text, arena);

		JSONValue rebuilt = JSONValue::MakeObject();
		const JSONValue& importer = existing["importer"];
		const JSONValue* previous = importer.IsObject() ? importer.Find("settings") : nullptr;
		if (previous && previous->IsObject()) {
			for (size_t i = 0; i < previous->MemberCount(); ++i) {
				if (previous->KeyAt(i) != "faces" && previous->KeyAt(i) != "fallbacks") {
					rebuilt.AddMember(previous->KeyAt(i), previous->ChildAt(i));
				}
			}
		}
		rebuilt.AddMember("faces", idList(faces));
		rebuilt.AddMember("fallbacks", idList(fallbacks));

		if (!AssetDatabase::WriteImporterSettings(fontPath, rebuilt)) {
			return false;
		}
		if (!EditorAssetOps::Reimport(fontPath)) {
			SLT_LOG_WARN("Editor", "'{}': faces + fallbacks saved, the font couldn't be re-imported. Close what uses it and re-import",
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
