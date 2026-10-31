#include "Shalltear/Core/UI/StyleSheetAsset.h"

#include "Shalltear/Core/FileSystem.h"
#include "Shalltear/Core/Log.h"
#include "Shalltear/Core/Memory.h"
#include "Shalltear/Core/UI/Style.h"

#include "UI/StyleValidation.h"

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
		return true;
	}

	bool StyleSheetAsset::Export(const Path& destPath) const {
		return FileSystem::WriteTextFile(destPath, source);
	}

	void StyleSheetAsset::AdoptReimport(Asset& fresh) {
		StyleSheetAsset& imported = static_cast<StyleSheetAsset&>(fresh);
		if (imported.source == source) {
			return;
		}
		std::swap(source, imported.source);
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
