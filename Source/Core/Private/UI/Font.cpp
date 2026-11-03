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

	FontAsset* FontAsset::GetFallback(size_t index) const {
		if (index >= fallbacks.size()) {
			return nullptr;
		}
		if (_held.size() != fallbacks.size()) {
			_held.resize(fallbacks.size());
		}
		Held& held = _held[index];
		if (FontAsset* live = held.font.Get()) {
			return live;
		}
		held.holds = false;
		if (held.refused || !fallbacks[index].IsValid()) {
			return nullptr;
		}
		Asset* resolved = AssetDatabase::LoadAssetByGuid(fallbacks[index]);
		if (!resolved || resolved->GetAssetTypeId() != FontAsset::GetReflectType()->name) {
			if (resolved) {
				AssetDatabase::UnloadAsset(resolved);
			}
			held.refused = true;
			SLT_LOG_WARN("UI", "font '{}' has fallback {} (not a loadable font, skipped)", virtualPath, fallbacks[index].ToString());
			return nullptr;
		}
		auto* loaded = static_cast<FontAsset*>(resolved);
		held.font = Handle<FontAsset>(loaded);
		held.holds = true;
		return loaded;
	}

	void FontAsset::OnUnload() {
		for (Held& held : _held) {
			FontAsset* font = held.font.Get();
			if (font && held.holds) {
				AssetDatabase::UnloadAsset(font);
			}
		}
		_held.clear();
	}

	void FontAsset::AdoptReimport(Asset& fresh) {
		FontAsset& imported = static_cast<FontAsset&>(fresh);
		if (imported.data == data && imported.fallbacks == fallbacks) {
			return;
		}
		OnUnload();
		data = std::move(imported.data);
		fallbacks = std::move(imported.fallbacks);
		++version;
	}

	bool FontAsset::Cook(CookWriter& descriptor, CookWriter& payload) const {
		if (data.empty()) {
			return false;
		}
		descriptor.Write(static_cast<u32>(fallbacks.size()));
		for (const Guid& fallback : fallbacks) {
			descriptor.WriteAssetRef(fallback);
		}
		payload.WriteBytes(data.data(), data.size());
		return true;
	}

	bool FontAsset::LoadCooked(CookReader& descriptor, CookReader& payload) {
		u32 count = 0;
		if (!descriptor.Read(count) || count > descriptor.Remaining() / sizeof(Guid)) {
			return false;
		}
		fallbacks.clear();
		for (u32 i = 0; i < count; ++i) {
			Guid fallback;
			if (!descriptor.Read(fallback)) {
				return false;
			}
			fallbacks.push_back(fallback);
		}
		return payload.ReadInto(data, payload.Remaining()) && !data.empty();
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
		const JSONValue& list = settings["fallbacks"];
		for (size_t i = 0; list.IsArray() && i < list.Size(); ++i) {
			const Guid fallback = Guid::FromString(list[i].AsString());
			if (!fallback.IsValid()) {
				SLT_LOG_WARN("UI", "'{}': fallback {} isn't an asset id (skipped)", virtualPath.ToString(), i);
				continue;
			}
			if (std::find(asset->fallbacks.begin(), asset->fallbacks.end(), fallback) == asset->fallbacks.end()) {
				asset->fallbacks.push_back(fallback);
			}
		}
		return asset;
	}

	void FontAssetImporter::SerializeSettings(const Path& sourcePath, JSONValue& out) const {
		(void)sourcePath;
		out.AddMember("fallbacks", JSONValue::MakeArray());
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::FontAsset)
