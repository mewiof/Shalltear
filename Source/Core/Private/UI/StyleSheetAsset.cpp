#include "Shalltear/Core/UI/StyleSheetAsset.h"

#include "Shalltear/Core/Asset/AssetDatabase.h"
#include "Shalltear/Core/Asset/PakFormat.h"
#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/UI/FontAsset.h"
#include "Shalltear/Core/UI/Style.h"

#include "UI/StyleValidation.h"
#include "UI/UIInternal.h"

namespace Shalltear {

	StyleSheetAsset::~StyleSheetAsset() = default;

	namespace {

		constexpr std::string_view StarterSheet =
			"/* Shalltear style document. Selectors are simple (one tag, one .class,\n"
			"   one #id, one :pseudo + compounds of those).\n"
			"   Descendant combinators/selector lists are NOT supported.. for now */\n"
			".example {\n"
			"\tpadding: 8px;\n"
			"}\n";

		str Refuse(std::string_view what) {
			return str("<stylesheet>: ") + str(what);
		}

		void CollectFontPaths(std::string_view source, Vector<str>& out) {
			out.clear();
			UI::StyleSheet sheet;
			sheet.Parse(source);
			for (const UI::StyleRule& rule : sheet.GetRules()) {
				for (const UI::StyleProperty& property : rule.properties) {
					if (property.name != StringHash("font-family")) {
						continue;
					}
					const std::string_view name = UI::FontNameOf(property.value);
					if (name.find("://") != std::string_view::npos && std::find(out.begin(), out.end(), name) == out.end()) {
						out.emplace_back(name);
					}
				}
			}
		}

		void ReleaseFonts(Vector<StyleSheetFont>& fonts) {
			for (StyleSheetFont& entry : fonts) {
				if (FontAsset* font = entry.font.Get()) {
					AssetDatabase::UnloadAsset(font);
				}
			}
			fonts.clear();
		}

		FontAsset* AsFont(Asset* loaded) {
			if (loaded && loaded->GetAssetTypeId() == FontAsset::GetReflectType()->name) {
				return static_cast<FontAsset*>(loaded);
			}
			if (loaded) {
				AssetDatabase::UnloadAsset(loaded);
			}
			return nullptr;
		}

	} // anonymous namespace

	bool StyleSheetAsset::Parse(std::string_view text, str& error) {
		source.clear();

		UI::StyleSheet sheet;
		sheet.Parse(text);
		if (sheet.GetRules().empty()) {
			error = Refuse("parsed no rules (a style document needs at least one 'selector { ... }' block)");
			return false;
		}

		for (const UI::StyleRule& rule : sheet.GetRules()) {
			for (const UI::StyleProperty& property : rule.properties) {
				if (property.name != StringHash("box-shadow")) {
					continue;
				}
				u32 count = UI::CountShadows(property.value);
				if (count > UI::Style::MAX_BOX_SHADOWS) {
					error = Refuse(str("'box-shadow: ") + property.value + str("' lists ") + str(std::to_string(count)) +
								   str(" shadows, the cap is ") + str(std::to_string(UI::Style::MAX_BOX_SHADOWS)));
					return false;
				}
			}
		}

		source = str(text);
		return true;
	}

	bool StyleSheetAsset::Cook(CookWriter& descriptor, CookWriter& payload) const {
		(void)payload;
		descriptor.Write(static_cast<u32>(fonts.size()));
		for (const StyleSheetFont& entry : fonts) {
			const FontAsset* font = entry.font.Get();
			if (!font || !font->assetId.IsValid()) {
				SLT_LOG_ERROR("UI", "StyleSheetAsset: '{}' can't cook font '{}' (never resolved)", virtualPath, entry.src);
				return false;
			}
			descriptor.WriteAssetRef(font->assetId);
		}
		descriptor.WriteBytes(source.data(), source.size());
		return true;
	}

