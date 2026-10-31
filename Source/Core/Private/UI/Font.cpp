#include "Shalltear/Core/UI/Font.h"
#include "Shalltear/Core/UI/FontAsset.h"

#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"

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

	bool FontAsset::Cook(CookWriter& descriptor, CookWriter& payload) const {
		(void)descriptor;
		if (data.empty()) {
			return false;
		}
		payload.WriteBytes(data.data(), data.size());
		return true;
	}

	bool FontAsset::LoadCooked(CookReader& descriptor, CookReader& payload) {
		(void)descriptor;
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

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::FontAsset)
