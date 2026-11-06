#include "Shalltear/Core/UI/Font.h"
#include "Shalltear/Core/UI/FontAsset.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/Serialization/JSON.h"

#include <algorithm>
#include <cmath>

namespace Shalltear::UI {

	CaretStop ShapedRun::StopForByte(u32 byteOffset) const {
		if (caretStops.empty()) {
			return {};
		}
		for (size_t index = 0; index < caretStops.size(); ++index) {
			if (caretStops[index].byteOffset >= byteOffset) {
				size_t last = index;
				while (last + 1 < caretStops.size() && caretStops[last + 1].byteOffset == caretStops[index].byteOffset) {
					++last;
				}
				return caretStops[last];
			}
		}
		return caretStops.back();
	}

	f32 ShapedRun::CaretXForByte(u32 byteOffset) const {
		return StopForByte(byteOffset).x;
	}

	u32 ShapedRun::ByteForCaret(f32 x, u32 line) const {
		if (caretStops.empty()) {
			return 0;
		}
		const u32 wanted = std::min(line, caretStops.back().line);
		u32 bestByte = caretStops.front().byteOffset;
		f32 bestDistance = INFINITY;
		for (size_t index = 0; index < caretStops.size(); ++index) {
			const CaretStop& stop = caretStops[index];
			if (stop.line != wanted) {
				continue;
			}
			if (index + 1 < caretStops.size() && caretStops[index + 1].line != stop.line &&
				caretStops[index + 1].byteOffset == stop.byteOffset) {
				continue;
			}
			const f32 distance = std::abs(stop.x - x);
			if (distance < bestDistance) {
				bestDistance = distance;
				bestByte = stop.byteOffset;
			}
		}
		return bestByte;
	}

	u32 ShapedRun::LineCount() const {
		return caretStops.empty() ? 1u : caretStops.back().line + 1u;
	}

} // namespace Shalltear::UI

namespace Shalltear {

	FontAsset::~FontAsset() = default;

	bool FontAsset::Import(const Path& sourcePath) {
		Buffer file = FileSystem::ReadFile(sourcePath, MemTag::Asset);
		if (!file || file.size == 0) {
			SLT_LOG_ERROR("UI", "FontAsset: '{}' unreadable", sourcePath.ToString());
			return false;
		}
		data.assign(file.data, file.data + file.size);
		return true;
	}

	FontAsset* FontAsset::Hold(const Vector<Guid>& list, Vector<Held>& held, size_t index, std::string_view role) const {
		if (index >= list.size()) {
			return nullptr;
		}
		if (held.size() != list.size()) {
			held.resize(list.size());
		}
		Held& entry = held[index];
		if (FontAsset* live = entry.font.Get()) {
			return live;
		}
		entry.holds = false;
		if (entry.refused || !list[index].IsValid()) {
			return nullptr;
		}
		Asset* resolved = AssetDatabase::LoadAssetByGuid(list[index]);
		if (!resolved || resolved->GetAssetTypeId() != FontAsset::GetReflectType()->name) {
			if (resolved) {
				AssetDatabase::UnloadAsset(resolved);
			}
			entry.refused = true;
			SLT_LOG_WARN("UI", "font '{}' has {} {} (not a loadable font, skipped)", virtualPath, role, list[index].ToString());
			return nullptr;
		}
		auto* loaded = static_cast<FontAsset*>(resolved);
		entry.font = Handle<FontAsset>(loaded);
		entry.holds = true;
		return loaded;
	}

	FontAsset* FontAsset::GetFace(size_t index) const {
		return Hold(faces, _heldFaces, index, "face");
	}

	FontAsset* FontAsset::GetFallback(size_t index) const {
		return Hold(fallbacks, _held, index, "fallback");
	}

	void FontAsset::OnUnload() {
		for (Vector<Held>* list : {&_heldFaces, &_held}) {
			for (Held& held : *list) {
				FontAsset* font = held.font.Get();
				if (font && held.holds) {
					AssetDatabase::UnloadAsset(font);
				}
			}
			list->clear();
		}
	}

	void FontAsset::AdoptReimport(Asset& fresh) {
		FontAsset& imported = static_cast<FontAsset&>(fresh);
		if (imported.data == data && imported.faces == faces && imported.fallbacks == fallbacks) {
			return;
		}
		OnUnload();
		data = std::move(imported.data);
		faces = std::move(imported.faces);
		fallbacks = std::move(imported.fallbacks);
		++version;
	}

	namespace {

		void CookList(CookWriter& descriptor, const Vector<Guid>& list) {
			descriptor.Write(static_cast<u32>(list.size()));
			for (const Guid& guid : list) {
				descriptor.WriteAssetRef(guid);
			}
		}

		bool LoadCookedList(CookReader& descriptor, Vector<Guid>& out) {
			u32 count = 0;
			if (!descriptor.Read(count) || count > descriptor.Remaining() / sizeof(Guid)) {
				return false;
			}
			out.clear();
			for (u32 i = 0; i < count; ++i) {
				Guid guid;
				if (!descriptor.Read(guid)) {
					return false;
				}
				out.push_back(guid);
			}
			return true;
		}

		void ReadIdList(const JSONValue& list, std::string_view key, const Path& virtualPath, Vector<Guid>& out) {
			for (size_t i = 0; list.IsArray() && i < list.Size(); ++i) {
				const Guid guid = Guid::FromString(list[i].AsString());
				if (!guid.IsValid()) {
					SLT_LOG_WARN("UI", "'{}': {} {} isn't an asset id (skipped)", virtualPath.ToString(), key, i);
					continue;
				}
				if (std::find(out.begin(), out.end(), guid) == out.end()) {
					out.push_back(guid);
				}
			}
		}

	} // anonymous namespace

	bool FontAsset::Cook(CookWriter& descriptor, CookWriter& payload) const {
		if (data.empty()) {
			return false;
		}
		CookList(descriptor, faces);
		CookList(descriptor, fallbacks);
		payload.WriteBytes(data.data(), data.size());
		return true;
	}

	bool FontAsset::LoadCooked(CookReader& descriptor, CookReader& payload) {
		return LoadCookedList(descriptor, faces) && LoadCookedList(descriptor, fallbacks) &&
			   payload.ReadInto(data, payload.Remaining()) && !data.empty();
	}

	Asset* FontAssetImporter::Import(const Path& sourcePath) {
		FontAsset* asset = New<FontAsset>(MemTag::Asset);
		if (!asset->Import(sourcePath)) {
			Delete(asset);
			return nullptr;
		}
		return asset;
	}

	Asset* FontAssetImporter::ImportWithSettings(const Path& sourcePath, const Path& virtualPath, const JSONValue& settings) {
		FontAsset* asset = static_cast<FontAsset*>(Import(sourcePath));
		if (!asset) {
			return nullptr;
		}
		ReadIdList(settings["faces"], "face", virtualPath, asset->faces);
		ReadIdList(settings["fallbacks"], "fallback", virtualPath, asset->fallbacks);
		return asset;
	}

	void FontAssetImporter::SerializeSettings(const Path& sourcePath, JSONValue& out) const {
		(void)sourcePath;
		out.AddMember("faces", JSONValue::MakeArray());
		out.AddMember("fallbacks", JSONValue::MakeArray());
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::FontAsset)