	bool StyleSheetAsset::LoadCooked(CookReader& descriptor, CookReader& payload) {
		(void)payload;
		u32 count = 0;
		descriptor.Read(count);
		if (descriptor.Failed() || count > descriptor.Remaining() / sizeof(Guid)) {
			return false;
		}
		Vector<Guid> guids{GetMemoryResource(MemTag::Asset)};
		for (u32 i = 0; i < count; ++i) {
			Guid guid{};
			descriptor.Read(guid);
			guids.push_back(guid);
		}
		const size_t size = descriptor.Remaining();
		const u8* bytes = size > 0 ? descriptor.ReadSpan(size) : nullptr;
		if (descriptor.Failed() || (size > 0 && !bytes)) {
			return false;
		}
		source.assign(bytes ? reinterpret_cast<const char*>(bytes) : "", size);
		Vector<str> paths{GetMemoryResource(MemTag::Asset)};
		CollectFontPaths(source, paths);
		if (count != paths.size()) {
			SLT_LOG_ERROR("UI", "StyleSheetAsset: cooked '{}' has {} font(s), its source has {}", virtualPath, count, paths.size());
			return false;
		}
		ReleaseFonts(fonts);
		for (size_t i = 0; i < paths.size(); ++i) {
			const str& path = paths[i];
			FontAsset* font = AsFont(AssetDatabase::LoadAssetByGuid(guids[i]));
			if (!font) {
				SLT_LOG_ERROR("UI", "StyleSheetAsset: cooked '{}' uses font '{}' (not in the package)", virtualPath, path);
				ReleaseFonts(fonts);
				return false;
			}
			fonts.push_back(StyleSheetFont{path, Handle<FontAsset>(font)});
		}
		return true;
	}

	void StyleSheetAsset::OnUnload() {
		ReleaseFonts(fonts);
	}

	bool StyleSheetAsset::Import(const Path& sourcePath) {
		str text = FileSystem::ReadTextFile(sourcePath, MemTag::Asset);
		if (text.empty()) {
			SLT_LOG_ERROR("UI", "StyleSheetAsset: '{}' unreadable/empty", sourcePath.ToString());
			return false;
		}
		str error;
		if (!Parse(text, error)) {
			SLT_LOG_ERROR("UI", "StyleSheetAsset: '{}' rejected -> {}", sourcePath.ToString(), error);
			return false;
		}
		Vector<str> paths{GetMemoryResource(MemTag::Asset)};
		CollectFontPaths(source, paths);
		for (const str& path : paths) {
			FontAsset* font = AsFont(AssetDatabase::LoadAsset(Path(path)));
			if (!font) {
				SLT_LOG_WARN("UI", "StyleSheetAsset: '{}' uses font '{}' (not a loadable font, skipped)", sourcePath.ToString(), path);
				continue;
			}
			fonts.push_back(StyleSheetFont{path, Handle<FontAsset>(font)});
		}
		return true;
	}

	bool StyleSheetAsset::Export(const Path& destPath) const {
		return FileSystem::WriteTextFile(destPath, source);
	}

	void StyleSheetAsset::AdoptReimport(Asset& fresh) {
		StyleSheetAsset& imported = static_cast<StyleSheetAsset&>(fresh);
		bool sameFonts = imported.fonts.size() == fonts.size();
		for (size_t i = 0; sameFonts && i < fonts.size(); ++i) {
			sameFonts = imported.fonts[i].src == fonts[i].src && imported.fonts[i].font == fonts[i].font;
		}
		if (imported.source == source && sameFonts) {
			return;
		}
		std::swap(source, imported.source);
		std::swap(fonts, imported.fonts);
		++version;
	}

	Asset* StyleSheetImporter::Import(const Path& sourcePath) {
		StyleSheetAsset* asset = New<StyleSheetAsset>(MemTag::Asset);
		if (!asset->Import(sourcePath)) {
			Delete(asset);
			return nullptr;
		}
		return asset;
	}

	Asset* StyleSheetImporter::CreateAsset() {
		StyleSheetAsset* asset = New<StyleSheetAsset>(MemTag::Asset);
		asset->source = str(StarterSheet);
		return asset;
	}

} // namespace Shalltear

SLT_REGISTER_TYPE(Shalltear::StyleSheetAsset)
